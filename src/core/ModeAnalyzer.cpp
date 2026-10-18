#include "ModeAnalyzer.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <map>

namespace Harmonizer
{

// Classical K-K Profiles
static const std::vector<float> KK_MAJOR = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
static const std::vector<float> KK_MINOR = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

float ModeAnalyzer::getScalePrior(const juce::String& scaleName)
{
    if (scaleName.containsIgnoreCase("Pentatonic"))
        return 0.60f;
    if (scaleName.containsIgnoreCase("Blues"))
        return 0.50f;
    if (scaleName.containsIgnoreCase("Ionian") || scaleName.containsIgnoreCase("Major"))
        return 1.00f;
    if (scaleName.containsIgnoreCase("Aeolian") || scaleName.containsIgnoreCase("Natural Minor"))
        return 0.92f;
    if (scaleName.containsIgnoreCase("Harmonic Minor"))
        return 0.85f;
    if (scaleName.containsIgnoreCase("Melodic Minor"))
        return 0.70f;
    if (scaleName.containsIgnoreCase("Dorian") || scaleName.containsIgnoreCase("Mixolydian"))
        return 0.38f;
    if (scaleName.containsIgnoreCase("Lydian"))
        return 0.30f;
    if (scaleName.containsIgnoreCase("Phrygian"))
        return 0.28f;
    if (scaleName.containsIgnoreCase("Locrian"))
        return 0.15f;
    return 0.35f;
}

std::vector<float> ModeAnalyzer::getScaleProfile(const juce::String& scaleName, const std::vector<int>& intervals)
{
    if (scaleName.containsIgnoreCase("Ionian") || (scaleName.containsIgnoreCase("Major") && !scaleName.containsIgnoreCase("Pentatonic")))
        return KK_MAJOR;
    if (scaleName.containsIgnoreCase("Aeolian") || (scaleName.containsIgnoreCase("Minor") && !scaleName.containsIgnoreCase("Harmonic") && !scaleName.containsIgnoreCase("Melodic") && !scaleName.containsIgnoreCase("Pentatonic")))
        return KK_MINOR;

    std::vector<float> profile(12, 0.1f);
    profile[0] = 6.4f; // Tonic
    for (int iv : intervals)
    {
        const int pc = (iv % 12 + 12) % 12;
        if (pc == 7) profile[7] = 5.2f; // Dominant
        else if (pc == 5) profile[5] = 4.0f; // Subdominant
        else if (pc == 4 || pc == 3) profile[pc] = 4.6f; // 3rd
        else if (pc != 0) profile[pc] = 3.0f;
    }
    return profile;
}

float ModeAnalyzer::pearsonCorrelation(const std::vector<float>& vec1, const std::vector<float>& vec2)
{
    int n = (int)vec1.size();
    if (n != (int)vec2.size() || n == 0) return 0.0f;

    float m1 = std::accumulate(vec1.begin(), vec1.end(), 0.0f) / (float)n;
    float m2 = std::accumulate(vec2.begin(), vec2.end(), 0.0f) / (float)n;

    float num = 0.0f, d1 = 0.0f, d2 = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float x = vec1[i] - m1;
        float y = vec2[i] - m2;
        num += x * y;
        d1 += x * x;
        d2 += y * y;
    }

    if (d1 <= 1e-9f || d2 <= 1e-9f)
        return 0.0f;

    return num / std::sqrt(d1 * d2);
}

std::vector<float> ModeAnalyzer::extractFeatures(const std::vector<NoteEvent>& notes, double barLengthBeats)
{
    std::vector<float> durationWeights(12, 0.0f);
    std::vector<float> metricWeights(12, 0.0f);
    std::vector<float> headTailWeights(12, 0.0f);

    int totalNotes = (int)notes.size();
    if (totalNotes == 0)
        return std::vector<float>(12, 1.0f / 12.0f);

    double barLen = (barLengthBeats > 0.5) ? barLengthBeats : 4.0;
    std::vector<const NoteEvent*> chronological;
    chronological.reserve(notes.size());
    for (const auto& note : notes) chronological.push_back(&note);
    std::stable_sort(chronological.begin(), chronological.end(), [](const auto* a, const auto* b) {
        return a->startBeat < b->startBeat;
    });
    for (int idx = 0; idx < totalNotes; ++idx)
    {
        const auto& n = *chronological[(size_t)idx];
        int pc = (n.pitch % 12 + 12) % 12;
        float dur = (float)n.lengthBeats;
        float vel = std::clamp(n.velocity, 0.1f, 1.0f);
        float baseW = dur * vel;

        durationWeights[pc] += baseW;

        // Metric accentuation: downbeat and mid-bar accentuation based on dynamic meter
        double beatInBar = std::fmod(n.startBeat, barLen);
        if (beatInBar < 0.2)
            metricWeights[pc] += baseW * 2.2f;
        else if (std::abs(beatInBar - (barLen * 0.5)) < 0.2)
            metricWeights[pc] += baseW * 1.4f;
        else
            metricWeights[pc] += baseW * 0.8f;

        // Head/tail cadence anchoring
        if (idx < std::max(4, (int)(totalNotes * 0.2f)) || idx >= (int)(totalNotes * 0.85f))
            headTailWeights[pc] += baseW * 1.8f;
        else
            headTailWeights[pc] += baseW * 0.5f;
    }

    float sumDur = std::accumulate(durationWeights.begin(), durationWeights.end(), 0.0f);
    float sumMet = std::accumulate(metricWeights.begin(), metricWeights.end(), 0.0f);
    float sumHt  = std::accumulate(headTailWeights.begin(), headTailWeights.end(), 0.0f);
    if (sumDur <= 1e-6f) sumDur = 1.0f;
    if (sumMet <= 1e-6f) sumMet = 1.0f;
    if (sumHt  <= 1e-6f) sumHt  = 1.0f;

    std::vector<float> combined(12, 0.0f);
    for (int pc = 0; pc < 12; ++pc)
    {
        combined[pc] = (durationWeights[pc] / sumDur) * 0.50f +
                       (metricWeights[pc] / sumMet) * 0.30f +
                       (headTailWeights[pc] / sumHt) * 0.20f;
    }
    return combined;
}

AnalysisResult ModeAnalyzer::analyze(const std::vector<NoteEvent>& notes, int topK, double barLengthBeats)
{
    AnalysisResult res;
    if (notes.empty())
    {
        res.pitchDistribution = std::vector<float>(12, 0.0f);
        return res;
    }

    auto observedVec = extractFeatures(notes, barLengthBeats);
    res.pitchDistribution = observedVec;

    auto allScales = ScaleDefinitions::getAllScales();
    static const char* pitchNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    std::vector<ModeCandidate> candidates;

    for (int tonicPc = 0; tonicPc < 12; ++tonicPc)
    {
        juce::String tonicName = pitchNames[tonicPc];

        for (const auto& sc : allScales)
        {
            auto profile = getScaleProfile(sc.name, sc.intervals);

            std::vector<float> shiftedProfile(12, 0.0f);
            for (int i = 0; i < 12; ++i)
                shiftedProfile[(tonicPc + i) % 12] = profile[i];

            float r = pearsonCorrelation(observedVec, shiftedProfile);
            float normR = std::max(0.0f, (r + 1.0f) * 0.5f);

            // In-scale coverage
            float inScaleCov = 0.0f;
            for (int iv : sc.intervals)
            {
                int notePc = (tonicPc + iv % 12 + 12) % 12;
                inScaleCov += observedVec[notePc];
            }

            // Anchor factor (Tonic & Dominant backbone)
            int domPc = (tonicPc + 7) % 12;
            float tonicWeight = observedVec[tonicPc];
            float domWeight = observedVec[domPc];
            float anchorFactor = std::min(1.0f, (tonicWeight * 2.0f + domWeight * 1.2f) * 3.5f);

            // Heuristic preference for commonly used scales.
            float prior = getScalePrior(sc.name);

            // Composite score: 40% profile correlation + 30% in-scale + 15% anchor + 15% prior
            float finalScore = (normR * 0.40f + inScaleCov * 0.30f + anchorFactor * 0.15f + prior * 0.15f) * 100.0f;
            if (prior < 0.5f)
                finalScore *= 0.92f;

            ModeCandidate cand;
            cand.tonic = tonicName;
            cand.tonicPc = tonicPc;
            cand.scaleName = sc.name;
            cand.correlation = r;
            cand.inScaleRatio = inScaleCov * 100.0f;
            cand.combinedScore = finalScore;
            cand.label = tonicName + " " + sc.name + " (" + juce::String((int)finalScore) + "%)";

            candidates.push_back(cand);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const ModeCandidate& a, const ModeCandidate& b) {
        return a.combinedScore > b.combinedScore;
    });

    if (!candidates.empty())
    {
        res.bestTonic = candidates[0].tonic;
        res.bestTonicPc = candidates[0].tonicPc;
        res.bestScale = candidates[0].scaleName;
        res.confidence = candidates[0].combinedScore;
        res.inScaleRatio = candidates[0].inScaleRatio;

        int count = std::min(topK, (int)candidates.size());
        for (int i = 0; i < count; ++i)
            res.topCandidates.push_back(candidates[i]);
    }

    return res;
}

AnalysisResult ModeAnalyzer::analyzeSelection(const std::vector<NoteEvent>& notes, int topK)
{
    AnalysisResult res;
    res.pitchDistribution.assign(12, 0.0f);
    if (notes.empty() || topK <= 0)
        return res;

    double firstBeat = notes.front().startBeat;
    double lastBeat = firstBeat;
    const NoteEvent* lowest = &notes.front();
    for (const auto& note : notes)
    {
        const int pc = (note.pitch % 12 + 12) % 12;
        // Weight repeated notes gently; one long note must not erase other selected tones.
        res.pitchDistribution[pc] += std::sqrt((float)std::max(0.1, note.lengthBeats))
                                   * std::clamp(note.velocity, 0.2f, 1.0f);
        firstBeat = std::min(firstBeat, note.startBeat);
        lastBeat = std::max(lastBeat, note.startBeat);
        if (note.pitch < lowest->pitch)
            lowest = &note;
    }

    const float totalWeight = std::accumulate(res.pitchDistribution.begin(),
                                              res.pitchDistribution.end(), 0.0f);
    for (auto& weight : res.pitchDistribution)
    {
        if (weight > 0.0f)
            ++res.distinctPitchClassCount;
        weight /= std::max(totalWeight, 0.0001f);
    }
    if (res.distinctPitchClassCount < 2)
        return res;

    const bool simultaneous = lastBeat - firstBeat < 0.125;
    const int bassPc = (lowest->pitch % 12 + 12) % 12;
    std::vector<ModeCandidate> candidates;
    for (int tonicPc = 0; tonicPc < 12; ++tonicPc)
    {
        for (const auto& scale : ScaleDefinitions::getAllScales())
        {
            bool members[12] = {};
            for (int interval : scale.intervals)
                members[(tonicPc + interval % 12 + 12) % 12] = true;

            int matched = 0;
            for (int pc = 0; pc < 12; ++pc)
                if (members[pc] && res.pitchDistribution[pc] > 0.0f)
                    ++matched;

            const float coverage = (float)matched / (float)res.distinctPitchClassCount;
            if (matched == res.distinctPitchClassCount)
                ++res.exactMatchCount;

            int characteristic = -1;
            if (scale.name.startsWithIgnoreCase("Dorian")) characteristic = 9;
            else if (scale.name.startsWithIgnoreCase("Phrygian")) characteristic = 1;
            else if (scale.name.startsWithIgnoreCase("Lydian")) characteristic = 6;
            else if (scale.name.startsWithIgnoreCase("Mixolydian")) characteristic = 10;
            else if (scale.name.startsWithIgnoreCase("Locrian")) characteristic = 6;
            else if (scale.name.startsWithIgnoreCase("Harmonic Minor")) characteristic = 11;
            else if (scale.name.startsWithIgnoreCase("Melodic Minor")) characteristic = 9;

            const float tonicEvidence = res.pitchDistribution[tonicPc];
            const float bassEvidence = simultaneous && bassPc == tonicPc ? 1.0f : 0.0f;
            const float colourEvidence = characteristic >= 0
                && res.pitchDistribution[(tonicPc + characteristic) % 12] > 0.0f ? 1.0f : 0.0f;
            const float unusedDegrees = (float)std::max(0, (int)scale.intervals.size() - matched);

            ModeCandidate candidate;
            candidate.tonicPc = tonicPc;
            candidate.tonic = ScaleDefinitions::getPitchName(tonicPc);
            candidate.scaleName = scale.name;
            candidate.inScaleRatio = coverage * 100.0f;
            // A ranking score, not a calibrated probability of the musical key.
            candidate.combinedScore = 75.0f * coverage + 10.0f * tonicEvidence
                + 6.0f * bassEvidence + 6.0f * colourEvidence
                + 3.0f * (1.0f - unusedDegrees / 7.0f);
            candidates.push_back(std::move(candidate));
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.inScaleRatio != b.inScaleRatio)
            return a.inScaleRatio > b.inScaleRatio;
        return a.combinedScore > b.combinedScore;
    });

    const int count = std::min(topK, res.exactMatchCount > 0
        ? res.exactMatchCount : (int)candidates.size());
    res.topCandidates.assign(candidates.begin(), candidates.begin() + count);
    if (!res.topCandidates.empty())
    {
        const auto& best = res.topCandidates.front();
        res.bestTonic = best.tonic;
        res.bestTonicPc = best.tonicPc;
        res.bestScale = best.scaleName;
        res.confidence = best.combinedScore;
        res.inScaleRatio = best.inScaleRatio;
    }
    return res;
}

} // namespace Harmonizer
