#include "TrackArrangerComponent.h"
#include "CubaseTheme.h"
#include "EventEditors.h"
#include "../core/Localization.h"
#include "../core/UndoActions.h"
#include "../core/KeyCommandManager.h"
#include "../core/TimelineSnapping.h"
#include <set>

namespace Harmonizer
{

TrackArrangerComponent::TrackArrangerComponent(TrackModel& tm, MidiModel& mm, AudioEngine& ae,
                                               juce::UndoManager& um,
                                               std::function<void(int, int64_t)> onClipDoubleClicked)
    : trackModel(tm), midiModel(mm), audioEngine(ae), undoManager(um), clipDoubleClickedCallback(onClipDoubleClicked)
{
    waveformFormats.registerBasicFormats();
    trackModel.addListener(this);
    midiModel.addListener(this);
    audioEngine.addPlayheadListener(this);

    btnAddTrack.setButtonText("+ " + tr("Add Track"));
    btnAddTrack.onClick = [this]() {
        showAddTrackMenu();
    };
    addAndMakeVisible(btnAddTrack);

    horizontalScrollBar.addListener(this);
    addAndMakeVisible(horizontalScrollBar);

    setWantsKeyboardFocus(true);
    tracksChanged();
}

//==============================================================================
// 4x4 Visual Color Palette Custom Component for Track Context Menu
//==============================================================================
class TrackColorPaletteComponent : public juce::PopupMenu::CustomComponent
{
public:
    TrackColorPaletteComponent(const std::vector<juce::Colour>& paletteColors, juce::Colour current, std::function<void(juce::Colour)> onSelect)
        : colors(paletteColors), currentColor(current), onColorSelected(std::move(onSelect))
    {
    }

    void getIdealSize(int& idealWidth, int& idealHeight) override
    {
        idealWidth = 168;
        idealHeight = 168;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(CubaseTheme::bgPanel());

        int cellSize = 38;
        int padding = 8;
        int swatchSize = 28;

        for (int i = 0; i < (int)colors.size() && i < 16; ++i)
        {
            int row = i / 4;
            int col = i % 4;
            int x = padding + col * cellSize;
            int y = padding + row * cellSize;

            auto swatchArea = juce::Rectangle<float>((float)x, (float)y, (float)swatchSize, (float)swatchSize);

            bool isHovered = (hoveredIndex == i);
            bool isCurrent = (colors[i] == currentColor);

            if (isHovered)
                swatchArea = swatchArea.expanded(1.5f);

            g.setColour(colors[i]);
            g.fillRoundedRectangle(swatchArea, 4.0f);

            if (isCurrent)
            {
                g.setColour(juce::Colours::white);
                g.drawRoundedRectangle(swatchArea, 4.0f, 2.0f);

                g.setColour(CubaseTheme::getReadableTextColor(colors[i]));
                g.setFont(CubaseTheme::getFont(13.0f, true));
                g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x93"), swatchArea.toNearestInt(), juce::Justification::centred);
            }
            else if (isHovered)
            {
                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.drawRoundedRectangle(swatchArea, 4.0f, 1.5f);
            }
            else
            {
                g.setColour(CubaseTheme::borderSubtle());
                g.drawRoundedRectangle(swatchArea, 4.0f, 1.0f);
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        int newHover = hitTestSwatch(e.getPosition());
        if (newHover != hoveredIndex)
        {
            hoveredIndex = newHover;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (hoveredIndex != -1)
        {
            hoveredIndex = -1;
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        int clicked = hitTestSwatch(e.getPosition());
        if (clicked >= 0 && clicked < (int)colors.size())
        {
            if (onColorSelected)
                onColorSelected(colors[clicked]);
            triggerMenuItem();
        }
    }

private:
    int hitTestSwatch(juce::Point<int> pos) const
    {
        int cellSize = 38;
        int padding = 8;
        int swatchSize = 28;

        for (int i = 0; i < (int)colors.size() && i < 16; ++i)
        {
            int row = i / 4;
            int col = i % 4;
            int x = padding + col * cellSize;
            int y = padding + row * cellSize;
            auto swatchArea = juce::Rectangle<int>(x, y, swatchSize, swatchSize);
            if (swatchArea.expanded(3).contains(pos))
                return i;
        }
        return -1;
    }

    std::vector<juce::Colour> colors;
    juce::Colour currentColor;
    std::function<void(juce::Colour)> onColorSelected;
    int hoveredIndex = -1;
};

void TrackArrangerComponent::paintOverChildren(juce::Graphics& g)
{
    if (isFocusedArea)
    {
        g.setColour(CubaseTheme::accentCyan().withAlpha(0.9f));
        g.drawRect(getLocalBounds(), 2);
    }
}

void TrackArrangerComponent::showAddTrackMenu()
{
    bool hasTempo = (trackModel.getTempoTrack() != nullptr);
    bool hasChord = (trackModel.getChordTrack() != nullptr);
    bool hasTimeSig = (trackModel.getTimeSignatureTrack() != nullptr);
    bool hasMode = (trackModel.getModeTrack() != nullptr);

    juce::PopupMenu menu;
    menu.addItem(1, "+ " + tr("Instrument / VST3 Track"));
    menu.addItem(2, "+ " + tr("Mode / Tonality Track"), !hasMode);
    menu.addItem(3, "+ " + tr("Marker Track"));
    menu.addItem(4, "+ " + tr("Chord Track"), !hasChord);
    menu.addItem(5, "+ " + tr("Time Signature Track"), !hasTimeSig);
    menu.addItem(6, "+ " + tr("Folder Track") + " / 文件夹轨道");
    menu.addItem(7, "+ " + tr("Tempo Track") + " / 速度轨", !hasTempo);
    menu.addItem(8, "+ " + juce::String::fromUTF8(u8"音频轨 / Audio Track"));

    juce::Component::SafePointer<TrackArrangerComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&btnAddTrack), [this, safeThis](int result) {
        if (safeThis.getComponent() == nullptr) return;
        if (result == 1)
        {
            int count = 1;
            for (const auto& t : trackModel.getTracks())
                if (t->getType() == TrackType::Instrument) count++;

            UndoActionHelper::performAtomic(&undoManager, new AddTrackUndoAction(trackModel, "Instrument " + juce::String(count), TrackType::Instrument), "Add Instrument Track");
        }
        else if (result == 2)
        {
            UndoActionHelper::performAtomic(&undoManager, new AddTrackUndoAction(trackModel, "Mode Track", TrackType::Mode), "Add Mode Track");
        }
        else if (result == 3)
        {
            UndoActionHelper::performAtomic(&undoManager, new AddTrackUndoAction(trackModel, "Marker Track", TrackType::Marker), "Add Marker Track");
        }
        else if (result == 4)
        {
            UndoActionHelper::performAtomic(&undoManager, new AddTrackUndoAction(trackModel, "Chord Track", TrackType::Chord), "Add Chord Track");
        }
        else if (result == 5)
        {
            UndoActionHelper::performAtomic(&undoManager, new AddTrackUndoAction(trackModel, "Time Signature Track", TrackType::TimeSignature), "Add Time Signature Track");
        }
        else if (result == 6)
        {
            int count = 1;
            for (const auto& t : trackModel.getTracks())
                if (t->getType() == TrackType::Folder) count++;
            UndoActionHelper::performAtomic(&undoManager, new AddTrackUndoAction(trackModel, "Folder " + juce::String(count), TrackType::Folder), "Add Folder Track");
        }
        else if (result == 7)
        {
            UndoActionHelper::performAtomic(&undoManager, new AddTrackUndoAction(trackModel, "Tempo Track", TrackType::Tempo), "Add Tempo Track");
        }
        else if (result == 8)
        {
            int count = 1;
            for (const auto& t : trackModel.getTracks())
                if (t->getType() == TrackType::Audio) ++count;
            UndoActionHelper::performAtomic(&undoManager,
                new AddTrackUndoAction(trackModel, "Audio " + juce::String(count), TrackType::Audio),
                "Add Audio Track");
        }
    });
}

void TrackArrangerComponent::importAudioForTrack(int trackId, double startBeat)
{
    auto* track = trackModel.getTrackById(trackId);
    if (track == nullptr || track->getType() != TrackType::Audio || track->isLocked()) return;
    juce::FileChooser chooser(juce::String::fromUTF8(u8"导入音频文件 / Import Audio"),
                             juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    if (!chooser.browseForFileToOpen()) return;
    if (startBeat < 0.0) startBeat = audioEngine.getPlayheadBeat();
    if (!trackModel.importAudioClip(trackId, chooser.getResult(), std::max(0.0, startBeat),
                                    midiModel.getBpm(), &undoManager))
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            juce::String::fromUTF8(u8"无法导入音频"),
            juce::String::fromUTF8(u8"文件无法读取、格式不受支持，或轨道已锁定。"));
}

bool TrackArrangerComponent::getFileDropTarget(juce::Point<int> localPoint,
                                               int& trackId, double& beat) const
{
    if (!isVisible() || !getLocalBounds().contains(localPoint)
        || localPoint.y >= getHeight() - 16)
        return false;

    trackId = -1;
    if (localPoint.y >= rulerHeight)
    {
        const auto visibleTracks = trackModel.getVisibleTracks();
        const int index = (localPoint.y - rulerHeight + scrollTrackY) / trackHeight;
        if (index >= 0 && index < (int)visibleTracks.size())
            trackId = visibleTracks[(size_t)index]->getId();
    }
    beat = localPoint.x >= headerWidth
        ? std::max(0.0, xToBeat((float)localPoint.x))
        : std::max(0.0, audioEngine.getPlayheadBeat());
    if (midiModel.isSnapEnabled())
        beat = std::max(0.0, midiModel.quantizeBeat(beat));
    return true;
}

void TrackArrangerComponent::tracksChanged()
{
    std::set<std::string> present;
    for (const auto& track : trackModel.getTracks())
        if (track->getType() == TrackType::Audio)
            for (const auto& clip : track->getAudioClips())
            {
                const auto key = clip.sourcePath.toStdString();
                present.insert(key);
                if (waveforms.find(key) == waveforms.end())
                {
                    juce::File file(clip.sourcePath);
                    if (!file.existsAsFile()) continue;
                    auto thumbnail = std::make_unique<juce::AudioThumbnail>(512, waveformFormats, waveformCache);
                    thumbnail->addChangeListener(this);
                    thumbnail->setSource(new juce::FileInputSource(file));
                    waveforms[key] = std::move(thumbnail);
                }
            }
    for (auto it = waveforms.begin(); it != waveforms.end();)
    {
        if (present.find(it->first) == present.end())
        {
            it->second->removeChangeListener(this);
            it = waveforms.erase(it);
        }
        else ++it;
    }
    const int totalTrackHeight = (int)trackModel.getVisibleTracks().size() * trackHeight;
    const int viewportHeight = std::max(0, getHeight() - rulerHeight - 40);
    scrollTrackY = std::clamp(scrollTrackY, 0, std::max(0, totalTrackHeight - viewportHeight));
    updateScrollBar();
    repaint();
}

void TrackArrangerComponent::showTrackContextMenu(int trackId, juce::Point<int> screenPos)
{
    auto* trk = trackModel.getTrackById(trackId);
    if (!trk) return;

    bool canDuplicate = (trk->getType() != TrackType::Tempo &&
                         trk->getType() != TrackType::Chord &&
                         trk->getType() != TrackType::TimeSignature &&
                         trk->getType() != TrackType::Mode);

    juce::PopupMenu menu;
    menu.addItem(1, tr("Add Track..."));
    menu.addSeparator();
    menu.addItem(2, tr("Duplicate Track"), canDuplicate);
    menu.addItem(3, tr("Move Track Up"));
    menu.addItem(4, tr("Move Track Down"));
    menu.addSeparator();
    menu.addItem(5, tr("Delete Track"));
    if (trk->getType() == TrackType::Audio)
        menu.addItem(6, juce::String::fromUTF8(u8"导入音频文件..."));

    // 16-color graphical visual palette
    static const uint32_t paletteArgbs[16] = {
        0xff2575fc, 0xff06b6d4, 0xff14b8a6, 0xff10b981,
        0xff84cc16, 0xffeab308, 0xfff59e0b, 0xfff97316,
        0xffef4444, 0xfff43f5e, 0xffec4899, 0xffd946ef,
        0xffa855f7, 0xff8b5cf6, 0xff6366f1, 0xff64748b
    };
    std::vector<juce::Colour> paletteColors;
    for (int i = 0; i < 16; ++i)
        paletteColors.push_back(juce::Colour(paletteArgbs[i]));

    juce::PopupMenu colorMenu;
    juce::Component::SafePointer<TrackArrangerComponent> safeThis(this);
    auto colorPaletteComp = std::make_unique<TrackColorPaletteComponent>(
        paletteColors, trk->getColor(),
        [this, safeThis, trackId](juce::Colour newCol) {
            if (safeThis.getComponent() == nullptr) return;
            if (auto* t = trackModel.getTrackById(trackId))
            {
                auto oldCol = t->getColor();
                if (oldCol != newCol)
                {
                    UndoActionHelper::performAtomic(&undoManager,
                        new TrackPropertyUndoAction(trackModel, trackId, oldCol, newCol),
                        "Track Color");
                    midiModel.triggerProjectSettingsChanged();
                    repaint();
                }
            }
        });
    colorMenu.addCustomItem(100, std::move(colorPaletteComp));
    menu.addSeparator();
    menu.addSubMenu(tr("Track Color") + " / 轨道颜色 [Alt+C]", colorMenu);

    // Folder containment options
    std::vector<int> folderIds;
    std::vector<juce::String> folderNames;
    for (const auto& t : trackModel.getTracks())
    {
        if (t->getType() == TrackType::Folder && t->getId() != trk->getId())
        {
            folderIds.push_back(t->getId());
            folderNames.push_back(t->getName());
        }
    }

    if (!folderIds.empty())
    {
        juce::PopupMenu folderMenu;
        for (int i = 0; i < (int)folderIds.size(); ++i)
        {
            bool isCurrent = (trk->getParentFolderId() == folderIds[i]);
            folderMenu.addItem(200 + i, folderNames[i],
                               trackModel.canNestInFolder(trackId, folderIds[i]), isCurrent);
        }
        menu.addSubMenu(tr("Move to Folder") + " / 移入文件夹", folderMenu);
    }
    if (trk->getParentFolderId() > 0)
    {
        menu.addItem(250, tr("Remove from Folder") + " / 移出文件夹");
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, safeThis, trackId, folderIds](int result) {
            if (safeThis.getComponent() == nullptr) return;
            if (result == 1)
            {
                showAddTrackMenu();
            }
            else if (result == 2)
            {
                UndoActionHelper::performAtomic(&undoManager,
                    new DuplicateTrackUndoAction(trackModel, trackId),
                    "Duplicate Track");
            }
            else if (result == 3)
            {
                trackModel.moveTrackUp(trackId, &undoManager);
            }
            else if (result == 4)
            {
                trackModel.moveTrackDown(trackId, &undoManager);
            }
            else if (result == 5)
            {
                UndoActionHelper::performAtomic(&undoManager,
                    new DeleteTrackUndoAction(trackModel, trackId),
                    "Delete Track");
            }
            else if (result == 6)
            {
                importAudioForTrack(trackId);
            }
            else if (result >= 200 && result < 200 + (int)folderIds.size())
            {
                int targetFolderId = folderIds[result - 200];
                if (auto* t = trackModel.getTrackById(trackId))
                {
                    int oldFolder = t->getParentFolderId();
                    if (oldFolder != targetFolderId && trackModel.canNestInFolder(trackId, targetFolderId))
                    {
                        UndoActionHelper::performAtomic(&undoManager,
                            new TrackPropertyUndoAction(trackModel, trackId, oldFolder, targetFolderId),
                            "Move to Folder");
                    }
                }
            }
            else if (result == 250)
            {
                if (auto* t = trackModel.getTrackById(trackId))
                {
                    int oldFolder = t->getParentFolderId();
                    if (oldFolder != -1)
                    {
                        UndoActionHelper::performAtomic(&undoManager,
                            new TrackPropertyUndoAction(trackModel, trackId, oldFolder, -1),
                            "Remove from Folder");
                    }
                }
            }
        });
}

void TrackArrangerComponent::showLaneContextMenu(int trackId, double beatPos, juce::Point<int> screenPos)
{
    auto* trk = trackModel.getTrackById(trackId);
    if (!trk) return;

    int bpb = midiModel.getTimeSigNumerator();
    double snapInterval = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.0;
    double snapBeat = beatPos;
    if (midiModel.isSnapEnabled() && snapInterval > 0.0)
        snapBeat = std::round(beatPos / snapInterval) * snapInterval;

    juce::PopupMenu menu;
    int64_t hitAudioClipId = -1;

    if (trk->getType() == TrackType::Audio)
    {
        menu.addItem(1, juce::String::fromUTF8(u8"在此处导入音频..."), !trk->isLocked());
        const AudioClip* hitClip = nullptr;
        for (auto it = trk->getAudioClips().rbegin(); it != trk->getAudioClips().rend(); ++it)
        {
            const auto& clip = *it;
            if (beatPos >= clip.startBeat && beatPos <= clip.startBeat + clip.lengthBeats)
            {
                hitClip = &clip;
                break;
            }
        }
        if (hitClip != nullptr)
        {
            hitAudioClipId = hitClip->id;
            menu.addItem(2, juce::String::fromUTF8(u8"删除音频片段"), !trk->isLocked());
            menu.addItem(3, hitClip->isMuted ? juce::String::fromUTF8(u8"取消片段静音")
                                              : juce::String::fromUTF8(u8"静音此片段"), !trk->isLocked());
            menu.addItem(4, juce::String::fromUTF8(u8"替换或重新关联源文件..."), !trk->isLocked());
        }
    }

    if (trk->getType() == TrackType::Chord)
    {
        menu.addItem(1, tr("Add Chord Block here"));
        // Check if there is an existing chord here to delete/edit
        int chordIdx = -1;
        for (int i = 0; i < (int)trk->getChords().size(); ++i)
        {
            const auto& c = trk->getChords()[i];
            if (beatPos >= c.startBeat && beatPos <= (c.startBeat + c.lengthBeats))
            {
                chordIdx = i;
                break;
            }
        }
        if (chordIdx >= 0)
        {
            menu.addItem(2, tr("Edit Chord: ") + trk->getChords()[chordIdx].chordName);
            menu.addItem(3, tr("Delete Chord"));
        }
    }
    else if (trk->getType() == TrackType::Mode)
    {
        menu.addItem(1, tr("Add Mode Section here"));
        int modeIdx = -1;
        for (int i = 0; i < (int)trk->getModes().size(); ++i)
        {
            const auto& m = trk->getModes()[i];
            if (beatPos >= m.startBeat && beatPos <= (m.startBeat + m.lengthBeats))
            {
                modeIdx = i;
                break;
            }
        }
        if (modeIdx >= 0)
        {
            menu.addItem(2, tr("Edit Mode: ") + trk->getModes()[modeIdx].name);
            menu.addItem(3, tr("Delete Mode Section"));
        }
    }
    else if (trk->getType() == TrackType::Marker)
    {
        menu.addItem(1, tr("Add Marker / Mode Section here"));
        int markerIdx = -1;
        for (int i = 0; i < (int)trk->getMarkers().size(); ++i)
        {
            const auto& m = trk->getMarkers()[i];
            if (beatPos >= m.startBeat && beatPos <= (m.startBeat + m.lengthBeats))
            {
                markerIdx = i;
                break;
            }
        }
        if (markerIdx >= 0)
        {
            menu.addItem(2, tr("Delete Marker"));
        }
    }
    else if (trk->getType() == TrackType::TimeSignature)
    {
        menu.addItem(1, tr("Add Time Signature Change here"));
        int sigIdx = -1;
        for (int i = 0; i < (int)trk->getTimeSignatures().size(); ++i)
        {
            const auto& ts = trk->getTimeSignatures()[i];
            if (beatPos >= ts.startBeat && beatPos <= (ts.startBeat + ts.lengthBeats))
            {
                sigIdx = i;
                break;
            }
        }
        if (sigIdx >= 0)
        {
            menu.addItem(2, tr("Delete Time Signature"));
            menu.addItem(3, tr("Edit Time Signature..."));
            menu.addSeparator();
            menu.addItem(4, juce::String::fromUTF8((const char*)u8"全局小节偏移... (Global Bar Offset...)"));
        }
        else
        {
            menu.addSeparator();
            menu.addItem(4, juce::String::fromUTF8((const char*)u8"全局小节偏移... (Global Bar Offset...)"));
        }
    }
    else if (trk->getType() == TrackType::Tempo)
    {
        menu.addItem(1, juce::String::fromUTF8((const char*)u8"在此处添加速度节点 (Add Tempo Point)"));
        int tempoIdx = -1;
        for (int i = 0; i < (int)trk->getTempoEvents().size(); ++i)
        {
            const auto& ev = trk->getTempoEvents()[(size_t)i];
            if (std::abs(beatPos - ev.startBeat) <= 1.0)
            {
                tempoIdx = i;
                break;
            }
        }
        if (tempoIdx >= 0)
        {
            menu.addItem(2, juce::String::fromUTF8((const char*)u8"删除速度节点 (Delete Tempo Point)"));
            menu.addItem(3, juce::String::fromUTF8((const char*)u8"编辑速度... (Edit Tempo...)"));
            menu.addItem(4, trk->getTempoEvents()[(size_t)tempoIdx].type == TempoTransitionType::Jump ?
                            juce::String::fromUTF8((const char*)u8"切换为渐变 (Switch to Ramp)") :
                            juce::String::fromUTF8((const char*)u8"切换为步进 (Switch to Jump)"));
        }
    }
    else if (trk->getType() == TrackType::Instrument)
    {
        menu.addItem(1, tr("Create MIDI Part here"));
        int clipIdx = -1;
        for (int i = 0; i < (int)trk->getClips().size(); ++i)
        {
            const auto& c = trk->getClips()[i];
            if (beatPos >= c.startBeat && beatPos <= (c.startBeat + c.lengthBeats))
            {
                clipIdx = i;
                break;
            }
        }
        if (clipIdx >= 0)
        {
            menu.addItem(2, tr("Delete MIDI Part"));
        }
        int selCount = 0;
        for (const auto& c : trk->getClips()) if (c.isSelected) selCount++;
        if (selCount >= 2)
        {
            menu.addItem(10, juce::String::fromUTF8((const char*)u8"合并/胶水选中的 MIDI 块 (Ctrl+G)"));
        }

        if (clipIdx >= 0 || selCount > 0)
        {
            static const uint32_t paletteArgbs[16] = {
                0xff2575fc, 0xff06b6d4, 0xff14b8a6, 0xff10b981,
                0xff84cc16, 0xffeab308, 0xfff59e0b, 0xfff97316,
                0xffef4444, 0xfff43f5e, 0xffec4899, 0xffd946ef,
                0xffa855f7, 0xff8b5cf6, 0xff6366f1, 0xff64748b
            };
            std::vector<juce::Colour> paletteColors;
            for (int i = 0; i < 16; ++i) paletteColors.push_back(juce::Colour(paletteArgbs[i]));

            juce::PopupMenu clipColorMenu;
            juce::Component::SafePointer<TrackArrangerComponent> safeColorThis(this);
            auto colorComp = std::make_unique<TrackColorPaletteComponent>(
                paletteColors, (clipIdx >= 0 ? trk->getClips()[clipIdx].color : trk->getColor()),
                [this, safeColorThis, trackId, clipIdx](juce::Colour chosenCol) {
                    if (safeColorThis.getComponent() == nullptr) return;
                    if (auto* t = trackModel.getTrackById(trackId))
                    {
                        auto before = t->getClips();
                        auto after = before;
                        bool anySelected = false;
                        for (auto& c : after)
                        {
                            if (c.isSelected)
                            {
                                c.color = chosenCol;
                                c.hasCustomColor = true;
                                anySelected = true;
                            }
                        }
                        if (!anySelected && clipIdx >= 0 && clipIdx < (int)after.size())
                        {
                            after[clipIdx].color = chosenCol;
                            after[clipIdx].hasCustomColor = true;
                        }
                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyClipsUndoAction(trackModel, trackId, before, after, "Clip Color"),
                            "Clip Color");
                        repaint();
                    }
                });
            clipColorMenu.addCustomItem(100, std::move(colorComp));
            clipColorMenu.addSeparator();
            clipColorMenu.addItem(101, juce::String::fromUTF8(u8"跟随所属轨道颜色 (Reset to Track Color)"));
            menu.addSubMenu(juce::String::fromUTF8(u8"事件块颜色 (Clip Color) [Alt+C]"), clipColorMenu);
        }
    }

    juce::Component::SafePointer<TrackArrangerComponent> safeMenuThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, safeMenuThis, trackId, snapBeat, beatPos, hitAudioClipId](int result) {
            if (safeMenuThis.getComponent() == nullptr) return;
            auto* t = trackModel.getTrackById(trackId);
            if (!t) return;

            if (t->getType() == TrackType::Audio)
            {
                if (t->isLocked()) return;
                if (result == 1) importAudioForTrack(trackId, snapBeat);
                else if (result == 2)
                {
                    auto before = t->getAudioClips();
                    auto after = before;
                    after.erase(std::remove_if(after.begin(), after.end(), [hitAudioClipId](const AudioClip& clip) {
                        return clip.id == hitAudioClipId;
                    }), after.end());
                    if (after.size() != before.size())
                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyAudioClipsUndoAction(trackModel, trackId, before, after), "Delete Audio Clip");
                }
                else if (result == 3)
                {
                    auto before = t->getAudioClips();
                    auto after = before;
                    for (auto& clip : after)
                        if (clip.id == hitAudioClipId)
                        {
                            clip.isMuted = !clip.isMuted;
                            break;
                        }
                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyAudioClipsUndoAction(trackModel, trackId, before, after), "Mute Audio Clip");
                }
                else if (result == 4)
                {
                    juce::FileChooser chooser(juce::String::fromUTF8(u8"选择音频源文件"),
                        juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
                    if (chooser.browseForFileToOpen()
                        && !trackModel.replaceAudioClipSource(trackId, hitAudioClipId,
                            chooser.getResult(), midiModel.getBpm(), &undoManager))
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                            juce::String::fromUTF8(u8"无法关联音频"),
                            juce::String::fromUTF8(u8"所选文件无法读取，或格式不受支持。"));
                }
                return;
            }

            if (t->getType() == TrackType::Chord)
            {
                if (result == 1)
                {
                    double chordLen = (double)midiModel.getTimeSigNumerator();
                    if (!trackModel.canPlaceEvent(t->getId(), snapBeat, chordLen))
                        return;

                    auto before = t->getChords();
                    auto after = before;
                    ChordEvent c;
                    c.id = juce::Time::currentTimeMillis();
                    c.startBeat = snapBeat;
                    c.lengthBeats = chordLen;
                    c.chordName = "C";
                    c.romanNumeral = "I";
                    c.color = juce::Colour(0xfff59e0b);
                    after.push_back(c);

                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyChordsUndoAction(trackModel, t->getId(), before, after, "Add Chord Block"),
                        "Add Chord Block");
                }
                else if (result == 2)
                {
                    // Edit chord popup
                    for (auto& c : t->getChords())
                    {
                        if (beatPos >= c.startBeat && beatPos <= (c.startBeat + c.lengthBeats))
                        {
                            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, tr("Edit Chord"),
                                "Edit chord name (e.g. Dm7, G7, Cmaj7, Fm):",
                                tr("OK"), tr("Cancel"), nullptr,
                                juce::ModalCallbackFunction::create([this, &c](int r) {
                                    // Default OK callback
                                    trackModel.triggerChordsChanged();
                                }));
                            break;
                        }
                    }
                }
                else if (result == 3)
                {
                    auto before = t->getChords();
                    auto after = before;
                    after.erase(std::remove_if(after.begin(), after.end(), [beatPos](const ChordEvent& c) {
                        return beatPos >= c.startBeat && beatPos <= (c.startBeat + c.lengthBeats);
                    }), after.end());

                    if (before.size() != after.size())
                    {
                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyChordsUndoAction(trackModel, t->getId(), before, after, "Delete Chord"),
                            "Delete Chord");
                    }
                }
            }
            else if (t->getType() == TrackType::Mode)
            {
                if (result == 1)
                {
                    double modeLen = (double)(midiModel.getTimeSigNumerator() * 4);
                    if (!trackModel.canPlaceEvent(t->getId(), snapBeat, modeLen))
                        return;

                    auto before = t->getModes();
                    auto after = before;
                    ModeEvent md;
                    md.id = juce::Time::currentTimeMillis();
                    md.startBeat = snapBeat;
                    md.lengthBeats = modeLen;
                    md.name = "C Major";
                    md.tonic = "C";
                    md.tonicPc = 0;
                    md.scaleName = "Ionian (Natural Major)";
                    md.color = juce::Colour(0xff10b981);
                    after.push_back(md);

                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyModesUndoAction(trackModel, t->getId(), before, after, "Add Mode Section"),
                        "Add Mode Section");
                }
                else if (result == 3)
                {
                    auto before = t->getModes();
                    auto after = before;
                    after.erase(std::remove_if(after.begin(), after.end(), [beatPos](const ModeEvent& m) {
                        return beatPos >= m.startBeat && beatPos <= (m.startBeat + m.lengthBeats);
                    }), after.end());

                    if (before.size() != after.size())
                    {
                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyModesUndoAction(trackModel, t->getId(), before, after, "Delete Mode Section"),
                            "Delete Mode Section");
                    }
                }
            }
            else if (t->getType() == TrackType::Marker)
            {
                if (result == 1)
                {
                    auto before = t->getMarkers();
                    auto after = before;
                    MarkerEvent m;
                    m.id = juce::String(juce::Time::currentTimeMillis());
                    m.startBeat = snapBeat;
                    m.lengthBeats = (double)(midiModel.getTimeSigNumerator() * 4);
                    m.name = "Section";
                    m.color = juce::Colour(0xff06b6d4);
                    after.push_back(m);

                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyMarkersUndoAction(trackModel, t->getId(), before, after, "Add Marker"),
                        "Add Marker");
                }
                else if (result == 2)
                {
                    auto before = t->getMarkers();
                    auto after = before;
                    after.erase(std::remove_if(after.begin(), after.end(), [beatPos](const MarkerEvent& m) {
                        return beatPos >= m.startBeat && beatPos <= (m.startBeat + m.lengthBeats);
                    }), after.end());

                    if (before.size() != after.size())
                    {
                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyMarkersUndoAction(trackModel, t->getId(), before, after, "Delete Marker"),
                            "Delete Marker");
                    }
                }
            }
            else if (t->getType() == TrackType::TimeSignature)
            {
                if (result == 1)
                {
                    auto before = t->getTimeSignatures();
                    auto after = before;
                    TimeSignatureEvent ts;
                    ts.id = juce::Time::currentTimeMillis();
                    ts.startBeat = snapBeat;
                    ts.lengthBeats = 16.0;
                    ts.numerator = midiModel.getTimeSigNumerator();
                    ts.denominator = midiModel.getTimeSigDenominator();
                    after.push_back(ts);

                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyTimeSignaturesUndoAction(trackModel, t->getId(), before, after, "Add Time Signature Change"),
                        "Add Time Signature Change");
                }
                else if (result == 2)
                {
                    auto before = t->getTimeSignatures();
                    auto after = before;
                    after.erase(std::remove_if(after.begin(), after.end(), [beatPos](const TimeSignatureEvent& ts) {
                        return beatPos >= ts.startBeat && beatPos <= (ts.startBeat + ts.lengthBeats);
                    }), after.end());

                    if (before.size() != after.size())
                    {
                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyTimeSignaturesUndoAction(trackModel, t->getId(), before, after, "Delete Time Signature"),
                            "Delete Time Signature");
                    }
                }
                else if (result == 3)
                {
                    for (auto& ts : t->getTimeSignatures())
                    {
                        if (beatPos >= ts.startBeat && beatPos <= (ts.startBeat + ts.lengthBeats))
                        {
                            int targetId = ts.id;
                            TimeSigEditorDialog::showModal(ts, [this, safeMenuThis, trackId, targetId](int newNum, int newDen) {
                                if (safeMenuThis.getComponent() == nullptr) return;
                                if (auto* trkObj = trackModel.getTrackById(trackId))
                                {
                                    auto before = trkObj->getTimeSignatures();
                                    auto after = before;
                                    for (auto& item : after)
                                    {
                                        if (item.id == targetId)
                                        {
                                            item.numerator = newNum;
                                            item.denominator = newDen;
                                            break;
                                        }
                                    }
                                    UndoActionHelper::performAtomic(&undoManager,
                                        new ModifyTimeSignaturesUndoAction(trackModel, trackId, before, after, "Edit Time Signature"),
                                        "Edit Time Signature");
                                }
                            });
                            break;
                        }
                    }
                }
                else if (result == 4)
                {
                    BarOffsetDialog::showModal(trackModel);
                }
            }
            else if (t->getType() == TrackType::Tempo)
            {
                if (result == 1)
                {
                    auto oldEvents = t->getTempoEvents();
                    TempoEvent ev;
                    ev.id = juce::Time::currentTimeMillis();
                    ev.startBeat = snapBeat;
                    ev.bpm = t->getTempoEvents().empty() ? midiModel.getBpm() : t->getTempoEvents().back().bpm;
                    ev.type = TempoTransitionType::Jump;
                    ev.color = t->getColor();

                    auto newEvents = oldEvents;
                    newEvents.push_back(ev);
                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyTempoEventsUndoAction(trackModel, t->getId(), oldEvents, newEvents, "Add Tempo Point"),
                        "Add Tempo Point");
                }
                else if (result == 2)
                {
                    auto oldEvents = t->getTempoEvents();
                    auto newEvents = oldEvents;
                    newEvents.erase(std::remove_if(newEvents.begin(), newEvents.end(), [beatPos](const TempoEvent& ev) {
                        return std::abs(beatPos - ev.startBeat) <= 1.0;
                    }), newEvents.end());
                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyTempoEventsUndoAction(trackModel, t->getId(), oldEvents, newEvents, "Delete Tempo Point"),
                        "Delete Tempo Point");
                }
                else if (result == 3)
                {
                    for (const auto& ev : t->getTempoEvents())
                    {
                        if (std::abs(beatPos - ev.startBeat) <= 1.0)
                        {
                            showEditTempoDialog(t->getId(), ev.id);
                            break;
                        }
                    }
                }
                else if (result == 4)
                {
                    auto oldEvents = t->getTempoEvents();
                    auto newEvents = oldEvents;
                    for (auto& ev : newEvents)
                    {
                        if (std::abs(beatPos - ev.startBeat) <= 1.0)
                        {
                            ev.type = (ev.type == TempoTransitionType::Jump) ? TempoTransitionType::Ramp : TempoTransitionType::Jump;
                            break;
                        }
                    }
                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyTempoEventsUndoAction(trackModel, t->getId(), oldEvents, newEvents, "Toggle Tempo Transition"),
                        "Toggle Tempo Transition");
                }
            }
            else if (t->getType() == TrackType::Instrument)
            {
                if (result == 1)
                {
                    int bpb = midiModel.getTimeSigNumerator();
                    auto before = t->getClips();
                    auto after = before;

                    MidiClip newClip;
                    newClip.id = juce::Time::currentTimeMillis();
                    newClip.name = "Part " + juce::String(before.size() + 1);
                    newClip.startBeat = snapBeat;
                    newClip.lengthBeats = (double)(bpb * 4);
                    newClip.color = t->getColor();
                    after.push_back(newClip);

                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyClipsUndoAction(trackModel, t->getId(), before, after, "Create MIDI Part"),
                        "Create MIDI Part");
                    trackModel.setActiveClip(t->getId(), newClip.id, &midiModel);
                }
                else if (result == 2)
                {
                    auto before = t->getClips();
                    auto after = before;
                    bool deletedActive = false;
                    int64_t currentActId = trackModel.getActiveClipId();
                    for (const auto& c : before)
                    {
                        if (beatPos >= c.startBeat && beatPos <= (c.startBeat + c.lengthBeats))
                        {
                            if (c.id == currentActId)
                                deletedActive = true;
                        }
                    }
                    after.erase(std::remove_if(after.begin(), after.end(), [beatPos](const MidiClip& c) {
                        return beatPos >= c.startBeat && beatPos <= (c.startBeat + c.lengthBeats);
                    }), after.end());

                    if (before.size() != after.size())
                    {
                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyClipsUndoAction(trackModel, t->getId(), before, after, "Delete MIDI Part"),
                            "Delete MIDI Part");
                        if (t->getId() == trackModel.getActiveTrackId())
                        {
                            if (deletedActive || after.empty())
                            {
                                if (!after.empty())
                                    trackModel.setActiveClip(t->getId(), after.front().id, &midiModel);
                                else
                                {
                                    trackModel.setActiveClipId(-1);
                                    midiModel.clearAllNotes(false);
                                    midiModel.deselectAll();
                                }
                            }
                        }
                    }
                }
                else if (result == 10)
                {
                    mergeSelectedClips();
                }
                else if (result == 101)
                {
                    auto before = t->getClips();
                    auto after = before;
                    bool changed = false;
                    for (auto& c : after)
                    {
                        if (c.isSelected || (beatPos >= c.startBeat && beatPos <= (c.startBeat + c.lengthBeats)))
                        {
                            c.hasCustomColor = false;
                            c.color = t->getColor();
                            changed = true;
                        }
                    }
                    if (changed)
                    {
                        UndoActionHelper::performAtomic(&undoManager,
                            new ModifyClipsUndoAction(trackModel, t->getId(), before, after, "Reset Clip Color"),
                            "Reset Clip Color");
                        repaint();
                    }
                }
            }
        });
}

void TrackArrangerComponent::copySelectedEvents()
{
    arrangerClipboard = ArrangerClipboard();
    double minBeat = 1e9;
    bool anyFound = false;

    for (const auto& t : trackModel.getTracks())
    {
        if (t->getType() == TrackType::Audio)
        {
            for (const auto& clip : t->getAudioClips())
                if (clip.isSelected)
                {
                    arrangerClipboard.audioClips.push_back(clip);
                    minBeat = std::min(minBeat, clip.startBeat);
                    arrangerClipboard.sourceTrackId = t->getId();
                    arrangerClipboard.sourceTrackType = TrackType::Audio;
                    anyFound = true;
                }
        }
        else if (t->getType() == TrackType::Instrument)
        {
            for (const auto& c : t->getClips())
            {
                if (c.isSelected)
                {
                    arrangerClipboard.clips.push_back(c);
                    minBeat = std::min(minBeat, c.startBeat);
                    arrangerClipboard.sourceTrackId = t->getId();
                    arrangerClipboard.sourceTrackType = TrackType::Instrument;
                    anyFound = true;
                }
            }
        }
        else if (t->getType() == TrackType::Chord)
        {
            for (const auto& c : t->getChords())
            {
                if (c.isSelected)
                {
                    arrangerClipboard.chords.push_back(c);
                    minBeat = std::min(minBeat, c.startBeat);
                    arrangerClipboard.sourceTrackId = t->getId();
                    arrangerClipboard.sourceTrackType = TrackType::Chord;
                    anyFound = true;
                }
            }
        }
        else if (t->getType() == TrackType::Mode)
        {
            for (const auto& m : t->getModes())
            {
                if (m.isSelected)
                {
                    arrangerClipboard.modes.push_back(m);
                    minBeat = std::min(minBeat, m.startBeat);
                    arrangerClipboard.sourceTrackId = t->getId();
                    arrangerClipboard.sourceTrackType = TrackType::Mode;
                    anyFound = true;
                }
            }
        }
        else if (t->getType() == TrackType::Marker)
        {
            for (const auto& m : t->getMarkers())
            {
                if (m.isSelected)
                {
                    arrangerClipboard.markers.push_back(m);
                    minBeat = std::min(minBeat, m.startBeat);
                    arrangerClipboard.sourceTrackId = t->getId();
                    arrangerClipboard.sourceTrackType = TrackType::Marker;
                    anyFound = true;
                }
            }
        }
        else if (t->getType() == TrackType::TimeSignature)
        {
            for (const auto& ts : t->getTimeSignatures())
            {
                if (ts.isSelected)
                {
                    arrangerClipboard.timeSignatures.push_back(ts);
                    minBeat = std::min(minBeat, ts.startBeat);
                    arrangerClipboard.sourceTrackId = t->getId();
                    arrangerClipboard.sourceTrackType = TrackType::TimeSignature;
                    anyFound = true;
                }
            }
        }
        else if (t->getType() == TrackType::Tempo)
        {
            for (const auto& te : t->getTempoEvents())
            {
                if (te.isSelected)
                {
                    arrangerClipboard.tempoEvents.push_back(te);
                    minBeat = std::min(minBeat, te.startBeat);
                    arrangerClipboard.sourceTrackId = t->getId();
                    arrangerClipboard.sourceTrackType = TrackType::Tempo;
                    anyFound = true;
                }
            }
        }
    }

    arrangerClipboard.minBeat = anyFound ? minBeat : 0.0;
}

void TrackArrangerComponent::cutSelectedEvents()
{
    copySelectedEvents();
    deleteSelectedEvents();
}

void TrackArrangerComponent::pasteEvents(double targetBeat)
{
    if (targetBeat < 0.0)
        targetBeat = currentPlayhead;

    double snapInt = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.0;
    if (snapInt > 0.0)
        targetBeat = std::round(targetBeat / snapInt) * snapInt;

    targetBeat = std::max(0.0, targetBeat);
    double offset = targetBeat - arrangerClipboard.minBeat;

    int targetTrackId = trackModel.getActiveTrackId();
    auto* targetTrack = trackModel.getTrackById(targetTrackId);

    auto compound = std::make_unique<CompoundUndoAction>("Paste Events");
    int64_t lastPastedClipId = -1;
    int lastPastedTrackId = -1;

    if (!arrangerClipboard.audioClips.empty())
    {
        Track* destination = targetTrack != nullptr && targetTrack->getType() == TrackType::Audio
            ? targetTrack : trackModel.getTrackById(arrangerClipboard.sourceTrackId);
        if (destination != nullptr && destination->getType() == TrackType::Audio && !destination->isLocked())
        {
            auto before = destination->getAudioClips();
            auto after = before;
            int64_t nextId = juce::Time::currentTimeMillis();
            for (const auto& existing : after) nextId = std::max(nextId, existing.id + 1);
            for (auto& clip : after) clip.isSelected = false;
            for (const auto& clip : arrangerClipboard.audioClips)
            {
                AudioClip pasted = clip;
                pasted.id = nextId++;
                pasted.startBeat = std::max(0.0, clip.startBeat + offset);
                pasted.isSelected = true;
                after.push_back(std::move(pasted));
            }
            compound->addAction(std::make_unique<ModifyAudioClipsUndoAction>(trackModel,
                destination->getId(), before, after));
        }
    }

    // 1. Clips
    if (!arrangerClipboard.clips.empty())
    {
        Track* destTrack = (targetTrack && targetTrack->getType() == TrackType::Instrument)
                           ? targetTrack
                           : trackModel.getTrackById(arrangerClipboard.sourceTrackId);
        if (!destTrack || destTrack->getType() != TrackType::Instrument)
            destTrack = trackModel.getActiveInstrumentTrack();

        if (destTrack)
        {
            auto before = destTrack->getClips();
            auto after = before;
            for (auto& c : after) c.isSelected = false;

            for (const auto& c : arrangerClipboard.clips)
            {
                double newStart = std::max(0.0, c.startBeat + offset);
                if (trackModel.canPlaceEvent(destTrack->getId(), newStart, c.lengthBeats))
                {
                    MidiClip newClip = c;
                    newClip.id = juce::Time::currentTimeMillis() + (int64_t)after.size() + 1;
                    newClip.startBeat = newStart;
                    newClip.isSelected = true;
                    double beatShift = newStart - c.startBeat;
                    for (auto& n : newClip.notes)
                        n.startBeat += beatShift;
                    after.push_back(newClip);
                    lastPastedClipId = newClip.id;
                    lastPastedTrackId = destTrack->getId();
                }
            }

            if (before.size() != after.size())
            {
                compound->addAction(std::make_unique<ModifyClipsUndoAction>(trackModel, destTrack->getId(), before, after, "Paste MIDI Parts"));
            }
        }
    }

    // 2. Chords
    if (!arrangerClipboard.chords.empty())
    {
        auto* chordTrk = trackModel.getChordTrack();
        if (chordTrk)
        {
            auto before = chordTrk->getChords();
            auto after = before;
            for (auto& c : after) c.isSelected = false;

            for (const auto& c : arrangerClipboard.chords)
            {
                double newStart = std::max(0.0, c.startBeat + offset);
                if (trackModel.canPlaceEvent(chordTrk->getId(), newStart, c.lengthBeats))
                {
                    ChordEvent newC = c;
                    newC.id = juce::Time::currentTimeMillis() + (int64_t)after.size() + 1;
                    newC.startBeat = newStart;
                    newC.isSelected = true;
                    after.push_back(newC);
                }
            }

            if (before.size() != after.size())
            {
                compound->addAction(std::make_unique<ModifyChordsUndoAction>(trackModel, chordTrk->getId(), before, after, "Paste Chords"));
            }
        }
    }

    // 3. Modes
    if (!arrangerClipboard.modes.empty())
    {
        auto* modeTrk = trackModel.getModeTrack();
        if (modeTrk)
        {
            auto before = modeTrk->getModes();
            auto after = before;
            for (auto& m : after) m.isSelected = false;

            for (const auto& m : arrangerClipboard.modes)
            {
                double newStart = std::max(0.0, m.startBeat + offset);
                if (trackModel.canPlaceEvent(modeTrk->getId(), newStart, m.lengthBeats))
                {
                    ModeEvent newM = m;
                    newM.id = juce::Time::currentTimeMillis() + (int64_t)after.size() + 1;
                    newM.startBeat = newStart;
                    newM.isSelected = true;
                    after.push_back(newM);
                }
            }

            if (before.size() != after.size())
            {
                compound->addAction(std::make_unique<ModifyModesUndoAction>(trackModel, modeTrk->getId(), before, after, "Paste Modes"));
            }
        }
    }

    // 4. Markers
    if (!arrangerClipboard.markers.empty())
    {
        auto* markerTrk = trackModel.getMarkerTrack();
        if (markerTrk)
        {
            auto before = markerTrk->getMarkers();
            auto after = before;
            for (auto& m : after) m.isSelected = false;

            for (const auto& m : arrangerClipboard.markers)
            {
                double newStart = std::max(0.0, m.startBeat + offset);
                MarkerEvent newM = m;
                newM.id = juce::Uuid().toString();
                newM.startBeat = newStart;
                newM.isSelected = true;
                after.push_back(newM);
            }

            if (before.size() != after.size())
            {
                compound->addAction(std::make_unique<ModifyMarkersUndoAction>(trackModel, markerTrk->getId(), before, after, "Paste Markers"));
            }
        }
    }

    // 5. Time Signatures
    if (!arrangerClipboard.timeSignatures.empty())
    {
        auto* tsTrk = trackModel.getTimeSignatureTrack();
        if (tsTrk)
        {
            auto before = tsTrk->getTimeSignatures();
            auto after = before;
            for (auto& ts : after) ts.isSelected = false;

            for (const auto& ts : arrangerClipboard.timeSignatures)
            {
                double newStart = std::max(0.0, ts.startBeat + offset);
                if (trackModel.canPlaceEvent(tsTrk->getId(), newStart, ts.lengthBeats))
                {
                    TimeSignatureEvent newTs = ts;
                    newTs.id = juce::Time::currentTimeMillis() + (int64_t)after.size() + 1;
                    newTs.startBeat = newStart;
                    newTs.isSelected = true;
                    after.push_back(newTs);
                }
            }

            if (before.size() != after.size())
            {
                compound->addAction(std::make_unique<ModifyTimeSignaturesUndoAction>(trackModel, tsTrk->getId(), before, after, "Paste Time Signatures"));
            }
        }
    }

    // 6. Tempo Events
    if (!arrangerClipboard.tempoEvents.empty())
    {
        auto* tempoTrk = trackModel.getTempoTrack();
        if (tempoTrk)
        {
            auto before = tempoTrk->getTempoEvents();
            auto after = before;
            for (auto& te : after) te.isSelected = false;

            for (const auto& te : arrangerClipboard.tempoEvents)
            {
                double newStart = std::max(0.0, te.startBeat + offset);
                if (trackModel.canPlaceEvent(tempoTrk->getId(), newStart, 0.0))
                {
                    TempoEvent newTe = te;
                    newTe.id = juce::Time::currentTimeMillis() + (int64_t)after.size() + 1;
                    newTe.startBeat = newStart;
                    newTe.isSelected = true;
                    after.push_back(newTe);
                }
            }

            if (before.size() != after.size())
            {
                compound->addAction(std::make_unique<ModifyTempoEventsUndoAction>(trackModel, tempoTrk->getId(), before, after, "Paste Tempo Events"));
            }
        }
    }

    if (!compound->isEmpty())
    {
        UndoActionHelper::performAtomic(&undoManager, compound.release(), "Paste Events");
        if (lastPastedClipId > 0 && lastPastedTrackId > 0)
            trackModel.setActiveClip(lastPastedTrackId, lastPastedClipId, &midiModel);
        trackModel.triggerTracksChanged();
    }
}

void TrackArrangerComponent::deleteSelectedEvents()
{
    auto compound = std::make_unique<CompoundUndoAction>("Delete Events");
    bool deletedActiveClip = false;

    for (auto& t : trackModel.getTracks())
    {
        if (t->getType() == TrackType::Audio && !t->isLocked())
        {
            auto before = t->getAudioClips();
            auto after = before;
            after.erase(std::remove_if(after.begin(), after.end(), [](const AudioClip& c) {
                return c.isSelected;
            }), after.end());
            if (after.size() != before.size())
                compound->addAction(std::make_unique<ModifyAudioClipsUndoAction>(trackModel, t->getId(), before, after));
        }
        else if (t->getType() == TrackType::Instrument)
        {
            auto before = t->getClips();
            auto after = before;
            int64_t currentActiveClipId = trackModel.getActiveClipId();
            for (const auto& c : before)
            {
                if (c.isSelected && c.id == currentActiveClipId)
                    deletedActiveClip = true;
            }

            after.erase(std::remove_if(after.begin(), after.end(), [](const MidiClip& c) { return c.isSelected; }), after.end());
            if (before.size() != after.size())
            {
                compound->addAction(std::make_unique<ModifyClipsUndoAction>(trackModel, t->getId(), before, after, "Delete MIDI Parts"));
            }
        }
        else if (t->getType() == TrackType::Chord)
        {
            auto before = t->getChords();
            auto after = before;
            after.erase(std::remove_if(after.begin(), after.end(), [](const ChordEvent& c) { return c.isSelected; }), after.end());
            if (before.size() != after.size())
            {
                compound->addAction(std::make_unique<ModifyChordsUndoAction>(trackModel, t->getId(), before, after, "Delete Chords"));
            }
        }
        else if (t->getType() == TrackType::Mode)
        {
            auto before = t->getModes();
            auto after = before;
            after.erase(std::remove_if(after.begin(), after.end(), [](const ModeEvent& m) { return m.isSelected; }), after.end());
            if (before.size() != after.size())
            {
                compound->addAction(std::make_unique<ModifyModesUndoAction>(trackModel, t->getId(), before, after, "Delete Modes"));
            }
        }
        else if (t->getType() == TrackType::Marker)
        {
            auto before = t->getMarkers();
            auto after = before;
            after.erase(std::remove_if(after.begin(), after.end(), [](const MarkerEvent& m) { return m.isSelected; }), after.end());
            if (before.size() != after.size())
            {
                compound->addAction(std::make_unique<ModifyMarkersUndoAction>(trackModel, t->getId(), before, after, "Delete Markers"));
            }
        }
        else if (t->getType() == TrackType::TimeSignature)
        {
            auto before = t->getTimeSignatures();
            auto after = before;
            after.erase(std::remove_if(after.begin(), after.end(), [](const TimeSignatureEvent& ts) { return ts.isSelected; }), after.end());
            if (before.size() != after.size())
            {
                compound->addAction(std::make_unique<ModifyTimeSignaturesUndoAction>(trackModel, t->getId(), before, after, "Delete Time Signatures"));
            }
        }
        else if (t->getType() == TrackType::Tempo)
        {
            auto before = t->getTempoEvents();
            auto after = before;
            after.erase(std::remove_if(after.begin(), after.end(), [](const TempoEvent& te) { return te.isSelected; }), after.end());
            if (before.size() != after.size())
            {
                compound->addAction(std::make_unique<ModifyTempoEventsUndoAction>(trackModel, t->getId(), before, after, "Delete Tempo Events"));
            }
        }
    }

    if (!compound->isEmpty())
    {
        UndoActionHelper::performAtomic(&undoManager, compound.release(), "Delete Events");
        if (deletedActiveClip)
        {
            if (auto* actTrk = trackModel.getActiveInstrumentTrack())
            {
                if (!actTrk->getClips().empty())
                    trackModel.setActiveClip(actTrk->getId(), actTrk->getClips().front().id, &midiModel);
                else
                {
                    trackModel.setActiveClipId(-1);
                    midiModel.clearAllNotes(false);
                    midiModel.deselectAll();
                }
            }
        }
        trackModel.triggerTracksChanged();
    }
}

void TrackArrangerComponent::duplicateSelectedEvents()
{
    auto compound = std::make_unique<CompoundUndoAction>("Duplicate Events");

    for (auto& t : trackModel.getTracks())
    {
        if (t->getType() == TrackType::Audio && !t->isLocked())
        {
            auto before = t->getAudioClips();
            auto after = before;
            int64_t nextId = juce::Time::currentTimeMillis();
            for (const auto& existing : after) nextId = std::max(nextId, existing.id + 1);
            for (const auto& clip : before)
                if (clip.isSelected)
                {
                    AudioClip duplicate = clip;
                    duplicate.id = nextId++;
                    duplicate.startBeat += clip.lengthBeats;
                    for (auto& old : after) old.isSelected = false;
                    duplicate.isSelected = true;
                    after.push_back(std::move(duplicate));
                }
            if (after.size() != before.size())
                compound->addAction(std::make_unique<ModifyAudioClipsUndoAction>(trackModel, t->getId(), before, after));
        }
        else if (t->getType() == TrackType::Instrument)
        {
            auto before = t->getClips();
            auto after = before;
            std::vector<MidiClip> newClips;
            for (const auto& c : before)
            {
                if (c.isSelected)
                {
                    double targetStart = c.startBeat + c.lengthBeats;
                    if (trackModel.canPlaceEvent(t->getId(), targetStart, c.lengthBeats))
                    {
                        MidiClip cl = c;
                        cl.id = juce::Time::currentTimeMillis() + (int64_t)newClips.size() + 1;
                        cl.startBeat = targetStart;
                        for (auto& n : cl.notes)
                            n.startBeat += c.lengthBeats;
                        newClips.push_back(cl);
                    }
                }
            }
            if (!newClips.empty())
            {
                for (auto& c : after) c.isSelected = false;
                for (auto& nc : newClips) nc.isSelected = true;
                after.insert(after.end(), newClips.begin(), newClips.end());
                compound->addAction(std::make_unique<ModifyClipsUndoAction>(trackModel, t->getId(), before, after, "Duplicate MIDI Parts"));
            }
        }
        else if (t->getType() == TrackType::Chord)
        {
            auto before = t->getChords();
            auto after = before;
            std::vector<ChordEvent> newChords;
            for (const auto& c : before)
            {
                if (c.isSelected)
                {
                    double targetStart = c.startBeat + c.lengthBeats;
                    if (trackModel.canPlaceEvent(t->getId(), targetStart, c.lengthBeats))
                    {
                        ChordEvent ch = c;
                        ch.id = juce::Time::currentTimeMillis() + (int64_t)newChords.size() + 1;
                        ch.startBeat = targetStart;
                        newChords.push_back(ch);
                    }
                }
            }
            if (!newChords.empty())
            {
                for (auto& c : after) c.isSelected = false;
                for (auto& nc : newChords) nc.isSelected = true;
                after.insert(after.end(), newChords.begin(), newChords.end());
                compound->addAction(std::make_unique<ModifyChordsUndoAction>(trackModel, t->getId(), before, after, "Duplicate Chords"));
            }
        }
        else if (t->getType() == TrackType::Mode)
        {
            auto before = t->getModes();
            auto after = before;
            std::vector<ModeEvent> newModes;
            for (const auto& m : before)
            {
                if (m.isSelected)
                {
                    double targetStart = m.startBeat + m.lengthBeats;
                    if (trackModel.canPlaceEvent(t->getId(), targetStart, m.lengthBeats))
                    {
                        ModeEvent md = m;
                        md.id = juce::Time::currentTimeMillis() + (int64_t)newModes.size() + 1;
                        md.startBeat = targetStart;
                        newModes.push_back(md);
                    }
                }
            }
            if (!newModes.empty())
            {
                for (auto& m : after) m.isSelected = false;
                for (auto& nm : newModes) nm.isSelected = true;
                after.insert(after.end(), newModes.begin(), newModes.end());
                compound->addAction(std::make_unique<ModifyModesUndoAction>(trackModel, t->getId(), before, after, "Duplicate Modes"));
            }
        }
        else if (t->getType() == TrackType::Marker)
        {
            auto before = t->getMarkers();
            auto after = before;
            std::vector<MarkerEvent> newMarkers;
            for (const auto& m : before)
            {
                if (m.isSelected)
                {
                    MarkerEvent mk = m;
                    mk.id = juce::String(juce::Time::currentTimeMillis() + (int64_t)newMarkers.size() + 1);
                    mk.startBeat = m.startBeat + m.lengthBeats;
                    newMarkers.push_back(mk);
                }
            }
            if (!newMarkers.empty())
            {
                for (auto& m : after) m.isSelected = false;
                for (auto& nm : newMarkers) nm.isSelected = true;
                after.insert(after.end(), newMarkers.begin(), newMarkers.end());
                compound->addAction(std::make_unique<ModifyMarkersUndoAction>(trackModel, t->getId(), before, after, "Duplicate Markers"));
            }
        }
        else if (t->getType() == TrackType::TimeSignature)
        {
            auto before = t->getTimeSignatures();
            auto after = before;
            std::vector<TimeSignatureEvent> newSigs;
            for (const auto& ts : before)
            {
                if (ts.isSelected)
                {
                    double targetStart = ts.startBeat + ts.lengthBeats;
                    if (trackModel.canPlaceEvent(t->getId(), targetStart, ts.lengthBeats))
                    {
                        TimeSignatureEvent nts = ts;
                        nts.id = juce::Time::currentTimeMillis() + (int64_t)newSigs.size() + 1;
                        nts.startBeat = targetStart;
                        newSigs.push_back(nts);
                    }
                }
            }
            if (!newSigs.empty())
            {
                for (auto& s : after) s.isSelected = false;
                for (auto& ns : newSigs) ns.isSelected = true;
                after.insert(after.end(), newSigs.begin(), newSigs.end());
                compound->addAction(std::make_unique<ModifyTimeSignaturesUndoAction>(trackModel, t->getId(), before, after, "Duplicate Time Signatures"));
            }
        }
        else if (t->getType() == TrackType::Tempo)
        {
            auto before = t->getTempoEvents();
            auto after = before;
            std::vector<TempoEvent> newTempos;
            for (const auto& te : before)
            {
                if (te.isSelected)
                {
                    double targetStart = te.startBeat + 4.0;
                    if (trackModel.canPlaceEvent(t->getId(), targetStart, 0.0))
                    {
                        TempoEvent nte = te;
                        nte.id = juce::Time::currentTimeMillis() + (int64_t)newTempos.size() + 1;
                        nte.startBeat = targetStart;
                        newTempos.push_back(nte);
                    }
                }
            }
            if (!newTempos.empty())
            {
                for (auto& s : after) s.isSelected = false;
                for (auto& ns : newTempos) ns.isSelected = true;
                after.insert(after.end(), newTempos.begin(), newTempos.end());
                compound->addAction(std::make_unique<ModifyTempoEventsUndoAction>(trackModel, t->getId(), before, after, "Duplicate Tempo Events"));
            }
        }
    }

    if (!compound->isEmpty())
    {
        UndoActionHelper::performAtomic(&undoManager, compound.release(), "Duplicate Events");
        trackModel.triggerTracksChanged();
    }
}

void TrackArrangerComponent::mergeSelectedClips()
{
    bool anyMerged = false;
    for (auto& t : trackModel.getTracks())
    {
        if (t->getType() == TrackType::Instrument)
        {
            std::vector<int64_t> selIds;
            for (const auto& c : t->getClips())
            {
                if (c.isSelected)
                    selIds.push_back(c.id);
            }
            if (selIds.size() >= 2)
            {
                if (trackModel.mergeClips(t->getId(), selIds, &undoManager))
                    anyMerged = true;
            }
        }
    }
    if (anyMerged)
    {
        trackModel.triggerTracksChanged();
        repaint();
    }
}

void TrackArrangerComponent::setLocatorsToSelectedEvents()
{
    double minStart = 1e9;
    double maxEnd = -1.0;
    bool hasSelection = false;

    for (const auto& trk : trackModel.getTracks())
    {
        for (const auto& clip : trk->getAudioClips())
        {
            if (clip.isSelected)
            {
                minStart = std::min(minStart, clip.startBeat);
                maxEnd = std::max(maxEnd, clip.startBeat + clip.lengthBeats);
                hasSelection = true;
            }
        }
        for (const auto& clip : trk->getClips())
        {
            if (clip.isSelected)
            {
                minStart = std::min(minStart, clip.startBeat);
                maxEnd = std::max(maxEnd, clip.startBeat + clip.lengthBeats);
                hasSelection = true;
            }
        }
        for (const auto& c : trk->getChords())
        {
            if (c.isSelected)
            {
                minStart = std::min(minStart, c.startBeat);
                maxEnd = std::max(maxEnd, c.startBeat + c.lengthBeats);
                hasSelection = true;
            }
        }
        for (const auto& m : trk->getModes())
        {
            if (m.isSelected)
            {
                minStart = std::min(minStart, m.startBeat);
                maxEnd = std::max(maxEnd, m.startBeat + m.lengthBeats);
                hasSelection = true;
            }
        }
        for (const auto& mk : trk->getMarkers())
        {
            if (mk.isSelected)
            {
                minStart = std::min(minStart, mk.startBeat);
                maxEnd = std::max(maxEnd, mk.startBeat + mk.lengthBeats);
                hasSelection = true;
            }
        }
        for (const auto& ts : trk->getTimeSignatures())
        {
            if (ts.isSelected)
            {
                minStart = std::min(minStart, ts.startBeat);
                maxEnd = std::max(maxEnd, ts.startBeat + ts.lengthBeats);
                hasSelection = true;
            }
        }
    }

    if (hasSelection && maxEnd >= minStart)
    {
        midiModel.setLoopRange(minStart, maxEnd);
        midiModel.triggerProjectSettingsChanged();
        repaint();
    }
}

void TrackArrangerComponent::showColorPickerForCurrentSelection()
{
    bool hasSelectedEvents = false;
    for (const auto& trk : trackModel.getTracks())
    {
        for (const auto& c : trk->getClips())           if (c.isSelected)  { hasSelectedEvents = true; break; }
        if (hasSelectedEvents) break;
        for (const auto& c : trk->getChords())          if (c.isSelected)  { hasSelectedEvents = true; break; }
        if (hasSelectedEvents) break;
        for (const auto& m : trk->getModes())           if (m.isSelected)  { hasSelectedEvents = true; break; }
        if (hasSelectedEvents) break;
        for (const auto& m : trk->getMarkers())         if (m.isSelected)  { hasSelectedEvents = true; break; }
        if (hasSelectedEvents) break;
        for (const auto& ts : trk->getTimeSignatures()) if (ts.isSelected) { hasSelectedEvents = true; break; }
        if (hasSelectedEvents) break;
        for (const auto& te : trk->getTempoEvents())    if (te.isSelected) { hasSelectedEvents = true; break; }
        if (hasSelectedEvents) break;
    }

    static const uint32_t paletteArgbs[16] = {
        0xff2575fc, 0xff06b6d4, 0xff14b8a6, 0xff10b981,
        0xff84cc16, 0xffeab308, 0xfff59e0b, 0xfff97316,
        0xffef4444, 0xfff43f5e, 0xffec4899, 0xffd946ef,
        0xffa855f7, 0xff8b5cf6, 0xff6366f1, 0xff64748b
    };
    std::vector<juce::Colour> paletteColors;
    for (int i = 0; i < 16; ++i) paletteColors.push_back(juce::Colour(paletteArgbs[i]));

    auto mousePos = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
    juce::Component::SafePointer<TrackArrangerComponent> safeThis(this);

    if (hasSelectedEvents)
    {
        juce::PopupMenu menu;
        auto colorComp = std::make_unique<TrackColorPaletteComponent>(
            paletteColors, CubaseTheme::accentBlue(),
            [this, safeThis](juce::Colour chosenCol) {
                if (safeThis.getComponent() == nullptr) return;
                for (auto& trk : trackModel.getTracks())
                {
                    for (auto& c : trk->getClips())
                    {
                        if (c.isSelected)
                        {
                            c.color = chosenCol;
                            c.hasCustomColor = true;
                        }
                    }
                    for (auto& c : trk->getChords())          if (c.isSelected) c.color = chosenCol;
                    for (auto& m : trk->getModes())           if (m.isSelected) m.color = chosenCol;
                    for (auto& m : trk->getMarkers())         if (m.isSelected) m.color = chosenCol;
                    for (auto& ts : trk->getTimeSignatures()) if (ts.isSelected) ts.color = chosenCol;
                    for (auto& te : trk->getTempoEvents())    if (te.isSelected) te.color = chosenCol;
                }
                trackModel.triggerTracksChanged();
                repaint();
            });
        menu.addCustomItem(100, std::move(colorComp));
        menu.addSeparator();
        menu.addItem(101, juce::String::fromUTF8(u8"跟随所属轨道颜色 (Reset to Track Color)"));

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(mousePos.x, mousePos.y, 1, 1)),
            [this, safeThis](int result) {
                if (safeThis.getComponent() == nullptr) return;
                if (result == 101)
                {
                    for (auto& trk : trackModel.getTracks())
                    {
                        for (auto& c : trk->getClips())
                        {
                            if (c.isSelected)
                            {
                                c.hasCustomColor = false;
                                c.color = trk->getColor();
                            }
                        }
                    }
                    trackModel.triggerTracksChanged();
                    repaint();
                }
            });
    }
    else
    {
        std::vector<int> targetTrackIds;
        for (const auto& t : trackModel.getTracks())
        {
            if (t->isSelected())
                targetTrackIds.push_back(t->getId());
        }
        if (targetTrackIds.empty())
        {
            int actId = trackModel.getActiveTrackId();
            if (auto* trk = trackModel.getTrackById(actId))
                targetTrackIds.push_back(trk->getId());
            else if (!trackModel.getTracks().empty())
                targetTrackIds.push_back(trackModel.getTracks()[0]->getId());
        }

        if (!targetTrackIds.empty())
        {
            auto* firstTrk = trackModel.getTrackById(targetTrackIds[0]);
            juce::Colour currentTrackCol = firstTrk ? firstTrk->getColor() : CubaseTheme::accentBlue();
            juce::PopupMenu menu;
            auto colorComp = std::make_unique<TrackColorPaletteComponent>(
                paletteColors, currentTrackCol,
                [this, safeThis, targetTrackIds](juce::Colour newCol) {
                    if (safeThis.getComponent() == nullptr) return;
                    for (int tid : targetTrackIds)
                    {
                        if (auto* t = trackModel.getTrackById(tid))
                        {
                            t->setColor(newCol);
                        }
                    }
                    trackModel.triggerTracksChanged();
                    midiModel.triggerProjectSettingsChanged();
                    repaint();
                });
            menu.addCustomItem(100, std::move(colorComp));
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(mousePos.x, mousePos.y, 1, 1)));
        }
    }
}

bool TrackArrangerComponent::keyPressed(const juce::KeyPress& key)
{
    if (KeyCommandManager::getInstance().matches(Cmd_DeleteTrack, key))
    {
        const int trackId = trackModel.getActiveTrackId();
        if (trackModel.getTrackById(trackId) != nullptr)
            UndoActionHelper::performAtomic(&undoManager,
                new DeleteTrackUndoAction(trackModel, trackId), "Delete Track");
        return true;
    }
    // 1. Forward Undo / Redo
    if (key.getModifiers().isCommandDown())
    {
        int code = key.getKeyCode();
        if (code == 'z' || code == 'Z')
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
        else if (code == 'c' || code == 'C')
        {
            copySelectedEvents();
            return true;
        }
        else if (code == 'x' || code == 'X')
        {
            cutSelectedEvents();
            return true;
        }
        else if (code == 'v' || code == 'V')
        {
            pasteEvents(currentPlayhead);
            return true;
        }
        else if (code == 'd' || code == 'D')
        {
            duplicateSelectedEvents();
            return true;
        }
        else if (code == 'g' || code == 'G')
        {
            mergeSelectedClips();
            return true;
        }
        else if (code == 'a' || code == 'A')
        {
            trackModel.selectAllArrangerEvents();
            return true;
        }
    }

    // 2. Forward Delete key / custom delete shortcut to batch delete selected events
    bool isDeleteCmd = KeyCommandManager::getInstance().matches(Cmd_Delete, key)
                       || key.isKeyCode(juce::KeyPress::deleteKey)
                       || key.isKeyCode(juce::KeyPress::backspaceKey);
    if (isDeleteCmd)
    {
        if (trackModel.getSelectedArrangerEventCount() > 0)
        {
            deleteSelectedEvents();
            return true;
        }
    }

    // 3. 'P' key: Set Locators to Selection (快速将事件范围定位为所选事件块范围)
    if (!key.getModifiers().isCommandDown() && (key.getKeyCode() == 'p' || key.getKeyCode() == 'P'))
    {
        setLocatorsToSelectedEvents();
        return true;
    }

    // 4. Alt+C: Quick color popup for selected clips or active track
    if (key.getModifiers().isAltDown() && (key.getKeyCode() == 'c' || key.getKeyCode() == 'C'))
    {
        showColorPickerForCurrentSelection();
        return true;
    }

    // 5. Space key for Play/Pause
    if (key.isKeyCode(juce::KeyPress::spaceKey))
    {
        if (audioEngine.isPlaying())
            audioEngine.pause();
        else
            audioEngine.play();
        return true;
    }

    return false;
}

TrackArrangerComponent::~TrackArrangerComponent()
{
    stopTimer();
    for (auto& [path, thumbnail] : waveforms)
        thumbnail->removeChangeListener(this);
    waveforms.clear();
    midiModel.removeListener(this);
    horizontalScrollBar.removeListener(this);
    audioEngine.removePlayheadListener(this);
    trackModel.removeListener(this);
}

void TrackArrangerComponent::timerCallback()
{
    if (dragMode != ArrangerDragMode::ScrubPlayhead)
    {
        stopTimer();
        return;
    }

    const int edgeMargin = 36;
    int leftThreshold = headerWidth + edgeMargin;
    int rightThreshold = getWidth() - edgeMargin;
    int deltaPx = 0;

    if (lastRulerMousePos.x < leftThreshold)
    {
        float over = (float)(leftThreshold - lastRulerMousePos.x);
        deltaPx = -(int)std::clamp(over * 0.25f + 4.0f, 3.0f, 30.0f);
    }
    else if (lastRulerMousePos.x > rightThreshold)
    {
        float over = (float)(lastRulerMousePos.x - rightThreshold);
        deltaPx = (int)std::clamp(over * 0.25f + 4.0f, 3.0f, 30.0f);
    }
    else
    {
        stopTimer();
        return;
    }

    if (deltaPx != 0 && pixelsPerBeat > 0.0f)
    {
        double deltaBeats = (double)deltaPx / (double)pixelsPerBeat;
        scrollBeats = std::max(0.0, scrollBeats + deltaBeats);
        updateScrollBar();

        double b = xToBeat((float)lastRulerMousePos.x);
        double qb = midiModel.isSnapEnabled() ? midiModel.quantizeBeat(b) : b;
        audioEngine.setPlayheadBeat(std::max(0.0, qb));
        repaint();
    }
}

void TrackArrangerComponent::scrollBarMoved(juce::ScrollBar*, double newRangeStart)
{
    scrollBeats = std::max(0.0, newRangeStart);
    repaint();
}

void TrackArrangerComponent::updateScrollBar()
{
    double maxB = 64.0;
    for (const auto& t : trackModel.getTracks())
    {
        for (const auto& c : t->getClips()) maxB = std::max(maxB, c.startBeat + c.lengthBeats);
        for (const auto& c : t->getAudioClips()) maxB = std::max(maxB, c.startBeat + c.lengthBeats);
        for (const auto& c : t->getChords()) maxB = std::max(maxB, c.startBeat + c.lengthBeats);
        for (const auto& m : t->getModes()) maxB = std::max(maxB, m.startBeat + m.lengthBeats);
        for (const auto& m : t->getMarkers()) maxB = std::max(maxB, m.startBeat + m.lengthBeats);
        for (const auto& ts : t->getTimeSignatures()) maxB = std::max(maxB, ts.startBeat + 16.0);
    }
    maxB = std::max(maxB, audioEngine.getPlayheadBeat() + 16.0);
    double visibleBeats = std::max(1.0, (double)(getWidth() - headerWidth) / (double)pixelsPerBeat);
    horizontalScrollBar.setRangeLimits(0.0, maxB + visibleBeats, juce::dontSendNotification);
    horizontalScrollBar.setCurrentRange(scrollBeats, visibleBeats, juce::dontSendNotification);
}

void TrackArrangerComponent::playheadMoved(double beatPosition)
{
    currentPlayhead = beatPosition;
    repaint();
}

void TrackArrangerComponent::setPixelsPerBeat(float ppb)
{
    pixelsPerBeat = std::clamp(ppb, 2.0f, 500.0f);
    updateScrollBar();
    repaint();
}

float TrackArrangerComponent::beatToX(double beat) const
{
    return (float)headerWidth + (float)((beat - scrollBeats) * pixelsPerBeat);
}

double TrackArrangerComponent::xToBeat(float x) const
{
    float relX = x - (float)headerWidth;
    return (relX / pixelsPerBeat) + scrollBeats;
}

void TrackArrangerComponent::resized()
{
    int scrollbarH = 16;
    btnAddTrack.setBounds(8, getHeight() - 32, headerWidth - 16, 24);
    horizontalScrollBar.setBounds(headerWidth, getHeight() - scrollbarH, getWidth() - headerWidth, scrollbarH);
    updateScrollBar();
}

//==============================================================================
// TrackArrangerComponent Paint
//==============================================================================
void TrackArrangerComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    int w = bounds.getWidth();
    int h = bounds.getHeight();

    // 1. Background
    g.setColour(CubaseTheme::bgDeep());
    g.fillRect(bounds);

    // 2. Ruler Header (Top)
    auto rulerRect = juce::Rectangle<int>(headerWidth, 0, w - headerWidth, rulerHeight);
    g.setColour(CubaseTheme::bgPanel());
    g.fillRect(rulerRect);
    g.setColour(CubaseTheme::borderSubtle());
    g.drawHorizontalLine(rulerHeight - 1, 0.0f, (float)w);

    // Bar markers in ruler (using trackModel.getBarTimeline)
    double endBeat = xToBeat((float)w);
    auto barTimeline = trackModel.getBarTimelineRange(scrollBeats, endBeat + 16.0);

    g.setColour(CubaseTheme::textSecondary());
    g.setFont(juce::FontOptions(10.5f).withStyle("bold"));

    // Compute adaptive bar skip so labels don't overlap when zoomed out
    // Find minimum label spacing: aim for at least 40px between bar numbers
    int barSkip = 1;
    if (!barTimeline.empty())
    {
        // Use first bar's length as representative (irregular meters: conservative estimate)
        double refBarPx = (double)pixelsPerBeat * barTimeline[0].lengthBeats;
        if (refBarPx < 40.0 && refBarPx > 0.0)
        {
            barSkip = (int)std::ceil(40.0 / refBarPx);
            // Round up to power of 2 for clean visual groupings (1,2,4,8,16,...)
            int p = 1;
            while (p < barSkip) p *= 2;
            barSkip = p;
        }
    }

    // Track last rendered bar's time-signature to detect meter change boundaries
    int lastNumerator = -1, lastDenominator = -1;

    for (const auto& bar : barTimeline)
    {
        // Always render at meter-change boundaries and at barSkip intervals
        bool isMeterChange = (bar.numerator != lastNumerator || bar.denominator != lastDenominator);
        bool isSkipBoundary = ((bar.barIndex - 1) % barSkip == 0);
        lastNumerator   = bar.numerator;
        lastDenominator = bar.denominator;

        if (!isSkipBoundary && !isMeterChange) continue;

        float barX = beatToX(bar.startBeat);
        if (barX >= (float)headerWidth && barX <= (float)w)
        {
            g.setColour(isMeterChange ? CubaseTheme::gridBarLine().brighter(0.4f) : CubaseTheme::gridBarLine().brighter(0.2f));
            g.drawVerticalLine((int)barX, 0.0f, (float)rulerHeight);
            int offset = trackModel.getBarNumberOffset();
            g.drawText(juce::String(bar.barIndex + offset), (int)barX + 4, 2, 48, rulerHeight - 4, juce::Justification::centredLeft);
        }

        // Sub-beats only when zoomed in enough that they won't crowd
        if (barSkip <= 1 && (double)pixelsPerBeat * 1.0 >= 12.0)
        {
            if (midiModel.isSnapTriplet())
            {
                double tStep = midiModel.getSnapBeatInterval();
                if ((double)pixelsPerBeat * tStep >= 4.0)
                {
                    for (double tb = bar.startBeat + tStep; tb < bar.startBeat + bar.lengthBeats - 0.001; tb += tStep)
                    {
                        float sbX = beatToX(tb);
                        if (sbX >= (float)headerWidth && sbX <= (float)w)
                        {
                            bool isIntBeat = (std::abs(tb - std::round(tb)) < 0.001);
                            g.setColour(CubaseTheme::borderSubtle().withAlpha(isIntBeat ? 0.7f : 0.4f));
                            g.drawVerticalLine((int)sbX, (float)(rulerHeight / (isIntBeat ? 2 : 3)), (float)rulerHeight);
                        }
                    }
                }
            }
            else
            {
                for (int sb = 1; sb < bar.numerator; ++sb)
                {
                    float sbX = beatToX(bar.startBeat + (double)sb);
                    if (sbX >= (float)headerWidth && sbX <= (float)w)
                    {
                        g.setColour(CubaseTheme::borderSubtle().withAlpha(0.6f));
                        g.drawVerticalLine((int)sbX, (float)(rulerHeight / 2), (float)rulerHeight);
                    }
                }
            }
        }
    }

    // Header left-top corner
    g.setColour(CubaseTheme::bgPanel().darker(0.1f));
    g.fillRect(0, 0, headerWidth, rulerHeight);
    g.setColour(CubaseTheme::borderBright());
    g.drawVerticalLine(headerWidth - 1, 0.0f, (float)h);
    g.setColour(CubaseTheme::textPrimary());
    g.setFont(juce::FontOptions(11.0f).withStyle("bold"));
    g.drawText("TRACKS / ARRANGER", 12, 0, headerWidth - 24, rulerHeight, juce::Justification::centredLeft);

    // 3. Render Tracks
    int y = rulerHeight - scrollTrackY;
    auto tracks = trackModel.getVisibleTracks();

    for (int idx = 0; idx < (int)tracks.size(); ++idx)
    {
        const auto* trk = tracks[idx];
        auto headerArea = juce::Rectangle<int>(0, y, headerWidth, trackHeight);
        auto laneArea   = juce::Rectangle<int>(headerWidth, y, w - headerWidth, trackHeight);

        bool isSelected = trk->isSelected();
        bool isActive = (trk->getId() == trackModel.getActiveTrackId());

        // Header Background
        if (isSelected)
            g.setColour(CubaseTheme::bgSurface().brighter(0.18f));
        else if (isActive)
            g.setColour(CubaseTheme::bgSurface().brighter(0.08f));
        else
            g.setColour(CubaseTheme::bgSurface());
        g.fillRect(headerArea);

        if (isSelected)
        {
            g.setColour(CubaseTheme::accentBlue().withAlpha(0.75f));
            g.drawRect(headerArea, 1);
        }

        // Visual indent if linked auxiliary track or nested inside folder
        int depth = trackModel.getTrackDepth(trk->getId());
        bool isAux = (trk->getParentTrackId() > 0);
        int indentPixels = depth * 16 + (isAux ? 16 : 0);
        int textLeft = 12 + indentPixels;

        if (depth > 0 || isAux)
        {
            // Indent connector line
            g.setColour(isAux ? CubaseTheme::accentCyan().withAlpha(0.6f) : CubaseTheme::textSecondary().withAlpha(0.35f));
            int lineX = std::max(6, textLeft - 10);
            g.drawVerticalLine(lineX, (float)y, (float)(y + trackHeight / 2));
            g.drawHorizontalLine(y + trackHeight / 2, (float)lineX, (float)textLeft);
        }

        // Track Color Tab (Left 4px)
        g.setColour(trk->getColor());
        g.fillRect(std::min(indentPixels, headerWidth - 8), y, 4, trackHeight);

        // Track Name
        g.setColour(CubaseTheme::textPrimary());
        g.setFont(juce::FontOptions(12.0f).withStyle(isActive ? "bold" : "plain"));
        g.drawText(trk->getName(), textLeft, y + 4, headerWidth - textLeft - 60, 18, juce::Justification::centredLeft);

        // Track Type Sub-Label
        juce::String typeStr = "INST";
        if (trk->getType() == TrackType::Chord) typeStr = "CHORD";
        else if (trk->getType() == TrackType::Mode) typeStr = "MODE";
        else if (trk->getType() == TrackType::Marker) typeStr = "MARKER";
        else if (trk->getType() == TrackType::TimeSignature) typeStr = "TIME SIG";
        else if (trk->getType() == TrackType::Tempo) typeStr = "TEMPO";
        else if (trk->getType() == TrackType::Folder) typeStr = "FOLDER";
        else if (trk->getType() == TrackType::Audio) typeStr = "AUDIO";

        g.setColour(trk->getColor().withAlpha(0.75f));
        g.setFont(juce::FontOptions(9.5f).withStyle("bold"));
        g.drawText(typeStr, textLeft, y + 22, 60, 16, juce::Justification::centredLeft);

        // Track-specific buttons in header (Right side of header)
        auto btn1 = juce::Rectangle<int>(headerWidth - 56, y + 12, 22, 20);
        auto btn2 = juce::Rectangle<int>(headerWidth - 30, y + 12, 22, 20);

        if (trk->getType() == TrackType::Folder)
        {
            // Collapse / Expand toggle button [+] / [-]
            g.setColour(CubaseTheme::bgDeep());
            g.fillRoundedRectangle(btn2.toFloat(), 2.0f);
            g.setColour(CubaseTheme::accentOrange());
            g.setFont(juce::FontOptions(11.0f).withStyle("bold"));
            g.drawText(trk->isFolderCollapsed() ? "+" : "-", btn2, juce::Justification::centred);

            int childCount = (int)trackModel.getChildTrackIds(trk->getId(), false).size();
            g.setColour(CubaseTheme::textSecondary());
            g.setFont(juce::FontOptions(9.5f));
            g.drawText(juce::String(childCount) + " trks", headerWidth - 85, y + 14, 52, 16, juce::Justification::centredRight);
        }
        else if (trk->getType() == TrackType::Instrument || trk->getType() == TrackType::Audio)
        {
            // Mute / Solo
            g.setColour(trk->isMuted() ? juce::Colour(0xffe11d48) : CubaseTheme::bgDeep());
            g.fillRoundedRectangle(btn1.toFloat(), 2.0f);
            g.setColour(trk->isMuted() ? juce::Colours::white : CubaseTheme::textSecondary());
            g.setFont(juce::FontOptions(10.0f).withStyle("bold"));
            g.drawText("M", btn1, juce::Justification::centred);

            g.setColour(trk->isSolo() ? juce::Colour(0xfff59e0b) : CubaseTheme::bgDeep());
            g.fillRoundedRectangle(btn2.toFloat(), 2.0f);
            g.setColour(trk->isSolo() ? juce::Colours::black : CubaseTheme::textSecondary());
            g.drawText("S", btn2, juce::Justification::centred);
        }
        else if (trk->getType() == TrackType::Chord)
        {
            // Mute (M) & Harmonize (H)
            g.setColour(trk->isMuted() ? juce::Colour(0xffe11d48) : CubaseTheme::bgDeep());
            g.fillRoundedRectangle(btn1.toFloat(), 2.0f);
            g.setColour(trk->isMuted() ? juce::Colours::white : CubaseTheme::textSecondary());
            g.setFont(juce::FontOptions(10.0f).withStyle("bold"));
            g.drawText("M", btn1, juce::Justification::centred);

            g.setColour(trk->isHarmonizeEnabled() ? CubaseTheme::accentCyan() : CubaseTheme::bgDeep());
            g.fillRoundedRectangle(btn2.toFloat(), 2.0f);
            g.setColour(trk->isHarmonizeEnabled() ? juce::Colours::black : CubaseTheme::textSecondary());
            g.drawText("H", btn2, juce::Justification::centred);
        }
        else if (trk->getType() == TrackType::Mode)
        {
            // Mode Track: Lock (L)
            g.setColour(trk->isLocked() ? juce::Colour(0xffe11d48) : CubaseTheme::bgDeep());
            g.fillRoundedRectangle(btn1.toFloat(), 2.0f);
            g.setColour(trk->isLocked() ? juce::Colours::white : CubaseTheme::textSecondary());
            g.setFont(juce::FontOptions(10.0f).withStyle("bold"));
            g.drawText("L", btn1, juce::Justification::centred);
        }
        else if (trk->getType() == TrackType::Marker)
        {
            // Pure Marker Track: only Goto Next Marker (>)
            g.setColour(CubaseTheme::bgDeep());
            g.fillRoundedRectangle(btn2.toFloat(), 2.0f);
            g.setColour(CubaseTheme::accentBlue());
            g.setFont(juce::FontOptions(11.0f).withStyle("bold"));
            g.drawText(">", btn2, juce::Justification::centred);
        }
        else if (trk->getType() == TrackType::TimeSignature)
        {
            // Lock (L) & Click (C)
            g.setColour(trk->isLocked() ? juce::Colour(0xffe11d48) : CubaseTheme::bgDeep());
            g.fillRoundedRectangle(btn1.toFloat(), 2.0f);
            g.setColour(trk->isLocked() ? juce::Colours::white : CubaseTheme::textSecondary());
            g.setFont(juce::FontOptions(10.0f).withStyle("bold"));
            g.drawText("L", btn1, juce::Justification::centred);

            g.setColour(audioEngine.isMetronomeEnabled() ? CubaseTheme::accentOrange() : CubaseTheme::bgDeep());
            g.fillRoundedRectangle(btn2.toFloat(), 2.0f);
            g.setColour(audioEngine.isMetronomeEnabled() ? juce::Colours::black : CubaseTheme::textSecondary());
            g.drawText("C", btn2, juce::Justification::centred);
        }

        // Lane Background & Content (Clipped to prevent painting over track headers and ruler/scrollbar)
        {
            juce::Graphics::ScopedSaveState laneSaveState(g);
            g.reduceClipRegion(headerWidth, rulerHeight, w - headerWidth, h - rulerHeight - 14);

            g.setColour(isActive ? CubaseTheme::bgDeep().brighter(0.04f) : CubaseTheme::bgDeep());
            g.fillRect(laneArea);

            // Grid bar lines and beat lines in lane (using same barSkip as ruler above)
            int laneLastNum = -1, laneLastDen = -1;
            for (const auto& bar : barTimeline)
            {
                bool isLaneMeterChange = (bar.numerator != laneLastNum || bar.denominator != laneLastDen);
                bool isLaneSkipBound   = ((bar.barIndex - 1) % barSkip == 0);
                laneLastNum = bar.numerator;
                laneLastDen = bar.denominator;

                float barX = beatToX(bar.startBeat);
                if (barX >= (float)headerWidth && barX <= (float)w)
                {
                    if (isLaneSkipBound || isLaneMeterChange)
                    {
                        g.setColour(isLaneMeterChange
                            ? CubaseTheme::gridBarLine().withAlpha(0.80f)
                            : CubaseTheme::gridBarLine().withAlpha(0.60f));
                        g.drawVerticalLine((int)barX, (float)y, (float)(y + trackHeight));
                    }
                }

                // Sub-lines inside this bar based on active Snap interval
                if (barSkip <= 1 && (double)pixelsPerBeat >= 10.0)
                {
                    double snapStep = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 1.0;
                    if ((double)pixelsPerBeat * snapStep >= 3.0)
                    {
                        if (midiModel.isSnapTriplet())
                        {
                            double tStep = midiModel.getSnapBeatInterval();
                            for (double tb = bar.startBeat + tStep; tb < bar.startBeat + bar.lengthBeats - 0.001; tb += tStep)
                            {
                                float sbX = beatToX(tb);
                                if (sbX >= (float)headerWidth && sbX <= (float)w)
                                {
                                    bool isIntBeat = (std::abs(tb - std::round(tb)) < 0.001);
                                    g.setColour(CubaseTheme::gridBeatLine().withAlpha(isIntBeat ? 0.65f : 0.40f));
                                    g.drawVerticalLine((int)sbX, (float)y, (float)(y + trackHeight));
                                }
                            }
                        }
                        else
                        {
                            // Non-triplet mode:
                            // If snapStep >= bar.lengthBeats (1/1 Bar), 0 inner lines!
                            // If snapStep == 2.0 (1/2 Note), 1 half-bar line!
                            // If snapStep == 1.0 (1/4 Note), 3 beat lines!
                            // If snapStep < 1.0, all sub-beat lines!
                            if (snapStep < bar.lengthBeats - 0.001)
                            {
                                for (double b = bar.startBeat + snapStep; b < bar.startBeat + bar.lengthBeats - 0.001; b += snapStep)
                                {
                                    float sbX = beatToX(b);
                                    if (sbX >= (float)headerWidth && sbX <= (float)w)
                                    {
                                        bool isIntBeat = (std::abs(b - std::round(b)) < 0.001);
                                        bool isHalfBar = (std::abs(b - (bar.startBeat + bar.lengthBeats * 0.5)) < 0.001);
                                        if (isIntBeat || isHalfBar)
                                            g.setColour(CubaseTheme::gridBeatLine().withAlpha(0.65f));
                                        else
                                            g.setColour(CubaseTheme::gridSubBeat().withAlpha(0.45f));

                                        g.drawVerticalLine((int)sbX, (float)y, (float)(y + trackHeight));
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Render Clips / Items according to track type
            if (trk->getType() == TrackType::Instrument)
            {
                for (const auto& clip : trk->getClips())
                {
                    float x1 = beatToX(clip.startBeat);
                    float x2 = beatToX(clip.startBeat + clip.lengthBeats);
                    float clipW = std::max(6.0f, x2 - x1);

                    if (x1 + clipW < (float)headerWidth || x1 > (float)w)
                        continue;

                    auto clipRect = juce::Rectangle<float>(x1 + 1.0f, (float)y + 3.0f, clipW - 2.0f, (float)trackHeight - 6.0f);
                    bool isClipActive = (isActive && clip.id == trackModel.getActiveClipId());
                    bool isSelected = clip.isSelected;

                    // Clip Body
                    juce::Colour fillCol = isSelected ? clip.color.brighter(0.28f) : (isClipActive ? clip.color.brighter(0.15f) : clip.color.withAlpha(0.65f));
                    if (dragMode == ArrangerDragMode::MoveClip && isSelected && dragTargetTrackId >= 0 && dragTargetTrackId != draggedTrackId)
                        fillCol = fillCol.withAlpha(0.35f);
                    g.setColour(fillCol);
                    g.fillRoundedRectangle(clipRect, 3.0f);

                    // Outline & Selection
                    if (isSelected)
                    {
                        g.setColour(juce::Colours::white);
                        g.drawRoundedRectangle(clipRect, 3.0f, 2.0f);
                    }
                    else
                    {
                        g.setColour(clip.color.brighter(0.3f).withAlpha(0.6f));
                        g.drawRoundedRectangle(clipRect, 3.0f, 1.0f);
                    }

                    // Resize handle indicators on left & right edges
                    g.setColour(juce::Colours::white.withAlpha(0.35f));
                    g.fillRect(clipRect.getX(), clipRect.getY() + 4.0f, 2.0f, clipRect.getHeight() - 8.0f);
                    g.fillRect(clipRect.getRight() - 2.0f, clipRect.getY() + 4.0f, 2.0f, clipRect.getHeight() - 8.0f);

                    // Mini notes preview inside clip: Render from independent clip.notes relative to clip.startBeat
                    {
                        juce::Graphics::ScopedSaveState noteClipState(g);
                        g.reduceClipRegion(clipRect.toNearestInt());
                        g.setColour(juce::Colours::white.withAlpha(0.7f));
                        double snapStart = clip.startBeat;
                        if (dragMode == ArrangerDragMode::MoveClip && draggedItemId == clip.id)
                        {
                            snapStart = itemInitialStartBeat;
                        }
                        else
                        {
                            for (const auto& s : selectedDragSnapshots)
                            {
                                if (s.trackId == trk->getId() && s.intId == clip.id)
                                {
                                    snapStart = s.initialStartBeat;
                                    break;
                                }
                            }
                        }
                        for (const auto& n : clip.notes)
                        {
                            double relBeat = n.startBeat - snapStart;
                            if (relBeat + n.lengthBeats >= 0.0 && relBeat <= clip.lengthBeats)
                            {
                                float nx1 = x1 + (float)(relBeat / clip.lengthBeats) * clipW;
                                float nw  = std::max(2.0f, (float)(n.lengthBeats / clip.lengthBeats) * clipW);
                                if (nx1 + nw < (float)headerWidth || nx1 > (float)w)
                                    continue;
                                float ny  = (float)y + 6.0f + (float)(127 - n.pitch) / 127.0f * (trackHeight - 16.0f);
                                g.fillRect(nx1, ny, nw, 2.0f);
                            }
                        }
                    }

                    // Clip Name Label
                    g.setColour(juce::Colours::white);
                    g.setFont(CubaseTheme::getFont(10.0f, true));
                    g.drawText(clip.name, clipRect.reduced(4.0f, 2.0f).toNearestInt(), juce::Justification::topLeft);
                }

                // Cross-track drag ghost preview on target Instrument track
                if (dragMode == ArrangerDragMode::MoveClip && dragTargetTrackId == trk->getId() && dragTargetTrackId != draggedTrackId)
                {
                    if (auto* srcTrk = trackModel.getTrackById(draggedTrackId))
                    {
                        for (const auto& s : selectedDragSnapshots)
                        {
                            if (s.trackType == TrackType::Instrument && s.trackId == draggedTrackId)
                            {
                                for (const auto& c : srcTrk->getClips())
                                {
                                    if (c.id == s.intId)
                                    {
                                        float gx1 = beatToX(c.startBeat);
                                        float gx2 = beatToX(c.startBeat + c.lengthBeats);
                                        float gw = std::max(6.0f, gx2 - gx1);
                                        auto gRect = juce::Rectangle<float>(gx1 + 1.0f, (float)y + 3.0f, gw - 2.0f, (float)trackHeight - 6.0f);
                                        g.setColour(trk->getColor().withAlpha(0.65f));
                                        g.fillRoundedRectangle(gRect, 3.0f);
                                        g.setColour(juce::Colours::white);
                                        g.drawRoundedRectangle(gRect, 3.0f, 1.5f);
                                        g.setFont(CubaseTheme::getFont(10.0f, true));
                                        g.drawText(c.name + juce::String::fromUTF8(u8" ➜ ") + trk->getName(), gRect.reduced(4.0f, 2.0f).toNearestInt(), juce::Justification::topLeft);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            else if (trk->getType() == TrackType::Audio)
            {
                for (const auto& clip : trk->getAudioClips())
                {
                    const float x1 = beatToX(clip.startBeat);
                    const float x2 = beatToX(clip.startBeat + clip.lengthBeats);
                    if (x2 < (float)headerWidth || x1 > (float)w) continue;
                    auto rect = juce::Rectangle<float>(x1 + 1.0f, (float)y + 3.0f,
                        std::max(4.0f, x2 - x1 - 2.0f), (float)trackHeight - 6.0f);
                    g.setColour(trk->getColor().withAlpha(clip.isMuted ? 0.16f : 0.42f));
                    g.fillRoundedRectangle(rect, 3.0f);
                    g.setColour(clip.isSelected ? juce::Colours::white : trk->getColor().brighter(0.35f));
                    g.drawRoundedRectangle(rect, 3.0f, clip.isSelected ? 2.0f : 1.0f);
                    juce::Graphics::ScopedSaveState clipState(g);
                    g.reduceClipRegion(rect.toNearestInt());
                    auto found = waveforms.find(clip.sourcePath.toStdString());
                    if (found != waveforms.end() && found->second->getTotalLength() > 0.0)
                    {
                        g.setColour(trk->getColor().contrasting(0.9f).withAlpha(clip.isMuted ? 0.32f : 0.78f));
                        found->second->drawChannels(g, rect.reduced(3.0f, 7.0f).toNearestInt(),
                            clip.sourceOffsetSeconds,
                            clip.sourceOffsetSeconds + clip.durationSeconds, 0.9f);
                    }
                    else if (!juce::File(clip.sourcePath).existsAsFile())
                    {
                        g.setColour(juce::Colours::orange);
                        g.drawText(juce::String::fromUTF8(u8"文件丢失 / Missing file"),
                            rect.reduced(6.0f).toNearestInt(), juce::Justification::centred);
                    }
                    g.setColour(juce::Colours::white);
                    g.setFont(CubaseTheme::getFont(10.0f, true));
                    g.drawText(clip.name, rect.reduced(6.0f, 2.0f).toNearestInt(),
                               juce::Justification::topLeft);
                }
            }
            else if (trk->getType() == TrackType::Chord)
            {
                for (const auto& c : trk->getChords())
                {
                    float x1 = beatToX(c.startBeat);
                    float x2 = beatToX(c.startBeat + c.lengthBeats);
                    float itemW = std::max(4.0f, x2 - x1);

                    auto chordRect = juce::Rectangle<float>(x1 + 1.0f, (float)y + 4.0f, itemW - 2.0f, (float)trackHeight - 8.0f);
                    g.setColour(c.isSelected ? c.color.withAlpha(0.6f) : c.color.withAlpha(0.35f));
                    g.fillRoundedRectangle(chordRect, 3.0f);

                    if (c.isSelected)
                    {
                        g.setColour(juce::Colours::white);
                        g.drawRoundedRectangle(chordRect, 3.0f, 2.0f);
                    }
                    else
                    {
                        g.setColour(c.color);
                        g.drawRoundedRectangle(chordRect, 3.0f, 1.0f);
                    }

                    // Resize handles
                    g.setColour(juce::Colours::white.withAlpha(0.4f));
                    g.fillRect(chordRect.getX(), chordRect.getY() + 4.0f, 2.0f, chordRect.getHeight() - 8.0f);
                    g.fillRect(chordRect.getRight() - 2.0f, chordRect.getY() + 4.0f, 2.0f, chordRect.getHeight() - 8.0f);

                    g.setColour(juce::Colours::white);
                    g.setFont(CubaseTheme::getFont(12.0f, true));

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
                    g.drawText(text, chordRect.toNearestInt(), juce::Justification::centred);
                }
            }
            else if (trk->getType() == TrackType::Mode)
            {
                for (const auto& md : trk->getModes())
                {
                    float x1 = beatToX(md.startBeat);
                    float x2 = beatToX(md.startBeat + md.lengthBeats);
                    float itemW = std::max(4.0f, x2 - x1);

                    auto modeRect = juce::Rectangle<float>(x1 + 1.0f, (float)y + 4.0f, itemW - 2.0f, (float)trackHeight - 8.0f);
                    g.setColour(md.isSelected ? md.color.withAlpha(0.6f) : md.color.withAlpha(0.35f));
                    g.fillRoundedRectangle(modeRect, 3.0f);

                    if (md.isSelected)
                    {
                        g.setColour(juce::Colours::white);
                        g.drawRoundedRectangle(modeRect, 3.0f, 2.0f);
                    }
                    else
                    {
                        g.setColour(md.color);
                        g.drawRoundedRectangle(modeRect, 3.0f, 1.0f);
                    }

                    // Resize handles
                    g.setColour(juce::Colours::white.withAlpha(0.4f));
                    g.fillRect(modeRect.getX(), modeRect.getY() + 4.0f, 2.0f, modeRect.getHeight() - 8.0f);
                    g.fillRect(modeRect.getRight() - 2.0f, modeRect.getY() + 4.0f, 2.0f, modeRect.getHeight() - 8.0f);

                    juce::String label = md.name + " (" + md.tonic + " " + md.scaleName.upToFirstOccurrenceOf(" ", false, false) + ")";
                    g.setColour(juce::Colours::white);
                    g.setFont(CubaseTheme::getFont(11.0f, true));
                    g.drawText(label, modeRect.toNearestInt(), juce::Justification::centred);
                }
            }
            else if (trk->getType() == TrackType::Marker)
            {
                // Compute stratified overlapping layers for markers
                auto& markersList = const_cast<std::vector<MarkerEvent>&>(trk->getMarkers());
                TrackModel::computeMarkerStackLayers(markersList);

                // Compute maxLayer for each marker among all markers it intersects with
                std::vector<int> totalLayersForMarker(markersList.size(), 1);
                for (size_t i = 0; i < markersList.size(); ++i)
                {
                    int maxL = markersList[i].stackLayer;
                    for (size_t j = 0; j < markersList.size(); ++j)
                    {
                        if (i == j) continue;
                        double a1 = markersList[i].startBeat;
                        double a2 = a1 + markersList[i].lengthBeats;
                        double b1 = markersList[j].startBeat;
                        double b2 = b1 + markersList[j].lengthBeats;
                        if (std::max(a1, b1) < std::min(a2, b2) - 0.001)
                        {
                            maxL = std::max(maxL, markersList[j].stackLayer);
                        }
                    }
                    totalLayersForMarker[i] = maxL + 1;
                }

                // If any two markers overlap, ensure they share the max layer count of the connected cluster
                bool changed = true;
                while (changed)
                {
                    changed = false;
                    for (size_t i = 0; i < markersList.size(); ++i)
                    {
                        for (size_t j = 0; j < markersList.size(); ++j)
                        {
                            if (i == j) continue;
                            double a1 = markersList[i].startBeat;
                            double a2 = a1 + markersList[i].lengthBeats;
                            double b1 = markersList[j].startBeat;
                            double b2 = b1 + markersList[j].lengthBeats;
                            if (std::max(a1, b1) < std::min(a2, b2) - 0.001)
                            {
                                int commonMax = std::max(totalLayersForMarker[i], totalLayersForMarker[j]);
                                if (totalLayersForMarker[i] != commonMax) { totalLayersForMarker[i] = commonMax; changed = true; }
                                if (totalLayersForMarker[j] != commonMax) { totalLayersForMarker[j] = commonMax; changed = true; }
                            }
                        }
                    }
                }

                // Sort indices so lower stack layers draw first and higher stack layers draw on top
                std::vector<size_t> indices(markersList.size());
                for (size_t i = 0; i < indices.size(); ++i) indices[i] = i;
                std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                    return markersList[a].stackLayer < markersList[b].stackLayer;
                });

                for (size_t idxVal : indices)
                {
                    const auto& m = markersList[idxVal];
                    float x1 = beatToX(m.startBeat);
                    float x2 = beatToX(m.startBeat + m.lengthBeats);
                    float itemW = std::max(4.0f, x2 - x1);

                    // Equal height division for overlapping layers
                    int totalL = std::max(1, totalLayersForMarker[idxVal]);
                    float availH = (float)(trackHeight - 8);
                    float gap = 2.0f;
                    float slotH = std::max(14.0f, (availH - (float)(totalL - 1) * gap) / (float)totalL);
                    float layerY = (float)y + 4.0f + (float)m.stackLayer * (slotH + gap);

                    auto markerRect = juce::Rectangle<float>(x1 + 1.0f, layerY, itemW - 2.0f, slotH - 1.0f);
                    g.setColour(m.isSelected ? m.color.withAlpha(0.85f) : m.color.withAlpha(0.60f));
                    g.fillRoundedRectangle(markerRect, 3.0f);

                    if (m.isSelected)
                    {
                        g.setColour(juce::Colours::white);
                        g.drawRoundedRectangle(markerRect, 3.0f, 2.0f);
                    }
                    else
                    {
                        g.setColour(m.color.brighter(0.3f));
                        g.drawRoundedRectangle(markerRect, 3.0f, 1.0f);
                    }

                    // Marker Flag icon & Chinese text
                    g.setColour(juce::Colours::white);
                    g.setFont(CubaseTheme::getFont(std::min(10.0f, slotH - 3.0f), true));
                    g.drawText(juce::String::fromUTF8(u8"⚑ ") + m.name, markerRect.toNearestInt(), juce::Justification::centredLeft);
                }
            }
            else if (trk->getType() == TrackType::TimeSignature)
            {
                auto sigs = trk->getTimeSignatures();
                std::sort(sigs.begin(), sigs.end(), [](const TimeSignatureEvent& a, const TimeSignatureEvent& b) {
                    return a.startBeat < b.startBeat;
                });

                if (sigs.empty())
                {
                    g.setColour(CubaseTheme::textSecondary().withAlpha(0.35f));
                    g.setFont(CubaseTheme::getFont(11.0f));
                    juce::String defInfo = "Project Time Signature: " + juce::String(trackModel.getDefaultTimeSigNumerator()) + "/" + juce::String(trackModel.getDefaultTimeSigDenominator());
                    g.drawText(defInfo, laneArea.reduced(8, 2), juce::Justification::centredLeft);
                }
                else
                {
                    for (const auto& ts : sigs)
                    {
                        float x1 = beatToX(ts.startBeat);
                        float spanW = 42.0f; // Minimal compact badge just wide enough to clearly read time signature

                        auto sigRect = juce::Rectangle<float>(x1 + 1.0f, (float)y + 6.0f, spanW, (float)trackHeight - 12.0f);
                        g.setColour(ts.isSelected ? ts.color : ts.color.darker(0.25f));
                        g.fillRoundedRectangle(sigRect, 4.0f);

                        if (ts.isSelected)
                        {
                            g.setColour(juce::Colours::white);
                            g.drawRoundedRectangle(sigRect, 4.0f, 2.0f);
                        }
                        else
                        {
                            g.setColour(ts.color.brighter(0.3f));
                            g.drawRoundedRectangle(sigRect, 4.0f, 1.0f);
                        }

                        // Draw time signature text
                        g.setColour(juce::Colours::white);
                        g.setFont(CubaseTheme::getFont(12.0f, true));
                        juce::String text = juce::String(ts.numerator) + "/" + juce::String(ts.denominator);
                        g.drawText(text, sigRect.toNearestInt(), juce::Justification::centred);
                    }
                }
            }
            else if (trk->getType() == TrackType::Tempo)
            {
                auto events = trk->getTempoEvents();
                std::sort(events.begin(), events.end(), [](const TempoEvent& a, const TempoEvent& b) {
                    return a.startBeat < b.startBeat;
                });

                float topY = (float)y + 4.0f;
                float bottomY = (float)y + (float)trackHeight - 6.0f;
                float laneH = std::max(10.0f, bottomY - topY);

                // Subtle BPM horizontal reference lines (60, 120, 180)
                g.setColour(CubaseTheme::borderSubtle().withAlpha(0.22f));
                for (double refBpm : { 60.0, 120.0, 180.0 })
                {
                    float norm = (float)((refBpm - 40.0) / (240.0 - 40.0));
                    float refY = bottomY - juce::jlimit(0.0f, 1.0f, norm) * laneH;
                    g.drawHorizontalLine((int)refY, (float)headerWidth, (float)w);
                }

                if (events.empty())
                {
                    g.setColour(CubaseTheme::textSecondary().withAlpha(0.35f));
                    g.setFont(CubaseTheme::getFont(11.0f));
                    juce::String defInfo = "Project Tempo: " + juce::String(midiModel.getBpm(), 1) + " BPM (Default Jump)";
                    g.drawText(defInfo, laneArea.reduced(8, 2), juce::Justification::centredLeft);
                }
                else
                {
                    auto bpmToY = [topY, bottomY, laneH](double bpm) -> float {
                        float norm = (float)((bpm - 40.0) / (240.0 - 40.0));
                        return bottomY - juce::jlimit(0.0f, 1.0f, norm) * laneH;
                    };

                    juce::Path envelopePath;
                    juce::Path fillPath;

                    float startX = (float)headerWidth;
                    float firstNodeX = beatToX(events.front().startBeat);
                    float firstNodeY = bpmToY(events.front().bpm);

                    envelopePath.startNewSubPath(std::min(startX, firstNodeX), firstNodeY);
                    fillPath.startNewSubPath(std::min(startX, firstNodeX), bottomY);
                    fillPath.lineTo(std::min(startX, firstNodeX), firstNodeY);

                    if (firstNodeX > startX)
                    {
                        envelopePath.lineTo(firstNodeX, firstNodeY);
                        fillPath.lineTo(firstNodeX, firstNodeY);
                    }

                    for (size_t i = 0; i < events.size(); ++i)
                    {
                        const auto& ev = events[i];
                        float nx = beatToX(ev.startBeat);
                        float ny = bpmToY(ev.bpm);

                        if (i + 1 < events.size())
                        {
                            const auto& nextEv = events[i + 1];
                            float nextX = beatToX(nextEv.startBeat);
                            float nextY = bpmToY(nextEv.bpm);

                            if (ev.type == TempoTransitionType::Jump)
                            {
                                envelopePath.lineTo(nextX, ny);
                                envelopePath.lineTo(nextX, nextY);
                                fillPath.lineTo(nextX, ny);
                                fillPath.lineTo(nextX, nextY);
                            }
                            else
                            {
                                envelopePath.lineTo(nextX, nextY);
                                fillPath.lineTo(nextX, nextY);
                            }
                        }
                        else
                        {
                            float rightEdge = (float)w;
                            envelopePath.lineTo(rightEdge, ny);
                            fillPath.lineTo(rightEdge, ny);
                            fillPath.lineTo(rightEdge, bottomY);
                            fillPath.closeSubPath();
                        }
                    }

                    g.setColour(trk->getColor().withAlpha(0.18f));
                    g.fillPath(fillPath);

                    g.setColour(trk->getColor().brighter(0.2f));
                    g.strokePath(envelopePath, juce::PathStrokeType(2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));

                    // Draw node handles and BPM text badges
                    for (const auto& ev : events)
                    {
                        float nx = beatToX(ev.startBeat);
                        float ny = bpmToY(ev.bpm);

                        if (nx >= (float)headerWidth - 20.0f && nx <= (float)w + 20.0f)
                        {
                            auto nodeRect = juce::Rectangle<float>(nx - 4.5f, ny - 4.5f, 9.0f, 9.0f);
                            g.setColour(ev.isSelected ? juce::Colours::white : trk->getColor());
                            g.fillEllipse(nodeRect);
                            g.setColour(ev.isSelected ? trk->getColor() : juce::Colours::white);
                            g.drawEllipse(nodeRect, 1.5f);

                            juce::String bpmText = juce::String(ev.bpm, 1) + (ev.type == TempoTransitionType::Ramp ? " ~" : "");
                            g.setFont(CubaseTheme::getFont(10.5f, true));
                            int textW = (int)juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), bpmText) + 8;
                            float badgeX = nx + 4.0f;
                            float badgeY = (ny > (float)y + 20.0f) ? (ny - 16.0f) : (ny + 4.0f);
                            auto badgeRect = juce::Rectangle<float>(badgeX, badgeY, (float)textW, 14.0f);

                            g.setColour(juce::Colour(0xdd1a1d24));
                            g.fillRoundedRectangle(badgeRect, 3.0f);
                            if (ev.isSelected)
                            {
                                g.setColour(juce::Colours::white);
                                g.drawRoundedRectangle(badgeRect, 3.0f, 1.0f);
                            }
                            g.setColour(ev.isSelected ? juce::Colours::white : trk->getColor().brighter(0.4f));
                            g.drawText(bpmText, badgeRect.toNearestInt(), juce::Justification::centred);
                        }
                    }
                }
            }
            else if (trk->getType() == TrackType::Folder)
            {
                if (trk->isFolderCollapsed())
                {
                    // Render overview blocks summarizing all child clips
                    auto childIds = trackModel.getChildTrackIds(trk->getId(), true);
                    for (int cId : childIds)
                    {
                        if (auto* childTrk = trackModel.getTrackById(cId))
                        {
                            for (const auto& clip : childTrk->getClips())
                            {
                                float cx1 = beatToX(clip.startBeat);
                                float cx2 = beatToX(clip.startBeat + clip.lengthBeats);
                                float cw = std::max(3.0f, cx2 - cx1);
                                if (cx2 >= (float)headerWidth && cx1 <= (float)w)
                                {
                                    auto miniRect = juce::Rectangle<float>(std::max((float)headerWidth, cx1), (float)y + 6.0f, cw, (float)trackHeight - 12.0f);
                                    g.setColour(clip.color.withAlpha(0.65f));
                                    g.fillRoundedRectangle(miniRect, 2.0f);
                                    g.setColour(clip.color.brighter(0.2f));
                                    g.drawRoundedRectangle(miniRect, 2.0f, 1.0f);
                                }
                            }
                        }
                    }
                }
                else
                {
                    g.setColour(CubaseTheme::bgPanel().withAlpha(0.2f));
                    g.fillRect(laneArea.reduced(2, 4));
                }
            }
        } // End laneSaveState

        // Row Separator
        g.setColour(CubaseTheme::borderSubtle());
        g.drawHorizontalLine(y + trackHeight - 1, 0.0f, (float)w);

        y += trackHeight;
    }

    // Visual Alt+Drag Ghost Preview Box (Render all selected items simultaneously)
    if (dragMode == ArrangerDragMode::AltDuplicateItem)
    {
        double shift = altGhostStartBeat - itemInitialStartBeat;
        std::vector<DraggedEventSnapshot> snapshotsToRender = selectedDragSnapshots;
        if (snapshotsToRender.empty() && altGhostTrackId >= 0)
        {
            auto* trk = trackModel.getTrackById(altGhostTrackId);
            if (trk)
                snapshotsToRender.push_back({ trk->getType(), trk->getId(), draggedItemId, draggedItemStringId, itemInitialStartBeat, altGhostLengthBeats });
        }

        for (const auto& s : snapshotsToRender)
        {
            for (int tIdx = 0; tIdx < (int)tracks.size(); ++tIdx)
            {
                if (tracks[tIdx]->getId() == s.trackId)
                {
                    int gy = rulerHeight - scrollTrackY + tIdx * trackHeight;
                    double gStart = std::max(0.0, s.initialStartBeat + shift);
                    float gx1 = beatToX(gStart);
                    float gx2 = beatToX(gStart + s.initialLengthBeats);
                    float gw = std::max(6.0f, gx2 - gx1);
                    auto ghostRect = juce::Rectangle<float>(gx1 + 1.0f, (float)gy + 3.0f, gw - 2.0f, (float)trackHeight - 6.0f);

                    bool isValidPlacement = true;
                    if (s.trackType == TrackType::Chord || s.trackType == TrackType::Mode)
                        isValidPlacement = trackModel.canPlaceEvent(s.trackId, gStart, s.initialLengthBeats);

                    g.setColour(isValidPlacement ? CubaseTheme::accentOrange().withAlpha(0.35f) : juce::Colours::red.withAlpha(0.35f));
                    g.fillRoundedRectangle(ghostRect, 3.0f);
                    g.setColour(isValidPlacement ? juce::Colours::white : juce::Colours::red.withAlpha(0.8f));
                    float dashes[] = { 4.0f, 3.0f };
                    g.drawDashedLine(juce::Line<float>(ghostRect.getX(), ghostRect.getY(), ghostRect.getRight(), ghostRect.getY()), dashes, 2, 1.5f);
                    g.drawDashedLine(juce::Line<float>(ghostRect.getRight(), ghostRect.getY(), ghostRect.getRight(), ghostRect.getBottom()), dashes, 2, 1.5f);
                    g.drawDashedLine(juce::Line<float>(ghostRect.getRight(), ghostRect.getBottom(), ghostRect.getX(), ghostRect.getBottom()), dashes, 2, 1.5f);
                    g.drawDashedLine(juce::Line<float>(ghostRect.getX(), ghostRect.getBottom(), ghostRect.getX(), ghostRect.getY()), dashes, 2, 1.5f);
                    break;
                }
            }
        }
    }

    // Visual drag line when reordering tracks
    if (dragMode == ArrangerDragMode::ReorderTrack && dragTrackTargetIndex >= 0)
    {
        auto visTracks = trackModel.getVisibleTracks();
        bool isHoveringFolder = false;
        if (dragTrackTargetIndex >= 0 && dragTrackTargetIndex < (int)visTracks.size())
            isHoveringFolder = (visTracks[dragTrackTargetIndex]->getType() == TrackType::Folder);

        int folderBaseY = rulerHeight - scrollTrackY + dragTrackTargetIndex * trackHeight;
        int lineY = isHoveringFolder ? (folderBaseY + trackHeight) : folderBaseY;

        g.setColour(CubaseTheme::accentOrange());
        g.drawHorizontalLine(lineY, 0.0f, (float)w);
        g.fillRoundedRectangle(0.0f, (float)(lineY - 3), 12.0f, 6.0f, 2.0f);

        // Highlight folder track if hovering over it
        if (isHoveringFolder)
        {
            auto fBox = juce::Rectangle<int>(0, folderBaseY, headerWidth, trackHeight);
            g.setColour(CubaseTheme::accentCyan().withAlpha(0.25f));
            g.fillRect(fBox);
            g.setColour(CubaseTheme::accentCyan());
            g.drawRect(fBox, 2);
        }
    }

    // 3b. Marquee Selection Box
    if (dragMode == ArrangerDragMode::MarqueeSelect && !marqueeRect.isEmpty())
    {
        g.setColour(CubaseTheme::accentBlue().withAlpha(0.2f));
        g.fillRect(marqueeRect);
        g.setColour(CubaseTheme::accentBlue());
        g.drawRect(marqueeRect, 1);
    }

    // 3c. Full-Track Event Range Indicator Overlay & Locators (Always visible, loop-aware)
    double rangeStart = midiModel.getLoopStart();
    double rangeEnd = midiModel.getLoopEnd();
    bool isLoopActive = midiModel.isLoopEnabled();

    float rX1 = beatToX(rangeStart);
    float rX2 = beatToX(rangeEnd);

    if (rangeEnd > rangeStart)
    {
        float boxLeft = std::max((float)headerWidth, std::min(rX1, rX2));
        float boxRight = std::min((float)w, std::max(rX1, rX2));
        if (boxRight > boxLeft)
        {
            auto rangeRect = juce::Rectangle<float>(boxLeft, (float)rulerHeight, boxRight - boxLeft, (float)(h - rulerHeight));
            g.setColour(isLoopActive ? CubaseTheme::locatorRangeOverlay() : juce::Colours::white.withAlpha(0.035f));
            g.fillRect(rangeRect);
        }
    }

    juce::Colour locCol = isLoopActive ? CubaseTheme::locatorHandle() : juce::Colours::white.withAlpha(0.55f);
    if (rX1 >= (float)headerWidth && rX1 <= (float)w)
    {
        g.setColour(locCol);
        g.drawVerticalLine((int)rX1, (float)rulerHeight, (float)h);
    }
    if (rX2 >= (float)headerWidth && rX2 <= (float)w && std::abs(rX2 - rX1) >= 1.0f)
    {
        g.setColour(locCol);
        g.drawVerticalLine((int)rX2, (float)rulerHeight, (float)h);
    }

    // Ruler [L] and [R] Locator Badges & Cycle Bar
    {
        juce::Colour rangeAccent = isLoopActive ? CubaseTheme::locatorHandle() : CubaseTheme::textSecondary().withAlpha(0.65f);
        if (rX2 >= rX1 && rX2 >= (float)headerWidth)
        {
            if (rX2 > rX1 + 1.0f)
            {
                float leftDraw = std::max((float)headerWidth, rX1);
                float rightDraw = std::min((float)w, rX2);
                if (rightDraw > leftDraw)
                {
                    g.setColour(rangeAccent);
                    g.fillRect(leftDraw, 0.0f, rightDraw - leftDraw, isLoopActive ? 3.0f : 1.5f);
                }
                if (rX1 >= (float)headerWidth && rX1 <= (float)w)
                {
                    // Left Locator Badge: [L
                    g.setColour(rangeAccent);
                    g.fillRect(rX1 - 2.0f, 0.0f, 4.0f, (float)rulerHeight);
                    g.fillRect(rX1 - 1.0f, 0.0f, 16.0f, 13.0f);
                    g.setColour(isLoopActive ? CubaseTheme::locatorHandle().contrasting(0.95f) : juce::Colours::white);
                    g.setFont(CubaseTheme::getFont(9.5f, true));
                    g.drawText("[L", (int)rX1, 0, 15, 13, juce::Justification::centred);
                }
                if (rX2 >= (float)headerWidth && rX2 <= (float)w)
                {
                    // Right Locator Badge: R]
                    g.setColour(rangeAccent);
                    g.fillRect(rX2 - 2.0f, 0.0f, 4.0f, (float)rulerHeight);
                    g.fillRect(rX2 - 15.0f, 0.0f, 16.0f, 13.0f);
                    g.setColour(isLoopActive ? CubaseTheme::locatorHandle().contrasting(0.95f) : juce::Colours::white);
                    g.setFont(CubaseTheme::getFont(9.5f, true));
                    g.drawText("R]", (int)rX2 - 15, 0, 15, 13, juce::Justification::centred);
                }
            }
            else if (rX1 >= (float)headerWidth && rX1 <= (float)w)
            {
                // Coinciding locators (zero-length range): draw abutting [L and R] badges
                g.setColour(rangeAccent);
                g.fillRect(rX1 - 1.5f, 0.0f, 3.0f, (float)rulerHeight);

                g.fillRect(rX1 - 16.0f, 0.0f, 16.0f, 13.0f);
                g.fillRect(rX1, 0.0f, 16.0f, 13.0f);

                g.setColour(isLoopActive ? CubaseTheme::locatorHandle().contrasting(0.95f) : juce::Colours::white);
                g.setFont(CubaseTheme::getFont(9.5f, true));
                g.drawText("[L", (int)rX1 - 16, 0, 16, 13, juce::Justification::centred);
                g.drawText("R]", (int)rX1, 0, 16, 13, juce::Justification::centred);
            }
        }
    }

    // 4. Playhead Line
    float playheadX = beatToX(currentPlayhead);
    if (playheadX >= (float)headerWidth && playheadX <= (float)w)
    {
        g.setColour(CubaseTheme::accentOrange());
        g.drawVerticalLine((int)playheadX, 0.0f, (float)h);

        // Playhead triangle at ruler
        juce::Path triangle;
        triangle.addTriangle(playheadX - 5.0f, 0.0f, playheadX + 5.0f, 0.0f, playheadX, (float)rulerHeight);
        g.fillPath(triangle);
    }
}

//==============================================================================
// Mouse Move & Modifier Keys Changed (Cursor feedback)
//==============================================================================
void TrackArrangerComponent::mouseMove(const juce::MouseEvent& e)
{
    if (e.mods.isAltDown())
    {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }

    if (e.x < headerWidth)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    if (e.y < rulerHeight)
    {
        float loopX1 = beatToX(midiModel.getLoopStart());
        float loopX2 = beatToX(midiModel.getLoopEnd());
        if (std::abs(e.x - loopX1) <= 6.0f || std::abs(e.x - loopX2) <= 6.0f)
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    // Check if hovering near edge of any clip/chord/marker
    auto visTracks = trackModel.getVisibleTracks();
    int trkIdx = (e.y - rulerHeight + scrollTrackY) / trackHeight;
    if (trkIdx >= 0 && trkIdx < (int)visTracks.size())
    {
        auto* trk = visTracks[trkIdx];
        double mouseBeat = xToBeat((float)e.x);
        float thresholdBeats = 10.0f / pixelsPerBeat;

        if (trk->getType() == TrackType::Instrument)
        {
            for (const auto& clip : trk->getClips())
            {
                if (std::abs(mouseBeat - clip.startBeat) <= thresholdBeats ||
                    std::abs(mouseBeat - (clip.startBeat + clip.lengthBeats)) <= thresholdBeats)
                {
                    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                    return;
                }
            }
        }
        else if (trk->getType() == TrackType::Chord)
        {
            for (const auto& c : trk->getChords())
            {
                if (std::abs(mouseBeat - c.startBeat) <= thresholdBeats ||
                    std::abs(mouseBeat - (c.startBeat + c.lengthBeats)) <= thresholdBeats)
                {
                    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                    return;
                }
            }
        }
        else if (trk->getType() == TrackType::Mode)
        {
            for (const auto& md : trk->getModes())
            {
                if (std::abs(mouseBeat - md.startBeat) <= thresholdBeats ||
                    std::abs(mouseBeat - (md.startBeat + md.lengthBeats)) <= thresholdBeats)
                {
                    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                    return;
                }
            }
        }
        else if (trk->getType() == TrackType::Marker)
        {
            for (const auto& m : trk->getMarkers())
            {
                if (std::abs(mouseBeat - m.startBeat) <= thresholdBeats ||
                    std::abs(mouseBeat - (m.startBeat + m.lengthBeats)) <= thresholdBeats)
                {
                    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                    return;
                }
            }
        }
        else if (trk->getType() == TrackType::TimeSignature)
        {
            for (const auto& ts : trk->getTimeSignatures())
            {
                if (std::abs(mouseBeat - ts.startBeat) <= thresholdBeats * 1.5f ||
                    std::abs(mouseBeat - (ts.startBeat + ts.lengthBeats)) <= thresholdBeats * 1.5f)
                {
                    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                    return;
                }
            }
        }
    }

    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void TrackArrangerComponent::modifierKeysChanged(const juce::ModifierKeys& mods)
{
    if (mods.isAltDown())
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

//==============================================================================
// Mouse Wheel Zoom & Scroll (Cubase Parity)
//==============================================================================
void TrackArrangerComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (wheel.deltaY == 0.0f && wheel.deltaX == 0.0f) return;

    // 1. Horizontal swipe / scroll via deltaX (trackpad horizontal swipe or horizontal wheel)
    if (wheel.deltaX != 0.0f && !e.mods.isCtrlDown())
    {
        double beatDelta = -wheel.deltaX * (120.0 / (double)pixelsPerBeat);
        scrollBeats = std::max(0.0, scrollBeats + beatDelta);
        updateScrollBar();
        repaint();
        return;
    }

    // 2. Ctrl + Wheel: Horizontal Zoom (Pivoting around mouse beat)
    if (e.mods.isCtrlDown() && !e.mods.isShiftDown())
    {
        float factor = (wheel.deltaY > 0) ? 1.20f : 0.83f;
        int mouseTrackX = std::max(headerWidth, e.x);
        double anchorBeat = xToBeat((float)mouseTrackX);

        pixelsPerBeat = std::clamp(pixelsPerBeat * factor, 2.0f, 500.0f);
        scrollBeats = anchorBeat - (double)(mouseTrackX - headerWidth) / pixelsPerBeat;
        scrollBeats = std::max(0.0, scrollBeats);
        updateScrollBar();
        repaint();
        return;
    }

    // 3. Ctrl + Shift + Wheel: Vertical Zoom (Pivoting around mouse track position)
    if (e.mods.isCtrlDown() && e.mods.isShiftDown())
    {
        int deltaH = (wheel.deltaY > 0) ? 6 : -6;
        int oldH = trackHeight;
        int newH = std::clamp(oldH + deltaH, 26, 120);
        if (newH != oldH)
        {
            int relY = std::max(0, e.y - rulerHeight);
            double trackFrac = (double)(relY + scrollTrackY) / (double)oldH;

            trackHeight = newH;

            int totalTrackHeight = (int)trackModel.getVisibleTracks().size() * trackHeight;
            int maxScrollY = std::max(0, totalTrackHeight - (getHeight() - rulerHeight - 40));
            int newScrollY = (int)std::round(trackFrac * (double)newH - (double)relY);
            scrollTrackY = std::clamp(newScrollY, 0, maxScrollY);

            repaint();
        }
        return;
    }

    // 4. Shift + Wheel: Horizontal Scroll (strictly matching Piano Roll: wheel down -> scroll right/forward, wheel up -> scroll left)
    if (e.mods.isShiftDown())
    {
        double beatDelta = -wheel.deltaY * (120.0 / (double)pixelsPerBeat);
        scrollBeats = std::max(0.0, scrollBeats + beatDelta);
        updateScrollBar();
        repaint();
        return;
    }

    // 5. Regular Wheel: Vertical Track Scroll
    int yDelta = (wheel.deltaY > 0 ? -24 : 24);
    int totalTrackHeight = (int)trackModel.getVisibleTracks().size() * trackHeight;
    int maxScrollY = std::max(0, totalTrackHeight - (getHeight() - rulerHeight - 40));
    scrollTrackY = std::clamp(scrollTrackY + yDelta, 0, maxScrollY);
    repaint();
}

//==============================================================================
// Mouse Down
//==============================================================================
bool TrackArrangerComponent::splitEventAt(Track& track, double clickBeat,
                                          int64_t itemId, const juce::String& stringId)
{
    if (track.isLocked() || !std::isfinite(clickBeat)) return false;
    const double interval = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.0;
    const double snapped = interval > 0.0 ? std::round(clickBeat / interval) * interval : clickBeat;
    auto cutWithin = [&](double start, double length) {
        const auto valid = [=](double beat) {
            return beat > start + 0.01 && beat < start + length - 0.01;
        };
        return valid(snapped) ? snapped : (valid(clickBeat) ? clickBeat : -1.0);
    };

    if (track.getType() == TrackType::Instrument)
    {
        for (const auto& clip : track.getClips())
            if ((itemId < 0 || clip.id == itemId)
                && clickBeat >= clip.startBeat && clickBeat <= clip.startBeat + clip.lengthBeats)
            {
                const double cut = cutWithin(clip.startBeat, clip.lengthBeats);
                return cut >= 0.0 && trackModel.splitMidiClip(track.getId(), clip.id, cut,
                                                                &undoManager);
            }
    }
    else if (track.getType() == TrackType::Audio)
    {
        for (auto it = track.getAudioClips().rbegin(); it != track.getAudioClips().rend(); ++it)
            if ((itemId < 0 || it->id == itemId)
                && clickBeat >= it->startBeat && clickBeat <= it->startBeat + it->lengthBeats)
            {
                const double cut = cutWithin(it->startBeat, it->lengthBeats);
                return cut >= 0.0 && trackModel.splitAudioClip(track.getId(), it->id, cut,
                                                                 midiModel.getBpm(), &undoManager);
            }
    }
    else if (track.getType() == TrackType::Chord)
    {
        auto before = track.getChords();
        auto after = before;
        for (auto& chord : after)
            if ((itemId < 0 || chord.id == itemId)
                && clickBeat >= chord.startBeat && clickBeat <= chord.startBeat + chord.lengthBeats)
            {
                const double cut = cutWithin(chord.startBeat, chord.lengthBeats);
                if (cut < 0.0) return false;
                ChordEvent right = chord;
                right.id = juce::Time::currentTimeMillis();
                for (const auto& existing : before)
                    right.id = std::max(right.id, existing.id + 1);
                right.startBeat = cut;
                right.lengthBeats = chord.startBeat + chord.lengthBeats - cut;
                right.isSelected = true;
                chord.lengthBeats = cut - chord.startBeat;
                chord.isSelected = false;
                after.push_back(std::move(right));
                return UndoActionHelper::performAtomic(&undoManager,
                    new ModifyChordsUndoAction(trackModel, track.getId(), before, after,
                                               "Split Chord"), "Split Chord");
            }
    }
    else if (track.getType() == TrackType::Mode)
    {
        auto before = track.getModes();
        auto after = before;
        for (auto& mode : after)
            if ((itemId < 0 || mode.id == itemId)
                && clickBeat >= mode.startBeat && clickBeat <= mode.startBeat + mode.lengthBeats)
            {
                const double cut = cutWithin(mode.startBeat, mode.lengthBeats);
                if (cut < 0.0) return false;
                ModeEvent right = mode;
                right.id = juce::Time::currentTimeMillis();
                for (const auto& existing : before)
                    right.id = std::max(right.id, existing.id + 1);
                right.startBeat = cut;
                right.lengthBeats = mode.startBeat + mode.lengthBeats - cut;
                right.isSelected = true;
                mode.lengthBeats = cut - mode.startBeat;
                mode.isSelected = false;
                after.push_back(std::move(right));
                return UndoActionHelper::performAtomic(&undoManager,
                    new ModifyModesUndoAction(trackModel, track.getId(), before, after,
                                              "Split Mode"), "Split Mode");
            }
    }
    else if (track.getType() == TrackType::Marker)
    {
        auto before = track.getMarkers();
        auto after = before;
        for (auto& marker : after)
            if ((stringId.isEmpty() || marker.id == stringId)
                && clickBeat >= marker.startBeat && clickBeat <= marker.startBeat + marker.lengthBeats)
            {
                const double cut = cutWithin(marker.startBeat, marker.lengthBeats);
                if (cut < 0.0) return false;
                MarkerEvent right = marker;
                right.id = juce::Uuid().toString();
                right.startBeat = cut;
                right.lengthBeats = marker.startBeat + marker.lengthBeats - cut;
                right.isSelected = true;
                marker.lengthBeats = cut - marker.startBeat;
                marker.isSelected = false;
                after.push_back(std::move(right));
                return UndoActionHelper::performAtomic(&undoManager,
                    new ModifyMarkersUndoAction(trackModel, track.getId(), before, after,
                                                "Split Marker"), "Split Marker");
            }
    }
    return false;
}

void TrackArrangerComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (onFocusGained)
        onFocusGained();

    // Guard bottom horizontal scrollbar area from accidental item/playhead clicks
    const int scrollbarH = 16;
    if (e.y >= getHeight() - scrollbarH && e.x >= headerWidth)
        return;

    // Middle Mouse Button Pan (matching Piano Roll)
    if (e.mods.isMiddleButtonDown())
    {
        dragMouseDownPos = e.getScreenPosition();
        dragMode = ArrangerDragMode::MiddlePan;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    // Close inline rename editor if active
    finishTrackRename(true);

    if (e.x < headerWidth)
    {
        if (e.y < rulerHeight)
            return;

        // Click on track header
        auto visTracks = trackModel.getVisibleTracks();
        int trkIdx = (e.y - rulerHeight + scrollTrackY) / trackHeight;
        if (trkIdx >= 0 && trkIdx < (int)visTracks.size())
        {
            auto* trk = visTracks[trkIdx];
            if (e.mods.isCtrlDown())
            {
                trk->setSelected(!trk->isSelected());
            }
            else
            {
                trackModel.clearAllArrangerSelection();
                trackModel.clearAllTrackSelection();
                trk->setSelected(true);
            }

            if (trk->getType() == TrackType::Instrument)
            {
                int64_t clipId = trk->getClips().empty() ? -1 : trk->getClips()[0].id;
                trackModel.setActiveClip(trk->getId(), clipId, &midiModel);
            }
            else
            {
                trackModel.setActiveClip(trk->getId(), -1, &midiModel);
            }

            if (e.mods.isPopupMenu())
            {
                showTrackContextMenu(trk->getId(), e.getScreenPosition());
                return;
            }

            int trackY = rulerHeight - scrollTrackY + trkIdx * trackHeight;
            auto btn1 = juce::Rectangle<int>(headerWidth - 56, trackY + 12, 22, 20);
            auto btn2 = juce::Rectangle<int>(headerWidth - 30, trackY + 12, 22, 20);

            if (trk->getType() == TrackType::Folder)
            {
                if (btn2.contains(e.getPosition()))
                {
                    trk->setFolderCollapsed(!trk->isFolderCollapsed());
                    trackModel.triggerTracksChanged();
                    repaint();
                    return;
                }
            }

            // Track-specific buttons in header
            if (btn1.contains(e.getPosition()))
            {
                if (trk->getType() == TrackType::Instrument || trk->getType() == TrackType::Audio || trk->getType() == TrackType::Chord)
                {
                    trk->setMuted(!trk->isMuted());
                    trackModel.triggerTracksChanged();
                    repaint();
                    return;
                }
                else if (trk->getType() == TrackType::Marker || trk->getType() == TrackType::TimeSignature)
                {
                    trk->setLocked(!trk->isLocked());
                    trackModel.triggerTracksChanged();
                    repaint();
                    return;
                }
            }
            if (btn2.contains(e.getPosition()))
            {
                if (trk->getType() == TrackType::Instrument || trk->getType() == TrackType::Audio)
                {
                    trk->setSolo(!trk->isSolo());
                    trackModel.triggerTracksChanged();
                    repaint();
                    return;
                }
                else if (trk->getType() == TrackType::Chord)
                {
                    trk->setHarmonizeEnabled(!trk->isHarmonizeEnabled());
                    trackModel.triggerTracksChanged();
                    repaint();
                    return;
                }
                else if (trk->getType() == TrackType::Marker)
                {
                    // Goto next marker
                    double pb = audioEngine.getPlayheadBeat();
                    double nextMarkerBeat = 1e9;
                    for (const auto& m : trk->getMarkers())
                    {
                        if (m.startBeat > pb + 0.1 && m.startBeat < nextMarkerBeat)
                            nextMarkerBeat = m.startBeat;
                    }
                    if (nextMarkerBeat < 1e8)
                        audioEngine.setPlayheadBeat(nextMarkerBeat);
                    return;
                }
                else if (trk->getType() == TrackType::TimeSignature)
                {
                    audioEngine.setMetronomeEnabled(!audioEngine.isMetronomeEnabled());
                    repaint();
                    return;
                }
            }

            // Begin track reordering drag
            dragMode = ArrangerDragMode::ReorderTrack;
            dragTrackSourceIndex = trkIdx;
            dragTrackTargetIndex = trkIdx;
            dragTrackMouseY = e.y;
            repaint();
        }
        else if (e.mods.isPopupMenu())
        {
            showAddTrackMenu();
        }
        return;
    }

    if (e.y < rulerHeight)
    {
        float loopX1 = beatToX(midiModel.getLoopStart());
        float loopX2 = beatToX(midiModel.getLoopEnd());
        double clickedBeat = std::max(0.0, xToBeat((float)e.x));
        double qBeat = midiModel.isSnapEnabled() ? midiModel.quantizeBeat(clickedBeat) : clickedBeat;

        // 1. Hover/Drag existing locator handles
        if (std::abs(e.x - loopX1) <= 6.0f && std::abs(e.x - loopX2) <= 6.0f)
        {
            stationaryLocatorBeat = midiModel.getLoopStart();
            dragMode = ArrangerDragMode::RulerLeftLocator;
        }
        else if (std::abs(e.x - loopX1) <= 6.0f)
        {
            stationaryLocatorBeat = midiModel.getLoopEnd();
            dragMode = ArrangerDragMode::RulerLeftLocator;
        }
        else if (std::abs(e.x - loopX2) <= 6.0f)
        {
            stationaryLocatorBeat = midiModel.getLoopStart();
            dragMode = ArrangerDragMode::RulerRightLocator;
        }
        // 2. Cubase Shortcut: Ctrl + Left Click sets Left locator [L
        else if (e.mods.isCtrlDown())
        {
            stationaryLocatorBeat = midiModel.getLoopEnd();
            double newStart = std::min(qBeat, stationaryLocatorBeat);
            double newEnd = std::max(qBeat, stationaryLocatorBeat);
            midiModel.setLoopRange(newStart, newEnd);
            dragMode = ArrangerDragMode::RulerLeftLocator;
            repaint();
        }
        // 3. Cubase Shortcut: Alt + Left Click sets Right locator R]
        else if (e.mods.isAltDown())
        {
            stationaryLocatorBeat = midiModel.getLoopStart();
            double newStart = std::min(stationaryLocatorBeat, qBeat);
            double newEnd = std::max(stationaryLocatorBeat, qBeat);
            midiModel.setLoopRange(newStart, newEnd);
            dragMode = ArrangerDragMode::RulerRightLocator;
            repaint();
        }
        // 4. Regular Click: Scrub playhead with quantize snapping
        else
        {
            dragMode = ArrangerDragMode::ScrubPlayhead;
            wasPlayingBeforeDrag = audioEngine.isPlaying();
            if (wasPlayingBeforeDrag)
                audioEngine.pause();
            audioEngine.setPlayheadBeat(std::max(0.0, qBeat));
            lastRulerMousePos = e.getPosition();
            repaint();
        }
        return;
    }

    // Click in track lane
    int trkIdx = (e.y - rulerHeight + scrollTrackY) / trackHeight;
    auto visTracks = trackModel.getVisibleTracks();
    if (trkIdx >= 0 && trkIdx < (int)visTracks.size())
    {
        auto* trk = visTracks[trkIdx];
        if (trk->getType() == TrackType::Instrument)
        {
            if (trackModel.getActiveTrackId() != trk->getId())
            {
                const int64_t clipId = trk->getClips().empty() ? -1 : trk->getClips().front().id;
                trackModel.setActiveClip(trk->getId(), clipId, &midiModel);
            }
        }
        else
            trackModel.setActiveClip(trk->getId(), -1, &midiModel);
        int trackY = rulerHeight - scrollTrackY + trkIdx * trackHeight;

        double clickBeat = xToBeat((float)e.x);

        if (e.mods.isPopupMenu())
        {
            showLaneContextMenu(trk->getId(), clickBeat, e.getScreenPosition());
            return;
        }

        double snapInterval = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.0;
        double snapBeat = clickBeat;
        if (midiModel.isSnapEnabled() && snapInterval > 0.0)
            snapBeat = std::round(clickBeat / snapInterval) * snapInterval;
        float thresholdBeats = 10.0f / pixelsPerBeat;
        dragMouseDownPos = e.getPosition();

        // Arranger Tool Responses: Erase, Split, Mute, Draw
        if (midiModel.getCurrentTool() == ToolType::Erase && !e.mods.isAltDown())
        {
            if (trk->getType() == TrackType::Instrument)
            {
                auto before = trk->getClips();
                auto after = before;
                after.erase(std::remove_if(after.begin(), after.end(), [clickBeat](const MidiClip& c) {
                    return clickBeat >= c.startBeat && clickBeat <= c.startBeat + c.lengthBeats;
                }), after.end());
                if (before.size() != after.size())
                {
                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyClipsUndoAction(trackModel, trk->getId(), before, after, "Erase MIDI Part"),
                        "Erase MIDI Part");
                    repaint();
                    return;
                }
            }
            else if (trk->getType() == TrackType::Chord)
            {
                auto before = trk->getChords();
                auto after = before;
                after.erase(std::remove_if(after.begin(), after.end(), [clickBeat](const ChordEvent& c) {
                    return clickBeat >= c.startBeat && clickBeat <= c.startBeat + c.lengthBeats;
                }), after.end());
                if (before.size() != after.size())
                {
                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyChordsUndoAction(trackModel, trk->getId(), before, after, "Erase Chord"),
                        "Erase Chord");
                    repaint();
                    return;
                }
            }
            else if (trk->getType() == TrackType::Mode)
            {
                auto before = trk->getModes();
                auto after = before;
                after.erase(std::remove_if(after.begin(), after.end(), [clickBeat](const ModeEvent& m) {
                    return clickBeat >= m.startBeat && clickBeat <= m.startBeat + m.lengthBeats;
                }), after.end());
                if (before.size() != after.size())
                {
                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyModesUndoAction(trackModel, trk->getId(), before, after, "Erase Mode"),
                        "Erase Mode");
                    repaint();
                    return;
                }
            }
            else if (trk->getType() == TrackType::Marker)
            {
                auto before = trk->getMarkers();
                auto after = before;
                after.erase(std::remove_if(after.begin(), after.end(), [clickBeat](const MarkerEvent& m) {
                    return clickBeat >= m.startBeat && clickBeat <= m.startBeat + m.lengthBeats;
                }), after.end());
                if (before.size() != after.size())
                {
                    UndoActionHelper::performAtomic(&undoManager,
                        new ModifyMarkersUndoAction(trackModel, trk->getId(), before, after, "Erase Marker"),
                        "Erase Marker");
                    repaint();
                    return;
                }
            }
            return;
        }

        if (midiModel.getCurrentTool() == ToolType::Split && !e.mods.isAltDown())
        {
            splitEventAt(*trk, clickBeat);
            return;
        }

        if (midiModel.getCurrentTool() == ToolType::Mute && !e.mods.isAltDown())
        {
            if (trk->getType() == TrackType::Instrument)
            {
                for (auto& clip : trk->getClips())
                {
                    if (clickBeat >= clip.startBeat && clickBeat <= clip.startBeat + clip.lengthBeats)
                    {
                        clip.isMuted = !clip.isMuted;
                        trackModel.triggerTracksChanged();
                        repaint();
                        return;
                    }
                }
            }
            return;
        }

        if (midiModel.getCurrentTool() == ToolType::Draw && !e.mods.isAltDown())
        {
            double defaultLen = snapInterval > 0.0 ? snapInterval : (double)midiModel.getTimeSigNumerator();
            if (defaultLen <= 0.0) defaultLen = 4.0;

            if (trk->getType() == TrackType::Instrument)
            {
                MidiClip newClip;
                newClip.id = juce::Time::currentTimeMillis();
                newClip.name = "Part " + juce::String((int)trk->getClips().size() + 1);
                newClip.startBeat = snapBeat;
                newClip.lengthBeats = defaultLen;
                newClip.color = trk->getColor();

                auto oldClips = trk->getClips();
                auto newClips = oldClips;
                newClips.push_back(newClip);
                UndoActionHelper::performAtomic(&undoManager, new ModifyClipsUndoAction(trackModel, trk->getId(), oldClips, newClips, "Draw MIDI Part"), "Draw MIDI Part");
                trackModel.setActiveClip(trk->getId(), newClip.id, &midiModel);
            }
            else if (trk->getType() == TrackType::Chord)
            {
                ChordEvent ch;
                ch.id = juce::Time::currentTimeMillis();
                ch.startBeat = snapBeat;
                ch.lengthBeats = defaultLen;
                ch.chordName = "C";
                ch.root = 0;

                auto oldChords = trk->getChords();
                auto newChords = oldChords;
                newChords.push_back(ch);
                UndoActionHelper::performAtomic(&undoManager, new ModifyChordsUndoAction(trackModel, trk->getId(), oldChords, newChords, "Draw Chord"), "Draw Chord");
            }
            else if (trk->getType() == TrackType::Mode)
            {
                ModeEvent md;
                md.id = juce::Time::currentTimeMillis();
                md.startBeat = snapBeat;
                md.lengthBeats = defaultLen;
                md.name = "C Major";
                md.tonic = "C";

                auto oldModes = trk->getModes();
                auto newModes = oldModes;
                newModes.push_back(md);
                UndoActionHelper::performAtomic(&undoManager, new ModifyModesUndoAction(trackModel, trk->getId(), oldModes, newModes, "Draw Mode Section"), "Draw Mode Section");
            }
            else if (trk->getType() == TrackType::Marker)
            {
                MarkerEvent mk;
                mk.id = juce::Uuid().toString();
                mk.startBeat = snapBeat;
                mk.lengthBeats = defaultLen;
                mk.name = "Marker " + juce::String((int)trk->getMarkers().size() + 1);

                auto oldMarkers = trk->getMarkers();
                auto newMarkers = oldMarkers;
                newMarkers.push_back(mk);
                UndoActionHelper::performAtomic(&undoManager, new ModifyMarkersUndoAction(trackModel, trk->getId(), oldMarkers, newMarkers, "Draw Marker"), "Draw Marker");
            }
            repaint();
            return;
        }

        auto collectSelectedSnapshots = [this]() {
            selectedDragSnapshots.clear();
            for (const auto& t : trackModel.getTracks())
            {
                if (t->getType() == TrackType::Audio)
                {
                    for (const auto& clip : t->getAudioClips())
                        if (clip.isSelected)
                            selectedDragSnapshots.push_back({ t->getType(), t->getId(), clip.id,
                                {}, clip.startBeat, clip.lengthBeats });
                }
                else if (t->getType() == TrackType::Instrument)
                {
                    for (const auto& c : t->getClips())
                        if (c.isSelected)
                            selectedDragSnapshots.push_back({ t->getType(), t->getId(), c.id, {}, c.startBeat, c.lengthBeats });
                }
                else if (t->getType() == TrackType::Chord)
                {
                    for (const auto& c : t->getChords())
                        if (c.isSelected)
                            selectedDragSnapshots.push_back({ t->getType(), t->getId(), c.id, {}, c.startBeat, c.lengthBeats });
                }
                else if (t->getType() == TrackType::Mode)
                {
                    for (const auto& m : t->getModes())
                        if (m.isSelected)
                            selectedDragSnapshots.push_back({ t->getType(), t->getId(), m.id, {}, m.startBeat, m.lengthBeats });
                }
                else if (t->getType() == TrackType::Marker)
                {
                    for (const auto& m : t->getMarkers())
                        if (m.isSelected)
                            selectedDragSnapshots.push_back({ t->getType(), t->getId(), -1, m.id, m.startBeat, m.lengthBeats });
                }
                else if (t->getType() == TrackType::TimeSignature)
                {
                    for (const auto& ts : t->getTimeSignatures())
                        if (ts.isSelected)
                            selectedDragSnapshots.push_back({ t->getType(), t->getId(), ts.id, {}, ts.startBeat, 4.0 });
                }
                else if (t->getType() == TrackType::Tempo)
                {
                    for (const auto& ev : t->getTempoEvents())
                        if (ev.isSelected)
                            selectedDragSnapshots.push_back({ t->getType(), t->getId(), ev.id, {}, ev.startBeat, 1.0 });
                }
            }
        };

        // 1. FIRST: Check clicking on items (supports Move, Resize Left/Right, and Alt+Drag Duplicate)
        if (trk->getType() == TrackType::Audio)
        {
            for (auto& clip : trk->getAudioClips())
            {
                if (clickBeat < clip.startBeat || clickBeat > clip.startBeat + clip.lengthBeats)
                    continue;
                trackModel.setActiveClip(trk->getId(), -1, &midiModel);
                if (e.mods.isCtrlDown() && !e.mods.isAltDown()) clip.isSelected = !clip.isSelected;
                else
                {
                    if (!clip.isSelected)
                    {
                        trackModel.clearAllArrangerSelection();
                        clip.isSelected = true;
                    }
                }
                draggedTrackId = trk->getId();
                draggedItemId = clip.id;
                dragStartBeat = clickBeat;
                itemInitialStartBeat = clip.startBeat;
                itemInitialLengthBeats = clip.lengthBeats;
                altGhostStartBeat = clip.startBeat;
                altGhostLengthBeats = clip.lengthBeats;
                altGhostTrackId = trk->getId();
                if (e.mods.isAltDown() && clip.isSelected && !trk->isLocked())
                {
                    collectSelectedSnapshots();
                    isDuplicatingWithAlt = true;
                    dragMode = ArrangerDragMode::AltDuplicateItem;
                }
                else
                    dragMode = clip.isSelected && !trk->isLocked()
                        ? ArrangerDragMode::MoveAudioClip : ArrangerDragMode::None;
                repaint();
                return;
            }
        }
        else if (trk->getType() == TrackType::Instrument)
        {
            MidiClip* chosenClip = nullptr;
            int64_t currentActiveClipId = trackModel.getActiveClipId();

            // First: prioritize currently active editing clip if it contains clickBeat
            for (auto& clip : trk->getClips())
            {
                if (clip.id == currentActiveClipId &&
                    clickBeat >= clip.startBeat - thresholdBeats &&
                    clickBeat <= (clip.startBeat + clip.lengthBeats + thresholdBeats))
                {
                    chosenClip = &clip;
                    break;
                }
            }

            // Fallback: check other clips on this track
            if (chosenClip == nullptr)
            {
                for (auto& clip : trk->getClips())
                {
                    if (clickBeat >= clip.startBeat - thresholdBeats &&
                        clickBeat <= (clip.startBeat + clip.lengthBeats + thresholdBeats))
                    {
                        chosenClip = &clip;
                        break;
                    }
                }
            }

            if (chosenClip != nullptr)
            {
                auto& clip = *chosenClip;
                trackModel.setActiveClip(trk->getId(), clip.id, &midiModel);

                draggedTrackId = trk->getId();
                draggedItemId = clip.id;
                dragStartBeat = clickBeat;
                itemInitialStartBeat = clip.startBeat;
                itemInitialLengthBeats = clip.lengthBeats;
                altGhostStartBeat = clip.startBeat;
                altGhostLengthBeats = clip.lengthBeats;
                altGhostTrackId = trk->getId();

                if (e.mods.isAltDown())
                {
                    if (!clip.isSelected)
                    {
                        trackModel.clearAllArrangerSelection();
                        clip.isSelected = true;
                    }
                    collectSelectedSnapshots();
                    isDuplicatingWithAlt = true;
                    dragMode = ArrangerDragMode::AltDuplicateItem;
                }
                else
                {
                    if (e.mods.isCtrlDown())
                        clip.isSelected = !clip.isSelected;
                    else
                    {
                        trackModel.clearAllTrackSelection();
                        if (!clip.isSelected)
                        {
                            trackModel.clearAllArrangerSelection();
                            clip.isSelected = true;
                        }
                    }

                    if (std::abs(clickBeat - clip.startBeat) <= thresholdBeats)
                        dragMode = ArrangerDragMode::ResizeClipLeft;
                    else if (std::abs(clickBeat - (clip.startBeat + clip.lengthBeats)) <= thresholdBeats)
                        dragMode = ArrangerDragMode::ResizeClipRight;
                    else
                    {
                        dragMode = ArrangerDragMode::MoveClip;
                        collectSelectedSnapshots();
                    }
                }

                repaint();
                return;
            }
        }
        else if (trk->getType() == TrackType::Chord)
        {
            for (auto& c : trk->getChords())
            {
                if (clickBeat >= c.startBeat - thresholdBeats && clickBeat <= (c.startBeat + c.lengthBeats + thresholdBeats))
                {
                    draggedTrackId = trk->getId();
                    draggedItemId = c.id;
                    dragStartBeat = clickBeat;
                    itemInitialStartBeat = c.startBeat;
                    itemInitialLengthBeats = c.lengthBeats;
                    altGhostStartBeat = c.startBeat;
                    altGhostLengthBeats = c.lengthBeats;
                    altGhostTrackId = trk->getId();

                    if (e.mods.isAltDown())
                    {
                        if (!c.isSelected)
                        {
                            trackModel.clearAllArrangerSelection();
                            c.isSelected = true;
                        }
                        collectSelectedSnapshots();
                        isDuplicatingWithAlt = true;
                        dragMode = ArrangerDragMode::AltDuplicateItem;
                    }
                    else
                    {
                        if (e.mods.isCtrlDown())
                            c.isSelected = !c.isSelected;
                        else
                        {
                            trackModel.clearAllTrackSelection();
                            if (!c.isSelected)
                            {
                                trackModel.clearAllArrangerSelection();
                                c.isSelected = true;
                            }
                        }

                        if (std::abs(clickBeat - c.startBeat) <= thresholdBeats)
                            dragMode = ArrangerDragMode::ResizeChordLeft;
                        else if (std::abs(clickBeat - (c.startBeat + c.lengthBeats)) <= thresholdBeats)
                            dragMode = ArrangerDragMode::ResizeChordRight;
                        else
                        {
                            dragMode = ArrangerDragMode::MoveChord;
                            collectSelectedSnapshots();
                        }
                    }

                    repaint();
                    return;
                }
            }
        }
        else if (trk->getType() == TrackType::Mode)
        {
            for (auto& md : trk->getModes())
            {
                if (clickBeat >= md.startBeat - thresholdBeats && clickBeat <= (md.startBeat + md.lengthBeats + thresholdBeats))
                {
                    draggedTrackId = trk->getId();
                    draggedItemId = md.id;
                    dragStartBeat = clickBeat;
                    itemInitialStartBeat = md.startBeat;
                    itemInitialLengthBeats = md.lengthBeats;
                    altGhostStartBeat = md.startBeat;
                    altGhostLengthBeats = md.lengthBeats;
                    altGhostTrackId = trk->getId();

                    if (e.mods.isAltDown())
                    {
                        if (!md.isSelected)
                        {
                            trackModel.clearAllArrangerSelection();
                            md.isSelected = true;
                        }
                        collectSelectedSnapshots();
                        isDuplicatingWithAlt = true;
                        dragMode = ArrangerDragMode::AltDuplicateItem;
                    }
                    else
                    {
                        if (e.mods.isCtrlDown())
                            md.isSelected = !md.isSelected;
                        else
                        {
                            trackModel.clearAllTrackSelection();
                            if (!md.isSelected)
                            {
                                trackModel.clearAllArrangerSelection();
                                md.isSelected = true;
                            }
                        }

                        if (std::abs(clickBeat - md.startBeat) <= thresholdBeats)
                            dragMode = ArrangerDragMode::ResizeModeLeft;
                        else if (std::abs(clickBeat - (md.startBeat + md.lengthBeats)) <= thresholdBeats)
                            dragMode = ArrangerDragMode::ResizeModeRight;
                        else
                        {
                            dragMode = ArrangerDragMode::MoveMode;
                            collectSelectedSnapshots();
                        }
                    }

                    repaint();
                    return;
                }
            }
        }
        else if (trk->getType() == TrackType::Marker)
        {
            auto& markers = trk->getMarkers();
            for (int i = (int)markers.size() - 1; i >= 0; --i)
            {
                auto& m = markers[i];
                if (clickBeat >= m.startBeat - thresholdBeats && clickBeat <= (m.startBeat + m.lengthBeats + thresholdBeats))
                {
                    draggedTrackId = trk->getId();
                    draggedItemStringId = m.id;
                    dragStartBeat = clickBeat;
                    itemInitialStartBeat = m.startBeat;
                    itemInitialLengthBeats = m.lengthBeats;
                    altGhostStartBeat = m.startBeat;
                    altGhostLengthBeats = m.lengthBeats;
                    altGhostTrackId = trk->getId();

                    if (e.mods.isAltDown())
                    {
                        if (!m.isSelected)
                        {
                            trackModel.clearAllArrangerSelection();
                            m.isSelected = true;
                        }
                        collectSelectedSnapshots();
                        isDuplicatingWithAlt = true;
                        dragMode = ArrangerDragMode::AltDuplicateItem;
                    }
                    else
                    {
                        if (e.mods.isCtrlDown())
                            m.isSelected = !m.isSelected;
                        else
                        {
                            trackModel.clearAllTrackSelection();
                            if (!m.isSelected)
                            {
                                trackModel.clearAllArrangerSelection();
                                m.isSelected = true;
                            }
                        }

                        if (std::abs(clickBeat - m.startBeat) <= thresholdBeats)
                            dragMode = ArrangerDragMode::ResizeMarkerLeft;
                        else if (std::abs(clickBeat - (m.startBeat + m.lengthBeats)) <= thresholdBeats)
                            dragMode = ArrangerDragMode::ResizeMarkerRight;
                        else
                        {
                            dragMode = ArrangerDragMode::MoveMarker;
                            collectSelectedSnapshots();
                        }
                    }

                    repaint();
                    return;
                }
            }
        }
        else if (trk->getType() == TrackType::TimeSignature)
        {
            for (auto& ts : trk->getTimeSignatures())
            {
                float tsX1 = beatToX(ts.startBeat);
                float tsX2 = tsX1 + 42.0f;
                if ((float)e.x >= tsX1 - 2.0f && (float)e.x <= tsX2 + 2.0f)
                {
                    draggedTrackId = trk->getId();
                    draggedItemId = ts.id;
                    dragStartBeat = clickBeat;
                    itemInitialStartBeat = ts.startBeat;
                    itemInitialLengthBeats = 2.0;
                    altGhostStartBeat = ts.startBeat;
                    altGhostLengthBeats = 2.0;
                    altGhostTrackId = trk->getId();

                    if (e.mods.isAltDown())
                    {
                        if (!ts.isSelected)
                        {
                            trackModel.clearAllArrangerSelection();
                            ts.isSelected = true;
                        }
                        collectSelectedSnapshots();
                        isDuplicatingWithAlt = true;
                        dragMode = ArrangerDragMode::AltDuplicateItem;
                    }
                    else
                    {
                        if (e.mods.isCtrlDown())
                            ts.isSelected = !ts.isSelected;
                        else
                        {
                            trackModel.clearAllTrackSelection();
                            if (!ts.isSelected)
                            {
                                trackModel.clearAllArrangerSelection();
                                ts.isSelected = true;
                            }
                        }

                        dragMode = ArrangerDragMode::MoveTimeSig;
                        collectSelectedSnapshots();
                    }

                    repaint();
                    return;
                }
            }
        }
        else if (trk->getType() == TrackType::Tempo)
        {
            float topY = (float)trackY + 4.0f;
            float bottomY = (float)trackY + (float)trackHeight - 6.0f;
            float laneH = std::max(10.0f, bottomY - topY);

            for (auto& ev : trk->getTempoEvents())
            {
                float nx = beatToX(ev.startBeat);
                float norm = (float)((ev.bpm - 40.0) / (240.0 - 40.0));
                float ny = bottomY - juce::jlimit(0.0f, 1.0f, norm) * laneH;

                if (std::hypot((float)e.x - nx, (float)e.y - ny) <= 12.0f ||
                    ((float)e.x >= nx - 4.0f && (float)e.x <= nx + 36.0f && std::abs((float)e.y - ny) <= 12.0f))
                {
                    draggedTrackId = trk->getId();
                    draggedItemId = ev.id;
                    dragStartBeat = clickBeat;
                    itemInitialStartBeat = ev.startBeat;
                    itemInitialLengthBeats = 1.0;
                    altGhostStartBeat = ev.startBeat;
                    altGhostLengthBeats = 1.0;
                    altGhostTrackId = trk->getId();

                    if (e.mods.isAltDown())
                    {
                        if (!ev.isSelected)
                        {
                            trackModel.clearAllArrangerSelection();
                            ev.isSelected = true;
                        }
                        collectSelectedSnapshots();
                        isDuplicatingWithAlt = true;
                        dragMode = ArrangerDragMode::AltDuplicateItem;
                    }
                    else
                    {
                        if (e.mods.isCtrlDown())
                            ev.isSelected = !ev.isSelected;
                        else
                        {
                            trackModel.clearAllTrackSelection();
                            if (!ev.isSelected)
                            {
                                trackModel.clearAllArrangerSelection();
                                ev.isSelected = true;
                            }
                        }

                        dragMode = ArrangerDragMode::MoveTempoPoint;
                        collectSelectedSnapshots();
                    }

                    repaint();
                    return;
                }
            }
        }

        // Click on EMPTY space: if not Ctrl/Shift, deselect all events and tracks
        if (!e.mods.isCtrlDown() && !e.mods.isShiftDown())
        {
            trackModel.clearAllArrangerSelection();
            trackModel.clearAllTrackSelection();
        }

        // 2. SECOND: Click on EMPTY SPACE with Alt Down -> Draw / Create new item
        if (e.mods.isAltDown())
        {
            int bpb = midiModel.getTimeSigNumerator();
            if (trk->getType() == TrackType::Instrument)
            {
                if (!trackModel.canPlaceEvent(trk->getId(), snapBeat, (double)bpb))
                    return;

                auto oldClips = trk->getClips();
                MidiClip newClip;
                newClip.id = juce::Time::currentTimeMillis();
                newClip.name = "Part " + juce::String(trk->getClips().size() + 1);
                newClip.startBeat = snapBeat;
                newClip.lengthBeats = (double)bpb;
                newClip.color = trk->getColor();

                auto newClips = oldClips;
                newClips.push_back(newClip);

                UndoActionHelper::performAtomic(&undoManager, new ModifyClipsUndoAction(trackModel, trk->getId(), oldClips, newClips, "Add MIDI Part"), "Add MIDI Part");

                trackModel.setActiveClip(trk->getId(), newClip.id, &midiModel);

                isCreatingItemWithAlt = true;
                creatingTrackId = trk->getId();
                creatingItemId = newClip.id;
                creatingStartBeat = snapBeat;
                return;
            }
            else if (trk->getType() == TrackType::Chord)
            {
                double chordLen = (double)bpb;
                double availableLen = chordLen;
                for (const auto& other : trk->getChords())
                {
                    if (other.startBeat >= snapBeat)
                        availableLen = std::min(availableLen, other.startBeat - snapBeat);
                }
                double finalLen = std::min(chordLen, availableLen);
                if (finalLen < 0.25 || !trackModel.canPlaceEvent(trk->getId(), snapBeat, finalLen))
                    return;

                auto oldChords = trk->getChords();
                ChordEvent c;
                c.id = juce::Time::currentTimeMillis();
                c.startBeat = snapBeat;
                c.lengthBeats = finalLen;
                c.chordName = "C";
                c.romanNumeral = "I";
                c.color = juce::Colour(0xfff59e0b);

                auto newChords = oldChords;
                newChords.push_back(c);

                UndoActionHelper::performAtomic(&undoManager, new ModifyChordsUndoAction(trackModel, trk->getId(), oldChords, newChords, "Add Chord Block"), "Add Chord Block");

                isCreatingItemWithAlt = true;
                creatingTrackId = trk->getId();
                creatingItemId = c.id;
                creatingStartBeat = snapBeat;
                return;
            }
            else if (trk->getType() == TrackType::Mode)
            {
                double modeLen = (double)(bpb * 4);
                double availableLen = modeLen;
                for (const auto& other : trk->getModes())
                {
                    if (other.startBeat >= snapBeat)
                        availableLen = std::min(availableLen, other.startBeat - snapBeat);
                }
                if (availableLen < 1.0 || !trackModel.canPlaceEvent(trk->getId(), snapBeat, std::min(modeLen, availableLen)))
                    return;

                auto oldModes = trk->getModes();
                ModeEvent md;
                md.id = juce::Time::currentTimeMillis();
                md.startBeat = snapBeat;
                md.lengthBeats = std::min(modeLen, availableLen);
                md.name = "Section";
                md.tonic = "C";
                md.tonicPc = 0;
                md.scaleName = "Ionian (Natural Major)";
                md.color = juce::Colour(0xff10b981);

                auto newModes = oldModes;
                newModes.push_back(md);

                UndoActionHelper::performAtomic(&undoManager, new ModifyModesUndoAction(trackModel, trk->getId(), oldModes, newModes, "Add Mode Section"), "Add Mode Section");

                isCreatingItemWithAlt = true;
                creatingTrackId = trk->getId();
                creatingItemId = md.id;
                creatingStartBeat = snapBeat;
                return;
            }
            else if (trk->getType() == TrackType::Marker)
            {
                auto oldMarkers = trk->getMarkers();
                MarkerEvent m;
                m.id = juce::String(juce::Time::currentTimeMillis());
                m.startBeat = snapBeat;
                m.lengthBeats = (double)(bpb * 2);
                m.name = "Marker";
                m.color = juce::Colour(0xff0284c7);

                auto newMarkers = oldMarkers;
                newMarkers.push_back(m);

                UndoActionHelper::performAtomic(&undoManager, new ModifyMarkersUndoAction(trackModel, trk->getId(), oldMarkers, newMarkers, "Add Marker"), "Add Marker");

                isCreatingItemWithAlt = true;
                creatingTrackId = trk->getId();
                draggedItemStringId = m.id;
                creatingStartBeat = snapBeat;
                return;
            }
            else if (trk->getType() == TrackType::TimeSignature)
            {
                auto oldSigs = trk->getTimeSignatures();
                TimeSignatureEvent ts;
                ts.id = juce::Time::currentTimeMillis();
                ts.startBeat = snapBeat;
                ts.numerator = midiModel.getTimeSigNumerator();
                ts.denominator = midiModel.getTimeSigDenominator();
                ts.color = juce::Colour(0xff8b5cf6);

                auto newSigs = oldSigs;
                newSigs.push_back(ts);

                UndoActionHelper::performAtomic(&undoManager, new ModifyTimeSignaturesUndoAction(trackModel, trk->getId(), oldSigs, newSigs, "Add Time Signature"), "Add Time Signature");
                return;
            }
            else if (trk->getType() == TrackType::Tempo)
            {
                auto oldEvents = trk->getTempoEvents();
                TempoEvent ev;
                ev.id = juce::Time::currentTimeMillis();
                ev.startBeat = snapBeat;
                float topY = (float)trackY + 4.0f;
                float bottomY = (float)trackY + (float)trackHeight - 6.0f;
                float norm = 1.0f - juce::jlimit(0.0f, 1.0f, ((float)e.y - topY) / std::max(1.0f, bottomY - topY));
                double bpmVal = 40.0 + norm * (240.0 - 40.0);
                ev.bpm = std::round(bpmVal * 10.0) / 10.0;
                ev.type = TempoTransitionType::Jump;
                ev.color = trk->getColor();

                auto newEvents = oldEvents;
                newEvents.push_back(ev);

                UndoActionHelper::performAtomic(&undoManager, new ModifyTempoEventsUndoAction(trackModel, trk->getId(), oldEvents, newEvents, "Add Tempo Point"), "Add Tempo Point");
                return;
            }
        }

        // 3. THIRD: Click on EMPTY SPACE without Alt -> Start Marquee Selection Box
        dragMode = ArrangerDragMode::MarqueeSelect;
        marqueeStartPos = e.getPosition();
        marqueeRect = juce::Rectangle<int>(marqueeStartPos, marqueeStartPos);
        repaint();
    }
    else if (e.y >= rulerHeight)
    {
        // Click on empty space below the tracks
        if (!e.mods.isCtrlDown())
            trackModel.clearAllArrangerSelection();
        dragMode = ArrangerDragMode::MarqueeSelect;
        marqueeStartPos = e.getPosition();
        marqueeRect = juce::Rectangle<int>(marqueeStartPos, marqueeStartPos);
        repaint();
    }
}

//==============================================================================
// Mouse Drag
//==============================================================================
void TrackArrangerComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (dragMode == ArrangerDragMode::MoveAudioClip)
    {
        if (auto* track = trackModel.getTrackById(draggedTrackId))
        {
            double delta = xToBeat((float)e.x) - dragStartBeat;
            if (midiModel.isSnapEnabled() && !e.mods.isCtrlDown())
            {
                const double interval = midiModel.getSnapBeatInterval();
                delta = snapDraggedItemDelta(itemInitialStartBeat, delta, interval);
            }
            for (auto& clip : track->getAudioClips())
                if (clip.id == draggedItemId)
                {
                    clip.startBeat = std::max(0.0, itemInitialStartBeat + delta);
                    break;
                }
            repaint();
        }
        return;
    }
    // Middle Mouse Button Pan
    if (dragMode == ArrangerDragMode::MiddlePan)
    {
        auto curPos = e.getScreenPosition();
        int deltaX = curPos.x - dragMouseDownPos.x;
        int deltaY = curPos.y - dragMouseDownPos.y;
        dragMouseDownPos = curPos;

        double beatDelta = -(double)deltaX / (double)pixelsPerBeat;
        scrollBeats = std::max(0.0, scrollBeats + beatDelta);
        updateScrollBar();

        int totalTrackHeight = (int)trackModel.getVisibleTracks().size() * trackHeight;
        int maxScrollY = std::max(0, totalTrackHeight - (getHeight() - rulerHeight - 40));
        scrollTrackY = std::clamp(scrollTrackY - deltaY, 0, maxScrollY);
        repaint();
        return;
    }

    const int scrollbarH = 16;
    if (dragMode == ArrangerDragMode::None && e.y >= getHeight() - scrollbarH && e.x >= headerWidth)
        return;
    // 0. Ruler Scrub Playhead & Locator Dragging
    if (dragMode == ArrangerDragMode::ScrubPlayhead)
    {
        lastRulerMousePos = e.getPosition();
        double b = xToBeat((float)e.x);
        double qb = midiModel.isSnapEnabled() ? midiModel.quantizeBeat(b) : b;
        audioEngine.setPlayheadBeat(std::max(0.0, qb));

        const int edgeMargin = 36;
        int leftThreshold = headerWidth + edgeMargin;
        int rightThreshold = getWidth() - edgeMargin;
        bool inEdgeZone = (e.x < leftThreshold || e.x > rightThreshold);
        if (inEdgeZone)
        {
            if (!isTimerRunning())
                startTimerHz(60);
        }
        else
        {
            if (isTimerRunning())
                stopTimer();
        }

        repaint();
        return;
    }
    if (dragMode == ArrangerDragMode::RulerLeftLocator || dragMode == ArrangerDragMode::RulerRightLocator)
    {
        double currentBeat = std::max(0.0, xToBeat((float)e.x));
        double qBeat = midiModel.isSnapEnabled() ? midiModel.quantizeBeat(currentBeat) : currentBeat;
        double newStart = std::max(0.0, std::min(qBeat, stationaryLocatorBeat));
        double newEnd = std::max(0.0, std::max(qBeat, stationaryLocatorBeat));
        midiModel.setLoopRange(newStart, newEnd);
        repaint();
        return;
    }

    // 1. Reordering tracks
    if (dragMode == ArrangerDragMode::ReorderTrack)
    {
        dragTrackMouseY = e.y;
        int targetIdx = (e.y - rulerHeight + scrollTrackY + trackHeight / 2) / trackHeight;
        targetIdx = std::clamp(targetIdx, 0, (int)trackModel.getVisibleTracks().size());
        if (targetIdx != dragTrackTargetIndex)
        {
            dragTrackTargetIndex = targetIdx;
            repaint();
        }
        return;
    }

    // 2. Alt + Drag item expansion during creation
    if (isCreatingItemWithAlt && creatingTrackId >= 0)
    {
        double currentBeat = xToBeat((float)e.x);
        double snapInterval = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.0;
        double minLen = (midiModel.isSnapEnabled() && snapInterval > 0.0) ? snapInterval : 1.0;
        double len = std::max(minLen, currentBeat - creatingStartBeat);
        if (midiModel.isSnapEnabled() && snapInterval > 0.0)
            len = std::max(minLen, std::ceil(len / snapInterval) * snapInterval);

        if (auto* trk = trackModel.getTrackById(creatingTrackId))
        {
            double nextStart = 1e9;
            if (trk->getType() == TrackType::Instrument)
            {
                for (const auto& clip : trk->getClips())
                    if (clip.id != creatingItemId && clip.startBeat >= creatingStartBeat)
                        nextStart = std::min(nextStart, clip.startBeat);
                if (nextStart < 1e8) len = std::min(len, std::max(minLen, nextStart - creatingStartBeat));
                for (auto& clip : trk->getClips())
                    if (clip.id == creatingItemId) { clip.lengthBeats = len; break; }
            }
            else if (trk->getType() == TrackType::Chord)
            {
                for (const auto& c : trk->getChords())
                    if (c.id != creatingItemId && c.startBeat >= creatingStartBeat)
                        nextStart = std::min(nextStart, c.startBeat);
                if (nextStart < 1e8) len = std::min(len, std::max(minLen, nextStart - creatingStartBeat));
                for (auto& c : trk->getChords())
                    if (c.id == creatingItemId) { c.lengthBeats = len; break; }
            }
            else if (trk->getType() == TrackType::Mode)
            {
                for (const auto& md : trk->getModes())
                    if (md.id != creatingItemId && md.startBeat >= creatingStartBeat)
                        nextStart = std::min(nextStart, md.startBeat);
                if (nextStart < 1e8) len = std::min(len, std::max(minLen, nextStart - creatingStartBeat));
                for (auto& md : trk->getModes())
                    if (md.id == creatingItemId) { md.lengthBeats = len; break; }
            }
            else if (trk->getType() == TrackType::Marker)
            {
                for (auto& m : trk->getMarkers())
                    if (m.id == draggedItemStringId) { m.lengthBeats = len; break; }
            }
            repaint();
        }
        return;
    }

    // 2b. Marquee Selection Box (must execute without requiring draggedTrackId)
    if (dragMode == ArrangerDragMode::MarqueeSelect)
    {
        int mx1 = marqueeStartPos.x;
        int my1 = marqueeStartPos.y;
        int mx2 = e.getPosition().x;
        int my2 = e.getPosition().y;
        marqueeRect = juce::Rectangle<int>::leftTopRightBottom(std::min(mx1, mx2), std::min(my1, my2), std::max(mx1, mx2), std::max(my1, my2));
        int y = rulerHeight - scrollTrackY;
        for (auto& t : trackModel.getTracks())
        {
            if (t->getType() == TrackType::Audio)
            {
                for (auto& clip : t->getAudioClips())
                {
                    const float x1 = beatToX(clip.startBeat);
                    const float x2 = beatToX(clip.startBeat + clip.lengthBeats);
                    auto rect = juce::Rectangle<float>(x1, (float)y, x2 - x1, (float)trackHeight).toNearestInt();
                    clip.isSelected = rect.intersects(marqueeRect);
                }
            }
            else if (t->getType() == TrackType::Instrument)
            {
                for (auto& c : t->getClips())
                {
                    float x1 = beatToX(c.startBeat);
                    float x2 = beatToX(c.startBeat + c.lengthBeats);
                    auto r = juce::Rectangle<float>(x1, (float)y, x2 - x1, (float)trackHeight).toNearestInt();
                    c.isSelected = r.intersects(marqueeRect);
                }
            }
            else if (t->getType() == TrackType::Chord)
            {
                for (auto& c : t->getChords())
                {
                    float x1 = beatToX(c.startBeat);
                    float x2 = beatToX(c.startBeat + c.lengthBeats);
                    auto r = juce::Rectangle<float>(x1, (float)y, x2 - x1, (float)trackHeight).toNearestInt();
                    c.isSelected = r.intersects(marqueeRect);
                }
            }
            else if (t->getType() == TrackType::Mode)
            {
                for (auto& m : t->getModes())
                {
                    float x1 = beatToX(m.startBeat);
                    float x2 = beatToX(m.startBeat + m.lengthBeats);
                    auto r = juce::Rectangle<float>(x1, (float)y, x2 - x1, (float)trackHeight).toNearestInt();
                    m.isSelected = r.intersects(marqueeRect);
                }
            }
            else if (t->getType() == TrackType::Marker)
            {
                for (auto& m : t->getMarkers())
                {
                    float x1 = beatToX(m.startBeat);
                    float x2 = beatToX(m.startBeat + m.lengthBeats);
                    auto r = juce::Rectangle<float>(x1, (float)y, x2 - x1, (float)trackHeight).toNearestInt();
                    m.isSelected = r.intersects(marqueeRect);
                }
            }
            else if (t->getType() == TrackType::TimeSignature)
            {
                for (auto& ts : t->getTimeSignatures())
                {
                    float x1 = beatToX(ts.startBeat);
                    auto r = juce::Rectangle<float>(x1, (float)y, 54.0f, (float)trackHeight).toNearestInt();
                    ts.isSelected = r.intersects(marqueeRect);
                }
            }
            y += trackHeight;
        }
        repaint();
        return;
    }

    // 3. Move / Resize items with live preview
    double currentBeat = xToBeat((float)e.x);
    double snapInterval = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.0;
    double delta = currentBeat - dragStartBeat;
    bool isMovingAny = (dragMode == ArrangerDragMode::MoveClip ||
                        dragMode == ArrangerDragMode::MoveChord ||
                        dragMode == ArrangerDragMode::MoveMode ||
                        dragMode == ArrangerDragMode::MoveMarker ||
                        dragMode == ArrangerDragMode::MoveTimeSig);
    if (e.mods.isCtrlDown())
    {
        // Requirement 8: Ctrl down bypasses snapping for smooth micro-movement in Arranger
    }
    else if (midiModel.isSnapEnabled() && snapInterval > 0.0)
    {
        delta = (isMovingAny || dragMode == ArrangerDragMode::MoveTempoPoint)
            ? snapDraggedItemDelta(itemInitialStartBeat, delta, snapInterval)
            : std::round(delta / snapInterval) * snapInterval;
    }

    auto* trk = trackModel.getTrackById(draggedTrackId);
    if (!trk) return;

    if (isMovingAny)
    {
        // Detect target track under cursor for cross-track dragging
        int hoverIdx = (e.y - rulerHeight + scrollTrackY) / trackHeight;
        auto visTracks = trackModel.getVisibleTracks();
        if (hoverIdx >= 0 && hoverIdx < (int)visTracks.size())
        {
            auto* hoverTrk = visTracks[hoverIdx];
            if (hoverTrk->getType() == trk->getType())
            {
                dragTargetTrackId = hoverTrk->getId();
            }
        }

        if (!selectedDragSnapshots.empty())
        {
            double minDelta = -1e9;
            double maxDelta = 1e9;

            // 1. Clamp minDelta so no selected item moves before beat 0
            for (const auto& s : selectedDragSnapshots)
            {
                minDelta = std::max(minDelta, -s.initialStartBeat);
            }

            // 2. Prevent collision with unselected items on Chord and Mode tracks
            for (const auto& s : selectedDragSnapshots)
            {
                if (s.trackType == TrackType::Chord)
                {
                    if (auto* t = trackModel.getTrackById(s.trackId))
                    {
                        for (const auto& other : t->getChords())
                        {
                            bool isOtherSelected = false;
                            for (const auto& snap2 : selectedDragSnapshots)
                            {
                                if (snap2.trackId == s.trackId && snap2.intId == other.id)
                                {
                                    isOtherSelected = true;
                                    break;
                                }
                            }
                            if (isOtherSelected) continue;

                            if (other.startBeat + other.lengthBeats <= s.initialStartBeat + 0.001)
                            {
                                minDelta = std::max(minDelta, (other.startBeat + other.lengthBeats) - s.initialStartBeat);
                            }
                            else if (other.startBeat >= s.initialStartBeat + s.initialLengthBeats - 0.001)
                            {
                                maxDelta = std::min(maxDelta, other.startBeat - (s.initialStartBeat + s.initialLengthBeats));
                            }
                        }
                    }
                }
                else if (s.trackType == TrackType::Mode)
                {
                    if (auto* t = trackModel.getTrackById(s.trackId))
                    {
                        for (const auto& other : t->getModes())
                        {
                            bool isOtherSelected = false;
                            for (const auto& snap2 : selectedDragSnapshots)
                            {
                                if (snap2.trackId == s.trackId && snap2.intId == other.id)
                                {
                                    isOtherSelected = true;
                                    break;
                                }
                            }
                            if (isOtherSelected) continue;

                            if (other.startBeat + other.lengthBeats <= s.initialStartBeat + 0.001)
                            {
                                minDelta = std::max(minDelta, (other.startBeat + other.lengthBeats) - s.initialStartBeat);
                            }
                            else if (other.startBeat >= s.initialStartBeat + s.initialLengthBeats - 0.001)
                            {
                                maxDelta = std::min(maxDelta, other.startBeat - (s.initialStartBeat + s.initialLengthBeats));
                            }
                        }
                    }
                }
            }

            delta = clampDraggedItemDelta(itemInitialStartBeat, delta, minDelta, maxDelta,
                midiModel.isSnapEnabled() && !e.mods.isCtrlDown() ? snapInterval : 0.0);

            for (const auto& s : selectedDragSnapshots)
            {
                auto* t = trackModel.getTrackById(s.trackId);
                if (!t) continue;
                double newStart = std::max(0.0, s.initialStartBeat + delta);

                if (s.trackType == TrackType::Instrument)
                {
                    for (auto& clip : t->getClips())
                        if (clip.id == s.intId) { clip.startBeat = newStart; break; }
                }
                else if (s.trackType == TrackType::Chord)
                {
                    for (auto& c : t->getChords())
                        if (c.id == s.intId) { c.startBeat = newStart; break; }
                }
                else if (s.trackType == TrackType::Mode)
                {
                    for (auto& md : t->getModes())
                        if (md.id == s.intId) { md.startBeat = newStart; break; }
                }
                else if (s.trackType == TrackType::Marker)
                {
                    for (auto& m : t->getMarkers())
                        if (m.id == s.stringId) { m.startBeat = newStart; break; }
                }
                else if (s.trackType == TrackType::TimeSignature)
                {
                    for (auto& ts : t->getTimeSignatures())
                        if (ts.id == s.intId) { ts.startBeat = newStart; break; }
                }
                else if (s.trackType == TrackType::Tempo)
                {
                    for (auto& ev : t->getTempoEvents())
                        if (ev.id == s.intId) { ev.startBeat = newStart; break; }
                }
            }
        }
        else
        {
            if (dragMode == ArrangerDragMode::MoveClip)
            {
                for (auto& clip : trk->getClips())
                    if (clip.id == draggedItemId) { clip.startBeat = std::max(0.0, itemInitialStartBeat + delta); break; }
            }
            else if (dragMode == ArrangerDragMode::MoveChord)
            {
                double prevEnd = 0.0;
                double nextStart = 1e9;
                for (const auto& other : trk->getChords())
                {
                    if (other.id == draggedItemId) continue;
                    if (other.startBeat + other.lengthBeats <= itemInitialStartBeat + 0.001)
                        prevEnd = std::max(prevEnd, other.startBeat + other.lengthBeats);
                    else if (other.startBeat >= itemInitialStartBeat - 0.001)
                        nextStart = std::min(nextStart, other.startBeat);
                }
                for (auto& c : trk->getChords())
                {
                    if (c.id == draggedItemId)
                    {
                        double maxStart = (nextStart < 1e8) ? (nextStart - c.lengthBeats) : 1e9;
                        c.startBeat = itemInitialStartBeat + clampDraggedItemDelta(
                            itemInitialStartBeat, delta, prevEnd - itemInitialStartBeat,
                            maxStart - itemInitialStartBeat,
                            midiModel.isSnapEnabled() && !e.mods.isCtrlDown() ? snapInterval : 0.0);
                        break;
                    }
                }
            }
            else if (dragMode == ArrangerDragMode::MoveMode)
            {
                double prevEnd = 0.0;
                double nextStart = 1e9;
                for (const auto& other : trk->getModes())
                {
                    if (other.id == draggedItemId) continue;
                    if (other.startBeat + other.lengthBeats <= itemInitialStartBeat + 0.001)
                        prevEnd = std::max(prevEnd, other.startBeat + other.lengthBeats);
                    else if (other.startBeat >= itemInitialStartBeat - 0.001)
                        nextStart = std::min(nextStart, other.startBeat);
                }
                for (auto& md : trk->getModes())
                {
                    if (md.id == draggedItemId)
                    {
                        double maxStart = (nextStart < 1e8) ? (nextStart - md.lengthBeats) : 1e9;
                        md.startBeat = itemInitialStartBeat + clampDraggedItemDelta(
                            itemInitialStartBeat, delta, prevEnd - itemInitialStartBeat,
                            maxStart - itemInitialStartBeat,
                            midiModel.isSnapEnabled() && !e.mods.isCtrlDown() ? snapInterval : 0.0);
                        break;
                    }
                }
            }
            else if (dragMode == ArrangerDragMode::MoveMarker)
            {
                for (auto& m : trk->getMarkers())
                    if (m.id == draggedItemStringId) { m.startBeat = std::max(0.0, itemInitialStartBeat + delta); break; }
            }
            else if (dragMode == ArrangerDragMode::MoveTimeSig)
            {
                for (auto& ts : trk->getTimeSignatures())
                    if (ts.id == draggedItemId) { ts.startBeat = std::max(0.0, itemInitialStartBeat + delta); break; }
            }
            else if (dragMode == ArrangerDragMode::MoveTempoPoint)
            {
                int trackY = rulerHeight - scrollTrackY;
                for (const auto* t : trackModel.getVisibleTracks())
                {
                    if (t->getId() == draggedTrackId) break;
                    trackY += trackHeight;
                }
                float topY = (float)trackY + 4.0f;
                float bottomY = (float)trackY + (float)trackHeight - 6.0f;
                float norm = 1.0f - juce::jlimit(0.0f, 1.0f, ((float)e.y - topY) / std::max(1.0f, bottomY - topY));
                double newBpm = std::round((40.0 + norm * (240.0 - 40.0)) * 10.0) / 10.0;

                for (auto& ev : trk->getTempoEvents())
                {
                    if (ev.id == draggedItemId)
                    {
                        ev.startBeat = std::max(0.0, itemInitialStartBeat + delta);
                        ev.bpm = newBpm;
                        break;
                    }
                }
            }
        }
        repaint();
        return;
    }
    else if (dragMode == ArrangerDragMode::ResizeClipLeft)
    {
        double minLen = (midiModel.isSnapEnabled() && snapInterval > 0.0) ? snapInterval : 0.25;
        for (auto& clip : trk->getClips())
        {
            if (clip.id == draggedItemId)
            {
                double newStart = std::clamp(itemInitialStartBeat + delta, 0.0, itemInitialStartBeat + itemInitialLengthBeats - minLen);
                clip.lengthBeats = (itemInitialStartBeat + itemInitialLengthBeats) - newStart;
                clip.startBeat = newStart;
                break;
            }
        }
        repaint();
    }
    else if (dragMode == ArrangerDragMode::ResizeClipRight)
    {
        double minLen = (midiModel.isSnapEnabled() && snapInterval > 0.0) ? snapInterval : 0.25;
        for (auto& clip : trk->getClips())
        {
            if (clip.id == draggedItemId)
            {
                clip.lengthBeats = std::max(minLen, itemInitialLengthBeats + delta);
                break;
            }
        }
        repaint();
    }
    else if (dragMode == ArrangerDragMode::ResizeChordLeft)
    {
        double minLen = (midiModel.isSnapEnabled() && snapInterval > 0.0) ? snapInterval : 0.25;
        double prevEnd = 0.0;
        for (const auto& other : trk->getChords())
        {
            if (other.id == draggedItemId) continue;
            if (other.startBeat < itemInitialStartBeat - 0.001)
                prevEnd = std::max(prevEnd, other.startBeat + other.lengthBeats);
        }
        for (auto& c : trk->getChords())
        {
            if (c.id == draggedItemId)
            {
                double targetStart = itemInitialStartBeat + delta;
                double newStart = std::clamp(targetStart, prevEnd, itemInitialStartBeat + itemInitialLengthBeats - minLen);
                c.lengthBeats = (itemInitialStartBeat + itemInitialLengthBeats) - newStart;
                c.startBeat = newStart;
                break;
            }
        }
        repaint();
    }
    else if (dragMode == ArrangerDragMode::ResizeChordRight)
    {
        double minLen = (midiModel.isSnapEnabled() && snapInterval > 0.0) ? snapInterval : 0.25;
        double nextStart = 1e9;
        for (const auto& other : trk->getChords())
        {
            if (other.id == draggedItemId) continue;
            if (other.startBeat >= itemInitialStartBeat - 0.001)
                nextStart = std::min(nextStart, other.startBeat);
        }
        for (auto& c : trk->getChords())
        {
            if (c.id == draggedItemId)
            {
                double targetLen = itemInitialLengthBeats + delta;
                double maxLen = (nextStart < 1e8) ? (nextStart - c.startBeat) : 1e9;
                c.lengthBeats = std::clamp(targetLen, minLen, std::max(minLen, maxLen));
                break;
            }
        }
        repaint();
    }
    else if (dragMode == ArrangerDragMode::ResizeModeLeft)
    {
        double minLen = (midiModel.isSnapEnabled() && snapInterval > 0.0) ? snapInterval : 0.25;
        double prevEnd = 0.0;
        for (const auto& other : trk->getModes())
        {
            if (other.id == draggedItemId) continue;
            if (other.startBeat < itemInitialStartBeat - 0.001)
                prevEnd = std::max(prevEnd, other.startBeat + other.lengthBeats);
        }
        for (auto& md : trk->getModes())
        {
            if (md.id == draggedItemId)
            {
                double targetStart = itemInitialStartBeat + delta;
                double newStart = std::clamp(targetStart, prevEnd, itemInitialStartBeat + itemInitialLengthBeats - minLen);
                md.lengthBeats = (itemInitialStartBeat + itemInitialLengthBeats) - newStart;
                md.startBeat = newStart;
                break;
            }
        }
        repaint();
    }
    else if (dragMode == ArrangerDragMode::ResizeModeRight)
    {
        double minLen = (midiModel.isSnapEnabled() && snapInterval > 0.0) ? snapInterval : 0.25;
        double nextStart = 1e9;
        for (const auto& other : trk->getModes())
        {
            if (other.id == draggedItemId) continue;
            if (other.startBeat >= itemInitialStartBeat - 0.001)
                nextStart = std::min(nextStart, other.startBeat);
        }
        for (auto& md : trk->getModes())
        {
            if (md.id == draggedItemId)
            {
                double targetLen = itemInitialLengthBeats + delta;
                double maxLen = (nextStart < 1e8) ? (nextStart - md.startBeat) : 1e9;
                md.lengthBeats = std::clamp(targetLen, minLen, std::max(minLen, maxLen));
                break;
            }
        }
        repaint();
    }
    else if (dragMode == ArrangerDragMode::ResizeMarkerLeft)
    {
        double minLen = (midiModel.isSnapEnabled() && snapInterval > 0.0) ? snapInterval : 0.25;
        for (auto& m : trk->getMarkers())
        {
            if (m.id == draggedItemStringId)
            {
                double newStart = std::clamp(itemInitialStartBeat + delta, 0.0, itemInitialStartBeat + itemInitialLengthBeats - minLen);
                m.lengthBeats = (itemInitialStartBeat + itemInitialLengthBeats) - newStart;
                m.startBeat = newStart;
                break;
            }
        }
        repaint();
    }
    else if (dragMode == ArrangerDragMode::ResizeMarkerRight)
    {
        double minLen = (midiModel.isSnapEnabled() && snapInterval > 0.0) ? snapInterval : 0.25;
        for (auto& m : trk->getMarkers())
        {
            if (m.id == draggedItemStringId)
            {
                m.lengthBeats = std::max(minLen, itemInitialLengthBeats + delta);
                break;
            }
        }
        repaint();
    }
    else if (dragMode == ArrangerDragMode::AltDuplicateItem)
    {
        // Only move ghost preview box, original item remains completely stationary!
        double currentBeat = xToBeat((float)e.x);
        double delta = currentBeat - dragStartBeat;
        if (!e.mods.isCtrlDown() && midiModel.isSnapEnabled())
        {
            double snapInt = midiModel.getSnapBeatInterval();
            if (snapInt > 0.0) delta = snapDraggedItemDelta(itemInitialStartBeat, delta, snapInt);
            else delta = std::round(delta);
        }
        altGhostStartBeat = std::max(0.0, itemInitialStartBeat + delta);
        repaint();
    }
}

//==============================================================================
// Mouse Up (Commit actions to UndoManager)
//==============================================================================
void TrackArrangerComponent::mouseUp(const juce::MouseEvent& e)
{
    if (dragMode == ArrangerDragMode::MoveAudioClip)
    {
        if (auto* track = trackModel.getTrackById(draggedTrackId))
        {
            auto after = track->getAudioClips();
            auto before = after;
            bool moved = false;
            for (auto& clip : before)
                if (clip.id == draggedItemId)
                {
                    moved = std::abs(clip.startBeat - itemInitialStartBeat) > 0.0001;
                    clip.startBeat = itemInitialStartBeat;
                    break;
                }
            if (moved)
                UndoActionHelper::performAtomic(&undoManager,
                    new ModifyAudioClipsUndoAction(trackModel, draggedTrackId, before, after),
                    "Move Audio Clip");
        }
        dragMode = ArrangerDragMode::None;
        draggedTrackId = -1;
        draggedItemId = -1;
        repaint();
        return;
    }
    if (dragMode == ArrangerDragMode::MiddlePan)
    {
        dragMode = ArrangerDragMode::None;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    if (dragMode == ArrangerDragMode::ScrubPlayhead)
    {
        stopTimer();
        dragMode = ArrangerDragMode::None;
        if (wasPlayingBeforeDrag)
        {
            wasPlayingBeforeDrag = false;
            audioEngine.play();
        }
        repaint();
        return;
    }

    if (dragMode == ArrangerDragMode::RulerLeftLocator ||
        dragMode == ArrangerDragMode::RulerRightLocator)
    {
        dragMode = ArrangerDragMode::None;
        repaint();
        return;
    }

    if (dragMode == ArrangerDragMode::ReorderTrack && dragTrackSourceIndex >= 0 && dragTrackTargetIndex >= 0)
    {
        auto visTracks = trackModel.getVisibleTracks();
        if (dragTrackSourceIndex >= 0 && dragTrackSourceIndex < (int)visTracks.size())
        {
            auto* srcTrk = visTracks[dragTrackSourceIndex];
            int src = trackModel.getTrackIndex(srcTrk->getId());
            int oldParent = srcTrk->getParentFolderId();
            int newParent = oldParent;
            int dst = src;
            bool validDrop = true;

            if (dragTrackTargetIndex >= 0 && dragTrackTargetIndex < (int)visTracks.size())
            {
                auto* targetTrk = visTracks[dragTrackTargetIndex];
                int targetPhysIdx = trackModel.getTrackIndex(targetTrk->getId());

                if (targetTrk->getId() == srcTrk->getId())
                {
                    validDrop = false;
                }
                else if (targetTrk->getType() == TrackType::Folder)
                {
                    // Dropped onto folder track -> adopt into folder (up to 3 levels), ALWAYS placed directly beneath folder header!
                    validDrop = trackModel.canNestInFolder(srcTrk->getId(), targetTrk->getId());
                    if (validDrop)
                        newParent = targetTrk->getId();

                    dst = (src < targetPhysIdx) ? targetPhysIdx : (targetPhysIdx + 1);
                }
                else if (targetTrk->getParentFolderId() > 0 && e.x >= 35)
                {
                    // Dropped onto a child inside a folder
                    int candParent = targetTrk->getParentFolderId();
                    validDrop = trackModel.canNestInFolder(srcTrk->getId(), candParent);
                    if (validDrop)
                        newParent = candParent;

                    dst = (src < targetPhysIdx) ? targetPhysIdx : (targetPhysIdx + 1);
                }
                else
                {
                    // Dropped on a root-level track or dragged to left edge -> root level, remove from folder!
                    newParent = -1;
                    dst = targetPhysIdx;
                    if (dst > src)
                        dst--;
                }
            }
            else
            {
                // Dropped beyond the bottom -> move to end of tracks list at root level
                newParent = -1;
                dst = (int)trackModel.getTracks().size() - 1;
            }

            dst = std::clamp(dst, 0, (int)trackModel.getTracks().size() - 1);

            if (validDrop && (src != dst || oldParent != newParent))
            {
                UndoActionHelper::performAtomic(&undoManager, new MoveTrackUndoAction(trackModel, src, dst, srcTrk->getId(), oldParent, newParent), "Reorder Track");
            }
        }
        dragMode = ArrangerDragMode::None;
        dragTrackSourceIndex = -1;
        dragTrackTargetIndex = -1;
        repaint();
    }
    else if (dragMode == ArrangerDragMode::AltDuplicateItem && draggedTrackId >= 0)
    {
        const int dragDistance = (e.getPosition() - dragMouseDownPos).getDistanceFromOrigin();
        double shift = altGhostStartBeat - itemInitialStartBeat;

        if (dragDistance < 6)
        {
            if (auto* track = trackModel.getTrackById(draggedTrackId))
                splitEventAt(*track, dragStartBeat, draggedItemId, draggedItemStringId);
        }
        else if (std::abs(shift) >= 0.25)
        {
            trackModel.clearAllArrangerSelection();

            if (selectedDragSnapshots.empty())
            {
                auto* trk = trackModel.getTrackById(draggedTrackId);
                if (trk)
                    selectedDragSnapshots.push_back({ trk->getType(), trk->getId(), draggedItemId, draggedItemStringId, itemInitialStartBeat, altGhostLengthBeats });
            }

            auto compoundAction = std::make_unique<CompoundUndoAction>("Duplicate Selected Arranger Events");
            std::set<int> processedAudioTracks;

            for (const auto& s : selectedDragSnapshots)
            {
                auto* t = trackModel.getTrackById(s.trackId);
                if (!t) continue;
                double newStart = std::max(0.0, s.initialStartBeat + shift);

                if (s.trackType == TrackType::Audio)
                {
                    if (!processedAudioTracks.insert(t->getId()).second) continue;
                    const auto beforeClips = t->getAudioClips();
                    auto afterClips = beforeClips;
                    int64_t nextId = juce::Time::currentTimeMillis();
                    for (const auto& clip : beforeClips)
                        nextId = std::max(nextId, clip.id + 1);
                    for (auto& clip : afterClips) clip.isSelected = false;
                    for (const auto& selected : selectedDragSnapshots)
                    {
                        if (selected.trackType != TrackType::Audio || selected.trackId != t->getId())
                            continue;
                        auto source = std::find_if(beforeClips.begin(), beforeClips.end(),
                            [&](const AudioClip& clip) { return clip.id == selected.intId; });
                        if (source == beforeClips.end()) continue;
                        AudioClip clone = *source;
                        clone.id = nextId++;
                        clone.startBeat = std::max(0.0, selected.initialStartBeat + shift);
                        clone.isSelected = true;
                        afterClips.push_back(std::move(clone));
                    }
                    if (afterClips.size() > beforeClips.size())
                        compoundAction->addAction(std::make_unique<ModifyAudioClipsUndoAction>(
                            trackModel, t->getId(), beforeClips, afterClips));
                }
                else if (s.trackType == TrackType::Instrument)
                {
                    auto beforeClips = t->getClips();
                    MidiClip cloneClip;
                    bool found = false;
                    for (const auto& c : beforeClips)
                    {
                        if (c.id == s.intId) { cloneClip = c; found = true; break; }
                    }
                    if (found)
                    {
                        cloneClip.id = juce::Time::currentTimeMillis() + (int64_t)std::rand();
                        cloneClip.name = cloneClip.name + " (Copy)";
                        cloneClip.startBeat = newStart;
                        cloneClip.isSelected = true;
                        double noteShift = newStart - s.initialStartBeat;
                        for (auto& n : cloneClip.notes)
                            n.startBeat += noteShift;

                        auto afterClips = beforeClips;
                        for (auto& oldC : afterClips) oldC.isSelected = false;
                        afterClips.push_back(cloneClip);
                        compoundAction->addAction(std::make_unique<ModifyClipsUndoAction>(trackModel, t->getId(), beforeClips, afterClips, "Duplicate MIDI Part"));
                    }
                }
                else if (s.trackType == TrackType::Chord)
                {
                    auto beforeChords = t->getChords();
                    ChordEvent cloneChord;
                    bool found = false;
                    for (const auto& c : beforeChords)
                    {
                        if (c.id == s.intId) { cloneChord = c; found = true; break; }
                    }
                    if (found)
                    {
                        if (!trackModel.canPlaceEvent(t->getId(), newStart, cloneChord.lengthBeats))
                            continue;

                        cloneChord.id = juce::Time::currentTimeMillis() + (int64_t)std::rand();
                        cloneChord.startBeat = newStart;
                        cloneChord.isSelected = true;

                        auto afterChords = beforeChords;
                        for (auto& oldC : afterChords) oldC.isSelected = false;
                        afterChords.push_back(cloneChord);
                        compoundAction->addAction(std::make_unique<ModifyChordsUndoAction>(trackModel, t->getId(), beforeChords, afterChords, "Duplicate Chord Block"));
                    }
                }
                else if (s.trackType == TrackType::Mode)
                {
                    auto beforeModes = t->getModes();
                    ModeEvent cloneMode;
                    bool found = false;
                    for (const auto& m : beforeModes)
                    {
                        if (m.id == s.intId) { cloneMode = m; found = true; break; }
                    }
                    if (found)
                    {
                        if (!trackModel.canPlaceEvent(t->getId(), newStart, cloneMode.lengthBeats))
                            continue;

                        cloneMode.id = juce::Time::currentTimeMillis() + (int64_t)std::rand();
                        cloneMode.startBeat = newStart;
                        cloneMode.isSelected = true;

                        auto afterModes = beforeModes;
                        for (auto& oldM : afterModes) oldM.isSelected = false;
                        afterModes.push_back(cloneMode);
                        compoundAction->addAction(std::make_unique<ModifyModesUndoAction>(trackModel, t->getId(), beforeModes, afterModes, "Duplicate Mode Section"));
                    }
                }
                else if (s.trackType == TrackType::Marker)
                {
                    auto beforeMarkers = t->getMarkers();
                    MarkerEvent cloneMarker;
                    bool found = false;
                    for (const auto& m : beforeMarkers)
                    {
                        if (m.id == s.stringId) { cloneMarker = m; found = true; break; }
                    }
                    if (found)
                    {
                        cloneMarker.id = juce::String(juce::Time::currentTimeMillis() + (int64_t)std::rand());
                        cloneMarker.startBeat = newStart;
                        cloneMarker.isSelected = true;

                        auto afterMarkers = beforeMarkers;
                        for (auto& oldMk : afterMarkers) oldMk.isSelected = false;
                        afterMarkers.push_back(cloneMarker);
                        compoundAction->addAction(std::make_unique<ModifyMarkersUndoAction>(trackModel, t->getId(), beforeMarkers, afterMarkers, "Duplicate Marker"));
                    }
                }
                else if (s.trackType == TrackType::TimeSignature)
                {
                    auto beforeSigs = t->getTimeSignatures();
                    TimeSignatureEvent cloneSig;
                    bool found = false;
                    for (const auto& ts : beforeSigs)
                    {
                        if (ts.id == s.intId) { cloneSig = ts; found = true; break; }
                    }
                    if (found)
                    {
                        cloneSig.id = juce::Time::currentTimeMillis() + (int64_t)std::rand();
                        cloneSig.startBeat = newStart;
                        cloneSig.isSelected = true;

                        auto afterSigs = beforeSigs;
                        for (auto& oldSig : afterSigs) oldSig.isSelected = false;
                        afterSigs.push_back(cloneSig);
                        compoundAction->addAction(std::make_unique<ModifyTimeSignaturesUndoAction>(trackModel, t->getId(), beforeSigs, afterSigs, "Duplicate Time Signature"));
                    }
                }
                else if (s.trackType == TrackType::Tempo)
                {
                    auto beforeEvents = t->getTempoEvents();
                    TempoEvent cloneEv;
                    bool found = false;
                    for (const auto& ev : beforeEvents)
                    {
                        if (ev.id == s.intId) { cloneEv = ev; found = true; break; }
                    }
                    if (found)
                    {
                        cloneEv.id = juce::Time::currentTimeMillis() + (int64_t)std::rand();
                        cloneEv.startBeat = newStart;
                        cloneEv.isSelected = true;

                        auto afterEvents = beforeEvents;
                        for (auto& oldEv : afterEvents) oldEv.isSelected = false;
                        afterEvents.push_back(cloneEv);
                        compoundAction->addAction(std::make_unique<ModifyTempoEventsUndoAction>(trackModel, t->getId(), beforeEvents, afterEvents, "Duplicate Tempo Point"));
                    }
                }
            }

            if (!compoundAction->isEmpty())
            {
                UndoActionHelper::performAtomic(&undoManager, compoundAction.release(), "Duplicate Selected Arranger Events");
            }
        }
    }
    else if (draggedTrackId >= 0 && dragMode != ArrangerDragMode::None)
    {
        bool isMovingAny = (dragMode == ArrangerDragMode::MoveClip ||
                            dragMode == ArrangerDragMode::MoveChord ||
                            dragMode == ArrangerDragMode::MoveMode ||
                            dragMode == ArrangerDragMode::MoveMarker ||
                            dragMode == ArrangerDragMode::MoveTimeSig ||
                            dragMode == ArrangerDragMode::MoveTempoPoint);

        if (isMovingAny && !selectedDragSnapshots.empty())
        {
            // A. Cross-Track Drag & Drop between same-type tracks
            if (dragTargetTrackId >= 0 && dragTargetTrackId != draggedTrackId)
            {
                auto* srcTrk = trackModel.getTrackById(draggedTrackId);
                auto* dstTrk = trackModel.getTrackById(dragTargetTrackId);
                if (srcTrk && dstTrk && srcTrk->getType() == dstTrk->getType())
                {
                    if (srcTrk->getType() == TrackType::Instrument)
                    {
                        auto srcBeforeClips = srcTrk->getClips();
                        auto dstBeforeClips = dstTrk->getClips();
                        auto srcAfterClips = srcBeforeClips;
                        auto dstAfterClips = dstBeforeClips;

                        for (auto it = srcAfterClips.begin(); it != srcAfterClips.end();)
                        {
                            bool isMoved = false;
                            for (const auto& s : selectedDragSnapshots)
                            {
                                if (s.trackId == srcTrk->getId() && s.intId == it->id)
                                {
                                    isMoved = true;
                                    break;
                                }
                            }
                            if (isMoved)
                            {
                                MidiClip movedClip = *it;
                                it = srcAfterClips.erase(it);

                                for (const auto& s : selectedDragSnapshots)
                                {
                                    if (s.trackId == srcTrk->getId() && s.intId == movedClip.id)
                                    {
                                        double shift = movedClip.startBeat - s.initialStartBeat;
                                        if (std::abs(shift) > 0.0001)
                                        {
                                            for (auto& n : movedClip.notes)
                                                n.startBeat += shift;
                                        }
                                        break;
                                    }
                                }
                                if (!movedClip.hasCustomColor)
                                    movedClip.color = dstTrk->getColor();

                                dstAfterClips.push_back(movedClip);
                            }
                            else
                            {
                                ++it;
                            }
                        }

                        auto* compound = new CompoundUndoAction("Move Clip Between Tracks");
                        compound->addAction(std::make_unique<ModifyClipsUndoAction>(trackModel, srcTrk->getId(), srcBeforeClips, srcAfterClips, "Move Clip (Source)"));
                        compound->addAction(std::make_unique<ModifyClipsUndoAction>(trackModel, dstTrk->getId(), dstBeforeClips, dstAfterClips, "Move Clip (Target)"));
                        UndoActionHelper::performAtomic(&undoManager, compound, "Move Clip Between Tracks");

                        // If active clip was moved, re-target active clip on destination track
                        for (const auto& s : selectedDragSnapshots)
                        {
                            if (s.trackId == srcTrk->getId() && s.intId == trackModel.getActiveClipId())
                            {
                                trackModel.setActiveClip(dstTrk->getId(), s.intId, &midiModel);
                                break;
                            }
                        }

                        trackModel.triggerTracksChanged();
                        selectedDragSnapshots.clear();
                        dragMode = ArrangerDragMode::None;
                        draggedTrackId = -1;
                        dragTargetTrackId = -1;
                        repaint();
                        return;
                    }
                    else if (srcTrk->getType() == TrackType::Chord)
                    {
                        auto srcBefore = srcTrk->getChords();
                        auto dstBefore = dstTrk->getChords();
                        auto srcAfter = srcBefore;
                        auto dstAfter = dstBefore;

                        for (auto it = srcAfter.begin(); it != srcAfter.end();)
                        {
                            bool isMoved = false;
                            for (const auto& s : selectedDragSnapshots)
                            {
                                if (s.trackId == srcTrk->getId() && s.intId == it->id) { isMoved = true; break; }
                            }
                            if (isMoved)
                            {
                                dstAfter.push_back(*it);
                                it = srcAfter.erase(it);
                            }
                            else ++it;
                        }

                        auto* compound = new CompoundUndoAction("Move Chords Between Tracks");
                        compound->addAction(std::make_unique<ModifyChordsUndoAction>(trackModel, srcTrk->getId(), srcBefore, srcAfter, "Move Chords (Source)"));
                        compound->addAction(std::make_unique<ModifyChordsUndoAction>(trackModel, dstTrk->getId(), dstBefore, dstAfter, "Move Chords (Target)"));
                        UndoActionHelper::performAtomic(&undoManager, compound, "Move Chords Between Tracks");

                        trackModel.triggerChordsChanged();
                        trackModel.triggerTracksChanged();
                        selectedDragSnapshots.clear();
                        dragMode = ArrangerDragMode::None;
                        draggedTrackId = -1;
                        dragTargetTrackId = -1;
                        repaint();
                        return;
                    }
                    else if (srcTrk->getType() == TrackType::Mode)
                    {
                        auto srcBefore = srcTrk->getModes();
                        auto dstBefore = dstTrk->getModes();
                        auto srcAfter = srcBefore;
                        auto dstAfter = dstBefore;

                        for (auto it = srcAfter.begin(); it != srcAfter.end();)
                        {
                            bool isMoved = false;
                            for (const auto& s : selectedDragSnapshots)
                            {
                                if (s.trackId == srcTrk->getId() && s.intId == it->id) { isMoved = true; break; }
                            }
                            if (isMoved)
                            {
                                dstAfter.push_back(*it);
                                it = srcAfter.erase(it);
                            }
                            else ++it;
                        }

                        auto* compound = new CompoundUndoAction("Move Modes Between Tracks");
                        compound->addAction(std::make_unique<ModifyModesUndoAction>(trackModel, srcTrk->getId(), srcBefore, srcAfter, "Move Modes (Source)"));
                        compound->addAction(std::make_unique<ModifyModesUndoAction>(trackModel, dstTrk->getId(), dstBefore, dstAfter, "Move Modes (Target)"));
                        UndoActionHelper::performAtomic(&undoManager, compound, "Move Modes Between Tracks");

                        trackModel.triggerModesChanged();
                        trackModel.triggerTracksChanged();
                        selectedDragSnapshots.clear();
                        dragMode = ArrangerDragMode::None;
                        draggedTrackId = -1;
                        dragTargetTrackId = -1;
                        repaint();
                        return;
                    }
                    else if (srcTrk->getType() == TrackType::Marker)
                    {
                        auto srcBefore = srcTrk->getMarkers();
                        auto dstBefore = dstTrk->getMarkers();
                        auto srcAfter = srcBefore;
                        auto dstAfter = dstBefore;

                        for (auto it = srcAfter.begin(); it != srcAfter.end();)
                        {
                            bool isMoved = false;
                            for (const auto& s : selectedDragSnapshots)
                            {
                                if (s.trackId == srcTrk->getId() && s.stringId == it->id) { isMoved = true; break; }
                            }
                            if (isMoved)
                            {
                                dstAfter.push_back(*it);
                                it = srcAfter.erase(it);
                            }
                            else ++it;
                        }

                        auto* compound = new CompoundUndoAction("Move Markers Between Tracks");
                        compound->addAction(std::make_unique<ModifyMarkersUndoAction>(trackModel, srcTrk->getId(), srcBefore, srcAfter, "Move Markers (Source)"));
                        compound->addAction(std::make_unique<ModifyMarkersUndoAction>(trackModel, dstTrk->getId(), dstBefore, dstAfter, "Move Markers (Target)"));
                        UndoActionHelper::performAtomic(&undoManager, compound, "Move Markers Between Tracks");

                        trackModel.triggerMarkersChanged();
                        trackModel.triggerTracksChanged();
                        selectedDragSnapshots.clear();
                        dragMode = ArrangerDragMode::None;
                        draggedTrackId = -1;
                        dragTargetTrackId = -1;
                        repaint();
                        return;
                    }
                }
            }

            // B. Within-track movement
            // 1. Shift notes in all moved instrument clips, and sync active clip to midiModel
            for (const auto& s : selectedDragSnapshots)
            {
                if (s.trackType == TrackType::Instrument)
                {
                    if (auto* t = trackModel.getTrackById(s.trackId))
                    {
                        for (auto& c : t->getClips())
                        {
                            if (c.id == s.intId)
                            {
                                double shift = c.startBeat - s.initialStartBeat;
                                if (std::abs(shift) > 0.0001)
                                {
                                    for (auto& n : c.notes)
                                        n.startBeat += shift;
                                    if (c.id == trackModel.getActiveClipId())
                                        midiModel.setAllNotes(c.notes, false);
                                }
                                break;
                            }
                        }
                    }
                }
            }

            // 2. Commit Undo Actions for all affected tracks
            std::set<int> affectedTrackIds;
            for (const auto& s : selectedDragSnapshots)
                affectedTrackIds.insert(s.trackId);

            auto compoundAction = std::make_unique<CompoundUndoAction>("Move Arranger Events");
            for (int tid : affectedTrackIds)
            {
                auto* t = trackModel.getTrackById(tid);
                if (!t) continue;
                if (t->getType() == TrackType::Instrument)
                {
                    auto currentClips = t->getClips();
                    auto beforeClips = currentClips;
                    for (auto& bc : beforeClips)
                    {
                        for (const auto& s : selectedDragSnapshots)
                        {
                            if (s.trackId == tid && s.intId == bc.id)
                            {
                                double shift = bc.startBeat - s.initialStartBeat;
                                bc.startBeat = s.initialStartBeat;
                                for (auto& n : bc.notes) n.startBeat -= shift;
                                break;
                            }
                        }
                    }
                    compoundAction->addAction(std::make_unique<ModifyClipsUndoAction>(trackModel, tid, beforeClips, currentClips, "Move MIDI Parts"));
                }
                else if (t->getType() == TrackType::Chord)
                {
                    auto currentChords = t->getChords();
                    auto beforeChords = currentChords;
                    for (auto& bc : beforeChords)
                    {
                        for (const auto& s : selectedDragSnapshots)
                        {
                            if (s.trackId == tid && s.intId == bc.id)
                            {
                                bc.startBeat = s.initialStartBeat;
                                break;
                            }
                        }
                    }
                    compoundAction->addAction(std::make_unique<ModifyChordsUndoAction>(trackModel, tid, beforeChords, currentChords, "Move Chords"));
                }
                else if (t->getType() == TrackType::Mode)
                {
                    auto currentModes = t->getModes();
                    auto beforeModes = currentModes;
                    for (auto& bm : beforeModes)
                    {
                        for (const auto& s : selectedDragSnapshots)
                        {
                            if (s.trackId == tid && s.intId == bm.id)
                            {
                                bm.startBeat = s.initialStartBeat;
                                break;
                            }
                        }
                    }
                    compoundAction->addAction(std::make_unique<ModifyModesUndoAction>(trackModel, tid, beforeModes, currentModes, "Move Modes"));
                }
                else if (t->getType() == TrackType::Marker)
                {
                    auto currentMarkers = t->getMarkers();
                    auto beforeMarkers = currentMarkers;
                    for (auto& bm : beforeMarkers)
                    {
                        for (const auto& s : selectedDragSnapshots)
                        {
                            if (s.trackId == tid && s.stringId == bm.id)
                            {
                                bm.startBeat = s.initialStartBeat;
                                break;
                            }
                        }
                    }
                    compoundAction->addAction(std::make_unique<ModifyMarkersUndoAction>(trackModel, tid, beforeMarkers, currentMarkers, "Move Markers"));
                }
                else if (t->getType() == TrackType::TimeSignature)
                {
                    auto currentSigs = t->getTimeSignatures();
                    auto beforeSigs = currentSigs;
                    for (auto& bts : beforeSigs)
                    {
                        for (const auto& s : selectedDragSnapshots)
                        {
                            if (s.trackId == tid && s.intId == bts.id)
                            {
                                bts.startBeat = s.initialStartBeat;
                                break;
                            }
                        }
                    }
                    compoundAction->addAction(std::make_unique<ModifyTimeSignaturesUndoAction>(trackModel, tid, beforeSigs, currentSigs, "Move Time Signatures"));
                }
                else if (t->getType() == TrackType::Tempo)
                {
                    auto currentEvents = t->getTempoEvents();
                    auto beforeEvents = currentEvents;
                    for (auto& bev : beforeEvents)
                    {
                        for (const auto& s : selectedDragSnapshots)
                        {
                            if (s.trackId == tid && s.intId == bev.id)
                            {
                                bev.startBeat = s.initialStartBeat;
                                break;
                            }
                        }
                    }
                    std::sort(currentEvents.begin(), currentEvents.end(), [](const TempoEvent& a, const TempoEvent& b) {
                        return a.startBeat < b.startBeat;
                    });
                    compoundAction->addAction(std::make_unique<ModifyTempoEventsUndoAction>(trackModel, tid, beforeEvents, currentEvents, "Move Tempo Points"));
                }
            }
            if (!compoundAction->isEmpty())
            {
                UndoActionHelper::performAtomic(&undoManager, compoundAction.release(), "Move Arranger Events");
            }
            trackModel.triggerTracksChanged();
            selectedDragSnapshots.clear();
        }
        else
        {
            auto* trk = trackModel.getTrackById(draggedTrackId);
            if (trk)
            {
                if (dragMode == ArrangerDragMode::MoveClip || dragMode == ArrangerDragMode::ResizeClipLeft || dragMode == ArrangerDragMode::ResizeClipRight)
                {
                    double targetStart = 0.0, targetLen = 1.0;
                    for (const auto& c : trk->getClips())
                        if (c.id == draggedItemId) { targetStart = c.startBeat; targetLen = c.lengthBeats; break; }

                    if (dragMode == ArrangerDragMode::MoveClip && !trackModel.canPlaceEvent(draggedTrackId, targetStart, targetLen, draggedItemId))
                    {
                        for (auto& c : trk->getClips())
                            if (c.id == draggedItemId) { c.startBeat = itemInitialStartBeat; break; }
                    }
                    else
                    {
                        auto currentClips = trk->getClips();
                        double shift = targetStart - itemInitialStartBeat;
                        for (auto& c : currentClips)
                        {
                            if (c.id == draggedItemId && dragMode == ArrangerDragMode::MoveClip && std::abs(shift) > 0.0001)
                            {
                                for (auto& n : c.notes) n.startBeat += shift;
                                if (c.id == trackModel.getActiveClipId())
                                    midiModel.setAllNotes(c.notes, false);
                                break;
                            }
                        }
                        trk->getClips() = currentClips;

                        for (auto& c : trk->getClips())
                        {
                            if (c.id == draggedItemId)
                            {
                                c.startBeat = itemInitialStartBeat;
                                c.lengthBeats = itemInitialLengthBeats;
                                if (dragMode == ArrangerDragMode::MoveClip && std::abs(shift) > 0.0001)
                                {
                                    for (auto& n : c.notes) n.startBeat -= shift;
                                }
                                break;
                            }
                        }
                        auto beforeClips = trk->getClips();
                        trk->getClips() = currentClips;

                        UndoActionHelper::performAtomic(&undoManager, new ModifyClipsUndoAction(trackModel, trk->getId(), beforeClips, currentClips, "Modify MIDI Part"), "Modify MIDI Part");
                    }
                }
                else if (dragMode == ArrangerDragMode::MoveChord || dragMode == ArrangerDragMode::ResizeChordLeft || dragMode == ArrangerDragMode::ResizeChordRight)
                {
                    double targetStart = 0.0, targetLen = 1.0;
                    for (const auto& c : trk->getChords())
                        if (c.id == draggedItemId) { targetStart = c.startBeat; targetLen = c.lengthBeats; break; }

                    if (!trackModel.canPlaceEvent(draggedTrackId, targetStart, targetLen, draggedItemId))
                    {
                        for (auto& c : trk->getChords())
                        {
                            if (c.id == draggedItemId)
                            {
                                c.startBeat = itemInitialStartBeat;
                                c.lengthBeats = itemInitialLengthBeats;
                                break;
                            }
                        }
                    }
                    else
                    {
                        auto currentChords = trk->getChords();
                        for (auto& c : trk->getChords())
                        {
                            if (c.id == draggedItemId)
                            {
                                c.startBeat = itemInitialStartBeat;
                                c.lengthBeats = itemInitialLengthBeats;
                                break;
                            }
                        }
                        auto beforeChords = trk->getChords();
                        trk->getChords() = currentChords;

                        UndoActionHelper::performAtomic(&undoManager, new ModifyChordsUndoAction(trackModel, trk->getId(), beforeChords, currentChords, "Modify Chord"), "Modify Chord");
                    }
                }
                else if (dragMode == ArrangerDragMode::MoveMode || dragMode == ArrangerDragMode::ResizeModeLeft || dragMode == ArrangerDragMode::ResizeModeRight)
                {
                    double targetStart = 0.0, targetLen = 1.0;
                    for (const auto& md : trk->getModes())
                        if (md.id == draggedItemId) { targetStart = md.startBeat; targetLen = md.lengthBeats; break; }

                    if (!trackModel.canPlaceEvent(draggedTrackId, targetStart, targetLen, draggedItemId))
                    {
                        for (auto& md : trk->getModes())
                        {
                            if (md.id == draggedItemId)
                            {
                                md.startBeat = itemInitialStartBeat;
                                md.lengthBeats = itemInitialLengthBeats;
                                break;
                            }
                        }
                    }
                    else
                    {
                        auto currentModes = trk->getModes();
                        for (auto& md : trk->getModes())
                        {
                            if (md.id == draggedItemId)
                            {
                                md.startBeat = itemInitialStartBeat;
                                md.lengthBeats = itemInitialLengthBeats;
                                break;
                            }
                        }
                        auto beforeModes = trk->getModes();
                        trk->getModes() = currentModes;

                        UndoActionHelper::performAtomic(&undoManager, new ModifyModesUndoAction(trackModel, trk->getId(), beforeModes, currentModes, "Modify Mode Section"), "Modify Mode Section");
                    }
                }
                else if (dragMode == ArrangerDragMode::MoveMarker || dragMode == ArrangerDragMode::ResizeMarkerLeft || dragMode == ArrangerDragMode::ResizeMarkerRight)
                {
                    auto currentMarkers = trk->getMarkers();
                    for (auto& m : trk->getMarkers())
                    {
                        if (m.id == draggedItemStringId)
                        {
                            m.startBeat = itemInitialStartBeat;
                            m.lengthBeats = itemInitialLengthBeats;
                            break;
                        }
                    }
                    auto beforeMarkers = trk->getMarkers();
                    trk->getMarkers() = currentMarkers;

                    UndoActionHelper::performAtomic(&undoManager, new ModifyMarkersUndoAction(trackModel, trk->getId(), beforeMarkers, currentMarkers, "Modify Marker"), "Modify Marker");
                }
                else if (dragMode == ArrangerDragMode::MoveTimeSig)
                {
                    double targetStart = 0.0;
                    double targetLen = 16.0;
                    for (const auto& ts : trk->getTimeSignatures())
                    {
                        if (ts.id == draggedItemId) { targetStart = ts.startBeat; targetLen = ts.lengthBeats; break; }
                    }

                    if (!trackModel.canPlaceEvent(draggedTrackId, targetStart, targetLen, draggedItemId))
                    {
                        for (auto& ts : trk->getTimeSignatures())
                        {
                            if (ts.id == draggedItemId)
                            {
                                ts.startBeat = itemInitialStartBeat;
                                ts.lengthBeats = itemInitialLengthBeats;
                                break;
                            }
                        }
                    }
                    else
                    {
                        auto currentSigs = trk->getTimeSignatures();
                        for (auto& ts : trk->getTimeSignatures())
                        {
                            if (ts.id == draggedItemId)
                            {
                                ts.startBeat = itemInitialStartBeat;
                                ts.lengthBeats = itemInitialLengthBeats;
                                break;
                            }
                        }
                        auto beforeSigs = trk->getTimeSignatures();
                        trk->getTimeSignatures() = currentSigs;

                        juce::String actName = (dragMode == ArrangerDragMode::MoveTimeSig) ? "Move Time Signature" : "Resize Time Signature";
                        UndoActionHelper::performAtomic(&undoManager, new ModifyTimeSignaturesUndoAction(trackModel, trk->getId(), beforeSigs, currentSigs, actName), actName);
                    }
                }
                else if (dragMode == ArrangerDragMode::MoveTempoPoint)
                {
                    auto currentEvents = trk->getTempoEvents();
                    auto beforeEvents = currentEvents;
                    for (auto& ev : beforeEvents)
                    {
                        if (ev.id == draggedItemId)
                        {
                            ev.startBeat = itemInitialStartBeat;
                            break;
                        }
                    }
                    std::sort(currentEvents.begin(), currentEvents.end(), [](const TempoEvent& a, const TempoEvent& b) {
                        return a.startBeat < b.startBeat;
                    });
                    trk->getTempoEvents() = currentEvents;
                    UndoActionHelper::performAtomic(&undoManager, new ModifyTempoEventsUndoAction(trackModel, trk->getId(), beforeEvents, currentEvents, "Move Tempo Point"), "Move Tempo Point");
                }
            }
        }
    }

    dragMode = ArrangerDragMode::None;
    isDuplicatingWithAlt = false;
    isCreatingItemWithAlt = false;
    marqueeRect = {};
    creatingTrackId = -1;
    creatingItemId = -1;
    draggedTrackId = -1;
    draggedItemId = -1;
    draggedItemStringId.clear();
    dragTrackSourceIndex = -1;
    dragTrackTargetIndex = -1;
    dragTargetTrackId = -1;
    selectedDragSnapshots.clear();
    repaint();
}

//==============================================================================
// Mouse Double Click (Inline Rename, Open Key Editor, or Edit Event Dialog)
//==============================================================================
void TrackArrangerComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    // Double click in track header: Inline Rename Track Name
    if (e.x < headerWidth && e.y >= rulerHeight)
    {
        int trkIdx = (e.y - rulerHeight + scrollTrackY) / trackHeight;
        auto visTracks = trackModel.getVisibleTracks();
        if (trkIdx >= 0 && trkIdx < (int)visTracks.size())
        {
            auto* trk = visTracks[trkIdx];
            auto headerArea = juce::Rectangle<int>(12, rulerHeight - scrollTrackY + trkIdx * trackHeight + 4, headerWidth - 75, 22);
            startTrackRename(trk->getId(), headerArea);
            return;
        }
    }

    if (e.x >= headerWidth && e.y >= rulerHeight)
    {
        int trkIdx = (e.y - rulerHeight + scrollTrackY) / trackHeight;
        auto visTracks = trackModel.getVisibleTracks();
        if (trkIdx >= 0 && trkIdx < (int)visTracks.size())
        {
            auto* trk = visTracks[trkIdx];
            double clickBeat = xToBeat((float)e.x);

            if (trk->getType() == TrackType::Audio)
            {
                bool hitExisting = false;
                for (const auto& clip : trk->getAudioClips())
                    if (clickBeat >= clip.startBeat && clickBeat <= clip.startBeat + clip.lengthBeats)
                    {
                        hitExisting = true;
                        break;
                    }
                if (!hitExisting)
                {
                    const double interval = midiModel.isSnapEnabled() ? midiModel.getSnapBeatInterval() : 0.0;
                    const double start = interval > 0.0 ? std::round(clickBeat / interval) * interval : clickBeat;
                    importAudioForTrack(trk->getId(), std::max(0.0, start));
                }
            }
            else if (trk->getType() == TrackType::Instrument)
            {
                for (const auto& clip : trk->getClips())
                {
                    if (clickBeat >= clip.startBeat && clickBeat <= (clip.startBeat + clip.lengthBeats))
                    {
                        if (clipDoubleClickedCallback)
                            clipDoubleClickedCallback(trk->getId(), clip.id);
                        return;
                    }
                }

                // If empty area on an Instrument track, create a new 4-bar MIDI clip
                int bpb = std::max(1, midiModel.getTimeSigNumerator());
                int bar = std::max(0, (int)(clickBeat / bpb));
                double startB = (double)(bar * bpb);
                double lenB = (double)(bpb * 4);

                if (trackModel.canPlaceEvent(trk->getId(), startB, lenB))
                {
                    auto oldClips = trk->getClips();
                    MidiClip newClip;
                    newClip.id = juce::Time::currentTimeMillis();
                    newClip.name = "Part " + juce::String(trk->getClips().size() + 1);
                    newClip.startBeat = startB;
                    newClip.lengthBeats = lenB;
                    newClip.color = trk->getColor();

                    auto newClips = oldClips;
                    newClips.push_back(newClip);

                    UndoActionHelper::performAtomic(&undoManager, new ModifyClipsUndoAction(trackModel, trk->getId(), oldClips, newClips, "Create MIDI Part"), "Create MIDI Part");

                    trackModel.setActiveClip(trk->getId(), newClip.id, &midiModel);

                    if (clipDoubleClickedCallback)
                        clipDoubleClickedCallback(trk->getId(), newClip.id);
                }
            }
            else if (trk->getType() == TrackType::Chord)
            {
                for (const auto& c : trk->getChords())
                {
                    if (clickBeat >= c.startBeat && clickBeat <= (c.startBeat + c.lengthBeats))
                    {
                        showEditChordDialog(trk->getId(), c.id);
                        return;
                    }
                }
            }
            else if (trk->getType() == TrackType::Mode)
            {
                for (const auto& md : trk->getModes())
                {
                    if (clickBeat >= md.startBeat && clickBeat <= (md.startBeat + md.lengthBeats))
                    {
                        showEditModeDialog(trk->getId(), md.id);
                        return;
                    }
                }
            }
            else if (trk->getType() == TrackType::Marker)
            {
                for (const auto& m : trk->getMarkers())
                {
                    if (clickBeat >= m.startBeat && clickBeat <= (m.startBeat + m.lengthBeats))
                    {
                        showEditMarkerDialog(trk->getId(), m.id);
                        return;
                    }
                }
            }
            else if (trk->getType() == TrackType::TimeSignature)
            {
                for (const auto& ts : trk->getTimeSignatures())
                {
                    float tsX1 = beatToX(ts.startBeat);
                    float tsX2 = beatToX(ts.startBeat + ts.lengthBeats);
                    if (tsX2 < tsX1 + 40.0f) tsX2 = tsX1 + 40.0f;
                    if ((float)e.x >= tsX1 - 4.0f && (float)e.x <= tsX2 + 4.0f)
                    {
                        showEditTimeSigDialog(trk->getId(), ts.id);
                        return;
                    }
                }
            }
            else if (trk->getType() == TrackType::Tempo)
            {
                int trackY = rulerHeight - scrollTrackY;
                for (const auto* t : trackModel.getVisibleTracks())
                {
                    if (t->getId() == trk->getId()) break;
                    trackY += trackHeight;
                }
                float topY = (float)trackY + 4.0f;
                float bottomY = (float)trackY + (float)trackHeight - 6.0f;
                float laneH = std::max(10.0f, bottomY - topY);

                for (const auto& ev : trk->getTempoEvents())
                {
                    float nx = beatToX(ev.startBeat);
                    float norm = (float)((ev.bpm - 40.0) / (240.0 - 40.0));
                    float ny = bottomY - juce::jlimit(0.0f, 1.0f, norm) * laneH;

                    if (std::hypot((float)e.x - nx, (float)e.y - ny) <= 14.0f ||
                        ((float)e.x >= nx - 4.0f && (float)e.x <= nx + 36.0f && std::abs((float)e.y - ny) <= 14.0f))
                    {
                        showEditTempoDialog(trk->getId(), ev.id);
                        return;
                    }
                }
            }
        }
    }
}

//==============================================================================
// Inline Track Renaming
//==============================================================================
void TrackArrangerComponent::startTrackRename(int trackId, juce::Rectangle<int> bounds)
{
    auto* trk = trackModel.getTrackById(trackId);
    if (!trk) return;

    renamingTrackId = trackId;
    inlineRenameEditor = std::make_unique<juce::TextEditor>();
    inlineRenameEditor->setText(trk->getName(), false);
    inlineRenameEditor->selectAll();
    inlineRenameEditor->setBounds(bounds);
    inlineRenameEditor->setColour(juce::TextEditor::backgroundColourId, CubaseTheme::bgPanel());
    inlineRenameEditor->setColour(juce::TextEditor::textColourId, CubaseTheme::textPrimary());
    inlineRenameEditor->setColour(juce::TextEditor::outlineColourId, CubaseTheme::accentOrange());

    inlineRenameEditor->onReturnKey = [this]() { finishTrackRename(true); };
    inlineRenameEditor->onEscapeKey = [this]() { finishTrackRename(false); };
    inlineRenameEditor->onFocusLost = [this]() { finishTrackRename(true); };

    addAndMakeVisible(inlineRenameEditor.get());
    inlineRenameEditor->grabKeyboardFocus();
}

void TrackArrangerComponent::finishTrackRename(bool apply)
{
    if (!inlineRenameEditor) return;

    if (apply && renamingTrackId > 0)
    {
        juce::String newName = inlineRenameEditor->getText().trim();
        if (newName.isNotEmpty())
        {
            if (auto* trk = trackModel.getTrackById(renamingTrackId))
            {
                auto oldName = trk->getName();
                if (oldName != newName)
                {
                    UndoActionHelper::performAtomic(&undoManager,
                        new TrackPropertyUndoAction(trackModel, renamingTrackId, oldName, newName),
                        "Rename Track");
                }
            }
        }
    }

    removeChildComponent(inlineRenameEditor.get());
    inlineRenameEditor.reset();
    renamingTrackId = -1;
    repaint();
}

//==============================================================================
// Edit Event Dialogs (Chords, Modes, Markers, TimeSignatures)
//==============================================================================
void TrackArrangerComponent::showEditChordDialog(int trackId, int64_t chordId)
{
    auto* trk = trackModel.getTrackById(trackId);
    if (!trk) return;
    juce::Component::SafePointer<TrackArrangerComponent> safeThis(this);

    for (const auto& c : trk->getChords())
    {
        if (c.id == chordId)
        {
            ChordEditorDialog::showModal(c, [this, safeThis, trackId, chordId](const juce::String& newName, const juce::String& newRoman) {
                if (safeThis.getComponent() == nullptr) return;
                if (auto* t = trackModel.getTrackById(trackId))
                {
                    auto oldChords = t->getChords();
                    auto newChords = oldChords;
                    for (auto& item : newChords)
                    {
                        if (item.id == chordId)
                        {
                            item.chordName = newName;
                            item.romanNumeral = newRoman;
                            break;
                        }
                    }
                    UndoActionHelper::performAtomic(&undoManager, new ModifyChordsUndoAction(trackModel, trackId, oldChords, newChords, "Edit Chord Block"), "Edit Chord Block");
                }
            });
            return;
        }
    }
}

void TrackArrangerComponent::showEditModeDialog(int trackId, int64_t modeId)
{
    auto* trk = trackModel.getTrackById(trackId);
    if (!trk) return;
    juce::Component::SafePointer<TrackArrangerComponent> safeThis(this);

    for (const auto& md : trk->getModes())
    {
        if (md.id == modeId)
        {
            auto initialMode = md;
            auto oldModes = trk->getModes();

            ModeEditorDialog::showModal(md,
                [this, safeThis, trackId, modeId](const juce::String& name, const juce::String& tonic, int tonicPc, const juce::String& scaleName, juce::Colour color) {
                    if (safeThis.getComponent() == nullptr) return;
                    if (auto* t = trackModel.getTrackById(trackId))
                    {
                        for (auto& item : t->getModes())
                        {
                            if (item.id == modeId)
                            {
                                item.name = name;
                                item.tonic = tonic;
                                item.tonicPc = tonicPc;
                                item.scaleName = scaleName;
                                item.color = color;
                                break;
                            }
                        }
                        trackModel.triggerModesChanged();
                        repaint();
                    }
                },
                [this, safeThis, trackId, modeId, initialMode]() {
                    if (safeThis.getComponent() == nullptr) return;
                    if (auto* t = trackModel.getTrackById(trackId))
                    {
                        for (auto& item : t->getModes())
                        {
                            if (item.id == modeId)
                            {
                                item.name = initialMode.name;
                                item.tonic = initialMode.tonic;
                                item.tonicPc = initialMode.tonicPc;
                                item.scaleName = initialMode.scaleName;
                                item.color = initialMode.color;
                                break;
                            }
                        }
                        trackModel.triggerModesChanged();
                        repaint();
                    }
                },
                [this, safeThis, trackId, oldModes]() {
                    if (safeThis.getComponent() == nullptr) return;
                    if (auto* t = trackModel.getTrackById(trackId))
                    {
                        auto newModes = t->getModes();
                        UndoActionHelper::performAtomic(&undoManager, new ModifyModesUndoAction(trackModel, trackId, oldModes, newModes, "Edit Mode Section"), "Edit Mode Section");
                    }
                }
            );
            return;
        }
    }
}

void TrackArrangerComponent::showEditMarkerDialog(int trackId, const juce::String& markerId)
{
    auto* trk = trackModel.getTrackById(trackId);
    if (!trk) return;
    juce::Component::SafePointer<TrackArrangerComponent> safeThis(this);

    for (const auto& m : trk->getMarkers())
    {
        if (m.id == markerId)
        {
            MarkerEditorDialog::showModal(m, [this, safeThis, trackId, markerId](const juce::String& name, juce::Colour color) {
                if (safeThis.getComponent() == nullptr) return;
                if (auto* t = trackModel.getTrackById(trackId))
                {
                    auto oldMarkers = t->getMarkers();
                    auto newMarkers = oldMarkers;
                    for (auto& item : newMarkers)
                    {
                        if (item.id == markerId)
                        {
                            item.name = name;
                            item.color = color;
                            break;
                        }
                    }
                    UndoActionHelper::performAtomic(&undoManager, new ModifyMarkersUndoAction(trackModel, trackId, oldMarkers, newMarkers, "Edit Marker Section"), "Edit Marker Section");
                }
            });
            return;
        }
    }
}

void TrackArrangerComponent::showEditTimeSigDialog(int trackId, int64_t sigId)
{
    auto* trk = trackModel.getTrackById(trackId);
    if (!trk) return;
    juce::Component::SafePointer<TrackArrangerComponent> safeThis(this);

    for (const auto& ts : trk->getTimeSignatures())
    {
        if (ts.id == sigId)
        {
            TimeSigEditorDialog::showModal(ts, [this, safeThis, trackId, sigId](int num, int den) {
                if (safeThis.getComponent() == nullptr) return;
                if (auto* t = trackModel.getTrackById(trackId))
                {
                    auto oldSigs = t->getTimeSignatures();
                    auto newSigs = oldSigs;
                    for (auto& item : newSigs)
                    {
                        if (item.id == sigId)
                        {
                            item.numerator = num;
                            item.denominator = den;
                            break;
                        }
                    }
                    UndoActionHelper::performAtomic(&undoManager, new ModifyTimeSignaturesUndoAction(trackModel, trackId, oldSigs, newSigs, "Edit Time Signature"), "Edit Time Signature");
                }
            });
            return;
        }
    }
}

void TrackArrangerComponent::showEditTempoDialog(int trackId, int64_t tempoId)
{
    auto* trk = trackModel.getTrackById(trackId);
    if (!trk) return;
    juce::Component::SafePointer<TrackArrangerComponent> safeThis(this);

    for (const auto& ev : trk->getTempoEvents())
    {
        if (ev.id == tempoId)
        {
            TempoEditorDialog::showModal(ev, [this, safeThis, trackId, tempoId](double newBpm, TempoTransitionType newTrans) {
                if (safeThis.getComponent() == nullptr) return;
                if (auto* t = trackModel.getTrackById(trackId))
                {
                    auto oldEvents = t->getTempoEvents();
                    auto newEvents = oldEvents;
                    for (auto& item : newEvents)
                    {
                        if (item.id == tempoId)
                        {
                            item.bpm = newBpm;
                            item.type = newTrans;
                            break;
                        }
                    }
                    UndoActionHelper::performAtomic(&undoManager, new ModifyTempoEventsUndoAction(trackModel, trackId, oldEvents, newEvents, "Edit Tempo Event"), "Edit Tempo Event");
                }
            });
            return;
        }
    }
}

} // namespace Harmonizer
