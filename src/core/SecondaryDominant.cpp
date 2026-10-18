#include "SecondaryDominant.h"
#include "ChordClassifier.h"
#include "ChordPlanAnalysis.h"
#include "HarmonyGrouping.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <set>

namespace Harmonizer
{
namespace
{
constexpr double onsetTolerance = 0.12;
constexpr double infinity = 1.0e12;

int pitchClass(int pitch) { return (pitch % 12 + 12) % 12; }

std::vector<std::vector<NoteEvent>> selectedGroups(const MidiModel& model)
{
    const double barLength = (double)model.getTimeSigNumerator() * 4.0
        / (double)std::max(1, model.getTimeSigDenominator());
    return groupSelectedHarmonyNotes(model.getSelectedNotes(), barLength);
}

std::optional<int> selectedGroupRoot(const std::vector<NoteEvent>& group,
                                     const MidiModel& model, TrackModel& tracks)
{
    if (group.empty()) return std::nullopt;
    const auto layout = model.getEffectiveLayoutAtBeat(group.front().startBeat, &tracks);
    const auto chord = ChordClassifier::identifyChord(
        group, layout.tonicPc, layout.scaleName, layout.tonicName);
    if (!isUsableSingleChord(chord))
        return std::nullopt;
    return chord.rootPitchClass;
}

juce::String targetFunction(int targetRoot, const IdentifiedChord& target,
                            const KeyLayout& layout)
{
    const auto* scale = ScaleDefinitions::findScale(layout.scaleName);
    if (scale != nullptr && scale->intervals.size() == 7)
    {
        static constexpr const char* numerals[7] = {
            "I", "II", "III", "IV", "V", "VI", "VII"
        };
        for (int degree = 0; degree < 7; ++degree)
        {
            if (pitchClass(layout.tonicPc + scale->intervals[(size_t)degree]) != targetRoot)
                continue;
            auto numeral = scale->degrees.size() > (size_t)degree
                ? scale->degrees[(size_t)degree].upToFirstOccurrenceOf(" ", false, false)
                : juce::String(numerals[degree]);
            if (numeral.isEmpty()) numeral = numerals[degree];
            int third = target.thirdInterval;
            if (third < 0)
                third = pitchClass(scale->intervals[(size_t)((degree + 2) % 7)]
                    - scale->intervals[(size_t)degree]);
            if (third == 3) numeral = numeral.toLowerCase();
            if (target.family == ChordFamily::Diminished)
                numeral += juce::String::fromUTF8(u8"°");
            return "V/" + numeral;
        }
    }
    return "V/" + target.rootName;
}

std::optional<int> parseChordRoot(const ChordEvent& chord)
{
    auto text = chord.chordName.trim().replace(juce::String::fromUTF8(u8"♯"), "#")
                                      .replace(juce::String::fromUTF8(u8"♭"), "b");
    if (text.isEmpty())
        return chord.root >= 0 && chord.root < 12 ? std::optional<int>(chord.root) : std::nullopt;

    if (text.length() == 0)
        return std::nullopt;
    const juce::juce_wchar letter = juce::CharacterFunctions::toUpperCase(text[0]);
    int base = -1;
    switch (letter)
    {
        case 'C': base = 0; break;
        case 'D': base = 2; break;
        case 'E': base = 4; break;
        case 'F': base = 5; break;
        case 'G': base = 7; break;
        case 'A': base = 9; break;
        case 'B': base = 11; break;
        default: return std::nullopt;
    }
    if (text.length() > 1 && text[1] == '#') ++base;
    if (text.length() > 1 && text[1] == 'b') --base;
    return pitchClass(base);
}

std::vector<int> makeReferencePitches(const std::vector<NoteEvent>& group, int voiceCount)
{
    std::vector<int> pitches;
    for (const auto& note : group) pitches.push_back(note.pitch);
    std::sort(pitches.begin(), pitches.end());
    if ((int)pitches.size() == voiceCount)
        return pitches;

    const double centre = std::accumulate(pitches.begin(), pitches.end(), 0.0) / pitches.size();
    const double low = std::min((double)pitches.front(), centre - 6.0);
    const double high = std::max((double)pitches.back(), centre + 6.0);
    std::vector<int> reference;
    for (int i = 0; i < voiceCount; ++i)
        reference.push_back((int)std::lround(low + (high - low) * i / (voiceCount - 1)));
    return reference;
}

std::vector<int> chooseVoicing(const std::vector<int>& reference, int dominantRoot,
                               bool flatNine, bool rootInBass)
{
    const std::array<int, 4> core { 0, 4, 7, 10 };
    std::vector<int> best;
    double bestCost = infinity;
    const int voiceCount = (int)reference.size();

    for (int rotation = 0; rotation < (rootInBass ? 1 : 4); ++rotation)
    {
        std::vector<int> tones;
        for (int j = 0; j < voiceCount; ++j)
        {
            if (flatNine && j == 4)
                tones.push_back(1);
            else
                tones.push_back(core[(j - (flatNine && j > 4 ? 1 : 0) + rotation) % 4]);
        }

        std::vector<double> previous(128, infinity), current(128, infinity);
        std::vector<std::array<int, 128>> back(voiceCount);
        for (auto& row : back) row.fill(-1);

        for (int pitch = 0; pitch < 128; ++pitch)
            if (pitchClass(pitch) == pitchClass(dominantRoot + tones.front()))
                previous[pitch] = std::abs(pitch - reference.front());

        for (int voice = 1; voice < voiceCount; ++voice)
        {
            std::fill(current.begin(), current.end(), infinity);
            for (int pitch = 0; pitch < 128; ++pitch)
            {
                if (pitchClass(pitch) != pitchClass(dominantRoot + tones[voice]))
                    continue;
                for (int prior = 0; prior < pitch; ++prior)
                {
                    if (previous[prior] == infinity)
                        continue;
                    const double gapPenalty = std::max(0, pitch - prior - 12) * 0.4;
                    const double cost = previous[prior] + std::abs(pitch - reference[voice]) + gapPenalty;
                    if (cost < current[pitch])
                    {
                        current[pitch] = cost;
                        back[voice][pitch] = prior;
                    }
                }
            }
            previous.swap(current);
        }

        for (int finalPitch = 0; finalPitch < 128; ++finalPitch)
        {
            if (previous[finalPitch] >= bestCost)
                continue;
            std::vector<int> result(voiceCount);
            result.back() = finalPitch;
            bool valid = true;
            for (int voice = voiceCount - 1; voice > 0; --voice)
            {
                const int prior = back[voice][result[voice]];
                if (prior < 0) { valid = false; break; }
                result[voice - 1] = prior;
            }
            if (valid)
            {
                bestCost = previous[finalPitch];
                best = std::move(result);
            }
        }
    }
    return best;
}

std::vector<int> matchExistingVoices(const std::vector<NoteEvent>& sortedNotes,
                                     const std::vector<int>& voiced)
{
    const int oldCount = (int)sortedNotes.size();
    const int newCount = (int)voiced.size();
    std::vector<std::vector<double>> dp(oldCount + 1, std::vector<double>(newCount + 1, infinity));
    std::vector<std::vector<bool>> took(oldCount + 1, std::vector<bool>(newCount + 1, false));
    dp[0][0] = 0.0;
    for (int j = 1; j <= newCount; ++j)
    {
        for (int i = 0; i <= std::min(j, oldCount); ++i)
        {
            dp[i][j] = dp[i][j - 1];
            if (i > 0)
            {
                const double cost = dp[i - 1][j - 1] + std::abs(sortedNotes[i - 1].pitch - voiced[j - 1]);
                if (cost <= dp[i][j])
                {
                    dp[i][j] = cost;
                    took[i][j] = true;
                }
            }
        }
    }
    std::vector<int> destinations(oldCount, -1);
    for (int i = oldCount, j = newCount; i > 0 && j > 0; --j)
        if (took[i][j]) destinations[--i] = j - 1;
    return destinations;
}

} // namespace

int SecondaryDominant::countSelectedGroups(const MidiModel& model)
{
    return model.getSelectedNotes().empty() ? 0 : 1;
}

std::optional<int> SecondaryDominant::inferTargetRoot(double groupBeat, const MidiModel& model,
                                                       TrackModel& tracks)
{
    if (const auto* chordTrack = tracks.getChordTrack())
    {
        const ChordEvent* next = nullptr;
        for (const auto& chord : chordTrack->getChords())
            if (chord.startBeat > groupBeat + onsetTolerance
                && (next == nullptr || chord.startBeat < next->startBeat))
                next = &chord;
        if (next != nullptr)
            if (auto root = parseChordRoot(*next))
                return root;
    }

    // Search every clip of the active instrument so that a target just across a
    // clip boundary is still available. The editing clip's live notes take priority.
    auto findInNotes = [&](std::vector<NoteEvent> candidates) -> std::optional<int>
    {
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const NoteEvent& note) {
            return note.isMuted || note.startBeat <= groupBeat + onsetTolerance;
        }), candidates.end());
        const double barLength = (double)model.getTimeSigNumerator() * 4.0
            / (double)std::max(1, model.getTimeSigDenominator());
        for (const auto& group : groupSelectedHarmonyNotes(candidates, barLength))
        {
            std::set<int> classes;
            for (const auto& note : group) classes.insert(pitchClass(note.pitch));
            if (classes.size() < 3)
                continue;
            auto layout = model.getEffectiveLayoutAtBeat(group.front().startBeat, &tracks);
            auto chord = ChordClassifier::identifyChord(
                group, layout.tonicPc, layout.scaleName, layout.tonicName);
            if (isUsableSingleChord(chord))
                return chord.rootPitchClass;
        }
        return std::nullopt;
    };

    std::vector<NoteEvent> candidates = model.getNotes();
    const auto* activeTrack = tracks.getActiveInstrumentTrack();
    if (activeTrack != nullptr)
        for (const auto& clip : activeTrack->getClips())
            if (clip.id != tracks.getActiveClipId() && !clip.isMuted)
                candidates.insert(candidates.end(), clip.notes.begin(), clip.notes.end());
    if (auto root = findInNotes(std::move(candidates)))
        return root;

    // Accompaniment may live on another instrument track while the selected
    // notes are a melody or a sparse voicing on the active track.
    std::vector<NoteEvent> accompaniment;
    for (const auto& track : tracks.getTracks())
    {
        if (track->getType() != TrackType::Instrument || track.get() == activeTrack || track->isMuted())
            continue;
        for (const auto& clip : track->getClips())
            if (!clip.isMuted)
                accompaniment.insert(accompaniment.end(), clip.notes.begin(), clip.notes.end());
    }
    return findInNotes(std::move(accompaniment));
}

SecondaryDominantResult SecondaryDominant::prepare(const MidiModel& model, TrackModel& tracks,
                                                     const SecondaryDominantOptions& options)
{
    SecondaryDominantResult result;
    const auto* activeTrack = tracks.getActiveInstrumentTrack();
    const auto* clip = tracks.getActiveClip();
    if (activeTrack == nullptr || activeTrack->getType() != TrackType::Instrument || clip == nullptr)
        result.error = juce::String::fromUTF8(u8"请先打开乐器轨上的 MIDI 片段。");
    else if (activeTrack->isLocked())
        result.error = juce::String::fromUTF8(u8"乐器轨已锁定，无法修改音符。");
    else if (options.targetRootPc < -1 || options.targetRootPc > 11)
        result.error = juce::String::fromUTF8(u8"目标根音无效。");
    if (result.error.isNotEmpty()) return result;

    const bool blockChord = options.targetSource == SecondaryDominantTargetSource::SelectedChords
        || options.targetSource == SecondaryDominantTargetSource::ManualRoot;
    auto groups = selectedGroups(model);
    if (blockChord)
    {
        auto selected = model.getSelectedNotes();
        if (!selected.empty())
        {
            std::stable_sort(selected.begin(), selected.end(), [](const NoteEvent& a, const NoteEvent& b) {
                return a.startBeat < b.startBeat || (a.startBeat == b.startBeat && a.pitch < b.pitch);
            });
            groups = { std::move(selected) };
        }
    }
    else if (options.targetSource == SecondaryDominantTargetSource::SelectedNotes)
    {
        groups.clear();
        for (const auto& note : model.getSelectedNotes())
            groups.push_back({ note });
        std::stable_sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) {
            return a.front().startBeat < b.front().startBeat;
        });
    }
    result.groupCount = (int)groups.size();
    auto reject = [&result](const juce::String& error) {
        SecondaryDominantResult failed;
        failed.error = error;
        failed.groupCount = result.groupCount;
        return failed;
    };
    if (groups.empty())
        return reject(juce::String::fromUTF8(u8"请先在当前 MIDI 片段中选择要重配的音符。"));
    result.blockChord = blockChord;

    const double clipEnd = clip->startBeat + clip->lengthBeats;
    for (const auto& group : groups)
    {
        for (const auto& note : group)
            if (!std::isfinite(note.startBeat) || !std::isfinite(note.lengthBeats)
                || note.startBeat < clip->startBeat - 0.0001
                || note.startBeat + note.lengthBeats > clipEnd + 0.0001
                || note.lengthBeats <= 0.0)
                return reject(juce::String::fromUTF8(u8"选中的音符超出当前 MIDI 片段范围。"));

        if (group.size() >= 6)
        {
            const auto sourceLayout = model.getEffectiveLayoutAtBeat(group.front().startBeat, &tracks);
            if (ChordClassifier::identifyChord(group, sourceLayout.tonicPc,
                                               sourceLayout.scaleName,
                                               sourceLayout.tonicName).isPolychord)
                return reject(juce::String::fromUTF8(
                    u8"复合和弦需要分别选择上下结构，不能整体生成副属和弦。"));
        }

        int targetRoot = options.targetRootPc;
        if (targetRoot < 0)
        {
            switch (options.targetSource)
            {
                case SecondaryDominantTargetSource::SelectedChords:
                    targetRoot = selectedGroupRoot(group, model, tracks).value_or(-1);
                    break;
                case SecondaryDominantTargetSource::SelectedNotes:
                    targetRoot = pitchClass(group.front().pitch);
                    break;
                case SecondaryDominantTargetSource::NextHarmony:
                    targetRoot = inferTargetRoot(std::max_element(group.begin(), group.end(),
                        [](const NoteEvent& a, const NoteEvent& b) {
                            return a.startBeat < b.startBeat;
                        })->startBeat, model, tracks).value_or(-1);
                    break;
                case SecondaryDominantTargetSource::ManualRoot:
                    return reject(juce::String::fromUTF8(u8"请在和声算子设置中指定目标根音。"));
            }
        }
        if (targetRoot < 0)
            return reject(options.targetSource == SecondaryDominantTargetSource::NextHarmony
                ? juce::String::fromUTF8(u8"后续和弦轨或 MIDI 中没有可识别的目标和弦。可在和声算子设置中改用选中音符或固定根音。")
                : juce::String::fromUTF8(u8"无法识别选中音符的目标和弦。请在和声算子设置中选择固定目标根音。"));
        const auto layout = model.getEffectiveLayoutAtBeat(group.front().startBeat, &tracks);
        const bool selectedTarget = options.targetRootPc < 0
            && (options.targetSource == SecondaryDominantTargetSource::SelectedChords
                || options.targetSource == SecondaryDominantTargetSource::SelectedNotes);
        const bool fullSelectedChord = options.targetRootPc < 0
            && options.targetSource == SecondaryDominantTargetSource::SelectedChords;
        const auto targetChord = selectedTarget
            ? ChordClassifier::identifyChord(
                group, layout.tonicPc, layout.scaleName, layout.tonicName)
            : ChordClassifier::identifyChordFromPitches(
                { 60 + targetRoot }, layout.tonicPc, layout.scaleName,
                layout.tonicName);
        result.targetRoots.push_back(targetRoot);
        result.targetChordNames.push_back(targetChord.fullName);
        result.targetChordIsRootOnly.push_back(!fullSelectedChord);
        result.targetFunctions.push_back(targetFunction(targetRoot, targetChord, layout));

        auto sorted = group;
        std::sort(sorted.begin(), sorted.end(), [](const NoteEvent& a, const NoteEvent& b) {
            return a.pitch < b.pitch || (a.pitch == b.pitch && a.id < b.id);
        });
        const int voiceCount = blockChord ? (options.flatNine ? 5 : 4)
                                          : std::max((int)sorted.size(), options.flatNine ? 5 : 4);
        if (voiceCount > 24)
            return reject(juce::String::fromUTF8(u8"单个和弦选中的音符过多，请分批处理。"));
        const auto reference = makeReferencePitches(sorted, voiceCount);
        const auto voiced = chooseVoicing(reference, pitchClass(targetRoot + 7),
                                          options.flatNine, options.rootInBass);
        if ((int)voiced.size() != voiceCount)
            return reject(juce::String::fromUTF8(u8"当前音域无法生成完整属七和弦，请调整选中音符的音高。"));
        const size_t changedStart = result.modifiedNotes.size();
        const size_t addedStart = result.addedNotes.size();
        std::vector<bool> used(voiceCount, false);
        if (!blockChord)
        {
            const auto mapping = matchExistingVoices(sorted, voiced);
            for (size_t i = 0; i < sorted.size(); ++i)
            {
                const int destination = mapping[i];
                if (destination < 0)
                    return reject(juce::String::fromUTF8(u8"无法为选中音符分配属七和弦声部。"));
                used[destination] = true;
                if (sorted[i].pitch != voiced[destination])
                {
                    auto changed = sorted[i];
                    changed.pitch = voiced[destination];
                    result.modifiedNotes.push_back(changed);
                }
            }
        }

        const double startBeat = std::min_element(group.begin(), group.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.startBeat < b.startBeat; })->startBeat;
        const double length = std::min(clipEnd - startBeat,
            std::min_element(group.begin(), group.end(), [](const NoteEvent& a, const NoteEvent& b) {
                return a.lengthBeats < b.lengthBeats;
            })->lengthBeats);
        if (length <= 0.0)
            return reject(juce::String::fromUTF8(u8"无法在当前片段范围内补全和弦音。"));
        const float velocity = std::accumulate(group.begin(), group.end(), 0.0f,
            [](float sum, const NoteEvent& n) { return sum + n.velocity; }) / group.size();
        for (int voice = 0; voice < voiceCount; ++voice)
        {
            if (used[voice]) continue;
            NoteEvent added;
            added.pitch = voiced[voice];
            added.startBeat = startBeat;
            added.lengthBeats = length;
            added.velocity = velocity;
            added.isMuted = std::all_of(group.begin(), group.end(), [](const NoteEvent& n) { return n.isMuted; });
            added.isSelected = true;
            result.addedNotes.push_back(added);
        }
        const std::vector<NoteEvent> groupChanges(
            result.modifiedNotes.begin() + (std::ptrdiff_t)changedStart,
            result.modifiedNotes.end());
        const std::vector<NoteEvent> groupAdded(
            result.addedNotes.begin() + (std::ptrdiff_t)addedStart,
            result.addedNotes.end());
        const auto generated = blockChord ? groupAdded
            : materialiseChordPlan(group, groupChanges, groupAdded);
        const auto recognized = ChordClassifier::identifyChord(
            generated, layout.tonicPc, layout.scaleName, layout.tonicName);
        if (!isUsableSingleChord(recognized)
            || recognized.rootPitchClass != pitchClass(targetRoot + 7))
            return reject(juce::String::fromUTF8(
                u8"生成的音符未能识别为目标根音的属和弦，请调整所选音域。"));
        result.generatedChordNames.push_back(recognized.fullName);
        result.generatedGroups.push_back(generated);
    }
    return result;
}

std::vector<NoteEvent> SecondaryDominant::createGeneratedNotes(const MidiModel&,
                                                                 const SecondaryDominantResult& result)
{
    if (!result.succeeded())
        return {};

    std::vector<NoteEvent> generated;
    for (const auto& group : result.generatedGroups)
        for (auto note : group)
        {
            note.id = 0;
            note.isSelected = true;
            generated.push_back(note);
        }
    return generated;
}

std::vector<NoteEvent> SecondaryDominant::shiftGeneratedNotes(const std::vector<NoteEvent>& generated,
                                                                double targetStartBeat)
{
    if (generated.empty() || !std::isfinite(targetStartBeat))
        return {};
    const double sourceStart = std::min_element(generated.begin(), generated.end(),
        [](const NoteEvent& a, const NoteEvent& b) { return a.startBeat < b.startBeat; })->startBeat;
    std::vector<NoteEvent> positioned = generated;
    for (auto& note : positioned)
        note.startBeat += targetStartBeat - sourceStart;
    return positioned;
}

} // namespace Harmonizer
