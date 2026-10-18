#include "ScaleDefinitions.h"
#include <cmath>
#include <limits>

namespace Harmonizer
{

static const char* PITCH_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const int STANDARD_WHITE_PCS[7] = { 0, 2, 4, 5, 7, 9, 11 };

namespace
{
int wrapPitchClass(int value) { return (value % 12 + 12) % 12; }

int tonicNamePitchClass(const juce::String& input)
{
    const auto name = input.trim();
    if (name.isEmpty()) return -1;
    const juce::String letters = "CDEFGAB";
    const int letter = letters.indexOfChar(juce::CharacterFunctions::toUpperCase(name[0]));
    if (letter < 0) return -1;
    int pitch = STANDARD_WHITE_PCS[letter];
    for (int i = 1; i < name.length(); ++i)
    {
        const auto accidental = name[i];
        if (accidental == '#' || accidental == 0x266F) ++pitch;
        else if (accidental == 'b' || accidental == 0x266D) --pitch;
        else return -1;
    }
    return wrapPitchClass(pitch);
}

int spellingCost(const juce::String& tonicName, int tonicPc,
                 const std::vector<int>& scaleIntervals)
{
    const juce::String letters = "CDEFGAB";
    const int tonicLetter = letters.indexOfChar(tonicName[0]);
    if (tonicLetter < 0 || scaleIntervals.size() != 7)
        return std::numeric_limits<int>::max();

    int cost = 0;
    for (size_t degree = 0; degree < scaleIntervals.size(); ++degree)
    {
        const int natural = STANDARD_WHITE_PCS[(tonicLetter + static_cast<int>(degree)) % 7];
        int accidental = wrapPitchClass(tonicPc + scaleIntervals[degree] - natural);
        if (accidental > 6) accidental -= 12;
        const int magnitude = std::abs(accidental);
        // A double accidental is much harder to read than two single ones.
        cost += magnitude + (magnitude > 1 ? 4 * (magnitude - 1) : 0);
    }
    return cost;
}
} // namespace

juce::String ScaleDefinitions::getPitchName(int pc)
{
    int safePc = ((pc % 12) + 12) % 12;
    return PITCH_NAMES[safePc];
}

bool ScaleDefinitions::isPhysicalWhite(int pc)
{
    int safePc = ((pc % 12) + 12) % 12;
    for (int w : STANDARD_WHITE_PCS)
        if (w == safePc) return true;
    return false;
}

bool ScaleDefinitions::isPhysicalBlack(int pc)
{
    return !isPhysicalWhite(pc);
}

static std::vector<ScaleConfig> s_allScales;
static bool s_scalesInitialized = false;

static void ensureDefaultScales()
{
    if (s_scalesInitialized) return;
    s_scalesInitialized = true;
    s_allScales = {
        {
            "Ionian (Natural Major)",
            "Major Family",
            { 0, 2, 4, 5, 7, 9, 11 },
            { "I (Tonic)", "II (Supertonic)", "III (Mediant)", "IV (Subdominant)", "V (Dominant)", "VI (Submediant)", "VII (Leading Tone)" }
        },
        {
            "Aeolian (Natural Minor)",
            "Minor Family",
            { 0, 2, 3, 5, 7, 8, 10 },
            { "I (Tonic)", "II (Supertonic)", "bIII (Mediant)", "IV (Subdominant)", "V (Dominant)", "bVI (Submediant)", "bVII (Subtonic)" }
        },
        {
            "Harmonic Minor",
            "Minor Family",
            { 0, 2, 3, 5, 7, 8, 11 },
            { "I", "II", "bIII", "IV", "V", "bVI", "VII (Leading Tone)" }
        },
        {
            "Melodic Minor (Ascending)",
            "Minor Family",
            { 0, 2, 3, 5, 7, 9, 11 },
            { "I", "II", "bIII", "IV", "V", "VI", "VII" }
        },
        {
            "Dorian",
            "Church Modes",
            { 0, 2, 3, 5, 7, 9, 10 },
            { "I", "II", "bIII", "IV", "V", "VI (Char. Maj6)", "bVII" }
        },
        {
            "Phrygian",
            "Church Modes",
            { 0, 1, 3, 5, 7, 8, 10 },
            { "I", "bII (Char. Min2)", "bIII", "IV", "V", "bVI", "bVII" }
        },
        {
            "Lydian",
            "Church Modes",
            { 0, 2, 4, 6, 7, 9, 11 },
            { "I", "II", "III", "#IV (Char. Aug4)", "V", "VI", "VII" }
        },
        {
            "Mixolydian",
            "Church Modes",
            { 0, 2, 4, 5, 7, 9, 10 },
            { "I", "II", "III", "IV", "V", "VI", "bVII (Char. Min7)" }
        },
        {
            "Locrian",
            "Church Modes",
            { 0, 1, 3, 5, 6, 8, 10 },
            { "I", "bII", "bIII", "IV", "bV (Dim5)", "bVI", "bVII" }
        },
        {
            "Major Pentatonic",
            "Pentatonic & Blues",
            { 0, 2, 4, 7, 9 },
            { "Gong (1)", "Shang (2)", "Jiao (3)", "Zhi (5)", "Yu (6)" }
        },
        {
            "Minor Pentatonic",
            "Pentatonic & Blues",
            { 0, 3, 5, 7, 10 },
            { "Yu (6)", "Gong (1)", "Shang (2)", "Jiao (3)", "Zhi (5)" }
        },
        {
            "Blues Scale",
            "Pentatonic & Blues",
            { 0, 3, 5, 6, 7, 10 },
            { "I", "bIII", "IV", "bV (Blue Note)", "V", "bVII" }
        },
        {
            "Miyako-bushi",
            "Japanese Pentatonic",
            { 0, 1, 5, 7, 8 },
            { "I", "bII", "IV", "V", "bVI" }
        },
        {
            "Ritsu",
            "Japanese Pentatonic",
            { 0, 2, 5, 7, 9 },
            { "I", "II", "IV", "V", "VI" }
        },
        {
            "Ryukyu",
            "Japanese Pentatonic",
            { 0, 4, 5, 7, 11 },
            { "I", "III", "IV", "V", "VII" }
        },
        {
            "Yonanuki Minor",
            "Japanese Pentatonic",
            { 0, 2, 3, 7, 8 },
            { "I", "II", "bIII", "V", "bVI" }
        }
    };
}

const std::vector<ScaleConfig>& ScaleDefinitions::getAllScales()
{
    ensureDefaultScales();
    return s_allScales;
}

void ScaleDefinitions::registerCustomScale(const juce::String& name, const std::vector<int>& intervals, const std::vector<juce::String>& degrees)
{
    ensureDefaultScales();
    for (auto& s : s_allScales)
    {
        if (s.name.equalsIgnoreCase(name))
        {
            s.intervals = intervals;
            s.degrees = degrees;
            return;
        }
    }
    ScaleConfig sc;
    sc.name = name;
    sc.category = "Custom";
    sc.intervals = intervals;
    sc.degrees = degrees;
    s_allScales.push_back(sc);
}


const ScaleConfig* ScaleDefinitions::findScale(const juce::String& name)
{
    const auto& scales = getAllScales();
    for (const auto& s : scales)
    {
        if (s.name.equalsIgnoreCase(name) || s.name.startsWithIgnoreCase(name))
            return &s;
    }
    return &scales[0];
}

juce::String ScaleDefinitions::getPreferredTonicName(int tonicPc,
                                                     const juce::String& scaleName)
{
    const int root = wrapPitchClass(tonicPc);
    static constexpr const char* preferred[] {
        "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static constexpr const char* sharps[] {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static constexpr const char* flats[] {
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

    const auto* scale = findScale(scaleName);
    if (scale == nullptr || scale->intervals.size() != 7)
        return preferred[root];
    const juce::String sharp = sharps[root];
    const juce::String flat = flats[root];
    if (sharp == flat) return sharp;
    const int sharpCost = spellingCost(sharp, root, scale->intervals);
    const int flatCost = spellingCost(flat, root, scale->intervals);
    if (sharpCost < flatCost) return sharp;
    if (flatCost < sharpCost) return flat;
    return preferred[root];
}

KeyLayout ScaleDefinitions::computeLayout(int tonicPc, const juce::String& scaleName,
                                          const juce::String& tonicName)
{
    KeyLayout layout;
    layout.tonicPc = ((tonicPc % 12) + 12) % 12;
    layout.tonicName = tonicNamePitchClass(tonicName) == layout.tonicPc
        ? tonicName.trim() : getPreferredTonicName(layout.tonicPc, scaleName);
    layout.scaleName = scaleName;

    const auto* config = findScale(scaleName);
    if (config == nullptr)
        config = &getAllScales()[0];

    std::map<int, juce::String> pcDegreeMap;
    for (size_t i = 0; i < config->intervals.size(); ++i)
    {
        int interval = config->intervals[i];
        int pc = (layout.tonicPc + interval) % 12;
        juce::String deg = (i < config->degrees.size()) ? config->degrees[i] : ("Step " + juce::String((int)i + 1));
        pcDegreeMap[pc] = deg;
    }

    for (int pc = 0; pc < 12; ++pc)
    {
        auto& info = layout.pitchClasses[pc];
        info.pc = pc;
        info.name = PITCH_NAMES[pc];
        info.isPhysicalWhite = isPhysicalWhite(pc);
        info.isTonic = (pc == layout.tonicPc);

        auto it = pcDegreeMap.find(pc);
        if (it != pcDegreeMap.end())
        {
            info.isInScale = true;
            info.degreeName = it->second;
        }
        else
        {
            info.isInScale = false;
            info.degreeName = "";
        }
    }

    return layout;
}

juce::Colour ScaleDefinitions::getNoteColor(const KeyLayout& layout, int pitch, ColoringMode mode, bool isSelected, juce::Colour trackColor, juce::Colour clipColor)
{
    auto info = layout.getInfo(pitch);
    juce::Colour base;

    switch (mode)
    {
        case ColoringMode::TrackColor:
            base = trackColor;
            break;

        case ColoringMode::ClipColor:
            base = clipColor;
            break;

        case ColoringMode::ScaleAdaptive:
        {
            if (info.isInScale)
            {
                if (info.isTonic)
                    base = juce::Colour(0xff059669); // Deep emerald for tonic
                else
                    base = juce::Colour(0xff10b981); // Emerald green for in-scale
            }
            else
                base = juce::Colour(0xfff43f5e);     // Warning red for out-of-scale
            break;
        }

        case ColoringMode::DegreeFunction:
        {
            const auto degree = info.degreeName.upToFirstOccurrenceOf(" ", false, false);
            if (!info.isInScale)
                base = juce::Colour(0xffef4444);     // Red out-of-scale
            else if (info.isTonic)
                base = juce::Colour(0xffeab308);     // Amber gold for I
            else if (degree == "IV" || degree == "#IV")
                base = juce::Colour(0xff06b6d4);     // Cyan for Subdominant IV
            else if (degree == "V")
                base = juce::Colour(0xff0ea5e9);     // Sky blue for Dominant V
            else
                base = juce::Colour(0xff10b981);     // Green for others
            break;
        }

        case ColoringMode::Uniform:
        default:
            base = juce::Colour(0xff2575fc);         // Cubase theme blue
            break;
    }

    // Selection changes the emphasis, never the note's musical colour.
    // The piano roll adds a separate outline and glow for recognition.
    return isSelected ? base.brighter(0.16f) : base;
}

juce::Colour ScaleDefinitions::getNoteStrokeColor(const KeyLayout& layout, int pitch, ColoringMode mode, bool isSelected, juce::Colour trackColor, juce::Colour clipColor)
{
    if (isSelected)
        return juce::Colours::white;

    auto fill = getNoteColor(layout, pitch, mode, false, trackColor, clipColor);
    return fill.brighter(0.25f);
}


} // namespace Harmonizer
