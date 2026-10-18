#pragma once

#include "MidiModel.h"
#include <juce_data_structures/juce_data_structures.h>

namespace Harmonizer
{

class TrackModel;
enum class TrackType;
class Track;

class AddNotesUndoAction : public juce::UndoableAction
{
public:
    AddNotesUndoAction(MidiModel& model, const std::vector<NoteEvent>& notesAdded, TrackModel* trackModel = nullptr);
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return (int)notes.size() + 1; }

private:
    MidiModel& midiModel;
    std::vector<NoteEvent> notes;
    TrackModel* trackModel = nullptr;
    int targetTrackId = -1;
    int64_t targetClipId = -1;
};

class DeleteNotesUndoAction : public juce::UndoableAction
{
public:
    DeleteNotesUndoAction(MidiModel& model, const std::vector<NoteEvent>& notesDeleted, TrackModel* trackModel = nullptr);
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return (int)notes.size() + 1; }

private:
    MidiModel& midiModel;
    std::vector<NoteEvent> notes;
    TrackModel* trackModel = nullptr;
    int targetTrackId = -1;
    int64_t targetClipId = -1;
};

class ModifyNotesUndoAction : public juce::UndoableAction
{
public:
    ModifyNotesUndoAction(MidiModel& model, 
                          const std::vector<NoteEvent>& oldState, 
                          const std::vector<NoteEvent>& newState,
                          const juce::String& actionDescription = "Modify Notes",
                          TrackModel* trackModel = nullptr);
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return (int)(before.size() + after.size()) + 1; }

private:
    MidiModel& midiModel;
    std::vector<NoteEvent> before;
    std::vector<NoteEvent> after;
    juce::String description;
    TrackModel* trackModel = nullptr;
    int targetTrackId = -1;
    int64_t targetClipId = -1;
};

// Replaces one clip's complete note state, even if a different clip is active
// when Undo or Redo is invoked.
class ModifyClipNotesUndoAction : public juce::UndoableAction
{
public:
    ModifyClipNotesUndoAction(MidiModel& model, TrackModel& tracks, int trackId, int64_t clipId,
                              const std::vector<NoteEvent>& before,
                              const std::vector<NoteEvent>& after);
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return (int)(before.size() + after.size()) + 1; }

private:
    bool apply(const std::vector<NoteEvent>& notes);
    MidiModel& midiModel;
    TrackModel& trackModel;
    int targetTrackId;
    int64_t targetClipId;
    std::vector<NoteEvent> before;
    std::vector<NoteEvent> after;
};

class AddTrackUndoAction : public juce::UndoableAction
{
public:
    AddTrackUndoAction(TrackModel& model, const juce::String& name, TrackType type);
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 10; }

private:
    TrackModel& trackModel;
    juce::String trackName;
    TrackType trackType;
    int createdTrackId = -1;
    int insertedIndex = -1;
    std::unique_ptr<Track> savedTrack;
};

class DeleteTrackUndoAction : public juce::UndoableAction
{
public:
    DeleteTrackUndoAction(TrackModel& model, int trackId);
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 10; }

private:
    TrackModel& trackModel;
    int targetTrackId;
    int originalIndex = -1;
    std::vector<int> directChildIds;
    std::unique_ptr<Track> savedTrack;
};

class DuplicateTrackUndoAction : public juce::UndoableAction
{
public:
    DuplicateTrackUndoAction(TrackModel& model, int sourceTrackId);
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 10; }

private:
    TrackModel& trackModel;
    int srcTrackId;
    int createdTrackId = -1;
    std::vector<int> createdTrackIds;
    std::vector<std::pair<int, std::unique_ptr<Track>>> savedTracks;
};

enum class TrackPropertyType
{
    Name,
    Color,
    ParentFolder
};

class TrackPropertyUndoAction : public juce::UndoableAction
{
public:
    TrackPropertyUndoAction(TrackModel& model, int trackId,
                            const juce::String& oldName, const juce::String& newName);
    TrackPropertyUndoAction(TrackModel& model, int trackId,
                            juce::Colour oldColor, juce::Colour newColor);
    TrackPropertyUndoAction(TrackModel& model, int trackId,
                            int oldFolderId, int newFolderId);
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 2; }

private:
    bool applyProperty(const juce::String& strVal, juce::Colour colVal, int intVal);

    TrackModel& trackModel;
    int trkId;
    TrackPropertyType propType;
    juce::String oldStrVal, newStrVal;
    juce::Colour oldColorVal, newColorVal;
    int oldIntVal = -1, newIntVal = -1;
};

class MoveTrackUndoAction : public juce::UndoableAction
{
public:
    MoveTrackUndoAction(TrackModel& model, int fromIdx, int toIdx, int trackId = -1, int oldParent = -1, int newParent = -1);
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 5; }

private:
    TrackModel& trackModel;
    int fromIndex;
    int toIndex;
    int trkId;
    int oldParentId;
    int newParentId;
};

//==============================================================================
// Event / Clip Undo Actions for Track Arranger
//==============================================================================
struct MidiClip;
struct ChordEvent;
struct ModeEvent;
struct MarkerEvent;
struct TimeSignatureEvent;
struct TempoEvent;
struct AudioClip;

class ModifyClipsUndoAction : public juce::UndoableAction
{
public:
    ModifyClipsUndoAction(TrackModel& model, int trackId,
                          const std::vector<MidiClip>& before,
                          const std::vector<MidiClip>& after,
                          const juce::String& actionName = "Modify Clips");
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 10; }

private:
    TrackModel& trackModel;
    int trackId;
    std::vector<MidiClip> oldClips;
    std::vector<MidiClip> newClips;
    juce::String name;
};

class ModifyAudioClipsUndoAction : public juce::UndoableAction
{
public:
    ModifyAudioClipsUndoAction(TrackModel& model, int trackId,
                               std::vector<AudioClip> before,
                               std::vector<AudioClip> after);
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 10; }

private:
    TrackModel& trackModel;
    int trackId;
    std::vector<AudioClip> oldClips;
    std::vector<AudioClip> newClips;
};

class ModifyChordsUndoAction : public juce::UndoableAction
{
public:
    ModifyChordsUndoAction(TrackModel& model, int trackId,
                           const std::vector<ChordEvent>& before,
                           const std::vector<ChordEvent>& after,
                           const juce::String& actionName = "Modify Chords");
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 10; }

private:
    TrackModel& trackModel;
    int trackId;
    std::vector<ChordEvent> oldChords;
    std::vector<ChordEvent> newChords;
    juce::String name;
};

class ModifyModesUndoAction : public juce::UndoableAction
{
public:
    ModifyModesUndoAction(TrackModel& model, int trackId,
                          const std::vector<ModeEvent>& before,
                          const std::vector<ModeEvent>& after,
                          const juce::String& actionName = "Modify Modes");
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 10; }

private:
    TrackModel& trackModel;
    int trackId;
    std::vector<ModeEvent> oldModes;
    std::vector<ModeEvent> newModes;
    juce::String name;
};

// Applies the project key/scale and its mode-track event as one undo step.
class ApplySelectedModeUndoAction : public juce::UndoableAction
{
public:
    ApplySelectedModeUndoAction(MidiModel& model, TrackModel& tracks,
                                KeySignature beforeKey, juce::String beforeScale,
                                KeySignature afterKey, juce::String afterScale,
                                int modeTrackId, std::vector<ModeEvent> beforeModes,
                                std::vector<ModeEvent> afterModes);
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 10; }

private:
    bool apply(const KeySignature& key, const juce::String& scale,
               const std::vector<ModeEvent>& modes);
    MidiModel& midiModel;
    TrackModel& trackModel;
    KeySignature oldKey, newKey;
    juce::String oldScale, newScale;
    int targetModeTrackId;
    std::vector<ModeEvent> oldModes, newModes;
};

class ModifyMarkersUndoAction : public juce::UndoableAction
{
public:
    ModifyMarkersUndoAction(TrackModel& model, int trackId,
                            const std::vector<MarkerEvent>& before,
                            const std::vector<MarkerEvent>& after,
                            const juce::String& actionName = "Modify Markers");
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 10; }

private:
    TrackModel& trackModel;
    int trackId;
    std::vector<MarkerEvent> oldMarkers;
    std::vector<MarkerEvent> newMarkers;
    juce::String name;
};

class ModifyTimeSignaturesUndoAction : public juce::UndoableAction
{
public:
    ModifyTimeSignaturesUndoAction(TrackModel& model, int trackId,
                                   const std::vector<TimeSignatureEvent>& before,
                                   const std::vector<TimeSignatureEvent>& after,
                                   const juce::String& actionName = "Modify Time Signatures");
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 10; }

private:
    TrackModel& trackModel;
    int trackId;
    std::vector<TimeSignatureEvent> oldSigs;
    std::vector<TimeSignatureEvent> newSigs;
    juce::String name;
};

class ModifyTempoEventsUndoAction : public juce::UndoableAction
{
public:
    ModifyTempoEventsUndoAction(TrackModel& model, int trackId,
                                const std::vector<TempoEvent>& before,
                                const std::vector<TempoEvent>& after,
                                const juce::String& actionName = "Modify Tempo Events");
    bool perform() override;
    bool undo() override;
    int getSizeInUnits() override { return 10; }

private:
    TrackModel& trackModel;
    int trackId;
    std::vector<TempoEvent> oldTempos;
    std::vector<TempoEvent> newTempos;
    juce::String name;
};

class CompoundUndoAction : public juce::UndoableAction
{
public:
    CompoundUndoAction(const juce::String& actionDescription = "Compound Action")
        : description(actionDescription)
    {
    }

    CompoundUndoAction(std::vector<std::unique_ptr<juce::UndoableAction>> subActions,
                       const juce::String& actionDescription = "Compound Action")
        : actions(std::move(subActions)), description(actionDescription)
    {
    }

    void addAction(std::unique_ptr<juce::UndoableAction> action)
    {
        if (action != nullptr)
            actions.push_back(std::move(action));
    }

    void addAction(juce::UndoableAction* action)
    {
        if (action != nullptr)
            actions.push_back(std::unique_ptr<juce::UndoableAction>(action));
    }

    bool isEmpty() const noexcept { return actions.empty(); }
    size_t size() const noexcept { return actions.size(); }

    bool perform() override
    {
        for (auto& a : actions)
        {
            if (a != nullptr && !a->perform())
                return false;
        }
        return true;
    }

    bool undo() override
    {
        for (auto it = actions.rbegin(); it != actions.rend(); ++it)
        {
            if (*it != nullptr && !(*it)->undo())
                return false;
        }
        return true;
    }

    int getSizeInUnits() override
    {
        int total = 1;
        for (const auto& a : actions)
            if (a != nullptr) total += a->getSizeInUnits();
        return total;
    }

private:
    std::vector<std::unique_ptr<juce::UndoableAction>> actions;
    juce::String description;
};

struct UndoActionHelper
{
    static bool performAtomic(juce::UndoManager* um, juce::UndoableAction* action, const juce::String& name = {})
    {
        if (action == nullptr)
            return false;

        if (um == nullptr)
        {
            bool ok = action->perform();
            delete action;
            return ok;
        }

        um->beginNewTransaction(name);
        bool ok = um->perform(action, name);
        um->beginNewTransaction();
        return ok;
    }
};

} // namespace Harmonizer
