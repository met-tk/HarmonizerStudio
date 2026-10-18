#include "AudioEngine.h"
#include "ChannelStripMath.h"
#include "../core/TrackModel.h"
#include <cmath>
#include <set>
#include <tuple>

namespace Harmonizer
{

AudioEngine::AudioEngine(MidiModel& model, bool openAudioDevice)
    : midiModel(model), usesAudioDevice(openAudioDevice)
{
    pausePlayheadMode.store(GeneralSettings::getInstance().getPausePlayheadMode());
    metronomeEnabled.store(openAudioDevice
        && GeneralSettings::getInstance().getMetronomeEnabled());
    metronomeSound.store(GeneralSettings::getInstance().getMetronomeSound());
    audioFormatManager.registerBasicFormats();
    audioReadAheadThread.startThread();
    midiModel.addListener(this);
    // Initialise audio with 0 inputs, 2 outputs, restoring user's device/sampleRate/buffer settings
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("HarmonizerStudio");
    auto file = dir.getChildFile("audio_settings.xml");
    std::unique_ptr<juce::XmlElement> savedAudioXml;
    if (file.existsAsFile())
        savedAudioXml = juce::parseXML(file);

    if (openAudioDevice)
    {
        deviceManager.initialise(0, 2, savedAudioXml.get(), true);
        deviceManager.addAudioCallback(this);
    }

    startTimerHz(60); // 60 FPS playhead UI update
}

AudioEngine::~AudioEngine()
{
    midiModel.removeListener(this);
    if (auto* tm = trackModel.exchange(nullptr))
        tm->removeListener(this);
    if (usesAudioDevice) saveAudioSettings();
    stopTimer();
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
    {
        const juce::ScopedLock sl(trackRuntimeLock);
        audioTrackRuntimes.clear();
    }
    audioReadAheadThread.stopThread(3000);
}

void AudioEngine::setTrackModel(TrackModel* tm)
{
    if (auto* previous = trackModel.exchange(tm))
        previous->removeListener(this);
    if (tm != nullptr)
    {
        tm->setFallbackTempoBpm(midiModel.getBpm());
        tm->addListener(this);
        rebuildMeterRegions();
        syncTrackInstruments();
        syncAudioTracks();
    }
}

void AudioEngine::projectSettingsChanged()
{
    if (auto* tm = trackModel.load())
        tm->setFallbackTempoBpm(midiModel.getBpm());
    rebuildMeterRegions();
}

void AudioEngine::timeSignaturesChanged()
{
    rebuildMeterRegions();
}

void AudioEngine::rebuildMeterRegions()
{
    auto regions = std::make_shared<std::vector<MeterRegion>>();
    const int defaultNum = std::clamp(midiModel.getTimeSigNumerator(), 1, 32);
    const int defaultDen = std::clamp(midiModel.getTimeSigDenominator(), 1, 32);
    regions->push_back({ 0.0, 1.0e12, defaultNum, defaultDen });
    if (auto* tm = trackModel.load())
        if (const auto* meter = tm->getTimeSignatureTrack())
        {
            auto events = meter->getTimeSignatures();
            std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
                return a.startBeat < b.startBeat;
            });
            for (const auto& event : events)
            {
                if (!std::isfinite(event.startBeat) || event.startBeat < 0.0) continue;
                regions->back().endBeat = event.startBeat;
                regions->push_back({ event.startBeat, 1.0e12,
                    std::clamp(event.numerator, 1, 32),
                    std::clamp(event.denominator, 1, 32) });
            }
        }
    const auto previous = meterRegions.load();
    if (previous != nullptr && previous->size() == regions->size()
        && std::equal(previous->begin(), previous->end(), regions->begin(),
            [](const MeterRegion& a, const MeterRegion& b) {
                return a.startBeat == b.startBeat && a.endBeat == b.endBeat
                    && a.numerator == b.numerator && a.denominator == b.denominator;
            }))
        return;
    meterRegions.store(regions);
    metronomeResetSerial.fetch_add(1);
}

void AudioEngine::setMetronomeEnabled(bool enabled, bool savePreference)
{
    metronomeEnabled.store(enabled);
    metronomeResetSerial.fetch_add(1);
    if (savePreference) GeneralSettings::getInstance().setMetronomeEnabled(enabled);
}

void AudioEngine::setMetronomeSound(MetronomeSound sound, bool savePreference)
{
    metronomeSound.store(sound);
    if (savePreference) GeneralSettings::getInstance().setMetronomeSound(sound);
}

void AudioEngine::tracksChanged()
{
    rebuildMeterRegions();
    syncTrackInstruments();
    syncAudioTracks();
}

void AudioEngine::trackWillBeRemoved(Track& track)
{
    if (track.getType() == TrackType::Instrument && vstManager.hasLoadedPlugin(track.getId()))
        track.setAssignedVstState(vstManager.getPluginState(track.getId()));
}

void AudioEngine::trackWillBeDuplicated(Track& track)
{
    trackWillBeRemoved(track);
}

void AudioEngine::projectWillReplace()
{
    stopAllActiveNotes();
    vstManager.unloadAllPlugins();
    const juce::ScopedLock sl(trackRuntimeLock);
    trackRuntimes.clear();
    audioTrackRuntimes.clear();
}

void AudioEngine::capturePluginStatesToTracks()
{
    if (auto* tm = trackModel.load())
        for (auto& track : tm->getTracks())
            if (track->getType() == TrackType::Instrument && vstManager.hasLoadedPlugin(track->getId()))
                track->setAssignedVstState(vstManager.getPluginState(track->getId()));
}

void AudioEngine::setTrackVolume(int trackId, float volume)
{
    const juce::ScopedLock sl(trackRuntimeLock);
    if (auto* runtime = findTrackRuntime(trackId))
        runtime->volume = std::clamp(volume, 0.0f, 2.0f);
    if (auto it = audioTrackRuntimes.find(trackId); it != audioTrackRuntimes.end())
        it->second->volume = std::clamp(volume, 0.0f, 2.0f);
}

void AudioEngine::setTrackPan(int trackId, float pan)
{
    const juce::ScopedLock sl(trackRuntimeLock);
    if (auto* runtime = findTrackRuntime(trackId))
        runtime->pan = std::clamp(pan, -1.0f, 1.0f);
    if (auto it = audioTrackRuntimes.find(trackId); it != audioTrackRuntimes.end())
        it->second->pan = std::clamp(pan, -1.0f, 1.0f);
}

AudioEngine::TrackRuntime* AudioEngine::findTrackRuntime(int trackId)
{
    if (auto it = trackRuntimes.find(trackId); it != trackRuntimes.end())
        return it->second.get();
    return nullptr;
}

void AudioEngine::syncTrackInstruments()
{
    auto* tm = trackModel.load();
    if (tm == nullptr) return;

    struct DesiredTrack
    {
        Track* track;
        int id;
        float volume;
        float pan;
        bool playable;
    };
    std::vector<DesiredTrack> desired;
    bool hasSolo = false;
    for (const auto& track : tm->getTracks())
        if ((track->getType() == TrackType::Instrument || track->getType() == TrackType::Audio) && track->isSolo())
            hasSolo = true;
    for (const auto& track : tm->getTracks())
        if (track->getType() == TrackType::Instrument)
            desired.push_back({ track.get(), track->getId(), track->getVolume(), track->getPan(),
                                !track->isMuted() && (!hasSolo || track->isSolo()) });

    {
        const juce::ScopedLock sl(trackRuntimeLock);
        for (auto it = trackRuntimes.begin(); it != trackRuntimes.end();)
        {
            const bool exists = std::any_of(desired.begin(), desired.end(), [&](const DesiredTrack& d) { return d.id == it->first; });
            if (!exists) it = trackRuntimes.erase(it);
            else ++it;
        }
        for (const auto& track : desired)
        {
            auto& runtime = trackRuntimes[track.id];
            if (runtime == nullptr)
            {
                runtime = std::make_unique<TrackRuntime>();
                runtime->activeNotes.reserve(256);
                runtime->synth.prepare(currentSampleRate.load(), currentBlockSize.load());
            }
            runtime->volume = track.volume;
            runtime->pan = track.pan;
            runtime->playable = track.playable;
        }
    }

    for (int loadedId : vstManager.getLoadedTrackIds())
    {
        const auto it = std::find_if(desired.begin(), desired.end(), [&](const DesiredTrack& d) { return d.id == loadedId; });
        if (it == desired.end() || it->track->getAssignedVstName().isEmpty())
            vstManager.unloadPlugin(loadedId);
    }

    const auto& list = vstManager.getPluginList();
    for (const auto& track : desired)
    {
        const auto pluginName = track.track->getAssignedVstName();
        const auto pluginIdentifier = track.track->getAssignedVstIdentifier();
        bool loaded = false;
        if (pluginName.isNotEmpty())
        {
            loaded = vstManager.hasLoadedPlugin(track.id)
                && (pluginIdentifier.isNotEmpty()
                    ? vstManager.getLoadedPluginIdentifier(track.id) == pluginIdentifier
                    : vstManager.getLoadedPluginName(track.id).equalsIgnoreCase(pluginName));
            if (!loaded)
            {
                const juce::String key = pluginIdentifier.isNotEmpty() ? pluginIdentifier : pluginName;
                juce::String failedKey;
                {
                    const juce::ScopedLock sl(trackRuntimeLock);
                    if (auto* runtime = findTrackRuntime(track.id))
                        failedKey = runtime->failedPluginIdentifier;
                }
                if (failedKey != key)
                {
                    const juce::PluginDescription* description = nullptr;
                    for (const auto& candidate : list.getTypes())
                        if ((pluginIdentifier.isNotEmpty() && candidate.createIdentifierString() == pluginIdentifier)
                            || (pluginIdentifier.isEmpty() && candidate.name.equalsIgnoreCase(pluginName)))
                        {
                            description = &candidate;
                            break;
                        }
                    if (description != nullptr)
                    {
                        loaded = vstManager.loadPlugin(track.id, *description);
                        if (loaded)
                        {
                            const auto& state = track.track->getAssignedVstState();
                            if (state.getSize() > 0)
                                vstManager.setPluginState(track.id, state.getData(), (int)state.getSize());
                            if (auto* modelTrack = tm->getTrackById(track.id))
                                modelTrack->setAssignedVstIdentifier(description->createIdentifierString());
                        }
                    }
                    if (!loaded)
                    {
                        const juce::ScopedLock sl(trackRuntimeLock);
                        if (auto* runtime = findTrackRuntime(track.id))
                            runtime->failedPluginIdentifier = key;
                    }
                }
            }
        }
        if (!loaded && vstManager.hasLoadedPlugin(track.id))
            vstManager.unloadPlugin(track.id);
        const juce::ScopedLock sl(trackRuntimeLock);
        if (auto* runtime = findTrackRuntime(track.id))
        {
            if (runtime->useVst != loaded)
            {
                runtime->synth.allNotesOff();
                runtime->midi.clear();
                runtime->activeNotes.clear();
            }
            runtime->useVst = loaded;
            if (loaded || pluginName.isEmpty())
                runtime->failedPluginIdentifier.clear();
        }
    }
}

void AudioEngine::syncAudioTracks()
{
    auto* tm = trackModel.load();
    if (tm == nullptr) return;
    bool hasSolo = false;
    for (const auto& track : tm->getTracks())
        if ((track->getType() == TrackType::Instrument || track->getType() == TrackType::Audio) && track->isSolo())
            hasSolo = true;

    using ClipKey = std::tuple<int, int64_t, std::string>;
    std::set<ClipKey> existing;
    {
        const juce::ScopedLock sl(trackRuntimeLock);
        for (const auto& [trackId, runtime] : audioTrackRuntimes)
            for (const auto& clip : runtime->clips)
                existing.emplace(trackId, clip->clip.id, clip->clip.sourcePath.toStdString());
    }

    // Opening and decoding a new media file may block. Keep it outside the
    // audio callback lock, then swap the prepared players into place quickly.
    std::map<int, std::vector<std::unique_ptr<AudioClipRuntime>>> fresh;
    for (const auto& track : tm->getTracks())
    {
        if (track->getType() != TrackType::Audio) continue;
        for (const auto& clip : track->getAudioClips())
        {
            if (existing.find({ track->getId(), clip.id, clip.sourcePath.toStdString() }) != existing.end())
                continue;
            juce::File file(clip.sourcePath);
            std::unique_ptr<juce::AudioFormatReader> reader(file.existsAsFile()
                ? audioFormatManager.createReaderFor(file) : nullptr);
            if (reader == nullptr) continue; // Missing media remains visible in the project.
            auto clipRuntime = std::make_unique<AudioClipRuntime>();
            const double sourceRate = reader->sampleRate;
            clipRuntime->source = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
            clipRuntime->transport.setSource(clipRuntime->source.get(), 32768,
                                             &audioReadAheadThread, sourceRate, 2);
            clipRuntime->transport.prepareToPlay(currentBlockSize.load(), currentSampleRate.load());
            clipRuntime->prepared = true;
            clipRuntime->clip = clip;
            fresh[track->getId()].push_back(std::move(clipRuntime));
        }
    }

    std::map<int, std::unique_ptr<AudioTrackRuntime>> updated;
    {
        const juce::ScopedLock sl(trackRuntimeLock);
        for (const auto& track : tm->getTracks())
        {
            if (track->getType() != TrackType::Audio) continue;
            auto runtime = std::make_unique<AudioTrackRuntime>();
            runtime->volume = track->getVolume();
            runtime->pan = track->getPan();
            runtime->playable = !track->isMuted() && (!hasSolo || track->isSolo());
            auto previous = audioTrackRuntimes.find(track->getId());
            for (const auto& clip : track->getAudioClips())
            {
                std::unique_ptr<AudioClipRuntime> clipRuntime;
                if (previous != audioTrackRuntimes.end())
                    for (auto& old : previous->second->clips)
                        if (old != nullptr && old->clip.id == clip.id && old->clip.sourcePath == clip.sourcePath)
                        {
                            clipRuntime = std::move(old);
                            break;
                        }
                if (clipRuntime == nullptr)
                    for (auto& prepared : fresh[track->getId()])
                        if (prepared != nullptr && prepared->clip.id == clip.id
                            && prepared->clip.sourcePath == clip.sourcePath)
                        {
                            clipRuntime = std::move(prepared);
                            break;
                        }
                if (clipRuntime == nullptr) continue;
                clipRuntime->clip = clip;
                runtime->clips.push_back(std::move(clipRuntime));
            }
            updated[track->getId()] = std::move(runtime);
        }
        audioTrackRuntimes.swap(updated);
    }
}

void AudioEngine::saveAudioSettings()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("HarmonizerStudio");
    dir.createDirectory();
    auto file = dir.getChildFile("audio_settings.xml");
    if (auto xml = deviceManager.createStateXml())
        xml->writeTo(file);
}

void AudioEngine::play()
{
    if (!playing.load())
    {
        // A stopped piano-roll drag may have deferred rebuilding the playback
        // snapshot. Publish the latest clip notes before the audio thread starts.
        if (auto* tm = trackModel.load())
            tm->refreshPlaybackSnapshotIfDirty();
        playbackStartBeat.store(playheadBeat.load());
        metronomeResetSerial.fetch_add(1);
        playing.store(true);
        playheadListeners.call([](PlayheadListener& l) { l.playbackStateChanged(true); });
    }
}

void AudioEngine::pause()
{
    if (playing.exchange(false))
    {
        stopAllActiveNotes();
        playheadListeners.call([](PlayheadListener& l) { l.playbackStateChanged(false); });
        if (pausePlayheadMode.load() == PausePlayheadMode::ReturnToPlaybackStart)
        {
            playheadBeat.store(playbackStartBeat.load());
            playheadListeners.call([this](PlayheadListener& l) {
                l.playheadMoved(playheadBeat.load());
            });
        }
    }
}

void AudioEngine::stop()
{
    const bool wasPlaying = playing.exchange(false);
    stopAllActiveNotes();
    if (wasPlaying)
        playheadListeners.call([](PlayheadListener& l) { l.playbackStateChanged(false); });
    playheadBeat.store(0.0);
    playheadListeners.call([](PlayheadListener& l) { l.playheadMoved(0.0); });
}

void AudioEngine::setPlayheadBeat(double newBeat)
{
    stopAllActiveNotes();
    const double beat = std::isfinite(newBeat) ? std::max(0.0, newBeat) : 0.0;
    playheadBeat.store(beat);
    metronomeResetSerial.fetch_add(1);
    if (playing.load()) playbackStartBeat.store(beat);
    playheadListeners.call([this](PlayheadListener& l) { l.playheadMoved(playheadBeat.load()); });
}

void AudioEngine::previewNoteOn(int midiPitch, float velocity, bool liveVelocity)
{
    velocity = std::clamp(velocity, 1.0f / 127.0f, 1.0f);
    int trackId = 0;
    if (auto* tm = trackModel.load())
        if (auto* instrument = tm->getActiveInstrumentTrack())
            trackId = instrument->getId();
    const juce::ScopedLock sl(trackRuntimeLock);
    // Retriggering the same pitch after switching tracks must release the
    // previous track's voice before the routing entry is replaced.
    if (auto previous = activePreviewPitches.find(midiPitch); previous != activePreviewPitches.end())
    {
        if (auto* previousRuntime = findTrackRuntime(previous->second))
        {
            if (previousRuntime->useVst)
                vstManager.sendMidiNoteOff(previous->second, 1, midiPitch);
            else
                previousRuntime->synth.noteOff(1, midiPitch);
        }
        else
            builtinSynth.noteOff(1, midiPitch);
    }
    activePreviewPitches[midiPitch] = trackId;
    if (liveVelocity && !playing.load())
    {
        // A VST normally reads note velocity only at note-on. Start its preview
        // voice once, then adjust the audible level without retriggering it.
        velocityPreviewPitch = midiPitch;
        velocityPreviewTrackId = trackId;
        velocityPreviewMidiVelocity = std::max(velocity, 0.5f);
        velocityPreviewGain.setCurrentAndTargetValue(velocity / velocityPreviewMidiVelocity);
        velocity = velocityPreviewMidiVelocity;
    }
    else
    {
        velocityPreviewPitch = -1;
        velocityPreviewGain.setCurrentAndTargetValue(1.0f);
    }
    if (auto* runtime = findTrackRuntime(trackId))
    {
        if (runtime->useVst)
            vstManager.sendMidiNoteOn(trackId, 1, midiPitch, velocity);
        else
            runtime->synth.noteOn(1, midiPitch, velocity);
    }
    else
        builtinSynth.noteOn(1, midiPitch, velocity);
}

void AudioEngine::updatePreviewNoteVelocity(int midiPitch, float velocity)
{
    const juce::ScopedLock sl(trackRuntimeLock);
    if (midiPitch == velocityPreviewPitch)
        velocityPreviewGain.setTargetValue(std::clamp(velocity, 1.0f / 127.0f, 1.0f)
                                           / velocityPreviewMidiVelocity);
}

void AudioEngine::previewNoteOff(int midiPitch)
{
    const juce::ScopedLock sl(trackRuntimeLock);
    if (midiPitch == velocityPreviewPitch)
    {
        velocityPreviewPitch = -1;
        velocityPreviewGain.setTargetValue(1.0f);
    }
    int trackId = 0;
    if (auto it = activePreviewPitches.find(midiPitch); it != activePreviewPitches.end())
    {
        trackId = it->second;
        activePreviewPitches.erase(it);
    }
    if (auto* runtime = findTrackRuntime(trackId))
    {
        if (runtime->useVst)
            vstManager.sendMidiNoteOff(trackId, 1, midiPitch);
        else
            runtime->synth.noteOff(1, midiPitch);
    }
    else
        builtinSynth.noteOff(1, midiPitch);
}

void AudioEngine::allPreviewNotesOff()
{
    const juce::ScopedLock sl(trackRuntimeLock);
    velocityPreviewPitch = -1;
    velocityPreviewGain.setTargetValue(1.0f);
    for (const auto& [pitch, trackId] : activePreviewPitches)
    {
        if (auto* runtime = findTrackRuntime(trackId))
        {
            if (runtime->useVst)
                vstManager.sendMidiNoteOff(trackId, 1, pitch);
            else
                runtime->synth.noteOff(1, pitch);
        }
        else
            builtinSynth.noteOff(1, pitch);
    }
    activePreviewPitches.clear();
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr)
    {
        currentSampleRate.store(device->getCurrentSampleRate());
        currentBlockSize.store(device->getCurrentBufferSizeSamples());
    }

    int maxChannels = (device != nullptr) ? device->getActiveOutputChannels().countNumberOfSetBits() : 2;
    tempProcessBuffer.setSize(std::max(2, maxChannels), std::max(2048, currentBlockSize.load()));
    activeNotes.reserve(256);

    {
        const juce::ScopedLock sl(trackRuntimeLock);
        velocityPreviewPitch = -1;
        velocityPreviewGain.reset(currentSampleRate.load(), 0.025);
        velocityPreviewGain.setCurrentAndTargetValue(1.0f);
    }

    builtinSynth.prepare(currentSampleRate.load(), currentBlockSize.load());
    {
        const juce::ScopedLock sl(trackRuntimeLock);
        for (auto& [trackId, runtime] : trackRuntimes)
            runtime->synth.prepare(currentSampleRate.load(), currentBlockSize.load());
        for (auto& [trackId, runtime] : audioTrackRuntimes)
            for (auto& clip : runtime->clips)
            {
                clip->transport.prepareToPlay(currentBlockSize.load(), currentSampleRate.load());
                clip->prepared = true;
            }
    }
    vstManager.prepareToPlay(currentSampleRate.load(), currentBlockSize.load());
}

void AudioEngine::audioDeviceStopped()
{
    stopAllActiveNotes();
    vstManager.releaseResources();
    const juce::ScopedLock sl(trackRuntimeLock);
    for (auto& [trackId, runtime] : audioTrackRuntimes)
        for (auto& clip : runtime->clips)
        {
            clip->transport.releaseResources();
            clip->prepared = false;
        }
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const*,
                                                  int,
                                                  float* const* outputChannelData,
                                                  int numOutputChannels,
                                                  int numSamples,
                                                  const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);

    if (numOutputChannels <= 0 || numSamples <= 0)
        return;

    if (tempProcessBuffer.getNumChannels() < numOutputChannels || tempProcessBuffer.getNumSamples() < numSamples)
        tempProcessBuffer.setSize(numOutputChannels, numSamples, false, true, true);

    juce::AudioBuffer<float> tempBuffer(tempProcessBuffer.getArrayOfWritePointers(), numOutputChannels, numSamples);
    juce::MidiBuffer fallbackMidi;
    const juce::CriticalSection::ScopedTryLockType runtimeLock(trackRuntimeLock);
    if (!runtimeLock.isLocked())
        return;

    for (auto& [trackId, runtime] : trackRuntimes)
        runtime->midi.clear();
    const bool isPlayingBlock = playing.load();
    const double blockStartBeat = playheadBeat.load();
    if (isPlayingBlock)
        processSequencerBlock(numSamples, fallbackMidi);

    if (trackModel.load() == nullptr)
    {
        tempBuffer.clear();
        builtinSynth.renderNextBlock(tempBuffer, fallbackMidi, 0, numSamples);
        if (!isPlayingBlock)
            applyVelocityPreviewGain(tempBuffer, 0);
        const float volume = outputVolume.load();
        const float pan = outputPan.load();
        const auto panGains = equalPowerPanGains(pan);
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
            {
                const float gain = volume * (ch == 0 ? panGains.left
                                                     : ch == 1 ? panGains.right : 1.0f);
                juce::FloatVectorOperations::copy(outputChannelData[ch], tempBuffer.getReadPointer(ch), numSamples);
                juce::FloatVectorOperations::multiply(outputChannelData[ch], gain, numSamples);
            }
        if (isPlayingBlock && metronomeEnabled.load())
            mixMetronome(outputChannelData, numOutputChannels, numSamples,
                         blockStartBeat, midiModel.getBpm());
        return;
    }

    const float masterVolume = outputVolume.load();
    const float masterPan = outputPan.load();
    for (auto& [trackId, runtime] : trackRuntimes)
    {
        tempBuffer.clear();
        if (runtime->useVst)
            vstManager.processBlock(trackId, tempBuffer, runtime->midi);
        else
            runtime->synth.renderNextBlock(tempBuffer, runtime->midi, 0, numSamples);

        if (!isPlayingBlock)
            applyVelocityPreviewGain(tempBuffer, trackId);

        if (!runtime->playable)
            continue;

        const float volume = runtime->volume * masterVolume;
        const float pan = std::clamp(runtime->pan + masterPan, -1.0f, 1.0f);
        const auto panGains = equalPowerPanGains(pan);
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
            {
                const float gain = volume * (ch == 0 ? panGains.left
                                                     : ch == 1 ? panGains.right : 1.0f);
                juce::FloatVectorOperations::addWithMultiply(outputChannelData[ch],
                    tempBuffer.getReadPointer(ch), gain, numSamples);
            }
    }
    if (isPlayingBlock)
    {
        auto* tm = trackModel.load();
        const double bpm = tm != nullptr ? tm->getTempoAtBeat(blockStartBeat, midiModel.getBpm())
                                         : midiModel.getBpm();
        const double samplesPerBeat = currentSampleRate.load() * 60.0 / std::max(20.0, bpm);
        const double loopStart = midiModel.getLoopStart();
        const double loopEnd = midiModel.getLoopEnd();
        const bool wraps = midiModel.isLoopEnabled() && loopEnd > loopStart
            && blockStartBeat < loopEnd
            && blockStartBeat + numSamples / samplesPerBeat > loopEnd;
        if (wraps)
        {
            const int beforeLoop = std::clamp((int)std::ceil((loopEnd - blockStartBeat) * samplesPerBeat),
                                               0, numSamples);
            if (beforeLoop > 0)
                mixAudioTracks(outputChannelData, numOutputChannels, beforeLoop, blockStartBeat, tempBuffer);
            if (beforeLoop < numSamples)
                mixAudioTracks(outputChannelData, numOutputChannels, numSamples - beforeLoop,
                               loopStart, tempBuffer, beforeLoop);
            if (metronomeEnabled.load())
            {
                if (beforeLoop > 0)
                    mixMetronome(outputChannelData, numOutputChannels, beforeLoop, blockStartBeat, bpm);
                if (beforeLoop < numSamples)
                    mixMetronome(outputChannelData, numOutputChannels, numSamples - beforeLoop,
                                 loopStart, bpm, beforeLoop);
            }
        }
        else
        {
            mixAudioTracks(outputChannelData, numOutputChannels, numSamples, blockStartBeat, tempBuffer);
            if (metronomeEnabled.load())
                mixMetronome(outputChannelData, numOutputChannels, numSamples, blockStartBeat, bpm);
        }
    }
}

void AudioEngine::applyVelocityPreviewGain(juce::AudioBuffer<float>& buffer, int trackId)
{
    if (velocityPreviewTrackId != trackId
        || (!velocityPreviewGain.isSmoothing()
            && velocityPreviewGain.getCurrentValue() == 1.0f))
        return;

    // Advance the ramp once per sample so both channels receive the same gain.
    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    auto* const* samplesByChannel = buffer.getArrayOfWritePointers();
    for (int sample = 0; sample < samples; ++sample)
    {
        const float gain = velocityPreviewGain.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
            samplesByChannel[channel][sample] *= gain;
    }
}

void AudioEngine::mixMetronome(float* const* outputs, int channels, int samples,
                               double startBeat, double bpm, int outputOffset)
{
    const auto reset = metronomeResetSerial.load();
    if (renderedMetronomeResetSerial != reset)
    {
        renderedMetronomeResetSerial = reset;
        lastMetronomeTickBeat = -1.0;
        lastMetronomeRenderedBeat = -1.0;
        metronomeClickSamplesLeft = 0;
    }
    const double sampleRate = std::max(8000.0, currentSampleRate.load());
    const double beatStep = std::max(20.0, bpm) / (60.0 * sampleRate);
    const auto regions = meterRegions.load();
    const MeterRegion fallback { 0.0, 1.0e12,
        std::clamp(midiModel.getTimeSigNumerator(), 1, 32),
        std::clamp(midiModel.getTimeSigDenominator(), 1, 32) };
    const float masterVolume = outputVolume.load();
    size_t regionIndex = 0;
    const auto sound = metronomeSound.load();
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    for (int i = 0; i < samples; ++i)
    {
        const double beat = startBeat + i * beatStep;
        if (beat + 0.00001 < lastMetronomeRenderedBeat)
            lastMetronomeTickBeat = -1.0;
        lastMetronomeRenderedBeat = beat;
        if (regions != nullptr && !regions->empty())
            while (regionIndex + 1 < regions->size()
                   && beat >= (*regions)[regionIndex].endBeat - 0.000001)
                ++regionIndex;
        const auto& meter = regions != nullptr && !regions->empty()
            ? (*regions)[regionIndex] : fallback;
        const double unit = 4.0 / meter.denominator;
        const auto tick = std::max(0LL, (long long)std::floor(
            (beat - meter.startBeat + 0.000001) / unit));
        const double tickBeat = meter.startBeat + tick * unit;
        if (tickBeat > lastMetronomeTickBeat + 0.000001)
        {
            lastMetronomeTickBeat = tickBeat;
            if (beat - tickBeat <= beatStep * 1.5)
            {
                const bool accent = tick % meter.numerator == 0;
                metronomeFrequency = sound == MetronomeSound::Electronic
                    ? (accent ? 1640.0 : 1180.0)
                    : sound == MetronomeSound::Wood
                        ? (accent ? 910.0 : 640.0)
                        : (accent ? 1140.0 : 770.0);
                metronomeClickLength = std::max(1, (int)(sampleRate
                    * (sound == MetronomeSound::Wood ? 0.042 : 0.028)));
                metronomeClickSamplesLeft = metronomeClickLength;
                metronomeAmplitude = accent ? 0.26f : 0.17f;
                metronomePhase = 0.0;
            }
        }
        if (metronomeClickSamplesLeft <= 0) continue;
        const double progress = 1.0 - (double)metronomeClickSamplesLeft / metronomeClickLength;
        const double envelope = std::exp(-8.0 * progress);
        const double harmonic = sound == MetronomeSound::Wood ? 0.35
            : sound == MetronomeSound::Electronic ? 0.18 : 0.08;
        const float value = (float)((std::sin(metronomePhase)
            + harmonic * std::sin(metronomePhase * 2.1))
            * envelope * metronomeAmplitude * masterVolume);
        metronomePhase += twoPi * metronomeFrequency / sampleRate;
        --metronomeClickSamplesLeft;
        for (int ch = 0; ch < channels; ++ch)
            if (outputs[ch] != nullptr)
                outputs[ch][outputOffset + i] += value;
    }
}

void AudioEngine::mixAudioTracks(float* const* outputs, int channels, int samples,
                                 double blockStartBeat, juce::AudioBuffer<float>& tempBuffer,
                                 int outputOffset)
{
    auto* tm = trackModel.load();
    if (tm == nullptr) return;
    const double bpm = std::max(20.0, tm->getTempoAtBeat(blockStartBeat, midiModel.getBpm()));
    const double samplesPerBeat = currentSampleRate.load() * 60.0 / bpm;
    const double blockEndBeat = blockStartBeat + samples / samplesPerBeat;
    const float masterVolume = outputVolume.load();
    const float masterPan = outputPan.load();
    for (auto& [trackId, runtime] : audioTrackRuntimes)
    {
        if (!runtime->playable) continue;
        for (auto& clipRuntime : runtime->clips)
        {
            const auto& clip = clipRuntime->clip;
            if (clip.isMuted || !clipRuntime->prepared || clip.startBeat >= blockEndBeat
                || clip.startBeat + clip.lengthBeats <= blockStartBeat)
                continue;
            const int offset = std::clamp((int)std::ceil((clip.startBeat - blockStartBeat) * samplesPerBeat), 0, samples);
            const double expectedSeconds = tm->getSecondsBetweenBeats(clip.startBeat,
                                                                        blockStartBeat, midiModel.getBpm());
            if (expectedSeconds >= clip.durationSeconds) continue;
            const int count = (int)std::clamp(std::ceil((clip.durationSeconds - expectedSeconds)
                                                       * currentSampleRate.load()),
                                               0.0, (double)(samples - offset));
            if (count <= 0) continue;
            const double sourcePosition = clip.sourceOffsetSeconds + expectedSeconds;
            if (!clipRuntime->transport.isPlaying()
                || std::abs(clipRuntime->transport.getCurrentPosition() - sourcePosition) > 0.05)
                clipRuntime->transport.setPosition(sourcePosition);
            if (!clipRuntime->transport.isPlaying()) clipRuntime->transport.start();
            tempBuffer.clear();
            juce::AudioSourceChannelInfo info(&tempBuffer, offset, count);
            clipRuntime->transport.getNextAudioBlock(info);
            const float pan = std::clamp(runtime->pan + masterPan, -1.0f, 1.0f);
            const float volume = runtime->volume * masterVolume;
            const auto panGains = equalPowerPanGains(pan);
            for (int ch = 0; ch < channels; ++ch)
                if (outputs[ch] != nullptr)
                {
                    const float gain = volume * (ch == 0 ? panGains.left
                                                        : ch == 1 ? panGains.right : 1.0f);
                    juce::FloatVectorOperations::addWithMultiply(outputs[ch] + outputOffset + offset,
                        tempBuffer.getReadPointer(ch, offset), gain, count);
                }
        }
    }
}

void AudioEngine::processSequencerBlock(int numSamples, juce::MidiBuffer& outMidi)
{
    const juce::ScopedLock sl(playbackLock);

    double startBeat = playheadBeat.load();
    auto* tm = trackModel.load();
    double bpm = (tm != nullptr) ? tm->getTempoAtBeat(startBeat, midiModel.getBpm()) : midiModel.getBpm();
    double samplesPerBeat = (currentSampleRate.load() * 60.0) / std::max(20.0, bpm);
    double beatsInBlock = numSamples / samplesPerBeat;
    double endBeat = startBeat + beatsInBlock;

    double loopStart = midiModel.getLoopStart();
    double loopEnd = midiModel.getLoopEnd();
    bool loop = midiModel.isLoopEnabled() && (loopEnd > loopStart);

    // Each track keeps its own active notes so overlapping pitches on different
    // instruments cannot stop one another.
    if (tm != nullptr)
    {
        for (auto& [trackId, runtime] : trackRuntimes)
            for (auto it = runtime->activeNotes.begin(); it != runtime->activeNotes.end();)
            {
                int offSampleOffset = 0;
                bool hasEnded = false;
                if (it->endBeat >= startBeat && it->endBeat < endBeat)
                {
                    hasEnded = true;
                    offSampleOffset = (int)std::clamp((it->endBeat - startBeat) * samplesPerBeat, 0.0, (double)(numSamples - 1));
                }
                else if (startBeat >= it->endBeat || (loop && startBeat < loopStart + 0.1 && it->endBeat > loopStart))
                    hasEnded = true;

                if (hasEnded)
                {
                    runtime->midi.addEvent(juce::MidiMessage::noteOff(1, it->pitch), offSampleOffset);
                    it = runtime->activeNotes.erase(it);
                }
                else ++it;
            }
    }
    else for (auto it = activeNotes.begin(); it != activeNotes.end();)
    {
        bool hasEnded = false;
        int offSampleOffset = 0;

        if (it->endBeat >= startBeat && it->endBeat < endBeat)
        {
            hasEnded = true;
            offSampleOffset = (int)std::clamp((it->endBeat - startBeat) * samplesPerBeat, 0.0, (double)(numSamples - 1));
        }
        else if (startBeat >= it->endBeat)
        {
            hasEnded = true;
            offSampleOffset = 0;
        }
        else if (loop && startBeat < loopStart + 0.1 && it->endBeat > loopStart)
        {
            hasEnded = true;
            offSampleOffset = 0;
        }

        if (hasEnded)
        {
            outMidi.addEvent(juce::MidiMessage::noteOff(1, it->pitch), offSampleOffset);
            it = activeNotes.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // 2. Trigger notes that start in this time slice (Multi-track playback from TrackModel or fallback to MidiModel)
    if (tm != nullptr)
    {
        if (auto snapshot = tm->getPlaybackSnapshot())
        {
            for (const auto& n : *snapshot)
            {
                if (n.isMuted)
                    continue;
                auto* runtime = findTrackRuntime(n.trackId);
                if (runtime == nullptr)
                    continue;

                bool startsHere = false;
                double noteStart = n.startBeat;
                int sampleOffset = 0;

                if (noteStart >= startBeat && noteStart < endBeat)
                {
                    startsHere = true;
                    sampleOffset = (int)std::clamp((noteStart - startBeat) * samplesPerBeat, 0.0, (double)(numSamples - 1));
                }
                else if (loop && endBeat >= loopEnd)
                {
                    double wrappedEnd = loopStart + (endBeat - loopEnd);
                    if (noteStart >= loopStart && noteStart < wrappedEnd)
                    {
                        startsHere = true;
                        double wrapDist = (loopEnd - startBeat) + (noteStart - loopStart);
                        sampleOffset = (int)std::clamp(wrapDist * samplesPerBeat, 0.0, (double)(numSamples - 1));
                    }
                }

                if (startsHere)
                {
                    // If the same pitch is already active, send an immediate noteOff before noteOn to prevent voice hanging
                    for (auto it = runtime->activeNotes.begin(); it != runtime->activeNotes.end();)
                    {
                        if (it->pitch == n.pitch)
                        {
                            runtime->midi.addEvent(juce::MidiMessage::noteOff(1, it->pitch), std::max(0, sampleOffset - 1));
                            it = runtime->activeNotes.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }

                    juce::uint8 vel = (juce::uint8)std::clamp((int)(n.velocity * 127.0f), 1, 127);
                    runtime->midi.addEvent(juce::MidiMessage::noteOn(1, n.pitch, vel), sampleOffset);
                    runtime->activeNotes.push_back({ n.pitch, n.startBeat + n.lengthBeats });
                }
            }
        }
    }
    else
    {
        if (auto snapshot = midiModel.getNotesSnapshot())
        {
            for (const auto& n : *snapshot)
            {
                if (n.isMuted)
                    continue;

                bool startsHere = false;
                double noteStart = n.startBeat;
                int sampleOffset = 0;

                if (noteStart >= startBeat && noteStart < endBeat)
                {
                    startsHere = true;
                    sampleOffset = (int)std::clamp((noteStart - startBeat) * samplesPerBeat, 0.0, (double)(numSamples - 1));
                }
                else if (loop && endBeat >= loopEnd)
                {
                    double wrappedEnd = loopStart + (endBeat - loopEnd);
                    if (noteStart >= loopStart && noteStart < wrappedEnd)
                    {
                        startsHere = true;
                        double wrapDist = (loopEnd - startBeat) + (noteStart - loopStart);
                        sampleOffset = (int)std::clamp(wrapDist * samplesPerBeat, 0.0, (double)(numSamples - 1));
                    }
                }

                if (startsHere)
                {
                    for (auto it = activeNotes.begin(); it != activeNotes.end();)
                    {
                        if (it->pitch == n.pitch)
                        {
                            outMidi.addEvent(juce::MidiMessage::noteOff(1, it->pitch), std::max(0, sampleOffset - 1));
                            it = activeNotes.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }

                    juce::uint8 vel = (juce::uint8)std::clamp((int)(n.velocity * 127.0f), 1, 127);
                    outMidi.addEvent(juce::MidiMessage::noteOn(1, n.pitch, vel), sampleOffset);
                    activeNotes.push_back({ n.pitch, n.startBeat + n.lengthBeats });
                }
            }
        }
    }

    // Advance playhead
    if (loop && endBeat >= loopEnd)
    {
        double overflow = endBeat - loopEnd;
        playheadBeat.store(loopStart + std::fmod(overflow, loopEnd - loopStart));
    }
    else
    {
        playheadBeat.store(endBeat);
    }
}

void AudioEngine::stopAllActiveNotes()
{
    const juce::ScopedLock runtimeLock(trackRuntimeLock);
    const juce::ScopedLock playbackGuard(playbackLock);
    builtinSynth.allNotesOff();
    for (auto& [trackId, runtime] : trackRuntimes)
    {
        runtime->synth.allNotesOff();
        runtime->midi.clear();
        runtime->activeNotes.clear();
    }
    for (auto& [trackId, runtime] : audioTrackRuntimes)
        for (auto& clip : runtime->clips)
            clip->transport.stop();
    vstManager.allNotesOff();
    activeNotes.clear();
    activePreviewPitches.clear();
    velocityPreviewPitch = -1;
    velocityPreviewGain.setCurrentAndTargetValue(1.0f);
}

void AudioEngine::timerCallback()
{
    if (playing.load())
    {
        playheadListeners.call([this](PlayheadListener& l) {
            l.playheadMoved(playheadBeat.load());
        });
    }
}

} // namespace Harmonizer
