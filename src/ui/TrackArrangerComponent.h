#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../core/TrackModel.h"
#include "../core/MidiModel.h"
#include "../audio/AudioEngine.h"

namespace Harmonizer
{

class TrackArrangerComponent : public juce::Component,
                              public TrackModel::Listener,
                              public MidiModel::Listener,
                              public AudioEngine::PlayheadListener,
                              public juce::ScrollBar::Listener,
                              public juce::ChangeListener,
                              private juce::Timer
{
public:
    TrackArrangerComponent(TrackModel& trackModel, MidiModel& midiModel, AudioEngine& audioEngine,
                           juce::UndoManager& undoManager,
                           std::function<void(int trackId, int64_t clipId)> onClipDoubleClicked);
    ~TrackArrangerComponent() override;

    void scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart) override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void modifierKeysChanged(const juce::ModifierKeys& mods) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // TrackModel Listener
    void tracksChanged() override;
    void clipNotesChanged() override { repaint(); }
    void markersChanged() override  { repaint(); }
    void modesChanged() override    { repaint(); }
    void chordsChanged() override   { repaint(); }
    void timeSignaturesChanged() override { repaint(); }
    void tempoChanged() override    { updateScrollBar(); repaint(); }
    void activeClipChanged() override { repaint(); }

    // MidiModel Listener
    void noteListChanged() override { repaint(); }

    // Batch operations on selected arranger events
    void copySelectedEvents();
    void cutSelectedEvents();
    void pasteEvents(double targetBeat = -1.0);
    void deleteSelectedEvents();
    void duplicateSelectedEvents();
    void mergeSelectedClips();

    // AudioEngine PlayheadListener
    void playheadMoved(double beatPosition) override;
    void playbackStateChanged(bool) override { repaint(); }

    void setPixelsPerBeat(float ppb);
    float  getPixelsPerBeat()  const { return pixelsPerBeat; }
    double getScrollBeats()    const { return scrollBeats; }
    int    getTrackHeight()    const { return trackHeight; }
    bool getFileDropTarget(juce::Point<int> localPoint, int& trackId, double& beat) const;

    /// Restore view state (called on startup from AppStateManager)
    void applyViewState(float ppb, double scroll, int trkHeight)
    {
        pixelsPerBeat = std::clamp(ppb, 2.0f, 500.0f);
        scrollBeats   = std::max(0.0, scroll);
        trackHeight   = std::clamp(trkHeight, 26, 120);
        updateScrollBar();
        repaint();
    }

    void showAddTrackMenu();
    void importAudioForTrack(int trackId, double startBeat = -1.0);
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }
    void showTrackContextMenu(int trackId, juce::Point<int> screenPos);
    void showLaneContextMenu(int trackId, double beatPos, juce::Point<int> screenPos);
    void showColorPickerForCurrentSelection();
    void setLocatorsToSelectedEvents();

    void paintOverChildren(juce::Graphics& g) override;
    void setFocusedArea(bool focused) { isFocusedArea = focused; repaint(); }
    bool getFocusedArea() const       { return isFocusedArea; }

    std::function<void()> onFocusGained;

private:
    bool isFocusedArea = false;
    TrackModel& trackModel;
    MidiModel& midiModel;
    AudioEngine& audioEngine;
    juce::UndoManager& undoManager;
    std::function<void(int, int64_t)> clipDoubleClickedCallback;

    int headerWidth = 190;
    int trackHeight = 44;
    int rulerHeight = 24;

    float pixelsPerBeat = 25.0f; // Track overview scale
    double scrollBeats = 0.0;
    int scrollTrackY = 0;
    double currentPlayhead = 0.0;

    // Dragging clips / items
    enum class ArrangerDragMode
    {
        None,
        MoveClip,
        MoveAudioClip,
        ResizeClipLeft,
        ResizeClipRight,
        MoveChord,
        ResizeChordLeft,
        ResizeChordRight,
        MoveMode,
        ResizeModeLeft,
        ResizeModeRight,
        MoveMarker,
        ResizeMarkerLeft,
        ResizeMarkerRight,
        MoveTimeSig,
        MoveTempoPoint,
        ReorderTrack,
        MarqueeSelect,
        AltDuplicateItem,
        ScrubPlayhead,
        MiddlePan,
        RulerLeftLocator,
        RulerRightLocator
    };

    ArrangerDragMode dragMode = ArrangerDragMode::None;
    int draggedTrackId = -1;
    int dragTargetTrackId = -1;
    int64_t draggedItemId = -1;
    juce::String draggedItemStringId; // for MarkerEvent which uses juce::String id
    double dragStartBeat = 0.0;
    double itemInitialStartBeat = 0.0;
    double itemInitialLengthBeats = 0.0;
    double itemInitialBpm = 120.0;
    double stationaryLocatorBeat = 0.0;
    bool wasPlayingBeforeDrag = false;
    juce::Point<int> lastRulerMousePos;
    void timerCallback() override;

    int dragTrackSourceIndex = -1;
    int dragTrackTargetIndex = -1;
    int dragTrackMouseY = 0;

    // Arranger Clipboard for Ctrl+C / Ctrl+X / Ctrl+V
    struct ArrangerClipboard
    {
        std::vector<MidiClip> clips;
        std::vector<AudioClip> audioClips;
        std::vector<ChordEvent> chords;
        std::vector<ModeEvent> modes;
        std::vector<MarkerEvent> markers;
        std::vector<TimeSignatureEvent> timeSignatures;
        std::vector<TempoEvent> tempoEvents;
        double minBeat = 0.0;
        int sourceTrackId = -1;
        TrackType sourceTrackType = TrackType::Instrument;
    };
    static inline ArrangerClipboard arrangerClipboard;

    // Marquee Box in Arranger Lane
    juce::Point<int> marqueeStartPos;
    juce::Rectangle<int> marqueeRect;

    // Alt + Drag duplicate or create
    bool isDuplicatingWithAlt = false;
    bool isCreatingItemWithAlt = false;
    int creatingTrackId = -1;
    int64_t creatingItemId = -1;
    double creatingStartBeat = 0.0;
    juce::Point<int> dragMouseDownPos;
    double altGhostStartBeat = 0.0;
    double altGhostLengthBeats = 4.0;
    int altGhostTrackId = -1;

    float beatToX(double beat) const;
    double xToBeat(float x) const;
    bool splitEventAt(Track& track, double clickBeat, int64_t itemId = -1,
                      const juce::String& stringId = {});

    // Inline track rename editor
    std::unique_ptr<juce::TextEditor> inlineRenameEditor;
    int renamingTrackId = -1;
    void startTrackRename(int trackId, juce::Rectangle<int> bounds);
    void finishTrackRename(bool apply);

    // Edit item popup helpers
    void showEditChordDialog(int trackId, int64_t chordId);
    void showEditModeDialog(int trackId, int64_t modeId);
    void showEditMarkerDialog(int trackId, const juce::String& markerId);
    void showEditTimeSigDialog(int trackId, int64_t sigId);
    void showEditTempoDialog(int trackId, int64_t tempoId);
    struct DraggedEventSnapshot
    {
        TrackType trackType = TrackType::Instrument;
        int trackId = -1;
        int64_t intId = -1;
        juce::String stringId;
        double initialStartBeat = 0.0;
        double initialLengthBeats = 0.0;
    };
    std::vector<DraggedEventSnapshot> selectedDragSnapshots;

    juce::ScrollBar horizontalScrollBar { false };
    void updateScrollBar();

    juce::TextButton btnAddTrack { "+ Track" };
    juce::AudioFormatManager waveformFormats;
    juce::AudioThumbnailCache waveformCache { 32 };
    std::map<std::string, std::unique_ptr<juce::AudioThumbnail>> waveforms;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackArrangerComponent)
};

} // namespace Harmonizer
