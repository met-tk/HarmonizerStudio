#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include "SectionLayout.h"

namespace Harmonizer
{

class AppStateManager
{
public:
    static AppStateManager& getInstance()
    {
        static AppStateManager instance;
        return instance;
    }

    // Main window
    int  mainWindowX = 50, mainWindowY = 50;
    int  mainWindowW = 1360, mainWindowH = 840;
    bool mainWindowMaximized = false;

    // Main layout (ViewLayout: 0=Split,1=ArrangerOnly,2=EditorOnly,3=DualWindow)
    int   viewLayout = 0;
    float splitRatio = 0.38f;
    bool  leftZoneVisible  = true;
    bool  rightZoneVisible = true;
    int   leftZoneWidth    = 260;
    int   rightZoneWidth   = 260;

    // Floating Key Editor window
    int  floatWinX = 200, floatWinY = 120;
    int  floatWinW = 1000, floatWinH = 620;
    bool floatWinAlwaysOnTop = false;
    bool floatWinWasOpen = false;

    // Inspector workspace (sections x 3 contexts: Arranger/KeyEditor/Default)
    int  sectionPositions[kInspectorSectionCount][3] = {};
    std::array<int, kInspectorSectionCount> sectionOrder = kDefaultSectionOrder;
    bool inspectorStateValid = false;

    // TrackArranger view state
    float  arrangerPixelsPerBeat = 25.0f;
    double arrangerScrollBeats   = 0.0;
    int    arrangerTrackHeight   = 44;

    // PianoRoll view state
    double pianoRollPixelsPerBeat = 40.0;
    float  pianoRollNoteHeight    = 16.0f;
    int    pianoRollScrollX = 0;
    int    pianoRollScrollY = 0;

    // Edit settings
    bool snapEnabled  = true;
    int  snapDivision = 2;   // SnapDivision::Quarter
    bool snapTriplet  = false;
    int  followMode   = 0;   // PlayheadFollowMode::Off
    int  lastEnabledFollowMode = 1; // PlayheadFollowMode::Page

    // Loop
    bool   loopEnabled = false;
    double loopStart   = 0.0;
    double loopEnd     = 16.0;

    // Real-time Chord Analysis Custom Settings
    int  chordAnalysisDisplayMode = 0;  // 0 = Both (Name + Roman), 1 = Chord Name Only, 2 = Roman Numeral Only
    int  chordAnalysisFontSize    = 0;  // 0 = Medium (Standard), 1 = Small (Compact), 2 = Large (Prominent)
    int  chordAnalysisFontSizePt  = 18; // 10 ~ 48 pt
    int  chordAnalysisOrientation = 0;  // 0 = Vertical Stack (上下堆叠), 1 = Horizontal Inline (左右并排)
    int  chordAnalysisVisualStyle = 0;  // 0 to 7 (8 visual themes)

    // Note Inspector Custom Field Settings
    int  notePropertyFontSizePt = 12;    // 9 ~ 24 pt (默认 12pt)
    bool noteShowPitch          = true;
    bool noteShowStart          = true;
    bool noteShowLength         = true;
    bool noteShowVelocity       = true;
    bool noteShowScaleDegree    = true;  // 调式音级 (如: 1, ♭2, ♮6)
    bool noteShowFunctionalRole = true;  // 功能与角色 (传统 T/S/D / 调式中心 Finalis / 特征音)
    bool noteShowMotionTendency = true;  // 倾向与导向 (势能 + 方向目标向量)
    bool noteShowColorUsage     = true;  // 色彩与用法 (和声色彩张力 + 实用编曲标签)
    bool noteShowInterval       = true;  // 2个音符之间的音程 (如: 纯五度 (7个半音))

    // Show explicit major/minor/dominant stack directions in addition to
    // the natural, scale-aware stack controls.
    bool showChordStackDirections = false;
    bool quickChordComplexMode = false;
    bool quickChordPreserveSelectedTiming = true;

    // Secondary Dominant defaults to selected target chords,
    // V7 and the closest voice leading.
    int secondaryDominantTargetSource = 0;
    int secondaryDominantManualRootPc = 0;
    bool secondaryDominantFlatNine = false;
    bool secondaryDominantRootInBass = false;
    int reharmonizationAlgorithm = 0;
    int modeBorrowingStyle = 0; // 0 = same tonic, 1 = approach
    bool modeBorrowingCustomSource = false;
    int modeBorrowingSourceRootPc = 0;
    juce::String modeBorrowingSourceScale = "Ionian (Natural Major)";
    int modeBorrowingTargetRootPc = 0;
    juce::String modeBorrowingScale = "Aeolian (Natural Minor)"; // target mode type
    int modeBorrowingDegree = -1; // keep selected chord root

    void save()
    {
        auto dir = getAppDataDir();
        dir.createDirectory();
        auto file = dir.getChildFile("ui_state.xml");

        juce::XmlElement root("UIState");
        root.setAttribute("version", 3);

        auto* win = root.createNewChildElement("MainWindow");
        win->setAttribute("x",         mainWindowX);
        win->setAttribute("y",         mainWindowY);
        win->setAttribute("width",     mainWindowW);
        win->setAttribute("height",    mainWindowH);
        win->setAttribute("maximized", mainWindowMaximized);

        auto* lay = root.createNewChildElement("MainLayout");
        lay->setAttribute("mode",           viewLayout);
        lay->setAttribute("splitRatio",     (double)splitRatio);
        lay->setAttribute("leftZone",       leftZoneVisible);
        lay->setAttribute("rightZone",      rightZoneVisible);
        lay->setAttribute("leftZoneWidth",  leftZoneWidth);
        lay->setAttribute("rightZoneWidth", rightZoneWidth);

        auto* fw = root.createNewChildElement("FloatingKeyEditor");
        fw->setAttribute("x",           floatWinX);
        fw->setAttribute("y",           floatWinY);
        fw->setAttribute("width",       floatWinW);
        fw->setAttribute("height",      floatWinH);
        fw->setAttribute("alwaysOnTop", floatWinAlwaysOnTop);
        fw->setAttribute("wasOpen",     floatWinWasOpen);

        auto* ws    = root.createNewChildElement("Workspace");
        auto* sects = ws->createNewChildElement("Sections");
        for (int i = 0; i < kInspectorSectionCount; ++i)
        {
            auto* s = sects->createNewChildElement("Section");
            s->setAttribute("index",             i);
            s->setAttribute("position",          sectionPositions[i][0]);
            s->setAttribute("keyEditorPosition", sectionPositions[i][1]);
            s->setAttribute("defaultPosition",   sectionPositions[i][2]);
        }
        auto* orderElem = ws->createNewChildElement("SectionOrder");
        for (int i = 0; i < kInspectorSectionCount; ++i)
        {
            auto* o = orderElem->createNewChildElement("Item");
            o->setAttribute("section", sectionOrder[i]);
        }

        auto* ta = root.createNewChildElement("TrackArranger");
        ta->setAttribute("pixelsPerBeat", (double)arrangerPixelsPerBeat);
        ta->setAttribute("scrollBeats",   arrangerScrollBeats);
        ta->setAttribute("trackHeight",   arrangerTrackHeight);

        auto* pr = root.createNewChildElement("PianoRoll");
        pr->setAttribute("pixelsPerBeat", pianoRollPixelsPerBeat);
        pr->setAttribute("noteHeight",    (double)pianoRollNoteHeight);
        pr->setAttribute("scrollX",       pianoRollScrollX);
        pr->setAttribute("scrollY",       pianoRollScrollY);

        auto* es = root.createNewChildElement("EditSettings");
        es->setAttribute("snapEnabled",  snapEnabled);
        es->setAttribute("snapDivision", snapDivision);
        es->setAttribute("snapTriplet",  snapTriplet);
        es->setAttribute("followMode",   followMode);
        es->setAttribute("lastEnabledFollowMode", lastEnabledFollowMode);

        auto* lp = root.createNewChildElement("Loop");
        lp->setAttribute("enabled", loopEnabled);
        lp->setAttribute("start",   loopStart);
        lp->setAttribute("end",     loopEnd);

        auto* ms = root.createNewChildElement("ModuleSettings");
        auto* ca = ms->createNewChildElement("ChordAnalysis");
        ca->setAttribute("displayMode", chordAnalysisDisplayMode);
        ca->setAttribute("fontSize",    chordAnalysisFontSize);
        ca->setAttribute("fontSizePt",  chordAnalysisFontSizePt);
        ca->setAttribute("orientation", chordAnalysisOrientation);
        ca->setAttribute("visualStyle", chordAnalysisVisualStyle);

        auto* ni = ms->createNewChildElement("NoteInspector");
        ni->setAttribute("fontSizePt",         notePropertyFontSizePt);
        ni->setAttribute("showPitch",          noteShowPitch);
        ni->setAttribute("showStart",          noteShowStart);
        ni->setAttribute("showLength",         noteShowLength);
        ni->setAttribute("showVelocity",       noteShowVelocity);
        ni->setAttribute("showScaleDegree",    noteShowScaleDegree);
        ni->setAttribute("showFunctionalRole", noteShowFunctionalRole);
        ni->setAttribute("showMotionTendency", noteShowMotionTendency);
        ni->setAttribute("showColorUsage",     noteShowColorUsage);
        ni->setAttribute("showInterval",       noteShowInterval);

        auto* cs = ms->createNewChildElement("ChordShape");
        cs->setAttribute("showStackDirections", showChordStackDirections);
        auto* qc = ms->createNewChildElement("QuickChord");
        qc->setAttribute("complexMode", quickChordComplexMode);
        qc->setAttribute("preserveSelectedTiming", quickChordPreserveSelectedTiming);

        auto* sd = ms->createNewChildElement("SecondaryDominant");
        sd->setAttribute("targetSource", secondaryDominantTargetSource);
        sd->setAttribute("manualRootPc", secondaryDominantManualRootPc);
        sd->setAttribute("flatNine", secondaryDominantFlatNine);
        sd->setAttribute("rootInBass", secondaryDominantRootInBass);
        auto* rh = ms->createNewChildElement("Reharmonization");
        rh->setAttribute("algorithm", reharmonizationAlgorithm);
        auto* mb = ms->createNewChildElement("ModeBorrowing");
        mb->setAttribute("style", modeBorrowingStyle);
        mb->setAttribute("customSource", modeBorrowingCustomSource);
        mb->setAttribute("sourceRootPc", modeBorrowingSourceRootPc);
        mb->setAttribute("sourceScale", modeBorrowingSourceScale);
        mb->setAttribute("targetRootPc", modeBorrowingTargetRootPc);
        mb->setAttribute("scale", modeBorrowingScale);
        mb->setAttribute("degree", modeBorrowingDegree);

        juce::TemporaryFile temporary(file);
        if (root.writeTo(temporary.getFile()))
            temporary.overwriteTargetFileWithTemporary();
    }

    void load()
    {
        auto file = getAppDataDir().getChildFile("ui_state.xml");
        if (!file.existsAsFile()) return;

        auto xml = juce::parseXML(file);
        if (!xml || xml->getTagName() != "UIState") return;
        const int uiStateVersion = xml->getIntAttribute("version", 1);

        if (auto* win = xml->getChildByName("MainWindow"))
        {
            mainWindowX         = win->getIntAttribute("x",         mainWindowX);
            mainWindowY         = win->getIntAttribute("y",         mainWindowY);
            mainWindowW         = win->getIntAttribute("width",     mainWindowW);
            mainWindowH         = win->getIntAttribute("height",    mainWindowH);
            mainWindowMaximized = win->getBoolAttribute("maximized", mainWindowMaximized);
            mainWindowW = std::max(mainWindowW, 800);
            mainWindowH = std::max(mainWindowH, 500);
        }

        if (auto* lay = xml->getChildByName("MainLayout"))
        {
            viewLayout       = lay->getIntAttribute("mode",           viewLayout);
            splitRatio       = (float)lay->getDoubleAttribute("splitRatio", (double)splitRatio);
            leftZoneVisible  = lay->getBoolAttribute("leftZone",       leftZoneVisible);
            rightZoneVisible = lay->getBoolAttribute("rightZone",      rightZoneVisible);
            leftZoneWidth    = lay->getIntAttribute("leftZoneWidth",   leftZoneWidth);
            rightZoneWidth   = lay->getIntAttribute("rightZoneWidth",  rightZoneWidth);
            splitRatio       = std::clamp(splitRatio, 0.15f, 0.85f);
            viewLayout       = std::clamp(viewLayout, 0, 3);
            leftZoneWidth    = std::clamp(leftZoneWidth, 160, 500);
            rightZoneWidth   = std::clamp(rightZoneWidth, 160, 500);
        }

        if (auto* fw = xml->getChildByName("FloatingKeyEditor"))
        {
            floatWinX           = fw->getIntAttribute("x",           floatWinX);
            floatWinY           = fw->getIntAttribute("y",           floatWinY);
            floatWinW           = fw->getIntAttribute("width",       floatWinW);
            floatWinH           = fw->getIntAttribute("height",      floatWinH);
            floatWinAlwaysOnTop = fw->getBoolAttribute("alwaysOnTop", floatWinAlwaysOnTop);
            floatWinWasOpen     = fw->getBoolAttribute("wasOpen",     false);
            if (floatWinW < 700 || floatWinH < 450 || (floatWinX < 50 && floatWinY < 50))
            {
                floatWinW = 1000;
                floatWinH = 620;
                floatWinX = 200;
                floatWinY = 120;
            }
        }

        if (auto* ws = xml->getChildByName("Workspace"))
        {
            inspectorStateValid = true;
            if (auto* sects = ws->getChildByName("Sections"))
            {
                for (auto* s : sects->getChildIterator())
                {
                    int idx = s->getIntAttribute("index", -1);
                    if (isInspectorSectionId(idx)
                        && (uiStateVersion >= 3 || idx != kQuickChordSectionId))
                    {
                        sectionPositions[idx][0] = s->getIntAttribute("position",          0);
                        sectionPositions[idx][1] = s->getIntAttribute("keyEditorPosition", 0);
                        sectionPositions[idx][2] = s->getIntAttribute("defaultPosition",   0);
                    }
                }
            }
            if (auto* ord = ws->getChildByName("SectionOrder"))
            {
                std::array<bool, kInspectorSectionCount> seen{};
                int i = 0;
                for (auto* o : ord->getChildIterator())
                {
                    const int id = o->getIntAttribute("section", -1);
                    if (isInspectorSectionId(id)
                        && (uiStateVersion >= 3 || id != kQuickChordSectionId)
                        && !seen[(size_t)id])
                    {
                        sectionOrder[i++] = id;
                        seen[(size_t)id] = true;
                    }
                }
                // Preserve the saved order and append sections introduced later.
                for (int id : kDefaultSectionOrder)
                    if (!seen[(size_t)id])
                        sectionOrder[i++] = id;
                if (uiStateVersion < 3)
                {
                    const auto quick = std::find(sectionOrder.begin(), sectionOrder.end(),
                                                 kQuickChordSectionId);
                    const auto predecessor = std::find(sectionOrder.begin(), sectionOrder.end(),
                                                       kModeBorrowingSectionId);
                    if (quick != sectionOrder.end() && predecessor != sectionOrder.end())
                    {
                        auto target = predecessor + 1;
                        if (quick > target)
                            std::rotate(target, quick, quick + 1);
                        else if (quick < target)
                            std::rotate(quick, quick + 1, target);
                    }
                }
            }
        }

        if (auto* ta = xml->getChildByName("TrackArranger"))
        {
            arrangerPixelsPerBeat = (float)ta->getDoubleAttribute("pixelsPerBeat", (double)arrangerPixelsPerBeat);
            arrangerScrollBeats   = ta->getDoubleAttribute("scrollBeats",    arrangerScrollBeats);
            arrangerTrackHeight   = ta->getIntAttribute("trackHeight",       arrangerTrackHeight);
            arrangerPixelsPerBeat = std::clamp(arrangerPixelsPerBeat, 2.0f, 500.0f);
            arrangerScrollBeats   = std::max(0.0, arrangerScrollBeats);
            arrangerTrackHeight   = std::clamp(arrangerTrackHeight, 26, 120);
        }

        if (auto* pr = xml->getChildByName("PianoRoll"))
        {
            pianoRollPixelsPerBeat = pr->getDoubleAttribute("pixelsPerBeat", pianoRollPixelsPerBeat);
            pianoRollNoteHeight    = (float)pr->getDoubleAttribute("noteHeight", (double)pianoRollNoteHeight);
            pianoRollScrollX       = pr->getIntAttribute("scrollX", pianoRollScrollX);
            pianoRollScrollY       = pr->getIntAttribute("scrollY", pianoRollScrollY);
            pianoRollPixelsPerBeat = std::clamp(pianoRollPixelsPerBeat, 2.0, 500.0);
            pianoRollNoteHeight    = std::clamp(pianoRollNoteHeight, 5.0f, 48.0f);
            pianoRollScrollX       = std::max(0, pianoRollScrollX);
            pianoRollScrollY       = std::max(0, pianoRollScrollY);
        }

        if (auto* es = xml->getChildByName("EditSettings"))
        {
            snapEnabled  = es->getBoolAttribute("snapEnabled",  snapEnabled);
            snapDivision = es->getIntAttribute("snapDivision",  snapDivision);
            snapTriplet  = es->getBoolAttribute("snapTriplet",  snapTriplet);
            followMode   = es->getIntAttribute("followMode",    followMode);
            lastEnabledFollowMode = es->getIntAttribute("lastEnabledFollowMode", lastEnabledFollowMode);
            snapDivision = std::clamp(snapDivision, 0, 6);
            followMode   = std::clamp(followMode, 0, 3);
            lastEnabledFollowMode = std::clamp(lastEnabledFollowMode, 1, 3);
        }

        if (auto* lp = xml->getChildByName("Loop"))
        {
            loopEnabled = lp->getBoolAttribute("enabled", loopEnabled);
            loopStart   = lp->getDoubleAttribute("start", loopStart);
            loopEnd     = lp->getDoubleAttribute("end",   loopEnd);
        }

        if (auto* ms = xml->getChildByName("ModuleSettings"))
        {
            if (auto* ca = ms->getChildByName("ChordAnalysis"))
            {
                chordAnalysisDisplayMode = ca->getIntAttribute("displayMode", chordAnalysisDisplayMode);
                chordAnalysisFontSize    = ca->getIntAttribute("fontSize",    chordAnalysisFontSize);
                chordAnalysisOrientation = ca->getIntAttribute("orientation", chordAnalysisOrientation);
                chordAnalysisVisualStyle = ca->getIntAttribute("visualStyle", chordAnalysisVisualStyle);

                int fallbackPt = (chordAnalysisFontSize == 1) ? 15 : ((chordAnalysisFontSize == 2) ? 22 : 18);
                chordAnalysisFontSizePt  = ca->getIntAttribute("fontSizePt",  fallbackPt);

                chordAnalysisDisplayMode = std::clamp(chordAnalysisDisplayMode, 0, 2);
                chordAnalysisFontSize    = std::clamp(chordAnalysisFontSize, 0, 2);
                chordAnalysisFontSizePt  = std::clamp(chordAnalysisFontSizePt, 10, 48);
                chordAnalysisOrientation = std::clamp(chordAnalysisOrientation, 0, 1);
                chordAnalysisVisualStyle = std::clamp(chordAnalysisVisualStyle, 0, 7);
            }
            if (auto* ni = ms->getChildByName("NoteInspector"))
            {
                notePropertyFontSizePt = ni->getIntAttribute("fontSizePt", notePropertyFontSizePt);
                notePropertyFontSizePt = std::clamp(notePropertyFontSizePt, 9, 24);
                noteShowPitch          = ni->getBoolAttribute("showPitch",          noteShowPitch);
                noteShowStart          = ni->getBoolAttribute("showStart",          noteShowStart);
                noteShowLength         = ni->getBoolAttribute("showLength",         noteShowLength);
                noteShowVelocity       = ni->getBoolAttribute("showVelocity",       noteShowVelocity);
                noteShowScaleDegree    = ni->getBoolAttribute("showScaleDegree",    noteShowScaleDegree);
                noteShowFunctionalRole = ni->getBoolAttribute("showFunctionalRole",
                                         ni->getBoolAttribute("showScaleFunction", noteShowFunctionalRole));
                noteShowMotionTendency = ni->getBoolAttribute("showMotionTendency",
                                         ni->getBoolAttribute("showTendency", noteShowMotionTendency));
                noteShowColorUsage     = ni->getBoolAttribute("showColorUsage",
                                         ni->getBoolAttribute("showColorTone", noteShowColorUsage));
                noteShowInterval       = ni->getBoolAttribute("showInterval",       noteShowInterval);
            }
            if (auto* cs = ms->getChildByName("ChordShape"))
                showChordStackDirections = cs->getBoolAttribute(
                    "showStackDirections", showChordStackDirections);
            if (auto* qc = ms->getChildByName("QuickChord"))
            {
                quickChordComplexMode = qc->getBoolAttribute("complexMode", quickChordComplexMode);
                quickChordPreserveSelectedTiming = qc->getBoolAttribute(
                    "preserveSelectedTiming", quickChordPreserveSelectedTiming);
            }
            if (auto* sd = ms->getChildByName("SecondaryDominant"))
            {
                secondaryDominantTargetSource = std::clamp(
                    sd->getIntAttribute("targetSource", secondaryDominantTargetSource), 0, 3);
                secondaryDominantManualRootPc = std::clamp(
                    sd->getIntAttribute("manualRootPc", secondaryDominantManualRootPc), 0, 11);
                secondaryDominantFlatNine = sd->getBoolAttribute("flatNine", secondaryDominantFlatNine);
                secondaryDominantRootInBass = sd->getBoolAttribute("rootInBass", secondaryDominantRootInBass);
            }
            if (auto* rh = ms->getChildByName("Reharmonization"))
            {
                reharmonizationAlgorithm = std::clamp(
                    rh->getIntAttribute("algorithm", reharmonizationAlgorithm), 0, 6);
            }
            if (auto* mb = ms->getChildByName("ModeBorrowing"))
            {
                modeBorrowingStyle = std::clamp(mb->getIntAttribute("style", modeBorrowingStyle), 0, 1);
                modeBorrowingCustomSource = mb->getBoolAttribute("customSource", modeBorrowingCustomSource);
                modeBorrowingSourceRootPc = std::clamp(
                    mb->getIntAttribute("sourceRootPc", modeBorrowingSourceRootPc), 0, 11);
                modeBorrowingSourceScale = mb->getStringAttribute("sourceScale", modeBorrowingSourceScale);
                modeBorrowingTargetRootPc = std::clamp(
                    mb->getIntAttribute("targetRootPc", modeBorrowingTargetRootPc), 0, 11);
                modeBorrowingScale = mb->getStringAttribute("scale", modeBorrowingScale);
                modeBorrowingDegree = std::clamp(mb->getIntAttribute("degree", modeBorrowingDegree), -1, 6);
            }
        }

        mainWindowX = std::clamp(mainWindowX, -1000000, 1000000);
        mainWindowY = std::clamp(mainWindowY, -1000000, 1000000);
        mainWindowW = std::clamp(mainWindowW, 800, 3840);
        mainWindowH = std::clamp(mainWindowH, 500, 2160);
        floatWinX = std::clamp(floatWinX, -1000000, 1000000);
        floatWinY = std::clamp(floatWinY, -1000000, 1000000);
        floatWinW = std::clamp(floatWinW, 700, 3840);
        floatWinH = std::clamp(floatWinH, 450, 2160);
        if (!std::isfinite(splitRatio)) splitRatio = 0.38f;
        if (!std::isfinite(arrangerPixelsPerBeat)) arrangerPixelsPerBeat = 25.0f;
        if (!std::isfinite(arrangerScrollBeats)) arrangerScrollBeats = 0.0;
        if (!std::isfinite(pianoRollPixelsPerBeat)) pianoRollPixelsPerBeat = 40.0;
        if (!std::isfinite(pianoRollNoteHeight)) pianoRollNoteHeight = 16.0f;
        splitRatio = std::clamp(splitRatio, 0.15f, 0.85f);
        arrangerPixelsPerBeat = std::clamp(arrangerPixelsPerBeat, 2.0f, 500.0f);
        arrangerScrollBeats = std::clamp(arrangerScrollBeats, 0.0, 1000000.0);
        pianoRollPixelsPerBeat = std::clamp(pianoRollPixelsPerBeat, 2.0, 500.0);
        pianoRollNoteHeight = std::clamp(pianoRollNoteHeight, 5.0f, 48.0f);
        if (!std::isfinite(loopStart) || !std::isfinite(loopEnd)
            || loopStart < 0.0 || loopEnd <= loopStart || loopEnd > 1000000.0)
        {
            loopStart = 0.0;
            loopEnd = 16.0;
        }
        std::array<bool, kInspectorSectionCount> validOrder{};
        bool orderOk = true;
        for (int i = 0; i < kInspectorSectionCount; ++i)
        {
            for (int context = 0; context < 3; ++context)
                sectionPositions[i][context] = std::clamp(sectionPositions[i][context], 0, 2);
            int id = sectionOrder[i];
            if (!isInspectorSectionId(id) || validOrder[(size_t)id])
                orderOk = false;
            else
                validOrder[(size_t)id] = true;
        }
        if (!orderOk)
            sectionOrder = kDefaultSectionOrder;
    }

    static juce::File getAppDataDir()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("HarmonizerStudio");
    }

private:
    AppStateManager()
    {
        for (int context = 0; context < 3; ++context)
        {
            sectionPositions[kReharmonizationSectionId][context] = 1;
            sectionPositions[kChordShapeSectionId][context] = 1;
            sectionPositions[kModeBorrowingSectionId][context] = 1;
            sectionPositions[kQuickChordSectionId][context] = 1;
        }
    }
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppStateManager)
};

} // namespace Harmonizer
