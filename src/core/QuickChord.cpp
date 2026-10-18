#include "QuickChord.h"
#include "QuickChordCatalog.h"
#include "TrackModel.h"
#include <algorithm>
#include <cmath>

namespace Harmonizer
{

QuickChordPlan QuickChord::prepare(const MidiModel& model, const TrackModel& tracks,
                                   const QuickChordType& type,
                                   bool preserveSelectedTiming)
{
    QuickChordPlan plan;
    auto fail = [&plan](const char8_t* message) {
        plan.generated.clear();
        plan.selectedUpperIdsToRemove.clear();
        plan.error = juce::String::fromUTF8(message);
        return plan;
    };

    const auto* track = tracks.getTrackById(tracks.getActiveTrackId());
    const auto* clip = tracks.getActiveClip();
    if (track == nullptr || track->getType() != TrackType::Instrument || clip == nullptr)
        return fail(u8"请先打开乐器轨上的 MIDI 片段。");
    if (track->isLocked())
        return fail(u8"乐器轨已锁定，无法修改音符。");
    if (type.semitoneIntervals.empty() || type.semitoneIntervals.front() != 0
        || !std::is_sorted(type.semitoneIntervals.begin(), type.semitoneIntervals.end()))
        return fail(u8"和弦音程结构无效。");

    const double clipEnd = clip->startBeat + clip->lengthBeats;
    if (!std::isfinite(clip->startBeat) || !std::isfinite(clipEnd)
        || clip->lengthBeats <= 0.0)
        return fail(u8"当前 MIDI 片段的时间范围无效。");

    auto selected = model.getSelectedNotes();
    if (selected.empty())
    {
        plan.requiresPlacement = true;
        const double length = std::min(1.0, clip->lengthBeats);
        for (int interval : type.semitoneIntervals)
        {
            NoteEvent note;
            note.pitch = 60 + interval;
            if (note.pitch > 127 || note.pitch < 0)
                return fail(u8"和弦音程超出 MIDI 音域。");
            note.startBeat = clip->startBeat;
            note.lengthBeats = length;
            note.isSelected = true;
            plan.generated.push_back(note);
        }
        return plan;
    }

    // The lowest selected pitch stays as the root. Match the other selected
    // voices to the new chord from low to high so their rhythms survive.
    const auto rootIt = std::min_element(selected.begin(), selected.end(),
        [](const NoteEvent& a, const NoteEvent& b) {
            return a.pitch < b.pitch
                || (a.pitch == b.pitch && (a.startBeat < b.startBeat
                    || (a.startBeat == b.startBeat && a.id < b.id)));
        });
    const auto& root = *rootIt;
    constexpr double beatTolerance = 0.0001;
    if (root.pitch < 0 || root.pitch > 127
        || !std::isfinite(root.startBeat) || !std::isfinite(root.lengthBeats)
        || root.startBeat < clip->startBeat - beatTolerance
        || root.startBeat >= clipEnd || root.lengthBeats <= 0.0)
        return fail(u8"所选根音不在当前 MIDI 片段的有效范围内。");
    const double length = std::min(root.lengthBeats, clipEnd - root.startBeat);
    if (length <= 0.0)
        return fail(u8"所选根音没有可用于生成和弦的时长。");

    std::vector<const NoteEvent*> selectedUpperVoices;
    for (const auto& note : selected)
        if (note.id != root.id)
        {
            plan.selectedUpperIdsToRemove.push_back(note.id);
            selectedUpperVoices.push_back(&note);
        }
    std::sort(selectedUpperVoices.begin(), selectedUpperVoices.end(),
        [](const NoteEvent* a, const NoteEvent* b) {
            return a->pitch < b->pitch
                || (a->pitch == b->pitch && (a->startBeat < b->startBeat
                    || (a->startBeat == b->startBeat && a->id < b->id)));
        });

    for (size_t intervalIndex = 1; intervalIndex < type.semitoneIntervals.size(); ++intervalIndex)
    {
        const size_t voiceIndex = intervalIndex - 1;
        const auto& source = preserveSelectedTiming
            && voiceIndex < selectedUpperVoices.size()
            ? *selectedUpperVoices[voiceIndex] : root;
        if (!std::isfinite(source.startBeat) || !std::isfinite(source.lengthBeats)
            || source.startBeat < clip->startBeat - beatTolerance
            || source.startBeat >= clipEnd || source.lengthBeats <= 0.0)
            return fail(u8"所选音符不在当前 MIDI 片段的有效范围内。");
        NoteEvent note = source;
        note.id = 0;
        note.pitch = root.pitch + type.semitoneIntervals[intervalIndex];
        if (note.pitch > 127 || note.pitch < 0)
            return fail(u8"选中根音过高，无法在其上方生成完整和弦。");
        note.lengthBeats = std::min(source.lengthBeats, clipEnd - source.startBeat);
        if (note.lengthBeats <= 0.0)
            return fail(u8"所选音符没有可用于生成和弦的时长。");
        note.isSelected = true;
        plan.generated.push_back(note);
    }
    return plan;
}

} // namespace Harmonizer
