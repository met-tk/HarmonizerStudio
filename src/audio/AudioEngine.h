#pragma once

#include "BuiltinSynth.h"
#include "VstHostManager.h"
#include "../core/MidiModel.h"
#include "../core/TrackModel.h"
#include "../core/GeneralSettings.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>

namespace Harmonizer
{

class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::Timer,
                    private TrackModel::Listener,
                    private MidiModel::Listener
{
public:
    AudioEngine(MidiModel& model, bool openAudioDevice = true);
    ~AudioEngine() override;

    void setTrackModel(TrackModel* tm);
    void syncTrackInstruments();
    void syncAudioTracks();
    void capturePluginStatesToTracks();
    void setTrackVolume(int trackId, float volume);
    void setTrackPan(int trackId, float pan);

    class PlayheadListener
    {
    public:
        virtual ~PlayheadListener() = default;
        virtual void playheadMoved(double beatPosition) = 0;
        virtual void playbackStateChanged(bool isPlaying) = 0;
    };

    void addPlayheadListener(PlayheadListener* l)    { playheadListeners.add(l); }
    void removePlayheadListener(PlayheadListener* l) { playheadListeners.remove(l); }

    juce::AudioDeviceManager& getDeviceManager()     { return deviceManager; }
    VstHostManager& getVstHostManager()               { return vstManager; }
    BuiltinSynth& getBuiltinSynth()                   { return builtinSynth; }

    void saveAudioSettings();

    // Transport controls
    void play();
    void pause();
    void stop();
    bool isPlaying() const                            { return playing.load(); }
    void setPausePlayheadMode(PausePlayheadMode mode)   { pausePlayheadMode.store(mode); }
    PausePlayheadMode getPausePlayheadMode() const      { return pausePlayheadMode.load(); }

    double getPlayheadBeat() const                    { return playheadBeat.load(); }
    void setPlayheadBeat(double newBeat);

    // Audition preview
    void previewNoteOn(int midiPitch, float velocity = 0.8f, bool liveVelocity = false);
    void updatePreviewNoteVelocity(int midiPitch, float velocity);
    void previewNoteOff(int midiPitch);
    void allPreviewNotesOff();

    // Metronome
    bool isMetronomeEnabled() const                   { return metronomeEnabled.load(); }
    void setMetronomeEnabled(bool enabled, bool savePreference = true);
    MetronomeSound getMetronomeSound() const           { return metronomeSound.load(); }
    void setMetronomeSound(MetronomeSound sound, bool savePreference = true);

    // Channel Strip Gain and Pan (Applies directly to VST3 and BuiltinSynth)
    float getOutputVolume() const                     { return outputVolume.load(); }
    void setOutputVolume(float v)                     { outputVolume.store(std::clamp(v, 0.0f, 2.0f)); }

    float getOutputPan() const                        { return outputPan.load(); }
    void setOutputPan(float p)                        { outputPan.store(std::clamp(p, -1.0f, 1.0f)); }

    // AudioIODeviceCallback implementation
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // Timer callback (for UI playhead updates ~60Hz)
    void timerCallback() override;

private:
    void tracksChanged() override;
    void tempoChanged() override { syncAudioTracks(); }
    void timeSignaturesChanged() override;
    void projectSettingsChanged() override;
    void trackWillBeRemoved(Track& track) override;
    void trackWillBeDuplicated(Track& track) override;
    void projectWillReplace() override;

    MidiModel& midiModel;
    bool usesAudioDevice = true;
    std::atomic<TrackModel*> trackModel { nullptr };
    juce::AudioDeviceManager deviceManager;
    BuiltinSynth builtinSynth;
    VstHostManager vstManager;

    juce::AudioBuffer<float> tempProcessBuffer;

    std::atomic<bool> playing { false };
    std::atomic<double> playheadBeat { 0.0 };
    std::atomic<double> playbackStartBeat { 0.0 };
    std::atomic<PausePlayheadMode> pausePlayheadMode { PausePlayheadMode::StayAtPause };
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<MetronomeSound> metronomeSound { MetronomeSound::Classic };
    std::atomic<unsigned int> metronomeResetSerial { 0 };
    unsigned int renderedMetronomeResetSerial = 0;
    double lastMetronomeTickBeat = -1.0;
    double lastMetronomeRenderedBeat = -1.0;
    int metronomeClickSamplesLeft = 0;
    int metronomeClickLength = 1;
    double metronomePhase = 0.0;
    double metronomeFrequency = 880.0;
    float metronomeAmplitude = 0.0f;
    struct MeterRegion
    {
        double startBeat = 0.0;
        double endBeat = 1.0e12;
        int numerator = 4;
        int denominator = 4;
    };
    std::atomic<std::shared_ptr<const std::vector<MeterRegion>>> meterRegions;
    void rebuildMeterRegions();
    void mixMetronome(float* const* outputs, int channels, int samples,
                      double startBeat, double bpm, int outputOffset = 0);
    std::atomic<float> outputVolume { 1.0f };
    std::atomic<float> outputPan { 0.0f };

    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int> currentBlockSize { 512 };

    // Active playing notes tracker to send proper note-offs
    struct ActiveNote
    {
        int pitch;
        double endBeat;
    };
    struct TrackRuntime
    {
        BuiltinSynth synth;
        juce::MidiBuffer midi;
        std::vector<ActiveNote> activeNotes;
        float volume = 1.0f;
        float pan = 0.0f;
        bool playable = true;
        bool useVst = false;
        juce::String failedPluginIdentifier;
    };
    std::map<int, std::unique_ptr<TrackRuntime>> trackRuntimes;
    struct AudioClipRuntime
    {
        AudioClip clip;
        std::unique_ptr<juce::AudioFormatReaderSource> source;
        juce::AudioTransportSource transport;
        bool prepared = false;
        ~AudioClipRuntime() { transport.setSource(nullptr); }
    };
    struct AudioTrackRuntime
    {
        std::vector<std::unique_ptr<AudioClipRuntime>> clips;
        float volume = 1.0f;
        float pan = 0.0f;
        bool playable = true;
    };
    std::map<int, std::unique_ptr<AudioTrackRuntime>> audioTrackRuntimes;
    juce::AudioFormatManager audioFormatManager;
    juce::TimeSliceThread audioReadAheadThread { "Audio clip read ahead" };
    juce::CriticalSection trackRuntimeLock;
    std::vector<ActiveNote> activeNotes;
    std::map<int, int> activePreviewPitches;
    int velocityPreviewPitch = -1;
    int velocityPreviewTrackId = 0;
    float velocityPreviewMidiVelocity = 1.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> velocityPreviewGain { 1.0f };
    juce::CriticalSection playbackLock;

    juce::ListenerList<PlayheadListener> playheadListeners;

    void processSequencerBlock(int numSamples, juce::MidiBuffer& outMidi);
    void stopAllActiveNotes();
    void mixAudioTracks(float* const* outputChannelData, int numOutputChannels,
                        int numSamples, double blockStartBeat, juce::AudioBuffer<float>& tempBuffer,
                        int outputOffset = 0);
    void applyVelocityPreviewGain(juce::AudioBuffer<float>& buffer, int trackId);
    TrackRuntime* findTrackRuntime(int trackId);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};

} // namespace Harmonizer
