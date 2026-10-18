#include "ChordEditing.h"
#include "ChordClassifier.h"
#include "ChordPlanAnalysis.h"
#include "HarmonyGrouping.h"
#include "ScaleDefinitions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>

namespace Harmonizer
{
namespace
{
constexpr double infinity = 1.0e12;
int pc(int pitch) { return (pitch % 12 + 12) % 12; }

const ScaleConfig* exactSevenNoteScale(const juce::String& name)
{
    for (const auto& scale : ScaleDefinitions::getAllScales())
        if (scale.name.equalsIgnoreCase(name) && scale.intervals.size() == 7
            && scale.intervals.front() == 0 && scale.intervals.back() <= 11
            && std::adjacent_find(scale.intervals.begin(), scale.intervals.end(),
                [](int a, int b) { return a >= b; }) == scale.intervals.end())
            return &scale;
    return nullptr;
}

bool validate(const TrackModel& tracks, const std::vector<NoteEvent>& notes,
              ChordEditPlan& plan)
{
    const auto* track = tracks.getTrackById(tracks.getActiveTrackId());
    const auto* clip = tracks.getActiveClip();
    if (track == nullptr || track->getType() != TrackType::Instrument || clip == nullptr)
        plan.error = juce::String::fromUTF8(u8"请先打开乐器轨上的 MIDI 片段。");
    else if (track->isLocked())
        plan.error = juce::String::fromUTF8(u8"乐器轨已锁定。");
    else if (notes.empty())
        plan.error = juce::String::fromUTF8(u8"请先选中要编辑的音符。");
    else
    {
        const double end = clip->startBeat + clip->lengthBeats;
        for (const auto& n : notes)
            if (!std::isfinite(n.startBeat) || !std::isfinite(n.lengthBeats)
                || n.startBeat < clip->startBeat - 0.0001
                || n.startBeat + n.lengthBeats > end + 0.0001
                || n.lengthBeats <= 0.0 || n.pitch < 0 || n.pitch > 127)
            {
                plan.error = juce::String::fromUTF8(u8"选中音符超出当前片段或 MIDI 音域。");
                break;
            }
    }
    return plan.error.isEmpty();
}

void reject(ChordEditPlan& plan, ChordEditPlan::Group& row, const char8_t* message)
{
    row.issue = juce::String::fromUTF8(message);
    if (plan.error.isEmpty()) plan.error = row.issue;
}

void finish(ChordEditPlan& plan)
{
    if (plan.error.isNotEmpty())
    {
        plan.changedNotes.clear();
        plan.addedNotes.clear();
        plan.removedNoteIds.clear();
        for (auto& group : plan.groups)
            group.supported = false;
    }
}

std::vector<int> closestVoicing(const std::vector<NoteEvent>& sorted,
                                const std::vector<int>& tones)
{
    if (sorted.empty() || tones.empty() || sorted.size() > 16 || tones.size() > sorted.size())
        return {};
    double bestCost = infinity;
    std::vector<int> best;
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
                    const double cost = previous[prior]
                        + std::abs(pitch - sorted[(size_t)voice].pitch)
                        + std::max(0, pitch - prior - 12) * 0.4;
                    if (cost < current[pitch])
                    {
                        current[pitch] = cost;
                        back[(size_t)voice][pitch] = prior;
                    }
                }
            }
            previous = current;
        }
        for (int last = 0; last < 128; ++last)
        {
            if (previous[last] >= bestCost) continue;
            std::vector<int> pitches((size_t)voices);
            pitches.back() = last;
            bool valid = true;
            for (int voice = voices - 1; voice > 0; --voice)
            {
                int prior = back[(size_t)voice][pitches[(size_t)voice]];
                if (prior < 0) { valid = false; break; }
                pitches[(size_t)voice - 1] = prior;
            }
            if (valid) { best = std::move(pitches); bestCost = previous[last]; }
        }
    }
    return best;
}

std::vector<int> scaleChord(int tonic, const ScaleConfig& scale, int degree, bool seventh)
{
    const int count = seventh ? 4 : 3;
    std::vector<int> tones;
    for (int i = 0; i < count; ++i)
        tones.push_back(pc(tonic + scale.intervals[(degree + i * 2) % 7]));
    return tones;
}

int closestPitch(int source, int targetPc)
{
    int best = -1;
    int cost = 1000;
    for (int pitch = targetPc; pitch < 128; pitch += 12)
    {
        int distance = std::abs(pitch - source);
        if (distance < cost) { cost = distance; best = pitch; }
    }
    return best;
}

int rolePenalty(int sourceInterval, int targetInterval)
{
    auto role = [](int iv) {
        if (iv == 3 || iv == 4) return 1;
        if (iv == 6 || iv == 7 || iv == 8) return 2;
        if (iv == 10 || iv == 11) return 3;
        return 0;
    };
    const int sourceRole = role(sourceInterval);
    if (sourceRole == 0) return 0;
    return role(targetInterval) == sourceRole ? 0 : 5;
}

// Keep each voice's chord tone and its order. Move the outer voices on both
// sides of the original centre, then distribute the inner voices evenly.
std::vector<int> evenVoicing(const std::vector<NoteEvent>& sorted, bool expand)
{
    const int count = (int)sorted.size();
    if (count < 3 || count > 128) return {};
    const int originalSpan = sorted.back().pitch - sorted.front().pitch;
    const int desiredSpan = std::clamp(originalSpan + (expand ? 24 : -24),
                                       count - 1, 127);
    if (desiredSpan == originalSpan) return {};
    const double centre = (sorted.front().pitch + sorted.back().pitch) * 0.5;
    const double idealBass = std::clamp(centre - desiredSpan * 0.5,
                                       0.0, 127.0 - desiredSpan);
    const double targetGap = (double)desiredSpan / (count - 1);

    struct State
    {
        int last = 0;
        int first = 0;
        double cost = 0.0;
        std::array<int, 128> assignment{};
    };
    std::vector<State> beam;
    for (int pitch = pc(sorted.front().pitch); pitch < 128; pitch += 12)
    {
        if (std::abs(pitch - sorted.front().pitch) > 36) continue;
        State state;
        state.last = state.first = pitch;
        state.assignment[0] = pitch;
        state.cost = 2.0 * std::pow(pitch - idealBass, 2)
            + 0.16 * std::pow(pitch - sorted.front().pitch, 2);
        beam.push_back(state);
    }
    if (beam.empty()) return {};
    for (int rank = 1; rank < count; ++rank)
    {
        std::vector<State> next;
        for (const auto& state : beam)
        {
            const double ideal = idealBass + targetGap * rank;
            for (int pitch = pc(sorted[(size_t)rank].pitch); pitch < 128; pitch += 12)
            {
                if (pitch <= state.last
                    || std::abs(pitch - sorted[(size_t)rank].pitch) > 36) continue;
                State candidate = state;
                candidate.last = pitch;
                candidate.assignment[(size_t)rank] = pitch;
                const double gap = pitch - state.last;
                candidate.cost += 2.0 * std::pow(pitch - ideal, 2)
                    + 5.0 * std::pow(gap - targetGap, 2)
                    + 0.16 * std::pow(pitch - sorted[(size_t)rank].pitch, 2);
                next.push_back(std::move(candidate));
            }
        }
        if (next.empty()) return {};
        std::sort(next.begin(), next.end(), [](const State& a, const State& b) {
            return a.cost < b.cost;
        });
        std::set<std::pair<int, int>> seen;
        beam.clear();
        for (auto& candidate : next)
        {
            if (!seen.emplace(candidate.first, candidate.last).second) continue;
            beam.push_back(std::move(candidate));
            if (beam.size() >= 256) break;
        }
    }

    double bestCost = infinity;
    std::vector<int> best;
    for (const auto& state : beam)
    {
        const int span = state.last - state.first;
        if ((expand && span <= originalSpan) || (!expand && span >= originalSpan))
            continue;
        if (expand && (state.first >= sorted.front().pitch
                       || state.last <= sorted.back().pitch))
            continue;
        if (!expand && (state.first <= sorted.front().pitch
                        || state.last >= sorted.back().pitch))
            continue;
        const double afterCentre = (state.first + state.last) * 0.5;
        if (std::abs(afterCentre - centre) > 6.0) continue;
        const double averageGap = (double)span / (count - 1);
        double variance = 0.0;
        for (int rank = 1; rank < count; ++rank)
        {
            const double delta = state.assignment[(size_t)rank]
                - state.assignment[(size_t)rank - 1] - averageGap;
            variance += delta * delta;
        }
        const double spanError = span - desiredSpan;
        const double cost = state.cost + 20.0 * variance
            + 1.5 * spanError * spanError
            + 12.0 * std::pow(afterCentre - centre, 2);
        if (cost < bestCost)
        {
            bestCost = cost;
            best.assign(state.assignment.begin(), state.assignment.begin() + count);
        }
    }
    return best;
}

struct StackLayerChoice
{
    int layer = -1; // 0..3 = seventh, ninth, eleventh, thirteenth
    int addedPitchClass = -1;
    std::vector<int> removedPitchClasses;
    juce::String issue;
};

StackLayerChoice chooseStackLayer(const IdentifiedChord& chord,
                                 const std::set<int>& existing,
                                 const KeyLayout& layout, int family, bool remove)
{
    StackLayerChoice choice;
    auto fail = [&choice](const char8_t* message) {
        choice.issue = juce::String::fromUTF8(message);
        return choice;
    };
    if (!isUsableSingleChord(chord) || existing.size() < 3)
        return fail(u8"请选中可可靠识别的三音以上和弦。");

    const int root = chord.rootPitchClass;
    std::array<int, 4> targets{};
    if (family == 3)
    {
        const auto* scale = exactSevenNoteScale(layout.scaleName);
        if (scale == nullptr)
            return fail(u8"调内高叠需要有效的七声音阶。");
        const auto it = std::find_if(scale->intervals.begin(), scale->intervals.end(),
            [root, &layout](int interval) {
                return pc(layout.tonicPc + interval) == root;
            });
        if (it == scale->intervals.end())
            return fail(u8"和弦根音不在当前调式内。");
        const int degree = (int)std::distance(scale->intervals.begin(), it);
        const int expectedThird = pc(layout.tonicPc
            + scale->intervals[(size_t)((degree + 2) % 7)]);
        const int expectedFifth = pc(layout.tonicPc
            + scale->intervals[(size_t)((degree + 4) % 7)]);
        if (!existing.count(expectedThird)
            || (chord.fifthInterval >= 0 && !existing.count(expectedFifth)))
            return fail(u8"原和弦的三度或五度不符合当前调式的自然高叠。");
        for (int layer = 0; layer < 4; ++layer)
            targets[(size_t)layer] = pc(layout.tonicPc
                + scale->intervals[(size_t)((degree + 6 + layer * 2) % 7)]);
        if (chord.seventhInterval >= 0
            && chord.seventhInterval != pc(targets[0] - root))
            return fail(u8"现有七度音与当前调式的自然高叠冲突。");
    }
    else
    {
        const bool majorSource = chord.family == ChordFamily::Major;
        const bool minorSource = chord.family == ChordFamily::Minor;
        const bool dominantSource = chord.hasDominantGuideTones();
        const bool compatible = family == 0
            ? majorSource && (chord.seventhInterval < 0 || chord.seventhInterval == 11)
            : family == 1
                ? minorSource && (chord.seventhInterval < 0 || chord.seventhInterval == 10)
                : dominantSource || (!remove && majorSource && chord.seventhInterval < 0);
        if (!compatible)
            return fail(u8"所选和弦的结构与这个高叠方向不一致。");
        if (remove && chord.seventhInterval < 0)
            return fail(u8"当前和弦尚无可去除的高叠层。");
        targets = { pc(root + (family == 0 ? 11 : 10)),
                    pc(root + 2), pc(root + 5), pc(root + 9) };
    }
    if (remove && chord.seventhInterval < 0)
        return fail(u8"当前和弦尚无可去除的高叠层。");

    std::array<std::vector<int>, 4> present;
    if (chord.seventhInterval >= 0)
        present[0].push_back(pc(root + chord.seventhInterval));
    for (int interval : { 1, 2, 3 })
        if (interval != chord.thirdInterval && chord.containsInterval(interval))
            present[1].push_back(pc(root + interval));
    for (int interval : { 5, 6 })
        if (interval != chord.thirdInterval && interval != chord.fifthInterval
            && chord.containsInterval(interval))
            present[2].push_back(pc(root + interval));
    for (int interval : { 8, 9 })
        if (interval != chord.fifthInterval && interval != chord.seventhInterval
            && chord.containsInterval(interval))
            present[3].push_back(pc(root + interval));

    if (remove)
    {
        for (int layer = 3; layer >= 0; --layer)
            if (!present[(size_t)layer].empty())
            {
                if (existing.size() <= present[(size_t)layer].size() + 2)
                    return fail(u8"移除该层后不足以形成三音和弦。");
                choice.layer = layer;
                choice.removedPitchClasses = std::move(present[(size_t)layer]);
                return choice;
            }
        return fail(u8"未找到可移除的高叠音。");
    }
    for (int layer = 0; layer < 4; ++layer)
        if (present[(size_t)layer].empty())
        {
            choice.layer = layer;
            choice.addedPitchClass = targets[(size_t)layer];
            if (existing.count(choice.addedPitchClass))
                return fail(u8"目标高叠音与已有和弦音重合。");
            return choice;
        }
    return fail(u8"十三度之前的高叠音已齐全。");
}

} // namespace

ChordEditPlan ChordEditing::changeQuality(const MidiModel& model, const TrackModel& tracks,
                                         const std::vector<NoteEvent>& baseline,
                                         ChordShapeQuality quality)
{
    ChordEditPlan plan;
    if (!validate(tracks, baseline, plan)) return plan;
    const auto target = [&]() -> std::array<int, 3> {
        switch (quality)
        {
            case ChordShapeQuality::Major:            return { 4, 7, -1 };
            case ChordShapeQuality::Minor:            return { 3, 7, -1 };
            case ChordShapeQuality::Diminished:       return { 3, 6, -1 };
            case ChordShapeQuality::Augmented:        return { 4, 8, -1 };
            case ChordShapeQuality::Sus2:             return { 2, 7, -1 };
            case ChordShapeQuality::Sus4:             return { 5, 7, -1 };
            case ChordShapeQuality::Dominant7:        return { 4, 7, 10 };
            case ChordShapeQuality::Major7:           return { 4, 7, 11 };
            case ChordShapeQuality::Minor7:           return { 3, 7, 10 };
            case ChordShapeQuality::HalfDiminished7: return { 3, 6, 10 };
        }
        return { 4, 7, -1 };
    }();
    // Every selected event belongs to this one chord, regardless of onset.
    {
        const auto& group = baseline;
        ChordEditPlan::Group row;
        row.beat = std::min_element(group.begin(), group.end(),
            [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; })->startBeat;
        std::vector<int> pitches;
        std::set<int> sourcePcs;
        std::map<int, int> voiceCountByPc;
        for (const auto& n : group)
        {
            pitches.push_back(n.pitch);
            sourcePcs.insert(pc(n.pitch));
            ++voiceCountByPc[pc(n.pitch)];
        }
        if (row.issue.isEmpty() && sourcePcs.size() < 3)
            reject(plan, row, u8"至少选择三个不同音高类别。");
        if (row.issue.isEmpty())
        {
            const auto layout = model.getEffectiveLayoutAtBeat(row.beat, &tracks);
            const auto chord = ChordClassifier::identifyChord(
                group, layout.tonicPc, layout.scaleName, layout.tonicName);
            row.source = chord.fullName;
            const int root = chord.rootPitchClass;
            int sourceThird = chord.thirdInterval;
            if (chord.isPolychord)
                reject(plan, row, u8"复合和弦需要分别选择上下结构，无法整体转换和弦类型。");
            else if (!isUsableSingleChord(chord))
                reject(plan, row, u8"和弦根音识别不可靠，请调整选区。");
            else
            {
                // In a suspended add9 chord both 2 and 4 are present. The
                // classifier distinguishes the suspension from the colour tone.
                if (sourceThird < 0 && chord.quality.contains("sus4")) sourceThird = 5;
                if (sourceThird < 0 && chord.quality.contains("sus2")) sourceThird = 2;
                if (sourceThird < 0)
                    reject(plan, row, u8"无法唯一确定三度或挂留音。");
            }
            int sourceFifth = -1;
            if (sourcePcs.count(pc(root + 7))) sourceFifth = 7;
            else
            {
                const bool flat = sourcePcs.count(pc(root + 6)) != 0;
                const bool sharp = sourcePcs.count(pc(root + 8)) != 0;
                if (flat != sharp) sourceFifth = flat ? 6 : 8;
                else if (flat && sharp)
                    reject(plan, row, u8"无法唯一确定五度音。");
            }
            if (row.issue.isEmpty() && sourceFifth < 0
                && (target[1] != 7 || target[2] >= 0))
                reject(plan, row, u8"此和弦类型需要明确的五度音。");

            int sourceSeventh = -1;
            if (target[2] >= 0 && row.issue.isEmpty())
            {
                const bool flatSeventh = sourcePcs.count(pc(root + 10)) != 0;
                const bool majorSeventh = sourcePcs.count(pc(root + 11)) != 0;
                if (flatSeventh && majorSeventh)
                    reject(plan, row, u8"同时存在两种七度音，无法确定转换对象。");
                else if (flatSeventh) sourceSeventh = 10;
                else if (majorSeventh) sourceSeventh = 11;
                else if (sourceThird == 3 && sourceFifth == 6
                         && sourcePcs.count(pc(root + 9)))
                    sourceSeventh = 9;
            }

            const NoteEvent* promotedVoice = nullptr;
            if (target[2] >= 0 && sourceSeventh < 0 && row.issue.isEmpty())
            {
                if (group.size() < 4)
                    reject(plan, row, u8"七和弦至少需要四个声部；请再选一个音符。");
                else
                {
                    const int bass = *std::min_element(pitches.begin(), pitches.end());
                    int bestCost = 1000;
                    for (const auto& note : group)
                    {
                        const int notePc = pc(note.pitch);
                        const bool doubled = voiceCountByPc[notePc] > 1;
                        const bool extra = notePc != pc(root)
                            && notePc != pc(root + sourceThird)
                            && notePc != pc(root + sourceFifth);
                        if (!doubled && !extra) continue;
                        const int newPitch = closestPitch(note.pitch, pc(root + target[2]));
                        if (newPitch < 0) continue;
                        const int cost = std::abs(newPitch - note.pitch)
                            + (note.pitch == bass ? 12 : 0) + (doubled ? 0 : 8);
                        if (cost < bestCost) { bestCost = cost; promotedVoice = &note; }
                    }
                    if (promotedVoice == nullptr)
                        reject(plan, row, u8"没有可转为七度的重复音或附加音。");
                }
            }
            if (row.issue.isEmpty())
            {
                std::set<int> targetPcs;
                std::vector<NoteEvent> changes;
                for (const auto& n : group)
                {
                    int newPitch = n.pitch;
                    if (promotedVoice != nullptr && n.id == promotedVoice->id)
                        newPitch = closestPitch(n.pitch, pc(root + target[2]));
                    else if (pc(n.pitch) == pc(root + sourceThird))
                        newPitch += target[0] - sourceThird;
                    else if (sourceFifth >= 0 && pc(n.pitch) == pc(root + sourceFifth))
                        newPitch += target[1] - sourceFifth;
                    else if (sourceSeventh >= 0
                             && pc(n.pitch) == pc(root + sourceSeventh))
                        newPitch += target[2] - sourceSeventh;
                    if (newPitch < 0 || newPitch > 127)
                    {
                        reject(plan, row, u8"目标音超出 MIDI 音域。");
                        break;
                    }
                    targetPcs.insert(pc(newPitch));
                    if (newPitch != n.pitch)
                    {
                        auto changed = n;
                        changed.pitch = newPitch;
                        changes.push_back(changed);
                    }
                }
                const size_t expectedPcs = sourcePcs.size()
                    + (promotedVoice != nullptr
                       && voiceCountByPc[pc(promotedVoice->pitch)] > 1 ? 1 : 0);
                if (row.issue.isEmpty() && (targetPcs.size() != expectedPcs
                    || !targetPcs.count(pc(root + target[0]))
                    || (sourceFifth >= 0 && !targetPcs.count(pc(root + target[1])))
                    || (target[2] >= 0 && !targetPcs.count(pc(root + target[2])))))
                    reject(plan, row, u8"目标音与原和弦音冲突，无法保持独立声部。");
                if (row.issue.isEmpty())
                {
                    const auto recognized = identifyPlannedChord(group, changes,
                        {}, {}, layout.tonicPc, layout.scaleName, layout.tonicName);
                    if (!isUsableSingleChord(recognized))
                        reject(plan, row, u8"目标音符无法可靠识别为和弦，请调整音符排列。");
                    else
                    {
                        row.target = recognized.fullName;
                        row.supported = true;
                        row.changedVoices = (int)changes.size();
                        plan.changedNotes.insert(plan.changedNotes.end(),
                                                 changes.begin(), changes.end());
                    }
                }
            }
        }
        plan.groups.push_back(std::move(row));
    }
    finish(plan);
    return plan;
}

ChordEditPlan ChordEditing::changeVoicing(const MidiModel& model, const TrackModel& tracks,
                                         const std::vector<NoteEvent>& baseline,
                                         ChordShapeVoicing voicing)
{
    ChordEditPlan plan;
    if (!validate(tracks, baseline, plan)) return plan;
    {
        auto group = baseline;
        ChordEditPlan::Group row;
        row.beat = std::min_element(group.begin(), group.end(),
            [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; })->startBeat;
        std::sort(group.begin(), group.end(), [](const auto& a, const auto& b) {
            return a.pitch < b.pitch || (a.pitch == b.pitch && a.id < b.id);
        });
        const auto allEvents = group;
        group.erase(std::unique(group.begin(), group.end(),
            [](const auto& a, const auto& b) { return a.pitch == b.pitch; }), group.end());
        if (group.size() < 3)
            reject(plan, row, u8"排列操作至少需要三个不同音高。");
        std::vector<int> pitches;
        for (const auto& n : group) pitches.push_back(n.pitch);
        const auto layout = model.getEffectiveLayoutAtBeat(row.beat, &tracks);
        const auto chord = ChordClassifier::identifyChord(
            baseline, layout.tonicPc, layout.scaleName, layout.tonicName);
        row.source = chord.fullName;
        row.target = row.source;
        if (row.issue.isEmpty() && chord.isPolychord)
            reject(plan, row, u8"复合和弦需要分别选择上下结构，无法整体更改排列。");
        if (row.issue.isEmpty() && !isUsableSingleChord(chord))
            reject(plan, row, u8"无法可靠识别所选和弦。");
        if (row.issue.isEmpty() && voicing == ChordShapeVoicing::Drop2 && group.size() != 4)
            reject(plan, row, u8"Drop-2 需要恰好四个声部。");
        if (row.issue.isEmpty())
        {
            auto after = pitches;
            if (voicing == ChordShapeVoicing::Expand)
            {
                after = evenVoicing(group, true);
                if (after.empty())
                    reject(plan, row, u8"当前 MIDI 音域无法继续均匀展开。");
            }
            else if (voicing == ChordShapeVoicing::Contract)
            {
                auto contracted = evenVoicing(group, false);
                if (!contracted.empty()) after = std::move(contracted);
            }
            else after[group.size() - 2] -= 12;
            if (row.issue.isEmpty() && voicing == ChordShapeVoicing::Drop2)
            {
                std::set<int> uniquePitches(after.begin(), after.end());
                if (uniquePitches.size() != after.size())
                    reject(plan, row, u8"Drop-2 会产生重叠音高，请先调整排列。");
            }
            for (size_t i = 0; i < after.size(); ++i)
                if (after[i] < 0 || after[i] > 127)
                {
                    reject(plan, row, u8"排列后会超出 MIDI 音域。");
                    break;
                }
            if (row.issue.isEmpty())
            {
                std::map<int, int> targetByPitch;
                for (size_t i = 0; i < group.size(); ++i)
                    targetByPitch[group[i].pitch] = after[i];
                std::vector<NoteEvent> changes;
                for (const auto& note : allEvents)
                    if (targetByPitch.at(note.pitch) != note.pitch)
                    {
                        auto changed = note;
                        changed.pitch = targetByPitch.at(note.pitch);
                        changes.push_back(changed);
                    }
                const auto recognized = identifyPlannedChord(baseline, changes,
                    {}, {}, layout.tonicPc, layout.scaleName, layout.tonicName);
                if (!isUsableSingleChord(recognized))
                    reject(plan, row, u8"目标排列无法可靠识别为和弦。");
                else
                {
                    row.target = recognized.fullName;
                    row.supported = true;
                    row.changedVoices = (int)changes.size();
                    plan.changedNotes.insert(plan.changedNotes.end(),
                                             changes.begin(), changes.end());
                }
            }
        }
        plan.groups.push_back(std::move(row));
    }
    finish(plan);
    return plan;
}

ChordEditPlan ChordEditing::changeStack(const MidiModel& model, const TrackModel& tracks,
                                       const std::vector<NoteEvent>& selected,
                                       ChordStackDirection direction)
{
    ChordEditPlan plan;
    if (!validate(tracks, selected, plan)) return plan;
    const bool remove = (int)direction >= (int)ChordStackDirection::RemoveMajor;
    const int family = (int)direction % 4;
    {
        auto group = selected;
        ChordEditPlan::Group row;
        row.beat = std::min_element(group.begin(), group.end(),
            [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; })->startBeat;
        std::sort(group.begin(), group.end(), [](const auto& a, const auto& b) {
            return a.pitch < b.pitch || (a.pitch == b.pitch && a.id < b.id);
        });
        std::set<int> existing;
        for (const auto& note : group)
        {
            existing.insert(pc(note.pitch));
        }
        const auto layout = model.getEffectiveLayoutAtBeat(row.beat, &tracks);
        const auto chord = ChordClassifier::identifyChord(
            group, layout.tonicPc, layout.scaleName, layout.tonicName);
        row.source = chord.fullName;
        if (chord.isPolychord)
            reject(plan, row, u8"复合和弦需要分别选择上下结构，无法整体增减高叠音。");
        else if (existing.size() < 3 || !isUsableSingleChord(chord))
            reject(plan, row, u8"请选中一个可识别的三音以上和弦。");
        StackLayerChoice choice;
        if (row.issue.isEmpty())
        {
            choice = chooseStackLayer(chord, existing, layout, family, remove);
            if (choice.issue.isNotEmpty())
            {
                row.issue = choice.issue;
                if (plan.error.isEmpty()) plan.error = choice.issue;
            }
        }
        if (row.issue.isEmpty())
        {
            if (remove)
            {
                for (const auto& note : group)
                    if (std::find(choice.removedPitchClasses.begin(),
                                  choice.removedPitchClasses.end(), pc(note.pitch))
                        != choice.removedPitchClasses.end())
                    {
                        plan.removedNoteIds.push_back(note.id);
                        ++row.changedVoices;
                    }
            }
            else
            {
                int pitch = -1;
                const auto& reference = *std::max_element(group.begin(), group.end(),
                    [](const NoteEvent& a, const NoteEvent& b) {
                        return a.startBeat < b.startBeat;
                    });
                const auto available = [&](int candidate) {
                    if (pc(candidate) != choice.addedPitchClass) return false;
                    const double start = reference.startBeat;
                    const double end = start + reference.lengthBeats;
                    for (const auto& note : model.getNotes())
                        if (note.pitch == candidate
                            && note.startBeat < end - 0.0001
                            && note.startBeat + note.lengthBeats > start + 0.0001)
                            return false;
                    for (const auto& note : plan.addedNotes)
                        if (note.pitch == candidate
                            && note.startBeat < end - 0.0001
                            && note.startBeat + note.lengthBeats > start + 0.0001)
                            return false;
                    return true;
                };
                for (int candidate = group.back().pitch + 1; candidate < 128; ++candidate)
                    if (available(candidate)) { pitch = candidate; break; }
                if (pitch < 0)
                    reject(plan, row, u8"当前音域无法在和弦上方加入高叠音。");
                else
                {
                    NoteEvent added = reference;
                    added.id = 0; // Assigned when the atomic edit is committed.
                    added.pitch = pitch;
                    added.isSelected = true;
                    plan.addedNotes.push_back(added);
                    row.changedVoices = 1;
                }
            }
            if (row.issue.isEmpty())
            {
                const auto recognized = identifyPlannedChord(group, {},
                    plan.addedNotes, plan.removedNoteIds,
                    layout.tonicPc, layout.scaleName, layout.tonicName);
                if (!isUsableSingleChord(recognized))
                    reject(plan, row, u8"高叠变换后的音符无法可靠识别为和弦。");
                else
                {
                    row.target = recognized.fullName;
                    row.supported = true;
                }
            }
        }
        plan.groups.push_back(std::move(row));
    }
    finish(plan);
    return plan;
}

juce::String ChordEditing::borrowedChordName(int tonicPc, const juce::String& sourceScale,
                                             int degree, bool seventh)
{
    const auto* scale = exactSevenNoteScale(sourceScale);
    if (scale == nullptr || tonicPc < 0 || tonicPc > 11 || degree < 0 || degree > 6)
        return {};
    auto notes = createBorrowedChordNotes(tonicPc, sourceScale, degree, 0.0, 1.0);
    if (seventh && notes.size() == 3)
    {
        const int scaleIndex = degree + 6;
        NoteEvent seventhNote = notes.back();
        seventhNote.pitch = 48 + tonicPc
            + scale->intervals[(size_t)(scaleIndex % 7)]
            + 12 * (scaleIndex / 7);
        while (seventhNote.pitch <= notes.back().pitch) seventhNote.pitch += 12;
        notes.push_back(seventhNote);
    }
    if (notes.empty()) return {};
    const auto chord = ChordClassifier::identifyChord(notes, tonicPc, sourceScale);
    return isUsableSingleChord(chord) ? chord.fullName : juce::String{};
}

std::vector<NoteEvent> ChordEditing::createBorrowedChordNotes(
    int tonicPc, const juce::String& targetScale, int degree,
    double startBeat, double lengthBeats)
{
    const auto* scale = exactSevenNoteScale(targetScale);
    if (scale == nullptr || tonicPc < 0 || tonicPc > 11
        || degree < 0 || degree > 6 || !std::isfinite(startBeat)
        || !std::isfinite(lengthBeats) || lengthBeats <= 0.0)
        return {};
    std::vector<NoteEvent> notes;
    int previousPitch = -1;
    for (int voice = 0; voice < 3; ++voice)
    {
        const int scaleIndex = degree + voice * 2;
        int pitch = 48 + tonicPc + scale->intervals[(size_t)(scaleIndex % 7)]
            + 12 * (scaleIndex / 7);
        while (pitch <= previousPitch) pitch += 12;
        if (pitch < 0 || pitch > 127) return {};
        NoteEvent note;
        note.pitch = pitch;
        note.startBeat = startBeat;
        note.lengthBeats = lengthBeats;
        notes.push_back(note);
        previousPitch = pitch;
    }
    return notes;
}

ChordEditPlan ChordEditing::borrow(const MidiModel& model, const TrackModel& tracks,
                                  const std::vector<NoteEvent>& selected,
                                  int sourceTonicPc, const juce::String& sourceScale,
                                  int targetTonicPc, const juce::String& targetScale,
                                  ModeBorrowingStyle style,
                                  int degree, bool automaticSource)
{
    ChordEditPlan plan;
    if (!validate(tracks, selected, plan)) return plan;
    const auto* targetMode = exactSevenNoteScale(targetScale);
    if ((!automaticSource && (exactSevenNoteScale(sourceScale) == nullptr
            || sourceTonicPc < 0 || sourceTonicPc > 11)) || targetMode == nullptr
        || targetTonicPc < 0 || targetTonicPc > 11
        || degree < -1 || degree > 6)
    {
        plan.error = juce::String::fromUTF8(u8"请选择有效的前调式和目标调式。");
        return plan;
    }
    const double barLength = (double)model.getTimeSigNumerator() * 4.0
        / (double)std::max(1, model.getTimeSigDenominator());
    for (auto group : groupSelectedHarmonyNotes(selected, barLength))
    {
        ChordEditPlan::Group row;
        const size_t groupChangeStart = plan.changedNotes.size();
        row.beat = group.front().startBeat;
        const auto effective = automaticSource
            ? model.getEffectiveLayoutAtBeat(row.beat, &tracks) : KeyLayout{};
        const int groupSourceTonic = automaticSource ? effective.tonicPc : sourceTonicPc;
        const auto groupSourceScale = automaticSource ? effective.scaleName : sourceScale;
        const auto groupSourceTonicName = automaticSource ? effective.tonicName
            : ScaleDefinitions::getPreferredTonicName(sourceTonicPc, sourceScale);
        const int targetTonic = style == ModeBorrowingStyle::SameTonic
            ? groupSourceTonic : targetTonicPc;
        const auto targetTonicName = style == ModeBorrowingStyle::SameTonic
            ? groupSourceTonicName
            : ScaleDefinitions::getPreferredTonicName(targetTonic, targetScale);
        row.sourceMode = ChordClassifier::spellPitchClass(
            groupSourceTonic, groupSourceTonic, groupSourceScale,
            groupSourceTonicName)
            + " " + groupSourceScale;
        row.targetMode = ChordClassifier::spellPitchClass(
            targetTonic, targetTonic, targetScale, targetTonicName)
            + " " + targetScale;
        std::set<int> pcs;
        for (const auto& n : group) pcs.insert(pc(n.pitch));
        if (group.size() > 16 || pcs.size() < 3)
            reject(plan, row, u8"每组至少需要三个不同和弦音，且不能超过 16 个声部。");
        if (row.issue.isEmpty() && exactSevenNoteScale(groupSourceScale) == nullptr)
            reject(plan, row, u8"当前音符位置的前调式不是有效的七声音阶。");
        if (row.issue.isEmpty())
        {
            const auto chord = ChordClassifier::identifyChord(
                group, groupSourceTonic, groupSourceScale, groupSourceTonicName);
            row.source = chord.fullName;
            if (chord.isPolychord)
                reject(plan, row, u8"复合和弦需要分别选择上下结构，无法整体进行调式借用。");
            else if (!isUsableSingleChord(chord))
                reject(plan, row, u8"无法可靠识别所选和弦。");
            else if (style == ModeBorrowingStyle::SameTonic)
            {
                int targetDegree = degree;
                if (targetDegree < 0)
                {
                    auto it = std::find_if(targetMode->intervals.begin(), targetMode->intervals.end(),
                        [&](int iv) { return pc(targetTonic + iv) == chord.rootPitchClass; });
                    if (it == targetMode->intervals.end())
                        reject(plan, row, u8"目标调式没有当前和弦的根音，请改选目标级数。");
                    else targetDegree = (int)std::distance(targetMode->intervals.begin(), it);
                }
                if (row.issue.isEmpty())
                {
                    const bool seventh = pcs.size() >= 4 && group.size() >= 4;
                    auto target = scaleChord(targetTonic, *targetMode, targetDegree, seventh);
                    std::sort(group.begin(), group.end(), [](const auto& a, const auto& b) {
                        return a.pitch < b.pitch || (a.pitch == b.pitch && a.id < b.id);
                    });
                    // Identical MIDI pitches are one harmonic voice even if
                    // selected at different times. Map every event in that
                    // voice to the same destination and keep its own timing.
                    auto voices = group;
                    voices.erase(std::unique(voices.begin(), voices.end(),
                        [](const NoteEvent& a, const NoteEvent& b) {
                            return a.pitch == b.pitch;
                        }), voices.end());
                    auto assigned = closestVoicing(voices, target);
                    if (assigned.size() != voices.size())
                        reject(plan, row, u8"当前音域无法排列目标和弦。");
                    else
                    {
                        std::map<int, int> targetByPitch;
                        for (size_t i = 0; i < voices.size(); ++i)
                            targetByPitch[voices[i].pitch] = assigned[i];
                        row.supported = true;
                        for (const auto& note : group)
                            if (note.pitch != targetByPitch.at(note.pitch))
                            {
                                auto changed = note;
                                changed.pitch = targetByPitch.at(note.pitch);
                                plan.changedNotes.push_back(changed);
                                ++row.changedVoices;
                            }
                    }
                }
            }
            else
            {
                const int root = chord.rootPitchClass;
                std::set<int> targetScale;
                for (int iv : targetMode->intervals) targetScale.insert(pc(targetTonic + iv));
                if (!targetScale.count(root))
                    reject(plan, row, u8"目标调式没有原根音，无法在保留根音的前提下趋近。");
                else
                {
                    std::map<int, int> mapped;
                    std::set<int> used;
                    for (int sourcePc : pcs)
                        if (targetScale.count(sourcePc)) { mapped[sourcePc] = sourcePc; used.insert(sourcePc); }
                    std::vector<int> displaced;
                    for (int sourcePc : pcs)
                        if (!targetScale.count(sourcePc)) displaced.push_back(sourcePc);
                    std::stable_sort(displaced.begin(), displaced.end(), [root](int a, int b) {
                        auto priority = [root](int tone) {
                            int iv = pc(tone - root);
                            return (iv == 3 || iv == 4) ? 0
                                : (iv == 6 || iv == 7 || iv == 8) ? 1
                                : (iv == 10 || iv == 11) ? 2 : 3;
                        };
                        return priority(a) < priority(b);
                    });
                    for (int sourcePc : displaced)
                    {
                        int targetPc = -1;
                        int bestCost = 1000;
                        for (int candidate : targetScale)
                        {
                            if (used.count(candidate) || candidate == root) continue;
                            int distance = std::min(pc(candidate - sourcePc), pc(sourcePc - candidate));
                            int cost = distance * 10 + rolePenalty(pc(sourcePc - root), pc(candidate - root));
                            if (cost < bestCost) { bestCost = cost; targetPc = candidate; }
                        }
                        if (targetPc < 0) { reject(plan, row, u8"目标调式没有足够的不同和弦音。"); break; }
                        mapped[sourcePc] = targetPc;
                        used.insert(targetPc);
                    }
                    if (row.issue.isEmpty())
                    {
                        row.supported = true;
                        for (const auto& n : group)
                        {
                            int targetPc = mapped[pc(n.pitch)];
                            int targetPitch = closestPitch(n.pitch, targetPc);
                            if (targetPitch < 0)
                            {
                                reject(plan, row, u8"目标音超出 MIDI 音域。");
                                row.supported = false;
                                break;
                            }
                            if (targetPitch != n.pitch)
                            {
                                auto changed = n;
                                changed.pitch = targetPitch;
                                plan.changedNotes.push_back(changed);
                                ++row.changedVoices;
                            }
                        }
                    }
                }
            }
        }
        if (row.supported && row.issue.isEmpty())
        {
            const std::vector<NoteEvent> groupChanges(
                plan.changedNotes.begin() + (std::ptrdiff_t)groupChangeStart,
                plan.changedNotes.end());
            const auto recognized = identifyPlannedChord(group, groupChanges,
                {}, {}, targetTonic, targetScale, targetTonicName);
            if (!isUsableSingleChord(recognized))
            {
                row.supported = false;
                reject(plan, row, u8"借用后的音符无法可靠识别为和弦。");
            }
            else row.target = recognized.fullName;
        }
        plan.groups.push_back(std::move(row));
    }
    finish(plan);
    return plan;
}

} // namespace Harmonizer
