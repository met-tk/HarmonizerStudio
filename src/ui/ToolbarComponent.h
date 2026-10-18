#pragma once

#include "../core/MidiModel.h"
#include "../core/TrackModel.h"
#include "../core/Localization.h"
#include "CubaseTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>

namespace Harmonizer
{

class ToolbarComponent : public juce::Component,
                         public MidiModel::Listener,
                         public Localization::Listener,
                         public CubaseTheme::Listener
{
public:
    ToolbarComponent(MidiModel& model, juce::UndoManager& undoManager,
                     std::function<void()> onToggleView,
                     std::function<void(juce::Component*)> onShowSectionsMenu,
                     std::function<void()> onToggleLeftZone = nullptr,
                     std::function<void()> onToggleRightZone = nullptr);
    ~ToolbarComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void toolChanged() override;
    void projectSettingsChanged() override;
    void languageChanged() override;
    void themeChanged() override;

    void setViewMode(int layout);
    void setZoneStates(bool leftVisible, bool rightVisible);
    void setTrackModel(TrackModel* tm) { trackModel = tm; }

private:
    TrackModel* trackModel = nullptr;
    MidiModel& midiModel;
    juce::UndoManager& undoManager;
    std::function<void()> toggleViewCallback;
    std::function<void(juce::Component*)> showSectionsMenuCallback;
    std::function<void()> toggleLeftZoneCallback;
    std::function<void()> toggleRightZoneCallback;

    int currentViewLayout = 0;

    // View Switcher (Dynamic Vector Icon / Text / Dropdown indicator)
    class ViewSwitcherButton : public juce::Button
    {
    public:
        ViewSwitcherButton();
        void setViewMode(int layout);
        void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;
    private:
        int currentLayout = 0;
    };

    ViewSwitcherButton btnToggleView;

public:
    class VectorIconButton : public juce::Button
    {
    public:
        VectorIconButton(DAWIcon iconType, const juce::String& tooltipText = {});
        void setIconType(DAWIcon newType);
        void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;
    private:
        DAWIcon type;
    };

private:
    // Cubase Editing Tools with Monochrome Vector Icons (Independent Regular Buttons)
    VectorIconButton btnSelect { DAWIcon::Select, juce::String::fromUTF8(u8"1: 选择工具 (Select Tool) [1]") };
    VectorIconButton btnSplit  { DAWIcon::Split,  juce::String::fromUTF8(u8"3: 剪刀工具 (Split Tool) [3]") };
    VectorIconButton btnErase  { DAWIcon::Erase,  juce::String::fromUTF8(u8"5: 橡皮擦 (Erase Tool) [5]") };
    VectorIconButton btnDraw   { DAWIcon::Draw,   juce::String::fromUTF8(u8"8: 铅笔工具 (Draw Tool) [8]") };

    // Snap & Quantize
    juce::ToggleButton btnSnapToggle { "Snap [J]" };
    juce::ComboBox snapCombo;
    juce::ToggleButton btnTriplet { "Triplet" };

    // Window Layout Controls (Cubase-style Left & Right Zone toggles with vector icons)
    VectorIconButton btnToggleLeftZone  { DAWIcon::LeftZone,  "Toggle Left Inspector Zone" };
    VectorIconButton btnToggleRightZone { DAWIcon::RightZone, "Toggle Right Inspector Zone" };

    // Inspector layout / sections button
    juce::TextButton btnInspectorSections { "Sections" };

    // Multi-track visibility in Piano Roll
    juce::TextButton btnVisibleTracks { juce::String::fromUTF8(u8"显示轨道") };
    void showVisibleTracksMenu();

    // Note Coloring Mode
    juce::ComboBox coloringModeCombo;

    // Follow Playhead Mode (Off, Page, ContinuousLeft, ContinuousCenter)
    juce::ComboBox followPlayheadCombo;

    void updateToolHighlights();
    void updateViewButtonDisplay();
    void updateSnapCombo();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToolbarComponent)
};

} // namespace Harmonizer
