#include "VstHostManager.h"
#include <cmath>
#include <thread>

namespace Harmonizer
{
namespace
{
constexpr int maxSupportedPluginChannels = 256;
constexpr int maxSupportedBlockSize = 65536;
}

PluginEditorWindow::PluginEditorWindow(juce::AudioPluginInstance* plugin, const juce::String& pluginName, std::function<void()> onClose)
    : juce::DocumentWindow(pluginName + " (VST3 Editor)",
                           juce::Colour(0xff25272c),
                           juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      activePlugin(plugin),
      onCloseCallback(std::move(onClose))
{
    setUsingNativeTitleBar(true);

    if (activePlugin != nullptr)
    {
        if (auto* editor = activePlugin->createEditorIfNeeded())
        {
            setContentOwned(editor, true);
            setResizable(editor->isResizable(), false);
        }
    }

    centreWithSize(getWidth() > 100 ? getWidth() : 800, getHeight() > 100 ? getHeight() : 600);
    setVisible(true);
}

PluginEditorWindow::~PluginEditorWindow()
{
    clearContentComponent();
}

void PluginEditorWindow::closeButtonPressed()
{
    setVisible(false);
    clearContentComponent();
    if (onCloseCallback)
        onCloseCallback();
}

#if JUCE_WINDOWS
#include <windows.h>

static bool safeScanNextFile(juce::PluginDirectoryScanner* scanner, juce::String* outPluginBeingScanned)
{
    __try
    {
        return scanner->scanNextFile(true, *outPluginBeingScanned);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
#else
static bool safeScanNextFile(juce::PluginDirectoryScanner* scanner, juce::String* outPluginBeingScanned)
{
    try
    {
        return scanner->scanNextFile(true, *outPluginBeingScanned);
    }
    catch (...)
    {
        return false;
    }
}
#endif

//==============================================================================
VstHostManager::VstHostManager()
{
    juce::addDefaultFormatsToManager (formatManager);
    loadConfig();
}

VstHostManager::~VstHostManager()
{
    shouldCancelScan.store(true);
    if (scanThread.joinable())
        scanThread.join();
    cancelPendingUpdate();
    closePluginEditor();
    unloadAllPlugins();
}

void VstHostManager::requestClosePluginEditor()
{
    closeEditorRequested = true;
    triggerAsyncUpdate();
}

void VstHostManager::handleAsyncUpdate()
{
    if (closeEditorRequested)
    {
        closeEditorRequested = false;
        closePluginEditor();
    }

    if (scanResultReady.exchange(false))
    {
        juce::Array<juce::PluginDescription> results;
        {
            const juce::ScopedLock sl(scanResultLock);
            results.swapWith(pendingScanResults);
        }
        for (const auto& description : results)
            knownPluginList.addType(description);
        {
            const juce::ScopedLock sl(scanStatusLock);
            currentScanningPlugin.clear();
        }
        isScanningFlag.store(false);
        saveConfig();
        listeners.call([](Listener& l) { l.pluginListChanged(); });
    }
}

juce::String VstHostManager::getScanStatusText() const
{
    const juce::ScopedLock sl(scanStatusLock);
    return currentScanningPlugin;
}

juce::StringArray VstHostManager::getDefaultSearchPaths()
{
    juce::StringArray paths;
#if JUCE_WINDOWS
    // STRICTLY scan standard 64-bit VST3 paths. DO NOT scan VST2 or arbitrary dll folders!
    paths.add("C:\\Program Files\\Common Files\\VST3");

    auto localVst3 = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getParentDirectory().getChildFile("Local\\Programs\\Common\\VST3");
    if (localVst3.isDirectory())
        paths.add(localVst3.getFullPathName());
#elif JUCE_MAC
    paths.add("/Library/Audio/Plug-Ins/VST3");
    paths.add("~/Library/Audio/Plug-Ins/VST3");
#endif
    return paths;
}

void VstHostManager::addCustomSearchPath(const juce::String& path)
{
    if (path.isNotEmpty() && !customSearchPaths.contains(path))
    {
        customSearchPaths.add(path);
        saveConfig();
    }
}

void VstHostManager::removeCustomSearchPath(int index)
{
    if (index >= 0 && index < customSearchPaths.size())
    {
        customSearchPaths.remove(index);
        saveConfig();
    }
}

void VstHostManager::resetCustomSearchPaths()
{
    customSearchPaths.clear();
    saveConfig();
}

void VstHostManager::saveConfig()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("HarmonizerStudio");
    dir.createDirectory();

    // 1. Save search paths
    auto file = dir.getChildFile("vst_paths.xml");
    juce::XmlElement root("VstPaths");
    for (const auto& p : customSearchPaths)
    {
        auto* elem = root.createNewChildElement("Path");
        elem->setAttribute("dir", p);
    }
    juce::TemporaryFile pathsTemporary(file);
    if (root.writeTo(pathsTemporary.getFile()))
        pathsTemporary.overwriteTargetFileWithTemporary();

    // 2. Cache known plugin list to avoid re-scanning
    auto pluginFile = dir.getChildFile("vst_plugins.xml");
    if (auto pluginXml = knownPluginList.createXml())
    {
        juce::TemporaryFile pluginsTemporary(pluginFile);
        if (pluginXml->writeTo(pluginsTemporary.getFile()))
            pluginsTemporary.overwriteTargetFileWithTemporary();
    }
}

void VstHostManager::loadConfig()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("HarmonizerStudio");
    auto file = dir.getChildFile("vst_paths.xml");
    if (file.existsAsFile())
    {
        if (auto xml = juce::parseXML(file))
        {
            customSearchPaths.clear();
            for (auto* elem : xml->getChildIterator())
            {
                if (elem->hasTagName("Path"))
                {
                    auto p = elem->getStringAttribute("dir");
                    if (p.isNotEmpty() && !customSearchPaths.contains(p))
                        customSearchPaths.add(p);
                }
            }
        }
    }

    auto pluginFile = dir.getChildFile("vst_plugins.xml");
    if (pluginFile.existsAsFile())
    {
        if (auto pluginXml = juce::parseXML(pluginFile))
        {
            knownPluginList.recreateFromXml(*pluginXml);
            listeners.call([](Listener& l) { l.pluginListChanged(); });
        }
    }
}

void VstHostManager::scanPlugins(const juce::StringArray& extraPaths)
{
    if (isScanningFlag.load())
        return;

    isScanningFlag.store(true);

    auto deadMansPedal = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                             .getChildFile("HarmonizerStudio").getChildFile("vst_pedal.txt");
    deadMansPedal.getParentDirectory().createDirectory();

    shouldCancelScan.store(false);
    if (scanThread.joinable())
        scanThread.join();

    auto paths = customSearchPaths;
    auto knownXml = knownPluginList.createXml();
    scanThread = std::thread([this, extraPaths, deadMansPedal, paths, knownXml = std::move(knownXml)]() mutable {
        juce::KnownPluginList scannedPlugins;
        if (knownXml != nullptr)
            scannedPlugins.recreateFromXml(*knownXml);
        juce::AudioPluginFormatManager scanFormats;
        juce::addDefaultFormatsToManager(scanFormats);
        juce::FileSearchPath searchPath;
        for (const auto& p : getDefaultSearchPaths())
        {
            juce::File f(p);
            if (f.isDirectory())
                searchPath.add(f);
        }
        for (const auto& p : paths)
        {
            juce::File f(p);
            if (f.isDirectory())
                searchPath.add(f);
        }
        for (const auto& p : extraPaths)
        {
            juce::File f(p);
            if (f.isDirectory())
                searchPath.add(f);
        }

        for (int i = 0; i < scanFormats.getNumFormats(); ++i)
        {
            if (shouldCancelScan.load()) break;
            auto* format = scanFormats.getFormat(i);
            if (format != nullptr)
            {
                juce::PluginDirectoryScanner scanner(scannedPlugins, *format, searchPath, true, deadMansPedal);
                juce::String pluginBeingScanned;
                while (!shouldCancelScan.load() && safeScanNextFile(&scanner, &pluginBeingScanned))
                {
                    const juce::ScopedLock sl(scanStatusLock);
                    currentScanningPlugin = pluginBeingScanned;
                }
            }
        }

        if (deadMansPedal.existsAsFile())
            deadMansPedal.deleteFile();

        if (!shouldCancelScan.load())
        {
            {
                const juce::ScopedLock sl(scanResultLock);
                pendingScanResults = scannedPlugins.getTypes();
            }
            scanResultReady.store(true);
            triggerAsyncUpdate();
        }
    });
}

bool VstHostManager::scanSinglePluginFile(const juce::File& file)
{
    if (!file.exists()) return false;

    bool anyFound = false;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat(i);
        if (format != nullptr && format->fileMightContainThisPluginType(file.getFullPathName()))
        {
            juce::OwnedArray<juce::PluginDescription> descriptions;
            format->findAllTypesForFile(descriptions, file.getFullPathName());
            for (auto* desc : descriptions)
            {
                if (desc != nullptr)
                {
                    knownPluginList.addType(*desc);
                    anyFound = true;
                }
            }
        }
    }

    if (anyFound)
    {
        saveConfig();
        listeners.call([](Listener& l) { l.pluginListChanged(); });
    }
    return anyFound;
}

bool VstHostManager::loadPlugin(int trackId, const juce::PluginDescription& desc, double sampleRate, int samplesPerBlock)
{
    if (trackId <= 0)
        return false;

    juce::String errorMessage;
    auto instance = formatManager.createPluginInstance(desc, sampleRate, samplesPerBlock, errorMessage);

    if (instance == nullptr)
    {
        juce::Logger::writeToLog("Failed to load plugin: " + errorMessage);
        return false;
    }

    // AudioProcessor::processBlock requires a buffer wide enough for every active
    // input/output channel, even when the audio device itself is only stereo.
    const int requiredChannels = (std::max)({ 1, instance->getTotalNumInputChannels(),
                                               instance->getTotalNumOutputChannels() });
    if (requiredChannels > maxSupportedPluginChannels || samplesPerBlock <= 0
        || samplesPerBlock > maxSupportedBlockSize || !std::isfinite(sampleRate)
        || sampleRate <= 0.0)
    {
        juce::Logger::writeToLog("Unsupported plugin channel count or playback configuration: " + desc.name);
        return false;
    }

    auto replacement = std::make_unique<PluginSlot>();
    replacement->identifier = desc.createIdentifierString();
    replacement->processBuffer.setSize(requiredChannels, (std::max)(2048, samplesPerBlock));
    instance->prepareToPlay(sampleRate, samplesPerBlock);
    replacement->instance = std::move(instance);

    if (editorTrackId == trackId)
        closePluginEditor();
    std::unique_ptr<PluginSlot> oldSlot;
    {
        const juce::SpinLock::ScopedLockType sl(pluginLock);
        auto& slot = plugins[trackId];
        oldSlot = std::move(slot);
        slot = std::move(replacement);
        currentSampleRate = sampleRate;
        currentBlockSize = samplesPerBlock;
    }
    if (oldSlot != nullptr)
        oldSlot->instance->releaseResources();
    {
        const juce::ScopedLock sl(midiLock);
        pendingLiveMidi.erase(trackId);
    }

    const auto loadedName = getLoadedPluginName(trackId);
    listeners.call([&](Listener& l) { l.loadedPluginChanged(loadedName); });
    return true;
}

bool VstHostManager::loadPlugin(int trackId, const juce::PluginDescription& desc)
{
    double sampleRate;
    int samplesPerBlock;
    {
        const juce::SpinLock::ScopedLockType sl(pluginLock);
        sampleRate = currentSampleRate;
        samplesPerBlock = currentBlockSize;
    }
    return loadPlugin(trackId, desc, sampleRate, samplesPerBlock);
}

bool VstHostManager::loadPluginByName(int trackId, const juce::String& name)
{
    if (name.isEmpty()) return false;
    for (const auto& desc : knownPluginList.getTypes())
    {
        if (desc.name.equalsIgnoreCase(name) || desc.descriptiveName.equalsIgnoreCase(name) || desc.fileOrIdentifier.equalsIgnoreCase(name))
        {
            return loadPlugin(trackId, desc);
        }
    }
    return false;
}

juce::MemoryBlock VstHostManager::getPluginState(int trackId) const
{
    juce::MemoryBlock mb;
    const juce::SpinLock::ScopedLockType sl(pluginLock);
    if (auto it = plugins.find(trackId); it != plugins.end())
        it->second->instance->getStateInformation(mb);
    return mb;
}

bool VstHostManager::setPluginState(int trackId, const void* data, int sizeInBytes)
{
    const juce::SpinLock::ScopedLockType sl(pluginLock);
    if (auto it = plugins.find(trackId); it != plugins.end() && data != nullptr && sizeInBytes > 0)
    {
        it->second->instance->setStateInformation(data, sizeInBytes);
        return true;
    }
    return false;
}

void VstHostManager::unloadPlugin(int trackId)
{
    if (editorTrackId == trackId)
        closePluginEditor();
    std::unique_ptr<PluginSlot> oldSlot;
    {
        const juce::SpinLock::ScopedLockType sl(pluginLock);
        if (auto it = plugins.find(trackId); it != plugins.end())
        {
            oldSlot = std::move(it->second);
            plugins.erase(it);
        }
    }
    {
        const juce::ScopedLock sl(midiLock);
        pendingLiveMidi.erase(trackId);
    }
    if (oldSlot != nullptr)
    {
        oldSlot->instance->releaseResources();
        oldSlot.reset();
        listeners.call([](Listener& l) { l.loadedPluginChanged(""); });
    }
}

void VstHostManager::unloadAllPlugins()
{
    closePluginEditor();
    std::map<int, std::unique_ptr<PluginSlot>> oldSlots;
    {
        const juce::SpinLock::ScopedLockType sl(pluginLock);
        oldSlots.swap(plugins);
    }
    {
        const juce::ScopedLock sl(midiLock);
        pendingLiveMidi.clear();
    }
    for (auto& [trackId, slot] : oldSlots)
        slot->instance->releaseResources();
    if (!oldSlots.empty())
        listeners.call([](Listener& l) { l.loadedPluginChanged(""); });
}

std::vector<int> VstHostManager::getLoadedTrackIds() const
{
    std::vector<int> ids;
    const juce::SpinLock::ScopedLockType sl(pluginLock);
    ids.reserve(plugins.size());
    for (const auto& [trackId, slot] : plugins)
        ids.push_back(trackId);
    return ids;
}

bool VstHostManager::hasLoadedPlugin(int trackId) const
{
    const juce::SpinLock::ScopedLockType sl(pluginLock);
    return plugins.find(trackId) != plugins.end();
}

juce::String VstHostManager::getLoadedPluginName(int trackId) const
{
    const juce::SpinLock::ScopedLockType sl(pluginLock);
    if (auto it = plugins.find(trackId); it != plugins.end())
        return it->second->instance->getName();
    return "";
}

juce::String VstHostManager::getLoadedPluginIdentifier(int trackId) const
{
    const juce::SpinLock::ScopedLockType sl(pluginLock);
    if (auto it = plugins.find(trackId); it != plugins.end())
        return it->second->identifier;
    return {};
}

void VstHostManager::openPluginEditor(int trackId)
{
    juce::AudioPluginInstance* instance = nullptr;
    {
        const juce::SpinLock::ScopedLockType sl(pluginLock);
        if (auto it = plugins.find(trackId); it != plugins.end())
            instance = it->second->instance.get();
    }
    if (instance == nullptr) return;

    if (editorWindow == nullptr || editorTrackId != trackId)
    {
        closePluginEditor();
        editorTrackId = trackId;
        editorWindow = std::make_unique<PluginEditorWindow>(instance, instance->getName(), [this]() {
            requestClosePluginEditor();
        });
    }
    else
    {
        editorWindow->toFront(true);
    }
}

void VstHostManager::closePluginEditor()
{
    if (editorWindow != nullptr)
    {
        editorWindow.reset();
    }
    editorTrackId = -1;
}

void VstHostManager::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    const juce::SpinLock::ScopedLockType sl(pluginLock);
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    for (auto& [trackId, slot] : plugins)
    {
        auto& instance = *slot->instance;
        const int requiredChannels = (std::max)({ 1, instance.getTotalNumInputChannels(),
                                                   instance.getTotalNumOutputChannels() });
        if (requiredChannels <= maxSupportedPluginChannels && samplesPerBlock > 0
            && samplesPerBlock <= maxSupportedBlockSize)
            slot->processBuffer.setSize(requiredChannels, (std::max)(2048, samplesPerBlock));
        instance.prepareToPlay(sampleRate, samplesPerBlock);
    }
}

void VstHostManager::processBlock(int trackId, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    const juce::SpinLock::ScopedTryLockType stl(pluginLock);
    if (!stl.isLocked())
        return;
    auto it = plugins.find(trackId);
    if (it == plugins.end())
        return;
    auto& slot = *it->second;
    auto& instance = *slot.instance;

    const int requiredChannels = (std::max)({ 1, instance.getTotalNumInputChannels(),
                                               instance.getTotalNumOutputChannels() });
    if (requiredChannels > maxSupportedPluginChannels || buffer.getNumSamples() > maxSupportedBlockSize)
    {
        buffer.clear();
        return;
    }

    juce::MidiBuffer combinedMidi;
    combinedMidi.addEvents(midiMessages, 0, buffer.getNumSamples(), 0);

    {
        const juce::ScopedLock sl(midiLock);
        if (auto pending = pendingLiveMidi.find(trackId); pending != pendingLiveMidi.end())
        {
            combinedMidi.addEvents(pending->second, 0, buffer.getNumSamples(), 0);
            pending->second.clear();
        }
    }

    if (buffer.getNumChannels() >= requiredChannels)
    {
        instance.processBlock(buffer, combinedMidi);
        return;
    }

    // Keep the device's channel layout unchanged. The wider scratch buffer is
    // prepared when the plugin or audio device starts; resize only if a driver
    // unexpectedly supplies a larger block than it advertised.
    if (slot.processBuffer.getNumChannels() < requiredChannels
        || slot.processBuffer.getNumSamples() < buffer.getNumSamples())
        slot.processBuffer.setSize(requiredChannels,
                                    (std::max)(buffer.getNumSamples(), slot.processBuffer.getNumSamples()));

    juce::AudioBuffer<float> pluginView(slot.processBuffer.getArrayOfWritePointers(),
                                        requiredChannels, buffer.getNumSamples());
    pluginView.clear();
    const int inputChannels = (std::min)(buffer.getNumChannels(), instance.getTotalNumInputChannels());
    for (int channel = 0; channel < inputChannels; ++channel)
        pluginView.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());

    instance.processBlock(pluginView, combinedMidi);

    const int outputChannels = instance.getTotalNumOutputChannels();
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        if (outputChannels > 0)
            buffer.copyFrom(channel, 0, pluginView, (std::min)(channel, outputChannels - 1),
                            0, buffer.getNumSamples());
        else
            buffer.clear(channel, 0, buffer.getNumSamples());
    }
}

void VstHostManager::releaseResources()
{
    const juce::SpinLock::ScopedLockType sl(pluginLock);
    for (auto& [trackId, slot] : plugins)
        slot->instance->releaseResources();
}

void VstHostManager::sendMidiNoteOn(int trackId, int channel, int noteNumber, float velocity)
{
    const juce::ScopedLock sl(midiLock);
    pendingLiveMidi[trackId].addEvent(juce::MidiMessage::noteOn(channel, noteNumber, velocity), 0);
}

void VstHostManager::sendMidiNoteOff(int trackId, int channel, int noteNumber)
{
    const juce::ScopedLock sl(midiLock);
    pendingLiveMidi[trackId].addEvent(juce::MidiMessage::noteOff(channel, noteNumber), 0);
}

void VstHostManager::allNotesOff()
{
    const auto trackIds = getLoadedTrackIds();
    const juce::ScopedLock sl(midiLock);
    pendingLiveMidi.clear();
    for (int trackId : trackIds)
    {
        auto& pending = pendingLiveMidi[trackId];
        for (int ch = 1; ch <= 16; ++ch)
        {
            pending.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
            pending.addEvent(juce::MidiMessage::allControllersOff(ch), 0);
            pending.addEvent(juce::MidiMessage::allSoundOff(ch), 0);
        }
    }
}

} // namespace Harmonizer
