#pragma once

#include "../core/MidiModel.h"
#include "../core/TrackModel.h"
#include "../core/ScaleDefinitions.h"
#include "../core/Localization.h"
#include "../core/SectionLayout.h"
#include "../core/ChordEditing.h"
#include "../core/Reharmonizer.h"
#include "../audio/AudioEngine.h"
#include "EventEditors.h"
#include "ChannelStripSlider.h"
#include <juce_gui_basics/juce_gui_basics.h>

#include "CubaseTheme.h"
#include <array>
#include <functional>

namespace Harmonizer
{

struct AnalysisResult;

class NotePropertyRowComponent : public juce::Component
{
public:
    NotePropertyRowComponent()
    {
        lblTitle.setFont(CubaseTheme::getFont(11.0f));
        lblTitle.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
        lblTitle.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(lblTitle);

        lblMain.setFont(CubaseTheme::getFont(11.0f));
        lblMain.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
        lblMain.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(lblMain);

        lblBracket.setFont(CubaseTheme::getFont(11.0f));
        lblBracket.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
        lblBracket.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(lblBracket);
    }

    void setTitle(const juce::String& title)
    {
        currentTitle = title;
        lblTitle.setText(title, juce::dontSendNotification);
    }

    void setTextParts(const juce::String& mainText, const juce::String& bracketText)
    {
        currentMainText = mainText;
        currentBracketText = bracketText;
        lblMain.setText(compactLayout && bracketText.isNotEmpty()
                            ? mainText + " " + bracketText : mainText,
                        juce::dontSendNotification);
        lblBracket.setText(bracketText, juce::dontSendNotification);
    }

    void setContent(const juce::String& fullContent)
    {
        juce::String trimmed = fullContent.trim();
        if (trimmed.isEmpty() || trimmed == "--")
        {
            setTextParts("--", "");
            return;
        }

        if (trimmed.startsWithChar('(') && trimmed.endsWithChar(')'))
        {
            setTextParts(trimmed, "");
            return;
        }

        if (trimmed.contains(" / "))
        {
            auto parts = juce::StringArray::fromTokens(trimmed, "/", "");
            juce::String combinedMain;
            juce::String combinedBracket;
            for (int i = 0; i < parts.size(); ++i)
            {
                auto p = parts[i].trim();
                int openIdx = p.indexOfChar('(');
                int closeIdx = p.lastIndexOfChar(')');
                if (openIdx >= 0)
                {
                    auto m = p.substring(0, openIdx).trim();
                    auto b = (closeIdx > openIdx) ? p.substring(openIdx + 1, closeIdx).trim() : p.substring(openIdx + 1).trim();
                    if (combinedMain.isNotEmpty()) combinedMain << " / ";
                    combinedMain << m;
                    if (combinedBracket.isNotEmpty()) combinedBracket << " / ";
                    combinedBracket << b;
                }
                else
                {
                    if (combinedMain.isNotEmpty()) combinedMain << " / ";
                    combinedMain << p;
                }
            }
            if (combinedBracket.isNotEmpty())
                setTextParts(combinedMain, "(" + combinedBracket + ")");
            else
                setTextParts(combinedMain, "");
            return;
        }

        int openParen = trimmed.indexOfChar('(');
        if (openParen >= 0)
        {
            juce::String mainPart = trimmed.substring(0, openParen).trim();
            juce::String bracketPart = trimmed.substring(openParen).trim();
            setTextParts(mainPart, bracketPart);
        }
        else
        {
            setTextParts(trimmed, "");
        }
    }

    void setText(const juce::String& fullTextWithTitle, juce::NotificationType = juce::dontSendNotification)
    {
        int colonIdx = fullTextWithTitle.indexOfChar(':');
        if (colonIdx >= 0)
        {
            setTitle(fullTextWithTitle.substring(0, colonIdx + 1).trim());
            setContent(fullTextWithTitle.substring(colonIdx + 1).trim());
        }
        else
        {
            setContent(fullTextWithTitle.trim());
        }
    }

    void setColour(int colourId, juce::Colour colour)
    {
        lblMain.setColour(colourId, colour);
    }

    void setColours(juce::Colour titleCol, juce::Colour mainCol, juce::Colour bracketCol)
    {
        lblTitle.setColour(juce::Label::textColourId, titleCol);
        lblMain.setColour(juce::Label::textColourId, mainCol);
        lblBracket.setColour(juce::Label::textColourId, bracketCol);
    }

    void setFont(const juce::FontOptions& font)
    {
        currentFontPt = font.getHeight();
        auto f = CubaseTheme::getFont(currentFontPt);
        lblTitle.setFont(f);
        lblMain.setFont(f);
        lblBracket.setFont(f);
        resized();
    }

    int getDesiredTitleWidth(const juce::Font& font) const
    {
        if (currentTitle.isEmpty()) return 0;
        return (int)std::ceil(juce::GlyphArrangement::getStringWidth(font, currentTitle));
    }

    int getDesiredMainWidth(const juce::Font& font) const
    {
        if (currentMainText.isEmpty()) return 0;
        return (int)std::ceil(juce::GlyphArrangement::getStringWidth(font, currentMainText));
    }

    bool hasBracketContent() const
    {
        return currentBracketText.isNotEmpty();
    }

    void setColumnWidths(int titleWidth, int mainWidth)
    {
        fixedTitleW = titleWidth;
        fixedMainW = mainWidth;
        resized();
    }

    void setCompact(bool compact)
    {
        if (compactLayout == compact) return;
        compactLayout = compact;
        resized();
    }

    void resized() override
    {
        int titleW = fixedTitleW > 0 ? fixedTitleW : juce::roundToInt(currentFontPt * 4.2f + 4.0f);
        int mainW = fixedMainW > 0 ? fixedMainW : juce::roundToInt(currentFontPt * 6.0f + 6.0f);

        int h = getHeight();
        if (compactLayout)
        {
            const int lineH = h / 2;
            lblTitle.setBounds(0, 0, getWidth(), lineH);
            lblMain.setText(currentMainText + (currentBracketText.isNotEmpty()
                                             ? " " + currentBracketText : juce::String()),
                            juce::dontSendNotification);
            lblMain.setBounds(0, lineH, getWidth(), h - lineH);
            lblBracket.setBounds(0, 0, 0, 0);
        }
        else
        {
            lblMain.setText(currentMainText, juce::dontSendNotification);
            const int actualTitleW = std::min(titleW, std::max(0, getWidth() - 50));
            const int actualMainW = std::min(mainW, std::max(0, getWidth() - actualTitleW));
            lblTitle.setBounds(0, 0, actualTitleW, h);
            lblMain.setBounds(actualTitleW, 0, actualMainW, h);
            lblBracket.setBounds(actualTitleW + actualMainW, 0,
                                 std::max(0, getWidth() - actualTitleW - actualMainW), h);
        }
    }

private:
    float currentFontPt = 11.0f;
    bool compactLayout = false;
    int fixedTitleW = -1;
    int fixedMainW = -1;

    juce::String currentTitle;
    juce::String currentMainText;
    juce::String currentBracketText;

    juce::Label lblTitle;
    juce::Label lblMain;
    juce::Label lblBracket;
};

class SourceModeDisclosureButton : public juce::Button
{
public:
    SourceModeDisclosureButton() : juce::Button("sourceModeDisclosure") {}

    void setSummary(const juce::String& newSummary)
    {
        if (summary == newSummary) return;
        summary = newSummary;
        repaint();
    }

    void setExpanded(bool shouldExpand)
    {
        if (expanded == shouldExpand) return;
        expanded = shouldExpand;
        repaint();
    }

    void setStatus(const juce::String& newStatus)
    {
        if (status == newStatus) return;
        status = newStatus;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool isMouseOverButton,
                     bool isButtonDown) override;

private:
    juce::String summary, status;
    bool expanded = false;
};

class ReharmonizationModeButton : public juce::Button
{
public:
    enum class Availability { Unknown, Supported, Unavailable };

    ReharmonizationModeButton() : juce::Button("reharmonizationMode") {}

    std::function<void(bool)> onHoverChanged;

    void setAvailability(Availability newAvailability)
    {
        if (availability == newAvailability) return;
        availability = newAvailability;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool isMouseOverButton,
                     bool isButtonDown) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

private:
    Availability availability = Availability::Unknown;
};

class InspectorComponent : public juce::Component,
                           public MidiModel::Listener,
                           public TrackModel::Listener,
                           public VstHostManager::Listener,
                           public Localization::Listener,
                           public CubaseTheme::Listener
{
public:
    enum class DockSide { Left, Right, Both };
    enum class DockContext { Arranger, KeyEditor, Default };

    InspectorComponent(MidiModel& model, TrackModel& trackModel, AudioEngine& engine,
                       juce::UndoManager& undoManager, std::function<void()> onOpenPrefs);
    ~InspectorComponent() override;

    void themeChanged() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // TrackModel Listener
    void tracksChanged() override          { updateTrackContext(); updateModePossibilities(); updateReharmonizationPreview(); refreshChordEditingPreview(); }
    void activeClipChanged() override      { updateTrackContext(); updateModePossibilities(); updateReharmonizationPreview(); refreshChordEditingPreview(); }
    void modesChanged() override           { updateNoteInfo(); updateReharmonizationPreview(); refreshChordEditingPreview(); }
    void markersChanged() override         { updateMarkerTrackControls(); }

    // Listeners
    void noteListChanged() override        { updateNoteInfo(); updateModePossibilities(); updateReharmonizationPreview(); refreshChordEditingPreview(); }
    void notePreviewChanged() override     {}
    void selectionChanged() override       { updateNoteInfo(); updateModePossibilities(); updateReharmonizationPreview(); refreshChordEditingPreview(); }
    void selectionPreviewChanged() override {}
    void projectSettingsChanged() override { updateScaleAndKeyControls(); updateNoteInfo(); updateReharmonizationPreview(); refreshModeBorrowingSettings(); }
    void loadedPluginChanged(const juce::String&) override { updateVstInfo(); }
    void pluginListChanged() override      { updateVstCombo(); }
    void languageChanged() override;

    void setDockFilter(DockSide side) {
        dockFilter = side;
        resized();
        repaint();
    }
    DockSide getDockFilter() const { return dockFilter; }
    void setPeerDock(InspectorComponent* peer) { peerDock = peer; }

    void setDockContext(DockContext ctx) {
        if (currentDockContext != ctx) {
            currentDockContext = ctx;
            resized();
            repaint();
        }
    }
    DockContext getDockContext() const { return currentDockContext; }

    std::function<void()> onSectionLayoutChanged;
    std::function<void()> onOpenSectionsManager;
    std::function<void(DockSide)> onEnsureDockVisible;
    std::function<void()> onSecondaryDominantRequested;
    std::function<void()> onSecondaryDominantSettingsChanged;
    std::function<void()> onReharmonizationRequested;
    std::function<void()> onReharmonizationSettingsChanged;
    std::function<void(ChordShapeQuality)> onChordQualityRequested;
    std::function<void(ChordShapeVoicing)> onChordVoicingRequested;
    std::function<void(ChordStackDirection)> onChordStackRequested;
    std::function<void(int chordTypeId)> onQuickChordRequested;
    std::function<void()> onQuickChordSettingsChanged;
    std::function<void()> onModeBorrowingRequested;
    std::function<void()> onModeBorrowingSettingsChanged;
    void refreshSecondaryDominantSettings();
    void refreshReharmonizationSettings();
    void refreshChordEditingPreview();
    void refreshModeBorrowingSettings();
    void refreshQuickChordMode();
    void setSecondaryDominantSettingsExpanded(bool expanded);
    void focusSecondaryDominantSettings();

    SectionDockPosition getSectionPosition(int index) const {
        if (isInspectorSectionId(index)) {
            if (currentDockContext == DockContext::KeyEditor) return keyEditorPositions[index];
            if (currentDockContext == DockContext::Default) return defaultPositions[index];
            return sectionPositions[index];
        }
        return SectionDockPosition::Left;
    }
    void setSectionPosition(int index, SectionDockPosition pos) {
        if (isInspectorSectionId(index)) {
            if (currentDockContext == DockContext::KeyEditor) keyEditorPositions[index] = pos;
            else if (currentDockContext == DockContext::Default) defaultPositions[index] = pos;
            else sectionPositions[index] = pos;
            resized();
            repaint();
            if (onSectionLayoutChanged && !isInternalUpdating) {
                juce::ScopedValueSetter<bool> svs(isInternalUpdating, true);
                onSectionLayoutChanged();
            }
        }
    }
    SectionDockPosition getContextSectionPosition(DockContext ctx, int index) const {
        if (isInspectorSectionId(index)) {
            if (ctx == DockContext::KeyEditor) return keyEditorPositions[index];
            if (ctx == DockContext::Default) return defaultPositions[index];
            return sectionPositions[index];
        }
        return SectionDockPosition::Left;
    }
    void setContextSectionPosition(DockContext ctx, int index, SectionDockPosition pos) {
        if (isInspectorSectionId(index)) {
            if (ctx == DockContext::KeyEditor) keyEditorPositions[index] = pos;
            else if (ctx == DockContext::Default) defaultPositions[index] = pos;
            else sectionPositions[index] = pos;
            resized();
            repaint();
            if (onSectionLayoutChanged && !isInternalUpdating) {
                juce::ScopedValueSetter<bool> svs(isInternalUpdating, true);
                onSectionLayoutChanged();
            }
        }
    }

    bool isSectionVisibleInThisDock(int sectionIndex) const {
        if (!isInspectorSectionId(sectionIndex)) return false;
        auto pos = getSectionPosition(sectionIndex);
        if (pos == SectionDockPosition::Hidden) return false;
        if (dockFilter == DockSide::Both) return true;
        if (dockFilter == DockSide::Left) return pos == SectionDockPosition::Left;
        if (dockFilter == DockSide::Right) return pos == SectionDockPosition::Right;
        return false;
    }

    bool hasVisibleSections() const {
        if (currentDockContext == DockContext::KeyEditor) {
            for (int i : kNoteTrackSectionIds) {
                if (isSectionVisibleInThisDock(i))
                    return true;
            }
            return false;
        }

        int trackId = trackModel.getActiveTrackId();
        if (auto* trk = trackModel.getTrackById(trackId)) {
            if (trk->getType() == TrackType::Chord) return isSectionVisibleInThisDock(5);
            if (trk->getType() == TrackType::Mode) return isSectionVisibleInThisDock(6);
            if (trk->getType() == TrackType::Marker) return isSectionVisibleInThisDock(7);
            if (trk->getType() == TrackType::TimeSignature) return isSectionVisibleInThisDock(8);
            if (trk->getType() == TrackType::Folder) return isSectionVisibleInThisDock(9);
            if (trk->getType() == TrackType::Audio) return isSectionVisibleInThisDock(kAudioTrackSectionId);
        }
        for (int i : kNoteTrackSectionIds) {
            if (isSectionVisibleInThisDock(i))
                return true;
        }
        return false;
    }

    bool isSectionVisible(int sectionIndex) const {
        if (isInspectorSectionId(sectionIndex))
            return getSectionPosition(sectionIndex) != SectionDockPosition::Hidden;
        return false;
    }
    void setSectionVisible(int sectionIndex, bool visible) {
        if (isInspectorSectionId(sectionIndex)) {
            if (!visible)
                setSectionPosition(sectionIndex, SectionDockPosition::Hidden);
            else if (getSectionPosition(sectionIndex) == SectionDockPosition::Hidden)
                setSectionPosition(sectionIndex, SectionDockPosition::Left);
        }
    }

    const std::vector<int>& getSectionOrder() const { return sectionOrder; }
    void setSectionOrder(const std::vector<int>& order) {
        if (order.size() != kInspectorSectionCount) return;
        std::array<bool, kInspectorSectionCount> seen{};
        for (int id : order) {
            if (!isInspectorSectionId(id) || seen[(size_t)id]) return;
            seen[(size_t)id] = true;
        }
        sectionOrder = order;
        resized();
        repaint();
    }
    bool isDockedOnRight() const { return dockFilter == DockSide::Right || dockOnRight; }
    void setDockedOnRight(bool onRight) {
        dockOnRight = onRight;
        dockFilter = onRight ? DockSide::Right : DockSide::Left;
        resized();
        repaint();
    }

    void updateTrackContext();
    void updateMarkerTrackControls();
    void applyCustomModuleSettings();
    void alignNotePropertyRows();

private:
    MidiModel& midiModel;
    TrackModel& trackModel;
    AudioEngine& audioEngine;
    juce::UndoManager& undoManager;
    std::function<void()> openPrefsCallback;

    bool dockOnRight = false;
    DockSide dockFilter = DockSide::Both;
    DockContext currentDockContext = DockContext::Default;
    SectionDockPosition sectionPositions[kInspectorSectionCount] = {
        SectionDockPosition::Left,   // 0. Note Inspector
        SectionDockPosition::Left,   // 1. Scale & Mode Analysis
        SectionDockPosition::Left,   // 2. Real-time Chord Analysis
        SectionDockPosition::Left,   // 3. Instrument Strip
        SectionDockPosition::Right,  // 4. Secondary Dominant
        SectionDockPosition::Left,   // 5. Chord Track
        SectionDockPosition::Left,   // 6. Mode Track
        SectionDockPosition::Left,   // 7. Marker Track
        SectionDockPosition::Left,   // 8. Time Signature Track
        SectionDockPosition::Left,   // 9. Folder Track
        SectionDockPosition::Right,  // 10. Reharmonization
        SectionDockPosition::Left,   // 11. Audio Track
        SectionDockPosition::Right,  // 12. Chord shaping
        SectionDockPosition::Right,  // 13. Mode borrowing
        SectionDockPosition::Right   // 14. Quick chord
    };
    SectionDockPosition keyEditorPositions[kInspectorSectionCount] = {
        SectionDockPosition::Left,   // 0. Note Inspector
        SectionDockPosition::Right,  // 1. Scale & Mode Analysis
        SectionDockPosition::Right,  // 2. Real-time Chord Analysis
        SectionDockPosition::Hidden, // 3. Instrument Strip
        SectionDockPosition::Right,  // 4. Secondary Dominant
        SectionDockPosition::Hidden, // 5. Chord Track
        SectionDockPosition::Hidden, // 6. Mode Track
        SectionDockPosition::Hidden, // 7. Marker Track
        SectionDockPosition::Hidden, // 8. Time Signature Track
        SectionDockPosition::Hidden, // 9. Folder Track
        SectionDockPosition::Right,  // 10. Reharmonization
        SectionDockPosition::Hidden, // 11. Audio Track
        SectionDockPosition::Right,  // 12. Chord shaping
        SectionDockPosition::Right,  // 13. Mode borrowing
        SectionDockPosition::Right   // 14. Quick chord
    };
    SectionDockPosition defaultPositions[kInspectorSectionCount] = {
        SectionDockPosition::Left,   // 0. Note Inspector
        SectionDockPosition::Left,   // 1. Scale & Mode Analysis
        SectionDockPosition::Left,   // 2. Real-time Chord Analysis
        SectionDockPosition::Left,   // 3. Instrument Strip
        SectionDockPosition::Right,  // 4. Secondary Dominant
        SectionDockPosition::Left,   // 5. Chord Track
        SectionDockPosition::Left,   // 6. Mode Track
        SectionDockPosition::Left,   // 7. Marker Track
        SectionDockPosition::Left,   // 8. Time Signature Track
        SectionDockPosition::Left,   // 9. Folder Track
        SectionDockPosition::Right,  // 10. Reharmonization
        SectionDockPosition::Left,   // 11. Audio Track
        SectionDockPosition::Right,  // 12. Chord shaping
        SectionDockPosition::Right,  // 13. Mode borrowing
        SectionDockPosition::Right   // 14. Quick chord
    };
    std::vector<int> sectionOrder = std::vector<int>(kDefaultSectionOrder.begin(), kDefaultSectionOrder.end());

    void showSectionMenu(int sectionIndex, juce::Point<int> screenPosition);

    // Drag-to-reorder state
    InspectorComponent* peerDock = nullptr;
    int draggingSectionIndex = -1;
    bool isDraggingSection = false;
    bool dragHasMoved = false;
    InspectorComponent* currentDropDock = nullptr;
    int currentDropIndex = -1;
    int dropIndicatorY = -1;

    std::vector<int> visibleSectionIdsExcluding(int excludedSection) const;
    int insertionIndexAt(int contentY, int excludedSection) const;
    int insertionLineAt(int insertionIndex, int excludedSection) const;
    const juce::GroupComponent* groupForSection(int sectionIndex) const;
    void clearDropFeedback();
    void moveSectionToDock(int sectionIndex, InspectorComponent& destination, int insertionIndex);

    juce::Viewport viewport;
    juce::Component contentComp;
    class DockDropOverlay;
    std::unique_ptr<DockDropOverlay> dropOverlay;
    juce::Label lblEmptyDock;
    juce::TextButton btnEmptyDockManage;

    // 1. Note Details Group
    juce::GroupComponent grpNoteInfo { "grpNote", "Note Inspector" };
    juce::Label lblSelectionCount;
    NotePropertyRowComponent lblNotePitch;
    NotePropertyRowComponent lblNoteStart;
    NotePropertyRowComponent lblNoteLength;
    NotePropertyRowComponent lblNoteVelocity;
    NotePropertyRowComponent lblNoteScaleDegree;
    NotePropertyRowComponent lblNoteFunctionalRole;
    NotePropertyRowComponent lblNoteMotionTendency;
    NotePropertyRowComponent lblNoteColorUsage;
    NotePropertyRowComponent lblNoteInterval;

    // 2. Scale & Mode Analysis Group (Integrated from Mode Annotation project)
    juce::GroupComponent grpScaleMode { "grpScaleMode", "Mode Colour" };
    juce::ComboBox keyRootCombo;
    juce::ComboBox scaleTypeCombo;
    juce::TextButton btnApplyCurrentMode { "Apply Mode" };
    juce::TextButton btnCustomScale { juce::String::fromUTF8(u8"+ 自定义音阶...") };
    juce::TextButton btnAnalyzeMode { "Analyze Track" };
    juce::Label lblModeAnalysisConfidence;

    // 2b. Real-time Chord Detection Group
    juce::GroupComponent grpChordDetection { "grpChordDet", "Chord Analysis" };
    juce::Label lblDetectedChord;
    juce::Label lblRomanNumeral;

    // 3. Instrument Group & Channel Strip
    juce::GroupComponent grpInstrument { "grpInst", "Instrument / Channel Strip" };
    juce::ComboBox vstPluginCombo;
    juce::TextButton btnOpenEditor { "Open VST GUI (Kontakt)" };
    ChannelStripSlider volSlider { ChannelSliderKind::Volume };
    juce::Label lblVol;
    ChannelStripSlider panSlider { ChannelSliderKind::Pan };
    juce::Label lblPan;

    // Audio Track module
    juce::GroupComponent grpAudioTrack { "grpAudioTrack", "Audio Track" };
    juce::Label lblAudioStatus;
    juce::TextButton btnImportAudio { "Import Audio..." };
    juce::Label lblAudioVolume;
    ChannelStripSlider audioVolumeSlider { ChannelSliderKind::Volume };
    juce::Label lblAudioPan;
    ChannelStripSlider audioPanSlider { ChannelSliderKind::Pan };
    juce::TextButton btnAudioMute { "Mute" };
    juce::TextButton btnAudioSolo { "Solo" };

    // 4. Phase 2 Reharm Operators Group
    juce::GroupComponent grpReharm { "grpReharm", "Secondary Dominant" };
    juce::TextButton btnSecondaryDominant { "Secondary Dominant" };
    juce::TextButton btnReharmOptions { "Harmonic Settings" };
    juce::Label lblSecondaryTarget;
    juce::ComboBox secondaryTargetCombo;
    juce::Label lblSecondaryManualRoot;
    juce::ComboBox secondaryManualRootCombo;
    juce::Label lblSecondaryType;
    juce::ComboBox secondaryTypeCombo;
    juce::Label lblSecondaryVoicing;
    juce::ComboBox secondaryVoicingCombo;
    bool secondarySettingsExpanded = false;

    // 10. Selected-chord reharmonization (kept separate from secondary dominants).
    juce::GroupComponent grpReharmonization { "grpReharmonization", "Reharmonization" };
    std::array<ReharmonizationModeButton, kReharmonizationAlgorithmCount> reharmonizationModeButtons;
    juce::TextEditor txtReharmonizationPreview;
    std::array<ReharmonizationResult, kReharmonizationAlgorithmCount> reharmonizationPlans;
    std::array<juce::String, kReharmonizationAlgorithmCount> reharmonizationPreviewTexts;
    int hoveredReharmonizationMode = -1;

    juce::GroupComponent grpChordShape { "grpChordShape", "Chord Shape" };
    juce::Label lblChordShapeStatus;
    juce::TextButton btnShapeMajor { "Maj" }, btnShapeMinor { "Min" };
    juce::TextButton btnShapeDiminished { "dim" }, btnShapeAugmented { "aug" };
    juce::TextButton btnShapeSus2 { "Sus2" }, btnShapeSus4 { "Sus4" };
    juce::TextButton btnShapeDominant7 { "7" }, btnShapeMajor7 { "maj7" };
    juce::TextButton btnShapeMinor7 { "m7" }, btnShapeHalfDiminished7 { "m7b5" };
    juce::TextButton btnVoicingExpand { "Expand" }, btnVoicingContract { "Contract" };
    juce::TextButton btnVoicingDrop2 { "Drop-2" };
    std::array<juce::TextButton, 8> stackButtons;

    juce::GroupComponent grpQuickChord { "grpQuickChord", "Quick Chord" };
    std::array<juce::Label, 6> quickChordCategoryLabels;
    struct QuickChordButton
    {
        int id = -1;
        std::unique_ptr<juce::TextButton> button;
    };
    std::vector<QuickChordButton> quickChordButtons;
    int quickChordModuleHeight(int contentWidth) const;
    void layoutQuickChordModule();

    juce::GroupComponent grpModeBorrowing { "grpModeBorrowing", "Mode Borrowing" };
    SourceModeDisclosureButton btnBorrowingSourceDisclosure;
    juce::TextButton btnBorrowingSourceAuto, btnBorrowingSourceCustom;
    juce::Label lblBorrowingTarget;
    bool borrowingSourceSettingsExpanded = false;
    juce::ComboBox borrowingStyleCombo, borrowingSourceRootCombo, borrowingSourceScaleCombo;
    juce::ComboBox borrowingTargetRootCombo, borrowingScaleCombo, borrowingDegreeCombo;
    juce::TextEditor txtBorrowingPreview;
    juce::TextButton btnApplyBorrowing { "Apply" };

    // 5. Chord Track Inspector (Visible when active track is Chord Track)
    juce::GroupComponent grpChordTrack { "grpChordTrk", "Chord Track Inspector" };
    juce::TextButton btnExtractChordsFromMidi { "Auto-Extract Chords From MIDI" };
    juce::Label lblChordDisplayMode;
    juce::ComboBox chordDisplayModeCombo;
    juce::Label lblChordTrackInfo;

    // 6. Mode Track Inspector (Visible when active track is Mode Track)
    juce::GroupComponent grpModeTrack { "grpModeTrk", "Mode Track Inspector" };
    juce::TextButton btnAnalyzeAndPopulateModes { "Analyze & Populate Modes" };
    juce::ToggleButton btnModeTrackOverride { juce::String::fromUTF8(u8"启用调式轨全局覆盖 (Mode Track Override)") };
    juce::Label lblModeTrackInfo;

    // 7. Marker Track Inspector (Visible when active track is Marker Track)
    juce::GroupComponent grpMarkerTrack { "grpMarkerTrk", "Marker Inspector (标记轨列表与属性)" };
    juce::Label lblMarkerTrackInfo;
    juce::ListBox markerListBox;
    class MarkerListModel : public juce::ListBoxModel
    {
    public:
        MarkerListModel(TrackModel& tm, std::function<void(int)> onSel) : trackModel(tm), onSelect(std::move(onSel)) {}
        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void selectedRowsChanged(int lastRowSelected) override;
    private:
        TrackModel& trackModel;
        std::function<void(int)> onSelect;
    };
    std::unique_ptr<MarkerListModel> markerListModel;

    juce::Label lblMarkerNameTitle;
    juce::TextEditor txtMarkerName;
    juce::Label lblMarkerStartTitle;
    juce::TextEditor txtMarkerStart;
    juce::Label lblMarkerLengthTitle;
    juce::TextEditor txtMarkerLength;
    juce::Label lblMarkerColorTitle;
    juce::ComboBox markerColorCombo;
    juce::TextButton btnJumpToMarker { "Jump to Marker (跳转播放指针)" };
    juce::TextButton btnAddMarkerAtPlayhead { "+ Add Marker at Playhead" };
    juce::TextButton btnDeleteMarker { "Delete Marker" };
    int selectedMarkerIndex = -1;

    void updateMarkerPropertyFields();

    // 8. Time Signature Track Inspector (Visible when active track is TimeSig Track)
    juce::GroupComponent grpTimeSigTrack { "grpTimeSigTrk", "拍号轨属性 (Time Signature)" };
    juce::Label lblTimeSigInfo;

    // 9. Folder Track Inspector (Visible when active track is Folder Track)
    juce::GroupComponent grpFolderTrack { "grpFolderTrk", "文件夹轨属性 (Folder Track)" };
    juce::Label lblFolderTrackSummary;
    juce::Label lblFolderNotesTitle;
    juce::TextEditor txtFolderNotes;

    struct CandidateCache
    {
        juce::String tonic;
        int tonicPc = 0;
        juce::String scaleName;
    };
    std::vector<CandidateCache> cachedCandidates;
    enum class ModeGrouping { Tonic, ScaleType };
    struct CandidateDisplayRow
    {
        int candidateIndex = -1; // -1 marks a group heading.
        juce::String heading;
        int groupCandidateIndex = -1;
        int groupCount = 0;
    };
    ModeGrouping modeGrouping = ModeGrouping::Tonic;
    std::vector<CandidateDisplayRow> modeCandidateRows;
    class ModeCandidateListModel : public juce::ListBoxModel
    {
    public:
        explicit ModeCandidateListModel(InspectorComponent& inspector) : owner(inspector) {}
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
        void selectedRowsChanged(int row) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    private:
        InspectorComponent& owner;
    };
    ModeCandidateListModel modeCandidateListModel { *this };
    juce::ListBox modeCandidatesList;
    juce::TextButton btnGroupByTonic { "By tonic" };
    juce::TextButton btnGroupByType { "By mode type" };
    juce::TextButton btnApplyCandidate { "Apply Selected Mode" };
    juce::TextButton btnApplyCandidateToRange { "Apply Selection to Mode Track" };
    bool showingSelectionResults = false;

    void updateNoteInfo();
    void updateScaleAndKeyControls();
    void updateVstCombo();
    void updateVstSelection();
    void updateVstInfo();
    void updateChannelStrip();
    void extractChordsFromMidiTrack();
    void analyzeAndPopulateModeTrack();
    juce::String analyzeChord(const std::vector<NoteEvent>& sel, juce::String& outRoman);
    void updateChordAnalysis(const std::vector<NoteEvent>& selected);
    void runModeRecommendation();
    void updateModePossibilities();
    void showModeCandidates(const AnalysisResult& result, bool fromSelection);
    void rebuildModeCandidateRows(int candidateToSelect = -1);
    int candidateIndexForRow(int row) const;
    void applyCurrentMode();
    void applyModeCandidate(int index);
    void applyModeChoice(int tonicPc, const juce::String& scaleName,
                         bool includeCandidateFallback);
    void applyModeCandidateToSelectedRange(int index);
    void updateReharmonizationPreview();
    void showReharmonizationPreview();
    void openCustomScaleDialog();

    bool isInternalUpdating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InspectorComponent)
};

} // namespace Harmonizer
