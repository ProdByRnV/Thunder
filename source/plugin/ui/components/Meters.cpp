#include "Meters.h"

#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderIcons.h"
#include "plugin/ui/theme/ThunderTheme.h"

#include <cmath>

namespace thunder::ui
{

using namespace theme;

namespace
{
juce::String decibels (float db)
{
    if (db <= visualiser::meterMinDb)
        return juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e dB"); // −∞ dB
    const auto text = juce::String (std::abs (db), 1) + " dB";
    return db < -0.05f ? juce::String::fromUTF8 ("\xe2\x88\x92") + text : text;
}
} // namespace

// ---- OutputMeter
// -----------------------------------------------------------------------------------------------------

OutputMeter::OutputMeter (MeterModel& m)
    : meters (m)
{
    setTitle ("Output meter");
    setDescription (
        "The output level of each channel, with peak hold and clip lights. Click a clip light to clear it.");
    setTooltip ("Output level. Click the clip lights to clear them.");
}

float OutputMeter::fraction (float db) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - visualiser::meterMinDb) / (0.0f - visualiser::meterMinDb));
}

juce::String OutputMeter::describe() const
{
    return "Left " + decibels (meters.getLevelDb (0)) + ", right " + decibels (meters.getLevelDb (1)) +
           (meters.isClipped (0) || meters.isClipped (1) ? ", clipped" : "");
}

void OutputMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    const auto captions = area.removeFromBottom (size::labelHeight);
    const auto lights = area.removeFromTop (space::s1);
    area.removeFromTop (space::s1);
    const auto bars = area.toFloat();

    // dim → full → hot in the top 3 dB (UI.md §9.9).
    juce::ColourGradient gradient (colour::accentPrimaryDim, 0.0f, bars.getBottom(), colour::accentHot, 0.0f,
                                   bars.getY(), false);
    gradient.addColour (static_cast<double> (fraction (visualiser::meterHotDb)), colour::accentPrimary);

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto x = static_cast<float> (channel * (size::meterBar + size::meterGap));
        const auto bar = juce::Rectangle<float> (x, bars.getY(), static_cast<float> (size::meterBar), bars.getHeight());

        g.setColour (meters.isClipped (channel) ? colour::stateWarning : colour::meterTrack);
        g.fillRoundedRectangle (
            juce::Rectangle<int> (static_cast<int> (x), lights.getY(), size::meterBar, lights.getHeight()).toFloat(),
            border::width);

        g.setColour (colour::meterTrack);
        g.fillRoundedRectangle (bar, radius::sm * 0.5f);

        const auto level = fraction (meters.getLevelDb (channel));
        if (level > 0.0f)
        {
            const juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (bar.withTop (bar.getBottom() - level * bar.getHeight()).toNearestInt());
            g.setGradientFill (gradient);
            g.fillRoundedRectangle (bar, radius::sm * 0.5f);
        }

        const auto hold = fraction (meters.getPeakHoldDb (channel));
        if (hold > 0.0f)
        {
            g.setColour (colour::textPrimary);
            g.fillRect (bar.withTop (bar.getBottom() - hold * bar.getHeight()).withHeight (border::width));
        }

        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (type::caption));
        g.drawText (channel == 0 ? "L" : "R", captions.withX (static_cast<int> (x)).withWidth (size::meterBar),
                    juce::Justification::centred);
    }
}

void OutputMeter::mouseUp (const juce::MouseEvent& e)
{
    // The clip lights (and a little around them, so they are easy to hit).
    if (e.position.y <= static_cast<float> (space::s3))
    {
        meters.resetClip();
        repaint();
    }
}

std::unique_ptr<juce::AccessibilityHandler> OutputMeter::createAccessibilityHandler()
{
    class Value final : public juce::AccessibilityValueInterface
    {
    public:
        explicit Value (OutputMeter& m)
            : meter (m)
        {
        }

        bool isReadOnly() const override { return true; }
        double getCurrentValue() const override { return meter.meters.getLevelDb (0); }
        juce::String getCurrentValueAsString() const override { return meter.describe(); }
        void setValue (double) override {}
        void setValueAsString (const juce::String&) override {}
        AccessibleValueRange getRange() const override { return {{visualiser::meterMinDb, 0.0}, 0.0}; }

    private:
        OutputMeter& meter;
    };

    return std::make_unique<juce::AccessibilityHandler> (
        *this, juce::AccessibilityRole::progressBar, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<Value> (*this)});
}

// ---- LoudnessReadout
// -------------------------------------------------------------------------------------------------

LoudnessReadout::LoudnessReadout (const MeterModel& m)
    : meters (m)
{
    setInterceptsMouseClicks (true, false);
    setTitle ("Loudness");
    setTooltip ("Short-term loudness of the output: the last 3 seconds, in LUFS.");
}

juce::String LoudnessReadout::getText() const
{
    const auto lufs = meters.getShortTermLufs();
    if (!lufs)
        return juce::String::fromUTF8 ("\xe2\x80\x93 LUFS"); // – LUFS

    const auto rounded = std::round (*lufs * 10.0f) / 10.0f;
    const auto text = juce::String (std::abs (rounded), 1) + " LUFS";
    return rounded < 0.0f ? juce::String::fromUTF8 ("\xe2\x88\x92") + text : text;
}

void LoudnessReadout::paint (juce::Graphics& g)
{
    g.setColour (colour::textSecondary);
    g.setFont (fonts::mono (type::value));
    g.drawText (getText(), getLocalBounds(), juce::Justification::centredRight, false);
}

// ---- CorrelationMeter
// ------------------------------------------------------------------------------------------------

CorrelationMeter::CorrelationMeter (const MeterModel& m)
    : meters (m)
{
    setTitle ("Correlation");
    setTooltip ("How the output sums to mono: +1 is mono, 0 is wide, below 0 parts cancel in mono.");
}

void CorrelationMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto r = bounds.getHeight() * 0.5f;
    g.setColour (colour::bgDeep);
    g.fillRoundedRectangle (bounds, r);

    const auto centre = bounds.getCentreX();
    if (const auto correlation = meters.getCorrelation())
    {
        const auto x = centre + *correlation * bounds.getWidth() * 0.5f;
        g.setColour (*correlation < 0.0f ? colour::stateWarning : colour::accentPrimaryDim);
        g.fillRoundedRectangle (
            juce::Rectangle<float> (std::min (centre, x), bounds.getY(), std::abs (x - centre), bounds.getHeight()), r);
    }

    g.setColour (colour::borderStrong);
    g.fillRect (
        juce::Rectangle<float> (centre - border::width * 0.5f, bounds.getY(), border::width, bounds.getHeight()));
}

// ---- LimiterLight
// ----------------------------------------------------------------------------------------------------

LimiterLight::LimiterLight (const MeterModel& m)
    : meters (m)
{
    setInterceptsMouseClicks (true, false);
    setTitle ("Limiter working");
    setTooltip ("Lights while the safety limiter is lowering the level by more than 1 dB.");
}

void LimiterLight::paint (juce::Graphics& g)
{
    if (!meters.isLimiting())
        return;

    const auto glyph = getLocalBounds().toFloat().withSizeKeepingCentre (static_cast<float> (size::limiterIcon),
                                                                         static_cast<float> (size::limiterIcon));
    g.setColour (colour::stateWarning);
    g.fillPath (icons::fitted (icons::warning(), glyph));
}

} // namespace thunder::ui
