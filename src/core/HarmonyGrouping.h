#pragma once

#include "MidiModel.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace Harmonizer
{

// Keep block chords separate while collecting short staggered runs into
// arpeggiated harmony groups for chord editing and harmonic operations.
inline std::vector<std::vector<NoteEvent>> groupSelectedHarmonyNotes(
    const std::vector<NoteEvent>& input, double barLengthBeats)
{
    constexpr double onsetTolerance = 0.12;
    auto pc = [](int pitch) { return (pitch % 12 + 12) % 12; };
    auto notes = input;
    std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
        return a.startBeat < b.startBeat || (a.startBeat == b.startBeat && a.pitch < b.pitch);
    });

    std::vector<std::vector<NoteEvent>> onsets;
    for (const auto& note : notes)
    {
        if (onsets.empty() || note.startBeat - onsets.back().front().startBeat > onsetTolerance)
            onsets.emplace_back();
        onsets.back().push_back(note);
    }

    const double barLength = std::max(1.0, barLengthBeats);
    const double maxGap = std::min(1.5, std::max(0.75, barLength / 2.0));
    const double maxSpan = std::min(4.0, std::max(2.0, barLength));
    auto distinctPcs = [&pc](const std::vector<NoteEvent>& group) {
        std::set<int> pcs;
        for (const auto& note : group) pcs.insert(pc(note.pitch));
        return pcs.size();
    };
    std::vector<std::vector<NoteEvent>> groups;
    bool lastWasBlock = false;
    auto futureFormsChord = [&](size_t startIndex) {
        std::set<int> pcs;
        const double first = onsets[startIndex].front().startBeat;
        double previous = first;
        for (size_t index = startIndex; index < onsets.size(); ++index)
        {
            const double beat = onsets[index].front().startBeat;
            if (beat - previous > maxGap || beat - first > maxSpan) break;
            for (const auto& note : onsets[index]) pcs.insert(pc(note.pitch));
            if (pcs.size() >= 3) return true;
            previous = onsets[index].back().startBeat;
        }
        return false;
    };
    for (size_t onsetIndex = 0; onsetIndex < onsets.size(); ++onsetIndex)
    {
        const auto& onset = onsets[onsetIndex];
        const bool blockChord = distinctPcs(onset) >= 3;
        if (blockChord)
        {
            groups.push_back(onset);
            lastWasBlock = true;
            continue;
        }
        if (groups.empty() || lastWasBlock)
        {
            groups.push_back(onset);
            lastWasBlock = false;
            continue;
        }
        auto& current = groups.back();
        const double first = current.front().startBeat;
        const double next = onset.front().startBeat;
        const bool crossesBar = std::floor(first / barLength) != std::floor(next / barLength);
        const bool contourTurns = current.size() >= 3
            && current.back().startBeat - current[current.size() - 2].startBeat > onsetTolerance
            && (current.back().pitch - current[current.size() - 2].pitch)
                * (onset.front().pitch - current.back().pitch) < 0;
        if (next - current.back().startBeat > maxGap || next - first > maxSpan
            || (crossesBar && distinctPcs(current) >= 3 && futureFormsChord(onsetIndex))
            || current.size() + onset.size() > 16
            || (distinctPcs(current) >= 3 && contourTurns && futureFormsChord(onsetIndex)))
            groups.push_back(onset);
        else
            current.insert(current.end(), onset.begin(), onset.end());
    }
    return groups;
}

} // namespace Harmonizer
