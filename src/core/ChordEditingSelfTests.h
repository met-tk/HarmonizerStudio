#pragma once

#include "ChordEditing.h"
#include "ScaleDefinitions.h"
#include "UndoActions.h"
#include <algorithm>
#include <cstdio>
#include <set>

namespace Harmonizer
{

inline int runChordEditingSelfTests()
{
    const auto logFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                             .getSiblingFile("chord_editing_tests.log");
    logFile.deleteFile();
    int failures = 0;
    auto check = [&](const char* name, bool passed) {
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", name);
        logFile.appendText(juce::String(passed ? "[PASS] " : "[FAIL] ") + name + "\n");
        if (!passed) ++failures;
    };
    auto note = [](int64_t id, int pitch, double beat = 0.0) {
        NoteEvent n;
        n.id = id;
        n.pitch = pitch;
        n.startBeat = beat;
        n.lengthBeats = 1.0;
        n.isSelected = true;
        return n;
    };
    auto transformed = [](std::vector<NoteEvent> notes, const ChordEditPlan& plan) {
        for (auto& current : notes)
            for (const auto& changed : plan.changedNotes)
                if (current.id == changed.id) current.pitch = changed.pitch;
        return notes;
    };
    auto classes = [](const std::vector<NoteEvent>& notes) {
        std::set<int> result;
        for (const auto& n : notes) result.insert(n.pitch % 12);
        return result;
    };
    auto spacing = [](const std::vector<NoteEvent>& notes) {
        std::vector<int> pitches;
        for (const auto& n : notes) pitches.push_back(n.pitch);
        std::sort(pitches.begin(), pitches.end());
        int smallest = 128, largest = 0;
        for (size_t i = 1; i < pitches.size(); ++i)
        {
            smallest = std::min(smallest, pitches[i] - pitches[i - 1]);
            largest = std::max(largest, pitches[i] - pitches[i - 1]);
        }
        return std::pair<int, int>(pitches.back() - pitches.front(), largest - smallest);
    };

    MidiModel model;
    TrackModel tracks;
    model.setAllNotes({ note(1, 60), note(2, 64), note(3, 67) });
    const auto baseline = model.getSelectedNotes();
    const auto minor = ChordEditing::changeQuality(model, tracks, baseline, ChordShapeQuality::Minor);
    const auto sus4 = ChordEditing::changeQuality(model, tracks, baseline, ChordShapeQuality::Sus4);
    const auto major = ChordEditing::changeQuality(model, tracks, baseline, ChordShapeQuality::Major);
    check("quality clicks derive from the original voicing",
          minor.succeeded() && classes(transformed(baseline, minor)) == std::set<int>({0, 3, 7})
          && sus4.succeeded() && classes(transformed(baseline, sus4)) == std::set<int>({0, 5, 7})
          && major.succeeded() && !major.hasChanges()
          && transformed(baseline, minor)[0].pitch == baseline[0].pitch);
    check("quality preview names suspended chords",
          sus4.groups.size() == 1 && sus4.groups[0].target == "Csus4");

    model.setAllNotes({ note(300, 60, 0.0), note(301, 64, 0.5), note(302, 67, 1.0) });
    const auto qualityArpeggio = ChordEditing::changeQuality(model, tracks,
        model.getSelectedNotes(), ChordShapeQuality::Minor);
    check("quality editing treats a short arpeggio as one chord and preserves note timing",
          qualityArpeggio.succeeded() && qualityArpeggio.groups.size() == 1
          && qualityArpeggio.groups[0].target == "Cm"
          && qualityArpeggio.changedNotes.size() == 1
          && qualityArpeggio.changedNotes[0].id == 301
          && qualityArpeggio.changedNotes[0].startBeat == 0.5
          && classes(transformed(model.getSelectedNotes(), qualityArpeggio))
              == std::set<int>({0, 3, 7}));

    model.setAllNotes({ note(303, 60, 0.0), note(304, 64, 0.25), note(305, 67, 0.5),
                        note(306, 60, 8.0), note(307, 64, 8.25), note(308, 67, 8.5) });
    const auto adjacentQuality = ChordEditing::changeQuality(model, tracks,
        model.getSelectedNotes(), ChordShapeQuality::Minor);
    check("all selected repeated notes receive one chord-quality edit",
          adjacentQuality.succeeded() && adjacentQuality.groups.size() == 1
          && adjacentQuality.changedNotes.size() == 2
          && adjacentQuality.changedNotes[0].id == 304
          && adjacentQuality.changedNotes[1].id == 307);

    model.setAllNotes({ note(830, 67, 0.0), note(831, 69, 0.5),
                        note(832, 72, 1.0), note(833, 74, 2.0),
                        note(834, 67, 2.5), note(835, 79, 3.0) });
    const auto screenshotSelection = model.getSelectedNotes();
    const auto screenshotQuality = ChordEditing::changeQuality(model, tracks,
        screenshotSelection, ChordShapeQuality::Minor);
    check("complex selected suspended chord can change quality as one chord",
          screenshotQuality.succeeded() && screenshotQuality.groups.size() == 1
          && screenshotQuality.groups[0].source == "Gsus4(9)"
          && screenshotQuality.hasChanges()
          && transformed(screenshotSelection, screenshotQuality)[0].pitch
              == transformed(screenshotSelection, screenshotQuality)[4].pitch);

    model.setAllNotes({ note(836, 67, 0.0), note(837, 71, 0.5),
                        note(838, 74, 1.0), note(839, 77, 1.5),
                        note(840, 74, 8.0) });
    const auto repeatedVoices = model.getSelectedNotes();
    const auto repeatedDrop2 = ChordEditing::changeVoicing(model, tracks,
        repeatedVoices, ChordShapeVoicing::Drop2);
    const auto afterDrop2 = transformed(repeatedVoices, repeatedDrop2);
    check("drop-2 moves both occurrences of the same selected voice",
          repeatedDrop2.succeeded() && repeatedDrop2.groups.size() == 1
          && afterDrop2[2].pitch == 62 && afterDrop2[4].pitch == 62
          && afterDrop2[4].startBeat == 8.0);

    const auto diminished = ChordEditing::changeQuality(model, tracks, baseline,
        ChordShapeQuality::Diminished);
    const auto augmented = ChordEditing::changeQuality(model, tracks, baseline,
        ChordShapeQuality::Augmented);
    check("diminished and augmented keep the original three voice positions",
          diminished.succeeded() && augmented.succeeded()
          && classes(transformed(baseline, diminished)) == std::set<int>({0, 3, 6})
          && classes(transformed(baseline, augmented)) == std::set<int>({0, 4, 8})
          && diminished.groups[0].target == "Cdim"
          && augmented.groups[0].target == "Caug");

    model.setAllNotes({ note(110, 60), note(111, 63), note(112, 66) });
    const auto directDiminished = ChordEditing::changeQuality(model, tracks,
        model.getSelectedNotes(), ChordShapeQuality::Major);
    model.setAllNotes({ note(113, 60), note(114, 64), note(115, 68) });
    const auto directAugmented = ChordEditing::changeQuality(model, tracks,
        model.getSelectedNotes(), ChordShapeQuality::Minor);
    check("direct edits of diminished and augmented triads retain their root",
          directDiminished.succeeded() && directAugmented.succeeded()
          && classes(transformed({ note(110, 60), note(111, 63), note(112, 66) }, directDiminished))
              == std::set<int>({0, 4, 7})
          && classes(transformed({ note(113, 60), note(114, 64), note(115, 68) }, directAugmented))
              == std::set<int>({0, 3, 7}));

    model.setAllNotes({ note(101, 60), note(102, 64), note(103, 67), note(104, 72) });
    const auto fourVoiceBaseline = model.getSelectedNotes();
    const auto dominant7 = ChordEditing::changeQuality(model, tracks, fourVoiceBaseline,
        ChordShapeQuality::Dominant7);
    const auto major7 = ChordEditing::changeQuality(model, tracks, fourVoiceBaseline,
        ChordShapeQuality::Major7);
    const auto minor7 = ChordEditing::changeQuality(model, tracks, fourVoiceBaseline,
        ChordShapeQuality::Minor7);
    const auto halfDiminished7 = ChordEditing::changeQuality(model, tracks, fourVoiceBaseline,
        ChordShapeQuality::HalfDiminished7);
    check("seventh qualities repurpose a doubled voice without adding notes",
          dominant7.succeeded() && major7.succeeded() && minor7.succeeded()
          && halfDiminished7.succeeded()
          && classes(transformed(fourVoiceBaseline, dominant7)) == std::set<int>({0, 4, 7, 10})
          && classes(transformed(fourVoiceBaseline, major7)) == std::set<int>({0, 4, 7, 11})
          && classes(transformed(fourVoiceBaseline, minor7)) == std::set<int>({0, 3, 7, 10})
          && classes(transformed(fourVoiceBaseline, halfDiminished7))
              == std::set<int>({0, 3, 6, 10})
          && transformed(fourVoiceBaseline, dominant7)[0].pitch == 60
          && dominant7.groups[0].target == "C7"
          && halfDiminished7.groups[0].target == "Cm7b5");

    const auto threeVoiceSeventh = ChordEditing::changeQuality(model, tracks, baseline,
        ChordShapeQuality::Major7);
    check("three voices cannot silently become a seventh chord",
          !threeVoiceSeventh.succeeded() && threeVoiceSeventh.changedNotes.empty());

    model.setAllNotes({ note(105, 60), note(106, 64), note(107, 67), note(108, 69) });
    const auto addedToneSeventh = ChordEditing::changeQuality(model, tracks,
        model.getSelectedNotes(), ChordShapeQuality::Dominant7);
    check("seventh quality can replace an added sixth while retaining the triad",
          addedToneSeventh.succeeded()
          && classes(transformed(model.getSelectedNotes(), addedToneSeventh))
              == std::set<int>({0, 4, 7, 10}));

    model.setAllNotes({ note(4, 64), note(5, 67), note(6, 72) });
    const auto inverted = ChordEditing::changeQuality(model, tracks,
        model.getSelectedNotes(), ChordShapeQuality::Minor);
    check("first inversion retains voice identity and bass position",
          inverted.succeeded()
          && classes(transformed(model.getSelectedNotes(), inverted)) == std::set<int>({0, 3, 7})
          && transformed(model.getSelectedNotes(), inverted)[0].pitch == 63);

    model.setAllNotes({ note(1, 60), note(2, 64), note(3, 67) });
    const auto expanded = ChordEditing::changeVoicing(model, tracks,
        model.getSelectedNotes(), ChordShapeVoicing::Expand);
    const auto expandedNotes = transformed(model.getSelectedNotes(), expanded);
    const auto contracted = ChordEditing::changeVoicing(model, tracks,
        expandedNotes, ChordShapeVoicing::Contract);
    check("expand and contract preserve chord tones and can reverse",
          expanded.succeeded() && contracted.succeeded()
          && classes(expandedNotes) == std::set<int>({0, 4, 7})
          && spacing(expandedNotes).first > spacing(baseline).first
          && spacing(expandedNotes).second <= 2
          && transformed(expandedNotes, contracted)[0].pitch == 60
          && transformed(expandedNotes, contracted)[1].pitch == 64
          && transformed(expandedNotes, contracted)[2].pitch == 67);

    model.setAllNotes({ note(5, 60), note(6, 64), note(7, 67), note(8, 71) });
    const auto evenFour = ChordEditing::changeVoicing(model, tracks,
        model.getSelectedNotes(), ChordShapeVoicing::Expand);
    check("four-voice expansion balances adjacent gaps",
          evenFour.succeeded() && evenFour.hasChanges()
          && classes(transformed(model.getSelectedNotes(), evenFour)) == std::set<int>({0, 4, 7, 11})
          && spacing(transformed(model.getSelectedNotes(), evenFour)).second <= 2);

    model.setAllNotes({ note(80, 60), note(81, 72), note(82, 76), note(83, 79) });
    const auto doubled = ChordEditing::changeVoicing(model, tracks,
        model.getSelectedNotes(), ChordShapeVoicing::Expand);
    auto doubledResult = transformed(model.getSelectedNotes(), doubled);
    int tonicCount = 0;
    for (const auto& n : doubledResult) if (n.pitch % 12 == 0) ++tonicCount;
    check("expansion preserves doubled chord tones around the chord centre",
          doubled.succeeded() && tonicCount == 2
          && std::abs((std::min_element(doubledResult.begin(), doubledResult.end(),
                [](const auto& a, const auto& b) { return a.pitch < b.pitch; })->pitch
                + std::max_element(doubledResult.begin(), doubledResult.end(),
                [](const auto& a, const auto& b) { return a.pitch < b.pitch; })->pitch) / 2.0
                - 69.5) <= 6.0
          && spacing(doubledResult).second <= 4);

    model.setAllNotes({ note(90, 120), note(91, 124), note(92, 127) });
    const auto overflow = ChordEditing::changeVoicing(model, tracks,
        model.getSelectedNotes(), ChordShapeVoicing::Expand);
    check("upper MIDI boundary rejects expansion atomically",
          !overflow.succeeded() && overflow.changedNotes.empty());

    model.setAllNotes({ note(701, 60), note(702, 64), note(703, 67) });
    const auto addMajor = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddMajor);
    check("major stack adds a seventh above the chord without changing existing voices",
          addMajor.succeeded() && addMajor.addedNotes.size() == 1
          && addMajor.addedNotes.front().pitch == 71
          && addMajor.changedNotes.empty() && addMajor.removedNoteIds.empty());
    auto occupiedSeventh = note(709, 71);
    occupiedSeventh.isSelected = false;
    model.setAllNotes({ note(701, 60), note(702, 64), note(703, 67), occupiedSeventh });
    const auto addAroundOccupied = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddMajor);
    check("high stack avoids an unselected overlapping note",
          addAroundOccupied.succeeded() && addAroundOccupied.addedNotes.size() == 1
          && addAroundOccupied.addedNotes.front().pitch == 83);
    model.setAllNotes({ note(701, 60), note(702, 64), note(703, 67) });
    const auto beforeStack = model.getNotes();
    auto withSeventh = beforeStack;
    if (!addMajor.addedNotes.empty())
    {
        auto seventh = addMajor.addedNotes.front();
        seventh.id = 704;
        withSeventh.push_back(seventh);
    }
    juce::UndoManager stackUndo;
    const bool stackApplied = UndoActionHelper::performAtomic(&stackUndo,
        new ModifyClipNotesUndoAction(model, tracks, tracks.getActiveTrackId(),
            tracks.getActiveClipId(), beforeStack, withSeventh), "Add Seventh");
    const bool stackUndone = stackUndo.undo() && model.getNotes().size() == beforeStack.size();
    const bool stackRedone = stackUndo.redo() && model.getNotes().size() == withSeventh.size();
    check("high-stack addition is one undoable clip edit",
          stackApplied && stackUndone && stackRedone);
    const auto addMode = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddMode);
    const auto conflictingDominant = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddDominant);
    const auto removeMajor = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::RemoveMajor);
    check("mode stack adds the ninth and rejects a conflicting dominant seventh",
          addMode.succeeded() && addMode.addedNotes.size() == 1
          && addMode.addedNotes.front().pitch == 74
          && !conflictingDominant.succeeded()
          && removeMajor.succeeded() && removeMajor.removedNoteIds == std::vector<int64_t>{ 704 });
    model.setAllNotes({ note(705, 60), note(706, 63), note(707, 67) });
    const auto addMinor = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddMinor);
    check("minor stack uses the minor seventh", addMinor.succeeded()
          && addMinor.addedNotes.size() == 1 && addMinor.addedNotes.front().pitch == 70);

    model.setAllNotes({ note(720, 59), note(721, 63), note(722, 66), note(723, 69) });
    const auto b7 = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddDominant);
    check("dominant seventh is eligible for a ninth", b7.succeeded()
          && b7.addedNotes.size() == 1 && b7.addedNotes[0].pitch == 73);
    model.setAllNotes({ note(720, 59), note(721, 63), note(722, 66),
                        note(723, 69), note(724, 73) });
    const auto b9 = ChordClassifier::identifyChordFromPitches({ 59, 63, 66, 69, 73 });
    const auto b11Plan = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddDominant);
    const auto b9WrongMajor = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddMajor);
    const auto b9WrongMinor = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddMinor);
    const auto b9Remove = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::RemoveDominant);
    check("B9 retains dominant property and extends to B11",
          b9.rootPitchClass == 11 && b9.family == ChordFamily::Dominant
          && b11Plan.succeeded() && b11Plan.addedNotes.size() == 1
          && b11Plan.addedNotes[0].pitch == 76
          && !b9WrongMajor.hasChanges() && !b9WrongMinor.hasChanges()
          && b9Remove.succeeded() && b9Remove.removedNoteIds == std::vector<int64_t>{ 724 });
    model.setAllNotes({ note(720, 59), note(721, 63), note(722, 66),
                        note(723, 69), note(724, 73), note(725, 76) });
    const auto b11 = ChordClassifier::identifyChordFromPitches({ 59, 63, 66, 69, 73, 76 });
    const auto b13Plan = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddDominant);
    check("B11 retains dominant property and extends to B13",
          b11.rootPitchClass == 11 && b11.family == ChordFamily::Dominant
          && b11.fullName == "B11"
          && b13Plan.succeeded() && b13Plan.addedNotes.size() == 1
          && b13Plan.addedNotes[0].pitch == 80);
    model.setAllNotes({ note(720, 59), note(721, 63), note(722, 66),
                        note(723, 69), note(724, 73), note(725, 76), note(726, 80) });
    const auto b13 = ChordClassifier::identifyChordFromPitches(
        { 59, 63, 66, 69, 73, 76, 80 });
    const auto b13OmitEleventh = ChordClassifier::identifyChordFromPitches(
        { 59, 63, 66, 69, 73, 80 });
    const auto b13Stop = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddDominant);
    const auto b13Remove = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::RemoveDominant);
    check("thirteenth is the upper limit and can remove its top layer",
          b13.rootPitchClass == 11 && b13.family == ChordFamily::Dominant
          && b13.fullName == "B13"
          && b13OmitEleventh.fullName == "B13"
          && !b13Stop.hasChanges() && b13Remove.succeeded()
          && b13Remove.removedNoteIds == std::vector<int64_t>{ 726 });
    model.setAllNotes({ note(730, 59), note(731, 63), note(732, 66),
                        note(733, 69), note(734, 72) });
    const auto flatNinth = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::RemoveDominant);
    check("altered flat ninth counts as a removable dominant extension",
          flatNinth.succeeded() && flatNinth.removedNoteIds == std::vector<int64_t>{ 734 });
    model.setAllNotes({ note(735, 60), note(736, 64), note(737, 67), note(738, 71),
                        note(739, 74) });
    const auto modeEleventh = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddMode);
    check("default mode stack extends a ninth to an eleventh",
          modeEleventh.succeeded() && modeEleventh.addedNotes.size() == 1
          && modeEleventh.addedNotes[0].pitch == 77);
    model.setAllNotes({ note(735, 60), note(736, 64), note(737, 67), note(738, 71),
                        note(739, 74), note(740, 77) });
    const auto modeThirteenth = ChordEditing::changeStack(model, tracks,
        model.getSelectedNotes(), ChordStackDirection::AddMode);
    check("default mode stack reaches the natural thirteenth",
          modeThirteenth.succeeded() && modeThirteenth.addedNotes.size() == 1
          && modeThirteenth.addedNotes[0].pitch == 81);

    model.setAllNotes({ note(10, 60), note(11, 64), note(12, 67), note(13, 71) });
    const auto drop2 = ChordEditing::changeVoicing(model, tracks,
        model.getSelectedNotes(), ChordShapeVoicing::Drop2);
    check("drop-2 lowers only the second highest voice",
          drop2.succeeded() && drop2.changedNotes.size() == 1
          && drop2.changedNotes[0].id == 12 && drop2.changedNotes[0].pitch == 55);

    model.setAllNotes({ note(309, 60, 0.0), note(310, 64, 0.25),
                        note(311, 67, 0.5), note(312, 71, 0.75) });
    const auto arpeggioBaseline = model.getSelectedNotes();
    const auto arpeggioDrop2 = ChordEditing::changeVoicing(model, tracks,
        arpeggioBaseline, ChordShapeVoicing::Drop2);
    const auto arpeggioExpand = ChordEditing::changeVoicing(model, tracks,
        arpeggioBaseline, ChordShapeVoicing::Expand);
    bool preservedTiming = arpeggioExpand.succeeded() && arpeggioExpand.hasChanges();
    for (const auto& changed : arpeggioExpand.changedNotes)
    {
        const auto original = std::find_if(arpeggioBaseline.begin(), arpeggioBaseline.end(),
            [&](const NoteEvent& note) { return note.id == changed.id; });
        preservedTiming = preservedTiming && original != arpeggioBaseline.end()
            && changed.startBeat == original->startBeat
            && changed.lengthBeats == original->lengthBeats;
    }
    check("voicing edits treat a staggered seventh as one chord without flattening rhythm",
          arpeggioDrop2.succeeded() && arpeggioDrop2.groups.size() == 1
          && arpeggioDrop2.changedNotes.size() == 1
          && arpeggioDrop2.changedNotes[0].id == 311
          && arpeggioDrop2.changedNotes[0].pitch == 55
          && arpeggioDrop2.changedNotes[0].startBeat == 0.5
          && arpeggioExpand.groups.size() == 1 && preservedTiming);

    model.setAllNotes({ note(1, 60), note(2, 64), note(3, 67) });
    const auto borrowed = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 0, "Aeolian (Natural Minor)", ModeBorrowingStyle::SameTonic);
    check("same-tonic borrowing constructs the source mode triad",
          borrowed.succeeded() && borrowed.groups.size() == 1
          && borrowed.groups[0].target == "Cm"
          && classes(transformed(model.getSelectedNotes(), borrowed)) == std::set<int>({0, 3, 7}));

    model.setAllNotes({ note(200, 60, 0.0), note(201, 64, 0.5), note(202, 67, 1.0) });
    const auto borrowedArpeggio = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 0, "Aeolian (Natural Minor)", ModeBorrowingStyle::SameTonic);
    check("same-tonic borrowing accepts a short staggered chord without changing its rhythm",
          borrowedArpeggio.succeeded() && borrowedArpeggio.groups.size() == 1
          && borrowedArpeggio.groups[0].target == "Cm"
          && borrowedArpeggio.changedNotes.size() == 1
          && borrowedArpeggio.changedNotes[0].id == 201
          && borrowedArpeggio.changedNotes[0].startBeat == 0.5
          && classes(transformed(model.getSelectedNotes(), borrowedArpeggio))
              == std::set<int>({0, 3, 7}));

    model.setAllNotes({ note(203, 60, 0.0), note(204, 64, 0.25),
                        note(205, 67, 0.5), note(206, 71, 0.75) });
    const auto approachedArpeggio = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 0, "Aeolian (Natural Minor)", ModeBorrowingStyle::Approach);
    check("mode approach borrows a staggered seventh as one harmony",
          approachedArpeggio.succeeded() && approachedArpeggio.groups.size() == 1
          && approachedArpeggio.changedNotes.size() == 2
          && classes(transformed(model.getSelectedNotes(), approachedArpeggio))
              == std::set<int>({0, 3, 7, 10}));

    model.setAllNotes({ note(20, 60), note(21, 64), note(22, 67), note(23, 71) });
    const auto approach = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 0, "Aeolian (Natural Minor)", ModeBorrowingStyle::Approach);
    check("mode approach keeps root and fifth while moving only E and B",
          approach.succeeded() && approach.changedNotes.size() == 2
          && classes(transformed(model.getSelectedNotes(), approach)) == std::set<int>({0, 3, 7, 10}));
    const auto arbitraryTonic = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 5, "Phrygian", ModeBorrowingStyle::Approach);
    check("approach accepts an independently chosen target tonic",
          arbitraryTonic.succeeded() && arbitraryTonic.changedNotes.size() == 3
          && classes(transformed(model.getSelectedNotes(), arbitraryTonic))
              == std::set<int>({0, 3, 6, 10}));
    const auto before = model.getNotes();
    auto after = transformed(before, approach);
    juce::UndoManager undo;
    const bool committed = UndoActionHelper::performAtomic(&undo,
        new ModifyClipNotesUndoAction(model, tracks, tracks.getActiveTrackId(),
                                      tracks.getActiveClipId(), before, after), "Borrow Chord");
    const bool undone = committed && undo.undo();
    const bool redone = undone && undo.redo();
    check("borrowed chord can undo and redo", redone && classes(model.getNotes())
          == std::set<int>({0, 3, 7, 10}));

    model.setAllNotes({ note(60, 60), note(61, 64), note(62, 67) });
    const auto explicitSource = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        2, "Dorian", 0, "Aeolian (Natural Minor)", ModeBorrowingStyle::SameTonic);
    check("same-tonic mode follows the chosen source tonic instead of project tonic",
          explicitSource.succeeded() && !explicitSource.hasChanges());

    model.setAllNotes({ note(30, 60, 0.0), note(31, 64, 0.0), note(32, 67, 0.0),
                        note(33, 62, 2.0), note(34, 65, 2.0), note(35, 69, 2.0) });
    const auto multiple = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 0, "Aeolian (Natural Minor)", ModeBorrowingStyle::Approach);
    check("different onsets are edited as separate chords",
          multiple.succeeded() && multiple.groups.size() == 2);
    model.setAllNotes({ note(207, 60, 0.0), note(208, 64, 0.5), note(209, 67, 1.0),
                        note(210, 65, 4.0), note(211, 69, 4.5), note(212, 72, 5.0) });
    const auto separateArpeggios = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 0, "Aeolian (Natural Minor)", ModeBorrowingStyle::Approach);
    check("arpeggios in different bars remain separate borrowed chords",
          separateArpeggios.succeeded() && separateArpeggios.groups.size() == 2
          && separateArpeggios.changedNotes.size() == 2
          && classes(transformed(model.getSelectedNotes(), separateArpeggios))
              == std::set<int>({0, 3, 5, 7, 8}));
    model.setAllNotes({ note(213, 60, 0.0), note(214, 64, 0.25), note(215, 67, 0.5),
                        note(216, 65, 1.5), note(217, 69, 1.75), note(218, 72, 2.0) });
    const auto adjacentArpeggios = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 0, "Aeolian (Natural Minor)", ModeBorrowingStyle::Approach);
    check("a contour turn separates adjacent borrowed arpeggios",
          adjacentArpeggios.succeeded() && adjacentArpeggios.groups.size() == 2
          && adjacentArpeggios.changedNotes.size() == 2);
    model.setAllNotes({ note(219, 60, 0.0), note(220, 64, 0.5), note(221, 67, 1.0),
                        note(222, 62, 8.0), note(223, 65, 8.5) });
    const auto incompletePhrase = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 0, "Aeolian (Natural Minor)", ModeBorrowingStyle::Approach);
    check("an incomplete later arpeggio cancels all borrowing changes",
          !incompletePhrase.succeeded() && incompletePhrase.groups.size() == 2
          && incompletePhrase.changedNotes.empty());
    model.setAllNotes({ note(40, 60, 0.0), note(41, 64, 0.0), note(42, 67, 0.0),
                        note(43, 69, 10.0), note(44, 72, 10.0), note(45, 76, 10.0) });
    const auto automaticBlocks = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "invalid saved source", 0, "Aeolian (Natural Minor)",
        ModeBorrowingStyle::SameTonic, -1, true);
    check("automatic source follows each mode-track block independently",
          automaticBlocks.succeeded() && automaticBlocks.groups.size() == 2
          && automaticBlocks.groups[0].target == "Cm"
          && automaticBlocks.groups[1].target == "Am"
          && automaticBlocks.groups[0].sourceMode.contains("C Ionian")
          && automaticBlocks.groups[1].sourceMode.contains("A Aeolian")
          && automaticBlocks.changedNotes.size() == 1);
    tracks.setModeTrackOverrideEnabled(false);
    auto key = model.getKeySignature();
    key.rootPitchClass = 2;
    model.setKeySignature(key);
    model.setScaleName("Dorian");
    model.setAllNotes({ note(46, 62, 10.0), note(47, 65, 10.0), note(48, 69, 10.0) });
    const auto automaticGlobal = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "invalid saved source", 0, "Ionian (Natural Major)",
        ModeBorrowingStyle::SameTonic, -1, true);
    check("automatic source falls back to the global mode when override is off",
          automaticGlobal.succeeded() && automaticGlobal.groups.size() == 1
          && automaticGlobal.groups[0].target == "D"
          && classes(transformed(model.getSelectedNotes(), automaticGlobal))
              == std::set<int>({ 2, 6, 9 }));
    tracks.setModeTrackOverrideEnabled(true);
    model.setScaleName("Ionian (Natural Major)");
    key.rootPitchClass = 0;
    model.setKeySignature(key);
    model.setAllNotes({ note(30, 60, 0.0), note(31, 64, 0.0), note(32, 67, 0.0),
                        note(33, 62, 2.0), note(34, 65, 2.0), note(35, 69, 2.0) });
    const auto invalid = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 0, "not a real mode", ModeBorrowingStyle::Approach);
    check("unknown source mode is rejected", !invalid.succeeded() && invalid.changedNotes.empty());
    ScaleDefinitions::registerCustomScale("Broken Borrowing Mode", {0, 2, 4, 4, 7, 9, 11});
    const auto broken = ChordEditing::borrow(model, tracks, model.getSelectedNotes(),
        0, "Ionian (Natural Major)", 0, "Broken Borrowing Mode", ModeBorrowingStyle::Approach);
    check("malformed custom seven-note mode is rejected",
          !broken.succeeded() && broken.changedNotes.empty()
          && ChordEditing::borrowedChordName(0, "Broken Borrowing Mode", 0, false).isEmpty());
    return failures == 0 ? 0 : 1;
}

} // namespace Harmonizer
