#include "ModuleDisplays.h"

#include "core/dsp/duck/DuckModule.h"
#include "core/dsp/shape/ShapeModule.h"
#include "core/dsp/static/StaticModulator.h"
#include "plugin/ui/Motion.h"
#include "plugin/ui/ParamUi.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderTheme.h"

#include <cmath>

namespace thunder::ui
{

using namespace theme;
using params::ParamId;

// ---- ModuleDisplay
// ---------------------------------------------------------------------------------------------------

ModuleDisplay::ModuleDisplay (const VisualModel& v, juce::AudioProcessorValueTreeState& state)
    : visuals (v)
    , apvts (state)
{
    setInterceptsMouseClicks (false, false);
    setWantsKeyboardFocus (false);
    setOpaque (false);
}

float ModuleDisplay::value (ParamId id) const noexcept
{
    if (const auto* raw = apvts.getRawParameterValue (paramIdString (id)))
        return raw->load (std::memory_order_relaxed);
    return 0.0f;
}

void ModuleDisplay::paint (juce::Graphics& g)
{
    const auto frame = getLocalBounds().toFloat();
    g.setColour (colour::bgDeep);
    g.fillRoundedRectangle (frame, radius::sm);

    {
        const juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (frame, radius::sm);
        g.reduceClipRegion (clip);
        paintContent (g, frame);
    }

    g.setColour (colour::borderSubtle);
    g.drawRoundedRectangle (frame.reduced (border::width * 0.5f), radius::sm, border::width);
}

// ---- GrainCloudView
// --------------------------------------------------------------------------------------------------

GrainCloudView::GrainCloudView (const VisualModel& v, juce::AudioProcessorValueTreeState& state)
    : ModuleDisplay (v, state)
{
    setTitle ("Grain cloud");
    setDescription ("Each sounding grain as a dot: across, where it reads from (newest at the left); up and down, "
                    "its pitch; its size, the grain size.");
    setTooltip ("The grains playing now: across, how far back each reads; up and down, its pitch.");
}

juce::Point<float> GrainCloudView::dotPosition (const visual::GrainSpark& spark, juce::Rectangle<float> area) noexcept
{
    const auto age = std::clamp (spark.ageSeconds / kMaxAgeSeconds, 0.0f, 1.0f);
    const auto pitch = std::clamp (spark.semitones / kPitchRange, -1.0f, 1.0f);
    return {area.getX() + area.getWidth() * std::sqrt (age), area.getCentreY() - area.getHeight() * 0.5f * pitch};
}

void GrainCloudView::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    // Under Reduce Motion: a still bar of how many grains are playing (UI.md §11.3).
    if (reduceMotion::isOn())
    {
        const auto share = std::clamp (static_cast<float> (visuals.getGrains().count) / kDensityFull, 0.0f, 1.0f);
        const auto bar = area.reduced (static_cast<float> (space::s2))
                             .withSizeKeepingCentre (area.getWidth() - 2.0f * static_cast<float> (space::s2),
                                                     size::reductionBarHeight);
        g.setColour (colour::meterTrack);
        g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
        if (share > 0.0f)
        {
            g.setColour (colour::accentPrimary);
            g.fillRoundedRectangle (bar.withWidth (std::max (bar.getHeight(), bar.getWidth() * share)),
                                    bar.getHeight() * 0.5f);
        }
        return;
    }

    // The unshifted pitch.
    g.setColour (colour::gridBeat);
    g.fillRect (area.withY (area.getCentreY() - border::width * 0.5f).withHeight (border::width));

    const auto inner = area.reduced (size::displayDotMax);
    const auto& grains = visuals.getGrains();
    for (int i = 0; i < grains.count; ++i)
    {
        const auto& spark = grains.sparks[static_cast<std::size_t> (i)];
        const auto at = dotPosition (spark, inner);
        const auto sizeShare = std::sqrt (std::clamp (spark.sizeSeconds / 0.5f, 0.0f, 1.0f));
        const auto r = size::displayDotMin + (size::displayDotMax - size::displayDotMin) * sizeShare;
        const auto alpha = 0.25f + 0.75f * std::clamp (spark.window, 0.0f, 1.0f);

        g.setColour (colour::accentPrimary.withAlpha (alpha * 0.35f));
        g.fillEllipse (juce::Rectangle<float> (2.0f * r + 2.0f, 2.0f * r + 2.0f).withCentre (at));
        g.setColour (colour::accentHot.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (r, r).withCentre (at));
    }
}

// ---- StaticTraceView
// -------------------------------------------------------------------------------------------------

namespace
{
constexpr std::array<ParamId, VisualModel::kStaticTargets> kStaticTargetParams{
    ParamId::staticTgtPosition, ParamId::staticTgtPitch, ParamId::staticTgtFilter, ParamId::staticTgtPan,
    ParamId::staticTgtLevel};
} // namespace

StaticTraceView::StaticTraceView (const VisualModel& v, juce::AudioProcessorValueTreeState& state)
    : ModuleDisplay (v, state)
{
    setTitle ("Static movement");
    setDescription ("How far Static moves each target that is on, over the last two seconds.");
    setTooltip ("Static's movement over the last two seconds, one line per target that is on.");
}

float StaticTraceView::drawnValue (int target, float v) noexcept
{
    using Target = dsp::StaticModulator::Target;
    switch (target)
    {
        case Target::filter:
        case Target::pan:
            return std::clamp (v, -1.0f, 1.0f);
        case Target::level:
            return -std::clamp (v, 0.0f, 1.0f); // a dip
        default:
            return std::clamp (v, 0.0f, 1.0f);
    }
}

void StaticTraceView::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (colour::gridBeat);
    g.fillRect (area.withY (area.getCentreY() - border::width * 0.5f).withHeight (border::width));

    const auto inner = area.reduced (space::s1, space::s1);
    const auto points = VisualModel::kTracePoints;
    auto drawn = 0;
    for (int t = 0; t < VisualModel::kStaticTargets; ++t)
    {
        if (value (kStaticTargetParams[static_cast<std::size_t> (t)]) < 0.5f)
            continue;

        juce::Path line;
        for (int i = 0; i < points; ++i)
        {
            const auto x = inner.getX() + inner.getWidth() * static_cast<float> (i) / static_cast<float> (points - 1);
            const auto y =
                inner.getCentreY() - inner.getHeight() * 0.5f * drawnValue (t, visuals.getStaticTrace (t, i));
            if (i == 0)
                line.startNewSubPath (x, y);
            else
                line.lineTo (x, y);
        }

        // The first line drawn is the brightest; the rest sit behind it.
        g.setColour (colour::accentSecondary.withAlpha (drawn == 0 ? 1.0f : 0.45f));
        g.strokePath (line, juce::PathStrokeType (size::displayStroke));
        ++drawn;
    }
}

// ---- ShapeView
// -------------------------------------------------------------------------------------------------------

ShapeView::ShapeView (const VisualModel& v, juce::AudioProcessorValueTreeState& state)
    : ModuleDisplay (v, state)
{
    setTitle ("Shape display");
    setDescription ("The level over one cycle of the shape, with a playhead where the song is now.");
    setTooltip ("One cycle of the shape (full level at the top) and where the song is in it.");
}

float ShapeView::gainAt (params::ShapeType shape, float depth, float smooth, double phase) noexcept
{
    // A centred average, as ShapeModule's box smoothing.
    constexpr int kTaps = 48;
    const auto width = std::max (0.004, static_cast<double> (std::clamp (smooth, 0.0f, 1.0f)) * 0.5);
    auto sum = 0.0;
    for (int i = 0; i < kTaps; ++i)
        sum += dsp::ShapeModule::shapeAt (shape, phase + width * ((i + 0.5) / kTaps - 0.5));
    const auto level = static_cast<float> (sum / kTaps);
    return 1.0f - std::clamp (depth, 0.0f, 1.0f) * (1.0f - level);
}

void ShapeView::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto shape = static_cast<params::ShapeType> (juce::roundToInt (value (ParamId::shapeType)));
    const auto depth = value (ParamId::shapeDepth) / 100.0f;
    const auto smooth = value (ParamId::shapeSmooth) / 100.0f;
    const auto inner = area.reduced (space::s2, space::s1);

    // Quarter lines.
    g.setColour (colour::gridBeat);
    for (int q = 1; q < 4; ++q)
        g.fillRect (juce::Rectangle<float> (inner.getX() + inner.getWidth() * static_cast<float> (q) / 4.0f,
                                            area.getY(), border::width, area.getHeight()));

    const auto yFor = [&] (float gain) { return inner.getBottom() - inner.getHeight() * gain; };

    // The curve is rebuilt only when the settings or the size change.
    const CurveKey key{shape, depth, smooth, inner};
    if (!(key == cachedKey))
    {
        cachedKey = key;
        curve.clear();
        fill.clear();
        const auto steps = std::max (2, static_cast<int> (inner.getWidth()));
        for (int i = 0; i <= steps; ++i)
        {
            const auto phase = std::min (static_cast<double> (i) / steps, 0.9999);
            const auto x = inner.getX() + inner.getWidth() * static_cast<float> (i) / static_cast<float> (steps);
            const auto y = yFor (gainAt (shape, depth, smooth, phase));
            if (i == 0)
            {
                curve.startNewSubPath (x, y);
                fill.startNewSubPath (x, inner.getBottom());
            }
            else
            {
                curve.lineTo (x, y);
            }
            fill.lineTo (x, y);
        }
        fill.lineTo (inner.getRight(), inner.getBottom());
        fill.closeSubPath();
    }

    g.setColour (colour::accentPrimary.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (colour::accentPrimary);
    g.strokePath (curve, juce::PathStrokeType (size::displayStroke));

    // The playhead: where Shape is now, while it is on.
    if (value (ParamId::shapeOn) >= 0.5f && !reduceMotion::isOn())
    {
        const auto& activity = visuals.getActivity();
        const auto x = inner.getX() + inner.getWidth() * std::clamp (activity.shapePhase, 0.0f, 1.0f);
        g.setColour (colour::accentHot.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (x - border::width * 0.5f, area.getY(), border::width, area.getHeight()));
        const auto dot = juce::Point<float> (x, yFor (std::clamp (activity.shapeGain, 0.0f, 1.0f)));
        g.setColour (colour::accentHot);
        g.fillEllipse (juce::Rectangle<float> (size::displayDotMax, size::displayDotMax).withCentre (dot));
    }
}

// ---- ReductionMeter
// --------------------------------------------------------------------------------------------------

ReductionMeter::ReductionMeter (const VisualModel& v, juce::AudioProcessorValueTreeState& state)
    : ModuleDisplay (v, state)
{
    setTitle ("Duck reduction");
    setDescription ("How far Duck is turning the layer down now, 0 to 24 dB.");
    setTooltip ("How far Duck is turning the layer down now (0 to 24 dB).");
}

float ReductionMeter::fraction (float reductionDb) noexcept
{
    return std::clamp (reductionDb / dsp::DuckModule::kMaxReductionDb, 0.0f, 1.0f);
}

juce::String ReductionMeter::getValueText() const
{
    const auto db = visuals.getDuckReductionDb();
    if (db < 0.05f)
        return "0.0 dB";
    return juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (db, 1) + " dB"; // −6.0 dB
}

void ReductionMeter::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto inner = area.reduced (space::s2, 0.0f);
    const auto text = inner.removeFromRight (static_cast<float> (size::reductionValueWidth));
    const auto bar = inner.withSizeKeepingCentre (inner.getWidth(), size::reductionBarHeight);

    g.setColour (colour::meterTrack);
    g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);

    const auto reach = fraction (visuals.getDuckReductionDb());
    if (reach > 0.0f)
    {
        g.setColour (colour::accentSecondary);
        g.fillRoundedRectangle (bar.withWidth (std::max (bar.getHeight(), bar.getWidth() * reach)),
                                bar.getHeight() * 0.5f);
    }

    // −6, −12 and −18 dB ticks, over the bar.
    g.setColour (colour::bgDeep.withAlpha (0.8f));
    for (int db = 6; db < 24; db += 6)
        g.fillRect (juce::Rectangle<float> (bar.getX() + bar.getWidth() * fraction (static_cast<float> (db)),
                                            bar.getY(), border::width, bar.getHeight()));

    g.setColour (colour::textPrimary);
    g.setFont (fonts::mono (type::value));
    g.drawText (getValueText(), text, juce::Justification::centredRight, false);
}

} // namespace thunder::ui
