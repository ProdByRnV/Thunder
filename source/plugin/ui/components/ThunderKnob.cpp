#include "ThunderKnob.h"

#include "core/params/ParamText.h"
#include "plugin/ui/ParamHelp.h"
#include "plugin/ui/ParamUi.h"
#include "plugin/ui/theme/ThunderFonts.h"
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

juce::PathStrokeType arcStroke (float width)
{
    return juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
}
} // namespace

// ---- Dial
// ------------------------------------------------------------------------------------------------------------

ThunderKnob::Dial::Dial (ThunderKnob& o)
    : owner (o)
{
    setMouseClickGrabsKeyboardFocus (false);
    setWantsKeyboardFocus (true);
}

void ThunderKnob::Dial::paint (juce::Graphics& g)
{
    // C-KNOB anatomy (UI.md §9.1): track, glowing value arc, face, pointer, spark.
    const auto bounds = getLocalBounds().toFloat();
    const auto diameter = std::min (bounds.getWidth(), bounds.getHeight());
    const auto radius = diameter * 0.5f - size::knobTrack;
    const auto centre = bounds.getCentre();
    const auto proportion = valueToProportionOfLength (getValue());
    const auto rotary = getRotaryParameters();
    const auto start = rotary.startAngleRadians;
    const auto end = rotary.endAngleRadians;
    const auto angle = start + static_cast<float> (proportion) * (end - start);
    const auto enabled = isEnabled();

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour (colour::borderSubtle);
    g.strokePath (track, arcStroke (size::knobTrack));

    const auto from = bipolar ? (start + end) * 0.5f : start;
    const auto hasArc = std::abs (angle - from) > 0.001f;
    if (hasArc)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, std::min (from, angle), std::max (from, angle),
                           true);

        if (enabled)
        {
            juce::Path outline;
            arcStroke (size::knobTrack).createStrokedPath (outline, arc);
            arcGlowCache.draw (g, outline, colour::glowPrimary, glow::medium,
                               ThunderKnob::arcGlow (proportion, bipolar, isMouseOverOrDragging()));
        }

        g.setColour (enabled ? colour::accentPrimary : colour::textDisabled);
        g.strokePath (arc, arcStroke (size::knobTrack));
    }

    // Face: a raised disc with a faint top highlight.
    const auto face = juce::Rectangle<float> (diameter * size::knobFace, diameter * size::knobFace).withCentre (centre);
    g.setGradientFill (
        juce::ColourGradient::vertical (colour::bgHover, face.getY(), colour::bgRaised, face.getBottom()));
    g.fillEllipse (face);
    g.setColour (colour::borderSubtle);
    g.drawEllipse (face, border::width);

    const auto pointerFrom = centre.getPointOnCircumference (diameter * 0.5f * size::knobPointerFrom, angle);
    const auto pointerTo = centre.getPointOnCircumference (diameter * 0.5f * size::knobPointerTo, angle);
    g.setColour (enabled ? colour::textPrimary : colour::textDisabled);
    g.drawLine ({pointerFrom, pointerTo}, size::knobPointer);

    // The spark at the arc's tip (§9.1): hot core with a small glow.
    if (hasArc && enabled)
    {
        const auto tip = centre.getPointOnCircumference (radius, angle);
        const auto spark = juce::Rectangle<float> (size::knobSpark, size::knobSpark).withCentre (tip);
        juce::Path dot;
        dot.addEllipse (spark);
        sparkGlowCache.draw (g, dot, colour::glowPrimary, glow::subtle, 1.0f);
        g.setColour (colour::accentHot);
        g.fillEllipse (spark);
    }

    if (hasKeyboardFocus (false))
        paint::drawFocusRing (g, juce::Rectangle<float> (diameter, diameter).withCentre (centre), diameter * 0.5f);
}

void ThunderKnob::Dial::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        owner.showMenu();
        return;
    }

    Slider::mouseDown (e); // starts the host gesture
    lastY = e.position.y;
    dragProportion = valueToProportionOfLength (getValue());
    dragging = false;
}

void ThunderKnob::Dial::mouseDrag (const juce::MouseEvent& e)
{
    if (!isEnabled() || e.mods.isPopupMenu())
        return;

    // Hide the cursor while dragging (§9.1); it comes back where it was.
    if (!dragging)
    {
        dragging = true;
        e.source.enableUnboundedMouseMovement (true);
    }

    const auto pixels = static_cast<double> (lastY - e.position.y);
    lastY = e.position.y;

    dragProportion = ThunderKnob::dragged (dragProportion, pixels, e.mods.isShiftDown());
    setValue (proportionOfLengthToValue (dragProportion), juce::sendNotificationSync);
}

void ThunderKnob::Dial::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (dragging)
    {
        e.source.enableUnboundedMouseMovement (false);
        dragging = false;
    }

    Slider::mouseUp (e); // ends the host gesture
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

// ---- Value
// -----------------------------------------------------------------------------------------------------------

ThunderKnob::Value::Value (ThunderKnob& o)
    : owner (o)
{
}

void ThunderKnob::Value::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        owner.showMenu();
    else
        owner.showValueEditor();
}

// ---- ThunderKnob
// -----------------------------------------------------------------------------------------------------

ThunderKnob::ThunderKnob (juce::AudioProcessorValueTreeState& state, params::ParamId id, const juce::String& text,
                          Size knobSize)
    : apvts (state)
    , paramId (id)
    , size (knobSize)
    , dial (*this)
    , value (*this)
{
    const auto& def = params::paramDef (id);

    dial.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    dial.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    dial.setRotaryParameters (size::knobArcStart * juce::MathConstants<float>::pi,
                              size::knobArcEnd * juce::MathConstants<float>::pi, true);
    dial.bipolar = def.kind == params::Kind::Float && juce::exactlyEqual (def.min, -def.max);
    dial.setTitle (text);
    dial.setDescription (help::describe (id));
    dial.setComponentID (paramIdString (id));
    addAndMakeVisible (dial);

    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setFont (fonts::ui (type::label, fonts::Weight::medium));
    label.setColour (juce::Label::textColourId, colour::textSecondary);
    label.setJustificationType (juce::Justification::centred);
    label.setInterceptsMouseClicks (false, false);
    label.setAccessible (false); // the dial carries the name
    addAndMakeVisible (label);

    value.setFont (fonts::mono (knobSize == Size::large ? type::valueLarge : type::value));
    value.setColour (juce::Label::textColourId, colour::textPrimary);
    value.setJustificationType (juce::Justification::centred);
    value.setEditable (false, false, false);
    value.setAccessible (false); // the dial reads its own value
    value.setMouseCursor (juce::MouseCursor::IBeamCursor);
    value.onTextChange = [this]
    {
        if (!enterValue (value.getText()))
            updateValueText();
    };
    addAndMakeVisible (value);

    attachment =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramIdString (id), dial);
    dial.setDoubleClickReturnValue (true, def.def);
    dial.onValueChange = [this] { updateValueText(); };
    updateValueText();
}

ThunderKnob::~ThunderKnob() = default;

double ThunderKnob::dragged (double proportion, double pixels, bool fine) noexcept
{
    const auto factor = fine ? static_cast<double> (size::knobFineFactor) : 1.0;
    return juce::jlimit (0.0, 1.0, proportion + pixels / size::knobDragPixels * factor);
}

float ThunderKnob::arcGlow (double proportion, bool bipolar, bool hovered) noexcept
{
    // glow.medium × value (§6.3, §9.1); a bipolar knob glows with its distance from the centre.
    const auto level = bipolar ? std::abs (proportion - 0.5) * 2.0 : proportion;
    const auto glowLevel = glow::medium.opacity * static_cast<float> (juce::jlimit (0.0, 1.0, level));
    return juce::jmin (1.0f, glowLevel + (hovered ? glow::hoverBoost : 0.0f));
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
    if (!value.isBeingEdited())
        value.setText (formatter ? formatter (dial.getValue()) : dial.getTextFromValue (dial.getValue()),
                       juce::dontSendNotification);

    if (auto* p = apvts.getParameter (paramIdString (paramId)))
    {
        // With a formatter, the value and range read the way the knob shows them.
        if (formatter)
            dial.setTooltip (help::name (paramId) + ": " + help::describe (paramId) + "\n" +
                             formatter (dial.getValue()) + "  (" + formatter (dial.getMinimum()) + " to " +
                             formatter (dial.getMaximum()) + ")");
        else
            dial.setTooltip (help::tooltip (paramId, *p));
    }
}

void ThunderKnob::setValueFormatter (std::function<juce::String (double)> format)
{
    formatter = std::move (format);
    updateValueText();
}

void ThunderKnob::showValueEditor()
{
    value.showEditor();
    if (auto* editor = value.getCurrentTextEditor())
    {
        editor->setJustification (juce::Justification::centred);
        editor->selectAll();
    }
}

bool ThunderKnob::enterValue (const juce::String& text)
{
    const auto parsed = params::fromText (params::paramDef (paramId), text);
    if (!parsed.has_value())
        return false;

    const juce::Slider::ScopedDragNotification gesture (dial); // one host gesture
    dial.setValue (*parsed, juce::sendNotificationSync);
    updateValueText();
    return true;
}

void ThunderKnob::resetToDefault()
{
    const juce::Slider::ScopedDragNotification gesture (dial);
    dial.setValue (params::paramDef (paramId).def, juce::sendNotificationSync);
}

void ThunderKnob::copyValue() const
{
    juce::SystemClipboard::copyTextToClipboard (value.getText());
}

bool ThunderKnob::pasteValue()
{
    return enterValue (juce::SystemClipboard::getTextFromClipboard());
}

void ThunderKnob::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Reset to default", [this] { resetToDefault(); });
    menu.addItem (juce::String::fromUTF8 ("Enter value\xe2\x80\xa6"), [this] { showValueEditor(); });
    menu.addSeparator();
    menu.addItem ("Copy value", [this] { copyValue(); });
    menu.addItem (
        "Paste value",
        params::fromText (params::paramDef (paramId), juce::SystemClipboard::getTextFromClipboard()).has_value(), false,
        [this] { pasteValue(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&dial));
}

} // namespace thunder::ui
