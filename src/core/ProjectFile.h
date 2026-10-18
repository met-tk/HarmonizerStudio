#pragma once

#include "MidiModel.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace juce { class UndoManager; }

namespace Harmonizer
{

class TrackModel;

class ProjectFile
{
public:
    static bool saveProject(const MidiModel& model, const juce::File& targetFile, const TrackModel* trackModel = nullptr);
    static bool loadProject(MidiModel& model, const juce::File& sourceFile, TrackModel* trackModel = nullptr);

    static bool importMidi(MidiModel& model, const juce::File& midiFile, TrackModel* trackModel = nullptr);
    static bool importMidiAsClips(MidiModel& model, const juce::File& midiFile,
                                  TrackModel& trackModel, int preferredTrackId,
                                  double startBeat, juce::UndoManager* undoManager = nullptr);
    static bool importAudioAsClip(TrackModel& trackModel, const juce::File& audioFile,
                                  int preferredTrackId, double startBeat, double bpm,
                                  juce::UndoManager* undoManager = nullptr);
    static bool exportMidi(const MidiModel& model, const juce::File& targetFile, const TrackModel* trackModel = nullptr, double rangeStartBeat = -1.0, double rangeEndBeat = -1.0);
};

} // namespace Harmonizer
