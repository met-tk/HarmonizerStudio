#pragma once

#include "../core/MidiModel.h"
#include <vector>

namespace Harmonizer
{

/** A phrase and the target it was generated for in the source context. */
struct ChordPlacementGroup
{
    std::vector<NoteEvent> notes;
    juce::String originalTargetChord;
    juce::String originalTargetFunction;
    bool originalTargetIsRootOnly = false;
};

} // namespace Harmonizer
