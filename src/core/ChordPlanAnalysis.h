#pragma once

#include "ChordClassifier.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Harmonizer
{

inline bool isUsableSingleChord(const IdentifiedChord& chord)
{
    return (chord.status == ChordRecognitionStatus::Recognized
            || chord.status == ChordRecognitionStatus::Ambiguous)
        && chord.confidence >= 0.5 && !chord.isPolychord
        && chord.fullName.isNotEmpty() && chord.containsInterval(0)
        && std::find(chord.omittedIntervals.begin(), chord.omittedIntervals.end(), 0)
            == chord.omittedIntervals.end();
}

/**
 * Materialises the exact note group an operator will leave behind. Operators
 * should identify this group for their preview instead of naming an abstract
 * pitch-class template: the actual bass, retained extensions and octave
 * placement all affect the displayed chord.
 */
inline std::vector<NoteEvent> materialiseChordPlan(
    const std::vector<NoteEvent>& source,
    const std::vector<NoteEvent>& changed,
    const std::vector<NoteEvent>& added = {},
    const std::vector<int64_t>& removedIds = {})
{
    std::vector<NoteEvent> result;
    result.reserve(source.size() + added.size());
    for (const auto& note : source)
    {
        if (std::find(removedIds.begin(), removedIds.end(), note.id) != removedIds.end())
            continue;
        const auto replacement = std::find_if(changed.begin(), changed.end(),
            [&note](const NoteEvent& candidate) { return candidate.id == note.id; });
        result.push_back(replacement == changed.end() ? note : *replacement);
    }
    result.insert(result.end(), added.begin(), added.end());
    return result;
}

inline IdentifiedChord identifyPlannedChord(
    const std::vector<NoteEvent>& source,
    const std::vector<NoteEvent>& changed,
    const std::vector<NoteEvent>& added,
    const std::vector<int64_t>& removedIds,
    int tonicPc,
    const juce::String& scaleName,
    const juce::String& tonicName = {})
{
    return ChordClassifier::identifyChord(
        materialiseChordPlan(source, changed, added, removedIds), tonicPc, scaleName,
        tonicName);
}

} // namespace Harmonizer
