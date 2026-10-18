#include "ChordClassifier.h"
#include "HarmonyGrouping.h"
#include "ScaleDefinitions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace Harmonizer
{
namespace
{
int pc(int pitch) { return (pitch % 12 + 12) % 12; }
int interval(int pitchClass, int root) { return pc(pitchClass - root); }
uint16_t bit(int semitones) { return uint16_t(1) << pc(semitones); }

int bitCount(uint16_t value)
{
    int count = 0;
    while (value != 0) { count += value & 1; value >>= 1; }
    return count;
}

uint16_t maskOf(const std::vector<int>& intervals)
{
    uint16_t result = 0;
    for (int iv : intervals) result |= bit(iv);
    return result;
}

std::vector<int> intervalsIn(uint16_t mask)
{
    std::vector<int> result;
    for (int iv = 0; iv < 12; ++iv)
        if ((mask & bit(iv)) != 0) result.push_back(iv);
    return result;
}

// Chord symbols describe interval *sets*. Repeated notes and octave placement
// remain in the source NoteEvent objects and in the note-role mapping.
const std::vector<ChordClassifier::QualityOption>& qualityCatalog()
{
    using Q = ChordClassifier::QualityOption;
    static const std::vector<Q> qualities {
        { "Maj", "Major", "Triad", { 0, 4, 7 } },
        { "m", "Minor", "Triad", { 0, 3, 7 } },
        { "dim", "Diminished", "Triad", { 0, 3, 6 } },
        { "aug", "Augmented", "Triad", { 0, 4, 8 } },
        { "sus4", "Suspended fourth", "Triad", { 0, 5, 7 } },
        { "sus2", "Suspended second", "Triad", { 0, 2, 7 } },
        { "sus4(9)", "Suspended fourth with added ninth", "Extended", { 0, 2, 5, 7 } },
        { "b5", "Major flat fifth", "Triad", { 0, 4, 6 } },
        { "add9", "Added ninth", "Extended", { 0, 2, 4, 7 } },
        { "m(add9)", "Minor added ninth", "Extended", { 0, 2, 3, 7 } },
        { "add11", "Added eleventh", "Extended", { 0, 4, 5, 7 } },
        { "add4", "Added fourth (alternate spelling)", "Extended", { 0, 4, 5, 7 } },
        { "m(add11)", "Minor added eleventh", "Extended", { 0, 3, 5, 7 } },
        { "6", "Major sixth", "Sixth", { 0, 4, 7, 9 } },
        { "m6", "Minor sixth", "Sixth", { 0, 3, 7, 9 } },
        { "6/9", "Major sixth ninth", "Sixth", { 0, 2, 4, 7, 9 } },
        { "m6/9", "Minor sixth ninth", "Sixth", { 0, 2, 3, 7, 9 } },
        { "7", "Dominant seventh", "Seventh", { 0, 4, 7, 10 } },
        { "maj7", "Major seventh", "Seventh", { 0, 4, 7, 11 } },
        { "m7", "Minor seventh", "Seventh", { 0, 3, 7, 10 } },
        { "m(maj7)", "Minor major seventh", "Seventh", { 0, 3, 7, 11 } },
        { "m7b5", "Half diminished seventh", "Seventh", { 0, 3, 6, 10 } },
        { "dim7", "Diminished seventh", "Seventh", { 0, 3, 6, 9 } },
        { "aug7", "Augmented dominant seventh", "Seventh", { 0, 4, 8, 10 } },
        { "7(#5)", "Dominant sharp fifth (alternate spelling)", "Altered", { 0, 4, 8, 10 } },
        { "maj7#5", "Augmented major seventh", "Seventh", { 0, 4, 8, 11 } },
        { "7sus4", "Dominant seventh suspended fourth", "Seventh", { 0, 5, 7, 10 } },
        { "7sus2", "Dominant seventh suspended second", "Seventh", { 0, 2, 7, 10 } },
        { "maj7sus4", "Major seventh suspended fourth", "Seventh", { 0, 5, 7, 11 } },
        { "maj7sus2", "Major seventh suspended second", "Seventh", { 0, 2, 7, 11 } },
        { "7(b5)", "Dominant flat fifth", "Altered", { 0, 4, 6, 10 } },
        { "7(b9)", "Dominant flat ninth", "Altered", { 0, 1, 4, 7, 10 } },
        { "7(#9)", "Dominant sharp ninth", "Altered", { 0, 3, 4, 7, 10 } },
        { "7(#11)", "Dominant sharp eleventh", "Altered", { 0, 4, 6, 7, 10 } },
        { "7(b13)", "Dominant flat thirteenth", "Altered", { 0, 4, 7, 8, 10 } },
        { "7alt", "Altered dominant", "Altered", { 0, 1, 4, 6, 10 } },
        { "maj7(#11)", "Major seventh sharp eleventh", "Altered", { 0, 4, 6, 7, 11 } },
        { "9", "Dominant ninth", "Extended", { 0, 2, 4, 7, 10 } },
        { "maj9", "Major ninth", "Extended", { 0, 2, 4, 7, 11 } },
        { "m9", "Minor ninth", "Extended", { 0, 2, 3, 7, 10 } },
        { "m(maj9)", "Minor major ninth", "Extended", { 0, 2, 3, 7, 11 } },
        { "9sus4", "Dominant ninth suspended fourth", "Extended", { 0, 2, 5, 7, 10 } },
        { "9(#5)", "Dominant ninth sharp fifth", "Altered", { 0, 2, 4, 8, 10 } },
        { "9(b5)", "Dominant ninth flat fifth", "Altered", { 0, 2, 4, 6, 10 } },
        { "11", "Dominant eleventh", "Extended", { 0, 2, 4, 5, 7, 10 },
            { { 0, 2, 5, 7, 10 } } },
        { "m11", "Minor eleventh", "Extended", { 0, 2, 3, 5, 7, 10 } },
        { "maj11", "Major eleventh", "Extended", { 0, 2, 4, 5, 7, 11 } },
        { "9(#11)", "Dominant ninth sharp eleventh", "Altered", { 0, 2, 4, 6, 7, 10 } },
        { "maj9(#11)", "Major ninth sharp eleventh", "Altered", { 0, 2, 4, 6, 7, 11 } },
        { "13", "Dominant thirteenth", "Extended", { 0, 2, 4, 5, 7, 9, 10 },
            { { 0, 2, 4, 7, 9, 10 } } },
        { "maj13", "Major thirteenth", "Extended", { 0, 2, 4, 5, 7, 9, 11 },
            { { 0, 2, 4, 7, 9, 11 } } },
        { "m13", "Minor thirteenth", "Extended", { 0, 2, 3, 5, 7, 9, 10 },
            { { 0, 2, 3, 7, 9, 10 } } },
        { "13sus4", "Dominant thirteenth suspended fourth", "Extended", { 0, 2, 5, 7, 9, 10 } },
        { "13(b9)", "Dominant thirteenth flat ninth", "Altered", { 0, 1, 4, 7, 9, 10 } },
        { "13(#9)", "Dominant thirteenth sharp ninth", "Altered", { 0, 3, 4, 7, 9, 10 } },
        { "13(#11)", "Dominant thirteenth sharp eleventh", "Altered", { 0, 2, 4, 6, 7, 9, 10 } }
    };
    return qualities;
}

struct Pattern
{
    juce::String quality;
    uint16_t mask = 0;
    int priority = 0;
};

const std::vector<Pattern>& patterns()
{
    static const std::vector<Pattern> all = [] {
        std::vector<Pattern> entries;
        for (const auto& q : qualityCatalog())
        {
            const int aliasPriority = q.id == "add4" || q.id == "7(#5)" ? -2 : 0;
            entries.push_back({ q.id, maskOf(q.requiredIntervals), aliasPriority });
            for (const auto& voicing : q.acceptedVoicings)
                entries.push_back({ q.id, maskOf(voicing),
                    q.id == "11" ? -2 : -1 });
        }
        return entries;
    }();
    return all;
}

juce::String plainPitchName(int pitchClass, bool flats)
{
    static constexpr const char* sharp[] {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static constexpr const char* flat[] {
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    return (flats ? flat : sharp)[pc(pitchClass)];
}

juce::String validatedTonicName(const juce::String& supplied, int tonicPc)
{
    static constexpr int naturals[] { 0, 2, 4, 5, 7, 9, 11 };
    auto name = supplied.trim()
        .replace(juce::String::fromUTF8(u8"♯"), "#")
        .replace(juce::String::fromUTF8(u8"♭"), "b");
    if (name.isNotEmpty())
        name = name.substring(0, 1).toUpperCase() + name.substring(1);
    const int letter = juce::String("CDEFGAB").indexOfChar(name.isEmpty() ? ' ' : name[0]);
    if (letter < 0 || name.length() > 7) return {};
    int namedPc = naturals[letter];
    for (int i = 1; i < name.length(); ++i)
    {
        if (name[i] == '#') ++namedPc;
        else if (name[i] == 'b') --namedPc;
        else return {};
    }
    return pc(namedPc) == pc(tonicPc) ? name : juce::String{};
}

juce::String contextPitchName(int pitchClass, int tonicPc,
                              const juce::String& scaleName,
                              const juce::String& explicitTonicName = {})
{
    static constexpr int naturals[] { 0, 2, 4, 5, 7, 9, 11 };
    const auto validated = validatedTonicName(explicitTonicName, tonicPc);
    const juce::String tonicName = validated.isNotEmpty() ? validated
        : ScaleDefinitions::getPreferredTonicName(tonicPc, scaleName);
    const juce::String letters = "CDEFGAB";
    const int tonicLetter = letters.indexOfChar(tonicName[0]);
    const auto spellDegree = [&](int degree) {
        const int letter = (tonicLetter + degree) % 7;
        int accidental = interval(pitchClass, naturals[letter]);
        if (accidental > 6) accidental -= 12;
        if (std::abs(accidental) > 6) return juce::String{};
        juce::String result = letters.substring(letter, letter + 1);
        if (accidental > 0) result += juce::String::repeatedString("#", accidental);
        if (accidental < 0) result += juce::String::repeatedString("b", -accidental);
        return result;
    };
    const auto* scale = ScaleDefinitions::findScale(scaleName);
    if (scale != nullptr && scale->intervals.size() == 7 && tonicLetter >= 0)
        for (size_t degree = 0; degree < 7; ++degree)
        {
            if (pc(tonicPc + scale->intervals[degree]) != pc(pitchClass))
                continue;
            const auto name = spellDegree(static_cast<int>(degree));
            if (name.isNotEmpty()) return name;
        }
    // Chromatic roots are spelled to agree with the degree label. In C major,
    // for example, relative 1 is Db/bII and relative 8 is Ab/bVI.
    if (tonicLetter >= 0)
    {
        static constexpr int chromaticDegreeIndex[] {
            0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };
        const auto name = spellDegree(chromaticDegreeIndex[interval(pitchClass, tonicPc)]);
        if (name.isNotEmpty()) return name;
    }
    return plainPitchName(pitchClass,
        tonicName.containsChar('b') || tonicName == "F");
}

ChordFamily familyFor(const juce::String& quality)
{
    if (quality == "dim" || quality == "dim7") return ChordFamily::Diminished;
    if (quality == "m7b5") return ChordFamily::HalfDiminished;
    if (quality == "aug" || quality == "aug7" || quality == "7(#5)"
        || quality == "maj7#5") return ChordFamily::Augmented;
    if (quality.contains("sus")) return ChordFamily::Suspended;
    if (quality.startsWith("m") && !quality.startsWith("maj")) return ChordFamily::Minor;
    if (quality.startsWith("7") || quality.startsWith("9")
        || quality.startsWith("11") || quality.startsWith("13"))
        return ChordFamily::Dominant;
    return ChordFamily::Major;
}

juce::String extraLabel(int iv, uint16_t patternMask)
{
    const bool hasSeventh = (patternMask & (bit(10) | bit(11))) != 0;
    const bool hasThird = (patternMask & (bit(3) | bit(4))) != 0;
    switch (iv)
    {
        case 1: return "b9";
        case 2: return "9";
        case 3: return (patternMask & bit(4)) != 0 ? "#9" : "";
        case 5: return hasThird ? "11" : "";
        case 6: return (patternMask & bit(7)) != 0 ? "#11" : "";
        case 8: return (patternMask & bit(7)) != 0 ? "b13" : "";
        case 9: return hasSeventh ? "13" : "6";
        default: return "";
    }
}

juce::String suffix(const juce::String& quality, const std::vector<int>& extras,
                    uint16_t patternMask, const std::vector<int>& omitted)
{
    juce::String result = quality == "Maj" ? "" : quality;
    if (!extras.empty())
    {
        result += "(";
        for (size_t i = 0; i < extras.size(); ++i)
        {
            if (i > 0) result += ",";
            result += extraLabel(extras[i], patternMask);
        }
        result += ")";
    }
    if (!omitted.empty())
    {
        result += "(no";
        for (size_t i = 0; i < omitted.size(); ++i)
        {
            if (i > 0) result += ",";
            result += omitted[i] == 0 ? "1" : omitted[i] == 5 ? "11" : "5";
        }
        result += ")";
    }
    return result;
}

struct RankedCandidate
{
    ChordCandidate candidate;
    uint16_t patternMask = 0;
    uint16_t actualMask = 0;
};

IdentifiedChord noClearChord()
{
    IdentifiedChord result;
    result.rootPitchClass = -1;
    result.rootName.clear();
    result.quality.clear();
    result.fullName = "None";
    result.romanNumeral = "--";
    result.confidence = 0.0;
    result.family = ChordFamily::Unknown;
    result.status = ChordRecognitionStatus::None;
    return result;
}

void describeStructure(IdentifiedChord& chord)
{
    const auto has = [&chord](int iv) { return chord.containsInterval(iv); };
    if (has(4)) chord.thirdInterval = 4;
    else if (has(3)) chord.thirdInterval = 3;
    else if (has(5)) chord.thirdInterval = 5;
    else if (has(2)) chord.thirdInterval = 2;
    if (has(7)) chord.fifthInterval = 7;
    else if (has(6)) chord.fifthInterval = 6;
    else if (has(8)) chord.fifthInterval = 8;
    if (has(11)) chord.seventhInterval = 11;
    else if (has(10)) chord.seventhInterval = 10;
    else if (chord.family == ChordFamily::Diminished && has(9))
        chord.seventhInterval = 9;
    for (int iv : { 1, 2, 3, 5, 6, 8, 9 })
        if (has(iv) && iv != chord.thirdInterval
            && iv != chord.fifthInterval && iv != chord.seventhInterval)
            chord.tensionIntervals.push_back(iv);
    const int bassIv = interval(chord.bassPitchClass, chord.rootPitchClass);
    if (bassIv == 0) chord.inversion = 0;
    else if (bassIv == chord.thirdInterval) chord.inversion = 1;
    else if (bassIv == chord.fifthInterval) chord.inversion = 2;
    else if (bassIv == chord.seventhInterval) chord.inversion = 3;
}

ChordToneRole roleFor(int iv, const IdentifiedChord& chord)
{
    if (iv == 0) return ChordToneRole::Root;
    if (iv == chord.thirdInterval) return iv == 2 || iv == 5
        ? ChordToneRole::Suspension : ChordToneRole::Third;
    if (iv == chord.fifthInterval) return ChordToneRole::Fifth;
    if (iv == chord.seventhInterval) return ChordToneRole::Seventh;
    if (iv == 9 && chord.seventhInterval < 0) return ChordToneRole::Sixth;
    if (iv == 1 || iv == 3 || iv == 6 || iv == 8)
        return ChordToneRole::Alteration;
    if (iv == 2 || iv == 5 || iv == 9) return ChordToneRole::Extension;
    return ChordToneRole::Other;
}

void attachNotes(IdentifiedChord& chord, const std::vector<int>& pitches,
                 const std::vector<int64_t>& ids)
{
    for (size_t i = 0; i < pitches.size(); ++i)
    {
        const int64_t id = i < ids.size() ? ids[i] : 0;
        if (chord.status == ChordRecognitionStatus::None
            || chord.status == ChordRecognitionStatus::SinglePitch
            || chord.status == ChordRecognitionStatus::Interval)
        {
            if (i < ids.size()) chord.noteIds.push_back(id);
            chord.noteRoles.push_back({ id, pitches[i], -1, ChordToneRole::Other, -1 });
            continue;
        }
        const int layer = chord.isPolychord
            ? (pitches[i] >= chord.polychordSplitPitch ? 1 : 0) : -1;
        const int roleRoot = layer == 1 ? chord.upperRootPitchClass : chord.rootPitchClass;
        const int iv = interval(pitches[i], roleRoot);
        if (i < ids.size()) chord.noteIds.push_back(id);
        ChordToneRole role = roleFor(iv, chord);
        if (layer >= 0)
            role = iv == 0 ? ChordToneRole::Root :
                (iv == 3 || iv == 4) ? ChordToneRole::Third :
                (iv == 6 || iv == 7 || iv == 8) ? ChordToneRole::Fifth :
                ChordToneRole::Other;
        chord.noteRoles.push_back({ id, pitches[i], iv, role, layer });
    }
}

void describeCandidate(ChordCandidate& candidate, const std::vector<int>& pitches,
                       int keyTonicPc, const juce::String& scaleName)
{
    if (pitches.empty()) return;
    IdentifiedChord reading;
    reading.rootPitchClass = candidate.rootPitchClass;
    reading.bassPitchClass = pc(*std::min_element(pitches.begin(), pitches.end()));
    reading.quality = candidate.quality;
    reading.family = candidate.quality == "poly" ? ChordFamily::Unknown
                                                  : familyFor(candidate.quality);
    reading.status = ChordRecognitionStatus::Recognized;
    reading.isPolychord = candidate.quality == "poly";
    reading.upperRootPitchClass = candidate.upperRootPitchClass;
    reading.polychordSplitPitch = candidate.polychordSplitPitch;
    for (int pitch : pitches)
        reading.intervalMask |= bit(interval(pitch, reading.rootPitchClass));
    if (!reading.isPolychord) describeStructure(reading);
    else reading.inversion = candidate.lowerInversion;
    attachNotes(reading, pitches, {});
    candidate.bassPitchClass = reading.bassPitchClass;
    candidate.family = reading.family;
    candidate.intervalMask = reading.intervalMask;
    candidate.thirdInterval = reading.thirdInterval;
    candidate.fifthInterval = reading.fifthInterval;
    candidate.seventhInterval = reading.seventhInterval;
    candidate.inversion = reading.inversion;
    candidate.tensionIntervals = std::move(reading.tensionIntervals);
    candidate.noteRoles = std::move(reading.noteRoles);
    candidate.romanNumeral = candidate.quality == "poly" ? "--" :
        ChordClassifier::computeRomanNumeral(candidate.rootPitchClass,
            candidate.bassPitchClass, candidate.quality, keyTonicPc, scaleName);
}

} // namespace

const std::vector<ChordClassifier::QualityOption>& ChordClassifier::getSupportedQualities()
{
    return qualityCatalog();
}

IdentifiedChord ChordClassifier::identifyChord(const std::vector<NoteEvent>& notes,
                                                int keyTonicPc,
                                                const juce::String& scaleName,
                                                const juce::String& tonicName)
{
    std::vector<int> pitches;
    std::vector<int64_t> ids;
    pitches.reserve(notes.size());
    ids.reserve(notes.size());
    for (const auto& note : notes)
    {
        pitches.push_back(note.pitch);
        ids.push_back(note.id);
    }
    auto result = identifyChordFromPitches(pitches, keyTonicPc, scaleName, tonicName);
    result.noteIds.clear();
    result.noteRoles.clear();
    attachNotes(result, pitches, ids);
    for (auto& candidate : result.alternatives)
        for (size_t i = 0; i < candidate.noteRoles.size() && i < ids.size(); ++i)
            candidate.noteRoles[i].noteId = ids[i];
    return result;
}

std::vector<IdentifiedChordGroup> ChordClassifier::identifyChordGroups(
    const std::vector<NoteEvent>& notes, double barLengthBeats,
    const std::function<ChordAnalysisContext(double)>& contextAtBeat)
{
    std::vector<NoteEvent> sounding;
    sounding.reserve(notes.size());
    for (const auto& note : notes)
        if (std::isfinite(note.startBeat)
            && std::isfinite(note.lengthBeats))
            sounding.push_back(note);

    std::vector<IdentifiedChordGroup> results;
    for (const auto& group : groupSelectedHarmonyNotes(sounding, barLengthBeats))
    {
        if (group.empty()) continue;
        IdentifiedChordGroup result;
        result.startBeat = group.front().startBeat;
        result.endBeat = result.startBeat;
        for (const auto& note : group)
            result.endBeat = std::max(result.endBeat,
                note.startBeat + std::max(0.0, note.lengthBeats));
        const auto context = contextAtBeat ? contextAtBeat(result.startBeat)
                                           : ChordAnalysisContext {};
        result.chord = identifyChord(group, context.tonicPc, context.scaleName,
                                     context.tonicName);
        results.push_back(std::move(result));
    }
    return results;
}

IdentifiedChord ChordClassifier::identifyChordFromPitches(
    const std::vector<int>& pitches, int keyTonicPc,
    const juce::String& scaleName, const juce::String& tonicName)
{
    if (pitches.empty()) return noClearChord();
    const int bass = pc(*std::min_element(pitches.begin(), pitches.end()));
    std::set<int> pitchClasses;
    for (int pitch : pitches) pitchClasses.insert(pc(pitch));

    if (pitchClasses.size() > 8)
    {
        auto result = noClearChord();
        result.bassPitchClass = bass;
        attachNotes(result, pitches, {});
        return result;
    }

    if (pitchClasses.size() <= 2)
    {
        IdentifiedChord chord;
        chord.rootPitchClass = bass;
        chord.bassPitchClass = bass;
        chord.rootName = contextPitchName(bass, keyTonicPc, scaleName, tonicName);
        if (pitchClasses.size() == 1)
        {
            chord.status = ChordRecognitionStatus::SinglePitch;
            chord.family = ChordFamily::Single;
            chord.quality.clear();
            chord.fullName = chord.rootName;
            chord.confidence = 0.3;
        }
        else
        {
            const int other = *std::find_if(pitchClasses.begin(), pitchClasses.end(),
                [bass](int notePc) { return notePc != bass; });
            const int bassToOther = interval(other, bass);
            if (bassToOther == 7 || bassToOther == 5)
            {
                chord.rootPitchClass = bassToOther == 7 ? bass : other;
                chord.rootName = contextPitchName(chord.rootPitchClass,
                                                  keyTonicPc, scaleName, tonicName);
                chord.quality = "5";
                chord.family = ChordFamily::Power;
                chord.status = ChordRecognitionStatus::Recognized;
                chord.isSlashChord = chord.rootPitchClass != bass;
                chord.fullName = chord.rootName + "5";
                if (chord.isSlashChord)
                    chord.fullName += "/" + contextPitchName(bass, keyTonicPc, scaleName,
                                                               tonicName);
                chord.confidence = 0.8;
            }
            else
            {
                chord.status = ChordRecognitionStatus::Interval;
                chord.family = ChordFamily::Dyad;
                chord.quality.clear();
                chord.fullName = chord.rootName + "-"
                    + contextPitchName(other, keyTonicPc, scaleName, tonicName)
                    + " (interval)";
                chord.confidence = 0.3;
            }
        }
        chord.romanNumeral = chord.status == ChordRecognitionStatus::Recognized
            ? computeRomanNumeral(chord.rootPitchClass, bass, chord.quality,
                                  keyTonicPc, scaleName) : "--";
        for (int notePc : pitchClasses)
            chord.intervalMask |= bit(interval(notePc, chord.rootPitchClass));
        if (chord.status == ChordRecognitionStatus::Recognized)
            describeStructure(chord);
        attachNotes(chord, pitches, {});
        return chord;
    }

    // Four adjacent chromatic pitch classes describe a cluster more reliably
    // than a pile of simultaneous b9, natural 9 and #9 chord alterations.
    for (int start = 0; start < 12; ++start)
    {
        bool chromaticRun = true;
        for (int offset = 0; offset < 4; ++offset)
            chromaticRun &= pitchClasses.count(pc(start + offset)) != 0;
        if (chromaticRun)
        {
            auto result = noClearChord();
            result.bassPitchClass = bass;
            attachNotes(result, pitches, {});
            return result;
        }
    }

    std::vector<RankedCandidate> ranked;
    const auto* scale = ScaleDefinitions::findScale(scaleName);
    auto inScale = [&](int pitchClass) {
        if (scale == nullptr) return false;
        for (int iv : scale->intervals)
            if (pc(keyTonicPc + iv) == pitchClass) return true;
        return false;
    };

    for (int root = 0; root < 12; ++root)
    {
        uint16_t actual = 0;
        for (int notePc : pitchClasses) actual |= bit(interval(notePc, root));
        for (const auto& pattern : patterns())
        {
            const uint16_t missing = pattern.mask & ~actual;
            const uint16_t extra = actual & ~pattern.mask;
            const int missingCount = bitCount(missing);
            const int extraCount = bitCount(extra);
            const bool rooted = pitchClasses.count(root) != 0;
            double tier = 0.0;
            std::vector<int> omitted, added;
            if (missingCount == 0 && extraCount == 0 && rooted)
                tier = 1000.0;
            else if (rooted && extraCount == 0 && missingCount == 1
                     && bitCount(pattern.mask) >= 4
                     && missing == bit(7))
            {
                tier = 800.0;
                omitted = intervalsIn(missing);
            }
            else if (rooted && missingCount == 0 && extraCount <= 3
                     && bitCount(pattern.mask) >= 3)
            {
                added = intervalsIn(extra);
                bool meaningful = true;
                for (int iv : added)
                    if (extraLabel(iv, pattern.mask).isEmpty())
                        meaningful = false;
                if (meaningful) tier = 600.0 - 12.0 * extraCount;
            }
            else if (rooted && missingCount == 1 && extraCount <= 2
                     && bitCount(pattern.mask) >= 4
                     && missing == bit(7))
            {
                added = intervalsIn(extra);
                bool meaningful = true;
                for (int iv : added)
                    if (extraLabel(iv, pattern.mask).isEmpty())
                        meaningful = false;
                if (meaningful)
                {
                    tier = 510.0 - 12.0 * extraCount;
                    omitted = intervalsIn(missing);
                }
            }
            else if (!rooted && (pattern.mask & bit(0)) != 0
                     && bitCount(pattern.mask) >= 4 && extraCount == 0
                     && (missing == bit(0)
                         || (missingCount == 2
                             && (missing & bit(0)) != 0
                             && (missing & bit(7)) != 0)))
            {
                tier = 420.0;
                omitted = intervalsIn(missing);
            }
            if (tier == 0.0) continue;

            // A triad plus a seventh must be named with the seventh formula
            // where available, rather than as an arbitrary added colour.
            const int bassIv = interval(bass, root);
            const double bassBonus = bassIv == 0 ? 18.0 :
                (pattern.mask & bit(bassIv)) != 0 ? 2.0 : -8.0;
            const double score = tier + 4.0 * bitCount(pattern.mask)
                + bassBonus + (pc(root) == pc(keyTonicPc) ? 6.0
                    : inScale(root) ? 2.0 : 0.0)
                + pattern.priority - 5.0 * omitted.size();
            ChordCandidate candidate;
            candidate.rootPitchClass = root;
            candidate.quality = pattern.quality;
            candidate.score = score;
            candidate.omittedIntervals = omitted;
            candidate.addedIntervals = added;
            candidate.fullName = contextPitchName(root, keyTonicPc, scaleName,
                                                   tonicName)
                + suffix(pattern.quality, added, pattern.mask, omitted);
            if (bass != root)
                candidate.fullName += "/" + contextPitchName(bass, keyTonicPc,
                                                                scaleName, tonicName);
            ranked.push_back({ std::move(candidate), pattern.mask, actual });
        }
    }

    // Recognise clearly separated upper and lower triads as a polychord.
    // The register test prevents an ordinary close-position extension from
    // being split merely because its pitch classes admit two triads.
    std::vector<int> sortedPitches = pitches;
    std::sort(sortedPitches.begin(), sortedPitches.end());
    if (sortedPitches.size() >= 6)
        for (size_t split = 3; split + 3 <= sortedPitches.size(); ++split)
        {
            if (sortedPitches[split] - sortedPitches[split - 1] < 5)
                continue;
            auto triadRoot = [&](size_t first, size_t last)
                -> std::pair<int, juce::String> {
                std::set<int> pcs;
                for (size_t i = first; i < last; ++i) pcs.insert(pc(sortedPitches[i]));
                if (pcs.size() != 3) return { -1, {} };
                for (int candidateRoot : pcs)
                {
                    uint16_t actual = 0;
                    for (int notePc : pcs) actual |= bit(interval(notePc, candidateRoot));
                    for (const auto& quality : qualityCatalog())
                        if (quality.category == "Triad"
                            && bitCount(maskOf(quality.requiredIntervals)) == 3
                            && actual == maskOf(quality.requiredIntervals))
                            return { candidateRoot, quality.id };
                }
                return { -1, {} };
            };
            const auto lower = triadRoot(0, split);
            const auto upper = triadRoot(split, sortedPitches.size());
            if (lower.first < 0 || upper.first < 0
                || lower.first == upper.first) continue;
            const auto layerName = [&](const std::pair<int, juce::String>& layer,
                                       int bassPitch) {
                juce::String name = contextPitchName(layer.first, keyTonicPc,
                                                      scaleName, tonicName)
                    + (layer.second == "Maj" ? juce::String{} : layer.second);
                if (pc(bassPitch) != layer.first)
                    name += "/" + contextPitchName(bassPitch, keyTonicPc,
                                                     scaleName, tonicName);
                return name;
            };
            const auto triadInversion = [](int rootPc, int bassPitch) {
                const int bassIv = interval(bassPitch, rootPc);
                if (bassIv == 0) return 0;
                if (bassIv == 3 || bassIv == 4) return 1;
                if (bassIv == 6 || bassIv == 7 || bassIv == 8) return 2;
                return -1;
            };
            ChordCandidate poly;
            poly.rootPitchClass = lower.first;
            poly.upperRootPitchClass = upper.first;
            poly.polychordSplitPitch = sortedPitches[split];
            poly.lowerQuality = lower.second;
            poly.upperQuality = upper.second;
            poly.lowerInversion = triadInversion(lower.first, sortedPitches.front());
            poly.upperInversion = triadInversion(upper.first, sortedPitches[split]);
            poly.quality = "poly";
            poly.fullName = layerName(upper, sortedPitches[split])
                + "|" + layerName(lower, sortedPitches.front());
            std::set<int> lowerPitchClasses;
            for (size_t i = 0; i < split; ++i)
                lowerPitchClasses.insert(pc(sortedPitches[i]));
            const bool exactUnifiedChordRootedBelow = std::any_of(
                ranked.begin(), ranked.end(), [&](const RankedCandidate& c) {
                    return c.patternMask == c.actualMask
                        && lowerPitchClasses.count(c.candidate.rootPitchClass) != 0
                        && c.candidate.score >= 1000.0;
                });
            // A complete, lower-rooted extension takes precedence; otherwise
            // the clear register split may be the more useful first reading.
            poly.score = exactUnifiedChordRootedBelow ? 990.0 : 1040.0;
            ranked.push_back({ poly, 0, 0 });
            break;
        }

    if (ranked.empty())
    {
        auto result = noClearChord();
        result.bassPitchClass = bass;
        attachNotes(result, pitches, {});
        return result;
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.candidate.score != b.candidate.score)
            return a.candidate.score > b.candidate.score;
        if (a.candidate.rootPitchClass != b.candidate.rootPitchClass)
            return a.candidate.rootPitchClass < b.candidate.rootPitchClass;
        return a.candidate.quality < b.candidate.quality;
    });

    // Different formula spellings for the same actual notes are retained,
    // while equivalent variant patterns do not flood the candidate list.
    std::vector<RankedCandidate> unique;
    for (const auto& candidate : ranked)
    {
        const auto duplicate = std::find_if(unique.begin(), unique.end(),
            [&](const auto& previous) {
                return previous.candidate.rootPitchClass == candidate.candidate.rootPitchClass
                    && previous.candidate.quality == candidate.candidate.quality
                    && previous.candidate.omittedIntervals == candidate.candidate.omittedIntervals
                    && previous.candidate.addedIntervals == candidate.candidate.addedIntervals;
            });
        if (duplicate == unique.end()) unique.push_back(candidate);
    }

    const auto& best = unique.front().candidate;
    IdentifiedChord result;
    result.rootPitchClass = best.rootPitchClass;
    result.bassPitchClass = bass;
    result.rootName = contextPitchName(best.rootPitchClass, keyTonicPc, scaleName,
                                       tonicName);
    result.quality = best.quality;
    result.fullName = best.fullName;
    result.isSlashChord = bass != best.rootPitchClass;
    result.omittedIntervals = best.omittedIntervals;
    result.addedIntervals = best.addedIntervals;
    result.family = best.quality == "poly" ? ChordFamily::Unknown : familyFor(best.quality);
    result.isPolychord = best.quality == "poly";
    result.upperRootPitchClass = best.upperRootPitchClass;
    result.polychordSplitPitch = best.polychordSplitPitch;
    result.lowerQuality = best.lowerQuality;
    result.upperQuality = best.upperQuality;
    result.lowerInversion = best.lowerInversion;
    result.upperInversion = best.upperInversion;
    if (result.isPolychord) result.inversion = best.lowerInversion;
    result.status = ChordRecognitionStatus::Recognized;
    const double tier = best.score;
    result.confidence = tier >= 1000.0 ? 0.98 :
        tier >= 800.0 ? 0.84 : tier >= 700.0 ? 0.82 :
        tier >= 550.0 ? 0.67 : 0.55;
    if (unique.size() > 1)
    {
        const auto& second = unique[1].candidate;
        if (best.score - second.score < 12.0
            && (best.rootPitchClass != second.rootPitchClass
                || best.quality != second.quality))
        {
            result.status = ChordRecognitionStatus::Ambiguous;
            result.confidence = std::min(result.confidence, 0.85);
        }
    }
    if (result.isPolychord && std::any_of(unique.begin() + 1, unique.end(),
        [](const RankedCandidate& c) {
            return c.candidate.quality != "poly"
                && c.patternMask == c.actualMask;
        }))
    {
        result.status = ChordRecognitionStatus::Ambiguous;
        result.confidence = std::min(result.confidence, 0.85);
    }
    for (size_t i = 1; i < unique.size() && result.alternatives.size() < 12; ++i)
    {
        auto candidate = unique[i].candidate;
        describeCandidate(candidate, pitches, keyTonicPc, scaleName);
        result.alternatives.push_back(std::move(candidate));
    }
    for (int notePc : pitchClasses)
        result.intervalMask |= bit(interval(notePc, result.rootPitchClass));
    if (!result.isPolychord) describeStructure(result);
    result.romanNumeral = best.quality == "poly" ? "--" :
        computeRomanNumeral(result.rootPitchClass, bass, best.quality,
                            keyTonicPc, scaleName);
    attachNotes(result, pitches, {});
    return result;
}

juce::String ChordClassifier::computeRomanNumeral(
    int rootPc, int bassPc, const juce::String& quality, int keyTonicPc,
    const juce::String& scaleName)
{
    if (quality == "poly" || quality.isEmpty()) return "--";
    static constexpr const char* majorDegrees[] {
        "I", "II", "III", "IV", "V", "VI", "VII" };
    static constexpr int majorIntervals[] { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr const char* chromaticDegrees[] {
        "I", "bII", "II", "bIII", "III", "IV",
        "#IV", "V", "bVI", "VI", "bVII", "VII" };
    const int relative = interval(rootPc, keyTonicPc);
    juce::String numeral = chromaticDegrees[relative];
    const auto* scale = ScaleDefinitions::findScale(scaleName);
    if (scale != nullptr && scale->intervals.size() == 7)
        for (size_t degree = 0; degree < 7; ++degree)
        {
            if (pc(scale->intervals[degree]) != relative) continue;
            int accidental = relative - majorIntervals[degree];
            if (accidental > 6) accidental -= 12;
            if (accidental < -6) accidental += 12;
            if (std::abs(accidental) <= 2)
            {
                numeral.clear();
                if (accidental < 0)
                    numeral += juce::String::repeatedString("b", -accidental);
                if (accidental > 0)
                    numeral += juce::String::repeatedString("#", accidental);
                numeral += majorDegrees[degree];
            }
            break;
        }

    const bool minor = quality.startsWith("m") && !quality.startsWith("maj");
    const bool diminished = quality == "dim" || quality == "dim7"
        || quality == "m7b5";
    if (minor || diminished) numeral = numeral.toLowerCase();
    if (quality == "dim") numeral += juce::String::fromUTF8(u8"°");
    else if (quality == "dim7") numeral += juce::String::fromUTF8(u8"°7");
    else if (quality == "m7b5") numeral += juce::String::fromUTF8(u8"ø7");
    else if (quality == "aug") numeral += "+";
    else if (quality == "aug7") numeral += "+7";
    else if (quality == "m(maj7)") numeral += "(maj7)";
    else if (quality == "m(maj9)") numeral += "(maj9)";
    else if (quality == "Maj" || quality == "m") {}
    else if (minor) numeral += quality.substring(1);
    else numeral += quality;

    if (pc(bassPc) != pc(rootPc))
    {
        const int bassRelative = interval(bassPc, keyTonicPc);
        static constexpr const char* degreeNumbers[] {
            "1", "b2", "2", "b3", "3", "4", "#4", "5", "b6", "6", "b7", "7" };
        numeral += "/" + juce::String(degreeNumbers[bassRelative]);
    }
    return numeral;
}

juce::String ChordClassifier::spellPitchClass(
    int pitchClass, int keyTonicPc, const juce::String& scaleName,
    const juce::String& tonicName)
{
    return contextPitchName(pitchClass, keyTonicPc, scaleName, tonicName);
}

juce::String ChordClassifier::buildChordName(
    const juce::String& root, const juce::String& quality,
    const std::vector<juce::String>& activeTensions,
    const juce::String& slashBass)
{
    juce::String name = root;
    if (quality != "Maj") name += quality;
    if (!activeTensions.empty())
    {
        name += "(";
        for (size_t i = 0; i < activeTensions.size(); ++i)
        {
            if (i > 0) name += ",";
            name += activeTensions[i];
        }
        name += ")";
    }
    if (slashBass.isNotEmpty() && slashBass != root && slashBass != "--")
        name += "/" + slashBass;
    return name;
}

} // namespace Harmonizer
