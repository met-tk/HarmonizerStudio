#include "PreferencesDialog.h"
#include "CubaseTheme.h"
#include "../core/Localization.h"
#include "../core/GeneralSettings.h"
#include <algorithm>

namespace Harmonizer
{

namespace
{
class PresetNameDialog final : public juce::Component
{
public:
    PresetNameDialog(const juce::String& prompt,
                     const juce::String& initialName,
                     const juce::String& actionText,
                     const juce::String& failureText,
                     std::function<bool(const juce::String&)> onAccept)
        : accept(std::move(onAccept)), failureMessage(failureText)
    {
        lblPrompt.setText(prompt, juce::dontSendNotification);
        lblPrompt.setFont(CubaseTheme::getFont(13.5f));
        lblPrompt.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
        lblPrompt.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(lblPrompt);

        lblName.setText(tr("Preset name"), juce::dontSendNotification);
        lblName.setFont(CubaseTheme::getFont(12.0f, true));
        lblName.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
        lblName.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(lblName);

        nameEditor.setText(initialName, false);
        nameEditor.setInputRestrictions(64);
        nameEditor.setFont(CubaseTheme::getFont(14.0f));
        nameEditor.setColour(juce::TextEditor::backgroundColourId, CubaseTheme::inputBackground());
        nameEditor.setColour(juce::TextEditor::textColourId, CubaseTheme::textPrimary());
        nameEditor.setColour(juce::TextEditor::outlineColourId, CubaseTheme::borderSubtle());
        nameEditor.setColour(juce::TextEditor::focusedOutlineColourId, CubaseTheme::accentBlue());
        nameEditor.onTextChange = [this]() { lblError.setText({}, juce::dontSendNotification); };
        nameEditor.onReturnKey = [this]() { submit(); };
        nameEditor.onEscapeKey = [this]() { close(0); };
        addAndMakeVisible(nameEditor);

        lblError.setFont(CubaseTheme::getFont(11.5f));
        lblError.setColour(juce::Label::textColourId, CubaseTheme::accentOrange());
        lblError.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(lblError);

        btnAccept.setButtonText(actionText);
        btnAccept.onClick = [this]() { submit(); };
        addAndMakeVisible(btnAccept);

        btnCancel.setButtonText(tr("Cancel"));
        btnCancel.onClick = [this]() { close(0); };
        addAndMakeVisible(btnCancel);

        setSize(480, 218);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(CubaseTheme::bgPanel());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(24, 20);
        lblPrompt.setBounds(area.removeFromTop(26));
        area.removeFromTop(10);
        lblName.setBounds(area.removeFromTop(20));
        area.removeFromTop(4);
        nameEditor.setBounds(area.removeFromTop(34));
        area.removeFromTop(6);
        lblError.setBounds(area.removeFromTop(24));

        auto buttons = area.removeFromBottom(34);
        btnCancel.setBounds(buttons.removeFromRight(100));
        buttons.removeFromRight(10);
        btnAccept.setBounds(buttons.removeFromRight(100));
    }

    void focusName() { nameEditor.grabKeyboardFocus(); }

private:
    void submit()
    {
        const auto name = nameEditor.getText().trim();
        if (accept && accept(name))
            close(1);
        else
            lblError.setText(failureMessage, juce::dontSendNotification);
    }

    void close(int result)
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState(result);
    }

    std::function<bool(const juce::String&)> accept;
    juce::String failureMessage;
    juce::Label lblPrompt, lblName, lblError;
    juce::TextEditor nameEditor;
    juce::TextButton btnAccept, btnCancel;
};

void showPresetNameDialog(juce::Component& parent,
                          const juce::String& title,
                          const juce::String& prompt,
                          const juce::String& initialName,
                          const juce::String& actionText,
                          const juce::String& failureText,
                          std::function<bool(const juce::String&)> onAccept)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = title;
    options.dialogBackgroundColour = CubaseTheme::bgPanel();
    options.componentToCentreAround = &parent;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;
    auto* content = new PresetNameDialog(prompt, initialName, actionText,
                                         failureText, std::move(onAccept));
    options.content.setOwned(content);
    if (options.launchAsync() != nullptr)
        content->focusName();
}
} // namespace

//==============================================================================
// Key Capture Modal Dialog
//==============================================================================
KeyCaptureDialog::KeyCaptureDialog(std::function<void(const juce::KeyPress&)> onKeyAssigned)
    : assignCallback(std::move(onKeyAssigned))
{
    lblInstruction.setText(tr("Press any key combination on your keyboard:"), juce::dontSendNotification);
    lblInstruction.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    lblInstruction.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblInstruction);

    lblKeyDisplay.setText(tr("Waiting for input..."), juce::dontSendNotification);
    lblKeyDisplay.setFont(juce::FontOptions(18.0f).withStyle("bold"));
    lblKeyDisplay.setColour(juce::Label::textColourId, CubaseTheme::accentCyan());
    lblKeyDisplay.setColour(juce::Label::backgroundColourId, CubaseTheme::bgDeep());
    lblKeyDisplay.setColour(juce::Label::outlineColourId, CubaseTheme::borderBright());
    lblKeyDisplay.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblKeyDisplay);

    btnAssign.setButtonText(tr("Assign"));
    btnAssign.setEnabled(false);
    btnAssign.onClick = [this]() {
        if (capturedKey.isValid() && assignCallback)
            assignCallback(capturedKey);
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    };
    addAndMakeVisible(btnAssign);

    btnCancel.setButtonText(tr("Cancel"));
    btnCancel.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(btnCancel);

    setWantsKeyboardFocus(true);
    setSize(380, 180);
}

void KeyCaptureDialog::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgPanel());
    g.setColour(CubaseTheme::accentBlue());
    g.drawRect(getLocalBounds(), 2);
}

void KeyCaptureDialog::resized()
{
    auto area = getLocalBounds().reduced(16);
    lblInstruction.setBounds(area.removeFromTop(26));
    area.removeFromTop(10);
    lblKeyDisplay.setBounds(area.removeFromTop(44));
    area.removeFromTop(16);

    auto btnArea = area.removeFromBottom(30);
    btnCancel.setBounds(btnArea.removeFromRight(90));
    btnArea.removeFromRight(12);
    btnAssign.setBounds(btnArea.removeFromRight(90));
}

bool KeyCaptureDialog::keyPressed(const juce::KeyPress& key)
{
    capturedKey = key;
    lblKeyDisplay.setText(capturedKey.getTextDescription(), juce::dontSendNotification);
    btnAssign.setEnabled(true);
    return true;
}

void KeyCaptureDialog::showModal(std::function<void(const juce::KeyPress&)> onKeyAssigned)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr("Assign Key Shortcut");
    options.dialogBackgroundColour = CubaseTheme::bgPanel();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;

    auto dialogComp = std::make_unique<KeyCaptureDialog>(std::move(onKeyAssigned));
    options.content.setOwned(dialogComp.release());
    options.launchAsync();
}

//==============================================================================
// Preferences Component
//==============================================================================
PreferencesComponent::PreferencesComponent(AudioEngine& engine)
    : audioEngine(engine)
{
    // 0. General Tab
    generalTab = std::make_unique<GeneralTab>(audioEngine);
    tabbedComponent.addTab(tr("General"), CubaseTheme::bgPanel(), generalTab.get(), false);

    // 1. Audio Tab
    audioSetupComp = std::make_unique<juce::AudioDeviceSelectorComponent>(
        audioEngine.getDeviceManager(),
        0, 0, // min, max input
        1, 2, // min, max output
        true, true, true, false
    );
    tabbedComponent.addTab(tr("Audio Hardware"), CubaseTheme::bgPanel(), audioSetupComp.get(), false);

    // 2. VST Tab
    vstTab = std::make_unique<VstTab>(audioEngine.getVstHostManager());
    tabbedComponent.addTab(tr("VST3 Plugins"), CubaseTheme::bgPanel(), vstTab.get(), false);

    // 3. Shortcuts Tab
    shortcutsTab = std::make_unique<ShortcutsTab>();
    tabbedComponent.addTab(tr("Keyboard Shortcuts"), CubaseTheme::bgPanel(), shortcutsTab.get(), false);

    // 4. Appearance / Theme Tab
    themeTab = std::make_unique<ThemeTab>();
    tabbedComponent.addTab(tr("Appearance / Colors"), CubaseTheme::bgPanel(), themeTab.get(), false);

    // 5. Language Tab
    languageTab = std::make_unique<LanguageTab>([this]() {
        updateTabTitles();
    });
    tabbedComponent.addTab(tr("Language"), CubaseTheme::bgPanel(), languageTab.get(), false);

    tabbedComponent.getTabbedButtonBar().setColour(juce::TabbedButtonBar::tabTextColourId, CubaseTheme::textPrimary());
    tabbedComponent.getTabbedButtonBar().setColour(juce::TabbedButtonBar::frontTextColourId, CubaseTheme::textPrimary());

    addAndMakeVisible(tabbedComponent);
    CubaseTheme::addListener(this);
    themeChanged();
}

PreferencesComponent::~PreferencesComponent()
{
    CubaseTheme::removeListener(this);
}

void PreferencesComponent::themeChanged()
{
    for (int i = 0; i < tabbedComponent.getNumTabs(); ++i)
        tabbedComponent.setTabBackgroundColour(i, CubaseTheme::bgPanel());
    tabbedComponent.getTabbedButtonBar().setColour(juce::TabbedButtonBar::tabTextColourId, CubaseTheme::textSecondary());
    tabbedComponent.getTabbedButtonBar().setColour(juce::TabbedButtonBar::frontTextColourId, CubaseTheme::textPrimary());

    if (generalTab) generalTab->themeChanged();

    if (audioSetupComp)
    {
        audioSetupComp->sendLookAndFeelChange();
        audioSetupComp->repaint();
    }

    if (themeTab) themeTab->themeChanged();
    if (shortcutsTab) shortcutsTab->themeChanged();
    if (vstTab) vstTab->themeChanged();
    if (languageTab) languageTab->themeChanged();

    tabbedComponent.getTabbedButtonBar().repaint();
    sendLookAndFeelChange();
    repaint();
}

void PreferencesComponent::updateTabTitles()
{
    tabbedComponent.setTabName(0, tr("General"));
    tabbedComponent.setTabName(1, tr("Audio Hardware"));
    tabbedComponent.setTabName(2, tr("VST3 Plugins"));
    tabbedComponent.setTabName(3, tr("Keyboard Shortcuts"));
    tabbedComponent.setTabName(4, tr("Appearance / Colors"));
    tabbedComponent.setTabName(5, tr("Language"));
    if (generalTab) generalTab->updateText();
    if (themeTab) themeTab->updateText();
    repaint();
}

void PreferencesComponent::resized()
{
    tabbedComponent.setBounds(getLocalBounds());
}

//==============================================================================
// VST Tab
//==============================================================================
PreferencesComponent::VstTab::VstTab(VstHostManager& host)
    : vstManager(host), pathListModel(host), listModel(host)
{
    vstManager.addListener(this);

    btnAddPath.setButtonText("+ " + tr("Add Folder"));
    btnAddPath.onClick = [this]() {
        juce::FileChooser chooser(tr("Select VST3 Plugin Folder"), juce::File());
        if (chooser.browseForDirectory())
        {
            auto folder = chooser.getResult();
            if (folder.isDirectory())
            {
                vstManager.addCustomSearchPath(folder.getFullPathName());
                pathListBox.updateContent();
                pathListBox.repaint();
            }
        }
    };
    addAndMakeVisible(btnAddPath);

    btnRemovePath.setButtonText("- " + tr("Remove Folder"));
    btnRemovePath.onClick = [this]() {
        int selected = pathListBox.getSelectedRow();
        if (selected >= 0)
        {
            vstManager.removeCustomSearchPath(selected);
            pathListBox.updateContent();
            pathListBox.repaint();
        }
    };
    addAndMakeVisible(btnRemovePath);

    btnResetPaths.setButtonText(tr("Reset Default"));
    btnResetPaths.onClick = [this]() {
        vstManager.resetCustomSearchPaths();
        pathListBox.updateContent();
        pathListBox.repaint();
    };
    addAndMakeVisible(btnResetPaths);

    pathListBox.setModel(&pathListModel);
    pathListBox.setRowHeight(24);
    pathListBox.setColour(juce::ListBox::backgroundColourId, CubaseTheme::bgDeep());
    pathListBox.setColour(juce::ListBox::outlineColourId, CubaseTheme::borderSubtle());
    addAndMakeVisible(pathListBox);

    btnRescan.setButtonText(tr("Scan for VST3 Plugins"));
    btnRescan.onClick = [this]() {
        btnRescan.setEnabled(false);
        btnRescan.setButtonText(tr("Scanning VST3..."));
        vstManager.scanPlugins();
    };
    addAndMakeVisible(btnRescan);

    btnImportSingleFile.setButtonText(tr("+ Import .vst3 File Directly..."));
    btnImportSingleFile.onClick = [this]() {
        juce::FileChooser chooser(tr("Select VST3 Plugin (.vst3)"), juce::File(), "*.vst3");
        if (chooser.browseForFileToOpen())
        {
            auto f = chooser.getResult();
            if (f.exists())
            {
                bool ok = vstManager.scanSinglePluginFile(f);
                if (ok)
                {
                    pluginListBox.updateContent();
                    pluginListBox.repaint();
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                        tr("VST3 Import"), tr("Successfully imported: ") + f.getFileName());
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                        tr("VST3 Import Failed"), tr("Could not recognize plugin file: ") + f.getFileName());
                }
            }
        }
    };
    addAndMakeVisible(btnImportSingleFile);

    pluginListBox.setModel(&listModel);
    pluginListBox.setRowHeight(28);
    pluginListBox.setColour(juce::ListBox::backgroundColourId, CubaseTheme::bgDeep());
    pluginListBox.setColour(juce::ListBox::outlineColourId, CubaseTheme::borderSubtle());
    addAndMakeVisible(pluginListBox);
}

PreferencesComponent::VstTab::~VstTab()
{
    vstManager.removeListener(this);
}

void PreferencesComponent::VstTab::pluginListChanged()
{
    btnRescan.setEnabled(true);
    btnRescan.setButtonText(tr("Scan for VST3 Plugins"));
    pluginListBox.updateContent();
    pluginListBox.repaint();
}

void PreferencesComponent::VstTab::themeChanged()
{
    pathListBox.setColour(juce::ListBox::backgroundColourId, CubaseTheme::bgDeep());
    pathListBox.setColour(juce::ListBox::outlineColourId, CubaseTheme::borderSubtle());
    pluginListBox.setColour(juce::ListBox::backgroundColourId, CubaseTheme::bgDeep());
    pluginListBox.setColour(juce::ListBox::outlineColourId, CubaseTheme::borderSubtle());
    pathListBox.repaint();
    pluginListBox.repaint();
    repaint();
}

void PreferencesComponent::VstTab::paint(juce::Graphics& g)
{
    g.setColour(CubaseTheme::bgPanel());
    g.fillAll();

    auto area = getLocalBounds().reduced(16);
    g.setColour(CubaseTheme::accentOrange());
    g.setFont(juce::FontOptions(13.0f).withStyle("bold"));
    g.drawText(tr("VST3 Search Directories:"), area.removeFromTop(20), juce::Justification::centredLeft);

    area.removeFromTop(105); // space occupied by path list and buttons

    g.setColour(CubaseTheme::accentOrange());
    g.setFont(juce::FontOptions(13.0f).withStyle("bold"));
    g.drawText(tr("Installed Plugins:"), area.removeFromTop(20), juce::Justification::centredLeft);
}

void PreferencesComponent::VstTab::resized()
{
    auto area = getLocalBounds().reduced(16);

    // Section 1: Search Paths Header
    area.removeFromTop(22);

    auto pathRow = area.removeFromTop(80);
    pathListBox.setBounds(pathRow.removeFromLeft(pathRow.getWidth() - 120));
    pathRow.removeFromLeft(8);

    btnAddPath.setBounds(pathRow.removeFromTop(24));
    pathRow.removeFromTop(4);
    btnRemovePath.setBounds(pathRow.removeFromTop(24));
    pathRow.removeFromTop(4);
    btnResetPaths.setBounds(pathRow.removeFromTop(24));

    area.removeFromTop(16);

    // Section 2: Plugins Header & Scan Buttons
    auto scanRow = area.removeFromTop(32);
    btnRescan.setBounds(scanRow.removeFromRight(180));
    scanRow.removeFromRight(8);
    btnImportSingleFile.setBounds(scanRow.removeFromRight(200));

    area.removeFromTop(8);
    pluginListBox.setBounds(area);
}

int PreferencesComponent::VstTab::PathListModel::getNumRows()
{
    return vst.getCustomSearchPaths().size();
}

void PreferencesComponent::VstTab::PathListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
    {
        g.setColour(CubaseTheme::accentBlue());
        g.fillRect(0, 0, width, height);
    }

    if (rowNumber >= 0 && rowNumber < vst.getCustomSearchPaths().size())
    {
        g.setColour(rowIsSelected ? juce::Colours::white : CubaseTheme::textPrimary());
        g.setFont(CubaseTheme::getFont(12.0f, false));
        g.drawText(vst.getCustomSearchPaths()[rowNumber], 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    }
}

int PreferencesComponent::VstTab::PluginListModel::getNumRows()
{
    return vst.getPluginList().getNumTypes();
}

void PreferencesComponent::VstTab::PluginListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
    {
        g.setColour(CubaseTheme::accentBlue());
        g.fillRect(0, 0, width, height);
    }

    if (auto* desc = vst.getPluginList().getType(rowNumber))
    {
        g.setColour(rowIsSelected ? juce::Colours::white : CubaseTheme::textPrimary());
        g.setFont(CubaseTheme::getFont(12.5f, false));
        g.drawText(desc->name + " (" + desc->manufacturerName + ") - " + desc->pluginFormatName,
                   10, 0, width - 20, height, juce::Justification::centredLeft, true);
    }
}

//==============================================================================
// Shortcuts Tab (Categorized & Customizable)
//==============================================================================
class ShortcutRowComponent : public juce::Component
{
public:
    ShortcutRowComponent(const KeyCommand& cmd, std::function<void()> onChange)
        : command(cmd), changeCallback(onChange)
    {
        btnAdd.setButtonText("+ " + tr("Add Key"));
        btnAdd.onClick = [this]() {
            int cmdId = command.id;
            auto onChange = changeCallback;
            KeyCaptureDialog::showModal([cmdId, onChange](const juce::KeyPress& kp) {
                KeyCommandManager::getInstance().addKeyShortcut(cmdId, kp);
                juce::MessageManager::callAsync([cb = onChange]() {
                    if (cb) cb();
                });
            });
        };
        addAndMakeVisible(btnAdd);

        btnClear.setButtonText(tr("Clear"));
        btnClear.onClick = [this]() {
            int cmdId = command.id;
            KeyCommandManager::getInstance().clearKeyShortcuts(cmdId);
            juce::MessageManager::callAsync([cb = changeCallback]() {
                if (cb) cb();
            });
        };
        addAndMakeVisible(btnClear);

        btnReset.setButtonText(tr("Reset"));
        btnReset.onClick = [this]() {
            int cmdId = command.id;
            KeyCommandManager::getInstance().resetCommandToDefault(cmdId);
            juce::MessageManager::callAsync([cb = changeCallback]() {
                if (cb) cb();
            });
        };
        addAndMakeVisible(btnReset);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        g.setColour(CubaseTheme::bgSurface());
        g.fillRect(bounds);

        g.setColour(CubaseTheme::borderSubtle());
        g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, (float)bounds.getWidth());

        int rightButtonsW = 195;
        int avail = bounds.getWidth() - rightButtonsW - 16;
        int nameW = std::clamp((int)(avail * 0.45f), 130, 220);
        int keyX = 10 + nameW + 10;
        int keyW = std::max(60, avail - nameW - 14);

        // Command Name - 100% solid, crisp, high contrast
        g.setColour(CubaseTheme::textPrimary());
        g.setFont(CubaseTheme::getFont(12.0f, true));
        g.drawText(tr(command.name), 10, 0, nameW, bounds.getHeight(), juce::Justification::centredLeft, false);

        // Current Shortcut keys
        auto keyArea = juce::Rectangle<float>((float)keyX, 4.0f, (float)keyW, (float)bounds.getHeight() - 8.0f);
        g.setColour(CubaseTheme::bgDeep());
        g.fillRoundedRectangle(keyArea, 3.0f);
        g.setColour(CubaseTheme::borderBright());
        g.drawRoundedRectangle(keyArea, 3.0f, 1.0f);

        bool isDisabled = (command.getDisplayString() == "(Disabled)");
        juce::Colour keyTextColor = isDisabled
            ? CubaseTheme::textSecondary()
            : (CubaseTheme::isDarkTheme() ? CubaseTheme::accentCyan() : CubaseTheme::accentBlue());
        g.setColour(keyTextColor);
        g.setFont(CubaseTheme::getFont(11.5f, !isDisabled));
        g.drawText(command.getDisplayString().isEmpty() ? ("(" + tr("None") + ")") : tr(command.getDisplayString()),
                   keyArea.toNearestInt(), juce::Justification::centred, false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4);
        btnReset.setBounds(area.removeFromRight(55)); area.removeFromRight(4);
        btnClear.setBounds(area.removeFromRight(55)); area.removeFromRight(4);
        btnAdd.setBounds(area.removeFromRight(75));
    }

private:
    KeyCommand command;
    std::function<void()> changeCallback;
    juce::TextButton btnAdd;
    juce::TextButton btnClear;
    juce::TextButton btnReset;
};

PreferencesComponent::ShortcutsTab::ShortcutsTab()
{
    KeyCommandManager::getInstance().addListener(this);

    categoryCombo.addItem(tr("All Categories"), 1);
    categoryCombo.addItem(tr("Edit"), 2);
    categoryCombo.addItem(tr("1: Select") + " (Tools)", 3);
    categoryCombo.addItem(tr("Transport"), 4);
    categoryCombo.addItem(tr("Zoom +") + " (View)", 5);
    categoryCombo.addItem(tr("Harmony Editing"), 6);
    categoryCombo.setSelectedId(1, juce::dontSendNotification);
    categoryCombo.onChange = [this]() { updateFilter(); };
    addAndMakeVisible(categoryCombo);

    btnResetAll.setButtonText(tr("Reset All to Defaults"));
    btnResetAll.onClick = [this]() {
        KeyCommandManager::getInstance().resetAllToDefault();
    };
    addAndMakeVisible(btnResetAll);

    commandListBox.setModel(this);
    commandListBox.setRowHeight(34);
    commandListBox.setColour(juce::ListBox::backgroundColourId, CubaseTheme::bgDeep());
    commandListBox.setColour(juce::ListBox::outlineColourId, CubaseTheme::borderSubtle());
    addAndMakeVisible(commandListBox);

    updateFilter();
}

PreferencesComponent::ShortcutsTab::~ShortcutsTab()
{
    KeyCommandManager::getInstance().removeListener(this);
}

void PreferencesComponent::ShortcutsTab::themeChanged()
{
    categoryCombo.setColour(juce::ComboBox::backgroundColourId, CubaseTheme::inputBackground());
    categoryCombo.setColour(juce::ComboBox::textColourId, CubaseTheme::textPrimary());
    categoryCombo.setColour(juce::ComboBox::outlineColourId, CubaseTheme::borderSubtle());
    categoryCombo.setColour(juce::ComboBox::arrowColourId, CubaseTheme::textSecondary());

    btnResetAll.setColour(juce::TextButton::buttonColourId, CubaseTheme::buttonBackground());
    btnResetAll.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());

    commandListBox.setColour(juce::ListBox::backgroundColourId, CubaseTheme::bgDeep());
    commandListBox.setColour(juce::ListBox::outlineColourId, CubaseTheme::borderSubtle());

    commandListBox.updateContent();
    commandListBox.repaint();
    repaint();
}

void PreferencesComponent::ShortcutsTab::updateFilter()
{
    filteredCommands.clear();
    int catId = categoryCombo.getSelectedId();

    const auto& all = KeyCommandManager::getInstance().getAllCommands();
    for (const auto& cmd : all)
    {
        if (catId == 1) // All
            filteredCommands.push_back(&cmd);
        else if (catId == 2 && cmd.category == CommandCategory::Edit)
            filteredCommands.push_back(&cmd);
        else if (catId == 3 && cmd.category == CommandCategory::Tools)
            filteredCommands.push_back(&cmd);
        else if (catId == 4 && cmd.category == CommandCategory::Transport)
            filteredCommands.push_back(&cmd);
        else if (catId == 5 && cmd.category == CommandCategory::ViewZoom)
            filteredCommands.push_back(&cmd);
        else if (catId == 6 && cmd.category == CommandCategory::Harmony)
            filteredCommands.push_back(&cmd);
    }
    commandListBox.updateContent();
    commandListBox.repaint();
}

void PreferencesComponent::ShortcutsTab::keyCommandsChanged()
{
    updateFilter();
}

int PreferencesComponent::ShortcutsTab::getNumRows()
{
    return (int)filteredCommands.size();
}

void PreferencesComponent::ShortcutsTab::paintListBoxItem(int, juce::Graphics&, int, int, bool)
{
}

juce::Component* PreferencesComponent::ShortcutsTab::refreshComponentForRow(int rowNumber, bool, juce::Component* existingComponentToUpdate)
{
    delete existingComponentToUpdate;
    if (rowNumber >= 0 && rowNumber < (int)filteredCommands.size())
    {
        juce::Component::SafePointer<ShortcutsTab> safeThis(this);
        return new ShortcutRowComponent(*filteredCommands[rowNumber], [safeThis]() {
            if (auto* self = safeThis.getComponent())
                self->updateFilter();
        });
    }
    return nullptr;
}

void PreferencesComponent::ShortcutsTab::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgPanel());
}

void PreferencesComponent::ShortcutsTab::resized()
{
    auto area = getLocalBounds().reduced(16);

    auto topRow = area.removeFromTop(30);
    categoryCombo.setBounds(topRow.removeFromLeft(200));
    btnResetAll.setBounds(topRow.removeFromRight(160));

    area.removeFromTop(10);
    commandListBox.setBounds(area);
}

//==============================================================================
// Theme & Appearance Tab
//==============================================================================
void ThemeColourButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    auto background = CubaseTheme::buttonBackground();
    if (isButtonDown)
        background = background.interpolatedWith(CubaseTheme::accentBlue(), 0.15f);
    else if (isMouseOver)
        background = background.interpolatedWith(CubaseTheme::accentBlue(), 0.08f);

    g.setColour(background);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(isMouseOver ? CubaseTheme::borderBright() : CubaseTheme::borderSubtle());
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

    const auto chip = juce::Rectangle<int>(10, (getHeight() - 18) / 2, 18, 18);
    g.setColour(overlayPreviewBase.isTransparent() ? juce::Colours::white : overlayPreviewBase);
    g.fillRoundedRectangle(chip.toFloat(), 3.0f);
    if (swatch.getAlpha() < 255 && overlayPreviewBase.isTransparent())
    {
        g.setColour(juce::Colour(0xffb8bec6));
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x)
                if ((x + y) % 2 == 0)
                    g.fillRect(chip.getX() + x * 6, chip.getY() + y * 6, 6, 6);
    }
    g.setColour(swatch);
    g.fillRoundedRectangle(chip.toFloat(), 3.0f);
    g.setColour(CubaseTheme::borderBright().withAlpha(0.75f));
    g.drawRoundedRectangle(chip.toFloat(), 3.0f, 1.0f);

    g.setColour((isEnabled() ? CubaseTheme::textPrimary() : CubaseTheme::textSecondary().withAlpha(0.5f)));
    g.setFont(CubaseTheme::getFont(11.5f));
    g.drawFittedText(getButtonText(), 37, 1, std::max(0, getWidth() - 44), getHeight() - 2,
                     juce::Justification::centredLeft, 1, 0.82f);
}

PreferencesComponent::ThemeTab::ThemeTab()
{
    CubaseTheme::addThemeListener(this);
    Localization::getInstance().addListener(this);

    lblPresetTitle.setFont(CubaseTheme::getFont(13.5f, true));
    lblPresetTitle.setColour(juce::Label::textColourId, CubaseTheme::accentOrange());
    addAndMakeVisible(lblPresetTitle);

    presetCombo.onChange = [this]() {
        int id = presetCombo.getSelectedId();
        if (id >= 1 && id <= 7)
            CubaseTheme::setPreset((ThemePreset)(id - 1));
        else if (id >= savedPresetComboIdBase)
        {
            if (!CubaseTheme::applySavedPreset(id - savedPresetComboIdBase))
                refreshPresetChoices();
        }
    };
    addAndMakeVisible(presetCombo);

    btnSaveAsPreset.onClick = [this]() { promptSaveAsPreset(); };
    btnRenamePreset.onClick = [this]() { promptRenamePreset(); };
    btnOverwritePreset.onClick = [this]() { confirmOverwritePreset(); };
    btnDeletePreset.onClick = [this]() { confirmDeletePreset(); };
    addAndMakeVisible(btnSaveAsPreset);
    addAndMakeVisible(btnRenamePreset);
    addAndMakeVisible(btnOverwritePreset);
    addAndMakeVisible(btnDeletePreset);

    lblCustomColorsTitle.setFont(CubaseTheme::getFont(13.0f, true));
    lblCustomColorsTitle.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    addAndMakeVisible(lblCustomColorsTitle);

    btnBgDeep.onClick = [this]() { pickColor(tr("Background Deep"), "bgDeep", CubaseTheme::bgDeep()); };
    btnBgSurface.onClick = [this]() { pickColor(tr("Background Surface"), "bgSurface", CubaseTheme::bgSurface()); };
    btnBgPanel.onClick = [this]() { pickColor(tr("Background Panel"), "bgPanel", CubaseTheme::bgPanel()); };
    btnToolbarBackground.onClick = [this]() { pickColor(tr("Toolbar Background"), "toolbarBackground", CubaseTheme::toolbarBackground()); };
    btnSidebarBackground.onClick = [this]() { pickColor(tr("Sidebar Background"), "sidebarBackground", CubaseTheme::sidebarBackground()); };
    btnModuleHeaderBackground.onClick = [this]() { pickColor(tr("Module Header Background"), "moduleHeaderBackground", CubaseTheme::moduleHeaderBackground()); };
    btnButtonBackground.onClick = [this]() { pickColor(tr("Button Background"), "buttonBackground", CubaseTheme::buttonBackground()); };
    btnInputBackground.onClick = [this]() { pickColor(tr("Input Background"), "inputBackground", CubaseTheme::inputBackground()); };
    btnMenuBackground.onClick = [this]() { pickColor(tr("Menu Background"), "menuBackground", CubaseTheme::menuBackground()); };
    btnTextPrimary.onClick = [this]() { pickColor(tr("Primary Text"), "textPrimary", CubaseTheme::textPrimary()); };
    btnTextSecondary.onClick = [this]() { pickColor(tr("Secondary Text"), "textSecondary", CubaseTheme::textSecondary()); };

    btnAccentBlue.onClick = [this]() { pickColor(tr("Accent Blue"), "accentBlue", CubaseTheme::accentBlue()); };
    btnAccentOrange.onClick = [this]() { pickColor(tr("Accent Orange"), "accentOrange", CubaseTheme::accentOrange()); };
    btnAccentCyan.onClick = [this]() { pickColor(tr("Accent Cyan"), "accentCyan", CubaseTheme::accentCyan()); };
    btnAccentGreen.onClick = [this]() { pickColor(tr("Accent Green"), "accentGreen", CubaseTheme::accentGreen()); };
    btnAccentPurple.onClick = [this]() { pickColor(tr("Accent Purple"), "accentPurple", CubaseTheme::accentPurple()); };
    btnNoteGlow.onClick = [this]() { pickColor(tr("Note Selection Glow"), "noteGlow", CubaseTheme::noteGlow()); };
    btnBorderSubtle.onClick = [this]() { pickColor(tr("Subtle Border"), "borderSubtle", CubaseTheme::borderSubtle()); };
    btnBorderBright.onClick = [this]() { pickColor(tr("Bright Border"), "borderBright", CubaseTheme::borderBright()); };

    addAndMakeVisible(btnBgDeep);
    addAndMakeVisible(btnBgSurface);
    addAndMakeVisible(btnBgPanel);
    addAndMakeVisible(btnToolbarBackground);
    addAndMakeVisible(btnSidebarBackground);
    addAndMakeVisible(btnModuleHeaderBackground);
    addAndMakeVisible(btnButtonBackground);
    addAndMakeVisible(btnInputBackground);
    addAndMakeVisible(btnMenuBackground);
    addAndMakeVisible(btnBorderSubtle);
    addAndMakeVisible(btnBorderBright);
    addAndMakeVisible(btnTextPrimary);
    addAndMakeVisible(btnTextSecondary);
    addAndMakeVisible(btnAccentBlue);
    addAndMakeVisible(btnAccentOrange);
    addAndMakeVisible(btnAccentCyan);
    addAndMakeVisible(btnAccentGreen);
    addAndMakeVisible(btnAccentPurple);
    addAndMakeVisible(btnNoteGlow);

    // Locator handles & range
    lblLocatorColorsTitle.setFont(CubaseTheme::getFont(13.0f, true));
    lblLocatorColorsTitle.setColour(juce::Label::textColourId, CubaseTheme::accentCyan());
    addAndMakeVisible(lblLocatorColorsTitle);

    btnLocatorHandle.onClick = [this]() { pickColor(tr("Locator Handles [L/R]"), "locatorHandle", CubaseTheme::locatorHandle()); };
    btnLocatorRangeOverlay.onClick = [this]() { pickColor(tr("Locator Range Overlay"), "locatorRangeOverlay", CubaseTheme::locatorRangeOverlay()); };
    addAndMakeVisible(btnLocatorHandle);
    addAndMakeVisible(btnLocatorRangeOverlay);

    // Piano roll & grid
    lblPianoRollColorsTitle.setFont(CubaseTheme::getFont(13.0f, true));
    lblPianoRollColorsTitle.setColour(juce::Label::textColourId, CubaseTheme::accentOrange());
    addAndMakeVisible(lblPianoRollColorsTitle);

    btnGridWhiteKey.onClick = [this]() { pickColor(tr("White Key Row"), "gridWhiteKeyRow", CubaseTheme::gridWhiteKeyRow()); };
    btnGridBlackKey.onClick = [this]() { pickColor(tr("Black Key Row"), "gridBlackKeyRow", CubaseTheme::gridBlackKeyRow()); };
    btnGridBarLine.onClick = [this]() { pickColor(tr("Bar Line"), "gridBarLine", CubaseTheme::gridBarLine()); };
    btnGridBeatLine.onClick = [this]() { pickColor(tr("Beat Line"), "gridBeatLine", CubaseTheme::gridBeatLine()); };
    btnGridSubBeat.onClick = [this]() { pickColor(tr("Sub-Beat Line"), "gridSubBeat", CubaseTheme::gridSubBeat()); };
    btnTonicTint.onClick = [this]() { pickColor(tr("Tonic Highlight Tint"), "pianoRollTonicTint", CubaseTheme::pianoRollTonicTint()); };

    addAndMakeVisible(btnGridWhiteKey);
    addAndMakeVisible(btnGridBlackKey);
    addAndMakeVisible(btnGridBarLine);
    addAndMakeVisible(btnGridBeatLine);
    addAndMakeVisible(btnGridSubBeat);
    addAndMakeVisible(btnTonicTint);

    btnResetTheme.onClick = [this]() {
        const int savedId = CubaseTheme::getActiveSavedPresetId();
        if (savedId > 0)
            CubaseTheme::applySavedPreset(savedId);
        else
        {
            auto preset = CubaseTheme::getCurrentPreset();
            CubaseTheme::setPreset(preset == ThemePreset::Custom
                ? CubaseTheme::getLastNamedPreset() : preset);
        }
    };
    addAndMakeVisible(btnResetTheme);

    updateText();
}

PreferencesComponent::ThemeTab::~ThemeTab()
{
    Localization::getInstance().removeListener(this);
    CubaseTheme::removeThemeListener(this);
    if (activeColourSelector != nullptr)
        activeColourSelector->removeChangeListener(this);
}

void PreferencesComponent::ThemeTab::updateText()
{
    lblPresetTitle.setText(tr("Theme preset"), juce::dontSendNotification);
    btnResetTheme.setButtonText(tr("Restore preset"));
    btnSaveAsPreset.setButtonText(tr("Save as custom preset"));
    btnRenamePreset.setButtonText(tr("Rename preset"));
    btnOverwritePreset.setButtonText(tr("Overwrite preset"));
    btnDeletePreset.setButtonText(tr("Delete preset"));
    refreshPresetChoices();

    lblCustomColorsTitle.setText(tr("Interface colours"), juce::dontSendNotification);
    lblLocatorColorsTitle.setText(tr("Selection and locators"), juce::dontSendNotification);
    lblPianoRollColorsTitle.setText(tr("Piano roll and grid"), juce::dontSendNotification);

    updateButtonSwatches();
}

void PreferencesComponent::ThemeTab::refreshPresetChoices()
{
    const int activeId = CubaseTheme::getActiveSavedPresetId();
    presetCombo.clear(juce::dontSendNotification);
    presetCombo.addItem(tr("Cubase Classic Dark"), 1);
    presetCombo.addItem(tr("Modern Studio Slate"), 2);
    presetCombo.addItem(tr("Ableton Minimalist"), 3);
    presetCombo.addItem(tr("Warm Space Grey"), 4);
    presetCombo.addItem(tr("Modern Studio Light"), 5);
    presetCombo.addItem(tr("Cubase Classic Light"), 6);
    presetCombo.addItem(tr("Custom Theme"), 7);

    const auto saved = CubaseTheme::getSavedPresets();
    if (!saved.empty())
    {
        presetCombo.addSeparator();
        presetCombo.addSectionHeading(tr("Saved presets"));
        for (const auto& item : saved)
        {
            auto label = item.name;
            if (item.id == activeId && CubaseTheme::isActiveSavedPresetModified())
                label += " (" + tr("Modified") + ")";
            presetCombo.addItem(label, savedPresetComboIdBase + item.id);
        }
    }

    presetCombo.setSelectedId(activeId > 0 ? savedPresetComboIdBase + activeId
                                             : (int)CubaseTheme::getCurrentPreset() + 1,
                              juce::dontSendNotification);
    btnOverwritePreset.setEnabled(activeId > 0 && CubaseTheme::isActiveSavedPresetModified());
    btnRenamePreset.setEnabled(activeId > 0);
    btnDeletePreset.setEnabled(activeId > 0);
}

void PreferencesComponent::ThemeTab::promptSaveAsPreset()
{
    const auto saved = CubaseTheme::getSavedPresets();
    int number = 1;
    juce::String suggested;
    for (;;)
    {
        suggested = tr("Custom preset") + " " + juce::String(number++);
        const bool exists = std::any_of(saved.begin(), saved.end(), [&](const auto& item) {
            return item.name.equalsIgnoreCase(suggested);
        });
        if (!exists) break;
    }

    juce::Component::SafePointer<ThemeTab> safeThis(this);
    showPresetNameDialog(*this, tr("Save as custom preset"),
        tr("Enter a unique name for the current colors."), suggested, tr("Save"),
        tr("Use a unique name of 1 to 64 characters; check that settings can be saved."),
        [safeThis](const juce::String& name) {
            return safeThis != nullptr && CubaseTheme::saveAsPreset(name) >= 0;
        });
}

void PreferencesComponent::ThemeTab::promptRenamePreset()
{
    const int id = CubaseTheme::getActiveSavedPresetId();
    if (id <= 0) return;
    const auto saved = CubaseTheme::getSavedPresets();
    const auto entry = std::find_if(saved.begin(), saved.end(), [id](const auto& item) {
        return item.id == id;
    });
    if (entry == saved.end()) return;

    juce::Component::SafePointer<ThemeTab> safeThis(this);
    showPresetNameDialog(*this, tr("Rename preset"),
        tr("Enter a unique name for this preset."), entry->name, tr("Rename"),
        tr("Use a unique name of 1 to 64 characters; check that settings can be saved."),
        [safeThis, id](const juce::String& name) {
            return safeThis != nullptr && CubaseTheme::renameSavedPreset(id, name);
        });
}

void PreferencesComponent::ThemeTab::confirmOverwritePreset()
{
    const int id = CubaseTheme::getActiveSavedPresetId();
    if (id <= 0) return;
    const auto saved = CubaseTheme::getSavedPresets();
    const auto entry = std::find_if(saved.begin(), saved.end(), [id](const auto& item) {
        return item.id == id;
    });
    if (entry == saved.end()) return;

    const auto options = juce::MessageBoxOptions()
        .withIconType(juce::AlertWindow::QuestionIcon)
        .withTitle(tr("Overwrite preset"))
        .withMessage(tr("Replace the saved colors in")
                     + juce::String::fromUTF8(u8" “") + entry->name
                     + juce::String::fromUTF8(u8"”?"))
        .withButton(tr("Overwrite"))
        .withButton(tr("Cancel"))
        .withAssociatedComponent(this);
    juce::Component::SafePointer<ThemeTab> safeThis(this);
    juce::AlertWindow::showAsync(options, [safeThis, id](int choice) {
        // JUCE AlertWindow maps the first of two buttons to result 1.
        if (choice == 1 && safeThis != nullptr)
        {
            if (!CubaseTheme::overwriteSavedPreset(id))
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    tr("Preset could not be updated"), tr("Please check that theme settings can be saved."),
                    tr("OK"), safeThis.getComponent());
        }
    });
}

void PreferencesComponent::ThemeTab::confirmDeletePreset()
{
    const int id = CubaseTheme::getActiveSavedPresetId();
    if (id <= 0) return;
    const auto saved = CubaseTheme::getSavedPresets();
    const auto entry = std::find_if(saved.begin(), saved.end(), [id](const auto& item) {
        return item.id == id;
    });
    if (entry == saved.end()) return;

    const auto options = juce::MessageBoxOptions()
        .withIconType(juce::AlertWindow::QuestionIcon)
        .withTitle(tr("Delete preset"))
        .withMessage(tr("Delete the saved preset")
                     + juce::String::fromUTF8(u8" “") + entry->name
                     + juce::String::fromUTF8(u8"”?\n")
                     + tr("The current colors will remain available as a custom theme."))
        .withButton(tr("Delete"))
        .withButton(tr("Cancel"))
        .withAssociatedComponent(this);
    juce::Component::SafePointer<ThemeTab> safeThis(this);
    juce::AlertWindow::showAsync(options, [safeThis, id](int choice) {
        if (choice == 1 && safeThis != nullptr)
        {
            if (!CubaseTheme::deleteSavedPreset(id))
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    tr("Preset could not be updated"), tr("Please check that theme settings can be saved."),
                    tr("OK"), safeThis.getComponent());
        }
    });
}

void PreferencesComponent::ThemeTab::languageChanged()
{
    updateText();
    repaint();
}

void PreferencesComponent::ThemeTab::themeChanged()
{
    refreshPresetChoices();
    presetCombo.setColour(juce::ComboBox::backgroundColourId, CubaseTheme::inputBackground());
    presetCombo.setColour(juce::ComboBox::textColourId, CubaseTheme::textPrimary());
    presetCombo.setColour(juce::ComboBox::outlineColourId, CubaseTheme::borderSubtle());
    presetCombo.setColour(juce::ComboBox::arrowColourId, CubaseTheme::textSecondary());

    lblPresetTitle.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblCustomColorsTitle.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblLocatorColorsTitle.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblPianoRollColorsTitle.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());

    btnResetTheme.setColour(juce::TextButton::buttonColourId, CubaseTheme::buttonBackground());
    btnResetTheme.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());
    for (auto* button : { &btnSaveAsPreset, &btnRenamePreset,
                          &btnOverwritePreset, &btnDeletePreset })
    {
        button->setColour(juce::TextButton::buttonColourId, CubaseTheme::buttonBackground());
        button->setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());
    }

    updateButtonSwatches();
    sendLookAndFeelChange();
    repaint();
}

void PreferencesComponent::ThemeTab::updateButtonSwatches()
{
    auto applyStyle = [](ThemeColourButton& btn, const juce::String& text, juce::Colour col) {
        btn.setButtonText(text);
        btn.setSwatchColour(col);
        btn.setTooltip(text + "  " + col.toDisplayString(true));
    };

    applyStyle(btnBgDeep, tr("Background Deep"), CubaseTheme::bgDeep());
    applyStyle(btnBgSurface, tr("Background Surface"), CubaseTheme::bgSurface());
    applyStyle(btnBgPanel, tr("Background Panel"), CubaseTheme::bgPanel());
    applyStyle(btnToolbarBackground, tr("Toolbar Background"), CubaseTheme::toolbarBackground());
    applyStyle(btnSidebarBackground, tr("Sidebar Background"), CubaseTheme::sidebarBackground());
    applyStyle(btnModuleHeaderBackground, tr("Module Header Background"), CubaseTheme::moduleHeaderBackground());
    applyStyle(btnButtonBackground, tr("Button Background"), CubaseTheme::buttonBackground());
    applyStyle(btnInputBackground, tr("Input Background"), CubaseTheme::inputBackground());
    applyStyle(btnMenuBackground, tr("Menu Background"), CubaseTheme::menuBackground());
    applyStyle(btnTextPrimary, tr("Primary Text"), CubaseTheme::textPrimary());
    applyStyle(btnTextSecondary, tr("Secondary Text"), CubaseTheme::textSecondary());

    applyStyle(btnAccentBlue, tr("Accent Blue"), CubaseTheme::accentBlue());
    applyStyle(btnAccentOrange, tr("Accent Orange"), CubaseTheme::accentOrange());
    applyStyle(btnAccentCyan, tr("Accent Cyan"), CubaseTheme::accentCyan());
    applyStyle(btnAccentGreen, tr("Accent Green"), CubaseTheme::accentGreen());
    applyStyle(btnAccentPurple, tr("Accent Purple"), CubaseTheme::accentPurple());
    applyStyle(btnNoteGlow, tr("Note Glow"), CubaseTheme::noteGlow());
    applyStyle(btnBorderSubtle, tr("Subtle Border"), CubaseTheme::borderSubtle());
    applyStyle(btnBorderBright, tr("Bright Border"), CubaseTheme::borderBright());

    applyStyle(btnLocatorHandle, tr("Locator Handles [L/R]"), CubaseTheme::locatorHandle());
    applyStyle(btnLocatorRangeOverlay, tr("Locator Range Overlay"), CubaseTheme::locatorRangeOverlay());

    applyStyle(btnGridWhiteKey, tr("White Key Row"), CubaseTheme::gridWhiteKeyRow());
    applyStyle(btnGridBlackKey, tr("Black Key Row"), CubaseTheme::gridBlackKeyRow());
    applyStyle(btnTonicTint, tr("Tonic Tint"), CubaseTheme::pianoRollTonicTint());
    btnTonicTint.setOverlayPreviewBase(CubaseTheme::gridWhiteKeyRow());
    btnTonicTint.setTooltip(tr("Tonic Tint") + "  "
        + CubaseTheme::pianoRollTonicTint().toDisplayString(true) + "\n"
        + tr("Overlay on White Key Row"));

    applyStyle(btnGridBarLine, tr("Bar Line"), CubaseTheme::gridBarLine());
    applyStyle(btnGridBeatLine, tr("Beat Line"), CubaseTheme::gridBeatLine());
    applyStyle(btnGridSubBeat, tr("Sub-Beat Line"), CubaseTheme::gridSubBeat());
}

void PreferencesComponent::ThemeTab::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (activeColourSelector != nullptr && source == activeColourSelector.getComponent())
    {
        if (activeColorKey.isNotEmpty())
        {
            CubaseTheme::applyCustomColor(activeColorKey, activeColourSelector->getCurrentColour());
        }
    }
}

void PreferencesComponent::ThemeTab::pickColor(const juce::String& title, const juce::String& colorKey, juce::Colour initial)
{
    activeColorKey = colorKey;
    if (activeColourSelector != nullptr)
    {
        activeColourSelector->removeChangeListener(this);
        if (auto* window = activeColourSelector->findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState(0);
    }

    int flags = juce::ColourSelector::showColourAtTop
              | juce::ColourSelector::showSliders
              | juce::ColourSelector::showColourspace;
    if (colorKey == "locatorRangeOverlay" || colorKey == "pianoRollTonicTint")
        flags |= juce::ColourSelector::showAlphaChannel;
    auto* selector = new juce::ColourSelector(flags);
    selector->setCurrentColour(initial);
    selector->setSize(300, 280);
    selector->addChangeListener(this);
    activeColourSelector = selector;

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = tr("Choose Color: ") + title;
    options.dialogBackgroundColour = CubaseTheme::bgPanel();
    options.content.setOwned(selector);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;

    options.launchAsync();
}

void PreferencesComponent::ThemeTab::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgPanel());

    auto area = getLocalBounds().reduced(20, 12);
    area.removeFromTop(18 + 4 + 30 + 8 + 30 + 8);
    const auto preview = area.removeFromTop(58);
    g.setColour(CubaseTheme::bgDeep());
    g.fillRoundedRectangle(preview.toFloat(), 7.0f);
    g.setColour(CubaseTheme::borderSubtle());
    g.drawRoundedRectangle(preview.toFloat().reduced(0.5f), 7.0f, 1.0f);

    auto content = preview.reduced(9);
    auto sample = content.removeFromLeft(std::min(340, content.getWidth() * 54 / 100));
    g.setColour(CubaseTheme::sidebarBackground());
    g.fillRoundedRectangle(sample.toFloat(), 5.0f);
    sample.reduce(5, 4);
    g.setColour(CubaseTheme::bgPanel());
    g.fillRoundedRectangle(sample.toFloat(), 4.0f);
    g.setColour(CubaseTheme::moduleHeaderBackground());
    g.fillRoundedRectangle(sample.toFloat().withHeight(18.0f), 4.0f);
    g.setColour(CubaseTheme::textPrimary());
    g.setFont(CubaseTheme::getFont(11.5f, true));
    g.drawFittedText(presetCombo.getText(), sample.getX() + 10, sample.getY() + 3,
                     sample.getWidth() - 20, 18, juce::Justification::centredLeft, 1);
    g.setColour(CubaseTheme::textSecondary());
    g.setFont(CubaseTheme::getFont(10.0f));
    g.drawText("Harmonizer Studio", sample.getX() + 10, sample.getY() + 21,
               sample.getWidth() - 20, 15, juce::Justification::centredLeft);

    const juce::Colour accents[] = { CubaseTheme::accentBlue(), CubaseTheme::accentOrange(),
                                     CubaseTheme::accentCyan(), CubaseTheme::accentGreen(),
                                     CubaseTheme::accentPurple() };
    const int chipWidth = std::min(36, std::max(12, (content.getWidth() - 32) / 5));
    const int startX = content.getX() + (content.getWidth() - (5 * chipWidth + 4 * 8)) / 2;
    for (int i = 0; i < 5; ++i)
    {
        g.setColour(accents[i]);
        g.fillRoundedRectangle((float)(startX + i * (chipWidth + 8)),
                               (float)content.getCentreY() - 13.0f,
                               (float)chipWidth, 26.0f, 4.0f);
    }
}

void PreferencesComponent::ThemeTab::resized()
{
    auto area = getLocalBounds().reduced(20, 12);
    lblPresetTitle.setBounds(area.removeFromTop(18));
    area.removeFromTop(4);
    auto presetRow = area.removeFromTop(30);
    btnResetTheme.setBounds(presetRow.removeFromRight(150));
    presetRow.removeFromRight(10);
    presetCombo.setBounds(presetRow);
    area.removeFromTop(8);
    auto actions = area.removeFromTop(30);
    const int saveWidth = std::min(210, actions.getWidth() * 32 / 100);
    btnSaveAsPreset.setBounds(actions.removeFromLeft(saveWidth));
    actions.removeFromLeft(8);
    const int actionWidth = (actions.getWidth() - 16) / 3;
    btnRenamePreset.setBounds(actions.removeFromLeft(actionWidth));
    actions.removeFromLeft(8);
    btnOverwritePreset.setBounds(actions.removeFromLeft(actionWidth));
    actions.removeFromLeft(8);
    btnDeletePreset.setBounds(actions);
    area.removeFromTop(8 + 58 + 12); // Live palette preview is painted behind this space.

    auto layoutRow = [&area](std::initializer_list<ThemeColourButton*> buttons) {
        auto row = area.removeFromTop(30);
        const int count = (int)buttons.size();
        const int width = (row.getWidth() - (count - 1) * 8) / count;
        int i = 0;
        for (auto* button : buttons)
        {
            button->setBounds(row.removeFromLeft(i == count - 1 ? row.getWidth() : width));
            if (++i < count) row.removeFromLeft(8);
        }
    };

    lblCustomColorsTitle.setBounds(area.removeFromTop(20));
    area.removeFromTop(4);
    layoutRow({ &btnBgDeep, &btnBgSurface, &btnBgPanel });
    area.removeFromTop(6);
    layoutRow({ &btnToolbarBackground, &btnSidebarBackground, &btnModuleHeaderBackground });
    area.removeFromTop(6);
    layoutRow({ &btnButtonBackground, &btnInputBackground, &btnMenuBackground });
    area.removeFromTop(6);
    layoutRow({ &btnTextPrimary, &btnTextSecondary, &btnBorderSubtle });
    area.removeFromTop(6);
    layoutRow({ &btnBorderBright, &btnAccentBlue, &btnAccentOrange });
    area.removeFromTop(6);
    layoutRow({ &btnAccentCyan, &btnAccentGreen, &btnAccentPurple });
    area.removeFromTop(12);

    lblLocatorColorsTitle.setBounds(area.removeFromTop(20));
    area.removeFromTop(4);
    layoutRow({ &btnNoteGlow, &btnLocatorHandle, &btnLocatorRangeOverlay });
    area.removeFromTop(12);

    lblPianoRollColorsTitle.setBounds(area.removeFromTop(20));
    area.removeFromTop(4);
    layoutRow({ &btnGridWhiteKey, &btnGridBlackKey, &btnTonicTint });
    area.removeFromTop(6);
    layoutRow({ &btnGridBarLine, &btnGridBeatLine, &btnGridSubBeat });
}

//==============================================================================
// Language Tab
//==============================================================================
PreferencesComponent::LanguageTab::LanguageTab(std::function<void()> onLangChanged)
    : langChangedCallback(onLangChanged)
{
    langCombo.addItem("English", 1);
    langCombo.addItem(juce::String::fromUTF8("简体中文 (Simplified Chinese)"), 2);

    auto curLang = Localization::getInstance().getLanguage();
    langCombo.setSelectedId(curLang == Language::English ? 1 : 2, juce::dontSendNotification);

    langCombo.onChange = [this]() {
        auto selectedId = langCombo.getSelectedId();
        Localization::getInstance().setLanguage(selectedId == 1 ? Language::English : Language::SimplifiedChinese);
        lblTip.setText(tr("Changes take effect immediately across all windows."), juce::dontSendNotification);
        if (langChangedCallback)
            langChangedCallback();
        repaint();
    };
    addAndMakeVisible(langCombo);

    lblTip.setText(tr("Changes take effect immediately across all windows."), juce::dontSendNotification);
    lblTip.setFont(CubaseTheme::getFont(12.0f, false));
    lblTip.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    addAndMakeVisible(lblTip);
}

void PreferencesComponent::LanguageTab::paint(juce::Graphics& g)
{
    g.setColour(CubaseTheme::bgPanel());
    g.fillAll();

    auto area = getLocalBounds().reduced(20);

    g.setColour(CubaseTheme::accentOrange());
    g.setFont(CubaseTheme::getFont(14.0f, true));
    g.drawText(tr("Select Interface Language:"), area.removeFromTop(28), juce::Justification::left);
}

void PreferencesComponent::LanguageTab::resized()
{
    auto area = getLocalBounds().reduced(20);
    area.removeFromTop(35);
    langCombo.setBounds(area.removeFromTop(30).removeFromLeft(300));
    area.removeFromTop(12);
    lblTip.setBounds(area.removeFromTop(25));
}

void PreferencesComponent::LanguageTab::themeChanged()
{
    langCombo.setColour(juce::ComboBox::backgroundColourId, CubaseTheme::inputBackground());
    langCombo.setColour(juce::ComboBox::textColourId, CubaseTheme::textPrimary());
    langCombo.setColour(juce::ComboBox::outlineColourId, CubaseTheme::borderSubtle());
    langCombo.setColour(juce::ComboBox::arrowColourId, CubaseTheme::textSecondary());
    lblTip.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    sendLookAndFeelChange();
    repaint();
}

//==============================================================================
// General Preferences Tab
//==============================================================================
PreferencesComponent::GeneralTab::GeneralTab(AudioEngine& engine)
    : audioEngine(engine)
{
    lblMidiPartOpenTitle.setFont(CubaseTheme::getFont(13.0f, true));
    lblMidiPartOpenTitle.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    addAndMakeVisible(lblMidiPartOpenTitle);
    midiPartOpenCombo.onChange = [this]() {
        int id = midiPartOpenCombo.getSelectedId();
        if (id == 1) GeneralSettings::getInstance().setMidiPartOpenMode(MidiPartOpenMode::SeparateWindow);
        else if (id == 2) GeneralSettings::getInstance().setMidiPartOpenMode(MidiPartOpenMode::LowerZone);
        else if (id == 3) GeneralSettings::getInstance().setMidiPartOpenMode(MidiPartOpenMode::MainScreen);
    };
    addAndMakeVisible(midiPartOpenCombo);
    lblTip.setFont(CubaseTheme::getFont(11.0f, false));
    lblTip.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    addAndMakeVisible(lblTip);

    lblPausePlayheadTitle.setFont(CubaseTheme::getFont(13.0f, true));
    addAndMakeVisible(lblPausePlayheadTitle);
    pausePlayheadCombo.onChange = [this]() {
        const auto mode = pausePlayheadCombo.getSelectedId() == 2
            ? PausePlayheadMode::ReturnToPlaybackStart : PausePlayheadMode::StayAtPause;
        GeneralSettings::getInstance().setPausePlayheadMode(mode);
        audioEngine.setPausePlayheadMode(mode);
    };
    addAndMakeVisible(pausePlayheadCombo);
    lblPausePlayheadTip.setFont(CubaseTheme::getFont(11.0f, false));
    addAndMakeVisible(lblPausePlayheadTip);

    updateText();
    themeChanged();
}

void PreferencesComponent::GeneralTab::updateText()
{
    const bool chinese = Localization::getInstance().getLanguage() == Language::SimplifiedChinese;
    lblMidiPartOpenTitle.setText(chinese
        ? juce::String::fromUTF8(u8"双击 MIDI Part 时的默认打开方式：")
        : juce::String("Default action when double-clicking a MIDI part:"),
        juce::dontSendNotification);
    const int openId = (int)GeneralSettings::getInstance().getMidiPartOpenMode() + 1;
    midiPartOpenCombo.clear(juce::dontSendNotification);
    midiPartOpenCombo.addItem(chinese ? juce::String::fromUTF8(u8"在独立窗口中打开")
                                      : juce::String("Open in a separate window"), 1);
    midiPartOpenCombo.addItem(chinese ? juce::String::fromUTF8(u8"在主界面下方区域打开")
                                      : juce::String("Open in the lower zone"), 2);
    midiPartOpenCombo.addItem(chinese ? juce::String::fromUTF8(u8"在主界面屏幕打开")
                                      : juce::String("Open in the main view"), 3);
    midiPartOpenCombo.setSelectedId(openId, juce::dontSendNotification);
    lblTip.setText(chinese
        ? juce::String::fromUTF8(u8"控制双击编排区域中的 MIDI Part 时，钢琴卷帘窗的默认显示位置。")
        : juce::String("Choose where the piano roll opens when you double-click a MIDI part."),
        juce::dontSendNotification);

    lblPausePlayheadTitle.setText(chinese ? juce::String::fromUTF8(u8"暂停播放时的光标位置：")
                                          : juce::String("Playhead position after pausing:"),
                                  juce::dontSendNotification);
    const int pauseId = GeneralSettings::getInstance().getPausePlayheadMode()
        == PausePlayheadMode::ReturnToPlaybackStart ? 2 : 1;
    pausePlayheadCombo.clear(juce::dontSendNotification);
    pausePlayheadCombo.addItem(chinese ? juce::String::fromUTF8(u8"停留在暂停位置")
                                         : juce::String("Stay at paused position"), 1);
    pausePlayheadCombo.addItem(chinese ? juce::String::fromUTF8(u8"回到本次播放的起始位置")
                                         : juce::String("Return to playback start"), 2);
    pausePlayheadCombo.setSelectedId(pauseId, juce::dontSendNotification);
    lblPausePlayheadTip.setText(chinese
        ? juce::String::fromUTF8(u8"按空格或播放按钮暂停时生效；“停止”按钮始终回到项目起点。")
        : juce::String("Applies to Pause via Space or Play. Stop always returns to the project start."),
        juce::dontSendNotification);
    repaint();
}

void PreferencesComponent::GeneralTab::paint(juce::Graphics& g)
{
    g.setColour(CubaseTheme::bgPanel());
    g.fillAll();

    auto area = getLocalBounds().reduced(20);
    g.setColour(CubaseTheme::accentOrange());
    g.setFont(CubaseTheme::getFont(14.0f, true));
    g.drawText(tr("General / Editing Preferences:"), area.removeFromTop(28), juce::Justification::left);
}

void PreferencesComponent::GeneralTab::resized()
{
    auto area = getLocalBounds().reduced(20);
    area.removeFromTop(35);
    lblMidiPartOpenTitle.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    midiPartOpenCombo.setBounds(area.removeFromTop(30).removeFromLeft(360));
    area.removeFromTop(10);
    lblTip.setBounds(area.removeFromTop(36));
    area.removeFromTop(24);
    lblPausePlayheadTitle.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);
    pausePlayheadCombo.setBounds(area.removeFromTop(30).removeFromLeft(std::min(360, area.getWidth())));
    area.removeFromTop(10);
    lblPausePlayheadTip.setBounds(area.removeFromTop(36));
}

void PreferencesComponent::GeneralTab::themeChanged()
{
    midiPartOpenCombo.setColour(juce::ComboBox::backgroundColourId, CubaseTheme::inputBackground());
    midiPartOpenCombo.setColour(juce::ComboBox::textColourId, CubaseTheme::textPrimary());
    midiPartOpenCombo.setColour(juce::ComboBox::outlineColourId, CubaseTheme::borderSubtle());
    midiPartOpenCombo.setColour(juce::ComboBox::arrowColourId, CubaseTheme::textSecondary());
    pausePlayheadCombo.setColour(juce::ComboBox::backgroundColourId, CubaseTheme::inputBackground());
    pausePlayheadCombo.setColour(juce::ComboBox::textColourId, CubaseTheme::textPrimary());
    pausePlayheadCombo.setColour(juce::ComboBox::outlineColourId, CubaseTheme::borderSubtle());
    pausePlayheadCombo.setColour(juce::ComboBox::arrowColourId, CubaseTheme::textSecondary());
    lblMidiPartOpenTitle.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblTip.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    lblPausePlayheadTitle.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblPausePlayheadTip.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    sendLookAndFeelChange();
    repaint();
}

//==============================================================================
// Preferences Window
//==============================================================================
PreferencesWindow::PreferencesWindow(AudioEngine& engine)
    : juce::DocumentWindow(tr("Preferences & Settings"),
                           CubaseTheme::bgDeep(),
                           juce::DocumentWindow::closeButton),
      engineRef(engine)
{
    setUsingNativeTitleBar(true);
    setAlwaysOnTop(true); // Always on top as requested!
    setContentOwned(new PreferencesComponent(engine), true);
    centreWithSize(720, 700);
    setVisible(true);
}

void PreferencesWindow::closeButtonPressed()
{
    engineRef.saveAudioSettings();
    setVisible(false);
}

} // namespace Harmonizer
