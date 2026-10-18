#pragma once

#include "ModeAnalyzer.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace Harmonizer
{

inline int runModeAnalyzerSelfTests()
{
    const auto logFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                             .getSiblingFile("mode_colour_tests.log");
    logFile.deleteFile();
    int failures = 0;
    auto check = [&](const char* name, bool passed) {
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", name);
        logFile.appendText(juce::String(passed ? "[PASS] " : "[FAIL] ") + name + "\n");
        if (!passed) ++failures;
    };
    auto note = [](int pitch) {
        NoteEvent result;
        result.pitch = pitch;
        result.startBeat = 0.0;
        result.lengthBeats = 1.0;
        result.velocity = 0.8f;
        return result;
    };

    const auto majorLayout = ScaleDefinitions::computeLayout(0, "Ionian (Natural Major)");
    auto preservesColourWhenSelected = [&](int pitch, ColoringMode mode,
                                            juce::Colour trackColour = juce::Colours::purple,
                                            juce::Colour clipColour = juce::Colours::orange) {
        const auto base = ScaleDefinitions::getNoteColor(majorLayout, pitch, mode, false,
                                                         trackColour, clipColour);
        const auto selected = ScaleDefinitions::getNoteColor(majorLayout, pitch, mode, true,
                                                             trackColour, clipColour);
        const float hueDistance = std::abs(base.getHue() - selected.getHue());
        return base != selected && selected.getPerceivedBrightness() > base.getPerceivedBrightness()
            && std::min(hueDistance, 1.0f - hueDistance) < 0.025f;
    };
    check("selected notes preserve scale-adaptive tonic, scale, and out-of-scale colours",
          preservesColourWhenSelected(60, ColoringMode::ScaleAdaptive)
          && preservesColourWhenSelected(62, ColoringMode::ScaleAdaptive)
          && preservesColourWhenSelected(61, ColoringMode::ScaleAdaptive)
          && ScaleDefinitions::getNoteColor(majorLayout, 60, ColoringMode::ScaleAdaptive, true)
              != ScaleDefinitions::getNoteColor(majorLayout, 61, ColoringMode::ScaleAdaptive, true));
    check("selected notes preserve function, uniform, track, and clip colour modes",
          preservesColourWhenSelected(60, ColoringMode::DegreeFunction)
          && preservesColourWhenSelected(65, ColoringMode::DegreeFunction)
          && preservesColourWhenSelected(67, ColoringMode::DegreeFunction)
          && preservesColourWhenSelected(60, ColoringMode::Uniform)
          && preservesColourWhenSelected(60, ColoringMode::TrackColor)
          && preservesColourWhenSelected(60, ColoringMode::ClipColor));
    check("IV, V, and VI retain distinct function colours",
          ScaleDefinitions::getNoteColor(majorLayout, 65, ColoringMode::DegreeFunction)
              == juce::Colour(0xff06b6d4)
          && ScaleDefinitions::getNoteColor(majorLayout, 67, ColoringMode::DegreeFunction)
              == juce::Colour(0xff0ea5e9)
          && ScaleDefinitions::getNoteColor(majorLayout, 69, ColoringMode::DegreeFunction)
              == juce::Colour(0xff10b981));

    const auto triad = ModeAnalyzer::analyzeSelection({ note(60), note(64), note(67) }, 48);
    check("C major triad has several exact modal possibilities",
          triad.distinctPitchClassCount == 3 && triad.exactMatchCount > 1);
    bool foundIonian = false;
    for (const auto& candidate : triad.topCandidates)
        if (candidate.tonicPc == 0 && candidate.scaleName.startsWithIgnoreCase("Ionian")
            && candidate.inScaleRatio == 100.0f)
            foundIonian = true;
    check("C Ionian appears as an exact candidate", foundIonian);

    const auto miyako = ModeAnalyzer::analyzeSelection(
        { note(60), note(61), note(65), note(67), note(68) }, 48);
    bool foundMiyako = false;
    for (const auto& candidate : miyako.topCandidates)
        if (candidate.tonicPc == 0 && candidate.scaleName == "Miyako-bushi"
            && candidate.inScaleRatio == 100.0f)
            foundMiyako = true;
    check("Chordior Miyako-bushi scale matches its five selected tones", foundMiyako);

    const auto singlePitch = ModeAnalyzer::analyzeSelection({ note(60), note(72) });
    check("octave doubles do not pretend to identify a mode",
          singlePitch.distinctPitchClassCount == 1 && singlePitch.topCandidates.empty());

    std::vector<NoteEvent> chromatic;
    for (int pitch = 60; pitch < 72; ++pitch)
        chromatic.push_back(note(pitch));
    const auto chromaticResult = ModeAnalyzer::analyzeSelection(chromatic, 5);
    check("chromatic selection reports partial matches without false exact hits",
          chromaticResult.exactMatchCount == 0
          && !chromaticResult.topCandidates.empty()
          && chromaticResult.topCandidates.front().inScaleRatio < 100.0f);

    const auto phrygian = ModeAnalyzer::analyzeSelection(
        { note(60), note(61), note(63), note(65), note(67), note(68), note(70) }, 5);
    check("characteristic flat second and tonic bass favour C Phrygian",
          !phrygian.topCandidates.empty()
          && phrygian.topCandidates.front().tonicPc == 0
          && phrygian.topCandidates.front().scaleName.startsWithIgnoreCase("Phrygian"));

    const auto noNotes = ModeAnalyzer::analyzeSelection({});
    check("empty selection produces no candidates", noNotes.topCandidates.empty());

    std::vector<NoteEvent> chronologicalNotes;
    for (int i = 0; i < 6; ++i)
    {
        auto n = note(60 + (i * 3) % 12);
        n.startBeat = (double)i;
        n.lengthBeats = 0.5 + (double)i * 0.25;
        chronologicalNotes.push_back(n);
    }
    auto reversedNotes = chronologicalNotes;
    std::reverse(reversedNotes.begin(), reversedNotes.end());
    const auto orderedTrack = ModeAnalyzer::analyze(chronologicalNotes, 3);
    const auto reversedTrack = ModeAnalyzer::analyze(reversedNotes, 3);
    check("whole-track analysis uses timeline order rather than clip storage order",
          !orderedTrack.topCandidates.empty() && !reversedTrack.topCandidates.empty()
          && orderedTrack.bestTonicPc == reversedTrack.bestTonicPc
          && orderedTrack.bestScale == reversedTrack.bestScale
          && std::abs(orderedTrack.confidence - reversedTrack.confidence) < 0.0001f);

    ScaleDefinitions::registerCustomScale("Extended Safety Test", { 0, 14, -1 },
                                          { "I", "II", "VII" });
    const auto custom = ModeAnalyzer::analyzeSelection({ note(60), note(62), note(71) });
    const auto customTrack = ModeAnalyzer::analyze({ note(60), note(62), note(71) }, 5);
    check("custom intervals outside one octave are normalized safely",
          !custom.topCandidates.empty() && !customTrack.topCandidates.empty());
    return failures == 0 ? 0 : 1;
}

} // namespace Harmonizer
