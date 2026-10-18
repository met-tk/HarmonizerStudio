#pragma once

#include "../core/MidiModel.h"
#include "../core/Localization.h"
#include "../audio/AudioEngine.h"
#include "CubaseTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace Harmonizer
{

class TransportBar : public juce::Component,
                     public MidiModel::Listener,
                     public AudioEngine::PlayheadListener,
                     public Localization::Listener,
                     public CubaseTheme::Listener
{
public:
    TransportBar(MidiModel& model, AudioEngine& engine, std::function<void()> onOpenPreferences, class TrackModel* tm = nullptr);
    ~TransportBar() override;

    void themeChanged() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Listeners
    void projectSettingsChanged() override;
    void playheadMoved(double beatPosition) override;
    void playbackStateChanged(bool isPlaying) override;
    void languageChanged() override;
    void refreshMetronomeState();

private:
    MidiModel& midiModel;
    AudioEngine& audioEngine;
    class TrackModel* trackModel = nullptr;
    std::function<void()> openPreferencesCallback;

    // Custom Vector Transport Button (alias to DAWIconButton)
    using TransportButton = DAWIconButton;

    // Transport buttons
    TransportButton btnRewind { DAWIcon::Rewind, juce::String::fromUTF8(u8"回到起点 (Rewind) [Home]") };
    TransportButton btnPlay   { DAWIcon::Play,   juce::String::fromUTF8(u8"播放 / 暂停 (Play / Pause) [Space]") };
    TransportButton btnStop   { DAWIcon::Stop,   juce::String::fromUTF8(u8"停止 (Stop) [Numpad 0]") };
    TransportButton btnLoop   { DAWIcon::Loop,   juce::String::fromUTF8(u8"循环播放 (Cycle / Loop) [/]") };
    juce::TextButton btnMetronome { "Click" };
    juce::TextButton btnMetronomeSound { "Sound" };

    // BPM & TimeSig editors
    juce::Label lblBpm;
    juce::Label lblTimeSig;

    // Timecode display strings
    juce::String currentBarBeatTimecode = "001 . 01 . 01 . 000";
    juce::String currentSecondsTimecode = "00:00:00.000";

    void updateTimecodeStrings(double beat);
    void updatePlayButtonState(bool isPlaying);
    void showTimeSigPopup();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace Harmonizer
