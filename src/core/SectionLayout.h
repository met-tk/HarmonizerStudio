#pragma once

#include <array>

namespace Harmonizer
{

inline constexpr int kInspectorSectionCount = 15;
inline constexpr int kReharmonizationSectionId = 10;
inline constexpr int kAudioTrackSectionId = 11;
inline constexpr int kChordShapeSectionId = 12;
inline constexpr int kModeBorrowingSectionId = 13;
inline constexpr int kQuickChordSectionId = 14;
inline constexpr std::array<int, 9> kNoteTrackSectionIds = {
    0, 1, 2, 3, 4, kReharmonizationSectionId,
    kChordShapeSectionId, kModeBorrowingSectionId, kQuickChordSectionId
};

inline constexpr std::array<int, kInspectorSectionCount> kDefaultSectionOrder = {
    0, 1, 2, 3, 4, kReharmonizationSectionId,
    kChordShapeSectionId, kModeBorrowingSectionId, kQuickChordSectionId,
    5, 6, 7, 8, 9, kAudioTrackSectionId
};

inline constexpr bool isInspectorSectionId(int id) noexcept
{
    return id >= 0 && id < kInspectorSectionCount;
}

inline constexpr bool isNoteTrackSectionId(int id) noexcept
{
    for (int sectionId : kNoteTrackSectionIds)
        if (sectionId == id)
            return true;
    return false;
}

} // namespace Harmonizer
