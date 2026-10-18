#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include "../core/TrackModel.h"
#include "../core/MidiModel.h"

namespace Harmonizer
{

class AudioEngine;

class GlobalTrackOverviewLane : public juce::Component,
                                public TrackModel::Listener
{
public:
    GlobalTrackOverviewLane(TrackModel& trackModel, MidiModel& midiModel, juce::UndoManager& undoManager, AudioEngine* engine = nullptr);
    ~GlobalTrackOverviewLane() override;

    void setAudioEngine(AudioEngine* engine) { audioEngine = engine; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void modifierKeysChanged(const juce::ModifierKeys& mods) override;
    bool keyPressed(const juce::KeyPress& key) override;

    std::function<void(double beat)> onSeekPlayhead;

    void setPixelsPerBeat(float ppb);
    void setScrollOffsetBeats(double beats);

    bool isChordLaneVisible() const   { return showChordLane; }
    bool isModeLaneVisible() const    { return showModeLane; }
    bool isMarkerLaneVisible() const  { return showMarkerLane; }
    bool isTimeSigLaneVisible() const { return showTimeSigLane; }
    void setTimeSigLaneVisible(bool v){ showTimeSigLane = v; repaint(); }
    void setLaneVisibility(bool chord, bool mode, bool marker, bool timeSig)
    {
        showChordLane = chord;
        showModeLane = mode;
        showMarkerLane = marker;
        showTimeSigLane = timeSig;
        if (!showChordLane && !showModeLane && !showMarkerLane && !showTimeSigLane)
            showChordLane = true;
        repaint();
    }
    void setLaneVisibility(bool chord, bool mode, bool marker)
    {
        setLaneVisibility(chord, mode, marker, showTimeSigLane);
    }

    void tracksChanged() override         { repaint(); }
    void markersChanged() override        { repaint(); }
    void chordsChanged() override         { repaint(); }
    void modesChanged() override          { repaint(); }
    void timeSignaturesChanged() override { repaint(); }

    enum class SelectedEventType { None, Chord, Mode, Marker, TimeSignature };

    bool hasSelection() const { return selectedType != SelectedEventType::None; }
    void clearSelection();
    void deleteSelectedEvent();

    std::function<void()> onFocusGained;

private:
    TrackModel& trackModel;
    MidiModel& midiModel;
    juce::UndoManager& undoManager;
    AudioEngine* audioEngine = nullptr;

    float pixelsPerBeat = 60.0f;
    double scrollOffsetBeats = 0.0;
    static constexpr int headerWidth = 64;

    bool showChordLane = true;
    bool showModeLane = true;
    bool showMarkerLane = false;
    bool showTimeSigLane = true;

    // Interaction & Selection state
    SelectedEventType selectedType = SelectedEventType::None;
    int64_t selectedId = -1;
    juce::String selectedMarkerStrId;

    bool isDragging = false;
    bool isAltDuplicating = false;
    double dragClickBeat = 0.0;
    double initialStartBeat = 0.0;
    double initialLengthBeats = 4.0;
    double currentPreviewBeat = 0.0;

    bool isCreatingItemWithAlt = false;
    TrackType creatingTrackType = TrackType::Chord;
    int64_t creatingItemId = -1;
    juce::String creatingItemStringId;
    double creatingStartBeat = 0.0;

    float beatToX(double beat) const;
    double xToBeat(float x) const;

    struct HitTestResult
    {
        SelectedEventType type = SelectedEventType::None;
        int64_t intId = -1;
        juce::String strId;
        double startBeat = 0.0;
        double lengthBeats = 0.0;
        juce::Rectangle<float> bounds;
    };

    HitTestResult hitTestEvent(juce::Point<int> pos) const;
    void showContextMenu(const HitTestResult& hit, juce::Point<int> screenPos);
    void openEditorForEvent(const HitTestResult& hit);

    void copySelectedEvent();
    void cutSelectedEvent();
    void pasteEvent();
    void duplicateSelectedEvent();

    struct OverviewClipboardItem
    {
        SelectedEventType type = SelectedEventType::None;
        ChordEvent chord;
        ModeEvent mode;
        MarkerEvent marker;
        TimeSignatureEvent timeSig;
        double lengthBeats = 4.0;
    };
    static inline OverviewClipboardItem overviewClipboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlobalTrackOverviewLane)
};

} // namespace Harmonizer
