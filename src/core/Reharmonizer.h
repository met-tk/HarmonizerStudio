#pragma once

#include "MidiModel.h"
#include "TrackModel.h"

namespace Harmonizer
{

enum class ReharmonizationAlgorithm
{
    DiatonicCommonTone = 0,
    TritoneSubstitution = 1,
    ParallelMode = 2,
    ChromaticMediant = 3,
    FunctionalSubstitution = 4,
    BackdoorDominant = 5,
    SmoothDiatonic = 6
};

inline constexpr int kReharmonizationAlgorithmCount = 7;

struct ReharmonizationResult
{
    struct GroupPreview
    {
        double startBeat = 0.0;
        juce::String sourceChord;
        juce::String targetChord;
        juce::String issue;
        int rootMotion = 0;
        int commonTones = 0;
        bool supported = false;
    };

    juce::String error;
    std::vector<NoteEvent> changedNotes;
    std::vector<GroupPreview> groups;
    int groupCount = 0;

    bool succeeded() const { return error.isEmpty(); }
    bool hasChanges() const { return succeeded() && !changedNotes.empty(); }
};

class Reharmonizer
{
public:
    // Treat all selected note events as one chord, preserving each event's timing.
    static ReharmonizationResult prepare(const MidiModel& model, TrackModel& tracks,
                                         ReharmonizationAlgorithm algorithm);
};

} // namespace Harmonizer
