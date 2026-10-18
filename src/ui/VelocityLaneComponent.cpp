#include "VelocityLaneComponent.h"
#include "CubaseTheme.h"
#include "../core/UndoActions.h"

namespace Harmonizer
{

VelocityLaneComponent::VelocityLaneComponent(MidiModel& model, juce::UndoManager& um, TrackModel* tm)
    : midiModel(model), undoManager(um), trackModel(tm)
{
    midiModel.addListener(this);
}

VelocityLaneComponent::~VelocityLaneComponent()
{
    midiModel.removeListener(this);
}

void VelocityLaneComponent::setZoomAndScroll(double ppb, double sob)
{
    pixelsPerBeat = std::max(2.0, ppb);
    scrollOffsetBeats = std::max(0.0, sob);
    repaint();
}

float VelocityLaneComponent::beatToX(double beat) const
{
    return (float)((beat - scrollOffsetBeats) * pixelsPerBeat);
}

double VelocityLaneComponent::xToBeat(float x) const
{
    double safePpb = std::max(2.0, pixelsPerBeat);
    return scrollOffsetBeats + (x / safePpb);
}

void VelocityLaneComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour(CubaseTheme::bgPanel().darker(0.1f));
    g.fillRect(bounds);

    // Top border
    g.setColour(CubaseTheme::borderSubtle());
    g.drawHorizontalLine(0, 0.0f, bounds.getWidth());

    // Grid guide lines at 25%, 50%, 75%, 100%
    g.setColour(CubaseTheme::gridSubBeat().withAlpha(0.6f));
    for (int pct : { 32, 64, 96, 127 })
    {
        float y = bounds.getBottom() - (pct / 127.0f) * bounds.getHeight();
        g.drawHorizontalLine((int)y, 0.0f, bounds.getWidth());
    }

    // Label: "Velocity"
    g.setFont(juce::FontOptions(11.0f));
    g.setColour(CubaseTheme::textSecondary().withAlpha(0.6f));
    g.drawText("Velocity", 8, 4, 80, 16, juce::Justification::left);

    // Render stalks
    const auto& notes = midiModel.getNotes();
    for (const auto& n : notes)
    {
        float x = beatToX(n.startBeat);
        if (x < -10.0f || x > bounds.getWidth() + 10.0f)
            continue;

        float stalkHeight = n.velocity * bounds.getHeight();
        float y = bounds.getBottom() - stalkHeight;

        bool sel = n.isSelected;

        // Stalk line
        g.setColour(sel ? CubaseTheme::accentBlue() : CubaseTheme::accentOrange().withAlpha(0.6f));
        g.drawVerticalLine((int)x, y, bounds.getBottom());

        // Stalk head (circle)
        float radius = sel ? 3.5f : 2.5f;
        g.setColour(sel ? juce::Colours::white : CubaseTheme::accentOrange());
        g.fillEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f);
    }
}

void VelocityLaneComponent::mouseDown(const juce::MouseEvent& e)
{
    isDraggingVelocity = true;
    dragInitialNotes = midiModel.getNotes();

    mouseDrag(e);
}

void VelocityLaneComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (!isDraggingVelocity)
        return;

    float normVel = std::clamp(1.0f - ((float)e.y / (float)getHeight()), 0.01f, 1.0f);
    double mouseBeat = xToBeat((float)e.x);

    // Check if there are selected notes
    auto& notes = midiModel.getNotes();
    const bool hasSelection = std::any_of(notes.begin(), notes.end(),
        [](const NoteEvent& note) { return note.isSelected; });
    if (hasSelection)
    {
        bool changed = false;
        for (auto& note : notes)
        {
            if (note.isSelected && note.velocity != normVel)
            {
                note.velocity = normVel;
                changed = true;
            }
        }
        if (changed) midiModel.triggerNotePreviewChanged(trackModel != nullptr);
    }
    else
    {
        // Adjust nearest note within threshold
        NoteEvent* nearestNote = nullptr;
        double minDistance = 1.0; // 1 beat threshold

        for (auto& n : notes)
        {
            double dist = std::abs(n.startBeat - mouseBeat);
            if (dist < minDistance)
            {
                minDistance = dist;
                nearestNote = &n;
            }
        }

        if (nearestNote != nullptr && nearestNote->velocity != normVel)
        {
            nearestNote->velocity = normVel;
            midiModel.triggerNotePreviewChanged(trackModel != nullptr);
        }
    }
}

void VelocityLaneComponent::mouseUp(const juce::MouseEvent&)
{
    if (isDraggingVelocity)
    {
        isDraggingVelocity = false;
        UndoActionHelper::performAtomic(&undoManager,
            new ModifyNotesUndoAction(midiModel, dragInitialNotes, midiModel.getNotes(), "Change Velocity", trackModel),
            "Change Velocity");
    }
}

} // namespace Harmonizer
