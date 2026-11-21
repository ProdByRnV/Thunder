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
    area.removeFromTop (size::moduleHeaderHeight);

    mode.setBounds (area.removeFromTop (size::controlHeight));
    area.removeFromTop (space::s1);

    const auto half = area.getWidth() / 2;
    auto row = area.removeFromTop (ThunderKnob::preferredBounds (ThunderKnob::Size::medium).getHeight());
    mix.setBounds (row.removeFromLeft (half));
    layer.setBounds (row);

    row = area.removeFromTop (ThunderKnob::preferredBounds (ThunderKnob::Size::small).getHeight());
    input.setBounds (row.removeFromLeft (half));
    output.setBounds (row);

    area.removeFromTop (space::s1);
    row = area.removeFromTop (size::controlHeight);
    autoGain.setBounds (row.removeFromLeft (half));
    limiter.setBounds (row);
}

} // namespace thunder::ui
