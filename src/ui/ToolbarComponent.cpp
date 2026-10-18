#include "ToolbarComponent.h"
#include "CubaseTheme.h"
#include "../core/Localization.h"

namespace Harmonizer
{

//==============================================================================
// ViewSwitcherButton Implementation
//==============================================================================
ToolbarComponent::ViewSwitcherButton::ViewSwitcherButton()
    : juce::Button("ViewSwitch")
{
    setTooltip(juce::String::fromUTF8(u8"切换工作区视图 (轨道 / 卷帘 / 浮窗 / 分割)"));
}

void ToolbarComponent::ViewSwitcherButton::setViewMode(int layout)
{
    currentLayout = layout;
    repaint();
}

void ToolbarComponent::ViewSwitcherButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    if (isButtonDown)
        bounds = bounds.translated(0.0f, 0.5f);

    juce::Colour bgCol = CubaseTheme::buttonBackground();
    juce::Colour borderCol = CubaseTheme::borderSubtle();
    if (isButtonDown)
    {
        bgCol = bgCol.brighter(0.12f);
        borderCol = CubaseTheme::borderBright();
    }
    else if (isMouseOver)
    {
        bgCol = bgCol.brighter(0.06f);
        borderCol = CubaseTheme::borderBright();
    }

    g.setColour(bgCol);
    g.fillRoundedRectangle(bounds, 3.5f);

    g.setColour(borderCol);
    g.drawRoundedRectangle(bounds, 3.5f, 1.0f);

    auto contentArea = bounds.reduced(6.0f, 2.0f);
    auto iconArea = contentArea.removeFromLeft(16.0f);
    contentArea.removeFromLeft(4.0f);
    auto textArea = contentArea;

    DAWIcon icon = DAWIcon::ViewSplit;
    juce::String text = juce::String::fromUTF8(u8"分割视图");
    if (currentLayout == 1)
    {
        icon = DAWIcon::ViewArranger;
        text = juce::String::fromUTF8(u8"轨道视图");
    }
    else if (currentLayout == 2)
    {
        icon = DAWIcon::ViewPianoRoll;
        text = juce::String::fromUTF8(u8"卷帘视图");
    }
    else if (currentLayout == 3)
    {
        icon = DAWIcon::ViewDualWindow;
        text = juce::String::fromUTF8(u8"卷帘浮窗");
    }

    CubaseTheme::drawDAWIcon(g, icon, iconArea, isMouseOver ? CubaseTheme::accentCyan() : CubaseTheme::textPrimary());

    g.setFont(CubaseTheme::getFont(12.0f, false));
    g.setColour(isMouseOver ? CubaseTheme::textPrimary().brighter(0.15f) : CubaseTheme::textPrimary());
    g.drawText(text, textArea, juce::Justification::centred, false);
}


//==============================================================================
// VectorIconButton Implementation
//==============================================================================
ToolbarComponent::VectorIconButton::VectorIconButton(DAWIcon iconType, const juce::String& tooltipText)
    : juce::Button(tooltipText), type(iconType)
{
    if (tooltipText.isNotEmpty())
        setTooltip(tooltipText);
}

void ToolbarComponent::VectorIconButton::setIconType(DAWIcon newType)
{
    type = newType;
    repaint();
}

void ToolbarComponent::VectorIconButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    bool isToggled = getToggleState();

    juce::Colour bgCol = CubaseTheme::buttonBackground();
    juce::Colour borderCol = CubaseTheme::borderSubtle();
    juce::Colour iconCol = CubaseTheme::textSecondary();

    if (isToggled)
    {
        bgCol = CubaseTheme::accentCyan().withAlpha(0.20f);
        borderCol = CubaseTheme::accentCyan();
        iconCol = CubaseTheme::accentCyan().brighter(0.35f);
    }
    else if (isButtonDown)
    {
        bgCol = bgCol.brighter(0.12f);
        iconCol = CubaseTheme::textPrimary();
    }
    else if (isMouseOver)
    {
        bgCol = bgCol.brighter(0.06f);
        iconCol = CubaseTheme::textPrimary();
    }

    float corner = 3.5f;
    g.setColour(bgCol);
    g.fillRoundedRectangle(bounds, corner);

    g.setColour(borderCol);
    g.drawRoundedRectangle(bounds, corner, 1.0f);

    auto iconArea = bounds.reduced(bounds.getWidth() * 0.20f, bounds.getHeight() * 0.20f);
    CubaseTheme::drawDAWIcon(g, type, iconArea, iconCol, isToggled);
}


//==============================================================================
// ToolbarComponent Implementation
//==============================================================================
ToolbarComponent::ToolbarComponent(MidiModel& model, juce::UndoManager& um,
                                   std::function<void()> onToggleView,
                                   std::function<void(juce::Component*)> onShowSectionsMenu,
                                   std::function<void()> onToggleLeftZone,
                                   std::function<void()> onToggleRightZone)
    : midiModel(model), undoManager(um),
      toggleViewCallback(onToggleView),
      showSectionsMenuCallback(onShowSectionsMenu),
      toggleLeftZoneCallback(onToggleLeftZone),
      toggleRightZoneCallback(onToggleRightZone)
{
    midiModel.addListener(this);
    Localization::getInstance().addListener(this);
    CubaseTheme::addThemeListener(this);

    // 1. View Toggle with dynamic Cubase style vector icon & text
    updateViewButtonDisplay();
    btnToggleView.onClick = toggleViewCallback;
    addAndMakeVisible(btnToggleView);

    // 2. Editing Tools (Cubase 1, 3, 5, 8) with Vector Icons
    btnSelect.onClick = [this]() { midiModel.setCurrentTool(ToolType::Select); };
    btnSplit.onClick  = [this]() { midiModel.setCurrentTool(ToolType::Split); };
    btnErase.onClick  = [this]() { midiModel.setCurrentTool(ToolType::Erase); };
    btnDraw.onClick   = [this]() { midiModel.setCurrentTool(ToolType::Draw); };

    addAndMakeVisible(btnSelect);
    addAndMakeVisible(btnSplit);
    addAndMakeVisible(btnErase);
    addAndMakeVisible(btnDraw);
    updateToolHighlights();

    // 3. Snap Toggle
    btnSnapToggle.setButtonText(tr("Snap [J]"));
    btnSnapToggle.setColour(juce::ToggleButton::textColourId, CubaseTheme::textPrimary());
    btnSnapToggle.setToggleState(midiModel.isSnapEnabled(), juce::dontSendNotification);
    btnSnapToggle.onClick = [this]() { midiModel.setSnapEnabled(btnSnapToggle.getToggleState()); };
    addAndMakeVisible(btnSnapToggle);

    // 4. Snap Divisions
    snapCombo.onChange = [this]() {
        midiModel.setSnapDivision((SnapDivision)(snapCombo.getSelectedId() - 1));
    };
    addAndMakeVisible(snapCombo);

    // 5. Triplet Toggle
    btnTriplet.setButtonText(tr("Triplet"));
    btnTriplet.setColour(juce::ToggleButton::textColourId, CubaseTheme::textPrimary());
    btnTriplet.setToggleState(midiModel.isSnapTriplet(), juce::dontSendNotification);
    btnTriplet.onClick = [this]() {
        midiModel.setSnapTriplet(btnTriplet.getToggleState());
        updateSnapCombo();
    };
    addAndMakeVisible(btnTriplet);

    updateSnapCombo();

    // 6. Window Layout Controls (Cubase Left & Right Zone Toggles with vector icons)
    btnToggleLeftZone.setTooltip(juce::String::fromUTF8(u8"切换左侧检查器分区 (Toggle Left Zone)"));
    btnToggleLeftZone.onClick = [this]() {
        if (toggleLeftZoneCallback) toggleLeftZoneCallback();
    };
    addAndMakeVisible(btnToggleLeftZone);

    btnToggleRightZone.setTooltip(juce::String::fromUTF8(u8"切换右侧检查器分区 (Toggle Right Zone)"));
    btnToggleRightZone.onClick = [this]() {
        if (toggleRightZoneCallback) toggleRightZoneCallback();
    };
    addAndMakeVisible(btnToggleRightZone);

    // 7. Inspector Sections Menu Button
    btnInspectorSections.setButtonText(juce::String::fromUTF8(u8"分区管理"));
    btnInspectorSections.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());
    btnInspectorSections.onClick = [this]() {
        if (showSectionsMenuCallback)
            showSectionsMenuCallback(&btnInspectorSections);
    };
    addAndMakeVisible(btnInspectorSections);

    // 8. Multi-track visible in Piano Roll
    btnVisibleTracks.setButtonText(juce::String::fromUTF8(u8"显示轨道"));
    btnVisibleTracks.setTooltip(juce::String::fromUTF8(u8"选择在钢琴卷帘窗中并发显示的轨道 (Visible Tracks)"));
    btnVisibleTracks.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());
    btnVisibleTracks.onClick = [this]() { showVisibleTracksMenu(); };
    addAndMakeVisible(btnVisibleTracks);

    // 9. Note Coloring Mode Combo
    coloringModeCombo.addItem(juce::String::fromUTF8(u8"调式自适应 (Scale Adaptive)"), 1);
    coloringModeCombo.addItem(juce::String::fromUTF8(u8"级数功能 (Degree Function)"), 2);
    coloringModeCombo.addItem(juce::String::fromUTF8(u8"统一蓝色 (Uniform Blue)"), 3);
    coloringModeCombo.addItem(juce::String::fromUTF8(u8"乐器轨颜色 (Instrument Track Color)"), 4);
    coloringModeCombo.addItem(juce::String::fromUTF8(u8"MIDI Part 颜色 (MIDI Part Color)"), 5);
    coloringModeCombo.setSelectedId((int)midiModel.getColoringMode() + 1, juce::dontSendNotification);
    coloringModeCombo.onChange = [this]() {
        int id = coloringModeCombo.getSelectedId();
        midiModel.setColoringMode((ColoringMode)(id - 1));
    };
    coloringModeCombo.setTooltip(juce::String::fromUTF8(u8"音符着色方案 (Note Coloring Mode)"));
    addAndMakeVisible(coloringModeCombo);

    // 10. Follow Playhead Mode Combo
    followPlayheadCombo.addItem(juce::String::fromUTF8(u8"光标: 不追踪"), 1);
    followPlayheadCombo.addItem(juce::String::fromUTF8(u8"光标: 翻页追踪"), 2);
    followPlayheadCombo.addItem(juce::String::fromUTF8(u8"光标: 开头追踪"), 3);
    followPlayheadCombo.addItem(juce::String::fromUTF8(u8"光标: 居中追踪"), 4);
    followPlayheadCombo.setSelectedId((int)midiModel.getPlayheadFollowMode() + 1, juce::dontSendNotification);
    followPlayheadCombo.onChange = [this]() {
        int id = followPlayheadCombo.getSelectedId();
        midiModel.setPlayheadFollowMode((PlayheadFollowMode)(id - 1));
    };
    followPlayheadCombo.setTooltip(juce::String::fromUTF8(u8"播放跟随光标模式 (Follow Playhead / Auto-Scroll)"));
    addAndMakeVisible(followPlayheadCombo);

    themeChanged();
}

ToolbarComponent::~ToolbarComponent()
{
    CubaseTheme::removeThemeListener(this);
    Localization::getInstance().removeListener(this);
    midiModel.removeListener(this);
}

void ToolbarComponent::themeChanged()
{
    btnSnapToggle.setColour(juce::ToggleButton::textColourId, CubaseTheme::textPrimary());
    btnTriplet.setColour(juce::ToggleButton::textColourId, CubaseTheme::textPrimary());

    btnInspectorSections.setColour(juce::TextButton::buttonColourId, CubaseTheme::buttonBackground());
    btnInspectorSections.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());

    btnVisibleTracks.setColour(juce::TextButton::buttonColourId, CubaseTheme::buttonBackground());
    btnVisibleTracks.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());

    btnToggleView.setColour(juce::TextButton::buttonColourId, CubaseTheme::buttonBackground());
    btnToggleView.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());

    snapCombo.setColour(juce::ComboBox::backgroundColourId, CubaseTheme::inputBackground());
    snapCombo.setColour(juce::ComboBox::textColourId, CubaseTheme::textPrimary());
    snapCombo.setColour(juce::ComboBox::outlineColourId, CubaseTheme::borderSubtle());
    snapCombo.setColour(juce::ComboBox::arrowColourId, CubaseTheme::textSecondary());

    coloringModeCombo.setColour(juce::ComboBox::backgroundColourId, CubaseTheme::inputBackground());
    coloringModeCombo.setColour(juce::ComboBox::textColourId, CubaseTheme::textPrimary());
    coloringModeCombo.setColour(juce::ComboBox::outlineColourId, CubaseTheme::borderSubtle());
    coloringModeCombo.setColour(juce::ComboBox::arrowColourId, CubaseTheme::textSecondary());

    followPlayheadCombo.setColour(juce::ComboBox::backgroundColourId, CubaseTheme::inputBackground());
    followPlayheadCombo.setColour(juce::ComboBox::textColourId, CubaseTheme::textPrimary());
    followPlayheadCombo.setColour(juce::ComboBox::outlineColourId, CubaseTheme::borderSubtle());
    followPlayheadCombo.setColour(juce::ComboBox::arrowColourId, CubaseTheme::textSecondary());

    followPlayheadCombo.repaint();
    coloringModeCombo.repaint();
    snapCombo.repaint();
    btnToggleLeftZone.repaint();
    btnToggleRightZone.repaint();
    btnSelect.repaint();
    btnSplit.repaint();
    btnErase.repaint();
    btnDraw.repaint();
    sendLookAndFeelChange();
    repaint();
}

void ToolbarComponent::setViewMode(int layout)
{
    currentViewLayout = layout;
    updateViewButtonDisplay();
}

void ToolbarComponent::setZoneStates(bool leftVisible, bool rightVisible)
{
    btnToggleLeftZone.setToggleState(leftVisible, juce::dontSendNotification);
    btnToggleRightZone.setToggleState(rightVisible, juce::dontSendNotification);
    repaint();
}

void ToolbarComponent::updateViewButtonDisplay()
{
    btnToggleView.setViewMode(currentViewLayout);
}


void ToolbarComponent::updateToolHighlights()
{
    auto activeTool = midiModel.getCurrentTool();
    btnSelect.setToggleState(activeTool == ToolType::Select, juce::dontSendNotification);
    btnSplit.setToggleState(activeTool == ToolType::Split, juce::dontSendNotification);
    btnErase.setToggleState(activeTool == ToolType::Erase, juce::dontSendNotification);
    btnDraw.setToggleState(activeTool == ToolType::Draw, juce::dontSendNotification);

    if (activeTool == ToolType::Select) btnSelect.toFront(false);
    else if (activeTool == ToolType::Split) btnSplit.toFront(false);
    else if (activeTool == ToolType::Erase) btnErase.toFront(false);
    else if (activeTool == ToolType::Draw) btnDraw.toFront(false);

    repaint();
}


void ToolbarComponent::languageChanged()
{
    updateViewButtonDisplay();
    btnSnapToggle.setButtonText(tr("Snap [J]"));
    btnTriplet.setButtonText(tr("Triplet"));
    btnInspectorSections.setButtonText(juce::String::fromUTF8(u8"分区管理"));
    repaint();
}

void ToolbarComponent::toolChanged()
{
    updateToolHighlights();
}

void ToolbarComponent::updateSnapCombo()
{
    snapCombo.clear(juce::dontSendNotification);
    if (midiModel.isSnapTriplet())
    {
        snapCombo.addItem("1/2 Triplet", (int)SnapDivision::Half + 1);
        snapCombo.addItem("1/4 Triplet", (int)SnapDivision::Quarter + 1);
        snapCombo.addItem("1/8 Triplet", (int)SnapDivision::Eighth + 1);
        snapCombo.addItem("1/16 Triplet", (int)SnapDivision::Sixteenth + 1);
        snapCombo.addItem("1/32 Triplet", (int)SnapDivision::ThirtySecond + 1);
        snapCombo.addItem("1/64 Triplet", (int)SnapDivision::SixtyFourth + 1);
    }
    else
    {
        snapCombo.addItem("1/1 Bar", (int)SnapDivision::Bar + 1);
        snapCombo.addItem("1/2 Note", (int)SnapDivision::Half + 1);
        snapCombo.addItem("1/4 Note", (int)SnapDivision::Quarter + 1);
        snapCombo.addItem("1/8 Note", (int)SnapDivision::Eighth + 1);
        snapCombo.addItem("1/16 Note", (int)SnapDivision::Sixteenth + 1);
        snapCombo.addItem("1/32 Note", (int)SnapDivision::ThirtySecond + 1);
        snapCombo.addItem("1/64 Note", (int)SnapDivision::SixtyFourth + 1);
    }
    snapCombo.setSelectedId((int)midiModel.getSnapDivision() + 1, juce::dontSendNotification);
}

void ToolbarComponent::projectSettingsChanged()
{
    btnSnapToggle.setToggleState(midiModel.isSnapEnabled(), juce::dontSendNotification);
    btnTriplet.setToggleState(midiModel.isSnapTriplet(), juce::dontSendNotification);
    updateSnapCombo();
    coloringModeCombo.setSelectedId((int)midiModel.getColoringMode() + 1, juce::dontSendNotification);
    followPlayheadCombo.setSelectedId((int)midiModel.getPlayheadFollowMode() + 1, juce::dontSendNotification);
}

void ToolbarComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setColour(CubaseTheme::toolbarBackground());
    g.fillRect(bounds);

    g.setColour(CubaseTheme::borderSubtle());
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, (float)bounds.getWidth());
}

void ToolbarComponent::resized()
{
    auto area = getLocalBounds().reduced(6, 6);
    int gap = 6;

    // 1. View switcher (Left)
    btnToggleView.setBounds(area.removeFromLeft(110));
    area.removeFromLeft(gap * 2);

    // 2. Editing tools (Independent regular rounded rectangles with uniform spacing)
    int toolW = 32;
    int toolGap = 3;
    btnSelect.setBounds(area.removeFromLeft(toolW));
    area.removeFromLeft(toolGap);
    btnSplit.setBounds(area.removeFromLeft(toolW));
    area.removeFromLeft(toolGap);
    btnErase.setBounds(area.removeFromLeft(toolW));
    area.removeFromLeft(toolGap);
    btnDraw.setBounds(area.removeFromLeft(toolW));
    area.removeFromLeft(gap * 2);



    // 3. Snap & Quantize
    btnSnapToggle.setBounds(area.removeFromLeft(76));
    area.removeFromLeft(gap);
    snapCombo.setBounds(area.removeFromLeft(96));
    area.removeFromLeft(gap);
    btnTriplet.setBounds(area.removeFromLeft(70));
    area.removeFromLeft(gap * 2);

    // Multi-track visibility in Piano Roll
    btnVisibleTracks.setBounds(area.removeFromLeft(82));
    area.removeFromLeft(gap);

    // Note Coloring Mode Combo
    coloringModeCombo.setBounds(area.removeFromLeft(145));
    area.removeFromLeft(gap);

    // Follow Playhead Mode Combo
    followPlayheadCombo.setBounds(area.removeFromLeft(125));

    // 4. Right side: Window Layout Controls & Sections button
    btnInspectorSections.setBounds(area.removeFromRight(82));
    area.removeFromRight(gap);
    btnToggleRightZone.setBounds(area.removeFromRight(34));
    area.removeFromRight(4);
    btnToggleLeftZone.setBounds(area.removeFromRight(34));
}

void ToolbarComponent::showVisibleTracksMenu()
{
    if (!trackModel) return;

    juce::PopupMenu menu;
    menu.addSectionHeader(juce::String::fromUTF8(u8"钢琴卷帘窗显示轨道"));

    std::vector<int> instrumentTrackIds;
    int idx = 1;
    for (auto& t : trackModel->getTracks())
    {
        if (t->getType() == TrackType::Instrument)
        {
            instrumentTrackIds.push_back(t->getId());
            bool isVis = trackModel->isPianoRollTrackVisible(t->getId());
            juce::String name = t->getName();
            if (name.isEmpty()) name = juce::String::fromUTF8(u8"乐器轨 ") + juce::String(idx);
            menu.addItem(idx, name, true, isVis);
            idx++;
        }
    }

    if (instrumentTrackIds.empty())
    {
        menu.addItem(9999, juce::String::fromUTF8(u8"(无可用乐器轨)"), false);
    }
    else
    {
        menu.addSeparator();
        menu.addItem(1001, juce::String::fromUTF8(u8"显示所有乐器轨"));
        menu.addItem(1002, juce::String::fromUTF8(u8"仅焦点/选中轨 (清除多显)"));
    }

    juce::Component::SafePointer<ToolbarComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&btnVisibleTracks),
        [this, safeThis, instrumentTrackIds](int result) {
            if (safeThis.getComponent() == nullptr) return;
            if (result <= 0 || !trackModel) return;

            if (result == 1001)
            {
                for (int trackId : instrumentTrackIds)
                    if (trackModel->getTrackById(trackId) != nullptr)
                        trackModel->setPianoRollTrackVisible(trackId, true);
            }
            else if (result == 1002)
            {
                trackModel->clearPianoRollVisibleTracks();
            }
            else if (result >= 1 && result <= (int)instrumentTrackIds.size())
            {
                int trackId = instrumentTrackIds[result - 1];
                if (trackModel->getTrackById(trackId) != nullptr)
                {
                    bool cur = trackModel->isPianoRollTrackVisible(trackId);
                    trackModel->setPianoRollTrackVisible(trackId, !cur);
                }
            }
        });
}

} // namespace Harmonizer
