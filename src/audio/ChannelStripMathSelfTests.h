#pragma once

#include "ChannelStripMath.h"
#include "../ui/ChannelStripSlider.h"
#include "../ui/CubaseTheme.h"
#include <juce_core/juce_core.h>
#include <array>
#include <cstdio>

namespace Harmonizer
{

inline int runChannelStripMathSelfTests()
{
    const auto logFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                             .getSiblingFile("channel_strip_tests.log");
    logFile.deleteFile();
    int failures = 0;
    auto check = [&](const char* name, bool passed) {
        std::printf("[%s] %s\n", passed ? "PASS" : "FAIL", name);
        logFile.appendText(juce::String(passed ? "[PASS] " : "[FAIL] ") + name + "\n");
        if (!passed) ++failures;
    };
    auto approximatelyEqual = [](double a, double b) { return std::abs(a - b) < 0.0001; };

    const auto centre = equalPowerPanGains(0.0f);
    check("centre pan preserves both existing channel gains",
          approximatelyEqual(centre.left, 1.0) && approximatelyEqual(centre.right, 1.0));
    const auto left = equalPowerPanGains(-1.0f);
    const auto right = equalPowerPanGains(1.0f);
    check("hard pan mutes the opposite channel without six dB edge gain",
          approximatelyEqual(left.left, std::sqrt(2.0)) && approximatelyEqual(left.right, 0.0)
          && approximatelyEqual(right.left, 0.0) && approximatelyEqual(right.right, std::sqrt(2.0)));
    bool constantPower = true;
    for (float pan : std::array<float, 7> { -1.0f, -0.75f, -0.5f, 0.0f,
                                           0.5f, 0.75f, 1.0f })
    {
        const auto gains = equalPowerPanGains(pan);
        constantPower &= approximatelyEqual(gains.left * gains.left + gains.right * gains.right, 2.0);
    }
    check("left and right squared gains have constant sum", constantPower);
    check("out-of-range pan is clamped", approximatelyEqual(equalPowerPanGains(2.0f).left, 0.0)
          && approximatelyEqual(equalPowerPanGains(-2.0f).right, 0.0));

    bool roundTrip = true;
    for (double gain : std::array<double, 4> { 0.001, 0.125892541, 1.0, 2.0 })
        roundTrip &= approximatelyEqual(channelDecibelsToGain(channelGainToDecibels(gain)), gain);
    check("dB readout converts back to the stored amplitude", roundTrip);
    check("mute reads as negative infinity and returns zero gain",
          !std::isfinite(channelGainToDecibels(0.0))
          && channelDecibelsToGain(channelGainToDecibels(0.0)) == 0.0);

    CubaseTheme theme;
    ChannelStripSlider volume(ChannelSliderKind::Volume);
    ChannelStripSlider pan(ChannelSliderKind::Pan);
    volume.setLookAndFeel(&theme);
    pan.setLookAndFeel(&theme);
    volume.setBounds(0, 0, 360, 28);
    pan.setBounds(0, 0, 360, 28);
    volume.setValue(1.0, juce::dontSendNotification);
    pan.setValue(-0.35, juce::dontSendNotification);
    check("volume readout accepts dB input and shows unity as zero dB",
          approximatelyEqual(volume.getValueFromText("-6 dB"),
                             channelDecibelsToGain(-6.0))
          && volume.getTextFromValue(1.0) == "0.0 dB");
    check("pan readout accepts left, centre and right input",
          approximatelyEqual(pan.getValueFromText("L35"), -0.35)
          && approximatelyEqual(pan.getValueFromText("C"), 0.0)
          && approximatelyEqual(pan.getValueFromText("R70"), 0.7));
    juce::Image preview(juce::Image::ARGB, 400, 104, true);
    {
        juce::Graphics graphics(preview);
        graphics.fillAll(CubaseTheme::bgPanel());
        graphics.setColour(CubaseTheme::textSecondary());
        graphics.setFont(CubaseTheme::getFont(11.0f, true));
        graphics.drawText("VOL", 12, 13, 40, 22, juce::Justification::centredLeft);
        graphics.drawText("PAN", 12, 57, 40, 22, juce::Justification::centredLeft);
        graphics.drawImageAt(volume.createComponentSnapshot(volume.getLocalBounds()), 48, 10);
        graphics.drawImageAt(pan.createComponentSnapshot(pan.getLocalBounds()), 48, 54);
    }
    const auto previewFile = logFile.getSiblingFile("channel_strip_preview.png");
    previewFile.deleteFile();
    if (auto stream = previewFile.createOutputStream())
        check("channel sliders render to an offscreen preview",
              juce::PNGImageFormat().writeImageToStream(preview, *stream));
    else
        check("channel sliders render to an offscreen preview", false);
    return failures;
}

} // namespace Harmonizer
