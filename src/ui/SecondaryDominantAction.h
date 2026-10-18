#pragma once

#include "../core/MidiModel.h"
#include "../core/TrackModel.h"
#include "ChordPlacementGroup.h"
#include <functional>

namespace Harmonizer
{

// Generate a preview immediately using the saved Secondary Dominant settings.
// Only the placement callback may change the MIDI clip.
void generateSecondaryDominantPreview(MidiModel& model, TrackModel& tracks,
                                      std::function<void(std::vector<ChordPlacementGroup>)> onReadyToPlace);

} // namespace Harmonizer
