#include "CubaseTheme.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <iterator>

namespace Harmonizer
{

static ThemePalette activePalette;
static ThemePreset currentPresetType = ThemePreset::CubaseClassicDark;
static ThemePreset lastNamedPresetType = ThemePreset::CubaseClassicDark;
static juce::ListenerList<CubaseTheme::Listener> themeListeners;
static juce::Array<CubaseTheme*> themeInstances;

namespace
{
struct SavedThemeEntry
{
    int id = 0;
    juce::String name;
    ThemePalette palette;
};

std::vector<SavedThemeEntry> savedThemes;
int activeSavedThemeId = -1;
bool activeSavedThemeModified = false;
juce::File testThemeSettingsFile;
bool loadingThemeConfig = false;

juce::File themeSettingsFile()
{
    if (testThemeSettingsFile.getFullPathName().isNotEmpty())
        return testThemeSettingsFile;
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("HarmonizerStudio").getChildFile("theme_settings.xml");
}

auto findSavedTheme(int id)
{
    return std::find_if(savedThemes.begin(), savedThemes.end(), [id](const auto& entry) {
        return entry.id == id;
    });
}

bool validPresetName(const juce::String& name)
{
    return name.isNotEmpty() && name.length() <= 64 && !name.containsAnyOf("\r\n\t");
}

#define THEME_COLOURS(X) \
    X(bgDeep) X(bgSurface) X(bgPanel) \
    X(toolbarBackground) X(sidebarBackground) X(moduleHeaderBackground) \
    X(buttonBackground) X(inputBackground) X(menuBackground) \
    X(borderSubtle) X(borderBright) \
    X(accentBlue) X(accentOrange) X(accentCyan) X(accentGreen) X(accentPurple) \
    X(noteGlow) X(textPrimary) X(textSecondary) \
    X(gridBarLine) X(gridBeatLine) X(gridSubBeat) \
    X(gridWhiteKeyRow) X(gridBlackKeyRow) X(pianoRollTonicTint) \
    X(locatorHandle) X(locatorRangeOverlay)

void writePalette(juce::XmlElement& xml, const ThemePalette& palette)
{
#define WRITE_COLOUR(field) xml.setAttribute(#field, palette.field.toDisplayString(true));
    THEME_COLOURS(WRITE_COLOUR)
#undef WRITE_COLOUR
}

void readPalette(const juce::XmlElement& xml, ThemePalette& palette)
{
    auto parse = [&](const char* attribute, juce::Colour fallback) {
        const auto value = xml.getStringAttribute(attribute);
        return value.isNotEmpty() ? juce::Colour::fromString(value) : fallback;
    };

    palette.bgDeep = parse("bgDeep", palette.bgDeep);
    palette.bgSurface = parse("bgSurface", palette.bgSurface);
    palette.bgPanel = parse("bgPanel", palette.bgPanel);
    // Compatibility with custom themes saved before component colours existed.
    palette.toolbarBackground = parse("toolbarBackground",
        palette.bgSurface.interpolatedWith(palette.bgPanel, 0.25f));
    palette.sidebarBackground = parse("sidebarBackground", palette.bgSurface);
    palette.moduleHeaderBackground = parse("moduleHeaderBackground",
        palette.bgPanel.interpolatedWith(palette.bgDeep, 0.25f));
    palette.buttonBackground = parse("buttonBackground",
        palette.bgSurface.interpolatedWith(palette.bgPanel, 0.45f));
    palette.inputBackground = parse("inputBackground",
        palette.bgSurface.interpolatedWith(palette.bgDeep, 0.16f));
    palette.menuBackground = parse("menuBackground", palette.bgPanel);

#define READ_COLOUR(field) palette.field = parse(#field, palette.field);
    READ_COLOUR(borderSubtle) READ_COLOUR(borderBright)
    READ_COLOUR(accentBlue) READ_COLOUR(accentOrange) READ_COLOUR(accentCyan)
    READ_COLOUR(accentGreen) READ_COLOUR(accentPurple) READ_COLOUR(noteGlow)
    READ_COLOUR(textPrimary) READ_COLOUR(textSecondary)
    READ_COLOUR(gridBarLine) READ_COLOUR(gridBeatLine) READ_COLOUR(gridSubBeat)
    READ_COLOUR(gridWhiteKeyRow) READ_COLOUR(gridBlackKeyRow)
    READ_COLOUR(pianoRollTonicTint) READ_COLOUR(locatorHandle)
    READ_COLOUR(locatorRangeOverlay)
#undef READ_COLOUR
}

#undef THEME_COLOURS
}

ThemePalette& CubaseTheme::getPalette()
{
    return activePalette;
}

ThemePreset CubaseTheme::getCurrentPreset()
{
    return currentPresetType;
}

ThemePreset CubaseTheme::getLastNamedPreset()
{
    return lastNamedPresetType;
}

std::vector<SavedThemePreset> CubaseTheme::getSavedPresets()
{
    std::vector<SavedThemePreset> result;
    result.reserve(savedThemes.size());
    for (const auto& entry : savedThemes)
        result.push_back({ entry.id, entry.name });
    return result;
}

int CubaseTheme::getActiveSavedPresetId()
{
    return activeSavedThemeId;
}

bool CubaseTheme::isActiveSavedPresetModified()
{
    return activeSavedThemeId > 0 && activeSavedThemeModified;
}

bool CubaseTheme::applySavedPreset(int id)
{
    const auto entry = findSavedTheme(id);
    if (entry == savedThemes.end()) return false;
    const auto previousPalette = activePalette;
    const auto previousPreset = currentPresetType;
    const int previousId = activeSavedThemeId;
    const bool previousModified = activeSavedThemeModified;
    activePalette = entry->palette;
    activePalette.name = entry->name;
    currentPresetType = ThemePreset::Custom;
    activeSavedThemeId = id;
    activeSavedThemeModified = false;
    if (!saveThemeConfig())
    {
        activePalette = previousPalette;
        currentPresetType = previousPreset;
        activeSavedThemeId = previousId;
        activeSavedThemeModified = previousModified;
        return false;
    }
    notifyThemeChanged();
    return true;
}

int CubaseTheme::saveAsPreset(const juce::String& requestedName)
{
    const auto name = requestedName.trim();
    if (!validPresetName(name) || savedThemes.size() >= 1000)
        return -1;
    for (const auto& entry : savedThemes)
        if (entry.name.equalsIgnoreCase(name)) return -1;

    int id = 1;
    for (const auto& entry : savedThemes)
        id = std::max(id, entry.id + 1);
    if (id > 1000000) return -1;

    auto palette = activePalette;
    palette.name = name;
    const auto previousName = activePalette.name;
    const auto previousPreset = currentPresetType;
    const int previousId = activeSavedThemeId;
    const bool previousModified = activeSavedThemeModified;
    savedThemes.push_back({ id, name, palette });
    activePalette.name = name;
    currentPresetType = ThemePreset::Custom;
    activeSavedThemeId = id;
    activeSavedThemeModified = false;
    if (!saveThemeConfig())
    {
        savedThemes.pop_back();
        activePalette.name = previousName;
        currentPresetType = previousPreset;
        activeSavedThemeId = previousId;
        activeSavedThemeModified = previousModified;
        return -1;
    }
    notifyThemeChanged();
    return id;
}

bool CubaseTheme::renameSavedPreset(int id, const juce::String& requestedName)
{
    const auto entry = findSavedTheme(id);
    const auto name = requestedName.trim();
    if (entry == savedThemes.end() || !validPresetName(name)) return false;
    for (const auto& other : savedThemes)
        if (other.id != id && other.name.equalsIgnoreCase(name)) return false;

    const auto previousName = entry->name;
    const auto previousPaletteName = activePalette.name;
    entry->name = name;
    entry->palette.name = name;
    if (activeSavedThemeId == id)
        activePalette.name = name;
    if (!saveThemeConfig())
    {
        entry->name = previousName;
        entry->palette.name = previousName;
        activePalette.name = previousPaletteName;
        return false;
    }
    notifyThemeChanged();
    return true;
}

bool CubaseTheme::overwriteSavedPreset(int id)
{
    const auto entry = findSavedTheme(id);
    if (entry == savedThemes.end()) return false;
    const auto previousPalette = entry->palette;
    const auto previousName = activePalette.name;
    const auto previousPreset = currentPresetType;
    const int previousId = activeSavedThemeId;
    const bool previousModified = activeSavedThemeModified;
    entry->palette = activePalette;
    entry->palette.name = entry->name;
    activePalette.name = entry->name;
    currentPresetType = ThemePreset::Custom;
    activeSavedThemeId = id;
    activeSavedThemeModified = false;
    if (!saveThemeConfig())
    {
        entry->palette = previousPalette;
        activePalette.name = previousName;
        currentPresetType = previousPreset;
        activeSavedThemeId = previousId;
        activeSavedThemeModified = previousModified;
        return false;
    }
    notifyThemeChanged();
    return true;
}

bool CubaseTheme::deleteSavedPreset(int id)
{
    const auto entry = findSavedTheme(id);
    if (entry == savedThemes.end()) return false;
    const auto previousEntry = *entry;
    const auto position = (size_t)std::distance(savedThemes.begin(), entry);
    const auto previousName = activePalette.name;
    const int previousId = activeSavedThemeId;
    const bool previousModified = activeSavedThemeModified;
    savedThemes.erase(entry);
    if (activeSavedThemeId == id)
    {
        activeSavedThemeId = -1;
        activeSavedThemeModified = false;
        activePalette.name = "Custom";
    }
    if (!saveThemeConfig())
    {
        savedThemes.insert(savedThemes.begin() + (std::ptrdiff_t)position, previousEntry);
        activePalette.name = previousName;
        activeSavedThemeId = previousId;
        activeSavedThemeModified = previousModified;
        return false;
    }
    notifyThemeChanged();
    return true;
}

void CubaseTheme::setPreset(ThemePreset preset)
{
    activeSavedThemeId = -1;
    activeSavedThemeModified = false;
    if (preset != ThemePreset::Custom)
        lastNamedPresetType = preset;
    currentPresetType = preset;
    switch (preset)
    {
        case ThemePreset::CubaseClassicDark:
            activePalette.name = "Cubase Classic Dark";
            activePalette.bgDeep          = juce::Colour(0xff181a1f);
            activePalette.bgSurface       = juce::Colour(0xff23262c);
            activePalette.bgPanel         = juce::Colour(0xff2c3037);
            activePalette.toolbarBackground = juce::Colour(0xff252b32);
            activePalette.sidebarBackground = juce::Colour(0xff222830);
            activePalette.moduleHeaderBackground = juce::Colour(0xff252c34);
            activePalette.buttonBackground = juce::Colour(0xff343b45);
            activePalette.inputBackground = juce::Colour(0xff1e252c);
            activePalette.menuBackground = juce::Colour(0xff303741);
            activePalette.borderSubtle    = juce::Colour(0xff414751);
            activePalette.borderBright    = juce::Colour(0xff5a6370);
            activePalette.accentBlue      = juce::Colour(0xff467bd0);
            activePalette.accentOrange    = juce::Colour(0xffd79a58);
            activePalette.accentCyan      = juce::Colour(0xff58afc1);
            activePalette.accentGreen     = juce::Colour(0xff54ae88);
            activePalette.accentPurple    = juce::Colour(0xffaa8acb);
            activePalette.noteGlow        = juce::Colour(0xff79bde2);
            activePalette.textPrimary     = juce::Colour(0xffeceff4);
            activePalette.textSecondary   = juce::Colour(0xffb2bac6);
            activePalette.gridBarLine     = juce::Colour(0xff4b5360);
            activePalette.gridBeatLine    = juce::Colour(0xff39414a);
            activePalette.gridSubBeat     = juce::Colour(0xff2d333b);
            activePalette.gridWhiteKeyRow = juce::Colour(0xff242930);
            activePalette.gridBlackKeyRow = juce::Colour(0xff1e2329);
            activePalette.pianoRollTonicTint = juce::Colour(0x3bd79a58);
            activePalette.locatorHandle = activePalette.accentBlue;
            activePalette.locatorRangeOverlay = juce::Colour(0x25467bd0);
            break;

        case ThemePreset::ModernStudioSlate:
            activePalette.name = "Modern Studio Slate";
            activePalette.bgDeep          = juce::Colour(0xff151b25);
            activePalette.bgSurface       = juce::Colour(0xff202b38);
            activePalette.bgPanel         = juce::Colour(0xff2b3948);
            activePalette.toolbarBackground = juce::Colour(0xff223141);
            activePalette.sidebarBackground = juce::Colour(0xff1e2b39);
            activePalette.moduleHeaderBackground = juce::Colour(0xff26394b);
            activePalette.buttonBackground = juce::Colour(0xff33475a);
            activePalette.inputBackground = juce::Colour(0xff1d2d3c);
            activePalette.menuBackground = juce::Colour(0xff304255);
            activePalette.borderSubtle    = juce::Colour(0xff415268);
            activePalette.borderBright    = juce::Colour(0xff66788b);
            activePalette.accentBlue      = juce::Colour(0xff427cae);
            activePalette.accentOrange    = juce::Colour(0xffc99364);
            activePalette.accentCyan      = juce::Colour(0xff65b9bc);
            activePalette.accentGreen     = juce::Colour(0xff63af9a);
            activePalette.accentPurple    = juce::Colour(0xffa997d2);
            activePalette.noteGlow        = juce::Colour(0xff81c7e0);
            activePalette.textPrimary     = juce::Colour(0xfff1f5f8);
            activePalette.textSecondary   = juce::Colour(0xffbac7d4);
            activePalette.gridBarLine     = juce::Colour(0xff536376);
            activePalette.gridBeatLine    = juce::Colour(0xff3c4b59);
            activePalette.gridSubBeat     = juce::Colour(0xff303e4b);
            activePalette.gridWhiteKeyRow = juce::Colour(0xff283744);
            activePalette.gridBlackKeyRow = juce::Colour(0xff1c2835);
            activePalette.pianoRollTonicTint = juce::Colour(0x3a65b9bc);
            activePalette.locatorHandle = activePalette.accentBlue;
            activePalette.locatorRangeOverlay = juce::Colour(0x25427cae);
            break;

        case ThemePreset::AbletonMinimalist:
            activePalette.name = "Ableton Minimalist";
            activePalette.bgDeep          = juce::Colour(0xff1c1e21);
            activePalette.bgSurface       = juce::Colour(0xff292c30);
            activePalette.bgPanel         = juce::Colour(0xff33373c);
            activePalette.toolbarBackground = juce::Colour(0xff30343a);
            activePalette.sidebarBackground = juce::Colour(0xff25292e);
            activePalette.moduleHeaderBackground = juce::Colour(0xff2d3238);
            activePalette.buttonBackground = juce::Colour(0xff3b4149);
            activePalette.inputBackground = juce::Colour(0xff252a30);
            activePalette.menuBackground = juce::Colour(0xff393e45);
            activePalette.borderSubtle    = juce::Colour(0xff484e56);
            activePalette.borderBright    = juce::Colour(0xff646c76);
            activePalette.accentBlue      = juce::Colour(0xff557ba9);
            activePalette.accentOrange    = juce::Colour(0xffc8925b);
            activePalette.accentCyan      = juce::Colour(0xff6eabb2);
            activePalette.accentGreen     = juce::Colour(0xff72ae8e);
            activePalette.accentPurple    = juce::Colour(0xffa68dad);
            activePalette.noteGlow        = juce::Colour(0xff83bed0);
            activePalette.textPrimary     = juce::Colour(0xffeef0f2);
            activePalette.textSecondary   = juce::Colour(0xffbac1c8);
            activePalette.gridBarLine     = juce::Colour(0xff565c63);
            activePalette.gridBeatLine    = juce::Colour(0xff42474d);
            activePalette.gridSubBeat     = juce::Colour(0xff353a40);
            activePalette.gridWhiteKeyRow = juce::Colour(0xff2b2f33);
            activePalette.gridBlackKeyRow = juce::Colour(0xff222529);
            activePalette.pianoRollTonicTint = juce::Colour(0x36c8925b);
            activePalette.locatorHandle = activePalette.accentBlue;
            activePalette.locatorRangeOverlay = juce::Colour(0x25557ba9);
            break;

        case ThemePreset::WarmSpaceGrey:
            activePalette.name = "Warm Space Grey";
            activePalette.bgDeep          = juce::Colour(0xff211d1b);
            activePalette.bgSurface       = juce::Colour(0xff302926);
            activePalette.bgPanel         = juce::Colour(0xff3c3430);
            activePalette.toolbarBackground = juce::Colour(0xff332b28);
            activePalette.sidebarBackground = juce::Colour(0xff2a2422);
            activePalette.moduleHeaderBackground = juce::Colour(0xff372e2b);
            activePalette.buttonBackground = juce::Colour(0xff453934);
            activePalette.inputBackground = juce::Colour(0xff292321);
            activePalette.menuBackground = juce::Colour(0xff403631);
            activePalette.borderSubtle    = juce::Colour(0xff554943);
            activePalette.borderBright    = juce::Colour(0xff78645a);
            activePalette.accentBlue      = juce::Colour(0xff4f789c);
            activePalette.accentOrange    = juce::Colour(0xffc58d61);
            activePalette.accentCyan      = juce::Colour(0xff81b3aa);
            activePalette.accentGreen     = juce::Colour(0xff8faf87);
            activePalette.accentPurple    = juce::Colour(0xffb29abd);
            activePalette.noteGlow        = juce::Colour(0xffd3ae81);
            activePalette.textPrimary     = juce::Colour(0xfff5eee8);
            activePalette.textSecondary   = juce::Colour(0xffcdbfb3);
            activePalette.gridBarLine     = juce::Colour(0xff6a5a50);
            activePalette.gridBeatLine    = juce::Colour(0xff51463f);
            activePalette.gridSubBeat     = juce::Colour(0xff3e3631);
            activePalette.gridWhiteKeyRow = juce::Colour(0xff312b28);
            activePalette.gridBlackKeyRow = juce::Colour(0xff231f1d);
            activePalette.pianoRollTonicTint = juce::Colour(0x3cc58d61);
            activePalette.locatorHandle = activePalette.accentBlue;
            activePalette.locatorRangeOverlay = juce::Colour(0x2a4f789c);
            break;

        case ThemePreset::ModernStudioLight:
            activePalette.name = "Modern Studio Light";
            activePalette.bgDeep          = juce::Colour(0xffe7ecf1);
            activePalette.bgSurface       = juce::Colour(0xfff2f5f8);
            activePalette.bgPanel         = juce::Colour(0xffffffff);
            activePalette.toolbarBackground = juce::Colour(0xffe8eef4);
            activePalette.sidebarBackground = juce::Colour(0xffedf2f7);
            activePalette.moduleHeaderBackground = juce::Colour(0xffe3ebf3);
            activePalette.buttonBackground = juce::Colour(0xffe5edf5);
            activePalette.inputBackground = juce::Colour(0xffffffff);
            activePalette.menuBackground = juce::Colour(0xffffffff);
            activePalette.borderSubtle    = juce::Colour(0xffcbd4df);
            activePalette.borderBright    = juce::Colour(0xff9aa9b8);
            activePalette.accentBlue      = juce::Colour(0xff356fa5);
            activePalette.accentOrange    = juce::Colour(0xffa45d35);
            activePalette.accentCyan      = juce::Colour(0xff247f8f);
            activePalette.accentGreen     = juce::Colour(0xff2d815b);
            activePalette.accentPurple    = juce::Colour(0xff735998);
            activePalette.noteGlow        = juce::Colour(0xff3797c1);
            activePalette.textPrimary     = juce::Colour(0xff172334);
            activePalette.textSecondary   = juce::Colour(0xff516173);
            activePalette.gridBarLine     = juce::Colour(0xffa9b7c6);
            activePalette.gridBeatLine    = juce::Colour(0xffcdd7e1);
            activePalette.gridSubBeat     = juce::Colour(0xffe5ebf1);
            activePalette.gridWhiteKeyRow = juce::Colour(0xffffffff);
            activePalette.gridBlackKeyRow = juce::Colour(0xffe9eef4);
            activePalette.pianoRollTonicTint = juce::Colour(0x30356fa5);
            activePalette.locatorHandle = activePalette.accentBlue;
            activePalette.locatorRangeOverlay = juce::Colour(0x20356fa5);
            break;

        case ThemePreset::CubaseClassicLight:
            activePalette.name = "Cubase Classic Light";
            activePalette.bgDeep          = juce::Colour(0xffdce1e6);
            activePalette.bgSurface       = juce::Colour(0xffe9edf1);
            activePalette.bgPanel         = juce::Colour(0xfff5f7f9);
            activePalette.toolbarBackground = juce::Colour(0xffe3e9ef);
            activePalette.sidebarBackground = juce::Colour(0xffe8edf2);
            activePalette.moduleHeaderBackground = juce::Colour(0xffdce5ee);
            activePalette.buttonBackground = juce::Colour(0xffe4ebf2);
            activePalette.inputBackground = juce::Colour(0xffffffff);
            activePalette.menuBackground = juce::Colour(0xfff8fafb);
            activePalette.borderSubtle    = juce::Colour(0xffbcc6d0);
            activePalette.borderBright    = juce::Colour(0xff8998a8);
            activePalette.accentBlue      = juce::Colour(0xff3b689b);
            activePalette.accentOrange    = juce::Colour(0xff9f653d);
            activePalette.accentCyan      = juce::Colour(0xff2a7d87);
            activePalette.accentGreen     = juce::Colour(0xff357a59);
            activePalette.accentPurple    = juce::Colour(0xff70578d);
            activePalette.noteGlow        = juce::Colour(0xff4a9aba);
            activePalette.textPrimary     = juce::Colour(0xff1c2633);
            activePalette.textSecondary   = juce::Colour(0xff4d5a68);
            activePalette.gridBarLine     = juce::Colour(0xff98a7b5);
            activePalette.gridBeatLine    = juce::Colour(0xffbecad4);
            activePalette.gridSubBeat     = juce::Colour(0xffd9e0e7);
            activePalette.gridWhiteKeyRow = juce::Colour(0xfff4f6f8);
            activePalette.gridBlackKeyRow = juce::Colour(0xffdfe5ea);
            activePalette.pianoRollTonicTint = juce::Colour(0x329f653d);
            activePalette.locatorHandle = activePalette.accentBlue;
            activePalette.locatorRangeOverlay = juce::Colour(0x203b689b);
            break;

        case ThemePreset::Custom:
            activePalette.name = "Custom";
            break;
    }

    if (!loadingThemeConfig)
        saveThemeConfig();
    notifyThemeChanged();
}

void CubaseTheme::applyCustomColor(const juce::String& key, juce::Colour newColor)
{
    currentPresetType = ThemePreset::Custom;
    if (activeSavedThemeId > 0)
        activeSavedThemeModified = true;
    else
        activePalette.name = "Custom";

    if (key == "bgDeep") activePalette.bgDeep = newColor;
    else if (key == "bgSurface") activePalette.bgSurface = newColor;
    else if (key == "bgPanel") activePalette.bgPanel = newColor;
    else if (key == "toolbarBackground") activePalette.toolbarBackground = newColor;
    else if (key == "sidebarBackground") activePalette.sidebarBackground = newColor;
    else if (key == "moduleHeaderBackground") activePalette.moduleHeaderBackground = newColor;
    else if (key == "buttonBackground") activePalette.buttonBackground = newColor;
    else if (key == "inputBackground") activePalette.inputBackground = newColor;
    else if (key == "menuBackground") activePalette.menuBackground = newColor;
    else if (key == "accentBlue") activePalette.accentBlue = newColor;
    else if (key == "accentOrange") activePalette.accentOrange = newColor;
    else if (key == "accentCyan") activePalette.accentCyan = newColor;
    else if (key == "accentGreen") activePalette.accentGreen = newColor;
    else if (key == "accentPurple") activePalette.accentPurple = newColor;
    else if (key == "noteGlow") activePalette.noteGlow = newColor;
    else if (key == "borderSubtle") activePalette.borderSubtle = newColor;
    else if (key == "borderBright") activePalette.borderBright = newColor;
    else if (key == "textPrimary") activePalette.textPrimary = newColor;
    else if (key == "textSecondary") activePalette.textSecondary = newColor;
    else if (key == "gridBarLine") activePalette.gridBarLine = newColor;
    else if (key == "gridBeatLine") activePalette.gridBeatLine = newColor;
    else if (key == "gridSubBeat") activePalette.gridSubBeat = newColor;
    else if (key == "gridWhiteKeyRow") activePalette.gridWhiteKeyRow = newColor;
    else if (key == "gridBlackKeyRow") activePalette.gridBlackKeyRow = newColor;
    else if (key == "pianoRollTonicTint") activePalette.pianoRollTonicTint = newColor;
    else if (key == "locatorHandle") activePalette.locatorHandle = newColor;
    else if (key == "locatorRangeOverlay") activePalette.locatorRangeOverlay = newColor;

    saveThemeConfig();
    notifyThemeChanged();
}

void CubaseTheme::addThemeListener(Listener* l)
{
    themeListeners.add(l);
}

void CubaseTheme::removeThemeListener(Listener* l)
{
    themeListeners.remove(l);
}

void CubaseTheme::notifyThemeChanged()
{
    for (auto* inst : themeInstances)
    {
        if (inst != nullptr)
            inst->updateLookAndFeelColours();
    }

    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
    {
        if (auto* top = juce::TopLevelWindow::getTopLevelWindow(i))
            top->sendLookAndFeelChange();
    }

    themeListeners.call([](Listener& l) { l.themeChanged(); });
}

bool CubaseTheme::saveThemeConfig()
{
    auto file = themeSettingsFile();
    auto dir = file.getParentDirectory();
    if (!dir.createDirectory().wasOk()) return false;

    juce::XmlElement root("ThemeSettings");
    root.setAttribute("preset", (int)currentPresetType);
    root.setAttribute("basePreset", (int)lastNamedPresetType);
    root.setAttribute("name", activePalette.name);
    root.setAttribute("activeSavedPresetId", activeSavedThemeId);
    root.setAttribute("savedPresetModified", activeSavedThemeModified);
    writePalette(root, activePalette);
    auto* saved = root.createNewChildElement("SavedPresets");
    for (const auto& entry : savedThemes)
    {
        auto* item = saved->createNewChildElement("Preset");
        item->setAttribute("id", entry.id);
        item->setAttribute("name", entry.name);
        writePalette(*item, entry.palette);
    }

    juce::TemporaryFile temporary(file);
    return root.writeTo(temporary.getFile())
        && temporary.overwriteTargetFileWithTemporary();
}

void CubaseTheme::loadThemeConfig()
{
    struct LoadingGuard
    {
        LoadingGuard() : previous(loadingThemeConfig) { loadingThemeConfig = true; }
        ~LoadingGuard() { loadingThemeConfig = previous; }
        bool previous;
    } guard;
    savedThemes.clear();
    activeSavedThemeId = -1;
    activeSavedThemeModified = false;
    auto file = themeSettingsFile();
    if (!file.existsAsFile())
    {
        setPreset(ThemePreset::CubaseClassicDark);
        return;
    }

    auto xml = juce::parseXML(file);
    if (!xml || xml->getTagName() != "ThemeSettings")
    {
        setPreset(ThemePreset::CubaseClassicDark);
        return;
    }

    if (auto* saved = xml->getChildByName("SavedPresets"))
    {
        for (auto* item = saved->getFirstChildElement(); item != nullptr;
             item = item->getNextElement())
        {
            if (!item->hasTagName("Preset") || savedThemes.size() >= 1000) continue;
            const int id = item->getIntAttribute("id", -1);
            const auto name = item->getStringAttribute("name").trim();
            if (id <= 0 || id > 1000000 || !validPresetName(name)
                || findSavedTheme(id) != savedThemes.end())
                continue;
            const bool duplicateName = std::any_of(savedThemes.begin(), savedThemes.end(),
                [&](const auto& entry) { return entry.name.equalsIgnoreCase(name); });
            if (duplicateName) continue;
            ThemePalette palette;
            readPalette(*item, palette);
            palette.name = name;
            savedThemes.push_back({ id, name, palette });
        }
    }

    int presetInt = xml->getIntAttribute("preset", (int)ThemePreset::CubaseClassicDark);
    if (presetInt >= (int)ThemePreset::CubaseClassicDark
        && presetInt < (int)ThemePreset::Custom)
    {
        // Named presets use the current balanced palette. Custom themes retain
        // every saved colour, including themes created with older versions.
        setPreset((ThemePreset)presetInt);
        return;
    }
    if (presetInt != (int)ThemePreset::Custom)
    {
        setPreset(ThemePreset::CubaseClassicDark);
        return;
    }
    currentPresetType = (ThemePreset)presetInt;
    const int basePreset = xml->getIntAttribute("basePreset", (int)ThemePreset::CubaseClassicDark);
    lastNamedPresetType = basePreset >= (int)ThemePreset::CubaseClassicDark
                          && basePreset < (int)ThemePreset::Custom
                              ? (ThemePreset)basePreset : ThemePreset::CubaseClassicDark;
    activePalette.name = xml->getStringAttribute("name", "Custom");
    readPalette(*xml, activePalette);
    const int savedId = xml->getIntAttribute("activeSavedPresetId", -1);
    if (findSavedTheme(savedId) != savedThemes.end())
    {
        activeSavedThemeId = savedId;
        activeSavedThemeModified = xml->getBoolAttribute("savedPresetModified", false);
        activePalette.name = findSavedTheme(savedId)->name;
    }
}

int runThemePresetSelfTests()
{
    const auto oldPalette = activePalette;
    const auto oldPreset = currentPresetType;
    const auto oldNamedPreset = lastNamedPresetType;
    const auto oldSaved = savedThemes;
    const int oldActiveId = activeSavedThemeId;
    const bool oldModified = activeSavedThemeModified;
    const auto oldTestFile = testThemeSettingsFile;
    const auto testFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("harmonizer_theme_presets_", ".xml", false);
    struct Restore
    {
        ~Restore()
        {
            activePalette = palette;
            currentPresetType = preset;
            lastNamedPresetType = namedPreset;
            savedThemes = saved;
            activeSavedThemeId = activeId;
            activeSavedThemeModified = modified;
            testThemeSettingsFile = previousTestFile;
            file.deleteFile();
        }
        ThemePalette palette;
        ThemePreset preset;
        ThemePreset namedPreset;
        std::vector<SavedThemeEntry> saved;
        int activeId;
        bool modified;
        juce::File previousTestFile;
        juce::File file;
    } restore { oldPalette, oldPreset, oldNamedPreset, oldSaved,
                oldActiveId, oldModified, oldTestFile, testFile };

    testThemeSettingsFile = testFile;
    savedThemes.clear();
    activeSavedThemeId = -1;
    activeSavedThemeModified = false;
    int failures = 0;
    auto check = [&](const char* label, bool passed) {
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", label);
        if (!passed) ++failures;
    };

    CubaseTheme::setPreset(ThemePreset::CubaseClassicDark);
    CubaseTheme::applyCustomColor("buttonBackground", juce::Colour(0xff334455));
    CubaseTheme::applyCustomColor("pianoRollTonicTint", juce::Colour(0x4a112233));
    const int id = CubaseTheme::saveAsPreset("Test Palette");
    check("save a named palette", id > 0 && testFile.existsAsFile());
    CubaseTheme::applyCustomColor("buttonBackground", juce::Colour(0xff667788));
    check("editing marks saved palette as modified without overwriting",
          CubaseTheme::getActiveSavedPresetId() == id
          && CubaseTheme::isActiveSavedPresetModified());
    CubaseTheme::loadThemeConfig();
    check("modified colors and preset association survive reload",
          CubaseTheme::getActiveSavedPresetId() == id
          && CubaseTheme::isActiveSavedPresetModified()
          && CubaseTheme::buttonBackground() == juce::Colour(0xff667788));
    check("selecting the saved preset restores its original snapshot",
          CubaseTheme::applySavedPreset(id)
          && CubaseTheme::buttonBackground() == juce::Colour(0xff334455)
          && CubaseTheme::pianoRollTonicTint() == juce::Colour(0x4a112233));
    CubaseTheme::applyCustomColor("buttonBackground", juce::Colour(0xff8899aa));
    check("overwrite commits the edited colors",
          CubaseTheme::overwriteSavedPreset(id)
          && !CubaseTheme::isActiveSavedPresetModified());
    CubaseTheme::loadThemeConfig();
    check("overwritten colors survive reload",
          CubaseTheme::applySavedPreset(id)
          && CubaseTheme::buttonBackground() == juce::Colour(0xff8899aa));
    check("rename persists", CubaseTheme::renameSavedPreset(id, "Renamed Palette"));
    CubaseTheme::loadThemeConfig();
    const auto renamed = CubaseTheme::getSavedPresets();
    check("renamed name survives reload", renamed.size() == 1
          && renamed[0].id == id && renamed[0].name == "Renamed Palette");
    check("duplicate names are rejected", CubaseTheme::saveAsPreset("renamed palette") < 0);
    check("delete removes only the saved copy", CubaseTheme::deleteSavedPreset(id)
          && CubaseTheme::getActiveSavedPresetId() < 0
          && CubaseTheme::buttonBackground() == juce::Colour(0xff8899aa));
    CubaseTheme::loadThemeConfig();
    check("deletion and current custom colors survive reload",
          CubaseTheme::getSavedPresets().empty()
          && CubaseTheme::buttonBackground() == juce::Colour(0xff8899aa));
    return failures == 0 ? 0 : 1;
}

void CubaseTheme::updateLookAndFeelColours()
{
    setColour(juce::ResizableWindow::backgroundColourId, bgDeep());
    setColour(juce::PopupMenu::backgroundColourId, menuBackground());
    setColour(juce::PopupMenu::textColourId, textPrimary());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accentBlue());
    setColour(juce::PopupMenu::highlightedTextColourId, textOn(accentBlue()));

    setColour(juce::GroupComponent::outlineColourId, borderSubtle());
    setColour(juce::GroupComponent::textColourId, textPrimary());

    setColour(juce::AlertWindow::backgroundColourId, bgPanel());
    setColour(juce::AlertWindow::textColourId, textPrimary());
    setColour(juce::AlertWindow::outlineColourId, borderBright());

    setColour(juce::TextEditor::backgroundColourId, inputBackground());
    setColour(juce::TextEditor::textColourId, textPrimary());
    setColour(juce::TextEditor::highlightColourId, accentBlue());
    setColour(juce::TextEditor::highlightedTextColourId, textOn(accentBlue()));
    setColour(juce::TextEditor::outlineColourId, borderSubtle());
    setColour(juce::TextEditor::focusedOutlineColourId, accentBlue());

    setColour(juce::ListBox::backgroundColourId, bgDeep());
    setColour(juce::ListBox::textColourId, textPrimary());
    setColour(juce::ListBox::outlineColourId, borderSubtle());

    setColour(juce::PropertyComponent::backgroundColourId, bgSurface());
    setColour(juce::PropertyComponent::labelTextColourId, textPrimary());

    setColour(juce::ComboBox::backgroundColourId, inputBackground());
    setColour(juce::ComboBox::textColourId, textPrimary());
    setColour(juce::ComboBox::outlineColourId, borderSubtle());
    setColour(juce::ComboBox::arrowColourId, textSecondary());

    setColour(juce::TextButton::buttonColourId, buttonBackground());
    setColour(juce::TextButton::textColourOffId, textPrimary());
    setColour(juce::TextButton::textColourOnId, textOn(accentBlue()));

    setColour(juce::ToggleButton::textColourId, textPrimary());
    setColour(juce::ToggleButton::tickColourId, accentCyan());
    setColour(juce::ToggleButton::tickDisabledColourId, borderBright());

    setColour(juce::Label::textColourId, textPrimary());
    setColour(juce::Slider::textBoxTextColourId, textPrimary());
    setColour(juce::Slider::rotarySliderFillColourId, accentBlue());

    setColour(juce::ScrollBar::thumbColourId, borderBright());
    setColour(juce::ScrollBar::backgroundColourId, bgDeep().withAlpha(0.5f));

    setColour(juce::TabbedButtonBar::tabTextColourId, textPrimary());
    setColour(juce::TabbedButtonBar::frontTextColourId, textPrimary());
    setColour(juce::TabbedComponent::backgroundColourId, bgPanel());
}

//==============================================================================
CubaseTheme::CubaseTheme()
{
    themeInstances.addIfNotAlreadyThere(this);
    loadThemeConfig();
    setDefaultSansSerifTypefaceName("Microsoft YaHei");
    updateLookAndFeelColours();
}

CubaseTheme::~CubaseTheme()
{
    themeInstances.removeFirstMatchingValue(this);
}

void CubaseTheme::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                      bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto baseColour = backgroundColour;

    if (button.getToggleState())
        baseColour = accentBlue();
    else if (shouldDrawButtonAsDown)
        baseColour = baseColour.brighter(0.2f);
    else if (shouldDrawButtonAsHighlighted)
        baseColour = baseColour.brighter(0.08f);

    g.setColour(baseColour);
    g.fillRoundedRectangle(bounds, 3.0f);

    g.setColour(button.getToggleState() ? accentBlue().brighter(0.2f) : borderSubtle());
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
}

void CubaseTheme::drawComboBox(juce::Graphics& g, int width, int height, bool,
                              int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, 3.0f);

    g.setColour(box.hasKeyboardFocus(false) ? accentBlue() : borderSubtle());
    g.drawRoundedRectangle(bounds, 3.0f, 1.0f);

    // Draw dropdown arrow
    juce::Path arrow;
    auto arrowZone = juce::Rectangle<float>((float)width - 18.0f, 0.0f, 14.0f, (float)height);
    float midY = arrowZone.getCentreY();
    float midX = arrowZone.getCentreX();
    arrow.startNewSubPath(midX - 3.5f, midY - 2.0f);
    arrow.lineTo(midX + 3.5f, midY - 2.0f);
    arrow.lineTo(midX, midY + 2.5f);
    arrow.closeSubPath();

    g.setColour(textSecondary());
    g.fillPath(arrow);
}

juce::Label* CubaseTheme::createComboBoxTextBox(juce::ComboBox& box)
{
    juce::ignoreUnused(box);
    auto* l = new juce::Label(juce::String(), juce::String());
    l->setFont(getFont(12.0f));
    l->setColour(juce::Label::textColourId, textPrimary());
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

void CubaseTheme::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(1, 1, juce::jmax(0, box.getWidth() - 22), juce::jmax(0, box.getHeight() - 2));
    label.setFont(getFont(12.0f));
    label.setColour(juce::Label::textColourId, textPrimary());
}

void CubaseTheme::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.setColour(menuBackground());
    g.fillRoundedRectangle(0.0f, 0.0f, (float)width, (float)height, 4.0f);

    g.setColour(borderBright());
    g.drawRoundedRectangle(0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f, 4.0f, 1.0f);
}

void CubaseTheme::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                   bool isActive, bool isHighlighted, bool isTicked, bool,
                                   const juce::String& text, const juce::String& shortcutKeyText,
                                   const juce::Drawable* icon, const juce::Colour* textColourToUse)
{
    if (isSeparator)
    {
        g.setColour(borderSubtle());
        g.fillRect(area.getX(), area.getCentreY(), area.getWidth(), 1);
        return;
    }

    auto r = area.reduced(2);

    if (isHighlighted && isActive)
    {
        g.setColour(accentBlue());
        g.fillRoundedRectangle(r.toFloat(), 3.0f);
    }

    juce::Colour textColour = (textColourToUse != nullptr) ? *textColourToUse
                              : (isHighlighted ? getReadableTextColor(accentBlue())
                                               : (isActive ? textPrimary() : textSecondary().darker(0.3f)));

    // Left accent pill or tick indicator
    if (isTicked)
    {
        // Left vertical indicator pill
        g.setColour(isHighlighted ? textOn(accentBlue()) : accentCyan());
        g.fillRoundedRectangle((float)r.getX() + 2.0f, (float)r.getY() + 4.0f, 3.0f, (float)r.getHeight() - 8.0f, 1.5f);

        // Checkmark badge
        g.setColour(isHighlighted ? textOn(accentBlue()) : accentCyan());
        g.setFont(getFont(12.0f, true));
        g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x93"), r.getX() + 6, r.getY(), 16, r.getHeight(), juce::Justification::centred);
    }

    int leftOffset = 24;
    if (icon != nullptr)
    {
        icon->drawWithin(g, juce::Rectangle<float>((float)r.getX() + (float)leftOffset, (float)r.getY() + ((float)r.getHeight() - 14.0f) * 0.5f, 14.0f, 14.0f),
                         juce::RectanglePlacement::centred, 1.0f);
        leftOffset += 18;
    }

    auto textRect = r;
    textRect.removeFromLeft(leftOffset);
    textRect.removeFromRight(8);

    g.setColour(textColour);
    g.setFont(getFont(12.0f, isTicked));
    g.drawFittedText(text, textRect, juce::Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour(isHighlighted ? getReadableTextColor(accentBlue()).withAlpha(0.85f) : textSecondary());
        g.setFont(getFont(11.0f, false));
        g.drawFittedText(shortcutKeyText, textRect, juce::Justification::centredRight, 1);
    }
}

void CubaseTheme::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                               bool, int thumbStartPosition, int thumbSize,
                               bool isMouseOver, bool isMouseDown)
{
    auto bgArea = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height);
    g.setColour(bgDeep().withAlpha(0.5f));
    g.fillRect(bgArea);

    juce::Rectangle<float> thumbArea;
    if (width < height) // vertical
    {
        thumbArea = juce::Rectangle<float>((float)x + 2.0f, (float)thumbStartPosition, (float)width - 4.0f, (float)thumbSize);
    }
    else // horizontal
    {
        thumbArea = juce::Rectangle<float>((float)thumbStartPosition, (float)y + 2.0f, (float)thumbSize, (float)height - 4.0f);
    }

    auto thumbColour = isMouseDown ? accentBlue() : (isMouseOver ? borderBright().brighter(0.2f) : borderBright());
    g.setColour(thumbColour);
    g.fillRoundedRectangle(thumbArea, 2.5f);
}

void CubaseTheme::drawMenuBarBackground(juce::Graphics& g, int width, int height, bool, juce::MenuBarComponent&)
{
    g.setColour(toolbarBackground());
    g.fillRect(0, 0, width, height);
    g.setColour(borderSubtle());
    g.drawHorizontalLine(height - 1, 0.0f, (float)width);
}

void CubaseTheme::drawMenuBarItem(juce::Graphics& g, int width, int height,
                                 int itemIndex, const juce::String& itemText,
                                 bool isMouseOverItem, bool isMenuOpen, bool isMouseOverBar,
                                 juce::MenuBarComponent& menuBar)
{
    juce::ignoreUnused(itemIndex, isMouseOverBar, menuBar);
    if (isMouseOverItem || isMenuOpen)
    {
        g.setColour(accentBlue());
        g.fillRoundedRectangle(2.0f, 2.0f, (float)width - 4.0f, (float)height - 4.0f, 3.0f);
        g.setColour(textOn(accentBlue()));
    }
    else
    {
        g.setColour(textPrimary());
    }
    g.setFont(getFont(12.0f));
    g.drawFittedText(itemText, 0, 0, width, height, juce::Justification::centred, 1);
}

void CubaseTheme::drawGroupComponentOutline(juce::Graphics& g, int width, int height, const juce::String& text,
                                           const juce::Justification&, juce::GroupComponent& group)
{
    if ((bool)group.getProperties()["inspectorModule"])
    {
        const auto card = juce::Rectangle<float>(0.5f, 0.5f,
                                                  (float)width - 1.0f, (float)height - 1.0f);
        g.setColour(bgPanel());
        g.fillRoundedRectangle(card, 6.0f);
        g.setColour(moduleHeaderBackground());
        g.fillRoundedRectangle(1.0f, 1.0f, (float)width - 2.0f, 28.0f, 5.0f);
        g.setColour(accentCyan());
        g.fillRoundedRectangle(2.0f, 7.0f, 3.0f, 16.0f, 1.5f);
        g.setColour(borderSubtle());
        g.drawRoundedRectangle(card, 6.0f, 1.0f);

        g.setColour(textSecondary());
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                g.fillEllipse(12.0f + col * 5.0f, 8.0f + row * 5.0f, 2.0f, 2.0f);

        g.setColour(textPrimary());
        g.setFont(getFont(11.5f, true));
        g.drawFittedText(text, 27, 2, std::max(0, width - 39), 25,
                         juce::Justification::centredLeft, 1, 0.8f);
        return;
    }

    const float textH = 14.0f;
    const float indent = 12.0f;
    const float textEdgeGap = 4.0f;
    const float cs = 4.0f;

    auto font = getFont(11.5f, true);
    auto textW = juce::GlyphArrangement::getStringWidth(font, text);

    juce::Path p;
    float x = 0.5f;
    float y = textH * 0.5f;
    float w = (float)width - 1.0f;
    float h = (float)height - y - 1.0f;

    p.startNewSubPath(x + textEdgeGap + indent + textW, y);
    p.lineTo(x + w - cs, y);
    p.addArc(x + w - cs * 2.0f, y, cs * 2.0f, cs * 2.0f, 0.0f, juce::MathConstants<float>::halfPi);
    p.lineTo(x + w, y + h - cs);
    p.addArc(x + w - cs * 2.0f, y + h - cs * 2.0f, cs * 2.0f, cs * 2.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi);
    p.lineTo(x + cs, y + h);
    p.addArc(x, y + h - cs * 2.0f, cs * 2.0f, cs * 2.0f, juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 1.5f);
    p.lineTo(x, y + cs);
    p.addArc(x, y, cs * 2.0f, cs * 2.0f, juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::twoPi);
    p.lineTo(x + indent - textEdgeGap, y);

    g.setColour(borderSubtle());
    g.strokePath(p, juce::PathStrokeType(1.0f));

    g.setFont(font);
    g.setColour(textPrimary());
    g.drawText(text, (int)indent, 0, (int)textW + 4, (int)textH, juce::Justification::centredLeft, true);
}

void CubaseTheme::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    juce::Colour bgColour = button.findColour(juce::TextButton::buttonColourId);
    if (bgColour.isTransparent() || bgColour.getAlpha() < 30)
        bgColour = bgPanel();

    if (button.getToggleState())
        bgColour = accentBlue();
    else if (shouldDrawButtonAsDown)
        bgColour = bgColour.brighter(0.2f);
    else if (shouldDrawButtonAsHighlighted)
        bgColour = bgColour.brighter(0.08f);

    juce::Colour textCol = button.isColourSpecified(juce::TextButton::textColourOffId)
                               ? button.findColour(juce::TextButton::textColourOffId)
                               : getReadableTextColor(bgColour);
    if (getContrastRatio(bgColour, textCol) < 4.5f)
        textCol = getReadableTextColor(bgColour);
    if (!button.isEnabled())
        textCol = textCol.withAlpha(0.4f);

    g.setColour(textCol);

    auto text = button.getButtonText();
    auto bounds = button.getLocalBounds();

    // Keep compact controls legible without reducing their available text area.
    if (bounds.getWidth() <= 26 || bounds.getHeight() <= 20)
    {
        g.setFont(getFont(bounds.getHeight() <= 18 ? 10.5f : 11.5f, false));
        g.drawText(text, bounds, juce::Justification::centred, false);
    }
    else
    {
        g.setFont(getFont(bounds.getHeight() < 24 ? 11.5f : 12.0f, false));
        // Use gentle 2px horizontal margin and NEVER force ellipsis truncation on buttons
        g.drawText(text, bounds.reduced(2, 0), juce::Justification::centred, false);
    }
}

void CubaseTheme::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.fillAll (label.findColour (juce::Label::backgroundColourId));

    if (! label.isBeingEdited())
    {
        auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        const auto font = label.getFont();

        auto textColour = label.findColour (juce::Label::textColourId);

        // Determine actual surface/panel color underneath the label
        juce::Colour bg = label.findColour (juce::Label::backgroundColourId);
        if (bg.isTransparent() || bg.getAlpha() < 20)
        {
            if (auto* parent = label.getParentComponent())
            {
                if (parent->isColourSpecified(juce::ComboBox::backgroundColourId))
                    bg = parent->findColour(juce::ComboBox::backgroundColourId);
                else if (parent->isColourSpecified(juce::ListBox::backgroundColourId))
                    bg = parent->findColour(juce::ListBox::backgroundColourId);
                else if (parent->isColourSpecified(juce::TabbedComponent::backgroundColourId))
                    bg = parent->findColour(juce::TabbedComponent::backgroundColourId);
            }
            if (bg.isTransparent() || bg.getAlpha() < 20)
                bg = bgPanel();
        }

        // Only enforce high-contrast fallback if the configured color genuinely collides with the background
        if (getContrastRatio(bg, textColour) < 2.5f)
            textColour = getReadableTextColor(bg);

        g.setColour (textColour.withMultipliedAlpha (alpha));
        g.setFont (font);

        auto textArea = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());

        g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
                          juce::jmax (1, (int) ((float) textArea.getHeight() / font.getHeight())),
                          label.getMinimumHorizontalScale());

        g.setColour (label.findColour (juce::Label::outlineColourId).withMultipliedAlpha (alpha));
    }
    else if (label.isEnabled())
    {
        g.setColour (label.findColour (juce::Label::outlineColourId));
    }

    g.drawRect (label.getLocalBounds());
}

void CubaseTheme::drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics& g)
{
    g.fillAll(bgSurface());
}

void CubaseTheme::drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics& g, int w, int h)
{
    // DO NOT call g.fillAll() here! BehindFrontTabComp sits in front of all inactive tabs
    // and behind the active tab. Calling fillAll() would completely cover all inactive tabs!
    g.setColour(borderSubtle());
    g.drawHorizontalLine(h - 1, 0.0f, (float)w);
}

void CubaseTheme::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown)
{
    const auto area = button.getActiveArea();
    auto bounds = area.toFloat();

    bool isFront = button.isFrontTab();
    juce::Colour bgCol = isFront ? bgPanel() 
                                 : (isMouseOver ? bgSurface().brighter(0.08f) 
                                                : (isDarkTheme() ? bgSurface().darker(0.12f) : bgSurface().darker(0.05f)));

    g.setColour(bgCol);
    g.fillRect(bounds);

    if (isFront)
    {
        // Top accent highlight line for the active front tab
        g.setColour(accentCyan());
        g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 3.0f);

        // Border left & right
        g.setColour(borderSubtle());
        g.drawVerticalLine((int)bounds.getX(), bounds.getY(), bounds.getBottom());
        g.drawVerticalLine((int)bounds.getRight() - 1, bounds.getY(), bounds.getBottom());
    }
    else
    {
        // Inactive tab: subtle vertical separator on the right
        g.setColour(borderSubtle());
        g.drawVerticalLine((int)bounds.getRight() - 1, bounds.getY() + 3.0f, bounds.getBottom() - 3.0f);

        // Bottom line connecting to the rest of the bar
        g.drawHorizontalLine((int)bounds.getBottom() - 1, bounds.getX(), bounds.getRight());
    }

    drawTabButtonText(button, g, isMouseOver, isMouseDown);
}

void CubaseTheme::drawTabButtonText (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool)
{
    auto area = button.getTextArea();
    if (area.isEmpty()) area = button.getActiveArea();

    bool isFront = button.isFrontTab();
    juce::Colour bgCol = isFront ? bgPanel() 
                                 : (isMouseOver ? bgSurface().brighter(0.08f) 
                                                : (isDarkTheme() ? bgSurface().darker(0.12f) : bgSurface().darker(0.05f)));

    juce::Colour textCol = isFront ? textPrimary() : (isMouseOver ? textPrimary() : textSecondary());

    if (getContrastRatio(bgCol, textCol) < 2.5f)
        textCol = getReadableTextColor(bgCol);

    g.setFont(getFont(12.5f, isFront));
    g.setColour(textCol);
    g.drawText(button.getButtonText().trim(), area, juce::Justification::centred, false);
}

static const char* getLucideSVG(DAWIcon icon)
{
    switch (icon)
    {
        case DAWIcon::Select:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m3 3 7.07 16.97 2.51-7.39 7.39-2.51L3 3z"/><path d="m13 13 6 6"/></svg>)";

        case DAWIcon::Split:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="6" cy="6" r="3"/><path d="M8.12 8.12 12 12"/><path d="M20 4 8.12 15.88"/><circle cx="6" cy="18" r="3"/><path d="M14.8 14.8 20 20"/></svg>)";

        case DAWIcon::Erase:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m7 21-4.3-4.3c-1-1-1-2.5 0-3.4l9.6-9.6c1-1 2.5-1 3.4 0l5.6 5.6c1 1 1 2.5 0 3.4L13 21"/><path d="M22 21H7"/><path d="m5 11 9 9"/></svg>)";

        case DAWIcon::Draw:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17 3a2.85 2.83 0 1 1 4 4L7.5 20.5 2 22l1.5-5.5Z"/><path d="m15 5 4 4"/></svg>)";

        case DAWIcon::Snap:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 4v8a7 7 0 0 0 14 0V4"/><path d="M5 9h4"/><path d="M15 9h4"/></svg>)";

        case DAWIcon::Rewind:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="white" stroke="white" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><polygon points="19 20 9 12 19 4 19 20"/><line x1="5" x2="5" y1="19" y2="5" stroke-width="2.5"/></svg>)";

        case DAWIcon::Play:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="white" stroke="none"><polygon points="6 3 20 12 6 21 6 3"/></svg>)";

        case DAWIcon::Pause:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="white" stroke="none"><rect width="4" height="16" x="6" y="4" rx="1"/><rect width="4" height="16" x="14" y="4" rx="1"/></svg>)";

        case DAWIcon::Stop:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="white" stroke="none"><rect width="14" height="14" x="5" y="5" rx="2"/></svg>)";

        case DAWIcon::Loop:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m17 2 4 4-4 4"/><path d="M3 11v-1a4 4 0 0 1 4-4h14"/><path d="m7 22-4-4 4-4"/><path d="M21 13v1a4 4 0 0 1-4 4H3"/></svg>)";

        case DAWIcon::ViewArranger:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect width="18" height="18" x="3" y="3" rx="2"/><path d="M7 8h10"/><path d="M7 12h7"/><path d="M7 16h10"/></svg>)";

        case DAWIcon::ViewPianoRoll:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect width="18" height="18" x="3" y="3" rx="2"/><path d="M8 3v18"/><path d="M13 8v4"/><path d="M17 14v4"/></svg>)";

        case DAWIcon::ViewDualWindow:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect width="13" height="13" x="8" y="8" rx="2"/><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"/></svg>)";

        case DAWIcon::ViewSplit:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect width="18" height="18" x="3" y="3" rx="2"/><path d="M3 12h18"/></svg>)";

        case DAWIcon::LeftZone:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect width="18" height="18" x="3" y="3" rx="2"/><path d="M9 3v18"/></svg>)";

        case DAWIcon::RightZone:
            return R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect width="18" height="18" x="3" y="3" rx="2"/><path d="M15 3v18"/></svg>)";

        default:
            return nullptr;
    }
}

void CubaseTheme::drawDAWIcon(juce::Graphics& g, DAWIcon icon, juce::Rectangle<float> bounds, juce::Colour colour, bool)
{
    const char* svgStr = getLucideSVG(icon);
    if (svgStr == nullptr)
        return;

    auto drawable = juce::Drawable::createFromSVGString(svgStr);
    if (drawable != nullptr)
    {
        drawable->replaceColour(juce::Colours::white, colour);
        drawable->drawWithin(g, bounds, juce::RectanglePlacement::centred, 1.0f);
    }
}

//==============================================================================
// DAWIconButton Implementation
//==============================================================================
DAWIconButton::DAWIconButton(DAWIcon icon, const juce::String& tooltipText)
    : juce::Button(tooltipText), currentIcon(icon)
{
    if (tooltipText.isNotEmpty())
        setTooltip(tooltipText);
}

void DAWIconButton::setDAWIcon(DAWIcon newIcon)
{
    if (currentIcon != newIcon)
    {
        currentIcon = newIcon;
        repaint();
    }
}

void DAWIconButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    if (isButtonDown)
        bounds = bounds.translated(0.0f, 0.5f);

    bool isToggled = getToggleState();
    juce::Colour bgCol = CubaseTheme::bgSurface();
    juce::Colour borderCol = CubaseTheme::borderSubtle();
    juce::Colour iconCol = CubaseTheme::textPrimary();

    if (currentIcon == DAWIcon::Pause || (currentIcon == DAWIcon::Play && isToggled))
    {
        bgCol = CubaseTheme::accentGreen().withAlpha(0.18f);
        borderCol = CubaseTheme::accentGreen();
        iconCol = CubaseTheme::accentGreen();
    }
    else if (currentIcon == DAWIcon::Loop && isToggled)
    {
        bgCol = CubaseTheme::accentCyan().withAlpha(0.18f);
        borderCol = CubaseTheme::accentCyan();
        iconCol = CubaseTheme::accentCyan();
    }
    else if (isToggled)
    {
        bgCol = CubaseTheme::accentCyan().withAlpha(0.20f);
        borderCol = CubaseTheme::accentCyan();
        iconCol = CubaseTheme::accentCyan().brighter(0.35f);
    }
    else if (isButtonDown)
    {
        bgCol = bgCol.brighter(0.10f);
    }
    else if (isMouseOver)
    {
        bgCol = bgCol.brighter(0.05f);
        borderCol = CubaseTheme::borderBright();
    }

    float corner = 3.5f;
    g.setColour(bgCol);
    g.fillRoundedRectangle(bounds, corner);

    g.setColour(borderCol);
    g.drawRoundedRectangle(bounds, corner, 1.0f);

    auto iconArea = bounds.reduced(bounds.getWidth() * 0.20f, bounds.getHeight() * 0.20f);
    CubaseTheme::drawDAWIcon(g, currentIcon, iconArea, iconCol, isToggled);
}

} // namespace Harmonizer



