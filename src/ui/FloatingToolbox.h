#pragma once

#include "CubaseTheme.h"
#include "../core/MidiModel.h"

namespace Harmonizer
{

class FloatingToolbox : public juce::PopupMenu::CustomComponent
{
public:
    FloatingToolbox(MidiModel& model, std::function<void()> onToolSelected);
    ~FloatingToolbox() override = default;

    void getIdealSize(int& idealWidth, int& idealHeight) override { idealWidth = 260; idealHeight = 36; }

    void paint(juce::Graphics& g) override;
    void resized() override;

    static void showAt(MidiModel& model, juce::Component* parent, juce::Point<int> screenPos);

    // Minimalist Vector Icon Tool Button
    class ToolboxIconButton : public juce::Button
    {
    public:
        ToolboxIconButton(DAWIcon icon, const juce::String& tooltipText);
        void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;
    private:
        DAWIcon iconType;
    };

    // Magnetic Snap Toggle Button
    class ToolboxSnapButton : public juce::Button
    {
    public:
        ToolboxSnapButton();
        void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;
    };

private:
    MidiModel& midiModel;
    std::function<void()> toolSelectedCallback;

    ToolboxIconButton btnSelect { DAWIcon::Select, juce::String::fromUTF8(u8"选择工具 (Select) [1]") };
    ToolboxIconButton btnSplit  { DAWIcon::Split,  juce::String::fromUTF8(u8"剪刀工具 (Split) [3]") };
    ToolboxIconButton btnErase  { DAWIcon::Erase,  juce::String::fromUTF8(u8"橡皮擦 (Erase) [5]") };
    ToolboxIconButton btnDraw   { DAWIcon::Draw,   juce::String::fromUTF8(u8"铅笔工具 (Draw) [8]") };
    ToolboxSnapButton btnSnap;


    void selectTool(ToolType t);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FloatingToolbox)
};


} // namespace Harmonizer
