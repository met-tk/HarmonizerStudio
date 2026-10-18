#include "MainComponent.h"
#include "EventEditors.h"
#include "SecondaryDominantAction.h"
#include "../core/Reharmonizer.h"
#include "../core/ChordPlanAnalysis.h"
#include "../core/NoteEditOperations.h"
#include "../core/GeneralSettings.h"
#include "../core/AppStateManager.h"
#include "../core/QuickChord.h"
#include "../core/QuickChordCatalog.h"
#include <cmath>
#include <unordered_set>

namespace Harmonizer
{

namespace
{
bool isDroppedAudioFile(const juce::File& file)
{
    return file.hasFileExtension(".wav") || file.hasFileExtension(".wave")
        || file.hasFileExtension(".aif") || file.hasFileExtension(".aiff")
        || file.hasFileExtension(".flac") || file.hasFileExtension(".mp3")
        || file.hasFileExtension(".ogg");
}

bool sameChordSelection(std::vector<NoteEvent> a, std::vector<NoteEvent> b)
{
    if (a.size() != b.size()) return false;
    auto byId = [](const NoteEvent& x, const NoteEvent& y) { return x.id < y.id; };
    std::sort(a.begin(), a.end(), byId);
    std::sort(b.begin(), b.end(), byId);
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].pitch != b[i].pitch
            || a[i].startBeat != b[i].startBeat || a[i].lengthBeats != b[i].lengthBeats
            || a[i].velocity != b[i].velocity || a[i].isMuted != b[i].isMuted)
            return false;
    return true;
}
}

enum MenuIDs
{
    Menu_New = 1,
    Menu_Open,
    Menu_Save,
    Menu_SaveAs,
    Menu_ImportMidi,
    Menu_ExportMidi,
    Menu_Preferences,
    Menu_Exit,

    Menu_Undo = 20,
    Menu_Redo,
    Menu_Cut,
    Menu_Copy,
    Menu_Paste,
    Menu_Duplicate,
    Menu_Delete,
    Menu_SelectAll,

    Menu_PlayPause = 40,
    Menu_Stop,
    Menu_LoopToggle,
    Menu_BarOffset
};

MainComponent::MainComponent()
    : audioEngine(midiModel)
{
    juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName("Microsoft YaHei");
    setLookAndFeel(&theme);

    // Sync active clip notes to midiModel if available
    if (auto* trk = trackModel.getTrackById(trackModel.getActiveTrackId()))
    {
        for (const auto& clip : trk->getClips())
        {
            if (clip.id == trackModel.getActiveClipId())
            {
                midiModel.setAllNotes(clip.notes, false);
                break;
            }
        }
    }

    // Create sub-components
    menuBar = std::make_unique<juce::MenuBarComponent>(this);
    addAndMakeVisible(menuBar.get());

    pianoRoll = std::make_unique<PianoRollComponent>(midiModel, trackModel, audioEngine, undoManager);

    trackArranger = std::make_unique<TrackArrangerComponent>(
        trackModel, midiModel, audioEngine, undoManager,
        [this](int trackId, int64_t clipId) {
            auto* trk = trackModel.getTrackById(trackId);
            if (trk)
            {
                for (const auto& clip : trk->getClips())
                {
                    if (clip.id == clipId)
                    {
                        trackModel.setActiveClip(trackId, clipId, &midiModel);

                        auto openMode = GeneralSettings::getInstance().getMidiPartOpenMode();
                        ViewLayout targetLayout = ViewLayout::Split;
                        if (openMode == MidiPartOpenMode::SeparateWindow)
                            targetLayout = ViewLayout::DualWindow;
                        else if (openMode == MidiPartOpenMode::LowerZone)
                            targetLayout = ViewLayout::Split;
                        else if (openMode == MidiPartOpenMode::MainScreen)
                            targetLayout = ViewLayout::EditorOnly;

                        if (currentLayout != targetLayout)
                        {
                            setViewLayout(targetLayout);
                        }
                        else if (targetLayout == ViewLayout::DualWindow && detachedKeyEditorWindow)
                        {
                            detachedKeyEditorWindow->setVisible(true);
                            detachedKeyEditorWindow->toFront(true);
                        }

                        pianoRoll->scrollToBeat(clip.startBeat);
                        setActiveFocusArea(FocusArea::KeyEditor);
                        break;
                    }
                }
            }
        }
    );
    trackArranger->onFocusGained = [this]() {
        setActiveFocusArea(FocusArea::Arranger);
    };
    addAndMakeVisible(trackArranger.get());

    splitterBar = std::make_unique<SplitterBar>([this](int deltaY) {
        int totalH = getHeight() - 24 - 44 - 48; // menu, toolbar, transport
        if (totalH > 100)
        {
            splitRatio += (float)deltaY / (float)totalH;
            splitRatio = std::clamp(splitRatio, 0.15f, 0.85f);
            auto& st = AppStateManager::getInstance();
            st.splitRatio = splitRatio;
            st.save();
            resized();
        }
    });
    addAndMakeVisible(splitterBar.get());

    leftInspector = std::make_unique<InspectorComponent>(
        midiModel, trackModel, audioEngine, undoManager,
        [this]() { openPreferences(); }
    );
    leftInspector->setDockFilter(InspectorComponent::DockSide::Left);
    leftInspector->onSecondaryDominantRequested = [this]() { startSecondaryDominantPlacement(); };
    leftInspector->onQuickChordRequested = [this](int id) { startQuickChord(id); };
    leftInspector->onQuickChordSettingsChanged = [this]() { refreshQuickChordMode(); };
    leftInspector->onSecondaryDominantSettingsChanged = [this]() { refreshSecondaryDominantSettings(); };
    leftInspector->onReharmonizationRequested = [this]() { applyReharmonization(); };
    leftInspector->onReharmonizationSettingsChanged = [this]() { refreshReharmonizationSettings(); };
    leftInspector->onChordQualityRequested = [this](ChordShapeQuality q) { applyChordQuality(q); };
    leftInspector->onChordVoicingRequested = [this](ChordShapeVoicing v) { applyChordVoicing(v); };
    leftInspector->onChordStackRequested = [this](ChordStackDirection d) { applyChordStack(d); };
    leftInspector->onModeBorrowingRequested = [this]() { applyModeBorrowing(); };
    leftInspector->onModeBorrowingSettingsChanged = [this]() { refreshModeBorrowingSettings(); };
    leftInspector->onOpenSectionsManager = [this]() { showSectionsManager(); };
    leftInspector->onEnsureDockVisible = [this](InspectorComponent::DockSide side) {
        if (side == InspectorComponent::DockSide::Left && !isLeftZoneVisible) toggleLeftZone();
        if (side == InspectorComponent::DockSide::Right && !isRightZoneVisible) toggleRightZone();
    };
    addAndMakeVisible(leftInspector.get());

    rightInspector = std::make_unique<InspectorComponent>(
        midiModel, trackModel, audioEngine, undoManager,
        [this]() { openPreferences(); }
    );
    rightInspector->setDockFilter(InspectorComponent::DockSide::Right);
    rightInspector->onSecondaryDominantRequested = [this]() { startSecondaryDominantPlacement(); };
    rightInspector->onQuickChordRequested = [this](int id) { startQuickChord(id); };
    rightInspector->onQuickChordSettingsChanged = [this]() { refreshQuickChordMode(); };
    rightInspector->onSecondaryDominantSettingsChanged = [this]() { refreshSecondaryDominantSettings(); };
    rightInspector->onReharmonizationRequested = [this]() { applyReharmonization(); };
    rightInspector->onReharmonizationSettingsChanged = [this]() { refreshReharmonizationSettings(); };
    rightInspector->onChordQualityRequested = [this](ChordShapeQuality q) { applyChordQuality(q); };
    rightInspector->onChordVoicingRequested = [this](ChordShapeVoicing v) { applyChordVoicing(v); };
    rightInspector->onChordStackRequested = [this](ChordStackDirection d) { applyChordStack(d); };
    rightInspector->onModeBorrowingRequested = [this]() { applyModeBorrowing(); };
    rightInspector->onModeBorrowingSettingsChanged = [this]() { refreshModeBorrowingSettings(); };
    rightInspector->onOpenSectionsManager = [this]() { showSectionsManager(); };
    rightInspector->onEnsureDockVisible = [this](InspectorComponent::DockSide side) {
        if (side == InspectorComponent::DockSide::Left && !isLeftZoneVisible) toggleLeftZone();
        if (side == InspectorComponent::DockSide::Right && !isRightZoneVisible) toggleRightZone();
    };
    leftInspector->setPeerDock(rightInspector.get());
    rightInspector->setPeerDock(leftInspector.get());
    addAndMakeVisible(rightInspector.get());

    leftSplitterBar = std::make_unique<VerticalSplitterBar>([this](int deltaX) {
        auto& st = AppStateManager::getInstance();
        int maxW = std::max(180, getWidth() / 2 - 80);
        st.leftZoneWidth = std::clamp(st.leftZoneWidth + deltaX, 160, maxW);
        resized();
        st.save();
        if (detachedKeyEditorWindow && detachedKeyEditorWindow->getContainer())
            detachedKeyEditorWindow->getContainer()->resized();
    });
    addAndMakeVisible(leftSplitterBar.get());

    rightSplitterBar = std::make_unique<VerticalSplitterBar>([this](int deltaX) {
        auto& st = AppStateManager::getInstance();
        int maxW = std::max(180, getWidth() / 2 - 80);
        st.rightZoneWidth = std::clamp(st.rightZoneWidth - deltaX, 160, maxW);
        resized();
        st.save();
        if (detachedKeyEditorWindow && detachedKeyEditorWindow->getContainer())
            detachedKeyEditorWindow->getContainer()->resized();
    });
    addAndMakeVisible(rightSplitterBar.get());

    leftInspector->onSectionLayoutChanged = [this]() {
        if (isSyncingSections) return;
        juce::ScopedValueSetter<bool> svs(isSyncingSections, true);
        if (leftInspector && rightInspector)
        {
            for (int i = 0; i < kInspectorSectionCount; ++i)
            {
                rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, i,
                                                           leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, i));
                rightInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i,
                                                           leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i));
                rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Default, i,
                                                           leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Default, i));
            }
            rightInspector->setSectionOrder(leftInspector->getSectionOrder());
        }
        if (detachedKeyEditorWindow)
            detachedKeyEditorWindow->syncSectionsFromOwner();
        // Persist inspector layout immediately
        if (leftInspector)
        {
            auto& st = AppStateManager::getInstance();
            st.inspectorStateValid = true;
            for (int i = 0; i < kInspectorSectionCount; ++i)
            {
                st.sectionPositions[i][0] = (int)leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger,  i);
                st.sectionPositions[i][1] = (int)leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i);
                st.sectionPositions[i][2] = (int)leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Default,   i);
            }
            const auto& order = leftInspector->getSectionOrder();
            for (int i = 0; i < std::min((int)order.size(), kInspectorSectionCount); ++i)
                st.sectionOrder[i] = order[i];
            st.save();
        }
        resized();
    };

    rightInspector->onSectionLayoutChanged = [this]() {
        if (isSyncingSections) return;
        juce::ScopedValueSetter<bool> svs(isSyncingSections, true);
        if (leftInspector && rightInspector)
        {
            for (int i = 0; i < kInspectorSectionCount; ++i)
            {
                leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, i,
                                                           rightInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, i));
                leftInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i,
                                                           rightInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i));
                leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Default, i,
                                                           rightInspector->getContextSectionPosition(InspectorComponent::DockContext::Default, i));
            }
            leftInspector->setSectionOrder(rightInspector->getSectionOrder());
        }
        if (detachedKeyEditorWindow)
            detachedKeyEditorWindow->syncSectionsFromOwner();
        // Persist inspector layout immediately
        if (leftInspector)
        {
            auto& st = AppStateManager::getInstance();
            st.inspectorStateValid = true;
            for (int i = 0; i < kInspectorSectionCount; ++i)
            {
                st.sectionPositions[i][0] = (int)leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger,  i);
                st.sectionPositions[i][1] = (int)leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i);
                st.sectionPositions[i][2] = (int)leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Default,   i);
            }
            const auto& order = leftInspector->getSectionOrder();
            for (int i = 0; i < std::min((int)order.size(), kInspectorSectionCount); ++i)
                st.sectionOrder[i] = order[i];
            st.save();
        }
        resized();
    };

    toolbar = std::make_unique<ToolbarComponent>(
        midiModel, undoManager,
        [this]() { toggleViewLayout(); },
        [this](juce::Component*) { showSectionsManager(); },
        [this]() { toggleLeftZone(); },
        [this]() { toggleRightZone(); }
    );
    toolbar->setTrackModel(&trackModel);
    addAndMakeVisible(toolbar.get());

    audioEngine.setTrackModel(&trackModel);

    pianoRoll->onFocusGained = [this]() {
        setActiveFocusArea(FocusArea::KeyEditor);
    };
    addAndMakeVisible(pianoRoll.get());

    transport = std::make_unique<TransportBar>(
        midiModel, audioEngine,
        [this]() { openPreferences(); },
        &trackModel
    );
    addAndMakeVisible(transport.get());

    setActiveFocusArea(FocusArea::Arranger);

    Localization::getInstance().addListener(this);
    CubaseTheme::addThemeListener(this);

    setWantsKeyboardFocus(true);
    setSize(1280, 780);

    // Restore all UI state from previous session
    applyUIState();
}

MainComponent::~MainComponent()
{
    collectAndSaveUIState();
    CubaseTheme::removeThemeListener(this);
    Localization::getInstance().removeListener(this);
    setLookAndFeel(nullptr);
}

void MainComponent::themeChanged()
{
    theme.updateLookAndFeelColours();
    sendLookAndFeelChange();

    if (toolbar) toolbar->themeChanged();
    if (leftInspector) leftInspector->themeChanged();
    if (rightInspector) rightInspector->themeChanged();
    if (transport) transport->themeChanged();
    if (trackArranger) trackArranger->repaint();
    if (pianoRoll) pianoRoll->repaint();
    if (splitterBar) splitterBar->repaint();
    if (leftSplitterBar) leftSplitterBar->repaint();
    if (rightSplitterBar) rightSplitterBar->repaint();

    if (detachedKeyEditorWindow)
    {
        detachedKeyEditorWindow->setBackgroundColour(CubaseTheme::bgDeep());
        detachedKeyEditorWindow->sendLookAndFeelChange();
        detachedKeyEditorWindow->repaint();
    }

    repaint();
}

void MainComponent::languageChanged()
{
    juce::MenuBarModel::menuItemsChanged();
    repaint();
}

void MainComponent::openPreferences()
{
    if (preferencesWindow == nullptr)
    {
        preferencesWindow = std::make_unique<PreferencesWindow>(audioEngine);
    }
    else
    {
        preferencesWindow->setVisible(true);
        preferencesWindow->toFront(true);
    }
}

void MainComponent::startSecondaryDominantPlacement()
{
    juce::Component::SafePointer<MainComponent> safeThis(this);
    generateSecondaryDominantPreview(midiModel, trackModel,
        [safeThis](std::vector<ChordPlacementGroup> groups) {
            if (auto* self = safeThis.getComponent())
            {
                if (self->currentLayout == ViewLayout::ArrangerOnly)
                    self->setViewLayout(self->preferredKeyEditorLayout);
                if (self->currentLayout == ViewLayout::DualWindow
                    && self->detachedKeyEditorWindow != nullptr)
                    self->detachedKeyEditorWindow->toFront(true);
                self->setActiveFocusArea(FocusArea::KeyEditor);
                self->pianoRoll->beginSecondaryDominantPlacement(std::move(groups));
            }
        });
}

void MainComponent::startQuickChord(int chordTypeId)
{
    const auto* type = QuickChordCatalog::find(chordTypeId);
    if (type == nullptr) return;

    const auto plan = QuickChord::prepare(midiModel, trackModel, *type,
        AppStateManager::getInstance().quickChordPreserveSelectedTiming);
    if (!plan.succeeded())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            juce::String::fromUTF8(u8"快速和弦"), plan.error);
        return;
    }

    if (pianoRoll != nullptr && pianoRoll->isSecondaryDominantPlacementActive())
        pianoRoll->cancelSecondaryDominantPlacement();

    if (plan.requiresPlacement)
    {
        if (currentLayout == ViewLayout::ArrangerOnly)
            setViewLayout(preferredKeyEditorLayout);
        if (currentLayout == ViewLayout::DualWindow && detachedKeyEditorWindow != nullptr)
            detachedKeyEditorWindow->toFront(true);
        setActiveFocusArea(FocusArea::KeyEditor);
        pianoRoll->beginChordPlacement(plan.generated, "Place Quick Chord",
            "C" + type->symbol + " · " + type->chineseName);
        return;
    }

    const auto before = midiModel.getNotes();
    auto after = before;
    const std::unordered_set<int64_t> removed(
        plan.selectedUpperIdsToRemove.begin(), plan.selectedUpperIdsToRemove.end());
    after.erase(std::remove_if(after.begin(), after.end(), [&removed](const NoteEvent& note) {
        return removed.count(note.id) != 0;
    }), after.end());
    for (auto note : plan.generated)
    {
        note.id = midiModel.generateNextNoteId();
        note.isSelected = true;
        after.push_back(note);
    }
    const bool committed = UndoActionHelper::performAtomic(&undoManager,
        new ModifyClipNotesUndoAction(midiModel, trackModel, trackModel.getActiveTrackId(),
                                      trackModel.getActiveClipId(), before, after),
        "Create Quick Chord");
    if (!committed)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            juce::String::fromUTF8(u8"快速和弦"),
            juce::String::fromUTF8(u8"无法将和弦写入当前 MIDI 片段。"));
}

void MainComponent::refreshQuickChordMode()
{
    if (leftInspector != nullptr) leftInspector->refreshQuickChordMode();
    if (rightInspector != nullptr) rightInspector->refreshQuickChordMode();
    if (detachedKeyEditorWindow != nullptr && detachedKeyEditorWindow->getContainer() != nullptr)
    {
        auto* container = detachedKeyEditorWindow->getContainer();
        if (auto* left = container->getLeftInspector()) left->refreshQuickChordMode();
        if (auto* right = container->getRightInspector()) right->refreshQuickChordMode();
    }
}

void MainComponent::refreshSecondaryDominantSettings()
{
    if (leftInspector != nullptr) leftInspector->refreshSecondaryDominantSettings();
    if (rightInspector != nullptr) rightInspector->refreshSecondaryDominantSettings();
    if (detachedKeyEditorWindow != nullptr && detachedKeyEditorWindow->getContainer() != nullptr)
    {
        auto* container = detachedKeyEditorWindow->getContainer();
        if (auto* left = container->getLeftInspector())
            left->refreshSecondaryDominantSettings();
        if (auto* right = container->getRightInspector())
            right->refreshSecondaryDominantSettings();
    }
}

void MainComponent::refreshReharmonizationSettings()
{
    if (leftInspector) leftInspector->refreshReharmonizationSettings();
    if (rightInspector) rightInspector->refreshReharmonizationSettings();
    if (detachedKeyEditorWindow != nullptr)
        if (auto* container = detachedKeyEditorWindow->getContainer())
        {
            if (auto* left = container->getLeftInspector()) left->refreshReharmonizationSettings();
            if (auto* right = container->getRightInspector()) right->refreshReharmonizationSettings();
        }
}

void MainComponent::applyReharmonization()
{
    if (pianoRoll != nullptr && pianoRoll->isSecondaryDominantPlacementActive())
        pianoRoll->cancelSecondaryDominantPlacement();
    const auto algorithm = static_cast<ReharmonizationAlgorithm>(
        std::clamp(AppStateManager::getInstance().reharmonizationAlgorithm,
                   0, kReharmonizationAlgorithmCount - 1));
    const auto plan = Reharmonizer::prepare(midiModel, trackModel, algorithm);
    if (!plan.succeeded())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            tr("Reharmonization"), plan.error);
        return;
    }
    if (!plan.hasChanges()) return;

    const auto before = midiModel.getNotes();
    auto after = before;
    for (const auto& changed : plan.changedNotes)
    {
        auto it = std::find_if(after.begin(), after.end(), [&changed](const NoteEvent& n) {
            return n.id == changed.id;
        });
        if (it == after.end()) return;
        it->pitch = changed.pitch;
    }
    UndoActionHelper::performAtomic(&undoManager,
        new ModifyClipNotesUndoAction(midiModel, trackModel, trackModel.getActiveTrackId(),
                                      trackModel.getActiveClipId(), before, after),
        tr("Apply Reharmonization"));
}

bool MainComponent::applyChordEditPlan(const ChordEditPlan& plan, const juce::String& undoName,
                                        const std::vector<NoteEvent>* completeTarget)
{
    if (!plan.succeeded())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            undoName, plan.error);
        return false;
    }
    const auto before = midiModel.getNotes();
    auto after = before;
    const auto& target = completeTarget != nullptr ? *completeTarget : plan.changedNotes;
    bool changed = false;
    for (const auto& note : target)
    {
        auto it = std::find_if(after.begin(), after.end(), [&note](const NoteEvent& candidate) {
            return candidate.id == note.id;
        });
        if (it == after.end()) return false;
        if (it->pitch != note.pitch)
        {
            it->pitch = note.pitch;
            changed = true;
        }
    }
    if (!plan.removedNoteIds.empty())
    {
        const std::unordered_set<int64_t> removed(plan.removedNoteIds.begin(),
                                                   plan.removedNoteIds.end());
        const auto oldSize = after.size();
        after.erase(std::remove_if(after.begin(), after.end(), [&removed](const NoteEvent& note) {
            return removed.count(note.id) != 0;
        }), after.end());
        if (oldSize - after.size() != removed.size()) return false;
        changed = true;
    }
    for (auto added : plan.addedNotes)
    {
        added.id = midiModel.generateNextNoteId();
        after.push_back(added);
        changed = true;
    }
    if (!changed) return true;
    return UndoActionHelper::performAtomic(&undoManager,
        new ModifyClipNotesUndoAction(midiModel, trackModel, trackModel.getActiveTrackId(),
                                      trackModel.getActiveClipId(), before, after), undoName);
}

void MainComponent::applyChordQuality(ChordShapeQuality quality)
{
    const auto selected = midiModel.getSelectedNotes();
    auto& session = qualityEditSession;
    if (session.trackId != trackModel.getActiveTrackId()
        || session.clipId != trackModel.getActiveClipId()
        || !sameChordSelection(selected, session.lastOutput))
    {
        session.trackId = trackModel.getActiveTrackId();
        session.clipId = trackModel.getActiveClipId();
        session.baseline = selected;
        session.lastOutput = selected;
    }
    const auto plan = ChordEditing::changeQuality(midiModel, trackModel, session.baseline, quality);
    if (!plan.succeeded())
    {
        applyChordEditPlan(plan, "Change Chord Quality");
        return;
    }
    auto target = session.baseline;
    for (const auto& changed : plan.changedNotes)
        for (auto& note : target)
            if (note.id == changed.id) { note.pitch = changed.pitch; break; }
    if (applyChordEditPlan(plan, "Change Chord Quality", &target))
        session.lastOutput = midiModel.getSelectedNotes();
}

void MainComponent::applyChordVoicing(ChordShapeVoicing voicing)
{
    qualityEditSession = {};
    const auto plan = ChordEditing::changeVoicing(midiModel, trackModel,
                                                  midiModel.getSelectedNotes(), voicing);
    applyChordEditPlan(plan, "Change Chord Voicing");
}

void MainComponent::applyChordStack(ChordStackDirection direction)
{
    qualityEditSession = {};
    const auto plan = ChordEditing::changeStack(midiModel, trackModel,
                                                midiModel.getSelectedNotes(), direction);
    applyChordEditPlan(plan, "Change Chord Stack");
}

void MainComponent::refreshModeBorrowingSettings()
{
    if (leftInspector) leftInspector->refreshModeBorrowingSettings();
    if (rightInspector) rightInspector->refreshModeBorrowingSettings();
    if (detachedKeyEditorWindow != nullptr)
        if (auto* container = detachedKeyEditorWindow->getContainer())
        {
            if (auto* left = container->getLeftInspector()) left->refreshModeBorrowingSettings();
            if (auto* right = container->getRightInspector()) right->refreshModeBorrowingSettings();
        }
}

void MainComponent::applyModeBorrowing()
{
    qualityEditSession = {};
    const auto& state = AppStateManager::getInstance();
    const auto selected = midiModel.getSelectedNotes();
    const auto style = state.modeBorrowingStyle == 1 ? ModeBorrowingStyle::Approach
                                                       : ModeBorrowingStyle::SameTonic;
    const auto* clip = trackModel.getActiveClip();
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
    const int targetTonic = style == ModeBorrowingStyle::SameTonic
        ? sourceTonic : state.modeBorrowingTargetRootPc;
    if (!selected.empty())
    {
        const auto plan = ChordEditing::borrow(midiModel, trackModel, selected,
            sourceTonic, sourceScale, targetTonic, state.modeBorrowingScale,
            style, state.modeBorrowingDegree, !state.modeBorrowingCustomSource);
        applyChordEditPlan(plan, "Borrow Chord From Mode");
        return;
    }
    auto* track = trackModel.getActiveInstrumentTrack();
    if (style != ModeBorrowingStyle::SameTonic || track == nullptr || clip == nullptr
        || track->isLocked() || clip->lengthBeats <= 0.0)
        return;
    const ScaleConfig* sourceMode = nullptr;
    const ScaleConfig* targetMode = nullptr;
    for (const auto& scale : ScaleDefinitions::getAllScales())
    {
        if (scale.name.equalsIgnoreCase(sourceScale)
            && scale.intervals.size() == 7) sourceMode = &scale;
        if (scale.name.equalsIgnoreCase(state.modeBorrowingScale)
            && scale.intervals.size() == 7) targetMode = &scale;
    }
    auto validScale = [](const ScaleConfig* scale) {
        return scale != nullptr && scale->intervals.front() == 0
            && scale->intervals.back() <= 11
            && std::adjacent_find(scale->intervals.begin(), scale->intervals.end(),
                [](int a, int b) { return a >= b; }) == scale->intervals.end();
    };
    if (!validScale(sourceMode) || !validScale(targetMode)) return;
    const int degree = std::clamp(state.modeBorrowingDegree, 0, 6);
    auto generated = ChordEditing::createBorrowedChordNotes(targetTonic,
        state.modeBorrowingScale, degree, clip->startBeat,
        std::min(4.0, clip->lengthBeats));
    if (generated.empty() || !isUsableSingleChord(ChordClassifier::identifyChord(
            generated, targetTonic, state.modeBorrowingScale)))
        return;
    if (currentLayout == ViewLayout::ArrangerOnly)
        setViewLayout(preferredKeyEditorLayout);
    if (currentLayout == ViewLayout::DualWindow && detachedKeyEditorWindow != nullptr)
        detachedKeyEditorWindow->toFront(true);
    setActiveFocusArea(FocusArea::KeyEditor);
    pianoRoll->beginChordPlacement(std::move(generated), "Place Borrowed Chord",
        ChordEditing::borrowedChordName(targetTonic, state.modeBorrowingScale,
                                        degree, false));
}

void MainComponent::showSecondaryDominantSettings()
{
    InspectorComponent* left = leftInspector.get();
    InspectorComponent* right = rightInspector.get();
    if (currentLayout == ViewLayout::DualWindow && detachedKeyEditorWindow != nullptr)
        if (auto* container = detachedKeyEditorWindow->getContainer())
        {
            left = container->getLeftInspector();
            right = container->getRightInspector();
            detachedKeyEditorWindow->toFront(true);
        }

    if (right != nullptr && right->isSectionVisibleInThisDock(4))
    {
        if (right == rightInspector.get() && !isRightZoneVisible)
            toggleRightZone();
        right->focusSecondaryDominantSettings();
    }
    else if (left != nullptr && left->isSectionVisibleInThisDock(4))
    {
        if (left == leftInspector.get() && !isLeftZoneVisible)
            toggleLeftZone();
        left->focusSecondaryDominantSettings();
    }
}

//==============================================================================
//==============================================================================
// MenuBarModel
//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { tr("File"), tr("Edit"), tr("Transport"), tr("Workspace"), tr("Help") };
}

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String&)
{
    juce::PopupMenu menu;
    if (index == 0) // File
    {
        menu.addItem(Menu_New, tr("New Project"));
        menu.addItem(Menu_Open, tr("Open Project... (Ctrl+O)"));
        menu.addItem(Menu_Save, tr("Save Project (Ctrl+S)"));
        menu.addItem(Menu_SaveAs, tr("Save Project As..."));
        menu.addSeparator();
        menu.addItem(Menu_ImportMidi, tr("Import MIDI File (.mid)..."));
        menu.addItem(Menu_ExportMidi, tr("Export MIDI File (.mid)..."));
        menu.addSeparator();
        menu.addItem(Menu_Preferences, tr("Preferences / Audio & VST..."));
        menu.addSeparator();
        menu.addItem(Menu_Exit, tr("Exit"));
    }
    else if (index == 1) // Edit
    {
        menu.addItem(Menu_Undo, tr("Undo (Ctrl+Z)"), undoManager.canUndo());
        menu.addItem(Menu_Redo, tr("Redo (Ctrl+Y)"), undoManager.canRedo());
        menu.addSeparator();
        menu.addItem(Menu_Copy, tr("Copy (Ctrl+C)"), !midiModel.getSelectedNotes().empty());
        menu.addItem(Menu_Paste, tr("Paste (Ctrl+V)"));
        menu.addItem(Menu_Duplicate, tr("Duplicate (Ctrl+D)"), !midiModel.getSelectedNotes().empty());
        menu.addItem(Menu_Delete, tr("Delete (Del)"), !midiModel.getSelectedNotes().empty());
        menu.addSeparator();
        menu.addItem(Menu_SelectAll, tr("Select All (Ctrl+A)"));
    }
    else if (index == 2) // Transport
    {
        menu.addItem(Menu_PlayPause, audioEngine.isPlaying() ? tr("Pause (Space)") : tr("Play (Space)"));
        menu.addItem(Menu_Stop, tr("Stop"));
        menu.addItem(Menu_LoopToggle, tr("Toggle Loop (/)"), true, midiModel.isLoopEnabled());
        menu.addSeparator();
        menu.addItem(Menu_BarOffset, tr("Bar Number Offset..."));
    }
    else if (index == 3) // Workspace
    {
        menu.addItem(201, tr("Split Layout (Default)"), true, currentLayout == ViewLayout::Split);
        menu.addItem(202, tr("Arranger Only (Full Screen)"), true, currentLayout == ViewLayout::ArrangerOnly);
        menu.addItem(203, tr("Key Editor Only (Full Screen)"), true, currentLayout == ViewLayout::EditorOnly);
        menu.addItem(204, tr("Dual Windows (Detached Key Editor)"), true, currentLayout == ViewLayout::DualWindow);
        menu.addSeparator();
        menu.addItem(205, tr("Save Workspace Preset..."));
        menu.addItem(206, tr("Load Workspace Preset..."));
    }
    else if (index == 4) // Help
    {
        menu.addItem(100, tr("Cubase Shortcuts Guide"));
        menu.addItem(101, tr("About Harmonizer Studio"));
    }
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int)
{
    switch (menuItemID)
    {
        case Menu_New:          newProject(); break;
        case Menu_Open:         openProject(); break;
        case Menu_Save:         saveProject(); break;
        case Menu_SaveAs:       saveProjectAs(); break;
        case Menu_ImportMidi:   importMidiFile(); break;
        case Menu_ExportMidi:   exportMidiFile(); break;
        case Menu_Preferences:  openPreferences(); break;
        case Menu_Exit:         juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;

        case Menu_Undo:         undoManager.undo(); break;
        case Menu_Redo:         undoManager.redo(); break;
        case Menu_Copy:
            if (currentFocusArea == FocusArea::Arranger && trackArranger != nullptr)
                trackArranger->copySelectedEvents();
            else
                midiModel.copySelectedNotes();
            break;
        case Menu_Paste:
        {
            if (currentFocusArea == FocusArea::Arranger && trackArranger != nullptr)
            {
                trackArranger->pasteEvents(audioEngine.getPlayheadBeat());
            }
            else
            {
                double clipStart = -1.0, clipEnd = -1.0;
                if (const auto* clip = trackModel.getActiveClip()) { clipStart = clip->startBeat; clipEnd = clip->startBeat + clip->lengthBeats; }
                midiModel.pasteNotes(audioEngine.getPlayheadBeat(), &undoManager, clipStart, clipEnd, &trackModel);
            }
            break;
        }
        case Menu_Duplicate:
        {
            if (currentFocusArea == FocusArea::Arranger && trackArranger != nullptr)
            {
                trackArranger->duplicateSelectedEvents();
            }
            else
            {
                double clipStart = -1.0, clipEnd = -1.0;
                if (const auto* clip = trackModel.getActiveClip()) { clipStart = clip->startBeat; clipEnd = clip->startBeat + clip->lengthBeats; }
                midiModel.duplicateSelectedNotes(&undoManager, clipStart, clipEnd, &trackModel);
            }
            break;
        }
        case Menu_Delete:
        {
            if (currentFocusArea == FocusArea::Arranger && trackArranger != nullptr)
            {
                trackArranger->deleteSelectedEvents();
            }
            else
            {
                if (midiModel.hasSelection())
                    midiModel.deleteSelectedNotes(&undoManager, &trackModel);
                else if (pianoRoll != nullptr && pianoRoll->getOverviewLane() != nullptr && pianoRoll->getOverviewLane()->hasSelection())
                    pianoRoll->getOverviewLane()->deleteSelectedEvent();
            }
            break;
        }
        case Menu_SelectAll:    midiModel.selectAll(); break;

        case Menu_PlayPause:    audioEngine.isPlaying() ? audioEngine.pause() : audioEngine.play(); break;
        case Menu_Stop:         audioEngine.stop(); break;
        case Menu_LoopToggle:   midiModel.setLoopEnabled(!midiModel.isLoopEnabled()); break;
        case Menu_BarOffset:
        {
            BarOffsetDialog::showModal(trackModel);
            break;
        }

        // Workspace Menu Items
        case 201: setViewLayout(ViewLayout::Split); break;
        case 202: setViewLayout(ViewLayout::ArrangerOnly); break;
        case 203: setViewLayout(ViewLayout::EditorOnly); break;
        case 204: setViewLayout(ViewLayout::DualWindow); break;
        case 205:
        {
            juce::FileChooser chooser(tr("Save Workspace Preset"), juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("HarmonizerStudio/workspaces"), "*.xml");
            if (chooser.browseForFileToSave(true))
            {
                auto f = chooser.getResult();
                if (f != juce::File())
                {
                    if (f.getFileExtension().isEmpty()) f = f.withFileExtension("xml");
                    saveWorkspaceXml(f);
                }
            }
            break;
        }
        case 206:
        {
            juce::FileChooser chooser(tr("Load Workspace Preset"), juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("HarmonizerStudio/workspaces"), "*.xml");
            if (chooser.browseForFileToOpen())
            {
                auto f = chooser.getResult();
                if (f.existsAsFile())
                    loadWorkspaceXml(f);
            }
            break;
        }

        case 100: openPreferences(); break;
        case 101:
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "About Harmonizer Studio",
                "Harmonizer Studio v1.0.0 (Phase 1 Build)\n"
                "A Cubase-inspired MIDI Reharmonization & VST3 Workstation.\n\n"
                "Created for professional music composers.");
            break;
    }
}

//==============================================================================
// File Operations
//==============================================================================
void MainComponent::newProject()
{
    audioEngine.stop();
    midiModel.resetToDefaults();
    trackModel.resetToDefaults();
    undoManager.clearUndoHistory();
    currentProjectFile = juce::File();
}

void MainComponent::openProject()
{
    juce::FileChooser chooser("Open Harmonizer Project", juce::File(), "*.reharm");
    if (chooser.browseForFileToOpen())
    {
        auto file = chooser.getResult();
        if (file.existsAsFile())
        {
            audioEngine.stop();
            if (ProjectFile::loadProject(midiModel, file, &trackModel))
            {
                currentProjectFile = file;
                undoManager.clearUndoHistory();
            }
            else
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Open Project Failed", "The selected project could not be loaded.");
        }
    }
}

void MainComponent::saveProject()
{
    if (currentProjectFile.existsAsFile())
    {
        audioEngine.capturePluginStatesToTracks();
        if (!ProjectFile::saveProject(midiModel, currentProjectFile, &trackModel))
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                "Save Project Failed", "The project could not be saved.");
    }
    else
    {
        saveProjectAs();
    }
}

void MainComponent::saveProjectAs()
{
    juce::FileChooser chooser("Save Harmonizer Project", juce::File(), "*.reharm");
    if (chooser.browseForFileToSave(true))
    {
        auto file = chooser.getResult();
        if (file != juce::File())
        {
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension("reharm");
            audioEngine.capturePluginStatesToTracks();
            if (ProjectFile::saveProject(midiModel, file, &trackModel))
                currentProjectFile = file;
            else
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Save Project Failed", "The project could not be saved.");
        }
    }
}

void MainComponent::importMidiFile()
{
    juce::FileChooser chooser("Import Standard MIDI File", juce::File(), "*.mid;*.midi");
    if (chooser.browseForFileToOpen())
    {
        auto file = chooser.getResult();
        if (file.existsAsFile())
        {
            audioEngine.stop();
            if (ProjectFile::importMidi(midiModel, file, &trackModel))
            {
                undoManager.clearUndoHistory();
                pianoRoll->scrollToBeat(0.0);
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "MIDI Import Failed", "Could not parse the selected MIDI file.");
            }
        }
    }
}

void MainComponent::exportMidiFile()
{
    juce::FileChooser chooser("Export Standard MIDI File", juce::File(), "*.mid");
    if (chooser.browseForFileToSave(true))
    {
        auto file = chooser.getResult();
        if (file != juce::File())
        {
            double rStart = midiModel.getLoopStart();
            double rEnd = midiModel.getLoopEnd();
            if (!ProjectFile::exportMidi(midiModel, file, &trackModel, rStart, rEnd))
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "MIDI Export Failed", "The MIDI file could not be written.");
        }
    }
}

//==============================================================================
// Drag & Drop
//==============================================================================
bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const juce::File file(f);
        if (file.hasFileExtension(".mid") || file.hasFileExtension(".midi")
            || file.hasFileExtension(".reharm") || isDroppedAudioFile(file))
            return true;
    }
    return false;
}

void MainComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    // A project file replaces the whole project; media files add timeline parts.
    for (const auto& f : files)
    {
        juce::File file(f);
        if (file.hasFileExtension(".reharm"))
        {
            audioEngine.stop();
            if (ProjectFile::loadProject(midiModel, file, &trackModel))
            {
                currentProjectFile = file;
                undoManager.clearUndoHistory();
            }
            else
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Open Project Failed", "The dropped project could not be loaded.");
            return;
        }
    }

    int targetTrackId = -1;
    double dropBeat = std::max(0.0, audioEngine.getPlayheadBeat());
    if (midiModel.isSnapEnabled())
        dropBeat = std::max(0.0, midiModel.quantizeBeat(dropBeat));
    if (trackArranger != nullptr)
    {
        const auto localPoint = trackArranger->getLocalPoint(this, juce::Point<int>(x, y));
        trackArranger->getFileDropTarget(localPoint, targetTrackId, dropBeat);
    }

    bool usedMidiLane = false;
    bool usedAudioLane = false;
    juce::StringArray failedFiles;
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (file.hasFileExtension(".mid") || file.hasFileExtension(".midi"))
        {
            auto* target = trackModel.getTrackById(targetTrackId);
            const int preferred = !usedMidiLane && target != nullptr
                && target->getType() == TrackType::Instrument ? targetTrackId : -1;
            if (ProjectFile::importMidiAsClips(midiModel, file, trackModel,
                                               preferred, dropBeat, &undoManager))
                usedMidiLane = true;
            else
                failedFiles.add(file.getFileName());
        }
        else if (isDroppedAudioFile(file))
        {
            auto* target = trackModel.getTrackById(targetTrackId);
            const int preferred = !usedAudioLane && target != nullptr
                && target->getType() == TrackType::Audio ? targetTrackId : -1;
            if (ProjectFile::importAudioAsClip(trackModel, file, preferred, dropBeat,
                                               midiModel.getBpm(), &undoManager))
                usedAudioLane = true;
            else
                failedFiles.add(file.getFileName());
        }
    }

    if (!failedFiles.isEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            juce::String::fromUTF8(u8"文件导入失败"),
            juce::String::fromUTF8(u8"无法读取以下 MIDI 或音频文件：\n") + failedFiles.joinIntoString("\n"));
}

void MainComponent::toggleViewLayout()
{
    if (currentLayout == ViewLayout::Split)
        setViewLayout(ViewLayout::ArrangerOnly);
    else if (currentLayout == ViewLayout::ArrangerOnly)
        setViewLayout(ViewLayout::EditorOnly);
    else if (currentLayout == ViewLayout::EditorOnly)
        setViewLayout(ViewLayout::DualWindow);
    else
        setViewLayout(ViewLayout::Split);
}

void MainComponent::setViewLayout(ViewLayout layout)
{
    if (layout == ViewLayout::ArrangerOnly && pianoRoll != nullptr)
        pianoRoll->cancelSecondaryDominantPlacement();

    if (layout == ViewLayout::Split || layout == ViewLayout::DualWindow)
        preferredKeyEditorLayout = layout;

    if (currentLayout == layout) return;

    if (currentLayout == ViewLayout::DualWindow && layout != ViewLayout::DualWindow)
    {
        // Dock back Piano Roll into MainComponent
        if (detachedKeyEditorWindow)
        {
            detachedKeyEditorWindow->saveGeometry();
            detachedKeyEditorWindow->clearContentComponent();
            detachedKeyEditorWindow.reset();
        }
        addAndMakeVisible(pianoRoll.get());
    }

    currentLayout = layout;

    if (currentLayout == ViewLayout::DualWindow)
    {
        // Remove Piano Roll from MainComponent and host in floating window
        removeChildComponent(pianoRoll.get());
        juce::Component::SafePointer<MainComponent> safeThis(this);
        detachedKeyEditorWindow = std::make_unique<KeyEditorWindow>(*this, pianoRoll.get(), [safeThis]() {
            if (auto* owner = safeThis.getComponent())
            {
                owner->preferredKeyEditorLayout = ViewLayout::DualWindow;
                owner->setViewLayout(ViewLayout::ArrangerOnly);
            }
        });
        pianoRoll->setVisible(true);

        // Restore floating window geometry from AppStateManager
        const auto& st = AppStateManager::getInstance();
        int w = (st.floatWinW >= 700) ? st.floatWinW : 1000;
        int h = (st.floatWinH >= 450) ? st.floatWinH : 620;
        int x = st.floatWinX;
        int y = st.floatWinY;

        detachedKeyEditorWindow->setBounds(x, y, w, h);
        // Keep it on-screen, or center if coordinates are at top-left zero or off-screen
        auto screens = juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
        if (x < 50 && y < 50)
        {
            detachedKeyEditorWindow->centreWithSize(w, h);
        }
        else if (!screens.intersects(detachedKeyEditorWindow->getBounds().reduced(50)))
        {
            detachedKeyEditorWindow->centreWithSize(w, h);
        }
        if (st.floatWinAlwaysOnTop)
            detachedKeyEditorWindow->setAlwaysOnTop(true);

        detachedKeyEditorWindow->setVisible(true);
        detachedKeyEditorWindow->markInitialized();
    }

    AppStateManager::getInstance().viewLayout = (int)currentLayout;
    AppStateManager::getInstance().save();

    juce::MenuBarModel::menuItemsChanged();
    if (toolbar)
    {
        toolbar->setViewMode((int)currentLayout);
        toolbar->setZoneStates(isLeftZoneVisible, isRightZoneVisible);
    }
    resized();
}

void MainComponent::saveWorkspaceXml(const juce::File& file)
{
    juce::XmlElement root("HarmonizerWorkspace");
    root.setAttribute("version", 4);
    root.setAttribute("layout", (int)currentLayout);
    root.setAttribute("splitRatio", (double)splitRatio);
    root.setAttribute("dockOnRight", rightInspector && rightInspector->hasVisibleSections());

    juce::XmlElement* sectionsElem = root.createNewChildElement("Sections");
    for (int i = 0; i < kInspectorSectionCount; ++i)
    {
        juce::XmlElement* s = sectionsElem->createNewChildElement("Section");
        s->setAttribute("index", i);
        s->setAttribute("position", (int)(leftInspector ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, i) : SectionDockPosition::Left));
        s->setAttribute("keyEditorPosition", (int)(leftInspector ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i) : SectionDockPosition::Left));
        s->setAttribute("defaultPosition", (int)(leftInspector ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Default, i) : SectionDockPosition::Left));
    }

    juce::XmlElement* orderElem = root.createNewChildElement("SectionOrder");
    if (leftInspector)
    {
        for (int sec : leftInspector->getSectionOrder())
        {
            juce::XmlElement* o = orderElem->createNewChildElement("Item");
            o->setAttribute("section", sec);
        }
    }

    juce::TemporaryFile temporary(file);
    if (root.writeTo(temporary.getFile()) && temporary.overwriteTargetFileWithTemporary())
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, tr("Save Workspace"), tr("Workspace preset saved successfully!"));
    else
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, tr("Save Workspace"), tr("Workspace preset could not be saved."));
}

void MainComponent::loadWorkspaceXml(const juce::File& file)
{
    auto xml = juce::parseXML(file);
    if (!xml || xml->getTagName() != "HarmonizerWorkspace")
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, tr("Load Workspace"), tr("Invalid workspace file."));
        return;
    }

    int lay = xml->getIntAttribute("layout", 0);
    double loadedRatio = xml->getDoubleAttribute("splitRatio", 0.38);
    if (lay < (int)ViewLayout::Split || lay > (int)ViewLayout::DualWindow || !std::isfinite(loadedRatio))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, tr("Load Workspace"), tr("Invalid workspace file."));
        return;
    }
    std::vector<int> loadedOrder;
    if (auto* orderElement = xml->getChildByName("SectionOrder"))
    {
        std::array<bool, kInspectorSectionCount> seen{};
        for (auto* item : orderElement->getChildIterator())
        {
            int section = item->getIntAttribute("section", -1);
            // In workspace versions before Quick Chord, id 14 belonged to a
            // removed cadence module. In version 4 it is Quick Chord itself.
            if (section == kQuickChordSectionId
                && xml->getIntAttribute("version", 0) < 4) continue;
            if (!isInspectorSectionId(section) || seen[(size_t)section])
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, tr("Load Workspace"), tr("Invalid workspace file."));
                return;
            }
            seen[(size_t)section] = true;
            loadedOrder.push_back(section);
        }
        if (loadedOrder.size() < 10 || loadedOrder.size() > kInspectorSectionCount)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, tr("Load Workspace"), tr("Invalid workspace file."));
            return;
        }
        for (int id : kDefaultSectionOrder)
            if (!seen[(size_t)id]) loadedOrder.push_back(id); // Keep older workspace presets usable.
    }
    if (auto* sectionsElement = xml->getChildByName("Sections"))
        for (auto* section : sectionsElement->getChildIterator())
            for (const char* name : { "position", "keyEditorPosition", "defaultPosition" })
                if (section->hasAttribute(name))
                {
                    int value = section->getIntAttribute(name, -1);
                    if (value < (int)SectionDockPosition::Left || value > (int)SectionDockPosition::Hidden)
                    {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, tr("Load Workspace"), tr("Invalid workspace file."));
                        return;
                    }
                }
    splitRatio = std::clamp((float)loadedRatio, 0.15f, 0.85f);

    if (auto* sectionsElem = xml->getChildByName("Sections"))
    {
        for (auto* s : sectionsElem->getChildIterator())
        {
            int idx = s->getIntAttribute("index", -1);
            if (isInspectorSectionId(idx)
                && (xml->getIntAttribute("version", 0) >= 4
                    || idx != kQuickChordSectionId))
            {
                SectionDockPosition pos = SectionDockPosition::Left;
                if (s->hasAttribute("position"))
                    pos = (SectionDockPosition)s->getIntAttribute("position", 0);
                else
                    pos = s->getBoolAttribute("visible", true) ? SectionDockPosition::Left : SectionDockPosition::Hidden;

                SectionDockPosition keyPos = pos;
                if (s->hasAttribute("keyEditorPosition"))
                    keyPos = (SectionDockPosition)s->getIntAttribute("keyEditorPosition", 0);

                SectionDockPosition defPos = pos;
                if (s->hasAttribute("defaultPosition"))
                    defPos = (SectionDockPosition)s->getIntAttribute("defaultPosition", 0);

                if (leftInspector)
                {
                    leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, idx, pos);
                    leftInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, idx, keyPos);
                    leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Default, idx, defPos);
                }
                if (rightInspector)
                {
                    rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, idx, pos);
                    rightInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, idx, keyPos);
                    rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Default, idx, defPos);
                }
            }
        }
    }

    if (!loadedOrder.empty())
    {
        if (leftInspector) leftInspector->setSectionOrder(loadedOrder);
        if (rightInspector) rightInspector->setSectionOrder(loadedOrder);
    }

    if (detachedKeyEditorWindow)
        detachedKeyEditorWindow->syncSectionsFromOwner();

    setViewLayout((ViewLayout)lay);
    collectAndSaveUIState();
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, tr("Load Workspace"), tr("Workspace preset loaded successfully!"));
}

//==============================================================================
// Keyboard Shortcuts (Cubase Style via KeyCommandManager)
//==============================================================================
bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.isKeyCode(juce::KeyPress::escapeKey) && pianoRoll != nullptr
        && pianoRoll->isSecondaryDominantPlacementActive())
    {
        pianoRoll->cancelSecondaryDominantPlacement();
        return true;
    }

    // 0. Focus-dependent routing: if Arranger is focused, give trackArranger priority
    if (currentFocusArea == FocusArea::Arranger && trackArranger != nullptr)
    {
        if (trackArranger->keyPressed(key))
            return true;
    }

    int cmd = KeyCommandManager::getInstance().findMatchingCommandId(key);
    if (cmd != 0)
    {
        switch (cmd)
        {
            case Cmd_Undo:
                undoManager.undo();
                return true;
            case Cmd_Redo:
                undoManager.redo();
                return true;
            case Cmd_DeleteTrack:
                if (currentFocusArea == FocusArea::Arranger && trackArranger != nullptr)
                    return trackArranger->keyPressed(key);
                return true;
            case Cmd_ToggleMetronome:
                audioEngine.setMetronomeEnabled(!audioEngine.isMetronomeEnabled());
                if (transport) transport->refreshMetronomeState();
                return true;
            case Cmd_Copy:
                if (currentFocusArea == FocusArea::Arranger && trackArranger != nullptr)
                    trackArranger->copySelectedEvents();
                else
                    midiModel.copySelectedNotes();
                return true;
            case Cmd_Paste:
                if (currentFocusArea == FocusArea::Arranger && trackArranger != nullptr)
                    trackArranger->pasteEvents(audioEngine.getPlayheadBeat());
                else
                {
                    double clipStart = -1.0, clipEnd = -1.0;
                    if (const auto* clip = trackModel.getActiveClip()) { clipStart = clip->startBeat; clipEnd = clip->startBeat + clip->lengthBeats; }
                    midiModel.pasteNotes(audioEngine.getPlayheadBeat(), &undoManager, clipStart, clipEnd, &trackModel);
                }
                return true;
            case Cmd_Duplicate:
                if (currentFocusArea == FocusArea::Arranger && trackArranger != nullptr)
                    trackArranger->duplicateSelectedEvents();
                else
                {
                    double clipStart = -1.0, clipEnd = -1.0;
                    if (const auto* clip = trackModel.getActiveClip()) { clipStart = clip->startBeat; clipEnd = clip->startBeat + clip->lengthBeats; }
                    midiModel.duplicateSelectedNotes(&undoManager, clipStart, clipEnd, &trackModel);
                }
                return true;
            case Cmd_Delete:
            {
                if (currentFocusArea == FocusArea::KeyEditor)
                {
                    if (midiModel.hasSelection())
                    {
                        midiModel.deleteSelectedNotes(&undoManager, &trackModel);
                        return true;
                    }

                    if (pianoRoll != nullptr && pianoRoll->getOverviewLane() != nullptr && pianoRoll->getOverviewLane()->hasSelection())
                    {
                        pianoRoll->getOverviewLane()->deleteSelectedEvent();
                        return true;
                    }
                    return true;
                }
                else if (currentFocusArea == FocusArea::Arranger)
                {
                    if (trackArranger != nullptr && trackModel.getSelectedArrangerEventCount() > 0)
                    {
                        trackArranger->deleteSelectedEvents();
                        return true;
                    }
                    return true;
                }
                else
                {
                    if (midiModel.hasSelection())
                    {
                        midiModel.deleteSelectedNotes(&undoManager, &trackModel);
                        return true;
                    }
                    if (pianoRoll != nullptr && pianoRoll->getOverviewLane() != nullptr && pianoRoll->getOverviewLane()->hasSelection())
                    {
                        pianoRoll->getOverviewLane()->deleteSelectedEvent();
                        return true;
                    }
                    if (trackArranger != nullptr && trackModel.getSelectedArrangerEventCount() > 0)
                    {
                        trackArranger->deleteSelectedEvents();
                        return true;
                    }
                }
                break;
            }
            case Cmd_SelectAll:
                if (currentFocusArea == FocusArea::Arranger && trackArranger != nullptr)
                    trackModel.selectAllArrangerEvents();
                else
                    midiModel.selectAll();
                return true;
            case Cmd_OctaveUp:
            case Cmd_OctaveDown:
            {
                auto* focused = juce::Component::getCurrentlyFocusedComponent();
                if (currentFocusArea != FocusArea::KeyEditor || pianoRoll == nullptr
                    || pianoRoll->isSecondaryDominantPlacementActive()
                    || focused == nullptr
                    || (focused != pianoRoll.get() && !pianoRoll->isParentOf(focused)))
                    return false;
                NoteEditOperations::transposeSelectedOctave(midiModel, trackModel, undoManager,
                    cmd == Cmd_OctaveUp ? 1 : -1);
                return true;
            }

            case Cmd_ToolSelect:
                midiModel.setCurrentTool(ToolType::Select);
                return true;
            case Cmd_ToolSplit:
                midiModel.setCurrentTool(ToolType::Split);
                return true;
            case Cmd_ToolErase:
                midiModel.setCurrentTool(ToolType::Erase);
                return true;
            case Cmd_ToolDraw:
                midiModel.setCurrentTool(ToolType::Draw);
                return true;

            case Cmd_PlayPause:
                if (audioEngine.isPlaying())
                    audioEngine.pause();
                else
                    audioEngine.play();
                return true;
            case Cmd_Stop:
                audioEngine.stop();
                return true;
            case Cmd_ToggleLoop:
                midiModel.setLoopEnabled(!midiModel.isLoopEnabled());
                return true;
            case Cmd_RewindStart:
                audioEngine.setPlayheadBeat(0.0);
                return true;

            case Cmd_SnapToggle:
                midiModel.setSnapEnabled(!midiModel.isSnapEnabled());
                return true;
            case Cmd_TogglePlayheadFollow:
                midiModel.togglePlayheadFollow();
                return true;
            case Cmd_SecondaryDominant:
                startSecondaryDominantPlacement();
                return true;
            case Cmd_ReharmonizeOptions:
                showSecondaryDominantSettings();
                return true;
            case Cmd_ApplyReharmonization:
                applyReharmonization();
                return true;
            case Cmd_ZoomInX:
                pianoRoll->zoomInHorizontal();
                return true;
            case Cmd_ZoomOutX:
                pianoRoll->zoomOutHorizontal();
                return true;
            case Cmd_ZoomInY:
                pianoRoll->zoomInVertical();
                return true;
            case Cmd_ZoomOutY:
                pianoRoll->zoomOutVertical();
                return true;

            default:
                break;
        }
    }

    // Fallback: Delete key if not mapped
    if (key.isKeyCode(juce::KeyPress::deleteKey) || key.isKeyCode(juce::KeyPress::backspaceKey))
    {
        if (currentFocusArea == FocusArea::KeyEditor)
        {
            if (midiModel.hasSelection())
            {
                midiModel.deleteSelectedNotes(&undoManager, &trackModel);
                return true;
            }

            if (pianoRoll != nullptr && pianoRoll->getOverviewLane() != nullptr && pianoRoll->getOverviewLane()->hasSelection())
            {
                pianoRoll->getOverviewLane()->deleteSelectedEvent();
                return true;
            }
            return true;
        }
        else if (currentFocusArea == FocusArea::Arranger)
        {
            if (trackArranger != nullptr && trackModel.getSelectedArrangerEventCount() > 0)
            {
                trackArranger->deleteSelectedEvents();
                return true;
            }
            return true;
        }
        else
        {
            if (midiModel.hasSelection())
            {
                midiModel.deleteSelectedNotes(&undoManager, &trackModel);
                return true;
            }
            if (pianoRoll != nullptr && pianoRoll->getOverviewLane() != nullptr && pianoRoll->getOverviewLane()->hasSelection())
            {
                pianoRoll->getOverviewLane()->deleteSelectedEvent();
                return true;
            }
            if (trackArranger != nullptr && trackModel.getSelectedArrangerEventCount() > 0)
            {
                trackArranger->deleteSelectedEvents();
                return true;
            }
        }
    }

    return false;
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgDeep());
}

void MainComponent::setActiveFocusArea(FocusArea area)
{
    currentFocusArea = area;
    if (area == FocusArea::Arranger)
    {
        if (trackArranger) trackArranger->setFocusedArea(true);
        if (pianoRoll) pianoRoll->setFocusedArea(false);
        if (leftInspector && leftInspector->getDockContext() != InspectorComponent::DockContext::Arranger)
            leftInspector->setDockContext(InspectorComponent::DockContext::Arranger);
        if (rightInspector && rightInspector->getDockContext() != InspectorComponent::DockContext::Arranger)
            rightInspector->setDockContext(InspectorComponent::DockContext::Arranger);
    }
    else if (area == FocusArea::KeyEditor)
    {
        if (trackArranger) trackArranger->setFocusedArea(false);
        if (pianoRoll) pianoRoll->setFocusedArea(true);
        if (leftInspector && leftInspector->getDockContext() != InspectorComponent::DockContext::KeyEditor)
            leftInspector->setDockContext(InspectorComponent::DockContext::KeyEditor);
        if (rightInspector && rightInspector->getDockContext() != InspectorComponent::DockContext::KeyEditor)
            rightInspector->setDockContext(InspectorComponent::DockContext::KeyEditor);
    }
    else
    {
        if (trackArranger) trackArranger->setFocusedArea(false);
        if (pianoRoll) pianoRoll->setFocusedArea(false);
        if (leftInspector && leftInspector->getDockContext() != InspectorComponent::DockContext::Default)
            leftInspector->setDockContext(InspectorComponent::DockContext::Default);
        if (rightInspector && rightInspector->getDockContext() != InspectorComponent::DockContext::Default)
            rightInspector->setDockContext(InspectorComponent::DockContext::Default);
    }
    resized();
}

void MainComponent::toggleLeftZone()
{
    isLeftZoneVisible = !isLeftZoneVisible;
    if (toolbar)
        toolbar->setZoneStates(isLeftZoneVisible, isRightZoneVisible);
    auto& st = AppStateManager::getInstance();
    st.leftZoneVisible = isLeftZoneVisible;
    st.save();
    resized();
}

void MainComponent::toggleRightZone()
{
    isRightZoneVisible = !isRightZoneVisible;
    if (toolbar)
        toolbar->setZoneStates(isLeftZoneVisible, isRightZoneVisible);
    auto& st = AppStateManager::getInstance();
    st.rightZoneVisible = isRightZoneVisible;
    st.save();
    resized();
}

void MainComponent::showSectionsManager(int initialContextIndex, bool fromDetached)
{
    SectionsManagerDialog::ManagerData mgrData;
    auto* detachedContainer = fromDetached && detachedKeyEditorWindow
        ? detachedKeyEditorWindow->getContainer() : nullptr;
    mgrData.detachedZoneControls = detachedContainer != nullptr;
    mgrData.leftZoneVisible = detachedContainer ? detachedContainer->isLeftZoneShown() : isLeftZoneVisible;
    mgrData.rightZoneVisible = detachedContainer ? detachedContainer->isRightZoneShown() : isRightZoneVisible;
    if (initialContextIndex < 0)
    {
        initialContextIndex = 1;
        if (leftInspector && leftInspector->getDockContext() == InspectorComponent::DockContext::KeyEditor)
            initialContextIndex = 0;
        else if (const auto* track = trackModel.getTrackById(trackModel.getActiveTrackId()))
        {
            switch (track->getType())
            {
                case TrackType::Chord:         initialContextIndex = 2; break;
                case TrackType::Mode:          initialContextIndex = 3; break;
                case TrackType::Marker:        initialContextIndex = 4; break;
                case TrackType::TimeSignature: initialContextIndex = 5; break;
                case TrackType::Folder:        initialContextIndex = 6; break;
                case TrackType::Audio:         initialContextIndex = 7; break;
                default: break;
            }
        }
    }
    mgrData.initialContextIndex = initialContextIndex;
    std::vector<int> curOrder = leftInspector ? leftInspector->getSectionOrder()
        : std::vector<int>(kDefaultSectionOrder.begin(), kDefaultSectionOrder.end());
    if (curOrder.empty())
        curOrder.assign(kDefaultSectionOrder.begin(), kDefaultSectionOrder.end());

    // 1. Key Editor modules
    static const char* keySecNames[kInspectorSectionCount] = {
        "Note Inspector",
        "Mode Colour",
        "Real-time Chord Analysis",
        "Instrument / VST3",
        "Secondary Dominant Module",
        nullptr, nullptr, nullptr, nullptr, nullptr,
        "Reharmonization", nullptr, "Chord Shape & Voicing", "Mode Borrowing",
        "Quick Chord"
    };
    std::set<int> addedKey;
    for (int id : curOrder)
    {
        if (isNoteTrackSectionId(id))
        {
            SectionDockItem item;
            item.id = id;
            item.name = tr(keySecNames[id]);
            item.position = leftInspector ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, id) : SectionDockPosition::Left;
            mgrData.keyEditorItems.push_back(item);
            addedKey.insert(id);
        }
    }
    for (int id : kNoteTrackSectionIds)
    {
        if (addedKey.find(id) == addedKey.end())
        {
            SectionDockItem item;
            item.id = id;
            item.name = tr(keySecNames[id]);
            item.position = leftInspector ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, id) : SectionDockPosition::Left;
            mgrData.keyEditorItems.push_back(item);
        }
    }

    // 2. Instrument Track modules
    static const char* instSecNames[kInspectorSectionCount] = {
        "Track & Note Inspector",
        "Mode Colour",
        "Real-time Chord Analysis",
        "Instrument / VST3",
        "Secondary Dominant Module",
        nullptr, nullptr, nullptr, nullptr, nullptr,
        "Reharmonization", nullptr, "Chord Shape & Voicing", "Mode Borrowing",
        "Quick Chord"
    };
    std::set<int> addedInst;
    for (int id : curOrder)
    {
        if (isNoteTrackSectionId(id))
        {
            SectionDockItem item;
            item.id = id;
            item.name = tr(instSecNames[id]);
            item.position = leftInspector ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, id) : SectionDockPosition::Left;
            mgrData.instTrackItems.push_back(item);
            addedInst.insert(id);
        }
    }
    for (int id : kNoteTrackSectionIds)
    {
        if (addedInst.find(id) == addedInst.end())
        {
            SectionDockItem item;
            item.id = id;
            item.name = tr(instSecNames[id]);
            item.position = leftInspector ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, id) : SectionDockPosition::Left;
            mgrData.instTrackItems.push_back(item);
        }
    }

    // 3. Chord Track (5)
    {
        SectionDockItem item;
        item.id = 5;
        item.name = tr("Chord Track Inspector");
        item.position = leftInspector ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, 5) : SectionDockPosition::Left;
        mgrData.chordTrackItems.push_back(item);
    }

    // 4. Mode Track (6)
    {
        SectionDockItem item;
        item.id = 6;
        item.name = tr("Mode Track Inspector");
        item.position = leftInspector ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, 6) : SectionDockPosition::Left;
        mgrData.modeTrackItems.push_back(item);
    }

    // 5. Marker Track (7)
    {
        SectionDockItem item;
        item.id = 7;
        item.name = tr("Marker Inspector");
        item.position = leftInspector ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, 7) : SectionDockPosition::Left;
        mgrData.markerTrackItems.push_back(item);
    }

    // 6. TimeSig Track (8)
    {
        SectionDockItem item;
        item.id = 8;
        item.name = tr("Time Signature Track Inspector");
        item.position = leftInspector ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, 8) : SectionDockPosition::Left;
        mgrData.timeSigTrackItems.push_back(item);
    }

    // 7. Folder Track (9)
    {
        SectionDockItem item;
        item.id = 9;
        item.name = tr("Folder Track Inspector");
        item.position = leftInspector ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, 9) : SectionDockPosition::Left;
        mgrData.folderTrackItems.push_back(item);
    }

    // 8. Audio Track (11)
    {
        SectionDockItem item;
        item.id = kAudioTrackSectionId;
        item.name = tr("Audio Track");
        item.position = leftInspector
            ? leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger, kAudioTrackSectionId)
            : SectionDockPosition::Left;
        mgrData.audioTrackItems.push_back(item);
    }

    juce::Component::SafePointer<MainComponent> safeThis(this);
    SectionsManagerDialog::showModal(mgrData, [this, safeThis, curOrder](const SectionsManagerDialog::ManagerData& newData) {
        if (safeThis.getComponent() == nullptr) return;
        if (isSyncingSections) return;
        juce::ScopedValueSetter<bool> svs(isSyncingSections, true);

        // Apply Key Editor items
        for (const auto& item : newData.keyEditorItems)
        {
            if (leftInspector) leftInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, item.id, item.position);
            if (rightInspector) rightInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, item.id, item.position);
        }

        // Apply Instrument Track items
        for (const auto& item : newData.instTrackItems)
        {
            if (leftInspector) {
                leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
                leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Default, item.id, item.position);
            }
            if (rightInspector) {
                rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
                rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Default, item.id, item.position);
            }
        }

        // Apply Chord Track item
        for (const auto& item : newData.chordTrackItems)
        {
            if (leftInspector) leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
            if (rightInspector) rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
        }

        // Apply Mode Track item
        for (const auto& item : newData.modeTrackItems)
        {
            if (leftInspector) leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
            if (rightInspector) rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
        }

        // Apply Marker Track item
        for (const auto& item : newData.markerTrackItems)
        {
            if (leftInspector) leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
            if (rightInspector) rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
        }

        // Apply TimeSig Track item
        for (const auto& item : newData.timeSigTrackItems)
        {
            if (leftInspector) leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
            if (rightInspector) rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
        }

        // Apply Folder Track item
        for (const auto& item : newData.folderTrackItems)
        {
            if (leftInspector) leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
            if (rightInspector) rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
        }

        for (const auto& item : newData.audioTrackItems)
        {
            if (leftInspector) leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
            if (rightInspector) rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger, item.id, item.position);
        }

        // The editor and instrument tabs share one order. Keep the other track
        // inspectors in their existing positions when applying that order.
        std::vector<int> finalOrder = curOrder;
        auto shared = newData.instTrackItems.begin();
        for (int& id : finalOrder)
        {
            if (isNoteTrackSectionId(id))
            {
                if (shared != newData.instTrackItems.end())
                    id = (shared++)->id;
            }
        }

        if (!finalOrder.empty())
        {
            if (leftInspector) leftInspector->setSectionOrder(finalOrder);
            if (rightInspector) rightInspector->setSectionOrder(finalOrder);
        }

        if (detachedKeyEditorWindow)
        {
            detachedKeyEditorWindow->syncSectionsFromOwner();
        }

        // Persist inspector layout immediately
        if (leftInspector)
        {
            auto& st = AppStateManager::getInstance();
            st.inspectorStateValid = true;
            for (int i = 0; i < kInspectorSectionCount; ++i)
            {
                st.sectionPositions[i][0] = (int)leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger,  i);
                st.sectionPositions[i][1] = (int)leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i);
                st.sectionPositions[i][2] = (int)leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Default,   i);
            }
            const auto& order = leftInspector->getSectionOrder();
            for (int i = 0; i < std::min((int)order.size(), kInspectorSectionCount); ++i)
                st.sectionOrder[i] = order[i];
            st.save();
        }

        if (leftInspector) leftInspector->applyCustomModuleSettings();
        if (rightInspector) rightInspector->applyCustomModuleSettings();

        if (newData.detachedZoneControls)
        {
            if (detachedKeyEditorWindow)
            {
                if (auto* container = detachedKeyEditorWindow->getContainer())
                {
                    if (container->isLeftZoneShown() != newData.leftZoneVisible)
                        container->toggleDetachedLeftZone();
                    if (container->isRightZoneShown() != newData.rightZoneVisible)
                        container->toggleDetachedRightZone();
                }
            }
        }
        else
        {
            if (isLeftZoneVisible != newData.leftZoneVisible)
                toggleLeftZone();
            if (isRightZoneVisible != newData.rightZoneVisible)
                toggleRightZone();
        }

        resized();
    }, [this, safeThis]() {
        if (safeThis.getComponent() == nullptr) return;
        if (leftInspector) leftInspector->applyCustomModuleSettings();
        if (rightInspector) rightInspector->applyCustomModuleSettings();
        if (detachedKeyEditorWindow) detachedKeyEditorWindow->syncSectionsFromOwner();
        refreshQuickChordMode();
        resized();
    });
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Menu Bar (top)
    menuBar->setBounds(area.removeFromTop(24));

    // Toolbar
    toolbar->setBounds(area.removeFromTop(44));
    toolbar->setViewMode((int)currentLayout);
    toolbar->setZoneStates(isLeftZoneVisible, isRightZoneVisible);

    // Transport (bottom)
    transport->setBounds(area.removeFromBottom(48));

    // Dual Dock Inspectors (Left and Right)
    auto& st = AppStateManager::getInstance();
    int leftW = st.leftZoneWidth;
    int rightW = st.rightZoneWidth;

    if (isLeftZoneVisible && leftInspector)
    {
        const int displayWidth = leftInspector->hasVisibleSections() ? leftW : std::min(leftW, 154);
        leftInspector->setBounds(area.removeFromLeft(displayWidth));
        if (leftSplitterBar)
        {
            leftSplitterBar->setVisible(true);
            leftSplitterBar->setBounds(area.removeFromLeft(5));
        }
    }
    else
    {
        if (leftInspector) leftInspector->setBounds(0, 0, 0, 0);
        if (leftSplitterBar) leftSplitterBar->setVisible(false);
    }

    if (isRightZoneVisible && rightInspector)
    {
        const int displayWidth = rightInspector->hasVisibleSections() ? rightW : std::min(rightW, 154);
        rightInspector->setBounds(area.removeFromRight(displayWidth));
        if (rightSplitterBar)
        {
            rightSplitterBar->setVisible(true);
            rightSplitterBar->setBounds(area.removeFromRight(5));
        }
    }
    else
    {
        if (rightInspector) rightInspector->setBounds(0, 0, 0, 0);
        if (rightSplitterBar) rightSplitterBar->setVisible(false);
    }

    // Center area divided between Arranger and Piano Roll according to currentLayout
    if (currentLayout == ViewLayout::Split)
    {
        trackArranger->setVisible(true);
        splitterBar->setVisible(true);
        pianoRoll->setVisible(true);

        int totalH = area.getHeight();
        int splitterH = 6;
        int availableH = totalH - splitterH;
        int arrangerH = std::clamp(juce::roundToInt(availableH * splitRatio), 100, availableH - 100);

        trackArranger->setBounds(area.removeFromTop(arrangerH));
        splitterBar->setBounds(area.removeFromTop(splitterH));
        pianoRoll->setBounds(area);
    }
    else if (currentLayout == ViewLayout::ArrangerOnly || currentLayout == ViewLayout::DualWindow)
    {
        trackArranger->setVisible(true);
        splitterBar->setVisible(false);
        if (currentLayout == ViewLayout::ArrangerOnly)
            pianoRoll->setVisible(false);
        trackArranger->setBounds(area);
    }
    else if (currentLayout == ViewLayout::EditorOnly)
    {
        trackArranger->setVisible(false);
        splitterBar->setVisible(false);
        pianoRoll->setVisible(true);
        pianoRoll->setBounds(area);
    }
}

//==============================================================================
// AppStateManager Integration
//==============================================================================
void MainComponent::collectAndSaveUIState()
{
    auto& st = AppStateManager::getInstance();

    // Layout
    st.viewLayout       = (int)currentLayout;
    st.splitRatio       = splitRatio;
    st.leftZoneVisible  = isLeftZoneVisible;
    st.rightZoneVisible = isRightZoneVisible;

    // Inspector workspace
    if (leftInspector)
    {
        st.inspectorStateValid = true;
        for (int i = 0; i < kInspectorSectionCount; ++i)
        {
            st.sectionPositions[i][0] = (int)leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Arranger,  i);
            st.sectionPositions[i][1] = (int)leftInspector->getContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i);
            st.sectionPositions[i][2] = (int)leftInspector->getContextSectionPosition(InspectorComponent::DockContext::Default,   i);
        }
        const auto& order = leftInspector->getSectionOrder();
        for (int i = 0; i < std::min((int)order.size(), kInspectorSectionCount); ++i)
            st.sectionOrder[i] = order[i];
    }

    // TrackArranger view
    if (trackArranger)
    {
        st.arrangerPixelsPerBeat = trackArranger->getPixelsPerBeat();
        st.arrangerScrollBeats   = trackArranger->getScrollBeats();
        st.arrangerTrackHeight   = trackArranger->getTrackHeight();
    }

    // PianoRoll view
    if (pianoRoll)
    {
        st.pianoRollPixelsPerBeat = pianoRoll->getCanvas().getPixelsPerBeat();
        st.pianoRollNoteHeight    = pianoRoll->getCanvas().getNoteHeight();
        st.pianoRollScrollX       = pianoRoll->getViewportScrollX();
        st.pianoRollScrollY       = pianoRoll->getViewportScrollY();
    }

    // Floating window
    if (detachedKeyEditorWindow)
    {
        st.floatWinWasOpen     = true;
        if (detachedKeyEditorWindow->getWidth() >= 500 && detachedKeyEditorWindow->getHeight() >= 350)
        {
            st.floatWinX           = detachedKeyEditorWindow->getX();
            st.floatWinY           = detachedKeyEditorWindow->getY();
            st.floatWinW           = detachedKeyEditorWindow->getWidth();
            st.floatWinH           = detachedKeyEditorWindow->getHeight();
        }
        st.floatWinAlwaysOnTop = detachedKeyEditorWindow->isAlwaysOnTop();
    }
    else
    {
        st.floatWinWasOpen = false;
    }

    // Edit settings from MidiModel
    st.snapEnabled  = midiModel.isSnapEnabled();
    st.snapDivision = (int)midiModel.getSnapDivision();
    st.snapTriplet  = midiModel.isSnapTriplet();
    st.followMode   = (int)midiModel.getPlayheadFollowMode();
    st.lastEnabledFollowMode = (int)midiModel.getLastEnabledPlayheadFollowMode();

    // Loop
    st.loopEnabled = midiModel.isLoopEnabled();
    st.loopStart   = midiModel.getLoopStart();
    st.loopEnd     = midiModel.getLoopEnd();

    st.save();
}

void MainComponent::applyUIState()
{
    const auto& st = AppStateManager::getInstance();

    // --- Edit settings (applied first, before any UI rebuild) ---
    midiModel.setSnapEnabled(st.snapEnabled);
    midiModel.setSnapDivision((SnapDivision)st.snapDivision);
    midiModel.setSnapTriplet(st.snapTriplet);
    midiModel.setPlayheadFollowMode((PlayheadFollowMode)st.lastEnabledFollowMode);
    midiModel.setPlayheadFollowMode((PlayheadFollowMode)st.followMode);
    midiModel.setLoopEnabled(st.loopEnabled);
    midiModel.setLoopRange(st.loopStart, st.loopEnd);

    // --- Inspector workspace ---
    if (st.inspectorStateValid && leftInspector && rightInspector)
    {
        juce::ScopedValueSetter<bool> svs(isSyncingSections, true);
        for (int i = 0; i < kInspectorSectionCount; ++i)
        {
            auto arr = (SectionDockPosition)st.sectionPositions[i][0];
            auto key = (SectionDockPosition)st.sectionPositions[i][1];
            auto def = (SectionDockPosition)st.sectionPositions[i][2];
            leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger,  i, arr);
            leftInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i, key);
            leftInspector->setContextSectionPosition(InspectorComponent::DockContext::Default,   i, def);
            rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Arranger,  i, arr);
            rightInspector->setContextSectionPosition(InspectorComponent::DockContext::KeyEditor, i, key);
            rightInspector->setContextSectionPosition(InspectorComponent::DockContext::Default,   i, def);
        }
        std::vector<int> order(st.sectionOrder.begin(), st.sectionOrder.end());
        leftInspector->setSectionOrder(order);
        rightInspector->setSectionOrder(order);

        if (detachedKeyEditorWindow)
            detachedKeyEditorWindow->syncSectionsFromOwner();
    }

    if (leftInspector) leftInspector->applyCustomModuleSettings();
    if (rightInspector) rightInspector->applyCustomModuleSettings();

    // --- Layout (splitRatio, zone visibility) ---
    splitRatio       = st.splitRatio;
    isLeftZoneVisible  = st.leftZoneVisible;
    isRightZoneVisible = st.rightZoneVisible;

    // Restore view layout (Skip DualWindow — we won't re-open floating window automatically here)
    ViewLayout restoredLayout = (ViewLayout)st.viewLayout;
    if (restoredLayout != currentLayout)
        setViewLayout(restoredLayout);

    // --- TrackArranger view ---
    if (trackArranger)
        trackArranger->applyViewState(st.arrangerPixelsPerBeat,
                                      st.arrangerScrollBeats,
                                      st.arrangerTrackHeight);

    // --- PianoRoll view (deferred after layout is settled) ---
    if (pianoRoll)
    {
        pianoRoll->applyViewState(st.pianoRollPixelsPerBeat,
                                  st.pianoRollNoteHeight,
                                  st.pianoRollScrollX,
                                  st.pianoRollScrollY);
    }

    // Update toolbar to reflect restored state
    if (toolbar)
    {
        toolbar->setViewMode((int)currentLayout);
        toolbar->setZoneStates(isLeftZoneVisible, isRightZoneVisible);
    }

    // If state file didn't exist yet, save default state to disk immediately
    auto stateFile = AppStateManager::getAppDataDir().getChildFile("ui_state.xml");
    if (!stateFile.existsAsFile())
        collectAndSaveUIState();

    resized();
}

} // namespace Harmonizer
