#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace Harmonizer
{

class BuiltinSynthVoice : public juce::SynthesiserVoice
{
public:
    BuiltinSynthVoice();
    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int newPitchWheelValue) override;
    void controllerMoved(int controllerNumber, int newControllerValue) override;
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

private:
    double currentAngle = 0.0;
    double angleDelta = 0.0;
    double harmonicAngle = 0.0;
    double harmonicDelta = 0.0;
    double level = 0.0;
    double tailOff = 0.0;
    juce::ADSR adsr;
};

class BuiltinSynthSound : public juce::SynthesiserSound
{
public:
    BuiltinSynthSound() = default;
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

class BuiltinSynth
{
public:
    BuiltinSynth();
    ~BuiltinSynth() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void renderNextBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages, int startSample, int numSamples);
    void allNotesOff();

    void noteOn(int channel, int midiNote, float velocity);
    void noteOff(int channel, int midiNote);

private:
    juce::Synthesiser synth;
    juce::MidiBuffer pendingMidi;
    juce::CriticalSection lock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BuiltinSynth)
};

} // namespace Harmonizer
