#pragma once

#include <juce_core/juce_core.h>

namespace Harmonizer
{

enum class MidiPartOpenMode
{
    SeparateWindow = 0, // 在独立窗口中打开
    LowerZone = 1,      // 在主界面下方区域打开
    MainScreen = 2      // 在主界面屏幕打开
};

enum class PausePlayheadMode
{
    StayAtPause = 0,
    ReturnToPlaybackStart = 1
};

enum class MetronomeSound
{
    Classic = 0,
    Wood = 1,
    Electronic = 2
};

class GeneralSettings
{
public:
    static GeneralSettings& getInstance()
    {
        static GeneralSettings instance;
        return instance;
    }

    MidiPartOpenMode getMidiPartOpenMode() const { return midiPartOpenMode; }
    void setMidiPartOpenMode(MidiPartOpenMode mode)
    {
        if (midiPartOpenMode != mode)
        {
            midiPartOpenMode = mode;
            saveConfig();
        }
    }

    PausePlayheadMode getPausePlayheadMode() const { return pausePlayheadMode; }
    void setPausePlayheadMode(PausePlayheadMode mode)
    {
        if (pausePlayheadMode != mode)
        {
            pausePlayheadMode = mode;
            saveConfig();
        }
    }

    bool getMetronomeEnabled() const { return metronomeEnabled; }
    void setMetronomeEnabled(bool enabled)
    {
        if (metronomeEnabled != enabled)
        {
            metronomeEnabled = enabled;
            saveConfig();
        }
    }

    MetronomeSound getMetronomeSound() const { return metronomeSound; }
    void setMetronomeSound(MetronomeSound sound)
    {
        if (metronomeSound != sound)
        {
            metronomeSound = sound;
            saveConfig();
        }
    }

    void saveConfig()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("HarmonizerStudio");
        dir.createDirectory();
        auto file = dir.getChildFile("general_settings.xml");

        juce::XmlElement root("GeneralSettings");
        root.setAttribute("midiPartOpenMode", (int)midiPartOpenMode);
        root.setAttribute("pausePlayheadMode", (int)pausePlayheadMode);
        root.setAttribute("metronomeEnabled", metronomeEnabled);
        root.setAttribute("metronomeSound", (int)metronomeSound);
        juce::TemporaryFile temporary(file);
        if (root.writeTo(temporary.getFile()))
            temporary.overwriteTargetFileWithTemporary();
    }

    void loadConfig()
    {
        auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("HarmonizerStudio");
        auto file = dir.getChildFile("general_settings.xml");
        if (!file.existsAsFile())
            return;

        auto xml = juce::parseXML(file);
        if (!xml || xml->getTagName() != "GeneralSettings")
            return;

        int modeVal = xml->getIntAttribute("midiPartOpenMode", (int)MidiPartOpenMode::LowerZone);
        if (modeVal >= 0 && modeVal <= 2)
            midiPartOpenMode = (MidiPartOpenMode)modeVal;
        const int pauseVal = xml->getIntAttribute("pausePlayheadMode", (int)PausePlayheadMode::StayAtPause);
        if (pauseVal >= 0 && pauseVal <= 1)
            pausePlayheadMode = (PausePlayheadMode)pauseVal;
        metronomeEnabled = xml->getBoolAttribute("metronomeEnabled", false);
        const int soundVal = xml->getIntAttribute("metronomeSound", 0);
        if (soundVal >= 0 && soundVal <= 2)
            metronomeSound = (MetronomeSound)soundVal;
    }

private:
    GeneralSettings()
    {
        loadConfig();
    }

    MidiPartOpenMode midiPartOpenMode = MidiPartOpenMode::LowerZone;
    PausePlayheadMode pausePlayheadMode = PausePlayheadMode::StayAtPause;
    bool metronomeEnabled = false;
    MetronomeSound metronomeSound = MetronomeSound::Classic;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneralSettings)
};

} // namespace Harmonizer
