#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include "MidiModel.h"
#include <vector>
#include <memory>

namespace juce
{
    class UndoManager;
}

namespace Harmonizer
{

enum class TrackType
{
    Instrument = 0,
    Chord = 1,
    Marker = 2,
    TimeSignature = 3,
    Mode = 4,
    Folder = 5,
    Tempo = 6,
    Audio = 7
};

struct MidiClip
{
    int64_t id = 0;
    juce::String name = "MIDI Part";
    double startBeat = 0.0;
    double lengthBeats = 16.0;
    std::vector<NoteEvent> notes;
    juce::Colour color = juce::Colour(0xff2575fc);
    bool hasCustomColor = false;
    bool isMuted = false;
    bool isSelected = false;
};

struct AudioClip
{
    int64_t id = 0;
    juce::String name;
    juce::String sourcePath;
    double startBeat = 0.0;
    double lengthBeats = 1.0;
    double sourceOffsetSeconds = 0.0;
    double durationSeconds = 0.0;
    bool isMuted = false;
    bool isSelected = false;
};

// Flattened note representation for real-time multi-track audio playback
struct PlaybackNoteEvent
{
    int64_t id = 0;
    int trackId = 0;
    int pitch = 60;
    double startBeat = 0.0;
    double lengthBeats = 1.0;
    float velocity = 0.8f;
    bool isMuted = false;
};

enum class ChordDisplayMode
{
    NameAndRoman = 0,    // 和弦名 + 罗马级数 (Cmaj7 / Imaj7)
    NameOnly,            // 仅和弦名 (Cmaj7)
    RomanOnly,           // 仅罗马级数 (Imaj7)
    RomanAndQuality      // 罗马级数 + 和弦性质 (I maj7)
};

struct ChordEvent
{
    int64_t id = 0;
    double startBeat = 0.0;
    double lengthBeats = 4.0;
    int root = 0;
    juce::String chordName = "C";
    juce::String romanNumeral = "I";
    juce::Colour color = juce::Colour(0xfff59e0b);
    bool isSelected = false;
};

// Mode / Tonality event on dedicated Mode Track
struct ModeEvent
{
    int64_t id = 0;
    double startBeat = 0.0;
    double lengthBeats = 16.0;
    juce::String name = "C Major";
    juce::String tonic = "C";
    int tonicPc = 0;
    juce::String scaleName = "Ionian (Natural Major)";
    juce::Colour color = juce::Colour(0xff10b981); // Emerald
    bool isSelected = false;
};

// Cubase-style Timeline Marker event
struct MarkerEvent
{
    juce::String id;
    double startBeat = 0.0;
    double lengthBeats = 8.0;
    juce::String name = "Marker";
    juce::Colour color = juce::Colour(0xff06b6d4);
    int stackLayer = 0; // for vertical stratified stacking when overlapping
    bool isSelected = false;
};

struct TimeSignatureEvent
{
    int64_t id = 0;
    double startBeat = 0.0;
    double lengthBeats = 16.0;
    int numerator = 4;
    int denominator = 4;
    juce::Colour color = juce::Colour(0xff8b5cf6); // Purple
    bool isSelected = false;
};

enum class TempoTransitionType
{
    Jump = 0, // Step change
    Ramp = 1  // Linear transition to next tempo point
};

struct TempoEvent
{
    int64_t id = 0;
    double startBeat = 0.0;
    double bpm = 120.0;
    TempoTransitionType type = TempoTransitionType::Jump;
    juce::Colour color = juce::Colour(0xff3b82f6); // Cubase Blue
    bool isSelected = false;
};

class Track
{
public:
    Track(int id, const juce::String& name, TrackType type);
    virtual ~Track() = default;

    int getId() const                      { return trackId; }
    juce::String getName() const           { return trackName; }
    void setName(const juce::String& n)    { trackName = n; }
    TrackType getType() const              { return trackType; }
    juce::Colour getColor() const          { return trackColor; }
    void setColor(const juce::Colour& c)
    {
        trackColor = c;
        for (auto& clip : clips)
        {
            if (!clip.hasCustomColor)
                clip.color = c;
        }
    }

    bool isMuted() const                   { return muted; }
    void setMuted(bool m)                  { muted = m; }
    bool isSolo() const                    { return solo; }
    void setSolo(bool s)                   { solo = s; }

    bool isLocked() const                  { return locked; }
    void setLocked(bool l)                 { locked = l; }
    bool isHarmonizeEnabled() const        { return harmonizeEnabled; }
    void setHarmonizeEnabled(bool h)       { harmonizeEnabled = h; }
    bool isSelected() const                { return selected; }
    void setSelected(bool s)               { selected = s; }

    // Channel Strip Controls (Instrument and Audio Tracks)
    float getVolume() const                { return volume; }
    void setVolume(float v)                { volume = std::clamp(v, 0.0f, 2.0f); }
    float getPan() const                   { return pan; }
    void setPan(float p)                   { pan = std::clamp(p, -1.0f, 1.0f); }
    juce::String getAssignedVstName() const { return assignedVstName; }
    void setAssignedVstName(const juce::String& name) { assignedVstName = name; }
    const juce::String& getAssignedVstIdentifier() const { return assignedVstIdentifier; }
    void setAssignedVstIdentifier(const juce::String& identifier) { assignedVstIdentifier = identifier; }
    const juce::MemoryBlock& getAssignedVstState() const { return assignedVstState; }
    void setAssignedVstState(const juce::MemoryBlock& mb) { assignedVstState = mb; }

    // Instrument Track Clips
    std::vector<MidiClip>& getClips()      { return clips; }
    const std::vector<MidiClip>& getClips() const { return clips; }
    void addClip(const MidiClip& clip)     { clips.push_back(clip); }
    std::vector<AudioClip>& getAudioClips() { return audioClips; }
    const std::vector<AudioClip>& getAudioClips() const { return audioClips; }
    bool hasSelectedClips() const
    {
        for (const auto& c : clips)
            if (c.isSelected) return true;
        return false;
    }

    // Chord Track Events
    std::vector<ChordEvent>& getChords()   { return chords; }
    const std::vector<ChordEvent>& getChords() const { return chords; }

    // Mode Track Events
    std::vector<ModeEvent>& getModes()     { return modes; }
    const std::vector<ModeEvent>& getModes() const { return modes; }

    // Marker Track Events
    std::vector<MarkerEvent>& getMarkers() { return markers; }
    const std::vector<MarkerEvent>& getMarkers() const { return markers; }

    // Time Signature Track Events
    std::vector<TimeSignatureEvent>& getTimeSignatures() { return timeSignatures; }
    const std::vector<TimeSignatureEvent>& getTimeSignatures() const { return timeSignatures; }

    // Tempo Track Events
    std::vector<TempoEvent>& getTempoEvents() { return tempoEvents; }
    const std::vector<TempoEvent>& getTempoEvents() const { return tempoEvents; }

    // Auxiliary mode marker track for individual instrument track
    bool hasAuxiliaryMarkerTrack() const   { return hasAuxMarker; }
    void setHasAuxiliaryMarkerTrack(bool b){ hasAuxMarker = b; }
    std::vector<ModeEvent>& getAuxModes()  { return auxModes; }
    const std::vector<ModeEvent>& getAuxModes() const { return auxModes; }

    // Parent track linkage for auxiliary / sub-tracks (-1 if standalone)
    int getParentTrackId() const           { return parentTrackId; }
    void setParentTrackId(int pid)         { parentTrackId = pid; }

    // Folder track hierarchy & properties
    int getParentFolderId() const          { return parentFolderId; }
    void setParentFolderId(int fid)        { parentFolderId = fid; }
    bool isFolderCollapsed() const         { return folderCollapsed; }
    void setFolderCollapsed(bool c)        { folderCollapsed = c; }
    const juce::String& getFolderNotes() const { return folderNotes; }
    void setFolderNotes(const juce::String& notes) { folderNotes = notes; }

    std::unique_ptr<Track> clone(int newTrackId) const
    {
        auto copy = std::make_unique<Track>(newTrackId, trackName, trackType);
        copy->setColor(trackColor);
        copy->setMuted(muted);
        copy->setSolo(solo);
        copy->setLocked(locked);
        copy->setHarmonizeEnabled(harmonizeEnabled);
        copy->setVolume(volume);
        copy->setPan(pan);
        copy->setAssignedVstName(assignedVstName);
        copy->setAssignedVstIdentifier(assignedVstIdentifier);
        copy->setAssignedVstState(assignedVstState);
        copy->setHasAuxiliaryMarkerTrack(hasAuxMarker);
        copy->setParentTrackId(parentTrackId);
        copy->setParentFolderId(parentFolderId);
        copy->setFolderCollapsed(folderCollapsed);
        copy->setFolderNotes(folderNotes);
        copy->setSelected(selected);
        copy->getAuxModes() = auxModes;
        copy->getClips() = clips;
        copy->getAudioClips() = audioClips;
        copy->getChords() = chords;
        copy->getModes() = modes;
        copy->getMarkers() = markers;
        copy->getTimeSignatures() = timeSignatures;
        copy->getTempoEvents() = tempoEvents;
        return copy;
    }

private:
    int trackId = 0;
    juce::String trackName;
    TrackType trackType;
    juce::Colour trackColor;
    bool muted = false;
    bool solo = false;
    bool locked = false;
    bool harmonizeEnabled = false;
    bool selected = false;
    float volume = 1.0f;
    float pan = 0.0f;
    juce::String assignedVstName;
    juce::String assignedVstIdentifier;
    juce::MemoryBlock assignedVstState;
    int parentTrackId = -1;
    int parentFolderId = -1;
    bool folderCollapsed = false;
    juce::String folderNotes;

    bool hasAuxMarker = false;
    std::vector<ModeEvent> auxModes;

    std::vector<MidiClip> clips;
    std::vector<AudioClip> audioClips;
    std::vector<ChordEvent> chords;
    std::vector<ModeEvent> modes;
    std::vector<MarkerEvent> markers;
    std::vector<TimeSignatureEvent> timeSignatures;
    std::vector<TempoEvent> tempoEvents;
};

struct BarSpan
{
    int barIndex = 1;          // 1-based bar number
    double startBeat = 0.0;
    double endBeat = 4.0;
    double lengthBeats = 4.0;
    int numerator = 4;
    int denominator = 4;
    double getLengthBeats() const { return lengthBeats; }
};

class TrackModel
{
public:
    TrackModel();

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void tracksChanged() {}
        virtual void clipNotesChanged() {}
        virtual void clipNotesCommitted() { clipNotesChanged(); }
        virtual void markersChanged() {}
        virtual void modesChanged() {}
        virtual void chordsChanged() {}
        virtual void timeSignaturesChanged() {}
        virtual void tempoChanged() {}
        virtual void activeClipChanged() {}
        virtual void trackWillBeRemoved(Track&) {}
        virtual void trackWillBeDuplicated(Track&) {}
        virtual void projectWillReplace() {}
    };

    void addListener(Listener* l)       { listeners.add(l); }
    void removeListener(Listener* l)    { listeners.remove(l); }

    std::vector<std::unique_ptr<Track>>& getTracks() { return tracks; }
    const std::vector<std::unique_ptr<Track>>& getTracks() const { return tracks; }

    Track* getTrackById(int id);
    const Track* getTrackById(int id) const;
    Track* getActiveInstrumentTrack();
    Track* getChordTrack();
    Track* getModeTrack();
    // Update every mode block intersecting [startBeat, endBeat). If there are
    // none, append one block covering that range. Existing block timing stays
    // intact so adjacent sections are not silently resized.
    static bool applyModeToRange(std::vector<ModeEvent>& modes, double startBeat,
                                 double endBeat, const ModeEvent& requested);
    Track* getMarkerTrack();
    Track* getTimeSignatureTrack();
    const Track* getTimeSignatureTrack() const;
    Track* getTempoTrack();
    const Track* getTempoTrack() const;

    double getTempoAtBeat(double beat, double fallbackBpm = 120.0) const;
    double getSecondsBetweenBeats(double startBeat, double endBeat, double fallbackBpm = 120.0) const;
    double getBeatAfterSeconds(double startBeat, double seconds, double fallbackBpm = 120.0) const;
    void setFallbackTempoBpm(double bpm);

    Track* addTrack(const juce::String& name, TrackType type);
    static double getAudioFileDuration(const juce::File& file);
    bool importAudioClip(int trackId, const juce::File& file, double startBeat,
                         double fallbackBpm, juce::UndoManager* undoManager = nullptr);
    bool replaceAudioClipSource(int trackId, int64_t clipId, const juce::File& file,
                                double fallbackBpm, juce::UndoManager* undoManager = nullptr);
    bool splitMidiClip(int trackId, int64_t clipId, double splitBeat,
                       juce::UndoManager* undoManager = nullptr);
    bool splitAudioClip(int trackId, int64_t clipId, double splitBeat, double fallbackBpm,
                        juce::UndoManager* undoManager = nullptr);
    Track* insertTrack(int index, std::unique_ptr<Track> track);
    std::unique_ptr<Track> extractTrack(int id, int& outIndex);
    void removeTrack(int id);

    bool moveTrack(int fromIndex, int toIndex);
    bool moveTrackUp(int id, juce::UndoManager* um = nullptr);
    bool moveTrackDown(int id, juce::UndoManager* um = nullptr);
    Track* duplicateTrack(int id);
    int getTrackIndex(int id) const;

    // Folder track hierarchy helpers
    int getTrackDepth(int trackId) const;
    int getSubtreeDepth(int trackId) const;
    bool canNestInFolder(int trackId, int targetFolderId) const;
    bool setTrackParent(int trackId, int folderId);
    void repairFolderHierarchy();
    bool isTrackEffectivelyVisible(int trackId) const;
    std::vector<int> getChildTrackIds(int folderId, bool recursive = true) const;
    std::vector<Track*> getVisibleTracks() const;

    // Dynamic Time Signature timeline engine
    void setDefaultTimeSignature(int num, int den) { defaultTimeSigNumerator = num; defaultTimeSigDenominator = den; }
    int getDefaultTimeSigNumerator() const { return defaultTimeSigNumerator; }
    int getDefaultTimeSigDenominator() const { return defaultTimeSigDenominator; }
    std::vector<BarSpan> getBarTimeline(double maxBeat = 64.0, int fallbackNumerator = -1, int fallbackDenominator = -1) const;
    std::vector<BarSpan> getBarTimelineRange(double firstBeat, double lastBeat,
        int fallbackNumerator = -1, int fallbackDenominator = -1) const;
    BarSpan getBarAtBeat(double beat) const;
    int beatToBarNumber(double beat) const;
    double barToStartBeat(int barNumber) const;

    // Anti-overlap collision validator for non-marker tracks
    bool canPlaceEvent(int trackId, double startBeat, double lengthBeats, int64_t ignoreSelfId = -1, const juce::String& ignoreSelfStrId = "") const;

    // Arranger event selection & batch operations
    void clearAllArrangerSelection();
    void selectAllArrangerEvents();
    int getSelectedArrangerEventCount() const;

    // Track-level selection
    void clearAllTrackSelection()
    {
        for (auto& t : tracks)
            t->setSelected(false);
    }
    int getSelectedTrackCount() const
    {
        int count = 0;
        for (const auto& t : tracks)
            if (t->isSelected()) count++;
        return count;
    }

    // Active editing clip in Piano Roll
    int getActiveTrackId() const        { return activeTrackId; }
    void setActiveTrackId(int id);

    int64_t getActiveClipId() const     { return activeClipId; }
    void setActiveClipId(int64_t clipId);

    void setActiveClip(int trackId, int64_t clipId, MidiModel* midiModelToSync = nullptr);
    // During stopped drag previews, clip data stays current while playback
    // publication can wait until the next preview flush or Play.
    void syncActiveClipNotes(const std::vector<NoteEvent>& notes,
                             bool refreshPlaybackSnapshot = true);

    // Merge / Glue multiple clips on an Instrument Track
    bool mergeClips(int trackId, const std::vector<int64_t>& clipIds, juce::UndoManager* undoManager = nullptr);

    // Chord Display Mode
    ChordDisplayMode getChordDisplayMode() const { return chordDisplayMode; }
    void setChordDisplayMode(ChordDisplayMode mode);

    int getBarNumberOffset() const      { return barNumberOffset; }
    void setBarNumberOffset(int offset);

    MidiClip* getActiveClip();
    const MidiClip* getActiveClip() const;
    void validateActiveClip();

    // Context effective mode lookup at given beat (Mode Track only)
    const ModeEvent* findEffectiveModeAtBeat(double beat) const;
    void getModeAtBeat(double beat, int defaultTonicPc, const juce::String& defaultScaleName, int& outTonicPc, juce::String& outScaleName) const;

    // Cubase-style overlapping marker stratification helper
    static void computeMarkerStackLayers(std::vector<MarkerEvent>& markers);

    // Multi-track visibility in Piano Roll
    bool isPianoRollTrackVisible(int trackId) const
    {
        return pianoRollVisibleTrackIds.find(trackId) != pianoRollVisibleTrackIds.end();
    }
    void setPianoRollTrackVisible(int trackId, bool visible)
    {
        if (visible)
            pianoRollVisibleTrackIds.insert(trackId);
        else
            pianoRollVisibleTrackIds.erase(trackId);
        triggerTracksChanged();
    }
    const std::set<int>& getPianoRollVisibleTrackIds() const { return pianoRollVisibleTrackIds; }
    void clearPianoRollVisibleTracks()
    {
        pianoRollVisibleTrackIds.clear();
        triggerTracksChanged();
    }

    // Mode Track Override Enabled (if false, global scale is used everywhere)
    bool isModeTrackOverrideEnabled() const { return modeTrackOverrideEnabled; }
    void setModeTrackOverrideEnabled(bool en)
    {
        if (modeTrackOverrideEnabled != en)
        {
            modeTrackOverrideEnabled = en;
            triggerModesChanged();
        }
    }

    // Thread-safe multi-track playback snapshot
    std::shared_ptr<const std::vector<PlaybackNoteEvent>> getPlaybackSnapshot() const
    {
        const juce::SpinLock::ScopedLockType sl(playbackSnapshotLock);
        return playbackSnapshot;
    }
    void updatePlaybackSnapshot();
    void refreshPlaybackSnapshotIfDirty()
    {
        if (playbackSnapshotDirty)
            updatePlaybackSnapshot();
    }
    void updateTempoSnapshot();
    void resetToDefaults();
    void replaceProjectData(TrackModel& loaded);

    void triggerTracksChanged()
    {
        validateActiveClip();
        updatePlaybackSnapshot();
        updateTempoSnapshot();
        listeners.call([](Listener& l) { l.tracksChanged(); });
    }
    void triggerClipNotesCommitted()
    {
        updatePlaybackSnapshot();
        listeners.call([](Listener& l) { l.clipNotesCommitted(); });
    }
    void triggerMarkersChanged()        { listeners.call([](Listener& l) { l.markersChanged(); }); }
    void triggerModesChanged()          { listeners.call([](Listener& l) { l.modesChanged(); }); }
    void triggerChordsChanged()         { listeners.call([](Listener& l) { l.chordsChanged(); }); }
    void triggerTimeSignaturesChanged() { listeners.call([](Listener& l) { l.timeSignaturesChanged(); }); }
    void triggerTempoChanged()          { updateTempoSnapshot(); listeners.call([](Listener& l) { l.tempoChanged(); }); }

private:
    std::vector<std::unique_ptr<Track>> tracks;
    int nextTrackId = 1;
    int activeTrackId = 1;
    int64_t activeClipId = 1;
    int barNumberOffset = 0;
    ChordDisplayMode chordDisplayMode = ChordDisplayMode::NameAndRoman;
    std::set<int> pianoRollVisibleTrackIds;
    bool modeTrackOverrideEnabled = true;
    int defaultTimeSigNumerator = 4;
    int defaultTimeSigDenominator = 4;
    double fallbackTempoBpm = 120.0;

    mutable juce::SpinLock playbackSnapshotLock;
    std::shared_ptr<const std::vector<PlaybackNoteEvent>> playbackSnapshot;
    bool playbackSnapshotDirty = false;
    mutable juce::SpinLock tempoSnapshotLock;
    std::shared_ptr<const std::vector<TempoEvent>> tempoSnapshot;

    juce::ListenerList<Listener> listeners;
};

} // namespace Harmonizer
