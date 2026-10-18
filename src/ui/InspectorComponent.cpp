#include "InspectorComponent.h"
#include "CubaseTheme.h"
#include "EventEditors.h"
#include "../core/ModeAnalyzer.h"
#include "../core/Reharmonizer.h"
#include "../core/ChordClassifier.h"
#include "../core/UndoActions.h"
#include "../core/AppStateManager.h"
#include "../core/QuickChordCatalog.h"
#include "../core/ModalFunctionEngine.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <set>
#include <map>

namespace Harmonizer
{

namespace
{
constexpr std::array<const char*, 6> quickChordCategoryKeys = {
    "Triad", "Added", "Sixth", "Seventh", "Extended", "Altered"
};

int quickChordCategoryIndex(const juce::String& category)
{
    for (int i = 0; i < (int)quickChordCategoryKeys.size(); ++i)
        if (category == quickChordCategoryKeys[(size_t)i]) return i;
    return -1;
}

int quickChordColumns(int groupWidth)
{
    // Leave at least 76 px for long symbols such as maj13#11.
    const int usableWidth = std::max(0, groupWidth - 16);
    return std::clamp((usableWidth + 4) / 80, 2, 4);
}

constexpr int quickChordCategoryLabelHeight = 16;
constexpr int quickChordButtonHeight = 22;
constexpr int quickChordButtonRowGap = 2;
constexpr int quickChordCategoryGap = 5;
constexpr int quickChordPackedGap = 6;

int quickChordSingletonsPerRow(int groupWidth)
{
    constexpr int minPackedColumnWidth = 84;
    const int bodyWidth = std::max(0, groupWidth - 16);
    return std::max(1, (bodyWidth + quickChordPackedGap)
        / (minPackedColumnWidth + quickChordPackedGap));
}

int quickChordRowCount(int chordCount, int columns)
{
    if (chordCount <= 0) return 0;
    return (chordCount + columns - 1) / columns;
}
}

void SourceModeDisclosureButton::paintButton(juce::Graphics& g, bool hovered, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto accent = CubaseTheme::accentCyan();
    g.setColour(CubaseTheme::inputBackground().brighter(down ? 0.12f : hovered ? 0.07f : 0.0f));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(expanded ? accent.withAlpha(0.75f) : CubaseTheme::borderSubtle());
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    const int badgeWidth = juce::jlimit(44, 72, (int)status.length() * 7 + 14);
    const int badgeX = getWidth() - badgeWidth - 27;
    auto badge = juce::Rectangle<float>((float)badgeX, 10.0f,
                                         (float)badgeWidth, 22.0f);
    g.setColour(accent.withAlpha(0.13f));
    g.fillRoundedRectangle(badge, 4.0f);
    g.setColour(accent);
    g.setFont(CubaseTheme::getFont(10.0f, true));
    g.drawFittedText(status, badge.toNearestInt(), juce::Justification::centred, 1);

    const int textWidth = juce::jmax(0, badgeX - 20);
    g.setColour(CubaseTheme::textPrimary());
    g.setFont(CubaseTheme::getFont(11.0f, true));
    g.drawFittedText(getButtonText(), 10, 6, textWidth, 18,
                     juce::Justification::centredLeft, 1);
    g.setColour(CubaseTheme::textSecondary());
    g.setFont(CubaseTheme::getFont(10.5f));
    g.drawFittedText(summary, 10, 25, textWidth, 17,
                     juce::Justification::centredLeft, 1);

    const float cx = (float)getWidth() - 14.0f;
    const float cy = (float)getHeight() * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - 4.0f, cy + (expanded ? 2.0f : -2.0f));
    chevron.lineTo(cx, cy + (expanded ? -2.0f : 2.0f));
    chevron.lineTo(cx + 4.0f, cy + (expanded ? 2.0f : -2.0f));
    g.setColour(CubaseTheme::textSecondary());
    g.strokePath(chevron, juce::PathStrokeType(1.5f));
}

void ReharmonizationModeButton::paintButton(juce::Graphics& g, bool hovered, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto statusColour = availability == Availability::Supported
        ? CubaseTheme::accentGreen()
        : availability == Availability::Unavailable
            ? CubaseTheme::accentOrange() : CubaseTheme::textSecondary();
    auto background = CubaseTheme::inputBackground();
    if (availability != Availability::Unknown)
        background = background.interpolatedWith(statusColour, 0.13f);
    if (hovered || down)
        background = background.brighter(down ? 0.14f : 0.07f);
    g.setColour(background);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(getToggleState() || hasKeyboardFocus(true)
        ? CubaseTheme::accentBlue()
        : availability == Availability::Unknown
            ? CubaseTheme::borderSubtle() : statusColour.withAlpha(0.55f));
    g.drawRoundedRectangle(bounds, 5.0f, getToggleState() ? 1.5f : 1.0f);

    if (availability != Availability::Unknown)
    {
        g.setColour(statusColour);
        g.fillRoundedRectangle(6.0f, 6.0f, 3.0f,
                               (float)juce::jmax(0, getHeight() - 12), 1.5f);
    }
    g.setColour(availability == Availability::Unavailable && !getToggleState()
        ? CubaseTheme::textSecondary() : CubaseTheme::textPrimary());
    g.setFont(CubaseTheme::getFont(10.5f, getToggleState()));
    g.drawFittedText(getButtonText(), 14, 2, juce::jmax(0, getWidth() - 20),
                     juce::jmax(0, getHeight() - 4), juce::Justification::centred, 1);
}

void ReharmonizationModeButton::mouseEnter(const juce::MouseEvent& event)
{
    juce::Button::mouseEnter(event);
    if (onHoverChanged) onHoverChanged(true);
}

void ReharmonizationModeButton::mouseExit(const juce::MouseEvent& event)
{
    juce::Button::mouseExit(event);
    if (onHoverChanged) onHoverChanged(false);
}

class InspectorComponent::DockDropOverlay : public juce::Component
{
public:
    explicit DockDropOverlay(InspectorComponent& ownerIn) : owner(ownerIn)
    {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override
    {
        if (owner.dropIndicatorY < 0) return;

        g.setColour(CubaseTheme::accentCyan().withAlpha(0.55f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 7.0f, 2.0f);
        const int y = owner.dropIndicatorY - owner.viewport.getViewPositionY();
        if (y >= 0 && y < getHeight())
        {
            g.setColour(CubaseTheme::accentCyan());
            g.fillRoundedRectangle(7.0f, (float)y - 2.0f,
                                   (float)getWidth() - 14.0f, 4.0f, 2.0f);
        }
    }

private:
    InspectorComponent& owner;
};

int InspectorComponent::MarkerListModel::getNumRows()
{
    int activeTrkId = trackModel.getActiveTrackId();
    auto* trk = trackModel.getTrackById(activeTrkId);
    if (trk && trk->getType() == TrackType::Marker)
        return (int)trk->getMarkers().size();
    return 0;
}

void InspectorComponent::MarkerListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll(CubaseTheme::accentCyan().withAlpha(0.25f));
    else if (rowNumber % 2 == 1)
        g.fillAll(juce::Colour(0x08ffffff));

    int activeTrkId = trackModel.getActiveTrackId();
    auto* trk = trackModel.getTrackById(activeTrkId);
    if (!trk || trk->getType() != TrackType::Marker) return;
    const auto& markers = trk->getMarkers();
    if (rowNumber < 0 || rowNumber >= (int)markers.size()) return;

    const auto& m = markers[rowNumber];
    g.setColour(m.color);
    g.fillRoundedRectangle(4.0f, (float)(height - 12) * 0.5f, 10.0f, 12.0f, 2.0f);

    g.setFont(CubaseTheme::getFont(11.0f));
    g.setColour(rowIsSelected ? CubaseTheme::textPrimary() : CubaseTheme::textSecondary());
    juce::String text = juce::String(rowNumber + 1) + ". " + m.name + " (" + juce::String(m.startBeat, 1) + "b, " + juce::String(m.lengthBeats, 1) + "b)";
    g.drawText(text, 20, 0, width - 24, height, juce::Justification::centredLeft, true);
}

void InspectorComponent::MarkerListModel::selectedRowsChanged(int lastRowSelected)
{
    if (onSelect)
        onSelect(lastRowSelected);
}

InspectorComponent::InspectorComponent(MidiModel& model, TrackModel& tm, AudioEngine& engine,
                                       juce::UndoManager& um, std::function<void()> onOpenPrefs)
    : midiModel(model), trackModel(tm), audioEngine(engine), undoManager(um),
      openPrefsCallback(onOpenPrefs)
{
    midiModel.addListener(this);
    audioEngine.getVstHostManager().addListener(this);
    Localization::getInstance().addListener(this);
    CubaseTheme::addListener(this);

    auto setupLabel = [this](juce::Label& lbl, const juce::String& text, bool highlight = false) {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(juce::FontOptions(11.5f));
        lbl.setColour(juce::Label::textColourId, highlight ? CubaseTheme::accentOrange() : CubaseTheme::textPrimary());
    };

    // Enable smooth vertical scrolling
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&contentComp, false);
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(10);
    dropOverlay = std::make_unique<DockDropOverlay>(*this);
    addAndMakeVisible(dropOverlay.get());

    lblEmptyDock.setJustificationType(juce::Justification::centred);
    lblEmptyDock.setFont(CubaseTheme::getFont(11.5f));
    lblEmptyDock.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    lblEmptyDock.setInterceptsMouseClicks(false, false);
    contentComp.addChildComponent(lblEmptyDock);
    btnEmptyDockManage.onClick = [this]() {
        if (onOpenSectionsManager) onOpenSectionsManager();
    };
    contentComp.addChildComponent(btnEmptyDockManage);

    // 1. Note Details Group
    contentComp.addAndMakeVisible(grpNoteInfo);
    setupLabel(lblSelectionCount, "No Selection");
    grpNoteInfo.addAndMakeVisible(lblSelectionCount);
    auto setupRow = [this](NotePropertyRowComponent& row, const juce::String& text, juce::Colour mainCol) {
        row.setText(text, juce::dontSendNotification);
        row.setFont(juce::FontOptions(11.0f));
        row.setColours(CubaseTheme::textSecondary(), mainCol, CubaseTheme::textSecondary());
        grpNoteInfo.addAndMakeVisible(row);
    };

    setupRow(lblNotePitch, juce::String::fromUTF8(u8"音符音高: --"), CubaseTheme::textPrimary());
    setupRow(lblNoteStart, juce::String::fromUTF8(u8"起始位置: --"), CubaseTheme::textPrimary());
    setupRow(lblNoteLength, juce::String::fromUTF8(u8"音符长度: --"), CubaseTheme::textPrimary());
    setupRow(lblNoteVelocity, juce::String::fromUTF8(u8"按键力度: --"), CubaseTheme::textPrimary());
    setupRow(lblNoteScaleDegree, juce::String::fromUTF8(u8"调式音级: --"), CubaseTheme::accentCyan());
    setupRow(lblNoteFunctionalRole, juce::String::fromUTF8(u8"调式功能: --"), CubaseTheme::accentGold());
    setupRow(lblNoteMotionTendency, juce::String::fromUTF8(u8"运动倾向: --"), CubaseTheme::accentOrange());
    setupRow(lblNoteColorUsage, juce::String::fromUTF8(u8"色彩定位: --"), CubaseTheme::accentPurple());
    setupRow(lblNoteInterval, juce::String::fromUTF8(u8"音程关系: --"), CubaseTheme::accentCyan());
    grpNoteInfo.addMouseListener(this, true);

    // 2. Scale & Mode Analysis Group
    contentComp.addAndMakeVisible(grpScaleMode);

    static const char* noteRoots[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i)
        keyRootCombo.addItem(noteRoots[i], i + 1);
    keyRootCombo.onChange = [this]() {
        btnApplyCurrentMode.setEnabled(keyRootCombo.getSelectedId() > 0
                                       && scaleTypeCombo.getSelectedId() > 0);
    };
    grpScaleMode.addAndMakeVisible(keyRootCombo);

    int scaleIdx = 1;
    for (const auto& s : ScaleDefinitions::getAllScales())
        scaleTypeCombo.addItem(s.name, scaleIdx++);
    scaleTypeCombo.onChange = [this]() {
        btnApplyCurrentMode.setEnabled(keyRootCombo.getSelectedId() > 0
                                       && scaleTypeCombo.getSelectedId() > 0);
    };
    grpScaleMode.addAndMakeVisible(scaleTypeCombo);

    btnApplyCurrentMode.onClick = [this] { applyCurrentMode(); };
    grpScaleMode.addAndMakeVisible(btnApplyCurrentMode);

    btnCustomScale.setButtonText(juce::String::fromUTF8((const char*)u8"+ 自定义音阶..."));
    btnCustomScale.onClick = [this]() { openCustomScaleDialog(); };
    grpScaleMode.addAndMakeVisible(btnCustomScale);

    btnAnalyzeMode.setButtonText(juce::String::fromUTF8(u8"分析整轨"));
    btnAnalyzeMode.onClick = [this]() { runModeRecommendation(); };
    grpScaleMode.addAndMakeVisible(btnAnalyzeMode);

    setupLabel(lblModeAnalysisConfidence, juce::String::fromUTF8(u8"多选音符后显示调式可能性"));
    lblModeAnalysisConfidence.setFont(juce::FontOptions(11.0f));
    grpScaleMode.addAndMakeVisible(lblModeAnalysisConfidence);

    // 2b. Real-time Chord Detection Hero Card Group
    contentComp.addAndMakeVisible(grpChordDetection);
    lblDetectedChord.setText("None", juce::dontSendNotification);
    lblDetectedChord.setFont(juce::FontOptions(18.0f).withStyle("bold"));
    lblDetectedChord.setColour(juce::Label::textColourId, CubaseTheme::accentGold());
    lblDetectedChord.setJustificationType(juce::Justification::centred);
    lblDetectedChord.setInterceptsMouseClicks(false, false);
    grpChordDetection.addAndMakeVisible(lblDetectedChord);

    lblRomanNumeral.setText(tr("Degree") + ": --", juce::dontSendNotification);
    lblRomanNumeral.setFont(juce::FontOptions(13.0f).withStyle("bold"));
    lblRomanNumeral.setColour(juce::Label::textColourId, CubaseTheme::accentCyan());
    lblRomanNumeral.setJustificationType(juce::Justification::centred);
    lblRomanNumeral.setInterceptsMouseClicks(false, false);
    grpChordDetection.addAndMakeVisible(lblRomanNumeral);

    grpChordDetection.addMouseListener(this, true);

    // 3. Instrument Group & Channel Strip
    contentComp.addAndMakeVisible(grpInstrument);
    vstPluginCombo.setTextWhenNoChoicesAvailable("No VST3 Found (Scan in Settings)");
    vstPluginCombo.onChange = [this]() {
        int id = vstPluginCombo.getSelectedId();
        auto* track = trackModel.getTrackById(trackModel.getActiveTrackId());
        if (track == nullptr || track->getType() != TrackType::Instrument)
            return;
        const int trackId = track->getId();
        auto& host = audioEngine.getVstHostManager();
        if (id == 1)
        {
            host.unloadPlugin(trackId);
            track->setAssignedVstName({});
            track->setAssignedVstIdentifier({});
            track->setAssignedVstState({});
            trackModel.triggerTracksChanged();
        }
        else if (id > 1)
        {
            const auto& list = host.getPluginList();
            if (auto* desc = list.getType(id - 2))
            {
                const juce::PluginDescription selectedPlugin(*desc);
                if (host.loadPlugin(trackId, selectedPlugin))
                {
                    track->setAssignedVstName(selectedPlugin.name);
                    track->setAssignedVstIdentifier(selectedPlugin.createIdentifierString());
                    track->setAssignedVstState({});
                    trackModel.triggerTracksChanged();
                }
                else
                {
                    updateVstSelection();
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                        tr("VST Load Failed"), selectedPlugin.name + " could not be loaded.");
                }
            }
        }
    };
    grpInstrument.addAndMakeVisible(vstPluginCombo);

    btnOpenEditor.onClick = [this]() {
        audioEngine.getVstHostManager().openPluginEditor(trackModel.getActiveTrackId());
    };
    grpInstrument.addAndMakeVisible(btnOpenEditor);

    // Channel Strip Controls (Volume & Pan)
    lblVol.setText("VOL", juce::dontSendNotification);
    lblVol.setFont(CubaseTheme::getFont(10.0f, true));
    grpInstrument.addAndMakeVisible(lblVol);

    volSlider.setValue(1.0, juce::dontSendNotification);
    volSlider.setTooltip(juce::String::fromUTF8(u8"音量 · dB 读数 · 双击恢复 0 dB"));
    volSlider.onValueChange = [this]() {
        float val = (float)volSlider.getValue();
        if (auto* trk = trackModel.getTrackById(trackModel.getActiveTrackId()))
        {
            trk->setVolume(val);
        }
        audioEngine.setTrackVolume(trackModel.getActiveTrackId(), val);
    };
    grpInstrument.addAndMakeVisible(volSlider);

    lblPan.setText("PAN", juce::dontSendNotification);
    lblPan.setFont(CubaseTheme::getFont(10.0f, true));
    grpInstrument.addAndMakeVisible(lblPan);

    panSlider.setValue(0.0, juce::dontSendNotification);
    panSlider.setTooltip(juce::String::fromUTF8(u8"声像 · L/C/R 读数 · 双击居中"));
    panSlider.onValueChange = [this]() {
        float p = (float)panSlider.getValue();
        if (auto* trk = trackModel.getTrackById(trackModel.getActiveTrackId()))
        {
            trk->setPan(p);
        }
        audioEngine.setTrackPan(trackModel.getActiveTrackId(), p);
    };
    grpInstrument.addAndMakeVisible(panSlider);

    // Audio track channel strip and media import.
    contentComp.addChildComponent(grpAudioTrack);
    lblAudioStatus.setFont(CubaseTheme::getFont(11.0f));
    lblAudioStatus.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    grpAudioTrack.addAndMakeVisible(lblAudioStatus);
    btnImportAudio.onClick = [this]() {
        auto* track = trackModel.getTrackById(trackModel.getActiveTrackId());
        if (track == nullptr || track->getType() != TrackType::Audio || track->isLocked()) return;
        juce::FileChooser chooser(juce::String::fromUTF8(u8"导入音频文件 / Import Audio"),
                                 juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        if (chooser.browseForFileToOpen()
            && !trackModel.importAudioClip(track->getId(), chooser.getResult(),
                                            audioEngine.getPlayheadBeat(), midiModel.getBpm(), &undoManager))
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                juce::String::fromUTF8(u8"无法导入音频"),
                juce::String::fromUTF8(u8"文件无法读取、格式不受支持，或轨道已锁定。"));
    };
    grpAudioTrack.addAndMakeVisible(btnImportAudio);
    for (auto* label : { &lblAudioVolume, &lblAudioPan })
    {
        label->setFont(CubaseTheme::getFont(11.0f));
        label->setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
        grpAudioTrack.addAndMakeVisible(label);
    }
    for (auto* slider : { &audioVolumeSlider, &audioPanSlider })
        grpAudioTrack.addAndMakeVisible(slider);
    audioVolumeSlider.setTooltip(juce::String::fromUTF8(u8"音量 · dB 读数 · 双击恢复 0 dB"));
    audioVolumeSlider.onValueChange = [this]() {
        if (auto* track = trackModel.getTrackById(trackModel.getActiveTrackId()))
            if (track->getType() == TrackType::Audio)
            {
                const float value = (float)audioVolumeSlider.getValue();
                track->setVolume(value);
                audioEngine.setTrackVolume(track->getId(), value);
            }
    };
    audioPanSlider.setTooltip(juce::String::fromUTF8(u8"声像 · L/C/R 读数 · 双击居中"));
    audioPanSlider.onValueChange = [this]() {
        if (auto* track = trackModel.getTrackById(trackModel.getActiveTrackId()))
            if (track->getType() == TrackType::Audio)
            {
                const float value = (float)audioPanSlider.getValue();
                track->setPan(value);
                audioEngine.setTrackPan(track->getId(), value);
            }
    };
    btnAudioMute.onClick = [this]() {
        if (auto* track = trackModel.getTrackById(trackModel.getActiveTrackId()))
            if (track->getType() == TrackType::Audio)
            {
                track->setMuted(!track->isMuted());
                trackModel.triggerTracksChanged();
            }
    };
    btnAudioSolo.onClick = [this]() {
        if (auto* track = trackModel.getTrackById(trackModel.getActiveTrackId()))
            if (track->getType() == TrackType::Audio)
            {
                track->setSolo(!track->isSolo());
                trackModel.triggerTracksChanged();
            }
    };
    btnAudioMute.setClickingTogglesState(true);
    btnAudioSolo.setClickingTogglesState(true);
    grpAudioTrack.addAndMakeVisible(btnAudioMute);
    grpAudioTrack.addAndMakeVisible(btnAudioSolo);

    // 4. Reharm Group
    contentComp.addAndMakeVisible(grpReharm);
    btnSecondaryDominant.onClick = [this]() {
        if (onSecondaryDominantRequested)
            onSecondaryDominantRequested();
    };
    grpReharm.addAndMakeVisible(btnSecondaryDominant);

    btnReharmOptions.onClick = [this]() {
        setSecondaryDominantSettingsExpanded(!secondarySettingsExpanded);
    };
    grpReharm.addAndMakeVisible(btnReharmOptions);

    for (auto* label : { &lblSecondaryTarget, &lblSecondaryManualRoot,
                         &lblSecondaryType, &lblSecondaryVoicing })
    {
        label->setFont(CubaseTheme::getFont(11.0f));
        label->setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
        grpReharm.addAndMakeVisible(label);
    }
    for (auto* combo : { &secondaryTargetCombo, &secondaryManualRootCombo,
                         &secondaryTypeCombo, &secondaryVoicingCombo })
        grpReharm.addAndMakeVisible(combo);

    secondaryTargetCombo.onChange = [this]() {
        auto& state = AppStateManager::getInstance();
        state.secondaryDominantTargetSource = std::clamp(secondaryTargetCombo.getSelectedId() - 1, 0, 3);
        state.save();
        if (onSecondaryDominantSettingsChanged) onSecondaryDominantSettingsChanged();
        else refreshSecondaryDominantSettings();
    };
    secondaryManualRootCombo.onChange = [this]() {
        auto& state = AppStateManager::getInstance();
        state.secondaryDominantManualRootPc = std::clamp(secondaryManualRootCombo.getSelectedId() - 1, 0, 11);
        state.save();
        if (onSecondaryDominantSettingsChanged) onSecondaryDominantSettingsChanged();
        else refreshSecondaryDominantSettings();
    };
    secondaryTypeCombo.onChange = [this]() {
        auto& state = AppStateManager::getInstance();
        state.secondaryDominantFlatNine = secondaryTypeCombo.getSelectedId() == 2;
        state.save();
        if (onSecondaryDominantSettingsChanged) onSecondaryDominantSettingsChanged();
        else refreshSecondaryDominantSettings();
    };
    secondaryVoicingCombo.onChange = [this]() {
        auto& state = AppStateManager::getInstance();
        state.secondaryDominantRootInBass = secondaryVoicingCombo.getSelectedId() == 2;
        state.save();
        if (onSecondaryDominantSettingsChanged) onSecondaryDominantSettingsChanged();
        else refreshSecondaryDominantSettings();
    };

    contentComp.addAndMakeVisible(grpReharmonization);
    for (size_t i = 0; i < reharmonizationModeButtons.size(); ++i)
    {
        auto& button = reharmonizationModeButtons[i];
        grpReharmonization.addAndMakeVisible(button);
        button.onHoverChanged = [this, i](bool hovered) {
            if (hovered)
                hoveredReharmonizationMode = (int)i;
            else if (hoveredReharmonizationMode == (int)i)
                hoveredReharmonizationMode = -1;
            showReharmonizationPreview();
        };
        button.onClick = [this, i] {
            if (!reharmonizationPlans[i].hasChanges()) return;
            auto& state = AppStateManager::getInstance();
            state.reharmonizationAlgorithm = (int)i;
            state.save();
            if (onReharmonizationSettingsChanged) onReharmonizationSettingsChanged();
            else refreshReharmonizationSettings();
            if (onReharmonizationRequested) onReharmonizationRequested();
        };
    }
    txtReharmonizationPreview.setMultiLine(true);
    txtReharmonizationPreview.setReadOnly(true);
    txtReharmonizationPreview.setScrollbarsShown(true);
    txtReharmonizationPreview.setFont(CubaseTheme::getFont(11.5f));
    txtReharmonizationPreview.setColour(juce::TextEditor::backgroundColourId, CubaseTheme::inputBackground());
    txtReharmonizationPreview.setColour(juce::TextEditor::textColourId, CubaseTheme::textPrimary());
    txtReharmonizationPreview.setColour(juce::TextEditor::outlineColourId, CubaseTheme::borderSubtle());
    grpReharmonization.addAndMakeVisible(txtReharmonizationPreview);

    contentComp.addAndMakeVisible(grpChordShape);
    lblChordShapeStatus.setFont(CubaseTheme::getFont(11.0f, true));
    lblChordShapeStatus.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    grpChordShape.addAndMakeVisible(lblChordShapeStatus);
    for (auto* button : { &btnShapeMajor, &btnShapeMinor,
                          &btnShapeDiminished, &btnShapeAugmented,
                          &btnShapeSus2, &btnShapeSus4,
                          &btnShapeDominant7, &btnShapeMajor7,
                          &btnShapeMinor7, &btnShapeHalfDiminished7,
                          &btnVoicingExpand, &btnVoicingContract, &btnVoicingDrop2 })
        grpChordShape.addAndMakeVisible(*button);
    btnShapeMajor.onClick = [this] { if (onChordQualityRequested) onChordQualityRequested(ChordShapeQuality::Major); };
    btnShapeMinor.onClick = [this] { if (onChordQualityRequested) onChordQualityRequested(ChordShapeQuality::Minor); };
    btnShapeDiminished.onClick = [this] { if (onChordQualityRequested) onChordQualityRequested(ChordShapeQuality::Diminished); };
    btnShapeAugmented.onClick = [this] { if (onChordQualityRequested) onChordQualityRequested(ChordShapeQuality::Augmented); };
    btnShapeSus2.onClick = [this] { if (onChordQualityRequested) onChordQualityRequested(ChordShapeQuality::Sus2); };
    btnShapeSus4.onClick = [this] { if (onChordQualityRequested) onChordQualityRequested(ChordShapeQuality::Sus4); };
    btnShapeDominant7.onClick = [this] { if (onChordQualityRequested) onChordQualityRequested(ChordShapeQuality::Dominant7); };
    btnShapeMajor7.onClick = [this] { if (onChordQualityRequested) onChordQualityRequested(ChordShapeQuality::Major7); };
    btnShapeMinor7.onClick = [this] { if (onChordQualityRequested) onChordQualityRequested(ChordShapeQuality::Minor7); };
    btnShapeHalfDiminished7.onClick = [this] { if (onChordQualityRequested) onChordQualityRequested(ChordShapeQuality::HalfDiminished7); };
    btnVoicingExpand.onClick = [this] { if (onChordVoicingRequested) onChordVoicingRequested(ChordShapeVoicing::Expand); };
    btnVoicingContract.onClick = [this] { if (onChordVoicingRequested) onChordVoicingRequested(ChordShapeVoicing::Contract); };
    btnVoicingDrop2.onClick = [this] { if (onChordVoicingRequested) onChordVoicingRequested(ChordShapeVoicing::Drop2); };
    for (size_t i = 0; i < stackButtons.size(); ++i)
    {
        auto& button = stackButtons[i];
        grpChordShape.addAndMakeVisible(button);
        button.onClick = [this, i] {
            if (onChordStackRequested)
                onChordStackRequested(static_cast<ChordStackDirection>(i));
        };
    }

    contentComp.addAndMakeVisible(grpQuickChord);
    for (auto& label : quickChordCategoryLabels)
    {
        label.setFont(CubaseTheme::getFont(10.0f, true));
        label.setColour(juce::Label::textColourId, CubaseTheme::accentCyan());
        label.setJustificationType(juce::Justification::centredLeft);
        grpQuickChord.addAndMakeVisible(label);
    }
    for (const auto& type : QuickChordCatalog::all())
    {
        QuickChordButton item;
        item.id = type.id;
        item.button = std::make_unique<juce::TextButton>(type.symbol);
        item.button->onClick = [this, id = type.id] {
            if (onQuickChordRequested) onQuickChordRequested(id);
        };
        grpQuickChord.addAndMakeVisible(*item.button);
        quickChordButtons.push_back(std::move(item));
    }

    contentComp.addAndMakeVisible(grpModeBorrowing);
    for (auto* combo : { &borrowingStyleCombo, &borrowingSourceRootCombo,
                          &borrowingSourceScaleCombo, &borrowingTargetRootCombo,
                          &borrowingScaleCombo, &borrowingDegreeCombo })
        grpModeBorrowing.addAndMakeVisible(*combo);
    lblBorrowingTarget.setFont(CubaseTheme::getFont(11.0f, true));
    lblBorrowingTarget.setJustificationType(juce::Justification::centredLeft);
    grpModeBorrowing.addAndMakeVisible(lblBorrowingTarget);
    grpModeBorrowing.addAndMakeVisible(btnBorrowingSourceDisclosure);
    grpModeBorrowing.addAndMakeVisible(btnBorrowingSourceAuto);
    grpModeBorrowing.addAndMakeVisible(btnBorrowingSourceCustom);
    btnBorrowingSourceDisclosure.onClick = [this] {
        borrowingSourceSettingsExpanded = !borrowingSourceSettingsExpanded;
        refreshModeBorrowingSettings();
    };
    auto setCustomSource = [this](bool custom) {
        auto& state = AppStateManager::getInstance();
        if (state.modeBorrowingCustomSource == custom) return;
        state.modeBorrowingCustomSource = custom;
        state.save();
        if (onModeBorrowingSettingsChanged) onModeBorrowingSettingsChanged();
        else refreshModeBorrowingSettings();
    };
    btnBorrowingSourceAuto.onClick = [setCustomSource] { setCustomSource(false); };
    btnBorrowingSourceCustom.onClick = [setCustomSource] { setCustomSource(true); };
    borrowingSourceRootCombo.setTooltip(juce::String::fromUTF8(u8"手动选择前调式的主音，不修改项目调式。"));
    borrowingSourceScaleCombo.setTooltip(juce::String::fromUTF8(u8"手动选择前调式类型，用于解释当前和弦。"));
    borrowingTargetRootCombo.setTooltip(juce::String::fromUTF8(u8"调式趋近时可选任意目标主音；同主音时跟随前调式。"));
    borrowingScaleCombo.setTooltip(juce::String::fromUTF8(u8"选择要借用的目标调式类型。"));
    txtBorrowingPreview.setMultiLine(true);
    txtBorrowingPreview.setReadOnly(true);
    txtBorrowingPreview.setScrollbarsShown(true);
    txtBorrowingPreview.setFont(CubaseTheme::getFont(11.0f));
    grpModeBorrowing.addAndMakeVisible(txtBorrowingPreview);
    grpModeBorrowing.addAndMakeVisible(btnApplyBorrowing);
    btnApplyBorrowing.onClick = [this] { if (onModeBorrowingRequested) onModeBorrowingRequested(); };
    borrowingStyleCombo.onChange = [this] {
        auto& state = AppStateManager::getInstance();
        state.modeBorrowingStyle = std::clamp(borrowingStyleCombo.getSelectedId() - 1, 0, 1);
        state.save();
        if (onModeBorrowingSettingsChanged) onModeBorrowingSettingsChanged();
        else refreshChordEditingPreview();
    };
    borrowingScaleCombo.onChange = [this] {
        auto& state = AppStateManager::getInstance();
        state.modeBorrowingScale = borrowingScaleCombo.getText();
        state.save();
        if (onModeBorrowingSettingsChanged) onModeBorrowingSettingsChanged();
        else refreshChordEditingPreview();
    };
    borrowingSourceRootCombo.onChange = [this] {
        auto& state = AppStateManager::getInstance();
        state.modeBorrowingSourceRootPc = std::clamp(borrowingSourceRootCombo.getSelectedId() - 1, 0, 11);
        state.save();
        if (onModeBorrowingSettingsChanged) onModeBorrowingSettingsChanged();
        else refreshModeBorrowingSettings();
    };
    borrowingSourceScaleCombo.onChange = [this] {
        auto& state = AppStateManager::getInstance();
        state.modeBorrowingSourceScale = borrowingSourceScaleCombo.getText();
        state.save();
        if (onModeBorrowingSettingsChanged) onModeBorrowingSettingsChanged();
        else refreshModeBorrowingSettings();
    };
    borrowingTargetRootCombo.onChange = [this] {
        auto& state = AppStateManager::getInstance();
        state.modeBorrowingTargetRootPc = std::clamp(borrowingTargetRootCombo.getSelectedId() - 1, 0, 11);
        state.save();
        if (onModeBorrowingSettingsChanged) onModeBorrowingSettingsChanged();
        else refreshModeBorrowingSettings();
    };
    borrowingDegreeCombo.onChange = [this] {
        auto& state = AppStateManager::getInstance();
        state.modeBorrowingDegree = std::clamp(borrowingDegreeCombo.getSelectedId() - 2, -1, 6);
        state.save();
        if (onModeBorrowingSettingsChanged) onModeBorrowingSettingsChanged();
        else refreshChordEditingPreview();
    };

    // 5. Chord Track Inspector
    contentComp.addChildComponent(grpChordTrack);
    btnExtractChordsFromMidi.setButtonText(tr("Extract Chords From MIDI"));
    btnExtractChordsFromMidi.onClick = [this]() {
        extractChordsFromMidiTrack();
    };
    grpChordTrack.addAndMakeVisible(btnExtractChordsFromMidi);

    lblChordDisplayMode.setText(tr("Display:"), juce::dontSendNotification);
    lblChordDisplayMode.setFont(CubaseTheme::getFont(11.0f));
    lblChordDisplayMode.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    grpChordTrack.addAndMakeVisible(lblChordDisplayMode);

    chordDisplayModeCombo.addItem(juce::String::fromUTF8((const char*)u8"和弦名 + 罗马级数"), 1);
    chordDisplayModeCombo.addItem(juce::String::fromUTF8((const char*)u8"仅和弦名"), 2);
    chordDisplayModeCombo.addItem(juce::String::fromUTF8((const char*)u8"仅罗马级数"), 3);
    chordDisplayModeCombo.addItem(juce::String::fromUTF8((const char*)u8"罗马级数 + 和弦性质"), 4);
    chordDisplayModeCombo.setSelectedId((int)trackModel.getChordDisplayMode() + 1, juce::dontSendNotification);
    chordDisplayModeCombo.onChange = [this]() {
        int id = chordDisplayModeCombo.getSelectedId();
        if (id >= 1 && id <= 4)
            trackModel.setChordDisplayMode((ChordDisplayMode)(id - 1));
    };
    grpChordTrack.addAndMakeVisible(chordDisplayModeCombo);

    lblChordTrackInfo.setText(tr("Extracts polyphonic MIDI harmonies to Chord blocks"), juce::dontSendNotification);
    lblChordTrackInfo.setFont(CubaseTheme::getFont(11.0f));
    lblChordTrackInfo.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    grpChordTrack.addAndMakeVisible(lblChordTrackInfo);

    // 6. Mode Track Inspector
    contentComp.addChildComponent(grpModeTrack);
    btnAnalyzeAndPopulateModes.setButtonText(tr("Analyze & Populate Modes"));
    btnAnalyzeAndPopulateModes.onClick = [this]() {
        analyzeAndPopulateModeTrack();
    };
    grpModeTrack.addAndMakeVisible(btnAnalyzeAndPopulateModes);

    btnModeTrackOverride.setButtonText(juce::String::fromUTF8((const char*)u8"启用调式轨全局覆盖 (Mode Track Override)"));
    btnModeTrackOverride.setToggleState(trackModel.isModeTrackOverrideEnabled(), juce::dontSendNotification);
    btnModeTrackOverride.onClick = [this]() {
        trackModel.setModeTrackOverrideEnabled(btnModeTrackOverride.getToggleState());
        midiModel.triggerProjectSettingsChanged();
    };
    grpModeTrack.addAndMakeVisible(btnModeTrackOverride);

    lblModeTrackInfo.setText(tr("Governs tonality, scales, and chromatic modal guidance"), juce::dontSendNotification);
    lblModeTrackInfo.setFont(CubaseTheme::getFont(11.0f));
    lblModeTrackInfo.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    grpModeTrack.addAndMakeVisible(lblModeTrackInfo);

    // 7. Marker Track Inspector
    contentComp.addChildComponent(grpMarkerTrack);
    lblMarkerTrackInfo.setText(tr("Pure arrangement milestones with overlapping stacking"), juce::dontSendNotification);
    lblMarkerTrackInfo.setFont(CubaseTheme::getFont(11.0f));
    lblMarkerTrackInfo.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    grpMarkerTrack.addAndMakeVisible(lblMarkerTrackInfo);

    markerListModel = std::make_unique<MarkerListModel>(trackModel, [this](int row) {
        selectedMarkerIndex = row;
        int activeTrkId = trackModel.getActiveTrackId();
        auto* trk = trackModel.getTrackById(activeTrkId);
        if (trk && trk->getType() == TrackType::Marker)
        {
            auto& markers = trk->getMarkers();
            for (size_t i = 0; i < markers.size(); ++i)
                markers[i].isSelected = ((int)i == row);
            trackModel.triggerMarkersChanged();
        }
        updateMarkerPropertyFields();
    });
    markerListBox.setModel(markerListModel.get());
    markerListBox.setRowHeight(22);
    markerListBox.setColour(juce::ListBox::backgroundColourId, CubaseTheme::bgPanel());
    grpMarkerTrack.addAndMakeVisible(markerListBox);

    lblMarkerNameTitle.setText(tr("Name") + ":", juce::dontSendNotification);
    lblMarkerNameTitle.setFont(CubaseTheme::getFont(11.0f));
    lblMarkerNameTitle.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    grpMarkerTrack.addAndMakeVisible(lblMarkerNameTitle);

    grpMarkerTrack.addAndMakeVisible(txtMarkerName);
    txtMarkerName.setFont(CubaseTheme::getFont(11.5f));
    txtMarkerName.onTextChange = [this]() {
        int activeTrkId = trackModel.getActiveTrackId();
        auto* trk = trackModel.getTrackById(activeTrkId);
        if (trk && trk->getType() == TrackType::Marker)
        {
            auto& markers = trk->getMarkers();
            if (selectedMarkerIndex >= 0 && selectedMarkerIndex < (int)markers.size())
            {
                markers[selectedMarkerIndex].name = txtMarkerName.getText();
                trackModel.triggerMarkersChanged();
                markerListBox.repaint();
            }
        }
    };

    lblMarkerStartTitle.setText(tr("Start") + ":", juce::dontSendNotification);
    lblMarkerStartTitle.setFont(CubaseTheme::getFont(11.0f));
    lblMarkerStartTitle.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    grpMarkerTrack.addAndMakeVisible(lblMarkerStartTitle);

    grpMarkerTrack.addAndMakeVisible(txtMarkerStart);
    txtMarkerStart.setFont(CubaseTheme::getFont(11.5f));
    txtMarkerStart.onReturnKey = [this]() {
        int activeTrkId = trackModel.getActiveTrackId();
        auto* trk = trackModel.getTrackById(activeTrkId);
        if (trk && trk->getType() == TrackType::Marker)
        {
            auto& markers = trk->getMarkers();
            if (selectedMarkerIndex >= 0 && selectedMarkerIndex < (int)markers.size())
            {
                double val = txtMarkerStart.getText().getDoubleValue();
                markers[selectedMarkerIndex].startBeat = std::max(0.0, val);
                trackModel.triggerMarkersChanged();
                markerListBox.repaint();
            }
        }
    };

    lblMarkerLengthTitle.setText(tr("Len") + ":", juce::dontSendNotification);
    lblMarkerLengthTitle.setFont(CubaseTheme::getFont(11.0f));
    lblMarkerLengthTitle.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    grpMarkerTrack.addAndMakeVisible(lblMarkerLengthTitle);

    grpMarkerTrack.addAndMakeVisible(txtMarkerLength);
    txtMarkerLength.setFont(CubaseTheme::getFont(11.5f));
    txtMarkerLength.onReturnKey = [this]() {
        int activeTrkId = trackModel.getActiveTrackId();
        auto* trk = trackModel.getTrackById(activeTrkId);
        if (trk && trk->getType() == TrackType::Marker)
        {
            auto& markers = trk->getMarkers();
            if (selectedMarkerIndex >= 0 && selectedMarkerIndex < (int)markers.size())
            {
                double val = txtMarkerLength.getText().getDoubleValue();
                markers[selectedMarkerIndex].lengthBeats = std::max(0.25, val);
                trackModel.triggerMarkersChanged();
                markerListBox.repaint();
            }
        }
    };

    lblMarkerColorTitle.setText(tr("Color") + ":", juce::dontSendNotification);
    lblMarkerColorTitle.setFont(CubaseTheme::getFont(11.0f));
    lblMarkerColorTitle.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    grpMarkerTrack.addAndMakeVisible(lblMarkerColorTitle);

    markerColorCombo.addItem(tr("Cyan"), 1);
    markerColorCombo.addItem(tr("Orange"), 2);
    markerColorCombo.addItem(tr("Red"), 3);
    markerColorCombo.addItem(tr("Green"), 4);
    markerColorCombo.addItem(tr("Purple"), 5);
    markerColorCombo.addItem(tr("Yellow"), 6);
    markerColorCombo.addItem(tr("Blue"), 7);
    markerColorCombo.addItem(tr("Grey"), 8);
    markerColorCombo.onChange = [this]() {
        int activeTrkId = trackModel.getActiveTrackId();
        auto* trk = trackModel.getTrackById(activeTrkId);
        if (trk && trk->getType() == TrackType::Marker)
        {
            auto& markers = trk->getMarkers();
            if (selectedMarkerIndex >= 0 && selectedMarkerIndex < (int)markers.size())
            {
                static const juce::Colour palette[] = {
                    CubaseTheme::accentCyan(),
                    CubaseTheme::accentOrange(),
                    juce::Colour(0xffef4444),
                    juce::Colour(0xff10b981),
                    juce::Colour(0xff8b5cf6),
                    juce::Colour(0xffeab308),
                    juce::Colour(0xff3b82f6),
                    juce::Colours::grey
                };
                int sel = markerColorCombo.getSelectedId() - 1;
                if (sel >= 0 && sel < 8)
                {
                    markers[selectedMarkerIndex].color = palette[sel];
                    trackModel.triggerMarkersChanged();
                    markerListBox.repaint();
                }
            }
        }
    };
    grpMarkerTrack.addAndMakeVisible(markerColorCombo);

    btnJumpToMarker.setButtonText(tr("Jump to Marker (跳转播放指针)"));
    btnJumpToMarker.onClick = [this]() {
        int activeTrkId = trackModel.getActiveTrackId();
        auto* trk = trackModel.getTrackById(activeTrkId);
        if (trk && trk->getType() == TrackType::Marker)
        {
            const auto& markers = trk->getMarkers();
            if (selectedMarkerIndex >= 0 && selectedMarkerIndex < (int)markers.size())
            {
                audioEngine.setPlayheadBeat(markers[selectedMarkerIndex].startBeat);
            }
        }
    };
    grpMarkerTrack.addAndMakeVisible(btnJumpToMarker);

    btnAddMarkerAtPlayhead.setButtonText(tr("+ Add Marker at Playhead"));
    btnAddMarkerAtPlayhead.onClick = [this]() {
        int activeTrkId = trackModel.getActiveTrackId();
        auto* trk = trackModel.getTrackById(activeTrkId);
        if (trk && trk->getType() == TrackType::Marker)
        {
            MarkerEvent me;
            me.id = juce::Uuid().toString();
            me.startBeat = audioEngine.getPlayheadBeat();
            me.lengthBeats = 8.0;
            me.name = "Marker " + juce::String((int)trk->getMarkers().size() + 1);
            me.color = CubaseTheme::accentCyan();
            for (auto& m : trk->getMarkers()) m.isSelected = false;
            me.isSelected = true;
            trk->getMarkers().push_back(me);
            selectedMarkerIndex = (int)trk->getMarkers().size() - 1;
            trackModel.triggerMarkersChanged();
            updateMarkerTrackControls();
        }
    };
    grpMarkerTrack.addAndMakeVisible(btnAddMarkerAtPlayhead);

    btnDeleteMarker.setButtonText(tr("Delete Marker"));
    btnDeleteMarker.onClick = [this]() {
        int activeTrkId = trackModel.getActiveTrackId();
        auto* trk = trackModel.getTrackById(activeTrkId);
        if (trk && trk->getType() == TrackType::Marker)
        {
            auto& markers = trk->getMarkers();
            if (selectedMarkerIndex >= 0 && selectedMarkerIndex < (int)markers.size())
            {
                markers.erase(markers.begin() + selectedMarkerIndex);
                if (selectedMarkerIndex >= (int)markers.size())
                    selectedMarkerIndex = (int)markers.size() - 1;
                trackModel.triggerMarkersChanged();
                updateMarkerTrackControls();
            }
        }
    };
    grpMarkerTrack.addAndMakeVisible(btnDeleteMarker);

    // 8. Time Signature Track Inspector
    contentComp.addChildComponent(grpTimeSigTrack);
    lblTimeSigInfo.setText(tr("Dynamic piecewise meter and measure grid management"), juce::dontSendNotification);
    lblTimeSigInfo.setFont(CubaseTheme::getFont(11.0f));
    lblTimeSigInfo.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    grpTimeSigTrack.addAndMakeVisible(lblTimeSigInfo);

    // 9. Folder Track Inspector
    contentComp.addChildComponent(grpFolderTrack);
    lblFolderTrackSummary.setFont(CubaseTheme::getFont(11.0f));
    lblFolderTrackSummary.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    for (auto* label : { &lblSecondaryTarget, &lblSecondaryManualRoot,
                         &lblSecondaryType, &lblSecondaryVoicing })
        label->setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    grpFolderTrack.addAndMakeVisible(lblFolderTrackSummary);

    lblFolderNotesTitle.setText(tr("Folder Notes") + ":", juce::dontSendNotification);
    lblFolderNotesTitle.setFont(CubaseTheme::getFont(11.0f, true));
    lblFolderNotesTitle.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    grpFolderTrack.addAndMakeVisible(lblFolderNotesTitle);

    txtFolderNotes.setMultiLine(true);
    txtFolderNotes.setReturnKeyStartsNewLine(true);
    txtFolderNotes.setFont(CubaseTheme::getFont(11.5f));
    txtFolderNotes.setColour(juce::TextEditor::backgroundColourId, CubaseTheme::inputBackground());
    txtFolderNotes.setColour(juce::TextEditor::textColourId, CubaseTheme::textPrimary());
    txtFolderNotes.setColour(juce::TextEditor::outlineColourId, CubaseTheme::borderSubtle());
    txtFolderNotes.onTextChange = [this]() {
        int activeTrkId = trackModel.getActiveTrackId();
        if (auto* trk = trackModel.getTrackById(activeTrkId))
        {
            if (trk->getType() == TrackType::Folder)
                trk->setFolderNotes(txtFolderNotes.getText());
        }
    };
    grpFolderTrack.addAndMakeVisible(txtFolderNotes);

    modeCandidatesList.setModel(&modeCandidateListModel);
    modeCandidatesList.setRowHeight(26);
    modeCandidatesList.setMultipleSelectionEnabled(false);
    grpScaleMode.addChildComponent(modeCandidatesList);

    for (auto* button : { &btnGroupByTonic, &btnGroupByType })
    {
        button->setClickingTogglesState(true);
        button->setRadioGroupId(6701);
        grpScaleMode.addChildComponent(button);
    }
    btnGroupByTonic.setToggleState(true, juce::dontSendNotification);
    btnGroupByTonic.onClick = [this]() {
        const int selected = candidateIndexForRow(modeCandidatesList.getSelectedRow());
        modeGrouping = ModeGrouping::Tonic;
        btnGroupByTonic.setToggleState(true, juce::dontSendNotification);
        btnGroupByType.setToggleState(false, juce::dontSendNotification);
        rebuildModeCandidateRows(selected);
    };
    btnGroupByType.onClick = [this]() {
        const int selected = candidateIndexForRow(modeCandidatesList.getSelectedRow());
        modeGrouping = ModeGrouping::ScaleType;
        btnGroupByTonic.setToggleState(false, juce::dontSendNotification);
        btnGroupByType.setToggleState(true, juce::dontSendNotification);
        rebuildModeCandidateRows(selected);
    };

    btnApplyCandidate.onClick = [this]() {
        applyModeCandidate(candidateIndexForRow(modeCandidatesList.getSelectedRow()));
    };
    grpScaleMode.addChildComponent(btnApplyCandidate);
    btnApplyCandidateToRange.onClick = [this]() {
        applyModeCandidateToSelectedRange(candidateIndexForRow(modeCandidatesList.getSelectedRow()));
    };
    grpScaleMode.addChildComponent(btnApplyCandidateToRange);

    // The card header is the drag handle and its context menu holds module actions.
    juce::GroupComponent* allGroups[kInspectorSectionCount] = {
        &grpNoteInfo, &grpScaleMode, &grpChordDetection, &grpInstrument, &grpReharm,
        &grpChordTrack, &grpModeTrack, &grpMarkerTrack, &grpTimeSigTrack, &grpFolderTrack,
        &grpReharmonization, &grpAudioTrack, &grpChordShape, &grpModeBorrowing,
        &grpQuickChord
    };
    for (int i = 0; i < kInspectorSectionCount; ++i)
    {
        allGroups[i]->getProperties().set("inspectorModule", true);
        // GroupComponent ignores its own mouse events by default, which makes
        // its empty title bar impossible to drag without opting in here.
        allGroups[i]->setInterceptsMouseClicks(true, true);
        allGroups[i]->addMouseListener(this, false);
    }

    trackModel.addListener(this);
    CubaseTheme::addListener(this);
    updateScaleAndKeyControls();
    languageChanged();
    updateVstCombo();
    updateTrackContext();
    themeChanged();
    applyCustomModuleSettings();
}

void InspectorComponent::showSectionMenu(int sectionIndex, juce::Point<int> screenPosition)
{
    if (!isInspectorSectionId(sectionIndex)) return;

    auto position = getSectionPosition(sectionIndex);
    juce::PopupMenu menu;
    menu.addItem(1, tr("Dock Left"), true,
                 position == SectionDockPosition::Left);
    menu.addItem(2, tr("Dock Right"), true,
                 position == SectionDockPosition::Right);
    menu.addItem(3, tr("Hide Module"));
    menu.addSeparator();
    menu.addItem(4, tr("Open Sections Manager"));
    if (sectionIndex == 0 || sectionIndex == 2 || sectionIndex == 12
        || sectionIndex == kQuickChordSectionId)
    {
        menu.addSeparator();
        menu.addItem(5, tr("Module Settings"));
    }

    juce::Component::SafePointer<InspectorComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           { screenPosition.x, screenPosition.y, 1, 1 }),
                       [safeThis, sectionIndex](int result) {
        auto* owner = safeThis.getComponent();
        if (owner == nullptr) return;
        switch (result)
        {
            case 1:
                owner->setSectionPosition(sectionIndex, SectionDockPosition::Left);
                if (owner->onEnsureDockVisible) owner->onEnsureDockVisible(DockSide::Left);
                break;
            case 2:
                owner->setSectionPosition(sectionIndex, SectionDockPosition::Right);
                if (owner->onEnsureDockVisible) owner->onEnsureDockVisible(DockSide::Right);
                break;
            case 3: owner->setSectionPosition(sectionIndex, SectionDockPosition::Hidden); break;
            case 4: if (owner->onOpenSectionsManager) owner->onOpenSectionsManager(); break;
            case 5:
                if (sectionIndex == 0)
                    NoteInspectorSettingsDialog::showModal([safeThis]() {
                        if (auto* inspector = safeThis.getComponent()) inspector->applyCustomModuleSettings();
                    });
                else if (sectionIndex == 2)
                    ChordAnalysisSettingsDialog::showModal([safeThis]() {
                        if (auto* inspector = safeThis.getComponent()) inspector->applyCustomModuleSettings();
                    });
                else if (sectionIndex == 12)
                    ChordShapeSettingsDialog::showModal([safeThis]() {
                        if (auto* inspector = safeThis.getComponent()) inspector->applyCustomModuleSettings();
                    });
                else if (sectionIndex == kQuickChordSectionId)
                    QuickChordSettingsDialog::showModal([safeThis]() {
                        if (auto* inspector = safeThis.getComponent())
                        {
                            if (inspector->onQuickChordSettingsChanged)
                                inspector->onQuickChordSettingsChanged();
                            else
                                inspector->refreshQuickChordMode();
                        }
                    });
                break;
            default: break;
        }
    });
}

InspectorComponent::~InspectorComponent()
{
    CubaseTheme::removeListener(this);
    trackModel.removeListener(this);
    Localization::getInstance().removeListener(this);
    audioEngine.getVstHostManager().removeListener(this);
    midiModel.removeListener(this);
}

void InspectorComponent::themeChanged()
{
    for (auto& label : quickChordCategoryLabels)
        label.setColour(juce::Label::textColourId, CubaseTheme::accentCyan());
    lblEmptyDock.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    lblSelectionCount.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblNotePitch.setColours(CubaseTheme::textSecondary(), CubaseTheme::textPrimary(), CubaseTheme::textSecondary());
    lblNoteStart.setColours(CubaseTheme::textSecondary(), CubaseTheme::textPrimary(), CubaseTheme::textSecondary());
    lblNoteLength.setColours(CubaseTheme::textSecondary(), CubaseTheme::textPrimary(), CubaseTheme::textSecondary());
    lblNoteVelocity.setColours(CubaseTheme::textSecondary(), CubaseTheme::textPrimary(), CubaseTheme::textSecondary());
    lblNoteScaleDegree.setColours(CubaseTheme::textSecondary(), CubaseTheme::accentCyan(), CubaseTheme::textSecondary());
    lblNoteFunctionalRole.setColours(CubaseTheme::textSecondary(), CubaseTheme::accentGold(), CubaseTheme::textSecondary());
    lblNoteMotionTendency.setColours(CubaseTheme::textSecondary(), CubaseTheme::accentOrange(), CubaseTheme::textSecondary());
    lblNoteColorUsage.setColours(CubaseTheme::textSecondary(), CubaseTheme::accentPurple(), CubaseTheme::textSecondary());
    lblNoteInterval.setColours(CubaseTheme::textSecondary(), CubaseTheme::accentCyan(), CubaseTheme::textSecondary());
    lblModeAnalysisConfidence.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblVol.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblPan.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblAudioStatus.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    lblAudioVolume.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblAudioPan.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblBorrowingTarget.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    btnBorrowingSourceDisclosure.repaint();
    txtReharmonizationPreview.setColour(juce::TextEditor::backgroundColourId, CubaseTheme::inputBackground());
    txtReharmonizationPreview.setColour(juce::TextEditor::textColourId, CubaseTheme::textPrimary());
    txtReharmonizationPreview.setColour(juce::TextEditor::outlineColourId, CubaseTheme::borderSubtle());
    txtBorrowingPreview.setColour(juce::TextEditor::backgroundColourId, CubaseTheme::inputBackground());
    txtBorrowingPreview.setColour(juce::TextEditor::textColourId, CubaseTheme::textPrimary());
    txtBorrowingPreview.setColour(juce::TextEditor::outlineColourId, CubaseTheme::borderSubtle());
    lblChordDisplayMode.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblChordTrackInfo.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblModeTrackInfo.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblMarkerTrackInfo.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblMarkerNameTitle.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblMarkerColorTitle.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    txtMarkerName.setColour(juce::TextEditor::backgroundColourId, CubaseTheme::inputBackground());
    txtMarkerName.setColour(juce::TextEditor::textColourId, CubaseTheme::textPrimary());
    txtMarkerName.setColour(juce::TextEditor::outlineColourId, CubaseTheme::borderSubtle());
    markerListBox.setColour(juce::ListBox::backgroundColourId, CubaseTheme::bgSurface());
    markerListBox.setColour(juce::ListBox::outlineColourId, CubaseTheme::borderSubtle());
    lblTimeSigInfo.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblFolderTrackSummary.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblFolderNotesTitle.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    txtFolderNotes.setColour(juce::TextEditor::backgroundColourId, CubaseTheme::inputBackground());
    txtFolderNotes.setColour(juce::TextEditor::textColourId, CubaseTheme::textPrimary());
    txtFolderNotes.setColour(juce::TextEditor::outlineColourId, CubaseTheme::borderSubtle());

    auto styleBtn = [](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, CubaseTheme::buttonBackground());
        btn.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());
    };

    styleBtn(btnCustomScale);
    styleBtn(btnApplyCurrentMode);
    styleBtn(btnAnalyzeMode);
    styleBtn(btnSecondaryDominant);
    styleBtn(btnReharmOptions);
    for (auto& item : quickChordButtons) styleBtn(*item.button);
    for (auto* button : { &btnShapeMajor, &btnShapeMinor,
                          &btnShapeDiminished, &btnShapeAugmented,
                          &btnShapeSus2, &btnShapeSus4,
                          &btnShapeDominant7, &btnShapeMajor7,
                          &btnShapeMinor7, &btnShapeHalfDiminished7,
                          &btnVoicingExpand, &btnVoicingContract, &btnVoicingDrop2, &btnApplyBorrowing })
        styleBtn(*button);
    for (auto& button : stackButtons) styleBtn(button);
    styleBtn(btnBorrowingSourceAuto);
    styleBtn(btnBorrowingSourceCustom);
    styleBtn(btnExtractChordsFromMidi);
    styleBtn(btnAnalyzeAndPopulateModes);
    styleBtn(btnOpenEditor);
    styleBtn(btnImportAudio);
    styleBtn(btnAudioMute);
    styleBtn(btnAudioSolo);
    btnAudioMute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffc7374a));
    btnAudioSolo.setColour(juce::TextButton::buttonOnColourId, CubaseTheme::accentOrange());
    btnAudioMute.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    btnAudioSolo.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    styleBtn(btnGroupByTonic);
    styleBtn(btnGroupByType);
    styleBtn(btnApplyCandidate);
    styleBtn(btnApplyCandidateToRange);
    styleBtn(btnJumpToMarker);
    styleBtn(btnAddMarkerAtPlayhead);
    styleBtn(btnDeleteMarker);
    styleBtn(btnEmptyDockManage);

    btnModeTrackOverride.setColour(juce::ToggleButton::textColourId, CubaseTheme::textPrimary());

    auto styleCombo = [](juce::ComboBox& box) {
        box.setColour(juce::ComboBox::backgroundColourId, CubaseTheme::inputBackground());
        box.setColour(juce::ComboBox::textColourId, CubaseTheme::textPrimary());
        box.setColour(juce::ComboBox::outlineColourId, CubaseTheme::borderSubtle());
        box.setColour(juce::ComboBox::arrowColourId, CubaseTheme::textSecondary());
    };

    styleCombo(keyRootCombo);
    styleCombo(scaleTypeCombo);
    styleCombo(vstPluginCombo);
    styleCombo(chordDisplayModeCombo);
    styleCombo(secondaryTargetCombo);
    styleCombo(secondaryManualRootCombo);
    styleCombo(secondaryTypeCombo);
    styleCombo(secondaryVoicingCombo);
    for (auto& button : reharmonizationModeButtons) button.repaint();
    styleCombo(borrowingStyleCombo);
    styleCombo(borrowingSourceRootCombo);
    styleCombo(borrowingSourceScaleCombo);
    styleCombo(borrowingTargetRootCombo);
    styleCombo(borrowingScaleCombo);
    styleCombo(borrowingDegreeCombo);
    styleCombo(markerColorCombo);
    modeCandidatesList.setColour(juce::ListBox::backgroundColourId, CubaseTheme::bgSurface());
    modeCandidatesList.setColour(juce::ListBox::outlineColourId, CubaseTheme::borderSubtle());
    modeCandidatesList.repaint();

    applyCustomModuleSettings();
    updateNoteInfo();
    updateReharmonizationPreview();
    refreshChordEditingPreview();
    sendLookAndFeelChange();
    repaint();
}

void InspectorComponent::alignNotePropertyRows()
{
    NotePropertyRowComponent* rows[] = {
        &lblNotePitch, &lblNoteStart, &lblNoteLength, &lblNoteVelocity,
        &lblNoteScaleDegree, &lblNoteFunctionalRole, &lblNoteMotionTendency,
        &lblNoteColorUsage, &lblNoteInterval
    };

    auto& state = AppStateManager::getInstance();
    float notePt = (float)std::clamp(state.notePropertyFontSizePt, 9, 24);
    auto font = CubaseTheme::getFont(notePt);

    int maxTitleW = 0;
    int maxMainW = 0;

    for (auto* row : rows)
    {
        if (row->isVisible())
        {
            maxTitleW = std::max(maxTitleW, row->getDesiredTitleWidth(font));
            maxMainW = std::max(maxMainW, row->getDesiredMainWidth(font));
        }
    }

    // 紧凑精致的高空间利用率列距：
    // 第一列（标题）后紧跟 6px 间隙
    int unifiedTitleW = maxTitleW > 0 ? (maxTitleW + 6) : juce::roundToInt(notePt * 4.2f + 4.0f);
    // 第二列（无括号主体）后紧跟 8px 间隙
    int unifiedMainW = maxMainW > 0 ? (maxMainW + 8) : juce::roundToInt(notePt * 6.0f + 6.0f);

    for (auto* row : rows)
    {
        row->setColumnWidths(unifiedTitleW, unifiedMainW);
    }
}

void InspectorComponent::applyCustomModuleSettings()
{
    auto& state = AppStateManager::getInstance();

    // 1. Note Inspector Visibility & Font Size
    lblNotePitch.setVisible(state.noteShowPitch);
    lblNoteStart.setVisible(state.noteShowStart);
    lblNoteLength.setVisible(state.noteShowLength);
    lblNoteVelocity.setVisible(state.noteShowVelocity);
    lblNoteScaleDegree.setVisible(state.noteShowScaleDegree);
    lblNoteFunctionalRole.setVisible(state.noteShowFunctionalRole);
    lblNoteMotionTendency.setVisible(state.noteShowMotionTendency);
    lblNoteColorUsage.setVisible(state.noteShowColorUsage);
    lblNoteInterval.setVisible(state.noteShowInterval);

    float noteFontSize = (float)std::clamp(state.notePropertyFontSizePt, 9, 24);
    auto noteFont = juce::FontOptions(noteFontSize);
    lblSelectionCount.setFont(noteFont.withStyle("bold"));
    lblNotePitch.setFont(noteFont);
    lblNoteStart.setFont(noteFont);
    lblNoteLength.setFont(noteFont);
    lblNoteVelocity.setFont(noteFont);
    lblNoteScaleDegree.setFont(noteFont);
    lblNoteFunctionalRole.setFont(noteFont);
    lblNoteMotionTendency.setFont(noteFont);
    lblNoteColorUsage.setFont(noteFont);
    lblNoteInterval.setFont(noteFont);

    alignNotePropertyRows();

    // 2. Chord Analysis Display Mode
    // 0 = Both, 1 = Chord Only, 2 = Roman Only
    if (state.chordAnalysisDisplayMode == 1)
    {
        lblDetectedChord.setVisible(true);
        lblRomanNumeral.setVisible(false);
    }
    else if (state.chordAnalysisDisplayMode == 2)
    {
        lblDetectedChord.setVisible(false);
        lblRomanNumeral.setVisible(true);
    }
    else
    {
        lblDetectedChord.setVisible(true);
        lblRomanNumeral.setVisible(true);
    }

    // Font Size: using chordAnalysisFontSizePt (10 ~ 48 pt)
    float chordFontSize = (float)std::clamp(state.chordAnalysisFontSizePt, 10, 48);
    float romanFontSize = std::max(9.0f, std::round(chordFontSize * 0.72f));
    lblDetectedChord.setFont(juce::FontOptions(chordFontSize).withStyle("bold"));
    lblRomanNumeral.setFont(juce::FontOptions(romanFontSize).withStyle("bold"));

    // Visual Style: 8 rich DAW palettes (0 to 7)
    switch (state.chordAnalysisVisualStyle)
    {
        case 1: // 霓虹科技 (Amber & Magenta)
            lblDetectedChord.setColour(juce::Label::textColourId, juce::Colour(0xffff9900));
            lblRomanNumeral.setColour(juce::Label::textColourId, juce::Colour(0xffff44aa));
            break;
        case 2: // 纯净白银 (Silver White & Ice Blue)
            lblDetectedChord.setColour(juce::Label::textColourId, juce::Colours::white);
            lblRomanNumeral.setColour(juce::Label::textColourId, juce::Colour(0xffb0c4de));
            break;
        case 3: // 翡翠薄荷 (Emerald & Mint)
            lblDetectedChord.setColour(juce::Label::textColourId, juce::Colour(0xff10b981));
            lblRomanNumeral.setColour(juce::Label::textColourId, juce::Colour(0xff6ee7b7));
            break;
        case 4: // 赛博粉紫 (Cyber Violet & Neon Pink)
            lblDetectedChord.setColour(juce::Label::textColourId, juce::Colour(0xffa855f7));
            lblRomanNumeral.setColour(juce::Label::textColourId, juce::Colour(0xfff472b6));
            break;
        case 5: // 落日炽焰 (Sunset Coral & Warm Gold)
            lblDetectedChord.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
            lblRomanNumeral.setColour(juce::Label::textColourId, juce::Colour(0xfff59e0b));
            break;
        case 6: // 极光幽蓝 (Aurora Cyan & Lavender)
            lblDetectedChord.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
            lblRomanNumeral.setColour(juce::Label::textColourId, juce::Colour(0xff818cf8));
            break;
        case 7: // 矩阵暗绿 (Matrix Neon Green & Sage)
            lblDetectedChord.setColour(juce::Label::textColourId, juce::Colour(0xff22c55e));
            lblRomanNumeral.setColour(juce::Label::textColourId, juce::Colour(0xff86efac));
            break;
        case 0: // 经典高亮 (Gold & Cyan)
        default:
            lblDetectedChord.setColour(juce::Label::textColourId, CubaseTheme::accentGold());
            lblRomanNumeral.setColour(juce::Label::textColourId, CubaseTheme::accentCyan());
            break;
    }

    updateChordAnalysis(midiModel.getSelectedNotes());

    refreshSecondaryDominantSettings();
    refreshQuickChordMode();
    resized();
    repaint();
}

void InspectorComponent::refreshQuickChordMode()
{
    const bool complexMode = AppStateManager::getInstance().quickChordComplexMode;
    for (auto& item : quickChordButtons)
        if (const auto* type = QuickChordCatalog::find(item.id))
            item.button->setVisible(complexMode || type->simple);
    resized();
    repaint();
}

int InspectorComponent::quickChordModuleHeight(int contentWidth) const
{
    const int groupWidth = contentWidth - 8;
    const int columns = quickChordColumns(groupWidth);
    const int singletonsPerRow = quickChordSingletonsPerRow(groupWidth);
    std::array<int, 6> counts{};
    for (const auto* type : QuickChordCatalog::visible(
             AppStateManager::getInstance().quickChordComplexMode))
    {
        const int index = quickChordCategoryIndex(type->category);
        if (index >= 0) ++counts[(size_t)index];
    }
    int height = 30 + 8;
    for (size_t category = 0; category < counts.size();)
    {
        const int count = counts[category];
        if (count == 0)
        {
            ++category;
            continue;
        }
        if (count == 1)
        {
            int packed = 1;
            while (packed < singletonsPerRow
                && category + (size_t)packed < counts.size()
                && counts[category + (size_t)packed] == 1)
                ++packed;
            height += quickChordCategoryLabelHeight + quickChordButtonHeight
                + quickChordCategoryGap;
            category += (size_t)packed;
            continue;
        }
        const int rows = quickChordRowCount(count, columns);
        height += quickChordCategoryLabelHeight + rows * quickChordButtonHeight
            + (rows - 1) * quickChordButtonRowGap + quickChordCategoryGap;
        ++category;
    }
    return height;
}

void InspectorComponent::layoutQuickChordModule()
{
    auto body = grpQuickChord.getLocalBounds().withTrimmedTop(30)
        .withTrimmedBottom(8).withTrimmedLeft(8).withTrimmedRight(8);
    const int columns = quickChordColumns(grpQuickChord.getWidth());
    const int singletonsPerRow = quickChordSingletonsPerRow(grpQuickChord.getWidth());
    const auto visible = QuickChordCatalog::visible(
        AppStateManager::getInstance().quickChordComplexMode);
    std::array<std::vector<const QuickChordType*>, 6> typesByCategory;
    for (const auto* type : visible)
    {
        const int category = quickChordCategoryIndex(type->category);
        if (category >= 0) typesByCategory[(size_t)category].push_back(type);
    }
    for (size_t category = 0; category < typesByCategory.size(); ++category)
        quickChordCategoryLabels[category].setVisible(!typesByCategory[category].empty());

    auto placeButton = [this](int id, juce::Rectangle<int> bounds)
    {
        for (auto& item : quickChordButtons)
            if (item.id == id)
            {
                item.button->setBounds(bounds);
                break;
            }
    };

    for (size_t category = 0; category < typesByCategory.size();)
    {
        const auto& types = typesByCategory[category];
        if (types.empty())
        {
            ++category;
            continue;
        }
        if (types.size() == 1)
        {
            int packed = 1;
            while (packed < singletonsPerRow
                && category + (size_t)packed < typesByCategory.size()
                && typesByCategory[category + (size_t)packed].size() == 1)
                ++packed;
            auto labelRow = body.removeFromTop(quickChordCategoryLabelHeight);
            auto buttonRow = body.removeFromTop(quickChordButtonHeight);
            const int itemWidth = (buttonRow.getWidth()
                - (packed - 1) * quickChordPackedGap) / packed;
            for (int i = 0; i < packed; ++i)
            {
                const int width = i == packed - 1 ? buttonRow.getWidth() : itemWidth;
                auto& label = quickChordCategoryLabels[category + (size_t)i];
                label.setBounds(labelRow.removeFromLeft(width));
                placeButton(typesByCategory[category + (size_t)i][0]->id,
                    buttonRow.removeFromLeft(width));
                if (i < packed - 1)
                {
                    labelRow.removeFromLeft(quickChordPackedGap);
                    buttonRow.removeFromLeft(quickChordPackedGap);
                }
            }
            body.removeFromTop(quickChordCategoryGap);
            category += (size_t)packed;
            continue;
        }
        auto& label = quickChordCategoryLabels[category];
        label.setBounds(body.removeFromTop(quickChordCategoryLabelHeight));
        for (size_t first = 0; first < types.size();)
        {
            auto row = body.removeFromTop(quickChordButtonHeight);
            const int count = std::min(columns, (int)types.size() - (int)first);
            const int buttonWidth = std::max(1, (row.getWidth() - (count - 1) * 4) / count);
            for (int i = 0; i < count; ++i)
            {
                const int id = types[first + (size_t)i]->id;
                placeButton(id, row.removeFromLeft(
                    i == count - 1 ? row.getWidth() : buttonWidth));
                if (i < count - 1) row.removeFromLeft(4);
            }
            first += (size_t)count;
            if (first < types.size()) body.removeFromTop(quickChordButtonRowGap);
        }
        body.removeFromTop(quickChordCategoryGap);
        ++category;
    }
}

void InspectorComponent::refreshSecondaryDominantSettings()
{
    const auto& state = AppStateManager::getInstance();
    const int targetSource = std::clamp(state.secondaryDominantTargetSource, 0, 3);
    secondaryTargetCombo.setSelectedId(targetSource + 1,
                                       juce::dontSendNotification);
    secondaryManualRootCombo.setSelectedId(std::clamp(state.secondaryDominantManualRootPc, 0, 11) + 1,
                                           juce::dontSendNotification);
    secondaryTypeCombo.setSelectedId(state.secondaryDominantFlatNine ? 2 : 1, juce::dontSendNotification);
    secondaryVoicingCombo.setSelectedId(state.secondaryDominantRootInBass ? 2 : 1, juce::dontSendNotification);
    const bool chinese = Localization::getInstance().getLanguage() == Language::SimplifiedChinese;
    btnReharmOptions.setButtonText(secondarySettingsExpanded
        ? (chinese ? juce::String::fromUTF8(u8"收起") : juce::String("Hide"))
        : (chinese ? juce::String::fromUTF8(u8"设置") : juce::String("Settings")));
    resized();
    repaint();
}

void InspectorComponent::refreshReharmonizationSettings()
{
    const int algorithm = std::clamp(AppStateManager::getInstance().reharmonizationAlgorithm,
                                     0, kReharmonizationAlgorithmCount - 1);
    for (size_t i = 0; i < reharmonizationModeButtons.size(); ++i)
        reharmonizationModeButtons[i].setToggleState((int)i == algorithm,
                                                    juce::dontSendNotification);
    updateReharmonizationPreview();
}

void InspectorComponent::updateReharmonizationPreview()
{
    const bool chinese = Localization::getInstance().getLanguage() == Language::SimplifiedChinese;
    const auto selected = midiModel.getSelectedNotes();
    static const char* descriptions[kReharmonizationAlgorithmCount] = {
        "Shares two tones within the current scale",
        "Replaces a dominant seventh a tritone away",
        "Borrows major or minor from the parallel mode",
        "Chooses a chromatic third-related chord",
        "Exchanges tonic with sixth degree or fourth with second",
        "Replaces V7 with a flat-seventh dominant in a major key",
        "Finds the closest chord within the current scale"
    };
    if (selected.empty())
    {
        for (size_t i = 0; i < reharmonizationModeButtons.size(); ++i)
        {
            reharmonizationPlans[i] = {};
            reharmonizationModeButtons[i].setAvailability(
                ReharmonizationModeButton::Availability::Unknown);
            reharmonizationModeButtons[i].setTooltip(tr(descriptions[i]));
            reharmonizationPreviewTexts[i] = tr(descriptions[i]) + "\n" + (chinese
                ? juce::String::fromUTF8(u8"选择和弦或琶音中的至少三个不同音高。")
                : juce::String("Select at least three distinct chord or arpeggio tones."));
        }
        showReharmonizationPreview();
        return;
    }

    for (size_t i = 0; i < reharmonizationPlans.size(); ++i)
    {
        auto& plan = reharmonizationPlans[i];
        plan = Reharmonizer::prepare(midiModel, trackModel,
                                    static_cast<ReharmonizationAlgorithm>(i));
        const bool supported = plan.hasChanges();
        reharmonizationModeButtons[i].setAvailability(supported
            ? ReharmonizationModeButton::Availability::Supported
            : ReharmonizationModeButton::Availability::Unavailable);
        const auto reason = plan.error.isNotEmpty() ? plan.error
            : (chinese ? juce::String::fromUTF8(u8"当前选择没有可应用的变化")
                       : juce::String("No applicable change for this selection"));
        reharmonizationModeButtons[i].setTooltip(tr(descriptions[i]) + "\n"
            + (supported
                ? (chinese ? juce::String::fromUTF8(u8"点击直接应用")
                           : juce::String("Click to apply"))
                : (chinese ? juce::String::fromUTF8(u8"不支持：")
                           : juce::String("Unavailable: ")) + reason));

        juce::StringArray lines;
        lines.add(tr(descriptions[i]) + juce::String::fromUTF8(u8" · ") + (supported
            ? (chinese ? juce::String::fromUTF8(u8"点击应用") : juce::String("Click to apply"))
            : (chinese ? juce::String::fromUTF8(u8"不支持") : juce::String("Unavailable"))));
        if (!supported) lines.add(reason);
        for (size_t groupIndex = 0; groupIndex < plan.groups.size(); ++groupIndex)
        {
            const auto& group = plan.groups[groupIndex];
            const auto prefix = plan.groups.size() > 1
                ? juce::String(groupIndex + 1) + ". " : juce::String();
            if (!group.supported)
            {
                lines.add(prefix + (group.sourceChord.isNotEmpty()
                    ? group.sourceChord + juce::String::fromUTF8(u8" · ") : juce::String())
                    + group.issue);
                continue;
            }
            auto line = prefix + group.sourceChord + juce::String::fromUTF8(u8" → ")
                + group.targetChord;
            const auto algorithm = static_cast<ReharmonizationAlgorithm>(i);
            if (algorithm == ReharmonizationAlgorithm::ParallelMode)
                line += chinese ? juce::String::fromUTF8(u8" · 同主音借用")
                                : juce::String::fromUTF8(u8" · parallel borrowing");
            else if (algorithm == ReharmonizationAlgorithm::TritoneSubstitution)
                line += chinese ? juce::String::fromUTF8(u8" · 三全音方向")
                                : juce::String::fromUTF8(u8" · tritone away");
            else if (algorithm == ReharmonizationAlgorithm::BackdoorDominant)
                line += chinese ? juce::String::fromUTF8(u8" · 后门属和弦")
                                : juce::String::fromUTF8(u8" · backdoor dominant");
            else
                line += (chinese ? juce::String::fromUTF8(u8" · 根音 ")
                                 : juce::String::fromUTF8(u8" · root "))
                    + (group.rootMotion >= 0 ? "+" : "") + juce::String(group.rootMotion)
                    + (chinese ? juce::String::fromUTF8(u8" 半音") : juce::String(" semitones"));
            line += (chinese ? juce::String::fromUTF8(u8" · 共有音 ")
                             : juce::String::fromUTF8(u8" · common tones "))
                + juce::String(group.commonTones);
            lines.add(line);
        }
        if (plan.groups.empty() && supported) lines.add(reason);
        reharmonizationPreviewTexts[i] = lines.joinIntoString("\n");
    }
    showReharmonizationPreview();
}

void InspectorComponent::showReharmonizationPreview()
{
    const int index = hoveredReharmonizationMode >= 0
        ? hoveredReharmonizationMode
        : std::clamp(AppStateManager::getInstance().reharmonizationAlgorithm,
                     0, kReharmonizationAlgorithmCount - 1);
    const auto& preview = reharmonizationPreviewTexts[(size_t)index];
    if (txtReharmonizationPreview.getText() != preview)
    {
        txtReharmonizationPreview.setText(preview, false);
        txtReharmonizationPreview.setCaretPosition(0);
    }
}

void InspectorComponent::refreshModeBorrowingSettings()
{
    const auto& state = AppStateManager::getInstance();
    btnBorrowingSourceDisclosure.setExpanded(borrowingSourceSettingsExpanded);
    btnBorrowingSourceAuto.setVisible(borrowingSourceSettingsExpanded);
    btnBorrowingSourceCustom.setVisible(borrowingSourceSettingsExpanded);
    btnBorrowingSourceAuto.setToggleState(!state.modeBorrowingCustomSource,
                                          juce::dontSendNotification);
    btnBorrowingSourceCustom.setToggleState(state.modeBorrowingCustomSource,
                                            juce::dontSendNotification);
    borrowingSourceRootCombo.setVisible(borrowingSourceSettingsExpanded
                                        && state.modeBorrowingCustomSource);
    borrowingSourceScaleCombo.setVisible(borrowingSourceSettingsExpanded
                                         && state.modeBorrowingCustomSource);
    borrowingStyleCombo.setSelectedId(state.modeBorrowingStyle + 1, juce::dontSendNotification);
    const auto& scales = ScaleDefinitions::getAllScales();
    auto populateScale = [&scales](juce::ComboBox& combo, const juce::String& saved) {
        combo.clear(juce::dontSendNotification);
        int scaleId = 0;
        int nextId = 1;
        for (const auto& scale : scales)
        {
            if (scale.intervals.size() != 7 || scale.intervals.front() != 0
                || scale.intervals.back() > 11
                || std::adjacent_find(scale.intervals.begin(), scale.intervals.end(),
                    [](int a, int b) { return a >= b; }) != scale.intervals.end())
                continue;
            combo.addItem(scale.name, nextId);
            if (scale.name == saved) scaleId = nextId;
            ++nextId;
        }
        if (scaleId == 0 && saved.isNotEmpty())
        {
            const bool chinese = Localization::getInstance().getLanguage() == Language::SimplifiedChinese;
            combo.addItem((chinese ? juce::String::fromUTF8(u8"不可用：")
                                   : juce::String("Unavailable: ")) + saved, nextId);
            combo.setItemEnabled(nextId, false);
            scaleId = nextId;
        }
        combo.setSelectedId(scaleId > 0 ? scaleId : 1, juce::dontSendNotification);
    };
    populateScale(borrowingSourceScaleCombo, state.modeBorrowingSourceScale);
    populateScale(borrowingScaleCombo, state.modeBorrowingScale);
    borrowingSourceRootCombo.setSelectedId(state.modeBorrowingSourceRootPc + 1,
                                           juce::dontSendNotification);
    borrowingTargetRootCombo.setSelectedId(
        (state.modeBorrowingStyle == 0 ? state.modeBorrowingSourceRootPc
                                      : state.modeBorrowingTargetRootPc) + 1,
        juce::dontSendNotification);
    borrowingTargetRootCombo.setEnabled(state.modeBorrowingStyle == 1);
    borrowingDegreeCombo.setSelectedId(std::clamp(state.modeBorrowingDegree, -1, 6) + 2,
                                      juce::dontSendNotification);
    refreshChordEditingPreview();
    resized();
}

void InspectorComponent::refreshChordEditingPreview()
{
    const bool chinese = Localization::getInstance().getLanguage() == Language::SimplifiedChinese;
    const auto selected = midiModel.getSelectedNotes();
    const auto* track = trackModel.getActiveInstrumentTrack();
    const auto* clip = trackModel.getActiveClip();
    const bool editable = track != nullptr && clip != nullptr && !track->isLocked();
    bool hasChord = editable && !selected.empty();
    bool hasSeventhVoices = hasChord;
    bool hasDropTwoVoices = hasChord;
    juce::String chordStatus = chinese ? juce::String::fromUTF8(u8"选择和弦音符")
                                       : juce::String("Select chord notes");
    if (hasChord)
    {
        std::set<int> pitchClasses;
        std::set<int> distinctPitches;
        for (const auto& note : selected)
        {
            pitchClasses.insert((note.pitch % 12 + 12) % 12);
            distinctPitches.insert(note.pitch);
        }
        const auto first = std::min_element(selected.begin(), selected.end(),
            [](const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
        const auto layout = midiModel.getEffectiveLayoutAtBeat(first->startBeat, &trackModel);
        const auto chord = ChordClassifier::identifyChord(selected, layout.tonicPc,
                                                          layout.scaleName, layout.tonicName);
        hasChord = pitchClasses.size() >= 3 && chord.confidence >= 0.5;
        hasSeventhVoices = hasChord && distinctPitches.size() >= 4;
        hasDropTwoVoices = hasChord && distinctPitches.size() == 4;
        chordStatus = hasChord ? chord.fullName : tr("No clear chord");
        if (hasChord && chord.family != ChordFamily::Unknown)
        {
            juce::String family;
            switch (chord.family)
            {
                case ChordFamily::Major: family = chinese ? juce::String::fromUTF8(u8"大") : "Major"; break;
                case ChordFamily::Minor: family = chinese ? juce::String::fromUTF8(u8"小") : "Minor"; break;
                case ChordFamily::Dominant: family = chinese ? juce::String::fromUTF8(u8"属") : "Dominant"; break;
                case ChordFamily::Diminished: family = chinese ? juce::String::fromUTF8(u8"减") : "Diminished"; break;
                case ChordFamily::HalfDiminished: family = chinese ? juce::String::fromUTF8(u8"半减") : "Half-diminished"; break;
                case ChordFamily::Augmented: family = chinese ? juce::String::fromUTF8(u8"增") : "Augmented"; break;
                case ChordFamily::Suspended: family = chinese ? juce::String::fromUTF8(u8"挂留") : "Suspended"; break;
                default: break;
            }
            if (family.isNotEmpty()) chordStatus += juce::String::fromUTF8(u8" · ") + family;
        }
    }
    lblChordShapeStatus.setText(chordStatus, juce::dontSendNotification);
    for (auto* button : { &btnShapeMajor, &btnShapeMinor,
                          &btnShapeDiminished, &btnShapeAugmented,
                          &btnShapeSus2, &btnShapeSus4,
                          &btnVoicingExpand, &btnVoicingContract })
        button->setEnabled(hasChord);
    for (auto* button : { &btnShapeDominant7, &btnShapeMajor7,
                          &btnShapeMinor7, &btnShapeHalfDiminished7 })
        button->setEnabled(hasChord && hasSeventhVoices);
    btnVoicingDrop2.setEnabled(hasChord && hasDropTwoVoices);
    for (size_t i = 0; i < stackButtons.size(); ++i)
    {
        bool available = false;
        if (hasChord && (AppStateManager::getInstance().showChordStackDirections
                         || i == 3 || i == 7))
        {
            const auto plan = ChordEditing::changeStack(midiModel, trackModel, selected,
                static_cast<ChordStackDirection>(i));
            available = plan.hasChanges();
        }
        auto& button = stackButtons[i];
        button.setEnabled(available);
        const auto background = available ? CubaseTheme::buttonBackground()
            : CubaseTheme::bgSurface().interpolatedWith(
                CubaseTheme::buttonBackground(), 0.25f);
        if (button.findColour(juce::TextButton::buttonColourId) != background)
            button.setColour(juce::TextButton::buttonColourId, background);
    }

    const auto& state = AppStateManager::getInstance();
    const auto style = state.modeBorrowingStyle == 1 ? ModeBorrowingStyle::Approach
                                                       : ModeBorrowingStyle::SameTonic;
    borrowingDegreeCombo.setEnabled(style == ModeBorrowingStyle::SameTonic);
    const double sourceBeat = selected.empty()
        ? (clip != nullptr ? clip->startBeat : 0.0)
        : std::min_element(selected.begin(), selected.end(), [](const auto& a, const auto& b) {
            return a.startBeat < b.startBeat;
        })->startBeat;
    const auto effective = midiModel.getEffectiveLayoutAtBeat(sourceBeat, &trackModel);
    const int sourceTonic = state.modeBorrowingCustomSource
        ? state.modeBorrowingSourceRootPc : effective.tonicPc;
    const auto sourceScale = state.modeBorrowingCustomSource
        ? state.modeBorrowingSourceScale : effective.scaleName;
    btnBorrowingSourceDisclosure.setStatus(state.modeBorrowingCustomSource
        ? (chinese ? juce::String::fromUTF8(u8"自定义") : juce::String("Custom"))
        : (chinese ? juce::String::fromUTF8(u8"自动") : juce::String("Auto")));
    const auto sourceTonicName = ChordClassifier::spellPitchClass(
        sourceTonic, sourceTonic, sourceScale);
    btnBorrowingSourceDisclosure.setSummary(sourceTonicName
        + " " + sourceScale.upToFirstOccurrenceOf(" (", false, false));
    btnBorrowingSourceDisclosure.setTooltip(state.modeBorrowingCustomSource
        ? (chinese ? juce::String::fromUTF8(u8"当前手动指定前调式：")
                   : juce::String("Custom source mode: "))
            + sourceTonicName + " " + sourceScale
        : (chinese ? juce::String::fromUTF8(u8"按每组所选音符所在位置读取调式轨；没有覆盖调式块时使用全局调式。")
                   : juce::String("Use each selected group's mode-track block, falling back to the global mode.")));
    if (style == ModeBorrowingStyle::SameTonic)
        borrowingTargetRootCombo.setSelectedId(sourceTonic + 1, juce::dontSendNotification);
    const int targetTonic = style == ModeBorrowingStyle::SameTonic
        ? sourceTonic : state.modeBorrowingTargetRootPc;
    const auto targetTonicName = ChordClassifier::spellPitchClass(
        targetTonic, targetTonic, state.modeBorrowingScale);
    const auto modeLine = (state.modeBorrowingCustomSource
        ? juce::String() : (chinese ? juce::String::fromUTF8(u8"自动：")
                                  : juce::String("Automatic: ")))
        + sourceTonicName
        + " " + sourceScale + juce::String::fromUTF8(u8" → ")
        + targetTonicName + " " + state.modeBorrowingScale;
    if (!editable)
    {
        txtBorrowingPreview.setText(chinese ? juce::String::fromUTF8(u8"请先打开未锁定乐器轨上的 MIDI 片段。")
                                            : juce::String("Open an unlocked MIDI clip first."), false);
        btnApplyBorrowing.setEnabled(false);
        return;
    }
    if (selected.empty())
    {
        const int degree = state.modeBorrowingDegree < 0 ? 0 : state.modeBorrowingDegree;
        const auto name = ChordEditing::borrowedChordName(targetTonic,
                                                           state.modeBorrowingScale, degree, false);
        const bool sourceReady = std::any_of(ScaleDefinitions::getAllScales().begin(),
            ScaleDefinitions::getAllScales().end(), [&sourceScale](const ScaleConfig& scale) {
                return scale.name == sourceScale && scale.intervals.size() == 7
                    && scale.intervals.front() == 0 && scale.intervals.back() <= 11
                    && std::adjacent_find(scale.intervals.begin(), scale.intervals.end(),
                        [](int a, int b) { return a >= b; }) == scale.intervals.end();
            });
        const bool ready = style == ModeBorrowingStyle::SameTonic
            && name.isNotEmpty() && sourceReady;
        txtBorrowingPreview.setText(modeLine + "\n" + (ready
            ? (chinese ? juce::String::fromUTF8(u8"生成 ") : juce::String("Create ")) + name
                + (chinese ? juce::String::fromUTF8(u8"，再用鼠标选择时间位置。")
                           : juce::String("; click a time in the piano roll."))
            : (!sourceReady
                ? (chinese ? juce::String::fromUTF8(u8"前调式不可用。")
                           : juce::String("Source mode unavailable."))
                : (style == ModeBorrowingStyle::SameTonic && name.isEmpty())
                ? (chinese ? juce::String::fromUTF8(u8"目标级的音组无法可靠识别为和弦。")
                           : juce::String("The target degree does not form a recognizable chord."))
                : (chinese ? juce::String::fromUTF8(u8"调式趋近需要先选择和弦音符。")
                           : juce::String("Select chord notes for mode approach.")))), false);
        btnApplyBorrowing.setEnabled(ready);
        return;
    }
    const auto plan = ChordEditing::borrow(midiModel, trackModel, selected,
        sourceTonic, sourceScale, targetTonic, state.modeBorrowingScale,
        style, state.modeBorrowingDegree, !state.modeBorrowingCustomSource);
    juce::StringArray lines;
    const bool multipleSources = !state.modeBorrowingCustomSource && plan.groups.size() > 1
        && std::any_of(std::next(plan.groups.begin()), plan.groups.end(), [&plan](const auto& group) {
            return group.sourceMode != plan.groups.front().sourceMode;
        });
    lines.add(multipleSources
        ? (chinese ? juce::String::fromUTF8(u8"自动读取每组音符所在调式")
                   : juce::String("Source mode follows each selected group"))
        : modeLine);
    if (!plan.succeeded())
        lines.add(plan.error);
    else for (const auto& group : plan.groups)
    {
        auto line = group.supported ? group.source + juce::String::fromUTF8(u8" → ") + group.target
                                    : group.issue;
        if (multipleSources)
            line += " (" + group.sourceMode + juce::String::fromUTF8(u8" → ")
                + group.targetMode + ")";
        lines.add(line);
    }
    if (plan.succeeded() && !plan.hasChanges())
        lines.add(chinese ? juce::String::fromUTF8(u8"已符合目标调式，无需移动音符。")
                          : juce::String("Already fits the target mode."));
    txtBorrowingPreview.setText(lines.joinIntoString("\n"), false);
    btnApplyBorrowing.setEnabled(plan.hasChanges());
}

void InspectorComponent::setSecondaryDominantSettingsExpanded(bool expanded)
{
    if (secondarySettingsExpanded == expanded) return;
    secondarySettingsExpanded = expanded;
    refreshSecondaryDominantSettings();
}

void InspectorComponent::focusSecondaryDominantSettings()
{
    if (!isSectionVisibleInThisDock(4)) return;
    setSecondaryDominantSettingsExpanded(true);
    viewport.setViewPosition(viewport.getViewPositionX(), std::max(0, grpReharm.getY() - 8));
}

void InspectorComponent::updateScaleAndKeyControls()
{
    keyRootCombo.setSelectedId(midiModel.getKeySignature().rootPitchClass + 1, juce::dontSendNotification);

    const auto& scales = ScaleDefinitions::getAllScales();
    for (size_t i = 0; i < scales.size(); ++i)
    {
        if (scales[i].name == midiModel.getScaleName())
        {
            scaleTypeCombo.setSelectedId((int)i + 1, juce::dontSendNotification);
            break;
        }
    }
    btnApplyCurrentMode.setEnabled(keyRootCombo.getSelectedId() > 0
                                   && scaleTypeCombo.getSelectedId() > 0);
}

void InspectorComponent::languageChanged()
{
    const bool chinese = Localization::getInstance().getLanguage() == Language::SimplifiedChinese;
    lblEmptyDock.setText(chinese ? juce::String::fromUTF8(u8"当前侧栏没有显示的模块")
                                 : juce::String("No modules in this sidebar"),
                            juce::dontSendNotification);
    btnEmptyDockManage.setButtonText(chinese ? juce::String::fromUTF8(u8"管理模块")
                                              : juce::String("Manage modules"));
    grpNoteInfo.setText(chinese ? juce::String::fromUTF8(u8"音符属性") : juce::String("Notes"));
    grpScaleMode.setText(chinese ? juce::String::fromUTF8(u8"调式色彩") : juce::String("Mode Colour"));
    grpChordDetection.setText(chinese ? juce::String::fromUTF8(u8"和弦分析") : juce::String("Chords"));
    btnAnalyzeMode.setButtonText(chinese ? juce::String::fromUTF8(u8"分析整轨")
                                         : juce::String("Analyze Track"));
    btnAnalyzeMode.setTooltip(chinese
        ? juce::String::fromUTF8(u8"统计当前乐器轨的所有 MIDI 片段，按时值、力度、节拍和调式常见度排序；多选音符列表按音高集合匹配，结果可能不同。")
        : juce::String("Rank modes from every MIDI clip on this track using duration, velocity, meter and scale priors. A note selection uses pitch-set matching instead."));
    grpInstrument.setText(chinese ? juce::String::fromUTF8(u8"乐器与音源") : juce::String("Instrument"));
    grpAudioTrack.setText(chinese ? juce::String::fromUTF8(u8"音频轨") : juce::String("Audio Track"));
    btnImportAudio.setButtonText(chinese ? juce::String::fromUTF8(u8"导入音频文件...") : juce::String("Import Audio..."));
    lblAudioVolume.setText(chinese ? juce::String::fromUTF8(u8"音量") : juce::String("Volume"), juce::dontSendNotification);
    lblAudioPan.setText(chinese ? juce::String::fromUTF8(u8"声像") : juce::String("Pan"), juce::dontSendNotification);
    btnAudioMute.setButtonText(chinese ? juce::String::fromUTF8(u8"静音") : juce::String("Mute"));
    btnAudioSolo.setButtonText(chinese ? juce::String::fromUTF8(u8"独奏") : juce::String("Solo"));
    btnOpenEditor.setButtonText(chinese ? juce::String::fromUTF8(u8"打开乐器界面")
                                        : juce::String("Open Instrument"));
    grpReharm.setText(chinese ? juce::String::fromUTF8(u8"副属和弦") : juce::String("Dominant"));
    btnSecondaryDominant.setButtonText(chinese ? juce::String::fromUTF8(u8"生成并放置")
                                             : juce::String("Generate & Place"));
    btnSecondaryDominant.setTooltip(chinese
        ? juce::String::fromUTF8(u8"为所选目标音或和弦生成副属和弦，再在钢琴卷帘窗中选择放置位置。")
        : juce::String("Generate a secondary dominant for the selected target, then place it in the piano roll."));
    btnReharmOptions.setButtonText(secondarySettingsExpanded
        ? (chinese ? juce::String::fromUTF8(u8"收起") : juce::String("Hide"))
        : (chinese ? juce::String::fromUTF8(u8"设置") : juce::String("Settings")));
    btnReharmOptions.setTooltip(chinese
        ? juce::String::fromUTF8(u8"设置目标来源、和弦类型与声部排列。")
        : juce::String("Choose the target source, chord type, and voicing."));
    lblSecondaryTarget.setText(tr("Target") + ":", juce::dontSendNotification);
    lblSecondaryManualRoot.setText(tr("Root") + ":", juce::dontSendNotification);
    lblSecondaryType.setText(tr("Chord Type") + ":", juce::dontSendNotification);
    lblSecondaryVoicing.setText(tr("Voicing") + ":", juce::dontSendNotification);
    secondaryTargetCombo.clear(juce::dontSendNotification);
    secondaryTargetCombo.addItem(tr("Selected notes as one chord"), 1);
    secondaryTargetCombo.addItem(tr("Each selected note"), 2);
    secondaryTargetCombo.addItem(tr("Next harmony event"), 3);
    secondaryTargetCombo.addItem(tr("Fixed target root"), 4);
    secondaryManualRootCombo.clear(juce::dontSendNotification);
    for (int pc = 0; pc < 12; ++pc)
        secondaryManualRootCombo.addItem(ScaleDefinitions::getPitchName(pc), pc + 1);
    secondaryTypeCombo.clear(juce::dontSendNotification);
    secondaryTypeCombo.addItem("V7", 1);
    secondaryTypeCombo.addItem(juce::String::fromUTF8(u8"V7(♭9)"), 2);
    secondaryVoicingCombo.clear(juce::dontSendNotification);
    secondaryVoicingCombo.addItem(tr("Smooth voice leading"), 1);
    secondaryVoicingCombo.addItem(tr("Root in bass"), 2);
    refreshSecondaryDominantSettings();
    grpReharmonization.setText(chinese ? juce::String::fromUTF8(u8"和声重配") : juce::String("Reharmonize"));
    static const juce::String shortNamesZh[kReharmonizationAlgorithmCount] = {
        juce::String::fromUTF8(u8"调内共同音"),
        juce::String::fromUTF8(u8"三全音替代"),
        juce::String::fromUTF8(u8"同主音借用"),
        juce::String::fromUTF8(u8"色彩三度"),
        juce::String::fromUTF8(u8"功能替代"),
        juce::String::fromUTF8(u8"后门属和弦"),
        juce::String::fromUTF8(u8"调内平滑")
    };
    static const char* shortNamesEn[kReharmonizationAlgorithmCount] = {
        "Common tones", "Tritone", "Parallel mode", "Chromatic 3rd",
        "Function", "Backdoor", "Smooth"
    };
    for (size_t i = 0; i < reharmonizationModeButtons.size(); ++i)
        reharmonizationModeButtons[i].setButtonText(chinese
            ? shortNamesZh[i] : juce::String(shortNamesEn[i]));
    refreshReharmonizationSettings();
    grpChordShape.setText(chinese ? juce::String::fromUTF8(u8"和弦类型与排列") : juce::String("Chord Shape & Voicing"));
    const auto qualityHint = chinese
        ? juce::String::fromUTF8(u8"连续切换以原和弦排列为基准；保留未涉及的扩展音。")
        : juce::String("Repeated changes use the original voicing and keep unrelated extensions.");
    auto qualityTooltip = [&qualityHint](juce::TextButton& button, const juce::String& description) {
        button.setTooltip(description + "\n" + qualityHint);
    };
    qualityTooltip(btnShapeMajor, chinese ? juce::String::fromUTF8(u8"大三和弦") : juce::String("Major triad"));
    qualityTooltip(btnShapeMinor, chinese ? juce::String::fromUTF8(u8"小三和弦") : juce::String("Minor triad"));
    qualityTooltip(btnShapeDiminished, chinese ? juce::String::fromUTF8(u8"减三和弦") : juce::String("Diminished triad"));
    qualityTooltip(btnShapeAugmented, chinese ? juce::String::fromUTF8(u8"增三和弦") : juce::String("Augmented triad"));
    qualityTooltip(btnShapeSus2, chinese ? juce::String::fromUTF8(u8"挂二和弦") : juce::String("Suspended second"));
    qualityTooltip(btnShapeSus4, chinese ? juce::String::fromUTF8(u8"挂四和弦") : juce::String("Suspended fourth"));
    const auto seventhHint = chinese
        ? juce::String::fromUTF8(u8"需要每组至少四个声部；将重复音或附加音改为七度，不新增音符。")
        : juce::String("Each group needs four voices; a doubled or added tone becomes the seventh. No note is added.");
    qualityTooltip(btnShapeDominant7, (chinese ? juce::String::fromUTF8(u8"属七和弦") : juce::String("Dominant seventh")) + "\n" + seventhHint);
    qualityTooltip(btnShapeMajor7, (chinese ? juce::String::fromUTF8(u8"大七和弦") : juce::String("Major seventh")) + "\n" + seventhHint);
    qualityTooltip(btnShapeMinor7, (chinese ? juce::String::fromUTF8(u8"小七和弦") : juce::String("Minor seventh")) + "\n" + seventhHint);
    qualityTooltip(btnShapeHalfDiminished7, (chinese ? juce::String::fromUTF8(u8"半减七和弦") : juce::String("Half-diminished seventh")) + "\n" + seventhHint);
    btnVoicingExpand.setButtonText(chinese ? juce::String::fromUTF8(u8"均匀展开") : juce::String("Evenly Expand"));
    btnVoicingContract.setButtonText(chinese ? juce::String::fromUTF8(u8"均匀收拢") : juce::String("Evenly Contract"));
    btnVoicingExpand.setTooltip(chinese ? juce::String::fromUTF8(u8"保留低音与和弦音，均匀扩大相邻声部间距。")
                                        : juce::String("Keep the bass and chord tones while spreading voices evenly."));
    btnVoicingContract.setTooltip(chinese ? juce::String::fromUTF8(u8"保留低音与和弦音，均匀缩小相邻声部间距。")
                                          : juce::String("Keep the bass and chord tones while bringing voices closer evenly."));
    btnVoicingDrop2.setTooltip(chinese ? juce::String::fromUTF8(u8"将四声部和弦中第二高音降低一个八度。")
                                      : juce::String("Lower the second-highest voice of a four-note chord by one octave."));
    static const char8_t* stackNamesZh[8] = {
        u8"+大", u8"+小", u8"+属", u8"+调内",
        u8"−大", u8"−小", u8"−属", u8"−调内"
    };
    static const char* stackNamesEn[8] = {
        "+Maj", "+Min", "+Dom", "+In key", "-Maj", "-Min", "-Dom", "-In key"
    };
    for (size_t i = 0; i < stackButtons.size(); ++i)
    {
        stackButtons[i].setButtonText(chinese
            ? juce::String::fromUTF8(stackNamesZh[i]) : juce::String(stackNamesEn[i]));
        stackButtons[i].setTooltip(chinese
            ? (i < 4 ? juce::String::fromUTF8(u8"按三度高叠加入下一层音（7、9、11、13）；")
                     : juce::String::fromUTF8(u8"从最高扩展层开始去高叠化；"))
                + juce::String::fromUTF8(u8"方向：")
                + juce::String::fromUTF8(i % 4 == 0 ? u8"大和弦" : i % 4 == 1 ? u8"小和弦"
                    : i % 4 == 2 ? u8"属和弦" : u8"当前调式")
            : (i < 4 ? "Add the next stacked third (7, 9, 11, 13): "
                     : "Remove the highest extension: ")
                + juce::String(stackNamesEn[i]));
    }
    grpQuickChord.setText(chinese ? juce::String::fromUTF8(u8"快速和弦")
                                  : juce::String("Quick Chord"));
    static const char8_t* categoryNamesZh[6] = {
        u8"三和弦与挂留", u8"加音和弦", u8"六和弦",
        u8"七和弦", u8"延伸和弦", u8"变化和弦"
    };
    static const char* categoryNamesEn[6] = {
        "Triads & suspensions", "Added tones", "Sixth chords",
        "Seventh chords", "Extensions", "Alterations"
    };
    static const char8_t* categoryTooltipsZh[6] = {
        u8"三和弦与挂留", u8"加音和弦", u8"六和弦", u8"七和弦", u8"延伸和弦", u8"变化和弦"
    };
    static const char* categoryTooltipsEn[6] = {
        "Triads & suspensions", "Added tones", "Sixth chords",
        "Seventh chords", "Extensions", "Alterations"
    };
    for (size_t i = 0; i < quickChordCategoryLabels.size(); ++i)
    {
        quickChordCategoryLabels[i].setText(chinese
            ? juce::String::fromUTF8(categoryNamesZh[i])
            : juce::String(categoryNamesEn[i]), juce::dontSendNotification);
        quickChordCategoryLabels[i].setTooltip(chinese
            ? juce::String::fromUTF8(categoryTooltipsZh[i])
            : juce::String(categoryTooltipsEn[i]));
    }
    for (auto& item : quickChordButtons)
        if (const auto* type = QuickChordCatalog::find(item.id))
        {
            item.button->setButtonText(type->symbol);
            item.button->setTooltip((chinese ? type->chineseName : type->englishName)
                + " (" + type->symbol + ")");
        }
    grpModeBorrowing.setText(chinese ? juce::String::fromUTF8(u8"调式借用") : juce::String("Mode Borrowing"));
    btnBorrowingSourceDisclosure.setButtonText(chinese
        ? juce::String::fromUTF8(u8"前调式") : juce::String("Source mode"));
    btnBorrowingSourceAuto.setButtonText(chinese
        ? juce::String::fromUTF8(u8"自动识别") : juce::String("Automatic"));
    btnBorrowingSourceCustom.setButtonText(chinese
        ? juce::String::fromUTF8(u8"自定义") : juce::String("Custom"));
    lblBorrowingTarget.setText(chinese ? juce::String::fromUTF8(u8"目标调式")
                                       : juce::String("Target mode"), juce::dontSendNotification);
    borrowingSourceRootCombo.clear(juce::dontSendNotification);
    borrowingTargetRootCombo.clear(juce::dontSendNotification);
    for (int tonic = 0; tonic < 12; ++tonic)
    {
        const auto name = ScaleDefinitions::getPitchName(tonic);
        borrowingSourceRootCombo.addItem(name, tonic + 1);
        borrowingTargetRootCombo.addItem(name, tonic + 1);
    }
    btnApplyBorrowing.setButtonText(chinese ? juce::String::fromUTF8(u8"应用 / 放置和弦")
                                            : juce::String("Apply / Place Chord"));
    borrowingStyleCombo.clear(juce::dontSendNotification);
    borrowingStyleCombo.addItem(chinese ? juce::String::fromUTF8(u8"同主音 · 借用目标级和弦")
                                         : juce::String("Same tonic · borrow degree chord"), 1);
    borrowingStyleCombo.addItem(chinese ? juce::String::fromUTF8(u8"调式趋近 · 保留原根音与共同音")
                                         : juce::String("Mode approach · keep root and common tones"), 2);
    borrowingDegreeCombo.clear(juce::dontSendNotification);
    borrowingDegreeCombo.addItem(chinese ? juce::String::fromUTF8(u8"沿用原和弦根音 / 无选择时 I 级")
                                         : juce::String("Keep root / I if empty"), 1);
    static const char* degrees[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    for (int i = 0; i < 7; ++i)
        borrowingDegreeCombo.addItem(degrees[i], i + 2);
    refreshModeBorrowingSettings();
    grpChordTrack.setText(chinese ? juce::String::fromUTF8(u8"和弦轨") : juce::String("Chord Track"));
    btnExtractChordsFromMidi.setButtonText(chinese ? juce::String::fromUTF8(u8"从 MIDI 提取和弦")
                                                  : juce::String("Extract Chords"));
    grpModeTrack.setText(chinese ? juce::String::fromUTF8(u8"调式轨") : juce::String("Mode Track"));
    btnAnalyzeAndPopulateModes.setButtonText(chinese ? juce::String::fromUTF8(u8"分析并填充调式")
                                                    : juce::String("Analyze Modes"));
    grpMarkerTrack.setText(chinese ? juce::String::fromUTF8(u8"标记轨") : juce::String("Markers"));
    lblMarkerNameTitle.setText(tr("Name") + ":", juce::dontSendNotification);
    lblMarkerColorTitle.setText(tr("Color") + ":", juce::dontSendNotification);
    btnAddMarkerAtPlayhead.setButtonText(chinese ? juce::String::fromUTF8(u8"在播放位置添加")
                                                : juce::String("Add at Playhead"));
    btnDeleteMarker.setButtonText(tr("Delete Marker"));
    grpTimeSigTrack.setText(chinese ? juce::String::fromUTF8(u8"拍号轨") : juce::String("Time Sig."));
    btnApplyCandidate.setButtonText(chinese ? juce::String::fromUTF8(u8"应用所选调式")
                                              : juce::String("Apply Selected Mode"));
    btnApplyCandidateToRange.setButtonText(chinese
        ? juce::String::fromUTF8(u8"应用当前所选范围到调式轨")
        : juce::String("Apply Selection Range to Mode Track"));
    btnApplyCandidateToRange.setTooltip(chinese
        ? juce::String::fromUTF8(u8"选中多个音符后，将候选调式写入音符覆盖的时间范围；范围内已有的调式块会更新调式并保留原时间。")
        : juce::String("Use the selected notes' span; update intersecting mode blocks without changing their timing."));
    btnGroupByTonic.setButtonText(chinese ? juce::String::fromUTF8(u8"按主音分类")
                                           : juce::String("By tonic"));
    btnGroupByType.setButtonText(chinese ? juce::String::fromUTF8(u8"按调式类型分类")
                                          : juce::String("By mode type"));
    modeCandidatesList.setTooltip(chinese
        ? juce::String::fromUTF8(u8"分类标题右侧数字表示该分类中的候选调式数。")
        : juce::String("The number on each group heading is its candidate count."));
    btnCustomScale.setButtonText(chinese ? juce::String::fromUTF8(u8"自定义音阶")
                                         : juce::String("Custom Scale"));
    btnApplyCurrentMode.setButtonText(chinese ? juce::String::fromUTF8(u8"应用当前调式")
                                             : juce::String("Apply Mode"));
    btnApplyCurrentMode.setTooltip(chinese
        ? juce::String::fromUTF8(u8"将上方主音和调式类型应用到项目；若有调式轨，也更新所选或播放位置的调式块。可撤销。")
        : juce::String("Apply the chosen tonic and mode to the project and the selected or active mode block. Undo is supported."));
    keyRootCombo.setTooltip(chinese ? juce::String::fromUTF8(u8"选择主音，然后点击“应用当前调式”。")
                                    : juce::String("Choose a tonic, then click Apply Mode."));
    scaleTypeCombo.setTooltip(chinese ? juce::String::fromUTF8(u8"选择调式类型，然后点击“应用当前调式”。")
                                      : juce::String("Choose a mode type, then click Apply Mode."));
    grpFolderTrack.setText(chinese ? juce::String::fromUTF8(u8"文件夹轨") : juce::String("Folders"));
    lblFolderNotesTitle.setText(tr("Folder Notes") + ":", juce::dontSendNotification);
    btnModeTrackOverride.setButtonText(chinese ? juce::String::fromUTF8(u8"启用调式轨覆盖")
                                                 : juce::String("Use Mode Track"));
    updateNoteInfo();
    updateModePossibilities();
    updateVstCombo();
    updateVstInfo();
    updateChannelStrip();
    repaint();
}

void InspectorComponent::updateChannelStrip()
{
    int trackId = trackModel.getActiveTrackId();
    if (auto* trk = trackModel.getTrackById(trackId))
    {
        volSlider.setValue(trk->getVolume(), juce::dontSendNotification);
        panSlider.setValue(trk->getPan(), juce::dontSendNotification);
        audioVolumeSlider.setValue(trk->getVolume(), juce::dontSendNotification);
        audioPanSlider.setValue(trk->getPan(), juce::dontSendNotification);
        audioEngine.setTrackVolume(trackId, trk->getVolume());
        audioEngine.setTrackPan(trackId, trk->getPan());
    }
}

void InspectorComponent::updateVstCombo()
{
    vstPluginCombo.clear(juce::dontSendNotification);
    vstPluginCombo.addItem(tr("Built-in Instrument"), 1);
    const auto& list = audioEngine.getVstHostManager().getPluginList();
    for (int i = 0; i < list.getNumTypes(); ++i)
    {
        if (auto* desc = list.getType(i))
            vstPluginCombo.addItem(desc->name + " (" + desc->manufacturerName + ")", i + 2);
    }
    updateVstSelection();
}

void InspectorComponent::updateVstSelection()
{
    vstPluginCombo.setTextWhenNothingSelected(tr("Select Instrument"));
    const auto* track = trackModel.getTrackById(trackModel.getActiveTrackId());
    const bool isInstrument = track != nullptr && track->getType() == TrackType::Instrument;
    vstPluginCombo.setEnabled(isInstrument);
    if (!isInstrument)
    {
        vstPluginCombo.setSelectedId(0, juce::dontSendNotification);
        return;
    }
    if (track->getAssignedVstName().isEmpty())
    {
        vstPluginCombo.setSelectedId(1, juce::dontSendNotification);
        return;
    }

    const auto& list = audioEngine.getVstHostManager().getPluginList();
    for (int i = 0; i < list.getNumTypes(); ++i)
        if (auto* description = list.getType(i))
            if ((track->getAssignedVstIdentifier().isNotEmpty()
                    && description->createIdentifierString() == track->getAssignedVstIdentifier())
                || (track->getAssignedVstIdentifier().isEmpty()
                    && description->name.equalsIgnoreCase(track->getAssignedVstName())))
            {
                vstPluginCombo.setSelectedId(i + 2, juce::dontSendNotification);
                return;
            }
    vstPluginCombo.setTextWhenNothingSelected(track->getAssignedVstName() + " (unavailable)");
    vstPluginCombo.setSelectedId(0, juce::dontSendNotification);
}

void InspectorComponent::updateVstInfo()
{
    bool hasPlugin = audioEngine.getVstHostManager().hasLoadedPlugin(trackModel.getActiveTrackId());
    btnOpenEditor.setEnabled(hasPlugin);
}

juce::String InspectorComponent::analyzeChord(const std::vector<NoteEvent>& sel, juce::String& outRoman)
{
    if (sel.empty())
    {
        outRoman = "--";
        return tr("None");
    }

    auto first = std::min_element(sel.begin(), sel.end(), [](const auto& a, const auto& b) {
        return a.startBeat < b.startBeat;
    });
    double firstBeat = first->startBeat;
    auto layout = midiModel.getEffectiveLayoutAtBeat(firstBeat, &trackModel);
    std::set<int> pitchClasses;
    for (const auto& note : sel)
        pitchClasses.insert((note.pitch % 12 + 12) % 12);
    auto identified = ChordClassifier::identifyChord(sel, layout.tonicPc,
                                                      layout.scaleName, layout.tonicName);
    if (pitchClasses.size() < 3 || identified.confidence < 0.5)
    {
        outRoman = "--";
        return tr("No clear chord");
    }
    outRoman = identified.romanNumeral;
    return identified.fullName;
}

void InspectorComponent::updateChordAnalysis(const std::vector<NoteEvent>& selected)
{
    juce::String roman;
    lblDetectedChord.setText(analyzeChord(selected, roman), juce::dontSendNotification);
    lblRomanNumeral.setText(tr("Degree") + ": " + roman,
                            juce::dontSendNotification);
}

namespace
{
juce::String shortModeName(const juce::String& name, bool chinese)
{
    struct NamePair { const char* prefix; const char* translation; };
    static const NamePair names[] = {
        { "Ionian", "自然大调" }, { "Aeolian", "自然小调" },
        { "Harmonic Minor", "和声小调" }, { "Melodic Minor", "旋律小调" },
        { "Dorian", "多利亚" }, { "Phrygian", "弗里吉亚" },
        { "Lydian", "利蒂亚" }, { "Mixolydian", "混合利蒂亚" },
        { "Locrian", "洛克里亚" }, { "Major Pentatonic", "大调五声" },
        { "Minor Pentatonic", "小调五声" }, { "Blues Scale", "蓝调音阶" },
        { "Miyako-bushi", "都节音阶" }, { "Ritsu", "日本律音阶" },
        { "Ryukyu", "琉球音阶" }, { "Yonanuki Minor", "去四七小调五声" }
    };
    for (const auto& item : names)
        if (name.startsWithIgnoreCase(item.prefix))
            return chinese ? juce::String::fromUTF8(item.translation)
                           : juce::String(item.prefix);
    return name;
}

juce::Colour modeAccent(const juce::String& name)
{
    if (name.startsWithIgnoreCase("Ionian")) return juce::Colour(0xffe29a48);
    if (name.startsWithIgnoreCase("Aeolian")) return juce::Colour(0xff36b7ba);
    if (name.startsWithIgnoreCase("Dorian")) return juce::Colour(0xff69a7ed);
    if (name.startsWithIgnoreCase("Phrygian")) return juce::Colour(0xffa37be1);
    if (name.startsWithIgnoreCase("Lydian")) return juce::Colour(0xffd985b8);
    if (name.startsWithIgnoreCase("Mixolydian")) return juce::Colour(0xffd2b15c);
    if (name.startsWithIgnoreCase("Locrian")) return juce::Colour(0xff8192a8);
    if (name.startsWithIgnoreCase("Harmonic Minor")) return juce::Colour(0xff63b88d);
    if (name.startsWithIgnoreCase("Melodic Minor")) return juce::Colour(0xff858ee8);
    if (name.startsWithIgnoreCase("Miyako-bushi")) return juce::Colour(0xffbd78bf);
    if (name.startsWithIgnoreCase("Ritsu")) return juce::Colour(0xff63ad91);
    if (name.startsWithIgnoreCase("Ryukyu")) return juce::Colour(0xff6ca8c9);
    if (name.startsWithIgnoreCase("Yonanuki Minor")) return juce::Colour(0xffc47d93);
    if (name.containsIgnoreCase("Pentatonic")) return juce::Colour(0xffbbab70);
    return CubaseTheme::accentCyan();
}

juce::Colour tonicAccent(int pitchClass)
{
    static constexpr juce::uint32 colours[12] = {
        0xff55b6c8, 0xff719adb, 0xff8f8cdb, 0xffaf86cc,
        0xffca82ad, 0xffd68788, 0xffd2946f, 0xffcaa953,
        0xffa6b66b, 0xff79b584, 0xff61b69f, 0xff65a8b9
    };
    return juce::Colour(colours[((pitchClass % 12) + 12) % 12]);
}

int modeTypePriority(const juce::String& name)
{
    if (name.startsWithIgnoreCase("Ionian") || name.containsIgnoreCase("Natural Major")) return 0;
    if (name.startsWithIgnoreCase("Aeolian") || name.containsIgnoreCase("Natural Minor")) return 1;
    if (name.startsWithIgnoreCase("Harmonic Minor")) return 2;
    if (name.startsWithIgnoreCase("Melodic Minor")) return 3;
    if (name.startsWithIgnoreCase("Major Pentatonic")) return 4;
    if (name.startsWithIgnoreCase("Minor Pentatonic")) return 5;
    if (name.startsWithIgnoreCase("Blues")) return 6;
    if (name.startsWithIgnoreCase("Dorian")) return 7;
    if (name.startsWithIgnoreCase("Mixolydian")) return 8;
    if (name.startsWithIgnoreCase("Lydian")) return 9;
    if (name.startsWithIgnoreCase("Phrygian")) return 10;
    if (name.startsWithIgnoreCase("Locrian")) return 11;
    return 12; // Regional and custom scales retain their analysis order.
}
}

int InspectorComponent::ModeCandidateListModel::getNumRows()
{
    return (int)owner.modeCandidateRows.size();
}

void InspectorComponent::ModeCandidateListModel::paintListBoxItem(
    int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int)owner.modeCandidateRows.size()) return;
    const auto& display = owner.modeCandidateRows[(size_t)row];
    if (display.groupCandidateIndex < 0
        || display.groupCandidateIndex >= (int)owner.cachedCandidates.size()) return;
    const auto& groupCandidate = owner.cachedCandidates[(size_t)display.groupCandidateIndex];
    const auto groupColour = owner.modeGrouping == ModeGrouping::Tonic
        ? tonicAccent(groupCandidate.tonicPc) : modeAccent(groupCandidate.scaleName);
    const auto surface = CubaseTheme::bgSurface();
    if (display.candidateIndex < 0)
    {
        auto bounds = juce::Rectangle<float>(2.0f, 1.0f,
            std::max(0.0f, (float)width - 4.0f), std::max(0.0f, (float)height - 2.0f));
        g.setColour(surface.interpolatedWith(groupColour, 0.14f));
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(groupColour);
        g.fillRoundedRectangle(3.0f, 3.0f, 3.0f, (float)height - 6.0f, 1.5f);
        g.setFont(CubaseTheme::getFont(10.5f, true));
        g.setColour(CubaseTheme::textPrimary());
        g.drawFittedText(display.heading, 11, 2, std::max(1, width - 58),
                         height - 4, juce::Justification::centredLeft, 1);
        g.setFont(CubaseTheme::getFont(9.5f, true));
        g.setColour(groupColour);
        g.drawFittedText(juce::String(display.groupCount), width - 43, 2, 33,
                         height - 4, juce::Justification::centredRight, 1);
        return;
    }
    const auto& candidate = owner.cachedCandidates[(size_t)display.candidateIndex];
    const bool chinese = Localization::getInstance().getLanguage() == Language::SimplifiedChinese;
    const auto itemColour = owner.modeGrouping == ModeGrouping::Tonic
        ? modeAccent(candidate.scaleName) : tonicAccent(candidate.tonicPc);
    auto bounds = juce::Rectangle<float>(21.0f, 1.0f,
        std::max(0.0f, (float)width - 23.0f), std::max(0.0f, (float)height - 2.0f));
    if (selected)
    {
        g.setColour(surface.interpolatedWith(groupColour, 0.23f));
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(groupColour.withAlpha(0.65f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
    }
    else
    {
        g.setColour(surface.interpolatedWith(groupColour, row % 2 == 0 ? 0.055f : 0.025f));
        g.fillRoundedRectangle(bounds, 3.0f);
    }
    g.setColour(groupColour.withAlpha(0.55f));
    g.fillRect(12, 0, 1, height);
    g.setColour(itemColour);
    g.fillEllipse(28.0f, ((float)height - 7.0f) * 0.5f, 7.0f, 7.0f);
    g.setFont(CubaseTheme::getFont(10.5f, selected));
    g.setColour(CubaseTheme::textPrimary());
    const auto itemLabel = owner.modeGrouping == ModeGrouping::Tonic
        ? shortModeName(candidate.scaleName, chinese) : candidate.tonic;
    g.drawFittedText(itemLabel,
                     40, 2, std::max(1, width - 49), height - 4,
                     juce::Justification::centredLeft, 1);
}

void InspectorComponent::ModeCandidateListModel::selectedRowsChanged(int)
{
    const int row = owner.modeCandidatesList.getSelectedRow();
    if (row >= 0 && row < (int)owner.modeCandidateRows.size()
        && owner.modeCandidateRows[(size_t)row].candidateIndex < 0)
    {
        if (row + 1 < (int)owner.modeCandidateRows.size())
            owner.modeCandidatesList.selectRow(row + 1);
        return;
    }
    owner.btnApplyCandidate.setEnabled(owner.candidateIndexForRow(row) >= 0);
    owner.btnApplyCandidateToRange.setEnabled(owner.candidateIndexForRow(row) >= 0
        && owner.midiModel.getSelectedNotes().size() >= 2);
}

void InspectorComponent::ModeCandidateListModel::listBoxItemDoubleClicked(
    int row, const juce::MouseEvent&)
{
    owner.applyModeCandidate(owner.candidateIndexForRow(row));
}

int InspectorComponent::candidateIndexForRow(int row) const
{
    return row >= 0 && row < (int)modeCandidateRows.size()
        ? modeCandidateRows[(size_t)row].candidateIndex : -1;
}

void InspectorComponent::rebuildModeCandidateRows(int candidateToSelect)
{
    struct Group
    {
        juce::String key;
        juce::String label;
        std::vector<int> candidateIndices;
    };
    std::vector<Group> groups;
    for (int i = 0; i < (int)cachedCandidates.size(); ++i)
    {
        const auto& candidate = cachedCandidates[(size_t)i];
        const auto key = modeGrouping == ModeGrouping::Tonic
            ? juce::String(candidate.tonicPc) : candidate.scaleName;
        auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& item) {
            return item.key == key;
        });
        if (group == groups.end())
        {
            groups.push_back({ key,
                modeGrouping == ModeGrouping::Tonic ? candidate.tonic : candidate.scaleName,
                {} });
            group = std::prev(groups.end());
        }
        group->candidateIndices.push_back(i);
    }

    if (modeGrouping == ModeGrouping::ScaleType)
        std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
            return modeTypePriority(a.key) < modeTypePriority(b.key);
        });
    else
        for (auto& group : groups)
            std::stable_sort(group.candidateIndices.begin(), group.candidateIndices.end(),
                [this](int a, int b) {
                    return modeTypePriority(cachedCandidates[(size_t)a].scaleName)
                        < modeTypePriority(cachedCandidates[(size_t)b].scaleName);
                });

    const bool chinese = Localization::getInstance().getLanguage() == Language::SimplifiedChinese;
    modeCandidateRows.clear();
    modeCandidateRows.reserve(cachedCandidates.size() + groups.size());
    for (const auto& group : groups)
    {
        auto label = modeGrouping == ModeGrouping::Tonic
            ? group.label : shortModeName(group.label, chinese);
        const int groupCandidateIndex = group.candidateIndices.front();
        modeCandidateRows.push_back({ -1, label, groupCandidateIndex,
                                      (int)group.candidateIndices.size() });
        for (int index : group.candidateIndices)
            modeCandidateRows.push_back({ index, {}, groupCandidateIndex, 0 });
    }

    modeCandidatesList.updateContent();
    int rowToSelect = -1;
    for (int row = 0; row < (int)modeCandidateRows.size(); ++row)
        if (modeCandidateRows[(size_t)row].candidateIndex == candidateToSelect)
        {
            rowToSelect = row;
            break;
        }
    if (rowToSelect < 0)
        for (int row = 0; row < (int)modeCandidateRows.size(); ++row)
            if (modeCandidateRows[(size_t)row].candidateIndex >= 0)
            {
                rowToSelect = row;
                break;
            }
    if (rowToSelect >= 0)
        modeCandidatesList.selectRow(rowToSelect);
    else
        modeCandidatesList.deselectAllRows();
    modeCandidatesList.repaint();
}

void InspectorComponent::showModeCandidates(const AnalysisResult& result, bool fromSelection)
{
    showingSelectionResults = fromSelection;
    const bool chinese = Localization::getInstance().getLanguage() == Language::SimplifiedChinese;
    modeCandidatesList.setTooltip(fromSelection
        ? (chinese ? juce::String::fromUTF8(u8"所选音符的音高集合匹配；优先显示包含全部所选音高的调式。")
                   : juce::String("Pitch-set matching; modes containing every selected pitch are shown first."))
        : (chinese ? juce::String::fromUTF8(u8"当前乐器轨全部 MIDI 片段的统计排序；允许经过音和调外音。")
                   : juce::String("Statistical ranking across every MIDI clip on this track; passing and outside notes are allowed.")));
    cachedCandidates.clear();
    for (const auto& candidate : result.topCandidates)
        cachedCandidates.push_back({ candidate.tonic, candidate.tonicPc,
                                     candidate.scaleName });

    if (cachedCandidates.empty())
    {
        const auto selection = midiModel.getSelectedNotes();
        const juce::String hint = !fromSelection
            ? (chinese ? juce::String::fromUTF8(u8"当前轨道没有可分析的音符")
                       : juce::String("No notes on this track"))
            : (chinese ? juce::String::fromUTF8(selection.size() > 1
                  ? u8"请选择至少两个不同音高" : u8"多选音符后显示调式可能性")
                       : juce::String(selection.size() > 1
                  ? "Select two different pitches" : "Select multiple notes to explore modes"));
        lblModeAnalysisConfidence.setText(hint, juce::dontSendNotification);
    }
    else if (fromSelection)
    {
        juce::String summary = chinese
            ? juce::String::fromUTF8(u8"所选 ") + juce::String(result.distinctPitchClassCount)
                + juce::String::fromUTF8(u8" 种音高 · ")
                + juce::String(result.exactMatchCount)
                + juce::String::fromUTF8(u8" 个完整匹配")
            : juce::String(result.distinctPitchClassCount)
                + juce::String::fromUTF8(u8" pitches · ")
                + juce::String(result.exactMatchCount) + " exact matches";
        if (result.exactMatchCount == 0)
            summary = chinese ? juce::String::fromUTF8(u8"没有完整匹配，以下为最接近的调式")
                              : juce::String("No exact match; closest modes shown");
        lblModeAnalysisConfidence.setText(summary, juce::dontSendNotification);
    }
    else
        lblModeAnalysisConfidence.setText(chinese ? juce::String::fromUTF8(u8"整轨分析 · 按时值与节拍排序")
                                                  : juce::String::fromUTF8(u8"Track analysis · duration and meter"),
                                          juce::dontSendNotification);

    rebuildModeCandidateRows();
    lblModeAnalysisConfidence.setVisible(true);
    btnGroupByTonic.setVisible(!cachedCandidates.empty());
    btnGroupByType.setVisible(!cachedCandidates.empty());
    modeCandidatesList.setVisible(!cachedCandidates.empty());
    btnApplyCandidate.setVisible(!cachedCandidates.empty());
    btnApplyCandidate.setEnabled(!cachedCandidates.empty());
    btnApplyCandidateToRange.setVisible(!cachedCandidates.empty());
    btnApplyCandidateToRange.setEnabled(!cachedCandidates.empty()
        && midiModel.getSelectedNotes().size() >= 2);
    resized();
    modeCandidatesList.repaint();
}

void InspectorComponent::updateModePossibilities()
{
    const auto selected = midiModel.getSelectedNotes();
    AnalysisResult result;
    if (selected.size() >= 2)
        result = ModeAnalyzer::analyzeSelection(
            selected, 12 * (int)ScaleDefinitions::getAllScales().size());
    showModeCandidates(result, true);
}

void InspectorComponent::runModeRecommendation()
{
    std::vector<NoteEvent> notes;
    if (auto* track = trackModel.getTrackById(trackModel.getActiveTrackId()))
    {
        if (track->getType() == TrackType::Instrument)
        {
            for (const auto& clip : track->getClips())
                for (const auto& note : clip.notes)
                {
                    NoteEvent shifted = note;
                    shifted.startBeat += clip.startBeat;
                    notes.push_back(shifted);
                }
        }
    }
    if (notes.empty())
        notes = midiModel.getNotes();

    AnalysisResult result;
    if (!notes.empty())
    {
        const double barLength = (double)midiModel.getTimeSigNumerator()
            * (4.0 / (double)midiModel.getTimeSigDenominator());
        result = ModeAnalyzer::analyze(notes, 24, barLength);
    }
    showModeCandidates(result, false);
}

void InspectorComponent::applyCurrentMode()
{
    const int tonicPc = keyRootCombo.getSelectedId() - 1;
    const int scaleIndex = scaleTypeCombo.getSelectedId() - 1;
    const auto& scales = ScaleDefinitions::getAllScales();
    if (tonicPc < 0 || tonicPc >= 12 || scaleIndex < 0
        || scaleIndex >= (int)scales.size()) return;
    applyModeChoice(tonicPc, scales[(size_t)scaleIndex].name, false);
}

void InspectorComponent::applyModeCandidate(int index)
{
    if (index < 0 || index >= (int)cachedCandidates.size()) return;
    const auto candidate = cachedCandidates[(size_t)index];
    applyModeChoice(candidate.tonicPc, candidate.scaleName, true);
}

void InspectorComponent::applyModeChoice(int tonicPc, const juce::String& scaleName,
                                         bool includeCandidateFallback)
{
    if (tonicPc < 0 || tonicPc >= 12 || scaleName.isEmpty()) return;
    const auto tonicName = ScaleDefinitions::getPreferredTonicName(tonicPc, scaleName);
    const auto beforeKey = midiModel.getKeySignature();
    const auto beforeScale = midiModel.getScaleName();
    auto afterKey = beforeKey;
    afterKey.rootPitchClass = tonicPc;

    int modeTrackId = -1;
    std::vector<ModeEvent> beforeModes, afterModes;
    bool modeChanged = false;

    if (auto* modeTrack = trackModel.getModeTrack())
    {
        const auto& existingModes = modeTrack->getModes();
        int targetIndex = -1;
        for (int i = 0; i < (int)existingModes.size(); ++i)
            if (existingModes[(size_t)i].isSelected) { targetIndex = i; break; }
        if (targetIndex < 0)
        {
            const double beat = audioEngine.getPlayheadBeat();
            for (int i = 0; i < (int)existingModes.size(); ++i)
            {
                const auto& mode = existingModes[(size_t)i];
                if (beat >= mode.startBeat && beat < mode.startBeat + mode.lengthBeats)
                { targetIndex = i; break; }
            }
        }
        if (targetIndex < 0 && includeCandidateFallback && !existingModes.empty())
            targetIndex = 0;
        if (targetIndex >= 0 || includeCandidateFallback)
        {
            modeTrackId = modeTrack->getId();
            beforeModes = existingModes;
            afterModes = beforeModes;
            if (targetIndex < 0)
            {
                ModeEvent mode;
                mode.id = juce::Time::currentTimeMillis();
                while (std::any_of(afterModes.begin(), afterModes.end(), [&mode](const ModeEvent& existing) {
                    return existing.id == mode.id;
                }))
                    mode.id = mode.id == std::numeric_limits<int64_t>::max() ? 1 : mode.id + 1;
                mode.startBeat = 0.0;
                mode.lengthBeats = 64.0;
                mode.color = CubaseTheme::accentCyan();
                afterModes.push_back(mode);
                targetIndex = (int)afterModes.size() - 1;
                modeChanged = true;
            }
            auto& target = afterModes[(size_t)targetIndex];
            const auto targetName = tonicName + " " + scaleName;
            modeChanged = modeChanged || target.tonic != tonicName
                || target.tonicPc != tonicPc
                || target.scaleName != scaleName || target.name != targetName;
            target.tonic = tonicName;
            target.tonicPc = tonicPc;
            target.scaleName = scaleName;
            target.name = targetName;
        }
    }
    if (beforeKey.rootPitchClass == afterKey.rootPitchClass
        && beforeScale == scaleName && !modeChanged)
        return;

    UndoActionHelper::performAtomic(&undoManager,
        new ApplySelectedModeUndoAction(midiModel, trackModel,
            beforeKey, beforeScale, afterKey, scaleName,
            modeTrackId, std::move(beforeModes), std::move(afterModes)),
        tr("Apply Selected Mode"));
}

void InspectorComponent::applyModeCandidateToSelectedRange(int index)
{
    if (index < 0 || index >= (int)cachedCandidates.size()) return;
    const auto selected = midiModel.getSelectedNotes();
    if (selected.size() < 2) return;

    double startBeat = std::numeric_limits<double>::infinity();
    double endBeat = 0.0;
    for (const auto& note : selected)
    {
        if (!std::isfinite(note.startBeat) || !std::isfinite(note.lengthBeats)
            || note.startBeat < 0.0 || note.lengthBeats <= 0.0)
            return;
        startBeat = std::min(startBeat, note.startBeat);
        endBeat = std::max(endBeat, note.startBeat + note.lengthBeats);
    }
    if (!std::isfinite(endBeat) || endBeat <= startBeat) return;

    const auto& candidate = cachedCandidates[(size_t)index];
    ModeEvent requested;
    requested.id = juce::Time::currentTimeMillis();
    requested.tonic = candidate.tonic;
    requested.tonicPc = candidate.tonicPc;
    requested.scaleName = candidate.scaleName;
    requested.name = candidate.tonic + " " + candidate.scaleName;
    requested.color = CubaseTheme::accentCyan();

    auto* modeTrack = trackModel.getModeTrack();
    const bool needsTrack = modeTrack == nullptr;
    const auto before = needsTrack ? std::vector<ModeEvent>{} : modeTrack->getModes();
    auto after = before;
    if (!TrackModel::applyModeToRange(after, startBeat, endBeat, requested)) return;

    const auto actionName = Localization::getInstance().getLanguage() == Language::SimplifiedChinese
        ? juce::String::fromUTF8(u8"应用所选范围到调式轨")
        : juce::String("Apply Selection Range to Mode Track");
    if (needsTrack)
    {
        undoManager.beginNewTransaction(actionName);
        if (!undoManager.perform(new AddTrackUndoAction(trackModel, "Mode Track", TrackType::Mode),
                                 actionName))
        {
            undoManager.beginNewTransaction();
            return;
        }
        modeTrack = trackModel.getModeTrack();
        if (modeTrack == nullptr)
        {
            undoManager.undoCurrentTransactionOnly();
            undoManager.beginNewTransaction();
            return;
        }
    }
    const bool applied = undoManager.perform(new ModifyModesUndoAction(trackModel,
        modeTrack->getId(), before, after, actionName), actionName);
    if (!applied && needsTrack) undoManager.undoCurrentTransactionOnly();
    undoManager.beginNewTransaction();
}

void InspectorComponent::analyzeAndPopulateModeTrack()
{
    // Find active instrument track or first instrument track with notes
    std::vector<NoteEvent> notes;
    for (auto& t : trackModel.getTracks())
    {
        if (t->getType() == TrackType::Instrument && !t->getClips().empty())
        {
            for (const auto& clip : t->getClips())
            {
                for (const auto& n : clip.notes)
                {
                    NoteEvent shifted = n;
                    shifted.startBeat += clip.startBeat;
                    notes.push_back(shifted);
                }
            }
            if (!notes.empty()) break;
        }
    }

    if (notes.empty())
        notes = midiModel.getNotes();

    if (notes.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            tr("Analyze & Populate Modes"),
            tr("No MIDI notes found to analyze modes from."));
        return;
    }

    auto* modeTrack = trackModel.getModeTrack();
    if (!modeTrack)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            tr("Analyze & Populate Modes"),
            tr("No Mode Track found in project."));
        return;
    }

    // Run analysis with dynamic meter
    double barLen = (double)midiModel.getTimeSigNumerator() * (4.0 / (double)midiModel.getTimeSigDenominator());
    auto result = ModeAnalyzer::analyze(notes, 5, barLen);

    double maxBeat = 32.0;
    for (const auto& n : notes)
        maxBeat = std::max(maxBeat, n.startBeat + n.lengthBeats);

    std::vector<ModeEvent> newModes;
    ModeEvent me;
    me.id = juce::Time::currentTimeMillis();
    me.startBeat = 0.0;
    me.lengthBeats = std::max(16.0, std::ceil(maxBeat / 4.0) * 4.0);
    me.name = result.bestTonic + " " + result.bestScale;
    me.tonic = result.bestTonic;
    me.tonicPc = result.bestTonicPc;
    me.scaleName = result.bestScale;
    me.color = CubaseTheme::accentCyan();
    newModes.push_back(me);

    modeTrack->getModes() = newModes;
    trackModel.triggerModesChanged();

    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        tr("Analyze & Populate Modes"),
        "Mode track populated with: " + me.name);
}

void InspectorComponent::updateNoteInfo()
{
    auto selected = midiModel.getSelectedNotes();
    if (selected.empty())
    {
        lblSelectionCount.setText(tr("No Notes Selected"), juce::dontSendNotification);
        lblNotePitch.setText(juce::String::fromUTF8(u8"音符音高: --"), juce::dontSendNotification);
        lblNoteStart.setText(juce::String::fromUTF8(u8"起始位置: --"), juce::dontSendNotification);
        lblNoteLength.setText(juce::String::fromUTF8(u8"音符长度: --"), juce::dontSendNotification);
        lblNoteVelocity.setText(juce::String::fromUTF8(u8"按键力度: --"), juce::dontSendNotification);
        lblNoteScaleDegree.setText(juce::String::fromUTF8(u8"调式音级: --"), juce::dontSendNotification);
        lblNoteFunctionalRole.setText(juce::String::fromUTF8(u8"调式功能: --"), juce::dontSendNotification);
        lblNoteMotionTendency.setText(juce::String::fromUTF8(u8"运动倾向: --"), juce::dontSendNotification);
        lblNoteColorUsage.setText(juce::String::fromUTF8(u8"色彩定位: --"), juce::dontSendNotification);
        lblNoteInterval.setText(juce::String::fromUTF8(u8"音程关系: --"), juce::dontSendNotification);

    }
    else if (selected.size() == 1)
    {
        const auto& n = selected[0];
        lblSelectionCount.setText(tr("1 Note Selected"), juce::dontSendNotification);

        juce::String pitchStr = ScaleDefinitions::getPitchName(n.pitch) + juce::String((n.pitch / 12) - 1)
                                + " (" + juce::String(n.pitch) + ")";
        lblNotePitch.setText(juce::String::fromUTF8(u8"音符音高: ") + pitchStr, juce::dontSendNotification);
        lblNoteStart.setText(juce::String::fromUTF8(u8"起始位置: ") + juce::String(n.startBeat, 2)
                                 + juce::String::fromUTF8(u8" 拍"), juce::dontSendNotification);
        lblNoteLength.setText(juce::String::fromUTF8(u8"音符长度: ") + juce::String(n.lengthBeats, 2)
                                  + juce::String::fromUTF8(u8" 拍"), juce::dontSendNotification);
        lblNoteVelocity.setText(juce::String::fromUTF8(u8"按键力度: ") + juce::String((int)(n.velocity * 127)), juce::dontSendNotification);

        // Music theory: Context scale degree and multi-dimensional modal function
        auto layout = midiModel.getEffectiveLayoutAtBeat(n.startBeat, &trackModel);
        int tonicPc = layout.tonicPc;
        int pc = ((n.pitch % 12) + 12) % 12;
        int rel = (pc - tonicPc + 12) % 12;

        auto modalInfo = ModalFunctionEngine::lookup(layout.scaleName, rel);

        lblNoteScaleDegree.setText(juce::String::fromUTF8(u8"调式音级: ") + modalInfo.degree, juce::dontSendNotification);
        lblNoteFunctionalRole.setText(juce::String::fromUTF8(u8"调式功能: ") + modalInfo.functionalRole, juce::dontSendNotification);
        lblNoteMotionTendency.setText(juce::String::fromUTF8(u8"运动倾向: ") + modalInfo.motionTendency, juce::dontSendNotification);
        lblNoteColorUsage.setText(juce::String::fromUTF8(u8"色彩定位: ") + modalInfo.colorUsage, juce::dontSendNotification);
        lblNoteInterval.setText(juce::String::fromUTF8(u8"音程关系: 单音 (无音程)"), juce::dontSendNotification);

    }
    else if (selected.size() == 2)
    {
        lblSelectionCount.setText(juce::String((int)selected.size()) + " " + tr("Notes Selected"), juce::dontSendNotification);
        int p1 = selected[0].pitch;
        int p2 = selected[1].pitch;
        juce::String p1Str = ScaleDefinitions::getPitchName(p1) + juce::String((p1 / 12) - 1);
        juce::String p2Str = ScaleDefinitions::getPitchName(p2) + juce::String((p2 / 12) - 1);
        lblNotePitch.setText(juce::String::fromUTF8(u8"音符音高: ") + p1Str + " / " + p2Str, juce::dontSendNotification);
        lblNoteStart.setText(juce::String::fromUTF8(u8"起始位置: (多值)"), juce::dontSendNotification);
        lblNoteLength.setText(juce::String::fromUTF8(u8"音符长度: (多值)"), juce::dontSendNotification);
        lblNoteVelocity.setText(juce::String::fromUTF8(u8"按键力度: (多值)"), juce::dontSendNotification);

        // Music theory: calculate interval between the 2 selected notes
        int diff = std::abs(p2 - p1);

        static const char* diatonicIntervals[13] = {
            (const char*)u8"纯一度", (const char*)u8"小二度", (const char*)u8"大二度", (const char*)u8"小三度",
            (const char*)u8"大三度", (const char*)u8"纯四度", (const char*)u8"增四/减五度", (const char*)u8"纯五度",
            (const char*)u8"小六度", (const char*)u8"大六度", (const char*)u8"小七度", (const char*)u8"大七度",
            (const char*)u8"纯八度"
        };
        static const char* compoundIntervals[12] = {
            (const char*)u8"纯八度", (const char*)u8"小九度", (const char*)u8"大九度", (const char*)u8"小十度", (const char*)u8"大十度", (const char*)u8"纯十一度",
            (const char*)u8"增十一度", (const char*)u8"纯十二度", (const char*)u8"小十三度", (const char*)u8"大十三度", (const char*)u8"小十四度", (const char*)u8"大十四度"
        };
        juce::String intName;
        if (diff <= 12)
        {
            intName = juce::String::fromUTF8(diatonicIntervals[diff]);
        }
        else
        {
            int oct = diff / 12;
            int rem = diff % 12;
            if (oct == 1)
                intName = juce::String::fromUTF8(compoundIntervals[rem]);
            else
                intName = juce::String(oct) + juce::String::fromUTF8(u8"八度 + ") + juce::String::fromUTF8(diatonicIntervals[rem]);
        }
        juce::String intFull = intName + " (" + juce::String(diff) + juce::String::fromUTF8(u8"个半音)");
        lblNoteInterval.setText(juce::String::fromUTF8(u8"两音音程: ") + intFull, juce::dontSendNotification);

        auto layout = midiModel.getEffectiveLayoutAtBeat(selected[0].startBeat, &trackModel);
        int tonicPc = layout.tonicPc;
        int rel1 = (((p1 % 12) + 12) % 12 - tonicPc + 12) % 12;
        int rel2 = (((p2 % 12) + 12) % 12 - tonicPc + 12) % 12;
        auto m1 = ModalFunctionEngine::lookup(layout.scaleName, rel1);
        auto m2 = ModalFunctionEngine::lookup(layout.scaleName, rel2);

        lblNoteScaleDegree.setText(juce::String::fromUTF8(u8"两音级数: ") + m1.degree + " / " + m2.degree, juce::dontSendNotification);
        lblNoteFunctionalRole.setText(juce::String::fromUTF8(u8"调式功能: ") + m1.functionalRole + " / " + m2.functionalRole, juce::dontSendNotification);
        lblNoteMotionTendency.setText(juce::String::fromUTF8(u8"运动倾向: ") + m1.motionTendency + " / " + m2.motionTendency, juce::dontSendNotification);
        lblNoteColorUsage.setText(juce::String::fromUTF8(u8"色彩定位: ") + m1.colorUsage + " / " + m2.colorUsage, juce::dontSendNotification);

    }
    else
    {
        lblSelectionCount.setText(juce::String((int)selected.size()) + " " + tr("Notes Selected"), juce::dontSendNotification);
        lblNotePitch.setText(juce::String::fromUTF8(u8"音符音高: (多值)"), juce::dontSendNotification);
        lblNoteStart.setText(juce::String::fromUTF8(u8"起始位置: (多值)"), juce::dontSendNotification);
        lblNoteLength.setText(juce::String::fromUTF8(u8"音符长度: (多值)"), juce::dontSendNotification);
        lblNoteVelocity.setText(juce::String::fromUTF8(u8"按键力度: (多值)"), juce::dontSendNotification);
        lblNoteScaleDegree.setText(juce::String::fromUTF8(u8"调式音级: (多音复选)"), juce::dontSendNotification);
        lblNoteFunctionalRole.setText(juce::String::fromUTF8(u8"调式功能: (多音复选)"), juce::dontSendNotification);
        lblNoteMotionTendency.setText(juce::String::fromUTF8(u8"运动倾向: (多音复选)"), juce::dontSendNotification);
        lblNoteColorUsage.setText(juce::String::fromUTF8(u8"色彩定位: (多音复选)"), juce::dontSendNotification);
        lblNoteInterval.setText(juce::String::fromUTF8(u8"音程关系: (>2音见和弦分析)"), juce::dontSendNotification);

    }

    updateChordAnalysis(selected);
    alignNotePropertyRows();
}

void InspectorComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setColour(CubaseTheme::sidebarBackground());
    g.fillRect(bounds);

    g.setColour(CubaseTheme::borderSubtle());
    if (dockFilter == DockSide::Right || dockOnRight)
        g.drawVerticalLine(0, 0.0f, (float)bounds.getHeight());
    else
        g.drawVerticalLine(bounds.getRight() - 1, 0.0f, (float)bounds.getHeight());

}

void InspectorComponent::resized()
{
    viewport.setBounds(getLocalBounds());
    if (dropOverlay) dropOverlay->setBounds(getLocalBounds());
    int contentW = viewport.getMaximumVisibleWidth();
    if (contentW <= 0) contentW = getWidth();

    int activeTrkId = trackModel.getActiveTrackId();
    auto* activeTrack = trackModel.getTrackById(activeTrkId);
    if (activeTrack != nullptr && activeTrack->getType() == TrackType::Audio)
    {
        int missing = 0;
        for (const auto& clip : activeTrack->getAudioClips())
            if (!juce::File(clip.sourcePath).existsAsFile()) ++missing;
        const bool chinese = Localization::getInstance().getLanguage() == Language::SimplifiedChinese;
        juce::String summary = chinese ? juce::String::fromUTF8(u8"音频片段：") : juce::String("Audio clips: ");
        summary << (int)activeTrack->getAudioClips().size();
        if (missing > 0)
            summary << (chinese ? juce::String::fromUTF8(u8"  ·  丢失文件：") : juce::String("  ·  Missing: ")) << missing;
        lblAudioStatus.setText(summary, juce::dontSendNotification);
        btnAudioMute.setToggleState(activeTrack->isMuted(), juce::dontSendNotification);
        btnAudioSolo.setToggleState(activeTrack->isSolo(), juce::dontSendNotification);
        btnImportAudio.setEnabled(!activeTrack->isLocked());
    }
    TrackType trkType = activeTrack != nullptr ? activeTrack->getType() : TrackType::Instrument;
    if (currentDockContext == DockContext::KeyEditor)
        trkType = TrackType::Instrument;
    grpReharmonization.setVisible(false);
    grpAudioTrack.setVisible(false);
    grpChordShape.setVisible(false);
    grpModeBorrowing.setVisible(false);
    grpQuickChord.setVisible(false);

    constexpr int headerBodyTop = 34;
    constexpr int bodyBottom = 10;
    constexpr int moduleGap = 10;
    constexpr int reharmonizationPreviewHeight = 64;
    const auto& appState = AppStateManager::getInstance();
    const bool compactControls = contentW < 230;
    const float noteFontPt = (float)std::clamp(appState.notePropertyFontSizePt, 9, 24);
    const bool compactNotes = contentW < std::max(220, juce::roundToInt(noteFontPt * 18.0f));
    const int noteLineH = std::max(18, (int)std::round(
        noteFontPt * 1.55f));
    const int notePropertyH = compactNotes ? noteLineH * 2 : noteLineH;

    auto bodyArea = [=](const juce::GroupComponent& group) {
        return group.getLocalBounds().withTrimmedTop(headerBodyTop)
            .withTrimmedBottom(bodyBottom).withTrimmedLeft(10).withTrimmedRight(10);
    };
    auto sectionHeight = [&](int sectionId) {
        switch (sectionId)
        {
            case 0:
            {
                int properties = (int)appState.noteShowPitch + (int)appState.noteShowStart
                    + (int)appState.noteShowLength + (int)appState.noteShowVelocity
                    + (int)appState.noteShowScaleDegree + (int)appState.noteShowFunctionalRole
                    + (int)appState.noteShowMotionTendency + (int)appState.noteShowColorUsage
                    + (int)appState.noteShowInterval;
                return headerBodyTop + noteLineH + properties * notePropertyH + bodyBottom;
            }
            case 1: return (compactControls ? 192 : 164)
                + (cachedCandidates.empty() ? 0 : 288);
            case 2:
            {
                const float pt = (float)std::clamp(appState.chordAnalysisFontSizePt, 10, 48);
                const int chordH = juce::roundToInt(pt * 1.35f);
                const int romanH = juce::roundToInt(pt * 0.72f * 1.35f);
                if (appState.chordAnalysisOrientation == 1
                    && !(compactControls && appState.chordAnalysisDisplayMode == 0))
                    return 56 + std::max(chordH, romanH);
                if (appState.chordAnalysisDisplayMode == 0)
                    return 52 + chordH + romanH;
                return 52 + (appState.chordAnalysisDisplayMode == 1 ? chordH : romanH);
            }
            case 3: return 160;
            case 4:
            {
                const int optionCount = appState.secondaryDominantTargetSource == 3 ? 4 : 3;
                return 118 + (secondarySettingsExpanded
                    ? optionCount * (compactControls ? 48 : 28) : 0);
            }
            case 5: return compactControls ? 182 : 164;
            case 6: return 160;
            case 7: return compactControls ? 378 : 344;
            case 8: return 104;
            case 9: return 224;
            case 10:
            {
                const int columns = contentW >= 270 ? 3 : 2;
                const int rows = (kReharmonizationAlgorithmCount + columns - 1) / columns;
                return headerBodyTop + bodyBottom + rows * 30 + (rows - 1) * 6
                    + 6 + reharmonizationPreviewHeight + 2;
            }
            case 11: return 252;
            case 12:
            {
                const int columns = contentW >= 280 ? 5 : contentW >= 210 ? 4 : 3;
                const int rows = (10 + columns - 1) / columns;
                return headerBodyTop + bodyBottom + 18 + 4
                    + rows * 26 + (rows - 1) * 4 + 8 + 26 + 8 + 26
                    + (appState.showChordStackDirections ? 6 + 26 + 4 + 26 : 0) + 2;
            }
            case 13: return (appState.modeBorrowingStyle == 0 ? 364 : 332)
                + (borrowingSourceSettingsExpanded
                    ? (appState.modeBorrowingCustomSource ? 71 : 36) : 0);
            case kQuickChordSectionId: return quickChordModuleHeight(contentW);
            default: return 0;
        }
    };

    int totalH = 12;
    int specialSection = trkType == TrackType::Chord ? 5
        : trkType == TrackType::Mode ? 6
        : trkType == TrackType::Marker ? 7
        : trkType == TrackType::TimeSignature ? 8
        : trkType == TrackType::Folder ? 9
        : trkType == TrackType::Audio ? 11 : -1;
    if (specialSection >= 0)
    {
        if (isSectionVisibleInThisDock(specialSection))
            totalH += sectionHeight(specialSection) + moduleGap;
    }
    else
    {
        for (int id : sectionOrder)
            if (isNoteTrackSectionId(id) && isSectionVisibleInThisDock(id))
                totalH += sectionHeight(id) + moduleGap;
    }
    const int finalH = std::max(totalH, viewport.getHeight());
    contentComp.setBounds(0, 0, contentW, finalH);

    const bool emptyDock = !hasVisibleSections();
    lblEmptyDock.setVisible(emptyDock);
    btnEmptyDockManage.setVisible(emptyDock);
    lblEmptyDock.setBounds(14, 80, std::max(0, contentW - 28), 44);
    btnEmptyDockManage.setBounds(22, 130, std::max(0, contentW - 44), 32);

    auto area = juce::Rectangle<int>(4, 6, contentW - 8, finalH - 12);

    if (trkType == TrackType::Audio)
    {
        grpNoteInfo.setVisible(false);
        grpScaleMode.setVisible(false);
        grpChordDetection.setVisible(false);
        grpInstrument.setVisible(false);
        grpReharm.setVisible(false);
        grpChordTrack.setVisible(false);
        grpModeTrack.setVisible(false);
        grpMarkerTrack.setVisible(false);
        grpTimeSigTrack.setVisible(false);
        grpFolderTrack.setVisible(false);
        const bool showAudio = isSectionVisibleInThisDock(kAudioTrackSectionId);
        grpAudioTrack.setVisible(showAudio);
        if (showAudio)
        {
            grpAudioTrack.setBounds(area.removeFromTop(sectionHeight(kAudioTrackSectionId)));
            auto audioArea = bodyArea(grpAudioTrack);
            lblAudioStatus.setBounds(audioArea.removeFromTop(24));
            audioArea.removeFromTop(5);
            btnImportAudio.setBounds(audioArea.removeFromTop(30));
            audioArea.removeFromTop(8);
            lblAudioVolume.setBounds(audioArea.removeFromTop(18));
            audioVolumeSlider.setBounds(audioArea.removeFromTop(28));
            audioArea.removeFromTop(5);
            lblAudioPan.setBounds(audioArea.removeFromTop(18));
            audioPanSlider.setBounds(audioArea.removeFromTop(28));
            audioArea.removeFromTop(8);
            auto buttons = audioArea.removeFromTop(28);
            btnAudioMute.setBounds(buttons.removeFromLeft(std::max(0, (buttons.getWidth() - 6) / 2)));
            buttons.removeFromLeft(6);
            btnAudioSolo.setBounds(buttons);
        }
        return;
    }

    if (trkType == TrackType::Chord)
    {
        grpNoteInfo.setVisible(false);
        grpScaleMode.setVisible(false);
        grpChordDetection.setVisible(false);
        grpInstrument.setVisible(false);
        grpReharm.setVisible(false);
        grpModeTrack.setVisible(false);
        grpMarkerTrack.setVisible(false);
        grpTimeSigTrack.setVisible(false);
        grpFolderTrack.setVisible(false);

        bool showChord = isSectionVisibleInThisDock(5);
        grpChordTrack.setVisible(showChord);
        if (showChord)
        {
            grpChordTrack.setBounds(area.removeFromTop(sectionHeight(5)));

            auto chordArea = bodyArea(grpChordTrack);
            btnExtractChordsFromMidi.setBounds(chordArea.removeFromTop(28));
            chordArea.removeFromTop(6);
            if (compactControls)
            {
                lblChordDisplayMode.setBounds(chordArea.removeFromTop(20));
                chordArea.removeFromTop(2);
                chordDisplayModeCombo.setBounds(chordArea.removeFromTop(24));
            }
            else
            {
                auto modeRow = chordArea.removeFromTop(24);
                lblChordDisplayMode.setBounds(modeRow.removeFromLeft(48));
                chordDisplayModeCombo.setBounds(modeRow);
            }
            chordArea.removeFromTop(6);
            lblChordTrackInfo.setBounds(chordArea.removeFromTop(44));
        }
        return;
    }
    else if (trkType == TrackType::Mode)
    {
        grpNoteInfo.setVisible(false);
        grpScaleMode.setVisible(false);
        grpChordDetection.setVisible(false);
        grpInstrument.setVisible(false);
        grpReharm.setVisible(false);
        grpChordTrack.setVisible(false);
        grpMarkerTrack.setVisible(false);
        grpTimeSigTrack.setVisible(false);
        grpFolderTrack.setVisible(false);

        bool showMode = isSectionVisibleInThisDock(6);
        grpModeTrack.setVisible(showMode);
        if (showMode)
        {
            grpModeTrack.setBounds(area.removeFromTop(sectionHeight(6)));

            auto modeArea = bodyArea(grpModeTrack);
            btnAnalyzeAndPopulateModes.setBounds(modeArea.removeFromTop(28));
            modeArea.removeFromTop(6);
            btnModeTrackOverride.setBounds(modeArea.removeFromTop(24));
            modeArea.removeFromTop(4);
            lblModeTrackInfo.setBounds(modeArea.removeFromTop(44));
        }
        return;
    }
    else if (trkType == TrackType::Marker)
    {
        grpNoteInfo.setVisible(false);
        grpScaleMode.setVisible(false);
        grpChordDetection.setVisible(false);
        grpInstrument.setVisible(false);
        grpReharm.setVisible(false);
        grpChordTrack.setVisible(false);
        grpModeTrack.setVisible(false);
        grpTimeSigTrack.setVisible(false);
        grpFolderTrack.setVisible(false);

        bool showMarker = isSectionVisibleInThisDock(7);
        grpMarkerTrack.setVisible(showMarker);
        if (showMarker)
        {
            grpMarkerTrack.setBounds(area.removeFromTop(sectionHeight(7)));

            auto markerArea = bodyArea(grpMarkerTrack);
            lblMarkerTrackInfo.setBounds(markerArea.removeFromTop(20));
            markerArea.removeFromTop(4);

            markerListBox.setBounds(markerArea.removeFromTop(90));
            markerArea.removeFromTop(8);

            auto nameRow = markerArea.removeFromTop(24);
            lblMarkerNameTitle.setBounds(nameRow.removeFromLeft(45));
            nameRow.removeFromLeft(4);
            txtMarkerName.setBounds(nameRow);
            markerArea.removeFromTop(4);

            if (compactControls)
            {
                auto startRow = markerArea.removeFromTop(24);
                lblMarkerStartTitle.setBounds(startRow.removeFromLeft(44));
                startRow.removeFromLeft(4);
                txtMarkerStart.setBounds(startRow);
                markerArea.removeFromTop(4);
                auto lengthRow = markerArea.removeFromTop(24);
                lblMarkerLengthTitle.setBounds(lengthRow.removeFromLeft(44));
                lengthRow.removeFromLeft(4);
                txtMarkerLength.setBounds(lengthRow);
            }
            else
            {
                auto timeRow = markerArea.removeFromTop(24);
                lblMarkerStartTitle.setBounds(timeRow.removeFromLeft(38));
                timeRow.removeFromLeft(2);
                txtMarkerStart.setBounds(timeRow.removeFromLeft(55));
                timeRow.removeFromLeft(6);
                lblMarkerLengthTitle.setBounds(timeRow.removeFromLeft(35));
                timeRow.removeFromLeft(2);
                txtMarkerLength.setBounds(timeRow);
            }
            markerArea.removeFromTop(4);

            auto colRow = markerArea.removeFromTop(24);
            lblMarkerColorTitle.setBounds(colRow.removeFromLeft(45));
            colRow.removeFromLeft(4);
            markerColorCombo.setBounds(colRow);
            markerArea.removeFromTop(6);

            btnJumpToMarker.setBounds(markerArea.removeFromTop(24));
            markerArea.removeFromTop(4);
            btnAddMarkerAtPlayhead.setBounds(markerArea.removeFromTop(24));
            markerArea.removeFromTop(4);
            btnDeleteMarker.setBounds(markerArea.removeFromTop(24));
        }
        return;
    }
    else if (trkType == TrackType::TimeSignature)
    {
        grpNoteInfo.setVisible(false);
        grpScaleMode.setVisible(false);
        grpChordDetection.setVisible(false);
        grpInstrument.setVisible(false);
        grpReharm.setVisible(false);
        grpChordTrack.setVisible(false);
        grpModeTrack.setVisible(false);
        grpMarkerTrack.setVisible(false);
        grpFolderTrack.setVisible(false);

        bool showTimeSig = isSectionVisibleInThisDock(8);
        grpTimeSigTrack.setVisible(showTimeSig);
        if (showTimeSig)
        {
            grpTimeSigTrack.setBounds(area.removeFromTop(sectionHeight(8)));

            auto tsArea = bodyArea(grpTimeSigTrack);
            lblTimeSigInfo.setBounds(tsArea.removeFromTop(50));
        }
        return;
    }
    else if (trkType == TrackType::Folder)
    {
        grpNoteInfo.setVisible(false);
        grpScaleMode.setVisible(false);
        grpChordDetection.setVisible(false);
        grpInstrument.setVisible(false);
        grpReharm.setVisible(false);
        grpChordTrack.setVisible(false);
        grpModeTrack.setVisible(false);
        grpMarkerTrack.setVisible(false);
        grpTimeSigTrack.setVisible(false);

        bool showFolder = isSectionVisibleInThisDock(9);
        grpFolderTrack.setVisible(showFolder);
        if (showFolder)
        {
            grpFolderTrack.setBounds(area.removeFromTop(sectionHeight(9)));

            auto fArea = bodyArea(grpFolderTrack);
            lblFolderTrackSummary.setBounds(fArea.removeFromTop(60));
            fArea.removeFromTop(4);
            lblFolderNotesTitle.setBounds(fArea.removeFromTop(20));
            fArea.removeFromTop(2);
            txtFolderNotes.setBounds(fArea);
        }
        return;
    }

    // Default: Instrument Track
    grpChordTrack.setVisible(false);
    grpModeTrack.setVisible(false);
    grpMarkerTrack.setVisible(false);
    grpTimeSigTrack.setVisible(false);
    grpFolderTrack.setVisible(false);
    grpNoteInfo.setVisible(false);
    grpScaleMode.setVisible(false);
    grpChordDetection.setVisible(false);
    grpInstrument.setVisible(false);
    grpReharm.setVisible(false);

    for (int secIdx : sectionOrder)
    {
        bool showThisSection = isSectionVisibleInThisDock(secIdx);
        if (secIdx == 0) // 1. Note Inspector Group
        {
            grpNoteInfo.setVisible(showThisSection);
            if (showThisSection)
            {
                auto& state = AppStateManager::getInstance();
                grpNoteInfo.setBounds(area.removeFromTop(sectionHeight(0)));

                auto noteArea = bodyArea(grpNoteInfo);
                lblSelectionCount.setBounds(noteArea.removeFromTop(noteLineH));
                NotePropertyRowComponent* noteRows[] = {
                    &lblNotePitch, &lblNoteStart, &lblNoteLength, &lblNoteVelocity,
                    &lblNoteScaleDegree, &lblNoteFunctionalRole, &lblNoteMotionTendency,
                    &lblNoteColorUsage, &lblNoteInterval
                };
                for (auto* row : noteRows) row->setCompact(compactNotes);
                if (state.noteShowPitch)          lblNotePitch.setBounds(noteArea.removeFromTop(notePropertyH));
                if (state.noteShowStart)          lblNoteStart.setBounds(noteArea.removeFromTop(notePropertyH));
                if (state.noteShowLength)         lblNoteLength.setBounds(noteArea.removeFromTop(notePropertyH));
                if (state.noteShowVelocity)       lblNoteVelocity.setBounds(noteArea.removeFromTop(notePropertyH));
                if (state.noteShowScaleDegree)    lblNoteScaleDegree.setBounds(noteArea.removeFromTop(notePropertyH));
                if (state.noteShowFunctionalRole) lblNoteFunctionalRole.setBounds(noteArea.removeFromTop(notePropertyH));
                if (state.noteShowMotionTendency) lblNoteMotionTendency.setBounds(noteArea.removeFromTop(notePropertyH));
                if (state.noteShowColorUsage)     lblNoteColorUsage.setBounds(noteArea.removeFromTop(notePropertyH));
                if (state.noteShowInterval)       lblNoteInterval.setBounds(noteArea.removeFromTop(notePropertyH));
                alignNotePropertyRows();
                area.removeFromTop(moduleGap);
            }
        }
        else if (secIdx == 1) // 2. Scale & Mode Analysis Group
        {
            grpScaleMode.setVisible(showThisSection);
            if (showThisSection)
            {
                int scaleGroupH = sectionHeight(1);
                grpScaleMode.setBounds(area.removeFromTop(scaleGroupH));

                auto scaleArea = bodyArea(grpScaleMode);

                if (compactControls)
                {
                    keyRootCombo.setBounds(scaleArea.removeFromTop(24));
                    scaleArea.removeFromTop(4);
                    scaleTypeCombo.setBounds(scaleArea.removeFromTop(24));
                }
                else
                {
                    auto rootRow = scaleArea.removeFromTop(24);
                    keyRootCombo.setBounds(rootRow.removeFromLeft(60));
                    rootRow.removeFromLeft(4);
                    scaleTypeCombo.setBounds(rootRow);
                }
                scaleArea.removeFromTop(6);

                auto modeActionRow = scaleArea.removeFromTop(22);
                const int actionWidth = (modeActionRow.getWidth() - 6) / 2;
                btnApplyCurrentMode.setBounds(modeActionRow.removeFromLeft(actionWidth));
                modeActionRow.removeFromLeft(6);
                btnCustomScale.setBounds(modeActionRow);
                scaleArea.removeFromTop(6);

                btnAnalyzeMode.setBounds(scaleArea.removeFromTop(26));
                scaleArea.removeFromTop(6);
                lblModeAnalysisConfidence.setBounds(scaleArea.removeFromTop(22));
                scaleArea.removeFromTop(6);
                if (!cachedCandidates.empty())
                {
                    auto groupRow = scaleArea.removeFromTop(24);
                    const int groupWidth = (groupRow.getWidth() - 6) / 2;
                    btnGroupByTonic.setBounds(groupRow.removeFromLeft(groupWidth));
                    groupRow.removeFromLeft(6);
                    btnGroupByType.setBounds(groupRow);
                    scaleArea.removeFromTop(6);
                    modeCandidatesList.setBounds(scaleArea.removeFromTop(190));
                    scaleArea.removeFromTop(6);
                    btnApplyCandidate.setBounds(scaleArea.removeFromTop(24));
                    scaleArea.removeFromTop(6);
                    btnApplyCandidateToRange.setBounds(scaleArea.removeFromTop(24));
                }
                area.removeFromTop(moduleGap);
            }
        }
        else if (secIdx == 2) // 3. Real-time Chord Detection Group
        {
            grpChordDetection.setVisible(showThisSection);
            if (showThisSection)
            {
                auto& state = AppStateManager::getInstance();
                float pt = (float)std::clamp(state.chordAnalysisFontSizePt, 10, 48);
                int chordH = juce::roundToInt(pt * 1.35f);
                int romanH = juce::roundToInt(pt * 0.72f * 1.35f);

                grpChordDetection.setBounds(area.removeFromTop(sectionHeight(2)));

                auto chordArea = bodyArea(grpChordDetection);
                const bool horizontal = state.chordAnalysisOrientation == 1
                    && !(compactControls && state.chordAnalysisDisplayMode == 0);
                if (horizontal)
                {
                    int rowH = std::max(chordH, romanH);
                    auto row = chordArea.removeFromTop(rowH);
                    if (state.chordAnalysisDisplayMode == 0) // Both
                    {
                        int halfW = row.getWidth() / 2;
                        lblDetectedChord.setJustificationType(juce::Justification::centred);
                        lblRomanNumeral.setJustificationType(juce::Justification::centred);
                        lblDetectedChord.setBounds(row.removeFromLeft(halfW));
                        lblRomanNumeral.setBounds(row);
                    }
                    else if (state.chordAnalysisDisplayMode == 1) // Chord only
                    {
                        lblDetectedChord.setJustificationType(juce::Justification::centred);
                        lblDetectedChord.setBounds(row);
                    }
                    else if (state.chordAnalysisDisplayMode == 2) // Roman only
                    {
                        lblRomanNumeral.setJustificationType(juce::Justification::centred);
                        lblRomanNumeral.setBounds(row);
                    }
                }
                else // Vertical Stack
                {
                    lblDetectedChord.setJustificationType(juce::Justification::centred);
                    lblRomanNumeral.setJustificationType(juce::Justification::centred);
                    if (state.chordAnalysisDisplayMode == 0)
                    {
                        lblDetectedChord.setBounds(chordArea.removeFromTop(chordH));
                        chordArea.removeFromTop(2);
                        lblRomanNumeral.setBounds(chordArea.removeFromTop(romanH));
                    }
                    else if (state.chordAnalysisDisplayMode == 1)
                    {
                        lblDetectedChord.setBounds(chordArea.removeFromTop(chordH));
                    }
                    else if (state.chordAnalysisDisplayMode == 2)
                    {
                        lblRomanNumeral.setBounds(chordArea.removeFromTop(romanH));
                    }
                }
                area.removeFromTop(moduleGap);
            }
        }
        else if (secIdx == 3) // 4. Instrument Group & Channel Strip
        {
            grpInstrument.setVisible(showThisSection);
            if (showThisSection)
            {
                int instGroupH = sectionHeight(3);
                grpInstrument.setBounds(area.removeFromTop(instGroupH));

                auto instArea = bodyArea(grpInstrument);
                vstPluginCombo.setBounds(instArea.removeFromTop(24));
                instArea.removeFromTop(6);
                btnOpenEditor.setBounds(instArea.removeFromTop(24));
                instArea.removeFromTop(6);

                auto volRow = instArea.removeFromTop(26);
                lblVol.setBounds(volRow.removeFromLeft(32));
                volSlider.setBounds(volRow);
                instArea.removeFromTop(4);

                auto panRow = instArea.removeFromTop(26);
                lblPan.setBounds(panRow.removeFromLeft(32));
                panSlider.setBounds(panRow);

                area.removeFromTop(moduleGap);
            }
        }
        else if (secIdx == 4) // Secondary Dominant
        {
            grpReharm.setVisible(showThisSection);
            if (showThisSection)
            {
                const bool showManualRoot = secondarySettingsExpanded
                    && AppStateManager::getInstance().secondaryDominantTargetSource == 3;
                int reharmGroupH = sectionHeight(4);
                grpReharm.setBounds(area.removeFromTop(reharmGroupH));

                auto reharmArea = bodyArea(grpReharm);
                btnSecondaryDominant.setBounds(reharmArea.removeFromTop(26));
                reharmArea.removeFromTop(6);
                btnReharmOptions.setBounds(reharmArea.removeFromTop(26));
                reharmArea.removeFromTop(6);

                auto layoutOption = [&reharmArea, compactControls](juce::Label& label, juce::ComboBox& combo) {
                    if (compactControls)
                    {
                        label.setBounds(reharmArea.removeFromTop(17));
                        reharmArea.removeFromTop(2);
                        combo.setBounds(reharmArea.removeFromTop(24));
                        reharmArea.removeFromTop(5);
                    }
                    else
                    {
                        auto row = reharmArea.removeFromTop(24);
                        label.setBounds(row.removeFromLeft(58));
                        row.removeFromLeft(3);
                        combo.setBounds(row);
                        reharmArea.removeFromTop(4);
                    }
                };
                lblSecondaryTarget.setVisible(secondarySettingsExpanded);
                secondaryTargetCombo.setVisible(secondarySettingsExpanded);
                lblSecondaryManualRoot.setVisible(showManualRoot);
                secondaryManualRootCombo.setVisible(showManualRoot);
                lblSecondaryType.setVisible(secondarySettingsExpanded);
                secondaryTypeCombo.setVisible(secondarySettingsExpanded);
                lblSecondaryVoicing.setVisible(secondarySettingsExpanded);
                secondaryVoicingCombo.setVisible(secondarySettingsExpanded);
                if (secondarySettingsExpanded)
                {
                    layoutOption(lblSecondaryTarget, secondaryTargetCombo);
                    if (showManualRoot)
                        layoutOption(lblSecondaryManualRoot, secondaryManualRootCombo);
                    layoutOption(lblSecondaryType, secondaryTypeCombo);
                    layoutOption(lblSecondaryVoicing, secondaryVoicingCombo);
                }
                area.removeFromTop(moduleGap);
            }
        }
        else if (secIdx == 10) // Reharmonization
        {
            grpReharmonization.setVisible(showThisSection);
            if (showThisSection)
            {
                grpReharmonization.setBounds(area.removeFromTop(sectionHeight(10)));
                auto editArea = bodyArea(grpReharmonization);
                const int columns = contentW >= 270 ? 3 : 2;
                for (int first = 0; first < kReharmonizationAlgorithmCount; first += columns)
                {
                    auto buttonRow = editArea.removeFromTop(30);
                    const int count = std::min(columns, kReharmonizationAlgorithmCount - first);
                    const int width = (buttonRow.getWidth() - (count - 1) * 6) / count;
                    for (int i = 0; i < count; ++i)
                    {
                        reharmonizationModeButtons[(size_t)(first + i)].setBounds(
                            buttonRow.removeFromLeft(i == count - 1 ? buttonRow.getWidth() : width));
                        if (i < count - 1) buttonRow.removeFromLeft(6);
                    }
                    if (first + count < kReharmonizationAlgorithmCount)
                        editArea.removeFromTop(6);
                }
                editArea.removeFromTop(6);
                txtReharmonizationPreview.setBounds(
                    editArea.removeFromTop(reharmonizationPreviewHeight));
                area.removeFromTop(moduleGap);
            }
        }
        else if (secIdx == kChordShapeSectionId)
        {
            grpChordShape.setVisible(showThisSection);
            if (showThisSection)
            {
                grpChordShape.setBounds(area.removeFromTop(sectionHeight(kChordShapeSectionId)));
                auto body = bodyArea(grpChordShape);
                lblChordShapeStatus.setBounds(body.removeFromTop(18));
                body.removeFromTop(4);
                juce::TextButton* qualityButtons[] = {
                    &btnShapeMajor, &btnShapeMinor, &btnShapeDiminished,
                    &btnShapeAugmented, &btnShapeSus2, &btnShapeSus4,
                    &btnShapeDominant7, &btnShapeMajor7, &btnShapeMinor7,
                    &btnShapeHalfDiminished7
                };
                const int columns = contentW >= 280 ? 5 : contentW >= 210 ? 4 : 3;
                for (int first = 0; first < 10; first += columns)
                {
                    auto row = body.removeFromTop(26);
                    const int count = std::min(columns, 10 - first);
                    const int buttonWidth = std::max(1, (row.getWidth() - (count - 1) * 4) / count);
                    for (int i = 0; i < count; ++i)
                    {
                        qualityButtons[first + i]->setBounds(row.removeFromLeft(
                            i == count - 1 ? row.getWidth() : buttonWidth));
                        if (i < count - 1) row.removeFromLeft(4);
                    }
                    if (first + count < 10) body.removeFromTop(4);
                }
                body.removeFromTop(8);
                auto voicingRow = body.removeFromTop(26);
                const int third = std::max(1, (voicingRow.getWidth() - 12) / 3);
                for (auto* button : { &btnVoicingExpand, &btnVoicingContract })
                {
                    button->setBounds(voicingRow.removeFromLeft(third));
                    voicingRow.removeFromLeft(6);
                }
                btnVoicingDrop2.setBounds(voicingRow);
                body.removeFromTop(8);
                auto naturalRow = body.removeFromTop(26);
                const int naturalWidth = std::max(1, (naturalRow.getWidth() - 4) / 2);
                stackButtons[3].setBounds(naturalRow.removeFromLeft(naturalWidth));
                naturalRow.removeFromLeft(4);
                stackButtons[7].setBounds(naturalRow);
                for (size_t i = 0; i < stackButtons.size(); ++i)
                    if (i != 3 && i != 7)
                        stackButtons[i].setVisible(appState.showChordStackDirections);
                if (appState.showChordStackDirections)
                {
                    body.removeFromTop(6);
                    for (int first : { 0, 4 })
                    {
                        auto row = body.removeFromTop(26);
                        const int width = std::max(1, (row.getWidth() - 8) / 3);
                        for (int i = 0; i < 3; ++i)
                        {
                            stackButtons[(size_t)(first + i)].setBounds(
                                row.removeFromLeft(i == 2 ? row.getWidth() : width));
                            if (i < 2) row.removeFromLeft(4);
                        }
                        if (first == 0) body.removeFromTop(4);
                    }
                }
                area.removeFromTop(moduleGap);
            }
        }
        else if (secIdx == kQuickChordSectionId)
        {
            grpQuickChord.setVisible(showThisSection);
            if (showThisSection)
            {
                grpQuickChord.setBounds(area.removeFromTop(sectionHeight(kQuickChordSectionId)));
                layoutQuickChordModule();
                area.removeFromTop(moduleGap);
            }
        }
        else if (secIdx == kModeBorrowingSectionId)
        {
            grpModeBorrowing.setVisible(showThisSection);
            if (showThisSection)
            {
                grpModeBorrowing.setBounds(area.removeFromTop(sectionHeight(kModeBorrowingSectionId)));
                auto body = bodyArea(grpModeBorrowing);
                borrowingStyleCombo.setBounds(body.removeFromTop(27));
                body.removeFromTop(8);
                btnBorrowingSourceDisclosure.setBounds(body.removeFromTop(46));
                if (borrowingSourceSettingsExpanded)
                {
                    body.removeFromTop(8);
                    auto choiceRow = body.removeFromTop(28);
                    const int half = (choiceRow.getWidth() - 6) / 2;
                    btnBorrowingSourceAuto.setBounds(choiceRow.removeFromLeft(half));
                    choiceRow.removeFromLeft(6);
                    btnBorrowingSourceCustom.setBounds(choiceRow);
                    if (appState.modeBorrowingCustomSource)
                    {
                        body.removeFromTop(8);
                        auto sourceRow = body.removeFromTop(27);
                        borrowingSourceRootCombo.setBounds(sourceRow.removeFromLeft(54));
                        sourceRow.removeFromLeft(6);
                        borrowingSourceScaleCombo.setBounds(sourceRow);
                    }
                }
                body.removeFromTop(10);
                lblBorrowingTarget.setBounds(body.removeFromTop(18));
                body.removeFromTop(2);
                auto targetRow = body.removeFromTop(27);
                borrowingTargetRootCombo.setBounds(targetRow.removeFromLeft(54));
                targetRow.removeFromLeft(6);
                borrowingScaleCombo.setBounds(targetRow);
                body.removeFromTop(10);
                const bool showDegree = appState.modeBorrowingStyle == 0;
                borrowingDegreeCombo.setVisible(showDegree);
                if (showDegree)
                {
                    borrowingDegreeCombo.setBounds(body.removeFromTop(27));
                    body.removeFromTop(8);
                }
                body.removeFromTop(8);
                txtBorrowingPreview.setBounds(body.removeFromTop(90));
                body.removeFromTop(8);
                btnApplyBorrowing.setBounds(body.removeFromTop(30));
                area.removeFromTop(moduleGap);
            }
        }
    }
}

void InspectorComponent::updateMarkerPropertyFields()
{
    int activeTrkId = trackModel.getActiveTrackId();
    auto* trk = trackModel.getTrackById(activeTrkId);
    if (!trk || trk->getType() != TrackType::Marker)
    {
        txtMarkerName.setText("", juce::dontSendNotification);
        txtMarkerStart.setText("", juce::dontSendNotification);
        txtMarkerLength.setText("", juce::dontSendNotification);
        txtMarkerName.setEnabled(false);
        txtMarkerStart.setEnabled(false);
        txtMarkerLength.setEnabled(false);
        markerColorCombo.setEnabled(false);
        btnJumpToMarker.setEnabled(false);
        btnDeleteMarker.setEnabled(false);
        return;
    }

    const auto& markers = trk->getMarkers();
    if (selectedMarkerIndex >= 0 && selectedMarkerIndex < (int)markers.size())
    {
        const auto& m = markers[selectedMarkerIndex];
        txtMarkerName.setText(m.name, juce::dontSendNotification);
        txtMarkerStart.setText(juce::String(m.startBeat, 2), juce::dontSendNotification);
        txtMarkerLength.setText(juce::String(m.lengthBeats, 2), juce::dontSendNotification);

        juce::Colour c = m.color;
        int colorId = 1;
        if (c == CubaseTheme::accentOrange() || c == juce::Colour(0xfff59e0b)) colorId = 2;
        else if (c == juce::Colours::red || c == juce::Colour(0xffef4444)) colorId = 3;
        else if (c == juce::Colours::green || c == juce::Colour(0xff10b981)) colorId = 4;
        else if (c == juce::Colours::purple || c == juce::Colour(0xff8b5cf6)) colorId = 5;
        else if (c == juce::Colours::yellow || c == juce::Colour(0xffeab308)) colorId = 6;
        else if (c == juce::Colours::dodgerblue || c == juce::Colour(0xff3b82f6)) colorId = 7;
        else if (c == juce::Colours::grey) colorId = 8;
        markerColorCombo.setSelectedId(colorId, juce::dontSendNotification);

        txtMarkerName.setEnabled(true);
        txtMarkerStart.setEnabled(true);
        txtMarkerLength.setEnabled(true);
        markerColorCombo.setEnabled(true);
        btnJumpToMarker.setEnabled(true);
        btnDeleteMarker.setEnabled(true);
    }
    else
    {
        txtMarkerName.setText("", juce::dontSendNotification);
        txtMarkerStart.setText("", juce::dontSendNotification);
        txtMarkerLength.setText("", juce::dontSendNotification);
        txtMarkerName.setEnabled(false);
        txtMarkerStart.setEnabled(false);
        txtMarkerLength.setEnabled(false);
        markerColorCombo.setEnabled(false);
        btnJumpToMarker.setEnabled(false);
        btnDeleteMarker.setEnabled(false);
    }
}

void InspectorComponent::updateMarkerTrackControls()
{
    int activeTrkId = trackModel.getActiveTrackId();
    auto* trk = trackModel.getTrackById(activeTrkId);
    if (!trk || trk->getType() != TrackType::Marker) return;

    const auto& markers = trk->getMarkers();
    if (selectedMarkerIndex == -1 && !markers.empty())
    {
        for (size_t i = 0; i < markers.size(); ++i)
        {
            if (markers[i].isSelected)
            {
                selectedMarkerIndex = (int)i;
                break;
            }
        }
        if (selectedMarkerIndex == -1)
            selectedMarkerIndex = 0;
    }
    else if (selectedMarkerIndex >= (int)markers.size())
    {
        selectedMarkerIndex = (int)markers.size() - 1;
    }

    markerListBox.updateContent();
    if (selectedMarkerIndex >= 0 && selectedMarkerIndex < (int)markers.size())
        markerListBox.selectRow(selectedMarkerIndex, false, true);

    updateMarkerPropertyFields();
}

void InspectorComponent::updateTrackContext()
{
    updateChannelStrip();
    updateVstSelection();
    updateVstInfo();
    btnModeTrackOverride.setToggleState(trackModel.isModeTrackOverrideEnabled(), juce::dontSendNotification);
    int activeTrkId = trackModel.getActiveTrackId();
    auto* activeTrack = trackModel.getTrackById(activeTrkId);
    if (activeTrack != nullptr && activeTrack->getType() == TrackType::Marker)
    {
        updateMarkerTrackControls();
    }
    else if (activeTrack != nullptr && activeTrack->getType() == TrackType::Folder)
    {
        auto childIds = trackModel.getChildTrackIds(activeTrkId);
        int instCount = 0, chordCount = 0, modeCount = 0, markerCount = 0;
        int totalClips = 0, totalNotes = 0;
        for (int cid : childIds)
        {
            if (auto* ct = trackModel.getTrackById(cid))
            {
                if (ct->getType() == TrackType::Instrument)
                {
                    instCount++;
                    totalClips += (int)ct->getClips().size();
                    for (const auto& cl : ct->getClips())
                        totalNotes += (int)cl.notes.size();
                }
                else if (ct->getType() == TrackType::Chord) chordCount++;
                else if (ct->getType() == TrackType::Mode) modeCount++;
                else if (ct->getType() == TrackType::Marker) markerCount++;
            }
        }
        juce::String summary;
        summary << tr("Child Tracks") << ": " << (int)childIds.size() << " ("
                << tr("Inst") << ":" << instCount << ", "
                << tr("Chord") << ":" << chordCount << ", "
                << tr("Mode") << ":" << modeCount << ", "
                << tr("Marker") << ":" << markerCount << ")\n";
        summary << tr("Total Clips") << ": " << totalClips << "  |  "
                << tr("Total Notes") << ": " << totalNotes;
        lblFolderTrackSummary.setText(summary, juce::dontSendNotification);
        txtFolderNotes.setText(activeTrack->getFolderNotes(), false);
    }
    resized();
    repaint();
}

void InspectorComponent::extractChordsFromMidiTrack()
{
    // Find the first instrument track with notes, or active clip
    Track* targetInstTrack = nullptr;
    for (auto& t : trackModel.getTracks())
    {
        if (t->getType() == TrackType::Instrument && !t->getClips().empty())
        {
            targetInstTrack = t.get();
            break;
        }
    }

    // Also look for Chord track
    Track* chordTrack = nullptr;
    for (auto& t : trackModel.getTracks())
    {
        if (t->getType() == TrackType::Chord)
        {
            chordTrack = t.get();
            break;
        }
    }

    if (chordTrack == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            tr("Extract Chords"),
            tr("No Chord track found in project. Please add a Chord track first."));
        return;
    }

    // Collect all notes from target instrument track clips or from midiModel
    std::vector<NoteEvent> allNotes;
    if (targetInstTrack != nullptr)
    {
        for (const auto& clip : targetInstTrack->getClips())
        {
            for (const auto& n : clip.notes)
            {
                NoteEvent shifted = n;
                shifted.startBeat += clip.startBeat;
                allNotes.push_back(shifted);
            }
        }
    }
    else
    {
        allNotes = midiModel.getNotes();
    }

    if (allNotes.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            tr("Extract Chords"),
            tr("No MIDI notes found to analyze chords from."));
        return;
    }

    // Find min and max beats
    double minBeat = 1e9, maxBeat = 0;
    for (const auto& n : allNotes)
    {
        minBeat = std::min(minBeat, n.startBeat);
        maxBeat = std::max(maxBeat, n.startBeat + n.lengthBeats);
    }

    int startBar = (int)std::floor(minBeat / 4.0);
    int endBar = (int)std::ceil(maxBeat / 4.0);
    if (endBar <= startBar) endBar = startBar + 1;

    std::vector<ChordEvent> newChords;
    for (int bar = startBar; bar < endBar; ++bar)
    {
        double barStart = bar * 4.0;
        double barEnd = barStart + 4.0;

        std::vector<NoteEvent> barNotes;
        for (const auto& n : allNotes)
        {
            if (n.startBeat < barEnd && (n.startBeat + n.lengthBeats) > barStart)
            {
                barNotes.push_back(n);
            }
        }

        if (!barNotes.empty())
        {
            // Chord-track extraction still expects one local harmony per bar;
            // the inspector's selection analysis intentionally has no time gate.
            const auto groups = ChordClassifier::identifyChordGroups(barNotes, 4.0,
                [this](double beat) {
                    const auto layout = midiModel.getEffectiveLayoutAtBeat(beat, &trackModel);
                    return ChordAnalysisContext { layout.tonicPc, layout.scaleName,
                                                  layout.tonicName };
                });
            if (groups.size() == 1 && groups.front().chord.confidence >= 0.5)
            {
                ChordEvent ce;
                ce.id = juce::Time::currentTimeMillis() + bar;
                ce.startBeat = barStart;
                ce.lengthBeats = 4.0;
                ce.chordName = groups.front().chord.fullName;
                ce.romanNumeral = groups.front().chord.romanNumeral;
                newChords.push_back(ce);
            }
        }
    }

    if (newChords.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            tr("Extract Chords"),
            tr("Could not reliably identify chords from notes."));
        return;
    }

    // Apply to Chord Track
    chordTrack->getChords() = newChords;
    trackModel.triggerChordsChanged();

    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        tr("Extract Chords"),
        juce::String((int)newChords.size()) + " " + tr("chord events extracted to Chord track!"));
}

class CustomScaleDialog : public juce::Component
{
public:
    CustomScaleDialog(std::function<void(const juce::String&)> onSave)
        : onSaveCallback(std::move(onSave))
    {
        addAndMakeVisible(lblName);
        lblName.setText(juce::String::fromUTF8((const char*)u8"音阶名称 (Scale Name):"), juce::dontSendNotification);
        lblName.setFont(CubaseTheme::getFont(12.0f, true));
        lblName.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());

        addAndMakeVisible(txtName);
        txtName.setText("My Custom Scale");
        txtName.setFont(CubaseTheme::getFont(12.0f));

        addAndMakeVisible(lblMatrix);
        lblMatrix.setText(juce::String::fromUTF8((const char*)u8"12半音音阶矩阵 (12-TET Matrix):"), juce::dontSendNotification);
        lblMatrix.setFont(CubaseTheme::getFont(11.5f, true));
        lblMatrix.setColour(juce::Label::textColourId, CubaseTheme::accentOrange());

        static const char* intervalLabels[12] = {
            "1 (Root)", "b2 (m2)", "2 (M2)", "b3 (m3)",
            "3 (M3)", "4 (P4)", "b5 (tritone)", "5 (P5)",
            "b6 (m6)", "6 (M6)", "b7 (m7)", "7 (M7)"
        };

        for (int i = 0; i < 12; ++i)
        {
            auto btn = std::make_unique<juce::ToggleButton>(intervalLabels[i]);
            btn->setColour(juce::ToggleButton::textColourId, CubaseTheme::textPrimary());
            btn->setColour(juce::ToggleButton::tickColourId, CubaseTheme::accentCyan());
            if (i == 0 || i == 2 || i == 4 || i == 5 || i == 7 || i == 9 || i == 11) // default Major
                btn->setToggleState(true, juce::dontSendNotification);
            if (i == 0)
                btn->setEnabled(false); // Root is always active
            addAndMakeVisible(*btn);
            semitoneToggles.push_back(std::move(btn));
        }

        auto setupPreset = [this](juce::TextButton& btn, const juce::String& text, const std::vector<int>& intervals) {
            btn.setButtonText(text);
            btn.onClick = [this, intervals]() {
                for (int i = 0; i < 12; ++i)
                {
                    bool active = (std::find(intervals.begin(), intervals.end(), i) != intervals.end());
                    semitoneToggles[i]->setToggleState(active || i == 0, juce::dontSendNotification);
                }
            };
            addAndMakeVisible(btn);
        };

        setupPreset(btnPresetMajor, juce::String::fromUTF8((const char*)u8"大调"), { 0, 2, 4, 5, 7, 9, 11 });
        setupPreset(btnPresetMinor, juce::String::fromUTF8((const char*)u8"小调"), { 0, 2, 3, 5, 7, 8, 10 });
        setupPreset(btnPresetPentatonic, juce::String::fromUTF8((const char*)u8"五声"), { 0, 2, 4, 7, 9 });
        setupPreset(btnPresetBlues, juce::String::fromUTF8((const char*)u8"布鲁斯"), { 0, 3, 5, 6, 7, 10 });
        setupPreset(btnPresetClear, juce::String::fromUTF8((const char*)u8"清空"), { 0 });

        btnSave.setButtonText(juce::String::fromUTF8((const char*)u8"保存并应用"));
        btnSave.setColour(juce::TextButton::buttonColourId, CubaseTheme::accentCyan().withAlpha(0.7f));
        btnSave.onClick = [this]() {
            saveAndClose();
        };
        addAndMakeVisible(btnSave);

        btnCancel.setButtonText(juce::String::fromUTF8((const char*)u8"取消"));
        btnCancel.onClick = [this]() {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState(0);
        };
        addAndMakeVisible(btnCancel);

        setSize(380, 320);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(CubaseTheme::bgPanel());
        g.setColour(CubaseTheme::borderSubtle());
        g.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(16);
        auto nameRow = area.removeFromTop(24);
        lblName.setBounds(nameRow.removeFromLeft(120));
        txtName.setBounds(nameRow);
        area.removeFromTop(12);

        lblMatrix.setBounds(area.removeFromTop(20));
        area.removeFromTop(6);

        // 12 semitones in 3 columns x 4 rows
        auto gridArea = area.removeFromTop(110);
        int colW = gridArea.getWidth() / 3;
        for (int row = 0; row < 4; ++row)
        {
            auto rowArea = gridArea.removeFromTop(26);
            for (int col = 0; col < 3; ++col)
            {
                int idx = row * 3 + col;
                if (idx < 12)
                    semitoneToggles[idx]->setBounds(rowArea.removeFromLeft(colW));
            }
        }
        area.removeFromTop(8);

        // Presets row
        auto presetRow = area.removeFromTop(24);
        int pw = presetRow.getWidth() / 5;
        btnPresetMajor.setBounds(presetRow.removeFromLeft(pw).reduced(2, 0));
        btnPresetMinor.setBounds(presetRow.removeFromLeft(pw).reduced(2, 0));
        btnPresetPentatonic.setBounds(presetRow.removeFromLeft(pw).reduced(2, 0));
        btnPresetBlues.setBounds(presetRow.removeFromLeft(pw).reduced(2, 0));
        btnPresetClear.setBounds(presetRow.reduced(2, 0));
        area.removeFromTop(14);

        // Save & Cancel buttons
        auto btnRow = area.removeFromBottom(28);
        btnCancel.setBounds(btnRow.removeFromRight(80));
        btnRow.removeFromRight(10);
        btnSave.setBounds(btnRow);
    }

private:
    void saveAndClose()
    {
        juce::String name = txtName.getText().trim();
        if (name.isEmpty())
            name = "Custom Scale";

        std::vector<int> intervals;
        static const char* degreeNames[12] = {
            "1", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7"
        };
        std::vector<juce::String> degrees;

        for (int i = 0; i < 12; ++i)
        {
            if (i == 0 || semitoneToggles[i]->getToggleState())
            {
                intervals.push_back(i);
                degrees.push_back(degreeNames[i]);
            }
        }

        ScaleDefinitions::registerCustomScale(name, intervals, degrees);

        if (onSaveCallback)
            onSaveCallback(name);

        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    }

    std::function<void(const juce::String&)> onSaveCallback;
    juce::Label lblName;
    juce::TextEditor txtName;
    juce::Label lblMatrix;
    std::vector<std::unique_ptr<juce::ToggleButton>> semitoneToggles;

    juce::TextButton btnPresetMajor;
    juce::TextButton btnPresetMinor;
    juce::TextButton btnPresetPentatonic;
    juce::TextButton btnPresetBlues;
    juce::TextButton btnPresetClear;

    juce::TextButton btnSave;
    juce::TextButton btnCancel;
};

void InspectorComponent::openCustomScaleDialog()
{
    juce::Component::SafePointer<InspectorComponent> safeThis(this);
    auto* dialog = new CustomScaleDialog([safeThis](const juce::String& newScaleName) {
        auto* owner = safeThis.getComponent();
        if (owner == nullptr) return;
        owner->scaleTypeCombo.clear();
        int scaleIdx = 1;
        int targetId = 1;
        const auto& scales = ScaleDefinitions::getAllScales();
        for (size_t i = 0; i < scales.size(); ++i)
        {
            owner->scaleTypeCombo.addItem(scales[i].name, scaleIdx);
            if (scales[i].name == newScaleName)
                targetId = scaleIdx;
            scaleIdx++;
        }
        owner->scaleTypeCombo.setSelectedId(targetId, juce::dontSendNotification);
        owner->applyCurrentMode();
    });

    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = juce::String::fromUTF8((const char*)u8"自定义12半音音阶设计器");
    opt.content.setOwned(dialog);
    opt.dialogBackgroundColour = CubaseTheme::bgPanel();
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = false;
    opt.resizable = false;
    opt.launchAsync();
}

const juce::GroupComponent* InspectorComponent::groupForSection(int sectionIndex) const
{
    switch (sectionIndex)
    {
        case 0: return &grpNoteInfo;
        case 1: return &grpScaleMode;
        case 2: return &grpChordDetection;
        case 3: return &grpInstrument;
        case 4: return &grpReharm;
        case 5: return &grpChordTrack;
        case 6: return &grpModeTrack;
        case 7: return &grpMarkerTrack;
        case 8: return &grpTimeSigTrack;
        case 9: return &grpFolderTrack;
        case 10: return &grpReharmonization;
        case 11: return &grpAudioTrack;
        case 12: return &grpChordShape;
        case 13: return &grpModeBorrowing;
        case kQuickChordSectionId: return &grpQuickChord;
        default: return nullptr;
    }
}

std::vector<int> InspectorComponent::visibleSectionIdsExcluding(int excludedSection) const
{
    std::vector<int> ids;
    for (int id : sectionOrder)
    {
        const auto* group = groupForSection(id);
        if (id != excludedSection && group != nullptr && group->isVisible()
            && isSectionVisibleInThisDock(id))
            ids.push_back(id);
    }
    return ids;
}

int InspectorComponent::insertionIndexAt(int contentY, int excludedSection) const
{
    int index = 0;
    for (int id : visibleSectionIdsExcluding(excludedSection))
    {
        const auto* group = groupForSection(id);
        if (group != nullptr && contentY < group->getBounds().getCentreY())
            break;
        ++index;
    }
    return index;
}

int InspectorComponent::insertionLineAt(int insertionIndex, int excludedSection) const
{
    const auto ids = visibleSectionIdsExcluding(excludedSection);
    if (ids.empty()) return 8;
    if (insertionIndex < (int)ids.size())
        return groupForSection(ids[(size_t)insertionIndex])->getY() - 4;
    return groupForSection(ids.back())->getBottom() + 4;
}

void InspectorComponent::clearDropFeedback()
{
    if (dropIndicatorY < 0) return;
    dropIndicatorY = -1;
    if (dropOverlay) dropOverlay->repaint();
}

void InspectorComponent::moveSectionToDock(int sectionIndex,
                                           InspectorComponent& destination,
                                           int insertionIndex)
{
    if (!isInspectorSectionId(sectionIndex)
        || destination.currentDockContext != currentDockContext)
        return;

    auto updatedOrder = sectionOrder;
    if (isNoteTrackSectionId(sectionIndex))
    {
        const auto destinationIds = destination.visibleSectionIdsExcluding(sectionIndex);
        auto previous = std::find(updatedOrder.begin(), updatedOrder.end(), sectionIndex);
        if (previous == updatedOrder.end()) return;
        updatedOrder.erase(previous);

        auto insertAt = updatedOrder.end();
        if (insertionIndex >= 0 && insertionIndex < (int)destinationIds.size())
        {
            insertAt = std::find(updatedOrder.begin(), updatedOrder.end(),
                                 destinationIds[(size_t)insertionIndex]);
        }
        else if (!destinationIds.empty())
        {
            insertAt = std::find(updatedOrder.begin(), updatedOrder.end(), destinationIds.back());
            if (insertAt != updatedOrder.end()) ++insertAt;
        }
        else
        {
            auto lastShared = std::find_if(updatedOrder.rbegin(), updatedOrder.rend(),
                                           [](int id) { return isNoteTrackSectionId(id); });
            if (lastShared != updatedOrder.rend()) insertAt = lastShared.base();
        }
        updatedOrder.insert(insertAt, sectionIndex);
    }

    const auto newPosition = destination.dockFilter == DockSide::Right
        ? SectionDockPosition::Right : SectionDockPosition::Left;
    const bool positionChanged = getSectionPosition(sectionIndex) != newPosition;
    if (!positionChanged && updatedOrder == sectionOrder) return;

    if (currentDockContext == DockContext::KeyEditor)
        keyEditorPositions[sectionIndex] = newPosition;
    else if (currentDockContext == DockContext::Default)
        defaultPositions[sectionIndex] = newPosition;
    else
        sectionPositions[sectionIndex] = newPosition;
    sectionOrder = std::move(updatedOrder);
    resized();
    repaint();
    if (onSectionLayoutChanged) onSectionLayoutChanged();
}

void InspectorComponent::mouseDown(const juce::MouseEvent& e)
{
    juce::Component::SafePointer<InspectorComponent> safeThis(this);
    if (e.mods.isPopupMenu())
    {
        for (int id = 0; id < kInspectorSectionCount; ++id)
        {
            const auto* group = groupForSection(id);
            if (e.eventComponent == group && group->isVisible() && e.y < 30)
            {
                showSectionMenu(id, e.getScreenPosition());
                return;
            }
        }

        auto posInNote = e.getEventRelativeTo(&grpNoteInfo).getPosition();
        if (grpNoteInfo.getLocalBounds().contains(posInNote))
        {
            NoteInspectorSettingsDialog::showModal([this, safeThis]() {
                if (safeThis.getComponent() == nullptr) return;
                applyCustomModuleSettings();
            });
            return;
        }

        auto posInChord = e.getEventRelativeTo(&grpChordDetection).getPosition();
        if (grpChordDetection.getLocalBounds().contains(posInChord))
        {
            ChordAnalysisSettingsDialog::showModal([this, safeThis]() {
                if (safeThis.getComponent() == nullptr) return;
                applyCustomModuleSettings();
            });
            return;
        }
    }

    if (!e.mods.isLeftButtonDown()) return;
    for (int id = 0; id < kInspectorSectionCount; ++id)
    {
        const auto* group = groupForSection(id);
        if (e.eventComponent == group && group->isVisible()
            && e.y < 30 && e.x < group->getWidth() - 8)
        {
            draggingSectionIndex = id;
            isDraggingSection = true;
            dragHasMoved = false;
            currentDropDock = nullptr;
            currentDropIndex = -1;
            return;
        }
    }
}

void InspectorComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (!isDraggingSection || draggingSectionIndex < 0) return;
    if (e.getDistanceFromDragStart() <= 5 && !dragHasMoved) return;
    dragHasMoved = true;

    InspectorComponent* target = nullptr;
    const auto screenPoint = e.getScreenPosition();
    if (getScreenBounds().contains(screenPoint))
        target = this;
    else if (peerDock != nullptr && peerDock->isShowing()
             && peerDock->getScreenBounds().contains(screenPoint))
        target = peerDock;

    if (currentDropDock != target)
    {
        if (currentDropDock != nullptr) currentDropDock->clearDropFeedback();
        currentDropDock = target;
    }
    currentDropIndex = -1;
    if (target == nullptr) return;

    const int contentY = target->getLocalPoint(nullptr, screenPoint).y
        + target->viewport.getViewPositionY();
    currentDropIndex = target->insertionIndexAt(contentY, draggingSectionIndex);
    const int lineY = target->insertionLineAt(currentDropIndex, draggingSectionIndex);
    if (target->dropIndicatorY != lineY)
    {
        target->dropIndicatorY = lineY;
        if (target->dropOverlay) target->dropOverlay->repaint();
    }
}

void InspectorComponent::mouseUp(const juce::MouseEvent&)
{
    if (!isDraggingSection) return;
    auto* target = currentDropDock;
    const int draggedId = draggingSectionIndex;
    const int index = currentDropIndex;
    if (target != nullptr) target->clearDropFeedback();
    isDraggingSection = false;
    dragHasMoved = false;
    draggingSectionIndex = -1;
    currentDropDock = nullptr;
    currentDropIndex = -1;
    if (target != nullptr && index >= 0)
        moveSectionToDock(draggedId, *target, index);
}

void InspectorComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        auto posInChord = e.getEventRelativeTo(&grpChordDetection).getPosition();
        if (grpChordDetection.getLocalBounds().contains(posInChord))
        {
            auto& state = AppStateManager::getInstance();
            int currentPt = state.chordAnalysisFontSizePt;
            int delta = (wheel.deltaY > 0 ? 1 : -1);
            int newPt = std::clamp(currentPt + delta, 10, 48);
            if (newPt != currentPt)
            {
                state.chordAnalysisFontSizePt = newPt;
                applyCustomModuleSettings();
                state.save();
            }
            return;
        }

        auto posInNote = e.getEventRelativeTo(&grpNoteInfo).getPosition();
        if (grpNoteInfo.getLocalBounds().contains(posInNote))
        {
            auto& state = AppStateManager::getInstance();
            int currentPt = state.notePropertyFontSizePt;
            int delta = (wheel.deltaY > 0 ? 1 : -1);
            int newPt = std::clamp(currentPt + delta, 9, 24);
            if (newPt != currentPt)
            {
                state.notePropertyFontSizePt = newPt;
                applyCustomModuleSettings();
                state.save();
            }
            return;
        }
    }

    viewport.mouseWheelMove(e, wheel);
}

} // namespace Harmonizer
