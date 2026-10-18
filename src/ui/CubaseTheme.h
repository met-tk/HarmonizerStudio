#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace Harmonizer
{

enum class ThemePreset
{
    CubaseClassicDark = 0,
    ModernStudioSlate = 1,
    AbletonMinimalist = 2,
    WarmSpaceGrey = 3,
    ModernStudioLight = 4,
    CubaseClassicLight = 5,
    Custom = 6
};

struct ThemePalette
{
    juce::String name = "Cubase Classic Dark";
    juce::Colour bgDeep          { 0xff18191c };
    juce::Colour bgSurface       { 0xff232428 };
    juce::Colour bgPanel         { 0xff2b2d33 };
    juce::Colour toolbarBackground { 0xff252a31 };
    juce::Colour sidebarBackground { 0xff22272e };
    juce::Colour moduleHeaderBackground { 0xff252b33 };
    juce::Colour buttonBackground { 0xff303741 };
    juce::Colour inputBackground { 0xff1f252c };
    juce::Colour menuBackground { 0xff2f353d };
    juce::Colour borderSubtle    { 0xff36383f };
    juce::Colour borderBright    { 0xff4a4e58 };

    juce::Colour accentBlue      { 0xff2575fc };
    juce::Colour accentOrange    { 0xfff59e0b };
    juce::Colour accentCyan      { 0xff06b6d4 };
    juce::Colour accentGreen     { 0xff10b981 };
    juce::Colour accentPurple    { 0xffa855f7 };
    juce::Colour noteGlow        { 0xff38bdf8 };
    juce::Colour textPrimary     { 0xffeceff4 };
    juce::Colour textSecondary   { 0xff8f95a0 };

    juce::Colour gridBarLine     { 0xff4b515d };
    juce::Colour gridBeatLine    { 0xff333740 };
    juce::Colour gridSubBeat     { 0xff24272d };
    juce::Colour gridWhiteKeyRow { 0xff212328 };
    juce::Colour gridBlackKeyRow { 0xff1b1c20 };
    juce::Colour pianoRollTonicTint { 0x35eab308 };

    juce::Colour locatorHandle        { 0xff2575fc }; // 标志定位滑块把手 [L] [R]
    juce::Colour locatorRangeOverlay { 0x222575fc }; // 轨道与钢琴窗范围选区颜色
};

struct SavedThemePreset
{
    int id = 0;
    juce::String name;
};

enum class DAWIcon

{
    Select,
    Split,
    Erase,
    Draw,
    Snap,
    Rewind,
    Play,
    Pause,
    Stop,
    Loop,
    ViewArranger,
    ViewPianoRoll,
    ViewDualWindow,
    ViewSplit,
    LeftZone,
    RightZone
};

class CubaseTheme : public juce::LookAndFeel_V4
{
public:
    static void drawDAWIcon(juce::Graphics& g, DAWIcon icon, juce::Rectangle<float> bounds, juce::Colour colour, bool active = false);

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void themeChanged() = 0;
    };

    CubaseTheme();
    ~CubaseTheme() override;

    static ThemePalette& getPalette();
    static void setPreset(ThemePreset preset);
    static ThemePreset getCurrentPreset();
    static ThemePreset getLastNamedPreset();
    static void applyCustomColor(const juce::String& colorKey, juce::Colour newColor);
    static std::vector<SavedThemePreset> getSavedPresets();
    static int getActiveSavedPresetId();
    static bool isActiveSavedPresetModified();
    static bool applySavedPreset(int id);
    static int saveAsPreset(const juce::String& name);
    static bool renameSavedPreset(int id, const juce::String& name);
    static bool overwriteSavedPreset(int id);
    static bool deleteSavedPreset(int id);

    static void addThemeListener(Listener* l);
    static void removeThemeListener(Listener* l);
    static void addListener(Listener* l) { addThemeListener(l); }
    static void removeListener(Listener* l) { removeThemeListener(l); }
    static void notifyThemeChanged();

    static bool saveThemeConfig();
    static void loadThemeConfig();

    // Color definitions forwarding dynamically to getPalette()
    static juce::Colour bgDeep()          { return getPalette().bgDeep; }
    static juce::Colour bgSurface()       { return getPalette().bgSurface; }
    static juce::Colour bgPanel()         { return getPalette().bgPanel; }
    static juce::Colour toolbarBackground() { return getPalette().toolbarBackground; }
    static juce::Colour sidebarBackground() { return getPalette().sidebarBackground; }
    static juce::Colour moduleHeaderBackground() { return getPalette().moduleHeaderBackground; }
    static juce::Colour buttonBackground() { return getPalette().buttonBackground; }
    static juce::Colour inputBackground() { return getPalette().inputBackground; }
    static juce::Colour menuBackground() { return getPalette().menuBackground; }
    static juce::Colour borderSubtle()    { return getPalette().borderSubtle; }
    static juce::Colour borderBright()    { return getPalette().borderBright; }

    static juce::Colour accentBlue()      { return getPalette().accentBlue; }
    static juce::Colour accentOrange()    { return getPalette().accentOrange; }
    static juce::Colour accentCyan()      { return getPalette().accentCyan; }
    static juce::Colour accentGreen()     { return getPalette().accentGreen; }
    static juce::Colour accentPurple()    { return getPalette().accentPurple; }
    static juce::Colour noteGlow()        { return getPalette().noteGlow; }
    static juce::Colour accentGold()      { return getPalette().accentOrange; }
    static juce::Colour textPrimary()     { return getPalette().textPrimary; }
    static juce::Colour textSecondary()   { return getPalette().textSecondary; }

    static juce::Colour gridBarLine()     { return getPalette().gridBarLine; }
    static juce::Colour gridBeatLine()    { return getPalette().gridBeatLine; }
    static juce::Colour gridSubBeat()     { return getPalette().gridSubBeat; }
    static juce::Colour gridWhiteKeyRow()    { return getPalette().gridWhiteKeyRow; }
    static juce::Colour gridBlackKeyRow()    { return getPalette().gridBlackKeyRow; }
    static juce::Colour pianoRollTonicTint() { return getPalette().pianoRollTonicTint; }
    static juce::Colour locatorHandle()        { return getPalette().locatorHandle; }
    static juce::Colour locatorRangeOverlay() { return getPalette().locatorRangeOverlay; }

    static bool isDarkTheme()
    {
        return getPalette().bgSurface.getBrightness() < 0.5f;
    }

    static float getRelativeLuminance(juce::Colour c)
    {
        auto toLinear = [](float srgb) -> float {
            return (srgb <= 0.04045f) ? (srgb / 12.92f) : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
        };
        float r = toLinear(c.getFloatRed());
        float g = toLinear(c.getFloatGreen());
        float b = toLinear(c.getFloatBlue());
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    }

    static float getContrastRatio(juce::Colour c1, juce::Colour c2)
    {
        float l1 = getRelativeLuminance(c1);
        float l2 = getRelativeLuminance(c2);
        float lighter = std::max(l1, l2);
        float darker = std::min(l1, l2);
        return (lighter + 0.05f) / (darker + 0.05f);
    }

    static juce::Colour getReadableTextColor(juce::Colour bg,
                                            juce::Colour preferredLight = juce::Colour(0xffffffff),
                                            juce::Colour preferredDark = juce::Colour(0xff111827))
    {
        juce::Colour effectiveBg = bg;
        if (bg.getAlpha() < 255)
            effectiveBg = bgPanel().overlaidWith(bg);

        return getContrastRatio(effectiveBg, preferredLight)
                   >= getContrastRatio(effectiveBg, preferredDark)
                   ? preferredLight : preferredDark;
    }

    static juce::Colour textOn(juce::Colour bg)
    {
        return getReadableTextColor(bg);
    }

    void updateLookAndFeelColours();

    static juce::Font getFont(float height = 12.0f, bool bold = false)
    {
        return juce::Font(juce::FontOptions("Microsoft YaHei", height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // LookAndFeel overrides
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Label* createComboBoxTextBox(juce::ComboBox& box) override;
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                           bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;

    void drawMenuBarBackground(juce::Graphics& g, int width, int height, bool isMouseOverBar, juce::MenuBarComponent& menuBar) override;
    void drawMenuBarItem(juce::Graphics& g, int width, int height,
                         int itemIndex, const juce::String& itemText,
                         bool isMouseOverItem, bool isMenuOpen, bool isMouseOverBar,
                         juce::MenuBarComponent& menuBar) override;
    void drawGroupComponentOutline(juce::Graphics& g, int width, int height, const juce::String& text,
                                   const juce::Justification& position, juce::GroupComponent& group) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& scrollbar, int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override;

    void drawLabel (juce::Graphics& g, juce::Label& label) override;
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g) override;
    void drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown) override;
    void drawTabButtonText (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown) override;
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar& bar, juce::Graphics& g, int w, int h) override;
};

int runThemePresetSelfTests();

//==============================================================================
/** Professional DAW Vector Icon Button with state glow and Lucide SVG icons */
class DAWIconButton : public juce::Button
{
public:
    DAWIconButton(DAWIcon icon, const juce::String& tooltipText = {});
    void setDAWIcon(DAWIcon newIcon);
    DAWIcon getDAWIcon() const { return currentIcon; }
    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;
private:
    DAWIcon currentIcon;
};

} // namespace Harmonizer
