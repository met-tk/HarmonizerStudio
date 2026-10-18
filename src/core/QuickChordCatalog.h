#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace Harmonizer
{

/** A root-position voicing for the Quick Chord sidebar module.
    IDs are persistent UI identifiers; add new chords without renumbering old ones.
    Intervals are MIDI semitones above the root, not pitch classes: 9/11/13
    retain their upper-octave positions (14/17/21).
 */
struct QuickChordType
{
    int id;
    juce::String symbol;
    juce::String chineseName;
    juce::String englishName;
    juce::String category;
    std::vector<int> semitoneIntervals;
    bool simple;
};

class QuickChordCatalog
{
public:
    QuickChordCatalog() = delete;

    static const std::vector<QuickChordType>& all()
    {
        static const std::vector<QuickChordType> types {
            // Triads, suspensions, and the two-note power chord.
            { 1, "maj",       u8"大三和弦",       "Major",                    "Triad", { 0, 4, 7 }, true },
            { 2, "m",         u8"小三和弦",       "Minor",                    "Triad", { 0, 3, 7 }, true },
            { 3, "dim",       u8"减三和弦",       "Diminished",               "Triad", { 0, 3, 6 }, true },
            { 4, "aug",       u8"增三和弦",       "Augmented",                "Triad", { 0, 4, 8 }, true },
            { 5, "sus2",      u8"挂二和弦",       "Suspended second",         "Triad", { 0, 2, 7 }, true },
            { 6, "sus4",      u8"挂四和弦",       "Suspended fourth",         "Triad", { 0, 5, 7 }, true },
            { 7, "5",         u8"强力和弦",       "Power chord",              "Triad", { 0, 7 }, true },
            { 8, "b5",        u8"降五大三和弦",   "Major flat fifth",         "Triad", { 0, 4, 6 }, false },

            // Added tones do not imply a seventh. add2 and add9 deliberately
            // share pitch classes but differ in the register of the added tone.
            { 20, "add9",    u8"加九和弦",       "Added ninth",              "Added", { 0, 4, 7, 14 }, true },
            { 21, "m(add9)", u8"小调加九和弦",   "Minor added ninth",        "Added", { 0, 3, 7, 14 }, false },
            { 22, "add2",    u8"加二和弦",       "Added second",             "Added", { 0, 2, 4, 7 }, false },
            { 23, "m(add2)", u8"小调加二和弦",   "Minor added second",       "Added", { 0, 2, 3, 7 }, false },
            { 24, "add11",   u8"加十一和弦",     "Added eleventh",           "Added", { 0, 4, 7, 17 }, false },
            { 25, "m(add11)",u8"小调加十一和弦", "Minor added eleventh",     "Added", { 0, 3, 7, 17 }, false },
            { 26, "add#11",  u8"加升十一和弦",   "Added sharp eleventh",     "Added", { 0, 4, 7, 18 }, false },
            { 27, "sus4(add9)", u8"挂四加九和弦", "Suspended fourth added ninth", "Added", { 0, 5, 7, 14 }, false },

            // Sixth chords.
            { 40, "6",       u8"大六和弦",       "Major sixth",              "Sixth", { 0, 4, 7, 9 }, true },
            { 41, "m6",      u8"小六和弦",       "Minor sixth",              "Sixth", { 0, 3, 7, 9 }, false },
            { 42, "6/9",     u8"六九和弦",       "Major six-nine",           "Sixth", { 0, 4, 7, 9, 14 }, false },
            { 43, "m6/9",    u8"小调六九和弦",   "Minor six-nine",           "Sixth", { 0, 3, 7, 9, 14 }, false },

            // Seventh chords, including altered fifths and suspended thirds.
            { 60, "7",        u8"属七和弦",         "Dominant seventh",          "Seventh", { 0, 4, 7, 10 }, true },
            { 61, "maj7",     u8"大七和弦",         "Major seventh",             "Seventh", { 0, 4, 7, 11 }, true },
            { 62, "m7",       u8"小七和弦",         "Minor seventh",             "Seventh", { 0, 3, 7, 10 }, true },
            { 63, "m(maj7)",  u8"小大七和弦",       "Minor-major seventh",       "Seventh", { 0, 3, 7, 11 }, false },
            { 64, "m7b5",     u8"半减七和弦",       "Half-diminished seventh",   "Seventh", { 0, 3, 6, 10 }, true },
            { 65, "dim7",     u8"减七和弦",         "Diminished seventh",        "Seventh", { 0, 3, 6, 9 }, false },
            { 66, "aug7",     u8"增属七和弦",       "Augmented dominant seventh","Seventh", { 0, 4, 8, 10 }, false },
            { 67, "maj7#5",   u8"增大七和弦",       "Augmented major seventh",   "Seventh", { 0, 4, 8, 11 }, false },
            { 68, "7sus4",    u8"属七挂四和弦",     "Dominant seventh sus4",     "Seventh", { 0, 5, 7, 10 }, true },
            { 69, "7sus2",    u8"属七挂二和弦",     "Dominant seventh sus2",     "Seventh", { 0, 2, 7, 10 }, false },
            { 70, "maj7sus4", u8"大七挂四和弦",     "Major seventh sus4",        "Seventh", { 0, 5, 7, 11 }, false },
            { 71, "maj7sus2", u8"大七挂二和弦",     "Major seventh sus2",        "Seventh", { 0, 2, 7, 11 }, false },

            // Extensions are voiced above the seventh. Thirteenths normally
            // omit the eleventh to avoid a semitone clash with the major third.
            { 80, "9",       u8"属九和弦",         "Dominant ninth",            "Extended", { 0, 4, 7, 10, 14 }, true },
            { 81, "maj9",    u8"大九和弦",         "Major ninth",               "Extended", { 0, 4, 7, 11, 14 }, true },
            { 82, "m9",      u8"小九和弦",         "Minor ninth",               "Extended", { 0, 3, 7, 10, 14 }, true },
            { 83, "m(maj9)", u8"小大九和弦",       "Minor-major ninth",         "Extended", { 0, 3, 7, 11, 14 }, false },
            { 84, "9sus4",   u8"属九挂四和弦",     "Dominant ninth sus4",       "Extended", { 0, 5, 7, 10, 14 }, false },
            { 85, "m7b5(9)", u8"半减九和弦",       "Half-diminished ninth",     "Extended", { 0, 3, 6, 10, 14 }, false },
            { 86, "11",      u8"属十一和弦",       "Dominant eleventh",         "Extended", { 0, 4, 7, 10, 14, 17 }, false },
            { 87, "maj11",   u8"大十一和弦",       "Major eleventh",            "Extended", { 0, 4, 7, 11, 14, 17 }, false },
            { 88, "m11",     u8"小十一和弦",       "Minor eleventh",            "Extended", { 0, 3, 7, 10, 14, 17 }, false },
            { 89, "13",      u8"属十三和弦",       "Dominant thirteenth",       "Extended", { 0, 4, 7, 10, 14, 21 }, false },
            { 90, "maj13",   u8"大十三和弦",       "Major thirteenth",          "Extended", { 0, 4, 7, 11, 14, 21 }, false },
            { 91, "m13",     u8"小十三和弦",       "Minor thirteenth",          "Extended", { 0, 3, 7, 10, 14, 21 }, false },
            { 92, "13sus4",  u8"属十三挂四和弦",   "Dominant thirteenth sus4",  "Extended", { 0, 5, 7, 10, 14, 21 }, false },

            // Dominant alterations and Lydian-flavoured major extensions.
            { 110, "7b5",       u8"属七降五和弦",       "Dominant seventh flat fifth",       "Altered", { 0, 4, 6, 10 }, false },
            { 111, "7#5",       u8"属七升五和弦",       "Dominant seventh sharp fifth",      "Altered", { 0, 4, 8, 10 }, false },
            { 112, "7b9",       u8"属七降九和弦",       "Dominant seventh flat ninth",       "Altered", { 0, 4, 7, 10, 13 }, false },
            { 113, "7#9",       u8"属七升九和弦",       "Dominant seventh sharp ninth",      "Altered", { 0, 4, 7, 10, 15 }, false },
            { 114, "7#11",      u8"属七升十一和弦",     "Dominant seventh sharp eleventh",  "Altered", { 0, 4, 7, 10, 18 }, false },
            { 115, "7b13",      u8"属七降十三和弦",     "Dominant seventh flat thirteenth", "Altered", { 0, 4, 7, 10, 20 }, false },
            { 116, "7alt",      u8"变化属七和弦",       "Altered dominant",                   "Altered", { 0, 4, 10, 13, 18 }, false },
            { 117, "maj7#11",   u8"大七升十一和弦",     "Major seventh sharp eleventh",     "Altered", { 0, 4, 7, 11, 18 }, false },
            { 118, "9b5",       u8"属九降五和弦",       "Dominant ninth flat fifth",         "Altered", { 0, 4, 6, 10, 14 }, false },
            { 119, "9#5",       u8"属九升五和弦",       "Dominant ninth sharp fifth",        "Altered", { 0, 4, 8, 10, 14 }, false },
            { 120, "9#11",      u8"属九升十一和弦",     "Dominant ninth sharp eleventh",    "Altered", { 0, 4, 7, 10, 14, 18 }, false },
            { 121, "maj9#11",   u8"大九升十一和弦",     "Major ninth sharp eleventh",       "Altered", { 0, 4, 7, 11, 14, 18 }, false },
            { 122, "13b9",      u8"属十三降九和弦",     "Dominant thirteenth flat ninth",   "Altered", { 0, 4, 7, 10, 13, 21 }, false },
            { 123, "13#9",      u8"属十三升九和弦",     "Dominant thirteenth sharp ninth",  "Altered", { 0, 4, 7, 10, 15, 21 }, false },
            { 124, "13#11",     u8"属十三升十一和弦",   "Dominant thirteenth sharp eleventh","Altered", { 0, 4, 7, 10, 14, 18, 21 }, false },
            { 125, "maj13#11",  u8"大十三升十一和弦",   "Major thirteenth sharp eleventh",   "Altered", { 0, 4, 7, 11, 14, 18, 21 }, false },
            { 126, "7b9b13",    u8"属七降九降十三和弦", "Dominant seventh b9 b13",           "Altered", { 0, 4, 7, 10, 13, 20 }, false },
            { 127, "7#9b13",    u8"属七升九降十三和弦", "Dominant seventh #9 b13",           "Altered", { 0, 4, 7, 10, 15, 20 }, false },
            { 128, "7b9#11",    u8"属七降九升十一和弦", "Dominant seventh b9 #11",           "Altered", { 0, 4, 7, 10, 13, 18 }, false },
            { 129, "7#9#11",    u8"属七升九升十一和弦", "Dominant seventh #9 #11",           "Altered", { 0, 4, 7, 10, 15, 18 }, false },
            { 130, "7#5b9",     u8"属七升五降九和弦", "Dominant seventh #5 b9",            "Altered", { 0, 4, 8, 10, 13 }, false },
            { 131, "7#5#9",     u8"属七升五升九和弦", "Dominant seventh #5 #9",            "Altered", { 0, 4, 8, 10, 15 }, false },
            { 132, "7b5b9",     u8"属七降五降九和弦", "Dominant seventh b5 b9",            "Altered", { 0, 4, 6, 10, 13 }, false },
            { 133, "7b5#9",     u8"属七降五升九和弦", "Dominant seventh b5 #9",            "Altered", { 0, 4, 6, 10, 15 }, false }
        };
        return types;
    }

    static const QuickChordType* find(int id)
    {
        for (const auto& type : all())
            if (type.id == id)
                return &type;
        return nullptr;
    }

    static std::vector<const QuickChordType*> visible(bool complexMode)
    {
        std::vector<const QuickChordType*> result;
        result.reserve(all().size());
        for (const auto& type : all())
            if (complexMode || type.simple)
                result.push_back(&type);
        return result;
    }
};

} // namespace Harmonizer
