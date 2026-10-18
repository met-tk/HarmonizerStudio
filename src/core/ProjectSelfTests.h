#pragma once

#include "ProjectFile.h"
#include "TrackModel.h"
#include "UndoActions.h"
#include "TimelineSnapping.h"
#include "../audio/AudioEngine.h"
#include "../ui/PianoRollComponent.h"
#include "../ui/TrackArrangerComponent.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

namespace Harmonizer
{

inline int runProjectSelfTests()
{
    struct TemporaryFiles
    {
        std::vector<juce::File> files;
        juce::File make(const juce::String& extension)
        {
            auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getNonexistentChildFile("harmonizer-project-test", extension, false);
            files.push_back(file);
            return file;
        }
        ~TemporaryFiles()
        {
            for (const auto& file : files)
                file.deleteFile();
        }
    } temporaryFiles;

    const auto logFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                             .getSiblingFile("project_tests.log");
    logFile.deleteFile();
    int failures = 0;
    auto check = [&](const char* name, bool passed) {
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", name);
        logFile.appendText(juce::String(passed ? "[PASS] " : "[FAIL] ") + name + "\n");
        if (!passed) ++failures;
    };
    auto approx = [](double a, double b) { return std::abs(a - b) < 0.0001; };

    {
        MidiModel clickMidi;
        AudioEngine clickEngine(clickMidi, false);
        clickEngine.audioDeviceAboutToStart(nullptr);
        clickEngine.setMetronomeEnabled(true, false);
        clickEngine.setMetronomeSound(MetronomeSound::Classic, false);
        juce::AudioIODeviceCallbackContext context;
        float left[512] = {}, right[512] = {};
        float* outputs[] = { left, right };
        auto render = [&](double beat) {
            clickEngine.setPlayheadBeat(beat);
            clickEngine.play();
            clickEngine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 512, context);
            double level = 0.0;
            for (float sample : left) level += std::abs(sample);
            return level;
        };
        const double accent = render(0.0);
        const double between = render(0.25);
        const double regular = render(1.0);
        clickEngine.setMetronomeSound(MetronomeSound::Electronic, false);
        const double electronic = render(0.0);
        check("metronome sounds on beat, accents bar start and stays silent between beats",
              accent > regular * 1.25 && regular > 0.1 && between < 0.001
              && electronic > 0.1 && !approx(electronic, accent));
        TrackModel clickTracks;
        if (auto* meter = clickTracks.getTimeSignatureTrack())
        {
            meter->getTimeSignatures().front().numerator = 3;
            meter->getTimeSignatures().push_back({ 99123, 4.0, 8.0, 5, 8 });
        }
        clickEngine.setTrackModel(&clickTracks);
        const double threeFourAccent = render(3.0);
        const double threeFourRegular = render(2.0);
        const double fiveEightAccent = render(4.0);
        const double fiveEightRegular = render(4.5);
        check("metronome follows time-signature accents and eighth-note beats",
              threeFourAccent > threeFourRegular * 1.25
              && fiveEightAccent > fiveEightRegular * 1.25
              && fiveEightRegular > 0.1);
        clickEngine.stop();
        clickEngine.setTrackModel(nullptr);
    }

    {
        MidiModel previewMidi;
        TrackModel previewTracks;
        AudioEngine previewEngine(previewMidi, false);
        previewEngine.audioDeviceAboutToStart(nullptr);
        previewEngine.setTrackModel(&previewTracks);
        juce::AudioIODeviceCallbackContext context;
        float left[512] = {}, right[512] = {};
        float* outputs[] = { left, right };
        auto render = [&] {
            previewEngine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 512, context);
            double energy = 0.0;
            for (float sample : left) energy += sample * sample;
            return std::sqrt(energy / 512.0);
        };
        previewEngine.previewNoteOn(69, 0.5f, true);
        for (int i = 0; i < 32; ++i) render();
        const double initial = render();
        previewEngine.updatePreviewNoteVelocity(69, 0.1f);
        const double falling = render();
        for (int i = 0; i < 4; ++i) render();
        const double quiet = render();
        previewEngine.updatePreviewNoteVelocity(69, 0.9f);
        const double rising = render();
        for (int i = 0; i < 4; ++i) render();
        const double loud = render();
        check("dragging velocity changes a sustained preview smoothly in both directions",
              initial > 0.001 && quiet < initial * 0.3
              && falling < initial * 0.98 && falling > quiet * 1.2
              && loud > initial * 1.5 && rising > quiet * 1.2
              && rising < loud * 0.9);

        previewEngine.previewNoteOff(69);
        for (int i = 0; i < 40; ++i) render();
        previewEngine.previewNoteOn(69, 0.5f);
        for (int i = 0; i < 32; ++i) render();
        const double normalPreview = render();
        check("ordinary note previews keep their original volume after velocity dragging",
              normalPreview > initial * 0.9 && normalPreview < initial * 1.1);
        previewEngine.allPreviewNotesOff();
    }

    {
        MidiModel transportMidi;
        AudioEngine transportEngine(transportMidi, false);
        transportEngine.audioDeviceAboutToStart(nullptr);
        juce::AudioIODeviceCallbackContext context;
        float left[512] = {}, right[512] = {};
        float* outputs[] = { left, right };
        auto advance = [&] {
            transportEngine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 512, context);
        };
        transportEngine.setPausePlayheadMode(PausePlayheadMode::ReturnToPlaybackStart);
        transportEngine.setPlayheadBeat(7.0);
        transportEngine.play();
        advance();
        const bool advancedFromStart = transportEngine.getPlayheadBeat() > 7.0;
        transportEngine.pause();
        check("pause can return to this playback's starting cursor position",
              advancedFromStart && approx(transportEngine.getPlayheadBeat(), 7.0)
              && !transportEngine.isPlaying());

        transportEngine.setPausePlayheadMode(PausePlayheadMode::StayAtPause);
        transportEngine.play();
        advance();
        const double pausedBeat = transportEngine.getPlayheadBeat();
        transportEngine.pause();
        check("pause can leave the cursor where playback stopped",
              pausedBeat > 7.0 && approx(transportEngine.getPlayheadBeat(), pausedBeat));
        transportEngine.setPausePlayheadMode(PausePlayheadMode::ReturnToPlaybackStart);
        transportEngine.play();
        transportEngine.setPlayheadBeat(9.0);
        advance();
        transportEngine.pause();
        check("seeking during playback updates the return position",
              approx(transportEngine.getPlayheadBeat(), 9.0));
        transportEngine.stop();
        check("Stop still returns to the project start in either pause mode",
              approx(transportEngine.getPlayheadBeat(), 0.0));
    }

    check("ordinary drag snaps an off-grid part back to the timeline grid",
          approx(1.13 + snapDraggedItemDelta(1.13, 0.37, 0.25), 1.5)
          && approx(1.13 + snapDraggedItemDelta(1.13, -0.12, 0.25), 1.0)
          && approx(1.13 + clampDraggedItemDelta(1.13,
              snapDraggedItemDelta(1.13, 2.0, 0.25), 0.0, 0.8, 0.25), 1.75));

    {
        TrackModel longTimeline;
        const auto originalBars = longTimeline.getBarTimeline(64.0);
        const auto rangedBars = longTimeline.getBarTimelineRange(0.0, 64.0);
        bool sameEarlyBars = originalBars.size() == rangedBars.size();
        for (size_t i = 0; sameEarlyBars && i < originalBars.size(); ++i)
            sameEarlyBars = originalBars[i].barIndex == rangedBars[i].barIndex
                && approx(originalBars[i].startBeat, rangedBars[i].startBeat)
                && approx(originalBars[i].lengthBeats, rangedBars[i].lengthBeats);
        check("visible-range bar generation preserves the original early timeline", sameEarlyBars);
        auto bars = longTimeline.getBarTimelineRange(8192.0, 8212.0);
        check("distant measures keep their grid and absolute bar numbers",
              !bars.empty() && bars.front().barIndex == 2049
              && approx(bars.front().startBeat, 8192.0)
              && longTimeline.getBarAtBeat(8200.0).barIndex == 2051
              && approx(longTimeline.barToStartBeat(2049), 8192.0));
        auto* meterTrack = longTimeline.getTimeSignatureTrack();
        TimeSignatureEvent change;
        change.id = 9999;
        change.startBeat = 8193.0;
        change.numerator = 5;
        change.denominator = 4;
        meterTrack->getTimeSignatures().push_back(change);
        bars = longTimeline.getBarTimelineRange(8192.0, 8212.0);
        check("far time-signature changes preserve shortened transition bars",
              bars.size() >= 2 && approx(bars[0].lengthBeats, 1.0)
              && approx(bars[1].startBeat, 8193.0)
              && bars[1].barIndex == 2050 && bars[1].numerator == 5
              && approx(longTimeline.barToStartBeat(2050), 8193.0));
    }

    {
        MidiModel longMidi;
        TrackModel longTracks;
        auto* track = longTracks.getActiveInstrumentTrack();
        MidiClip latePart;
        latePart.id = 92001;
        latePart.startBeat = 1600.0;
        latePart.lengthBeats = 16.0;
        NoteEvent lateNote;
        lateNote.id = 92002;
        lateNote.startBeat = 1601.0;
        lateNote.pitch = 72;
        latePart.notes.push_back(lateNote);
        track->addClip(latePart);
        longTracks.triggerTracksChanged();
        AudioEngine longEngine(longMidi, false);
        longEngine.setTrackModel(&longTracks);
        juce::UndoManager longUndo;
        PianoRollComponent roll(longMidi, longTracks, longEngine, longUndo);
        roll.setBounds(0, 0, 1100, 620);
        const bool hasLatePart = roll.getCanvas().getTimelineExtentBeats() > 1616.0;
        roll.scrollToBeat(1600.0);
        const bool scrollsPastOldLimit = roll.getViewportScrollX() > 400 * 40;
        roll.zoomInHorizontal();
        const bool zoomKeepsPart = roll.getCanvas().getTimelineExtentBeats() > 1616.0;
        roll.scrollToBeat(3200.0);
        check("piano roll expands to distant MIDI parts and keeps them through zoom",
              hasLatePart && scrollsPastOldLimit && zoomKeepsPart
              && roll.getCanvas().getTimelineExtentBeats() > 3200.0);
    }

    std::string malformedUtf8(16, '\x80');
    malformedUtf8 += "trailing";
    auto repairedString = juce::String(juce::CharPointer_UTF8(malformedUtf8.c_str()));
    check("malformed UTF-8 string stays within a valid result",
          juce::CharPointer_UTF8::isValidString(repairedString.toRawUTF8(), std::numeric_limits<int>::max())
          && repairedString.endsWith("trailing"));

    MidiModel model;
    TrackModel tracks;
    auto* instrument = tracks.getActiveInstrumentTrack();
    auto* clip = tracks.getActiveClip();
    NoteEvent note;
    note.id = 3001;
    note.pitch = 64;
    note.startBeat = 9.0;
    note.lengthBeats = 2.0;
    clip->startBeat = 8.0;
    clip->notes.push_back(note);
    instrument->setVolume(0.45f);
    instrument->setPan(-0.25f);
    const char pluginBytes[] = { 1, 2, 3, 4 };
    juce::MemoryBlock pluginState(pluginBytes, sizeof(pluginBytes));
    instrument->setAssignedVstName("Kontakt");
    instrument->setAssignedVstIdentifier("test-vst-identifier");
    instrument->setAssignedVstState(pluginState);
    ModeEvent auxMode;
    auxMode.id = 4001;
    auxMode.tonic = "G";
    auxMode.tonicPc = 7;
    instrument->getAuxModes().push_back(auxMode);
    tracks.getModeTrack()->getModes().front().tonicPc = 5;
    auto* tempoTrack = tracks.addTrack("Tempo Track", TrackType::Tempo);
    TempoEvent firstTempo;
    firstTempo.id = 5001;
    firstTempo.bpm = 120.0;
    firstTempo.type = TempoTransitionType::Ramp;
    TempoEvent secondTempo;
    secondTempo.id = 5002;
    secondTempo.startBeat = 4.0;
    secondTempo.bpm = 180.0;
    tempoTrack->getTempoEvents() = { firstTempo, secondTempo };
    auto sampleFile = temporaryFiles.make(".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
            sampleFile.createOutputStream().release(), 48000.0, 2, 16, {}, 0));
        if (writer != nullptr)
        {
            juce::AudioBuffer<float> samples(2, 4800);
            for (int i = 0; i < samples.getNumSamples(); ++i)
                for (int ch = 0; ch < samples.getNumChannels(); ++ch)
                    samples.setSample(ch, i, (i < 2400 ? 0.08f : 0.7f)
                        * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0));
            writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples());
        }
    }
    auto* audioTrack = tracks.addTrack("Imported Audio", TrackType::Audio);
    audioTrack->setVolume(0.6f);
    audioTrack->setPan(0.3f);
    juce::UndoManager audioUndo;
    const bool importedAudio = tracks.importAudioClip(audioTrack->getId(), sampleFile, 2.0, 120.0, &audioUndo);
    check("audio import reads duration and timeline placement", importedAudio
          && audioTrack->getAudioClips().size() == 1
          && approx(audioTrack->getAudioClips().front().durationSeconds, 0.1)
          && approx(audioTrack->getAudioClips().front().startBeat, 2.0));
    const bool audioUndone = audioUndo.undo() && audioTrack->getAudioClips().empty();
    const bool audioRedone = audioUndo.redo() && audioTrack->getAudioClips().size() == 1;
    check("audio import undo and redo", audioUndone && audioRedone);
    auto replacementAudioFile = temporaryFiles.make(".wav");
    const bool replacementCopied = sampleFile.copyFileTo(replacementAudioFile);
    const int64_t importedAudioClipId = audioTrack->getAudioClips().empty()
        ? -1 : audioTrack->getAudioClips().front().id;
    const bool relinked = replacementCopied && importedAudioClipId >= 0
        && tracks.replaceAudioClipSource(audioTrack->getId(),
        importedAudioClipId, replacementAudioFile, 120.0, &audioUndo);
    const bool relinkPathChanged = relinked
        && audioTrack->getAudioClips().front().sourcePath == replacementAudioFile.getFullPathName();
    const bool relinkUndone = relinked && audioUndo.undo()
        && audioTrack->getAudioClips().front().sourcePath == sampleFile.getFullPathName();
    check("audio source relink is undoable", relinkPathChanged && relinkUndone);

    {
        TrackModel droppedAudioTracks;
        juce::UndoManager dropUndo;
        const auto initialCount = droppedAudioTracks.getTracks().size();
        const bool dropped = ProjectFile::importAudioAsClip(droppedAudioTracks,
            sampleFile, -1, 7.5, 120.0, &dropUndo);
        const auto* newAudioTrack = dropped ? droppedAudioTracks.getTracks().back().get() : nullptr;
        check("audio file drop creates a track and waveform part at its timeline position",
              newAudioTrack != nullptr && newAudioTrack->getType() == TrackType::Audio
              && newAudioTrack->getAudioClips().size() == 1
              && approx(newAudioTrack->getAudioClips().front().startBeat, 7.5));
        const bool undone = dropped && dropUndo.undo()
            && droppedAudioTracks.getTracks().size() == initialCount;
        const bool redone = undone && dropUndo.redo()
            && droppedAudioTracks.getTracks().size() == initialCount + 1
            && droppedAudioTracks.getTracks().back()->getAudioClips().size() == 1;
        check("new audio track and waveform part undo and redo together", undone && redone);

        const int occupiedTrackId = redone
            ? droppedAudioTracks.getTracks().back()->getId() : -1;
        const auto countBeforeOverlap = droppedAudioTracks.getTracks().size();
        const bool overlapImported = redone && ProjectFile::importAudioAsClip(
            droppedAudioTracks, sampleFile, occupiedTrackId, 7.5, 120.0, &dropUndo);
        check("overlapping audio drop creates a selectable separate track",
              overlapImported && droppedAudioTracks.getTracks().size() == countBeforeOverlap + 1
              && droppedAudioTracks.getTracks().back()->getType() == TrackType::Audio
              && droppedAudioTracks.getTracks().back()->getAudioClips().size() == 1);
        check("overlapping audio drop undo removes its new track",
              overlapImported && dropUndo.undo()
              && droppedAudioTracks.getTracks().size() == countBeforeOverlap);
        const bool sameTrackImported = ProjectFile::importAudioAsClip(
            droppedAudioTracks, sampleFile, occupiedTrackId, 8.0, 120.0, &dropUndo);
        const auto* occupiedTrack = droppedAudioTracks.getTrackById(occupiedTrackId);
        check("nonoverlapping audio drop stays on the selected audio track",
              sameTrackImported && droppedAudioTracks.getTracks().size() == countBeforeOverlap
              && occupiedTrack != nullptr && occupiedTrack->getAudioClips().size() == 2
              && approx(occupiedTrack->getAudioClips().back().startBeat, 8.0));
        if (sameTrackImported)
            dropUndo.undo();

        auto invalidAudio = temporaryFiles.make(".wav");
        invalidAudio.replaceWithText("not an audio file");
        const auto countBeforeFailure = droppedAudioTracks.getTracks().size();
        check("invalid dropped audio leaves no empty track",
              !ProjectFile::importAudioAsClip(droppedAudioTracks, invalidAudio,
                  -1, 0.0, 120.0, &dropUndo)
              && droppedAudioTracks.getTracks().size() == countBeforeFailure);
    }

    {
        TrackModel splitTracks;
        auto* splitInstrument = splitTracks.getActiveInstrumentTrack();
        auto* splitClip = splitTracks.getActiveClip();
        splitClip->startBeat = 8.0;
        splitClip->lengthBeats = 4.0;
        NoteEvent crossingNote;
        crossingNote.id = 8001;
        crossingNote.pitch = 60;
        crossingNote.startBeat = 9.0;
        crossingNote.lengthBeats = 2.0;
        NoteEvent rightNote = crossingNote;
        rightNote.id = 8002;
        rightNote.pitch = 67;
        rightNote.startBeat = 10.5;
        rightNote.lengthBeats = 0.5;
        splitClip->notes = { crossingNote, rightNote };
        juce::UndoManager splitUndo;
        const bool midiSplit = splitTracks.splitMidiClip(splitInstrument->getId(), splitClip->id,
                                                         10.0, &splitUndo);
        const auto& pieces = splitInstrument->getClips();
        bool separatedNotes = midiSplit && pieces.size() == 2;
        if (separatedNotes)
            separatedNotes = approx(pieces[0].lengthBeats, 2.0)
                && pieces[0].notes.size() == 1
                && approx(pieces[0].notes[0].lengthBeats, 1.0)
                && approx(pieces[1].startBeat, 10.0)
                && pieces[1].notes.size() == 2
                && pieces[1].notes[0].id != pieces[0].notes[0].id
                && approx(pieces[1].notes[0].startBeat, 10.0)
                && approx(pieces[1].notes[0].lengthBeats, 1.0)
                && approx(pieces[1].notes[1].startBeat, 10.5);
        check("MIDI split separates crossing notes at the absolute beat", separatedNotes);
        check("MIDI split undo and redo", midiSplit && splitUndo.undo()
              && splitInstrument->getClips().size() == 1
              && splitUndo.redo() && splitInstrument->getClips().size() == 2);
    }

    {
        MidiModel splitMidi;
        TrackModel splitTracks;
        auto* splitAudio = splitTracks.addTrack("Split Audio", TrackType::Audio);
        juce::UndoManager splitUndo;
        const bool imported = splitTracks.importAudioClip(splitAudio->getId(), sampleFile,
                                                           0.0, 120.0);
        const int64_t sourceId = imported ? splitAudio->getAudioClips().front().id : -1;
        const bool divided = imported && splitTracks.splitAudioClip(splitAudio->getId(),
                                                sourceId, 0.1, 120.0, &splitUndo);
        const auto& pieces = splitAudio->getAudioClips();
        check("audio split retains the second segment source offset", divided
              && pieces.size() == 2
              && approx(pieces[0].durationSeconds, 0.05)
              && approx(pieces[1].startBeat, 0.1)
              && approx(pieces[1].sourceOffsetSeconds, 0.05)
              && approx(pieces[1].durationSeconds, 0.05));
        check("audio split undo and redo", divided && splitUndo.undo()
              && splitAudio->getAudioClips().size() == 1
              && splitUndo.redo() && splitAudio->getAudioClips().size() == 2);

        auto splitProject = temporaryFiles.make(".reharm");
        TrackModel restoredTracks;
        MidiModel restoredMidi;
        const bool restored = divided
            && ProjectFile::saveProject(splitMidi, splitProject, &splitTracks)
            && ProjectFile::loadProject(restoredMidi, splitProject, &restoredTracks);
        const auto* restoredAudio = restoredTracks.getTrackById(splitAudio->getId());
        check("audio source offset survives project reopen", restored
              && restoredAudio != nullptr && restoredAudio->getAudioClips().size() == 2
              && approx(restoredAudio->getAudioClips()[1].sourceOffsetSeconds, 0.05));

        if (divided)
        {
            AudioEngine splitEngine(splitMidi, false);
            splitEngine.setTrackModel(&splitTracks);
            splitEngine.audioDeviceAboutToStart(nullptr);
            juce::AudioIODeviceCallbackContext context;
            float left[512] = {}, right[512] = {};
            float* outputs[] = { left, right };
            auto measure = [&](double beat) {
                splitEngine.setPlayheadBeat(beat);
                juce::Thread::sleep(30);
                splitEngine.play();
                double level = 0.0;
                for (int block = 0; block < 4; ++block)
                {
                    splitEngine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 512, context);
                    if (block >= 1)
                        for (float value : left) level += std::abs(value);
                    juce::Thread::sleep(5);
                }
                splitEngine.stop();
                return level;
            };
            const double firstHalf = measure(0.0);
            const double secondHalf = measure(0.1);
            check("audio split plays the correct source segment", firstHalf > 0.1
                  && secondHalf > firstHalf * 3.0);
        }
    }

    TrackModel tempoAudioTracks;
    auto* tempoForAudio = tempoAudioTracks.addTrack("Tempo", TrackType::Tempo);
    TempoEvent tempoAtZero;
    tempoAtZero.id = 7101;
    tempoAtZero.bpm = 120.0;
    TempoEvent tempoAtFour;
    tempoAtFour.id = 7102;
    tempoAtFour.startBeat = 4.0;
    tempoAtFour.bpm = 60.0;
    tempoForAudio->getTempoEvents() = { tempoAtZero, tempoAtFour };
    tempoAudioTracks.triggerTempoChanged();
    auto* crossingAudio = tempoAudioTracks.addTrack("Crossing tempo", TrackType::Audio);
    const bool crossingImported = tempoAudioTracks.importAudioClip(crossingAudio->getId(),
                                                                    sampleFile, 3.9, 120.0);
    check("audio timing follows tempo changes", crossingImported
          && approx(tempoAudioTracks.getSecondsBetweenBeats(0.0, 6.0), 4.0)
          && approx(crossingAudio->getAudioClips().front().lengthBeats, 0.15));
    if (crossingImported)
    {
        crossingAudio->getAudioClips().front().startBeat = 4.0;
        tempoAudioTracks.triggerTracksChanged();
    }
    check("moving audio updates its end across tempo regions", crossingImported
          && approx(crossingAudio->getAudioClips().front().lengthBeats, 0.1));
    tempoForAudio->getTempoEvents()[1].bpm = 120.0;
    tempoAudioTracks.triggerTempoChanged();
    check("audio block length refreshes after tempo edit",
          crossingImported && approx(crossingAudio->getAudioClips().front().lengthBeats, 0.2));

    {
        MidiModel fallbackModel;
        fallbackModel.setBpm(90.0);
        TrackModel fallbackTracks;
        auto* fallbackAudio = fallbackTracks.addTrack("No tempo track", TrackType::Audio);
        AudioEngine fallbackEngine(fallbackModel, false);
        fallbackEngine.setTrackModel(&fallbackTracks);
        const bool imported = fallbackTracks.importAudioClip(fallbackAudio->getId(), sampleFile,
                                                               0.0, fallbackModel.getBpm());
        check("audio timing uses project tempo without a tempo track", imported
              && approx(fallbackAudio->getAudioClips().front().lengthBeats, 0.15));
        fallbackModel.setBpm(60.0);
        check("audio timing responds to project BPM change", imported
              && approx(fallbackAudio->getAudioClips().front().lengthBeats, 0.1));
        auto fallbackProjectFile = temporaryFiles.make(".reharm");
        MidiModel restoredFallbackModel;
        TrackModel restoredFallbackTracks;
        const bool fallbackRestored = imported
            && ProjectFile::saveProject(fallbackModel, fallbackProjectFile, &fallbackTracks)
            && ProjectFile::loadProject(restoredFallbackModel, fallbackProjectFile,
                                        &restoredFallbackTracks);
        auto* restoredFallbackAudio = restoredFallbackTracks.getTrackById(fallbackAudio->getId());
        check("audio BPM survives project reopen without a tempo track", fallbackRestored
              && approx(restoredFallbackModel.getBpm(), 60.0)
              && restoredFallbackAudio != nullptr
              && restoredFallbackAudio->getAudioClips().size() == 1
              && approx(restoredFallbackAudio->getAudioClips().front().lengthBeats, 0.1));
    }

    {
        MidiModel offlineModel;
        TrackModel offlineTracks;
        auto* offlineAudio = offlineTracks.addTrack("Playback", TrackType::Audio);
        offlineAudio->setVolume(0.6f);
        offlineAudio->setPan(0.5f);
        const bool prepared = offlineTracks.importAudioClip(offlineAudio->getId(), sampleFile,
                                                              0.0, 120.0);
        AudioEngine offlineEngine(offlineModel, false);
        offlineEngine.setTrackModel(&offlineTracks);
        offlineEngine.audioDeviceAboutToStart(nullptr);
        juce::Thread::sleep(30); // Allow the read-ahead thread to fill its first block.
        offlineEngine.play();
        juce::AudioIODeviceCallbackContext context;
        float left[512] = {}, right[512] = {};
        float* outputs[] = { left, right };
        double leftLevel = 0.0, rightLevel = 0.0;
        for (int block = 0; block < 5; ++block)
        {
            offlineEngine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 512, context);
            for (int i = 0; i < 512; ++i)
            {
                leftLevel += std::abs(left[i]);
                rightLevel += std::abs(right[i]);
            }
            juce::Thread::sleep(5);
        }
        check("audio playback and track pan", prepared && rightLevel > 1.0
              && leftLevel > 0.1 && rightLevel > leftLevel * 1.5);
        offlineAudio->setMuted(true);
        offlineTracks.triggerTracksChanged();
        offlineEngine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 512, context);
        bool silent = true;
        for (int i = 0; i < 512; ++i)
            silent = silent && std::abs(left[i]) < 1.0e-5f && std::abs(right[i]) < 1.0e-5f;
        check("audio track mute is silent", silent);
        offlineEngine.stop();
    }
    tracks.getTimeSignatureTrack()->getTimeSignatures().push_back({ 6001, 8.0, 16.0, 3, 4 });
    tracks.setChordDisplayMode(ChordDisplayMode::RomanOnly);
    tracks.triggerTracksChanged();
    tracks.triggerTempoChanged();
    model.setScaleName("Dorian");
    model.setColoringMode(ColoringMode::DegreeFunction);
    model.setAllNotes(clip->notes);

    auto playback = tracks.getPlaybackSnapshot();
    check("absolute note position in playback snapshot", playback != nullptr && playback->size() == 1
          && approx(playback->front().startBeat, 9.0));
    check("ramped tempo snapshot", approx(tracks.getTempoAtBeat(2.0), 150.0));

    auto projectFile = temporaryFiles.make(".reharm");
    bool saved = ProjectFile::saveProject(model, projectFile, &tracks);
    MidiModel loadedModel;
    TrackModel loadedTracks;
    bool loaded = saved && ProjectFile::loadProject(loadedModel, projectFile, &loadedTracks);
    auto* loadedInstrument = loadedTracks.getTrackById(instrument->getId());
    auto* loadedAudio = loadedTracks.getTrackById(audioTrack->getId());
    auto* loadedTempo = loadedTracks.getTempoTrack();
    auto* loadedMode = loadedTracks.getModeTrack();
    check("project round trip", loaded && loadedInstrument != nullptr && loadedTempo != nullptr
          && loadedMode != nullptr && loadedModel.getScaleName() == "Dorian"
          && loadedModel.getColoringMode() == ColoringMode::DegreeFunction
          && approx(loadedInstrument->getVolume(), 0.45) && approx(loadedInstrument->getPan(), -0.25)
          && loadedInstrument->getAssignedVstIdentifier() == "test-vst-identifier"
          && loadedInstrument->getAssignedVstState() == pluginState
          && loadedInstrument->getAuxModes().size() == 1
          && loadedInstrument->getAuxModes().front().tonicPc == 7
          && loadedMode->getModes().front().tonicPc == 5
          && loadedTempo->getTempoEvents().size() == 2
          && approx(loadedTracks.getTempoAtBeat(2.0), 150.0)
          && loadedTracks.getChordDisplayMode() == ChordDisplayMode::RomanOnly);
    check("audio track project round trip", loadedAudio != nullptr
          && loadedAudio->getType() == TrackType::Audio
          && approx(loadedAudio->getVolume(), 0.6)
          && approx(loadedAudio->getPan(), 0.3)
          && loadedAudio->getAudioClips().size() == 1
          && loadedAudio->getAudioClips().front().sourcePath == sampleFile.getFullPathName()
          && approx(loadedAudio->getAudioClips().front().durationSeconds, 0.1));
    int maxTrackId = 0;
    for (const auto& track : loadedTracks.getTracks())
        maxTrackId = std::max(maxTrackId, track->getId());
    auto* addedTrack = loadedTracks.addTrack("After Load", TrackType::Instrument);
    check("track IDs remain unique after load", addedTrack != nullptr && addedTrack->getId() > maxTrackId);

    auto brokenFile = temporaryFiles.make(".reharm");
    brokenFile.replaceWithText("{\"version\":3,\"tracks\":\"invalid\"}");
    auto notesBefore = loadedModel.getNotes().size();
    auto tracksBefore = loadedTracks.getTracks().size();
    check("invalid project preserves current data",
          !ProjectFile::loadProject(loadedModel, brokenFile, &loadedTracks)
          && loadedModel.getNotes().size() == notesBefore
          && loadedTracks.getTracks().size() == tracksBefore);

    auto overflowFile = temporaryFiles.make(".reharm");
    overflowFile.replaceWithText("{\"version\":3,\"tracks\":[{\"id\":1,\"type\":0,"
                                 "\"clips\":[{\"id\":1e300,\"startBeat\":0,\"lengthBeats\":16}]}]}");
    check("out-of-range event ID is rejected without changing the project",
          !ProjectFile::loadProject(loadedModel, overflowFile, &loadedTracks)
          && loadedModel.getNotes().size() == notesBefore
          && loadedTracks.getTracks().size() == tracksBefore);

    auto maliciousStateFile = temporaryFiles.make(".reharm");
    maliciousStateFile.replaceWithText("{\"version\":3,\"tracks\":[{\"id\":1,\"type\":0,"
                                       "\"assignedVstState\":\"9999999999999999.\"}]}");
    check("invalid plugin state length is rejected without changing the project",
          !ProjectFile::loadProject(loadedModel, maliciousStateFile, &loadedTracks)
          && loadedModel.getNotes().size() == notesBefore
          && loadedTracks.getTracks().size() == tracksBefore);

    auto midiFile = temporaryFiles.make(".mid");
    bool exported = loaded && ProjectFile::exportMidi(loadedModel, midiFile, &loadedTracks, -1.0, -1.0);
    juce::FileInputStream midiStream(midiFile);
    juce::MidiFile exportedMidi;
    bool parsedExport = exported && midiStream.openedOk() && exportedMidi.readFrom(midiStream);
    bool foundTempoChange = false, foundSignatureChange = false, foundNoteAtNine = false;
    if (parsedExport)
        for (int t = 0; t < exportedMidi.getNumTracks(); ++t)
            for (int i = 0; i < exportedMidi.getTrack(t)->getNumEvents(); ++i)
            {
                const auto& message = exportedMidi.getTrack(t)->getEventPointer(i)->message;
                foundTempoChange |= message.isTempoMetaEvent() && approx(message.getTimeStamp(), 4.0 * 960.0);
                foundSignatureChange |= message.isTimeSignatureMetaEvent() && approx(message.getTimeStamp(), 8.0 * 960.0);
                foundNoteAtNine |= message.isNoteOn() && approx(message.getTimeStamp(), 9.0 * 960.0);
            }
    check("MIDI export keeps tempo and signature changes", parsedExport && foundTempoChange && foundSignatureChange);
    check("MIDI export uses absolute note positions", parsedExport && foundNoteAtNine);

    loadedInstrument->setMuted(true);
    loadedTracks.triggerTracksChanged();
    auto mutedFile = temporaryFiles.make(".mid");
    bool mutedExport = ProjectFile::exportMidi(loadedModel, mutedFile, &loadedTracks, -1.0, -1.0);
    juce::FileInputStream mutedStream(mutedFile);
    juce::MidiFile mutedMidi;
    bool parsedMuted = mutedExport && mutedStream.openedOk() && mutedMidi.readFrom(mutedStream);
    int noteOns = 0;
    if (parsedMuted)
        for (int t = 0; t < mutedMidi.getNumTracks(); ++t)
            for (int i = 0; i < mutedMidi.getTrack(t)->getNumEvents(); ++i)
                if (mutedMidi.getTrack(t)->getEventPointer(i)->message.isNoteOn()) ++noteOns;
    check("muted tracks stay silent in MIDI export", parsedMuted && noteOns == 0);

    juce::MidiFile channelMidi;
    channelMidi.setTicksPerQuarterNote(960);
    juce::MidiMessageSequence sequence;
    sequence.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    sequence.addEvent(juce::MidiMessage::noteOn(2, 60, (juce::uint8)100), 240);
    sequence.addEvent(juce::MidiMessage::noteOff(1, 60), 480);
    sequence.addEvent(juce::MidiMessage::noteOff(2, 60), 960);
    channelMidi.addTrack(sequence);
    auto channelFile = temporaryFiles.make(".mid");
    juce::FileOutputStream channelStream(channelFile);
    bool channelWritten = channelStream.openedOk() && channelMidi.writeTo(channelStream);
    channelStream.flush();
    MidiModel imported;
    bool channelImported = channelWritten && ProjectFile::importMidi(imported, channelFile);
    check("MIDI channels pair note offs independently", channelImported && imported.getNotes().size() == 2
          && approx(imported.getNotes()[0].lengthBeats, 0.5)
          && approx(imported.getNotes()[1].lengthBeats, 0.75));

    {
        MidiModel droppedModel;
        droppedModel.setBpm(137.0);
        TrackModel droppedTracks;
        juce::UndoManager dropUndo;
        const int targetId = droppedTracks.getActiveTrackId();
        auto* target = droppedTracks.getTrackById(targetId);
        const auto originalCount = target->getClips().size();
        const auto originalTrackCount = droppedTracks.getTracks().size();
        const bool dropped = ProjectFile::importMidiAsClips(droppedModel,
            channelFile, droppedTracks, targetId, 20.25, &dropUndo);
        const auto& clips = target->getClips();
        bool placed = dropped && clips.size() == originalCount + 1
            && approx(droppedModel.getBpm(), 137.0)
            && droppedTracks.getTracks().size() == originalTrackCount;
        if (placed)
        {
            const auto& part = clips.back();
            placed = approx(part.startBeat, 20.25) && part.notes.size() == 2
                && approx(part.notes[0].startBeat, 20.25)
                && approx(part.notes[1].startBeat, 20.5)
                && part.notes[0].id != part.notes[1].id;
        }
        check("MIDI drop adds a part at the target beat without changing the project", placed);
        const bool undone = dropped && dropUndo.undo()
            && target->getClips().size() == originalCount;
        const bool redone = undone && dropUndo.redo()
            && target->getClips().size() == originalCount + 1;
        check("dropped MIDI part undo and redo", undone && redone);

        const auto beforeOverlapCount = droppedTracks.getTracks().size();
        const auto beforeTargetCount = target->getClips().size();
        const bool overlapImported = ProjectFile::importMidiAsClips(droppedModel,
            channelFile, droppedTracks, targetId, 5.25, &dropUndo);
        const auto* overlapTrack = overlapImported ? droppedTracks.getTracks().back().get() : nullptr;
        check("overlapping MIDI drop creates a usable separate track",
              overlapTrack != nullptr && overlapTrack->getType() == TrackType::Instrument
              && droppedTracks.getTracks().size() == beforeOverlapCount + 1
              && target->getClips().size() == beforeTargetCount
              && overlapTrack->getClips().size() == 1
              && approx(overlapTrack->getClips().front().startBeat, 5.25));
        check("overlapping MIDI drop undo removes its new track",
              overlapImported && dropUndo.undo()
              && droppedTracks.getTracks().size() == beforeOverlapCount);
    }

    {
        juce::MidiFile multiTrackMidi;
        multiTrackMidi.setTicksPerQuarterNote(960);
        for (int pitch : { 60, 67 })
        {
            juce::MidiMessageSequence part;
            part.addEvent(juce::MidiMessage::noteOn(1, pitch, (juce::uint8)100), 0);
            part.addEvent(juce::MidiMessage::noteOff(1, pitch), 960);
            multiTrackMidi.addTrack(part);
        }
        auto multiFile = temporaryFiles.make(".mid");
        juce::FileOutputStream multiStream(multiFile);
        const bool written = multiStream.openedOk() && multiTrackMidi.writeTo(multiStream);
        multiStream.flush();
        MidiModel multiModel;
        TrackModel multiTracks;
        juce::UndoManager multiUndo;
        const auto trackCount = multiTracks.getTracks().size();
        auto* preferred = multiTracks.getActiveInstrumentTrack();
        const auto partCount = preferred->getClips().size();
        const bool dropped = written && ProjectFile::importMidiAsClips(multiModel,
            multiFile, multiTracks, preferred->getId(), 20.0, &multiUndo);
        const bool placed = dropped && multiTracks.getTracks().size() == trackCount + 1
            && preferred->getClips().size() == partCount + 1
            && multiTracks.getTracks().back()->getClips().size() == 1
            && approx(multiTracks.getTracks().back()->getClips().front().startBeat, 20.0);
        check("multitrack MIDI drop keeps each source part on its own track", placed);
        const bool undone = dropped && multiUndo.undo()
            && multiTracks.getTracks().size() == trackCount
            && preferred->getClips().size() == partCount;
        const bool redone = undone && multiUndo.redo()
            && multiTracks.getTracks().size() == trackCount + 1
            && preferred->getClips().size() == partCount + 1;
        check("multitrack MIDI drop is one undo step", undone && redone);
    }

    juce::MidiFile smpteMidi;
    smpteMidi.setSmpteTimeFormat(25, 40);
    juce::MidiMessageSequence smpteSequence;
    smpteSequence.addEvent(juce::MidiMessage::tempoMetaEvent(500000), 0);
    smpteSequence.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    smpteSequence.addEvent(juce::MidiMessage::noteOff(1, 60), 1000);
    smpteMidi.addTrack(smpteSequence);
    auto smpteFile = temporaryFiles.make(".mid");
    juce::FileOutputStream smpteStream(smpteFile);
    bool smpteWritten = smpteStream.openedOk() && smpteMidi.writeTo(smpteStream);
    smpteStream.flush();
    MidiModel smpteImported;
    bool smpteLoaded = smpteWritten && ProjectFile::importMidi(smpteImported, smpteFile);
    check("SMPTE MIDI timing", smpteLoaded && smpteImported.getNotes().size() == 1
          && approx(smpteImported.getNotes().front().lengthBeats, 2.0));

    TrackModel duplicateModel;
    auto* sourceTrack = duplicateModel.getActiveInstrumentTrack();
    sourceTrack->setAssignedVstName("Kontakt");
    sourceTrack->setAssignedVstIdentifier("test-vst-identifier");
    sourceTrack->setAssignedVstState(pluginState);
    const int sourceTrackId = sourceTrack->getId();
    auto* duplicate = duplicateModel.duplicateTrack(sourceTrackId);
    const int duplicateId = duplicate != nullptr ? duplicate->getId() : -1;
    check("duplicated instrument tracks keep independent IDs and plugin assignments",
          duplicate != nullptr && duplicateId != sourceTrackId
          && duplicate->getAssignedVstIdentifier() == "test-vst-identifier"
          && duplicate->getAssignedVstState() == pluginState);
    duplicateModel.removeTrack(duplicateId);
    check("removing a copied track does not change the active source track",
          duplicateModel.getActiveTrackId() == sourceTrackId);

    TrackModel focusTracks;
    MidiModel focusMidi;
    auto* sourceClip = focusTracks.getActiveClip();
    auto* focusTarget = focusTracks.addTrack("Second Instrument", TrackType::Instrument);
    MidiClip targetClip;
    targetClip.id = 9001;
    NoteEvent targetNote;
    targetNote.id = 9002;
    targetNote.pitch = 72;
    targetClip.notes.push_back(targetNote);
    focusTarget->addClip(targetClip);
    NoteEvent editedSourceNote;
    editedSourceNote.id = 9003;
    editedSourceNote.pitch = 60;
    focusMidi.setAllNotes({ editedSourceNote }, false);
    focusTracks.setActiveClip(focusTarget->getId(), targetClip.id, &focusMidi);
    check("switching instrument tracks preserves and displays each clip's own notes",
          sourceClip != nullptr && sourceClip->notes.size() == 1 && sourceClip->notes.front().pitch == 60
          && focusTracks.getActiveTrackId() == focusTarget->getId()
          && focusMidi.getNotes().size() == 1 && focusMidi.getNotes().front().pitch == 72);

    {
        MidiModel partMidi;
        TrackModel partTracks;
        auto* partTrack = partTracks.getActiveInstrumentTrack();
        auto* firstPart = partTracks.getActiveClip();
        const int64_t firstPartId = firstPart->id;
        NoteEvent firstNote;
        firstNote.id = 9301;
        firstNote.pitch = 60;
        firstNote.startBeat = 1.0;
        firstPart->notes = { firstNote };
        MidiClip secondPart;
        secondPart.id = 9302;
        secondPart.startBeat = 16.0;
        secondPart.lengthBeats = 8.0;
        NoteEvent secondNote = firstNote;
        secondNote.id = 9303;
        secondNote.pitch = 64;
        secondNote.startBeat = 18.0;
        secondPart.notes = { secondNote };
        partTrack->getClips().push_back(secondPart);
        partMidi.setAllNotes({ firstNote }, false);
        AudioEngine partAudio(partMidi, false);
        partAudio.setTrackModel(&partTracks);
        juce::UndoManager partUndo;
        PianoRollCanvas canvas(partMidi, partTracks, partAudio, partUndo);
        TrackArrangerComponent arranger(partTracks, partMidi, partAudio, partUndo, {});
        arranger.setBounds(0, 0, 900, 600);
        arranger.setVisible(true);
        const auto visibleTracks = partTracks.getVisibleTracks();
        int instrumentRow = -1;
        for (int i = 0; i < (int)visibleTracks.size(); ++i)
            if (visibleTracks[(size_t)i]->getId() == partTrack->getId())
                instrumentRow = i;
        int droppedTrackId = -1;
        double droppedBeat = -1.0;
        const bool hitInstrument = instrumentRow >= 0
            && arranger.getFileDropTarget({ 298, 24 + instrumentRow * 44 + 22 },
                                          droppedTrackId, droppedBeat);
        check("file drop chooses the arranger track and snapped time",
              hitInstrument && droppedTrackId == partTrack->getId()
              && approx(droppedBeat, partMidi.quantizeBeat(108.0 / 25.0)));
        const bool hitEmptySpace = arranger.getFileDropTarget(
            { 298, 24 + (int)visibleTracks.size() * 44 + 10 }, droppedTrackId, droppedBeat);
        check("file drop on empty arranger space keeps its time and creates a track",
              hitEmptySpace && droppedTrackId == -1
              && approx(droppedBeat, partMidi.quantizeBeat(108.0 / 25.0)));
        auto click = [&](double beat, int pitch) {
            const juce::Point<float> position(canvas.beatToX(beat),
                canvas.pitchToY(pitch) + canvas.getNoteHeight() * 0.5f);
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),
                position, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &canvas, &canvas,
                now, position, now, 1, false);
            canvas.mouseDown(event);
            canvas.mouseUp(event);
        };
        click(17.0, 60);
        check("clicking another MIDI part background changes piano-roll focus",
              partTracks.getActiveClipId() == secondPart.id
              && partMidi.getNotes().size() == 1
              && partMidi.getNotes().front().pitch == 64);
        click(1.2, 60);
        check("clicking a dimmed note changes MIDI part without an empty undo step",
              partTracks.getActiveClipId() == firstPartId
              && partMidi.getNotes().size() == 1
              && partMidi.getNotes().front().pitch == 60
              && !partUndo.canUndo());
        partMidi.setCurrentTool(ToolType::Draw);
        click(17.0, 60);
        check("draw tool first focuses another part without adding a note",
              partTracks.getActiveClipId() == secondPart.id
              && partTrack->getClips()[1].notes.size() == 1 && !partUndo.canUndo());
        partMidi.setCurrentTool(ToolType::Erase);
        click(1.2, 60);
        check("erase tool first focuses a dimmed note without deleting it",
              partTracks.getActiveClipId() == firstPartId
              && partTrack->getClips()[0].notes.size() == 1 && !partUndo.canUndo());
    }

    {
        MidiModel previewMidi;
        TrackModel previewTracks;
        AudioEngine previewAudio(previewMidi, false);
        previewAudio.setTrackModel(&previewTracks);
        juce::UndoManager previewUndo;
        PianoRollCanvas previewCanvas(previewMidi, previewTracks, previewAudio, previewUndo);
        NoteEvent note;
        note.id = 94001;
        note.pitch = 60;
        previewMidi.setAllNotes({ note }, true);
        previewMidi.getNotes().front().pitch = 62;
        previewMidi.triggerNotePreviewChanged(true);
        const bool stoppedPreviewDeferred = previewTracks.getPlaybackSnapshot()->front().pitch == 60
            && previewTracks.getActiveClip()->notes.front().pitch == 62;
        previewAudio.play();
        const bool playStartsWithLatestNote = previewTracks.getPlaybackSnapshot()->front().pitch == 62;
        previewMidi.getNotes().front().pitch = 64;
        previewMidi.triggerNotePreviewChanged(true);
        const auto position = juce::Point<float>(0.0f, 0.0f);
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent release(juce::Desktop::getInstance().getMainMouseSource(),
            position, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            &previewCanvas, &previewCanvas, now, position, now, 1, false);
        previewCanvas.mouseUp(release);
        const bool releaseFlushesPreview = previewTracks.getPlaybackSnapshot()->front().pitch == 64
            && previewTracks.getActiveClip()->notes.front().pitch == 64
            && previewMidi.getNotesSnapshot()->front().pitch == 64;
        previewAudio.pause();
        check("piano-roll preview batches snapshots and flushes on Play and mouse release",
              stoppedPreviewDeferred && playStartsWithLatestNote && releaseFlushesPreview);
    }

    TrackModel folderTracks;
    MidiModel folderMidi;
    auto* folderRoot = folderTracks.addTrack("Folder", TrackType::Folder);
    auto* folderChild = folderTracks.addTrack("Child", TrackType::Instrument);
    const int folderRootId = folderRoot->getId(), folderChildId = folderChild->getId();
    const bool grouped = folderTracks.setTrackParent(folderChildId, folderRootId);
    folderRoot->setFolderCollapsed(true);
    auto folderFile = temporaryFiles.make(".reharm");
    const bool folderSaved = ProjectFile::saveProject(folderMidi, folderFile, &folderTracks);
    TrackModel reloadedFolders;
    MidiModel reloadedMidi;
    const bool folderLoaded = folderSaved && ProjectFile::loadProject(reloadedMidi, folderFile, &reloadedFolders);
    auto* restoredRoot = reloadedFolders.getTrackById(folderRootId);
    auto* restoredChild = reloadedFolders.getTrackById(folderChildId);
    check("folder parent and collapse state survive project round trip",
          grouped && folderLoaded && restoredRoot != nullptr && restoredChild != nullptr
          && restoredRoot->isFolderCollapsed() && restoredChild->getParentFolderId() == folderRootId
          && !reloadedFolders.isTrackEffectivelyVisible(folderChildId));

    auto brokenFolderFile = temporaryFiles.make(".reharm");
    brokenFolderFile.replaceWithText("{\"version\":3,\"tracks\":["
        "{\"id\":101,\"type\":5,\"parentFolderId\":102},"
        "{\"id\":102,\"type\":5,\"parentFolderId\":101},"
        "{\"id\":103,\"type\":0,\"parentFolderId\":999},"
        "{\"id\":104,\"type\":0,\"parentFolderId\":103}]}");
    TrackModel repairedFolders;
    MidiModel repairedMidi;
    const bool repaired = ProjectFile::loadProject(repairedMidi, brokenFolderFile, &repairedFolders);
    check("legacy folder cycles and missing parents are repaired without losing tracks",
          repaired && repairedFolders.getTrackById(101) != nullptr
          && repairedFolders.getTrackById(101)->getParentFolderId() == -1
          && repairedFolders.getTrackById(103) != nullptr
          && repairedFolders.getTrackById(103)->getParentFolderId() == -1
          && repairedFolders.getTrackById(104) != nullptr
          && repairedFolders.getTrackById(104)->getParentFolderId() == -1
          && repairedFolders.getVisibleTracks().size() == 4);

    sampleFile.deleteFile();
    MidiModel missingAudioModel;
    TrackModel missingAudioTracks;
    const bool missingAudioLoaded = ProjectFile::loadProject(missingAudioModel, projectFile, &missingAudioTracks);
    auto* missingAudioTrack = missingAudioTracks.getTrackById(audioTrack->getId());
    check("missing external audio does not invalidate project", missingAudioLoaded
          && missingAudioTrack != nullptr && missingAudioTrack->getAudioClips().size() == 1
          && missingAudioTrack->getAudioClips().front().sourcePath == sampleFile.getFullPathName());

    return failures == 0 ? 0 : 1;
}

} // namespace Harmonizer
