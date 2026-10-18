#pragma once

#include "QuickChord.h"
#include "QuickChordCatalog.h"
#include "TrackModel.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <vector>

namespace Harmonizer
{

inline int runQuickChordSelfTests()
{
    const auto logFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                             .getSiblingFile("quick_chord_tests.log");
    logFile.deleteFile();
    int failures = 0;
    auto check = [&](const char* name, bool passed) {
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", name);
        logFile.appendText(juce::String(passed ? "[PASS] " : "[FAIL] ") + name + "\n");
        if (!passed) ++failures;
    };
    auto note = [](int64_t id, int pitch, double beat, double length,
                   bool selected = true) {
        NoteEvent n;
        n.id = id;
        n.pitch = pitch;
        n.startBeat = beat;
        n.lengthBeats = length;
        n.velocity = 0.63f;
        n.isSelected = selected;
        return n;
    };
    auto pitches = [](const std::vector<NoteEvent>& notes) {
        std::vector<int> result;
        for (const auto& n : notes) result.push_back(n.pitch);
        return result;
    };

    const auto* major = QuickChordCatalog::find(1);
    const auto* minor = QuickChordCatalog::find(2);
    const auto* minor7 = QuickChordCatalog::find(62);
    const auto* major13 = QuickChordCatalog::find(90);
    check("catalog exposes stable representative chord IDs and compound intervals",
          major != nullptr && major->semitoneIntervals == std::vector<int>({ 0, 4, 7 })
          && minor != nullptr && minor->semitoneIntervals == std::vector<int>({ 0, 3, 7 })
          && minor7 != nullptr && minor7->semitoneIntervals == std::vector<int>({ 0, 3, 7, 10 })
          && major13 != nullptr && major13->semitoneIntervals == std::vector<int>({ 0, 4, 7, 11, 14, 21 }));
    const auto simple = QuickChordCatalog::visible(false);
    const auto complex = QuickChordCatalog::visible(true);
    const auto simpleContains = [&](int id) {
        return std::any_of(simple.begin(), simple.end(),
            [id](const QuickChordType* type) { return type->id == id; });
    };
    std::set<int> uniqueIds;
    bool validFormulas = !simple.empty() && complex.size() > simple.size();
    for (const auto& type : QuickChordCatalog::all())
    {
        uniqueIds.insert(type.id);
        validFormulas &= !type.semitoneIntervals.empty() && type.semitoneIntervals.front() == 0
            && std::adjacent_find(type.semitoneIntervals.begin(), type.semitoneIntervals.end(),
                [](int a, int b) { return a >= b; }) == type.semitoneIntervals.end();
    }
    check("catalog has unique IDs, rising voicings, and a smaller simple mode",
          validFormulas && uniqueIds.size() == complex.size() && simple.size() >= 15
          && complex.size() >= 60 && QuickChordCatalog::find(-1) == nullptr
          && simpleContains(1) && simpleContains(2) && simpleContains(3)
          && simpleContains(4) && simpleContains(5) && simpleContains(6)
          && simpleContains(60) && simpleContains(61) && simpleContains(62)
          && simpleContains(64));
    if (major == nullptr || minor == nullptr || minor7 == nullptr)
        return failures + 1;

    MidiModel model;
    TrackModel tracks;
    model.clearAllNotes();
    const auto empty = QuickChord::prepare(model, tracks, *minor7);
    bool c4Preview = empty.succeeded() && empty.requiresPlacement
        && empty.selectedUpperIdsToRemove.empty()
        && pitches(empty.generated) == std::vector<int>({ 60, 63, 67, 70 })
        && model.getNotes().empty();
    for (const auto& n : empty.generated)
        c4Preview &= n.id == 0 && n.startBeat == 0.0 && n.lengthBeats == 1.0
            && n.isSelected;
    check("empty selection prepares a complete C4 chord for mouse placement", c4Preview);

    model.setAllNotes({ note(11, 60, 2.5, 0.75) });
    const auto single = QuickChord::prepare(model, tracks, *major);
    bool singleKeepsRoot = single.succeeded() && !single.requiresPlacement
        && single.selectedUpperIdsToRemove.empty()
        && pitches(single.generated) == std::vector<int>({ 64, 67 })
        && model.getNotes().size() == 1 && model.getNotes()[0].id == 11
        && model.getNotes()[0].pitch == 60;
    for (const auto& n : single.generated)
        singleKeepsRoot &= n.id == 0 && n.startBeat == 2.5
            && n.lengthBeats == 0.75 && n.velocity == 0.63f && n.isSelected;
    check("one selected note stays as root and receives matching upper voices", singleKeepsRoot);

    // The input order is intentionally inverted: the lowest pitch must win.
    model.setAllNotes({ note(21, 67, 4.0, 1.5), note(22, 60, 4.0, 1.5),
                        note(23, 64, 4.0, 1.5), note(24, 72, 4.0, 1.5, false) });
    const auto block = QuickChord::prepare(model, tracks, *minor7);
    const std::set<int64_t> replaced(block.selectedUpperIdsToRemove.begin(),
                                     block.selectedUpperIdsToRemove.end());
    bool replacesOnlySelectedUpperVoices = block.succeeded() && !block.requiresPlacement
        && pitches(block.generated) == std::vector<int>({ 63, 67, 70 })
        && replaced == std::set<int64_t>({ 21, 23 })
        && model.getNotes().size() == 4 && model.getNotes()[1].pitch == 60
        && model.getNotes()[3].id == 24;
    for (const auto& n : block.generated)
        replacesOnlySelectedUpperVoices &= n.startBeat == 4.0 && n.lengthBeats == 1.5;
    check("selected block uses lowest pitch as root and replaces selected upper IDs",
          replacesOnlySelectedUpperVoices);

    model.setAllNotes({ note(31, 60, 1.0, 0.5), note(32, 64, 1.0, 0.5),
                        note(33, 62, 5.0, 1.25), note(34, 69, 5.0, 1.25) });
    const auto positions = QuickChord::prepare(model, tracks, *minor);
    const std::set<int64_t> removedAcrossPositions(positions.selectedUpperIdsToRemove.begin(),
        positions.selectedUpperIdsToRemove.end());
    check("cross-time selection keeps each surviving upper voice's timing",
          positions.succeeded() && !positions.requiresPlacement
          && pitches(positions.generated) == std::vector<int>({ 63, 67 })
          && removedAcrossPositions == std::set<int64_t>({ 32, 33, 34 })
          && positions.generated[0].startBeat == 5.0
          && positions.generated[1].startBeat == 1.0
          && positions.generated[0].lengthBeats == 1.25
          && positions.generated[1].lengthBeats == 0.5);

    const auto stackedPositions = QuickChord::prepare(model, tracks, *minor, false);
    check("timing option off retains root-aligned block chords",
          stackedPositions.succeeded()
          && pitches(stackedPositions.generated) == std::vector<int>({ 63, 67 })
          && stackedPositions.generated[0].startBeat == 1.0
          && stackedPositions.generated[1].startBeat == 1.0
          && stackedPositions.generated[0].lengthBeats == 0.5
          && stackedPositions.generated[1].lengthBeats == 0.5);

    // The root appears last in this arpeggiated selection. Existing upper
    // voices retain their rhythm; the added seventh uses the root's timing.
    auto firstUpper = note(35, 64, 2.0, 0.5);
    firstUpper.velocity = 0.31f;
    auto secondUpper = note(36, 67, 2.5, 0.75);
    secondUpper.velocity = 0.79f;
    model.setAllNotes({ firstUpper, secondUpper, note(37, 60, 3.0, 1.25) });
    const auto arpeggio = QuickChord::prepare(model, tracks, *minor7);
    const std::set<int64_t> arpeggioRemoved(arpeggio.selectedUpperIdsToRemove.begin(),
        arpeggio.selectedUpperIdsToRemove.end());
    bool arpeggioKeepsTiming = arpeggio.succeeded() && !arpeggio.requiresPlacement
        && pitches(arpeggio.generated) == std::vector<int>({ 63, 67, 70 })
        && arpeggioRemoved == std::set<int64_t>({ 35, 36 });
    if (arpeggioKeepsTiming)
        arpeggioKeepsTiming = arpeggio.generated[0].startBeat == 2.0
            && arpeggio.generated[0].lengthBeats == 0.5
            && arpeggio.generated[0].velocity == 0.31f
            && arpeggio.generated[1].startBeat == 2.5
            && arpeggio.generated[1].lengthBeats == 0.75
            && arpeggio.generated[1].velocity == 0.79f
            && arpeggio.generated[2].startBeat == 3.0
            && arpeggio.generated[2].lengthBeats == 1.25
            && arpeggio.generated[2].velocity == 0.63f;
    check("arpeggio preserves upper note rhythm and times added tones at the root",
          arpeggioKeepsTiming);

    auto* activeTrack = tracks.getTrackById(tracks.getActiveTrackId());
    activeTrack->setLocked(true);
    const auto locked = QuickChord::prepare(model, tracks, *major);
    check("locked instrument track rejects chord generation without edits",
          !locked.succeeded() && locked.generated.empty()
          && locked.selectedUpperIdsToRemove.empty() && locked.error.isNotEmpty());
    activeTrack->setLocked(false);

    model.setAllNotes({ note(41, 121, 2.0, 1.0) });
    const auto tooHigh = QuickChord::prepare(model, tracks, *major);
    check("high root rejects an incomplete chord beyond MIDI pitch 127",
          !tooHigh.succeeded() && tooHigh.generated.empty()
          && tooHigh.selectedUpperIdsToRemove.empty() && tooHigh.error.isNotEmpty()
          && model.getNotes()[0].pitch == 121);

    model.setAllNotes({ note(51, 60, 15.75, 1.0) });
    const auto atClipEnd = QuickChord::prepare(model, tracks, *major);
    check("generated upper notes stop at the current MIDI clip boundary",
          atClipEnd.succeeded() && atClipEnd.generated.size() == 2
          && atClipEnd.generated[0].startBeat == 15.75
          && atClipEnd.generated[0].lengthBeats == 0.25);

    model.setAllNotes({ note(61, 60, 15.0, 1.0), note(62, 64, 15.75, 1.0) });
    const auto staggeredAtClipEnd = QuickChord::prepare(model, tracks, *major);
    check("preserved upper timing is clipped at the MIDI clip boundary",
          staggeredAtClipEnd.succeeded() && staggeredAtClipEnd.generated.size() == 2
          && staggeredAtClipEnd.generated[0].startBeat == 15.75
          && staggeredAtClipEnd.generated[0].lengthBeats == 0.25
          && staggeredAtClipEnd.generated[1].startBeat == 15.0
          && staggeredAtClipEnd.generated[1].lengthBeats == 1.0);

    std::printf("Quick chord checks: %s\n", failures == 0 ? "all passed" : "failed");
    return failures;
}

} // namespace Harmonizer
