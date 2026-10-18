#include "Reharmonizer.h"
#include "ChordClassifier.h"
#include "ChordPlanAnalysis.h"
#include "ScaleDefinitions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>

namespace Harmonizer
{
namespace
{
constexpr double infinity = 1.0e12;

int pc(int pitch) { return (pitch % 12 + 12) % 12; }

struct Voicing
{
    std::vector<int> pitches;
    double cost = infinity;
};

Voicing findClosestVoicing(const std::vector<NoteEvent>& sorted, const std::vector<int>& tones)
{
    Voicing best;
    if (sorted.empty() || tones.empty() || tones.size() > sorted.size())
        return best;

    const int voices = (int)sorted.size();
    for (int rotation = 0; rotation < (int)tones.size(); ++rotation)
    {
        std::array<double, 128> previous, current;
        previous.fill(infinity);
        std::vector<std::array<int, 128>> back(voices);
        for (auto& row : back) row.fill(-1);

        for (int pitch = 0; pitch < 128; ++pitch)
            if (pc(pitch) == tones[rotation])
                previous[pitch] = std::abs(pitch - sorted.front().pitch);

        for (int voice = 1; voice < voices; ++voice)
        {
            current.fill(infinity);
            const int targetPc = tones[(voice + rotation) % tones.size()];
            for (int pitch = 0; pitch < 128; ++pitch)
            {
                if (pc(pitch) != targetPc) continue;
                for (int prior = 0; prior < pitch; ++prior)
                {
                    if (previous[prior] >= infinity) continue;
                    const double gapPenalty = std::max(0, pitch - prior - 12) * 0.4;
                    const double cost = previous[prior] + std::abs(pitch - sorted[voice].pitch) + gapPenalty;
                    if (cost < current[pitch])
                    {
                        current[pitch] = cost;
                        back[voice][pitch] = prior;
                    }
                }
            }
            previous = current;
        }

        for (int last = 0; last < 128; ++last)
        {
            if (previous[last] >= best.cost) continue;
            std::vector<int> pitches(voices);
            pitches.back() = last;
            bool valid = true;
            for (int voice = voices - 1; voice > 0; --voice)
            {
                const int prior = back[voice][pitches[voice]];
                if (prior < 0) { valid = false; break; }
                pitches[voice - 1] = prior;
            }
            if (valid)
            {
                best.pitches = std::move(pitches);
                best.cost = previous[last];
            }
        }
    }
    return best;
}

bool isDiatonic(const std::vector<int>& tones, const KeyLayout& layout)
{
    return std::all_of(tones.begin(), tones.end(), [&layout](int tone) {
        return layout.isInScale(tone);
    });
}

std::vector<int> diatonicStack(const ScaleConfig& scale, int tonicPc,
                               int degree, size_t desiredTones)
{
    std::vector<int> tones;
    const size_t count = std::clamp(desiredTones, size_t(3), size_t(7));
    tones.reserve(count);
    for (size_t layer = 0; layer < count; ++layer)
        tones.push_back(pc(tonicPc + scale.intervals[(size_t)
            ((degree + (int)layer * 2) % 7)]));
    return tones;
}

std::vector<int> dominantStack(int root, size_t desiredTones)
{
    constexpr int intervals[] = { 0, 4, 7, 10, 2, 5, 9 };
    std::vector<int> tones;
    for (size_t i = 0; i < std::clamp(desiredTones, size_t(4), size_t(7)); ++i)
        tones.push_back(pc(root + intervals[i]));
    return tones;
}

struct GroupPlan
{
    ReharmonizationResult::GroupPreview preview;
    std::vector<NoteEvent> changedNotes;
};

GroupPlan prepareGroup(const std::vector<NoteEvent>& group, const MidiClip& clip,
                       const KeyLayout& layout, ReharmonizationAlgorithm algorithm)
{
    GroupPlan result;
    result.preview.startBeat = std::min_element(group.begin(), group.end(),
        [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; })->startBeat;
    auto reject = [&result](const char8_t* message) {
        result.preview.issue = juce::String::fromUTF8(message);
        return result;
    };
    std::set<int> sourcePcs;
    std::vector<int> sourcePitches;
    const double clipEnd = clip.startBeat + clip.lengthBeats;
    for (const auto& note : group)
    {
        if (!std::isfinite(note.startBeat) || !std::isfinite(note.lengthBeats)
            || note.startBeat < clip.startBeat - 0.0001
            || note.startBeat + note.lengthBeats > clipEnd + 0.0001
            || note.lengthBeats <= 0.0 || note.pitch < 0 || note.pitch > 127)
            return reject(u8"选中的音符超出当前 MIDI 片段或有效音域。");
        sourcePcs.insert(pc(note.pitch));
        sourcePitches.push_back(note.pitch);
    }
    if (sourcePcs.size() < 3)
        return reject(u8"每组至少选择三个不同音高类别，才能识别并重配和弦。");

    const auto chord = ChordClassifier::identifyChord(
        group, layout.tonicPc, layout.scaleName, layout.tonicName);
    result.preview.sourceChord = chord.fullName;
    if (chord.isPolychord)
        return reject(u8"复合和弦需要分别选择上下结构，当前重配模式只处理单一和弦。");
    if (!isUsableSingleChord(chord))
        return reject(u8"无法可靠识别选中和弦，请调整选择。");
    const int root = chord.rootPitchClass;
    const bool majorThird = chord.thirdInterval == 4;
    const bool minorThird = chord.thirdInterval == 3;
    const bool perfectFifth = sourcePcs.count(pc(root + 7)) != 0;
    const bool hasSeventh = chord.seventhInterval >= 0;

    auto sorted = group;
    std::sort(sorted.begin(), sorted.end(), [](const NoteEvent& a, const NoteEvent& b) {
        return a.pitch < b.pitch || (a.pitch == b.pitch && a.id < b.id);
    });
    // Repeated events at the same pitch are one harmonic voice. Keep every
    // event and its timing, then apply the voice's target pitch to all copies.
    sorted.erase(std::unique(sorted.begin(), sorted.end(),
        [](const NoteEvent& a, const NoteEvent& b) { return a.pitch == b.pitch; }),
        sorted.end());

    struct Choice
    {
        Voicing voicing;
        int root = 0;
        int shared = 0;
        std::vector<int> tones;
        double score = infinity;
    } best;
    auto consider = [&](int targetRoot, std::vector<int> tones) {
        const std::set<int> targetPcs(tones.begin(), tones.end());
        if (targetPcs == sourcePcs || targetPcs.size() != tones.size()) return;
        const auto voicing = findClosestVoicing(sorted, tones);
        if (voicing.pitches.size() != sorted.size()) return;
        int shared = 0;
        for (int tone : targetPcs) shared += sourcePcs.count(tone) ? 1 : 0;
        const double score = voicing.cost - shared * 2.0;
        if (score < best.score)
            best = { voicing, targetRoot, shared, std::move(tones), score };
    };

    switch (algorithm)
    {
        case ReharmonizationAlgorithm::DiatonicCommonTone:
        case ReharmonizationAlgorithm::SmoothDiatonic:
        {
            const auto* scale = ScaleDefinitions::findScale(layout.scaleName);
            if (scale == nullptr || scale->intervals.size() != 7)
                return reject(u8"调内替代需要七声音阶，请先选择七声音阶。");
            for (int degree = 0; degree < 7; ++degree)
            {
                const int targetRoot = pc(layout.tonicPc + scale->intervals[degree]);
                if (targetRoot == root) continue;
                auto tones = diatonicStack(*scale, layout.tonicPc, degree,
                    std::min(sourcePcs.size(), sorted.size()));
                int common = 0;
                for (int tone : tones) common += sourcePcs.count(tone) ? 1 : 0;
                if (algorithm == ReharmonizationAlgorithm::DiatonicCommonTone
                    && common < 2) continue;
                consider(targetRoot, std::move(tones));
            }
            if (best.voicing.pitches.empty())
                return reject(algorithm == ReharmonizationAlgorithm::DiatonicCommonTone
                    ? u8"当前音阶中没有与所选和弦共享两个音的替代和弦。"
                    : u8"当前调式中没有音域合适的平滑替代和弦。");
            break;
        }
        case ReharmonizationAlgorithm::TritoneSubstitution:
        {
            // The root, major third and minor seventh establish a dominant
            // seventh; the two guide tones remain common to its tritone sub.
            if (!chord.hasDominantGuideTones() || !sourcePcs.count(root))
                return reject(u8"三全音替代需要属七和弦的根音、大三度与小七度。");
            const int subRoot = pc(root + 6);
            std::vector<int> tones { subRoot, pc(subRoot + 4), pc(subRoot + 10) };
            if (perfectFifth && sorted.size() >= 4)
                tones.insert(tones.begin() + 2, pc(subRoot + 7));
            // Preserve colour tones that become useful alterations of the
            // substitute (9 -> b13, #9 -> 13, b13 -> 9, 13 -> #9).
            for (int interval : { 2, 3, 8, 9, 1, 6 })
            {
                const int tone = pc(root + interval);
                if (sourcePcs.count(tone)
                    && std::find(tones.begin(), tones.end(), tone) == tones.end()
                    && tones.size() < sorted.size())
                    tones.push_back(tone);
            }
            if (chord.containsInterval(5) && tones.size() < sorted.size()
                && std::find(tones.begin(), tones.end(), root) == tones.end())
                tones.push_back(root); // 11 of the source becomes #11 of the substitute.
            if (tones.size() < std::min(sorted.size(), size_t(4)))
                tones.insert(tones.begin() + 2, pc(subRoot + 7));
            consider(subRoot, std::move(tones));
            break;
        }
        case ReharmonizationAlgorithm::ParallelMode:
        {
            const auto* current = ScaleDefinitions::findScale(layout.scaleName);
            if (current == nullptr || current->intervals.size() != 7)
                return reject(u8"平行调式借用需要七声音阶。");
            const bool majorMode = std::find(current->intervals.begin(), current->intervals.end(), 4)
                != current->intervals.end();
            const bool minorMode = std::find(current->intervals.begin(), current->intervals.end(), 3)
                != current->intervals.end();
            if (majorMode == minorMode)
                return reject(u8"当前调式没有明确的大调或小调三度，无法确定平行调式。");
            const auto* parallel = ScaleDefinitions::findScale(majorMode
                ? "Aeolian (Natural Minor)" : "Ionian (Natural Major)");
            const int relativeRoot = pc(root - layout.tonicPc);
            const auto degree = std::find(parallel->intervals.begin(), parallel->intervals.end(), relativeRoot);
            if (degree == parallel->intervals.end())
                return reject(u8"所选和弦的根音不在对应的平行调式中。");
            const int index = (int)std::distance(parallel->intervals.begin(), degree);
            auto tones = diatonicStack(*parallel, layout.tonicPc, index,
                std::min(sourcePcs.size(), sorted.size()));
            consider(root, std::move(tones));
            if (best.voicing.pitches.empty())
                return reject(u8"平行调式在同一根音上没有不同的可用和弦。");
            break;
        }
        case ReharmonizationAlgorithm::ChromaticMediant:
        {
            if (!perfectFifth || majorThird == minorThird)
                return reject(u8"色彩三度关系需要明确的大三和弦或小三和弦。");
            const int third = majorThird ? 4 : 3;
            for (int distance : { -4, -3, 3, 4 })
            {
                const int targetRoot = pc(root + distance);
                std::vector<int> tones { targetRoot, pc(targetRoot + third), pc(targetRoot + 7) };
                if (isDiatonic(tones, layout)) continue;
                int common = 0;
                for (int tone : tones) common += sourcePcs.count(tone) ? 1 : 0;
                if (common == 0) continue;
                if (hasSeventh && sorted.size() >= 4)
                {
                    const int seventh = sourcePcs.count(pc(root + 11)) ? 11
                        : sourcePcs.count(pc(root + 10)) ? 10 : 9;
                    tones.push_back(pc(targetRoot + seventh));
                }
                for (int extension : { 2, 5, 9 })
                    if (chord.containsInterval(extension) && tones.size() < sorted.size())
                        tones.push_back(pc(targetRoot + extension));
                consider(targetRoot, std::move(tones));
            }
            if (best.voicing.pitches.empty())
                return reject(u8"当前调式下没有共享音的色彩三度替代和弦。");
            break;
        }
        case ReharmonizationAlgorithm::FunctionalSubstitution:
        {
            const auto* scale = ScaleDefinitions::findScale(layout.scaleName);
            if (scale == nullptr || scale->intervals.size() != 7)
                return reject(u8"功能替代需要七声音阶。");
            const bool majorTonic = scale->intervals[2] == 4;
            const bool minorTonic = scale->intervals[2] == 3;
            if ((!majorTonic && !minorTonic) || scale->intervals[4] != 7)
                return reject(u8"当前调式没有明确的大或小主和弦。");
            const int relativeRoot = pc(root - layout.tonicPc);
            const auto degreeIt = std::find(scale->intervals.begin(), scale->intervals.end(), relativeRoot);
            if (degreeIt == scale->intervals.end())
                return reject(u8"所选和弦根音不属于当前调式。");
            const int degree = (int)std::distance(scale->intervals.begin(), degreeIt);
            const int targetDegree = degree == 0 ? 5 : degree == 5 ? 0
                : degree == 3 ? 1 : degree == 1 ? 3 : -1;
            if (targetDegree < 0)
                return reject(u8"功能替代支持主和弦与六级、四级与二级之间的转换。");
            const std::set<int> sourceTriad {
                pc(layout.tonicPc + scale->intervals[degree]),
                pc(layout.tonicPc + scale->intervals[(degree + 2) % 7]),
                pc(layout.tonicPc + scale->intervals[(degree + 4) % 7])
            };
            if ((degree == 0 || degree == 5)
                && pc(layout.tonicPc + scale->intervals[(degree + 4) % 7] - root) != 7)
                return reject(u8"该调式的六级不是可作主功能替代的大或小三和弦。");
            if (!std::includes(sourcePcs.begin(), sourcePcs.end(),
                               sourceTriad.begin(), sourceTriad.end())
                || !isDiatonic(sourcePitches, layout))
                return reject(u8"所选和弦需包含当前调式内的完整三和弦。");
            const int targetRoot = pc(layout.tonicPc + scale->intervals[targetDegree]);
            auto tones = diatonicStack(*scale, layout.tonicPc, targetDegree,
                std::min(sourcePcs.size(), sorted.size()));
            if ((degree == 0 || degree == 5)
                && (pc(tones[2] - targetRoot) != 7
                    || (pc(tones[1] - targetRoot) != 3
                        && pc(tones[1] - targetRoot) != 4)))
                return reject(u8"该调式的六级并非可作主功能替代的大或小三和弦。");
            consider(targetRoot, std::move(tones));
            if (best.voicing.pitches.empty())
                return reject(u8"当前和弦已是目标功能和弦，或音域不足。");
            break;
        }
        case ReharmonizationAlgorithm::BackdoorDominant:
        {
            const auto* scale = ScaleDefinitions::findScale(layout.scaleName);
            if (scale == nullptr || scale->intervals.size() != 7
                || scale->intervals[2] != 4 || scale->intervals[4] != 7)
                return reject(u8"后门属和弦需要以大三和弦为主和弦的七声调式。");
            if (root != pc(layout.tonicPc + 7) || !sourcePcs.count(root)
                || !chord.hasDominantGuideTones()
                || sorted.size() < 4)
                return reject(u8"后门替代需要当前调式五级上的四声部属七和弦。");
            const int targetRoot = pc(layout.tonicPc + 10);
            consider(targetRoot, dominantStack(targetRoot,
                std::min(sourcePcs.size(), sorted.size())));
            if (best.voicing.pitches.empty())
                return reject(u8"当前音域无法生成降七级属七和弦。");
            break;
        }
    }
    if (best.voicing.pitches.size() != sorted.size())
        return reject(u8"当前音域无法生成完整替代和弦，请调整选中音符的音高。");

    result.preview.commonTones = best.shared;
    std::map<int, int> targetByPitch;
    for (size_t voice = 0; voice < sorted.size(); ++voice)
        targetByPitch[sorted[voice].pitch] = best.voicing.pitches[voice];
    for (const auto& note : group)
    {
        const int targetPitch = targetByPitch.at(note.pitch);
        if (note.pitch == targetPitch) continue;
        auto changed = note;
        changed.pitch = targetPitch;
        result.changedNotes.push_back(changed);
    }
    const auto targetChord = identifyPlannedChord(group, result.changedNotes,
        {}, {}, layout.tonicPc, layout.scaleName, layout.tonicName);
    if (!isUsableSingleChord(targetChord))
        return reject(u8"目标音符无法可靠识别为和弦，请调整选中音符或重配模式。");
    result.preview.targetChord = targetChord.fullName;
    int motion = pc(targetChord.rootPitchClass - root);
    if (motion > 6) motion -= 12;
    result.preview.rootMotion = motion;
    result.preview.supported = true;
    return result;
}

} // namespace

ReharmonizationResult Reharmonizer::prepare(const MidiModel& model, TrackModel& tracks,
                                             ReharmonizationAlgorithm algorithm)
{
    ReharmonizationResult result;
    const auto* track = tracks.getActiveInstrumentTrack();
    const auto* clip = tracks.getActiveClip();
    if (track == nullptr || track->getType() != TrackType::Instrument || clip == nullptr)
        result.error = juce::String::fromUTF8(u8"请先打开乐器轨上的 MIDI 片段。");
    else if (track->isLocked())
        result.error = juce::String::fromUTF8(u8"乐器轨已锁定，无法重配音符。");
    else if ((int)algorithm < 0 || (int)algorithm >= kReharmonizationAlgorithmCount)
        result.error = juce::String::fromUTF8(u8"无效的和声重配算法。");
    if (result.error.isNotEmpty()) return result;

    const auto selected = model.getSelectedNotes();
    if (selected.empty())
    {
        result.error = juce::String::fromUTF8(u8"请先选择要重配的和弦音符。");
        return result;
    }
    const auto first = std::min_element(selected.begin(), selected.end(),
        [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
    result.groupCount = 1;
    auto plan = prepareGroup(selected, *clip,
        model.getEffectiveLayoutAtBeat(first->startBeat, &tracks), algorithm);
    if (!plan.preview.supported) result.error = plan.preview.issue;
    result.groups.push_back(std::move(plan.preview));
    result.changedNotes = std::move(plan.changedNotes);
    if (result.error.isNotEmpty()) result.changedNotes.clear();
    return result;
}

} // namespace Harmonizer
