#pragma once

#include "MidiModel.h"
#include "TrackModel.h"
#include <optional>

namespace Harmonizer
{

enum class SecondaryDominantTargetSource
{
    SelectedChords = 0, // All selected notes form one target chord and one block voicing.
    SelectedNotes = 1,  // Each selected note is its own target.
    NextHarmony = 2,    // Legacy search in subsequent chord/MIDI events.
    ManualRoot = 3
};

struct SecondaryDominantOptions
{
    // A non-negative root overrides the selected target source (also used by ManualRoot).
    int targetRootPc = -1;
    bool flatNine = false;
    bool rootInBass = false;
    SecondaryDominantTargetSource targetSource = SecondaryDominantTargetSource::SelectedChords;
};

struct SecondaryDominantResult
{
    juce::String error;
    std::vector<NoteEvent> modifiedNotes;
    std::vector<NoteEvent> addedNotes; // Call generateNextNoteId before applying.
    std::vector<int> targetRoots;
    std::vector<std::vector<NoteEvent>> generatedGroups; // Exact notes of each generated harmony.
    std::vector<juce::String> targetChordNames;    // One per target group.
    std::vector<bool> targetChordIsRootOnly;        // True when only a target root was supplied.
    std::vector<juce::String> generatedChordNames; // Identified from notes actually placed.
    std::vector<juce::String> targetFunctions;     // V/I is tonic dominant; V/ii etc. are secondary.
    int groupCount = 0;
    bool blockChord = false; // addedNotes contains the complete new chord, not just missing voices.

    bool succeeded() const { return error.isEmpty(); }
    bool hasChanges() const { return succeeded() && (!modifiedNotes.empty() || !addedNotes.empty()); }
};

class SecondaryDominant
{
public:
    static int countSelectedGroups(const MidiModel& model);
    static std::optional<int> inferTargetRoot(double groupBeat, const MidiModel& model,
                                               TrackModel& tracks);
    static SecondaryDominantResult prepare(const MidiModel& model, TrackModel& tracks,
                                            const SecondaryDominantOptions& options);
    // Produces a new chord phrase from the selected notes without editing the source.
    static std::vector<NoteEvent> createGeneratedNotes(const MidiModel& model,
                                                        const SecondaryDominantResult& result);
    static std::vector<NoteEvent> shiftGeneratedNotes(const std::vector<NoteEvent>& generated,
                                                       double targetStartBeat);
};

} // namespace Harmonizer
