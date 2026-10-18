#include "MidiModel.h"
#include "TrackModel.h"
#include "UndoActions.h"
#include <unordered_set>

namespace Harmonizer
{

juce::String KeySignature::getPitchClassName(int pc, bool useSharps)
{
    pc = (pc % 12 + 12) % 12;
    static const char* sharps[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    return useSharps ? sharps[pc] : flats[pc];
}

juce::String KeySignature::getScaleTypeName(ScaleType st)
{
    switch (st)
    {
        case ScaleType::Major:          return "Major (Ionian)";
        case ScaleType::NaturalMinor:   return "Natural Minor (Aeolian)";
        case ScaleType::HarmonicMinor:  return "Harmonic Minor";
        case ScaleType::MelodicMinor:   return "Melodic Minor";
        case ScaleType::Dorian:         return "Dorian";
        case ScaleType::Phrygian:       return "Phrygian";
        case ScaleType::Lydian:         return "Lydian";
        case ScaleType::Mixolydian:     return "Mixolydian";
        case ScaleType::Locrian:        return "Locrian";
        default:                        return "Major";
    }
}

std::vector<int> KeySignature::getScaleIntervals(ScaleType st)
{
    switch (st)
    {
        case ScaleType::Major:          return { 0, 2, 4, 5, 7, 9, 11 };
        case ScaleType::NaturalMinor:   return { 0, 2, 3, 5, 7, 8, 10 };
        case ScaleType::HarmonicMinor:  return { 0, 2, 3, 5, 7, 8, 11 };
        case ScaleType::MelodicMinor:   return { 0, 2, 3, 5, 7, 9, 11 };
        case ScaleType::Dorian:         return { 0, 2, 3, 5, 7, 9, 10 };
        case ScaleType::Phrygian:       return { 0, 1, 3, 5, 7, 8, 10 };
        case ScaleType::Lydian:         return { 0, 2, 4, 6, 7, 9, 11 };
        case ScaleType::Mixolydian:     return { 0, 2, 4, 5, 7, 9, 10 };
        case ScaleType::Locrian:        return { 0, 1, 3, 5, 6, 8, 10 };
        default:                        return { 0, 2, 4, 5, 7, 9, 11 };
    }
}

bool KeySignature::isPitchInScale(int midiPitch) const
{
    int pc = (midiPitch % 12 + 12) % 12;
    int relPc = (pc - rootPitchClass + 12) % 12;
    auto intervals = getScaleIntervals(scale);
    return std::find(intervals.begin(), intervals.end(), relPc) != intervals.end();
}

//==============================================================================
MidiModel::MidiModel()
{
    // Default 4-bar empty section with C Major
    keySig.rootPitchClass = 0;
    keySig.scale = ScaleType::Major;
    updateSnapshot();
}

void MidiModel::resetToDefaults()
{
    bpm = 120.0;
    timeSigNum = 4;
    timeSigDenom = 4;
    keySig = {};
    coloringMode = ColoringMode::ScaleAdaptive;
    currentScaleName = "Ionian (Natural Major)";
    loopStartBeat = 0.0;
    loopEndBeat = 16.0;
    loopEnabled = true;
    notes.clear();
    clipboardNotes.clear();
    nextId = 1000;
    triggerNoteListChanged();
    triggerProjectSettingsChanged();
}

void MidiModel::replaceProjectData(const MidiModel& loaded)
{
    bpm.store(loaded.bpm.load());
    timeSigNum = loaded.timeSigNum;
    timeSigDenom = loaded.timeSigDenom;
    keySig = loaded.keySig;
    coloringMode = loaded.coloringMode;
    currentScaleName = loaded.currentScaleName;
    loopStartBeat.store(loaded.loopStartBeat.load());
    loopEndBeat.store(loaded.loopEndBeat.load());
    loopEnabled.store(loaded.loopEnabled.load());
    notes = loaded.notes;
    clipboardNotes.clear();
    nextId = loaded.nextId;
    triggerNoteListChanged();
    triggerProjectSettingsChanged();
}

void MidiModel::setBpm(double newBpm)
{
    if (bpm != newBpm && newBpm >= 20.0 && newBpm <= 400.0)
    {
        bpm = newBpm;
        triggerProjectSettingsChanged();
    }
}

void MidiModel::setTimeSignature(int num, int denom)
{
    if (num > 0 && denom > 0 && (num != timeSigNum || denom != timeSigDenom))
    {
        timeSigNum = num;
        timeSigDenom = denom;
        triggerProjectSettingsChanged();
    }
}

void MidiModel::setKeySignature(const KeySignature& ks)
{
    keySig = ks;
    triggerProjectSettingsChanged();
}

void MidiModel::setColoringMode(ColoringMode m)
{
    if (coloringMode != m)
    {
        coloringMode = m;
        triggerProjectSettingsChanged();
    }
}

void MidiModel::setScaleName(const juce::String& name)
{
    if (currentScaleName != name)
    {
        currentScaleName = name;
        triggerProjectSettingsChanged();
    }
}

KeyLayout MidiModel::getEffectiveLayoutAtBeat(double beat, const TrackModel* trackModel) const
{
    if (trackModel != nullptr)
    {
        if (const auto* modeEv = trackModel->findEffectiveModeAtBeat(beat))
        {
            if (modeEv->scaleName.isNotEmpty())
            {
                return ScaleDefinitions::computeLayout(modeEv->tonicPc, modeEv->scaleName,
                                                       modeEv->tonic);
            }
        }
    }

    return ScaleDefinitions::computeLayout(keySig.rootPitchClass, currentScaleName);
}

void MidiModel::setLoopRange(double startBeat, double endBeat)
{
    loopStartBeat = std::max(0.0, std::min(startBeat, endBeat));
    loopEndBeat = std::max(0.0, std::max(startBeat, endBeat));
    triggerProjectSettingsChanged();
}

void MidiModel::setLoopEnabled(bool enabled)
{
    loopEnabled = enabled;
    triggerProjectSettingsChanged();
}

void MidiModel::setCurrentTool(ToolType t)
{
    if (currentTool != t)
    {
        currentTool = t;
        listeners.call([](Listener& l) { l.toolChanged(); });
    }
}

void MidiModel::setSnapDivision(SnapDivision sd)
{
    if (snapTriplet && sd == SnapDivision::Bar)
        sd = SnapDivision::Half;

    if (snapDiv != sd)
    {
        snapDiv = sd;
        triggerProjectSettingsChanged();
    }
}

void MidiModel::setSnapEnabled(bool enabled)
{
    if (snapEnabled != enabled)
    {
        snapEnabled = enabled;
        triggerProjectSettingsChanged();
    }
}

void MidiModel::setSnapTriplet(bool trip)
{
    if (snapTriplet != trip)
    {
        snapTriplet = trip;
        if (snapTriplet && snapDiv == SnapDivision::Bar)
            snapDiv = SnapDivision::Half;
        triggerProjectSettingsChanged();
    }
}

void MidiModel::setPlayheadFollowMode(PlayheadFollowMode mode)
{
    if (mode != PlayheadFollowMode::Off)
        lastEnabledPlayheadFollowMode = mode;
    if (playheadFollowMode != mode)
    {
        playheadFollowMode = mode;
        triggerProjectSettingsChanged();
    }
}

void MidiModel::togglePlayheadFollow()
{
    setPlayheadFollowMode(playheadFollowMode == PlayheadFollowMode::Off
                              ? lastEnabledPlayheadFollowMode : PlayheadFollowMode::Off);
}

double MidiModel::getSnapBeatInterval() const
{
    double base = 1.0;
    switch (snapDiv)
    {
        case SnapDivision::Bar:             base = timeSigNum * (4.0 / timeSigDenom); break;
        case SnapDivision::Half:            base = 2.0; break;
        case SnapDivision::Quarter:         base = 1.0; break;
        case SnapDivision::Eighth:          base = 0.5; break;
        case SnapDivision::Sixteenth:       base = 0.25; break;
        case SnapDivision::ThirtySecond:    base = 0.125; break;
        case SnapDivision::SixtyFourth:     base = 0.0625; break;
    }

    if (snapTriplet)
        base *= (2.0 / 3.0);

    return base;
}

double MidiModel::quantizeBeat(double beat) const
{
    if (!snapEnabled)
        return std::max(0.0, beat);

    if (snapTriplet)
    {
        double factor = 1.0;
        switch (snapDiv)
        {
            case SnapDivision::Bar:          factor = 3.0 / 8.0; break;
            case SnapDivision::Half:         factor = 3.0 / 4.0; break;
            case SnapDivision::Quarter:      factor = 1.5; break;
            case SnapDivision::Eighth:       factor = 3.0; break;
            case SnapDivision::Sixteenth:    factor = 6.0; break;
            case SnapDivision::ThirtySecond: factor = 12.0; break;
            case SnapDivision::SixtyFourth:  factor = 24.0; break;
        }
        return std::max(0.0, std::round(beat * factor) / factor);
    }

    double interval = getSnapBeatInterval();
    if (interval <= 0.0001)
        return beat;

    return std::max(0.0, std::round(beat / interval) * interval);
}

double MidiModel::snapBeatToCellStart(double beat) const
{
    if (!snapEnabled)
        return std::max(0.0, beat);

    if (snapTriplet)
    {
        double factor = 1.0;
        switch (snapDiv)
        {
            case SnapDivision::Bar:          factor = 3.0 / 8.0; break;
            case SnapDivision::Half:         factor = 3.0 / 4.0; break;
            case SnapDivision::Quarter:      factor = 1.5; break;
            case SnapDivision::Eighth:       factor = 3.0; break;
            case SnapDivision::Sixteenth:    factor = 6.0; break;
            case SnapDivision::ThirtySecond: factor = 12.0; break;
            case SnapDivision::SixtyFourth:  factor = 24.0; break;
        }
        return std::max(0.0, std::floor(beat * factor + 1e-5) / factor);
    }

    double interval = getSnapBeatInterval();
    if (interval <= 0.0001)
        return beat;

    return std::max(0.0, std::floor(beat / interval + 1e-5) * interval);
}

//==============================================================================
void MidiModel::addNote(const NoteEvent& note, bool notify)
{
    NoteEvent n = note;
    if (n.id == 0)
        n.id = generateNextNoteId();
    else if (n.id >= nextId)
        nextId = n.id;

    notes.push_back(n);

    if (notify)
        triggerNoteListChanged();
}

void MidiModel::addNotes(const std::vector<NoteEvent>& newNotes, bool notify)
{
    for (auto n : newNotes)
    {
        if (n.id == 0)
            n.id = generateNextNoteId();
        else if (n.id >= nextId)
            nextId = n.id;
        notes.push_back(n);
    }

    if (notify)
        triggerNoteListChanged();
}

void MidiModel::removeNoteById(int64_t id, bool notify)
{
    auto it = std::remove_if(notes.begin(), notes.end(), [id](const NoteEvent& n) { return n.id == id; });
    if (it != notes.end())
    {
        notes.erase(it, notes.end());
        if (notify)
            triggerNoteListChanged();
    }
}

void MidiModel::removeNotesById(const std::vector<int64_t>& ids, bool notify)
{
    if (ids.empty()) return;
    std::unordered_set<int64_t> idSet(ids.begin(), ids.end());
    auto it = std::remove_if(notes.begin(), notes.end(), [&](const NoteEvent& n) { return idSet.count(n.id) > 0; });
    if (it != notes.end())
    {
        notes.erase(it, notes.end());
        if (notify)
            triggerNoteListChanged();
    }
}

void MidiModel::updateNote(const NoteEvent& note, bool notify)
{
    for (auto& n : notes)
    {
        if (n.id == note.id)
        {
            n = note;
            if (notify)
                triggerNoteListChanged();
            return;
        }
    }
}

void MidiModel::setAllNotes(const std::vector<NoteEvent>& newNotes, bool notify)
{
    notes = newNotes;
    for (const auto& n : notes)
        if (n.id >= nextId)
            nextId = n.id;

    if (notify)
        triggerNoteListChanged();
}

void MidiModel::clearAllNotes(bool notify)
{
    notes.clear();
    if (notify)
        triggerNoteListChanged();
}

const NoteEvent* MidiModel::findNoteById(int64_t id) const
{
    for (const auto& n : notes)
        if (n.id == id)
            return &n;
    return nullptr;
}

NoteEvent* MidiModel::findNoteById(int64_t id)
{
    for (auto& n : notes)
        if (n.id == id)
            return &n;
    return nullptr;
}

//==============================================================================
void MidiModel::selectNote(int64_t id, bool addToExisting)
{
    bool changed = false;
    for (auto& n : notes)
    {
        if (n.id == id)
        {
            if (!n.isSelected)
            {
                n.isSelected = true;
                changed = true;
            }
        }
        else if (!addToExisting && n.isSelected)
        {
            n.isSelected = false;
            changed = true;
        }
    }

    if (changed)
        triggerSelectionChanged();
}

void MidiModel::deselectNote(int64_t id)
{
    for (auto& n : notes)
    {
        if (n.id == id && n.isSelected)
        {
            n.isSelected = false;
            triggerSelectionChanged();
            return;
        }
    }
}

void MidiModel::selectAll()
{
    bool changed = false;
    for (auto& n : notes)
    {
        if (!n.isSelected)
        {
            n.isSelected = true;
            changed = true;
        }
    }
    if (changed)
        triggerSelectionChanged();
}

void MidiModel::deselectAll()
{
    bool changed = false;
    for (auto& n : notes)
    {
        if (n.isSelected)
        {
            n.isSelected = false;
            changed = true;
        }
    }
    if (changed)
        triggerSelectionChanged();
}

bool MidiModel::selectWithinRange(double startBeat, double endBeat, int minPitch, int maxPitch,
                                  bool addToExisting, bool notify)
{
    if (startBeat > endBeat) std::swap(startBeat, endBeat);
    if (minPitch > maxPitch) std::swap(minPitch, maxPitch);

    bool changed = false;
    for (auto& n : notes)
    {
        double noteEnd = n.startBeat + n.lengthBeats;
        bool inRange = (n.startBeat < endBeat && noteEnd > startBeat && n.pitch >= minPitch && n.pitch <= maxPitch);

        if (inRange)
        {
            if (!n.isSelected)
            {
                n.isSelected = true;
                changed = true;
            }
        }
        else if (!addToExisting)
        {
            if (n.isSelected)
            {
                n.isSelected = false;
                changed = true;
            }
        }
    }

    if (changed && notify)
        triggerSelectionChanged();
    return changed;
}

std::vector<NoteEvent> MidiModel::getSelectedNotes() const
{
    std::vector<NoteEvent> sel;
    for (const auto& n : notes)
        if (n.isSelected)
            sel.push_back(n);
    return sel;
}

std::vector<int64_t> MidiModel::getSelectedNoteIds() const
{
    std::vector<int64_t> sel;
    for (const auto& n : notes)
        if (n.isSelected)
            sel.push_back(n.id);
    return sel;
}

bool MidiModel::isNoteSelected(int64_t id) const
{
    const auto* n = findNoteById(id);
    return n != nullptr && n->isSelected;
}

//==============================================================================
void MidiModel::copySelectedNotes()
{
    clipboardNotes.clear();
    for (const auto& n : notes)
        if (n.isSelected)
            clipboardNotes.push_back(n);
}

void MidiModel::pasteNotes(double targetBeat, juce::UndoManager* um, double clipStart, double clipEnd, TrackModel* tm)
{
    if (clipboardNotes.empty())
        return;

    double minBeat = 1e9;
    for (const auto& n : clipboardNotes)
        minBeat = std::min(minBeat, n.startBeat);

    double beatOffset = targetBeat - minBeat;

    deselectAll();

    std::vector<NoteEvent> toAdd;
    for (auto n : clipboardNotes)
    {
        n.id = generateNextNoteId();
        n.startBeat = std::max(0.0, n.startBeat + beatOffset);

        if (clipStart >= 0.0 && clipEnd > clipStart)
        {
            if (n.startBeat < clipStart)
                n.startBeat = clipStart;
            if (n.startBeat + n.lengthBeats > clipEnd)
            {
                n.lengthBeats = clipEnd - n.startBeat;
                if (n.lengthBeats <= 0.05)
                    continue;
            }
        }

        n.isSelected = true;
        toAdd.push_back(n);
    }

    if (toAdd.empty())
        return;

    if (um != nullptr)
        UndoActionHelper::performAtomic(um, new AddNotesUndoAction(*this, toAdd, tm), "Paste Notes");
    else
    {
        addNotes(toAdd);
        if (tm != nullptr)
            tm->syncActiveClipNotes(getNotes());
    }
}

void MidiModel::duplicateSelectedNotes(juce::UndoManager* um, double clipStart, double clipEnd, TrackModel* tm)
{
    auto selected = getSelectedNotes();
    if (selected.empty())
        return;

    double minBeat = 1e9;
    double maxBeat = -1e9;
    for (const auto& n : selected)
    {
        minBeat = std::min(minBeat, n.startBeat);
        maxBeat = std::max(maxBeat, n.startBeat + n.lengthBeats);
    }

    double totalSpan = maxBeat - minBeat;
    double quantSpan = quantizeBeat(totalSpan);
    if (quantSpan <= 0.0)
        quantSpan = getSnapBeatInterval();

    deselectAll();

    std::vector<NoteEvent> toAdd;
    for (auto n : selected)
    {
        n.id = generateNextNoteId();
        n.startBeat += quantSpan;

        if (clipStart >= 0.0 && clipEnd > clipStart)
        {
            if (n.startBeat < clipStart)
                n.startBeat = clipStart;
            if (n.startBeat + n.lengthBeats > clipEnd)
            {
                n.lengthBeats = clipEnd - n.startBeat;
                if (n.lengthBeats <= 0.05)
                    continue;
            }
        }

        n.isSelected = true;
        toAdd.push_back(n);
    }

    if (toAdd.empty())
        return;

    if (um != nullptr)
        UndoActionHelper::performAtomic(um, new AddNotesUndoAction(*this, toAdd, tm), "Duplicate Notes");
    else
    {
        addNotes(toAdd);
        if (tm != nullptr)
            tm->syncActiveClipNotes(getNotes());
    }
}

void MidiModel::deleteSelectedNotes(juce::UndoManager* um, TrackModel* tm)
{
    auto selected = getSelectedNotes();
    if (selected.empty())
        return;

    if (um != nullptr)
        UndoActionHelper::performAtomic(um, new DeleteNotesUndoAction(*this, selected, tm), "Delete Notes");
    else
    {
        std::vector<int64_t> ids;
        for (const auto& n : selected)
            ids.push_back(n.id);
        removeNotesById(ids, true);
        if (tm != nullptr)
            tm->syncActiveClipNotes(getNotes());
    }
}

void MidiModel::updateSnapshot()
{
    auto newSnap = std::make_shared<const std::vector<NoteEvent>>(notes);
    const juce::SpinLock::ScopedLockType sl(snapshotLock);
    notesSnapshot = newSnap;
}

//==============================================================================
void MidiModel::triggerNoteListChanged()
{
    updateSnapshot();
    listeners.call([](Listener& l) { l.noteListChanged(); });
}

void MidiModel::triggerNotePreviewChanged(bool deferSnapshot)
{
    if (!deferSnapshot)
        updateSnapshot();
    listeners.call([](Listener& l) { l.notePreviewChanged(); });
}

void MidiModel::triggerSelectionChanged()
{
    listeners.call([](Listener& l) { l.selectionChanged(); });
}

void MidiModel::triggerSelectionPreviewChanged()
{
    listeners.call([](Listener& l) { l.selectionPreviewChanged(); });
}

void MidiModel::triggerProjectSettingsChanged()
{
    listeners.call([](Listener& l) { l.projectSettingsChanged(); });
}

} // namespace Harmonizer
