#include "BuiltinSynth.h"
#include <cmath>

namespace Harmonizer
{

BuiltinSynthVoice::BuiltinSynthVoice()
{
    juce::ADSR::Parameters params;
    params.attack  = 0.006f; // 6ms
    params.decay   = 0.250f; // 250ms
    params.sustain = 0.500f; // 50%
    params.release = 0.350f; // 350ms
    adsr.setParameters(params);
}

bool BuiltinSynthVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<BuiltinSynthSound*>(sound) != nullptr;
}

void BuiltinSynthVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    currentAngle = 0.0;
    harmonicAngle = 0.0;
    level = velocity * 0.25;
    tailOff = 0.0;

    auto cyclesPerSecond = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    auto cyclesPerSample = cyclesPerSecond / getSampleRate();
    angleDelta = cyclesPerSample * 2.0 * juce::MathConstants<double>::pi;
    harmonicDelta = angleDelta * 2.0;

    adsr.setSampleRate(getSampleRate());
    adsr.reset();
    adsr.noteOn();
}

void BuiltinSynthVoice::stopNote(float, bool allowTailOff)
{
    if (allowTailOff)
    {
        adsr.noteOff();
    }
    else
    {
        clearCurrentNote();
        adsr.reset();
    }
}

void BuiltinSynthVoice::pitchWheelMoved(int) {}
void BuiltinSynthVoice::controllerMoved(int, int) {}

void BuiltinSynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (angleDelta != 0.0)
    {
        while (--numSamples >= 0)
        {
            float env = adsr.getNextSample();
            if (!adsr.isActive())
            {
                clearCurrentNote();
                angleDelta = 0.0;
                break;
            }

            // Warm EP/Piano harmonic blend: fundamental + softer 2nd harmonic
            auto currentSample = (float)((std::sin(currentAngle) * 0.75 + std::sin(harmonicAngle) * 0.25) * level * env);

            for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                outputBuffer.addSample(i, startSample, currentSample);

            currentAngle += angleDelta;
            harmonicAngle += harmonicDelta;
            ++startSample;
        }
    }
}

//==============================================================================
BuiltinSynth::BuiltinSynth()
{
    for (int i = 0; i < 32; ++i)
        synth.addVoice(new BuiltinSynthVoice());

    synth.addSound(new BuiltinSynthSound());
}

void BuiltinSynth::prepare(double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
}

void BuiltinSynth::renderNextBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages, int startSample, int numSamples)
{
    juce::MidiBuffer combined;
    combined.addEvents(midiMessages, startSample, numSamples, 0);

    {
        const juce::ScopedLock sl(lock);
        combined.addEvents(pendingMidi, startSample, numSamples, 0);
        pendingMidi.clear();
    }

    synth.renderNextBlock(buffer, combined, startSample, numSamples);
}

void BuiltinSynth::allNotesOff()
{
    synth.allNotesOff(0, false);
    const juce::ScopedLock sl(lock);
    pendingMidi.clear();
}

void BuiltinSynth::noteOn(int channel, int midiNote, float velocity)
{
    const juce::ScopedLock sl(lock);
    pendingMidi.addEvent(juce::MidiMessage::noteOn(channel, midiNote, velocity), 0);
}

void BuiltinSynth::noteOff(int channel, int midiNote)
{
    const juce::ScopedLock sl(lock);
    pendingMidi.addEvent(juce::MidiMessage::noteOff(channel, midiNote), 0);
}

} // namespace Harmonizer
