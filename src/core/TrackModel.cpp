#include "TrackModel.h"
#include "UndoActions.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <limits>

namespace Harmonizer
{

namespace
{
double readAudioDuration(const juce::File& file)
{
    if (!file.existsAsFile()) return 0.0;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return 0.0;
    const double duration = (double)reader->lengthInSamples / reader->sampleRate;
    return std::isfinite(duration) && duration > 0.0 && duration <= 86400.0 ? duration : 0.0;
}
}

Track::Track(int id, const juce::String& name, TrackType type)
    : trackId(id), trackName(name), trackType(type)
{
    switch (type)
    {
        case TrackType::Instrument:
            trackColor = juce::Colour(0xff2575fc); // Blue
            break;
        case TrackType::Chord:
            trackColor = juce::Colour(0xfff59e0b); // Amber
            break;
        case TrackType::Mode:
            trackColor = juce::Colour(0xff10b981); // Emerald Green
            break;
        case TrackType::Marker:
            trackColor = juce::Colour(0xff06b6d4); // Cyan
            break;
        case TrackType::TimeSignature:
            trackColor = juce::Colour(0xff8b5cf6); // Purple
            break;
        case TrackType::Tempo:
            trackColor = juce::Colour(0xff3b82f6); // Blue
            break;
        case TrackType::Folder:
            trackColor = juce::Colour(0xff64748b); // Slate
            break;
        case TrackType::Audio:
            trackColor = juce::Colour(0xff49a7b8); // Teal waveform
            break;
    }
}

TrackModel::TrackModel()
{
    // Initialize standard Cubase project track hierarchy:
    // 1. Chord Track
    auto* chordTrk = addTrack("Chord Track", TrackType::Chord);
    ChordEvent c1 { 1, 0.0, 4.0, 0, "Cmaj7", "I", juce::Colour(0xfff59e0b) };
    ChordEvent c2 { 2, 4.0, 4.0, 9, "Am7", "vi", juce::Colour(0xfff59e0b) };
    ChordEvent c3 { 3, 8.0, 4.0, 2, "Dm7", "ii", juce::Colour(0xfff59e0b) };
    ChordEvent c4 { 4, 12.0, 4.0, 7, "G7", "V", juce::Colour(0xfff59e0b) };
    chordTrk->getChords().push_back(c1);
    chordTrk->getChords().push_back(c2);
    chordTrk->getChords().push_back(c3);
    chordTrk->getChords().push_back(c4);

    // 2. Dedicated Mode Track (Responsible for Scale / Tonality & Piano Roll Coloring)
    auto* modeTrk = addTrack("Mode Track", TrackType::Mode);
    ModeEvent md1;
    md1.id = 1;
    md1.startBeat = 0.0;
    md1.lengthBeats = 8.0;
    md1.name = "Section A (C Major)";
    md1.tonic = "C";
    md1.tonicPc = 0;
    md1.scaleName = "Ionian (Natural Major)";
    md1.color = juce::Colour(0xff10b981);
    modeTrk->getModes().push_back(md1);

    ModeEvent md2;
    md2.id = 2;
    md2.startBeat = 8.0;
    md2.lengthBeats = 8.0;
    md2.name = "Section B (Modulation)";
    md2.tonic = "A";
    md2.tonicPc = 9;
    md2.scaleName = "Aeolian (Natural Minor)";
    md2.color = juce::Colour(0xff06b6d4);
    modeTrk->getModes().push_back(md2);

    // 3. Pure Cubase-Style Marker Track (For arrangement milestones & flags, allows overlap)
    auto* markerTrk = addTrack("Marker Track", TrackType::Marker);
    MarkerEvent m1;
    m1.id = "m1";
    m1.startBeat = 0.0;
    m1.lengthBeats = 8.0;
    m1.name = "Verse 1";
    m1.color = juce::Colour(0xff0284c7);
    markerTrk->getMarkers().push_back(m1);

    MarkerEvent m2;
    m2.id = "m2";
    m2.startBeat = 8.0;
    m2.lengthBeats = 8.0;
    m2.name = "Chorus";
    m2.color = juce::Colour(0xffec4899);
    markerTrk->getMarkers().push_back(m2);

    // 4. Primary Instrument Track
    auto* inst1 = addTrack("Piano", TrackType::Instrument);
    inst1->setHasAuxiliaryMarkerTrack(true);

    MidiClip clip1;
    clip1.id = 1;
    clip1.name = "Piano Melody";
    clip1.startBeat = 0.0;
    clip1.lengthBeats = 16.0;
    clip1.color = juce::Colour(0xff2575fc);
    inst1->addClip(clip1);

    // 5. Default Time Signature Track
    auto* timeSigTrk = addTrack("Tempo / Time Signature", TrackType::TimeSignature);
    TimeSignatureEvent ts1;
    ts1.id = 1;
    ts1.startBeat = 0.0;
    ts1.numerator = 4;
    ts1.denominator = 4;
    timeSigTrk->getTimeSignatures().push_back(ts1);

    activeTrackId = inst1->getId();
    activeClipId = clip1.id;
    updatePlaybackSnapshot();
    updateTempoSnapshot();
}

void TrackModel::resetToDefaults()
{
    TrackModel defaults;
    replaceProjectData(defaults);
}

void TrackModel::replaceProjectData(TrackModel& loaded)
{
    listeners.call([](Listener& l) { l.projectWillReplace(); });
    tracks = std::move(loaded.tracks);
    nextTrackId = 1;
    for (const auto& track : tracks)
        nextTrackId = std::max(nextTrackId, track->getId() + 1);
    activeTrackId = loaded.activeTrackId;
    activeClipId = loaded.activeClipId;
    barNumberOffset = loaded.barNumberOffset;
    chordDisplayMode = loaded.chordDisplayMode;
    pianoRollVisibleTrackIds = std::move(loaded.pianoRollVisibleTrackIds);
    modeTrackOverrideEnabled = loaded.modeTrackOverrideEnabled;
    defaultTimeSigNumerator = loaded.defaultTimeSigNumerator;
    defaultTimeSigDenominator = loaded.defaultTimeSigDenominator;
    fallbackTempoBpm = loaded.fallbackTempoBpm;
    if (getTrackById(activeTrackId) == nullptr)
    {
        activeTrackId = -1;
        activeClipId = -1;
        for (const auto& track : tracks)
            if (track->getType() == TrackType::Instrument && !track->getClips().empty())
            {
                activeTrackId = track->getId();
                activeClipId = track->getClips().front().id;
                break;
            }
    }
    triggerTracksChanged();
    triggerChordsChanged();
    triggerModesChanged();
    triggerMarkersChanged();
    triggerTimeSignaturesChanged();
    triggerTempoChanged();
}

Track* TrackModel::addTrack(const juce::String& name, TrackType type)
{
    if (type == TrackType::Tempo && getTempoTrack() != nullptr)
        return getTempoTrack();
    if (type == TrackType::Chord && getChordTrack() != nullptr)
        return getChordTrack();
    if (type == TrackType::TimeSignature && getTimeSignatureTrack() != nullptr)
        return getTimeSignatureTrack();
    if (type == TrackType::Mode && getModeTrack() != nullptr)
        return getModeTrack();

    auto trk = std::make_unique<Track>(nextTrackId++, name, type);
    auto* ptr = trk.get();
    tracks.push_back(std::move(trk));
    triggerTracksChanged();
    return ptr;
}

double TrackModel::getAudioFileDuration(const juce::File& file)
{
    return readAudioDuration(file);
}

bool TrackModel::importAudioClip(int trackId, const juce::File& file, double startBeat,
                                 double fallbackBpm, juce::UndoManager* undoManager)
{
    auto* track = getTrackById(trackId);
    if (track == nullptr || track->getType() != TrackType::Audio || track->isLocked()
        || !std::isfinite(startBeat) || startBeat < 0.0)
        return false;

    const double duration = readAudioDuration(file);
    if (duration <= 0.0) return false;
    if (std::isfinite(fallbackBpm) && fallbackBpm >= 20.0 && fallbackBpm <= 400.0)
        fallbackTempoBpm = fallbackBpm;

    AudioClip clip;
    clip.id = juce::Time::currentTimeMillis();
    auto oldClips = track->getAudioClips();
    for (const auto& existing : oldClips)
        clip.id = std::max(clip.id, existing.id + 1);
    clip.name = file.getFileNameWithoutExtension();
    clip.sourcePath = file.getFullPathName();
    clip.startBeat = startBeat;
    clip.durationSeconds = duration;
    clip.lengthBeats = getBeatAfterSeconds(startBeat, duration, fallbackBpm) - startBeat;
    auto newClips = oldClips;
    newClips.push_back(std::move(clip));
    return UndoActionHelper::performAtomic(undoManager,
        new ModifyAudioClipsUndoAction(*this, trackId, std::move(oldClips), std::move(newClips)),
        "Import Audio Clip");
}

bool TrackModel::replaceAudioClipSource(int trackId, int64_t clipId, const juce::File& file,
                                        double fallbackBpm, juce::UndoManager* undoManager)
{
    auto* track = getTrackById(trackId);
    if (track == nullptr || track->getType() != TrackType::Audio || track->isLocked())
        return false;
    const double duration = readAudioDuration(file);
    if (duration <= 0.0) return false;
    if (std::isfinite(fallbackBpm) && fallbackBpm >= 20.0 && fallbackBpm <= 400.0)
        fallbackTempoBpm = fallbackBpm;
    auto before = track->getAudioClips();
    auto after = before;
    auto found = std::find_if(after.begin(), after.end(), [clipId](const AudioClip& clip) {
        return clip.id == clipId;
    });
    if (found == after.end()) return false;
    found->sourcePath = file.getFullPathName();
    found->name = file.getFileNameWithoutExtension();
    if (found->sourceOffsetSeconds >= duration) return false;
    found->durationSeconds = std::min(found->durationSeconds,
                                      duration - found->sourceOffsetSeconds);
    found->lengthBeats = getBeatAfterSeconds(found->startBeat, found->durationSeconds,
                                             fallbackBpm) - found->startBeat;
    return UndoActionHelper::performAtomic(undoManager,
        new ModifyAudioClipsUndoAction(*this, trackId, std::move(before), std::move(after)),
        "Relink Audio Clip");
}

bool TrackModel::splitMidiClip(int trackId, int64_t clipId, double splitBeat,
                               juce::UndoManager* undoManager)
{
    auto* track = getTrackById(trackId);
    if (track == nullptr || track->getType() != TrackType::Instrument || track->isLocked()
        || !std::isfinite(splitBeat)) return false;
    auto before = track->getClips();
    auto after = before;
    auto found = std::find_if(after.begin(), after.end(), [clipId](const MidiClip& clip) {
        return clip.id == clipId;
    });
    if (found == after.end() || splitBeat <= found->startBeat + 0.01
        || splitBeat >= found->startBeat + found->lengthBeats - 0.01)
        return false;

    int64_t nextClipId = juce::Time::currentTimeMillis();
    int64_t nextNoteId = nextClipId;
    for (const auto& clip : before)
    {
        nextClipId = std::max(nextClipId, clip.id + 1);
        for (const auto& note : clip.notes)
            nextNoteId = std::max(nextNoteId, note.id + 1);
    }
    MidiClip right = *found;
    right.id = nextClipId;
    right.startBeat = splitBeat;
    right.lengthBeats = found->startBeat + found->lengthBeats - splitBeat;
    right.notes.clear();
    right.isSelected = true;
    std::vector<NoteEvent> leftNotes;
    leftNotes.reserve(found->notes.size());
    for (const auto& note : found->notes)
    {
        const double noteEnd = note.startBeat + note.lengthBeats;
        if (noteEnd <= splitBeat)
            leftNotes.push_back(note);
        else if (note.startBeat >= splitBeat)
            right.notes.push_back(note);
        else
        {
            NoteEvent left = note;
            left.lengthBeats = splitBeat - note.startBeat;
            leftNotes.push_back(left);
            NoteEvent rightNote = note;
            rightNote.id = nextNoteId++;
            rightNote.startBeat = splitBeat;
            rightNote.lengthBeats = noteEnd - splitBeat;
            right.notes.push_back(rightNote);
        }
    }
    found->lengthBeats = splitBeat - found->startBeat;
    found->notes = std::move(leftNotes);
    found->isSelected = false;
    after.push_back(std::move(right));
    return UndoActionHelper::performAtomic(undoManager,
        new ModifyClipsUndoAction(*this, trackId, std::move(before), std::move(after),
                                  "Split MIDI Part"), "Split MIDI Part");
}

bool TrackModel::splitAudioClip(int trackId, int64_t clipId, double splitBeat,
                                double fallbackBpm, juce::UndoManager* undoManager)
{
    auto* track = getTrackById(trackId);
    if (track == nullptr || track->getType() != TrackType::Audio || track->isLocked()
        || !std::isfinite(splitBeat)) return false;
    auto before = track->getAudioClips();
    auto after = before;
    auto found = std::find_if(after.begin(), after.end(), [clipId](const AudioClip& clip) {
        return clip.id == clipId;
    });
    if (found == after.end() || splitBeat <= found->startBeat + 0.01
        || splitBeat >= found->startBeat + found->lengthBeats - 0.01)
        return false;
    const double leftSeconds = getSecondsBetweenBeats(found->startBeat, splitBeat,
                                                      fallbackBpm);
    if (leftSeconds <= 0.0001 || leftSeconds >= found->durationSeconds - 0.0001)
        return false;

    int64_t nextClipId = juce::Time::currentTimeMillis();
    for (const auto& clip : before)
        nextClipId = std::max(nextClipId, clip.id + 1);
    AudioClip right = *found;
    right.id = nextClipId;
    right.startBeat = splitBeat;
    right.sourceOffsetSeconds += leftSeconds;
    right.durationSeconds -= leftSeconds;
    right.lengthBeats = getBeatAfterSeconds(splitBeat, right.durationSeconds,
                                            fallbackBpm) - splitBeat;
    right.isSelected = true;
    found->durationSeconds = leftSeconds;
    found->lengthBeats = splitBeat - found->startBeat;
    found->isSelected = false;
    after.push_back(std::move(right));
    return UndoActionHelper::performAtomic(undoManager,
        new ModifyAudioClipsUndoAction(*this, trackId, std::move(before), std::move(after)),
        "Split Audio Clip");
}

Track* TrackModel::insertTrack(int index, std::unique_ptr<Track> track)
{
    if (track == nullptr) return nullptr;
    auto* ptr = track.get();
    if (ptr->getId() >= nextTrackId)
        nextTrackId = ptr->getId() + 1;

    int idx = std::clamp(index, 0, (int)tracks.size());
    tracks.insert(tracks.begin() + idx, std::move(track));
    triggerTracksChanged();
    return ptr;
}

std::unique_ptr<Track> TrackModel::extractTrack(int id, int& outIndex)
{
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        if (tracks[i]->getId() == id)
        {
            listeners.call([&](Listener& l) { l.trackWillBeRemoved(*tracks[i]); });
            outIndex = (int)i;
            auto trk = std::move(tracks[i]);
            tracks.erase(tracks.begin() + i);

            // Keep a folder's contents in the project when its header is removed.
            // Undo actions can restore these direct parent links on reinsertion.
            if (trk->getType() == TrackType::Folder)
            {
                const int parentId = trk->getParentFolderId();
                const auto* parent = getTrackById(parentId);
                const int replacementId = parent != nullptr && parent->getType() == TrackType::Folder
                    ? parentId : -1;
                for (auto& child : tracks)
                    if (child->getParentFolderId() == id)
                        child->setParentFolderId(replacementId);
            }

            const bool hadActive = (activeTrackId == id);

            if (hadActive)
            {
                activeTrackId = -1;
                activeClipId = -1;
                for (const auto& remaining : tracks)
                {
                    if (remaining->getType() == TrackType::Instrument && !remaining->getClips().empty())
                    {
                        activeTrackId = remaining->getId();
                        activeClipId = remaining->getClips().front().id;
                        break;
                    }
                }
                listeners.call([](Listener& l) { l.activeClipChanged(); });
            }

            triggerTracksChanged();
            return trk;
        }
    }
    outIndex = -1;
    return nullptr;
}

void TrackModel::removeTrack(int id)
{
    int dummy = -1;
    extractTrack(id, dummy);
}

bool TrackModel::moveTrack(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= (int)tracks.size()) return false;
    if (toIndex < 0 || toIndex >= (int)tracks.size()) return false;
    if (fromIndex == toIndex) return true;

    auto trk = std::move(tracks[fromIndex]);
    tracks.erase(tracks.begin() + fromIndex);
    tracks.insert(tracks.begin() + toIndex, std::move(trk));
    triggerTracksChanged();
    return true;
}

bool TrackModel::moveTrackUp(int id, juce::UndoManager* um)
{
    const int source = getTrackIndex(id);
    if (source < 0) return false;
    const int parentId = tracks[(size_t)source]->getParentFolderId();
    for (int i = source - 1; i >= 0; --i)
    {
        if (tracks[(size_t)i]->getParentFolderId() == parentId)
        {
            if (um != nullptr)
                return UndoActionHelper::performAtomic(um, new MoveTrackUndoAction(*this, source, i, id, parentId, parentId), "Move Track Up");
            return moveTrack(source, i);
        }
    }
    return false;
}

bool TrackModel::moveTrackDown(int id, juce::UndoManager* um)
{
    const int source = getTrackIndex(id);
    if (source < 0) return false;
    const int parentId = tracks[(size_t)source]->getParentFolderId();
    for (int i = source + 1; i < (int)tracks.size(); ++i)
    {
        if (tracks[(size_t)i]->getParentFolderId() == parentId)
        {
            if (um != nullptr)
                return UndoActionHelper::performAtomic(um, new MoveTrackUndoAction(*this, source, i, id, parentId, parentId), "Move Track Down");
            return moveTrack(source, i);
        }
    }
    return false;
}

Track* TrackModel::duplicateTrack(int id)
{
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        if (tracks[i]->getId() == id)
        {
            auto type = tracks[i]->getType();
            if (type == TrackType::Tempo || type == TrackType::Chord || type == TrackType::TimeSignature || type == TrackType::Mode)
                return nullptr;

            std::vector<int> sourceIds { id };
            if (type == TrackType::Folder)
            {
                const auto descendants = getChildTrackIds(id, true);
                for (int descendantId : descendants)
                    if (const auto* child = getTrackById(descendantId))
                        if (child->getType() != TrackType::Tempo && child->getType() != TrackType::Chord
                            && child->getType() != TrackType::TimeSignature && child->getType() != TrackType::Mode)
                            sourceIds.push_back(descendantId);
            }

            std::map<int, int> copiedIds;
            std::vector<std::unique_ptr<Track>> copies;
            for (int sourceId : sourceIds)
            {
                auto* original = getTrackById(sourceId);
                listeners.call([&](Listener& l) { l.trackWillBeDuplicated(*original); });
                const int newId = nextTrackId++;
                copiedIds[sourceId] = newId;
                auto copy = original->clone(newId);
                copy->setSelected(false);
                if (sourceId == id)
                    copy->setName(original->getName() + " (Copy)");
                copies.push_back(std::move(copy));
            }
            for (auto& copy : copies)
            {
                if (auto parent = copiedIds.find(copy->getParentFolderId()); parent != copiedIds.end())
                    copy->setParentFolderId(parent->second);
                if (auto parent = copiedIds.find(copy->getParentTrackId()); parent != copiedIds.end())
                    copy->setParentTrackId(parent->second);
            }

            Track* result = copies.front().get();
            int insertAt = (int)i + 1;
            for (auto& copy : copies)
                tracks.insert(tracks.begin() + insertAt++, std::move(copy));
            triggerTracksChanged();
            return result;
        }
    }
    return nullptr;
}

int TrackModel::getTrackIndex(int id) const
{
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i]->getId() == id)
            return (int)i;
    return -1;
}

Track* TrackModel::getTrackById(int id)
{
    for (auto& t : tracks)
        if (t->getId() == id) return t.get();
    return nullptr;
}

const Track* TrackModel::getTrackById(int id) const
{
    for (const auto& t : tracks)
        if (t->getId() == id) return t.get();
    return nullptr;
}

Track* TrackModel::getActiveInstrumentTrack()
{
    if (auto* t = getTrackById(activeTrackId))
        if (t->getType() == TrackType::Instrument)
            return t;

    if (activeClipId > 0)
    {
        for (auto& t : tracks)
        {
            if (t->getType() == TrackType::Instrument)
            {
                for (const auto& c : t->getClips())
                    if (c.id == activeClipId)
                        return t.get();
            }
        }
    }

    for (auto& t : tracks)
        if (t->getType() == TrackType::Instrument)
            return t.get();

    return nullptr;
}

Track* TrackModel::getChordTrack()
{
    for (auto& t : tracks)
        if (t->getType() == TrackType::Chord) return t.get();
    return nullptr;
}

Track* TrackModel::getModeTrack()
{
    for (auto& t : tracks)
        if (t->getType() == TrackType::Mode) return t.get();
    return nullptr;
}

bool TrackModel::applyModeToRange(std::vector<ModeEvent>& modes, double startBeat,
                                  double endBeat, const ModeEvent& requested)
{
    if (!std::isfinite(startBeat) || !std::isfinite(endBeat)
        || startBeat < 0.0 || endBeat <= startBeat || requested.tonicPc < 0
        || requested.tonicPc > 11 || requested.scaleName.isEmpty())
        return false;

    bool intersects = false;
    bool changed = false;
    for (auto& mode : modes)
    {
        if (mode.startBeat >= endBeat || mode.startBeat + mode.lengthBeats <= startBeat)
            continue;
        intersects = true;
        if (mode.tonic != requested.tonic || mode.tonicPc != requested.tonicPc
            || mode.scaleName != requested.scaleName || mode.name != requested.name)
        {
            mode.tonic = requested.tonic;
            mode.tonicPc = requested.tonicPc;
            mode.scaleName = requested.scaleName;
            mode.name = requested.name;
            changed = true;
        }
    }
    if (intersects) return changed;

    ModeEvent inserted = requested;
    inserted.startBeat = startBeat;
    inserted.lengthBeats = endBeat - startBeat;
    inserted.isSelected = false;
    while (std::any_of(modes.begin(), modes.end(), [&inserted](const ModeEvent& mode) {
        return mode.id == inserted.id;
    }))
        inserted.id = inserted.id == std::numeric_limits<int64_t>::max() ? 1 : inserted.id + 1;
    modes.push_back(std::move(inserted));
    return true;
}

Track* TrackModel::getMarkerTrack()
{
    for (auto& t : tracks)
        if (t->getType() == TrackType::Marker) return t.get();
    return nullptr;
}

Track* TrackModel::getTimeSignatureTrack()
{
    for (auto& t : tracks)
        if (t->getType() == TrackType::TimeSignature) return t.get();
    return nullptr;
}

const Track* TrackModel::getTimeSignatureTrack() const
{
    for (const auto& t : tracks)
        if (t->getType() == TrackType::TimeSignature) return t.get();
    return nullptr;
}

Track* TrackModel::getTempoTrack()
{
    for (auto& t : tracks)
        if (t->getType() == TrackType::Tempo) return t.get();
    return nullptr;
}

const Track* TrackModel::getTempoTrack() const
{
    for (const auto& t : tracks)
        if (t->getType() == TrackType::Tempo) return t.get();
    return nullptr;
}

double TrackModel::getTempoAtBeat(double beat, double fallbackBpm) const
{
    std::shared_ptr<const std::vector<TempoEvent>> events;
    {
        const juce::SpinLock::ScopedLockType sl(tempoSnapshotLock);
        events = tempoSnapshot;
    }
    if (events == nullptr || events->empty())
        return fallbackBpm;

    if (beat <= events->front().startBeat)
        return events->front().bpm;

    if (beat >= events->back().startBeat)
        return events->back().bpm;

    for (size_t i = 0; i + 1 < events->size(); ++i)
    {
        const auto& e1 = (*events)[i];
        const auto& e2 = (*events)[i + 1];

        if (beat >= e1.startBeat && beat < e2.startBeat)
        {
            if (e1.type == TempoTransitionType::Jump)
            {
                return e1.bpm;
            }
            else
            {
                double span = e2.startBeat - e1.startBeat;
                if (span <= 0.0001)
                    return e2.bpm;
                double alpha = (beat - e1.startBeat) / span;
                return e1.bpm + alpha * (e2.bpm - e1.bpm);
            }
        }
    }

    return events->back().bpm;
}

double TrackModel::getSecondsBetweenBeats(double startBeat, double endBeat, double fallbackBpm) const
{
    const double start = std::max(0.0, startBeat);
    if (!std::isfinite(start) || !std::isfinite(endBeat) || endBeat <= start) return 0.0;
    std::shared_ptr<const std::vector<TempoEvent>> events;
    {
        const juce::SpinLock::ScopedLockType sl(tempoSnapshotLock);
        events = tempoSnapshot;
    }
    if (events == nullptr || events->empty())
        return (endBeat - start) * 60.0 / std::max(20.0, fallbackBpm);

    double seconds = 0.0;
    if (start < events->front().startBeat)
        seconds += (std::min(endBeat, events->front().startBeat) - start)
            * 60.0 / std::max(20.0, events->front().bpm);

    for (size_t i = 0; i < events->size(); ++i)
    {
        const auto& event = (*events)[i];
        const double from = std::max(start, event.startBeat);
        const double to = std::min(endBeat, i + 1 < events->size()
            ? (*events)[i + 1].startBeat : endBeat);
        if (to <= from) continue;
        if (event.type == TempoTransitionType::Ramp && i + 1 < events->size())
        {
            const auto& next = (*events)[i + 1];
            const double slope = (next.bpm - event.bpm) / std::max(1.0e-9, next.startBeat - event.startBeat);
            if (std::abs(slope) > 1.0e-9)
            {
                const double bpmFrom = std::max(20.0, event.bpm + slope * (from - event.startBeat));
                const double bpmTo = std::max(20.0, event.bpm + slope * (to - event.startBeat));
                seconds += 60.0 / slope * std::log(bpmTo / bpmFrom);
                continue;
            }
        }
        seconds += (to - from) * 60.0 / std::max(20.0, event.bpm);
    }
    return std::max(0.0, seconds);
}

double TrackModel::getBeatAfterSeconds(double startBeat, double seconds, double fallbackBpm) const
{
    if (!std::isfinite(startBeat) || !std::isfinite(seconds) || seconds <= 0.0)
        return std::max(0.0, startBeat);
    const double start = std::max(0.0, startBeat);
    double low = start;
    double high = start + std::max(1.0, seconds * 400.0 / 60.0);
    for (int i = 0; i < 8 && getSecondsBetweenBeats(start, high, fallbackBpm) < seconds; ++i)
        high = start + (high - start) * 2.0;
    for (int i = 0; i < 40; ++i)
    {
        const double middle = (low + high) * 0.5;
        if (getSecondsBetweenBeats(start, middle, fallbackBpm) < seconds)
            low = middle;
        else high = middle;
    }
    return high;
}

void TrackModel::setFallbackTempoBpm(double bpm)
{
    if (!std::isfinite(bpm) || bpm < 20.0 || bpm > 400.0
        || std::abs(fallbackTempoBpm - bpm) < 1.0e-6)
        return;
    fallbackTempoBpm = bpm;
    triggerTempoChanged();
}

int TrackModel::getTrackDepth(int trackId) const
{
    int depth = 0;
    int curId = trackId;
    std::set<int> visited;
    while (visited.insert(curId).second)
    {
        const Track* t = getTrackById(curId);
        if (!t) break;
        int pId = t->getParentFolderId();
        const Track* parent = getTrackById(pId);
        if (pId <= 0 || parent == nullptr || parent->getType() != TrackType::Folder) break;
        curId = pId;
        depth++;
    }
    return depth;
}

int TrackModel::getSubtreeDepth(int trackId) const
{
    int maximum = 0;
    std::set<int> visited { trackId };
    std::function<void(int, int)> visit = [&](int parentId, int depth)
    {
        for (const auto& child : tracks)
            if (child->getParentFolderId() == parentId && visited.insert(child->getId()).second)
            {
                maximum = std::max(maximum, depth + 1);
                if (child->getType() == TrackType::Folder)
                    visit(child->getId(), depth + 1);
            }
    };
    visit(trackId, 0);
    return maximum;
}

bool TrackModel::canNestInFolder(int trackId, int targetFolderId) const
{
    if (getTrackById(trackId) == nullptr) return false;
    if (targetFolderId <= 0) return true; // root level is always allowed
    if (trackId == targetFolderId) return false;

    const Track* target = getTrackById(targetFolderId);
    if (!target || target->getType() != TrackType::Folder) return false;

    // Check circular dependency: targetFolderId cannot be a descendant of trackId
    auto descendants = getChildTrackIds(trackId, true);
    if (std::find(descendants.begin(), descendants.end(), targetFolderId) != descendants.end())
        return false;

    // Depth check: max 3 levels of nesting (depth 1, 2, 3)
    int targetDepth = getTrackDepth(targetFolderId);
    int subtreeD = getSubtreeDepth(trackId);
    if (targetDepth + 1 + subtreeD > 3)
        return false;

    return true;
}

bool TrackModel::setTrackParent(int trackId, int folderId)
{
    auto* track = getTrackById(trackId);
    if (track == nullptr || !canNestInFolder(trackId, folderId))
        return false;
    const int normalizedId = folderId > 0 ? folderId : -1;
    if (track->getParentFolderId() == normalizedId)
        return true;
    track->setParentFolderId(normalizedId);
    triggerTracksChanged();
    return true;
}

void TrackModel::repairFolderHierarchy()
{
    // Older projects could contain orphan references or cycles because parent
    // IDs were assigned directly. Detach only the invalid link and keep tracks.
    for (auto& track : tracks)
    {
        int parentId = track->getParentFolderId();
        std::set<int> visited { track->getId() };
        int depth = 0;
        while (parentId > 0)
        {
            const auto* parent = getTrackById(parentId);
            if (parent == nullptr || parent->getType() != TrackType::Folder
                || !visited.insert(parentId).second || ++depth > 3)
            {
                track->setParentFolderId(-1);
                break;
            }
            parentId = parent->getParentFolderId();
        }
    }
}

bool TrackModel::isTrackEffectivelyVisible(int trackId) const
{
    int curId = trackId;
    std::set<int> visited;
    while (true)
    {
        visited.insert(curId);
        const Track* t = getTrackById(curId);
        if (!t) return true;
        int pId = t->getParentFolderId();
        if (pId <= 0 || visited.count(pId)) return true;
        const Track* p = getTrackById(pId);
        if (!p) return true;
        if (p->getType() == TrackType::Folder && p->isFolderCollapsed())
            return false;
        curId = pId;
    }
    return true;
}

std::vector<int> TrackModel::getChildTrackIds(int folderId, bool recursive) const
{
    std::vector<int> children;
    std::set<int> visited { folderId };
    std::function<void(int)> visit = [&](int parentId)
    {
        for (const auto& t : tracks)
            if (t->getParentFolderId() == parentId && visited.insert(t->getId()).second)
            {
                children.push_back(t->getId());
                if (recursive && t->getType() == TrackType::Folder)
                    visit(t->getId());
            }
    };
    visit(folderId);
    return children;
}

std::vector<Track*> TrackModel::getVisibleTracks() const
{
    std::vector<Track*> list;
    std::set<int> visited;
    std::function<void(Track*, bool)> visit = [&](Track* track, bool hidden)
    {
        if (!visited.insert(track->getId()).second)
            return;
        if (!hidden)
            list.push_back(track);
        const bool hideChildren = hidden || (track->getType() == TrackType::Folder && track->isFolderCollapsed());
        for (const auto& child : tracks)
            if (child->getParentFolderId() == track->getId())
                visit(child.get(), hideChildren);
    };
    for (const auto& t : tracks)
    {
        const auto* parent = getTrackById(t->getParentFolderId());
        if (parent == nullptr || parent->getType() != TrackType::Folder)
            visit(t.get(), false);
    }
    // Defensive fallback for a cyclic project that has not yet been repaired.
    for (const auto& t : tracks)
        if (visited.count(t->getId()) == 0)
            visit(t.get(), false);
    return list;
}

std::vector<BarSpan> TrackModel::getBarTimeline(double maxBeat, int fallbackNumerator, int fallbackDenominator) const
{
    if (fallbackNumerator <= 0) fallbackNumerator = defaultTimeSigNumerator;
    if (fallbackDenominator <= 0) fallbackDenominator = defaultTimeSigDenominator;

    fallbackNumerator = std::clamp(fallbackNumerator, 1, 32);
    fallbackDenominator = std::clamp(fallbackDenominator, 1, 32);

    std::vector<BarSpan> timeline;
    std::vector<TimeSignatureEvent> sigs;

    if (const auto* tsTrack = getTimeSignatureTrack())
        sigs = tsTrack->getTimeSignatures();

    std::sort(sigs.begin(), sigs.end(), [](const TimeSignatureEvent& a, const TimeSignatureEvent& b) {
        return a.startBeat < b.startBeat;
    });

    double currentBeat = 0.0;
    int barIdx = 1;
    double targetEnd = std::max(64.0, maxBeat + 16.0);

    while (currentBeat < targetEnd && barIdx < 2000)
    {
        int num = fallbackNumerator;
        int denom = fallbackDenominator;
        double nextTsBeat = 1e9;

        // Piecewise-constant time signature lookup:
        // Find latest event at or before currentBeat
        for (const auto& ts : sigs)
        {
            if (ts.startBeat <= currentBeat + 0.001)
            {
                num = ts.numerator;
                denom = ts.denominator;
            }
            else
            {
                if (ts.startBeat < nextTsBeat)
                    nextTsBeat = ts.startBeat;
            }
        }

        num = std::clamp(num, 1, 32);
        denom = std::clamp(denom, 1, 32);
        double barLength = (double)num * (4.0 / (double)denom);

        // Transition gracefully to next time signature at nextTsBeat
        if (currentBeat + barLength > nextTsBeat + 0.001)
        {
            if (nextTsBeat > currentBeat + 0.05)
                barLength = nextTsBeat - currentBeat;
        }

        BarSpan span;
        span.barIndex = barIdx++;
        span.startBeat = currentBeat;
        span.endBeat = currentBeat + barLength;
        span.lengthBeats = barLength;
        span.numerator = num;
        span.denominator = denom;
        timeline.push_back(span);

        currentBeat += barLength;
    }

    return timeline;
}

std::vector<BarSpan> TrackModel::getBarTimelineRange(double firstBeat, double lastBeat,
                                                     int fallbackNumerator,
                                                     int fallbackDenominator) const
{
    if (!std::isfinite(firstBeat) || !std::isfinite(lastBeat) || lastBeat < firstBeat)
        return {};
    if (fallbackNumerator <= 0) fallbackNumerator = defaultTimeSigNumerator;
    if (fallbackDenominator <= 0) fallbackDenominator = defaultTimeSigDenominator;
    fallbackNumerator = std::clamp(fallbackNumerator, 1, 32);
    fallbackDenominator = std::clamp(fallbackDenominator, 1, 32);

    std::vector<TimeSignatureEvent> sigs;
    if (const auto* track = getTimeSignatureTrack())
        sigs = track->getTimeSignatures();
    std::sort(sigs.begin(), sigs.end(), [](const auto& a, const auto& b) {
        return a.startBeat < b.startBeat;
    });

    std::vector<BarSpan> timeline;
    const double first = std::max(0.0, firstBeat);
    const double end = std::max(64.0, lastBeat + 16.0);
    double beat = 0.0;
    int64_t barIndex = 1;
    while (beat < end && barIndex < std::numeric_limits<int>::max())
    {
        int numerator = fallbackNumerator;
        int denominator = fallbackDenominator;
        double nextSignatureBeat = std::numeric_limits<double>::infinity();
        for (const auto& signature : sigs)
        {
            if (signature.startBeat <= beat + 0.001)
            {
                numerator = signature.numerator;
                denominator = signature.denominator;
            }
            else
                nextSignatureBeat = std::min(nextSignatureBeat, signature.startBeat);
        }
        numerator = std::clamp(numerator, 1, 32);
        denominator = std::clamp(denominator, 1, 32);
        const double regularLength = (double)numerator * 4.0 / (double)denominator;

        // Jump across complete off-screen bars; keep the global bar numbering
        // and stop before any time-signature boundary.
        const double jumpEnd = std::min(first, nextSignatureBeat);
        if (beat + regularLength <= jumpEnd - 0.000001)
        {
            const double count = std::floor((jumpEnd - beat + 0.000001) / regularLength);
            const auto skip = (int64_t)std::min(count,
                (double)(std::numeric_limits<int>::max() - barIndex - 1));
            if (skip > 0)
            {
                beat += (double)skip * regularLength;
                barIndex += skip;
                continue;
            }
        }

        double length = regularLength;
        if (beat + length > nextSignatureBeat + 0.001
            && nextSignatureBeat > beat + 0.05)
            length = nextSignatureBeat - beat;

        BarSpan span;
        span.barIndex = (int)barIndex++;
        span.startBeat = beat;
        span.endBeat = beat + length;
        span.lengthBeats = length;
        span.numerator = numerator;
        span.denominator = denominator;
        if (span.endBeat > first - 0.0001)
            timeline.push_back(span);
        beat += length;
    }
    return timeline;
}

BarSpan TrackModel::getBarAtBeat(double beat) const
{
    auto tl = getBarTimelineRange(beat, beat + 8.0);
    for (const auto& span : tl)
    {
        if (beat >= span.startBeat - 0.0001 && beat < span.endBeat - 0.0001)
            return span;
    }
    if (!tl.empty())
        return tl.back();

    BarSpan def;
    def.barIndex = (int)(beat / 4.0) + 1;
    def.startBeat = (def.barIndex - 1) * 4.0;
    def.endBeat = def.startBeat + 4.0;
    return def;
}

int TrackModel::beatToBarNumber(double beat) const
{
    return getBarAtBeat(beat).barIndex;
}

double TrackModel::barToStartBeat(int barNumber) const
{
    if (barNumber <= 1)
        return 0.0;

    // Every supported meter is at most 128 beats long. Search by absolute
    // beat using the range-aware bar lookup, avoiding a full bar list.
    double low = 0.0;
    double high = (double)barNumber * 128.0;
    for (int i = 0; i < 64; ++i)
    {
        const double middle = low + (high - low) * 0.5;
        if (getBarAtBeat(middle).barIndex < barNumber)
            low = middle;
        else
            high = middle;
    }
    return getBarAtBeat(high + 0.0002).startBeat;
}

bool TrackModel::canPlaceEvent(int trackId, double startBeat, double lengthBeats, int64_t ignoreSelfId, const juce::String& ignoreSelfStrId) const
{
    juce::ignoreUnused(ignoreSelfStrId);
    auto* trk = const_cast<TrackModel*>(this)->getTrackById(trackId);
    if (!trk) return false;

    if (trk->getType() == TrackType::Marker || trk->getType() == TrackType::Folder)
        return true;

    double newEnd = startBeat + lengthBeats;
    double eps = 0.001;

    if (trk->getType() == TrackType::Instrument)
    {
        for (const auto& c : trk->getClips())
        {
            if (c.id == ignoreSelfId) continue;
            double cEnd = c.startBeat + c.lengthBeats;
            if (startBeat < cEnd - eps && newEnd > c.startBeat + eps)
                return false;
        }
    }
    else if (trk->getType() == TrackType::Chord)
    {
        for (const auto& c : trk->getChords())
        {
            if (c.id == ignoreSelfId) continue;
            double cEnd = c.startBeat + c.lengthBeats;
            if (startBeat < cEnd - eps && newEnd > c.startBeat + eps)
                return false;
        }
    }
    else if (trk->getType() == TrackType::Mode)
    {
        for (const auto& m : trk->getModes())
        {
            if (m.id == ignoreSelfId) continue;
            double mEnd = m.startBeat + m.lengthBeats;
            if (startBeat < mEnd - eps && newEnd > m.startBeat + eps)
                return false;
        }
    }
    else if (trk->getType() == TrackType::TimeSignature)
    {
        for (const auto& ts : trk->getTimeSignatures())
        {
            if (ts.id == ignoreSelfId) continue;
            double tsEnd = ts.startBeat + ts.lengthBeats;
            if (startBeat < tsEnd - eps && newEnd > ts.startBeat + eps)
                return false;
        }
    }
    else if (trk->getType() == TrackType::Tempo)
    {
        for (const auto& te : trk->getTempoEvents())
        {
            if (te.id == ignoreSelfId) continue;
            if (std::abs(te.startBeat - startBeat) < eps)
                return false;
        }
    }

    return true;
}

void TrackModel::clearAllArrangerSelection()
{
    for (auto& t : tracks)
    {
        for (auto& c : t->getClips()) c.isSelected = false;
        for (auto& c : t->getAudioClips()) c.isSelected = false;
        for (auto& c : t->getChords()) c.isSelected = false;
        for (auto& m : t->getModes()) m.isSelected = false;
        for (auto& m : t->getMarkers()) m.isSelected = false;
        for (auto& ts : t->getTimeSignatures()) ts.isSelected = false;
        for (auto& te : t->getTempoEvents()) te.isSelected = false;
    }
    triggerTracksChanged();
}

void TrackModel::selectAllArrangerEvents()
{
    for (auto& t : tracks)
    {
        for (auto& c : t->getClips()) c.isSelected = true;
        for (auto& c : t->getAudioClips()) c.isSelected = true;
        for (auto& c : t->getChords()) c.isSelected = true;
        for (auto& m : t->getModes()) m.isSelected = true;
        for (auto& m : t->getMarkers()) m.isSelected = true;
        for (auto& ts : t->getTimeSignatures()) ts.isSelected = true;
        for (auto& te : t->getTempoEvents()) te.isSelected = true;
    }
    triggerTracksChanged();
}

int TrackModel::getSelectedArrangerEventCount() const
{
    int count = 0;
    for (const auto& t : tracks)
    {
        for (const auto& c : t->getClips()) if (c.isSelected) count++;
        for (const auto& c : t->getAudioClips()) if (c.isSelected) count++;
        for (const auto& c : t->getChords()) if (c.isSelected) count++;
        for (const auto& m : t->getModes()) if (m.isSelected) count++;
        for (const auto& m : t->getMarkers()) if (m.isSelected) count++;
        for (const auto& ts : t->getTimeSignatures()) if (ts.isSelected) count++;
        for (const auto& te : t->getTempoEvents()) if (te.isSelected) count++;
    }
    return count;
}

void TrackModel::setActiveTrackId(int id)
{
    if (activeTrackId != id)
    {
        activeTrackId = id;
        if (auto* trk = getTrackById(id))
        {
            if (!trk->getClips().empty())
                activeClipId = trk->getClips()[0].id;
        }
        listeners.call([](Listener& l) { l.activeClipChanged(); });
    }
}

void TrackModel::setActiveClipId(int64_t clipId)
{
    if (activeClipId != clipId)
    {
        activeClipId = clipId;
        listeners.call([](Listener& l) { l.activeClipChanged(); });
    }
}

void TrackModel::setActiveClip(int trackId, int64_t clipId, MidiModel* midiModelToSync)
{
    if (activeTrackId == trackId && activeClipId == clipId)
        return;

    // 1. Save current midiModel notes into currently active clip if requested
    if (midiModelToSync != nullptr)
    {
        if (auto* oldClip = getActiveClip())
        {
            oldClip->notes = midiModelToSync->getNotes();
        }
    }

    activeTrackId = trackId;
    activeClipId = clipId;

    // 2. Load newly active clip notes into midiModel
    if (midiModelToSync != nullptr)
    {
        if (auto* newClip = getActiveClip())
        {
            midiModelToSync->setAllNotes(newClip->notes, false);
        }
        else
        {
            midiModelToSync->clearAllNotes(false);
        }
    }

    updatePlaybackSnapshot();
    listeners.call([](Listener& l) { l.activeClipChanged(); });
    if (midiModelToSync != nullptr)
        midiModelToSync->triggerNoteListChanged();
}

void TrackModel::syncActiveClipNotes(const std::vector<NoteEvent>& notes,
                                     bool refreshPlaybackSnapshot)
{
    if (auto* clip = getActiveClip())
    {
        clip->notes = notes;
        if (refreshPlaybackSnapshot)
            updatePlaybackSnapshot();
        else
            playbackSnapshotDirty = true;
        listeners.call([](Listener& l) { l.clipNotesChanged(); });
    }
}

void TrackModel::setBarNumberOffset(int offset)
{
    if (barNumberOffset != offset)
    {
        barNumberOffset = offset;
        triggerTimeSignaturesChanged();
        triggerTracksChanged();
    }
}

void TrackModel::validateActiveClip()
{
    auto* trk = getTrackById(activeTrackId);
    if (trk == nullptr || trk->getType() != TrackType::Instrument)
    {
        if (activeClipId != -1)
        {
            activeClipId = -1;
            listeners.call([](Listener& l) { l.activeClipChanged(); });
        }
        return;
    }

    bool exists = false;
    for (const auto& c : trk->getClips())
    {
        if (c.id == activeClipId)
        {
            exists = true;
            break;
        }
    }

    if (!exists)
    {
        if (!trk->getClips().empty())
        {
            activeClipId = trk->getClips().front().id;
        }
        else
        {
            activeClipId = -1;
        }
        listeners.call([](Listener& l) { l.activeClipChanged(); });
    }
}

void TrackModel::updatePlaybackSnapshot()
{
    auto snapshot = std::make_shared<std::vector<PlaybackNoteEvent>>();

    bool hasSolo = false;
    for (const auto& t : tracks)
    {
        if ((t->getType() == TrackType::Instrument || t->getType() == TrackType::Audio) && t->isSolo())
        {
            hasSolo = true;
            break;
        }
    }

    for (const auto& t : tracks)
    {
        if (t->getType() != TrackType::Instrument)
            continue;

        bool trackPlayable = !t->isMuted() && (!hasSolo || t->isSolo());
        if (!trackPlayable)
            continue;

        for (const auto& clip : t->getClips())
        {
            if (clip.isMuted)
                continue;

            for (const auto& n : clip.notes)
            {
                if (n.isMuted)
                    continue;

                PlaybackNoteEvent pne;
                pne.id = n.id;
                pne.trackId = t->getId();
                pne.pitch = n.pitch;
                pne.startBeat = n.startBeat;
                pne.lengthBeats = n.lengthBeats;
                pne.velocity = n.velocity;
                pne.isMuted = false;
                snapshot->push_back(pne);
            }
        }
    }

    const juce::SpinLock::ScopedLockType sl(playbackSnapshotLock);
    playbackSnapshot = snapshot;
    playbackSnapshotDirty = false;
}

void TrackModel::updateTempoSnapshot()
{
    auto snapshot = std::make_shared<std::vector<TempoEvent>>();
    if (const auto* tempoTrack = getTempoTrack())
        *snapshot = tempoTrack->getTempoEvents();
    std::sort(snapshot->begin(), snapshot->end(), [](const TempoEvent& a, const TempoEvent& b) {
        return a.startBeat < b.startBeat;
    });
    {
        const juce::SpinLock::ScopedLockType sl(tempoSnapshotLock);
        tempoSnapshot = snapshot;
    }
    for (auto& track : tracks)
        if (track->getType() == TrackType::Audio)
            for (auto& clip : track->getAudioClips())
                if (clip.durationSeconds > 0.0)
                    clip.lengthBeats = getBeatAfterSeconds(clip.startBeat, clip.durationSeconds,
                                                           fallbackTempoBpm) - clip.startBeat;
}

MidiClip* TrackModel::getActiveClip()
{
    auto* trk = getTrackById(activeTrackId);
    if (trk == nullptr) return nullptr;

    for (auto& c : trk->getClips())
        if (c.id == activeClipId) return &c;

    return nullptr;
}

const MidiClip* TrackModel::getActiveClip() const
{
    const auto* trk = getTrackById(activeTrackId);
    if (trk == nullptr) return nullptr;

    for (const auto& c : trk->getClips())
        if (c.id == activeClipId) return &c;

    return nullptr;
}

const ModeEvent* TrackModel::findEffectiveModeAtBeat(double beat) const
{
    if (!modeTrackOverrideEnabled)
        return nullptr;

    // 1. Check active track auxiliary mode track first
    if (auto* activeTrk = const_cast<TrackModel*>(this)->getTrackById(activeTrackId))
    {
        if (activeTrk->hasAuxiliaryMarkerTrack())
        {
            for (const auto& m : activeTrk->getAuxModes())
            {
                if (beat >= m.startBeat && beat < (m.startBeat + m.lengthBeats))
                    return &m;
            }
        }
    }

    // 2. Check dedicated global mode track
    for (const auto& t : tracks)
    {
        if (t->getType() == TrackType::Mode)
        {
            for (const auto& m : t->getModes())
            {
                if (beat >= m.startBeat && beat < (m.startBeat + m.lengthBeats))
                    return &m;
            }
        }
    }

    return nullptr;
}

void TrackModel::computeMarkerStackLayers(std::vector<MarkerEvent>& markers)
{
    // Sort markers by startBeat
    std::sort(markers.begin(), markers.end(), [](const MarkerEvent& a, const MarkerEvent& b) {
        if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
        return a.lengthBeats < b.lengthBeats;
    });

    // Greedy interval graph coloring to assign minimal stackLayer to prevent visual collision
    std::vector<double> layerEndBeats;
    for (auto& m : markers)
    {
        int chosenLayer = -1;
        for (size_t l = 0; l < layerEndBeats.size(); ++l)
        {
            if (m.startBeat >= layerEndBeats[l] - 0.001)
            {
                chosenLayer = (int)l;
                layerEndBeats[l] = m.startBeat + m.lengthBeats;
                break;
            }
        }

        if (chosenLayer == -1)
        {
            chosenLayer = (int)layerEndBeats.size();
            layerEndBeats.push_back(m.startBeat + m.lengthBeats);
        }

        m.stackLayer = chosenLayer;
    }
}

void TrackModel::setChordDisplayMode(ChordDisplayMode mode)
{
    if (chordDisplayMode != mode)
    {
        chordDisplayMode = mode;
        triggerChordsChanged();
        triggerTracksChanged();
    }
}

void TrackModel::getModeAtBeat(double beat, int defaultTonicPc, const juce::String& defaultScaleName, int& outTonicPc, juce::String& outScaleName) const
{
    outTonicPc = defaultTonicPc;
    outScaleName = defaultScaleName;

    if (const auto* modeEv = findEffectiveModeAtBeat(beat))
    {
        outTonicPc = modeEv->tonicPc;
        if (modeEv->scaleName.isNotEmpty())
            outScaleName = modeEv->scaleName;
    }
}

bool TrackModel::mergeClips(int trackId, const std::vector<int64_t>& clipIds, juce::UndoManager* undoManager)
{
    auto* trk = getTrackById(trackId);
    if (!trk || trk->getType() != TrackType::Instrument || clipIds.size() < 2)
        return false;

    auto oldClips = trk->getClips();
    std::vector<MidiClip> toMerge;
    std::vector<MidiClip> remaining;

    for (const auto& c : oldClips)
    {
        if (std::find(clipIds.begin(), clipIds.end(), c.id) != clipIds.end())
            toMerge.push_back(c);
        else
            remaining.push_back(c);
    }

    if (toMerge.size() < 2)
        return false;

    double minStart = 1e9;
    double maxEnd = -1e9;
    std::vector<NoteEvent> mergedNotes;
    juce::String mergedName = toMerge.front().name + " (Merged)";
    juce::Colour mergedColor = toMerge.front().color;

    for (const auto& c : toMerge)
    {
        minStart = std::min(minStart, c.startBeat);
        maxEnd = std::max(maxEnd, c.startBeat + c.lengthBeats);
        for (const auto& n : c.notes)
        {
            mergedNotes.push_back(n);
        }
    }

    if (minStart > maxEnd) return false;

    MidiClip newClip;
    newClip.id = juce::Time::currentTimeMillis();
    newClip.name = mergedName;
    newClip.startBeat = minStart;
    newClip.lengthBeats = std::max(1.0, maxEnd - minStart);
    newClip.notes = mergedNotes;
    newClip.color = mergedColor;
    newClip.isSelected = true;

    auto newClips = remaining;
    newClips.push_back(newClip);

    std::sort(newClips.begin(), newClips.end(), [](const MidiClip& a, const MidiClip& b) {
        return a.startBeat < b.startBeat;
    });

    if (undoManager != nullptr)
    {
        UndoActionHelper::performAtomic(undoManager, new ModifyClipsUndoAction(*this, trackId, oldClips, newClips, "Merge MIDI Parts"), "Merge MIDI Parts");
    }
    else
    {
        trk->getClips() = newClips;
        triggerTracksChanged();
    }

    setActiveTrackId(trackId);
    setActiveClipId(newClip.id);
    return true;
}

} // namespace Harmonizer
