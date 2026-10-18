#pragma once

#include <juce_core/juce_core.h>
#include "TrackModel.h"
#include "MidiModel.h"
#include "UndoActions.h"
#include <iostream>
#include <cassert>

namespace Harmonizer
{

inline int runUndoSystemSelfTests()
{
    juce::File logFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("undo_tests.log");
    logFile.deleteFile();
    auto appendLog = [&](const juce::String& text) {
        logFile.appendText(text + "\r\n");
        printf("%s\n", text.toRawUTF8());
        fflush(stdout);
    };

    appendLog("===================================================");
    appendLog(" HarmonizerStudio - Undo/Redo System Self-Tests");
    appendLog("===================================================");

    juce::UndoManager undoManager;
    int testsPassed = 0;
    int testsTotal = 0;

    auto report = [&](const char* name, bool success) {
        testsTotal++;
        if (success) {
            testsPassed++;
            appendLog(juce::String(" [PASS] ") + name);
        } else {
            testsPassed += 0;
            appendLog(juce::String(" [FAIL] ") + name);
        }
        return success;
    };

    //--------------------------------------------------------------------------
    // Test 1: Ctrl+D Multiple Duplicate Isolation (Anti-Coalescing)
    //--------------------------------------------------------------------------
    {
        MidiModel midiModel;
        undoManager.clearUndoHistory();

        NoteEvent baseNote;
        baseNote.id = 0;
        baseNote.pitch = 60;
        baseNote.startBeat = 0.0;
        baseNote.lengthBeats = 1.0;
        baseNote.velocity = 0.8f;

        midiModel.addNote(baseNote);
        auto all = midiModel.getNotes();
        if (!all.empty())
            midiModel.selectNote(all[0].id, false);

        // Perform 3 consecutive duplicateSelectedNotes
        midiModel.duplicateSelectedNotes(&undoManager);
        midiModel.duplicateSelectedNotes(&undoManager);
        midiModel.duplicateSelectedNotes(&undoManager);

        bool test1_ok = true;
        if (midiModel.getNotes().size() != 4) test1_ok = false;

        // Undo 1st: should only roll back the 3rd duplicate (size becomes 3)
        undoManager.undo();
        if (midiModel.getNotes().size() != 3) test1_ok = false;

        // Undo 2nd: should only roll back the 2nd duplicate (size becomes 2)
        undoManager.undo();
        if (midiModel.getNotes().size() != 2) test1_ok = false;

        // Undo 3rd: should roll back to original single note (size becomes 1)
        undoManager.undo();
        if (midiModel.getNotes().size() != 1) test1_ok = false;

        // Redo 1st: size becomes 2
        undoManager.redo();
        if (midiModel.getNotes().size() != 2) test1_ok = false;

        // Redo 2nd & 3rd: size becomes 4
        undoManager.redo();
        undoManager.redo();
        if (midiModel.getNotes().size() != 4) test1_ok = false;

        report("Test 1: Ctrl+D Isolation (Each duplicate is an atomic transaction)", test1_ok);
    }

    //--------------------------------------------------------------------------
    // Test 2: Active Clip & MidiModel Strong Consistency Sync on Undo/Redo
    //--------------------------------------------------------------------------
    {
        TrackModel trackModel;
        MidiModel midiModel;
        undoManager.clearUndoHistory();

        auto* instTrk = trackModel.addTrack("Synth Track", TrackType::Instrument);
        MidiClip clip;
        clip.id = 555;
        clip.startBeat = 4.0;
        clip.lengthBeats = 8.0;
        instTrk->getClips().push_back(clip);

        trackModel.setActiveClip(instTrk->getId(), 555);

        NoteEvent n1;
        n1.id = 2001;
        n1.pitch = 64;
        n1.startBeat = 0.0;
        n1.lengthBeats = 2.0;

        std::vector<NoteEvent> toAdd = { n1 };
        UndoActionHelper::performAtomic(&undoManager,
            new AddNotesUndoAction(midiModel, toAdd, &trackModel),
            "Add Note With Sync");

        bool test2_ok = true;
        auto* active = trackModel.getActiveClip();
        if (active == nullptr || active->notes.size() != 1) test2_ok = false;

        // Undo
        undoManager.undo();
        active = trackModel.getActiveClip();
        if (active == nullptr || !active->notes.empty() || !midiModel.getNotes().empty()) test2_ok = false;

        // Redo
        undoManager.redo();
        active = trackModel.getActiveClip();
        if (active == nullptr || active->notes.size() != 1 || midiModel.getNotes().size() != 1) test2_ok = false;

        report("Test 2: Active Clip & MidiModel Sync on Undo/Redo", test2_ok);
    }

    //--------------------------------------------------------------------------
    // Test 3: CompoundUndoAction for Multi-Track / Multi-Entity Operations
    //--------------------------------------------------------------------------
    {
        TrackModel trackModel;
        undoManager.clearUndoHistory();

        auto* trkA = trackModel.addTrack("Track A", TrackType::Instrument);
        auto* trkB = trackModel.addTrack("Track B", TrackType::Instrument);

        MidiClip c1; c1.id = 1; c1.startBeat = 0; c1.lengthBeats = 4;
        MidiClip c2; c2.id = 2; c2.startBeat = 4; c2.lengthBeats = 4;
        trkA->getClips() = { c1, c2 };
        trkB->getClips() = {};

        auto beforeA = trkA->getClips();
        auto afterA = std::vector<MidiClip>{ c1 }; // c2 removed from A
        auto beforeB = trkB->getClips();
        auto afterB = std::vector<MidiClip>{ c2 }; // c2 moved to B

        auto* compound = new CompoundUndoAction("Cross-Track Move Clip");
        compound->addAction(new ModifyClipsUndoAction(trackModel, trkA->getId(), beforeA, afterA, "Remove from A"));
        compound->addAction(new ModifyClipsUndoAction(trackModel, trkB->getId(), beforeB, afterB, "Add to B"));

        UndoActionHelper::performAtomic(&undoManager, compound, "Cross-Track Move Clip");

        bool test3_ok = true;
        if (trkA->getClips().size() != 1 || trkB->getClips().size() != 1) test3_ok = false;

        // Single undo rolls back both tracks atomically
        undoManager.undo();
        if (trkA->getClips().size() != 2 || !trkB->getClips().empty()) test3_ok = false;

        // Single redo reapplies both tracks atomically
        undoManager.redo();
        if (trkA->getClips().size() != 1 || trkB->getClips().size() != 1) test3_ok = false;

        report("Test 3: CompoundUndoAction Atomic Cross-Track Rollback", test3_ok);
    }

    //--------------------------------------------------------------------------
    // Test 4: DuplicateTrackUndoAction Full Clone and Rollback
    //--------------------------------------------------------------------------
    {
        TrackModel trackModel;
        undoManager.clearUndoHistory();

        auto* srcTrk = trackModel.addTrack("Original Track", TrackType::Instrument);
        MidiClip c; c.id = 99; c.startBeat = 0; c.lengthBeats = 4;
        srcTrk->getClips().push_back(c);

        int originalCount = (int)trackModel.getTracks().size();

        UndoActionHelper::performAtomic(&undoManager,
            new DuplicateTrackUndoAction(trackModel, srcTrk->getId()),
            "Duplicate Track");

        bool test4_ok = true;
        if ((int)trackModel.getTracks().size() != originalCount + 1) test4_ok = false;

        // Undo duplicate
        undoManager.undo();
        if ((int)trackModel.getTracks().size() != originalCount) test4_ok = false;

        // Redo duplicate
        undoManager.redo();
        if ((int)trackModel.getTracks().size() != originalCount + 1) test4_ok = false;

        report("Test 4: DuplicateTrackUndoAction Clone & Rollback", test4_ok);
    }

    //--------------------------------------------------------------------------
    // Test 5: TrackPropertyUndoAction (Rename, Color, Folder)
    //--------------------------------------------------------------------------
    {
        TrackModel trackModel;
        undoManager.clearUndoHistory();

        auto* trk = trackModel.addTrack("Initial Name", TrackType::Instrument);
        trk->setColor(juce::Colours::red);

        UndoActionHelper::performAtomic(&undoManager,
            new TrackPropertyUndoAction(trackModel, trk->getId(), juce::String("Initial Name"), juce::String("Renamed Track")),
            "Rename Track");

        UndoActionHelper::performAtomic(&undoManager,
            new TrackPropertyUndoAction(trackModel, trk->getId(), juce::Colours::red, juce::Colours::blue),
            "Change Track Color");

        bool test5_ok = true;
        if (trk->getName() != "Renamed Track" || trk->getColor() != juce::Colours::blue) test5_ok = false;

        // Undo color
        undoManager.undo();
        if (trk->getColor() != juce::Colours::red || trk->getName() != "Renamed Track") test5_ok = false;

        // Undo name
        undoManager.undo();
        if (trk->getName() != "Initial Name") test5_ok = false;

        // Redo both
        undoManager.redo();
        undoManager.redo();
        if (trk->getName() != "Renamed Track" || trk->getColor() != juce::Colours::blue) test5_ok = false;

        report("Test 5: TrackPropertyUndoAction Rename & Color", test5_ok);
    }

    //--------------------------------------------------------------------------
    // Test 6: Marker Split & Erase Undo/Redo
    //--------------------------------------------------------------------------
    {
        TrackModel trackModel;
        undoManager.clearUndoHistory();

        auto* mkTrk = trackModel.addTrack("Marker Track", TrackType::Marker);
        MarkerEvent mk1; mk1.id = "m1"; mk1.name = "Verse"; mk1.startBeat = 0; mk1.lengthBeats = 16;
        mkTrk->getMarkers().push_back(mk1);

        auto before = mkTrk->getMarkers();
        auto after = before;
        after[0].lengthBeats = 8.0;
        MarkerEvent mk2; mk2.id = "m2"; mk2.name = "Chorus"; mk2.startBeat = 8; mk2.lengthBeats = 8;
        after.push_back(mk2);

        UndoActionHelper::performAtomic(&undoManager,
            new ModifyMarkersUndoAction(trackModel, mkTrk->getId(), before, after, "Split Marker"),
            "Split Marker");

        bool test6_ok = true;
        if (mkTrk->getMarkers().size() != 2) test6_ok = false;

        undoManager.undo();
        if (mkTrk->getMarkers().size() != 1 || mkTrk->getMarkers()[0].lengthBeats != 16.0) test6_ok = false;

        undoManager.redo();
        if (mkTrk->getMarkers().size() != 2 || mkTrk->getMarkers()[0].lengthBeats != 8.0) test6_ok = false;

        report("Test 6: Marker Modify/Split Undo & Redo", test6_ok);
    }

    //--------------------------------------------------------------------------
    // Folder hierarchy: grouping, collapse, invalid nesting, and sibling moves
    //--------------------------------------------------------------------------
    {
        TrackModel model;
        undoManager.clearUndoHistory();
        auto* child = model.addTrack("Child", TrackType::Instrument);
        auto* folder = model.addTrack("Folder", TrackType::Folder);
        auto* sibling = model.addTrack("Sibling", TrackType::Instrument);
        const int childId = child->getId(), folderId = folder->getId(), siblingId = sibling->getId();
        MidiClip childClip;
        childClip.id = 7001;
        NoteEvent childNote;
        childNote.id = 7002;
        childClip.notes.push_back(childNote);
        child->addClip(childClip);
        bool ok = model.setTrackParent(childId, folderId);
        auto visible = model.getVisibleTracks();
        auto folderIt = std::find(visible.begin(), visible.end(), folder);
        ok &= folderIt != visible.end() && folderIt + 1 != visible.end() && *(folderIt + 1) == child;
        folder->setFolderCollapsed(true);
        visible = model.getVisibleTracks();
        ok &= std::find(visible.begin(), visible.end(), child) == visible.end();
        const auto playbackWhileCollapsed = model.getPlaybackSnapshot();
        ok &= playbackWhileCollapsed != nullptr && playbackWhileCollapsed->size() == 1
              && playbackWhileCollapsed->front().trackId == childId;
        folder->setFolderCollapsed(false);
        ok &= !model.setTrackParent(folderId, folderId);

        auto* nested = model.addTrack("Nested", TrackType::Folder);
        auto* levelThree = model.addTrack("Level three", TrackType::Folder);
        ok &= model.setTrackParent(nested->getId(), folderId);
        ok &= model.setTrackParent(levelThree->getId(), nested->getId());
        ok &= !model.setTrackParent(folderId, levelThree->getId());
        auto* levelFour = model.addTrack("Level four", TrackType::Folder);
        ok &= model.setTrackParent(levelFour->getId(), levelThree->getId());
        ok &= !model.setTrackParent(siblingId, levelFour->getId());

        auto* otherRoot = model.addTrack("Other root", TrackType::Folder);
        ok &= model.setTrackParent(siblingId, otherRoot->getId());
        ok &= model.moveTrackUp(siblingId, &undoManager) == false;
        ok &= model.getTrackById(siblingId)->getParentFolderId() == otherRoot->getId();
        auto* secondSibling = model.addTrack("Second sibling", TrackType::Instrument);
        ok &= model.setTrackParent(secondSibling->getId(), otherRoot->getId());
        auto indexOf = [&model](int id) {
            const auto list = model.getVisibleTracks();
            for (size_t i = 0; i < list.size(); ++i)
                if (list[i]->getId() == id) return (int)i;
            return -1;
        };
        ok &= model.moveTrackDown(siblingId, &undoManager)
              && indexOf(secondSibling->getId()) < indexOf(siblingId);
        ok &= undoManager.undo() && indexOf(siblingId) < indexOf(secondSibling->getId());
        ok &= UndoActionHelper::performAtomic(&undoManager,
            new TrackPropertyUndoAction(model, siblingId, otherRoot->getId(), folderId),
            "Move to Folder");
        ok &= model.getTrackById(siblingId)->getParentFolderId() == folderId;
        ok &= undoManager.undo() && model.getTrackById(siblingId)->getParentFolderId() == otherRoot->getId();
        const int fromIndex = model.getTrackIndex(siblingId);
        const int folderIndex = model.getTrackIndex(folderId);
        const int toIndex = fromIndex < folderIndex ? folderIndex : folderIndex + 1;
        ok &= UndoActionHelper::performAtomic(&undoManager,
            new MoveTrackUndoAction(model, fromIndex, toIndex, siblingId, otherRoot->getId(), folderId),
            "Drag into Folder");
        ok &= model.getTrackById(siblingId)->getParentFolderId() == folderId;
        ok &= undoManager.undo() && model.getTrackById(siblingId)->getParentFolderId() == otherRoot->getId()
              && model.getTrackIndex(siblingId) == fromIndex;
        report("Test 7: Folder grouping, collapse, nesting limits, and sibling movement", ok);
    }

    //--------------------------------------------------------------------------
    // Deleting a folder preserves children; copying it clones the hierarchy.
    //--------------------------------------------------------------------------
    {
        TrackModel model;
        undoManager.clearUndoHistory();
        auto* folder = model.addTrack("Folder", TrackType::Folder);
        auto* child = model.addTrack("Instrument", TrackType::Instrument);
        auto* nested = model.addTrack("Nested", TrackType::Folder);
        auto* grandchild = model.addTrack("Grandchild", TrackType::Instrument);
        auto* auxiliary = model.addTrack("Auxiliary", TrackType::Marker);
        const int folderId = folder->getId(), childId = child->getId();
        const int nestedId = nested->getId(), grandchildId = grandchild->getId();
        const int auxiliaryId = auxiliary->getId();
        bool ok = model.setTrackParent(childId, folderId)
               && model.setTrackParent(nestedId, folderId)
               && model.setTrackParent(grandchildId, nestedId)
               && model.setTrackParent(auxiliaryId, folderId);
        auxiliary->setParentTrackId(childId);
        child->setAssignedVstName("Kontakt");

        ok &= UndoActionHelper::performAtomic(&undoManager,
            new DuplicateTrackUndoAction(model, folderId), "Duplicate Folder");
        auto* copiedFolder = model.getTracks().back().get();
        for (const auto& track : model.getTracks())
            if (track->getName() == "Folder (Copy)") copiedFolder = track.get();
        const int copiedId = copiedFolder->getId();
        auto copiedChildren = model.getChildTrackIds(copiedId, true);
        ok &= copiedId != folderId && copiedChildren.size() == 4;
        int copiedInstrumentId = -1;
        int copiedAuxiliaryId = -1;
        for (int id : copiedChildren)
            if (auto* track = model.getTrackById(id))
            {
                if (track->getName() == "Instrument") copiedInstrumentId = id;
                if (track->getName() == "Auxiliary") copiedAuxiliaryId = id;
            }
        ok &= copiedInstrumentId != childId && copiedInstrumentId > 0
              && model.getTrackById(copiedInstrumentId)->getAssignedVstName() == "Kontakt";
        ok &= copiedAuxiliaryId > 0
              && model.getTrackById(copiedAuxiliaryId)->getParentTrackId() == copiedInstrumentId;
        ok &= undoManager.undo() && model.getTrackById(copiedId) == nullptr;
        ok &= undoManager.redo() && model.getChildTrackIds(copiedId, true).size() == 4;

        undoManager.clearUndoHistory();
        ok &= UndoActionHelper::performAtomic(&undoManager,
            new DeleteTrackUndoAction(model, folderId), "Delete Folder");
        ok &= model.getTrackById(folderId) == nullptr
              && model.getTrackById(childId)->getParentFolderId() == -1
              && model.getTrackById(nestedId)->getParentFolderId() == -1
              && model.getTrackById(grandchildId)->getParentFolderId() == nestedId;
        ok &= undoManager.undo() && model.getTrackById(folderId) != nullptr
              && model.getTrackById(childId)->getParentFolderId() == folderId
              && model.getTrackById(nestedId)->getParentFolderId() == folderId;
        ok &= undoManager.redo() && model.getTrackById(folderId) == nullptr
              && model.getTrackById(childId)->getParentFolderId() == -1;
        report("Test 8: Folder duplication and deletion survive undo and redo", ok);
    }

    // Applying a mode changes both global settings and the mode-track event.
    {
        MidiModel midi;
        TrackModel tracks;
        undoManager.clearUndoHistory();
        auto* modeTrack = tracks.getModeTrack();
        const auto beforeModes = modeTrack->getModes();
        auto afterModes = beforeModes;
        afterModes.front().tonic = "D";
        afterModes.front().tonicPc = 2;
        afterModes.front().scaleName = "Dorian";
        afterModes.front().name = "D Dorian";
        const auto beforeKey = midi.getKeySignature();
        const auto beforeScale = midi.getScaleName();
        auto afterKey = beforeKey;
        afterKey.rootPitchClass = 2;
        const bool applied = UndoActionHelper::performAtomic(&undoManager,
            new ApplySelectedModeUndoAction(midi, tracks, beforeKey, beforeScale,
                afterKey, "Dorian", modeTrack->getId(), beforeModes, afterModes),
            "Apply Selected Mode");
        bool ok = applied && midi.getKeySignature().rootPitchClass == 2
            && midi.getScaleName() == "Dorian"
            && modeTrack->getModes().front().tonicPc == 2;
        ok &= undoManager.undo() && midi.getKeySignature().rootPitchClass == beforeKey.rootPitchClass
            && midi.getScaleName() == beforeScale
            && modeTrack->getModes().front().name == beforeModes.front().name;
        ok &= undoManager.redo() && midi.getKeySignature().rootPitchClass == 2
            && midi.getScaleName() == "Dorian"
            && modeTrack->getModes().front().name == "D Dorian";
        report("Apply selected mode restores project and mode track on undo/redo", ok);
    }

    // A note selection crossing two mode blocks updates both without moving
    // either block; an empty range creates exactly one block.
    {
        TrackModel tracks;
        undoManager.clearUndoHistory();
        auto* modeTrack = tracks.getModeTrack();
        const auto before = modeTrack->getModes();
        ModeEvent requested;
        requested.id = 2; // Deliberate collision with a built-in block.
        requested.tonic = "D";
        requested.tonicPc = 2;
        requested.scaleName = "Dorian";
        requested.name = "D Dorian";
        auto after = before;
        bool ok = TrackModel::applyModeToRange(after, 7.5, 8.5, requested)
            && after.size() == 2 && after[0].tonicPc == 2 && after[1].tonicPc == 2
            && after[0].startBeat == 0.0 && after[0].lengthBeats == 8.0
            && after[1].startBeat == 8.0 && after[1].lengthBeats == 8.0;
        ok &= UndoActionHelper::performAtomic(&undoManager,
            new ModifyModesUndoAction(tracks, modeTrack->getId(), before, after),
            "Apply Selection Range to Mode Track");
        ok &= undoManager.undo() && modeTrack->getModes()[0].tonicPc == before[0].tonicPc
            && modeTrack->getModes()[1].tonicPc == before[1].tonicPc;
        ok &= undoManager.redo() && modeTrack->getModes()[0].tonicPc == 2
            && modeTrack->getModes()[1].tonicPc == 2;
        auto appended = after;
        ok &= TrackModel::applyModeToRange(appended, 16.0, 19.0, requested)
            && appended.size() == 3 && appended.back().startBeat == 16.0
            && appended.back().lengthBeats == 3.0 && appended.back().id != 2;
        report("Selected note span updates intersecting mode blocks or creates one block", ok);
    }
    {
        TrackModel tracks;
        undoManager.clearUndoHistory();
        int removedIndex = -1;
        auto removed = tracks.extractTrack(tracks.getModeTrack()->getId(), removedIndex);
        ModeEvent requested;
        requested.id = 9001;
        requested.tonic = "F";
        requested.tonicPc = 5;
        requested.scaleName = "Ionian (Natural Major)";
        requested.name = "F Ionian";
        std::vector<ModeEvent> after;
        bool ok = removed != nullptr && tracks.getModeTrack() == nullptr
            && TrackModel::applyModeToRange(after, 24.0, 27.0, requested);
        undoManager.beginNewTransaction("Create Mode Track From Selection");
        ok &= undoManager.perform(new AddTrackUndoAction(tracks, "Mode Track", TrackType::Mode));
        auto* newTrack = tracks.getModeTrack();
        ok &= newTrack != nullptr;
        if (newTrack != nullptr)
            ok &= undoManager.perform(new ModifyModesUndoAction(tracks, newTrack->getId(),
                {}, after));
        undoManager.beginNewTransaction();
        ok &= tracks.getModeTrack() != nullptr && tracks.getModeTrack()->getModes().size() == 1;
        ok &= undoManager.undo() && tracks.getModeTrack() == nullptr;
        ok &= undoManager.redo() && tracks.getModeTrack() != nullptr
            && tracks.getModeTrack()->getModes().front().startBeat == 24.0;
        report("Creating a missing mode track and its range is one undo step", ok);
    }

    // Note edits must keep their original MIDI part when focus changes before undo.
    {
        MidiModel midi;
        TrackModel tracks;
        undoManager.clearUndoHistory();
        auto pitchOf = [](const std::vector<NoteEvent>& notes) {
            return notes.empty() ? -1 : notes.front().pitch;
        };
        auto* instrument = tracks.getActiveInstrumentTrack();
        const int trackId = instrument->getId();
        const int64_t firstId = tracks.getActiveClipId();
        MidiClip second;
        second.id = 88002;
        second.startBeat = 16.0;
        second.lengthBeats = 8.0;
        instrument->getClips().push_back(second);
        tracks.triggerTracksChanged();

        NoteEvent firstNote;
        firstNote.id = 88101;
        firstNote.pitch = 60;
        firstNote.startBeat = 1.0;
        NoteEvent secondNote = firstNote;
        secondNote.id = 88102;
        secondNote.pitch = 72;
        secondNote.startBeat = 17.0;
        bool ok = UndoActionHelper::performAtomic(&undoManager,
            new AddNotesUndoAction(midi, { firstNote }, &tracks), "Add First Part Note");
        tracks.setActiveClip(trackId, second.id, &midi);
        ok &= UndoActionHelper::performAtomic(&undoManager,
            new AddNotesUndoAction(midi, { secondNote }, &tracks), "Add Second Part Note");
        ok &= undoManager.undo() && instrument->getClips()[1].notes.empty();
        ok &= undoManager.undo() && instrument->getClips()[0].notes.empty()
              && midi.getNotes().empty();
        ok &= undoManager.redo() && instrument->getClips()[0].notes.size() == 1
              && midi.getNotes().empty();
        ok &= undoManager.redo() && instrument->getClips()[1].notes.size() == 1
              && midi.getNotes().size() == 1 && pitchOf(midi.getNotes()) == 72;

        tracks.setActiveClip(trackId, firstId, &midi);
        const auto before = midi.getNotes();
        auto after = before;
        if (!after.empty()) after.front().pitch = 65;
        else ok = false;
        ok &= UndoActionHelper::performAtomic(&undoManager,
            new ModifyNotesUndoAction(midi, before, after, "Change First Part Pitch", &tracks),
            "Change First Part Pitch");
        tracks.setActiveClip(trackId, second.id, &midi);
        ok &= undoManager.undo() && pitchOf(instrument->getClips()[0].notes) == 60
              && pitchOf(instrument->getClips()[1].notes) == 72
              && pitchOf(midi.getNotes()) == 72;
        ok &= undoManager.redo() && pitchOf(instrument->getClips()[0].notes) == 65;

        tracks.setActiveClip(trackId, firstId, &midi);
        const auto deleted = instrument->getClips()[0].notes;
        ok &= UndoActionHelper::performAtomic(&undoManager,
            new DeleteNotesUndoAction(midi, deleted, &tracks),
            "Delete First Part Note");
        tracks.setActiveClip(trackId, second.id, &midi);
        ok &= undoManager.undo() && instrument->getClips()[0].notes.size() == 1
              && instrument->getClips()[1].notes.size() == 1
              && midi.getNotes().size() == 1 && pitchOf(midi.getNotes()) == 72;
        report("Note edits undo in their original MIDI part after focus switches", ok);
    }

    // Large note batches must preserve note identity and undo semantics.
    {
        MidiModel midi;
        TrackModel tracks;
        juce::UndoManager batchUndo;
        std::vector<NoteEvent> initial;
        std::vector<NoteEvent> added;
        initial.reserve(5000);
        added.reserve(1201);
        for (int i = 0; i < 5000; ++i)
        {
            NoteEvent note;
            note.id = 100000 + i;
            note.pitch = 48 + i % 24;
            note.startBeat = i * 0.002;
            note.lengthBeats = 0.25;
            initial.push_back(note);
        }
        for (int i = 0; i < 1200; ++i)
        {
            auto note = initial[(size_t)i];
            note.id = 200000 + i;
            added.push_back(note);
        }
        added.push_back(added.front()); // Repeated IDs within a batch must not duplicate notes.
        midi.setAllNotes(initial, false);
        tracks.syncActiveClipNotes(initial);
        const double began = juce::Time::getMillisecondCounterHiRes();
        bool ok = UndoActionHelper::performAtomic(&batchUndo,
            new AddNotesUndoAction(midi, added, &tracks), "Bulk Add Notes");
        ok &= midi.getNotes().size() == 6200 && tracks.getActiveClip()->notes.size() == 6200;
        ok &= batchUndo.undo() && midi.getNotes().size() == 5000;
        ok &= batchUndo.redo() && midi.getNotes().size() == 6200;
        std::vector<NoteEvent> removed(initial.begin(), initial.begin() + 2500);
        ok &= UndoActionHelper::performAtomic(&batchUndo,
            new DeleteNotesUndoAction(midi, removed, &tracks), "Bulk Delete Notes");
        ok &= midi.getNotes().size() == 3700 && tracks.getActiveClip()->notes.size() == 3700;
        ok &= batchUndo.undo() && midi.getNotes().size() == 6200;
        ok &= batchUndo.redo() && midi.getNotes().size() == 3700;
        MidiModel standalone;
        standalone.setAllNotes(initial, false);
        juce::UndoManager standaloneUndo;
        ok &= UndoActionHelper::performAtomic(&standaloneUndo,
            new DeleteNotesUndoAction(standalone, removed), "Bulk Delete Without Track");
        ok &= standalone.getNotes().size() == 2500;
        ok &= standaloneUndo.undo() && standalone.getNotes().size() == 5000;
        ok &= standaloneUndo.redo() && standalone.getNotes().size() == 2500;
        report("Large add/delete batches keep IDs and undo/redo consistent", ok);
        appendLog(" Bulk note actions: "
            + juce::String(juce::Time::getMillisecondCounterHiRes() - began, 1) + " ms");
    }

    // Preview notifications should update drawing and playback without rerunning
    // the full analysis and track-configuration listener paths on every frame.
    {
        struct MidiProbe : MidiModel::Listener
        {
            int notes = 0, preview = 0, selection = 0, selectionPreview = 0;
            void noteListChanged() override { ++notes; }
            void notePreviewChanged() override { ++preview; }
            void selectionChanged() override { ++selection; }
            void selectionPreviewChanged() override { ++selectionPreview; }
        } midiProbe;
        struct TrackProbe : TrackModel::Listener
        {
            int tracks = 0, clipNotes = 0;
            void tracksChanged() override { ++tracks; }
            void clipNotesChanged() override { ++clipNotes; }
        } trackProbe;
        MidiModel midi;
        TrackModel tracks;
        midi.addListener(&midiProbe);
        tracks.addListener(&trackProbe);
        NoteEvent note;
        note.id = 300001;
        midi.addNote(note, false);
        midi.triggerNotePreviewChanged();
        midi.triggerSelectionPreviewChanged();
        tracks.syncActiveClipNotes(midi.getNotes());
        const auto playback = tracks.getPlaybackSnapshot();
        bool ok = midiProbe.preview == 1 && midiProbe.notes == 0
            && midiProbe.selectionPreview == 1 && midiProbe.selection == 0
            && trackProbe.clipNotes == 1 && trackProbe.tracks == 0
            && playback != nullptr && playback->size() == 1;
        midi.triggerNoteListChanged();
        midi.triggerSelectionChanged();
        ok &= midiProbe.notes == 1 && midiProbe.selection == 1;
        const int originalPitch = midi.getNotesSnapshot()->front().pitch;
        midi.getNotes().front().pitch = originalPitch + 1;
        midi.triggerNotePreviewChanged(true);
        ok &= midi.getNotesSnapshot()->front().pitch == originalPitch;
        midi.updateSnapshot();
        ok &= midi.getNotesSnapshot()->front().pitch == originalPitch + 1;
        midi.removeListener(&midiProbe);
        tracks.removeListener(&trackProbe);
        report("Drag preview uses lightweight notifications and keeps playback current", ok);
    }

    // Stopped playback can defer rebuilding the track snapshot until Play.
    {
        TrackModel tracks;
        NoteEvent note;
        note.id = 301001;
        note.pitch = 67;
        tracks.syncActiveClipNotes({ note }, false);
        const auto before = tracks.getPlaybackSnapshot();
        bool ok = before != nullptr && before->empty();
        tracks.refreshPlaybackSnapshotIfDirty();
        const auto after = tracks.getPlaybackSnapshot();
        ok &= after != nullptr && after->size() == 1 && after->front().pitch == 67;
        report("Deferred playback snapshot publishes the latest clip before Play", ok);
    }

    // A note edit must not announce a track configuration change, which would
    // reload instruments and audio waveforms during bulk edits.
    {
        struct Probe : TrackModel::Listener
        {
            int trackChanges = 0;
            int noteCommits = 0;
            void tracksChanged() override { ++trackChanges; }
            void clipNotesCommitted() override { ++noteCommits; }
        } probe;
        MidiModel midi;
        TrackModel tracks;
        juce::UndoManager undo;
        tracks.addListener(&probe);
        NoteEvent note;
        note.id = 302001;
        bool ok = UndoActionHelper::performAtomic(&undo,
            new AddNotesUndoAction(midi, { note }, &tracks), "Add Note");
        ok &= probe.trackChanges == 0 && probe.noteCommits == 1;
        ok &= tracks.getPlaybackSnapshot()->size() == 1;
        ok &= undo.undo() && probe.trackChanges == 0 && probe.noteCommits == 2;
        ok &= tracks.getPlaybackSnapshot()->empty();
        tracks.removeListener(&probe);
        report("Note undo updates playback without reloading track configuration", ok);
    }

    // Diagnostic workload: the same full-note snapshot path used by drag preview.
    {
        MidiModel midi;
        TrackModel tracks;
        std::vector<NoteEvent> notes;
        notes.reserve(50000);
        for (int i = 0; i < 50000; ++i)
        {
            NoteEvent note;
            note.id = 400000 + i;
            note.pitch = 48 + i % 24;
            note.startBeat = i * 0.01;
            notes.push_back(note);
        }
        midi.setAllNotes(notes, false);
        tracks.syncActiveClipNotes(notes);
        const double began = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 60; ++i)
        {
            midi.getNotes()[(size_t)i].pitch += 1;
            midi.triggerNotePreviewChanged();
            tracks.syncActiveClipNotes(midi.getNotes());
        }
        appendLog(" 60 preview snapshots / 50000 notes: "
            + juce::String(juce::Time::getMillisecondCounterHiRes() - began, 1) + " ms");
        const double batchedBegan = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 60; ++i)
        {
            midi.getNotes()[(size_t)i].pitch += 1;
            midi.triggerNotePreviewChanged(true);
        }
        midi.updateSnapshot();
        tracks.syncActiveClipNotes(midi.getNotes(), false);
        tracks.refreshPlaybackSnapshotIfDirty();
        appendLog(" Batched preview / 50000 notes: "
            + juce::String(juce::Time::getMillisecondCounterHiRes() - batchedBegan, 1) + " ms");
    }

    appendLog("===================================================");
    appendLog(juce::String(" Result: ") + juce::String(testsPassed) + " / " + juce::String(testsTotal) + " passed.");
    appendLog("===================================================");

    return (testsPassed == testsTotal) ? 0 : 1;
}

} // namespace Harmonizer
