#pragma once

#include "Reharmonizer.h"
#include "ChordClassifier.h"
#include "NoteEditOperations.h"
#include "KeyCommandManager.h"
#include "UndoActions.h"
#include <array>
#include <cstdio>
#include <set>

namespace Harmonizer
{

inline int runReharmonizerSelfTests()
{
    const auto logFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                             .getSiblingFile("reharmonization_tests.log");
    logFile.deleteFile();
    int failures = 0;
    auto check = [&](const char* name, bool passed) {
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", name);
        logFile.appendText(juce::String(passed ? "[PASS] " : "[FAIL] ") + name + "\n");
        if (!passed) ++failures;
    };
    auto note = [](int64_t id, int pitch, double beat, bool selected = true) {
        NoteEvent n;
        n.id = id;
        n.pitch = pitch;
        n.startBeat = beat;
        n.lengthBeats = 1.0;
        n.velocity = 0.6f;
        n.isSelected = selected;
        return n;
    };
    auto pcs = [](const std::vector<NoteEvent>& notes) {
        std::set<int> classes;
        for (const auto& n : notes) classes.insert(n.pitch % 12);
        return classes;
    };
    auto transformed = [](const MidiModel& model, const ReharmonizationResult& plan) {
        auto notes = model.getNotes();
        for (auto& n : notes)
            for (const auto& changed : plan.changedNotes)
                if (changed.id == n.id) { n.pitch = changed.pitch; break; }
        return notes;
    };

    std::vector<NoteEvent> chromaticSelection;
    for (int pitch = 60; pitch < 72; ++pitch)
        chromaticSelection.push_back(note(700 + pitch, pitch, 0.0));
    const auto chromaticChord = ChordClassifier::identifyChord(chromaticSelection);
    check("a chromatic octave is not named as an eleventh chord",
          chromaticChord.confidence == 0.0 && chromaticChord.fullName == "None");
    const auto repeatedChord = ChordClassifier::identifyChord({
        note(801, 60, 0.0), note(802, 64, 0.0), note(803, 67, 0.0),
        note(804, 60, 8.0), note(805, 64, 8.5), note(806, 67, 9.0) });
    check("all selected notes form one chord across distant onsets and repeated pitches",
          repeatedChord.confidence >= 0.5 && repeatedChord.fullName == "C");
    const auto screenshotChord = ChordClassifier::identifyChord({
        note(830, 67, 0.0), note(831, 69, 0.5), note(832, 72, 1.0),
        note(833, 74, 2.0), note(834, 67, 2.5), note(835, 79, 3.0) });
    check("selected G A C D G G notes are recognized as one chord",
          screenshotChord.confidence >= 0.5 && screenshotChord.fullName == "Gsus4(9)"
          && screenshotChord.rootPitchClass == 7);
    const auto progressionGroups = ChordClassifier::identifyChordGroups({
        note(801, 60, 0.0), note(802, 64, 0.0), note(803, 67, 0.0),
        note(804, 65, 2.0), note(805, 69, 2.0), note(806, 72, 2.0) },
        4.0, [](double) { return ChordAnalysisContext {}; });
    check("a selection across chord positions identifies each chord in order",
          progressionGroups.size() == 2
          && progressionGroups[0].chord.rootPitchClass == 0
          && progressionGroups[0].chord.confidence >= 0.5
          && progressionGroups[1].chord.rootPitchClass == 5
          && progressionGroups[1].chord.confidence >= 0.5);
    const auto modeChangingGroups = ChordClassifier::identifyChordGroups({
        note(801, 60, 0.0), note(802, 64, 0.0), note(803, 67, 0.0),
        note(804, 65, 2.0), note(805, 69, 2.0), note(806, 72, 2.0) },
        4.0, [](double beat) {
            return ChordAnalysisContext { beat >= 2.0 ? 5 : 0, "Ionian (Natural Major)" };
        });
    check("each chord position uses the mode active at its own beat",
          modeChangingGroups.size() == 2
          && modeChangingGroups[0].chord.romanNumeral == "I"
          && modeChangingGroups[1].chord.romanNumeral == "I");
    const auto staggeredProgression = ChordClassifier::identifyChordGroups({
        note(820, 60, 0.0), note(821, 64, 0.5), note(822, 67, 1.0),
        note(823, 65, 4.0), note(824, 69, 4.5), note(825, 72, 5.0) },
        4.0, [](double) { return ChordAnalysisContext {}; });
    check("arpeggios on separate bars remain separately identifiable",
          staggeredProgression.size() == 2
          && staggeredProgression[0].chord.rootPitchClass == 0
          && staggeredProgression[1].chord.rootPitchClass == 5);
    auto mixedSelection = chromaticSelection;
    mixedSelection.push_back(note(900, 67, 4.0));
    mixedSelection.push_back(note(901, 71, 4.0));
    mixedSelection.push_back(note(902, 74, 4.0));
    const auto mixedGroups = ChordClassifier::identifyChordGroups(mixedSelection,
        4.0, [](double) { return ChordAnalysisContext {}; });
    check("an invalid vertical pitch cluster does not hide a later valid chord",
          mixedGroups.size() == 2
          && mixedGroups[0].chord.confidence == 0.0
          && mixedGroups[1].chord.rootPitchClass == 7
          && mixedGroups[1].chord.confidence >= 0.5);
    const auto arpeggioChord = ChordClassifier::identifyChord({
        note(807, 60, 0.0), note(808, 64, 0.5), note(809, 67, 1.0) });
    const auto legitimateEleventh = ChordClassifier::identifyChord({
        note(810, 60, 0.0), note(811, 64, 0.0), note(812, 67, 0.0),
        note(813, 70, 0.0), note(814, 74, 0.0), note(815, 77, 0.0) });
    check("short arpeggios and genuine extended chords remain identifiable",
          arpeggioChord.confidence >= 0.5 && arpeggioChord.rootPitchClass == 0
          && legitimateEleventh.confidence >= 0.5
          && legitimateEleventh.fullName != "None");

    const auto& keys = KeyCommandManager::getInstance();
    const auto* up = keys.findCommand(Cmd_OctaveUp);
    const auto* down = keys.findCommand(Cmd_OctaveDown);
    check("preferences expose Shift+Up and Shift+Down as octave shortcuts",
          up != nullptr && down != nullptr
          && up->defaultKeys == std::vector<juce::KeyPress>{
              juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0) }
          && down->defaultKeys == std::vector<juce::KeyPress>{
              juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0) });

    MidiModel model;
    TrackModel tracks;
    auto* clip = tracks.getActiveClip();
    clip->notes = { note(1, 60, 0.0), note(2, 64, 0.0), note(3, 67, 0.0),
                    note(4, 73, 2.0, false) };
    model.setAllNotes(clip->notes);
    const auto original = model.getNotes();
    const auto diatonic = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::DiatonicCommonTone);
    const auto diatonicNotes = transformed(model, diatonic);
    std::set<int> common;
    const auto substitutePcs = pcs({ diatonicNotes[0], diatonicNotes[1], diatonicNotes[2] });
    for (int tone : substitutePcs)
        if (pcs({ original[0], original[1], original[2] }).count(tone)) common.insert(tone);
    check("diatonic substitute shares at least two tones and stays in key",
          diatonic.succeeded() && diatonic.groupCount == 1 && diatonic.hasChanges()
          && substitutePcs != std::set<int>({ 0, 4, 7 }) && common.size() >= 2
          && std::all_of(substitutePcs.begin(), substitutePcs.end(), [](int p) {
                 return std::set<int>({ 0, 2, 4, 5, 7, 9, 11 }).count(p) != 0;
             })
          && diatonicNotes[3].pitch == 73);

    const auto parallel = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::ParallelMode);
    const auto parallelNotes = transformed(model, parallel);
    check("parallel mode turns C major into C minor without moving notes in time",
          parallel.succeeded() && pcs({ parallelNotes[0], parallelNotes[1], parallelNotes[2] })
              == std::set<int>({ 0, 3, 7 })
          && parallelNotes[0].startBeat == original[0].startBeat
          && parallelNotes[1].velocity == original[1].velocity
          && parallelNotes[3].pitch == original[3].pitch);

    model.setAllNotes({ note(830, 67, 0.0), note(831, 69, 0.5),
                        note(832, 72, 1.0), note(833, 74, 2.0),
                        note(834, 67, 2.5), note(835, 79, 3.0) });
    bool screenshotCanReharmonize = false;
    bool screenshotTimingPreserved = true;
    for (int mode = 0; mode < kReharmonizationAlgorithmCount; ++mode)
    {
        const auto plan = Reharmonizer::prepare(model, tracks,
            static_cast<ReharmonizationAlgorithm>(mode));
        if (!plan.hasChanges()) continue;
        const auto after = transformed(model, plan);
        screenshotCanReharmonize = plan.groupCount == 1 && plan.groups.size() == 1
            && plan.groups[0].sourceChord == screenshotChord.fullName
            && after[0].pitch == after[4].pitch;
        for (size_t i = 0; i < after.size(); ++i)
            screenshotTimingPreserved = screenshotTimingPreserved
                && after[i].startBeat == model.getNotes()[i].startBeat
                && after[i].lengthBeats == model.getNotes()[i].lengthBeats;
        break;
    }
    check("named screenshot chord has a reharmonization mode that preserves duplicates and timing",
          screenshotCanReharmonize && screenshotTimingPreserved);

    juce::StringArray coverageGaps;
    int namedTemplates = 0;
    for (const auto& quality : ChordClassifier::getSupportedQualities())
    {
        std::vector<NoteEvent> notes;
        for (int interval : quality.requiredIntervals)
            notes.push_back(note(9000 + (int)notes.size(), 60 + interval, 0.0));
        model.setAllNotes(notes);
        const auto named = ChordClassifier::identifyChord(notes);
        if (named.confidence < 0.5) continue;
        ++namedTemplates;
        bool supported = false;
        for (int mode = 0; mode < kReharmonizationAlgorithmCount; ++mode)
            supported |= Reharmonizer::prepare(model, tracks,
                static_cast<ReharmonizationAlgorithm>(mode)).hasChanges();
        if (!supported) coverageGaps.add(quality.id + ": " + named.fullName);
    }
    if (!coverageGaps.isEmpty())
        logFile.appendText("[INFO] named chord coverage gaps: "
            + coverageGaps.joinIntoString(", ") + "\n");
    check("every recognized supported chord formula has a usable reharmonization mode",
          namedTemplates > 30 && coverageGaps.isEmpty());

    model.setAllNotes({ note(301, 64, 0.0), note(302, 67, 0.0), note(303, 72, 0.0) });
    const auto invertedBorrowing = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::ParallelMode);
    check("first-inversion tonic is recognized and borrowed on the same root",
          invertedBorrowing.succeeded()
          && pcs(transformed(model, invertedBorrowing)) == std::set<int>({ 0, 3, 7 }));
    model.setAllNotes({ note(1, 60, 0.0), note(2, 64, 0.0), note(3, 67, 0.0),
                        note(4, 73, 2.0, false) });

    const auto mediant = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::ChromaticMediant);
    const auto mediantNotes = transformed(model, mediant);
    const auto mediantPcs = pcs({ mediantNotes[0], mediantNotes[1], mediantNotes[2] });
    bool chromatic = false, shared = false;
    for (int tone : mediantPcs)
    {
        chromatic = chromatic || !std::set<int>({ 0, 2, 4, 5, 7, 9, 11 }).count(tone);
        shared = shared || std::set<int>({ 0, 4, 7 }).count(tone);
    }
    check("chromatic mediant keeps a common tone and adds chromatic color",
          mediant.succeeded() && mediant.hasChanges() && chromatic && shared);

    model.setAllNotes({ note(601, 60, 0.0), note(602, 64, 0.0), note(603, 67, 0.0) });
    const auto tonicFunction = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::FunctionalSubstitution);
    check("functional substitution maps major tonic to relative minor",
          tonicFunction.succeeded() && tonicFunction.hasChanges()
          && pcs(transformed(model, tonicFunction)) == std::set<int>({ 0, 4, 9 }));
    model.setAllNotes({ note(604, 65, 0.0), note(605, 69, 0.0), note(606, 72, 0.0) });
    const auto predominantFunction = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::FunctionalSubstitution);
    check("functional substitution maps fourth to second degree",
          predominantFunction.succeeded()
          && pcs(transformed(model, predominantFunction)) == std::set<int>({ 2, 5, 9 }));
    model.setAllNotes({ note(607, 67, 0.0), note(608, 71, 0.0),
                        note(609, 74, 0.0), note(610, 77, 0.0) });
    const auto backdoor = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::BackdoorDominant);
    check("backdoor substitutes V7 with flat-VII7 and keeps guide tones",
          backdoor.succeeded() && backdoor.hasChanges()
          && pcs(transformed(model, backdoor)) == std::set<int>({ 2, 5, 8, 10 })
          && backdoor.groups.front().commonTones >= 2);
    model.setAllNotes({ note(611, 60, 0.0), note(612, 64, 0.0), note(613, 67, 0.0) });
    const auto invalidBackdoor = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::BackdoorDominant);
    check("backdoor refuses a non-dominant source without changing notes",
          !invalidBackdoor.succeeded() && invalidBackdoor.changedNotes.empty());
    const auto smoothDiatonic = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::SmoothDiatonic);
    check("smooth diatonic mode offers a clear alternative for an ordinary triad",
          smoothDiatonic.succeeded() && smoothDiatonic.hasChanges()
          && smoothDiatonic.groups.size() == 1);
    check("classifier recognises common suspended and added-tone chords",
          ChordClassifier::identifyChordFromPitches({ 60, 62, 65, 67, 70 },
              0, "Ionian (Natural Major)").quality == "9sus4"
          && ChordClassifier::identifyChordFromPitches({ 60, 62, 63, 67 },
              0, "Aeolian (Natural Minor)").quality == "m(add9)");
    const auto majorNine = ChordClassifier::identifyChordFromPitches(
        { 60, 64, 67, 71, 74 }, 0, "Ionian (Natural Major)");
    const auto minorEleven = ChordClassifier::identifyChordFromPitches(
        { 60, 63, 67, 70, 74, 77 }, 0, "Aeolian (Natural Minor)");
    const auto dominantSharpNine = ChordClassifier::identifyChordFromPitches(
        { 59, 63, 66, 69, 74 }, 0, "Ionian (Natural Major)");
    check("structural families follow guide tones rather than suffix text",
          majorNine.family == ChordFamily::Major && majorNine.seventhInterval == 11
          && majorNine.romanNumeral.startsWith("I")
          && minorEleven.family == ChordFamily::Minor && minorEleven.seventhInterval == 10
          && dominantSharpNine.family == ChordFamily::Dominant);
    bool extendedFamiliesAcrossKeys = true;
    const std::array<std::pair<std::vector<int>, ChordFamily>, 5> formulas {{
        { { 0, 4, 7, 11, 14 }, ChordFamily::Major },
        { { 0, 3, 7, 10, 14 }, ChordFamily::Minor },
        { { 0, 4, 7, 10, 14 }, ChordFamily::Dominant },
        { { 0, 4, 7, 10, 14, 17 }, ChordFamily::Dominant },
        { { 0, 4, 7, 10, 14, 17, 21 }, ChordFamily::Dominant }
    }};
    for (int tonic = 0; tonic < 12; ++tonic)
        for (const auto& [formula, family] : formulas)
            for (bool inverted : { false, true })
            {
                std::vector<int> pitches;
                for (int interval : formula)
                    pitches.push_back(48 + tonic + interval);
                if (inverted) pitches.front() += 12;
                const auto recognized = ChordClassifier::identifyChordFromPitches(
                    pitches, tonic, "Ionian (Natural Major)");
                extendedFamiliesAcrossKeys &= recognized.rootPitchClass == tonic
                    && recognized.family == family && recognized.confidence >= 0.5;
            }
    check("ninth to thirteenth chord families survive all keys and first inversions",
          extendedFamiliesAcrossKeys);
    const auto majorSixNine = ChordClassifier::identifyChordFromPitches(
        { 60, 64, 67, 69, 74 });
    const auto minorMajorSeven = ChordClassifier::identifyChordFromPitches(
        { 60, 63, 67, 71 });
    const auto halfDiminished = ChordClassifier::identifyChordFromPitches(
        { 60, 63, 66, 70 });
    const auto diminishedSeven = ChordClassifier::identifyChordFromPitches(
        { 60, 63, 66, 69 });
    const auto alteredDominant = ChordClassifier::identifyChordFromPitches(
        { 60, 64, 66, 70 });
    const auto suspendedNine = ChordClassifier::identifyChordFromPitches(
        { 60, 62, 65, 67, 70 });
    const auto thirdDyad = ChordClassifier::identifyChordFromPitches({ 60, 64 });
    check("sixth, diminished, altered and suspended qualities keep distinct properties",
          majorSixNine.family == ChordFamily::Major
          && majorSixNine.seventhInterval < 0
          && minorMajorSeven.family == ChordFamily::Minor
          && minorMajorSeven.seventhInterval == 11
          && halfDiminished.family == ChordFamily::HalfDiminished
          && diminishedSeven.family == ChordFamily::Diminished
          && diminishedSeven.seventhInterval == 9
          && alteredDominant.family == ChordFamily::Dominant
          && suspendedNine.family == ChordFamily::Suspended
          && thirdDyad.family == ChordFamily::Dyad);
    model.setAllNotes({ note(881, 55, 0.0), note(882, 59, 0.0),
                        note(883, 62, 0.0), note(884, 65, 0.0),
                        note(885, 69, 0.0), note(886, 72, 0.0) });
    const auto extendedTritone = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::TritoneSubstitution);
    const auto extendedTargetNotes = transformed(model, extendedTritone);
    const auto extendedTargetName = ChordClassifier::identifyChord(extendedTargetNotes).fullName;
    check("G11 tritone substitution retains six voices and previews its actual pitches",
          extendedTritone.succeeded() && extendedTritone.hasChanges()
          && extendedTargetNotes.size() == 6
          && pcs(extendedTargetNotes) == std::set<int>({ 1, 5, 7, 8, 9, 11 })
          && extendedTritone.groups.size() == 1
          && extendedTritone.groups.front().targetChord == extendedTargetName);
    model.setAllNotes({ note(887, 55, 0.0), note(888, 59, 0.0),
                        note(889, 62, 0.0), note(890, 65, 0.0),
                        note(891, 69, 0.0) });
    const auto extendedBackdoor = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::BackdoorDominant);
    check("backdoor substitution carries a dominant ninth into its target",
          extendedBackdoor.succeeded() && extendedBackdoor.hasChanges()
          && pcs(transformed(model, extendedBackdoor)) == std::set<int>({ 0, 2, 5, 8, 10 }));

    tracks.setModeTrackOverrideEnabled(false);
    model.setScaleName("Dorian");
    model.setAllNotes({ note(667, 60, 0.0), note(668, 63, 0.0), note(669, 67, 0.0) });
    const auto dorianFunction = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::FunctionalSubstitution);
    check("diminished sixth degree is not presented as a traditional relative substitute",
          !dorianFunction.succeeded() && dorianFunction.changedNotes.empty());
    tracks.setModeTrackOverrideEnabled(true);
    model.setScaleName("Ionian (Natural Major)");

    model.setAllNotes({ note(11, 67, 0.0), note(12, 71, 0.0), note(13, 74, 0.0),
                        note(14, 77, 0.0) });
    const auto tritone = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::TritoneSubstitution);
    check("G7 tritone substitute is Db7 with all four chord tones",
          tritone.succeeded() && tritone.hasChanges()
          && pcs(transformed(model, tritone)) == std::set<int>({ 1, 5, 8, 11 }));

    model.setAllNotes({ note(304, 71, 0.0), note(305, 74, 0.0),
                        note(306, 77, 0.0), note(307, 79, 0.0) });
    const auto invertedTritone = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::TritoneSubstitution);
    check("inverted dominant seventh still substitutes its harmonic root",
          invertedTritone.succeeded()
          && pcs(transformed(model, invertedTritone)) == std::set<int>({ 1, 5, 8, 11 }));

    model.setAllNotes({ note(15, 67, 0.0), note(16, 71, 0.0), note(17, 77, 0.0) });
    const auto shell = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::TritoneSubstitution);
    check("three-note dominant shell keeps three voices through tritone substitution",
          shell.succeeded() && pcs(transformed(model, shell)) == std::set<int>({ 1, 5, 11 }));

    bool tritoneAllKeys = true;
    for (int rootPc = 0; rootPc < 12; ++rootPc)
    {
        const int rootPitch = 60 + rootPc;
        model.setAllNotes({ note(101, rootPitch, 0.0), note(102, rootPitch + 4, 0.0),
                            note(103, rootPitch + 7, 0.0) });
        model.setAllNotes({ note(111, rootPitch, 0.0), note(112, rootPitch + 4, 0.0),
                            note(113, rootPitch + 7, 0.0), note(114, rootPitch + 10, 0.0) });
        const auto sub = Reharmonizer::prepare(model, tracks,
            ReharmonizationAlgorithm::TritoneSubstitution);
        const int subRoot = (rootPc + 6) % 12;
        const std::set<int> expectedSub { subRoot, (subRoot + 4) % 12,
                                          (subRoot + 7) % 12, (subRoot + 10) % 12 };
        tritoneAllKeys = tritoneAllKeys && sub.succeeded()
            && pcs(transformed(model, sub)) == expectedSub;
    }
    check("tritone substitution works in all twelve keys", tritoneAllKeys);

    model.setAllNotes({ note(201, 62, 0.0), note(202, 65, 0.0), note(203, 69, 0.0) });
    const auto borrowedDiminished = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::ParallelMode);
    check("parallel C minor borrows D diminished instead of blindly flipping chord quality",
          borrowedDiminished.succeeded()
          && pcs(transformed(model, borrowedDiminished)) == std::set<int>({ 2, 5, 8 })
          && borrowedDiminished.groups.size() == 1
          && borrowedDiminished.groups.front().targetChord == "Ddim");

    model.setAllNotes({ note(204, 69, 0.0), note(205, 72, 0.0), note(206, 76, 0.0) });
    const auto unavailableBorrowing = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::ParallelMode);
    check("parallel borrowing rejects roots absent from the parallel mode",
          !unavailableBorrowing.succeeded() && unavailableBorrowing.changedNotes.empty()
          && unavailableBorrowing.groups.size() == 1
          && !unavailableBorrowing.groups.front().supported);

    model.setAllNotes({ note(207, 67, 0.0), note(208, 71, 0.0),
                        note(209, 77, 0.0), note(210, 81, 0.0) });
    const auto alteredTritone = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::TritoneSubstitution);
    const auto alteredTargetNotes = transformed(model, alteredTritone);
    const auto alteredTargetName = ChordClassifier::identifyChord(alteredTargetNotes).fullName;
    check("tritone substitution preserves the source ninth pitch and previews its actual chord",
          alteredTritone.succeeded()
          && alteredTargetNotes.size() == 4
          && pcs(alteredTargetNotes) == std::set<int>({ 1, 5, 9, 11 })
          && alteredTritone.groups.size() == 1
          && alteredTritone.groups.front().targetChord == alteredTargetName
          && alteredTritone.groups.front().commonTones >= 2);

    model.setAllNotes({ note(211, 60, 0.0), note(212, 64, 0.0),
                        note(213, 67, 0.0), note(214, 71, 0.0) });
    const auto diatonicSeventh = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::DiatonicCommonTone);
    check("diatonic substitution retains a four-tone seventh-chord target",
          diatonicSeventh.succeeded() && diatonicSeventh.hasChanges()
          && pcs(transformed(model, diatonicSeventh)).size() == 4
          && diatonicSeventh.groups.front().commonTones >= 2);
    const auto chromaticSeventh = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::ChromaticMediant);
    check("chromatic mediant keeps major seventh quality and previews its direction",
          chromaticSeventh.succeeded() && chromaticSeventh.hasChanges()
          && chromaticSeventh.groups.front().targetChord.contains("maj7")
          && std::abs(chromaticSeventh.groups.front().rootMotion) >= 3
          && std::abs(chromaticSeventh.groups.front().rootMotion) <= 4);

    model.setAllNotes({ note(215, 67, 0.0), note(216, 71, 0.0),
                        note(217, 74, 0.0), note(218, 78, 0.0) });
    const auto majorSeventhNotDominant = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::TritoneSubstitution);
    check("tritone substitution rejects a major seventh without dominant guide tones",
          !majorSeventhNotDominant.succeeded()
          && majorSeventhNotDominant.changedNotes.empty());

    tracks.setModeTrackOverrideEnabled(false);
    model.setScaleName("Aeolian (Natural Minor)");
    model.setAllNotes({ note(219, 60, 0.0), note(220, 63, 0.0), note(221, 67, 0.0) });
    const auto borrowedMajor = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::ParallelMode);
    check("parallel minor mode borrows its major tonic",
          borrowedMajor.succeeded()
          && pcs(transformed(model, borrowedMajor)) == std::set<int>({ 0, 4, 7 }));

    model.setScaleName("Major Pentatonic");
    const auto nonHeptatonic = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::DiatonicCommonTone);
    check("diatonic common-tone mode refuses non-heptatonic scales",
          !nonHeptatonic.succeeded() && nonHeptatonic.changedNotes.empty());
    tracks.setModeTrackOverrideEnabled(true);
    model.setScaleName("Ionian (Natural Major)");

    model.setAllNotes({ note(308, 69, 10.0), note(309, 72, 10.0), note(310, 76, 10.0) });
    const auto laterSectionBorrowing = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::ParallelMode);
    check("borrowing follows the mode-track event at the selected chord's beat",
          laterSectionBorrowing.succeeded()
          && pcs(transformed(model, laterSectionBorrowing)) == std::set<int>({ 1, 4, 9 }));

    model.setAllNotes({ note(21, 60, 0.0), note(22, 64, 0.0), note(23, 67, 0.0),
                        note(24, 60, 4.0), note(25, 64, 4.0), note(26, 67, 4.0) });
    const auto twoGroups = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::DiatonicCommonTone);
    const auto twoGroupNotes = transformed(model, twoGroups);
    bool retainedTiming = twoGroupNotes.size() == model.getNotes().size();
    for (size_t i = 0; i < twoGroupNotes.size(); ++i)
        retainedTiming = retainedTiming
            && twoGroupNotes[i].id == model.getNotes()[i].id
            && twoGroupNotes[i].startBeat == model.getNotes()[i].startBeat
            && twoGroupNotes[i].lengthBeats == model.getNotes()[i].lengthBeats;
    check("repeated selected events are reharmonized as one chord without changing timing",
          twoGroups.succeeded() && twoGroups.groupCount == 1
          && twoGroups.groups.size() == 1
          && twoGroups.groups.front().sourceChord.isNotEmpty()
          && twoGroups.groups.front().targetChord.isNotEmpty()
          && twoGroups.hasChanges() && retainedTiming
          && twoGroupNotes[0].pitch == twoGroupNotes[3].pitch
          && twoGroupNotes[1].pitch == twoGroupNotes[4].pitch
          && twoGroupNotes[2].pitch == twoGroupNotes[5].pitch);

    model.setAllNotes({ note(401, 60, 0.0), note(402, 64, 0.5),
                        note(403, 67, 1.0) });
    const auto arpeggio = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::ParallelMode);
    const auto arpeggioNotes = transformed(model, arpeggio);
    check("staggered chord notes form one reharmonizable arpeggio",
          arpeggio.succeeded() && arpeggio.groupCount == 1
          && pcs(arpeggioNotes) == std::set<int>({ 0, 3, 7 })
          && arpeggioNotes[0].startBeat == 0.0
          && arpeggioNotes[1].startBeat == 0.5
          && arpeggioNotes[2].startBeat == 1.0);
    model.setAllNotes({ note(404, 60, 0.0), note(405, 64, 0.5), note(406, 67, 1.0),
                        note(407, 60, 4.0), note(408, 64, 4.5), note(409, 67, 5.0) });
    const auto arpeggioPhrases = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::DiatonicCommonTone);
    check("distant selected arpeggio notes still make one harmony",
          arpeggioPhrases.succeeded() && arpeggioPhrases.groupCount == 1
          && arpeggioPhrases.groups.size() == 1);
    model.setAllNotes({ note(410, 60, 0.0), note(411, 64, 0.25), note(412, 67, 0.5),
                        note(413, 60, 0.75), note(414, 64, 1.0), note(415, 67, 1.25) });
    const auto adjacentArpeggios = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::ParallelMode);
    const auto adjacentNotes = transformed(model, adjacentArpeggios);
    check("a contour turn does not split the selected chord",
          adjacentArpeggios.succeeded() && adjacentArpeggios.groupCount == 1
          && pcs(adjacentNotes) == std::set<int>({ 0, 3, 7 }));
    model.setAllNotes({ note(416, 60, 0.0), note(417, 64, 0.25), note(418, 67, 0.5),
                        note(419, 60, 0.75), note(420, 64, 1.0), note(421, 67, 1.25) });
    const auto repeatedArpeggio = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::ParallelMode);
    check("repeated arpeggio cycles keep one selected chord",
          repeatedArpeggio.succeeded() && repeatedArpeggio.groupCount == 1
          && repeatedArpeggio.groups.size() == 1);
    model.setAllNotes({ note(422, 60, 3.0), note(423, 64, 3.25),
                        note(424, 67, 3.5), note(425, 71, 4.0) });
    const auto extendedAcrossBar = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::ParallelMode);
    check("a final seventh across a bar line remains with its arpeggio",
          extendedAcrossBar.succeeded() && extendedAcrossBar.groupCount == 1
          && pcs(transformed(model, extendedAcrossBar)) == std::set<int>({ 0, 3, 7, 10 }));
    model.setAllNotes({ note(21, 60, 0.0), note(22, 64, 0.0), note(23, 67, 0.0),
                        note(24, 60, 4.0), note(25, 64, 4.0), note(26, 67, 4.0) });

    const auto invalidTritone = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::TritoneSubstitution);
    check("unsupported source harmony fails without a partial edit plan",
          !invalidTritone.succeeded() && !invalidTritone.hasChanges()
          && invalidTritone.changedNotes.empty());

    model.setAllNotes({ note(51, 67, 0.0), note(52, 71, 0.0), note(53, 74, 0.0),
                        note(54, 77, 0.0), note(55, 60, 4.0), note(56, 64, 4.0),
                        note(57, 69, 4.0), note(58, 70, 4.0) });
    const auto laterFailure = Reharmonizer::prepare(model, tracks,
        ReharmonizationAlgorithm::TritoneSubstitution);
    check("an unsupported combined pitch set never partially edits the selection",
          !laterFailure.succeeded() && laterFailure.changedNotes.empty()
          && laterFailure.groups.size() == 1
          && !laterFailure.groups.front().supported
          && model.getNotes()[0].pitch == 67 && model.getNotes()[4].pitch == 60);

    model.setAllNotes({ note(21, 60, 0.0), note(22, 64, 0.0), note(23, 67, 0.0),
                        note(24, 60, 4.0), note(25, 64, 4.0), note(26, 67, 4.0) });

    juce::UndoManager undo;
    const auto before = model.getNotes();
    const bool applied = UndoActionHelper::performAtomic(&undo,
        new ModifyClipNotesUndoAction(model, tracks, tracks.getActiveTrackId(),
                                      tracks.getActiveClipId(), before, twoGroupNotes),
        "Reharmonize Selected Chords");
    check("reharmonization applies as one undo transaction",
          applied && model.getNotes().size() == before.size()
          && model.getNotes()[0].pitch == twoGroupNotes[0].pitch);
    undo.undo();
    check("undo restores the full selected chord", model.getNotes()[0].pitch == before[0].pitch
          && model.getNotes()[3].pitch == before[3].pitch);
    undo.redo();
    check("redo restores all reharmonized selected notes",
          model.getNotes()[0].pitch == twoGroupNotes[0].pitch
          && model.getNotes()[3].pitch == twoGroupNotes[3].pitch);

    model.setAllNotes({ note(31, 60, 0.0), note(32, 64, 0.0), note(33, 67, 0.0) });
    check("octave shortcut shifts all selected notes by twelve semitones",
          NoteEditOperations::transposeSelectedOctave(model, tracks, undo, 1)
          && model.getNotes()[0].pitch == 72 && model.getNotes()[1].pitch == 76
          && model.getNotes()[2].pitch == 79);
    undo.undo();
    check("octave shortcut is undone in one step",
          model.getNotes()[0].pitch == 60 && model.getNotes()[1].pitch == 64
          && model.getNotes()[2].pitch == 67);
    check("octave down is symmetric",
          NoteEditOperations::transposeSelectedOctave(model, tracks, undo, -1)
          && model.getNotes()[0].pitch == 48 && model.getNotes()[1].pitch == 52
          && model.getNotes()[2].pitch == 55);

    model.setAllNotes({ note(41, 120, 0.0), note(42, 60, 0.0) });
    check("octave overflow cancels all selected notes without clipping",
          !NoteEditOperations::transposeSelectedOctave(model, tracks, undo, 1)
          && model.getNotes()[0].pitch == 120 && model.getNotes()[1].pitch == 60);
    tracks.getActiveInstrumentTrack()->setLocked(true);
    check("locked instrument clip rejects reharmonization and octave edits",
          !Reharmonizer::prepare(model, tracks, ReharmonizationAlgorithm::ParallelMode).succeeded()
          && !NoteEditOperations::transposeSelectedOctave(model, tracks, undo, -1));

    std::printf("Reharmonization tests: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace Harmonizer
