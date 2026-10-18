#include "FloatingToolbox.h"
#include "CubaseTheme.h"
#include "../core/Localization.h"

namespace Harmonizer
{

//==============================================================================
// ToolboxIconButton Implementation (Pure Minimalist Lucide Icon)
//==============================================================================
FloatingToolbox::ToolboxIconButton::ToolboxIconButton(DAWIcon icon, const juce::String& tooltipText)
    : juce::Button(tooltipText), iconType(icon)
{
    if (tooltipText.isNotEmpty())
        setTooltip(tooltipText);
}

void FloatingToolbox::ToolboxIconButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    if (isButtonDown)
        bounds = bounds.translated(0.0f, 0.5f);

    bool isToggled = getToggleState();
    juce::Colour bgCol = juce::Colours::transparentBlack;
    juce::Colour borderCol = juce::Colours::transparentBlack;
    juce::Colour iconCol = CubaseTheme::textSecondary();

    if (isToggled)
    {
        bgCol = CubaseTheme::accentCyan().withAlpha(0.18f);
        borderCol = CubaseTheme::accentCyan();
        iconCol = CubaseTheme::accentCyan();
    }
    else if (isButtonDown)
    {
        bgCol = CubaseTheme::bgSurface().brighter(0.12f);
        iconCol = CubaseTheme::textPrimary();
    }
    else if (isMouseOver)
    {
        bgCol = CubaseTheme::bgSurface().brighter(0.06f);
        iconCol = CubaseTheme::textPrimary();
    }

    if (!bgCol.isTransparent())
    {
        g.setColour(bgCol);
        g.fillRoundedRectangle(bounds, 3.0f);
    }

    if (!borderCol.isTransparent())
    {
        g.setColour(borderCol);
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    }

    auto iconArea = bounds.reduced(bounds.getWidth() * 0.18f, bounds.getHeight() * 0.18f);
    CubaseTheme::drawDAWIcon(g, iconType, iconArea, iconCol, isToggled);
}

//==============================================================================
// ToolboxSnapButton Implementation (Pure Minimalist Magnet)
//==============================================================================
FloatingToolbox::ToolboxSnapButton::ToolboxSnapButton()
    : juce::Button("Snap")
{
    setTooltip(juce::String::fromUTF8(u8"时间网格吸附开关 (Snap to Grid) [J]"));
}

void FloatingToolbox::ToolboxSnapButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    if (isButtonDown)
        bounds = bounds.translated(0.0f, 0.5f);

    bool isToggled = getToggleState();
    juce::Colour bgCol = juce::Colours::transparentBlack;
    juce::Colour borderCol = juce::Colours::transparentBlack;
    juce::Colour contentCol = CubaseTheme::textSecondary();

    if (isToggled)
    {
        bgCol = CubaseTheme::accentCyan().withAlpha(0.18f);
        borderCol = CubaseTheme::accentCyan();
        contentCol = CubaseTheme::accentCyan();
    }
    else if (isButtonDown)
    {
        bgCol = CubaseTheme::bgSurface().brighter(0.12f);
        contentCol = CubaseTheme::textPrimary();
    }
    else if (isMouseOver)
    {
        bgCol = CubaseTheme::bgSurface().brighter(0.06f);
        contentCol = CubaseTheme::textPrimary();
    }

    if (!bgCol.isTransparent())
    {
        g.setColour(bgCol);
        g.fillRoundedRectangle(bounds, 3.0f);
    }

    if (!borderCol.isTransparent())
    {
        g.setColour(borderCol);
        g.drawRoundedRectangle(bounds, 3.0f, 1.0f);
    }

    auto inner = bounds.reduced(6.0f, 3.0f);
    auto iconArea = inner.removeFromLeft(16.0f);
    inner.removeFromLeft(4.0f);

    CubaseTheme::drawDAWIcon(g, DAWIcon::Snap, iconArea, contentCol, isToggled);

    g.setFont(CubaseTheme::getFont(12.0f, isToggled));
    g.setColour(isToggled ? CubaseTheme::accentCyan() : contentCol);
    g.drawText(juce::String::fromUTF8(u8"吸附"), inner, juce::Justification::centredLeft, false);
}

//==============================================================================
// FloatingToolbox Implementation
//==============================================================================
FloatingToolbox::FloatingToolbox(MidiModel& model, std::function<void()> onToolSelected)
    : juce::PopupMenu::CustomComponent(false), midiModel(model), toolSelectedCallback(onToolSelected)
{
    auto setupBtn = [this](ToolboxIconButton& b, ToolType t) {
        b.setToggleState(midiModel.getCurrentTool() == t, juce::dontSendNotification);
        b.onClick = [this, t]() { selectTool(t); };
        addAndMakeVisible(b);
    };

    setupBtn(btnSelect, ToolType::Select);
    setupBtn(btnSplit,  ToolType::Split);
    setupBtn(btnErase,  ToolType::Erase);
    setupBtn(btnDraw,   ToolType::Draw);

    btnSnap.setClickingTogglesState(true);
    btnSnap.setToggleState(midiModel.isSnapEnabled(), juce::dontSendNotification);
    btnSnap.onClick = [this]() {
        midiModel.setSnapEnabled(btnSnap.getToggleState());
    };
    addAndMakeVisible(btnSnap);

    setSize(260, 36);
}

void FloatingToolbox::selectTool(ToolType t)
{
    midiModel.setCurrentTool(t);
    if (toolSelectedCallback)
        toolSelectedCallback();
}

void FloatingToolbox::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // 纯粹平整的暗色极简面板
    g.setColour(CubaseTheme::bgPanel());
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(CubaseTheme::borderSubtle());
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
}

void FloatingToolbox::resized()
{
    auto area = getLocalBounds().reduced(4, 4);
    int btnW = 34;
    int gap = 3;

    btnSelect.setBounds(area.removeFromLeft(btnW)); area.removeFromLeft(gap);
    btnSplit.setBounds(area.removeFromLeft(btnW));  area.removeFromLeft(gap);
    btnErase.setBounds(area.removeFromLeft(btnW));  area.removeFromLeft(gap);
    btnDraw.setBounds(area.removeFromLeft(btnW));   area.removeFromLeft(gap * 2);

    btnSnap.setBounds(area);
}

void FloatingToolbox::showAt(MidiModel& model, juce::Component* parent, juce::Point<int> screenPos)
{
    auto toolbox = std::make_unique<FloatingToolbox>(model, []() {
        juce::PopupMenu::dismissAllActiveMenus();
    });

    juce::PopupMenu menu;
    menu.addCustomItem(100, std::move(toolbox));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)));
}

} // namespace Harmonizer
