#pragma once

#include <juce_core/juce_core.h>
#include "MidiModel.h"
#include <cstdint>
#include <functional>
#include <vector>
#include <set>

namespace Harmonizer
{

enum class ChordFamily
{
    Unknown, Single, Dyad, Power, Major, Minor, Dominant,
    Diminished, HalfDiminished, Augmented, Suspended
};

/** A name is only asserted when the pitch set supplies enough harmonic evidence. */
enum class ChordRecognitionStatus
{
    None, SinglePitch, Interval, Recognized, Ambiguous
};

enum class ChordToneRole
{
    Root, Third, Fifth, Seventh, Sixth, Suspension, Extension, Alteration, Other
};

struct ChordNoteRole
{
    int64_t noteId = 0;
    int pitch = 0;
    int intervalFromRoot = 0;
    ChordToneRole role = ChordToneRole::Other;
    int polychordLayer = -1;              // -1=single structure, 0=lower, 1=upper
};

struct ChordCandidate
{
    int rootPitchClass = 0;
    int bassPitchClass = 0;
    int upperRootPitchClass = -1;
    int polychordSplitPitch = -1;
    juce::String lowerQuality;             // Formula of the lower triad in a polychord
    juce::String upperQuality;             // Formula of the upper triad in a polychord
    int lowerInversion = -1;
    int upperInversion = -1;
    juce::String quality;
    juce::String fullName;
    juce::String romanNumeral;
    ChordFamily family = ChordFamily::Unknown;
    uint16_t intervalMask = 0;
    int thirdInterval = -1;
    int fifthInterval = -1;
    int seventhInterval = -1;
    int inversion = -1;
    std::vector<int> tensionIntervals;
    std::vector<ChordNoteRole> noteRoles;
    double score = 0.0; // Ranking value, not a probability.
    std::vector<int> omittedIntervals;
    std::vector<int> addedIntervals;
};

struct IdentifiedChord
{
    int rootPitchClass = 0;             // 0 = C, 1 = C#, ... 11 = B
    int bassPitchClass = 0;             // Actual lowest sounding note
    juce::String rootName = "C";        // e.g. "C", "F#"
    juce::String quality = "Maj";       // e.g. "maj7", "m9", "7alt"
    juce::String fullName = "Cmaj7";    // e.g. "Cmaj7/E"
    juce::String romanNumeral = "I";    // e.g. "Imaj7", "V7/V", "vi"
    std::vector<int> tensionIntervals;  // Semitone intervals from root for tensions
    bool isSlashChord = false;
    double confidence = 1.0;             // Heuristic match score, not a probability
    ChordFamily family = ChordFamily::Unknown;
    uint16_t intervalMask = 0;            // Actual pitch classes relative to the root
    int thirdInterval = -1;               // 3, 4, 2/5 for suspended, or unknown
    int fifthInterval = -1;               // 6, 7, 8, or omitted
    int seventhInterval = -1;             // 9 (diminished), 10, 11, or omitted
    ChordRecognitionStatus status = ChordRecognitionStatus::None;
    std::vector<int64_t> noteIds;          // Every selected source note, including doublings
    std::vector<ChordNoteRole> noteRoles;  // One role for each selected source note
    std::vector<ChordCandidate> alternatives; // Other defensible interpretations
    std::vector<int> omittedIntervals;
    std::vector<int> addedIntervals;
    int inversion = -1;                   // 0=root, 1=third, 2=fifth, 3=seventh; -1=other
    bool isPolychord = false;
    int upperRootPitchClass = -1;         // Only meaningful for a register-separated polychord
    int polychordSplitPitch = -1;
    juce::String lowerQuality;
    juce::String upperQuality;
    int lowerInversion = -1;
    int upperInversion = -1;

    bool containsInterval(int semitones) const
    {
        return (intervalMask & (uint16_t(1) << ((semitones % 12 + 12) % 12))) != 0;
    }
    bool hasDominantGuideTones() const
    {
        return containsInterval(4) && containsInterval(10);
    }
};

struct ChordAnalysisContext
{
    int tonicPc = 0;
    juce::String scaleName = "Ionian (Natural Major)";
    juce::String tonicName; // Optional explicit spelling, e.g. C# instead of Db
};

struct IdentifiedChordGroup
{
    double startBeat = 0.0;
    double endBeat = 0.0;
    IdentifiedChord chord;
};

class ChordClassifier
{
public:
    ChordClassifier() = delete;

    /** Classifies every note in the selection as one pitch set, regardless of onset. */
    static IdentifiedChord identifyChord(const std::vector<NoteEvent>& notes,
                                         int keyTonicPc = 0,
                                         const juce::String& scaleName = "Ionian (Natural Major)",
                                         const juce::String& tonicName = {});

    /** Splits a time-spanning selection into local chords before classifying each pitch set. */
    static std::vector<IdentifiedChordGroup> identifyChordGroups(
        const std::vector<NoteEvent>& notes, double barLengthBeats,
        const std::function<ChordAnalysisContext(double)>& contextAtBeat);

    /** Classifies a chord directly from a list of MIDI pitches. */
    static IdentifiedChord identifyChordFromPitches(const std::vector<int>& pitches,
                                                   int keyTonicPc = 0,
                                                   const juce::String& scaleName = "Ionian (Natural Major)",
                                                   const juce::String& tonicName = {});

    /** Returns all available chord qualities with display names and formula definitions. */
    struct QualityOption
    {
        juce::String id;
        juce::String displayName;
        juce::String category; // "Triad", "Seventh", "Sixth", "Extended", "Altered"
        std::vector<int> requiredIntervals;
        std::vector<std::vector<int>> acceptedVoicings; // Equally valid sets, e.g. 13 without 11
    };
    static const std::vector<QualityOption>& getSupportedQualities();

    /** Derives roman numeral relative to a key and mode. */
    static juce::String computeRomanNumeral(int rootPc, int bassPc, const juce::String& quality,
                                            int keyTonicPc, const juce::String& scaleName);

    /** Spells a pitch class consistently with the current key and scale. */
    static juce::String spellPitchClass(int pitchClass, int keyTonicPc,
                                       const juce::String& scaleName,
                                       const juce::String& tonicName = {});

    /** Constructs chord name from root, quality, and optional tensions & bass. */
    static juce::String buildChordName(const juce::String& root, const juce::String& quality,
                                       const std::vector<juce::String>& activeTensions,
                                       const juce::String& slashBass = "");
};

} // namespace Harmonizer
