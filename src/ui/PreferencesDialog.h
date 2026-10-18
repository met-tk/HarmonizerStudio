#pragma once

#include "../audio/AudioEngine.h"
#include "../core/KeyCommandManager.h"
#include "../core/Localization.h"
#include "CubaseTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace Harmonizer
{

class KeyCaptureDialog : public juce::Component
{
public:
    explicit KeyCaptureDialog(std::function<void(const juce::KeyPress&)> onKeyAssigned);
    ~KeyCaptureDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    static void showModal(std::function<void(const juce::KeyPress&)> onKeyAssigned);

private:
    std::function<void(const juce::KeyPress&)> assignCallback;

    juce::KeyPress capturedKey;
    juce::Label lblInstruction;
    juce::Label lblKeyDisplay;
    juce::TextButton btnAssign { "Assign" };
    juce::TextButton btnCancel { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyCaptureDialog)
};

class ThemeColourButton : public juce::TextButton
{
public:
    void setSwatchColour(juce::Colour colour) { swatch = colour; repaint(); }
    void setOverlayPreviewBase(juce::Colour colour) { overlayPreviewBase = colour; repaint(); }
    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;

private:
    juce::Colour swatch { juce::Colours::transparentBlack };
    juce::Colour overlayPreviewBase { juce::Colours::transparentBlack };
};

class PreferencesComponent : public juce::Component, public CubaseTheme::Listener
{
public:
    PreferencesComponent(AudioEngine& engine);
    ~PreferencesComponent() override;

    void resized() override;
    void themeChanged() override;

private:
    AudioEngine& audioEngine;

    juce::TabbedComponent tabbedComponent { juce::TabbedButtonBar::TabsAtTop };

    // Tab 0: General / Editing Options
    class GeneralTab : public juce::Component
    {
    public:
        explicit GeneralTab(AudioEngine& engine);
        void paint(juce::Graphics& g) override;
        void resized() override;
        void themeChanged();
        void updateText();
    private:
        AudioEngine& audioEngine;
        juce::Label lblMidiPartOpenTitle;
        juce::ComboBox midiPartOpenCombo;
        juce::Label lblTip;
        juce::Label lblPausePlayheadTitle;
        juce::ComboBox pausePlayheadCombo;
        juce::Label lblPausePlayheadTip;
    };
    std::unique_ptr<GeneralTab> generalTab;

    // Tab 1: Audio Device
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSetupComp;

    // Tab 2: VST3 Manager
    class VstTab : public juce::Component, public VstHostManager::Listener
    {
    public:
        VstTab(VstHostManager& vstHost);
        ~VstTab() override;

        void paint(juce::Graphics& g) override;
        void resized() override;
        void pluginListChanged() override;
        void themeChanged();

    private:
        VstHostManager& vstManager;
        juce::TextButton btnRescan { "Scan for VST3 Plugins" };
        juce::TextButton btnImportSingleFile { "+ Import .vst3 File Directly" };
        juce::TextButton btnAddPath { "+ Add Search Path" };
        juce::TextButton btnRemovePath { "- Remove Path" };
        juce::TextButton btnResetPaths { "Reset Paths" };
        juce::ListBox pathListBox;
        juce::ListBox pluginListBox;

        class PathListModel : public juce::ListBoxModel
        {
        public:
            PathListModel(VstHostManager& host) : vst(host) {}
            int getNumRows() override;
            void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        private:
            VstHostManager& vst;
        } pathListModel;

        class PluginListModel : public juce::ListBoxModel
        {
        public:
            PluginListModel(VstHostManager& host) : vst(host) {}
            int getNumRows() override;
            void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        private:
            VstHostManager& vst;
        } listModel;
    };
    std::unique_ptr<VstTab> vstTab;

    // Tab 3: Key Commands (Categorized & Customizable)
    class ShortcutsTab : public juce::Component,
                         public KeyCommandManager::Listener,
                         public juce::ListBoxModel
    {
    public:
        ShortcutsTab();
        ~ShortcutsTab() override;

        void paint(juce::Graphics& g) override;
        void resized() override;

        void keyCommandsChanged() override;
        void themeChanged();

        // ListBoxModel
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate) override;

    private:
        juce::ComboBox categoryCombo;
        juce::TextButton btnResetAll { "Reset All to Defaults" };
        juce::ListBox commandListBox;

        std::vector<const KeyCommand*> filteredCommands;
        void updateFilter();
    };
    std::unique_ptr<ShortcutsTab> shortcutsTab;

    // Tab 4: Theme & Appearance
    class ThemeTab : public juce::Component,
                     public CubaseTheme::Listener,
                     public juce::ChangeListener,
                     public Localization::Listener
    {
    public:
        ThemeTab();
        ~ThemeTab() override;
        void paint(juce::Graphics& g) override;
        void resized() override;
        void themeChanged() override;
        void languageChanged() override;
        void changeListenerCallback(juce::ChangeBroadcaster* source) override;

        void updateText();

    private:
        juce::Label lblPresetTitle;
        juce::ComboBox presetCombo;
        juce::TextButton btnResetTheme;
        juce::TextButton btnSaveAsPreset;
        juce::TextButton btnRenamePreset;
        juce::TextButton btnOverwritePreset;
        juce::TextButton btnDeletePreset;
        static constexpr int savedPresetComboIdBase = 1000;

        juce::Label lblCustomColorsTitle;
        ThemeColourButton btnBgDeep;
        ThemeColourButton btnBgSurface;
        ThemeColourButton btnBgPanel;
        ThemeColourButton btnToolbarBackground;
        ThemeColourButton btnSidebarBackground;
        ThemeColourButton btnModuleHeaderBackground;
        ThemeColourButton btnButtonBackground;
        ThemeColourButton btnInputBackground;
        ThemeColourButton btnMenuBackground;
        ThemeColourButton btnAccentBlue;
        ThemeColourButton btnAccentOrange;
        ThemeColourButton btnAccentCyan;
        ThemeColourButton btnAccentGreen;
        ThemeColourButton btnAccentPurple;
        ThemeColourButton btnNoteGlow;
        ThemeColourButton btnBorderSubtle;
        ThemeColourButton btnBorderBright;
        ThemeColourButton btnTextPrimary;
        ThemeColourButton btnTextSecondary;

        // Locator Handles & Range Colors
        juce::Label lblLocatorColorsTitle;
        ThemeColourButton btnLocatorHandle;
        ThemeColourButton btnLocatorRangeOverlay;

        // Piano Roll Custom Colors
        juce::Label lblPianoRollColorsTitle;
        ThemeColourButton btnGridWhiteKey;
        ThemeColourButton btnGridBlackKey;
        ThemeColourButton btnGridBarLine;
        ThemeColourButton btnGridBeatLine;
        ThemeColourButton btnGridSubBeat;
        ThemeColourButton btnTonicTint;

        juce::String activeColorKey;
        juce::Component::SafePointer<juce::ColourSelector> activeColourSelector;

        void pickColor(const juce::String& title, const juce::String& colorKey, juce::Colour initial);
        void updateButtonSwatches();
        void refreshPresetChoices();
        void promptSaveAsPreset();
        void promptRenamePreset();
        void confirmOverwritePreset();
        void confirmDeletePreset();
    };
    std::unique_ptr<ThemeTab> themeTab;

    // Tab 5: Language
    class LanguageTab : public juce::Component
    {
    public:
        LanguageTab(std::function<void()> onLanguageChanged);
        void paint(juce::Graphics& g) override;
        void resized() override;
        void themeChanged();
    private:
        juce::ComboBox langCombo;
        juce::Label lblTip;
        std::function<void()> langChangedCallback;
    };
    std::unique_ptr<LanguageTab> languageTab;

    void updateTabTitles();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesComponent)
};

class PreferencesWindow : public juce::DocumentWindow
{
public:
    PreferencesWindow(AudioEngine& engine);
    ~PreferencesWindow() override = default;
    void closeButtonPressed() override;

private:
    AudioEngine& engineRef;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesWindow)
};

} // namespace Harmonizer
