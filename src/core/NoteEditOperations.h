#pragma once

#include "MidiModel.h"
#include "TrackModel.h"

namespace Harmonizer
{

class NoteEditOperations
{
public:
    // Moves the entire selected chord by exactly one octave. An out-of-range
    // note cancels the whole edit instead of flattening its interval at 0/127.
    static bool transposeSelectedOctave(MidiModel& model, TrackModel& tracks,
                                        juce::UndoManager& undo, int direction);
};

} // namespace Harmonizer
