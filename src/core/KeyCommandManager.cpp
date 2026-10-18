#include "KeyCommandManager.h"
#include "Localization.h"

namespace Harmonizer
{

KeyCommandManager& KeyCommandManager::getInstance()
{
    static KeyCommandManager instance;
    return instance;
}

KeyCommandManager::KeyCommandManager()
{
    initDefaultCommands();
    loadConfig();
}

void KeyCommandManager::initDefaultCommands()
{
    commands.clear();

    // 1. Edit Category
    commands.push_back({
        Cmd_Undo,
        "Undo",
        CommandCategory::Edit,
        { juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0) },
        {}
    });

    commands.push_back({
        Cmd_Redo,
        "Redo",
        CommandCategory::Edit,
        {
            juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0),
            juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0)
        },
        {}
    });

    commands.push_back({
        Cmd_Copy,
        "Copy",
        CommandCategory::Edit,
        { juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0) },
        {}
    });

    commands.push_back({
        Cmd_Paste,
        "Paste",
        CommandCategory::Edit,
        { juce::KeyPress('v', juce::ModifierKeys::commandModifier, 0) },
        {}
    });

    commands.push_back({
        Cmd_Duplicate,
        "Duplicate",
        CommandCategory::Edit,
        { juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0) },
        {}
    });

    commands.push_back({
        Cmd_Delete,
        "Delete",
        CommandCategory::Edit,
        { juce::KeyPress(juce::KeyPress::deleteKey), juce::KeyPress(juce::KeyPress::backspaceKey) },
        {}
    });

    commands.push_back({
        Cmd_SelectAll,
        "Select All",
        CommandCategory::Edit,
        { juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0) },
        {}
    });

    commands.push_back({
        Cmd_OctaveUp,
        "Octave Up",
        CommandCategory::Edit,
        { juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0) },
        {}
    });

    commands.push_back({
        Cmd_OctaveDown,
        "Octave Down",
        CommandCategory::Edit,
        { juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0) },
        {}
    });

    commands.push_back({
        Cmd_DeleteTrack,
        "Delete Selected Track",
        CommandCategory::Edit,
        { juce::KeyPress('d', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0),
          juce::KeyPress('D', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0) },
        {}
    });

    // 2. Tools Category
    commands.push_back({
        Cmd_ToolSelect,
        "1: Select",
        CommandCategory::Tools,
        { juce::KeyPress('1') },
        {}
    });

    commands.push_back({
        Cmd_ToolSplit,
        "3: Split",
        CommandCategory::Tools,
        { juce::KeyPress('3') },
        {}
    });

    commands.push_back({
        Cmd_ToolErase,
        "5: Erase",
        CommandCategory::Tools,
        { juce::KeyPress('5') },
        {}
    });

    commands.push_back({
        Cmd_ToolDraw,
        "8: Draw",
        CommandCategory::Tools,
        { juce::KeyPress('8') },
        {}
    });

    // 3. Transport Category
    commands.push_back({
        Cmd_PlayPause,
        "Play (Space)",
        CommandCategory::Transport,
        { juce::KeyPress(juce::KeyPress::spaceKey) },
        {}
    });

    commands.push_back({
        Cmd_Stop,
        "Stop",
        CommandCategory::Transport,
        { juce::KeyPress(juce::KeyPress::escapeKey) },
        {}
    });

    commands.push_back({
        Cmd_ToggleLoop,
        "Toggle Loop (/)",
        CommandCategory::Transport,
        { juce::KeyPress('/') },
        {}
    });

    commands.push_back({
        Cmd_RewindStart,
        "Rewind to Start",
        CommandCategory::Transport,
        { juce::KeyPress(juce::KeyPress::homeKey) },
        {}
    });

    commands.push_back({
        Cmd_ToggleMetronome,
        "Toggle Metronome",
        CommandCategory::Transport,
        { juce::KeyPress('c'), juce::KeyPress('C') },
        {}
    });

    // 4. View & Zoom Category
    commands.push_back({
        Cmd_SnapToggle,
        "Snap [J]",
        CommandCategory::ViewZoom,
        { juce::KeyPress('j') },
        {},
        false
    });

    commands.push_back({
        Cmd_ZoomInX,
        "Zoom +",
        CommandCategory::ViewZoom,
        { juce::KeyPress('h') },
        {},
        false
    });

    commands.push_back({
        Cmd_ZoomOutX,
        "Zoom -",
        CommandCategory::ViewZoom,
        { juce::KeyPress('g') },
        {},
        false
    });

    commands.push_back({
        Cmd_ZoomInY,
        "Zoom Pitch (In)",
        CommandCategory::ViewZoom,
        { juce::KeyPress('h', juce::ModifierKeys::shiftModifier, 0) },
        {},
        false
    });

    commands.push_back({
        Cmd_ZoomOutY,
        "Zoom Pitch (Out)",
        CommandCategory::ViewZoom,
        { juce::KeyPress('g', juce::ModifierKeys::shiftModifier, 0) },
        {},
        false
    });

    commands.push_back({
        Cmd_TogglePlayheadFollow,
        "Toggle Playhead Follow",
        CommandCategory::ViewZoom,
        { juce::KeyPress('f') },
        {}
    });

    // 5. Harmony Category
    commands.push_back({
        Cmd_SecondaryDominant,
        "Secondary Dominant",
        CommandCategory::Harmony,
        { juce::KeyPress('k', juce::ModifierKeys::commandModifier, 0) },
        {},
        false
    });

    commands.push_back({
        Cmd_ReharmonizeOptions,
        "Harmonic Settings",
        CommandCategory::Harmony,
        { juce::KeyPress('r', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0) },
        {},
        false
    });

    commands.push_back({
        Cmd_ApplyReharmonization,
        "Apply Reharmonization",
        CommandCategory::Harmony,
        {},
        {}
    });
}

KeyCommand* KeyCommandManager::findCommand(int id)
{
    for (auto& cmd : commands)
        if (cmd.id == id) return &cmd;
    return nullptr;
}

const KeyCommand* KeyCommandManager::findCommand(int id) const
{
    for (const auto& cmd : commands)
        if (cmd.id == id) return &cmd;
    return nullptr;
}

bool KeyCommandManager::matches(int commandId, const juce::KeyPress& kp) const
{
    const auto* cmd = findCommand(commandId);
    return cmd != nullptr && cmd->matches(kp);
}

int KeyCommandManager::findMatchingCommandId(const juce::KeyPress& kp) const
{
    for (const auto& cmd : commands)
    {
        if (cmd.matches(kp))
            return cmd.id;
    }
    return 0;
}

void KeyCommandManager::addKeyShortcut(int commandId, const juce::KeyPress& kp)
{
    if (auto* cmd = findCommand(commandId))
    {
        if (!cmd->isCustomized)
            cmd->userKeys = cmd->defaultKeys;

        cmd->isCustomized = true;

        for (const auto& existing : cmd->userKeys)
            if (existing == kp) return;

        cmd->userKeys.push_back(kp);
        saveConfig();
        listeners.call([](Listener& l) { l.keyCommandsChanged(); });
    }
}

void KeyCommandManager::removeKeyShortcut(int commandId, int index)
{
    if (auto* cmd = findCommand(commandId))
    {
        if (!cmd->isCustomized)
            cmd->userKeys = cmd->defaultKeys;

        cmd->isCustomized = true;

        if (index >= 0 && index < (int)cmd->userKeys.size())
        {
            cmd->userKeys.erase(cmd->userKeys.begin() + index);
            saveConfig();
            listeners.call([](Listener& l) { l.keyCommandsChanged(); });
        }
    }
}

void KeyCommandManager::clearKeyShortcuts(int commandId)
{
    if (auto* cmd = findCommand(commandId))
    {
        cmd->isCustomized = true;
        cmd->userKeys.clear(); // Explicitly disables the shortcut!
        saveConfig();
        listeners.call([](Listener& l) { l.keyCommandsChanged(); });
    }
}

void KeyCommandManager::resetCommandToDefault(int commandId)
{
    if (auto* cmd = findCommand(commandId))
    {
        cmd->isCustomized = false;
        cmd->userKeys.clear();
        saveConfig();
        listeners.call([](Listener& l) { l.keyCommandsChanged(); });
    }
}

void KeyCommandManager::resetAllToDefault()
{
    for (auto& cmd : commands)
    {
        cmd.isCustomized = false;
        cmd.userKeys.clear();
    }

    saveConfig();
    listeners.call([](Listener& l) { l.keyCommandsChanged(); });
}

juce::String KeyCommandManager::getCategoryName(CommandCategory cat)
{
    switch (cat)
    {
        case CommandCategory::Edit:      return tr("Edit");
        case CommandCategory::Tools:     return tr("Tools");
        case CommandCategory::Transport: return tr("Transport");
        case CommandCategory::ViewZoom:  return tr("View & Zoom");
        case CommandCategory::Harmony:   return tr("Harmony Editing");
        default:                         return "General";
    }
}

juce::File KeyCommandManager::getConfigFile() const
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("HarmonizerStudio");
    dir.createDirectory();
    return dir.getChildFile("key_commands.xml");
}

void KeyCommandManager::saveConfig()
{
    juce::XmlElement root("KeyCommands");
    for (const auto& cmd : commands)
    {
        if (cmd.isCustomized)
        {
            auto* elem = root.createNewChildElement("Command");
            elem->setAttribute("id", cmd.id);
            elem->setAttribute("customized", 1);
            for (const auto& k : cmd.userKeys)
            {
                auto* keyElem = elem->createNewChildElement("Key");
                keyElem->setAttribute("code", k.getKeyCode());
                keyElem->setAttribute("mods", k.getModifiers().getRawFlags());
                keyElem->setAttribute("char", juce::String::charToString(k.getTextCharacter()));
            }
        }
    }
    root.writeTo(getConfigFile());
}

void KeyCommandManager::loadConfig()
{
    auto file = getConfigFile();
    if (!file.existsAsFile()) return;

    auto xml = juce::parseXML(file);
    if (xml == nullptr) return;

    for (auto* cmdElem : xml->getChildIterator())
    {
        if (cmdElem->hasTagName("Command"))
        {
            int id = cmdElem->getIntAttribute("id");
            if (auto* cmd = findCommand(id))
            {
                cmd->isCustomized = (cmdElem->getIntAttribute("customized", 0) != 0);
                cmd->userKeys.clear();
                for (auto* keyElem : cmdElem->getChildIterator())
                {
                    if (keyElem->hasTagName("Key"))
                    {
                        int code = keyElem->getIntAttribute("code");
                        int mods = keyElem->getIntAttribute("mods");
                        auto ch = keyElem->getStringAttribute("char");
                        juce::KeyPress kp(code, juce::ModifierKeys(mods), ch.isNotEmpty() ? ch[0] : 0);
                        cmd->userKeys.push_back(kp);
                    }
                }
            }
        }
    }
}

} // namespace Harmonizer
