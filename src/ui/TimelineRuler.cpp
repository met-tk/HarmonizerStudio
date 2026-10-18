#include "TimelineRuler.h"
#include "CubaseTheme.h"

namespace Harmonizer
{

TimelineRuler::TimelineRuler(MidiModel& model, AudioEngine& engine, TrackModel* tm)
    : midiModel(model), audioEngine(engine), trackModel(tm)
{
    midiModel.addListener(this);
    audioEngine.addPlayheadListener(this);
    if (trackModel)
        trackModel->addListener(this);
}

TimelineRuler::~TimelineRuler()
{
    stopTimer();
    if (trackModel)
        trackModel->removeListener(this);
    audioEngine.removePlayheadListener(this);
    midiModel.removeListener(this);
}

void TimelineRuler::setZoomAndScroll(double ppb, double sob)
{
    pixelsPerBeat = std::max(2.0, ppb);
    scrollOffsetBeats = std::max(0.0, sob);
    repaint();
}

double TimelineRuler::xToBeat(float x) const
{
    double safePpb = std::max(2.0, pixelsPerBeat);
    return scrollOffsetBeats + (x / safePpb);
}

float TimelineRuler::beatToX(double beat) const
{
    return (float)((beat - scrollOffsetBeats) * pixelsPerBeat);
}

void TimelineRuler::playheadMoved(double)
{
    repaint();
}

void TimelineRuler::mouseMove(const juce::MouseEvent& e)
{
    float loopX1 = beatToX(midiModel.getLoopStart());
    float loopX2 = beatToX(midiModel.getLoopEnd());

    if (std::abs(e.x - loopX1) <= 6.0f || std::abs(e.x - loopX2) <= 6.0f)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void TimelineRuler::mouseDown(const juce::MouseEvent& e)
{
    double clickedBeat = std::max(0.0, xToBeat((float)e.x));
    double qBeat = midiModel.isSnapEnabled() ? midiModel.quantizeBeat(clickedBeat) : clickedBeat;
    float loopX1 = beatToX(midiModel.getLoopStart());
    float loopX2 = beatToX(midiModel.getLoopEnd());

    // 1. Hover/Drag existing locator handles
    if (std::abs(e.x - loopX1) <= 6.0f && std::abs(e.x - loopX2) <= 6.0f)
    {
        stationaryLocatorBeat = midiModel.getLoopStart();
        currentDrag = DragTarget::LeftLocator;
    }
    else if (std::abs(e.x - loopX1) <= 6.0f)
    {
        currentDrag = DragTarget::LeftLocator;
        stationaryLocatorBeat = midiModel.getLoopEnd();
    }
    else if (std::abs(e.x - loopX2) <= 6.0f)
    {
        currentDrag = DragTarget::RightLocator;
        stationaryLocatorBeat = midiModel.getLoopStart();
    }
    // 2. Cubase Shortcut: Ctrl + Left Click sets Left locator [L
    else if (e.mods.isCtrlDown())
    {
        stationaryLocatorBeat = midiModel.getLoopEnd();
        double newStart = std::min(qBeat, stationaryLocatorBeat);
        double newEnd = std::max(qBeat, stationaryLocatorBeat);
        midiModel.setLoopRange(newStart, newEnd);
        currentDrag = DragTarget::LeftLocator;
    }
    // 3. Cubase Shortcut: Alt + Left Click sets Right locator R]
    else if (e.mods.isAltDown())
    {
        stationaryLocatorBeat = midiModel.getLoopStart();
        double newStart = std::min(stationaryLocatorBeat, qBeat);
        double newEnd = std::max(stationaryLocatorBeat, qBeat);
        midiModel.setLoopRange(newStart, newEnd);
        currentDrag = DragTarget::RightLocator;
    }
    // 4. Regular Click: Seek Playhead
    else
    {
        currentDrag = DragTarget::Playhead;
        wasPlayingBeforeDrag = audioEngine.isPlaying();
        if (wasPlayingBeforeDrag)
            audioEngine.pause();
        audioEngine.setPlayheadBeat(qBeat);
        lastDragMousePos = e.getPosition();
    }
}

void TimelineRuler::mouseDrag(const juce::MouseEvent& e)
{
    double currentBeat = std::max(0.0, xToBeat((float)e.x));
    double qBeat = midiModel.isSnapEnabled() ? midiModel.quantizeBeat(currentBeat) : currentBeat;

    if (currentDrag == DragTarget::Playhead)
    {
        lastDragMousePos = e.getPosition();
        audioEngine.setPlayheadBeat(qBeat);

        const int edgeMargin = 36;
        bool inEdgeZone = (e.x < edgeMargin || e.x > getWidth() - edgeMargin);
        if (inEdgeZone)
        {
            if (!isTimerRunning())
                startTimerHz(60);
        }
        else
        {
            if (isTimerRunning())
                stopTimer();
        }
    }
    else if (currentDrag == DragTarget::LeftLocator || currentDrag == DragTarget::RightLocator)
    {
        double newStart = std::max(0.0, std::min(qBeat, stationaryLocatorBeat));
        double newEnd = std::max(0.0, std::max(qBeat, stationaryLocatorBeat));
        midiModel.setLoopRange(newStart, newEnd);
    }
    repaint();
}

void TimelineRuler::mouseUp(const juce::MouseEvent&)
{
    if (currentDrag == DragTarget::Playhead)
    {
        stopTimer();
        if (wasPlayingBeforeDrag)
        {
            wasPlayingBeforeDrag = false;
            audioEngine.play();
        }
    }
    currentDrag = DragTarget::None;
}

void TimelineRuler::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setColour(CubaseTheme::bgPanel());
    g.fillRect(bounds);

    g.setColour(CubaseTheme::borderSubtle());
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, (float)bounds.getWidth());

    double startBeat = scrollOffsetBeats;
    double endBeat = xToBeat((float)bounds.getWidth());

    // Draw Loop / Event Range & Locators (Always resident / 常驻显示)
    {
        float loopX1 = beatToX(midiModel.getLoopStart());
        float loopX2 = beatToX(midiModel.getLoopEnd());
        bool loopOn = midiModel.isLoopEnabled();

        juce::Colour rangeAccent = loopOn ? CubaseTheme::locatorHandle() : CubaseTheme::textSecondary().withAlpha(0.65f);
        juce::Colour rangeTint = loopOn ? CubaseTheme::locatorRangeOverlay() : CubaseTheme::bgSurface().withAlpha(0.35f);

        if (loopX2 >= loopX1)
        {
            // Range highlight
            if (loopX2 > loopX1 + 1.0f)
            {
                g.setColour(rangeTint);
                g.fillRect(loopX1, 0.0f, loopX2 - loopX1, (float)bounds.getHeight());

                // Top cycle bar
                g.setColour(rangeAccent);
                g.fillRect(loopX1, 0.0f, loopX2 - loopX1, loopOn ? 3.0f : 1.5f);

                // Left Locator Badge: [L
                g.setColour(rangeAccent);
                g.fillRect(loopX1 - 2.0f, 0.0f, 4.0f, (float)bounds.getHeight());
                g.fillRect(loopX1 - 1.0f, 0.0f, 16.0f, 13.0f);
                g.setColour(loopOn ? CubaseTheme::locatorHandle().contrasting(0.95f) : juce::Colours::white);
                g.setFont(CubaseTheme::getFont(9.5f, true));
                g.drawText("[L", (int)loopX1, 0, 15, 13, juce::Justification::centred);

                // Right Locator Badge: R]
                g.setColour(rangeAccent);
                g.fillRect(loopX2 - 2.0f, 0.0f, 4.0f, (float)bounds.getHeight());
                g.fillRect(loopX2 - 15.0f, 0.0f, 16.0f, 13.0f);
                g.setColour(loopOn ? CubaseTheme::locatorHandle().contrasting(0.95f) : juce::Colours::white);
                g.setFont(CubaseTheme::getFont(9.5f, true));
                g.drawText("R]", (int)loopX2 - 15, 0, 15, 13, juce::Justification::centred);
            }
            else
            {
                // Coinciding locators (zero-length range)
                g.setColour(rangeAccent);
                g.fillRect(loopX1 - 1.5f, 0.0f, 3.0f, (float)bounds.getHeight());

                // [L badge extending to the left
                g.fillRect(loopX1 - 16.0f, 0.0f, 16.0f, 13.0f);
                // R] badge extending to the right
                g.fillRect(loopX1, 0.0f, 16.0f, 13.0f);

                g.setColour(loopOn ? CubaseTheme::locatorHandle().contrasting(0.95f) : juce::Colours::white);
                g.setFont(CubaseTheme::getFont(9.5f, true));
                g.drawText("[L", (int)loopX1 - 16, 0, 16, 13, juce::Justification::centred);
                g.drawText("R]", (int)loopX1, 0, 16, 13, juce::Justification::centred);
            }
        }
    }

    // Draw bars & ticks (dynamic if trackModel present)
    g.setFont(CubaseTheme::getFont(11.0f, true));

    if (trackModel)
    {
        auto barTimeline = trackModel->getBarTimelineRange(startBeat, endBeat + 16.0);
        // Adaptive bar skip: aim for >= 40px between labels
        int trBarSkip = 1;
        if (!barTimeline.empty())
        {
            double refPx = pixelsPerBeat * barTimeline[0].lengthBeats;
            if (refPx < 40.0 && refPx > 0.0)
            {
                trBarSkip = (int)std::ceil(40.0 / refPx);
                int p = 1; while (p < trBarSkip) p *= 2; trBarSkip = p;
            }
        }
        int trLastNum = -1, trLastDen = -1;
        for (const auto& bar : barTimeline)
        {
            bool isMC  = (bar.numerator != trLastNum || bar.denominator != trLastDen);
            bool isSB  = ((bar.barIndex - 1) % trBarSkip == 0);
            trLastNum = bar.numerator;
            trLastDen = bar.denominator;
            if (!isSB && !isMC) continue;

            float barX = beatToX(bar.startBeat);
            if (barX >= -20 && barX <= bounds.getWidth() + 20)
            {
                // Bar line
                g.setColour(isMC ? CubaseTheme::gridBarLine().brighter(0.4f)
                                 : CubaseTheme::gridBarLine().brighter(0.2f));
                g.drawVerticalLine((int)barX, (float)bounds.getHeight() - 14.0f, (float)bounds.getHeight());

                // Bar label (1, 2, 3...)
                g.setColour(CubaseTheme::textPrimary());
                int offset = trackModel ? trackModel->getBarNumberOffset() : 0;
                g.drawText(juce::String(bar.barIndex + offset), (int)barX + 4, 2, 48, 14, juce::Justification::left);

                // Sub-beats only when zoomed in
                if (trBarSkip <= 1 && pixelsPerBeat >= 12.0)
                {
                    double snapStep = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 1.0;
                    if (midiModel.isSnapTriplet())
                    {
                        double tStep = midiModel.getSnapBeatInterval();
                        if (pixelsPerBeat * tStep >= 4.0)
                        {
                            for (double tb = bar.startBeat + tStep; tb < bar.startBeat + bar.lengthBeats - 0.001; tb += tStep)
                            {
                                float sbX = beatToX(tb);
                                if (sbX >= 0 && sbX <= bounds.getWidth())
                                {
                                    bool isIntBeat = (std::abs(tb - std::round(tb)) < 0.001);
                                    g.setColour(CubaseTheme::borderSubtle().withAlpha(isIntBeat ? 0.7f : 0.4f));
                                    g.drawVerticalLine((int)sbX, (float)bounds.getHeight() - (isIntBeat ? 7.0f : 4.0f), (float)bounds.getHeight());
                                }
                            }
                        }
                    }
                    else
                    {
                        if (snapStep < bar.lengthBeats - 0.001)
                        {
                            for (double b = bar.startBeat + snapStep; b < bar.startBeat + bar.lengthBeats - 0.001; b += snapStep)
                            {
                                float sbX = beatToX(b);
                                if (sbX >= 0 && sbX <= bounds.getWidth())
                                {
                                    bool isIntBeat = (std::abs(b - std::round(b)) < 0.001);
                                    g.setColour(CubaseTheme::borderSubtle().withAlpha(isIntBeat ? 0.7f : 0.4f));
                                    g.drawVerticalLine((int)sbX, (float)bounds.getHeight() - (isIntBeat ? 7.0f : 4.0f), (float)bounds.getHeight());
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    else
    {
        int beatsPerBar = midiModel.getTimeSigNumerator();
        int startBar = (int)(startBeat / beatsPerBar);
        int endBar = (int)(endBeat / beatsPerBar) + 1;

        double snapStep = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 1.0;

        for (int b = startBar; b <= endBar; ++b)
        {
            double barBeat = b * beatsPerBar;
            float barX = beatToX(barBeat);

            g.setColour(CubaseTheme::gridBarLine().brighter(0.2f));
            g.drawVerticalLine((int)barX, (float)bounds.getHeight() - 14.0f, (float)bounds.getHeight());

            g.setColour(CubaseTheme::textPrimary());
            g.drawText(juce::String(b + 1), (int)barX + 4, 2, 40, 14, juce::Justification::left);

            if (snapStep < beatsPerBar - 0.001)
            {
                for (double sb = snapStep; sb < beatsPerBar - 0.001; sb += snapStep)
                {
                    float sbX = beatToX(barBeat + sb);
                    if (sbX >= 0 && sbX <= bounds.getWidth())
                    {
                        bool isIntBeat = (std::abs(sb - std::round(sb)) < 0.001);
                        g.setColour(CubaseTheme::borderSubtle().withAlpha(isIntBeat ? 0.7f : 0.4f));
                        g.drawVerticalLine((int)sbX, (float)bounds.getHeight() - (isIntBeat ? 7.0f : 4.0f), (float)bounds.getHeight());
                    }
                }
            }
        }
    }

    // Draw Playhead
    float playheadX = beatToX(audioEngine.getPlayheadBeat());
    if (playheadX >= -10 && playheadX <= bounds.getWidth() + 10)
    {
        juce::Path head;
        head.startNewSubPath(playheadX - 6.0f, 0.0f);
        head.lineTo(playheadX + 6.0f, 0.0f);
        head.lineTo(playheadX + 6.0f, 10.0f);
        head.lineTo(playheadX, 16.0f);
        head.lineTo(playheadX - 6.0f, 10.0f);
        head.closeSubPath();

        g.setColour(juce::Colour(0xffe63946)); // Cubase classic red playhead
        g.fillPath(head);
        g.drawVerticalLine((int)playheadX, 16.0f, (float)bounds.getHeight());
    }
}

void TimelineRuler::timerCallback()
{
    if (currentDrag != DragTarget::Playhead)
    {
        stopTimer();
        return;
    }

    const int edgeMargin = 36;
    int deltaPx = 0;
    if (lastDragMousePos.x < edgeMargin)
    {
        float over = (float)(edgeMargin - lastDragMousePos.x);
        deltaPx = -(int)std::clamp(over * 0.25f + 4.0f, 3.0f, 30.0f);
    }
    else if (lastDragMousePos.x > getWidth() - edgeMargin)
    {
        float over = (float)(lastDragMousePos.x - (getWidth() - edgeMargin));
        deltaPx = (int)std::clamp(over * 0.25f + 4.0f, 3.0f, 30.0f);
    }
    else
    {
        stopTimer();
        return;
    }

    if (onEdgeScroll && deltaPx != 0)
    {
        onEdgeScroll(deltaPx);

        double curBeat = std::max(0.0, xToBeat((float)lastDragMousePos.x));
        double qBeat = midiModel.isSnapEnabled() ? midiModel.quantizeBeat(curBeat) : curBeat;
        audioEngine.setPlayheadBeat(qBeat);
        repaint();
    }
}

} // namespace Harmonizer
