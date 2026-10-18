#pragma once

#include "../core/MidiModel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>

namespace Harmonizer
{

class TrackModel;

class VelocityLaneComponent : public juce::Component,
                             public MidiModel::Listener
{
public:
    VelocityLaneComponent(MidiModel& model, juce::UndoManager& undoManager, TrackModel* trackModel = nullptr);
    ~VelocityLaneComponent() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    void setZoomAndScroll(double pixelsPerBeat, double scrollOffsetBeats);

    // Listener overrides
    void noteListChanged() override { repaint(); }
    void selectionChanged() override { repaint(); }

private:
    MidiModel& midiModel;
    juce::UndoManager& undoManager;
    TrackModel* trackModel = nullptr;

    double pixelsPerBeat = 40.0;
    double scrollOffsetBeats = 0.0;

    std::vector<NoteEvent> dragInitialNotes;
    bool isDraggingVelocity = false;

    float beatToX(double beat) const;
    double xToBeat(float x) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VelocityLaneComponent)
};

} // namespace Harmonizer
