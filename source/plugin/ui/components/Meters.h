#pragma once

#include "plugin/ui/visualiser/MeterModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace thunder::ui
{

// The output meters in R-OUTPUT (PRD UI-VIS-007, FR-WIDTH-003; UI.md §8.5,
// §9.9), each drawing one part of a MeterModel. The editor's frame timer
// repaints them when the model changes.

// C-METER: two bars (L, R), −60…0 dBFS, dim cyan rising to hot in the top
// 3 dB, a 1 px peak-hold line, and a clip light above each bar (click to clear).
class OutputMeter final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    explicit OutputMeter (MeterModel& meters);

    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    // Where in the bars a level sits (from their bottom, 0 … 1).
    static float fraction (float db) noexcept;
    juce::String describe() const;

private:
    MeterModel& meters;
};

// The short-term loudness, "−14.2 LUFS", in the panel's header.
class LoudnessReadout final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    explicit LoudnessReadout (const MeterModel& meters);
    void paint (juce::Graphics& g) override;
    juce::String getText() const;

private:
    const MeterModel& meters;
};

// The correlation line under the meters: −1 … +1, filled from the centre;
// amber below 0, where parts of the sound cancel in mono.
class CorrelationMeter final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    explicit CorrelationMeter (const MeterModel& meters);
    void paint (juce::Graphics& g) override;

private:
    const MeterModel& meters;
};

// ⚠ after LIMITER: lit while the safety limiter takes more than 1 dB.
class LimiterLight final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    explicit LimiterLight (const MeterModel& meters);
    void paint (juce::Graphics& g) override;

private:
    const MeterModel& meters;
};

} // namespace thunder::ui
