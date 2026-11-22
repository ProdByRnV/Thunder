#pragma once

#include "plugin/ui/ParamUi.h"
#include "plugin/ui/components/Controls.h"
#include "plugin/ui/components/Meters.h"
#include "plugin/ui/components/ThunderKnob.h"

namespace thunder::ui
{

// R-OUTPUT (UI.md §8.5), always visible: Output Mode, Mix and Layer Level,
// Input and Output Gain, Auto Gain and the safety limiter, and the meters
// (PRD UI-VIS-007, FR-WIDTH-003). Mix is dimmed in Layer Only, where it has no
// effect. Laid out as in the 4A mockup: the knobs in a 2 × 2 grid beside the
// stereo meter; Auto Gain and Limiter (with its ⚠ light) below; the
// correlation line at the bottom; the short-term loudness in the header.
class OutputPanel final : public juce::Component
{
public:
    OutputPanel (juce::AudioProcessorValueTreeState& apvts, MeterModel& meters);

    void paint (juce::Graphics& g) override;
    void resized() override;

    // [MSG] The meters' model changed: repaint them (only them).
    void refreshMeters();

    // Where the meter is drawn.
    juce::Rectangle<int> meterArea() const;

    OutputMeter& getMeter() noexcept { return meter; }
    LoudnessReadout& getLoudness() noexcept { return loudness; }
    CorrelationMeter& getCorrelation() noexcept { return correlation; }
    LimiterLight& getLimiterLight() noexcept { return limiterLight; }

private:
    SegmentedControl mode;
    ThunderKnob mix;
    ThunderKnob layer;
    ThunderKnob input;
    ThunderKnob output;
    ParamCheckbox autoGain;
    ParamCheckbox limiter;
    OutputMeter meter;
    LoudnessReadout loudness;
    CorrelationMeter correlation;
    LimiterLight limiterLight;
    ParameterWatcher modeWatcher;
};

} // namespace thunder::ui
