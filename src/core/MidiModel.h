#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_data_structures/juce_data_structures.h>
#include "ScaleDefinitions.h"
#include <vector>
#include <set>
#include <memory>
#include <atomic>
#include <algorithm>

namespace Harmonizer
{

enum class ToolType
{
    Select = 1,     // [1] Object selection / pointer
    Range = 2,      // [2] Range selection
    Split = 3,      // [3] Scissors
    Glue = 4,       // [4] Glue
    Erase = 5,      // [5] Eraser
    Zoom = 6,       // [6] Zoom
    Mute = 7,       // [7] Mute toggle
    Draw = 8        // [8] Pencil / Draw
};

enum class PlayheadFollowMode
{
    Off = 0,             // 不追踪
    Page = 1,            // 翻页方式
    ContinuousLeft = 2,  // 光标位于开头并追踪
    ContinuousCenter = 3 // 光标位于中心并追踪
};

enum class SnapDivision
{
    Bar,            // 1/1
    Half,           // 1/2
    Quarter,        // 1/4
    Eighth,         // 1/8
    Sixteenth,      // 1/16
    ThirtySecond,   // 1/32
    SixtyFourth     // 1/64
};

enum class ScaleType
{
    Major = 0,
    NaturalMinor,
    HarmonicMinor,
    MelodicMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian
};

struct KeySignature
{
    int rootPitchClass = 0; // 0 = C, 1 = C#, ... 11 = B
    ScaleType scale = ScaleType::Major;

    static juce::String getPitchClassName(int pc, bool useSharps = true);
    static juce::String getScaleTypeName(ScaleType st);
    static std::vector<int> getScaleIntervals(ScaleType st);
    bool isPitchInScale(int midiPitch) const;
};

struct NoteEvent
{
    int64_t id = 0;
    int pitch = 60;             // 0 - 127 (60 = C4)
    double startBeat = 0.0;     // Quarter-note beats from start of project
    double lengthBeats = 1.0;   // Duration in beats
    float velocity = 0.8f;      // 0.0f - 1.0f
    bool isSelected = false;
    bool isMuted = false;

    bool operator==(const NoteEvent& other) const { return id == other.id; }
};

class TrackModel;

class MidiModel
{
public:
    MidiModel();
    ~MidiModel() = default;
    void resetToDefaults();
    void replaceProjectData(const MidiModel& loaded);

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void noteListChanged() {}
        virtual void notePreviewChanged() { noteListChanged(); }
        virtual void selectionChanged() {}
        virtual void selectionPreviewChanged() { selectionChanged(); }
        virtual void projectSettingsChanged() {}
        virtual void toolChanged() {}
    };

    void addListener(Listener* l)       { listeners.add(l); }
    void removeListener(Listener* l)    { listeners.remove(l); }

    // Project metadata & transport settings
    double getBpm() const               { return bpm.load(); }
    void setBpm(double newBpm);

    int getTimeSigNumerator() const     { return timeSigNum; }
    int getTimeSigDenominator() const   { return timeSigDenom; }
    void setTimeSignature(int num, int denom);

    const KeySignature& getKeySignature() const { return keySig; }
    void setKeySignature(const KeySignature& ks);

    ColoringMode getColoringMode() const            { return coloringMode; }
    void setColoringMode(ColoringMode m);

    juce::String getScaleName() const               { return currentScaleName; }
    void setScaleName(const juce::String& name);

    KeyLayout getEffectiveLayoutAtBeat(double beat, const class TrackModel* trackModel = nullptr) const;

    double getLoopStart() const         { return loopStartBeat.load(); }
    double getLoopEnd() const           { return loopEndBeat.load(); }
    bool isLoopEnabled() const          { return loopEnabled.load(); }
    void setLoopRange(double startBeat, double endBeat);
    void setLoopEnabled(bool enabled);

    // Tool & snap settings
    ToolType getCurrentTool() const     { return currentTool; }
    void setCurrentTool(ToolType t);

    SnapDivision getSnapDivision() const { return snapDiv; }
    void setSnapDivision(SnapDivision sd);

    bool isSnapEnabled() const          { return snapEnabled; }
    void setSnapEnabled(bool enabled);

    bool isSnapTriplet() const          { return snapTriplet; }
    void setSnapTriplet(bool trip);

    PlayheadFollowMode getPlayheadFollowMode() const { return playheadFollowMode; }
    void setPlayheadFollowMode(PlayheadFollowMode mode);
    PlayheadFollowMode getLastEnabledPlayheadFollowMode() const { return lastEnabledPlayheadFollowMode; }
    void togglePlayheadFollow();

    double getSnapBeatInterval() const;
    double quantizeBeat(double beat) const;
    double snapBeatToCellStart(double beat) const;

    // Note operations
    const std::vector<NoteEvent>& getNotes() const { return notes; }
    std::vector<NoteEvent>& getNotes()             { return notes; }

    // Real-time audio thread-safe snapshot (P0-8)
    std::shared_ptr<const std::vector<NoteEvent>> getNotesSnapshot() const
    {
        const juce::SpinLock::ScopedLockType sl(snapshotLock);
        return notesSnapshot;
    }
    void updateSnapshot();

    int64_t generateNextNoteId() { return ++nextId; }

    void addNote(const NoteEvent& note, bool notify = true);
    void addNotes(const std::vector<NoteEvent>& newNotes, bool notify = true);
    void removeNoteById(int64_t id, bool notify = true);
    void removeNotesById(const std::vector<int64_t>& ids, bool notify = true);
    void updateNote(const NoteEvent& note, bool notify = true);
    void setAllNotes(const std::vector<NoteEvent>& newNotes, bool notify = true);
    void clearAllNotes(bool notify = true);

    const NoteEvent* findNoteById(int64_t id) const;
    NoteEvent* findNoteById(int64_t id);

    // Selection management
    void selectNote(int64_t id, bool addToExisting);
    void deselectNote(int64_t id);
    void selectAll();
    void deselectAll();
    bool selectWithinRange(double startBeat, double endBeat, int minPitch, int maxPitch,
                           bool addToExisting, bool notify = true);
    std::vector<NoteEvent> getSelectedNotes() const;
    std::vector<int64_t> getSelectedNoteIds() const;
    bool isNoteSelected(int64_t id) const;
    bool hasSelection() const { return !getSelectedNoteIds().empty(); }

    // Notifications
    void triggerNoteListChanged();
    // Drag previews may defer the audio snapshot until the UI batches pending frames.
    void triggerNotePreviewChanged(bool deferSnapshot = false);
    void triggerSelectionChanged();
    void triggerSelectionPreviewChanged();
    void triggerProjectSettingsChanged();

    // Clipboard & Editing
    void copySelectedNotes();
    void pasteNotes(double targetBeat, juce::UndoManager* um = nullptr, double clipStart = -1.0, double clipEnd = -1.0, TrackModel* tm = nullptr);
    void duplicateSelectedNotes(juce::UndoManager* um = nullptr, double clipStart = -1.0, double clipEnd = -1.0, TrackModel* tm = nullptr);
    void deleteSelectedNotes(juce::UndoManager* um = nullptr, TrackModel* tm = nullptr);

private:
    std::atomic<double> bpm { 120.0 };
    int timeSigNum = 4;
    int timeSigDenom = 4;
    KeySignature keySig;
    ColoringMode coloringMode = ColoringMode::ScaleAdaptive;
    juce::String currentScaleName = "Ionian (Natural Major)";

    std::atomic<double> loopStartBeat { 0.0 };
    std::atomic<double> loopEndBeat { 16.0 };
    std::atomic<bool> loopEnabled { true };

    ToolType currentTool = ToolType::Select;
    SnapDivision snapDiv = SnapDivision::Sixteenth;
    bool snapEnabled = true;
    bool snapTriplet = false;
    PlayheadFollowMode playheadFollowMode = PlayheadFollowMode::Off;
    PlayheadFollowMode lastEnabledPlayheadFollowMode = PlayheadFollowMode::Page;

    std::vector<NoteEvent> notes;
    std::vector<NoteEvent> clipboardNotes;
    int64_t nextId = 1000;

    mutable juce::SpinLock snapshotLock;
    std::shared_ptr<const std::vector<NoteEvent>> notesSnapshot;

    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiModel)
};

} // namespace Harmonizer
