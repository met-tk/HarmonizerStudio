#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Harmonizer
{

enum class ChannelSliderKind { Volume, Pan };

class ChannelStripSlider : public juce::Slider
{
public:
    explicit ChannelStripSlider(ChannelSliderKind kind);
    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;

private:
    ChannelSliderKind kind;
};

} // namespace Harmonizer
