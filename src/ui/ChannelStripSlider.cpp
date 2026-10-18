#include "ChannelStripSlider.h"
#include "CubaseTheme.h"
#include "../audio/ChannelStripMath.h"
#include <algorithm>
#include <cmath>

namespace Harmonizer
{

ChannelStripSlider::ChannelStripSlider(ChannelSliderKind sliderKind) : kind(sliderKind)
{
    setSliderStyle(juce::Slider::LinearHorizontal);
    setTextBoxStyle(juce::Slider::TextBoxRight, false,
                    kind == ChannelSliderKind::Volume ? 68 : 50, 20);

    if (kind == ChannelSliderKind::Volume)
    {
        setRange(0.0, 2.0, 0.001);
        // The halfway point is about -18 dB; unity remains near the top of
        // the travel, as on a console fader.
        setSkewFactorFromMidPoint(channelDecibelsToGain(-18.0));
        setDoubleClickReturnValue(true, 1.0);
        textFromValueFunction = [](double gain) {
            if (gain <= 0.0) return juce::String::fromUTF8(u8"−∞ dB");
            const double db = channelGainToDecibels(gain);
            const juce::String sign = db > 0.05 ? "+" : "";
            return sign + juce::String(db, 1) + " dB";
        };
        valueFromTextFunction = [](const juce::String& text) {
            const auto entry = text.trim();
            if (entry.containsIgnoreCase("inf") || entry.containsChar(0x221e))
                return 0.0;
            return std::clamp(channelDecibelsToGain(entry.getDoubleValue()), 0.0, 2.0);
        };
    }
    else
    {
        setRange(-1.0, 1.0, 0.01);
        setDoubleClickReturnValue(true, 0.0);
        textFromValueFunction = [](double value) {
            const int percent = juce::roundToInt(std::abs(value) * 100.0);
            if (percent == 0) return juce::String("C");
            return juce::String(value < 0.0 ? "L" : "R") + juce::String(percent);
        };
        valueFromTextFunction = [](const juce::String& text) {
            const auto entry = text.trim().toUpperCase();
            if (entry == "C") return 0.0;
            if (entry.startsWithChar('L'))
                return -std::clamp(entry.substring(1).getDoubleValue() / 100.0, 0.0, 1.0);
            if (entry.startsWithChar('R'))
                return std::clamp(entry.substring(1).getDoubleValue() / 100.0, 0.0, 1.0);
            return std::clamp(entry.getDoubleValue(), -1.0, 1.0);
        };
    }
    lookAndFeelChanged();
}

void ChannelStripSlider::lookAndFeelChanged()
{
    juce::Slider::lookAndFeelChanged();
    setColour(juce::Slider::textBoxTextColourId, CubaseTheme::textPrimary());
    setColour(juce::Slider::textBoxBackgroundColourId, CubaseTheme::inputBackground());
    setColour(juce::Slider::textBoxOutlineColourId, CubaseTheme::borderSubtle());
    setColour(juce::Slider::textBoxHighlightColourId, CubaseTheme::accentBlue());
    repaint();
}

void ChannelStripSlider::paint(juce::Graphics& g)
{
    const auto layout = getLookAndFeel().getSliderLayout(*this);
    const auto bounds = layout.sliderBounds.toFloat();
    if (bounds.getWidth() <= 2.0f || bounds.getHeight() <= 2.0f) return;

    const float left = bounds.getX();
    const float right = bounds.getRight();
    const float centreY = bounds.getCentreY();
    const float position = left + (float)valueToProportionOfLength(getValue())
        * bounds.getWidth();
    const float centreX = left + 0.5f * bounds.getWidth();
    const auto rail = juce::Rectangle<float>(left, centreY - 3.0f,
                                              bounds.getWidth(), 6.0f);
    const float opacity = isEnabled() ? 1.0f : 0.45f;
    g.setOpacity(opacity);
    g.setColour(CubaseTheme::inputBackground());
    g.fillRoundedRectangle(rail.expanded(1.0f), 3.5f);
    g.setColour(CubaseTheme::borderBright());
    g.drawRoundedRectangle(rail.expanded(0.5f), 3.5f, 1.0f);

    if (kind == ChannelSliderKind::Volume)
    {
        const float filledWidth = std::max(0.0f, position - left);
        if (filledWidth > 0.0f)
        {
            juce::ColourGradient fill(CubaseTheme::accentBlue().darker(0.25f),
                left, centreY, CubaseTheme::accentCyan(), right, centreY, false);
            g.setGradientFill(fill);
            g.fillRoundedRectangle(juce::Rectangle<float>(left, rail.getY(),
                filledWidth, rail.getHeight()), 3.0f);
        }
        for (double db : { -36.0, -18.0, -6.0, 0.0 })
        {
            const float tick = left + (float)valueToProportionOfLength(
                channelDecibelsToGain(db)) * bounds.getWidth();
            g.setColour(db == 0.0 ? CubaseTheme::accentOrange()
                                  : CubaseTheme::borderBright());
            g.drawVerticalLine((int)std::round(tick), centreY - 8.0f, centreY - 4.0f);
        }
    }
    else
    {
        const auto accent = getValue() < 0.0 ? CubaseTheme::accentCyan()
                                             : CubaseTheme::accentBlue();
        g.setColour(accent);
        g.fillRoundedRectangle(juce::Rectangle<float>(std::min(centreX, position),
            rail.getY(), std::abs(position - centreX), rail.getHeight()), 2.5f);
        g.setColour(CubaseTheme::accentOrange());
        g.drawVerticalLine((int)std::round(centreX), centreY - 9.0f, centreY + 9.0f);
    }

    const auto cap = juce::Rectangle<float>(position - 6.0f, centreY - 9.0f,
                                             12.0f, 18.0f);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(cap.translated(0.0f, 2.0f), 3.0f);
    juce::ColourGradient capFill(CubaseTheme::borderBright().brighter(0.25f),
        cap.getX(), cap.getY(), CubaseTheme::buttonBackground(),
        cap.getX(), cap.getBottom(), false);
    g.setGradientFill(capFill);
    g.fillRoundedRectangle(cap, 3.0f);
    g.setColour(isMouseOverOrDragging() ? CubaseTheme::accentCyan()
                                       : CubaseTheme::textSecondary());
    g.drawRoundedRectangle(cap, 3.0f, 1.0f);
    g.drawHorizontalLine((int)std::round(centreY), cap.getX() + 3.0f,
                         cap.getRight() - 3.0f);
}

} // namespace Harmonizer
