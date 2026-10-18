#include "NoteEditOperations.h"
#include "UndoActions.h"
#include <algorithm>

namespace Harmonizer
{

bool NoteEditOperations::transposeSelectedOctave(MidiModel& model, TrackModel& tracks,
                                                   juce::UndoManager& undo, int direction)
{
    if (direction != 1 && direction != -1)
        return false;
    const auto* track = tracks.getActiveInstrumentTrack();
    const auto* clip = tracks.getActiveClip();
    if (track == nullptr || track->getType() != TrackType::Instrument
        || track->isLocked() || clip == nullptr)
        return false;

    const auto before = model.getNotes();
    auto after = before;
    bool selected = false;
    const int offset = direction * 12;
    for (auto& note : after)
    {
        if (!note.isSelected) continue;
        selected = true;
        if (note.pitch + offset < 0 || note.pitch + offset > 127)
            return false;
        note.pitch += offset;
    }
    if (!selected) return false;

    const auto actionName = direction > 0 ? "Transpose Selected Notes Up an Octave"
                                          : "Transpose Selected Notes Down an Octave";
    return UndoActionHelper::performAtomic(&undo,
        new ModifyClipNotesUndoAction(model, tracks, tracks.getActiveTrackId(),
                                      tracks.getActiveClipId(), before, after), actionName);
}

} // namespace Harmonizer
