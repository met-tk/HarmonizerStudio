#pragma once

#include "ChordClassifier.h"
#include "ChordEditing.h"
#include "Reharmonizer.h"
#include "SecondaryDominant.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
#include <utility>

namespace Harmonizer
{

// These fixtures are stated as pitches and expected music-theory results. They
// deliberately do not derive expected answers from getSupportedQualities().
inline int runChordTheorySelfTests()
{
    const auto logFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                             .getSiblingFile("chord_theory_tests.log");
    logFile.deleteFile();
    int failures = 0;
    int checks = 0;
    const auto check = [&](const juce::String& label, bool passed,
                           const juce::String& detail = {}) {
        ++checks;
        if (!passed) ++failures;
        const auto line = juce::String(passed ? "[PASS] " : "[FAIL] ") + label
            + (passed || detail.isEmpty() ? juce::String() : " (" + detail + ")");
        std::printf("%s\n", line.toRawUTF8());
        logFile.appendText(line + "\n");
    };
    const auto note = [](int64_t id, int pitch, double beat = 0.0) {
        NoteEvent n;
        n.id = id;
        n.pitch = pitch;
        n.startBeat = beat;
        n.lengthBeats = 0.75;
        n.velocity = 0.63f;
        n.isSelected = true;
        return n;
    };
    const auto pitchClasses = [](const std::vector<NoteEvent>& notes) {
        std::set<int> result;
        for (const auto& n : notes) result.insert((n.pitch % 12 + 12) % 12);
        return result;
    };
    const auto applyEdits = [](std::vector<NoteEvent> notes,
                               const std::vector<NoteEvent>& changed,
                               const std::vector<NoteEvent>& added = {},
                               const std::vector<int64_t>& removed = {}) {
        notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const auto& n) {
            return std::find(removed.begin(), removed.end(), n.id) != removed.end();
        }), notes.end());
        for (auto& current : notes)
            for (const auto& replacement : changed)
                if (current.id == replacement.id) { current = replacement; break; }
        notes.insert(notes.end(), added.begin(), added.end());
        return notes;
    };
    const auto preservesEvents = [](const std::vector<NoteEvent>& source,
                                     const std::vector<NoteEvent>& changed) {
        for (const auto& edit : changed)
        {
            const auto it = std::find_if(source.begin(), source.end(), [&](const auto& n) {
                return n.id == edit.id;
            });
            if (it == source.end() || it->startBeat != edit.startBeat
                || it->lengthBeats != edit.lengthBeats || it->velocity != edit.velocity
                || it->isMuted != edit.isMuted || it->isSelected != edit.isSelected)
                return false;
        }
        return true;
    };

    struct NamedFixture
    {
        const char* label;
        std::vector<int> pitches;
        const char* name;
        int root;
        int bass;
    };
    const std::vector<NamedFixture> fixtures {
        { "single C",                 { 60 },                         "C",               0, 0 },
        { "C power chord",            { 60, 67 },                     "C5",              0, 0 },
        { "C major",                  { 60, 64, 67 },                 "C",               0, 0 },
        { "C first inversion",        { 52, 55, 60 },                 "C/E",             0, 4 },
        { "C second inversion",       { 55, 60, 64 },                 "C/G",             0, 7 },
        { "A minor",                  { 57, 60, 64 },                 "Am",              9, 9 },
        { "D minor first inversion",  { 53, 57, 62 },                 "Dm/F",            2, 5 },
        { "G dominant seventh",       { 55, 59, 62, 65 },             "G7",              7, 7 },
        { "G7 first inversion",       { 59, 62, 65, 67 },             "G7/B",            7, 11 },
        { "C major seventh",         { 60, 64, 67, 71 },             "Cmaj7",           0, 0 },
        { "C major sixth",           { 60, 64, 67, 69 },             "C6",              0, 0 },
        { "C major six-nine",        { 60, 62, 64, 67, 69 },         "C6/9",            0, 0 },
        { "C minor sixth",           { 60, 63, 67, 69 },             "Cm6",             0, 0 },
        { "C minor-major seventh",   { 60, 63, 67, 71 },             "Cm(maj7)",        0, 0 },
        { "C augmented",             { 60, 64, 68 },                 "Caug",            0, 0 },
        { "C diminished",            { 60, 63, 66 },                 "Cdim",            0, 0 },
        { "B half diminished",        { 59, 62, 65, 69 },             "Bm7b5",          11, 11 },
        { "C diminished seventh",    { 60, 63, 66, 69 },             "Cdim7",           0, 0 },
        { "D suspended fourth",       { 62, 67, 69 },                 "Dsus4",           2, 2 },
        { "A suspended second",       { 57, 59, 64 },                 "Asus2",           9, 9 },
        { "C added ninth",            { 60, 62, 64, 67 },             "Cadd9",           0, 0 },
        { "C dominant ninth",        { 60, 64, 67, 70, 74 },         "C9",              0, 0 },
        { "C minor ninth",           { 60, 62, 63, 67, 70 },         "Cm9",             0, 0 },
        { "C suspended ninth",       { 60, 62, 65, 67, 70 },         "C9sus4",          0, 0 },
        { "C minor eleventh",        { 60, 62, 63, 65, 67, 70 },     "Cm11",            0, 0 },
        { "C dominant thirteenth",   { 60, 64, 67, 70, 74, 81 },     "C13",             0, 0 },
        { "C major thirteenth",      { 60, 62, 64, 67, 69, 71 },     "Cmaj13",          0, 0 },
        { "C major ninth",           { 60, 64, 67, 71, 74 },         "Cmaj9",           0, 0 },
        { "wide C major eleventh",   { 48, 52, 55, 59, 74, 77 },     "Cmaj11",          0, 0 },
        { "C Lydian major ninth",    { 60, 62, 64, 66, 67, 71 },     "Cmaj9#11",        0, 0 },
        { "C seventh flat ninth",    { 60, 61, 64, 67, 70 },         "C7b9",            0, 0 },
        { "C seventh flat fifth",    { 60, 64, 66, 70 },             "C7b5",            0, 0 },
        { "C seventh sharp eleven", { 60, 64, 66, 67, 70 },         "C7#11",           0, 0 },
        { "C seventh flat thirteen",{ 60, 64, 67, 68, 70 },         "C7b13",           0, 0 },
        { "C altered seventh",       { 60, 61, 64, 66, 70 },         "C7alt",           0, 0 },
        { "C thirteenth flat ninth", { 60, 61, 64, 67, 69, 70 },     "C13b9",           0, 0 },
        { "C major seventh no fifth",{ 60, 64, 71 },                 "Cmaj7(no5)",      0, 0 },
        { "G dominant no fifth",     { 55, 59, 65 },                 "G7(no5)",         7, 7 },
        { "C dominant no fifth",     { 60, 64, 70 },                 "C7(no5)",         0, 0 },
        { "E altered sharp ninth",   { 52, 56, 59, 62, 67 },         "E7#9",            4, 4 },
        { "E sharp ninth no fifth",  { 52, 56, 62, 67 },             "E7#9(no5)",       4, 4 },
        { "D triad over C triad",    { 48, 52, 55, 62, 66, 69 },     "D|C",             0, 0 },
        { "D triad over C minor",    { 48, 51, 55, 62, 66, 69 },     "D|Cm",            0, 0 },
        { "doubled C major",         { 48, 55, 60, 64, 67, 72 },     "C",               0, 0 },
    };
    for (const auto& fixture : fixtures)
    {
        const auto found = ChordClassifier::identifyChordFromPitches(fixture.pitches);
        const auto normalizedName = found.fullName.replace("(#9)", "#9")
            .replace("(b9)", "b9").replace("(#11)", "#11")
            .replace("(b13)", "b13").replace("(b5)", "b5");
        const auto actual = found.fullName + " root=" + juce::String(found.rootPitchClass)
            + " bass=" + juce::String(found.bassPitchClass);
        check(fixture.label,
              normalizedName == fixture.name && found.rootPitchClass == fixture.root
                  && found.bassPitchClass == fixture.bass,
              actual);
    }
    const auto& qualityCatalog = ChordClassifier::getSupportedQualities();
    const auto findQuality = [&](const juce::String& id) {
        return std::find_if(qualityCatalog.begin(), qualityCatalog.end(),
            [&](const auto& option) { return option.id == id; });
    };
    const bool exposesEnharmonicFormulae = findQuality("add4") != qualityCatalog.end()
        && findQuality("7(#5)") != qualityCatalog.end();
    check("public quality catalogue includes add-four and altered-fifth aliases",
          exposesEnharmonicFormulae);
    bool publishesThirteenthWithoutEleventh = true;
    for (const auto& [quality, voicing] :
         { std::pair<const char*, std::vector<int>>{ "13", { 0, 2, 4, 7, 9, 10 } },
           { "maj13", { 0, 2, 4, 7, 9, 11 } },
           { "m13", { 0, 2, 3, 7, 9, 10 } } })
    {
        const auto found = findQuality(quality);
        publishesThirteenthWithoutEleventh &= found != qualityCatalog.end();
        if (found != qualityCatalog.end())
            publishesThirteenthWithoutEleventh &= std::find(
                found->acceptedVoicings.begin(), found->acceptedVoicings.end(), voicing)
                != found->acceptedVoicings.end();
    }
    check("thirteenth catalogue publishes omit-eleventh accepted voicings",
          publishesThirteenthWithoutEleventh);

    // Formulae here are fixed independent examples, transposed through every
    // pitch class and moved to an inversion without changing the chord root.
    struct Formula { const char* label; std::vector<int> intervals; ChordFamily family; };
    const std::vector<Formula> formulas {
        { "major triad",        { 0, 4, 7 },             ChordFamily::Major },
        { "minor triad",        { 0, 3, 7 },             ChordFamily::Minor },
        { "dominant seventh",  { 0, 4, 7, 10 },         ChordFamily::Dominant },
        { "half diminished",   { 0, 3, 6, 10 },         ChordFamily::HalfDiminished },
        { "major ninth",       { 0, 4, 7, 11, 14 },     ChordFamily::Major },
        { "dominant ninth",    { 0, 4, 7, 10, 14 },     ChordFamily::Dominant },
        { "dominant thirteenth", { 0, 4, 7, 10, 14, 21 }, ChordFamily::Dominant },
    };
    for (const auto& formula : formulas)
        for (int root = 0; root < 12; ++root)
        {
            std::vector<int> pitches;
            for (int interval : formula.intervals)
                pitches.push_back(48 + root + interval);
            const auto named = ChordClassifier::identifyChordFromPitches(pitches, root);
            const auto label = juce::String(formula.label) + " root pc " + juce::String(root);
            check(label, named.rootPitchClass == root && named.family == formula.family
                          && named.confidence >= 0.5,
                  named.fullName);
            pitches.front() += 12;
            const auto inverted = ChordClassifier::identifyChordFromPitches(pitches, root);
            const int expectedBass = (root + formula.intervals[1]) % 12;
            bool correctInterpretation = inverted.rootPitchClass == root
                && inverted.isSlashChord && inverted.family == formula.family;
            if (formula.family == ChordFamily::HalfDiminished)
            {
                // A half-diminished seventh in first inversion shares every
                // sounding pitch with a minor-sixth chord on the bass.
                correctInterpretation |= std::any_of(inverted.alternatives.begin(),
                    inverted.alternatives.end(), [root](const auto& candidate) {
                        return candidate.rootPitchClass == root
                            && candidate.quality == "m7b5"
                            && candidate.fullName.containsChar('/');
                    });
            }
            const bool bassIsNamed = inverted.rootPitchClass == inverted.bassPitchClass
                ? !inverted.isSlashChord && !inverted.fullName.containsChar('/')
                : inverted.isSlashChord && inverted.fullName.containsChar('/');
            check(label + " inversion", inverted.bassPitchClass == expectedBass
                  && correctInterpretation && bassIsNamed
                  && inverted.confidence >= 0.5,
                  inverted.fullName);
        }

    const auto dyad = ChordClassifier::identifyChordFromPitches({ 60, 66 });
    check("tritone dyad is an uncertain interval, not a second", dyad.quality != "2"
          && dyad.fullName != "C2" && dyad.confidence < 0.5, dyad.fullName);
    const auto tritoneObjects = ChordClassifier::identifyChord(
        { note(10001, 60), note(10002, 66) });
    bool tritoneRolesUnknown = tritoneObjects.noteRoles.size() == 2;
    for (const auto& role : tritoneObjects.noteRoles)
        tritoneRolesUnknown &= role.role == ChordToneRole::Other
            && role.intervalFromRoot == -1 && role.polychordLayer == -1;
    check("uncertain tritone assigns no invented chord-tone role",
          tritoneObjects.status == ChordRecognitionStatus::Interval
              && tritoneRolesUnknown);
    const auto majorThirdDyad = ChordClassifier::identifyChordFromPitches({ 60, 64 });
    check("major-third dyad does not claim a complete major triad",
          majorThirdDyad.status == ChordRecognitionStatus::Interval
              && majorThirdDyad.confidence < 0.5 && majorThirdDyad.fullName != "C",
          majorThirdDyad.fullName);
    const auto unaltered = ChordClassifier::identifyChordFromPitches({ 60, 64, 70 });
    check("dominant guide tones do not invent altered notes",
          unaltered.rootPitchClass == 0 && unaltered.seventhInterval == 10
              && !unaltered.quality.containsIgnoreCase("alt")
              && unaltered.fullName == "C7(no5)"
              && std::find(unaltered.omittedIntervals.begin(),
                           unaltered.omittedIntervals.end(), 7)
                  != unaltered.omittedIntervals.end(), unaltered.fullName);
    const auto minorNoFifth = ChordClassifier::identifyChordFromPitches(
        { 60, 63, 70 }); // C-Eb-Bb: the defining Gb of Cø7 is absent.
    const bool inventsDiminishedFifth = std::any_of(
        minorNoFifth.alternatives.begin(), minorNoFifth.alternatives.end(),
        [](const auto& candidate) {
            return candidate.rootPitchClass == 0 && candidate.quality == "m7b5";
        });
    check("C minor seventh shell never invents a diminished fifth",
          minorNoFifth.rootPitchClass == 0
              && minorNoFifth.fullName == "Cm7(no5)"
              && !inventsDiminishedFifth,
          minorNoFifth.fullName);
    const auto actualSharpNine = ChordClassifier::identifyChordFromPitches(
        { 52, 56, 59, 62, 67 });
    check("sharp ninth label has a real sharp ninth",
          actualSharpNine.rootPitchClass == 4 && actualSharpNine.containsInterval(3)
              && actualSharpNine.containsInterval(4)
              && actualSharpNine.fullName.contains("#9"), actualSharpNine.fullName);
    const auto actualAltered = ChordClassifier::identifyChordFromPitches(
        { 60, 61, 64, 66, 70 });
    check("altered dominant requires real flat-nine and altered-fifth notes",
          actualAltered.fullName == "C7alt"
              && actualAltered.containsInterval(1)
              && actualAltered.containsInterval(6), actualAltered.fullName);
    std::vector<int> chromaticOctave;
    for (int pitch = 60; pitch < 72; ++pitch) chromaticOctave.push_back(pitch);
    const auto cluster = ChordClassifier::identifyChordFromPitches(chromaticOctave);
    check("all twelve pitch classes do not become a fictitious ordinary chord",
          cluster.confidence < 0.5 && cluster.status != ChordRecognitionStatus::Recognized,
          cluster.fullName);
    const auto chromaticSix = ChordClassifier::identifyChordFromPitches(
        { 60, 61, 62, 63, 64, 67 });
    check("dense chromatic cluster does not receive a confident ordinary chord label",
          chromaticSix.confidence < 0.5
              && chromaticSix.status != ChordRecognitionStatus::Recognized,
          chromaticSix.fullName);
    const auto ambiguousSixth = ChordClassifier::identifyChordFromPitches(
        { 60, 64, 67, 69 });
    const auto hasAlternative = [&](int root, const juce::String& quality) {
        return std::any_of(ambiguousSixth.alternatives.begin(),
                           ambiguousSixth.alternatives.end(), [&](const auto& candidate) {
            return candidate.rootPitchClass == root && candidate.quality == quality;
        });
    };
    check("C6 exposes its A minor seventh inversion alternative",
          ambiguousSixth.fullName == "C6" && hasAlternative(9, "m7"),
          ambiguousSixth.fullName + " / "
              + juce::String(ambiguousSixth.alternatives.size()) + " alternatives");
    const auto minorSeventhAlternative = std::find_if(
        ambiguousSixth.alternatives.begin(), ambiguousSixth.alternatives.end(),
        [](const auto& candidate) {
            return candidate.rootPitchClass == 9 && candidate.quality == "m7"
                && candidate.fullName == "Am7/C";
        });
    check("C6 names the A-minor-seventh-over-C alternative explicitly",
          minorSeventhAlternative != ambiguousSixth.alternatives.end());
    const auto sixthObjects = ChordClassifier::identifyChord({
        note(11001, 60), note(11002, 64), note(11003, 67), note(11004, 69) });
    const auto alternateWithRoles = std::find_if(
        sixthObjects.alternatives.begin(), sixthObjects.alternatives.end(),
        [](const auto& candidate) {
            return candidate.rootPitchClass == 9 && candidate.quality == "m7"
                && candidate.fullName == "Am7/C";
        });
    bool alternateRolesCorrect = alternateWithRoles != sixthObjects.alternatives.end();
    if (alternateRolesCorrect)
    {
        const std::array<int64_t, 4> ids { 11001, 11002, 11003, 11004 };
        const std::array<int, 4> intervals { 3, 7, 10, 0 };
        const std::array<ChordToneRole, 4> roles {
            ChordToneRole::Third, ChordToneRole::Fifth,
            ChordToneRole::Seventh, ChordToneRole::Root,
        };
        alternateRolesCorrect &= alternateWithRoles->bassPitchClass == 0
            && alternateWithRoles->noteRoles.size() == 4;
        for (size_t i = 0; i < 4 && i < alternateWithRoles->noteRoles.size(); ++i)
            alternateRolesCorrect &= alternateWithRoles->noteRoles[i].noteId == ids[i]
                && alternateWithRoles->noteRoles[i].intervalFromRoot == intervals[i]
                && alternateWithRoles->noteRoles[i].role == roles[i];
    }
    check("Am7 over C alternative retains roles for each selected note",
          alternateRolesCorrect);
    const auto rootPositionMinorSeven = ChordClassifier::identifyChordFromPitches(
        { 57, 60, 64, 67 }); // A-C-E-G, in C major.
    const auto sixthAsAlternative = std::any_of(
        rootPositionMinorSeven.alternatives.begin(),
        rootPositionMinorSeven.alternatives.end(), [](const auto& candidate) {
            return candidate.rootPitchClass == 0 && candidate.quality == "6"
                && candidate.fullName == "C6/A";
        });
    check("root-position A minor seventh outranks a tonic C sixth slash chord",
          rootPositionMinorSeven.fullName == "Am7"
              && rootPositionMinorSeven.rootPitchClass == 9
              && rootPositionMinorSeven.bassPitchClass == 9
              && !rootPositionMinorSeven.isSlashChord && sixthAsAlternative,
          rootPositionMinorSeven.fullName);
    const auto augmentedOrAltered = ChordClassifier::identifyChordFromPitches(
        { 61, 65, 69, 71 }, 5, "Ionian (Natural Major)");
    // Db-F-A-B = Dbaug7 = Db7(b13,no5).
    const auto hasAlteredInterpretation = std::any_of(
        augmentedOrAltered.alternatives.begin(),
        augmentedOrAltered.alternatives.end(), [](const auto& candidate) {
            return candidate.rootPitchClass == 1 && candidate.quality == "7"
                && candidate.addedIntervals == std::vector<int>{ 8 }
                && candidate.omittedIntervals == std::vector<int>{ 7 };
        });
    check("augmented seventh also exposes its flat-thirteen omit-five meaning",
          augmentedOrAltered.rootPitchClass == 1
              && augmentedOrAltered.bassPitchClass == 1
              && hasAlteredInterpretation,
          augmentedOrAltered.fullName + " / "
              + juce::String(augmentedOrAltered.alternatives.size()) + " alternatives");

    const auto bbInF = ChordClassifier::identifyChordFromPitches(
        { 58, 62, 65 }, 5, "Ionian (Natural Major)");
    check("F major spells the subdominant B-flat",
          bbInF.rootPitchClass == 10 && bbInF.rootName == "Bb"
              && bbInF.fullName == "Bb" && bbInF.romanNumeral == "IV",
          bbInF.fullName + " / " + bbInF.romanNumeral);
    const auto dorianSubtonic = ChordClassifier::identifyChordFromPitches(
        { 58, 62, 65 }, 0, "Dorian");
    check("C Dorian calls B-flat major flat seven",
          dorianSubtonic.rootPitchClass == 10
              && dorianSubtonic.romanNumeral.startsWithIgnoreCase("bVII"),
          dorianSubtonic.fullName + " / " + dorianSubtonic.romanNumeral);
    const auto mixolydianSubtonic = ChordClassifier::identifyChordFromPitches(
        { 53, 57, 60 }, 7, "Mixolydian");
    check("G Mixolydian calls F major flat seven",
          mixolydianSubtonic.rootPitchClass == 5
              && mixolydianSubtonic.romanNumeral.startsWithIgnoreCase("bVII"),
          mixolydianSubtonic.romanNumeral);
    const auto phrygianFlatTwo = ChordClassifier::identifyChordFromPitches(
        { 61, 65, 68 }, 0, "Phrygian");
    check("C Phrygian calls D-flat major flat two",
          phrygianFlatTwo.rootPitchClass == 1
              && phrygianFlatTwo.romanNumeral.startsWithIgnoreCase("bII"),
          phrygianFlatTwo.fullName + " / " + phrygianFlatTwo.romanNumeral);
    const auto flatTwoInMajor = ChordClassifier::identifyChordFromPitches(
        { 61, 65, 68 }, 0, "Ionian (Natural Major)");
    check("C major spells borrowed flat-two as D-flat and bII",
          flatTwoInMajor.fullName == "Db" && flatTwoInMajor.rootName == "Db"
              && flatTwoInMajor.romanNumeral == "bII",
          flatTwoInMajor.fullName + " / " + flatTwoInMajor.romanNumeral);
    const auto flatSixInMajor = ChordClassifier::identifyChordFromPitches(
        { 56, 60, 63 }, 0, "Ionian (Natural Major)");
    check("C major spells borrowed flat-six as A-flat and bVI",
          flatSixInMajor.fullName == "Ab" && flatSixInMajor.rootName == "Ab"
              && flatSixInMajor.romanNumeral == "bVI",
          flatSixInMajor.fullName + " / " + flatSixInMajor.romanNumeral);
    const auto sharpMinorTonic = ChordClassifier::identifyChordFromPitches(
        { 61, 64, 68 }, 1, "Aeolian (Natural Minor)");
    check("C-sharp natural minor tonic is spelled C-sharp minor",
          sharpMinorTonic.fullName == "C#m"
              && sharpMinorTonic.rootName == "C#"
              && sharpMinorTonic.romanNumeral == "i",
          sharpMinorTonic.fullName + " / " + sharpMinorTonic.romanNumeral);
    const auto explicitFlatMinor = ChordClassifier::identifyChordFromPitches(
        { 61, 64, 68 }, 1, "Aeolian (Natural Minor)", "Db");
    check("explicit D-flat tonic spelling remains available for the same MIDI notes",
          explicitFlatMinor.fullName == "Dbm"
              && explicitFlatMinor.rootName == "Db"
              && explicitFlatMinor.romanNumeral == "i",
          explicitFlatMinor.fullName + " / " + explicitFlatMinor.romanNumeral);
    const auto flatMajorLayout = ScaleDefinitions::computeLayout(
        1, "Ionian (Natural Major)");
    const auto sharpMinorLayout = ScaleDefinitions::computeLayout(
        1, "Aeolian (Natural Minor)");
    const auto explicitSharpMajorLayout = ScaleDefinitions::computeLayout(
        1, "Ionian (Natural Major)", "C#");
    check("default tonic spelling follows the mode; explicit C-sharp major persists",
          flatMajorLayout.tonicName == "Db"
              && sharpMinorLayout.tonicName == "C#"
              && explicitSharpMajorLayout.tonicName == "C#");
    const auto explicitSharpMajor = ChordClassifier::identifyChordFromPitches(
        { 61, 65, 68 }, explicitSharpMajorLayout.tonicPc,
        explicitSharpMajorLayout.scaleName, explicitSharpMajorLayout.tonicName);
    check("explicit C-sharp major tonic spells its major triad C-sharp",
          explicitSharpMajor.fullName == "C#"
              && explicitSharpMajor.rootName == "C#"
              && explicitSharpMajor.romanNumeral == "I",
          explicitSharpMajor.fullName + " / " + explicitSharpMajor.romanNumeral);
    MidiModel spellingModel;
    TrackModel spellingTracks;
    auto* spellingModeTrack = spellingTracks.getModeTrack();
    if (spellingModeTrack != nullptr)
    {
        ModeEvent sharpMode;
        sharpMode.id = 901;
        sharpMode.startBeat = 0.0;
        sharpMode.lengthBeats = 8.0;
        sharpMode.tonicPc = 1;
        sharpMode.tonic = "C#";
        sharpMode.scaleName = "Ionian (Natural Major)";
        spellingModeTrack->getModes() = { sharpMode };
    }
    spellingTracks.setModeTrackOverrideEnabled(true);
    const auto modeTrackLayout = spellingModel.getEffectiveLayoutAtBeat(2.0, &spellingTracks);
    check("mode-track C-sharp tonic survives effective-layout lookup",
          spellingModeTrack != nullptr
              && modeTrackLayout.tonicPc == 1
              && modeTrackLayout.tonicName == "C#"
              && modeTrackLayout.scaleName == "Ionian (Natural Major)",
          modeTrackLayout.tonicName);

    const std::vector<NoteEvent> arpeggiated {
        note(101, 60, 0.0), note(102, 64, 0.5), note(103, 67, 1.0),
        note(104, 60, 8.0), note(105, 64, 8.5), note(106, 67, 9.0),
    };
    const auto oneSelection = ChordClassifier::identifyChord(arpeggiated);
    check("selected note objects form one chord across onsets and repetitions",
          oneSelection.fullName == "C" && oneSelection.rootPitchClass == 0
              && oneSelection.bassPitchClass == 0, oneSelection.fullName);
    const std::vector<NoteEvent> cMajNineEvents {
        note(121, 60, 0.0), note(122, 64, 0.25), note(123, 67, 0.5),
        note(124, 71, 0.75), note(125, 74, 1.0),
    };
    const auto canonicalMajNine = ChordClassifier::identifyChord(cMajNineEvents);
    auto permutedMajNine = cMajNineEvents;
    std::reverse(permutedMajNine.begin(), permutedMajNine.end());
    for (auto& n : permutedMajNine) n.startBeat += 8.0;
    permutedMajNine.push_back(note(126, 72, 10.0)); // octave doubling above the bass
    const auto reorderedMajNine = ChordClassifier::identifyChord(permutedMajNine);
    check("note order, onset and octave doubling preserve Cmaj9 identity",
          canonicalMajNine.fullName == "Cmaj9"
              && reorderedMajNine.fullName == canonicalMajNine.fullName
              && reorderedMajNine.rootPitchClass == canonicalMajNine.rootPitchClass
              && reorderedMajNine.bassPitchClass == canonicalMajNine.bassPitchClass
              && reorderedMajNine.noteIds.size() == permutedMajNine.size()
              && reorderedMajNine.noteRoles.size() == permutedMajNine.size());
    std::multiset<int64_t> sourceIds, recognizedIds;
    bool everyRoleNamesItsSource = oneSelection.noteRoles.size() == arpeggiated.size();
    for (const auto& source : arpeggiated)
    {
        sourceIds.insert(source.id);
        const auto found = std::find_if(oneSelection.noteRoles.begin(),
            oneSelection.noteRoles.end(), [&](const auto& role) {
                return role.noteId == source.id;
            });
        const auto interval = (source.pitch - 60) % 12;
        const auto expectedRole = interval == 0 ? ChordToneRole::Root
            : interval == 4 ? ChordToneRole::Third : ChordToneRole::Fifth;
        everyRoleNamesItsSource &= found != oneSelection.noteRoles.end()
            && found->pitch == source.pitch && found->intervalFromRoot == interval
            && found->role == expectedRole;
    }
    for (const auto id : oneSelection.noteIds) recognizedIds.insert(id);
    check("recognition keeps every selected note ID and role, including duplicates",
          sourceIds == recognizedIds && everyRoleNamesItsSource);
    auto mutedSelection = arpeggiated;
    mutedSelection[1].isMuted = true;
    const auto mutedResult = ChordClassifier::identifyChord(mutedSelection);
    check("direct selection and edit operators analyze the same muted note objects",
          mutedResult.fullName == "C"
              && mutedResult.noteIds.size() == mutedSelection.size()
              && mutedResult.noteRoles.size() == mutedSelection.size()
              && mutedResult.noteRoles[1].noteId == mutedSelection[1].id);
    std::vector<NoteEvent> mutedSharpEleven {
        note(1501, 60), note(1502, 64), note(1503, 67), note(1504, 66),
    };
    mutedSharpEleven.back().isMuted = true;
    const auto directMutedSharpEleven = ChordClassifier::identifyChord(mutedSharpEleven);
    const auto groupedMutedSharpEleven = ChordClassifier::identifyChordGroups(
        mutedSharpEleven, 4.0, [](double) { return ChordAnalysisContext {}; });
    std::multiset<int64_t> originalMutedIds, directMutedIds, groupedMutedIds;
    for (const auto& selected : mutedSharpEleven) originalMutedIds.insert(selected.id);
    for (const auto id : directMutedSharpEleven.noteIds) directMutedIds.insert(id);
    if (groupedMutedSharpEleven.size() == 1)
        for (const auto id : groupedMutedSharpEleven.front().chord.noteIds)
            groupedMutedIds.insert(id);
    check("same-onset muted sharp-eleven yields identical direct and grouped recognition",
          groupedMutedSharpEleven.size() == 1
              && directMutedSharpEleven.fullName == groupedMutedSharpEleven.front().chord.fullName
              && directMutedSharpEleven.rootPitchClass
                   == groupedMutedSharpEleven.front().chord.rootPitchClass
              && directMutedSharpEleven.intervalMask
                   == groupedMutedSharpEleven.front().chord.intervalMask
              && originalMutedIds == directMutedIds
              && originalMutedIds == groupedMutedIds,
          directMutedSharpEleven.fullName
              + (groupedMutedSharpEleven.empty() ? " / no group"
                                                   : " / " + groupedMutedSharpEleven.front().chord.fullName));
    const std::vector<NoteEvent> alteredVoicing {
        note(111, 52), note(112, 56), note(113, 59), note(114, 62), note(115, 67),
    };
    const auto alteredRoles = ChordClassifier::identifyChord(alteredVoicing);
    const auto sharpNineRole = std::find_if(alteredRoles.noteRoles.begin(),
        alteredRoles.noteRoles.end(), [](const auto& role) { return role.noteId == 115; });
    check("sharp ninth note has its own alteration role",
          sharpNineRole != alteredRoles.noteRoles.end()
              && sharpNineRole->pitch == 67 && sharpNineRole->intervalFromRoot == 3
              && sharpNineRole->role == ChordToneRole::Alteration);
    const std::vector<NoteEvent> separatedTriads {
        note(801, 48), note(802, 52), note(803, 55),
        note(804, 62), note(805, 66), note(806, 69),
    };
    const auto layered = ChordClassifier::identifyChord(separatedTriads);
    bool layersAndRoles = layered.noteRoles.size() == separatedTriads.size();
    const std::array<ChordToneRole, 6> expectedLayerRoles {
        ChordToneRole::Root, ChordToneRole::Third, ChordToneRole::Fifth,
        ChordToneRole::Root, ChordToneRole::Third, ChordToneRole::Fifth,
    };
    const std::array<int, 6> expectedLayerIntervals { 0, 4, 7, 0, 4, 7 };
    for (size_t i = 0; i < separatedTriads.size(); ++i)
    {
        const auto role = std::find_if(layered.noteRoles.begin(),
            layered.noteRoles.end(), [&](const auto& value) {
                return value.noteId == separatedTriads[i].id;
            });
        layersAndRoles &= role != layered.noteRoles.end()
            && role->pitch == separatedTriads[i].pitch
            && role->polychordLayer == (i < 3 ? 0 : 1)
            && role->intervalFromRoot == expectedLayerIntervals[i]
            && role->role == expectedLayerRoles[i];
    }
    check("D over C keeps distinct lower and upper voice roles",
          layered.fullName == "D|C" && layered.isPolychord
              && layered.rootPitchClass == 0 && layered.upperRootPitchClass == 2
              && layered.polychordSplitPitch == 62 && layersAndRoles,
          layered.fullName);
    const auto dElevenAlternative = std::find_if(layered.alternatives.begin(),
        layered.alternatives.end(), [](const auto& candidate) {
            return candidate.rootPitchClass == 2 && candidate.quality == "11"
                && candidate.fullName == "D11/C";
        });
    check("D over C retains its exact D11 over C interpretation",
          dElevenAlternative != layered.alternatives.end());
    const auto invertedLayers = ChordClassifier::identifyChordFromPitches(
        { 52, 55, 60, 66, 69, 74 });
    check("both layers of an inverted polychord retain their slash basses",
          invertedLayers.fullName == "D/F#|C/E"
              && invertedLayers.isPolychord
              && invertedLayers.rootPitchClass == 0
              && invertedLayers.upperRootPitchClass == 2
              && invertedLayers.lowerInversion == 1
              && invertedLayers.upperInversion == 1,
          invertedLayers.fullName);
    const auto minorLower = ChordClassifier::identifyChordFromPitches(
        { 48, 51, 55, 62, 66, 69 });
    check("minor lower triad is represented in the polychord symbol",
          minorLower.fullName == "D|Cm" && minorLower.isPolychord
              && minorLower.rootPitchClass == 0
              && minorLower.upperRootPitchClass == 2,
          minorLower.fullName);
    const auto wideEleventh = ChordClassifier::identifyChordFromPitches(
        { 48, 52, 55, 59, 74, 77 });
    check("register separation does not hide an exact Cmaj11 identity",
          wideEleventh.fullName == "Cmaj11" && !wideEleventh.isPolychord
              && wideEleventh.rootPitchClass == 0,
          wideEleventh.fullName);
    const auto invertedWideEleventh = ChordClassifier::identifyChordFromPitches(
        { 52, 55, 60, 71, 74, 77 });
    check("Cmaj11 in first inversion is not split into two triads",
          invertedWideEleventh.fullName == "Cmaj11/E"
              && !invertedWideEleventh.isPolychord
              && invertedWideEleventh.rootPitchClass == 0
              && invertedWideEleventh.bassPitchClass == 4,
          invertedWideEleventh.fullName);

    const std::vector<NoteEvent> placementPitches {
        note(821, 61, 6.0), note(822, 65, 6.0), note(823, 68, 6.0),
    };
    const auto placedInC = ChordClassifier::identifyChord(
        placementPitches, 0, "Ionian (Natural Major)");
    const auto placedInB = ChordClassifier::identifyChord(
        placementPitches, 11, "Ionian (Natural Major)");
    check("preview pitch set is renamed when moved from C major to B major",
          placedInC.rootPitchClass == 1 && placedInB.rootPitchClass == 1
              && placedInC.fullName == "Db" && placedInB.fullName == "C#"
              && placedInC.romanNumeral == "bII"
              && placedInB.romanNumeral == "II",
          placedInC.fullName + " / " + placedInB.fullName);
    MidiModel previewModel;
    TrackModel previewTracks;
    auto* previewModeTrack = previewTracks.getModeTrack();
    if (previewModeTrack != nullptr)
    {
        ModeEvent cMode;
        cMode.id = 1001;
        cMode.startBeat = 0.0;
        cMode.lengthBeats = 8.0;
        cMode.tonicPc = 0;
        cMode.tonic = "C";
        cMode.scaleName = "Ionian (Natural Major)";
        ModeEvent bMode = cMode;
        bMode.id = 1002;
        bMode.startBeat = 8.0;
        bMode.tonicPc = 11;
        bMode.tonic = "B";
        previewModeTrack->getModes() = { cMode, bMode };
    }
    const std::vector<NoteEvent> samePitchesAcrossModes {
        note(831, 61, 2.0), note(832, 65, 2.0), note(833, 68, 2.0),
        note(841, 61, 10.0), note(842, 65, 10.0), note(843, 68, 10.0),
    };
    const auto acrossModes = ChordClassifier::identifyChordGroups(
        samePitchesAcrossModes, 4.0, [&](double beat) {
            const auto layout = previewModel.getEffectiveLayoutAtBeat(beat, &previewTracks);
            return ChordAnalysisContext { layout.tonicPc, layout.scaleName,
                                          layout.tonicName };
        });
    check("grouped previews recompute spelling and degree at each mode block",
          previewModeTrack != nullptr && acrossModes.size() == 2
              && acrossModes[0].chord.fullName == "Db"
              && acrossModes[0].chord.romanNumeral == "bII"
              && acrossModes[1].chord.fullName == "C#"
              && acrossModes[1].chord.romanNumeral == "II",
          acrossModes.size() == 2
              ? acrossModes[0].chord.fullName + " / " + acrossModes[1].chord.fullName
              : juce::String(acrossModes.size()) + " groups");

    std::vector<NoteEvent> chromaticEvents;
    for (int pitch = 60; pitch < 72; ++pitch)
        chromaticEvents.push_back(note(900 + pitch, pitch));
    const auto unresolved = ChordClassifier::identifyChord(chromaticEvents);
    bool unresolvedRolesAreUnknown = unresolved.noteRoles.size() == chromaticEvents.size();
    for (size_t i = 0; i < unresolved.noteRoles.size()
                       && i < chromaticEvents.size(); ++i)
        unresolvedRolesAreUnknown &= unresolved.noteRoles[i].noteId == chromaticEvents[i].id
            && unresolved.noteRoles[i].pitch == chromaticEvents[i].pitch
            && unresolved.noteRoles[i].intervalFromRoot == -1
            && unresolved.noteRoles[i].role == ChordToneRole::Other;
    check("unidentified pitch set has no fictional root or per-note roles",
          unresolved.status == ChordRecognitionStatus::None
              && unresolved.rootPitchClass == -1
              && unresolvedRolesAreUnknown);

    MidiModel model;
    TrackModel tracks;
    const std::vector<NoteEvent> cMajorNine {
        note(201, 60, 0.0), note(202, 64, 0.25), note(203, 67, 0.5),
        note(204, 71, 0.75), note(205, 74, 1.0),
    };
    model.setAllNotes(cMajorNine);
    const auto minorSevenEdit = ChordEditing::changeQuality(
        model, tracks, model.getSelectedNotes(), ChordShapeQuality::Minor7);
    const auto afterMinorSeven = applyEdits(cMajorNine, minorSevenEdit.changedNotes,
        minorSevenEdit.addedNotes, minorSevenEdit.removedNoteIds);
    const auto afterMinorSevenName = ChordClassifier::identifyChord(afterMinorSeven).fullName;
    check("quality edit retains the selected ninth and names the actual result",
          minorSevenEdit.succeeded() && minorSevenEdit.groups.size() == 1
              && minorSevenEdit.groups.front().source == "Cmaj9"
              && pitchClasses(afterMinorSeven) == std::set<int>({ 0, 2, 3, 7, 10 })
              && afterMinorSevenName == "Cm9"
              && minorSevenEdit.groups.front().target == afterMinorSevenName,
          minorSevenEdit.groups.empty() ? "no preview"
                                      : minorSevenEdit.groups.front().target + " / " + afterMinorSevenName);
    check("quality edits preserve note IDs, timing, length and velocity",
          preservesEvents(cMajorNine, minorSevenEdit.changedNotes));

    const std::vector<NoteEvent> doubledFifth {
        note(701, 60, 0.0), note(702, 64, 0.25),
        note(703, 67, 0.5), note(704, 67, 0.75),
    };
    model.setAllNotes(doubledFifth);
    const auto promotedSeventh = ChordEditing::changeQuality(
        model, tracks, model.getSelectedNotes(), ChordShapeQuality::Dominant7);
    const auto afterPromotion = applyEdits(doubledFifth, promotedSeventh.changedNotes,
        promotedSeventh.addedNotes, promotedSeventh.removedNoteIds);
    const auto promotedName = ChordClassifier::identifyChord(afterPromotion).fullName;
    const auto countPitch = [&](int pitch) {
        return std::count_if(afterPromotion.begin(), afterPromotion.end(),
            [pitch](const auto& n) { return n.pitch == pitch; });
    };
    check("one doubled fifth becomes the seventh while its twin stays put",
          promotedSeventh.succeeded() && promotedSeventh.groups.size() == 1
              && promotedSeventh.changedNotes.size() == 1
              && promotedSeventh.changedNotes.front().pitch == 70
              && promotedSeventh.changedNotes.front().id >= 703
              && countPitch(67) == 1 && countPitch(70) == 1
              && promotedName == "C7"
              && promotedSeventh.groups.front().target == promotedName
              && preservesEvents(doubledFifth, promotedSeventh.changedNotes),
          promotedSeventh.groups.empty() ? "no preview"
                                        : promotedSeventh.groups.front().target + " / " + promotedName);

    const std::vector<NoteEvent> cMajorSeven {
        note(301, 60), note(302, 64), note(303, 67), note(304, 71),
    };
    model.setAllNotes(cMajorSeven);
    const auto dropped = ChordEditing::changeVoicing(
        model, tracks, model.getSelectedNotes(), ChordShapeVoicing::Drop2);
    const auto droppedNotes = applyEdits(cMajorSeven, dropped.changedNotes,
        dropped.addedNotes, dropped.removedNoteIds);
    const auto droppedName = ChordClassifier::identifyChord(droppedNotes).fullName;
    check("drop-two preview reports the new bass",
          dropped.succeeded() && dropped.groups.size() == 1
              && dropped.groups.front().source == "Cmaj7"
              && droppedName == "Cmaj7/G"
              && dropped.groups.front().target == droppedName,
          dropped.groups.empty() ? "no preview"
                                 : dropped.groups.front().target + " / " + droppedName);
    check("voicing edits preserve note event metadata",
          preservesEvents(cMajorSeven, dropped.changedNotes));

    const std::vector<NoteEvent> cMajor {
        note(401, 60, 0.0), note(402, 64, 0.5), note(403, 67, 1.0),
    };
    model.setAllNotes(cMajor);
    const auto stacked = ChordEditing::changeStack(
        model, tracks, model.getSelectedNotes(), ChordStackDirection::AddMajor);
    const auto stackedNotes = applyEdits(cMajor, stacked.changedNotes,
        stacked.addedNotes, stacked.removedNoteIds);
    const auto stackedName = ChordClassifier::identifyChord(stackedNotes).fullName;
    check("added major seventh preview names the target pitches",
          stacked.succeeded() && stacked.groups.size() == 1
              && stacked.groups.front().source == "C"
              && stackedName == "Cmaj7" && stacked.groups.front().target == stackedName,
          stacked.groups.empty() ? "no preview"
                                 : stacked.groups.front().target + " / " + stackedName);

    const auto borrowed = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 0, "Aeolian (Natural Minor)",
        ModeBorrowingStyle::SameTonic);
    const auto borrowedNotes = applyEdits(cMajor, borrowed.changedNotes,
        borrowed.addedNotes, borrowed.removedNoteIds);
    const auto borrowedName = ChordClassifier::identifyChord(
        borrowedNotes, 0, "Aeolian (Natural Minor)").fullName;
    check("parallel minor borrowing preview matches transformed selected notes",
          borrowed.succeeded() && borrowed.groups.size() == 1
              && borrowed.groups.front().source == "C"
              && pitchClasses(borrowedNotes) == std::set<int>({ 0, 3, 7 })
              && borrowedName == "Cm"
              && borrowed.groups.front().target == borrowedName,
          borrowed.groups.empty() ? "no preview"
                                  : borrowed.groups.front().target + " / " + borrowedName);
    check("parallel borrowing preserves source note event metadata",
          preservesEvents(cMajor, borrowed.changedNotes));

    model.setAllNotes(cMajorSeven);
    const auto approached = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 0, "Aeolian (Natural Minor)",
        ModeBorrowingStyle::Approach);
    const auto approachedNotes = applyEdits(cMajorSeven, approached.changedNotes,
        approached.addedNotes, approached.removedNoteIds);
    const auto approachedName = ChordClassifier::identifyChord(
        approachedNotes, 0, "Aeolian (Natural Minor)").fullName;
    check("mode approach converts Cmaj7 to Cm7 and previews those exact notes",
          approached.succeeded() && approached.groups.size() == 1
              && pitchClasses(approachedNotes) == std::set<int>({ 0, 3, 7, 10 })
              && approachedName == "Cm7"
              && approached.groups.front().target == approachedName
              && preservesEvents(cMajorSeven, approached.changedNotes),
          approached.groups.empty() ? "no preview"
                                     : approached.groups.front().target + " / " + approachedName);

    const auto borrowedPlacement = ChordEditing::createBorrowedChordNotes(
        0, "Aeolian (Natural Minor)", 0, 6.0, 2.0);
    const auto borrowedPreviewName = ChordEditing::borrowedChordName(
        0, "Aeolian (Natural Minor)", 0, false);
    bool placementHasCorrectTiming = borrowedPlacement.size() == 3;
    for (const auto& placed : borrowedPlacement)
        placementHasCorrectTiming &= placed.startBeat == 6.0
            && placed.lengthBeats == 2.0;
    check("empty-selection borrowing preview matches its placed C minor notes",
          placementHasCorrectTiming
              && pitchClasses(borrowedPlacement) == std::set<int>({ 0, 3, 7 })
              && borrowedPreviewName == "Cm"
              && ChordClassifier::identifyChord(borrowedPlacement,
                  0, "Aeolian (Natural Minor)").fullName == borrowedPreviewName,
          borrowedPreviewName);

    int successfulModes = 0;
    bool allReharmonizationPreviewsMatch = true;
    bool allReharmonizationEventsPreserved = true;
    for (const auto& source : { cMajor, std::vector<NoteEvent> {
                               note(501, 55), note(502, 59), note(503, 62), note(504, 65) } })
        for (int mode = 0; mode < kReharmonizationAlgorithmCount; ++mode)
        {
            model.setAllNotes(source);
            const auto plan = Reharmonizer::prepare(model, tracks,
                static_cast<ReharmonizationAlgorithm>(mode));
            if (!plan.hasChanges()) continue;
            ++successfulModes;
            const auto after = applyEdits(source, plan.changedNotes);
            const auto actual = ChordClassifier::identifyChord(after).fullName;
            const auto before = ChordClassifier::identifyChord(source).fullName;
            allReharmonizationPreviewsMatch &= plan.groups.size() == 1
                && plan.groups.front().sourceChord == before
                && plan.groups.front().targetChord == actual;
            allReharmonizationEventsPreserved &= preservesEvents(source, plan.changedNotes);
        }
    check("all available reharmonization modes preview their actual note objects",
          successfulModes >= 4 && allReharmonizationPreviewsMatch,
          juce::String(successfulModes) + " successful modes");
    check("reharmonization retains note IDs and event metadata",
          successfulModes >= 4 && allReharmonizationEventsPreserved);

    model.setAllNotes(cMajor);
    const auto dominantPlan = SecondaryDominant::prepare(model, tracks, {});
    const auto generated = SecondaryDominant::createGeneratedNotes(model, dominantPlan);
    const auto generatedChord = ChordClassifier::identifyChord(generated);
    check("secondary dominant of C uses the four tones of G7",
          dominantPlan.succeeded() && dominantPlan.targetRoots == std::vector<int>{ 0 }
              && pitchClasses(generated) == std::set<int>({ 2, 5, 7, 11 })
              && generatedChord.rootPitchClass == 7 && generatedChord.quality == "7",
          generatedChord.fullName);
    check("generated dominant preview is analyzed from its actual MIDI notes",
          dominantPlan.targetChordNames == std::vector<juce::String>{ "C" }
              && dominantPlan.generatedChordNames
                  == std::vector<juce::String>{ generatedChord.fullName }
              && dominantPlan.targetFunctions == std::vector<juce::String>{ "V/I" });
    const auto placedPreview = SecondaryDominant::shiftGeneratedNotes(generated, 6.0);
    bool previewKeepsNotes = placedPreview.size() == generated.size();
    for (size_t i = 0; i < placedPreview.size() && i < generated.size(); ++i)
        previewKeepsNotes &= placedPreview[i].pitch == generated[i].pitch
            && placedPreview[i].id == generated[i].id
            && placedPreview[i].startBeat == 6.0
            && placedPreview[i].lengthBeats == generated[i].lengthBeats
            && placedPreview[i].velocity == generated[i].velocity;
    check("moving the dominant preview changes only its timeline position",
          previewKeepsNotes && !placedPreview.empty()
              && !dominantPlan.generatedChordNames.empty()
              && model.getNotes().size() == cMajor.size()
              && ChordClassifier::identifyChord(placedPreview).fullName
                  == dominantPlan.generatedChordNames.front());

    const std::vector<NoteEvent> dMinor {
        note(601, 62), note(602, 65), note(603, 69),
    };
    model.setAllNotes(dMinor);
    const auto appliedDominant = SecondaryDominant::prepare(model, tracks, {});
    const auto appliedNotes = SecondaryDominant::createGeneratedNotes(model, appliedDominant);
    const auto appliedChord = ChordClassifier::identifyChord(appliedNotes);
    check("dominant of the second degree is A7 resolving to D minor",
          appliedDominant.succeeded()
              && appliedDominant.targetRoots == std::vector<int>{ 2 }
              && pitchClasses(appliedNotes) == std::set<int>({ 1, 4, 7, 9 })
              && appliedChord.rootPitchClass == 9 && appliedChord.quality == "7"
              && appliedDominant.targetFunctions == std::vector<juce::String>{ "V/ii" }
              && appliedDominant.generatedChordNames
                  == std::vector<juce::String>{ appliedChord.fullName },
          appliedChord.fullName);

    SecondaryDominantOptions flatNine;
    flatNine.flatNine = true;
    const auto alteredDominant = SecondaryDominant::prepare(model, tracks, flatNine);
    const auto alteredNotes = SecondaryDominant::createGeneratedNotes(model, alteredDominant);
    const auto alteredName = ChordClassifier::identifyChord(alteredNotes).fullName;
    check("flat-nine option adds B-flat to A7 and previews the actual voicing",
          alteredDominant.succeeded()
              && pitchClasses(alteredNotes) == std::set<int>({ 1, 4, 7, 9, 10 })
              && alteredName.contains("b9")
              && alteredDominant.generatedChordNames
                  == std::vector<juce::String>{ alteredName },
          alteredName);

    const auto summary = juce::String(checks - failures) + "/" + juce::String(checks)
        + " music-theory acceptance checks passed";
    std::printf("%s\n", summary.toRawUTF8());
    logFile.appendText(summary + "\n");
    return failures == 0 ? 0 : 1;
}

} // namespace Harmonizer
