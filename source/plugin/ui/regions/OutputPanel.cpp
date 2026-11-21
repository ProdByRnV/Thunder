#include "OutputPanel.h"

#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;
using params::ParamId;

OutputPanel::OutputPanel (juce::AudioProcessorValueTreeState& apvts)
    : mode (apvts, ParamId::outputMode, {}, {"Blend", "Layer only"})
    , mix (apvts, ParamId::mix, "Mix")
    , layer (apvts, ParamId::layerLevel, "Layer")
    , input (apvts, ParamId::inputGain, "In", ThunderKnob::Size::small)
    , output (apvts, ParamId::outputGain, "Out", ThunderKnob::Size::small)
    , autoGain (apvts, ParamId::autoGain, "Auto gain")
    , limiter (apvts, ParamId::safetyLimiter, "Limiter")
    , modeWatcher (apvts, ParamId::outputMode,
                   [this] (float m)
                   {
                       const auto layerOnly =
                           juce::roundToInt (m) == static_cast<int> (params::enumIndex (params::OutputMode::LayerOnly));
                       mix.setAlpha (layerOnly ? opacity::disabledBody : 1.0f);
                   })
{
    setTitle ("Output");
    for (auto* c : std::initializer_list<juce::Component*>{&mode, &mix, &layer, &input, &output, &autoGain, &limiter})
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

    // The stereo output meter's tracks (C-METER, UI.md §9.9): clip lights on
    // top, two bars, L and R below. TODO(PRD): UI-VIS-007 — the levels, peak
    // hold and clip arrive with the meters (ROADMAP T-0.6-07).
    auto area = meterArea();
    auto captions = area.removeFromBottom (size::labelHeight);
    const auto clipLights = area.removeFromTop (space::s1);
    area.removeFromTop (space::s1);

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto x = area.getX() + channel * (size::meterBar + size::meterGap);
        g.setColour (colour::meterTrack);
        g.fillRoundedRectangle (
            juce::Rectangle<int> (x, clipLights.getY(), size::meterBar, clipLights.getHeight()).toFloat(),
            border::width);
        g.fillRoundedRectangle (juce::Rectangle<int> (x, area.getY(), size::meterBar, area.getHeight()).toFloat(),
                                radius::sm * 0.5f);

        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (type::caption));
        g.drawText (channel == 0 ? "L" : "R", captions.withX (x).withWidth (size::meterBar),
                    juce::Justification::centred);
    }
}

void OutputPanel::resized()
{
    auto area = getLocalBounds().reduced (space::s4, 0);
    area.removeFromTop (size::moduleHeaderHeight);

    mode.setBounds (area.removeFromTop (size::controlHeight));
    area.removeFromTop (space::s2);

    // Two columns of knobs: Mix and Layer, then In and Out.
    const auto mediumHeight = ThunderKnob::preferredBounds (ThunderKnob::Size::medium).getHeight();
    const auto smallHeight = ThunderKnob::preferredBounds (ThunderKnob::Size::small).getHeight();
    auto row = area.removeFromTop (mediumHeight);
    mix.setBounds (row.removeFromLeft (size::outputKnobColumn));
    layer.setBounds (row.removeFromLeft (size::outputKnobColumn));
    row = area.removeFromTop (smallHeight);
    input.setBounds (row.removeFromLeft (size::outputKnobColumn));
    output.setBounds (row.removeFromLeft (size::outputKnobColumn));

    row = area.removeFromTop (size::controlHeight);
    autoGain.setBounds (row.removeFromLeft (row.getWidth() / 2));
    limiter.setBounds (row);
}

} // namespace thunder::ui
