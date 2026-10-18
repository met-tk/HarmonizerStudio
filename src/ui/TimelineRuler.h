#pragma once

#include "../core/MidiModel.h"
#include "../core/TrackModel.h"
#include "../audio/AudioEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace Harmonizer
{

class TimelineRuler : public juce::Component,
                      public MidiModel::Listener,
                      public AudioEngine::PlayheadListener,
                      public TrackModel::Listener,
                      private juce::Timer
{
public:
    TimelineRuler(MidiModel& model, AudioEngine& engine, TrackModel* tm = nullptr);
    ~TimelineRuler() override;

    std::function<void(int deltaPixels)> onEdgeScroll;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

    void setZoomAndScroll(double pixelsPerBeat, double scrollOffsetBeats);

    // Listener overrides
    void projectSettingsChanged() override { repaint(); }
    void playheadMoved(double beatPosition) override;
    void playbackStateChanged(bool) override { repaint(); }

    // TrackModel Listener
    void timeSignaturesChanged() override { repaint(); }
    void tracksChanged() override         { repaint(); }

private:
    enum class DragTarget { None, Playhead, LeftLocator, RightLocator, NewRange };
    DragTarget currentDrag = DragTarget::None;
    double dragAnchorBeat = 0.0;
    double stationaryLocatorBeat = 0.0;

private:
    MidiModel& midiModel;
    AudioEngine& audioEngine;
    TrackModel* trackModel = nullptr;

    double pixelsPerBeat = 40.0;
    double scrollOffsetBeats = 0.0;

    double xToBeat(float x) const;
    float beatToX(double beat) const;

    void timerCallback() override;

    bool wasPlayingBeforeDrag = false;
    juce::Point<int> lastDragMousePos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineRuler)
};

} // namespace Harmonizer
