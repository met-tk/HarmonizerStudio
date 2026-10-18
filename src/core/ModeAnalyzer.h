#pragma once

#include <juce_core/juce_core.h>
#include "MidiModel.h"
#include <vector>

namespace Harmonizer
{

struct ModeCandidate
{
    juce::String tonic;
    int tonicPc = 0;
    juce::String scaleName;
    float correlation = 0.0f;
    float inScaleRatio = 0.0f;
    float combinedScore = 0.0f; // Ranking score, not a calibrated probability
    juce::String label;
};

struct AnalysisResult
{
    juce::String bestTonic = "C";
    int bestTonicPc = 0;
    juce::String bestScale = "Ionian (Natural Major)";
    float confidence = 0.0f; // Top candidate's ranking score
    float inScaleRatio = 0.0f;
    std::vector<ModeCandidate> topCandidates;
    std::vector<float> pitchDistribution; // 12 pitch classes
    int distinctPitchClassCount = 0;
    int exactMatchCount = 0;
};

class ModeAnalyzer
{
public:
    static AnalysisResult analyze(const std::vector<NoteEvent>& notes, int topK = 8, double barLengthBeats = 4.0);
    // Selected notes use pitch-set inclusion, independently of whole-track profiling.
    static AnalysisResult analyzeSelection(const std::vector<NoteEvent>& notes, int topK = 8);

private:
    static float pearsonCorrelation(const std::vector<float>& vec1, const std::vector<float>& vec2);
    static std::vector<float> extractFeatures(const std::vector<NoteEvent>& notes, double barLengthBeats = 4.0);
    static float getScalePrior(const juce::String& scaleName);
    static std::vector<float> getScaleProfile(const juce::String& scaleName, const std::vector<int>& intervals);
};

} // namespace Harmonizer
