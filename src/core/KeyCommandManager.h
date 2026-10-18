#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <map>

namespace Harmonizer
{

enum class CommandCategory
{
    Edit = 0,
    Tools = 1,
    Transport = 2,
    ViewZoom = 3,
    Harmony = 4
};

enum CommandID
{
    Cmd_Undo = 101,
    Cmd_Redo = 102,
    Cmd_Copy = 103,
    Cmd_Paste = 104,
    Cmd_Duplicate = 105,
    Cmd_Delete = 106,
    Cmd_SelectAll = 107,
    Cmd_OctaveUp = 108,
    Cmd_OctaveDown = 109,
    Cmd_DeleteTrack = 110,

    Cmd_ToolSelect = 201,
    Cmd_ToolSplit = 202,
    Cmd_ToolErase = 203,
    Cmd_ToolDraw = 204,

    Cmd_PlayPause = 301,
    Cmd_Stop = 302,
    Cmd_ToggleLoop = 303,
    Cmd_RewindStart = 304,
    Cmd_ToggleMetronome = 305,

    Cmd_SnapToggle = 401,
    Cmd_ZoomInX = 402,
    Cmd_ZoomOutX = 403,
    Cmd_ZoomInY = 404,
    Cmd_ZoomOutY = 405,
    Cmd_TogglePlayheadFollow = 406,

    Cmd_SecondaryDominant = 501,
    Cmd_ReharmonizeOptions = 502,
    Cmd_ApplyReharmonization = 503
};

struct KeyCommand
{
    int id;
    juce::String name;
    CommandCategory category;
    std::vector<juce::KeyPress> defaultKeys;
    std::vector<juce::KeyPress> userKeys;
    bool isCustomized = false;

    std::vector<juce::KeyPress>& getActiveKeys()
    {
        return isCustomized ? userKeys : defaultKeys;
    }

    const std::vector<juce::KeyPress>& getActiveKeys() const
    {
        return isCustomized ? userKeys : defaultKeys;
    }

    bool matches(const juce::KeyPress& kp) const
    {
        for (const auto& k : getActiveKeys())
        {
            if (k == kp)
                return true;
        }
        return false;
    }

    juce::String getDisplayString() const
    {
        if (isCustomized && userKeys.empty())
            return "(Disabled)";

        juce::StringArray parts;
        for (const auto& k : getActiveKeys())
        {
            auto desc = k.getTextDescription();
            if (!parts.contains(desc))
                parts.add(desc);
        }
        return parts.joinIntoString(" / ");
    }
};

class KeyCommandManager
{
public:
    static KeyCommandManager& getInstance();

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void keyCommandsChanged() = 0;
    };

    void addListener(Listener* l)       { listeners.add(l); }
    void removeListener(Listener* l)    { listeners.remove(l); }

    const std::vector<KeyCommand>& getAllCommands() const { return commands; }
    KeyCommand* findCommand(int id);
    const KeyCommand* findCommand(int id) const;

    bool matches(int commandId, const juce::KeyPress& kp) const;
    int findMatchingCommandId(const juce::KeyPress& kp) const;

    void addKeyShortcut(int commandId, const juce::KeyPress& kp);
    void removeKeyShortcut(int commandId, int index);
    void clearKeyShortcuts(int commandId);
    void resetCommandToDefault(int commandId);
    void resetAllToDefault();

    static juce::String getCategoryName(CommandCategory cat);

    void saveConfig();
    void loadConfig();

private:
    KeyCommandManager();
    std::vector<KeyCommand> commands;
    juce::ListenerList<Listener> listeners;

    void initDefaultCommands();
    juce::File getConfigFile() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyCommandManager)
};

} // namespace Harmonizer
