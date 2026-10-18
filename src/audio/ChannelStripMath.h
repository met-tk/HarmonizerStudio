#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace Harmonizer
{

struct StereoPanGains
{
    float left = 1.0f;
    float right = 1.0f;
};

// Centre remains at the project's existing 1.0/1.0 level. The squared
// channel gains sum to two across the travel, avoiding the 6 dB edge boost
// of the previous 1 +/- pan balance.
inline StereoPanGains equalPowerPanGains(float pan)
{
    const float position = std::clamp(pan, -1.0f, 1.0f);
    return { std::sqrt(1.0f - position), std::sqrt(1.0f + position) };
}

inline double channelGainToDecibels(double gain)
{
    return gain > 0.0 ? 20.0 * std::log10(gain)
                      : -std::numeric_limits<double>::infinity();
}

inline double channelDecibelsToGain(double decibels)
{
    return std::isfinite(decibels) ? std::pow(10.0, decibels / 20.0) : 0.0;
}

} // namespace Harmonizer
