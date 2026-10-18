#pragma once

#include <cmath>
#include <algorithm>

namespace Harmonizer
{

// Snap the item's destination, not its travel distance. An item moved freely with
// Ctrl may start between grid lines, but the next ordinary drag must rejoin the grid.
inline double snapDraggedItemDelta(double initialBeat, double pointerDelta, double interval)
{
    if (!std::isfinite(initialBeat) || !std::isfinite(pointerDelta)
        || !std::isfinite(interval) || interval <= 0.0)
        return pointerDelta;

    return std::round((initialBeat + pointerDelta) / interval) * interval - initialBeat;
}

inline double clampDraggedItemDelta(double initialBeat, double snappedDelta,
                                   double minDelta, double maxDelta, double interval)
{
    maxDelta = std::max(minDelta, maxDelta);
    if (std::isfinite(interval) && interval > 0.0)
    {
        const double firstGridDelta = std::ceil((initialBeat + minDelta - 1.0e-9) / interval)
            * interval - initialBeat;
        const double lastGridDelta = std::floor((initialBeat + maxDelta + 1.0e-9) / interval)
            * interval - initialBeat;
        if (firstGridDelta <= lastGridDelta)
            return std::clamp(snappedDelta, firstGridDelta, lastGridDelta);
    }
    return std::clamp(snappedDelta, minDelta, maxDelta);
}

} // namespace Harmonizer
