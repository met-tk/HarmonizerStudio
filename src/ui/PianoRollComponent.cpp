#include "PianoRollComponent.h"
#include "TimelineRuler.h"
#include "PianoKeyboardComponent.h"
#include "VelocityLaneComponent.h"
#include "FloatingToolbox.h"
#include "CubaseTheme.h"
#include "../core/UndoActions.h"
#include "../core/Localization.h"
#include "../core/ChordClassifier.h"
#include "SecondaryDominantAction.h"
#include "../core/NoteEditOperations.h"
#include "../core/SecondaryDominant.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Harmonizer
{

namespace
{
const juce::MouseCursor& velocityAdjustmentCursor()
{
    static const juce::MouseCursor cursor = [] {
        juce::Image image(juce::Image::ARGB, 36, 36, true);
        juce::Graphics g(image);

        // Keep an ordinary pointer tip so the note hit position stays clear.
        juce::Path pointer;
        pointer.startNewSubPath(2.0f, 2.0f);
        pointer.lineTo(2.0f, 24.0f);
        pointer.lineTo(7.3f, 18.5f);
        pointer.lineTo(11.0f, 27.0f);
        pointer.lineTo(14.8f, 25.3f);
        pointer.lineTo(11.0f, 17.0f);
        pointer.lineTo(19.0f, 17.0f);
        pointer.closeSubPath();
        g.setColour(juce::Colours::white);
        g.fillPath(pointer);
        g.setColour(juce::Colour::fromRGB(20, 27, 36));
        g.strokePath(pointer, juce::PathStrokeType(1.5f));

        // A compact speaker badge denotes velocity rather than note movement.
        const juce::Rectangle<float> badge(13.0f, 12.0f, 21.0f, 21.0f);
        g.setColour(juce::Colour::fromRGB(25, 43, 52));
        g.fillRoundedRectangle(badge, 5.0f);
        g.setColour(juce::Colour::fromRGB(91, 212, 229));
        g.drawRoundedRectangle(badge.reduced(0.6f), 4.4f, 1.3f);

        juce::Path speaker;
        speaker.startNewSubPath(16.0f, 20.0f);
        speaker.lineTo(19.0f, 20.0f);
        speaker.lineTo(23.0f, 16.7f);
        speaker.lineTo(23.0f, 28.3f);
        speaker.lineTo(19.0f, 25.0f);
        speaker.lineTo(16.0f, 25.0f);
        speaker.closeSubPath();
        g.setColour(juce::Colours::white);
        g.fillPath(speaker);

        juce::Path soundWaves;
        soundWaves.startNewSubPath(25.0f, 19.2f);
        soundWaves.quadraticTo(28.5f, 22.5f, 25.0f, 25.8f);
        soundWaves.startNewSubPath(27.0f, 17.1f);
        soundWaves.quadraticTo(32.0f, 22.5f, 27.0f, 27.9f);
        g.setColour(juce::Colour::fromRGB(112, 226, 238));
        g.strokePath(soundWaves, juce::PathStrokeType(1.5f));
        return juce::MouseCursor(image, 2, 2);
    }();
    return cursor;
}

juce::StringArray describePlacedChordGroups(
    const std::vector<ChordPlacementGroup>& groups,
    double beatOffset,
    const MidiModel& model,
    const TrackModel& tracks)
{
    juce::StringArray descriptions;
    for (size_t i = 0; i < groups.size(); ++i)
    {
        if (groups[i].notes.empty()) continue;
        auto positioned = groups[i].notes;
        for (auto& note : positioned) note.startBeat += beatOffset;
        const double groupBeat = std::min_element(positioned.begin(), positioned.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
                return a.startBeat < b.startBeat;
            })->startBeat;
        const auto layout = model.getEffectiveLayoutAtBeat(groupBeat, &tracks);
        const auto recognized = ChordClassifier::identifyChord(positioned,
            layout.tonicPc, layout.scaleName, layout.tonicName);
        juce::String description = recognized.status == ChordRecognitionStatus::None
            ? juce::String::fromUTF8(u8"未识别音组") : recognized.fullName;
        if (groups[i].originalTargetChord.isNotEmpty())
        {
            description += groups[i].originalTargetIsRootOnly
                ? juce::String::fromUTF8(u8" → 原目标根音 ")
                : juce::String::fromUTF8(u8" → 原目标和弦 ");
            description += groups[i].originalTargetChord;
            if (groups[i].originalTargetFunction.isNotEmpty())
                description += juce::String::fromUTF8(u8"；原调功能 ")
                    + groups[i].originalTargetFunction;
        }
        if (groups.size() > 1)
            description = juce::String((int)i + 1) + ". " + description;
        descriptions.add(description);
    }
    return descriptions;
}
}

//==============================================================================
// Canvas Implementation
//==============================================================================
PianoRollCanvas::PianoRollCanvas(MidiModel& model, TrackModel& tm, AudioEngine& engine, juce::UndoManager& um)
    : midiModel(model), trackModel(tm), audioEngine(engine), undoManager(um)
{
    midiModel.addListener(this);
    audioEngine.addPlayheadListener(this);
    trackModel.addListener(this);

    setWantsKeyboardFocus(true);

    setTimelineExtentBeats(timelineExtentBeats);
}

PianoRollCanvas::~PianoRollCanvas()
{
    stopTimer();
    trackModel.removeListener(this);
    audioEngine.removePlayheadListener(this);
    midiModel.removeListener(this);
}

void PianoRollCanvas::captureInitialNotesForDrag()
{
    initialNotesState = midiModel.getNotes();
    selectedDragNoteIndices.clear();
    primaryDragNoteIndex = initialNotesState.size();
    selectedDragMinBeat = 1.0e12;
    selectedDragMaxEndBeat = -1.0e12;
    dragDeltaInitialized = false;
    for (size_t i = 0; i < initialNotesState.size(); ++i)
    {
        const auto& note = initialNotesState[i];
        if (!note.isSelected) continue;
        selectedDragNoteIndices.push_back(i);
        selectedDragMinBeat = std::min(selectedDragMinBeat, note.startBeat);
        selectedDragMaxEndBeat = std::max(selectedDragMaxEndBeat,
                                          note.startBeat + note.lengthBeats);
        if (note.id == activeNoteId)
            primaryDragNoteIndex = i;
    }
    if (primaryDragNoteIndex == initialNotesState.size() && !selectedDragNoteIndices.empty())
        primaryDragNoteIndex = selectedDragNoteIndices.front();
}

void PianoRollCanvas::noteListChanged()
{
    stopTimer();
    if (auditionStopTimeMs > 0.0)
    {
        audioEngine.allPreviewNotesOff();
        auditionStopTimeMs = 0.0;
    }
    notePreviewPending = false;
    const auto& notes = midiModel.getNotes();
    if (const auto* clip = trackModel.getActiveClip())
    {
        const bool alreadySynced = clip->notes.size() == notes.size()
            && std::equal(clip->notes.begin(), clip->notes.end(), notes.begin(),
                [](const NoteEvent& a, const NoteEvent& b) {
                    return a.id == b.id && a.pitch == b.pitch
                        && a.startBeat == b.startBeat && a.lengthBeats == b.lengthBeats
                        && a.velocity == b.velocity && a.isSelected == b.isSelected
                        && a.isMuted == b.isMuted;
                });
        if (!alreadySynced)
            trackModel.syncActiveClipNotes(notes);
    }
    repaint();
    updateMouseCursor();
}

void PianoRollCanvas::notePreviewChanged()
{
    notePreviewPending = true;
    previewTrackId = trackModel.getActiveTrackId();
    previewClipId = trackModel.getActiveClipId();
    // Keep drawing responsive while coalescing audio and clip copies from
    // high-frequency pointer events into at most one update per interval.
    constexpr double syncIntervalMs = 24.0;
    const double elapsed = juce::Time::getMillisecondCounterHiRes() - lastPreviewSyncMs;
    if (elapsed >= syncIntervalMs)
        flushPendingNotePreview();
    else if (!isTimerRunning())
        startTimer(std::max(1, (int)std::ceil(syncIntervalMs - elapsed)));
    repaint();
}

void PianoRollCanvas::timerCallback()
{
    flushPendingNotePreview();
    if (auditionStopTimeMs > 0.0)
    {
        if (juce::Time::getMillisecondCounterHiRes() >= auditionStopTimeMs)
        {
            audioEngine.allPreviewNotesOff();
            auditionStopTimeMs = 0.0;
            if (!notePreviewPending) stopTimer();
        }
        else if (!isTimerRunning())
            startTimer(24);
    }
}

void PianoRollCanvas::flushPendingNotePreview()
{
    if (!notePreviewPending)
        return;
    stopTimer();
    notePreviewPending = false;
    if (previewTrackId != trackModel.getActiveTrackId()
        || previewClipId != trackModel.getActiveClipId())
        return;
    midiModel.updateSnapshot();
    trackModel.syncActiveClipNotes(midiModel.getNotes(), audioEngine.isPlaying());
    lastPreviewSyncMs = juce::Time::getMillisecondCounterHiRes();
}

void PianoRollCanvas::tracksChanged()
{
    flushPendingNotePreview();
    if (isSecondaryDominantPlacementActive())
    {
        const auto* track = trackModel.getTrackById(placementTrackId);
        if (track == nullptr || track->isLocked()
            || trackModel.getActiveTrackId() != placementTrackId
            || trackModel.getActiveClipId() != placementClipId
            || trackModel.getActiveClip() == nullptr)
            cancelSecondaryDominantPlacement();
    }
    if (auto* clip = trackModel.getActiveClip())
    {
        const auto& clipNotes = clip->notes;
        const auto& curNotes = midiModel.getNotes();
        bool differ = (clipNotes.size() != curNotes.size());
        if (!differ)
        {
            for (size_t i = 0; i < clipNotes.size(); ++i)
            {
                if (clipNotes[i].id != curNotes[i].id ||
                    clipNotes[i].pitch != curNotes[i].pitch ||
                    clipNotes[i].startBeat != curNotes[i].startBeat ||
                    clipNotes[i].lengthBeats != curNotes[i].lengthBeats)
                {
                    differ = true;
                    break;
                }
            }
        }
        if (differ)
        {
            midiModel.setAllNotes(clipNotes, false);
        }
    }
    else
    {
        if (!midiModel.getNotes().empty())
        {
            midiModel.clearAllNotes(false);
            midiModel.deselectAll();
        }
    }
    repaint();
}

void PianoRollCanvas::activeClipChanged()
{
    stopTimer();
    notePreviewPending = false;
    if (isSecondaryDominantPlacementActive())
        cancelSecondaryDominantPlacement();
    if (auto* clip = trackModel.getActiveClip())
    {
        midiModel.setAllNotes(clip->notes, false);
    }
    else
    {
        midiModel.clearAllNotes(false);
        midiModel.deselectAll();
    }
    repaint();
    updateMouseCursor();
}

void PianoRollCanvas::setZoomAndScroll(double ppb, double sob, float nh, int mp)
{
    pixelsPerBeat = ppb;
    scrollOffsetBeats = sob;
    noteHeight = nh;
    minPitch = mp;
    repaint();
}

void PianoRollCanvas::setTimelineExtentBeats(double endBeat)
{
    if (!std::isfinite(endBeat))
        return;

    constexpr double maxCanvasWidth = 1000000000.0;
    timelineExtentBeats = std::clamp(endBeat, 1024.0, maxCanvasWidth / pixelsPerBeat);
    const int width = (int)std::ceil(timelineExtentBeats * pixelsPerBeat);
    if (width != getWidth() || getHeight() != (int)(128 * noteHeight))
        setSize(width, (int)(128 * noteHeight));
}

void PianoRollCanvas::zoomHorizontal(float factor, int mouseCanvasX)
{
    auto* vp = findParentComponentOfClass<juce::Viewport>();
    int mouseInVpX = 0;
    double anchorBeat = 0.0;
    double oldPpb = pixelsPerBeat;

    if (vp != nullptr)
    {
        if (mouseCanvasX < 0)
        {
            mouseInVpX = vp->getViewWidth() / 2;
            mouseCanvasX = vp->getViewPositionX() + mouseInVpX;
        }
        else
        {
            mouseInVpX = mouseCanvasX - vp->getViewPositionX();
        }
        anchorBeat = (double)mouseCanvasX / oldPpb;
    }

    double newPpb = std::clamp(pixelsPerBeat * factor, 2.0, 500.0);
    if (std::abs(newPpb - oldPpb) < 0.001)
        return;

    pixelsPerBeat = newPpb;
    setTimelineExtentBeats(timelineExtentBeats);

    if (vp != nullptr)
    {
        int newCanvasX = (int)std::round(anchorBeat * newPpb);
        int newViewX = std::max(0, newCanvasX - mouseInVpX);
        vp->setViewPosition(newViewX, vp->getViewPositionY());
    }

    repaint();
}

void PianoRollCanvas::zoomVertical(float factor, int mouseCanvasY)
{
    auto* vp = findParentComponentOfClass<juce::Viewport>();
    int mouseInVpY = 0;
    double pitchFrac = 0.0;
    float oldNh = noteHeight;

    if (vp != nullptr)
    {
        if (mouseCanvasY < 0)
        {
            mouseInVpY = vp->getViewHeight() / 2;
            mouseCanvasY = vp->getViewPositionY() + mouseInVpY;
        }
        else
        {
            mouseInVpY = mouseCanvasY - vp->getViewPositionY();
        }
        pitchFrac = (double)mouseCanvasY / (double)oldNh;
    }

    float newNh = std::clamp(noteHeight * factor, 8.0f, 48.0f);
    if (std::abs(newNh - oldNh) < 0.001f)
        return;

    noteHeight = newNh;
    setSize(getWidth(), (int)(128 * noteHeight));

    if (vp != nullptr)
    {
        int newCanvasY = (int)std::round(pitchFrac * (double)newNh);
        int newViewY = std::max(0, newCanvasY - mouseInVpY);
        vp->setViewPosition(vp->getViewPositionX(), newViewY);
    }

    repaint();
}

float PianoRollCanvas::beatToX(double beat) const
{
    return (float)((beat - scrollOffsetBeats) * pixelsPerBeat);
}

double PianoRollCanvas::xToBeat(float x) const
{
    return (x / pixelsPerBeat) + scrollOffsetBeats;
}

float PianoRollCanvas::pitchToY(int pitch) const
{
    return (127 - pitch) * noteHeight;
}

int PianoRollCanvas::yToPitch(float y) const
{
    int p = 127 - (int)(y / noteHeight);
    return std::clamp(p, 0, 127);
}

void PianoRollCanvas::updateMouseCursor()
{
    if (isSecondaryDominantPlacementActive())
    {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }
    if (currentDrag == DragMode::AdjustVelocity)
    {
        setMouseCursor(velocityAdjustmentCursor());
        return;
    }
    if (currentDrag == DragMode::None && isMouseOver())
    {
        updateHoverCursor(getMouseXYRelative(),
            juce::ModifierKeys::getCurrentModifiersRealtime());
        return;
    }
    switch (midiModel.getCurrentTool())
    {
        case ToolType::Select: setMouseCursor(juce::MouseCursor::NormalCursor); break;
        case ToolType::Split:  setMouseCursor(juce::MouseCursor::CrosshairCursor); break;
        case ToolType::Erase:  setMouseCursor(juce::MouseCursor::CrosshairCursor); break;
        case ToolType::Draw:   setMouseCursor(juce::MouseCursor::NormalCursor); break;
        default:               setMouseCursor(juce::MouseCursor::NormalCursor); break;
    }
}

void PianoRollCanvas::beginSecondaryDominantPlacement(
    std::vector<ChordPlacementGroup> groups)
{
    std::vector<NoteEvent> generated;
    for (const auto& group : groups)
        generated.insert(generated.end(), group.notes.begin(), group.notes.end());
    beginChordPlacement(std::move(generated), "Place Secondary Dominant");
    if (!placementNotes.empty()) placementGroups = std::move(groups);
}

void PianoRollCanvas::beginChordPlacement(std::vector<NoteEvent> generated,
                                          juce::String undoName,
                                          juce::String description)
{
    cancelSecondaryDominantPlacement();
    if (generated.empty() || trackModel.getActiveClip() == nullptr)
        return;

    placementNotes = std::move(generated);
    placementGroups = { { placementNotes, {}, {} } };
    placementUndoName = std::move(undoName);
    placementDescription = std::move(description);
    placementTrackId = trackModel.getActiveTrackId();
    placementClipId = trackModel.getActiveClipId();
    placementHasPointer = false;
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    grabKeyboardFocus();
    const auto mouse = getMouseXYRelative();
    if (getLocalBounds().contains(mouse))
        updateSecondaryDominantPlacement((float)mouse.x);
    repaint();
}

void PianoRollCanvas::cancelSecondaryDominantPlacement()
{
    if (placementNotes.empty()) return;
    placementNotes.clear();
    placementGroups.clear();
    placementHasPointer = false;
    placementTrackId = -1;
    placementClipId = -1;
    placementUndoName = "Place Secondary Dominant";
    placementDescription.clear();
    updateMouseCursor();
    repaint();
}

void PianoRollCanvas::updateSecondaryDominantPlacement(float mouseX)
{
    if (!isSecondaryDominantPlacementActive()) return;
    placementAnchorBeat = midiModel.snapBeatToCellStart(xToBeat(mouseX));
    placementHasPointer = true;
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    repaint();
}

bool PianoRollCanvas::canPlaceSecondaryDominantAt(double beat) const
{
    if (!isSecondaryDominantPlacementActive()
        || trackModel.getActiveTrackId() != placementTrackId
        || trackModel.getActiveClipId() != placementClipId)
        return false;
    const auto* track = trackModel.getTrackById(placementTrackId);
    const auto* clip = trackModel.getActiveClip();
    if (track == nullptr || track->isLocked() || clip == nullptr)
        return false;
    const auto positioned = SecondaryDominant::shiftGeneratedNotes(placementNotes, beat);
    if (positioned.size() != placementNotes.size())
        return false;
    for (const auto& note : positioned)
        if (!std::isfinite(note.startBeat) || !std::isfinite(note.lengthBeats)
            || note.startBeat < clip->startBeat - 0.0001
            || note.startBeat + note.lengthBeats > clip->startBeat + clip->lengthBeats + 0.0001)
            return false;
    return true;
}

void PianoRollCanvas::commitSecondaryDominantPlacement()
{
    if (!placementHasPointer || !canPlaceSecondaryDominantAt(placementAnchorBeat))
        return;
    const auto before = midiModel.getNotes();
    auto after = before;
    for (auto& note : after)
        note.isSelected = false;
    auto generated = SecondaryDominant::shiftGeneratedNotes(placementNotes, placementAnchorBeat);
    const size_t selectedGroupSize = placementGroups.empty()
        ? generated.size() : placementGroups.front().notes.size();
    for (size_t i = 0; i < generated.size(); ++i)
    {
        auto& note = generated[i];
        note.id = midiModel.generateNextNoteId();
        note.isSelected = i < selectedGroupSize;
        after.push_back(note);
    }
    const bool committed = UndoActionHelper::performAtomic(&undoManager,
        new ModifyClipNotesUndoAction(midiModel, trackModel, placementTrackId, placementClipId,
                                       before, after), placementUndoName);
    if (committed)
    {
        lastPlacementCommitTimeMs = juce::Time::currentTimeMillis();
        cancelSecondaryDominantPlacement();
    }
}

int64_t PianoRollCanvas::findNoteAt(juce::Point<int> pos, bool& outNearLeftEdge, bool& outNearRightEdge) const
{
    outNearLeftEdge = false;
    outNearRightEdge = false;
    if (pos.y < 0 || pos.y >= getHeight()) return -1;

    const auto& notes = midiModel.getNotes();
    const int targetPitch = yToPitch((float)pos.y);

    for (auto it = notes.rbegin(); it != notes.rend(); ++it)
    {
        if (it->pitch != targetPitch) continue;
        float x1 = beatToX(it->startBeat);
        float x2 = beatToX(it->startBeat + it->lengthBeats);
        float w  = std::max(6.0f, x2 - x1);
        if ((float)pos.x >= x1 && (float)pos.x < x1 + w)
        {
            float edgeThreshold = std::min(10.0f, w * 0.35f);
            if (pos.x <= x1 + edgeThreshold)
                outNearLeftEdge = true;
            else if (pos.x >= x2 - edgeThreshold)
                outNearRightEdge = true;

            return it->id;
        }
    }
    return -1;
}

int64_t PianoRollCanvas::findClipBoundaryAt(juce::Point<int> pos, bool& outNearLeftEdge, bool& outNearRightEdge) const
{
    outNearLeftEdge = false;
    outNearRightEdge = false;
    const auto* clip = trackModel.getActiveClip();
    if (clip == nullptr)
        return -1;

    const float x1 = beatToX(clip->startBeat);
    const float x2 = beatToX(clip->startBeat + clip->lengthBeats);
    constexpr float threshold = 6.0f;
    if (std::abs((float)pos.x - x1) <= threshold)
    {
        outNearLeftEdge = true;
        return clip->id;
    }
    if (std::abs((float)pos.x - x2) <= threshold)
    {
        outNearRightEdge = true;
        return clip->id;
    }
    return -1;
}

const MidiClip* PianoRollCanvas::findInactiveClipAt(juce::Point<int> pos) const
{
    auto* activeTrk = trackModel.getTrackById(trackModel.getActiveTrackId());
    if (!activeTrk || activeTrk->getType() != TrackType::Instrument)
        return nullptr;

    const double beat = xToBeat((float)pos.x);
    if (const auto* active = trackModel.getActiveClip())
        if (beat >= active->startBeat && beat < active->startBeat + active->lengthBeats)
            return nullptr;
    for (const auto& clip : activeTrk->getClips())
    {
        if (clip.id != trackModel.getActiveClipId()
            && beat >= clip.startBeat && beat < clip.startBeat + clip.lengthBeats)
            return &clip;
    }
    return nullptr;
}

bool PianoRollCanvas::findGhostNoteAt(juce::Point<int> pos, GhostNoteHit& outHit) const
{
    if (pos.x < 0) return false;
    int clickPitch = yToPitch((float)pos.y);
    double clickBeat = xToBeat((float)pos.x);

    int activeTrkId = trackModel.getActiveTrackId();
    int64_t activeClipId = trackModel.getActiveClipId();

    // 1. Inactive clips on the active track (priority)
    if (auto* activeTrk = trackModel.getTrackById(activeTrkId))
    {
        for (const auto& clip : activeTrk->getClips())
        {
            if (clip.id == activeClipId) continue;
            for (const auto& n : clip.notes)
            {
                if (n.pitch == clickPitch && clickBeat >= n.startBeat && clickBeat <= (n.startBeat + n.lengthBeats))
                {
                    outHit.trackId = activeTrkId;
                    outHit.clipId = clip.id;
                    outHit.note = n;
                    float x1 = beatToX(n.startBeat);
                    float x2 = beatToX(n.startBeat + n.lengthBeats);
                    outHit.nearLeft = (std::abs((float)pos.x - x1) <= 4.0f);
                    outHit.nearRight = (std::abs((float)pos.x - x2) <= 4.0f);
                    return true;
                }
            }
        }
    }

    // 2. Visible clips from other Instrument tracks
    for (const auto& trk : trackModel.getTracks())
    {
        if (trk->getType() != TrackType::Instrument || trk->getId() == activeTrkId || trk->isMuted())
            continue;

        if (!trk->hasSelectedClips() && !trackModel.isPianoRollTrackVisible(trk->getId()))
            continue;

        for (const auto& clip : trk->getClips())
        {
            for (const auto& n : clip.notes)
            {
                if (n.pitch == clickPitch && clickBeat >= n.startBeat && clickBeat <= (n.startBeat + n.lengthBeats))
                {
                    outHit.trackId = trk->getId();
                    outHit.clipId = clip.id;
                    outHit.note = n;
                    float x1 = beatToX(n.startBeat);
                    float x2 = beatToX(n.startBeat + n.lengthBeats);
                    outHit.nearLeft = (std::abs((float)pos.x - x1) <= 4.0f);
                    outHit.nearRight = (std::abs((float)pos.x - x2) <= 4.0f);
                    return true;
                }
            }
        }
    }

    return false;
}

void PianoRollCanvas::updateHoverCursor(juce::Point<int> position,
                                       const juce::ModifierKeys& modifiers)
{
    if (currentDrag == DragMode::AdjustVelocity)
    {
        setMouseCursor(velocityAdjustmentCursor());
        return;
    }
    if (findInactiveClipAt(position) != nullptr)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }
    if (midiModel.getCurrentTool() == ToolType::Select)
    {
        if (modifiers.isAltDown())
        {
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
            return;
        }

        if (modifiers.isCtrlDown() && modifiers.isShiftDown())
        {
            bool nearLeft = false, nearRight = false;
            if (findNoteAt(position, nearLeft, nearRight) >= 0)
            {
                setMouseCursor(velocityAdjustmentCursor());
                return;
            }
        }

        bool nearClipLeft = false, nearClipRight = false;
        int64_t clipId = findClipBoundaryAt(position, nearClipLeft, nearClipRight);
        if (clipId >= 0 && (nearClipLeft || nearClipRight))
        {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }

        bool nearLeft = false, nearRight = false;
        int64_t id = findNoteAt(position, nearLeft, nearRight);
        if (id >= 0 && (nearLeft || nearRight))
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    else
    {
        const auto tool = midiModel.getCurrentTool();
        setMouseCursor(tool == ToolType::Split || tool == ToolType::Erase
            ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    }
}

void PianoRollCanvas::mouseEnter(const juce::MouseEvent& e)
{
    mouseMove(e);
}

void PianoRollCanvas::mouseMove(const juce::MouseEvent& e)
{
    if (isSecondaryDominantPlacementActive())
    {
        updateSecondaryDominantPlacement((float)e.x);
        return;
    }
    updateHoverCursor(e.getPosition(), e.mods);
}

void PianoRollCanvas::modifierKeysChanged(const juce::ModifierKeys& modifiers)
{
    if (isMouseOver() && !isSecondaryDominantPlacementActive())
        updateHoverCursor(getMouseXYRelative(), modifiers);
    juce::Component::modifierKeysChanged(modifiers);
}

void PianoRollCanvas::mouseExit(const juce::MouseEvent&)
{
    if (isSecondaryDominantPlacementActive())
    {
        placementHasPointer = false;
        repaint();
    }
    else if (currentDrag == DragMode::None)
        updateMouseCursor();
}

bool PianoRollCanvas::keyPressed(const juce::KeyPress& key)
{
    if (isSecondaryDominantPlacementActive() && key.isKeyCode(juce::KeyPress::escapeKey))
    {
        cancelSecondaryDominantPlacement();
        return true;
    }
    return false;
}

void PianoRollCanvas::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() && e.mods.isShiftDown())
    {
        // Vertical zoom (Semi-tone note height pivoting around mouse)
        float factor = (wheel.deltaY > 0.0f) ? 1.15f : 0.87f;
        zoomVertical(factor, e.y);
        if (onZoomChanged) onZoomChanged();
    }
    else if (e.mods.isCtrlDown())
    {
        // Horizontal zoom (Time / beats pivoting around mouse)
        float factor = (wheel.deltaY > 0.0f) ? 1.15f : 0.87f;
        zoomHorizontal(factor, e.x);
        if (onZoomChanged) onZoomChanged();
    }
    else if (e.mods.isShiftDown())
    {
        // Horizontal scroll matching Cubase: wheel down -> scroll right/forward (+X)
        int deltaPixels = (int)(-wheel.deltaY * 120.0f);
        if (onScrollRequested)
            onScrollRequested(deltaPixels);
    }
    else
    {
        Component::mouseWheelMove(e, wheel);
    }
}

void PianoRollCanvas::showContextMenuForSelectedNotes(juce::Point<int> screenPos)
{
    juce::PopupMenu menu;
    menu.addItem(1, tr("Quantize") + " (Q)");
    menu.addItem(2, tr("Transpose +12 (Octave Up)"));
    menu.addItem(3, tr("Transpose -12 (Octave Down)"));
    menu.addSeparator();
    menu.addItem(4, tr("Duplicate (Ctrl+D)"));
    menu.addItem(5, tr("Delete (Del)"));
    menu.addSeparator();
    menu.addItem(6, tr("Secondary Dominant"));
    menu.addSeparator();
    menu.addItem(7, tr("Add Selected Chords to Chord Track"));

    juce::Component::SafePointer<PianoRollCanvas> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
                       [this, safeThis](int result) {
        if (safeThis.getComponent() == nullptr) return;
        if (result == 1) // Quantize
        {
            auto oldNotes = midiModel.getNotes();
            auto newNotes = oldNotes;
            for (auto& n : newNotes)
                if (n.isSelected) n.startBeat = midiModel.quantizeBeat(n.startBeat);
            UndoActionHelper::performAtomic(&undoManager,
                new ModifyNotesUndoAction(midiModel, oldNotes, newNotes, "Quantize Selected Notes", &trackModel),
                "Quantize Selected Notes");
        }
        else if (result == 2) // Octave Up
        {
            NoteEditOperations::transposeSelectedOctave(midiModel, trackModel, undoManager, 1);
        }
        else if (result == 3) // Octave Down
        {
            NoteEditOperations::transposeSelectedOctave(midiModel, trackModel, undoManager, -1);
        }
        else if (result == 4) // Duplicate
        {
            double clipStart = -1.0, clipEnd = -1.0;
            if (const auto* clip = trackModel.getActiveClip()) { clipStart = clip->startBeat; clipEnd = clip->startBeat + clip->lengthBeats; }
            midiModel.duplicateSelectedNotes(&undoManager, clipStart, clipEnd, &trackModel);
        }
        else if (result == 5) // Delete
        {
            midiModel.deleteSelectedNotes(&undoManager, &trackModel);
        }
        else if (result == 6) // Secondary Dominant
        {
            juce::Component::SafePointer<PianoRollCanvas> safeCanvas(this);
            generateSecondaryDominantPreview(midiModel, trackModel,
                [safeCanvas](std::vector<ChordPlacementGroup> groups) {
                    if (auto* canvas = safeCanvas.getComponent())
                        canvas->beginSecondaryDominantPlacement(std::move(groups));
                });
        }
        else if (result == 7) // Add Selected Chords to Chord Track
        {
            auto sel = midiModel.getSelectedNotes();
            if (sel.empty())
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    tr("Add Selected Chords to Chord Track"),
                    tr("No notes selected. Please select notes first."));
                return;
            }

            const double barLength = (double)midiModel.getTimeSigNumerator() * 4.0
                / (double)std::max(1, midiModel.getTimeSigDenominator());
            const auto groups = ChordClassifier::identifyChordGroups(sel, barLength,
                [this](double beat) {
                    const auto layout = midiModel.getEffectiveLayoutAtBeat(beat, &trackModel);
                    return ChordAnalysisContext { layout.tonicPc, layout.scaleName,
                                                  layout.tonicName };
                });
            if (std::none_of(groups.begin(), groups.end(), [](const auto& group) {
                    return group.chord.confidence >= 0.5;
                }))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    tr("Add Selected Chords to Chord Track"),
                    tr("Select at least one clear chord or arpeggio."));
                return;
            }

            auto* chordTrk = trackModel.getChordTrack();
            if (chordTrk == nullptr)
                chordTrk = trackModel.addTrack("Chord Track", TrackType::Chord);
            if (chordTrk == nullptr) return;

            const auto oldChords = chordTrk->getChords();
            auto newChords = oldChords;
            int64_t nextId = juce::Time::currentTimeMillis();
            for (const auto& existing : oldChords)
                nextId = std::max(nextId, existing.id + 1);
            int inserted = 0;
            int collisions = 0;
            for (size_t index = 0; index < groups.size(); ++index)
            {
                const auto& group = groups[index];
                if (group.chord.confidence < 0.5) continue;
                double length = std::max(0.25, group.endBeat - group.startBeat);
                if (index + 1 < groups.size())
                    length = std::min(length, groups[index + 1].startBeat - group.startBeat);
                if (length <= 0.01) continue;
                if (!trackModel.canPlaceEvent(chordTrk->getId(), group.startBeat, length))
                {
                    ++collisions;
                    continue;
                }
                ChordEvent c;
                c.id = nextId++;
                c.startBeat = group.startBeat;
                c.lengthBeats = length;
                c.chordName = group.chord.fullName;
                c.romanNumeral = group.chord.romanNumeral;
                c.color = juce::Colour(0xfff59e0b);
                newChords.push_back(c);
                ++inserted;
            }
            if (inserted > 0)
            {
                std::stable_sort(newChords.begin(), newChords.end(),
                    [](const ChordEvent& a, const ChordEvent& b) {
                        return a.startBeat < b.startBeat;
                    });
                UndoActionHelper::performAtomic(&undoManager,
                    new ModifyChordsUndoAction(trackModel, chordTrk->getId(), oldChords, newChords, "Add Chord to Chord Track"),
                    "Add Chord to Chord Track");
            }
            if (collisions > 0)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    tr("Add Selected Chords to Chord Track"),
                    juce::String(collisions) + " " + tr("chord positions were skipped because the chord track is occupied."));
            }
        }
    });
}

void PianoRollCanvas::mouseDown(const juce::MouseEvent& e)
{
    if (auditionStopTimeMs > 0.0)
    {
        audioEngine.allPreviewNotesOff();
        auditionStopTimeMs = 0.0;
        if (!notePreviewPending) stopTimer();
    }
    ctrlDragDirectionChosen = false;
    ctrlVerticalLocked = false;
    pendingCtrlToggleNoteId = -1;
    maxDragDistance = 0;
    if (onFocusRequested)
        onFocusRequested();

    if (isSecondaryDominantPlacementActive())
    {
        currentDrag = DragMode::None;
        if (e.mods.isRightButtonDown())
            cancelSecondaryDominantPlacement();
        else if (e.mods.isLeftButtonDown())
        {
            updateSecondaryDominantPlacement((float)e.x);
            commitSecondaryDominantPlacement();
        }
        else if (e.mods.isMiddleButtonDown())
        {
            dragStartPos = e.getScreenPosition();
            currentDrag = DragMode::MiddlePan;
        }
        return;
    }

    if (e.getNumberOfClicks() > 1
        && juce::Time::currentTimeMillis() - lastPlacementCommitTimeMs < 450)
        return;

    if (onClearOverviewSelection)
        onClearOverviewSelection();

    // 1. Middle Mouse Button Pan
    if (e.mods.isMiddleButtonDown())
    {
        dragStartPos = e.getScreenPosition();
        currentDrag = DragMode::MiddlePan;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    // 2. Right Click Handling
    if (e.mods.isRightButtonDown())
    {
        if (e.mods.isShiftDown())
            showContextMenuForSelectedNotes(e.getScreenPosition());
        else
            FloatingToolbox::showAt(midiModel, this, e.getScreenPosition());
        return;
    }

    dragStartPos = e.getPosition();
    dragStartBeat = xToBeat((float)e.x);
    dragStartPitch = yToPitch((float)e.y);

    auto tool = midiModel.getCurrentTool();

    if (tool != ToolType::Select)
        if (const auto* inactive = findInactiveClipAt(e.getPosition()))
        {
            trackModel.setActiveClip(trackModel.getActiveTrackId(), inactive->id, &midiModel);
            currentDrag = DragMode::None;
            repaint();
            return;
        }

    if (tool == ToolType::Select)
    {
        // 1. First priority: Check if clicking a note on the ACTIVE clip
        bool nearLeft = false, nearRight = false;
        int64_t noteId = findNoteAt(e.getPosition(), nearLeft, nearRight);

        if (noteId >= 0)
        {
            lastClickedNoteId = noteId;
            lastNoteClickTimeMs = juce::Time::currentTimeMillis();
            activeNoteId = noteId;
            bool isAlreadySel = midiModel.isNoteSelected(noteId);

            if (e.mods.isAltDown())
            {
                // Alt + Drag Duplicate: clone current selected notes or this note
                if (!isAlreadySel)
                    midiModel.selectNote(noteId, false);

                captureInitialNotesForDrag();
                duplicatePreviewNotes = midiModel.getSelectedNotes();
                currentDrag = DragMode::AltDuplicate;

                if (const auto* n = midiModel.findNoteById(noteId))
                {
                    currentPreviewPitch = n->pitch;
                    audioEngine.previewNoteOn(currentPreviewPitch, n->velocity);
                }
                return;
            }

            if (e.mods.isCtrlDown() && e.mods.isShiftDown())
            {
                if (!isAlreadySel)
                    midiModel.selectNote(noteId, true);
            }
            else if (e.mods.isCtrlDown())
            {
                if (isAlreadySel)
                    pendingCtrlToggleNoteId = noteId;
                else
                    midiModel.selectNote(noteId, true);
            }
            else if (e.mods.isShiftDown())
            {
                if (isAlreadySel)
                    midiModel.deselectNote(noteId);
                else
                    midiModel.selectNote(noteId, true);
            }
            else if (!isAlreadySel)
            {
                // Direct drag on unselected note: deselect previous notes and select ONLY this note!
                midiModel.selectNote(noteId, false);
            }

            // Capture initialNotesState AFTER updating selection so dragging directly moves this note!
            captureInitialNotesForDrag();

            if (e.mods.isCtrlDown() && e.mods.isShiftDown())
            {
                currentDrag = DragMode::AdjustVelocity;
                setMouseCursor(velocityAdjustmentCursor());
            }
            else if (nearLeft)
                currentDrag = DragMode::ResizeLeft;
            else if (nearRight)
                currentDrag = DragMode::ResizeRight;
            else
                currentDrag = DragMode::MoveNotes;

            if (const auto* n = midiModel.findNoteById(noteId))
            {
                currentPreviewPitch = n->pitch;
                audioEngine.previewNoteOn(currentPreviewPitch, n->velocity,
                                          currentDrag == DragMode::AdjustVelocity);
            }
            return;
        }

        // 2. Second priority: Check if clicking a GHOST note on another clip or track (Cubase parity: instant focus switch)
        GhostNoteHit ghostHit;
        const auto* inactiveClip = findInactiveClipAt(e.getPosition());
        if (findGhostNoteAt(e.getPosition(), ghostHit)
            && (inactiveClip == nullptr || ghostHit.trackId == trackModel.getActiveTrackId()))
        {
            trackModel.setActiveClip(ghostHit.trackId, ghostHit.clipId, &midiModel);

            midiModel.selectNote(ghostHit.note.id, false);
            activeNoteId = ghostHit.note.id;
            captureInitialNotesForDrag();

            if (ghostHit.nearLeft)
                currentDrag = DragMode::ResizeLeft;
            else if (ghostHit.nearRight)
                currentDrag = DragMode::ResizeRight;
            else
                currentDrag = DragMode::MoveNotes;

            currentPreviewPitch = ghostHit.note.pitch;
            audioEngine.previewNoteOn(currentPreviewPitch, ghostHit.note.velocity);

            repaint();
            return;
        }

        if (inactiveClip != nullptr)
        {
            trackModel.setActiveClip(trackModel.getActiveTrackId(), inactiveClip->id, &midiModel);
            currentDrag = DragMode::None;
            activeNoteId = -1;
            repaint();
            return;
        }

        // 3. Third priority: Check clip boundary resizing on active track
        bool nearClipLeft = false, nearClipRight = false;
        int64_t bndClipId = findClipBoundaryAt(e.getPosition(), nearClipLeft, nearClipRight);
        if (bndClipId >= 0 && (nearClipLeft || nearClipRight))
        {
            if (auto* activeTrk = trackModel.getTrackById(trackModel.getActiveTrackId()))
            {
                for (const auto& c : activeTrk->getClips())
                {
                    if (c.id == bndClipId)
                    {
                        dragClipId = bndClipId;
                        clipInitialStartBeat = c.startBeat;
                        clipInitialLengthBeats = c.lengthBeats;
                        currentDrag = nearClipLeft ? DragMode::ResizeClipStart : DragMode::ResizeClipEnd;
                        return;
                    }
                }
            }
        }

        // 4. Fourth priority: Empty space clicked
        {
            activeNoteId = -1;
            if (e.mods.isAltDown())
            {
                // Alt + Click on empty canvas draws a new note (Draw tool behavior)
                captureInitialNotesForDrag();
                double snapBeat = midiModel.snapBeatToCellStart(dragStartBeat);
                double snapLen = midiModel.getSnapBeatInterval();
                if (snapLen <= 0.0) snapLen = 1.0;

                if (const auto* clip = trackModel.getActiveClip())
                {
                    if (snapBeat < clip->startBeat || snapBeat >= (clip->startBeat + clip->lengthBeats))
                        return;
                }

                NoteEvent newNote;
                newNote.id = midiModel.generateNextNoteId();
                newNote.pitch = dragStartPitch;
                newNote.startBeat = snapBeat;
                newNote.lengthBeats = std::max(0.03125, snapLen);
                if (const auto* clip = trackModel.getActiveClip())
                    newNote.lengthBeats = std::min(newNote.lengthBeats, (clip->startBeat + clip->lengthBeats) - snapBeat);
                newNote.velocity = 0.8f;
                newNote.isSelected = true;

                midiModel.deselectAll();
                midiModel.addNote(newNote, true);

                activeNoteId = newNote.id;
                currentDrag = DragMode::DrawNewNote;

                currentPreviewPitch = newNote.pitch;
                audioEngine.previewNoteOn(currentPreviewPitch, newNote.velocity);
                return;
            }

            if (e.getNumberOfClicks() >= 2)
            {
                // Cubase logic: double-click 2nd press creates note and immediately allows dragging its length
                captureInitialNotesForDrag();
                double snapBeat = midiModel.snapBeatToCellStart(dragStartBeat);
                double snapLen = midiModel.getSnapBeatInterval();
                if (snapLen <= 0.0) snapLen = 1.0;

                if (const auto* clip = trackModel.getActiveClip())
                {
                    if (snapBeat < clip->startBeat || snapBeat >= (clip->startBeat + clip->lengthBeats))
                        return;
                }

                NoteEvent newNote;
                newNote.id = midiModel.generateNextNoteId();
                newNote.pitch = dragStartPitch;
                newNote.startBeat = snapBeat;
                newNote.lengthBeats = std::max(0.03125, snapLen);
                if (const auto* clip = trackModel.getActiveClip())
                    newNote.lengthBeats = std::min(newNote.lengthBeats, (clip->startBeat + clip->lengthBeats) - snapBeat);
                newNote.velocity = 0.8f;
                newNote.isSelected = true;

                midiModel.deselectAll();
                midiModel.addNote(newNote, true);

                activeNoteId = newNote.id;
                currentDrag = DragMode::DrawNewNote;

                justCreatedNoteOnDoubleClick = true;
                lastCreatedNoteIdOnDoubleClick = newNote.id;

                currentPreviewPitch = newNote.pitch;
                audioEngine.previewNoteOn(currentPreviewPitch, newNote.velocity);
                return;
            }

            if (!e.mods.isShiftDown() && !e.mods.isCtrlDown())
                midiModel.deselectAll();

            captureInitialNotesForDrag();
            currentDrag = DragMode::MarqueeSelect;
            marqueeRect = juce::Rectangle<int>(e.x, e.y, 0, 0);
        }
    }
    else if (tool == ToolType::Draw)
    {
        captureInitialNotesForDrag();
        double snapBeat = midiModel.snapBeatToCellStart(dragStartBeat);
        double snapLen = midiModel.getSnapBeatInterval();
        if (snapLen <= 0.0) snapLen = 1.0;

        if (const auto* clip = trackModel.getActiveClip())
        {
            if (snapBeat < clip->startBeat || snapBeat >= (clip->startBeat + clip->lengthBeats))
                return;
        }

        NoteEvent newNote;
        newNote.id = midiModel.generateNextNoteId();
        newNote.pitch = dragStartPitch;
        newNote.startBeat = snapBeat;
        newNote.lengthBeats = std::max(0.03125, snapLen);
        if (const auto* clip = trackModel.getActiveClip())
            newNote.lengthBeats = std::min(newNote.lengthBeats, (clip->startBeat + clip->lengthBeats) - snapBeat);
        newNote.velocity = 0.8f;
        newNote.isSelected = true;

        midiModel.deselectAll();
        midiModel.addNote(newNote, true);

        activeNoteId = newNote.id;
        currentDrag = DragMode::DrawNewNote;

        currentPreviewPitch = newNote.pitch;
        audioEngine.previewNoteOn(currentPreviewPitch, newNote.velocity);
    }
    else if (tool == ToolType::Split)
    {
        bool nl, nr;
        int64_t noteId = findNoteAt(e.getPosition(), nl, nr);
        if (noteId >= 0)
        {
            if (const auto* n = midiModel.findNoteById(noteId))
            {
                double splitBeat = midiModel.quantizeBeat(dragStartBeat);
                if (splitBeat > n->startBeat && splitBeat < (n->startBeat + n->lengthBeats))
                {
                    NoteEvent left = *n;
                    left.lengthBeats = splitBeat - n->startBeat;

                    NoteEvent right = *n;
                    right.id = midiModel.generateNextNoteId();
                    right.startBeat = splitBeat;
                    right.lengthBeats = (n->startBeat + n->lengthBeats) - splitBeat;

                    auto oldNotes = midiModel.getNotes();
                    midiModel.removeNoteById(noteId, false);
                    midiModel.addNote(left, false);
                    midiModel.addNote(right, true);

                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyNotesUndoAction(midiModel, oldNotes, midiModel.getNotes(), "Split Note", &trackModel),
                        "Split Note");
                }
            }
        }
    }
    else if (tool == ToolType::Erase)
    {
        bool nl, nr;
        int64_t noteId = findNoteAt(e.getPosition(), nl, nr);
        if (noteId >= 0)
        {
            if (const auto* n = midiModel.findNoteById(noteId))
            {
                UndoActionHelper::performAtomic(&undoManager,
                    new DeleteNotesUndoAction(midiModel, { *n }, &trackModel),
                    "Erase Note");
            }
        }
    }
}

void PianoRollCanvas::mouseDrag(const juce::MouseEvent& e)
{
    maxDragDistance = std::max(maxDragDistance, e.getDistanceFromDragStart());
    if (currentDrag == DragMode::AdjustVelocity)
    {
        if (e.getDistanceFromDragStart() < 4) return;
        const float delta = (float)(dragStartPos.y - e.y) / 140.0f;
        bool changed = false;
        float previewVelocity = -1.0f;
        auto& notes = midiModel.getNotes();
        for (const size_t index : selectedDragNoteIndices)
        {
            const auto& before = initialNotesState[index];
            auto* note = index < notes.size() && notes[index].id == before.id
                ? &notes[index] : midiModel.findNoteById(before.id);
            if (note == nullptr) continue;
            const float velocity = std::clamp(before.velocity + delta, 1.0f / 127.0f, 1.0f);
            changed |= note->velocity != velocity;
            note->velocity = velocity;
            if (before.id == activeNoteId)
                previewVelocity = velocity;
        }
        if (previewVelocity > 0.0f && currentPreviewPitch >= 0)
            audioEngine.updatePreviewNoteVelocity(currentPreviewPitch, previewVelocity);
        if (changed) midiModel.triggerNotePreviewChanged(true);
        return;
    }
    if (currentDrag == DragMode::MoveNotes)
    {
        double currentBeat = xToBeat((float)e.x);
        int currentPitch = yToPitch((float)e.y);

        int deltaPitch = currentPitch - dragStartPitch;

        if (e.mods.isCtrlDown() && !e.mods.isShiftDown() && !ctrlDragDirectionChosen
            && e.getDistanceFromDragStart() >= 4)
        {
            const int horizontal = std::abs(e.x - dragStartPos.x);
            const int vertical = std::abs(e.y - dragStartPos.y);
            if (vertical >= 4 && vertical >= horizontal + 2)
            {
                ctrlDragDirectionChosen = true;
                ctrlVerticalLocked = true;
            }
            else if (horizontal >= 4 && horizontal >= vertical + 2)
                ctrlDragDirectionChosen = true;
        }

        // Preview pitch on vertical drag
        if (currentPitch != currentPreviewPitch)
        {
            if (currentPreviewPitch >= 0)
                audioEngine.previewNoteOff(currentPreviewPitch);
            currentPreviewPitch = currentPitch;
            audioEngine.previewNoteOn(currentPreviewPitch, 0.8f);
        }

        const NoteEvent* primaryNote = primaryDragNoteIndex < initialNotesState.size()
            ? &initialNotesState[primaryDragNoteIndex] : nullptr;

        double deltaBeat = 0.0;
        if (ctrlVerticalLocked)
            deltaBeat = 0.0;
        else if (e.mods.isCtrlDown())
        {
            // Requirement 8: Ctrl bypasses snap for smooth micro-movement
            deltaBeat = currentBeat - dragStartBeat;
        }
        else if (midiModel.isSnapEnabled() && primaryNote != nullptr)
        {
            // Requirement 7: Absolute Note-Head Quantize Snapping
            double targetStart = primaryNote->startBeat + (currentBeat - dragStartBeat);
            double snappedTarget = midiModel.quantizeBeat(targetStart);
            deltaBeat = snappedTarget - primaryNote->startBeat;
        }
        else
        {
            deltaBeat = currentBeat - dragStartBeat;
        }

        // Selection bounding box clamp to clip boundaries (strict container)
        double clipStart = 0.0;
        double clipEnd = 1e9;
        if (const auto* clip = trackModel.getActiveClip())
        {
            clipStart = clip->startBeat;
            clipEnd = clip->startBeat + clip->lengthBeats;
        }

        if (!selectedDragNoteIndices.empty())
        {
            if (selectedDragMinBeat + deltaBeat < clipStart)
                deltaBeat = clipStart - selectedDragMinBeat;
            if (selectedDragMaxEndBeat + deltaBeat > clipEnd)
                deltaBeat = clipEnd - selectedDragMaxEndBeat;
        }
        if (dragDeltaInitialized && deltaBeat == lastDragBeatDelta
            && deltaPitch == lastDragPitchDelta)
            return;
        lastDragBeatDelta = deltaBeat;
        lastDragPitchDelta = deltaPitch;
        dragDeltaInitialized = true;

        bool changed = false;
        auto& currentNotes = midiModel.getNotes();
        for (const size_t index : selectedDragNoteIndices)
        {
            const auto& initNote = initialNotesState[index];
            auto* n = index < currentNotes.size() && currentNotes[index].id == initNote.id
                ? &currentNotes[index] : midiModel.findNoteById(initNote.id);
            if (n != nullptr)
            {
                const double start = std::clamp(initNote.startBeat + deltaBeat,
                    clipStart, std::max(clipStart, clipEnd - n->lengthBeats));
                const int pitch = std::clamp(initNote.pitch + deltaPitch, 0, 127);
                changed |= n->startBeat != start || n->pitch != pitch;
                n->startBeat = start;
                n->pitch = pitch;
            }
        }
        if (changed) midiModel.triggerNotePreviewChanged(true);
    }
    else if (currentDrag == DragMode::ResizeRight)
    {
        double currentBeat = xToBeat((float)e.x);
        const NoteEvent* primaryNote = primaryDragNoteIndex < initialNotesState.size()
            ? &initialNotesState[primaryDragNoteIndex] : nullptr;

        double deltaLen = currentBeat - dragStartBeat;
        if (!e.mods.isCtrlDown() && midiModel.isSnapEnabled() && primaryNote != nullptr)
        {
            double targetEnd = primaryNote->startBeat + primaryNote->lengthBeats + deltaLen;
            double snappedEnd = midiModel.quantizeBeat(targetEnd);
            deltaLen = (snappedEnd - primaryNote->startBeat) - primaryNote->lengthBeats;
        }

        double clipEnd = 1e9;
        if (const auto* clip = trackModel.getActiveClip())
            clipEnd = clip->startBeat + clip->lengthBeats;
        if (dragDeltaInitialized && deltaLen == lastDragBeatDelta)
            return;
        lastDragBeatDelta = deltaLen;
        dragDeltaInitialized = true;
        double minLen = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.0625;
        if (minLen <= 0.0) minLen = 0.0625;
        bool changed = false;
        auto& currentNotes = midiModel.getNotes();
        for (const size_t index : selectedDragNoteIndices)
        {
            const auto& initNote = initialNotesState[index];
            auto* n = index < currentNotes.size() && currentNotes[index].id == initNote.id
                ? &currentNotes[index] : midiModel.findNoteById(initNote.id);
            if (n != nullptr)
            {
                const double maxLen = std::max(minLen, clipEnd - n->startBeat);
                const double targetLen = std::clamp(initNote.lengthBeats + deltaLen, minLen, maxLen);
                changed |= n->lengthBeats != targetLen;
                n->lengthBeats = targetLen;
            }
        }
        if (changed) midiModel.triggerNotePreviewChanged(true);
    }
    else if (currentDrag == DragMode::ResizeLeft)
    {
        double currentBeat = xToBeat((float)e.x);
        const NoteEvent* primaryNote = primaryDragNoteIndex < initialNotesState.size()
            ? &initialNotesState[primaryDragNoteIndex] : nullptr;

        double deltaStart = currentBeat - dragStartBeat;
        if (!e.mods.isCtrlDown() && midiModel.isSnapEnabled() && primaryNote != nullptr)
        {
            double targetStart = primaryNote->startBeat + deltaStart;
            double snappedStart = midiModel.quantizeBeat(targetStart);
            deltaStart = snappedStart - primaryNote->startBeat;
        }

        double clipStart = 0.0;
        if (const auto* clip = trackModel.getActiveClip())
            clipStart = clip->startBeat;
        if (dragDeltaInitialized && deltaStart == lastDragBeatDelta)
            return;
        lastDragBeatDelta = deltaStart;
        dragDeltaInitialized = true;
        double minLen = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.0625;
        if (minLen <= 0.0) minLen = 0.0625;
        bool changed = false;
        auto& currentNotes = midiModel.getNotes();
        for (const size_t index : selectedDragNoteIndices)
        {
            const auto& initNote = initialNotesState[index];
            auto* n = index < currentNotes.size() && currentNotes[index].id == initNote.id
                ? &currentNotes[index] : midiModel.findNoteById(initNote.id);
            if (n != nullptr)
            {
                const double newStart = std::clamp(initNote.startBeat + deltaStart,
                    clipStart, initNote.startBeat + initNote.lengthBeats - minLen);
                const double newLen = (initNote.startBeat + initNote.lengthBeats) - newStart;
                if (newLen >= minLen)
                {
                    changed |= n->startBeat != newStart || n->lengthBeats != newLen;
                    n->startBeat = newStart;
                    n->lengthBeats = newLen;
                }
            }
        }
        if (changed) midiModel.triggerNotePreviewChanged(true);
    }
    else if (currentDrag == DragMode::DrawNewNote)
    {
        double currentBeat = xToBeat((float)e.x);

        if (auto* n = midiModel.findNoteById(activeNoteId))
        {
            double newLen = currentBeat - n->startBeat;
            if (midiModel.isSnapEnabled())
            {
                double snapInt = midiModel.getSnapBeatInterval();
                if (snapInt > 0.0001)
                    newLen = std::max(snapInt, midiModel.quantizeBeat(newLen));
            }
            double minLen = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.03125;
            if (minLen <= 0.0) minLen = 0.03125;
            double clipEnd = 1e9;
            if (const auto* clip = trackModel.getActiveClip())
                clipEnd = clip->startBeat + clip->lengthBeats;
            double maxLen = (clipEnd < 1e8) ? std::max(minLen, clipEnd - n->startBeat) : 1e9;
            const double targetLen = std::clamp(newLen, minLen, maxLen);
            if (n->lengthBeats != targetLen)
            {
                n->lengthBeats = targetLen;
                midiModel.triggerNotePreviewChanged(true);
            }
        }
    }
    else if (currentDrag == DragMode::MarqueeSelect)
    {
        marqueeRect = juce::Rectangle<int>(dragStartPos, e.getPosition());
        double b1 = xToBeat((float)marqueeRect.getX());
        double b2 = xToBeat((float)marqueeRect.getRight());
        int p1 = yToPitch((float)marqueeRect.getY());
        int p2 = yToPitch((float)marqueeRect.getBottom());

        midiModel.selectWithinRange(b1, b2, p2, p1,
            e.mods.isShiftDown() || e.mods.isCtrlDown());
        repaint();
    }
    else if (currentDrag == DragMode::MiddlePan)
    {
        auto currentScreenPos = e.getScreenPosition();
        int deltaX = currentScreenPos.x - dragStartPos.x;
        int deltaY = currentScreenPos.y - dragStartPos.y;
        dragStartPos = currentScreenPos;

        if (onPanRequested)
            onPanRequested(deltaX, deltaY);
    }
    else if (currentDrag == DragMode::ResizeClipEnd)
    {
        double currentBeat = xToBeat((float)e.x);
        double delta = currentBeat - dragStartBeat;
        if (midiModel.isSnapEnabled())
        {
            double snapInt = midiModel.getSnapBeatInterval();
            delta = std::round(delta / snapInt) * snapInt;
        }

        if (auto* activeTrk = trackModel.getTrackById(trackModel.getActiveTrackId()))
        {
            for (auto& c : activeTrk->getClips())
            {
                if (c.id == dragClipId)
                {
                    double newLen = std::max(0.25, clipInitialLengthBeats + delta);
                    c.lengthBeats = newLen;
                    trackModel.triggerTracksChanged();
                    break;
                }
            }
        }
        repaint();
    }
    else if (currentDrag == DragMode::ResizeClipStart)
    {
        double currentBeat = xToBeat((float)e.x);
        double delta = currentBeat - dragStartBeat;
        if (midiModel.isSnapEnabled())
        {
            double snapInt = midiModel.getSnapBeatInterval();
            delta = std::round(delta / snapInt) * snapInt;
        }

        if (auto* activeTrk = trackModel.getTrackById(trackModel.getActiveTrackId()))
        {
            for (auto& c : activeTrk->getClips())
            {
                if (c.id == dragClipId)
                {
                    double maxStart = clipInitialStartBeat + clipInitialLengthBeats - 0.25;
                    double newStart = std::clamp(clipInitialStartBeat + delta, 0.0, maxStart);
                    double newLen = (clipInitialStartBeat + clipInitialLengthBeats) - newStart;
                    c.startBeat = newStart;
                    c.lengthBeats = newLen;
                    trackModel.triggerTracksChanged();
                    break;
                }
            }
        }
        repaint();
    }
    else if (currentDrag == DragMode::AltDuplicate)
    {
        double currentBeat = xToBeat((float)e.x);
        int currentPitch = yToPitch((float)e.y);

        int deltaPitch = currentPitch - dragStartPitch;

        const NoteEvent* primaryNote = primaryDragNoteIndex < initialNotesState.size()
            ? &initialNotesState[primaryDragNoteIndex] : nullptr;

        double deltaBeat = 0.0;
        if (e.mods.isCtrlDown())
        {
            deltaBeat = currentBeat - dragStartBeat;
        }
        else if (midiModel.isSnapEnabled() && primaryNote != nullptr)
        {
            double targetStart = primaryNote->startBeat + (currentBeat - dragStartBeat);
            double snappedTarget = midiModel.quantizeBeat(targetStart);
            deltaBeat = snappedTarget - primaryNote->startBeat;
        }
        else
        {
            deltaBeat = currentBeat - dragStartBeat;
        }

        if (dragDeltaInitialized && deltaBeat == lastDragBeatDelta
            && deltaPitch == lastDragPitchDelta)
            return;
        lastDragBeatDelta = deltaBeat;
        lastDragPitchDelta = deltaPitch;
        dragDeltaInitialized = true;

        if (currentPitch != currentPreviewPitch)
        {
            if (currentPreviewPitch >= 0)
                audioEngine.previewNoteOff(currentPreviewPitch);
            currentPreviewPitch = currentPitch;
            audioEngine.previewNoteOn(currentPreviewPitch, 0.8f);
        }

        // Update ghost / preview positions for duplicate notes
        duplicatePreviewNotes.clear();
        duplicatePreviewNotes.reserve(selectedDragNoteIndices.size());
        for (const size_t index : selectedDragNoteIndices)
        {
            auto n = initialNotesState[index];
            n.startBeat = std::max(0.0, n.startBeat + deltaBeat);
            n.pitch = std::clamp(n.pitch + deltaPitch, 0, 127);
            duplicatePreviewNotes.push_back(n);
        }
        repaint();
    }
}

void PianoRollCanvas::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.mods.isAltDown()) return;
    if (isSecondaryDominantPlacementActive()
        || juce::Time::currentTimeMillis() - lastPlacementCommitTimeMs < 450)
        return;
    if (justCreatedNoteOnDoubleClick)
    {
        justCreatedNoteOnDoubleClick = false;
        lastCreatedNoteIdOnDoubleClick = -1;
        return;
    }

    // Drag movement threshold: avoid accidental deletion during note drag/move
    if (e.getDistanceFromDragStart() > 3)
        return;

    if (midiModel.getCurrentTool() == ToolType::Select)
    {
        bool nearLeft = false, nearRight = false;
        int64_t noteId = findNoteAt(e.getPosition(), nearLeft, nearRight);
        if (noteId >= 0 && noteId != lastCreatedNoteIdOnDoubleClick)
        {
            int64_t now = juce::Time::currentTimeMillis();
            // Accidental click protection: only delete if second click is rapid (<= 380ms) and on same note
            if (noteId == lastClickedNoteId && (now - lastNoteClickTimeMs) <= 380)
            {
                if (const auto* n = midiModel.findNoteById(noteId))
                {
                    UndoActionHelper::performAtomic(&undoManager,
                        new DeleteNotesUndoAction(midiModel, { *n }, &trackModel),
                        "Delete Note");
                }
            }
        }
    }
    lastCreatedNoteIdOnDoubleClick = -1;
}

void PianoRollCanvas::mouseUp(const juce::MouseEvent& e)
{
    justCreatedNoteOnDoubleClick = false;
    std::vector<NoteEvent> auditionNotes;

    // CRITICAL BUGFIX: reliably stop any sounding preview notes
    if (currentPreviewPitch >= 0)
    {
        audioEngine.previewNoteOff(currentPreviewPitch);
        currentPreviewPitch = -1;
    }
    audioEngine.allPreviewNotesOff();
    activeNoteId = -1;

    if (currentDrag == DragMode::MoveNotes || currentDrag == DragMode::ResizeRight
        || currentDrag == DragMode::ResizeLeft || currentDrag == DragMode::AdjustVelocity)
    {
        const auto& currentNotes = midiModel.getNotes();
        const bool changed = initialNotesState.size() != currentNotes.size()
            || !std::equal(initialNotesState.begin(), initialNotesState.end(), currentNotes.begin(),
                [](const NoteEvent& before, const NoteEvent& after) {
                    return before.id == after.id && before.pitch == after.pitch
                        && before.startBeat == after.startBeat
                        && before.lengthBeats == after.lengthBeats
                        && before.velocity == after.velocity && before.isMuted == after.isMuted;
                });
        if (changed)
        {
            pendingCtrlToggleNoteId = -1;
            UndoActionHelper::performAtomic(&undoManager,
                new ModifyNotesUndoAction(midiModel, initialNotesState, currentNotes,
                                          "Edit Notes", &trackModel), "Edit Notes");
        }
    }
    else if (currentDrag == DragMode::AltDuplicate)
    {
        if (maxDragDistance < 6)
            auditionNotes = midiModel.getSelectedNotes();
        else if (!duplicatePreviewNotes.empty())
        {
            std::vector<NoteEvent> newNotes;
            for (auto& n : duplicatePreviewNotes)
            {
                n.id = midiModel.generateNextNoteId();
                n.isSelected = true;
                newNotes.push_back(n);
            }
            midiModel.deselectAll();
            midiModel.addNotes(newNotes, true);
            UndoActionHelper::performAtomic(&undoManager,
                new AddNotesUndoAction(midiModel, newNotes, &trackModel),
                "Duplicate Notes");
            duplicatePreviewNotes.clear();
        }
    }
    else if (currentDrag == DragMode::DrawNewNote)
    {
        std::vector<NoteEvent> added;
        std::unordered_set<int64_t> oldIds;
        oldIds.reserve(initialNotesState.size());
        for (const auto& old : initialNotesState) oldIds.insert(old.id);
        for (const auto& n : midiModel.getNotes())
            if (!oldIds.count(n.id))
                added.push_back(n);
        if (!added.empty())
        {
            UndoActionHelper::performAtomic(&undoManager,
                new AddNotesUndoAction(midiModel, added, &trackModel),
                "Create Note");
        }
    }

    if (currentDrag == DragMode::ResizeClipStart || currentDrag == DragMode::ResizeClipEnd)
    {
        trackModel.syncActiveClipNotes(midiModel.getNotes());
        dragClipId = -1;
    }

    flushPendingNotePreview();

    if (pendingCtrlToggleNoteId >= 0 && maxDragDistance < 4
        && e.mods.isCtrlDown() && !e.mods.isShiftDown())
        midiModel.deselectNote(pendingCtrlToggleNoteId);
    pendingCtrlToggleNoteId = -1;

    currentDrag = DragMode::None;
    marqueeRect = {};
    duplicatePreviewNotes.clear();
    selectedDragNoteIndices.clear();
    initialNotesState.clear();
    updateMouseCursor();
    repaint();
    if (!auditionNotes.empty())
    {
        std::set<int> soundingPitches;
        for (const auto& note : auditionNotes)
            if (!note.isMuted && soundingPitches.insert(note.pitch).second)
                audioEngine.previewNoteOn(note.pitch, note.velocity);
        if (!soundingPitches.empty())
        {
            auditionStopTimeMs = juce::Time::getMillisecondCounterHiRes() + 230.0;
            startTimer(24);
        }
    }
}

void PianoRollCanvas::paint(juce::Graphics& g)
{
    auto clipBounds = g.getClipBounds();
    int minVisiblePitch = yToPitch((float)clipBounds.getBottom());
    int maxVisiblePitch = yToPitch((float)clipBounds.getY());

    float width = (float)getWidth();

    // 1. Adaptive Mode Background Grid (Slice based on dynamic bar spans AND mode track event boundaries)
    double startBeat = std::max(0.0, xToBeat((float)clipBounds.getX()));
    double endBeat   = xToBeat((float)clipBounds.getRight());

    auto barTimeline = trackModel.getBarTimelineRange(startBeat, endBeat + 16.0);

    // Collect all horizontal slice split beats in visible range
    std::set<double> sliceSplitBeats;
    sliceSplitBeats.insert(0.0);
    for (const auto& bar : barTimeline)
    {
        sliceSplitBeats.insert(bar.startBeat);
        sliceSplitBeats.insert(bar.startBeat + bar.lengthBeats);
    }
    if (auto* mt = trackModel.getModeTrack())
    {
        for (const auto& md : mt->getModes())
        {
            if (md.startBeat + md.lengthBeats >= startBeat && md.startBeat <= endBeat + 16.0)
            {
                sliceSplitBeats.insert(md.startBeat);
                sliceSplitBeats.insert(md.startBeat + md.lengthBeats);
            }
        }
    }

    // Paint background slices
    auto it = sliceSplitBeats.begin();
    while (it != sliceSplitBeats.end())
    {
        double b1 = *it;
        ++it;
        if (it == sliceSplitBeats.end()) break;
        double b2 = *it;
        if (b2 < startBeat) continue;
        if (b1 > endBeat) break;

        float sliceX1 = beatToX(b1);
        float sliceX2 = beatToX(b2);
        float sliceW = sliceX2 - sliceX1;
        if (sliceW <= 0.0f) continue;

        auto layout = midiModel.getEffectiveLayoutAtBeat(0.5 * (b1 + b2), &trackModel);

        for (int p = minVisiblePitch; p <= maxVisiblePitch; ++p)
        {
            float y = pitchToY(p);
            auto info = layout.getInfo(p);

            // The tonic tint is an alpha overlay on the white-key row, never
            // a translucent replacement that exposes the canvas background.
            g.setColour(info.isTonic || info.isInScale
                            ? CubaseTheme::gridWhiteKeyRow()
                            : CubaseTheme::gridBlackKeyRow());
            g.fillRect(sliceX1, y, sliceW, noteHeight);
            if (info.isTonic)
            {
                g.setColour(CubaseTheme::pianoRollTonicTint());
                g.fillRect(sliceX1, y, sliceW, noteHeight);
            }

            // Row divider line
            g.setColour(CubaseTheme::gridBeatLine().withAlpha(0.55f));
            g.drawHorizontalLine((int)(y + noteHeight - 1), sliceX1, sliceX2);
        }
    }

    // 1b. Unallocated Region (Empty regions on active instrument track)
    // Compute exact gaps between MIDI clips based on quantized beat boundaries
    if (auto* activeTrk = trackModel.getTrackById(trackModel.getActiveTrackId()))
    {
        if (activeTrk->getType() == TrackType::Instrument)
        {
            struct Interval { double start; double end; };
            std::vector<Interval> clipIntervals;
            for (const auto& c : activeTrk->getClips())
            {
                if (c.lengthBeats > 0.0)
                    clipIntervals.push_back({ c.startBeat, c.startBeat + c.lengthBeats });
            }
            std::sort(clipIntervals.begin(), clipIntervals.end(), [](const Interval& a, const Interval& b) {
                return a.start < b.start;
            });

            // Merge overlapping intervals
            std::vector<Interval> mergedClips;
            for (const auto& iv : clipIntervals)
            {
                if (mergedClips.empty() || iv.start > mergedClips.back().end)
                    mergedClips.push_back(iv);
                else
                    mergedClips.back().end = std::max(mergedClips.back().end, iv.end);
            }

            // Calculate gaps within visible/song range
            double maxRangeBeat = std::max(endBeat + 32.0, 64.0);
            std::vector<Interval> gaps;
            double curBeat = 0.0;
            for (const auto& mc : mergedClips)
            {
                if (mc.start > curBeat)
                    gaps.push_back({ curBeat, mc.start });
                curBeat = std::max(curBeat, mc.end);
            }
            if (curBeat < maxRangeBeat)
                gaps.push_back({ curBeat, maxRangeBeat });

            // Render each gap strictly clipped within its rectangle
            for (const auto& gap : gaps)
            {
                float gapX1 = beatToX(gap.start);
                float gapX2 = beatToX(gap.end);
                if (gapX2 < (float)clipBounds.getX() || gapX1 > (float)clipBounds.getRight())
                    continue;

                juce::Rectangle<float> gapRect(gapX1, (float)clipBounds.getY(), gapX2 - gapX1, (float)clipBounds.getHeight());

                juce::Graphics::ScopedSaveState sss(g);
                g.reduceClipRegion(gapRect.toNearestInt());

                // Dimmed overlay for empty region
                g.setColour(CubaseTheme::bgDeep().withAlpha(0.55f));
                g.fillRect(gapRect);

                // 45-degree diagonal hatch stripes - strictly clipped inside gapRect!
                g.setColour(CubaseTheme::borderSubtle().withAlpha(0.24f));
                float h = gapRect.getHeight();
                for (float hx = gapRect.getX(); hx < gapRect.getRight() + h; hx += 28.0f)
                {
                    g.drawLine(hx, gapRect.getY(), hx - h, gapRect.getBottom(), 1.0f);
                }
            }
        }
    }

    // Event Range Indicator Overlay in Piano Roll (Always visible, loop-aware, matches Arranger)
    {
        double rangeStart = midiModel.getLoopStart();
        double rangeEnd = midiModel.getLoopEnd();
        bool isLoopActive = midiModel.isLoopEnabled();

        float rX1 = beatToX(rangeStart);
        float rX2 = beatToX(rangeEnd);

        if (rangeEnd > rangeStart)
        {
            float boxLeft = std::max((float)clipBounds.getX(), std::min(rX1, rX2));
            float boxRight = std::min((float)clipBounds.getRight(), std::max(rX1, rX2));
            if (boxRight > boxLeft)
            {
                auto rangeRect = juce::Rectangle<float>(boxLeft, (float)clipBounds.getY(), boxRight - boxLeft, (float)clipBounds.getHeight());
                g.setColour(isLoopActive ? CubaseTheme::locatorRangeOverlay() : juce::Colours::white.withAlpha(0.035f));
                g.fillRect(rangeRect);
            }
        }

        juce::Colour locCol = isLoopActive ? CubaseTheme::locatorHandle() : juce::Colours::white.withAlpha(0.55f);
        if (rX1 >= (float)clipBounds.getX() && rX1 <= (float)clipBounds.getRight())
        {
            g.setColour(locCol);
            g.drawVerticalLine((int)rX1, (float)clipBounds.getY(), (float)clipBounds.getBottom());
        }
        if (rX2 >= (float)clipBounds.getX() && rX2 <= (float)clipBounds.getRight() && std::abs(rX2 - rX1) >= 1.0f)
        {
            g.setColour(locCol);
            g.drawVerticalLine((int)rX2, (float)clipBounds.getY(), (float)clipBounds.getBottom());
        }
    }

    // Adaptive bar skip for PianoRoll (aim for >= 40px between bar lines)
    int prBarSkip = 1;
    if (!barTimeline.empty())
    {
        double refPx = pixelsPerBeat * barTimeline[0].lengthBeats;
        if (refPx < 40.0 && refPx > 0.0)
        {
            prBarSkip = (int)std::ceil(40.0 / refPx);
            int p = 1; while (p < prBarSkip) p *= 2; prBarSkip = p;
        }
    }
    int prLastNum = -1, prLastDen = -1;
    for (const auto& bar : barTimeline)
    {
        bool isPrMC = (bar.numerator != prLastNum || bar.denominator != prLastDen);
        bool isPrSB = ((bar.barIndex - 1) % prBarSkip == 0);
        prLastNum = bar.numerator;
        prLastDen = bar.denominator;

        float barX = beatToX(bar.startBeat);
        if (barX >= (float)clipBounds.getX() && barX <= (float)clipBounds.getRight())
        {
            if (isPrSB || isPrMC)
            {
                g.setColour(isPrMC ? CubaseTheme::gridBarLine().brighter(0.25f) : CubaseTheme::gridBarLine());
                g.drawVerticalLine((int)barX, (float)clipBounds.getY(), (float)clipBounds.getBottom());
            }
        }

        // Sub-lines inside this bar based on active Snap interval
        if (prBarSkip <= 1)
        {
            double snapStep = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 1.0;
            if ((double)pixelsPerBeat * snapStep >= 3.0)
            {
                if (midiModel.isSnapTriplet())
                {
                    double tStep = midiModel.getSnapBeatInterval();
                    for (double tb = bar.startBeat + tStep; tb < bar.startBeat + bar.lengthBeats - 0.001; tb += tStep)
                    {
                        float bx = beatToX(tb);
                        if (bx >= (float)clipBounds.getX() && bx <= (float)clipBounds.getRight())
                        {
                            bool isIntBeat = (std::abs(tb - std::round(tb)) < 0.001);
                            g.setColour(isIntBeat ? CubaseTheme::gridBeatLine().brighter(0.2f)
                                                  : CubaseTheme::gridSubBeat().brighter(0.15f).withAlpha(0.75f));
                            g.drawVerticalLine((int)bx, (float)clipBounds.getY(), (float)clipBounds.getBottom());
                        }
                    }
                }
                else
                {
                    // Non-triplet mode:
                    // If snapStep >= bar.lengthBeats (e.g. 1/1 Bar), this loop does not execute (0 inner lines)!
                    // If snapStep == 2.0 (1/2 Note in 4/4), executes at 2.0 (half-bar line)!
                    // If snapStep == 1.0 (1/4 Note in 4/4), executes at 1.0, 2.0, 3.0!
                    // If snapStep < 1.0 (1/8, 1/16, etc.), executes at each sub-step!
                    if (snapStep < bar.lengthBeats - 0.001)
                    {
                        for (double b = bar.startBeat + snapStep; b < bar.startBeat + bar.lengthBeats - 0.001; b += snapStep)
                        {
                            float bx = beatToX(b);
                            if (bx >= (float)clipBounds.getX() && bx <= (float)clipBounds.getRight())
                            {
                                bool isIntBeat = (std::abs(b - std::round(b)) < 0.001);
                                bool isHalfBar = (std::abs(b - (bar.startBeat + bar.lengthBeats * 0.5)) < 0.001);
                                if (isIntBeat || isHalfBar)
                                    g.setColour(CubaseTheme::gridBeatLine().brighter(0.25f));
                                else
                                    g.setColour(CubaseTheme::gridSubBeat().brighter(0.18f).withAlpha(0.70f));

                                g.drawVerticalLine((int)bx, (float)clipBounds.getY(), (float)clipBounds.getBottom());
                            }
                        }
                    }
                }
            }
        }
    }

    // 2b. Multi-Part MIDI Clip Visualization & Draggable Boundaries (Cubase Model)
    auto coloringMode = midiModel.getColoringMode();
    const auto* canvasViewport = findParentComponentOfClass<juce::Viewport>();
    const float viewTop = canvasViewport != nullptr ? (float)canvasViewport->getViewPositionY()
                                                      : (float)clipBounds.getY();
    const float viewLeft = canvasViewport != nullptr ? (float)canvasViewport->getViewPositionX()
                                                       : (float)clipBounds.getX();
    const float viewRight = canvasViewport != nullptr ? viewLeft + (float)canvasViewport->getViewWidth()
                                                        : (float)clipBounds.getRight();
    if (auto* activeTrk = trackModel.getActiveInstrumentTrack())
    {
        if (activeTrk->getType() == TrackType::Instrument)
        {
            int64_t activeCId = trackModel.getActiveClipId();
            if (activeCId <= 0 && !activeTrk->getClips().empty())
                activeCId = activeTrk->getClips().front().id;

            for (const auto& c : activeTrk->getClips())
            {
                bool isFocused = (c.id == activeCId);
                float clipX1 = beatToX(c.startBeat);
                float clipX2 = beatToX(c.startBeat + c.lengthBeats);
                float clipW = std::max(4.0f, clipX2 - clipX1);

                if (clipX2 < (float)clipBounds.getX() || clipX1 > (float)clipBounds.getRight())
                    continue;

                if (!isFocused)
                {
                    g.setColour(CubaseTheme::bgDeep().withAlpha(0.38f));
                    g.fillRect(clipX1, (float)clipBounds.getY(), clipW,
                               (float)clipBounds.getHeight());
                }

                if (isFocused)
                {
                    // Boundary lines
                    g.setColour(c.color.withAlpha(0.95f));
                    g.drawVerticalLine((int)clipX1, (float)clipBounds.getY(), (float)clipBounds.getBottom());
                    g.drawVerticalLine((int)clipX2, (float)clipBounds.getY(), (float)clipBounds.getBottom());

                    // Boundary resize handles at top and bottom
                    g.fillRect(clipX1 - 2.0f, (float)clipBounds.getY(), 4.0f, 22.0f);
                    g.fillRect(clipX2 - 2.0f, (float)clipBounds.getY(), 4.0f, 22.0f);
                    g.fillRect(clipX1 - 2.0f, (float)clipBounds.getBottom() - 22.0f, 4.0f, 22.0f);
                    g.fillRect(clipX2 - 2.0f, (float)clipBounds.getBottom() - 22.0f, 4.0f, 22.0f);
                }
                else
                {
                    // Inactive clip: dashed boundary lines
                    g.setColour(c.color.withAlpha(0.50f));
                    float dashes[] = { 5.0f, 4.0f };
                    g.drawDashedLine(juce::Line<float>(clipX1, (float)clipBounds.getY(), clipX1, (float)clipBounds.getBottom()), dashes, 2, 1.5f);
                    g.drawDashedLine(juce::Line<float>(clipX2, (float)clipBounds.getY(), clipX2, (float)clipBounds.getBottom()), dashes, 2, 1.5f);

                    // Render Inactive Clip's Notes (dimmed / non-editable, with pitch name)
                    for (const auto& in : c.notes)
                    {
                        float nx = beatToX(in.startBeat);
                        float nw = std::max(4.0f, (float)(in.lengthBeats * pixelsPerBeat));
                        float ny = pitchToY(in.pitch);

                        if (nx + nw < (float)clipBounds.getX() || nx > (float)clipBounds.getRight() ||
                            ny + noteHeight < (float)clipBounds.getY() || ny > (float)clipBounds.getBottom())
                            continue;

                        auto noteRect = juce::Rectangle<float>(nx, ny + 1.0f, nw - 1.0f, noteHeight - 2.0f);
                        juce::Colour noteCol = (coloringMode == ColoringMode::TrackColor) ? activeTrk->getColor() : c.color;
                        g.setColour(noteCol.withAlpha(0.28f));
                        g.fillRoundedRectangle(noteRect, 2.5f);
                        g.setColour(noteCol.withAlpha(0.50f));
                        g.drawRoundedRectangle(noteRect, 2.5f, 1.0f);

                        // Dimmed pitch name inside ghost note
                        if (nw > 18.0f)
                        {
                            g.setColour(noteCol.withAlpha(0.55f));
                            g.setFont(CubaseTheme::getFont(10.0f, true));
                            juce::String pName = ScaleDefinitions::getPitchName(in.pitch) + juce::String((in.pitch / 12) - 1);
                            g.drawText(pName, noteRect.reduced(3.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
                        }
                    }
                }

                const float labelX = std::max(clipX1 + 5.0f, viewLeft + 5.0f);
                const float labelWidth = std::min(148.0f,
                    std::min(clipX2 - 5.0f, viewRight - 5.0f) - labelX);
                if (labelWidth >= 34.0f)
                {
                    const juce::Rectangle<float> badge(labelX, viewTop + 4.0f,
                                                       labelWidth, 18.0f);
                    g.setColour(CubaseTheme::bgSurface().withAlpha(isFocused ? 0.92f : 0.76f));
                    g.fillRoundedRectangle(badge, 3.0f);
                    g.setColour(c.color.withAlpha(isFocused ? 0.95f : 0.48f));
                    g.drawRoundedRectangle(badge.reduced(0.5f), 3.0f, 1.0f);
                    g.setColour(CubaseTheme::textPrimary().withAlpha(isFocused ? 1.0f : 0.65f));
                    g.setFont(CubaseTheme::getFont(10.0f, isFocused));
                    g.drawFittedText(c.name, badge.reduced(5.0f, 0.0f).toNearestInt(),
                                     juce::Justification::centredLeft, 1);
                }
            }
        }
    }

    // 3. Render Ghost Notes from OTHER visible tracks (only if track has selected clips OR is checked in visible list)
    int activeTrkId = trackModel.getActiveTrackId();
    for (const auto& trk : trackModel.getTracks())
    {
        if (trk->getType() != TrackType::Instrument || trk->getId() == activeTrkId || trk->isMuted())
            continue;

        if (!trk->hasSelectedClips() && !trackModel.isPianoRollTrackVisible(trk->getId()))
            continue;

        juce::Colour trkCol = trk->getColor();

        for (const auto& clip : trk->getClips())
        {
            for (const auto& gn : clip.notes)
            {
                float nx = beatToX(gn.startBeat);
                float nw = std::max(4.0f, (float)(gn.lengthBeats * pixelsPerBeat));
                float ny = pitchToY(gn.pitch);

                if (nx + nw >= (float)clipBounds.getX() && nx <= (float)clipBounds.getRight() &&
                    ny + noteHeight >= (float)clipBounds.getY() && ny <= (float)clipBounds.getBottom())
                {
                    auto ghostRect = juce::Rectangle<float>(nx, ny + 1.0f, nw - 1.0f, noteHeight - 2.0f);
                    g.setColour(trkCol.withAlpha(0.24f));
                    g.fillRoundedRectangle(ghostRect, 2.5f);
                    g.setColour(trkCol.withAlpha(0.45f));
                    g.drawRoundedRectangle(ghostRect, 2.5f, 1.0f);

                    // Dimmed pitch name inside ghost note
                    if (nw > 18.0f)
                    {
                        g.setColour(trkCol.withAlpha(0.50f));
                        g.setFont(CubaseTheme::getFont(10.0f, true));
                        juce::String pName = ScaleDefinitions::getPitchName(gn.pitch) + juce::String((gn.pitch / 12) - 1);
                        g.drawText(pName, ghostRect.reduced(3.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
                    }
                }
            }
        }
    }

    // 4. Render Active Track MIDI Notes
    auto* activeInstTrk = trackModel.getActiveInstrumentTrack();
    juce::Colour activeTrackCol = activeInstTrk ? activeInstTrk->getColor() : juce::Colour(0xff2575fc);

    auto* activeClip = trackModel.getActiveClip();
    juce::Colour activeClipCol = activeClip ? activeClip->color : activeTrackCol;

    const auto globalLayout = ScaleDefinitions::computeLayout(
        midiModel.getKeySignature().rootPitchClass, midiModel.getScaleName());
    std::unordered_map<const ModeEvent*, KeyLayout> modeLayouts;
    std::array<juce::String, 128> pitchLabels;
    const bool showNoteLabels = noteHeight >= 11.0f;
    if (showNoteLabels)
        g.setFont(juce::FontOptions(std::clamp(noteHeight - 4.0f, 9.0f, 12.0f)).withStyle("bold"));

    for (const auto& note : midiModel.getNotes())
    {
        float nx = beatToX(note.startBeat);
        float nw = std::max(4.0f, (float)(note.lengthBeats * pixelsPerBeat));
        float ny = pitchToY(note.pitch);

        if (nx + nw < (float)clipBounds.getX() || nx > (float)clipBounds.getRight() ||
            ny + noteHeight < (float)clipBounds.getY() || ny > (float)clipBounds.getBottom())
            continue;

        const KeyLayout* layout = &globalLayout;
        if (const auto* mode = trackModel.findEffectiveModeAtBeat(note.startBeat))
            if (mode->scaleName.isNotEmpty())
            {
                auto [it, inserted] = modeLayouts.try_emplace(mode);
                if (inserted)
                    it->second = ScaleDefinitions::computeLayout(mode->tonicPc,
                                                                 mode->scaleName,
                                                                 mode->tonic);
                layout = &it->second;
            }

        juce::Colour noteFill = ScaleDefinitions::getNoteColor(*layout, note.pitch, coloringMode, note.isSelected, activeTrackCol, activeClipCol);
        juce::Colour noteStroke = ScaleDefinitions::getNoteStrokeColor(*layout, note.pitch, coloringMode, note.isSelected, activeTrackCol, activeClipCol);

        if (note.isMuted)
        {
            noteFill = noteFill.withAlpha(0.35f);
            noteStroke = noteStroke.withAlpha(0.5f);
        }

        auto noteRect = juce::Rectangle<float>(nx, ny + 1.0f, nw - 1.0f, noteHeight - 2.0f);

        // Note Body
        g.setColour(noteFill);
        g.fillRoundedRectangle(noteRect, 3.0f);

        // Keep the musical fill visible. A themed outer glow and a crisp inner
        // edge show selection even when adjacent notes have similar colours.
        if (note.isSelected)
        {
            g.setColour(CubaseTheme::noteGlow().withMultipliedAlpha(note.isMuted ? 0.6f : 0.95f));
            g.drawRoundedRectangle(noteRect.expanded(1.2f), 4.0f, 2.2f);
            g.setColour(noteStroke);
            g.drawRoundedRectangle(noteRect.reduced(0.35f), 2.8f, 1.2f);
        }
        else
        {
            g.setColour(noteStroke);
            g.drawRoundedRectangle(noteRect, 3.0f, 1.0f);
        }

        // Note name label
        if (nw > 22.0f && showNoteLabels)
        {
            juce::String label;
            if (note.pitch >= 0 && note.pitch < (int)pitchLabels.size())
            {
                auto& cached = pitchLabels[(size_t)note.pitch];
                if (cached.isEmpty())
                    cached = ScaleDefinitions::getPitchName(note.pitch)
                        + juce::String((note.pitch / 12) - 1);
                label = cached;
            }
            else
                label = ScaleDefinitions::getPitchName(note.pitch)
                    + juce::String((note.pitch / 12) - 1);
            g.setColour(juce::Colours::white.withAlpha(note.isMuted ? 0.72f : 0.96f));
            g.drawText(label, noteRect.reduced(3.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
        }
    }

    // 4b. Render Alt+Drag Duplicate Ghost Preview Notes
    if (currentDrag == DragMode::AltDuplicate)
    {
        for (const auto& dn : duplicatePreviewNotes)
        {
            float nx = beatToX(dn.startBeat);
            float nw = std::max(4.0f, (float)(dn.lengthBeats * pixelsPerBeat));
            float ny = pitchToY(dn.pitch);

            auto ghostRect = juce::Rectangle<float>(nx, ny + 1.0f, nw - 1.0f, noteHeight - 2.0f);
            g.setColour(CubaseTheme::accentOrange().withAlpha(0.7f));
            g.fillRoundedRectangle(ghostRect, 3.0f);
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(ghostRect, 3.0f, 1.5f);
        }
    }

    // Pending chord placement: draw the whole phrase at the mouse's time
    // position without changing the source notes or the active clip.
    if (isSecondaryDominantPlacementActive())
    {
        const bool valid = placementHasPointer && canPlaceSecondaryDominantAt(placementAnchorBeat);
        const auto previewColour = valid ? CubaseTheme::accentCyan() : juce::Colours::orangered;
        if (placementHasPointer)
        {
            const auto positioned = SecondaryDominant::shiftGeneratedNotes(placementNotes, placementAnchorBeat);
            for (const auto& note : positioned)
            {
                const float x = beatToX(note.startBeat);
                const float y = pitchToY(note.pitch);
                const float w = std::max(4.0f, (float)(note.lengthBeats * pixelsPerBeat));
                if (x + w < (float)clipBounds.getX() || x > (float)clipBounds.getRight()
                    || y + noteHeight < (float)clipBounds.getY() || y > (float)clipBounds.getBottom())
                    continue;
                const juce::Rectangle<float> rect(x, y + 1.0f, w - 1.0f, noteHeight - 2.0f);
                g.setColour(previewColour.withAlpha(0.42f));
                g.fillRoundedRectangle(rect, 3.0f);
                g.setColour(previewColour.withAlpha(0.95f));
                g.drawRoundedRectangle(rect, 3.0f, 1.5f);
            }
            const float anchorX = beatToX(placementAnchorBeat);
            g.setColour(previewColour.withAlpha(0.7f));
            g.drawVerticalLine((int)anchorX, (float)clipBounds.getY(), (float)clipBounds.getBottom());
        }

        auto* viewportParent = findParentComponentOfClass<juce::Viewport>();
        const int left = viewportParent ? viewportParent->getViewPositionX() : clipBounds.getX();
        const int top = viewportParent ? viewportParent->getViewPositionY() : clipBounds.getY();
        const int width = viewportParent ? viewportParent->getViewWidth() : getWidth();
        const double sourceStart = std::min_element(placementNotes.begin(), placementNotes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
                return a.startBeat < b.startBeat;
            })->startBeat;
        const double beatOffset = placementHasPointer ? placementAnchorBeat - sourceStart : 0.0;
        auto groupDescriptions = describePlacedChordGroups(
            placementGroups, beatOffset, midiModel, trackModel);
        if (placementUndoName == "Place Quick Chord" && placementDescription.isNotEmpty())
        {
            groupDescriptions.clear();
            groupDescriptions.add(placementDescription);
        }
        juce::StringArray shownDescriptions;
        const int visibleGroups = std::min(4, groupDescriptions.size());
        for (int i = 0; i < visibleGroups; ++i)
            shownDescriptions.add(groupDescriptions[i]);
        if (groupDescriptions.size() > visibleGroups)
            shownDescriptions.add(juce::String::fromUTF8(u8"另有 ")
                + juce::String(groupDescriptions.size() - visibleGroups)
                + juce::String::fromUTF8(u8" 组，已逐组识别"));
        if (shownDescriptions.isEmpty() && placementDescription.isNotEmpty())
            shownDescriptions.add(placementDescription);
        const bool hasDescription = !shownDescriptions.isEmpty();
        const int descriptionHeight = 22 * shownDescriptions.size();
        const juce::Rectangle<int> hint(left + 12, top + 8,
            std::max(200, std::min(600, width - 24)),
            hasDescription ? descriptionHeight + 30 : 30);
        g.setColour(CubaseTheme::bgDeep().withAlpha(0.9f));
        g.fillRoundedRectangle(hint.toFloat(), 5.0f);
        g.setColour(valid || !placementHasPointer ? CubaseTheme::accentCyan() : juce::Colours::orangered);
        g.setFont(CubaseTheme::getFont(12.0f, true));
        if (hasDescription)
            g.drawFittedText(shownDescriptions.joinIntoString("\n"),
                hint.reduced(8, 0).removeFromTop(descriptionHeight),
                juce::Justification::centredLeft, shownDescriptions.size());
        const bool borrowed = placementUndoName == "Place Borrowed Chord";
        const bool quick = placementUndoName == "Place Quick Chord";
        const auto chordLabel = quick ? juce::String::fromUTF8(u8"快速和弦")
            : borrowed ? juce::String::fromUTF8(u8"借用和弦")
                       : juce::String::fromUTF8(u8"副属和弦");
        g.drawText(!placementHasPointer
                       ? juce::String::fromUTF8(u8"移动鼠标预览") + chordLabel
                           + juce::String::fromUTF8(u8" · 左键放置 · 右键/Esc 取消")
                       : valid
                           ? juce::String::fromUTF8(u8"左键放置") + chordLabel
                               + juce::String::fromUTF8(u8" · 右键/Esc 取消")
                           : juce::String::fromUTF8(u8"超出当前 MIDI 片段，移动到有效时间位置"),
                   hint.reduced(8, 0).withTop(hint.getY() + (hasDescription ? descriptionHeight : 0)),
                   juce::Justification::centredLeft, true);
    }

    // 5. Marquee Selection Box
    if (currentDrag == DragMode::MarqueeSelect && !marqueeRect.isEmpty())
    {
        g.setColour(CubaseTheme::accentBlue().withAlpha(0.2f));
        g.fillRect(marqueeRect);
        g.setColour(CubaseTheme::accentBlue());
        g.drawRect(marqueeRect, 1);
    }

    // 6. Playhead Line
    float playheadX = beatToX(audioEngine.getPlayheadBeat());
    if (playheadX >= (float)clipBounds.getX() && playheadX <= (float)clipBounds.getRight())
    {
        g.setColour(CubaseTheme::accentOrange());
        g.drawVerticalLine((int)playheadX, (float)clipBounds.getY(), (float)clipBounds.getBottom());
    }
}

//==============================================================================
// PianoRollComponent Container
//==============================================================================
PianoRollComponent::PianoRollComponent(MidiModel& model, TrackModel& tm, AudioEngine& engine, juce::UndoManager& um)
    : midiModel(model), trackModel(tm), audioEngine(engine), undoManager(um),
      canvas(model, tm, engine, um)
{
    ruler = new TimelineRuler(midiModel, audioEngine, &trackModel);
    ruler->onEdgeScroll = [this](int deltaPx) {
        int curX = viewport.getViewPositionX();
        int newX = std::max(0, curX + deltaPx);
        viewport.setViewPosition(newX, viewport.getViewPositionY());
        syncViews();
    };
    keyboard = new PianoKeyboardComponent(audioEngine, midiModel);
    velocityLane = new VelocityLaneComponent(midiModel, undoManager, &trackModel);
    overviewLane = std::make_unique<GlobalTrackOverviewLane>(trackModel, midiModel, undoManager, &audioEngine);
    overviewLane->onFocusGained = [this]() {
        if (onFocusGained)
            onFocusGained();
    };

    canvas.onClearOverviewSelection = [this]() {
        if (overviewLane != nullptr)
            overviewLane->clearSelection();
    };

    addAndMakeVisible(ruler);
    addAndMakeVisible(overviewLane.get());
    addAndMakeVisible(velocityLane);

    keyboardViewport.setViewedComponent(keyboard, false);
    keyboardViewport.setScrollBarsShown(false, false);
    addAndMakeVisible(keyboardViewport);

    viewport.setViewedComponent(&canvas, false);
    viewport.setScrollBarsShown(true, true);
    addAndMakeVisible(viewport);

    viewport.onScrollPositionChanged = [this](int vx, int vy) {
        keyboardViewport.setViewPosition(0, vy);
        if (canvas.getPixelsPerBeat() > 0.0
            && ((double)vx + viewport.getViewWidth()) / canvas.getPixelsPerBeat()
                + 16.0 >= canvas.getTimelineExtentBeats())
            updateHorizontalExtent(((double)vx + viewport.getViewWidth())
                / canvas.getPixelsPerBeat());
        syncViews();
    };

    overviewLane->onSeekPlayhead = [this](double beat) {
        audioEngine.setPlayheadBeat(beat);
    };

    btnTrackSelect.setButtonText(juce::String::fromUTF8(u8"附属轨 ▾"));
    btnTrackSelect.setColour(juce::TextButton::buttonColourId, CubaseTheme::buttonBackground());
    btnTrackSelect.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());
    btnTrackSelect.onClick = [this]() {
        juce::PopupMenu menu;
        bool cVis = overviewLane ? overviewLane->isChordLaneVisible() : true;
        bool mVis = overviewLane ? overviewLane->isModeLaneVisible() : true;
        bool mkVis = overviewLane ? overviewLane->isMarkerLaneVisible() : false;
        bool tsVis = overviewLane ? overviewLane->isTimeSigLaneVisible() : true;

        menu.addItem(1, juce::String::fromUTF8(u8"和弦轨 (Chord Track)"), true, cVis);
        menu.addItem(2, juce::String::fromUTF8(u8"调式轨 (Mode Track)"), true, mVis);
        menu.addItem(3, juce::String::fromUTF8(u8"标记轨 (Marker Track)"), true, mkVis);
        menu.addItem(4, juce::String::fromUTF8(u8"拍号轨 (Time Signature Track)"), true, tsVis);

        juce::Component::SafePointer<PianoRollComponent> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&btnTrackSelect), [this, safeThis, cVis, mVis, mkVis, tsVis](int result) {
            if (safeThis.getComponent() == nullptr) return;
            if (!overviewLane) return;
            bool nc = cVis, nm = mVis, nmk = mkVis, nts = tsVis;
            if (result == 1) nc = !nc;
            else if (result == 2) nm = !nm;
            else if (result == 3) nmk = !nmk;
            else if (result == 4) nts = !nts;
            overviewLane->setLaneVisibility(nc, nm, nmk, nts);
        });
    };
    addAndMakeVisible(btnTrackSelect);

    overviewSplitter = std::make_unique<SplitterBar>([this](int deltaY) {
        overviewHeight = std::clamp(overviewHeight + deltaY, 24, 160);
        resized();
    });
    addAndMakeVisible(overviewSplitter.get());

    velocitySplitter = std::make_unique<SplitterBar>([this](int deltaY) {
        velocityHeight = std::clamp(velocityHeight - deltaY, 35, 240);
        resized();
    });
    addAndMakeVisible(velocitySplitter.get());

    canvas.onZoomChanged = [this]() { updateHorizontalExtent(); syncViews(); };
    canvas.onScrollRequested = [this](int deltaX) {
        int newX = std::max(0, viewport.getViewPositionX() + deltaX);
        viewport.setViewPosition(newX, viewport.getViewPositionY());
        syncViews();
    };
    canvas.onPanRequested = [this](int deltaX, int deltaY) {
        int newX = std::max(0, viewport.getViewPositionX() - deltaX);
        int newY = std::max(0, viewport.getViewPositionY() - deltaY);
        viewport.setViewPosition(newX, newY);
        keyboardViewport.setViewPosition(0, newY);
        syncViews();
    };

    canvas.onFocusRequested = [this]() {
        if (onFocusGained)
            onFocusGained();
    };

    keyboard->onScrollRequested = [this](float deltaY) {
        int deltaPixels = (int)(-deltaY * 100.0f);
        int newY = std::max(0, viewport.getViewPositionY() + deltaPixels);
        viewport.setViewPosition(viewport.getViewPositionX(), newY);
        keyboardViewport.setViewPosition(0, newY);
        syncViews();
    };

    addMouseListener(this, true);
    audioEngine.addPlayheadListener(this);
    midiModel.addListener(this);
    trackModel.addListener(this);

    updateHorizontalExtent();
    syncViews();
    scrollToPitch(60);
}

PianoRollComponent::~PianoRollComponent()
{
    trackModel.removeListener(this);
    midiModel.removeListener(this);
    audioEngine.removePlayheadListener(this);
    delete ruler;
    delete keyboard;
    delete velocityLane;
}

void PianoRollComponent::scrollToPitch(int pitch)
{
    int targetY = (int)canvas.pitchToY(pitch) - viewport.getHeight() / 2;
    viewport.setViewPosition(viewport.getViewPositionX(), std::max(0, targetY));
    keyboardViewport.setViewPosition(0, viewport.getViewPositionY());
}

void PianoRollComponent::scrollToBeat(double beat)
{
    if (!std::isfinite(beat)) return;
    beat = std::max(0.0, beat);
    updateHorizontalExtent(beat + (double)viewport.getViewWidth()
        / canvas.getPixelsPerBeat());
    const int targetX = (int)std::clamp(beat * canvas.getPixelsPerBeat()
        - (double)viewport.getWidth() / 4.0, 0.0, 1000000000.0);
    viewport.setViewPosition(std::max(0, targetX), viewport.getViewPositionY());
}

void PianoRollComponent::playheadMoved(double beat)
{
    if (std::isfinite(beat) && beat + 64.0 >= canvas.getTimelineExtentBeats())
        updateHorizontalExtent(beat);
    auto mode = midiModel.getPlayheadFollowMode();
    if (mode == PlayheadFollowMode::Off)
        return;

    if (!audioEngine.isPlaying())
        return;

    double ppb = canvas.getPixelsPerBeat();
    if (ppb <= 0.0) return;

    int curViewX = viewport.getViewPositionX();
    int vpWidth = viewport.getViewWidth();
    if (vpWidth <= 0) return;

    double startBeat = (double)curViewX / ppb;
    double visibleBeats = (double)vpWidth / ppb;
    double endBeat = startBeat + visibleBeats;

    if (mode == PlayheadFollowMode::Page)
    {
        if (beat >= endBeat - 0.25 || beat < startBeat)
        {
            double newStartBeat = std::max(0.0, beat - 0.5);
            int newViewX = (int)std::round(newStartBeat * ppb);
            viewport.setViewPosition(newViewX, viewport.getViewPositionY());
            syncViews();
        }
    }
    else if (mode == PlayheadFollowMode::ContinuousLeft)
    {
        double targetStartBeat = std::max(0.0, beat - 1.0);
        int targetViewX = (int)std::round(targetStartBeat * ppb);
        if (std::abs(targetViewX - curViewX) >= 2)
        {
            viewport.setViewPosition(targetViewX, viewport.getViewPositionY());
            syncViews();
        }
    }
    else if (mode == PlayheadFollowMode::ContinuousCenter)
    {
        double targetStartBeat = std::max(0.0, beat - (visibleBeats / 2.0));
        int targetViewX = (int)std::round(targetStartBeat * ppb);
        if (std::abs(targetViewX - curViewX) >= 2)
        {
            viewport.setViewPosition(targetViewX, viewport.getViewPositionY());
            syncViews();
        }
    }
}

void PianoRollComponent::projectWillReplace()
{
    const juce::ScopedValueSetter<bool> guard(updatingHorizontalExtent, true);
    viewport.setViewPosition(0, viewport.getViewPositionY());
    canvas.setTimelineExtentBeats(1024.0);
}

void PianoRollComponent::updateHorizontalExtent(double requiredBeat)
{
    if (updatingHorizontalExtent)
        return;
    const juce::ScopedValueSetter<bool> guard(updatingHorizontalExtent, true);

    double lastBeat = 0.0;
    auto include = [&lastBeat](double startBeat, double lengthBeats = 0.0) {
        const double endBeat = startBeat + lengthBeats;
        if (std::isfinite(endBeat) && endBeat >= 0.0)
            lastBeat = std::max(lastBeat, endBeat);
    };
    include(requiredBeat);
    include(midiModel.getLoopEnd());
    include(audioEngine.getPlayheadBeat());
    for (const auto& note : midiModel.getNotes())
        include(note.startBeat, note.lengthBeats);

    for (const auto& track : trackModel.getTracks())
    {
        for (const auto& clip : track->getClips())
        {
            include(clip.startBeat, clip.lengthBeats);
            for (const auto& note : clip.notes)
                include(note.startBeat, note.lengthBeats);
        }
        for (const auto& clip : track->getAudioClips())
            include(clip.startBeat, clip.lengthBeats);
        for (const auto& chord : track->getChords())
            include(chord.startBeat, chord.lengthBeats);
        for (const auto& mode : track->getModes())
            include(mode.startBeat, mode.lengthBeats);
        for (const auto& mode : track->getAuxModes())
            include(mode.startBeat, mode.lengthBeats);
        for (const auto& marker : track->getMarkers())
            include(marker.startBeat, marker.lengthBeats);
        for (const auto& signature : track->getTimeSignatures())
            include(signature.startBeat, signature.lengthBeats);
        for (const auto& tempo : track->getTempoEvents())
            include(tempo.startBeat);
    }

    const double viewEndBeat = ((double)viewport.getViewPositionX()
        + viewport.getViewWidth()) / canvas.getPixelsPerBeat();
    include(viewEndBeat);
    const double wantedEnd = std::max(1024.0, lastBeat + 256.0);
    if (wantedEnd > canvas.getTimelineExtentBeats() + 0.5)
        canvas.setTimelineExtentBeats(wantedEnd);
}

void PianoRollComponent::syncViews()
{
    double ppb = canvas.getPixelsPerBeat();
    double offset = (double)viewport.getViewPositionX() / ppb;

    keyboard->setNoteHeight(canvas.getNoteHeight());
    keyboard->setSize(64, (int)(128 * canvas.getNoteHeight()));
    keyboardViewport.setViewPosition(0, viewport.getViewPositionY());

    ruler->setZoomAndScroll(ppb, offset);
    overviewLane->setPixelsPerBeat((float)ppb);
    overviewLane->setScrollOffsetBeats(offset);
    velocityLane->setZoomAndScroll(ppb, offset);
}

void PianoRollComponent::resized()
{
    auto area = getLocalBounds();

    int keyboardWidth = 64;
    int rulerHeight = 24;
    int splitterThickness = 4;

    // Top: Timeline Ruler
    auto rulerRow = area.removeFromTop(rulerHeight);
    auto btnArea = rulerRow.removeFromLeft(keyboardWidth);
    btnTrackSelect.setBounds(btnArea.reduced(2, 1));
    ruler->setBounds(rulerRow);

    // Below Ruler: Global Track Overview Lane (Chords & Markers/Modes)
    auto overviewRow = area.removeFromTop(overviewHeight);
    overviewLane->setBounds(overviewRow);

    // Draggable Splitter between Overview Lane and Piano Roll canvas
    overviewSplitter->setBounds(area.removeFromTop(splitterThickness));

    // Bottom: Velocity Lane
    auto velocityRow = area.removeFromBottom(velocityHeight);
    velocityRow.removeFromLeft(keyboardWidth);
    velocityLane->setBounds(velocityRow);

    // Draggable Splitter above Velocity Lane
    auto velSplitterRow = area.removeFromBottom(splitterThickness);
    velSplitterRow.removeFromLeft(keyboardWidth);
    velocitySplitter->setBounds(velSplitterRow);

    // Left: Keyboard Viewport (Synchronized vertically with viewport)
    keyboardViewport.setBounds(area.removeFromLeft(keyboardWidth));

    // Center: Piano Roll Viewport
    viewport.setBounds(area);

    updateHorizontalExtent();
    syncViews();
}

void PianoRollComponent::mouseDown(const juce::MouseEvent&)
{
    if (onFocusGained)
        onFocusGained();
}

void PianoRollComponent::paintOverChildren(juce::Graphics& g)
{
    if (isFocusedArea)
    {
        g.setColour(CubaseTheme::accentCyan().withAlpha(0.9f));
        g.drawRect(getLocalBounds(), 2);
    }
}

} // namespace Harmonizer
