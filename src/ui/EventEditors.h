#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../core/TrackModel.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace Harmonizer
{

//==============================================================================
// 1. Customized Chord Block Editor Dialog
//==============================================================================
class ChordEditorDialog : public juce::Component
{
public:
    ChordEditorDialog(const ChordEvent& initialChord, std::function<void(const juce::String& chordName, const juce::String& roman)> onConfirm);
    ~ChordEditorDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static void showModal(const ChordEvent& chord, std::function<void(const juce::String&, const juce::String&)> onConfirm);

private:
    std::function<void(const juce::String&, const juce::String&)> confirmCallback;

    juce::Label lblPreview;
    juce::ComboBox rootCombo;
    juce::ComboBox qualityCombo;

    // Multi-select Tension Toggles
    juce::ToggleButton btnTension_9 { "9" };
    juce::ToggleButton btnTension_b9 { "b9" };
    juce::ToggleButton btnTension_sharp9 { "#9" };
    juce::ToggleButton btnTension_11 { "11" };
    juce::ToggleButton btnTension_sharp11 { "#11" };
    juce::ToggleButton btnTension_13 { "13" };
    juce::ToggleButton btnTension_b13 { "b13" };
    juce::ToggleButton btnTension_alt { "alt" };

    juce::ComboBox bassInversionCombo;
    juce::TextEditor romanEditor;

    juce::TextButton btnOk { "OK" };
    juce::TextButton btnCancel { "Cancel" };

    void updatePreview();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordEditorDialog)
};

//==============================================================================
// Scrollable ComboBox for rapid wheel navigation
//==============================================================================
class ScrollableComboBox : public juce::ComboBox
{
public:
    using juce::ComboBox::ComboBox;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY != 0.0f)
        {
            int numItems = getNumItems();
            if (numItems <= 0) return;
            int curIdx = getSelectedItemIndex();
            if (curIdx < 0) curIdx = 0;

            if (wheel.deltaY > 0.0f)
            {
                if (curIdx > 0)
                    setSelectedItemIndex(curIdx - 1, juce::sendNotificationSync);
            }
            else
            {
                if (curIdx < numItems - 1)
                    setSelectedItemIndex(curIdx + 1, juce::sendNotificationSync);
            }
            return;
        }
        juce::ComboBox::mouseWheelMove(e, wheel);
    }
};

//==============================================================================
// 2. Customized Mode Block Editor Dialog
//==============================================================================
class ModeEditorDialog : public juce::Component
{
public:
    using RealtimeChangeFn = std::function<void(const juce::String& name, const juce::String& tonic, int tonicPc, const juce::String& scaleName, juce::Colour color)>;
    using CancelFn = std::function<void()>;
    using FinishFn = std::function<void()>;

    ModeEditorDialog(const ModeEvent& initialMode,
                     RealtimeChangeFn onRealtimeChange,
                     CancelFn onCancel = nullptr,
                     FinishFn onFinish = nullptr);
    ~ModeEditorDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    static void showModal(const ModeEvent& mode,
                          RealtimeChangeFn onRealtimeChange,
                          CancelFn onCancel = nullptr,
                          FinishFn onFinish = nullptr);

private:
    void applyRealtime();

    RealtimeChangeFn realtimeCallback;
    CancelFn cancelCallback;
    FinishFn finishCallback;
    bool isCancelled = false;

    juce::TextEditor nameEditor;
    ScrollableComboBox tonicCombo;
    ScrollableComboBox scaleCombo;
    juce::ComboBox colorCombo;

    juce::TextButton btnCancel { "Cancel" };
    juce::TextButton btnOk { "OK" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModeEditorDialog)
};

//==============================================================================
// 3. Timeline Marker Editor Dialog
//==============================================================================
class MarkerEditorDialog : public juce::Component
{
public:
    MarkerEditorDialog(const MarkerEvent& initialMarker, std::function<void(const juce::String& name, juce::Colour color)> onConfirm);
    ~MarkerEditorDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static void showModal(const MarkerEvent& marker, std::function<void(const juce::String&, juce::Colour)> onConfirm);

private:
    std::function<void(const juce::String&, juce::Colour)> confirmCallback;

    juce::TextEditor nameEditor;
    juce::ComboBox colorCombo;

    juce::TextButton btnOk { "OK" };
    juce::TextButton btnCancel { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkerEditorDialog)
};

//==============================================================================
// 4. Time Signature Editor Dialog
//==============================================================================
class TimeSigEditorDialog : public juce::Component
{
public:
    TimeSigEditorDialog(const TimeSignatureEvent& initialSig, std::function<void(int numerator, int denominator)> onConfirm);
    ~TimeSigEditorDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static void showModal(const TimeSignatureEvent& sig, std::function<void(int, int)> onConfirm);

private:
    std::function<void(int, int)> confirmCallback;

    juce::ComboBox numCombo;
    juce::ComboBox denCombo;

    juce::TextButton btnOk { "OK" };
    juce::TextButton btnCancel { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimeSigEditorDialog)
};

//==============================================================================
// 4b. Tempo Event Editor Dialog
//==============================================================================
class TempoEditorDialog : public juce::Component
{
public:
    TempoEditorDialog(const TempoEvent& initialTempo, std::function<void(double bpm, TempoTransitionType transition)> onConfirm);
    ~TempoEditorDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static void showModal(const TempoEvent& tempo, std::function<void(double, TempoTransitionType)> onConfirm);

private:
    std::function<void(double, TempoTransitionType)> confirmCallback;

    juce::Slider bpmSlider;
    juce::ComboBox transitionCombo;

    juce::TextButton btnOk { "OK" };
    juce::TextButton btnCancel { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TempoEditorDialog)
};

//==============================================================================
// 5. Sections Manager Dialog (Dual-Docking & Visibility)
//==============================================================================
enum class SectionDockPosition
{
    Left = 0,
    Right = 1,
    Hidden = 2
};

struct SectionDockItem
{
    int id = 0;
    juce::String name;
    SectionDockPosition position = SectionDockPosition::Left;
};

class SectionsManagerDialog : public juce::Component,
                              public juce::DragAndDropContainer
{
public:
    struct ManagerData
    {
        int initialContextIndex = 0;
        bool leftZoneVisible = true;
        bool rightZoneVisible = true;
        bool detachedZoneControls = false;
        std::vector<SectionDockItem> keyEditorItems;
        std::vector<SectionDockItem> instTrackItems;
        std::vector<SectionDockItem> chordTrackItems;
        std::vector<SectionDockItem> modeTrackItems;
        std::vector<SectionDockItem> markerTrackItems;
        std::vector<SectionDockItem> timeSigTrackItems;
        std::vector<SectionDockItem> folderTrackItems;
        std::vector<SectionDockItem> audioTrackItems;
    };

    SectionsManagerDialog(const ManagerData& initialData,
                          std::function<void(const ManagerData&)> onApply);
    ~SectionsManagerDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static void showModal(const ManagerData& initialData,
                          std::function<void(const ManagerData&)> onApply,
                          std::function<void()> onSettingsApplied = nullptr);

    void setOnSettingsApplied(std::function<void()> cb) { onSettingsApplied = std::move(cb); }

private:
    std::function<void(const ManagerData&)> applyCallback;
    std::function<void()> onSettingsApplied;
    ManagerData data;
    int currentContextIndex = 0; // 0: Piano Roll, 1: Instrument, 2..7: individual track types

    std::array<std::unique_ptr<juce::TextButton>, 8> contextButtons;
    juce::ToggleButton chkLeftZone;
    juce::ToggleButton chkRightZone;

    class ModuleCard;
    class DockLane;
    std::array<std::unique_ptr<DockLane>, 3> lanes;
    std::vector<std::unique_ptr<ModuleCard>> cards;

    std::vector<SectionDockItem>& getItemsForContext(int idx);
    const std::vector<SectionDockItem>& getItemsForContext(int idx) const;
    void loadContextToUi(int contextIdx);
    void updateContextButtons();
    void openModuleSettings(int itemId);
    void moveItemToLane(int itemId, SectionDockPosition newPosition, int insertionIndex);
    int cardCountInLane(SectionDockPosition position, int excludingItemId = -1) const;
    void syncSharedModuleOrder();

    juce::TextButton btnReset;
    juce::TextButton btnOk;
    juce::TextButton btnCancel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SectionsManagerDialog)
};

//==============================================================================
// 5b. Note Inspector Properties Dialog
//==============================================================================
class NoteInspectorSettingsDialog : public juce::Component
{
public:
    NoteInspectorSettingsDialog(std::function<void()> onApplied);
    ~NoteInspectorSettingsDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static void showModal(std::function<void()> onApplied);

private:
    std::function<void()> applyCallback;

    juce::GroupComponent grpFields;
    juce::ToggleButton chkPitch;
    juce::ToggleButton chkStart;
    juce::ToggleButton chkLength;
    juce::ToggleButton chkVelocity;
    juce::ToggleButton chkScaleDegree;
    juce::ToggleButton chkFunctionalRole;
    juce::ToggleButton chkMotionTendency;
    juce::ToggleButton chkColorUsage;
    juce::ToggleButton chkInterval;

    juce::Label lblFontSizeTitle;
    juce::Slider sliderFontSize;
    juce::Label lblFontSizeHint;

    juce::Label lblHint;
    juce::TextButton btnResetDefault;
    juce::TextButton btnOk;
    juce::TextButton btnCancel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteInspectorSettingsDialog)
};

//==============================================================================
// 5c. Chord Analysis Properties Dialog
//==============================================================================
class ChordAnalysisSettingsDialog : public juce::Component
{
public:
    ChordAnalysisSettingsDialog(std::function<void()> onApplied);
    ~ChordAnalysisSettingsDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static void showModal(std::function<void()> onApplied);

private:
    std::function<void()> applyCallback;

    juce::Label lblDisplayModeTitle;
    juce::ComboBox comboDisplayMode;

    juce::Label lblOrientationTitle;
    juce::ComboBox comboOrientation;

    juce::Label lblFontSizeTitle;
    juce::Slider sliderFontSize;
    juce::Label lblFontSizeHint;

    juce::Label lblVisualStyleTitle;
    juce::ComboBox comboVisualStyle;

    juce::GroupComponent grpPreview;
    juce::Label lblPreviewChord;
    juce::Label lblPreviewRoman;

    juce::TextButton btnResetDefault;
    juce::TextButton btnOk;
    juce::TextButton btnCancel;

    void updatePreview();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordAnalysisSettingsDialog)
};

class ChordShapeSettingsDialog : public juce::Component
{
public:
    explicit ChordShapeSettingsDialog(std::function<void()> onApplied);
    void paint(juce::Graphics& g) override;
    void resized() override;
    static void showModal(std::function<void()> onApplied);

private:
    std::function<void()> applyCallback;
    juce::ToggleButton showDirections;
    juce::Label explanation;
    juce::TextButton saveButton, cancelButton;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordShapeSettingsDialog)
};

class QuickChordSettingsDialog : public juce::Component
{
public:
    explicit QuickChordSettingsDialog(std::function<void()> onApplied);
    void paint(juce::Graphics& g) override;
    void resized() override;
    static void showModal(std::function<void()> onApplied);

private:
    std::function<void()> applyCallback;
    juce::ToggleButton complexMode;
    juce::ToggleButton preserveTiming;
    juce::Label explanation;
    juce::Label timingExplanation;
    juce::TextButton saveButton, cancelButton;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuickChordSettingsDialog)
};

//==============================================================================
// 6. Humanized Bar Offset Dialog (Stepper & Presets)
//==============================================================================
class BarOffsetDialog : public juce::Component
{
public:
    BarOffsetDialog(int initialOffset, std::function<void(int newOffset)> onConfirm);
    ~BarOffsetDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static void showModal(TrackModel& trackModel);

private:
    std::function<void(int)> confirmCallback;
    int currentOffset = 0;

    juce::Label lblTitle;
    juce::TextButton btnMinus { "-" };
    juce::TextButton btnPlus { "+" };
    juce::Label lblValue;
    juce::Slider sliderOffset;

    juce::TextButton btnPresetStandard; // 0
    juce::TextButton btnPresetPickup;   // -1
    juce::TextButton btnPresetCountIn;  // -2
    juce::TextButton btnPresetIntro;    // -4

    juce::Label lblMappingExplanation;

    juce::TextButton btnOk;
    juce::TextButton btnCancel;

    void setOffsetValue(int val);
    void updateReadout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BarOffsetDialog)
};

} // namespace Harmonizer
