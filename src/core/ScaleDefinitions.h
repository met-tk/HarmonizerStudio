#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include <map>

namespace Harmonizer
{

enum class ColoringMode
{
    ScaleAdaptive = 0, // In-scale emerald green, Tonic gold/deep green, Out-of-scale alert red
    DegreeFunction = 1, // I: Amber gold, V: Sky blue, IV: Aurora cyan, others green, out: red
    Uniform = 2,        // Cubase classic blue
    TrackColor = 3,     // By Instrument Track color
    ClipColor = 4       // By MIDI Part (Clip) color
};

struct ScaleConfig
{
    juce::String name;
    juce::String category;
    std::vector<int> intervals;
    std::vector<juce::String> degrees;
};

struct PitchClassInfo
{
    int pc = 0;
    juce::String name;
    bool isPhysicalWhite = true;
    bool isInScale = true;
    bool isTonic = false;
    juce::String degreeName;
};

struct KeyLayout
{
    int tonicPc = 0;
    juce::String tonicName;
    juce::String scaleName;
    PitchClassInfo pitchClasses[12];

    bool isInScale(int pitch) const
    {
        int pc = ((pitch % 12) + 12) % 12;
        return pitchClasses[pc].isInScale;
    }

    bool isTonic(int pitch) const
    {
        int pc = ((pitch % 12) + 12) % 12;
        return pitchClasses[pc].isTonic;
    }

    PitchClassInfo getInfo(int pitch) const
    {
        int pc = ((pitch % 12) + 12) % 12;
        return pitchClasses[pc];
    }
};

class ScaleDefinitions
{
public:
    static const std::vector<ScaleConfig>& getAllScales();
    static const ScaleConfig* findScale(const juce::String& name);
    static KeyLayout computeLayout(int tonicPc, const juce::String& scaleName,
                                   const juce::String& tonicName = {});
    static juce::String getPreferredTonicName(int tonicPc, const juce::String& scaleName);
    static void registerCustomScale(const juce::String& name, const std::vector<int>& intervals, const std::vector<juce::String>& degrees = {});

    // Color calculation
    static juce::Colour getNoteColor(const KeyLayout& layout, int pitch, ColoringMode mode, bool isSelected = false,
                                     juce::Colour trackColor = juce::Colour(0xff2575fc),
                                     juce::Colour clipColor = juce::Colour(0xff10b981));
    static juce::Colour getNoteStrokeColor(const KeyLayout& layout, int pitch, ColoringMode mode, bool isSelected = false,
                                           juce::Colour trackColor = juce::Colour(0xff2575fc),
                                           juce::Colour clipColor = juce::Colour(0xff10b981));

    // Pitch helpers
    static juce::String getPitchName(int pc);
    static bool isPhysicalWhite(int pc);
    static bool isPhysicalBlack(int pc);
};

} // namespace Harmonizer
