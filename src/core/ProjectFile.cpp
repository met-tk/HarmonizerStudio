#include "ProjectFile.h"
#include "TrackModel.h"
#include "UndoActions.h"
#include <cmath>
#include <deque>
#include <map>
#include <limits>
#include <tuple>

namespace Harmonizer
{

static constexpr size_t maxProjectFileBytes = 256u * 1024u * 1024u;
static constexpr double maxImportedTrackId = 1.0e9;

static int pitchClassFromName(const juce::String& name)
{
    for (int pc = 0; pc < 12; ++pc)
        if (name.equalsIgnoreCase(KeySignature::getPitchClassName(pc, true))
            || name.equalsIgnoreCase(KeySignature::getPitchClassName(pc, false)))
            return pc;
    return 0;
}

static bool decodePluginState(const juce::String& encoded, juce::MemoryBlock& destination)
{
    constexpr size_t maxStateBytes = 128u * 1024u * 1024u;
    const int separator = encoded.indexOfChar('.');
    if (separator <= 0)
        return false;

    size_t stateBytes = 0;
    auto prefixCharacter = encoded.getCharPointer();
    for (int i = 0; i < separator; ++i)
    {
        const auto c = prefixCharacter.getAndAdvance();
        if (c < '0' || c > '9')
            return false;
        stateBytes = stateBytes * 10u + (size_t)(c - '0');
        if (stateBytes > maxStateBytes)
            return false;
    }

    const size_t expectedCharacters = (stateBytes * 8u + 5u) / 6u;
    const int encodedLength = encoded.length();
    if ((size_t)(encodedLength - separator - 1) != expectedCharacters)
        return false;
    auto character = encoded.getCharPointer() + separator + 1;
    for (size_t i = 0; i < expectedCharacters; ++i)
    {
        const auto c = character.getAndAdvance();
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
              || (c >= '0' && c <= '9') || c == '+' || c == '.'))
            return false;
    }
    return destination.fromBase64Encoding(encoded);
}

bool ProjectFile::saveProject(const MidiModel& model, const juce::File& targetFile, const TrackModel* trackModel)
{
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty("version", 5);
    obj->setProperty("bpm", model.getBpm());
    obj->setProperty("timeSigNum", model.getTimeSigNumerator());
    obj->setProperty("timeSigDenom", model.getTimeSigDenominator());
    obj->setProperty("keyRoot", model.getKeySignature().rootPitchClass);
    obj->setProperty("keyScale", (int)model.getKeySignature().scale);
    obj->setProperty("scaleName", model.getScaleName());
    obj->setProperty("coloringMode", (int)model.getColoringMode());
    obj->setProperty("loopStart", model.getLoopStart());
    obj->setProperty("loopEnd", model.getLoopEnd());
    obj->setProperty("loopEnabled", model.isLoopEnabled());

    juce::Array<juce::var> notesArray;
    for (const auto& note : model.getNotes())
    {
        auto nObj = std::make_unique<juce::DynamicObject>();
        nObj->setProperty("id", (juce::int64)note.id);
        nObj->setProperty("pitch", note.pitch);
        nObj->setProperty("startBeat", note.startBeat);
        nObj->setProperty("lengthBeats", note.lengthBeats);
        nObj->setProperty("velocity", note.velocity);
        nObj->setProperty("muted", note.isMuted);
        notesArray.add(juce::var(nObj.release()));
    }
    obj->setProperty("notes", notesArray);

    if (trackModel != nullptr)
    {
        obj->setProperty("activeTrackId", trackModel->getActiveTrackId());
        obj->setProperty("activeClipId", (juce::int64)trackModel->getActiveClipId());
        obj->setProperty("barNumberOffset", trackModel->getBarNumberOffset());
        obj->setProperty("chordDisplayMode", (int)trackModel->getChordDisplayMode());
        obj->setProperty("defaultTimeSigNum", trackModel->getDefaultTimeSigNumerator());
        obj->setProperty("defaultTimeSigDenom", trackModel->getDefaultTimeSigDenominator());

        juce::Array<juce::var> tracksArray;
        for (const auto& trk : trackModel->getTracks())
        {
            auto tObj = std::make_unique<juce::DynamicObject>();
            tObj->setProperty("id", trk->getId());
            tObj->setProperty("name", trk->getName());
            tObj->setProperty("type", (int)trk->getType());
            tObj->setProperty("color", trk->getColor().toDisplayString(true));
            tObj->setProperty("muted", trk->isMuted());
            tObj->setProperty("solo", trk->isSolo());
            tObj->setProperty("locked", trk->isLocked());
            tObj->setProperty("harmonizeEnabled", trk->isHarmonizeEnabled());
            tObj->setProperty("volume", trk->getVolume());
            tObj->setProperty("pan", trk->getPan());
            tObj->setProperty("parentTrackId", trk->getParentTrackId());
            tObj->setProperty("parentFolderId", trk->getParentFolderId());
            tObj->setProperty("folderCollapsed", trk->isFolderCollapsed());
            tObj->setProperty("folderNotes", trk->getFolderNotes());
            tObj->setProperty("hasAuxMarkerTrack", trk->hasAuxiliaryMarkerTrack());
            tObj->setProperty("assignedVstName", trk->getAssignedVstName());
            tObj->setProperty("assignedVstIdentifier", trk->getAssignedVstIdentifier());
            tObj->setProperty("assignedVstState", trk->getAssignedVstState().toBase64Encoding());

            // Clips
            juce::Array<juce::var> clipsArray;
            for (const auto& clip : trk->getClips())
            {
                auto cObj = std::make_unique<juce::DynamicObject>();
                cObj->setProperty("id", (juce::int64)clip.id);
                cObj->setProperty("name", clip.name);
                cObj->setProperty("startBeat", clip.startBeat);
                cObj->setProperty("lengthBeats", clip.lengthBeats);
                cObj->setProperty("color", clip.color.toDisplayString(true));
                cObj->setProperty("hasCustomColor", clip.hasCustomColor);
                cObj->setProperty("muted", clip.isMuted);

                juce::Array<juce::var> clipNotesArr;
                for (const auto& cn : clip.notes)
                {
                    auto cnObj = std::make_unique<juce::DynamicObject>();
                    cnObj->setProperty("id", (juce::int64)cn.id);
                    cnObj->setProperty("pitch", cn.pitch);
                    cnObj->setProperty("startBeat", cn.startBeat);
                    cnObj->setProperty("lengthBeats", cn.lengthBeats);
                    cnObj->setProperty("velocity", cn.velocity);
                    cnObj->setProperty("muted", cn.isMuted);
                    clipNotesArr.add(juce::var(cnObj.release()));
                }
                cObj->setProperty("notes", clipNotesArr);
                clipsArray.add(juce::var(cObj.release()));
            }
            tObj->setProperty("clips", clipsArray);

            juce::Array<juce::var> audioClipsArray;
            for (const auto& clip : trk->getAudioClips())
            {
                auto cObj = std::make_unique<juce::DynamicObject>();
                cObj->setProperty("id", (juce::int64)clip.id);
                cObj->setProperty("name", clip.name);
                cObj->setProperty("sourcePath", juce::File(clip.sourcePath)
                    .getRelativePathFrom(targetFile.getParentDirectory()));
                cObj->setProperty("absolutePath", clip.sourcePath);
                cObj->setProperty("startBeat", clip.startBeat);
                cObj->setProperty("lengthBeats", clip.lengthBeats);
                cObj->setProperty("sourceOffsetSeconds", clip.sourceOffsetSeconds);
                cObj->setProperty("durationSeconds", clip.durationSeconds);
                cObj->setProperty("muted", clip.isMuted);
                audioClipsArray.add(juce::var(cObj.release()));
            }
            tObj->setProperty("audioClips", audioClipsArray);

            // Chords
            juce::Array<juce::var> chordsArray;
            for (const auto& chord : trk->getChords())
            {
                auto chObj = std::make_unique<juce::DynamicObject>();
                chObj->setProperty("id", (juce::int64)chord.id);
                chObj->setProperty("startBeat", chord.startBeat);
                chObj->setProperty("lengthBeats", chord.lengthBeats);
                chObj->setProperty("root", chord.root);
                chObj->setProperty("name", chord.chordName);
                chObj->setProperty("roman", chord.romanNumeral);
                chObj->setProperty("color", chord.color.toDisplayString(true));
                chordsArray.add(juce::var(chObj.release()));
            }
            tObj->setProperty("chords", chordsArray);

            // Modes
            juce::Array<juce::var> modesArray;
            for (const auto& md : trk->getModes())
            {
                auto mObj = std::make_unique<juce::DynamicObject>();
                mObj->setProperty("id", (juce::int64)md.id);
                mObj->setProperty("startBeat", md.startBeat);
                mObj->setProperty("lengthBeats", md.lengthBeats);
                mObj->setProperty("name", md.name);
                mObj->setProperty("tonic", md.tonic);
                mObj->setProperty("tonicPc", md.tonicPc);
                mObj->setProperty("scaleName", md.scaleName);
                mObj->setProperty("color", md.color.toDisplayString(true));
                modesArray.add(juce::var(mObj.release()));
            }
            tObj->setProperty("modes", modesArray);

            juce::Array<juce::var> auxModesArray;
            for (const auto& md : trk->getAuxModes())
            {
                auto mObj = std::make_unique<juce::DynamicObject>();
                mObj->setProperty("id", (juce::int64)md.id);
                mObj->setProperty("startBeat", md.startBeat);
                mObj->setProperty("lengthBeats", md.lengthBeats);
                mObj->setProperty("name", md.name);
                mObj->setProperty("tonic", md.tonic);
                mObj->setProperty("tonicPc", md.tonicPc);
                mObj->setProperty("scaleName", md.scaleName);
                mObj->setProperty("color", md.color.toDisplayString(true));
                auxModesArray.add(juce::var(mObj.release()));
            }
            tObj->setProperty("auxModes", auxModesArray);

            // Markers
            juce::Array<juce::var> markersArray;
            for (const auto& mk : trk->getMarkers())
            {
                auto mkObj = std::make_unique<juce::DynamicObject>();
                mkObj->setProperty("id", mk.id);
                mkObj->setProperty("startBeat", mk.startBeat);
                mkObj->setProperty("lengthBeats", mk.lengthBeats);
                mkObj->setProperty("name", mk.name);
                mkObj->setProperty("color", mk.color.toDisplayString(true));
                mkObj->setProperty("stackLayer", mk.stackLayer);
                markersArray.add(juce::var(mkObj.release()));
            }
            tObj->setProperty("markers", markersArray);

            // Time Signatures
            juce::Array<juce::var> timeSigsArray;
            for (const auto& ts : trk->getTimeSignatures())
            {
                auto tsObj = std::make_unique<juce::DynamicObject>();
                tsObj->setProperty("id", (juce::int64)ts.id);
                tsObj->setProperty("startBeat", ts.startBeat);
                tsObj->setProperty("numerator", ts.numerator);
                tsObj->setProperty("denominator", ts.denominator);
                tsObj->setProperty("lengthBeats", ts.lengthBeats);
                tsObj->setProperty("color", ts.color.toDisplayString(true));
                timeSigsArray.add(juce::var(tsObj.release()));
            }
            tObj->setProperty("timeSignatures", timeSigsArray);

            juce::Array<juce::var> tempoArray;
            for (const auto& tempo : trk->getTempoEvents())
            {
                auto teObj = std::make_unique<juce::DynamicObject>();
                teObj->setProperty("id", (juce::int64)tempo.id);
                teObj->setProperty("startBeat", tempo.startBeat);
                teObj->setProperty("bpm", tempo.bpm);
                teObj->setProperty("type", (int)tempo.type);
                teObj->setProperty("color", tempo.color.toDisplayString(true));
                tempoArray.add(juce::var(teObj.release()));
            }
            tObj->setProperty("tempoEvents", tempoArray);

            tracksArray.add(juce::var(tObj.release()));
        }
        obj->setProperty("tracks", tracksArray);
        obj->setProperty("modeTrackOverrideEnabled", trackModel->isModeTrackOverrideEnabled());
        juce::Array<juce::var> visibleTrkIds;
        for (int id : trackModel->getPianoRollVisibleTrackIds())
            visibleTrkIds.add(id);
        obj->setProperty("pianoRollVisibleTrackIds", visibleTrkIds);
    }

    juce::var rootVar(obj.release());
    auto jsonString = juce::JSON::toString(rootVar, true);
    if (jsonString.getNumBytesAsUTF8() > maxProjectFileBytes)
        return false;
    juce::TemporaryFile tempFile(targetFile);
    if (tempFile.getFile().replaceWithText(jsonString))
        return tempFile.overwriteTargetFileWithTemporary();
    return false;
}

bool ProjectFile::loadProject(MidiModel& model, const juce::File& sourceFile, TrackModel* trackModel)
{
    if (!sourceFile.existsAsFile() || sourceFile.getSize() > (juce::int64)maxProjectFileBytes)
        return false;

    auto jsonText = sourceFile.loadFileAsString();
    auto rootVar = juce::JSON::parse(jsonText);
    if (!rootVar.isObject())
        return false;

    auto* obj = rootVar.getDynamicObject();
    if (obj == nullptr)
        return false;

    auto finiteFields = [](juce::DynamicObject* item, std::initializer_list<const char*> names) {
        for (const char* name : names)
        {
            const juce::Identifier key(name);
            if (!item->hasProperty(key))
                continue;
            auto value = item->getProperty(key);
            if (!(value.isInt() || value.isInt64() || value.isDouble())
                || !std::isfinite((double)value))
                return false;
        }
        return true;
    };
    auto integralField = [](juce::DynamicObject* item, const char* name, double minimum, double maximum) {
        const juce::Identifier key(name);
        if (!item->hasProperty(key))
            return true;
        const auto value = item->getProperty(key);
        if (!(value.isInt() || value.isInt64() || value.isDouble()))
            return false;
        const double number = (double)value;
        return std::isfinite(number) && number >= minimum && number <= maximum
            && std::trunc(number) == number;
    };
    auto objectArray = [](juce::DynamicObject* item, const char* name) {
        const juce::Identifier key(name);
        if (!item->hasProperty(key))
            return true;
        auto* array = item->getProperty(key).getArray();
        if (array == nullptr)
            return false;
        for (const auto& entry : *array)
            if (!entry.isObject())
                return false;
        return true;
    };
    auto validNote = [&](juce::DynamicObject* note) {
        if (!finiteFields(note, { "id", "pitch", "startBeat", "lengthBeats", "velocity" })
            || !integralField(note, "id", 0.0, 9.0e15)
            || !integralField(note, "pitch", 0.0, 127.0)
            || !note->hasProperty("pitch") || !note->hasProperty("startBeat")
            || !note->hasProperty("lengthBeats") || !note->hasProperty("velocity"))
            return false;
        double pitch = note->getProperty("pitch");
        double start = note->getProperty("startBeat");
        double length = note->getProperty("lengthBeats");
        double velocity = note->getProperty("velocity");
        return pitch >= 0.0 && pitch <= 127.0 && start >= 0.0 && start <= 1.0e9
            && length > 0.0 && length <= 1.0e9 && velocity >= 0.0 && velocity <= 1.0;
    };
    auto validTiming = [](juce::DynamicObject* event) {
        if (event->hasProperty("startBeat"))
        {
            double start = event->getProperty("startBeat");
            if (start < 0.0 || start > 1.0e9) return false;
        }
        if (event->hasProperty("lengthBeats"))
        {
            double length = event->getProperty("lengthBeats");
            if (length <= 0.0 || length > 1.0e9) return false;
        }
        return true;
    };

    if (!finiteFields(obj, { "bpm", "timeSigNum", "timeSigDenom", "keyRoot", "keyScale",
                             "loopStart", "loopEnd", "coloringMode", "barNumberOffset",
                             "defaultTimeSigNum", "defaultTimeSigDenom", "chordDisplayMode" })
        || !objectArray(obj, "notes") || !objectArray(obj, "tracks"))
        return false;
    for (const auto& field : { std::tuple { "version", 0.0, 5.0 },
                               { "timeSigNum", 1.0, 1024.0 }, { "timeSigDenom", 1.0, 1024.0 },
                               { "defaultTimeSigNum", 1.0, 1024.0 }, { "defaultTimeSigDenom", 1.0, 1024.0 },
                               { "keyRoot", 0.0, 11.0 }, { "keyScale", 0.0, 8.0 },
                               { "coloringMode", 0.0, (double)(int)ColoringMode::ClipColor },
                               { "chordDisplayMode", 0.0, (double)(int)ChordDisplayMode::RomanAndQuality },
                               { "barNumberOffset", -1000000.0, 1000000.0 },
                               { "activeTrackId", -1.0, maxImportedTrackId },
                               { "activeClipId", -1.0, 9.0e15 } })
        if (!integralField(obj, std::get<0>(field), std::get<1>(field), std::get<2>(field)))
            return false;
    if (obj->hasProperty("bpm") && ((double)obj->getProperty("bpm") < 20.0 || (double)obj->getProperty("bpm") > 400.0))
        return false;
    for (const char* name : { "timeSigNum", "timeSigDenom", "defaultTimeSigNum", "defaultTimeSigDenom" })
        if (obj->hasProperty(name) && ((double)obj->getProperty(name) < 1.0 || (double)obj->getProperty(name) > 1024.0))
            return false;
    for (const char* name : { "loopStart", "loopEnd" })
        if (obj->hasProperty(name) && ((double)obj->getProperty(name) < 0.0 || (double)obj->getProperty(name) > 1.0e9))
            return false;
    if (obj->hasProperty("pianoRollVisibleTrackIds"))
    {
        auto* visibleIds = obj->getProperty("pianoRollVisibleTrackIds").getArray();
        if (visibleIds == nullptr)
            return false;
        for (const auto& id : *visibleIds)
        {
            if (!(id.isInt() || id.isInt64() || id.isDouble()))
                return false;
            const double number = (double)id;
            if (!std::isfinite(number) || number < -1.0
                || number > maxImportedTrackId
                || std::trunc(number) != number)
                return false;
        }
    }
    if (auto* notes = obj->getProperty("notes").getArray())
        for (const auto& note : *notes)
            if (!validNote(note.getDynamicObject()))
                return false;
    if (auto* tracks = obj->getProperty("tracks").getArray())
    {
        for (const auto& track : *tracks)
        {
            auto* t = track.getDynamicObject();
            if (!finiteFields(t, { "id", "type", "volume", "pan", "parentTrackId", "parentFolderId" }))
                return false;
            if (t->hasProperty("volume") && ((double)t->getProperty("volume") < 0.0 || (double)t->getProperty("volume") > 2.0))
                return false;
            if (t->hasProperty("pan") && ((double)t->getProperty("pan") < -1.0 || (double)t->getProperty("pan") > 1.0))
                return false;
            for (const auto& field : { std::tuple { "id", 1.0, maxImportedTrackId },
                                       { "type", 0.0, (double)(int)TrackType::Audio },
                                       { "parentTrackId", -1.0, maxImportedTrackId },
                                       { "parentFolderId", -1.0, maxImportedTrackId } })
                if (!integralField(t, std::get<0>(field), std::get<1>(field), std::get<2>(field)))
                    return false;
            if ((double)t->getProperty("id") <= 0.0 || (double)t->getProperty("id") > maxImportedTrackId
                || (double)t->getProperty("type") < (double)(int)TrackType::Instrument
                || (double)t->getProperty("type") > (double)(int)TrackType::Audio)
                return false;
            for (const char* name : { "clips", "audioClips", "chords", "modes", "auxModes", "markers", "timeSignatures", "tempoEvents" })
            {
                if (!objectArray(t, name))
                    return false;
                if (auto* events = t->getProperty(name).getArray())
                    for (const auto& event : *events)
                    {
                        auto* e = event.getDynamicObject();
                        if (!finiteFields(e, { "startBeat", "lengthBeats", "sourceOffsetSeconds", "durationSeconds", "velocity", "bpm", "root",
                                                "tonicPc", "numerator", "denominator", "type", "stackLayer" }))
                            return false;
                        if (juce::String(name) != "markers" && !integralField(e, "id", 0.0, 9.0e15))
                            return false;
                        for (const auto& field : { std::tuple { "root", 0.0, 11.0 },
                                                   { "tonicPc", 0.0, 11.0 },
                                                   { "numerator", 1.0, 1024.0 },
                                                   { "denominator", 1.0, 1024.0 },
                                                   { "type", 0.0, (double)(int)TempoTransitionType::Ramp },
                                                   { "stackLayer", 0.0, 1000000.0 } })
                            if (!integralField(e, std::get<0>(field), std::get<1>(field), std::get<2>(field)))
                                return false;
                        if (!validTiming(e))
                            return false;
                        if (juce::String(name) == "audioClips"
                            && (!e->hasProperty("sourcePath") || !e->hasProperty("durationSeconds")
                                || !e->hasProperty("startBeat") || !e->hasProperty("lengthBeats")
                                || !e->getProperty("sourcePath").isString()
                                || e->getProperty("sourcePath").toString().isEmpty()
                                || e->getProperty("sourcePath").toString().length() > 4096
                                || (e->hasProperty("absolutePath")
                                    && (!e->getProperty("absolutePath").isString()
                                        || e->getProperty("absolutePath").toString().length() > 4096))
                                || (double)e->getProperty("durationSeconds") <= 0.0
                                || (double)e->getProperty("durationSeconds") > 24.0 * 3600.0
                                || (e->hasProperty("sourceOffsetSeconds")
                                    && ((double)e->getProperty("sourceOffsetSeconds") < 0.0
                                        || (double)e->getProperty("sourceOffsetSeconds") > 24.0 * 3600.0
                                        || (double)e->getProperty("sourceOffsetSeconds")
                                            + (double)e->getProperty("durationSeconds") > 24.0 * 3600.0))))
                            return false;
                        if (e->hasProperty("bpm") && ((double)e->getProperty("bpm") < 20.0 || (double)e->getProperty("bpm") > 400.0))
                            return false;
                        if (e->hasProperty("denominator") && ((double)e->getProperty("denominator") < 1.0 || (double)e->getProperty("denominator") > 1024.0))
                            return false;
                        if (e->hasProperty("numerator") && ((double)e->getProperty("numerator") < 1.0 || (double)e->getProperty("numerator") > 1024.0))
                            return false;
                        if (e->hasProperty("tonicPc") && ((double)e->getProperty("tonicPc") < 0.0 || (double)e->getProperty("tonicPc") > 11.0))
                            return false;
                        if (juce::String(name) == "tempoEvents" && e->hasProperty("type")
                            && ((double)e->getProperty("type") < (double)(int)TempoTransitionType::Jump
                                || (double)e->getProperty("type") > (double)(int)TempoTransitionType::Ramp))
                            return false;
                        if (juce::String(name) == "clips")
                        {
                            if (!objectArray(e, "notes"))
                                return false;
                            if (auto* clipNotes = e->getProperty("notes").getArray())
                                for (const auto& note : *clipNotes)
                                    if (!validNote(note.getDynamicObject()))
                                        return false;
                        }
                    }
            }
        }
    }

    MidiModel loadedModel;
    TrackModel loadedTracks;
    TrackModel* loadedTrackModel = trackModel != nullptr ? &loadedTracks : nullptr;

    if (obj->hasProperty("bpm"))
        loadedModel.setBpm(obj->getProperty("bpm"));
    loadedTracks.setFallbackTempoBpm(loadedModel.getBpm());

    if (obj->hasProperty("timeSigNum") && obj->hasProperty("timeSigDenom"))
        loadedModel.setTimeSignature(obj->getProperty("timeSigNum"), obj->getProperty("timeSigDenom"));

    if (obj->hasProperty("keyRoot") && obj->hasProperty("keyScale"))
    {
        KeySignature ks;
        ks.rootPitchClass = obj->getProperty("keyRoot");
        ks.scale = (ScaleType)(int)obj->getProperty("keyScale");
        if (ks.rootPitchClass < 0 || ks.rootPitchClass > 11
            || (int)ks.scale < (int)ScaleType::Major || (int)ks.scale > (int)ScaleType::Locrian)
            return false;
        loadedModel.setKeySignature(ks);
    }

    if (obj->hasProperty("scaleName"))
        loadedModel.setScaleName(obj->getProperty("scaleName").toString());
    if (obj->hasProperty("coloringMode"))
    {
        int mode = obj->getProperty("coloringMode");
        if (mode < (int)ColoringMode::ScaleAdaptive || mode > (int)ColoringMode::ClipColor)
            return false;
        loadedModel.setColoringMode((ColoringMode)mode);
    }

    if (obj->hasProperty("loopStart") && obj->hasProperty("loopEnd"))
        loadedModel.setLoopRange(obj->getProperty("loopStart"), obj->getProperty("loopEnd"));

    if (obj->hasProperty("loopEnabled"))
        loadedModel.setLoopEnabled(obj->getProperty("loopEnabled"));

    std::vector<NoteEvent> loadedNotes;
    if (auto* arr = obj->getProperty("notes").getArray())
    {
        for (const auto& noteVar : *arr)
        {
            if (auto* nObj = noteVar.getDynamicObject())
            {
                NoteEvent n;
                n.id = nObj->getProperty("id");
                n.pitch = std::clamp((int)nObj->getProperty("pitch"), 0, 127);
                n.startBeat = std::max(0.0, (double)nObj->getProperty("startBeat"));
                n.lengthBeats = std::max(0.001, (double)nObj->getProperty("lengthBeats"));
                n.velocity = std::clamp((float)(double)nObj->getProperty("velocity"), 0.01f, 1.0f);
                n.isMuted = nObj->getProperty("muted");
                n.isSelected = false;
                loadedNotes.push_back(n);
            }
        }
    }

    loadedModel.setAllNotes(loadedNotes, false);

    if (loadedTrackModel != nullptr && obj->hasProperty("tracks"))
    {
        loadedTrackModel->getTracks().clear();

        if (obj->hasProperty("barNumberOffset"))
            loadedTrackModel->setBarNumberOffset(obj->getProperty("barNumberOffset"));
        if (obj->hasProperty("chordDisplayMode"))
        {
            int mode = obj->getProperty("chordDisplayMode");
            if (mode < (int)ChordDisplayMode::NameAndRoman || mode > (int)ChordDisplayMode::RomanAndQuality)
                return false;
            loadedTrackModel->setChordDisplayMode((ChordDisplayMode)mode);
        }
        if (obj->hasProperty("defaultTimeSigNum") && obj->hasProperty("defaultTimeSigDenom"))
            loadedTrackModel->setDefaultTimeSignature(obj->getProperty("defaultTimeSigNum"), obj->getProperty("defaultTimeSigDenom"));

        if (auto* tArr = obj->getProperty("tracks").getArray())
        {
            for (const auto& tVar : *tArr)
            {
                if (auto* tObj = tVar.getDynamicObject())
                {
                    int tid = tObj->getProperty("id");
                    juce::String tname = tObj->getProperty("name");
                    TrackType ttype = (TrackType)(int)tObj->getProperty("type");

                    if (tid <= 0 || tid > (int)maxImportedTrackId
                        || (int)ttype < (int)TrackType::Instrument || (int)ttype > (int)TrackType::Audio)
                        return false;
                    for (const auto& existing : loadedTrackModel->getTracks())
                        if (existing->getId() == tid)
                            return false;

                    auto newTrk = std::make_unique<Track>(tid, tname, ttype);
                    if (tObj->hasProperty("color"))
                        newTrk->setColor(juce::Colour::fromString(tObj->getProperty("color").toString()));
                    if (tObj->hasProperty("muted"))
                        newTrk->setMuted((bool)tObj->getProperty("muted"));
                    if (tObj->hasProperty("solo"))
                        newTrk->setSolo((bool)tObj->getProperty("solo"));
                    if (tObj->hasProperty("locked"))
                        newTrk->setLocked((bool)tObj->getProperty("locked"));
                    if (tObj->hasProperty("harmonizeEnabled"))
                        newTrk->setHarmonizeEnabled((bool)tObj->getProperty("harmonizeEnabled"));
                    if (tObj->hasProperty("volume"))
                        newTrk->setVolume((float)(double)tObj->getProperty("volume"));
                    if (tObj->hasProperty("pan"))
                        newTrk->setPan((float)(double)tObj->getProperty("pan"));
                    if (tObj->hasProperty("parentTrackId"))
                        newTrk->setParentTrackId(tObj->getProperty("parentTrackId"));
                    if (tObj->hasProperty("hasAuxMarkerTrack"))
                        newTrk->setHasAuxiliaryMarkerTrack((bool)tObj->getProperty("hasAuxMarkerTrack"));
                    if (tObj->hasProperty("parentFolderId"))
                        newTrk->setParentFolderId((int)tObj->getProperty("parentFolderId"));
                    if (tObj->hasProperty("folderCollapsed"))
                        newTrk->setFolderCollapsed((bool)tObj->getProperty("folderCollapsed"));
                    if (tObj->hasProperty("folderNotes"))
                        newTrk->setFolderNotes(tObj->getProperty("folderNotes").toString());
                    if (tObj->hasProperty("assignedVstName"))
                        newTrk->setAssignedVstName(tObj->getProperty("assignedVstName").toString());
                    if (tObj->hasProperty("assignedVstIdentifier"))
                        newTrk->setAssignedVstIdentifier(tObj->getProperty("assignedVstIdentifier").toString());
                    if (tObj->hasProperty("assignedVstState"))
                    {
                        juce::MemoryBlock mb;
                        if (!decodePluginState(tObj->getProperty("assignedVstState").toString(), mb))
                            return false;
                        newTrk->setAssignedVstState(mb);
                    }

                    // Clips
                    if (auto* cArr = tObj->getProperty("clips").getArray())
                    {
                        for (const auto& cVar : *cArr)
                        {
                            if (auto* cObj = cVar.getDynamicObject())
                            {
                                MidiClip clip;
                                clip.id = cObj->getProperty("id");
                                clip.name = cObj->getProperty("name").toString();
                                clip.startBeat = cObj->getProperty("startBeat");
                                clip.lengthBeats = cObj->getProperty("lengthBeats");
                                if (cObj->hasProperty("color"))
                                    clip.color = juce::Colour::fromString(cObj->getProperty("color").toString());
                                clip.hasCustomColor = cObj->hasProperty("hasCustomColor") ? (bool)cObj->getProperty("hasCustomColor") : false;
                                clip.isMuted = cObj->hasProperty("muted") ? (bool)cObj->getProperty("muted") : false;

                                if (auto* cnArr = cObj->getProperty("notes").getArray())
                                {
                                    for (const auto& cnVar : *cnArr)
                                    {
                                        if (auto* cnObj = cnVar.getDynamicObject())
                                        {
                                            NoteEvent cn;
                                            cn.id = cnObj->getProperty("id");
                                            cn.pitch = std::clamp((int)cnObj->getProperty("pitch"), 0, 127);
                                            cn.startBeat = std::max(0.0, (double)cnObj->getProperty("startBeat"));
                                            cn.lengthBeats = std::max(0.001, (double)cnObj->getProperty("lengthBeats"));
                                            cn.velocity = std::clamp((float)(double)cnObj->getProperty("velocity"), 0.01f, 1.0f);
                                            cn.isMuted = cnObj->getProperty("muted");
                                            cn.isSelected = false;
                                            clip.notes.push_back(cn);
                                        }
                                    }
                                }
                                newTrk->addClip(clip);
                            }
                        }
                    }

                    if (auto* cArr = tObj->getProperty("audioClips").getArray())
                    {
                        for (const auto& cVar : *cArr)
                        {
                            auto* cObj = cVar.getDynamicObject();
                            AudioClip clip;
                            clip.id = (int64_t)cObj->getProperty("id");
                            clip.name = cObj->getProperty("name").toString();
                            const auto savedPath = cObj->getProperty("sourcePath").toString();
                            juce::File candidate = juce::File::isAbsolutePath(savedPath)
                                ? juce::File(savedPath)
                                : sourceFile.getParentDirectory().getChildFile(savedPath);
                            if (!candidate.existsAsFile() && cObj->hasProperty("absolutePath"))
                                candidate = juce::File(cObj->getProperty("absolutePath").toString());
                            clip.sourcePath = candidate.getFullPathName();
                            clip.startBeat = (double)cObj->getProperty("startBeat");
                            clip.lengthBeats = (double)cObj->getProperty("lengthBeats");
                            clip.sourceOffsetSeconds = cObj->hasProperty("sourceOffsetSeconds")
                                ? (double)cObj->getProperty("sourceOffsetSeconds") : 0.0;
                            clip.durationSeconds = (double)cObj->getProperty("durationSeconds");
                            clip.isMuted = (bool)cObj->getProperty("muted");
                            newTrk->getAudioClips().push_back(std::move(clip));
                        }
                    }

                    // Chords
                    if (auto* chArr = tObj->getProperty("chords").getArray())
                    {
                        for (const auto& chVar : *chArr)
                        {
                            if (auto* chObj = chVar.getDynamicObject())
                            {
                                ChordEvent ch;
                                ch.id = chObj->getProperty("id");
                                ch.startBeat = chObj->getProperty("startBeat");
                                ch.lengthBeats = chObj->getProperty("lengthBeats");
                                ch.root = chObj->getProperty("root");
                                ch.chordName = chObj->getProperty("name").toString();
                                ch.romanNumeral = chObj->getProperty("roman").toString();
                                if (chObj->hasProperty("color"))
                                    ch.color = juce::Colour::fromString(chObj->getProperty("color").toString());
                                newTrk->getChords().push_back(ch);
                            }
                        }
                    }

                    auto loadModes = [](const juce::var& value, std::vector<ModeEvent>& destination) {
                        if (auto* mArr = value.getArray())
                        {
                            for (const auto& mVar : *mArr)
                            {
                                if (auto* mObj = mVar.getDynamicObject())
                                {
                                    ModeEvent md;
                                    md.id = mObj->getProperty("id");
                                    md.startBeat = mObj->getProperty("startBeat");
                                    md.lengthBeats = mObj->getProperty("lengthBeats");
                                    md.name = mObj->getProperty("name").toString();
                                    md.tonic = mObj->getProperty("tonic").toString();
                                    md.tonicPc = mObj->hasProperty("tonicPc")
                                        ? (int)mObj->getProperty("tonicPc") : pitchClassFromName(md.tonic);
                                    md.scaleName = mObj->getProperty("scaleName").toString();
                                    if (mObj->hasProperty("color"))
                                        md.color = juce::Colour::fromString(mObj->getProperty("color").toString());
                                    destination.push_back(md);
                                }
                            }
                        }
                    };
                    loadModes(tObj->getProperty("modes"), newTrk->getModes());
                    loadModes(tObj->getProperty("auxModes"), newTrk->getAuxModes());

                    // Markers
                    if (auto* mkArr = tObj->getProperty("markers").getArray())
                    {
                        for (const auto& mkVar : *mkArr)
                        {
                            if (auto* mkObj = mkVar.getDynamicObject())
                            {
                                MarkerEvent mk;
                                mk.id = mkObj->getProperty("id").toString();
                                mk.startBeat = mkObj->getProperty("startBeat");
                                mk.lengthBeats = mkObj->getProperty("lengthBeats");
                                mk.name = mkObj->getProperty("name").toString();
                                if (mkObj->hasProperty("color"))
                                    mk.color = juce::Colour::fromString(mkObj->getProperty("color").toString());
                                if (mkObj->hasProperty("stackLayer"))
                                    mk.stackLayer = (int)mkObj->getProperty("stackLayer");
                                newTrk->getMarkers().push_back(mk);
                            }
                        }
                    }

                    // Time Signatures
                    if (auto* tsArr = tObj->getProperty("timeSignatures").getArray())
                    {
                        for (const auto& tsVar : *tsArr)
                        {
                            if (auto* tsObj = tsVar.getDynamicObject())
                            {
                                TimeSignatureEvent ts;
                                ts.id = tsObj->getProperty("id");
                                ts.startBeat = tsObj->getProperty("startBeat");
                                ts.numerator = tsObj->getProperty("numerator");
                                ts.denominator = tsObj->getProperty("denominator");
                                if (tsObj->hasProperty("lengthBeats"))
                                    ts.lengthBeats = tsObj->getProperty("lengthBeats");
                                else
                                    ts.lengthBeats = 16.0;
                                if (tsObj->hasProperty("color"))
                                    ts.color = juce::Colour::fromString(tsObj->getProperty("color").toString());
                                newTrk->getTimeSignatures().push_back(ts);
                            }
                        }
                    }

                    if (auto* tempoArr = tObj->getProperty("tempoEvents").getArray())
                    {
                        for (const auto& tempoVar : *tempoArr)
                        {
                            if (auto* tempoObj = tempoVar.getDynamicObject())
                            {
                                TempoEvent tempo;
                                tempo.id = tempoObj->getProperty("id");
                                tempo.startBeat = tempoObj->getProperty("startBeat");
                                tempo.bpm = tempoObj->getProperty("bpm");
                                tempo.type = (TempoTransitionType)(int)tempoObj->getProperty("type");
                                if (tempoObj->hasProperty("color"))
                                    tempo.color = juce::Colour::fromString(tempoObj->getProperty("color").toString());
                                newTrk->getTempoEvents().push_back(tempo);
                            }
                        }
                    }

                    loadedTrackModel->getTracks().push_back(std::move(newTrk));
                }
            }
        }

        loadedTrackModel->repairFolderHierarchy();

        if (obj->hasProperty("activeTrackId"))
            loadedTrackModel->setActiveTrackId(obj->getProperty("activeTrackId"));
        if (obj->hasProperty("activeClipId"))
            loadedTrackModel->setActiveClipId(obj->getProperty("activeClipId"));
        if (obj->hasProperty("modeTrackOverrideEnabled"))
            loadedTrackModel->setModeTrackOverrideEnabled(obj->getProperty("modeTrackOverrideEnabled"));
        if (auto* visArr = obj->getProperty("pianoRollVisibleTrackIds").getArray())
        {
            loadedTrackModel->clearPianoRollVisibleTracks();
            for (const auto& v : *visArr)
                loadedTrackModel->setPianoRollTrackVisible((int)v, true);
        }
    }

    if (trackModel != nullptr && obj->hasProperty("tracks"))
        trackModel->replaceProjectData(loadedTracks);
    model.replaceProjectData(loadedModel);
    return true;
}

//==============================================================================
bool ProjectFile::importMidi(MidiModel& model, const juce::File& midiFile, TrackModel* trackModel)
{
    juce::FileInputStream stream(midiFile);
    if (!stream.openedOk())
        return false;

    juce::MidiFile mf;
    if (!mf.readFrom(stream))
        return false;

    short timeFormat = mf.getTimeFormat();
    if (timeFormat == 0)
        return false;
    if (timeFormat < 0 && ((timeFormat & 0xff) == 0 || -(timeFormat >> 8) <= 0))
        return false;

    struct SmpteTempoPoint { double seconds, beat, bpm; };
    std::vector<SmpteTempoPoint> smpteTempos;
    if (timeFormat < 0)
    {
        mf.convertTimestampTicksToSeconds();
        std::vector<std::pair<double, double>> tempoChanges;
        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            const auto* track = mf.getTrack(t);
            for (int i = 0; i < track->getNumEvents(); ++i)
            {
                const auto& message = track->getEventPointer(i)->message;
                if (message.isTempoMetaEvent() && message.getTempoSecondsPerQuarterNote() > 0.0)
                    tempoChanges.emplace_back(message.getTimeStamp(), 60.0 / message.getTempoSecondsPerQuarterNote());
            }
        }
        std::stable_sort(tempoChanges.begin(), tempoChanges.end());
        smpteTempos.push_back({ 0.0, 0.0, 120.0 });
        for (const auto& change : tempoChanges)
        {
            const auto& previous = smpteTempos.back();
            double beat = previous.beat + (change.first - previous.seconds) * previous.bpm / 60.0;
            smpteTempos.push_back({ change.first, beat, change.second });
        }
    }
    auto timestampToBeat = [&](double timestamp) {
        if (timeFormat > 0)
            return timestamp / (double)timeFormat;
        auto it = std::upper_bound(smpteTempos.begin(), smpteTempos.end(), timestamp,
                                   [](double value, const SmpteTempoPoint& point) { return value < point.seconds; });
        const auto& point = *(it == smpteTempos.begin() ? it : std::prev(it));
        return point.beat + (timestamp - point.seconds) * point.bpm / 60.0;
    };

    // 1. Scan and parse tempo meta events
    std::vector<TempoEvent> importedTempos;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto* track = mf.getTrack(t);
        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto& msg = track->getEventPointer(i)->message;
            if (msg.isTempoMetaEvent())
            {
                double beat = timestampToBeat(msg.getTimeStamp());
                double secondsPerQuarter = msg.getTempoSecondsPerQuarterNote();
                double bpm = (secondsPerQuarter > 0.0001) ? (60.0 / secondsPerQuarter) : 120.0;
                if (bpm >= 20.0 && bpm <= 400.0)
                {
                    TempoEvent te;
                    te.id = juce::Time::currentTimeMillis() + (int64_t)importedTempos.size() + 1;
                    te.startBeat = std::max(0.0, beat);
                    te.bpm = bpm;
                    te.type = TempoTransitionType::Jump;
                    te.color = juce::Colour(0xff3b82f6);
                    importedTempos.push_back(te);
                }
            }
        }
    }

    std::sort(importedTempos.begin(), importedTempos.end(), [](const TempoEvent& a, const TempoEvent& b) {
        return a.startBeat < b.startBeat;
    });

    // 2. Scan and parse time signature meta events
    std::vector<TimeSignatureEvent> importedTimeSigs;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto* track = mf.getTrack(t);
        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto& msg = track->getEventPointer(i)->message;
            if (msg.isTimeSignatureMetaEvent())
            {
                int num = 4, denom = 4;
                msg.getTimeSignatureInfo(num, denom);
                if (num <= 0 || denom <= 0 || denom > 1024)
                    continue;
                double beat = timestampToBeat(msg.getTimeStamp());
                TimeSignatureEvent ts;
                ts.id = juce::Time::currentTimeMillis() + (int64_t)importedTimeSigs.size() + 1;
                ts.startBeat = std::max(0.0, beat);
                ts.lengthBeats = 16.0;
                ts.numerator = num;
                ts.denominator = denom;
                ts.color = juce::Colour(0xff8b5cf6);
                importedTimeSigs.push_back(ts);
            }
        }
    }

    std::sort(importedTimeSigs.begin(), importedTimeSigs.end(), [](const TimeSignatureEvent& a, const TimeSignatureEvent& b) {
        return a.startBeat < b.startBeat;
    });

    // 3. Match note-ons and note-offs per track
    struct ParsedTrack
    {
        juce::String name;
        std::vector<NoteEvent> notes;
        double maxBeat = 0.0;
    };
    std::vector<ParsedTrack> parsedTracks;

    juce::String baseFileName = midiFile.getFileNameWithoutExtension();
    if (baseFileName.isEmpty())
        baseFileName = "Instrument";

    int64_t currentId = 1000;
    for (const auto& note : model.getNotes())
        currentId = std::max(currentId, note.id);
    if (trackModel != nullptr)
        for (const auto& existingTrack : trackModel->getTracks())
            for (const auto& clip : existingTrack->getClips())
                for (const auto& note : clip.notes)
                    currentId = std::max(currentId, note.id);

    struct PendingNote
    {
        double startBeat = 0.0;
        float velocity = 0.8f;
    };

    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto* track = mf.getTrack(t);
        juce::String trackName;
        std::map<std::pair<int, int>, std::deque<PendingNote>> pendingNotes;
        std::vector<NoteEvent> trackNotes;

        for (int i = 0; i < track->getNumEvents(); ++i)
        {
            const auto& msg = track->getEventPointer(i)->message;
            if (msg.isTrackNameEvent() && trackName.isEmpty())
            {
                trackName = msg.getTextFromTextMetaEvent().trim();
            }

            double beat = timestampToBeat(msg.getTimeStamp());

            if (msg.isNoteOn())
            {
                int pitch = msg.getNoteNumber();
                float vel = msg.getFloatVelocity();
                pendingNotes[{ msg.getChannel(), pitch }].push_back({ beat, vel });
            }
            else if (msg.isNoteOff())
            {
                int pitch = msg.getNoteNumber();
                auto key = std::make_pair(msg.getChannel(), pitch);
                auto it = pendingNotes.find(key);
                if (it != pendingNotes.end() && !it->second.empty())
                {
                    auto prev = it->second.front();
                    it->second.pop_front();
                    double len = std::max(0.0625, beat - prev.startBeat);
                    NoteEvent n;
                    n.id = ++currentId;
                    n.pitch = pitch;
                    n.startBeat = prev.startBeat;
                    n.lengthBeats = len;
                    n.velocity = prev.velocity;
                    trackNotes.push_back(n);
                    if (it->second.empty())
                        pendingNotes.erase(it);
                }
            }
        }

        // Close any remaining dangling notes
        for (const auto& pair : pendingNotes)
            for (const auto& pending : pair.second)
            {
                NoteEvent n;
                n.id = ++currentId;
                n.pitch = pair.first.second;
                n.startBeat = pending.startBeat;
                n.lengthBeats = 1.0;
                n.velocity = pending.velocity;
                trackNotes.push_back(n);
            }

        if (!trackNotes.empty())
        {
            double maxB = 0.0;
            for (const auto& n : trackNotes)
                maxB = std::max(maxB, n.startBeat + n.lengthBeats);

            if (trackName.isEmpty())
            {
                if (mf.getNumTracks() == 1 || parsedTracks.empty())
                    trackName = baseFileName;
                else
                    trackName = baseFileName + " " + juce::String((int)parsedTracks.size() + 1);
            }
            parsedTracks.push_back({ trackName, trackNotes, maxB });
        }
    }

    if (!parsedTracks.empty() || !importedTempos.empty() || !importedTimeSigs.empty())
    {
        if (!importedTempos.empty())
        {
            model.setBpm(importedTempos.front().bpm);
            if (trackModel != nullptr)
            {
                auto* tempoTrk = trackModel->getTempoTrack();
                if (tempoTrk == nullptr)
                    tempoTrk = trackModel->addTrack("Tempo Track", TrackType::Tempo);
                tempoTrk->getTempoEvents() = importedTempos;
                trackModel->triggerTempoChanged();
            }
        }
        if (!importedTimeSigs.empty())
        {
            model.setTimeSignature(importedTimeSigs.front().numerator, importedTimeSigs.front().denominator);
            if (trackModel != nullptr)
            {
                trackModel->setDefaultTimeSignature(importedTimeSigs.front().numerator, importedTimeSigs.front().denominator);
                auto* tsTrk = trackModel->getTimeSignatureTrack();
                if (tsTrk == nullptr)
                    tsTrk = trackModel->addTrack("Time Signature", TrackType::TimeSignature);
                tsTrk->getTimeSignatures() = importedTimeSigs;
                trackModel->triggerTimeSignaturesChanged();
            }
        }
        if (trackModel != nullptr)
        {
            // If the project only has the initial default empty "Piano" track with 0 notes, clean it up
            auto* defaultInst = trackModel->getActiveInstrumentTrack();
            if (!parsedTracks.empty() && defaultInst != nullptr && defaultInst->getClips().size() == 1
                && defaultInst->getClips().front().notes.empty())
            {
                trackModel->removeTrack(defaultInst->getId());
            }

            int bpb = model.getTimeSigNumerator();
            if (bpb <= 0) bpb = 4;

            int firstTrackId = -1;
            int64_t firstClipId = -1;

            for (size_t i = 0; i < parsedTracks.size(); ++i)
            {
                const auto& pt = parsedTracks[i];
                double clipLen = std::ceil(pt.maxBeat / (double)bpb) * (double)bpb;
                if (clipLen < (double)bpb * 4)
                    clipLen = (double)bpb * 4;

                auto* newTrk = trackModel->addTrack(pt.name, TrackType::Instrument);
                if (newTrk != nullptr)
                {
                    MidiClip newClip;
                    newClip.id = juce::Time::currentTimeMillis() + (int64_t)i + 1;
                    newClip.name = pt.name;
                    newClip.startBeat = 0.0;
                    newClip.lengthBeats = clipLen;
                    newClip.notes = pt.notes;
                    newClip.color = newTrk->getColor();
                    newClip.isSelected = (i == 0);

                    newTrk->addClip(newClip);

                    if (firstTrackId == -1)
                    {
                        firstTrackId = newTrk->getId();
                        firstClipId = newClip.id;
                    }
                }
            }

            if (firstTrackId != -1)
            {
                trackModel->setActiveClip(firstTrackId, firstClipId, &model);
            }
            trackModel->triggerTracksChanged();
        }
        else if (!parsedTracks.empty())
        {
            model.setAllNotes(parsedTracks.front().notes, true);
        }

        return true;
    }

    return false;
}

bool ProjectFile::importMidiAsClips(MidiModel& model, const juce::File& midiFile,
                                    TrackModel& trackModel, int preferredTrackId,
                                    double startBeat, juce::UndoManager* undoManager)
{
    if (!std::isfinite(startBeat) || startBeat < 0.0 || !midiFile.existsAsFile()
        || midiFile.getSize() > (juce::int64)maxProjectFileBytes)
        return false;

    // Parse into a separate project so a dropped MIDI part cannot change the
    // current tempo, time signature, tracks or active notes.
    MidiModel importedModel;
    TrackModel importedTracks;
    if (!importMidi(importedModel, midiFile, &importedTracks))
        return false;

    struct ImportPart { juce::String trackName; MidiClip clip; };
    std::vector<ImportPart> parts;
    for (const auto& track : importedTracks.getTracks())
        if (track->getType() == TrackType::Instrument)
            for (const auto& clip : track->getClips())
                if (!clip.notes.empty())
                    parts.push_back({ track->getName(), clip });
    if (parts.empty())
        return false;

    size_t noteCount = 0;
    for (const auto& part : parts)
    {
        if (!std::isfinite(part.clip.startBeat + startBeat)
            || !std::isfinite(part.clip.lengthBeats) || part.clip.lengthBeats <= 0.0)
            return false;
        noteCount += part.clip.notes.size();
        for (const auto& note : part.clip.notes)
            if (!std::isfinite(note.startBeat + startBeat)
                || !std::isfinite(note.lengthBeats) || note.lengthBeats <= 0.0)
                return false;
    }

    int64_t nextClipId = 0;
    int64_t nextNoteId = 0;
    for (const auto& track : trackModel.getTracks())
        for (const auto& clip : track->getClips())
        {
            nextClipId = std::max(nextClipId, clip.id);
            for (const auto& note : clip.notes)
                nextNoteId = std::max(nextNoteId, note.id);
        }
    for (const auto& note : model.getNotes())
        nextNoteId = std::max(nextNoteId, note.id);
    if (parts.size() >= (size_t)(std::numeric_limits<int64_t>::max() - nextClipId)
        || noteCount >= (size_t)(std::numeric_limits<int64_t>::max() - nextNoteId))
        return false;

    auto* preferred = trackModel.getTrackById(preferredTrackId);
    const bool usePreferred = preferred != nullptr
        && preferred->getType() == TrackType::Instrument && !preferred->isLocked()
        && trackModel.canPlaceEvent(preferredTrackId,
            parts.front().clip.startBeat + startBeat, parts.front().clip.lengthBeats);
    if (undoManager != nullptr)
        undoManager->beginNewTransaction("Import MIDI File");

    bool succeeded = true;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        auto* destination = (i == 0 && usePreferred) ? preferred : nullptr;
        if (destination == nullptr)
        {
            if (undoManager != nullptr)
            {
                if (!undoManager->perform(new AddTrackUndoAction(trackModel,
                        parts[i].trackName, TrackType::Instrument), "Import MIDI File"))
                {
                    succeeded = false;
                    break;
                }
                destination = trackModel.getTracks().back().get();
            }
            else
                destination = trackModel.addTrack(parts[i].trackName, TrackType::Instrument);
        }
        if (destination == nullptr)
        {
            succeeded = false;
            break;
        }

        auto clip = parts[i].clip;
        clip.id = ++nextClipId;
        clip.startBeat += startBeat;
        clip.color = destination->getColor();
        clip.hasCustomColor = false;
        clip.isSelected = true;
        for (auto& note : clip.notes)
        {
            note.id = ++nextNoteId;
            note.startBeat += startBeat;
        }

        if (undoManager != nullptr)
        {
            auto after = destination->getClips();
            after.push_back(std::move(clip));
            if (!undoManager->perform(new ModifyClipsUndoAction(trackModel,
                    destination->getId(), destination->getClips(), after,
                    "Import MIDI Part"), "Import MIDI File"))
            {
                succeeded = false;
                break;
            }
        }
        else
            destination->addClip(clip);
    }

    if (undoManager != nullptr)
    {
        if (!succeeded)
            undoManager->undoCurrentTransactionOnly();
        undoManager->beginNewTransaction();
    }
    else if (succeeded)
        trackModel.triggerTracksChanged();
    return succeeded;
}

bool ProjectFile::importAudioAsClip(TrackModel& trackModel, const juce::File& audioFile,
                                    int preferredTrackId, double startBeat, double bpm,
                                    juce::UndoManager* undoManager)
{
    if (!std::isfinite(startBeat) || startBeat < 0.0 || !audioFile.existsAsFile())
        return false;

    const double duration = TrackModel::getAudioFileDuration(audioFile);
    if (duration <= 0.0)
        return false;

    auto* destination = trackModel.getTrackById(preferredTrackId);
    const double endBeat = trackModel.getBeatAfterSeconds(startBeat, duration, bpm);
    bool destinationIsFree = destination != nullptr
        && destination->getType() == TrackType::Audio && !destination->isLocked();
    if (destinationIsFree)
        for (const auto& clip : destination->getAudioClips())
            if (startBeat < clip.startBeat + clip.lengthBeats - 0.001
                && endBeat > clip.startBeat + 0.001)
            {
                destinationIsFree = false;
                break;
            }
    if (destinationIsFree)
        return trackModel.importAudioClip(destination->getId(), audioFile, startBeat,
                                          bpm, undoManager);

    if (undoManager != nullptr)
    {
        undoManager->beginNewTransaction("Import Audio File");
        if (!undoManager->perform(new AddTrackUndoAction(trackModel,
                audioFile.getFileNameWithoutExtension(), TrackType::Audio), "Import Audio File"))
        {
            undoManager->beginNewTransaction();
            return false;
        }
        destination = trackModel.getTracks().back().get();
        // The add-track action captures the clip with the whole new track.
        const bool imported = destination != nullptr && trackModel.importAudioClip(
            destination->getId(), audioFile, startBeat, bpm, nullptr);
        if (!imported)
            undoManager->undoCurrentTransactionOnly();
        undoManager->beginNewTransaction();
        return imported;
    }

    destination = trackModel.addTrack(audioFile.getFileNameWithoutExtension(), TrackType::Audio);
    if (destination == nullptr)
        return false;
    const int newTrackId = destination->getId();
    if (trackModel.importAudioClip(newTrackId, audioFile, startBeat, bpm, nullptr))
        return true;
    trackModel.removeTrack(newTrackId);
    return false;
}

bool ProjectFile::exportMidi(const MidiModel& model, const juce::File& targetFile, const TrackModel* trackModel, double rangeStartBeat, double rangeEndBeat)
{
    juce::MidiFile mf;
    const short ticksPerQuarter = 960;
    mf.setTicksPerQuarterNote(ticksPerQuarter);

    bool hasRange = (rangeEndBeat > rangeStartBeat && rangeStartBeat >= 0.0);
    double offsetBeat = hasRange ? rangeStartBeat : 0.0;

    // Track 0: Tempo & Time Signature meta track
    juce::MidiMessageSequence metaSeq;
    auto addTempo = [&](double beat, double bpm) {
        if (beat < offsetBeat || (hasRange && beat >= rangeEndBeat))
            return;
        int micros = (int)std::round(60000000.0 / std::clamp(bpm, 20.0, 400.0));
        metaSeq.addEvent(juce::MidiMessage::tempoMetaEvent(micros),
                         (beat - offsetBeat) * ticksPerQuarter);
    };
    auto addTimeSignature = [&](double beat, int numerator, int denominator) {
        if (beat < offsetBeat || (hasRange && beat >= rangeEndBeat))
            return;
        metaSeq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(numerator, denominator),
                         (beat - offsetBeat) * ticksPerQuarter);
    };

    double initialBpm = model.getBpm();
    if (trackModel != nullptr)
        initialBpm = trackModel->getTempoAtBeat(offsetBeat, initialBpm);
    addTempo(offsetBeat, initialBpm);

    int initialNum = model.getTimeSigNumerator();
    int initialDenom = model.getTimeSigDenominator();
    if (trackModel != nullptr)
    {
        if (const auto* tempoTrack = trackModel->getTempoTrack())
        {
            auto tempos = tempoTrack->getTempoEvents();
            std::sort(tempos.begin(), tempos.end(), [](const TempoEvent& a, const TempoEvent& b) {
                return a.startBeat < b.startBeat;
            });
            for (size_t i = 0; i < tempos.size(); ++i)
            {
                if (tempos[i].startBeat > offsetBeat)
                    addTempo(tempos[i].startBeat, tempos[i].bpm);
                if (i + 1 < tempos.size() && tempos[i].type == TempoTransitionType::Ramp)
                {
                    const auto& next = tempos[i + 1];
                    int steps = std::clamp((int)std::ceil(std::abs(next.bpm - tempos[i].bpm)), 1, 400);
                    for (int step = 1; step < steps; ++step)
                    {
                        double fraction = (double)step / (double)steps;
                        addTempo(tempos[i].startBeat + fraction * (next.startBeat - tempos[i].startBeat),
                                 tempos[i].bpm + fraction * (next.bpm - tempos[i].bpm));
                    }
                }
            }
        }
        if (const auto* signatureTrack = trackModel->getTimeSignatureTrack())
        {
            auto signatures = signatureTrack->getTimeSignatures();
            std::sort(signatures.begin(), signatures.end(), [](const TimeSignatureEvent& a, const TimeSignatureEvent& b) {
                return a.startBeat < b.startBeat;
            });
            for (const auto& signature : signatures)
                if (signature.startBeat <= offsetBeat)
                {
                    initialNum = signature.numerator;
                    initialDenom = signature.denominator;
                }
                else
                    addTimeSignature(signature.startBeat, signature.numerator, signature.denominator);
        }
    }
    addTimeSignature(offsetBeat, initialNum, initialDenom);
    metaSeq.updateMatchedPairs();
    mf.addTrack(metaSeq);

    // Multi-track or single-track notes
    if (trackModel != nullptr)
    {
        int midiChannel = 1;
        for (const auto& trk : trackModel->getTracks())
        {
            if (trk->getType() != TrackType::Instrument || trk->isMuted())
                continue;

            juce::MidiMessageSequence trkSeq;
            trkSeq.addEvent(juce::MidiMessage::textMetaEvent(3, trk->getName()), 0.0);

            bool hasNotes = false;
            for (const auto& clip : trk->getClips())
            {
                if (clip.isMuted)
                    continue;

                for (const auto& note : clip.notes)
                {
                    if (note.isMuted)
                        continue;

                    double nStart = note.startBeat;
                    double nEnd = nStart + note.lengthBeats;

                    if (hasRange)
                    {
                        if (nEnd <= rangeStartBeat || nStart >= rangeEndBeat)
                            continue;
                        nStart = std::max(nStart, rangeStartBeat);
                        nEnd = std::min(nEnd, rangeEndBeat);
                    }

                    double startTick = (nStart - offsetBeat) * ticksPerQuarter;
                    double endTick = std::max(startTick + 1.0, (nEnd - offsetBeat) * ticksPerQuarter);

                    auto onMsg = juce::MidiMessage::noteOn(midiChannel, note.pitch, (juce::uint8)std::clamp((int)(note.velocity * 127.0f), 1, 127));
                    auto offMsg = juce::MidiMessage::noteOff(midiChannel, note.pitch);

                    trkSeq.addEvent(onMsg, startTick);
                    trkSeq.addEvent(offMsg, endTick);
                    hasNotes = true;
                }
            }

            if (hasNotes)
            {
                trkSeq.updateMatchedPairs();
                mf.addTrack(trkSeq);
                if (++midiChannel > 16) midiChannel = 1;
            }
        }
    }

    if (trackModel == nullptr)
    {
        juce::MidiMessageSequence noteSeq;
        for (const auto& note : model.getNotes())
        {
            if (note.isMuted)
                continue;

            double nStart = note.startBeat;
            double nEnd = nStart + note.lengthBeats;

            if (hasRange)
            {
                if (nEnd <= rangeStartBeat || nStart >= rangeEndBeat)
                    continue;
                nStart = std::max(nStart, rangeStartBeat);
                nEnd = std::min(nEnd, rangeEndBeat);
            }

            double startTick = (nStart - offsetBeat) * ticksPerQuarter;
            double endTick = std::max(startTick + 1.0, (nEnd - offsetBeat) * ticksPerQuarter);

            auto onMsg = juce::MidiMessage::noteOn(1, note.pitch, (juce::uint8)std::clamp((int)(note.velocity * 127.0f), 1, 127));
            auto offMsg = juce::MidiMessage::noteOff(1, note.pitch);

            noteSeq.addEvent(onMsg, startTick);
            noteSeq.addEvent(offMsg, endTick);
        }

        noteSeq.updateMatchedPairs();
        mf.addTrack(noteSeq);
    }

    juce::TemporaryFile tempFile(targetFile);
    {
        juce::FileOutputStream stream(tempFile.getFile());
        if (!stream.openedOk())
            return false;
        if (!mf.writeTo(stream))
            return false;
        stream.flush();
        if (stream.getStatus().failed())
            return false;
    }

    return tempFile.overwriteTargetFileWithTemporary();
}

} // namespace Harmonizer
