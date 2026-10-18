#include "UndoActions.h"
#include "TrackModel.h"
#include <unordered_set>

namespace Harmonizer
{

namespace
{
void captureNoteTarget(TrackModel* tracks, int& trackId, int64_t& clipId)
{
    if (tracks != nullptr && tracks->getActiveClip() != nullptr)
    {
        trackId = tracks->getActiveTrackId();
        clipId = tracks->getActiveClipId();
    }
}

template <typename Edit>
bool editCapturedClip(MidiModel& midi, TrackModel& tracks, int trackId,
                      int64_t clipId, Edit&& edit)
{
    auto* track = tracks.getTrackById(trackId);
    if (track == nullptr || track->getType() != TrackType::Instrument)
        return false;
    auto& clips = track->getClips();
    auto clip = std::find_if(clips.begin(), clips.end(), [clipId](const MidiClip& item) {
        return item.id == clipId;
    });
    if (clip == clips.end()) return false;

    edit(clip->notes);
    if (tracks.getActiveTrackId() == trackId && tracks.getActiveClipId() == clipId)
        midi.setAllNotes(clip->notes, true);
    tracks.triggerClipNotesCommitted();
    return true;
}
}

AddNotesUndoAction::AddNotesUndoAction(MidiModel& model, const std::vector<NoteEvent>& notesAdded, TrackModel* tm)
    : midiModel(model), notes(notesAdded), trackModel(tm)
{
    captureNoteTarget(trackModel, targetTrackId, targetClipId);
}

bool AddNotesUndoAction::perform()
{
    if (trackModel != nullptr && targetClipId >= 0)
        return editCapturedClip(midiModel, *trackModel, targetTrackId, targetClipId,
            [this](std::vector<NoteEvent>& clipNotes) {
                std::unordered_set<int64_t> existingIds;
                existingIds.reserve(clipNotes.size() + notes.size());
                for (const auto& existing : clipNotes) existingIds.insert(existing.id);
                for (const auto& note : notes)
                    if (existingIds.insert(note.id).second)
                        clipNotes.push_back(note);
            });
    std::unordered_set<int64_t> existingIds;
    existingIds.reserve(midiModel.getNotes().size() + notes.size());
    for (const auto& note : midiModel.getNotes()) existingIds.insert(note.id);
    std::vector<NoteEvent> toAdd;
    toAdd.reserve(notes.size());
    for (const auto& note : notes)
        if (existingIds.insert(note.id).second) toAdd.push_back(note);
    midiModel.addNotes(toAdd, false);
    midiModel.triggerNoteListChanged();
    if (trackModel != nullptr)
        trackModel->syncActiveClipNotes(midiModel.getNotes());
    return true;
}

bool AddNotesUndoAction::undo()
{
    if (trackModel != nullptr && targetClipId >= 0)
        return editCapturedClip(midiModel, *trackModel, targetTrackId, targetClipId,
            [this](std::vector<NoteEvent>& clipNotes) {
                std::unordered_set<int64_t> removedIds;
                removedIds.reserve(notes.size());
                for (const auto& note : notes) removedIds.insert(note.id);
                clipNotes.erase(std::remove_if(clipNotes.begin(), clipNotes.end(),
                    [&](const NoteEvent& existing) {
                        return removedIds.count(existing.id) != 0;
                    }), clipNotes.end());
            });
    std::vector<int64_t> ids;
    for (const auto& n : notes)
        ids.push_back(n.id);
    midiModel.removeNotesById(ids, true);
    if (trackModel != nullptr)
        trackModel->syncActiveClipNotes(midiModel.getNotes());
    return true;
}

//==============================================================================
DeleteNotesUndoAction::DeleteNotesUndoAction(MidiModel& model, const std::vector<NoteEvent>& notesDeleted, TrackModel* tm)
    : midiModel(model), notes(notesDeleted), trackModel(tm)
{
    captureNoteTarget(trackModel, targetTrackId, targetClipId);
}

bool DeleteNotesUndoAction::perform()
{
    if (trackModel != nullptr && targetClipId >= 0)
        return editCapturedClip(midiModel, *trackModel, targetTrackId, targetClipId,
            [this](std::vector<NoteEvent>& clipNotes) {
                std::unordered_set<int64_t> removedIds;
                removedIds.reserve(notes.size());
                for (const auto& note : notes) removedIds.insert(note.id);
                clipNotes.erase(std::remove_if(clipNotes.begin(), clipNotes.end(),
                    [&](const NoteEvent& existing) {
                        return removedIds.count(existing.id) != 0;
                    }), clipNotes.end());
            });
    std::vector<int64_t> ids;
    for (const auto& n : notes)
        ids.push_back(n.id);
    midiModel.removeNotesById(ids, true);
    if (trackModel != nullptr)
        trackModel->syncActiveClipNotes(midiModel.getNotes());
    return true;
}

bool DeleteNotesUndoAction::undo()
{
    if (trackModel != nullptr && targetClipId >= 0)
        return editCapturedClip(midiModel, *trackModel, targetTrackId, targetClipId,
            [this](std::vector<NoteEvent>& clipNotes) {
                std::unordered_set<int64_t> existingIds;
                existingIds.reserve(clipNotes.size() + notes.size());
                for (const auto& existing : clipNotes) existingIds.insert(existing.id);
                for (const auto& note : notes)
                    if (existingIds.insert(note.id).second)
                        clipNotes.push_back(note);
            });
    midiModel.addNotes(notes, true);
    if (trackModel != nullptr)
        trackModel->syncActiveClipNotes(midiModel.getNotes());
    return true;
}

//==============================================================================
ModifyNotesUndoAction::ModifyNotesUndoAction(MidiModel& model, 
                                             const std::vector<NoteEvent>& oldState, 
                                             const std::vector<NoteEvent>& newState,
                                             const juce::String& actionDescription,
                                             TrackModel* tm)
    : midiModel(model), before(oldState), after(newState), description(actionDescription), trackModel(tm)
{
    captureNoteTarget(trackModel, targetTrackId, targetClipId);
}

bool ModifyNotesUndoAction::perform()
{
    if (trackModel != nullptr && targetClipId >= 0)
        return editCapturedClip(midiModel, *trackModel, targetTrackId, targetClipId,
            [this](std::vector<NoteEvent>& clipNotes) { clipNotes = after; });
    midiModel.setAllNotes(after, true);
    if (trackModel != nullptr)
        trackModel->syncActiveClipNotes(midiModel.getNotes());
    return true;
}

bool ModifyNotesUndoAction::undo()
{
    if (trackModel != nullptr && targetClipId >= 0)
        return editCapturedClip(midiModel, *trackModel, targetTrackId, targetClipId,
            [this](std::vector<NoteEvent>& clipNotes) { clipNotes = before; });
    midiModel.setAllNotes(before, true);
    if (trackModel != nullptr)
        trackModel->syncActiveClipNotes(midiModel.getNotes());
    return true;
}

ModifyClipNotesUndoAction::ModifyClipNotesUndoAction(MidiModel& model, TrackModel& tracks,
    int trackId, int64_t clipId, const std::vector<NoteEvent>& oldState,
    const std::vector<NoteEvent>& newState)
    : midiModel(model), trackModel(tracks), targetTrackId(trackId), targetClipId(clipId),
      before(oldState), after(newState)
{
}

bool ModifyClipNotesUndoAction::apply(const std::vector<NoteEvent>& notes)
{
    auto* track = trackModel.getTrackById(targetTrackId);
    if (track == nullptr || track->getType() != TrackType::Instrument)
        return false;
    auto& clips = track->getClips();
    auto it = std::find_if(clips.begin(), clips.end(), [this](const MidiClip& clip) {
        return clip.id == targetClipId;
    });
    if (it == clips.end())
        return false;

    it->notes = notes;
    if (trackModel.getActiveTrackId() == targetTrackId && trackModel.getActiveClipId() == targetClipId)
        midiModel.setAllNotes(notes, true);
    trackModel.triggerClipNotesCommitted();
    return true;
}

bool ModifyClipNotesUndoAction::perform() { return apply(after); }
bool ModifyClipNotesUndoAction::undo() { return apply(before); }

//==============================================================================
AddTrackUndoAction::AddTrackUndoAction(TrackModel& model, const juce::String& name, TrackType type)
    : trackModel(model), trackName(name), trackType(type)
{
}

bool AddTrackUndoAction::perform()
{
    if (savedTrack != nullptr)
    {
        trackModel.insertTrack(insertedIndex, std::move(savedTrack));
        savedTrack = nullptr;
    }
    else
    {
        auto* trk = trackModel.addTrack(trackName, trackType);
        if (trk)
        {
            createdTrackId = trk->getId();
            insertedIndex = (int)trackModel.getTracks().size() - 1;
        }
    }
    return true;
}

bool AddTrackUndoAction::undo()
{
    if (createdTrackId > 0)
    {
        savedTrack = trackModel.extractTrack(createdTrackId, insertedIndex);
        return savedTrack != nullptr;
    }
    return false;
}

//==============================================================================
DeleteTrackUndoAction::DeleteTrackUndoAction(TrackModel& model, int trackId)
    : trackModel(model), targetTrackId(trackId)
{
}

bool DeleteTrackUndoAction::perform()
{
    directChildIds.clear();
    if (const auto* track = trackModel.getTrackById(targetTrackId))
        if (track->getType() == TrackType::Folder)
            directChildIds = trackModel.getChildTrackIds(targetTrackId, false);
    savedTrack = trackModel.extractTrack(targetTrackId, originalIndex);
    return savedTrack != nullptr;
}

bool DeleteTrackUndoAction::undo()
{
    if (savedTrack != nullptr)
    {
        trackModel.insertTrack(originalIndex, std::move(savedTrack));
        savedTrack = nullptr;
        for (int childId : directChildIds)
            trackModel.setTrackParent(childId, targetTrackId);
        return true;
    }
    return false;
}

//==============================================================================
DuplicateTrackUndoAction::DuplicateTrackUndoAction(TrackModel& model, int sourceTrackId)
    : trackModel(model), srcTrackId(sourceTrackId)
{
}

bool DuplicateTrackUndoAction::perform()
{
    if (!savedTracks.empty())
    {
        for (auto& [index, track] : savedTracks)
            trackModel.insertTrack(index, std::move(track));
        savedTracks.clear();
        return true;
    }
    else
    {
        auto* trk = trackModel.duplicateTrack(srcTrackId);
        if (trk)
        {
            createdTrackId = trk->getId();
            createdTrackIds = { createdTrackId };
            if (trk->getType() == TrackType::Folder)
            {
                const auto children = trackModel.getChildTrackIds(createdTrackId, true);
                createdTrackIds.insert(createdTrackIds.end(), children.begin(), children.end());
            }
            return true;
        }
        return false;
    }
}

bool DuplicateTrackUndoAction::undo()
{
    if (createdTrackId > 0 && !createdTrackIds.empty())
    {
        savedTracks.clear();
        for (auto it = createdTrackIds.rbegin(); it != createdTrackIds.rend(); ++it)
        {
            int index = -1;
            auto track = trackModel.extractTrack(*it, index);
            if (track == nullptr)
                return false;
            savedTracks.emplace_back(index, std::move(track));
        }
        std::reverse(savedTracks.begin(), savedTracks.end());
        return true;
    }
    return false;
}

//==============================================================================
TrackPropertyUndoAction::TrackPropertyUndoAction(TrackModel& model, int trackId,
                                                 const juce::String& oldName, const juce::String& newName)
    : trackModel(model), trkId(trackId), propType(TrackPropertyType::Name),
      oldStrVal(oldName), newStrVal(newName)
{
}

TrackPropertyUndoAction::TrackPropertyUndoAction(TrackModel& model, int trackId,
                                                 juce::Colour oldColor, juce::Colour newColor)
    : trackModel(model), trkId(trackId), propType(TrackPropertyType::Color),
      oldColorVal(oldColor), newColorVal(newColor)
{
}

TrackPropertyUndoAction::TrackPropertyUndoAction(TrackModel& model, int trackId,
                                                 int oldFolderId, int newFolderId)
    : trackModel(model), trkId(trackId), propType(TrackPropertyType::ParentFolder),
      oldIntVal(oldFolderId), newIntVal(newFolderId)
{
}

bool TrackPropertyUndoAction::perform()
{
    return applyProperty(newStrVal, newColorVal, newIntVal);
}

bool TrackPropertyUndoAction::undo()
{
    return applyProperty(oldStrVal, oldColorVal, oldIntVal);
}

bool TrackPropertyUndoAction::applyProperty(const juce::String& strVal, juce::Colour colVal, int intVal)
{
    if (auto* t = trackModel.getTrackById(trkId))
    {
        if (propType == TrackPropertyType::Name)
            t->setName(strVal);
        else if (propType == TrackPropertyType::Color)
            t->setColor(colVal);
        else if (propType == TrackPropertyType::ParentFolder)
            return trackModel.setTrackParent(trkId, intVal);

        trackModel.triggerTracksChanged();
        return true;
    }
    return false;
}

//==============================================================================
MoveTrackUndoAction::MoveTrackUndoAction(TrackModel& model, int fromIdx, int toIdx, int trackId, int oldParent, int newParent)
    : trackModel(model), fromIndex(fromIdx), toIndex(toIdx), trkId(trackId), oldParentId(oldParent), newParentId(newParent)
{
}

bool MoveTrackUndoAction::perform()
{
    if (trkId > 0 && oldParentId != newParentId
        && !trackModel.canNestInFolder(trkId, newParentId))
        return false;
    bool ok = trackModel.moveTrack(fromIndex, toIndex);
    if (ok && trkId > 0 && oldParentId != newParentId)
        return trackModel.setTrackParent(trkId, newParentId);
    return ok;
}

bool MoveTrackUndoAction::undo()
{
    bool ok = trackModel.moveTrack(toIndex, fromIndex);
    if (ok && trkId > 0 && oldParentId != newParentId)
        return trackModel.setTrackParent(trkId, oldParentId);
    return ok;
}

//==============================================================================
ModifyClipsUndoAction::ModifyClipsUndoAction(TrackModel& model, int trkId,
                                             const std::vector<MidiClip>& before,
                                             const std::vector<MidiClip>& after,
                                             const juce::String& actionName)
    : trackModel(model), trackId(trkId), oldClips(before), newClips(after), name(actionName)
{
}

bool ModifyClipsUndoAction::perform()
{
    if (auto* t = trackModel.getTrackById(trackId))
    {
        t->getClips() = newClips;
        trackModel.triggerTracksChanged();
        return true;
    }
    return false;
}

bool ModifyClipsUndoAction::undo()
{
    if (auto* t = trackModel.getTrackById(trackId))
    {
        t->getClips() = oldClips;
        trackModel.triggerTracksChanged();
        return true;
    }
    return false;
}

ModifyAudioClipsUndoAction::ModifyAudioClipsUndoAction(TrackModel& model, int id,
                                                       std::vector<AudioClip> before,
                                                       std::vector<AudioClip> after)
    : trackModel(model), trackId(id), oldClips(std::move(before)), newClips(std::move(after)) {}

bool ModifyAudioClipsUndoAction::perform()
{
    if (auto* track = trackModel.getTrackById(trackId);
        track != nullptr && track->getType() == TrackType::Audio)
    {
        track->getAudioClips() = newClips;
        trackModel.triggerTracksChanged();
        return true;
    }
    return false;
}

bool ModifyAudioClipsUndoAction::undo()
{
    if (auto* track = trackModel.getTrackById(trackId);
        track != nullptr && track->getType() == TrackType::Audio)
    {
        track->getAudioClips() = oldClips;
        trackModel.triggerTracksChanged();
        return true;
    }
    return false;
}

//==============================================================================
ModifyChordsUndoAction::ModifyChordsUndoAction(TrackModel& model, int trkId,
                                               const std::vector<ChordEvent>& before,
                                               const std::vector<ChordEvent>& after,
                                               const juce::String& actionName)
    : trackModel(model), trackId(trkId), oldChords(before), newChords(after), name(actionName)
{
}

bool ModifyChordsUndoAction::perform()
{
    if (auto* t = trackModel.getTrackById(trackId))
    {
        t->getChords() = newChords;
        trackModel.triggerChordsChanged();
        return true;
    }
    return false;
}

bool ModifyChordsUndoAction::undo()
{
    if (auto* t = trackModel.getTrackById(trackId))
    {
        t->getChords() = oldChords;
        trackModel.triggerChordsChanged();
        return true;
    }
    return false;
}

//==============================================================================
ModifyModesUndoAction::ModifyModesUndoAction(TrackModel& model, int trkId,
                                             const std::vector<ModeEvent>& before,
                                             const std::vector<ModeEvent>& after,
                                             const juce::String& actionName)
    : trackModel(model), trackId(trkId), oldModes(before), newModes(after), name(actionName)
{
}

bool ModifyModesUndoAction::perform()
{
    if (auto* t = trackModel.getTrackById(trackId))
    {
        t->getModes() = newModes;
        trackModel.triggerModesChanged();
        return true;
    }
    return false;
}

bool ModifyModesUndoAction::undo()
{
    if (auto* t = trackModel.getTrackById(trackId))
    {
        t->getModes() = oldModes;
        trackModel.triggerModesChanged();
        return true;
    }
    return false;
}

ApplySelectedModeUndoAction::ApplySelectedModeUndoAction(
    MidiModel& model, TrackModel& tracks, KeySignature beforeKey,
    juce::String beforeScale, KeySignature afterKey, juce::String afterScale,
    int modeTrackId, std::vector<ModeEvent> beforeModes,
    std::vector<ModeEvent> afterModes)
    : midiModel(model), trackModel(tracks), oldKey(beforeKey), newKey(afterKey),
      oldScale(std::move(beforeScale)), newScale(std::move(afterScale)),
      targetModeTrackId(modeTrackId), oldModes(std::move(beforeModes)),
      newModes(std::move(afterModes))
{
}

bool ApplySelectedModeUndoAction::apply(const KeySignature& key, const juce::String& scale,
                                        const std::vector<ModeEvent>& modes)
{
    // Validate before touching project settings so a deleted mode track cannot
    // leave a half-applied transaction behind.
    Track* modeTrack = nullptr;
    if (targetModeTrackId > 0)
    {
        modeTrack = trackModel.getTrackById(targetModeTrackId);
        if (modeTrack == nullptr || modeTrack->getType() != TrackType::Mode)
            return false;
    }
    midiModel.setKeySignature(key);
    midiModel.setScaleName(scale);
    if (modeTrack != nullptr)
    {
        modeTrack->getModes() = modes;
        trackModel.triggerModesChanged();
    }
    return true;
}

bool ApplySelectedModeUndoAction::perform() { return apply(newKey, newScale, newModes); }
bool ApplySelectedModeUndoAction::undo() { return apply(oldKey, oldScale, oldModes); }

//==============================================================================
ModifyMarkersUndoAction::ModifyMarkersUndoAction(TrackModel& model, int trkId,
                                                 const std::vector<MarkerEvent>& before,
                                                 const std::vector<MarkerEvent>& after,
                                                 const juce::String& actionName)
    : trackModel(model), trackId(trkId), oldMarkers(before), newMarkers(after), name(actionName)
{
}

bool ModifyMarkersUndoAction::perform()
{
    if (auto* t = trackModel.getTrackById(trackId))
    {
        t->getMarkers() = newMarkers;
        trackModel.triggerMarkersChanged();
        return true;
    }
    return false;
}

bool ModifyMarkersUndoAction::undo()
{
    if (auto* t = trackModel.getTrackById(trackId))
    {
        t->getMarkers() = oldMarkers;
        trackModel.triggerMarkersChanged();
        return true;
    }
    return false;
}

//==============================================================================
ModifyTimeSignaturesUndoAction::ModifyTimeSignaturesUndoAction(TrackModel& model, int trkId,
                                                               const std::vector<TimeSignatureEvent>& before,
                                                               const std::vector<TimeSignatureEvent>& after,
                                                               const juce::String& actionName)
    : trackModel(model), trackId(trkId), oldSigs(before), newSigs(after), name(actionName)
{
}

bool ModifyTimeSignaturesUndoAction::perform()
{
    if (auto* t = trackModel.getTrackById(trackId))
    {
        t->getTimeSignatures() = newSigs;
        trackModel.triggerTracksChanged();
        return true;
    }
    return false;
}

bool ModifyTimeSignaturesUndoAction::undo()
{
    if (auto* t = trackModel.getTrackById(trackId))
    {
        t->getTimeSignatures() = oldSigs;
        trackModel.triggerTracksChanged();
        return true;
    }
    return false;
}

//==============================================================================
ModifyTempoEventsUndoAction::ModifyTempoEventsUndoAction(TrackModel& model, int trkId,
                                                         const std::vector<TempoEvent>& before,
                                                         const std::vector<TempoEvent>& after,
                                                         const juce::String& actionName)
    : trackModel(model), trackId(trkId), oldTempos(before), newTempos(after), name(actionName)
{
}

bool ModifyTempoEventsUndoAction::perform()
{
    if (auto* t = trackModel.getTrackById(trackId))
    {
        t->getTempoEvents() = newTempos;
        trackModel.triggerTempoChanged();
        trackModel.triggerTracksChanged();
        return true;
    }
    return false;
}

bool ModifyTempoEventsUndoAction::undo()
{
    if (auto* t = trackModel.getTrackById(trackId))
    {
        t->getTempoEvents() = oldTempos;
        trackModel.triggerTempoChanged();
        trackModel.triggerTracksChanged();
        return true;
    }
    return false;
}

} // namespace Harmonizer
