#pragma once

#include "plugin/ui/ParamUi.h"
#include "plugin/ui/components/Controls.h"
#include "plugin/ui/components/ThunderKnob.h"

namespace thunder::ui
{

// R-OUTPUT (UI.md §8.5), always visible: Output Mode, Mix and Layer Level,
// Input and Output Gain, Auto Gain and the safety limiter. Mix is dimmed in
// Layer Only, where it has no effect. Beside the knobs: the output meter.
// Laid out as in the 4A mockup: the knobs in a 2 × 2 grid so the meter, Auto
// Gain, Limiter and (later) the correlation line fit the 280 px height.
class OutputPanel final : public juce::Component
{
public:
    explicit OutputPanel (juce::AudioProcessorValueTreeState& apvts);

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Where the meter is drawn.
    juce::Rectangle<int> meterArea() const;

private:
    SegmentedControl mode;
    ThunderKnob mix;
    ThunderKnob layer;
    ThunderKnob input;
    ThunderKnob output;
    ParamCheckbox autoGain;
    ParamCheckbox limiter;
    ParameterWatcher modeWatcher;
};

} // namespace thunder::ui
