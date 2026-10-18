#pragma once

#include "MidiModel.h"
#include <vector>

namespace Harmonizer
{

class TrackModel;
struct QuickChordType;

struct QuickChordPlan
{
    // With a selection, these are the new upper voices. Without a selection,
    // generated contains the complete C4 chord for pointer placement.
    std::vector<NoteEvent> generated;
    std::vector<int64_t> selectedUpperIdsToRemove;
    bool requiresPlacement = false;
    juce::String error;

    bool succeeded() const { return error.isEmpty() && !generated.empty(); }
};

class QuickChord
{
public:
    static QuickChordPlan prepare(const MidiModel& model, const TrackModel& tracks,
                                  const QuickChordType& type,
                                  bool preserveSelectedTiming = true);
};

} // namespace Harmonizer
