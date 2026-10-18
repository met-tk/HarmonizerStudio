#pragma once

#include "MidiModel.h"
#include "TrackModel.h"

namespace Harmonizer
{

enum class ChordShapeQuality {
    Major, Minor, Diminished, Augmented, Sus2, Sus4,
    Dominant7, Major7, Minor7, HalfDiminished7
};
enum class ChordShapeVoicing { Expand, Contract, Drop2 };
enum class ChordStackDirection {
    AddMajor, AddMinor, AddDominant, AddMode,
    RemoveMajor, RemoveMinor, RemoveDominant, RemoveMode
};
enum class ModeBorrowingStyle { SameTonic, Approach };

struct ChordEditPlan
{
    struct Group
    {
        double beat = 0.0;
        juce::String source;
        juce::String target;
        juce::String sourceMode;
        juce::String targetMode;
        juce::String issue;
        bool supported = false;
        int changedVoices = 0;
    };

    juce::String error;
    std::vector<NoteEvent> changedNotes;
    std::vector<NoteEvent> addedNotes;
    std::vector<int64_t> removedNoteIds;
    std::vector<Group> groups;
    bool succeeded() const { return error.isEmpty(); }
    bool hasChanges() const { return succeeded() && (!changedNotes.empty()
        || !addedNotes.empty() || !removedNoteIds.empty()); }
};

class ChordEditing
{
public:
    // The baseline belongs to the active MIDI clip. Keeping it separate from
    // the current notes lets repeated quality clicks retain the first voicing.
    static ChordEditPlan changeQuality(const MidiModel& model, const TrackModel& tracks,
                                      const std::vector<NoteEvent>& baseline,
                                      ChordShapeQuality quality);
    static ChordEditPlan changeVoicing(const MidiModel& model, const TrackModel& tracks,
                                      const std::vector<NoteEvent>& baseline,
                                      ChordShapeVoicing voicing);
    static ChordEditPlan changeStack(const MidiModel& model, const TrackModel& tracks,
                                    const std::vector<NoteEvent>& selected,
                                    ChordStackDirection direction);
    // degree = -1 maps each selected chord's root to the target mode;
    // 0..6 explicitly chooses a scale degree.
    static ChordEditPlan borrow(const MidiModel& model, const TrackModel& tracks,
                               const std::vector<NoteEvent>& selected,
                               int sourceTonicPc, const juce::String& sourceScale,
                               int targetTonicPc, const juce::String& targetScale,
                               ModeBorrowingStyle style,
                               int degree = -1, bool automaticSource = false);
    static juce::String borrowedChordName(int tonicPc, const juce::String& sourceScale,
                                         int degree, bool seventh);
    /** Creates the exact triad used by the empty-selection borrowing preview. */
    static std::vector<NoteEvent> createBorrowedChordNotes(
        int tonicPc, const juce::String& targetScale, int degree,
        double startBeat, double lengthBeats);
};

} // namespace Harmonizer
