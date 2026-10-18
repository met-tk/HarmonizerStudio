#include "SecondaryDominantAction.h"
#include "../core/AppStateManager.h"
#include "../core/SecondaryDominant.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>

namespace Harmonizer
{

void generateSecondaryDominantPreview(MidiModel& model, TrackModel& tracks,
                                      std::function<void(std::vector<ChordPlacementGroup>)> onReadyToPlace)
{
    const auto& saved = AppStateManager::getInstance();
    SecondaryDominantOptions options;
    options.targetSource = static_cast<SecondaryDominantTargetSource>(
        std::clamp(saved.secondaryDominantTargetSource, 0, 3));
    options.targetRootPc = options.targetSource == SecondaryDominantTargetSource::ManualRoot
        ? std::clamp(saved.secondaryDominantManualRootPc, 0, 11) : -1;
    options.flatNine = saved.secondaryDominantFlatNine;
    options.rootInBass = saved.secondaryDominantRootInBass;

    const auto prepared = SecondaryDominant::prepare(model, tracks, options);
    if (!prepared.succeeded())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            juce::String::fromUTF8(u8"副属和弦重配"), prepared.error);
        return;
    }

    if (!prepared.generatedGroups.empty() && onReadyToPlace)
    {
        if (prepared.generatedGroups.size() != prepared.targetChordNames.size()
            || prepared.generatedGroups.size() != prepared.targetFunctions.size()
            || prepared.generatedGroups.size() != prepared.targetChordIsRootOnly.size())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                juce::String::fromUTF8(u8"副属和弦重配"),
                juce::String::fromUTF8(u8"生成和弦与目标信息不一致，无法预览。"));
            return;
        }
        std::vector<ChordPlacementGroup> placementGroups;
        const size_t count = prepared.generatedGroups.size();
        placementGroups.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            auto group = prepared.generatedGroups[i];
            for (auto& note : group)
            {
                note.id = 0;
                note.isSelected = true;
            }
            placementGroups.push_back({ std::move(group),
                prepared.targetChordNames[i], prepared.targetFunctions[i],
                prepared.targetChordIsRootOnly[i] });
        }
        if (!placementGroups.empty()) onReadyToPlace(std::move(placementGroups));
    }
}

} // namespace Harmonizer
