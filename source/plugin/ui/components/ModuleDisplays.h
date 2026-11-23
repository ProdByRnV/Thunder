#pragma once

#include "core/params/ParamDefs.h"
#include "plugin/ui/visualiser/VisualModel.h"

#include <juce_audio_processors/juce_audio_processors.h>

// The module panels' live displays (UI.md §11.3, §11.4, §11.8, §11.9; PRD
// UI-GEN-001). Each draws from VisualModel, which the editor's frame timer
// updates; the panel showing it repaints it when its data changed. They only
// draw: none takes clicks or keyboard focus, and each has a title and a
// description for screen readers.

namespace thunder::ui
{

// Base: the small recessed frame every display sits in (as Tone's curve) and
// a reader for parameter values.
class ModuleDisplay
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    ModuleDisplay (const VisualModel& visuals, juce::AudioProcessorValueTreeState& apvts);

    void paint (juce::Graphics& g) final;

protected:
    virtual void paintContent (juce::Graphics& g, juce::Rectangle<float> area) = 0;
    float value (params::ParamId id) const noexcept;

    const VisualModel& visuals;
    juce::AudioProcessorValueTreeState& apvts;
};

// Grains (§11.3): every sounding grain as a dot. x = where it reads in the
// buffer (newest at the left, on a square-root scale so short spreads still
// separate), y = its pitch (±12 semitones), size = grain size, opacity = its
// window. Under Reduce Motion, a still bar of how many grains play.
class GrainCloudView final : public ModuleDisplay
{
public:
    GrainCloudView (const VisualModel& visuals, juce::AudioProcessorValueTreeState& apvts);

    static constexpr float kMaxAgeSeconds = 4.5f;
    static constexpr float kPitchRange = 12.0f;
    static constexpr float kDensityFull = 32.0f; // grains for a full bar under Reduce Motion

    // Where a grain's dot sits in `area`.
    static juce::Point<float> dotPosition (const visual::GrainSpark& spark, juce::Rectangle<float> area) noexcept;

private:
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
};

// Static (§11.4): each target that is on, as a violet line over the last two
// seconds (Position first and brightest). Bipolar targets (Filter, Pan) move
// around the centre line, Position and Pitch above it, Level's dip below it.
class StaticTraceView final : public ModuleDisplay
{
public:
    StaticTraceView (const VisualModel& visuals, juce::AudioProcessorValueTreeState& apvts);

    // A target's value as drawn: −1 (bottom) … +1 (top).
    static float drawnValue (int target, float value) noexcept;

private:
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
};

// Shape (§11.8): the gain over one cycle with Depth and Smooth applied, a
// playhead where Shape is now and a dot on the curve.
class ShapeView final : public ModuleDisplay
{
public:
    ShapeView (const VisualModel& visuals, juce::AudioProcessorValueTreeState& apvts);

    // The drawn gain (0 … 1) at `phase` for a shape, depth (0 … 1) and smooth
    // (0 … 1); the smoothing as ShapeModule's (a centred average over up to
    // half a cycle).
    static float gainAt (params::ShapeType shape, float depth, float smooth, double phase) noexcept;

private:
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;

    // What the cached curve was drawn for.
    struct CurveKey
    {
        params::ShapeType shape = params::ShapeType::Pump;
        float depth = -1.0f;
        float smooth = -1.0f;
        juce::Rectangle<float> area;

        bool operator== (const CurveKey& other) const noexcept
        {
            return shape == other.shape && juce::exactlyEqual (depth, other.depth) &&
                   juce::exactlyEqual (smooth, other.smooth) && area == other.area;
        }
    };
    CurveKey cachedKey;
    juce::Path curve, fill;
};

// Duck (§11.9): the reduction as a violet bar, 0 to −24 dB from the left,
// with the value at the right.
class ReductionMeter final : public ModuleDisplay
{
public:
    ReductionMeter (const VisualModel& visuals, juce::AudioProcessorValueTreeState& apvts);

    // How far the bar reaches (0 … 1) for a reduction in dB.
    static float fraction (float reductionDb) noexcept;
    juce::String getValueText() const;

private:
    void paintContent (juce::Graphics& g, juce::Rectangle<float> area) override;
};

} // namespace thunder::ui
