#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <map>
#include <vector>

namespace Harmonizer
{

class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow(juce::AudioPluginInstance* plugin, const juce::String& pluginName, std::function<void()> onClose = nullptr);
    ~PluginEditorWindow() override;
    void closeButtonPressed() override;

private:
    juce::AudioPluginInstance* activePlugin = nullptr;
    std::function<void()> onCloseCallback;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
};

class VstHostManager : private juce::AsyncUpdater
{
public:
    VstHostManager();
    ~VstHostManager();

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void pluginListChanged() {}
        virtual void loadedPluginChanged(const juce::String& name) {}
    };

    void addListener(Listener* l)       { listeners.add(l); }
    void removeListener(Listener* l)    { listeners.remove(l); }

    void scanPlugins(const juce::StringArray& extraPaths = {});
    bool scanSinglePluginFile(const juce::File& file);
    const juce::KnownPluginList& getPluginList() const { return knownPluginList; }

    bool loadPlugin(int trackId, const juce::PluginDescription& desc, double sampleRate, int samplesPerBlock);
    bool loadPlugin(int trackId, const juce::PluginDescription& desc);
    bool loadPluginByName(int trackId, const juce::String& name);
    void unloadPlugin(int trackId);
    void unloadAllPlugins();
    std::vector<int> getLoadedTrackIds() const;

    juce::MemoryBlock getPluginState(int trackId) const;
    bool setPluginState(int trackId, const void* data, int sizeInBytes);

    bool hasLoadedPlugin(int trackId) const;
    juce::String getLoadedPluginName(int trackId) const;
    juce::String getLoadedPluginIdentifier(int trackId) const;

    void openPluginEditor(int trackId);
    void closePluginEditor();
    bool isPluginEditorOpen(int trackId) const { return editorWindow != nullptr && editorTrackId == trackId; }

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void processBlock(int trackId, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);
    void releaseResources();

    void sendMidiNoteOn(int trackId, int channel, int noteNumber, float velocity);
    void sendMidiNoteOff(int trackId, int channel, int noteNumber);
    void allNotesOff();

    bool isScanning() const { return isScanningFlag.load(); }
    juce::String getScanStatusText() const;

    // VST search paths
    static juce::StringArray getDefaultSearchPaths();
    void addCustomSearchPath(const juce::String& path);
    void removeCustomSearchPath(int index);
    const juce::StringArray& getCustomSearchPaths() const { return customSearchPaths; }
    void resetCustomSearchPaths();

    void saveConfig();
    void loadConfig();

private:
    void handleAsyncUpdate() override;
    void requestClosePluginEditor();

    struct PluginSlot
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::AudioBuffer<float> processBuffer;
        juce::String identifier;
    };

    juce::StringArray customSearchPaths;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    std::map<int, std::unique_ptr<PluginSlot>> plugins;
    std::unique_ptr<PluginEditorWindow> editorWindow;
    int editorTrackId = -1;

    std::atomic<bool> isScanningFlag { false };
    std::atomic<bool> scanResultReady { false };
    std::atomic<bool> shouldCancelScan { false };
    std::thread scanThread;
    juce::String currentScanningPlugin;
    mutable juce::CriticalSection scanStatusLock;
    juce::Array<juce::PluginDescription> pendingScanResults;
    juce::CriticalSection scanResultLock;
    bool closeEditorRequested = false;

    std::map<int, juce::MidiBuffer> pendingLiveMidi;
    juce::CriticalSection midiLock;
    mutable juce::SpinLock pluginLock;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VstHostManager)
};

} // namespace Harmonizer
