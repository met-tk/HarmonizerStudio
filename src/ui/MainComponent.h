#pragma once

#include "../core/MidiModel.h"
#include "../core/TrackModel.h"
#include "../core/KeyCommandManager.h"
#include "../core/ProjectFile.h"
#include "../core/UndoActions.h"
#include "../core/AppStateManager.h"
#include "../audio/AudioEngine.h"
#include "CubaseTheme.h"
#include "ToolbarComponent.h"
#include "TransportBar.h"
#include "InspectorComponent.h"
#include "PianoRollComponent.h"
#include "TrackArrangerComponent.h"
#include "PreferencesDialog.h"
#include "EventEditors.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>

namespace Harmonizer
{

enum class ViewLayout
{
    Split = 0,        // Top: Track Arranger, Bottom: Key Editor
    ArrangerOnly = 1, // Full screen Arranger
    EditorOnly = 2,   // Full screen Key Editor
    DualWindow = 3    // Main: Arranger, Detached Window: Key Editor
};

class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      public juce::FileDragAndDropTarget,
                      public Localization::Listener,
                      public CubaseTheme::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    void themeChanged() override;

    // Draggable Vertical Splitter between Inspector Zones and Center View
    class VerticalSplitterBar : public juce::Component
    {
    public:
        VerticalSplitterBar(std::function<void(int deltaX)> onDrag) : dragCallback(std::move(onDrag))
        {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        }
        void mouseEnter(const juce::MouseEvent&) override { repaint(); }
        void mouseExit(const juce::MouseEvent&) override { repaint(); }
        void paint(juce::Graphics& g) override
        {
            bool active = isMouseOverOrDragging();
            g.setColour(active ? CubaseTheme::bgSurface() : CubaseTheme::bgPanel());
            g.fillAll();

            g.setColour(active ? CubaseTheme::accentCyan() : CubaseTheme::borderSubtle());
            g.drawVerticalLine(0, 0.0f, (float)getHeight());
            g.drawVerticalLine(getWidth() - 1, 0.0f, (float)getHeight());

            // Center grip handle
            g.setColour(active ? CubaseTheme::accentCyan() : CubaseTheme::borderBright());
            int cx = getWidth() / 2;
            int cy = getHeight() / 2;
            g.fillRoundedRectangle((float)(cx - 1), (float)(cy - 24), 2.0f, 48.0f, 1.0f);
        }
        void mouseDown(const juce::MouseEvent& e) override
        {
            initialX = e.getScreenX();
            repaint();
        }
        void mouseDrag(const juce::MouseEvent& e) override
        {
            int currentX = e.getScreenX();
            int deltaX = currentX - initialX;
            if (deltaX != 0 && dragCallback)
            {
                dragCallback(deltaX);
                initialX = currentX;
            }
        }
        void mouseUp(const juce::MouseEvent&) override
        {
            repaint();
        }
    private:
        std::function<void(int)> dragCallback;
        int initialX = 0;
    };

    // Detached Key Editor Window class
    class KeyEditorWindow : public juce::DocumentWindow
    {
    public:
        class DetachedContainer : public juce::Component,
                                  public MidiModel::Listener,
                                  public TrackModel::Listener,
                                  public AudioEngine::PlayheadListener,
                                  public Localization::Listener,
                                  public CubaseTheme::Listener
        {
        public:
            DetachedContainer(MainComponent& mainComp, PianoRollComponent* roll)
                : owner(mainComp), pianoRollComp(roll)
            {
                owner.midiModel.addListener(this);
                owner.trackModel.addListener(this);
                owner.audioEngine.addPlayheadListener(this);
                Localization::getInstance().addListener(this);
                CubaseTheme::addThemeListener(this);

                // Center Piano Roll
                if (pianoRollComp)
                    addAndMakeVisible(pianoRollComp);

                // Bottom Mini Transport (Standard Vector DAW Icon Buttons)
                btnRewind.onClick = [this]() {
                    owner.audioEngine.stop();
                    owner.audioEngine.setPlayheadBeat(0.0);
                };
                addAndMakeVisible(btnRewind);

                btnPlay.onClick = [this]() {
                    if (owner.audioEngine.isPlaying())
                    {
                        owner.audioEngine.pause();
                    }
                    else
                    {
                        owner.audioEngine.play();
                    }
                    updatePlayButtonState(owner.audioEngine.isPlaying());
                };
                updatePlayButtonState(owner.audioEngine.isPlaying());
                addAndMakeVisible(btnPlay);

                btnStop.onClick = [this]() {
                    owner.audioEngine.stop();
                    updatePlayButtonState(false);
                };
                addAndMakeVisible(btnStop);

                btnLoop.setClickingTogglesState(true);
                btnLoop.setToggleState(owner.midiModel.isLoopEnabled(), juce::dontSendNotification);
                btnLoop.onClick = [this]() { owner.midiModel.setLoopEnabled(btnLoop.getToggleState()); };
                addAndMakeVisible(btnLoop);

                lblBpm.setText(juce::String(owner.midiModel.getBpm(), 1) + " BPM", juce::dontSendNotification);
                lblBpm.setColour(juce::Label::backgroundColourId, CubaseTheme::bgDeep());
                lblBpm.setColour(juce::Label::textColourId, CubaseTheme::accentOrange());
                lblBpm.setColour(juce::Label::outlineColourId, CubaseTheme::borderSubtle());
                lblBpm.setJustificationType(juce::Justification::centred);
                lblBpm.setEditable(true);
                lblBpm.onTextChange = [this]() {
                    double newBpm = lblBpm.getText().getDoubleValue();
                    if (newBpm >= 20.0 && newBpm <= 400.0)
                        owner.midiModel.setBpm(newBpm);
                };
                lblBpm.addMouseListener(this, false);
                addAndMakeVisible(lblBpm);

                lblTimeSig.setText(juce::String(owner.midiModel.getTimeSigNumerator()) + "/" + juce::String(owner.midiModel.getTimeSigDenominator()), juce::dontSendNotification);
                lblTimeSig.setColour(juce::Label::backgroundColourId, CubaseTheme::bgDeep());
                lblTimeSig.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
                lblTimeSig.setColour(juce::Label::outlineColourId, CubaseTheme::borderSubtle());
                lblTimeSig.setJustificationType(juce::Justification::centred);
                lblTimeSig.addMouseListener(this, false);
                addAndMakeVisible(lblTimeSig);

                // Docks for Floating Key Editor Window
                leftInspector = std::make_unique<InspectorComponent>(
                    owner.midiModel, owner.trackModel, owner.audioEngine, owner.undoManager,
                    [this]() { owner.openPreferences(); }
                );
                leftInspector->setDockFilter(InspectorComponent::DockSide::Left);
                leftInspector->onSecondaryDominantRequested = [this]() { owner.startSecondaryDominantPlacement(); };
                leftInspector->onQuickChordRequested = [this](int id) { owner.startQuickChord(id); };
                leftInspector->onQuickChordSettingsChanged = [this]() { owner.refreshQuickChordMode(); };
                leftInspector->onSecondaryDominantSettingsChanged = [this]() { owner.refreshSecondaryDominantSettings(); };
                leftInspector->onReharmonizationRequested = [this]() { owner.applyReharmonization(); };
                leftInspector->onReharmonizationSettingsChanged = [this]() { owner.refreshReharmonizationSettings(); };
                leftInspector->onChordQualityRequested = [this](ChordShapeQuality q) { owner.applyChordQuality(q); };
                leftInspector->onChordVoicingRequested = [this](ChordShapeVoicing v) { owner.applyChordVoicing(v); };
                leftInspector->onChordStackRequested = [this](ChordStackDirection d) { owner.applyChordStack(d); };
                leftInspector->onModeBorrowingRequested = [this]() { owner.applyModeBorrowing(); };
                leftInspector->onModeBorrowingSettingsChanged = [this]() { owner.refreshModeBorrowingSettings(); };
                leftInspector->onOpenSectionsManager = [this]() { owner.showSectionsManager(0, true); };
                leftInspector->onEnsureDockVisible = [this](InspectorComponent::DockSide side) {
                    if (side == InspectorComponent::DockSide::Left && !isLeftVisible) toggleDetachedLeftZone();
                    if (side == InspectorComponent::DockSide::Right && !isRightVisible) toggleDetachedRightZone();
                };
                leftInspector->setDockContext(InspectorComponent::DockContext::KeyEditor);

                rightInspector = std::make_unique<InspectorComponent>(
                    owner.midiModel, owner.trackModel, owner.audioEngine, owner.undoManager,
                    [this]() { owner.openPreferences(); }
                );
                rightInspector->setDockFilter(InspectorComponent::DockSide::Right);
                rightInspector->onSecondaryDominantRequested = [this]() { owner.startSecondaryDominantPlacement(); };
                rightInspector->onQuickChordRequested = [this](int id) { owner.startQuickChord(id); };
                rightInspector->onQuickChordSettingsChanged = [this]() { owner.refreshQuickChordMode(); };
                rightInspector->onSecondaryDominantSettingsChanged = [this]() { owner.refreshSecondaryDominantSettings(); };
                rightInspector->onReharmonizationRequested = [this]() { owner.applyReharmonization(); };
                rightInspector->onReharmonizationSettingsChanged = [this]() { owner.refreshReharmonizationSettings(); };
                rightInspector->onChordQualityRequested = [this](ChordShapeQuality q) { owner.applyChordQuality(q); };
                rightInspector->onChordVoicingRequested = [this](ChordShapeVoicing v) { owner.applyChordVoicing(v); };
                rightInspector->onChordStackRequested = [this](ChordStackDirection d) { owner.applyChordStack(d); };
                rightInspector->onModeBorrowingRequested = [this]() { owner.applyModeBorrowing(); };
                rightInspector->onModeBorrowingSettingsChanged = [this]() { owner.refreshModeBorrowingSettings(); };
                rightInspector->onOpenSectionsManager = [this]() { owner.showSectionsManager(0, true); };
                rightInspector->onEnsureDockVisible = [this](InspectorComponent::DockSide side) {
                    if (side == InspectorComponent::DockSide::Left && !isLeftVisible) toggleDetachedLeftZone();
                    if (side == InspectorComponent::DockSide::Right && !isRightVisible) toggleDetachedRightZone();
                };
                leftInspector->setPeerDock(rightInspector.get());
                rightInspector->setPeerDock(leftInspector.get());
                rightInspector->setDockContext(InspectorComponent::DockContext::KeyEditor);

                // Sync initial section positions and order from owner
                syncFromOwner();

                // Setup bidirectional section layout change callbacks
                leftInspector->onSectionLayoutChanged = [this]() {
                    if (owner.isSyncingSections) return;
                    juce::ScopedValueSetter<bool> svs(owner.isSyncingSections, true);
                    if (leftInspector && rightInspector)
                    {
                        for (int i = 0; i < kInspectorSectionCount; ++i)
                        {
                            auto pos = leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i);
                            rightInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i, pos);
                            if (owner.leftInspector)
                                owner.leftInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i, pos);
                            if (owner.rightInspector)
                                owner.rightInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i, pos);
                        }
                        auto order = leftInspector->getSectionOrder();
                        rightInspector->setSectionOrder(order);
                        if (owner.leftInspector) owner.leftInspector->setSectionOrder(order);
                        if (owner.rightInspector) owner.rightInspector->setSectionOrder(order);
                    }
                    updateZoneVisibility();
                    owner.resized();

                    if (owner.leftInspector)
                    {
                        auto& st = AppStateManager::getInstance();
                        st.inspectorStateValid = true;
                        for (int i = 0; i < kInspectorSectionCount; ++i)
                        {
                            st.sectionPositions[i][0] = (int)owner.leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger,  i);
                            st.sectionPositions[i][1] = (int)owner.leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i);
                            st.sectionPositions[i][2] = (int)owner.leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Default,   i);
                        }
                        const auto& order = owner.leftInspector->getSectionOrder();
                        for (int i = 0; i < std::min((int)order.size(), kInspectorSectionCount); ++i)
                            st.sectionOrder[i] = order[i];
                        st.save();
                    }
                };

                rightInspector->onSectionLayoutChanged = [this]() {
                    if (owner.isSyncingSections) return;
                    juce::ScopedValueSetter<bool> svs(owner.isSyncingSections, true);
                    if (leftInspector && rightInspector)
                    {
                        for (int i = 0; i < kInspectorSectionCount; ++i)
                        {
                            auto pos = rightInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i);
                            leftInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i, pos);
                            if (owner.leftInspector)
                                owner.leftInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i, pos);
                            if (owner.rightInspector)
                                owner.rightInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i, pos);
                        }
                        auto order = rightInspector->getSectionOrder();
                        leftInspector->setSectionOrder(order);
                        if (owner.leftInspector) owner.leftInspector->setSectionOrder(order);
                        if (owner.rightInspector) owner.rightInspector->setSectionOrder(order);
                    }
                    updateZoneVisibility();
                    owner.resized();

                    if (owner.leftInspector)
                    {
                        auto& st = AppStateManager::getInstance();
                        st.inspectorStateValid = true;
                        for (int i = 0; i < kInspectorSectionCount; ++i)
                        {
                            st.sectionPositions[i][0] = (int)owner.leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger,  i);
                            st.sectionPositions[i][1] = (int)owner.leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i);
                            st.sectionPositions[i][2] = (int)owner.leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Default,   i);
                        }
                        const auto& order = owner.leftInspector->getSectionOrder();
                        for (int i = 0; i < std::min((int)order.size(), kInspectorSectionCount); ++i)
                            st.sectionOrder[i] = order[i];
                        st.save();
                    }
                };

                addAndMakeVisible(leftInspector.get());
                addAndMakeVisible(rightInspector.get());

                leftSplitter = std::make_unique<VerticalSplitterBar>([this](int deltaX) {
                    auto& st = AppStateManager::getInstance();
                    int maxW = std::max(180, getWidth() / 2 - 80);
                    st.leftZoneWidth = std::clamp(st.leftZoneWidth + deltaX, 160, maxW);
                    resized();
                    st.save();
                    owner.resized();
                });
                addAndMakeVisible(leftSplitter.get());

                rightSplitter = std::make_unique<VerticalSplitterBar>([this](int deltaX) {
                    auto& st = AppStateManager::getInstance();
                    int maxW = std::max(180, getWidth() / 2 - 80);
                    st.rightZoneWidth = std::clamp(st.rightZoneWidth - deltaX, 160, maxW);
                    resized();
                    st.save();
                    owner.resized();
                });
                addAndMakeVisible(rightSplitter.get());

                // Top Menu Bar for Floating Key Editor Window (Full Parity with Main Window)
                menuBar = std::make_unique<juce::MenuBarComponent>(&owner);
                addAndMakeVisible(menuBar.get());

                // Top Toolbar for Floating Key Editor Window (Full Parity with Main Toolbar)
                detachedToolbar = std::make_unique<ToolbarComponent>(
                    owner.midiModel, owner.undoManager,
                    [this]() { owner.setViewLayout(ViewLayout::Split); },
                    [this](juce::Component*) { owner.showSectionsManager(0, true); },
                    [this]() { toggleDetachedLeftZone(); },
                    [this]() { toggleDetachedRightZone(); }
                );
                detachedToolbar->setViewMode(3); // DualWindow
                detachedToolbar->setTrackModel(&owner.trackModel);
                addAndMakeVisible(detachedToolbar.get());

                btnAlwaysOnTop.setButtonText(juce::String::fromUTF8(u8"窗口置顶"));
                btnAlwaysOnTop.setColour(juce::ToggleButton::textColourId, CubaseTheme::textPrimary());
                btnAlwaysOnTop.setToggleState(AppStateManager::getInstance().floatWinAlwaysOnTop, juce::dontSendNotification);
                btnAlwaysOnTop.onClick = [this]() {
                    bool onTop = btnAlwaysOnTop.getToggleState();
                    if (auto* w = findParentComponentOfClass<KeyEditorWindow>())
                        w->setAlwaysOnTop(onTop);
                    AppStateManager::getInstance().floatWinAlwaysOnTop = onTop;
                    AppStateManager::getInstance().save();
                };
                addAndMakeVisible(btnAlwaysOnTop);

                themeChanged();
                updateZoneVisibility();
            }

            ~DetachedContainer() override
            {
                if (pianoRollComp != nullptr)
                    removeChildComponent(pianoRollComp);

                Localization::getInstance().removeListener(this);
                CubaseTheme::removeThemeListener(this);
                owner.audioEngine.removePlayheadListener(this);
                owner.trackModel.removeListener(this);
                owner.midiModel.removeListener(this);
            }

            InspectorComponent* getLeftInspector() const { return leftInspector.get(); }
            InspectorComponent* getRightInspector() const { return rightInspector.get(); }
            ToolbarComponent* getToolbar() const { return detachedToolbar.get(); }
            bool isLeftZoneShown() const { return isLeftVisible; }
            bool isRightZoneShown() const { return isRightVisible; }

            void syncFromOwner()
            {
                if (leftInspector && owner.leftInspector)
                {
                    for (int i = 0; i < kInspectorSectionCount; ++i)
                    {
                        leftInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i,
                            owner.leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i));
                        leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, i,
                            owner.leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, i));
                        leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Default, i,
                            owner.leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Default, i));
                    }
                    leftInspector->setSectionOrder(owner.leftInspector->getSectionOrder());
                }
                if (rightInspector && owner.rightInspector)
                {
                    for (int i = 0; i < kInspectorSectionCount; ++i)
                    {
                        rightInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i,
                            owner.rightInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i));
                        rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, i,
                            owner.rightInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, i));
                        rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Default, i,
                            owner.rightInspector->getContextSectionPosition(InspectorComponent::DockContext::Default, i));
                    }
                    rightInspector->setSectionOrder(owner.rightInspector->getSectionOrder());
                }
                if (leftInspector) leftInspector->applyCustomModuleSettings();
                if (rightInspector) rightInspector->applyCustomModuleSettings();
                updateZoneVisibility();
            }

            void syncToOwner()
            {
                if (leftInspector && owner.leftInspector)
                {
                    for (int i = 0; i < kInspectorSectionCount; ++i)
                    {
                        auto pos = leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i);
                        owner.leftInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i, pos);
                        if (owner.rightInspector)
                            owner.rightInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i, pos);
                    }
                    owner.leftInspector->setSectionOrder(leftInspector->getSectionOrder());
                    if (owner.rightInspector)
                        owner.rightInspector->setSectionOrder(leftInspector->getSectionOrder());
                }
                owner.resized();
            }

            void updateZoneVisibility()
            {
                if (detachedToolbar)
                    detachedToolbar->setZoneStates(isLeftVisible, isRightVisible);

                resized();
                repaint();
            }

            void themeChanged() override
            {
                if (menuBar) menuBar->repaint();
                if (detachedToolbar) detachedToolbar->themeChanged();
                if (leftInspector) leftInspector->themeChanged();
                if (rightInspector) rightInspector->themeChanged();
                if (leftSplitter) leftSplitter->repaint();
                if (rightSplitter) rightSplitter->repaint();

                btnRewind.repaint();
                btnPlay.repaint();
                btnStop.repaint();
                btnLoop.repaint();

                lblBpm.setColour(juce::Label::backgroundColourId, CubaseTheme::bgDeep());
                lblBpm.setColour(juce::Label::textColourId, CubaseTheme::accentOrange());
                lblBpm.setColour(juce::Label::outlineColourId, CubaseTheme::borderSubtle());

                lblTimeSig.setColour(juce::Label::backgroundColourId, CubaseTheme::bgDeep());
                lblTimeSig.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
                lblTimeSig.setColour(juce::Label::outlineColourId, CubaseTheme::borderSubtle());

                btnAlwaysOnTop.setColour(juce::ToggleButton::textColourId, CubaseTheme::textPrimary());

                if (auto* w = findParentComponentOfClass<KeyEditorWindow>())
                    w->setBackgroundColour(CubaseTheme::bgDeep());

                sendLookAndFeelChange();
                repaint();
            }

            void toggleDetachedLeftZone()
            {
                isLeftVisible = !isLeftVisible;
                if (detachedToolbar)
                    detachedToolbar->setZoneStates(isLeftVisible, isRightVisible);
                resized();
            }

            void toggleDetachedRightZone()
            {
                isRightVisible = !isRightVisible;
                if (detachedToolbar)
                    detachedToolbar->setZoneStates(isLeftVisible, isRightVisible);
                resized();
            }

            // TrackModel::Listener
            void tracksChanged() override {}
            void activeClipChanged() override {}
            void markersChanged() override {}
            void timeSignaturesChanged() override
            {
                lblTimeSig.setText(juce::String(owner.midiModel.getTimeSigNumerator()) + "/" + juce::String(owner.midiModel.getTimeSigDenominator()), juce::dontSendNotification);
            }

            void projectSettingsChanged() override
            {
                btnLoop.setToggleState(owner.midiModel.isLoopEnabled(), juce::dontSendNotification);
                lblBpm.setText(juce::String(owner.midiModel.getBpm(), 1) + " BPM", juce::dontSendNotification);
                lblTimeSig.setText(juce::String(owner.midiModel.getTimeSigNumerator()) + "/" + juce::String(owner.midiModel.getTimeSigDenominator()), juce::dontSendNotification);
            }

            void updatePlayButtonState(bool isPlaying)
            {
                if (isPlaying)
                {
                    btnPlay.setDAWIcon(DAWIcon::Pause);
                    btnPlay.setToggleState(true, juce::dontSendNotification);
                }
                else
                {
                    btnPlay.setDAWIcon(DAWIcon::Play);
                    btnPlay.setToggleState(false, juce::dontSendNotification);
                }
                btnPlay.repaint();
            }

            void languageChanged() override
            {
                if (menuBar) menuBar->repaint();
                btnAlwaysOnTop.setButtonText(juce::String::fromUTF8(u8"窗口置顶"));
                btnRewind.setTooltip(juce::String::fromUTF8(u8"回到起点 (Rewind) [Home]"));
                btnPlay.setTooltip(juce::String::fromUTF8(u8"播放 / 暂停 (Play / Pause) [Space]"));
                btnStop.setTooltip(juce::String::fromUTF8(u8"停止 (Stop) [Numpad 0]"));
                btnLoop.setTooltip(juce::String::fromUTF8(u8"循环播放 (Cycle / Loop) [/]"));
                repaint();
            }

            void playbackStateChanged(bool isPlaying) override
            {
                updatePlayButtonState(isPlaying);
            }

            void playheadMoved(double) override {}

            void mouseDown(const juce::MouseEvent& e) override
            {
                if (e.eventComponent == &lblTimeSig || lblTimeSig.getBounds().contains(e.getPosition()))
                {
                    juce::PopupMenu menu;
                    menu.addItem(1, "4/4");
                    menu.addItem(2, "3/4");
                    menu.addItem(3, "2/4");
                    menu.addItem(4, "6/8");
                    menu.addItem(5, "5/4");
                    menu.addItem(6, "7/8");

                    juce::Component::SafePointer<DetachedContainer> safeThis(this);
                    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&lblTimeSig), [this, safeThis](int res) {
                        if (safeThis.getComponent() == nullptr) return;
                        int num = 4, den = 4;
                        if (res == 1) { num = 4; den = 4; }
                        else if (res == 2) { num = 3; den = 4; }
                        else if (res == 3) { num = 2; den = 4; }
                        else if (res == 4) { num = 6; den = 8; }
                        else if (res == 5) { num = 5; den = 4; }
                        else if (res == 6) { num = 7; den = 8; }
                        else return;

                        owner.midiModel.setTimeSignature(num, den);
                        owner.trackModel.setDefaultTimeSignature(num, den);
                        owner.trackModel.triggerTimeSignaturesChanged();
                        owner.trackModel.triggerTracksChanged();
                        lblTimeSig.setText(juce::String(num) + "/" + juce::String(den), juce::dontSendNotification);
                        repaint();
                    });
                }
            }

            void mouseDrag(const juce::MouseEvent& e) override
            {
                if (e.eventComponent == &lblBpm || lblBpm.getBounds().contains(e.getPosition()))
                {
                    double delta = -e.getDistanceFromDragStartY() * 0.1;
                    double current = owner.midiModel.getBpm();
                    double newBpm = std::clamp(std::round(current + delta), 20.0, 400.0);
                    if (std::abs(newBpm - current) >= 1.0)
                        owner.midiModel.setBpm(newBpm);
                }
            }

            void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
            {
                if (e.eventComponent == &lblBpm || lblBpm.getBounds().contains(e.getPosition()))
                {
                    double delta = (wheel.deltaY > 0 ? 1.0 : -1.0);
                    double newBpm = std::clamp(owner.midiModel.getBpm() + delta, 20.0, 400.0);
                    owner.midiModel.setBpm(newBpm);
                }
            }

            void paint(juce::Graphics& g) override
            {
                g.fillAll(CubaseTheme::bgDeep());
                g.setColour(CubaseTheme::borderSubtle());
                g.drawHorizontalLine(getHeight() - 36, 0.0f, (float)getWidth());
            }

            void resized() override
            {
                auto area = getLocalBounds();

                if (menuBar)
                    menuBar->setBounds(area.removeFromTop(24));

                if (detachedToolbar)
                    detachedToolbar->setBounds(area.removeFromTop(44));

                auto btmArea = area.removeFromBottom(36).reduced(6, 4);
                btnAlwaysOnTop.setBounds(btmArea.removeFromRight(85));
                int btnW = 38;
                int btnGap = 4;
                btnRewind.setBounds(btmArea.removeFromLeft(btnW));
                btmArea.removeFromLeft(btnGap);
                btnPlay.setBounds(btmArea.removeFromLeft(btnW));
                btmArea.removeFromLeft(btnGap);
                btnStop.setBounds(btmArea.removeFromLeft(btnW));
                btmArea.removeFromLeft(btnGap + 4);
                btnLoop.setBounds(btmArea.removeFromLeft(btnW));
                btmArea.removeFromLeft(12);
                lblBpm.setBounds(btmArea.removeFromLeft(75));
                btmArea.removeFromLeft(6);
                lblTimeSig.setBounds(btmArea.removeFromLeft(55));

                auto& st = AppStateManager::getInstance();
                int leftW = st.leftZoneWidth;
                int rightW = st.rightZoneWidth;

                if (isLeftVisible && leftInspector)
                {
                    const int displayWidth = leftInspector->hasVisibleSections()
                        ? leftW : std::min(leftW, 154);
                    leftInspector->setBounds(area.removeFromLeft(displayWidth));
                    if (leftSplitter)
                    {
                        leftSplitter->setVisible(true);
                        leftSplitter->setBounds(area.removeFromLeft(5));
                    }
                }
                else
                {
                    if (leftInspector) leftInspector->setBounds(0, 0, 0, 0);
                    if (leftSplitter)  leftSplitter->setVisible(false);
                }

                if (isRightVisible && rightInspector)
                {
                    const int displayWidth = rightInspector->hasVisibleSections()
                        ? rightW : std::min(rightW, 154);
                    rightInspector->setBounds(area.removeFromRight(displayWidth));
                    if (rightSplitter)
                    {
                        rightSplitter->setVisible(true);
                        rightSplitter->setBounds(area.removeFromRight(5));
                    }
                }
                else
                {
                    if (rightInspector) rightInspector->setBounds(0, 0, 0, 0);
                    if (rightSplitter)  rightSplitter->setVisible(false);
                }

                if (pianoRollComp)
                    pianoRollComp->setBounds(area);
            }

        private:
            MainComponent& owner;
            PianoRollComponent* pianoRollComp = nullptr;
            std::unique_ptr<juce::MenuBarComponent> menuBar;
            std::unique_ptr<ToolbarComponent> detachedToolbar;
            std::unique_ptr<InspectorComponent> leftInspector;
            std::unique_ptr<InspectorComponent> rightInspector;
            std::unique_ptr<VerticalSplitterBar> leftSplitter;
            std::unique_ptr<VerticalSplitterBar> rightSplitter;
            bool isLeftVisible = true;
            bool isRightVisible = true;
            DAWIconButton btnRewind { DAWIcon::Rewind, juce::String::fromUTF8(u8"回到起点 (Rewind) [Home]") };
            DAWIconButton btnPlay   { DAWIcon::Play,   juce::String::fromUTF8(u8"播放 / 暂停 (Play / Pause) [Space]") };
            DAWIconButton btnStop   { DAWIcon::Stop,   juce::String::fromUTF8(u8"停止 (Stop) [Numpad 0]") };
            DAWIconButton btnLoop   { DAWIcon::Loop,   juce::String::fromUTF8(u8"循环播放 (Cycle / Loop) [/]") };
            juce::Label lblBpm, lblTimeSig;
            juce::ToggleButton btnAlwaysOnTop;
        };

        KeyEditorWindow(MainComponent& mainComp, PianoRollComponent* roll, std::function<void()> onClose)
            : juce::DocumentWindow("Harmonizer Studio - Key Editor",
                                   CubaseTheme::bgDeep(),
                                   juce::DocumentWindow::allButtons),
              owner(mainComp),
              closeCallback(onClose)
        {
            setLookAndFeel(&owner.theme);
            setUsingNativeTitleBar(true);
            setResizable(true, true);

            containerHolder = std::make_unique<DetachedContainer>(owner, roll);
            container = containerHolder.get();
            setContentNonOwned(container, false);

            if (AppStateManager::getInstance().floatWinAlwaysOnTop)
                setAlwaysOnTop(true);
        }

        bool isInitializedAndVisible = false;

        void markInitialized()
        {
            isInitializedAndVisible = true;
        }

        void saveGeometry()
        {
            if (isInitializedAndVisible && getWidth() >= 500 && getHeight() >= 350)
            {
                auto& st = AppStateManager::getInstance();
                st.floatWinX = getX();
                st.floatWinY = getY();
                st.floatWinW = getWidth();
                st.floatWinH = getHeight();
                st.floatWinAlwaysOnTop = isAlwaysOnTop();
                st.save();
            }
        }

        ~KeyEditorWindow() override
        {
            saveGeometry();
            clearContentComponent();
            containerHolder.reset();
            container = nullptr;
            setLookAndFeel(nullptr);
        }

        void moved() override
        {
            juce::DocumentWindow::moved();
            if (!isInitializedAndVisible) return;
            if (getWidth() >= 500 && getHeight() >= 350)
            {
                auto& st = AppStateManager::getInstance();
                st.floatWinX = getX();
                st.floatWinY = getY();
            }
        }

        void resized() override
        {
            juce::DocumentWindow::resized();
            if (!isInitializedAndVisible) return;
            if (getWidth() >= 500 && getHeight() >= 350)
            {
                auto& st = AppStateManager::getInstance();
                st.floatWinW = getWidth();
                st.floatWinH = getHeight();
            }
        }

        void closeButtonPressed() override
        {
            saveGeometry();
            if (closeCallback)
            {
                auto cb = closeCallback;
                juce::MessageManager::callAsync([cb]() {
                    if (cb) cb();
                });
            }
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            if (owner.keyPressed(key))
                return true;
            return juce::DocumentWindow::keyPressed(key);
        }

        DetachedContainer* getContainer() const { return container; }

        void syncSectionsFromOwner()
        {
            if (container)
                container->syncFromOwner();
        }

        void syncSectionsToOwner()
        {
            if (container)
                container->syncToOwner();
        }

    private:
        MainComponent& owner;
        std::unique_ptr<DetachedContainer> containerHolder;
        DetachedContainer* container = nullptr;
        std::function<void()> closeCallback;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyEditorWindow)
    };

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void languageChanged() override;
    void openPreferences();
    void toggleViewLayout();

    enum class FocusArea
    {
        Arranger,
        KeyEditor,
        Other
    };

    void setActiveFocusArea(FocusArea area);
    FocusArea getActiveFocusArea() const { return currentFocusArea; }

    void toggleLeftZone();
    void toggleRightZone();
    void showSectionsManager(int initialContextIndex = -1, bool fromDetached = false);

    /// Called by MainWindow on close — collects current state and writes ui_state.xml
    void collectAndSaveUIState();
    /// Called once after all sub-components are created — applies state from AppStateManager
    void applyUIState();

private:
    CubaseTheme theme;
    MidiModel midiModel;
    TrackModel trackModel;
    juce::UndoManager undoManager;
    AudioEngine audioEngine;

    ViewLayout currentLayout = ViewLayout::Split;
    ViewLayout preferredKeyEditorLayout = ViewLayout::Split;
    FocusArea currentFocusArea = FocusArea::Arranger;
    bool isLeftZoneVisible = true;
    bool isRightZoneVisible = true;

    // Sub-components
    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<ToolbarComponent> toolbar;
    std::unique_ptr<InspectorComponent> leftInspector;
    std::unique_ptr<InspectorComponent> rightInspector;
    std::unique_ptr<TrackArrangerComponent> trackArranger;
    std::unique_ptr<PianoRollComponent> pianoRoll;
    std::unique_ptr<TransportBar> transport;

    // Draggable Splitter between Arranger and Piano Roll
    class SplitterBar : public juce::Component
    {
    public:
        SplitterBar(std::function<void(int deltaY)> onDrag) : dragCallback(onDrag)
        {
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        }
        void paint(juce::Graphics& g) override
        {
            g.setColour(CubaseTheme::bgPanel());
            g.fillAll();
            g.setColour(CubaseTheme::borderSubtle());
            g.drawHorizontalLine(0, 0.0f, (float)getWidth());
            g.drawHorizontalLine(getHeight() - 1, 0.0f, (float)getWidth());

            // Center grip handle
            g.setColour(CubaseTheme::borderBright());
            int cx = getWidth() / 2;
            int cy = getHeight() / 2;
            g.fillRoundedRectangle((float)(cx - 24), (float)(cy - 1), 48.0f, 2.0f, 1.0f);
        }
        void mouseDown(const juce::MouseEvent& e) override
        {
            initialY = e.getScreenY();
        }
        void mouseDrag(const juce::MouseEvent& e) override
        {
            int currentY = e.getScreenY();
            int deltaY = currentY - initialY;
            if (deltaY != 0 && dragCallback)
            {
                dragCallback(deltaY);
                initialY = currentY;
            }
        }
    private:
        std::function<void(int)> dragCallback;
        int initialY = 0;
    };
    std::unique_ptr<SplitterBar> splitterBar;
    std::unique_ptr<VerticalSplitterBar> leftSplitterBar;
    std::unique_ptr<VerticalSplitterBar> rightSplitterBar;
    float splitRatio = 0.38f;

    bool isSyncingSections = false;

    std::unique_ptr<PreferencesWindow> preferencesWindow;
    std::unique_ptr<KeyEditorWindow> detachedKeyEditorWindow;

    juce::File currentProjectFile;

    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void importMidiFile();
    void exportMidiFile();
    void startSecondaryDominantPlacement();
    void startQuickChord(int chordTypeId);
    void refreshQuickChordMode();
    void refreshSecondaryDominantSettings();
    void showSecondaryDominantSettings();
    void applyReharmonization();
    void refreshReharmonizationSettings();
    void applyChordQuality(ChordShapeQuality quality);
    void applyChordVoicing(ChordShapeVoicing voicing);
    void applyChordStack(ChordStackDirection direction);
    void applyModeBorrowing();
    void refreshModeBorrowingSettings();

    struct QualityEditSession
    {
        int trackId = -1;
        int64_t clipId = -1;
        std::vector<NoteEvent> baseline;
        std::vector<NoteEvent> lastOutput;
    } qualityEditSession;
    bool applyChordEditPlan(const ChordEditPlan& plan, const juce::String& undoName,
                            const std::vector<NoteEvent>* completeTarget = nullptr);

    // Workspaces
    void setViewLayout(ViewLayout layout);
    void saveWorkspaceXml(const juce::File& file);
    void loadWorkspaceXml(const juce::File& file);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace Harmonizer
