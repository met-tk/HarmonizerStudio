#pragma once

#include "SecondaryDominant.h"
#include "UndoActions.h"
#include "KeyCommandManager.h"
#include "AppStateManager.h"
#include <cstdio>
#include <set>

namespace Harmonizer
{

inline int runSecondaryDominantSelfTests()
{
    const auto logFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                             .getSiblingFile("harmony_tests.log");
    logFile.deleteFile();
    int failures = 0;
    auto check = [&](const char* name, bool passed) {
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", name);
        logFile.appendText(juce::String(passed ? "[PASS] " : "[FAIL] ") + name + "\n");
        if (!passed) ++failures;
    };
    auto makeNote = [](int64_t id, int pitch, double beat, bool selected) {
        NoteEvent n;
        n.id = id;
        n.pitch = pitch;
        n.startBeat = beat;
        n.lengthBeats = 1.0;
        n.velocity = 0.65f;
        n.isSelected = selected;
        return n;
    };
    auto pitchClasses = [](const MidiModel& model, const SecondaryDominantResult& result) {
        std::set<int> pcs;
        if (!result.blockChord)
        {
            for (const auto& n : model.getSelectedNotes())
            {
                int pitch = n.pitch;
                for (const auto& changed : result.modifiedNotes)
                    if (changed.id == n.id) { pitch = changed.pitch; break; }
                pcs.insert(pitch % 12);
            }
        }
        for (const auto& n : result.addedNotes) pcs.insert(n.pitch % 12);
        return pcs;
    };

    MidiModel follow;
    check("F toggle enables page follow by default", follow.getPlayheadFollowMode() == PlayheadFollowMode::Off
          && (follow.togglePlayheadFollow(), follow.getPlayheadFollowMode() == PlayheadFollowMode::Page));
    follow.setPlayheadFollowMode(PlayheadFollowMode::ContinuousCenter);
    follow.togglePlayheadFollow();
    follow.togglePlayheadFollow();
    check("F toggle restores the last enabled follow mode",
          follow.getPlayheadFollowMode() == PlayheadFollowMode::ContinuousCenter);
    const auto* followCommand = KeyCommandManager::getInstance().findCommand(Cmd_TogglePlayheadFollow);
    check("preferences lists F as default follow shortcut", followCommand != nullptr
          && followCommand->category == CommandCategory::ViewZoom
          && followCommand->defaultKeys == std::vector<juce::KeyPress>{ juce::KeyPress('f') });
    const auto& harmonicSettings = AppStateManager::getInstance();
    check("harmonic settings default to selected chords, V7 and smooth voicing",
          harmonicSettings.secondaryDominantTargetSource == 0
          && !harmonicSettings.secondaryDominantFlatNine
          && !harmonicSettings.secondaryDominantRootInBass);

    MidiModel model;
    TrackModel tracks;
    auto* clip = tracks.getActiveClip();
    clip->notes = { makeNote(1001, 60, 0.0, true), makeNote(1002, 64, 0.0, true),
                    makeNote(1003, 67, 0.0, true) };
    model.setAllNotes(clip->notes);
    const auto automatic = SecondaryDominant::prepare(model, tracks, {});
    check("selected C major notes are the target even with a future chord-track event",
          automatic.succeeded() && automatic.groupCount == 1
          && automatic.targetRoots == std::vector<int>{ 0 }
          && automatic.blockChord && automatic.modifiedNotes.empty()
          && automatic.addedNotes.size() == 4
          && pitchClasses(model, automatic) == std::set<int>({ 2, 5, 7, 11 }));
    SecondaryDominantOptions nextHarmony;
    nextHarmony.targetSource = SecondaryDominantTargetSource::NextHarmony;
    const auto legacyTarget = SecondaryDominant::prepare(model, tracks, nextHarmony);
    check("optional next-harmony mode keeps the former target lookup",
          legacyTarget.succeeded() && legacyTarget.targetRoots == std::vector<int>{ 9 }
          && pitchClasses(model, legacyTarget) == std::set<int>({ 2, 4, 8, 11 }));
    check("new note fits the clip and keeps timing and velocity", automatic.succeeded()
          && automatic.addedNotes.front().startBeat == 0.0
          && automatic.addedNotes.front().lengthBeats == 1.0
          && automatic.addedNotes.front().velocity == 0.65f);

    juce::UndoManager undo;
    const auto oldNotes = model.getNotes();
    const auto generated = SecondaryDominant::createGeneratedNotes(model, automatic);
    std::set<int> generatedPcs;
    bool freshIds = true;
    for (const auto& note : generated)
    {
        generatedPcs.insert(note.pitch % 12);
        freshIds = freshIds && note.id == 0 && note.isSelected;
    }
    check("generation creates a fresh V7 phrase without changing source notes",
          generated.size() == 4 && generatedPcs == std::set<int>({ 2, 5, 7, 11 })
          && freshIds && model.getNotes().size() == oldNotes.size()
          && model.getNotes()[0].pitch == 60 && model.getNotes()[1].pitch == 64
          && model.getNotes()[2].pitch == 67);

    auto positioned = SecondaryDominant::shiftGeneratedNotes(generated, 6.0);
    bool allAtMouseBeat = positioned.size() == generated.size();
    for (const auto& note : positioned)
        allAtMouseBeat = allAtMouseBeat && note.startBeat == 6.0
            && note.lengthBeats == 1.0 && note.velocity == 0.65f;
    check("mouse timeline position moves generated notes horizontally", allAtMouseBeat);

    auto newNotes = oldNotes;
    for (auto& note : newNotes) note.isSelected = false;
    for (auto& note : positioned)
    {
        note.id = model.generateNextNoteId();
        newNotes.push_back(note);
    }
    const bool applied = UndoActionHelper::performAtomic(&undo,
        new ModifyClipNotesUndoAction(model, tracks, tracks.getActiveTrackId(),
                                       tracks.getActiveClipId(), oldNotes, newNotes), "Place Secondary Dominant");
    check("click places a copy in the active MIDI clip atomically",
          applied && model.getNotes().size() == 7 && tracks.getActiveClip()->notes.size() == 7
          && model.getNotes()[0].pitch == 60 && model.getNotes()[1].pitch == 64
          && model.getNotes()[2].pitch == 67);
    undo.undo();
    check("one undo removes placed notes and restores selection", model.getNotes().size() == 3
          && model.getNotes()[0].pitch == 60 && model.getNotes()[1].pitch == 64
          && model.getNotes()[2].pitch == 67 && model.getNotes()[0].isSelected
          && tracks.getActiveClip()->notes.size() == 3);
    undo.redo();
    check("redo restores placed V7 beside source", model.getNotes().size() == 7
          && tracks.getActiveClip()->notes.size() == 7);

    auto* otherTrack = tracks.addTrack("Other instrument", TrackType::Instrument);
    MidiClip otherClip;
    otherClip.id = 500;
    otherClip.startBeat = 0.0;
    otherClip.lengthBeats = 16.0;
    otherClip.notes = { makeNote(5001, 72, 0.0, false) };
    otherTrack->addClip(otherClip);
    const int originalTrackId = tracks.getActiveTrackId();
    const int64_t originalClipId = tracks.getActiveClipId();
    tracks.setActiveClip(otherTrack->getId(), otherClip.id, &model);
    undo.undo();
    const auto* originalTrack = tracks.getTrackById(originalTrackId);
    const auto* targetClip = originalTrack != nullptr ? &originalTrack->getClips().front() : nullptr;
    check("undo after switching clips touches only the original clip",
          targetClip != nullptr && targetClip->id == originalClipId && targetClip->notes.size() == 3
          && model.getNotes().size() == 1 && model.getNotes().front().pitch == 72
          && otherTrack->getClips().front().notes.size() == 1);
    undo.redo();
    check("redo after switching clips preserves the active clip",
          targetClip != nullptr && targetClip->notes.size() == 7
          && model.getNotes().size() == 1 && model.getNotes().front().pitch == 72);
    tracks.setActiveClip(originalTrackId, originalClipId, &model);

    model.setAllNotes({ makeNote(1051, 64, 0.0, true), makeNote(1052, 67, 0.0, true),
                        makeNote(1053, 72, 0.0, true) });
    const auto inversion = SecondaryDominant::prepare(model, tracks, {});
    check("inverted selected C chord resolves to C rather than its bass E",
          inversion.succeeded() && inversion.targetRoots == std::vector<int>{ 0 });
    model.setAllNotes({ makeNote(1061, 60, 0.0, true), makeNote(1062, 64, 0.0, true) });
    const auto dyad = SecondaryDominant::prepare(model, tracks, {});
    check("ambiguous C-E dyad needs an explicit target root",
          !dyad.succeeded() && dyad.addedNotes.empty());
    SecondaryDominantOptions explicitDyadRoot;
    explicitDyadRoot.targetRootPc = 0;
    const auto rootedDyad = SecondaryDominant::prepare(model, tracks, explicitDyadRoot);
    check("an explicit C target can generate its dominant from a dyad selection",
          rootedDyad.succeeded() && rootedDyad.targetRoots == std::vector<int>{ 0 }
              && rootedDyad.addedNotes.size() == 4
              && pitchClasses(model, rootedDyad) == std::set<int>({ 2, 5, 7, 11 }));

    bool allSelectedTriadsWork = true;
    for (int pc = 0; pc < 12; ++pc)
    {
        model.setAllNotes({ makeNote(1071, 60 + pc, 0.0, true),
                            makeNote(1072, 64 + pc, 0.0, true),
                            makeNote(1073, 67 + pc, 0.0, true) });
        const auto triad = SecondaryDominant::prepare(model, tracks, {});
        if (!triad.succeeded() || triad.targetRoots != std::vector<int>{ pc })
            allSelectedTriadsWork = false;
    }
    check("selected major triads in all twelve keys identify their own roots",
          allSelectedTriadsWork);
    bool allSelectedMinorTriadsWork = true;
    for (int pc = 0; pc < 12; ++pc)
    {
        model.setAllNotes({ makeNote(1081, 60 + pc, 0.0, true),
                            makeNote(1082, 63 + pc, 0.0, true),
                            makeNote(1083, 67 + pc, 0.0, true) });
        const auto triad = SecondaryDominant::prepare(model, tracks, {});
        if (!triad.succeeded() || triad.targetRoots != std::vector<int>{ pc })
            allSelectedMinorTriadsWork = false;
    }
    check("selected minor triads in all twelve keys identify their own roots",
          allSelectedMinorTriadsWork);

    tracks.getChordTrack()->getChords().clear();
    model.setAllNotes({ makeNote(1101, 60, 0.0, true), makeNote(1102, 64, 0.0, true),
                        makeNote(1103, 67, 0.0, true), makeNote(1104, 65, 4.0, false),
                        makeNote(1105, 68, 4.0, false), makeNote(1106, 72, 4.0, false) });
    auto midiTarget = SecondaryDominant::inferTargetRoot(0.0, model, tracks);
    check("next MIDI triad supplies target when chord track is empty", midiTarget && *midiTarget == 5);

    model.setAllNotes({ makeNote(1121, 60, 0.0, true), makeNote(1122, 64, 0.5, true),
                        makeNote(1123, 67, 1.0, true), makeNote(1124, 65, 4.0, false),
                        makeNote(1125, 69, 4.5, false), makeNote(1126, 72, 5.0, false) });
    const auto arpeggiatedTarget = SecondaryDominant::prepare(model, tracks, nextHarmony);
    check("next-harmony option groups a selected arpeggio and recognizes a following arpeggio",
          arpeggiatedTarget.succeeded() && arpeggiatedTarget.groupCount == 1
          && arpeggiatedTarget.targetRoots == std::vector<int>{ 5 });

    model.setAllNotes({ makeNote(1111, 60, 0.0, true), makeNote(1112, 64, 0.0, true),
                        makeNote(1113, 67, 0.0, true) });
    otherTrack->getClips().front().notes = { makeNote(5101, 67, 4.0, false),
        makeNote(5102, 71, 4.0, false), makeNote(5103, 74, 4.0, false) };
    auto crossTrackTarget = SecondaryDominant::inferTargetRoot(0.0, model, tracks);
    check("accompaniment on another instrument track can supply target",
          crossTrackTarget && *crossTrackTarget == 7);

    SecondaryDominantOptions altered;
    altered.targetRootPc = 11;
    altered.flatNine = true;
    altered.rootInBass = true;
    auto flatNine = SecondaryDominant::prepare(model, tracks, altered);
    check("manual chromatic target creates full V7 flat nine", flatNine.succeeded()
          && flatNine.targetRoots == std::vector<int>{ 11 }
          && flatNine.blockChord && flatNine.addedNotes.size() == 5
          && pitchClasses(model, flatNine) == std::set<int>({ 1, 4, 6, 7, 10 }));
    int lowest = 128;
    for (const auto& n : flatNine.addedNotes) lowest = std::min(lowest, n.pitch);
    check("root-bass choice places dominant root below the other voices", lowest % 12 == 6);

    model.setAllNotes({ makeNote(1204, 72, 6.0, true), makeNote(1202, 64, 2.0, true),
                        makeNote(1203, 67, 4.0, true), makeNote(1201, 60, 0.0, true),
                        makeNote(1205, 76, 8.0, true), makeNote(1206, 79, 10.0, true) });
    const auto multiple = SecondaryDominant::prepare(model, tracks, {});
    const auto phrase = SecondaryDominant::createGeneratedNotes(model, multiple);
    const auto movedPhrase = SecondaryDominant::shiftGeneratedNotes(phrase, 8.0);
    bool oneColumn = phrase.size() == 4 && movedPhrase.size() == 4;
    for (const auto& note : phrase)
        oneColumn = oneColumn && note.startBeat == 0.0 && note.lengthBeats == 1.0;
    for (const auto& note : movedPhrase)
        oneColumn = oneColumn && note.startBeat == 8.0 && note.lengthBeats == 1.0;
    check("selected notes at different times create just one four-note block chord",
          multiple.succeeded() && multiple.blockChord && multiple.groupCount == 1
          && multiple.targetRoots == std::vector<int>{ 0 }
          && multiple.modifiedNotes.empty() && multiple.addedNotes.size() == 4
          && pitchClasses(model, multiple) == std::set<int>({ 2, 5, 7, 11 })
          && oneColumn && SecondaryDominant::countSelectedGroups(model) == 1);
    SecondaryDominantOptions splitFlatNine;
    splitFlatNine.flatNine = true;
    const auto splitAltered = SecondaryDominant::prepare(model, tracks, splitFlatNine);
    const auto splitAlteredNotes = SecondaryDominant::createGeneratedNotes(model, splitAltered);
    bool fiveTogether = splitAlteredNotes.size() == 5;
    for (const auto& note : splitAlteredNotes)
        fiveTogether = fiveTogether && note.startBeat == 0.0;
    check("flat-nine option creates one five-note block across selected times",
          splitAltered.succeeded() && splitAltered.groupCount == 1
          && splitAltered.addedNotes.size() == 5 && fiveTogether);

    model.setAllNotes({ makeNote(1201, 60, 0.0, true), makeNote(1202, 64, 0.0, true),
                        makeNote(1203, 67, 0.0, true), makeNote(1204, 62, 4.0, true),
                        makeNote(1205, 65, 4.0, true), makeNote(1206, 69, 4.0, true) });
    tracks.getChordTrack()->getChords() = {
        ChordEvent { 201, 2.0, 2.0, 0, "Dbm", "", juce::Colours::orange },
        ChordEvent { 202, 6.0, 2.0, 0, "Bb", "", juce::Colours::orange }
    };
    auto followingMultiple = SecondaryDominant::prepare(model, tracks, nextHarmony);
    check("optional next-harmony mode handles separate chromatic targets",
          followingMultiple.succeeded() && followingMultiple.groupCount == 2
          && followingMultiple.targetRoots == std::vector<int>({ 1, 10 }));
    const auto legacyPhrase = SecondaryDominant::createGeneratedNotes(model, followingMultiple);
    const auto movedLegacyPhrase = SecondaryDominant::shiftGeneratedNotes(legacyPhrase, 8.0);
    bool preservedSpacing = legacyPhrase.size() == movedLegacyPhrase.size() && !legacyPhrase.empty();
    for (size_t i = 0; i < legacyPhrase.size() && i < movedLegacyPhrase.size(); ++i)
        preservedSpacing = preservedSpacing
            && movedLegacyPhrase[i].startBeat - legacyPhrase[i].startBeat == 8.0;
    check("optional next-harmony mode preserves group spacing", preservedSpacing);

    tracks.getChordTrack()->getChords().pop_back();
    auto incomplete = SecondaryDominant::prepare(model, tracks, nextHarmony);
    check("one unresolved group never returns a partial edit plan",
          !incomplete.succeeded() && !incomplete.hasChanges()
          && incomplete.modifiedNotes.empty() && incomplete.addedNotes.empty());

    tracks.getChordTrack()->getChords().clear();
    otherTrack->getClips().front().notes.clear();
    model.setAllNotes({ makeNote(1301, 60, 0.0, true) });
    auto missing = SecondaryDominant::prepare(model, tracks, nextHarmony);
    check("missing automatic target fails without changing notes", !missing.succeeded()
          && model.getNotes().size() == 1 && model.getNotes()[0].pitch == 60);
    auto selectedSingle = SecondaryDominant::prepare(model, tracks, {});
    check("selected-chord mode does not infer a harmony from one note",
          !selectedSingle.succeeded() && selectedSingle.addedNotes.empty());
    auto manual = SecondaryDominant::prepare(model, tracks, { 2, false, false });
    check("fixed root overrides selected target notes", manual.succeeded()
          && manual.addedNotes.size() == 4);
    model.setAllNotes({ makeNote(1311, 60, 0.0, true), makeNote(1312, 64, 0.0, true),
                        makeNote(1313, 67, 0.0, true) });
    SecondaryDominantOptions eachNote;
    eachNote.targetSource = SecondaryDominantTargetSource::SelectedNotes;
    const auto perNote = SecondaryDominant::prepare(model, tracks, eachNote);
    check("explicit per-note mode uses each selected pitch as its target root",
          perNote.succeeded() && perNote.groupCount == 3
          && perNote.targetRoots == std::vector<int>({ 0, 4, 7 })
          && perNote.addedNotes.size() == 9);
    model.setAllNotes({ makeNote(1301, 60, 0.0, true) });
    bool allRootsWork = true;
    for (int target = 0; target < 12; ++target)
    {
        auto each = SecondaryDominant::prepare(model, tracks, { target, false, false });
        std::set<int> expected { (target + 7) % 12, (target + 11) % 12,
                                 (target + 2) % 12, (target + 5) % 12 };
        if (!each.succeeded() || pitchClasses(model, each) != expected)
            allRootsWork = false;
    }
    check("all twelve chromatic target roots yield the correct V7 tones", allRootsWork);

    model.setAllNotes({ makeNote(1401, 60, 15.5, true) });
    check("out-of-clip note is rejected before any edits",
          !SecondaryDominant::prepare(model, tracks, { 2, false, false }).succeeded()
          && model.getNotes().front().pitch == 60);

    tracks.getActiveInstrumentTrack()->setLocked(true);
    check("locked track rejects operator", !SecondaryDominant::prepare(model, tracks, { 2, false, false }).succeeded());

    std::printf("Harmony tests: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace Harmonizer
