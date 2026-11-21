#pragma once

#include "plugin/ui/ParamUi.h"
#include "plugin/ui/components/Controls.h"
#include "plugin/ui/components/ThunderKnob.h"

namespace thunder::ui
{

// R-OUTPUT (UI.md §8.5), always visible: Output Mode, Mix and Layer Level,
// Input and Output Gain, Auto Gain and the safety limiter. Mix is dimmed in
// Layer Only, where it has no effect. (Meters arrive with the visualiser.)
class OutputPanel final : public juce::Component
{
public:
    explicit OutputPanel (juce::AudioProcessorValueTreeState& apvts);

    void paint (juce::Graphics& g) override;
    void resized() override;

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
