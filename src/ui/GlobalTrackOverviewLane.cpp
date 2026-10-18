#include "GlobalTrackOverviewLane.h"
#include "CubaseTheme.h"
#include "EventEditors.h"
#include "../core/Localization.h"
#include "../core/UndoActions.h"
#include "../core/KeyCommandManager.h"
#include "../audio/AudioEngine.h"

namespace Harmonizer
{

GlobalTrackOverviewLane::GlobalTrackOverviewLane(TrackModel& tm, MidiModel& mm, juce::UndoManager& um, AudioEngine* ae)
    : trackModel(tm), midiModel(mm), undoManager(um), audioEngine(ae)
{
    trackModel.addListener(this);
    setWantsKeyboardFocus(true);
}

GlobalTrackOverviewLane::~GlobalTrackOverviewLane()
{
    trackModel.removeListener(this);
}

void GlobalTrackOverviewLane::resized()
{
}

void GlobalTrackOverviewLane::setPixelsPerBeat(float ppb)
{
    pixelsPerBeat = std::max(2.0f, ppb);
    repaint();
}

void GlobalTrackOverviewLane::setScrollOffsetBeats(double beats)
{
    scrollOffsetBeats = std::max(0.0, beats);
    repaint();
}

float GlobalTrackOverviewLane::beatToX(double beat) const
{
    return (float)headerWidth + (float)((beat - scrollOffsetBeats) * pixelsPerBeat);
}

double GlobalTrackOverviewLane::xToBeat(float x) const
{
    double safePpb = std::max(2.0, (double)pixelsPerBeat);
    return (((double)x - (double)headerWidth) / safePpb) + scrollOffsetBeats;
}

GlobalTrackOverviewLane::HitTestResult GlobalTrackOverviewLane::hitTestEvent(juce::Point<int> pos) const
{
    HitTestResult res;
    if (pos.x < headerWidth) return res;

    struct ActiveLane
    {
        TrackType type;
        Track* track = nullptr;
    };
    std::vector<ActiveLane> activeLanes;
    if (showChordLane && trackModel.getChordTrack())
        activeLanes.push_back({ TrackType::Chord, trackModel.getChordTrack() });
    if (showModeLane && trackModel.getModeTrack())
        activeLanes.push_back({ TrackType::Mode, trackModel.getModeTrack() });
    if (showMarkerLane && trackModel.getMarkerTrack())
        activeLanes.push_back({ TrackType::Marker, trackModel.getMarkerTrack() });
    if (showTimeSigLane && trackModel.getTimeSignatureTrack())
        activeLanes.push_back({ TrackType::TimeSignature, trackModel.getTimeSignatureTrack() });

    if (activeLanes.empty()) return res;

    float rowH = (float)getHeight() / (float)activeLanes.size();
    int rowIdx = std::clamp((int)(pos.y / rowH), 0, (int)activeLanes.size() - 1);
    float rowY = (float)rowIdx * rowH;
    auto& lane = activeLanes[(size_t)rowIdx];

    if (lane.type == TrackType::Chord && lane.track)
    {
        for (const auto& c : lane.track->getChords())
        {
            float x1 = beatToX(c.startBeat);
            float x2 = beatToX(c.startBeat + c.lengthBeats);
            float boxW = std::max(4.0f, x2 - x1);
            juce::Rectangle<float> itemRect(x1 + 1.0f, rowY + 2.0f, boxW - 2.0f, rowH - 4.0f);
            if (itemRect.contains(pos.toFloat()))
            {
                res.type = SelectedEventType::Chord;
                res.intId = c.id;
                res.startBeat = c.startBeat;
                res.lengthBeats = c.lengthBeats;
                res.bounds = itemRect;
                return res;
            }
        }
    }
    else if (lane.type == TrackType::Mode && lane.track)
    {
        for (const auto& m : lane.track->getModes())
        {
            float x1 = beatToX(m.startBeat);
            float x2 = beatToX(m.startBeat + m.lengthBeats);
            float boxW = std::max(4.0f, x2 - x1);
            juce::Rectangle<float> itemRect(x1 + 1.0f, rowY + 2.0f, boxW - 2.0f, rowH - 4.0f);
            if (itemRect.contains(pos.toFloat()))
            {
                res.type = SelectedEventType::Mode;
                res.intId = m.id;
                res.startBeat = m.startBeat;
                res.lengthBeats = m.lengthBeats;
                res.bounds = itemRect;
                return res;
            }
        }
    }
    else if (lane.type == TrackType::Marker && lane.track)
    {
        for (const auto& m : lane.track->getMarkers())
        {
            float x1 = beatToX(m.startBeat);
            float x2 = beatToX(m.startBeat + m.lengthBeats);
            float boxW = std::max(4.0f, x2 - x1);
            juce::Rectangle<float> itemRect(x1 + 1.0f, rowY + 2.0f, boxW - 2.0f, rowH - 4.0f);
            if (itemRect.contains(pos.toFloat()))
            {
                res.type = SelectedEventType::Marker;
                res.strId = m.id;
                res.startBeat = m.startBeat;
                res.lengthBeats = m.lengthBeats;
                res.bounds = itemRect;
                return res;
            }
        }
    }
    else if (lane.type == TrackType::TimeSignature && lane.track)
    {
        for (const auto& ts : lane.track->getTimeSignatures())
        {
            float x1 = beatToX(ts.startBeat);
            float boxW = 42.0f;
            juce::Rectangle<float> itemRect(x1 + 1.0f, rowY + 2.0f, boxW - 2.0f, rowH - 4.0f);
            if (itemRect.contains(pos.toFloat()))
            {
                res.type = SelectedEventType::TimeSignature;
                res.intId = ts.id;
                res.startBeat = ts.startBeat;
                res.lengthBeats = ts.lengthBeats;
                res.bounds = itemRect;
                return res;
            }
        }
    }

    return res;
}

void GlobalTrackOverviewLane::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    int w = bounds.getWidth();
    int h = bounds.getHeight();

    // Timeline background
    g.setColour(CubaseTheme::bgPanel());
    g.fillRect(bounds);

    struct ActiveLane
    {
        TrackType type;
        juce::String name;
        Track* track = nullptr;
    };

    std::vector<ActiveLane> activeLanes;
    if (showChordLane && trackModel.getChordTrack())
        activeLanes.push_back({ TrackType::Chord, trackModel.getChordTrack()->getName(), trackModel.getChordTrack() });
    if (showModeLane && trackModel.getModeTrack())
        activeLanes.push_back({ TrackType::Mode, trackModel.getModeTrack()->getName(), trackModel.getModeTrack() });
    if (showMarkerLane && trackModel.getMarkerTrack())
        activeLanes.push_back({ TrackType::Marker, trackModel.getMarkerTrack()->getName(), trackModel.getMarkerTrack() });
    if (showTimeSigLane && trackModel.getTimeSignatureTrack())
        activeLanes.push_back({ TrackType::TimeSignature, trackModel.getTimeSignatureTrack()->getName(), trackModel.getTimeSignatureTrack() });

    if (!activeLanes.empty())
    {
        float rowH = (float)h / (float)activeLanes.size();

        for (size_t i = 0; i < activeLanes.size(); ++i)
        {
            float rowY = (float)i * rowH;

            // Divider line
            g.setColour(CubaseTheme::borderSubtle());
            g.drawHorizontalLine((int)(rowY + rowH - 1), (float)headerWidth, (float)w);

            auto drawGhostBox = [&](double startB, double lenB, float rY, float rH) {
                float gx1 = beatToX(startB);
                float gx2 = beatToX(startB + lenB);
                float gBoxW = std::max(4.0f, gx2 - gx1);
                if (gx1 + gBoxW >= (float)headerWidth && gx1 <= (float)w)
                {
                    juce::Rectangle<float> ghostRect(gx1 + 1.0f, rY + 2.0f, gBoxW - 2.0f, rH - 4.0f);
                    g.setColour(CubaseTheme::accentOrange().withAlpha(0.60f));
                    g.fillRoundedRectangle(ghostRect, 2.0f);
                    g.setColour(juce::Colours::white);
                    float dashes[] = { 4.0f, 3.0f };
                    g.drawDashedLine(juce::Line<float>(ghostRect.getX(), ghostRect.getY(), ghostRect.getRight(), ghostRect.getY()), dashes, 2, 1.5f);
                    g.drawDashedLine(juce::Line<float>(ghostRect.getRight(), ghostRect.getY(), ghostRect.getRight(), ghostRect.getBottom()), dashes, 2, 1.5f);
                    g.drawDashedLine(juce::Line<float>(ghostRect.getRight(), ghostRect.getBottom(), ghostRect.getX(), ghostRect.getBottom()), dashes, 2, 1.5f);
                    g.drawDashedLine(juce::Line<float>(ghostRect.getX(), ghostRect.getBottom(), ghostRect.getX(), ghostRect.getY()), dashes, 2, 1.5f);
                }
            };

            // Render lane content
            if (activeLanes[i].type == TrackType::Chord && activeLanes[i].track)
            {
                for (const auto& c : activeLanes[i].track->getChords())
                {
                    bool isSelected = (selectedType == SelectedEventType::Chord && selectedId == c.id);
                    double drawStart = (isDragging && isSelected && !isAltDuplicating) ? currentPreviewBeat : c.startBeat;

                    float x1 = beatToX(drawStart);
                    float x2 = beatToX(drawStart + c.lengthBeats);
                    float boxW = std::max(4.0f, x2 - x1);

                    if (x1 + boxW < (float)headerWidth || x1 > (float)w)
                        continue;

                    juce::Rectangle<float> itemRect(x1 + 1.0f, rowY + 2.0f, boxW - 2.0f, rowH - 4.0f);

                    g.setColour(c.color.withAlpha(isSelected ? 0.45f : 0.28f));
                    g.fillRoundedRectangle(itemRect, 2.0f);

                    if (isSelected)
                    {
                        g.setColour(CubaseTheme::accentCyan());
                        g.drawRoundedRectangle(itemRect, 2.0f, 2.0f);
                    }
                    else
                    {
                        g.setColour(c.color.withAlpha(0.85f));
                        g.drawRoundedRectangle(itemRect, 2.0f, 1.0f);
                    }

                    if (boxW > 20.0f)
                    {
                        auto actualBg = CubaseTheme::bgPanel().overlaidWith(isSelected ? c.color.withAlpha(0.45f) : c.color.withAlpha(0.28f));
                        juce::Colour textCol = CubaseTheme::getReadableTextColor(actualBg);
                        g.setColour(textCol);
                        g.setFont(CubaseTheme::getFont(10.5f, true));

                        auto dispMode = trackModel.getChordDisplayMode();
                        juce::String text;
                        if (dispMode == ChordDisplayMode::NameOnly)
                        {
                            text = c.chordName;
                        }
                        else if (dispMode == ChordDisplayMode::RomanOnly)
                        {
                            text = (c.romanNumeral.isNotEmpty() && c.romanNumeral != "--") ? c.romanNumeral : c.chordName;
                        }
                        else if (dispMode == ChordDisplayMode::RomanAndQuality)
                        {
                            juce::String quality;
                            juce::String rootNames[] = { "C#", "D#", "F#", "G#", "A#", "Db", "Eb", "Gb", "Ab", "Bb",
                                                         "C", "D", "E", "F", "G", "A", "B" };
                            for (const auto& rn : rootNames)
                            {
                                if (c.chordName.startsWith(rn))
                                {
                                    quality = c.chordName.substring(rn.length());
                                    break;
                                }
                            }
                            text = (c.romanNumeral.isNotEmpty() && c.romanNumeral != "--" ? c.romanNumeral : c.chordName) + quality;
                        }
                        else // NameAndRoman
                        {
                            text = c.chordName;
                            if (c.romanNumeral.isNotEmpty() && c.romanNumeral != "--")
                                text += " (" + c.romanNumeral + ")";
                        }

                        g.drawText(text, itemRect.reduced(3.0f, 0.0f).toNearestInt(), juce::Justification::centred);
                    }
                }

                if (isDragging && isAltDuplicating && selectedType == SelectedEventType::Chord)
                    drawGhostBox(currentPreviewBeat, initialLengthBeats, rowY, rowH);
            }
            else if (activeLanes[i].type == TrackType::Mode && activeLanes[i].track)
            {
                for (const auto& md : activeLanes[i].track->getModes())
                {
                    bool isSelected = (selectedType == SelectedEventType::Mode && selectedId == md.id);
                    double drawStart = (isDragging && isSelected && !isAltDuplicating) ? currentPreviewBeat : md.startBeat;

                    float x1 = beatToX(drawStart);
                    float x2 = beatToX(drawStart + md.lengthBeats);
                    float boxW = std::max(4.0f, x2 - x1);

                    if (x1 + boxW < (float)headerWidth || x1 > (float)w)
                        continue;

                    juce::Rectangle<float> itemRect(x1 + 1.0f, rowY + 2.0f, boxW - 2.0f, rowH - 4.0f);

                    g.setColour(md.color.withAlpha(isSelected ? 0.50f : 0.35f));
                    g.fillRoundedRectangle(itemRect, 2.0f);

                    if (isSelected)
                    {
                        g.setColour(CubaseTheme::accentCyan());
                        g.drawRoundedRectangle(itemRect, 2.0f, 2.0f);
                    }
                    else
                    {
                        g.setColour(md.color);
                        g.drawRoundedRectangle(itemRect, 2.0f, 1.0f);
                    }

                    if (boxW > 20.0f)
                    {
                        juce::String label = md.name;
                        if (md.tonic.isNotEmpty())
                            label = md.tonic + " " + md.scaleName.upToFirstOccurrenceOf(" ", false, false);

                        auto actualBg = CubaseTheme::bgPanel().overlaidWith(isSelected ? md.color.withAlpha(0.50f) : md.color.withAlpha(0.35f));
                        juce::Colour textCol = CubaseTheme::getReadableTextColor(actualBg);
                        g.setColour(textCol);
                        g.setFont(CubaseTheme::getFont(10.0f, true));
                        g.drawText(label, itemRect.reduced(3.0f, 0.0f).toNearestInt(), juce::Justification::centred);
                    }
                }

                if (isDragging && isAltDuplicating && selectedType == SelectedEventType::Mode)
                    drawGhostBox(currentPreviewBeat, initialLengthBeats, rowY, rowH);
            }
            else if (activeLanes[i].type == TrackType::Marker && activeLanes[i].track)
            {
                for (const auto& m : activeLanes[i].track->getMarkers())
                {
                    bool isSelected = (selectedType == SelectedEventType::Marker && selectedMarkerStrId == m.id);
                    double drawStart = (isDragging && isSelected && !isAltDuplicating) ? currentPreviewBeat : m.startBeat;

                    float x1 = beatToX(drawStart);
                    float x2 = beatToX(drawStart + m.lengthBeats);
                    float boxW = std::max(4.0f, x2 - x1);

                    if (x1 + boxW < (float)headerWidth || x1 > (float)w)
                        continue;

                    juce::Rectangle<float> itemRect(x1 + 1.0f, rowY + 2.0f, boxW - 2.0f, rowH - 4.0f);

                    g.setColour(m.color.withAlpha(isSelected ? 0.50f : 0.35f));
                    g.fillRoundedRectangle(itemRect, 2.0f);

                    if (isSelected)
                    {
                        g.setColour(CubaseTheme::accentCyan());
                        g.drawRoundedRectangle(itemRect, 2.0f, 2.0f);
                    }
                    else
                    {
                        g.setColour(m.color);
                        g.drawRoundedRectangle(itemRect, 2.0f, 1.0f);
                    }

                    if (boxW > 20.0f)
                    {
                        auto actualBg = CubaseTheme::bgPanel().overlaidWith(isSelected ? m.color.withAlpha(0.50f) : m.color.withAlpha(0.35f));
                        juce::Colour textCol = CubaseTheme::getReadableTextColor(actualBg);
                        g.setColour(textCol);
                        g.setFont(CubaseTheme::getFont(10.0f, true));
                        g.drawText(m.name, itemRect.reduced(3.0f, 0.0f).toNearestInt(), juce::Justification::centred);
                    }
                }

                if (isDragging && isAltDuplicating && selectedType == SelectedEventType::Marker)
                    drawGhostBox(currentPreviewBeat, initialLengthBeats, rowY, rowH);
            }
            else if (activeLanes[i].type == TrackType::TimeSignature && activeLanes[i].track)
            {
                for (const auto& ts : activeLanes[i].track->getTimeSignatures())
                {
                    bool isSelected = (selectedType == SelectedEventType::TimeSignature && selectedId == ts.id);
                    double drawStart = (isDragging && isSelected && !isAltDuplicating) ? currentPreviewBeat : ts.startBeat;

                    float x1 = beatToX(drawStart);
                    float boxW = 42.0f; // Minimal compact badge matching TrackArranger

                    if (x1 + boxW < (float)headerWidth || x1 > (float)w)
                        continue;

                    juce::Rectangle<float> itemRect(x1 + 1.0f, rowY + 2.0f, boxW - 2.0f, rowH - 4.0f);

                    g.setColour(ts.color.withAlpha(isSelected ? 0.50f : 0.35f));
                    g.fillRoundedRectangle(itemRect, 2.0f);

                    if (isSelected)
                    {
                        g.setColour(CubaseTheme::accentCyan());
                        g.drawRoundedRectangle(itemRect, 2.0f, 2.0f);
                    }
                    else
                    {
                        g.setColour(ts.color);
                        g.drawRoundedRectangle(itemRect, 2.0f, 1.0f);
                    }

                    if (boxW > 16.0f)
                    {
                        juce::String label = juce::String(ts.numerator) + "/" + juce::String(ts.denominator);
                        auto actualBg = CubaseTheme::bgPanel().overlaidWith(isSelected ? ts.color.withAlpha(0.50f) : ts.color.withAlpha(0.35f));
                        juce::Colour textCol = CubaseTheme::getReadableTextColor(actualBg);
                        g.setColour(textCol);
                        g.setFont(CubaseTheme::getFont(10.5f, true));
                        g.drawText(label, itemRect.reduced(2.0f, 0.0f).toNearestInt(), juce::Justification::centred);
                    }
                }

                if (isDragging && isAltDuplicating && selectedType == SelectedEventType::TimeSignature)
                {
                    float gx1 = beatToX(currentPreviewBeat);
                    float gBoxW = 42.0f;
                    if (gx1 + gBoxW >= (float)headerWidth && gx1 <= (float)w)
                    {
                        juce::Rectangle<float> ghostRect(gx1 + 1.0f, rowY + 2.0f, gBoxW - 2.0f, rowH - 4.0f);
                        g.setColour(CubaseTheme::accentOrange().withAlpha(0.60f));
                        g.fillRoundedRectangle(ghostRect, 2.0f);
                        g.setColour(juce::Colours::white);
                        float dashes[] = { 4.0f, 3.0f };
                        g.drawDashedLine(juce::Line<float>(ghostRect.getX(), ghostRect.getY(), ghostRect.getRight(), ghostRect.getY()), dashes, 2, 1.5f);
                        g.drawDashedLine(juce::Line<float>(ghostRect.getRight(), ghostRect.getY(), ghostRect.getRight(), ghostRect.getBottom()), dashes, 2, 1.5f);
                        g.drawDashedLine(juce::Line<float>(ghostRect.getRight(), ghostRect.getBottom(), ghostRect.getX(), ghostRect.getBottom()), dashes, 2, 1.5f);
                        g.drawDashedLine(juce::Line<float>(ghostRect.getX(), ghostRect.getBottom(), ghostRect.getX(), ghostRect.getY()), dashes, 2, 1.5f);
                    }
                }
            }
        }
    }

    // Left Header Area (Cover 0 to headerWidth strictly)
    g.setColour(CubaseTheme::bgSurface());
    g.fillRect(0, 0, headerWidth, h);

    if (!activeLanes.empty())
    {
        float rowH = (float)h / (float)activeLanes.size();
        for (size_t i = 0; i < activeLanes.size(); ++i)
        {
            float rowY = (float)i * rowH;

            // Colored indicator stripe on left
            auto trkColor = activeLanes[i].track ? activeLanes[i].track->getColor() : CubaseTheme::accentCyan();
            g.setColour(trkColor);
            g.fillRect(0.0f, rowY + 1.0f, 3.0f, rowH - 2.0f);

            // Track Name (real name from track model, e.g. Chord Track, Mode Track, Marker Track)
            juce::String trkName = activeLanes[i].track ? activeLanes[i].track->getName() : activeLanes[i].name;
            g.setColour(CubaseTheme::textPrimary());
            g.setFont(CubaseTheme::getFont(10.0f, true));
            g.drawText(trkName, 6, (int)rowY, headerWidth - 8, (int)rowH, juce::Justification::centredLeft, true);

            // Horizontal row divider
            g.setColour(CubaseTheme::borderSubtle());
            g.drawHorizontalLine((int)(rowY + rowH - 1), 0.0f, (float)headerWidth);
        }
    }

    g.setColour(CubaseTheme::borderBright());
    g.drawVerticalLine(headerWidth - 1, 0.0f, (float)h);
    g.drawHorizontalLine(h - 1, 0.0f, (float)w);
}

void GlobalTrackOverviewLane::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (onFocusGained)
        onFocusGained();

    if (e.x < headerWidth)
        return;

    auto hit = hitTestEvent(e.getPosition());

    if (e.mods.isPopupMenu())
    {
        if (hit.type != SelectedEventType::None)
        {
            selectedType = hit.type;
            selectedId = hit.intId;
            selectedMarkerStrId = hit.strId;
            repaint();
            showContextMenu(hit, e.getScreenPosition());
        }
        return;
    }

    if (hit.type != SelectedEventType::None)
    {
        // Mutual exclusion: selecting an overview event deselects notes in the piano roll
        midiModel.deselectAll();

        selectedType = hit.type;
        selectedId = hit.intId;
        selectedMarkerStrId = hit.strId;
        isDragging = true;
        isAltDuplicating = e.mods.isAltDown();
        dragClickBeat = xToBeat((float)e.x);
        initialStartBeat = hit.startBeat;
        initialLengthBeats = hit.lengthBeats;
        currentPreviewBeat = hit.startBeat;
        repaint();
    }
    else
    {
        selectedType = SelectedEventType::None;
        selectedId = -1;
        selectedMarkerStrId = "";
        isDragging = false;

        if (e.mods.isAltDown())
        {
            struct LaneInfo { TrackType type; Track* track; };
            std::vector<LaneInfo> activeLanes;
            if (showChordLane && trackModel.getChordTrack())
                activeLanes.push_back({ TrackType::Chord, trackModel.getChordTrack() });
            if (showModeLane && trackModel.getModeTrack())
                activeLanes.push_back({ TrackType::Mode, trackModel.getModeTrack() });
            if (showMarkerLane && trackModel.getMarkerTrack())
                activeLanes.push_back({ TrackType::Marker, trackModel.getMarkerTrack() });
            if (showTimeSigLane && trackModel.getTimeSignatureTrack())
                activeLanes.push_back({ TrackType::TimeSignature, trackModel.getTimeSignatureTrack() });

            if (!activeLanes.empty())
            {
                float rowH = (float)getHeight() / (float)activeLanes.size();
                int rowIdx = std::clamp((int)(e.y / rowH), 0, (int)activeLanes.size() - 1);
                auto& lane = activeLanes[(size_t)rowIdx];

                double clickBeat = std::max(0.0, xToBeat((float)e.x));
                double snapInt = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.0;
                double snapBeat = clickBeat;
                if (snapInt > 0.0)
                    snapBeat = std::round(snapBeat / snapInt) * snapInt;
                else
                {
                    int bpb = midiModel.getTimeSigNumerator();
                    if (bpb <= 0) bpb = 4;
                    snapBeat = std::floor(snapBeat / (double)bpb) * (double)bpb;
                }

                int bpb = midiModel.getTimeSigNumerator();
                if (bpb <= 0) bpb = 4;

                if (lane.type == TrackType::Chord && lane.track)
                {
                    double chordLen = (double)bpb;
                    double availableLen = chordLen;
                    for (const auto& other : lane.track->getChords())
                    {
                        if (other.startBeat >= snapBeat)
                            availableLen = std::min(availableLen, other.startBeat - snapBeat);
                    }
                    double finalLen = std::min(chordLen, availableLen);
                    if (finalLen >= 0.25 && trackModel.canPlaceEvent(lane.track->getId(), snapBeat, finalLen))
                    {
                        auto oldChords = lane.track->getChords();
                        ChordEvent c;
                        c.id = juce::Time::currentTimeMillis();
                        c.startBeat = snapBeat;
                        c.lengthBeats = finalLen;
                        c.chordName = "C";
                        c.romanNumeral = "I";
                        c.color = juce::Colour(0xfff59e0b);

                        auto newChords = oldChords;
                        newChords.push_back(c);

                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyChordsUndoAction(trackModel, lane.track->getId(), oldChords, newChords, "Add Chord Block"),
                            "Add Chord Block");

                        selectedType = SelectedEventType::Chord;
                        selectedId = c.id;
                        selectedMarkerStrId = "";
                        isDragging = false;
                        isCreatingItemWithAlt = true;
                        creatingTrackType = TrackType::Chord;
                        creatingItemId = c.id;
                        creatingStartBeat = snapBeat;
                        repaint();
                        return;
                    }
                }
                else if (lane.type == TrackType::Mode && lane.track)
                {
                    double modeLen = (double)(bpb * 4);
                    double availableLen = modeLen;
                    for (const auto& other : lane.track->getModes())
                    {
                        if (other.startBeat >= snapBeat)
                            availableLen = std::min(availableLen, other.startBeat - snapBeat);
                    }
                    double finalLen = std::min(modeLen, availableLen);
                    if (availableLen >= 1.0 && trackModel.canPlaceEvent(lane.track->getId(), snapBeat, finalLen))
                    {
                        auto oldModes = lane.track->getModes();
                        ModeEvent md;
                        md.id = juce::Time::currentTimeMillis();
                        md.startBeat = snapBeat;
                        md.lengthBeats = finalLen;
                        md.name = "Section";
                        md.tonic = "C";
                        md.tonicPc = 0;
                        md.scaleName = "Ionian (Natural Major)";
                        md.color = juce::Colour(0xff10b981);

                        auto newModes = oldModes;
                        newModes.push_back(md);

                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyModesUndoAction(trackModel, lane.track->getId(), oldModes, newModes, "Add Mode Section"),
                            "Add Mode Section");

                        selectedType = SelectedEventType::Mode;
                        selectedId = md.id;
                        selectedMarkerStrId = "";
                        isDragging = false;
                        isCreatingItemWithAlt = true;
                        creatingTrackType = TrackType::Mode;
                        creatingItemId = md.id;
                        creatingStartBeat = snapBeat;
                        repaint();
                        return;
                    }
                }
                else if (lane.type == TrackType::Marker && lane.track)
                {
                    auto oldMarkers = lane.track->getMarkers();
                    MarkerEvent m;
                    m.id = juce::String(juce::Time::currentTimeMillis());
                    m.startBeat = snapBeat;
                    m.lengthBeats = (double)(bpb * 2);
                    m.name = "Marker " + juce::String((int)oldMarkers.size() + 1);
                    m.color = juce::Colour(0xff0284c7);

                    auto newMarkers = oldMarkers;
                    newMarkers.push_back(m);

                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyMarkersUndoAction(trackModel, lane.track->getId(), oldMarkers, newMarkers, "Add Marker"),
                        "Add Marker");

                    selectedType = SelectedEventType::Marker;
                    selectedId = -1;
                    selectedMarkerStrId = m.id;
                    isDragging = false;
                    isCreatingItemWithAlt = true;
                    creatingTrackType = TrackType::Marker;
                    creatingItemStringId = m.id;
                    creatingStartBeat = snapBeat;
                    repaint();
                    return;
                }
                else if (lane.type == TrackType::TimeSignature && lane.track)
                {
                    auto oldSigs = lane.track->getTimeSignatures();
                    TimeSignatureEvent ts;
                    ts.id = juce::Time::currentTimeMillis();
                    ts.startBeat = snapBeat;
                    ts.lengthBeats = (double)bpb;
                    ts.numerator = midiModel.getTimeSigNumerator();
                    ts.denominator = midiModel.getTimeSigDenominator();
                    ts.color = juce::Colour(0xff6366f1);

                    auto newSigs = oldSigs;
                    newSigs.push_back(ts);

                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyTimeSignaturesUndoAction(trackModel, lane.track->getId(), oldSigs, newSigs, "Add Time Signature"),
                        "Add Time Signature");

                    selectedType = SelectedEventType::TimeSignature;
                    selectedId = ts.id;
                    selectedMarkerStrId = "";
                    isDragging = false;
                    repaint();
                    return;
                }
            }
        }

        double clickBeat = std::max(0.0, xToBeat((float)e.x));
        if (onSeekPlayhead)
            onSeekPlayhead(clickBeat);
        repaint();
    }
}

void GlobalTrackOverviewLane::mouseDrag(const juce::MouseEvent& e)
{
    if (isCreatingItemWithAlt)
    {
        double curBeat = xToBeat((float)e.x);
        double snapInterval = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.0;
        double minLen = (midiModel.isSnapEnabled() && snapInterval > 0.0) ? snapInterval : 1.0;
        double len = std::max(minLen, curBeat - creatingStartBeat);
        if (midiModel.isSnapEnabled() && snapInterval > 0.0)
            len = std::max(minLen, std::ceil(len / snapInterval) * snapInterval);

        if (creatingTrackType == TrackType::Chord)
        {
            if (auto* trk = trackModel.getChordTrack())
            {
                double nextStart = 1e9;
                for (const auto& c : trk->getChords())
                    if (c.id != creatingItemId && c.startBeat >= creatingStartBeat)
                        nextStart = std::min(nextStart, c.startBeat);
                if (nextStart < 1e8) len = std::min(len, std::max(minLen, nextStart - creatingStartBeat));
                for (auto& c : trk->getChords())
                    if (c.id == creatingItemId) { c.lengthBeats = len; break; }
                repaint();
            }
        }
        else if (creatingTrackType == TrackType::Mode)
        {
            if (auto* trk = trackModel.getModeTrack())
            {
                double nextStart = 1e9;
                for (const auto& md : trk->getModes())
                    if (md.id != creatingItemId && md.startBeat >= creatingStartBeat)
                        nextStart = std::min(nextStart, md.startBeat);
                if (nextStart < 1e8) len = std::min(len, std::max(minLen, nextStart - creatingStartBeat));
                for (auto& md : trk->getModes())
                    if (md.id == creatingItemId) { md.lengthBeats = len; break; }
                repaint();
            }
        }
        else if (creatingTrackType == TrackType::Marker)
        {
            if (auto* trk = trackModel.getMarkerTrack())
            {
                for (auto& m : trk->getMarkers())
                    if (m.id == creatingItemStringId) { m.lengthBeats = len; break; }
                repaint();
            }
        }
        return;
    }

    if (isDragging && selectedType != SelectedEventType::None)
    {
        double curBeat = xToBeat((float)e.x);
        double delta = curBeat - dragClickBeat;
        double targetBeat = std::max(0.0, initialStartBeat + delta);

        double snapInt = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.0;
        if (snapInt > 0.0)
            targetBeat = std::round(targetBeat / snapInt) * snapInt;

        // Anti-collision neighbor clamping during drag
        if (selectedType == SelectedEventType::Chord)
        {
            if (auto* ct = trackModel.getChordTrack())
            {
                double prevEnd = 0.0;
                double nextStart = 1e9;
                for (const auto& other : ct->getChords())
                {
                    if (other.id == selectedId) continue;  // always skip self; for alt-dup, ignoreId is handled in mouseUp
                    if (other.startBeat < initialStartBeat - 0.001)
                        prevEnd = std::max(prevEnd, other.startBeat + other.lengthBeats);
                    else if (other.startBeat >= initialStartBeat - 0.001)
                        nextStart = std::min(nextStart, other.startBeat);
                }
                targetBeat = std::clamp(targetBeat, prevEnd, std::max(prevEnd, nextStart - initialLengthBeats));
            }
        }
        else if (selectedType == SelectedEventType::Mode)
        {
            if (auto* mt = trackModel.getModeTrack())
            {
                double prevEnd = 0.0;
                double nextStart = 1e9;
                for (const auto& other : mt->getModes())
                {
                    if (other.id == selectedId) continue;  // always skip self
                    if (other.startBeat < initialStartBeat - 0.001)
                        prevEnd = std::max(prevEnd, other.startBeat + other.lengthBeats);
                    else if (other.startBeat >= initialStartBeat - 0.001)
                        nextStart = std::min(nextStart, other.startBeat);
                }
                targetBeat = std::clamp(targetBeat, prevEnd, std::max(prevEnd, nextStart - initialLengthBeats));
            }
        }
        else if (selectedType == SelectedEventType::TimeSignature)
        {
            if (auto* tst = trackModel.getTimeSignatureTrack())
            {
                double prevEnd = 0.0;
                double nextStart = 1e9;
                for (const auto& other : tst->getTimeSignatures())
                {
                    if (other.id == selectedId) continue;
                    if (other.startBeat < initialStartBeat - 0.001)
                        prevEnd = std::max(prevEnd, other.startBeat + other.lengthBeats);
                    else if (other.startBeat >= initialStartBeat - 0.001)
                        nextStart = std::min(nextStart, other.startBeat);
                }
                targetBeat = std::clamp(targetBeat, prevEnd, std::max(prevEnd, nextStart - initialLengthBeats));
            }
        }

        if (targetBeat != currentPreviewBeat)
        {
            currentPreviewBeat = targetBeat;
            repaint();
        }
    }
}

void GlobalTrackOverviewLane::mouseUp(const juce::MouseEvent&)
{
    if (isCreatingItemWithAlt)
    {
        isCreatingItemWithAlt = false;
        if (creatingTrackType == TrackType::Chord)
            trackModel.triggerChordsChanged();
        else if (creatingTrackType == TrackType::Mode)
            trackModel.triggerModesChanged();
        else if (creatingTrackType == TrackType::Marker)
            trackModel.triggerMarkersChanged();
        else if (creatingTrackType == TrackType::TimeSignature)
            trackModel.triggerTimeSignaturesChanged();
        repaint();
        return;
    }

    if (isDragging && selectedType != SelectedEventType::None)
    {
        if (std::abs(currentPreviewBeat - initialStartBeat) > 0.001 || isAltDuplicating)
        {
            if (selectedType == SelectedEventType::Chord)
            {
                if (auto* ct = trackModel.getChordTrack())
                {
                    int64_t ignoreId = isAltDuplicating ? -1 : selectedId;
                    if (trackModel.canPlaceEvent(ct->getId(), currentPreviewBeat, initialLengthBeats, ignoreId))
                    {
                        auto beforeChords = ct->getChords();
                        auto afterChords = beforeChords;
                        if (isAltDuplicating)
                        {
                            for (const auto& c : beforeChords)
                            {
                                if (c.id == selectedId)
                                {
                                    ChordEvent newC = c;
                                    newC.id = juce::Time::currentTimeMillis();
                                    newC.startBeat = currentPreviewBeat;
                                    afterChords.push_back(newC);
                                    selectedId = newC.id;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            for (auto& c : afterChords)
                            {
                                if (c.id == selectedId)
                                {
                                    c.startBeat = currentPreviewBeat;
                                    break;
                                }
                            }
                        }
                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyChordsUndoAction(trackModel, ct->getId(), beforeChords, afterChords, isAltDuplicating ? "Duplicate Chord" : "Move Chord"),
                            isAltDuplicating ? "Duplicate Chord" : "Move Chord");
                    }
                }
            }
            else if (selectedType == SelectedEventType::Mode)
            {
                if (auto* mt = trackModel.getModeTrack())
                {
                    int64_t ignoreId = isAltDuplicating ? -1 : selectedId;
                    if (trackModel.canPlaceEvent(mt->getId(), currentPreviewBeat, initialLengthBeats, ignoreId))
                    {
                        auto beforeModes = mt->getModes();
                        auto afterModes = beforeModes;
                        if (isAltDuplicating)
                        {
                            for (const auto& m : beforeModes)
                            {
                                if (m.id == selectedId)
                                {
                                    ModeEvent newM = m;
                                    newM.id = juce::Time::currentTimeMillis();
                                    newM.startBeat = currentPreviewBeat;
                                    afterModes.push_back(newM);
                                    selectedId = newM.id;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            for (auto& m : afterModes)
                            {
                                if (m.id == selectedId)
                                {
                                    m.startBeat = currentPreviewBeat;
                                    break;
                                }
                            }
                        }
                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyModesUndoAction(trackModel, mt->getId(), beforeModes, afterModes, isAltDuplicating ? "Duplicate Mode" : "Move Mode"),
                            isAltDuplicating ? "Duplicate Mode" : "Move Mode");
                    }
                }
            }
            else if (selectedType == SelectedEventType::Marker)
            {
                if (auto* mkt = trackModel.getMarkerTrack())
                {
                    auto beforeMarkers = mkt->getMarkers();
                    auto afterMarkers = beforeMarkers;
                    if (isAltDuplicating)
                    {
                        for (const auto& m : beforeMarkers)
                        {
                            if (m.id == selectedMarkerStrId)
                            {
                                MarkerEvent newM = m;
                                newM.id = juce::Uuid().toString();
                                newM.startBeat = currentPreviewBeat;
                                afterMarkers.push_back(newM);
                                selectedMarkerStrId = newM.id;
                                break;
                            }
                        }
                    }
                    else
                    {
                        for (auto& m : afterMarkers)
                        {
                            if (m.id == selectedMarkerStrId)
                            {
                                m.startBeat = currentPreviewBeat;
                                break;
                            }
                        }
                    }
                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyMarkersUndoAction(trackModel, mkt->getId(), beforeMarkers, afterMarkers, isAltDuplicating ? "Duplicate Marker" : "Move Marker"),
                        isAltDuplicating ? "Duplicate Marker" : "Move Marker");
                }
            }
            else if (selectedType == SelectedEventType::TimeSignature)
            {
                if (auto* tst = trackModel.getTimeSignatureTrack())
                {
                    int64_t ignoreId = isAltDuplicating ? -1 : selectedId;
                    if (trackModel.canPlaceEvent(tst->getId(), currentPreviewBeat, initialLengthBeats, ignoreId))
                    {
                        auto beforeSigs = tst->getTimeSignatures();
                        auto afterSigs = beforeSigs;
                        if (isAltDuplicating)
                        {
                            for (const auto& ts : beforeSigs)
                            {
                                if (ts.id == selectedId)
                                {
                                    TimeSignatureEvent newTs = ts;
                                    newTs.id = juce::Time::currentTimeMillis();
                                    newTs.startBeat = currentPreviewBeat;
                                    afterSigs.push_back(newTs);
                                    selectedId = newTs.id;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            for (auto& ts : afterSigs)
                            {
                                if (ts.id == selectedId)
                                {
                                    ts.startBeat = currentPreviewBeat;
                                    break;
                                }
                            }
                        }
                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyTimeSignaturesUndoAction(trackModel, tst->getId(), beforeSigs, afterSigs, isAltDuplicating ? "Duplicate Time Signature" : "Move Time Signature"),
                            isAltDuplicating ? "Duplicate Time Signature" : "Move Time Signature");
                        trackModel.triggerTimeSignaturesChanged();
                    }
                }
            }
        }

        isDragging = false;
        isAltDuplicating = false;
        repaint();
    }
}

void GlobalTrackOverviewLane::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.x < headerWidth) return;

    auto hit = hitTestEvent(e.getPosition());
    if (hit.type != SelectedEventType::None)
    {
        selectedType = hit.type;
        selectedId = hit.intId;
        selectedMarkerStrId = hit.strId;
        repaint();
        openEditorForEvent(hit);
    }
}

bool GlobalTrackOverviewLane::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown())
    {
        int code = key.getKeyCode();
        if (code == 'c' || code == 'C')
        {
            copySelectedEvent();
            return true;
        }
        else if (code == 'x' || code == 'X')
        {
            cutSelectedEvent();
            return true;
        }
        else if (code == 'v' || code == 'V')
        {
            pasteEvent();
            return true;
        }
        else if (code == 'd' || code == 'D')
        {
            duplicateSelectedEvent();
            return true;
        }
        else if (code == 'z' || code == 'Z')
        {
            if (key.getModifiers().isShiftDown())
                undoManager.redo();
            else
                undoManager.undo();
            return true;
        }
        else if (code == 'y' || code == 'Y')
        {
            undoManager.redo();
            return true;
        }
    }

    bool isDelete = KeyCommandManager::getInstance().matches(Cmd_Delete, key)
                    || key == juce::KeyPress::deleteKey
                    || key == juce::KeyPress::backspaceKey;
    if (isDelete)
    {
        if (selectedType != SelectedEventType::None)
        {
            deleteSelectedEvent();
            return true;
        }
    }
    return false;
}

void GlobalTrackOverviewLane::copySelectedEvent()
{
    if (selectedType == SelectedEventType::None) return;

    overviewClipboard.type = selectedType;
    if (selectedType == SelectedEventType::Chord)
    {
        if (auto* ct = trackModel.getChordTrack())
        {
            for (const auto& c : ct->getChords())
            {
                if (c.id == selectedId)
                {
                    overviewClipboard.chord = c;
                    overviewClipboard.lengthBeats = c.lengthBeats;
                    break;
                }
            }
        }
    }
    else if (selectedType == SelectedEventType::Mode)
    {
        if (auto* mt = trackModel.getModeTrack())
        {
            for (const auto& m : mt->getModes())
            {
                if (m.id == selectedId)
                {
                    overviewClipboard.mode = m;
                    overviewClipboard.lengthBeats = m.lengthBeats;
                    break;
                }
            }
        }
    }
    else if (selectedType == SelectedEventType::Marker)
    {
        if (auto* mkt = trackModel.getMarkerTrack())
        {
            for (const auto& m : mkt->getMarkers())
            {
                if (m.id == selectedMarkerStrId)
                {
                    overviewClipboard.marker = m;
                    overviewClipboard.lengthBeats = m.lengthBeats;
                    break;
                }
            }
        }
    }
    else if (selectedType == SelectedEventType::TimeSignature)
    {
        if (auto* tst = trackModel.getTimeSignatureTrack())
        {
            for (const auto& ts : tst->getTimeSignatures())
            {
                if (ts.id == selectedId)
                {
                    overviewClipboard.timeSig = ts;
                    overviewClipboard.lengthBeats = ts.lengthBeats;
                    break;
                }
            }
        }
    }
}

void GlobalTrackOverviewLane::cutSelectedEvent()
{
    copySelectedEvent();
    deleteSelectedEvent();
}

void GlobalTrackOverviewLane::pasteEvent()
{
    if (overviewClipboard.type == SelectedEventType::None) return;

    double pasteBeat = audioEngine ? audioEngine->getPlayheadBeat() : ((selectedType != SelectedEventType::None) ? (initialStartBeat + initialLengthBeats) : scrollOffsetBeats);
    if (midiModel.isSnapEnabled() && midiModel.getSnapBeatInterval() > 0.0)
        pasteBeat = std::round(pasteBeat / midiModel.getSnapBeatInterval()) * midiModel.getSnapBeatInterval();

    if (overviewClipboard.type == SelectedEventType::Chord)
    {
        auto* ct = trackModel.getChordTrack();
        if (!ct) return;

        if (!trackModel.canPlaceEvent(ct->getId(), pasteBeat, overviewClipboard.lengthBeats))
            return;

        auto before = ct->getChords();
        ChordEvent newC = overviewClipboard.chord;
        newC.id = juce::Time::currentTimeMillis();
        newC.startBeat = pasteBeat;
        newC.lengthBeats = overviewClipboard.lengthBeats;

        auto after = before;
        after.push_back(newC);
        UndoActionHelper::performAtomic(&undoManager, new ModifyChordsUndoAction(trackModel, ct->getId(), before, after, "Paste Chord"), "Paste Chord");

        selectedType = SelectedEventType::Chord;
        selectedId = newC.id;
        selectedMarkerStrId = "";
        repaint();
    }
    else if (overviewClipboard.type == SelectedEventType::Mode)
    {
        auto* mt = trackModel.getModeTrack();
        if (!mt) return;

        if (!trackModel.canPlaceEvent(mt->getId(), pasteBeat, overviewClipboard.lengthBeats))
            return;

        auto before = mt->getModes();
        ModeEvent newM = overviewClipboard.mode;
        newM.id = juce::Time::currentTimeMillis();
        newM.startBeat = pasteBeat;
        newM.lengthBeats = overviewClipboard.lengthBeats;

        auto after = before;
        after.push_back(newM);
        UndoActionHelper::performAtomic(&undoManager, new ModifyModesUndoAction(trackModel, mt->getId(), before, after, "Paste Mode"), "Paste Mode");

        selectedType = SelectedEventType::Mode;
        selectedId = newM.id;
        selectedMarkerStrId = "";
        repaint();
    }
    else if (overviewClipboard.type == SelectedEventType::Marker)
    {
        auto* mkt = trackModel.getMarkerTrack();
        if (!mkt) return;

        auto before = mkt->getMarkers();
        MarkerEvent newMk = overviewClipboard.marker;
        newMk.id = juce::Uuid().toString();
        newMk.startBeat = pasteBeat;
        newMk.lengthBeats = overviewClipboard.lengthBeats;

        auto after = before;
        after.push_back(newMk);
        UndoActionHelper::performAtomic(&undoManager, new ModifyMarkersUndoAction(trackModel, mkt->getId(), before, after, "Paste Marker"), "Paste Marker");

        selectedType = SelectedEventType::Marker;
        selectedId = -1;
        selectedMarkerStrId = newMk.id;
        repaint();
    }
    else if (overviewClipboard.type == SelectedEventType::TimeSignature)
    {
        auto* tst = trackModel.getTimeSignatureTrack();
        if (!tst) return;

        if (!trackModel.canPlaceEvent(tst->getId(), pasteBeat, overviewClipboard.lengthBeats))
            return;

        auto before = tst->getTimeSignatures();
        TimeSignatureEvent newTs = overviewClipboard.timeSig;
        newTs.id = juce::Time::currentTimeMillis();
        newTs.startBeat = pasteBeat;
        newTs.lengthBeats = overviewClipboard.lengthBeats;

        auto after = before;
        after.push_back(newTs);
        UndoActionHelper::performAtomic(&undoManager, new ModifyTimeSignaturesUndoAction(trackModel, tst->getId(), before, after, "Paste Time Signature"), "Paste Time Signature");
        trackModel.triggerTimeSignaturesChanged();

        selectedType = SelectedEventType::TimeSignature;
        selectedId = newTs.id;
        selectedMarkerStrId = "";
        repaint();
    }
}

void GlobalTrackOverviewLane::duplicateSelectedEvent()
{
    if (selectedType == SelectedEventType::None) return;

    if (selectedType == SelectedEventType::Chord)
    {
        if (auto* ct = trackModel.getChordTrack())
        {
            for (const auto& c : ct->getChords())
            {
                if (c.id == selectedId)
                {
                    double newStart = c.startBeat + c.lengthBeats;
                    if (!trackModel.canPlaceEvent(ct->getId(), newStart, c.lengthBeats))
                        return;

                    auto before = ct->getChords();
                    ChordEvent dup = c;
                    dup.id = juce::Time::currentTimeMillis();
                    dup.startBeat = newStart;
                    auto after = before;
                    after.push_back(dup);

                    UndoActionHelper::performAtomic(&undoManager, new ModifyChordsUndoAction(trackModel, ct->getId(), before, after, "Duplicate Chord"), "Duplicate Chord");

                    selectedId = dup.id;
                    repaint();
                    break;
                }
            }
        }
    }
    else if (selectedType == SelectedEventType::Mode)
    {
        if (auto* mt = trackModel.getModeTrack())
        {
            for (const auto& m : mt->getModes())
            {
                if (m.id == selectedId)
                {
                    double newStart = m.startBeat + m.lengthBeats;
                    if (!trackModel.canPlaceEvent(mt->getId(), newStart, m.lengthBeats))
                        return;

                    auto before = mt->getModes();
                    ModeEvent dup = m;
                    dup.id = juce::Time::currentTimeMillis();
                    dup.startBeat = newStart;
                    auto after = before;
                    after.push_back(dup);

                    UndoActionHelper::performAtomic(&undoManager, new ModifyModesUndoAction(trackModel, mt->getId(), before, after, "Duplicate Mode"), "Duplicate Mode");

                    selectedId = dup.id;
                    repaint();
                    break;
                }
            }
        }
    }
    else if (selectedType == SelectedEventType::Marker)
    {
        if (auto* mkt = trackModel.getMarkerTrack())
        {
            for (const auto& m : mkt->getMarkers())
            {
                if (m.id == selectedMarkerStrId)
                {
                    auto before = mkt->getMarkers();
                    MarkerEvent dup = m;
                    dup.id = juce::Uuid().toString();
                    dup.startBeat += dup.lengthBeats;
                    auto after = before;
                    after.push_back(dup);

                    UndoActionHelper::performAtomic(&undoManager, new ModifyMarkersUndoAction(trackModel, mkt->getId(), before, after, "Duplicate Marker"), "Duplicate Marker");

                    selectedMarkerStrId = dup.id;
                    repaint();
                    break;
                }
            }
        }
    }
    else if (selectedType == SelectedEventType::TimeSignature)
    {
        if (auto* tst = trackModel.getTimeSignatureTrack())
        {
            for (const auto& ts : tst->getTimeSignatures())
            {
                if (ts.id == selectedId)
                {
                    double newStart = ts.startBeat + ts.lengthBeats;
                    if (!trackModel.canPlaceEvent(tst->getId(), newStart, ts.lengthBeats))
                        return;

                    auto before = tst->getTimeSignatures();
                    TimeSignatureEvent dup = ts;
                    dup.id = juce::Time::currentTimeMillis();
                    dup.startBeat = newStart;
                    auto after = before;
                    after.push_back(dup);

                    UndoActionHelper::performAtomic(&undoManager, new ModifyTimeSignaturesUndoAction(trackModel, tst->getId(), before, after, "Duplicate Time Signature"), "Duplicate Time Signature");
                    trackModel.triggerTimeSignaturesChanged();

                    selectedId = dup.id;
                    repaint();
                    break;
                }
            }
        }
    }
}

void GlobalTrackOverviewLane::clearSelection()
{
    if (selectedType != SelectedEventType::None || selectedId != -1 || selectedMarkerStrId.isNotEmpty())
    {
        selectedType = SelectedEventType::None;
        selectedId = -1;
        selectedMarkerStrId.clear();
        repaint();
    }
}

void GlobalTrackOverviewLane::deleteSelectedEvent()
{
    if (selectedType == SelectedEventType::Chord)
    {
        if (auto* ct = trackModel.getChordTrack())
        {
            auto before = ct->getChords();
            auto after = before;
            after.erase(std::remove_if(after.begin(), after.end(), [this](const ChordEvent& c) {
                return c.id == selectedId;
            }), after.end());
            if (before.size() != after.size())
            {
                UndoActionHelper::performAtomic(&undoManager, new ModifyChordsUndoAction(trackModel, ct->getId(), before, after, "Delete Chord"), "Delete Chord");
                selectedType = SelectedEventType::None;
                selectedId = -1;
                repaint();
            }
        }
    }
    else if (selectedType == SelectedEventType::Mode)
    {
        if (auto* mt = trackModel.getModeTrack())
        {
            auto before = mt->getModes();
            auto after = before;
            after.erase(std::remove_if(after.begin(), after.end(), [this](const ModeEvent& m) {
                return m.id == selectedId;
            }), after.end());
            if (before.size() != after.size())
            {
                UndoActionHelper::performAtomic(&undoManager, new ModifyModesUndoAction(trackModel, mt->getId(), before, after, "Delete Mode"), "Delete Mode");
                selectedType = SelectedEventType::None;
                selectedId = -1;
                repaint();
            }
        }
    }
    else if (selectedType == SelectedEventType::Marker)
    {
        if (auto* mkt = trackModel.getMarkerTrack())
        {
            auto before = mkt->getMarkers();
            auto after = before;
            after.erase(std::remove_if(after.begin(), after.end(), [this](const MarkerEvent& m) {
                return m.id == selectedMarkerStrId;
            }), after.end());
            if (before.size() != after.size())
            {
                UndoActionHelper::performAtomic(&undoManager, new ModifyMarkersUndoAction(trackModel, mkt->getId(), before, after, "Delete Marker"), "Delete Marker");
                selectedType = SelectedEventType::None;
                selectedMarkerStrId = "";
                repaint();
            }
        }
    }
    else if (selectedType == SelectedEventType::TimeSignature)
    {
        if (auto* tst = trackModel.getTimeSignatureTrack())
        {
            auto before = tst->getTimeSignatures();
            auto after = before;
            after.erase(std::remove_if(after.begin(), after.end(), [this](const TimeSignatureEvent& ts) {
                return ts.id == selectedId;
            }), after.end());
            if (before.size() != after.size())
            {
                UndoActionHelper::performAtomic(&undoManager, new ModifyTimeSignaturesUndoAction(trackModel, tst->getId(), before, after, "Delete Time Signature"), "Delete Time Signature");
                trackModel.triggerTimeSignaturesChanged();
                selectedType = SelectedEventType::None;
                selectedId = -1;
                repaint();
            }
        }
    }
}

void GlobalTrackOverviewLane::showContextMenu(const HitTestResult& hit, juce::Point<int> screenPos)
{
    juce::PopupMenu menu;
    menu.addItem(1, juce::String::fromUTF8(u8"编辑属性 (Edit)..."));
    menu.addItem(2, juce::String::fromUTF8(u8"复制副本 (Duplicate)"));
    menu.addItem(3, juce::String::fromUTF8(u8"设为播放循环区间 (Set Loop)"));
    menu.addSeparator();
    menu.addItem(4, juce::String::fromUTF8(u8"删除 (Delete)"));

    juce::Component::SafePointer<GlobalTrackOverviewLane> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)), [this, safeThis, hit](int result) {
        if (safeThis.getComponent() == nullptr) return;
        if (result == 1)
        {
            openEditorForEvent(hit);
        }
        else if (result == 2)
        {
            if (hit.type == SelectedEventType::Chord)
            {
                if (auto* ct = trackModel.getChordTrack())
                {
                    for (const auto& c : ct->getChords())
                    {
                        if (c.id == hit.intId)
                        {
                            double newStart = c.startBeat + c.lengthBeats;
                            if (!trackModel.canPlaceEvent(ct->getId(), newStart, c.lengthBeats))
                                break;

                            auto beforeChords = ct->getChords();
                            ChordEvent dup = c;
                            dup.id = juce::Time::currentTimeMillis();
                            dup.startBeat = newStart;
                            auto afterChords = beforeChords;
                            afterChords.push_back(dup);

                            UndoActionHelper::performAtomic(&undoManager, new ModifyChordsUndoAction(trackModel, ct->getId(), beforeChords, afterChords, "Duplicate Chord"), "Duplicate Chord");
                            break;
                        }
                    }
                }
            }
            else if (hit.type == SelectedEventType::Mode)
            {
                if (auto* mt = trackModel.getModeTrack())
                {
                    for (const auto& m : mt->getModes())
                    {
                        if (m.id == hit.intId)
                        {
                            double newStart = m.startBeat + m.lengthBeats;
                            if (!trackModel.canPlaceEvent(mt->getId(), newStart, m.lengthBeats))
                                break;

                            auto beforeModes = mt->getModes();
                            ModeEvent dup = m;
                            dup.id = juce::Time::currentTimeMillis();
                            dup.startBeat = newStart;
                            auto afterModes = beforeModes;
                            afterModes.push_back(dup);

                            UndoActionHelper::performAtomic(&undoManager, new ModifyModesUndoAction(trackModel, mt->getId(), beforeModes, afterModes, "Duplicate Mode"), "Duplicate Mode");
                            break;
                        }
                    }
                }
            }
            else if (hit.type == SelectedEventType::Marker)
            {
                if (auto* mkt = trackModel.getMarkerTrack())
                {
                    for (const auto& m : mkt->getMarkers())
                    {
                        if (m.id == hit.strId)
                        {
                            auto beforeMarkers = mkt->getMarkers();
                            MarkerEvent dup = m;
                            dup.id = juce::Uuid().toString();
                            dup.startBeat += dup.lengthBeats;
                            auto afterMarkers = beforeMarkers;
                            afterMarkers.push_back(dup);

                            UndoActionHelper::performAtomic(&undoManager, new ModifyMarkersUndoAction(trackModel, mkt->getId(), beforeMarkers, afterMarkers, "Duplicate Marker"), "Duplicate Marker");
                            break;
                        }
                    }
                }
            }
            else if (hit.type == SelectedEventType::TimeSignature)
            {
                if (auto* tst = trackModel.getTimeSignatureTrack())
                {
                    for (const auto& ts : tst->getTimeSignatures())
                    {
                        if (ts.id == hit.intId)
                        {
                            double newStart = ts.startBeat + ts.lengthBeats;
                            if (!trackModel.canPlaceEvent(tst->getId(), newStart, ts.lengthBeats))
                                break;

                            auto beforeSigs = tst->getTimeSignatures();
                            TimeSignatureEvent dup = ts;
                            dup.id = juce::Time::currentTimeMillis();
                            dup.startBeat = newStart;
                            auto afterSigs = beforeSigs;
                            afterSigs.push_back(dup);

                            UndoActionHelper::performAtomic(&undoManager, new ModifyTimeSignaturesUndoAction(trackModel, tst->getId(), beforeSigs, afterSigs, "Duplicate Time Signature"), "Duplicate Time Signature");
                            trackModel.triggerTimeSignaturesChanged();
                            break;
                        }
                    }
                }
            }
            repaint();
        }
        else if (result == 3)
        {
            midiModel.setLoopRange(hit.startBeat, hit.startBeat + hit.lengthBeats);
            midiModel.setLoopEnabled(true);
        }
        else if (result == 4)
        {
            deleteSelectedEvent();
        }
    });
}

void GlobalTrackOverviewLane::openEditorForEvent(const HitTestResult& hit)
{
    juce::Component::SafePointer<GlobalTrackOverviewLane> safeThis(this);
    if (hit.type == SelectedEventType::Chord)
    {
        if (auto* ct = trackModel.getChordTrack())
        {
            for (auto& c : ct->getChords())
            {
                if (c.id == hit.intId)
                {
                    int trackId = ct->getId();
                    ChordEditorDialog::showModal(c, [this, safeThis, trackId, id = c.id](const juce::String& name, const juce::String& roman) {
                        if (safeThis.getComponent() == nullptr) return;
                        if (auto* ct = trackModel.getChordTrack())
                        {
                            auto oldChords = ct->getChords();
                            auto newChords = oldChords;
                            for (auto& ch : newChords)
                            {
                                if (ch.id == id)
                                {
                                    ch.chordName = name;
                                    ch.romanNumeral = roman;
                                    break;
                                }
                            }
                            UndoActionHelper::performAtomic(&undoManager, new ModifyChordsUndoAction(trackModel, trackId, oldChords, newChords, "Edit Chord"), "Edit Chord");
                        }
                    });
                    break;
                }
            }
        }
    }
    else if (hit.type == SelectedEventType::Mode)
    {
        if (auto* mt = trackModel.getModeTrack())
        {
            for (auto& m : mt->getModes())
            {
                if (m.id == hit.intId)
                {
                    auto initialMode = m;
                    auto oldModes = mt->getModes();
                    int trackId = mt->getId();

                    ModeEditorDialog::showModal(m,
                        [this, safeThis, id = m.id](const juce::String& name, const juce::String& tonic, int tonicPc, const juce::String& scaleName, juce::Colour col) {
                            if (safeThis.getComponent() == nullptr) return;
                            if (auto* mt = trackModel.getModeTrack())
                            {
                                for (auto& md : mt->getModes())
                                {
                                    if (md.id == id)
                                    {
                                        md.name = name;
                                        md.tonic = tonic;
                                        md.tonicPc = tonicPc;
                                        md.scaleName = scaleName;
                                        md.color = col;
                                        trackModel.triggerModesChanged();
                                        repaint();
                                        break;
                                    }
                                }
                            }
                        },
                        [this, safeThis, id = m.id, initialMode]() {
                            if (safeThis.getComponent() == nullptr) return;
                            if (auto* mt = trackModel.getModeTrack())
                            {
                                for (auto& md : mt->getModes())
                                {
                                    if (md.id == id)
                                    {
                                        md.name = initialMode.name;
                                        md.tonic = initialMode.tonic;
                                        md.tonicPc = initialMode.tonicPc;
                                        md.scaleName = initialMode.scaleName;
                                        md.color = initialMode.color;
                                        trackModel.triggerModesChanged();
                                        repaint();
                                        break;
                                    }
                                }
                            }
                        },
                        [this, safeThis, trackId, oldModes]() {
                            if (safeThis.getComponent() == nullptr) return;
                            if (auto* mt = trackModel.getModeTrack())
                            {
                                auto newModes = mt->getModes();
                                UndoActionHelper::performAtomic(&undoManager, new ModifyModesUndoAction(trackModel, trackId, oldModes, newModes, "Edit Mode Section"), "Edit Mode Section");
                            }
                        }
                    );
                    break;
                }
            }
        }
    }
    else if (hit.type == SelectedEventType::Marker)
    {
        if (auto* mkt = trackModel.getMarkerTrack())
        {
            for (auto& m : mkt->getMarkers())
            {
                if (m.id == hit.strId)
                {
                    int trackId = mkt->getId();
                    MarkerEditorDialog::showModal(m, [this, safeThis, trackId, strId = m.id](const juce::String& name, juce::Colour col) {
                        if (safeThis.getComponent() == nullptr) return;
                        if (auto* mkt = trackModel.getMarkerTrack())
                        {
                            auto oldMarkers = mkt->getMarkers();
                            auto newMarkers = oldMarkers;
                            for (auto& mk : newMarkers)
                            {
                                if (mk.id == strId)
                                {
                                    mk.name = name;
                                    mk.color = col;
                                    break;
                                }
                            }
                            UndoActionHelper::performAtomic(&undoManager, new ModifyMarkersUndoAction(trackModel, trackId, oldMarkers, newMarkers, "Edit Marker Section"), "Edit Marker Section");
                        }
                    });
                    break;
                }
            }
        }
    }
    else if (hit.type == SelectedEventType::TimeSignature)
    {
        if (auto* tst = trackModel.getTimeSignatureTrack())
        {
            for (auto& ts : tst->getTimeSignatures())
            {
                if (ts.id == hit.intId)
                {
                    int trackId = tst->getId();
                    TimeSigEditorDialog::showModal(ts, [this, safeThis, trackId, id = ts.id](int newNum, int newDen) {
                        if (safeThis.getComponent() == nullptr) return;
                        if (auto* tst = trackModel.getTimeSignatureTrack())
                        {
                            auto oldSigs = tst->getTimeSignatures();
                            auto newSigs = oldSigs;
                            for (auto& s : newSigs)
                            {
                                if (s.id == id)
                                {
                                    s.numerator = newNum;
                                    s.denominator = newDen;
                                    break;
                                }
                            }
                            UndoActionHelper::performAtomic(&undoManager, new ModifyTimeSignaturesUndoAction(trackModel, trackId, oldSigs, newSigs, "Edit Time Signature"), "Edit Time Signature");
                        }
                    });
                    break;
                }
            }
        }
    }
}

void GlobalTrackOverviewLane::modifierKeysChanged(const juce::ModifierKeys& mods)
{
    if (mods.isAltDown())
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

} // namespace Harmonizer
