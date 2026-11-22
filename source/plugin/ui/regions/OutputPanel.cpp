#include "OutputPanel.h"

#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;
using params::ParamId;

OutputPanel::OutputPanel (juce::AudioProcessorValueTreeState& apvts, MeterModel& meters)
    : mode (apvts, ParamId::outputMode, {}, {"Blend", "Layer only"})
    , mix (apvts, ParamId::mix, "Mix")
    , layer (apvts, ParamId::layerLevel, "Layer")
    , input (apvts, ParamId::inputGain, "In", ThunderKnob::Size::small)
    , output (apvts, ParamId::outputGain, "Out", ThunderKnob::Size::small)
    , autoGain (apvts, ParamId::autoGain, "Auto gain")
    , limiter (apvts, ParamId::safetyLimiter, "Limiter")
    , meter (meters)
    , loudness (meters)
    , correlation (meters)
    , limiterLight (meters)
    , modeWatcher (apvts, ParamId::outputMode,
                   [this] (float m)
                   {
                       const auto layerOnly =
                           juce::roundToInt (m) == static_cast<int> (params::enumIndex (params::OutputMode::LayerOnly));
                       mix.setAlpha (layerOnly ? opacity::disabledBody : 1.0f);
                   })
{
    setTitle ("Output");
    for (auto* c : std::initializer_list<juce::Component*>{&loudness, &mode, &mix, &layer, &input, &output, &meter,
                                                           &autoGain, &limiter, &limiterLight, &correlation})
        addAndMakeVisible (c);
}

juce::Rectangle<int> OutputPanel::meterArea() const
{
    // Beside the knobs, from the top of Mix to the bottom of Out (UI.md §8.5).
    const auto knobs = mix.getBounds().getUnion (output.getBounds());
    const auto width = 2 * size::meterBar + size::meterGap;
    return {getWidth() - space::s4 - width, knobs.getY() + size::labelHeight, width,
            knobs.getHeight() - size::labelHeight};
}

void OutputPanel::paint (juce::Graphics& g)
{
    g.fillAll (colour::bgPanel);
    g.setColour (colour::textPrimary);
    g.setFont (fonts::ui (type::title, fonts::Weight::semiBold).withExtraKerningFactor (type::titleTracking));
    g.drawText ("OUTPUT", getLocalBounds().removeFromTop (size::moduleHeaderHeight).reduced (space::s4, 0),
                juce::Justification::centredLeft);
}

void OutputPanel::resized()
{
    auto area = getLocalBounds().reduced (space::s4, 0);
    auto header = area.removeFromTop (size::moduleHeaderHeight);
    loudness.setBounds (header.removeFromRight (size::loudnessWidth));

    mode.setBounds (area.removeFromTop (size::controlHeight));
    area.removeFromTop (space::s1);

    // Two columns of knobs: Mix and Layer, then In and Out.
    const auto mediumHeight = ThunderKnob::preferredBounds (ThunderKnob::Size::medium).getHeight();
    const auto smallHeight = ThunderKnob::preferredBounds (ThunderKnob::Size::small).getHeight();
    auto row = area.removeFromTop (mediumHeight);
    mix.setBounds (row.removeFromLeft (size::outputKnobColumn));
    layer.setBounds (row.removeFromLeft (size::outputKnobColumn));
    row = area.removeFromTop (smallHeight);
    input.setBounds (row.removeFromLeft (size::outputKnobColumn));
    output.setBounds (row.removeFromLeft (size::outputKnobColumn));

    meter.setBounds (meterArea());

    row = area.removeFromTop (size::controlHeight);
    autoGain.setBounds (row.removeFromLeft (row.getWidth() / 2));
    limiterLight.setBounds (row.removeFromRight (size::limiterIcon + space::s1));
    limiter.setBounds (row);

    // The correlation line, centred in what is left at the bottom.
    correlation.setBounds (area.withSizeKeepingCentre (area.getWidth(), size::correlationHeight));
}

void OutputPanel::refreshMeters()
{
    for (auto* c : std::initializer_list<juce::Component*>{&meter, &loudness, &correlation, &limiterLight})
        c->repaint();
}

} // namespace thunder::ui
