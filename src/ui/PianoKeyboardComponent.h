#pragma once

#include "../core/MidiModel.h"
#include "../audio/AudioEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace Harmonizer
{

class PianoKeyboardComponent : public juce::Component
{
public:
    PianoKeyboardComponent(AudioEngine& engine, MidiModel& model);
    ~PianoKeyboardComponent() override = default;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    std::function<void(float deltaY)> onScrollRequested;

    void setNoteHeight(float height);
    float getNoteHeight() const { return noteHeight; }

    int yToPitch(float y) const;
    float pitchToY(int pitch) const;

    static bool isBlackKey(int pitch);

private:
    AudioEngine& audioEngine;
    MidiModel& midiModel;
    float noteHeight = 16.0f;
    int lastPreviewPitch = -1;

    int getPitchAt(float x, float y) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoKeyboardComponent)
};

} // namespace Harmonizer
