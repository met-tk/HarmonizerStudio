#pragma once

#include "../core/MidiModel.h"
#include "../core/TrackModel.h"
#include "../audio/AudioEngine.h"
#include "GlobalTrackOverviewLane.h"
#include "CubaseTheme.h"
#include "ChordPlacementGroup.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <utility>

namespace Harmonizer
{

class PianoRollCanvas : public juce::Component,
                        public MidiModel::Listener,
                        public AudioEngine::PlayheadListener,
                        public TrackModel::Listener,
                        private juce::Timer
{
public:
    PianoRollCanvas(MidiModel& model, TrackModel& trackModel, AudioEngine& engine, juce::UndoManager& undoManager);
    ~PianoRollCanvas() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;
    bool keyPressed(const juce::KeyPress& key) override;

    void beginSecondaryDominantPlacement(std::vector<ChordPlacementGroup> groups);
    void beginChordPlacement(std::vector<NoteEvent> generated, juce::String undoName,
                             juce::String description = {});
    void cancelSecondaryDominantPlacement();
    bool isSecondaryDominantPlacementActive() const { return !placementNotes.empty(); }

    void setZoomAndScroll(double pixelsPerBeat, double scrollOffsetBeats, float noteHeight, int minVisiblePitch);

    // Zooming helpers (support pivoting around mouse position)
    void zoomHorizontal(float factor, int mouseCanvasX = -1);
    void zoomVertical(float factor, int mouseCanvasY = -1);

    double getPixelsPerBeat() const    { return pixelsPerBeat; }
    float getNoteHeight() const        { return noteHeight; }
    double getTimelineExtentBeats() const { return timelineExtentBeats; }
    void setTimelineExtentBeats(double endBeat);

    // Coordinate conversions
    float beatToX(double beat) const;
    double xToBeat(float x) const;
    float pitchToY(int pitch) const;
    int yToPitch(float y) const;

    // Listener overrides
    void noteListChanged() override;
    void notePreviewChanged() override;
    void selectionChanged() override   { repaint(); }
    void projectSettingsChanged() override { repaint(); }
    void toolChanged() override        { updateMouseCursor(); repaint(); }
    void playheadMoved(double) override { repaint(); }
    void playbackStateChanged(bool) override { repaint(); }

    // TrackModel Listener
    void tracksChanged() override;
    void clipNotesChanged() override { repaint(); }
    void markersChanged() override     { repaint(); }
    void modesChanged() override       { repaint(); }
    void chordsChanged() override      { repaint(); }
    void timeSignaturesChanged() override { repaint(); }
    void tempoChanged() override       { repaint(); }
    void activeClipChanged() override;

    std::function<void()> onZoomChanged;
    std::function<void(int deltaX)> onScrollRequested;
    std::function<void(int deltaX, int deltaY)> onPanRequested;
    std::function<void()> onFocusRequested;
    std::function<void()> onClearOverviewSelection;

private:
    void timerCallback() override;
    void flushPendingNotePreview();

    MidiModel& midiModel;
    TrackModel& trackModel;
    AudioEngine& audioEngine;
    juce::UndoManager& undoManager;

    double pixelsPerBeat = 40.0;
    double timelineExtentBeats = 1024.0;
    double scrollOffsetBeats = 0.0;
    float noteHeight = 16.0f;
    int minPitch = 0;

    enum class DragMode
    {
        None,
        MoveNotes,
        AdjustVelocity,
        ResizeLeft,
        ResizeRight,
        MarqueeSelect,
        DrawNewNote,
        AltDuplicate,
        MiddlePan,
        ResizeClipStart,
        ResizeClipEnd
    };

    DragMode currentDrag = DragMode::None;
    juce::Point<int> dragStartPos;
    double dragStartBeat = 0.0;
    int dragStartPitch = 60;
    juce::Rectangle<int> marqueeRect;

    std::vector<NoteEvent> initialNotesState;
    std::vector<size_t> selectedDragNoteIndices;
    size_t primaryDragNoteIndex = 0;
    double selectedDragMinBeat = 0.0;
    double selectedDragMaxEndBeat = 0.0;
    double lastDragBeatDelta = 0.0;
    int lastDragPitchDelta = 0;
    bool dragDeltaInitialized = false;
    bool ctrlDragDirectionChosen = false;
    bool ctrlVerticalLocked = false;
    int64_t pendingCtrlToggleNoteId = -1;
    int maxDragDistance = 0;
    double auditionStopTimeMs = 0.0;
    bool notePreviewPending = false;
    int previewTrackId = -1;
    int64_t previewClipId = -1;
    double lastPreviewSyncMs = 0.0;
    std::vector<NoteEvent> duplicatePreviewNotes;
    int64_t activeNoteId = -1;
    int currentPreviewPitch = -1;

    std::vector<NoteEvent> placementNotes;
    std::vector<ChordPlacementGroup> placementGroups;
    juce::String placementUndoName { "Place Secondary Dominant" };
    juce::String placementDescription;
    double placementAnchorBeat = 0.0;
    bool placementHasPointer = false;
    int placementTrackId = -1;
    int64_t placementClipId = -1;
    int64_t lastPlacementCommitTimeMs = 0;

    bool justCreatedNoteOnDoubleClick = false;
    int64_t lastCreatedNoteIdOnDoubleClick = -1;
    int64_t lastNoteClickTimeMs = 0;
    int64_t lastClickedNoteId = -1;

    int64_t dragClipId = -1;
    double clipInitialStartBeat = 0.0;
    double clipInitialLengthBeats = 0.0;

    void updateMouseCursor();
    void updateHoverCursor(juce::Point<int> position, const juce::ModifierKeys& modifiers);
    void captureInitialNotesForDrag();
    int64_t findNoteAt(juce::Point<int> pos, bool& outNearLeftEdge, bool& outNearRightEdge) const;
    int64_t findClipBoundaryAt(juce::Point<int> pos, bool& outNearLeftEdge, bool& outNearRightEdge) const;
    const MidiClip* findInactiveClipAt(juce::Point<int> pos) const;
    struct GhostNoteHit
    {
        int trackId = -1;
        int64_t clipId = -1;
        NoteEvent note;
        bool nearLeft = false;
        bool nearRight = false;
    };
    bool findGhostNoteAt(juce::Point<int> pos, GhostNoteHit& outHit) const;
    void showContextMenuForSelectedNotes(juce::Point<int> screenPos);
    void updateSecondaryDominantPlacement(float mouseX);
    bool canPlaceSecondaryDominantAt(double beat) const;
    void commitSecondaryDominantPlacement();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollCanvas)
};

class PianoRollComponent : public juce::Component,
                           public AudioEngine::PlayheadListener,
                           public TrackModel::Listener,
                           public MidiModel::Listener
{
public:
    PianoRollComponent(MidiModel& model, TrackModel& trackModel, AudioEngine& engine, juce::UndoManager& undoManager);
    ~PianoRollComponent() override;

    void resized() override;

    PianoRollCanvas& getCanvas() { return canvas; }
    void beginSecondaryDominantPlacement(std::vector<ChordPlacementGroup> groups)
    {
        canvas.beginSecondaryDominantPlacement(std::move(groups));
    }
    void beginChordPlacement(std::vector<NoteEvent> generated, juce::String undoName,
                             juce::String description = {})
    {
        canvas.beginChordPlacement(std::move(generated), std::move(undoName),
                                   std::move(description));
    }
    void cancelSecondaryDominantPlacement() { canvas.cancelSecondaryDominantPlacement(); }
    bool isSecondaryDominantPlacementActive() const { return canvas.isSecondaryDominantPlacementActive(); }

    void zoomInHorizontal()  { canvas.zoomHorizontal(1.25f); syncViews(); }
    void zoomOutHorizontal() { canvas.zoomHorizontal(0.80f); syncViews(); }
    void zoomInVertical()    { canvas.zoomVertical(1.20f);   syncViews(); }
    void zoomOutVertical()   { canvas.zoomVertical(0.83f);   syncViews(); }

    int getViewportScrollX() const { return viewport.getViewPositionX(); }
    int getViewportScrollY() const { return viewport.getViewPositionY(); }

    /// Restore view state from AppStateManager (called after resized() on startup)
    void applyViewState(double ppb, float noteH, int scrollX, int scrollY)
    {
        canvas.zoomHorizontal(ppb / canvas.getPixelsPerBeat());
        canvas.zoomVertical(noteH / canvas.getNoteHeight());
        updateHorizontalExtent((double)std::max(0, scrollX) / canvas.getPixelsPerBeat());
        syncViews();
        // Defer scroll position until after the component has been laid out
        juce::Component::SafePointer<PianoRollComponent> safeThis(this);
        juce::MessageManager::callAsync([safeThis, scrollX, scrollY]() {
            if (auto* self = safeThis.getComponent())
            {
                self->updateHorizontalExtent(
                    ((double)std::max(0, scrollX) + self->viewport.getViewWidth())
                        / self->canvas.getPixelsPerBeat());
                self->viewport.setViewPosition(scrollX, scrollY);
                self->keyboardViewport.setViewPosition(0, scrollY);
                self->syncViews();
            }
        });
    }

    void scrollToBeat(double beat);
    void scrollToPitch(int pitch);

    void playheadMoved(double beatPosition) override;
    void playbackStateChanged(bool) override {}

    void noteListChanged() override { updateHorizontalExtent(); }
    void notePreviewChanged() override {}
    void projectSettingsChanged() override { updateHorizontalExtent(); }
    void tracksChanged() override { updateHorizontalExtent(); }
    void clipNotesCommitted() override { updateHorizontalExtent(); }
    void chordsChanged() override { updateHorizontalExtent(); }
    void modesChanged() override { updateHorizontalExtent(); }
    void markersChanged() override { updateHorizontalExtent(); }
    void timeSignaturesChanged() override { updateHorizontalExtent(); }
    void tempoChanged() override { updateHorizontalExtent(); }
    void activeClipChanged() override { updateHorizontalExtent(); }
    void projectWillReplace() override;

    void setFocusedArea(bool focused) { isFocusedArea = focused; repaint(); }
    bool getFocusedArea() const       { return isFocusedArea; }

    void paintOverChildren(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    GlobalTrackOverviewLane* getOverviewLane() const { return overviewLane.get(); }

    std::function<void()> onFocusGained;

private:
    bool isFocusedArea = false;
    MidiModel& midiModel;
    TrackModel& trackModel;
    AudioEngine& audioEngine;
    juce::UndoManager& undoManager;

    class TimelineRuler* ruler = nullptr;
    class PianoKeyboardComponent* keyboard = nullptr;
    class VelocityLaneComponent* velocityLane = nullptr;
    std::unique_ptr<GlobalTrackOverviewLane> overviewLane;

    class PianoRollViewport : public juce::Viewport
    {
    public:
        std::function<void(int vx, int vy)> onScrollPositionChanged;
        void visibleAreaChanged(const juce::Rectangle<int>& r) override
        {
            if (onScrollPositionChanged)
                onScrollPositionChanged(r.getX(), r.getY());
        }
    };

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

            g.setColour(CubaseTheme::borderBright());
            int cx = getWidth() / 2;
            int cy = getHeight() / 2;
            g.fillRoundedRectangle((float)(cx - 16), (float)(cy - 1), 32.0f, 2.0f, 1.0f);
        }
        void mouseDown(const juce::MouseEvent& e) override { initialY = e.getScreenY(); }
        void mouseDrag(const juce::MouseEvent& e) override
        {
            int curY = e.getScreenY();
            int delta = curY - initialY;
            if (delta != 0 && dragCallback)
            {
                dragCallback(delta);
                initialY = curY;
            }
        }
    private:
        std::function<void(int)> dragCallback;
        int initialY = 0;
    };

    std::unique_ptr<SplitterBar> overviewSplitter;
    std::unique_ptr<SplitterBar> velocitySplitter;
    juce::TextButton btnTrackSelect;

    int overviewHeight = 36;
    int velocityHeight = 65;

    PianoRollCanvas canvas;
    PianoRollViewport viewport;
    juce::Viewport keyboardViewport;

    void syncViews();
    void updateHorizontalExtent(double requiredBeat = 0.0);
    bool updatingHorizontalExtent = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};

} // namespace Harmonizer
