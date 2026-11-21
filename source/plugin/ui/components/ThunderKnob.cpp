#include "ThunderKnob.h"

#include "plugin/ui/ParamUi.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderLookAndFeel.h"
#include "plugin/ui/theme/ThunderTheme.h"

#include <cmath>

namespace thunder::ui
{

using namespace theme;

namespace
{
int diameterFor (ThunderKnob::Size s)
{
    switch (s)
    {
        case ThunderKnob::Size::small:
            return size::knobSmall;
        case ThunderKnob::Size::large:
            return size::knobLarge;
        case ThunderKnob::Size::medium:
            break;
    }
    return size::knobMedium;
}
} // namespace

void ThunderKnob::Dial::mouseDown (const juce::MouseEvent& e)
{
    Slider::mouseDown (e); // starts the host gesture
    lastY = e.position.y;
    dragProportion = valueToProportionOfLength (getValue());
}

void ThunderKnob::Dial::mouseDrag (const juce::MouseEvent& e)
{
    if (!isEnabled() || e.mods.isPopupMenu())
        return;

    const auto pixels = static_cast<double> (lastY - e.position.y);
    lastY = e.position.y;

    dragProportion = ThunderKnob::dragged (dragProportion, pixels, e.mods.isShiftDown());
    setValue (proportionOfLengthToValue (dragProportion), juce::sendNotificationSync);
}

void ThunderKnob::Dial::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (!isEnabled() || juce::exactlyEqual (wheel.deltaY, 0.0f))
        return;

    const auto direction = (wheel.deltaY > 0.0f) != wheel.isReversed ? 1.0 : -1.0;
    const ScopedDragNotification gesture (*this);

    if (getInterval() >= 1.0)
        setValue (getValue() + direction * getInterval(), juce::sendNotificationSync);
    else
        setValue (proportionOfLengthToValue (juce::jlimit (
                      0.0, 1.0, valueToProportionOfLength (getValue()) + direction * size::knobWheelStep)),
                  juce::sendNotificationSync);
}

ThunderKnob::ThunderKnob (juce::AudioProcessorValueTreeState& apvts, params::ParamId id, const juce::String& text,
                          Size knobSize)
    : size (knobSize)
{
    const auto& def = params::paramDef (id);

    dial.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    dial.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    dial.setRotaryParameters (size::knobArcStart * juce::MathConstants<float>::pi,
                              size::knobArcEnd * juce::MathConstants<float>::pi, true);
    dial.getProperties().set (ThunderLookAndFeel::bipolarProperty,
                              def.kind == params::Kind::Float && juce::exactlyEqual (def.min, -def.max));
    dial.setTitle (text);
    dial.setComponentID (paramIdString (id));
    addAndMakeVisible (dial);

    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setFont (fonts::ui (type::label, fonts::Weight::medium));
    label.setColour (juce::Label::textColourId, colour::textSecondary);
    label.setJustificationType (juce::Justification::centred);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    value.setFont (fonts::mono (knobSize == Size::large ? type::valueLarge : type::value));
    value.setColour (juce::Label::textColourId, colour::textPrimary);
    value.setJustificationType (juce::Justification::centred);
    value.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (value);

    attachment =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramIdString (id), dial);
    dial.setDoubleClickReturnValue (true, def.def);
    dial.onValueChange = [this] { updateValueText(); };
    dial.setTooltip (text + ": " + dial.getTextFromValue (dial.getValue()));
    updateValueText();
}

ThunderKnob::~ThunderKnob() = default;

double ThunderKnob::dragged (double proportion, double pixels, bool fine) noexcept
{
    const auto factor = fine ? static_cast<double> (size::knobFineFactor) : 1.0;
    return juce::jlimit (0.0, 1.0, proportion + pixels / size::knobDragPixels * factor);
}

juce::Rectangle<int> ThunderKnob::preferredBounds (Size s)
{
    const auto d = diameterFor (s);
    return {0, 0, std::max (d, size::moduleColumnWidth / 2), size::labelHeight + d + size::valueHeight + space::s1};
}

void ThunderKnob::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromTop (size::labelHeight));
    value.setBounds (area.removeFromBottom (size::valueHeight));
    const auto d = diameterFor (size);
    dial.setBounds (area.withSizeKeepingCentre (d, d));
}

void ThunderKnob::updateValueText()
{
    const auto text = dial.getTextFromValue (dial.getValue());
    value.setText (text, juce::dontSendNotification);
    dial.setTooltip (dial.getTitle() + ": " + text);
}

} // namespace thunder::ui
