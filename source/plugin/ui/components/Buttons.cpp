#include "Buttons.h"

#include "plugin/ui/theme/ThunderFonts.h"

namespace thunder::ui
{

using namespace theme;

// ---- ThunderButton
// ---------------------------------------------------------------------------------------------------

ThunderButton::ThunderButton (const juce::String& buttonText, Variant v)
    : juce::TextButton (buttonText)
    , variant (v)
    , flash (*this)
{
    setMouseClickGrabsKeyboardFocus (false);
    setWantsKeyboardFocus (true);
}

void ThunderButton::setVariant (Variant v)
{
    variant = v;
    repaint();
}

void ThunderButton::clicked()
{
    flash.trigger();
}

void ThunderButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced (border::width * 0.5f);
    const auto enabled = isEnabled();

    if (down && enabled)
        bounds = bounds.withSizeKeepingCentre (bounds.getWidth() * size::pressedScale,
                                               bounds.getHeight() * size::pressedScale);

    auto fill = colour::bgRaised;
    auto outline = colour::borderSubtle;
    auto textColour = colour::textPrimary;

    switch (variant)
    {
        case Variant::primary:
            fill = enabled ? colour::accentPrimary : colour::textDisabled;
            outline = fill;
            textColour = colour::textOnAccent;
            break;
        case Variant::secondary:
            fill = highlighted || down ? colour::bgHover : colour::bgRaised;
            break;
        case Variant::ghost:
            fill = highlighted || down ? colour::bgHover : juce::Colours::transparentBlack;
            outline = juce::Colours::transparentBlack;
            textColour = highlighted ? colour::textPrimary : colour::textSecondary;
            break;
        case Variant::danger:
            fill = highlighted || down ? colour::bgHover : colour::bgRaised;
            outline = colour::stateError;
            textColour = colour::stateError;
            break;
    }

    if (!enabled && variant != Variant::primary)
        textColour = colour::textDisabled;

    if (variant == Variant::ghost)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, radius::sm);
    }
    else
    {
        paint::fillRaised (g, bounds, radius::sm, fill, outline);
    }

    // The click flash: the outline lights up and fades (§9.6).
    if (const auto level = flash.getLevel(); level > 0.0f)
    {
        g.setColour (colour::glowPrimary.withAlpha (glow::medium.opacity * level));
        g.drawRoundedRectangle (bounds.expanded (border::width), radius::sm + border::width, border::width * 2.0f);
        g.setColour (colour::accentPrimary.withAlpha (level));
        g.drawRoundedRectangle (bounds, radius::sm, border::width);
    }

    if (hasKeyboardFocus (false))
        paint::drawFocusRing (g, bounds, radius::sm);

    g.setColour (textColour);
    g.setFont (fonts::ui (type::body, fonts::Weight::medium));
    g.drawFittedText (getButtonText(), bounds.toNearestInt().reduced (space::s2, 0), juce::Justification::centred, 1);
}

// ---- IconButton
// ------------------------------------------------------------------------------------------------------

IconButton::IconButton (const juce::String& name, icons::Icon glyph)
    : juce::Button (name)
    , icon (std::move (glyph))
{
    setTitle (name);
    setTooltip (name);
    setMouseClickGrabsKeyboardFocus (false);
    setWantsKeyboardFocus (true);
}

void IconButton::setIcon (icons::Icon glyph)
{
    icon = std::move (glyph);
    repaint();
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto on = getToggleState();

    if (highlighted || down)
    {
        g.setColour (colour::bgHover);
        g.fillRoundedRectangle (bounds, radius::sm);
    }

    if (hasKeyboardFocus (false))
        paint::drawFocusRing (g, bounds, radius::sm);

    g.setColour (!isEnabled()
                     ? colour::textDisabled
                     : (on ? colour::accentPrimary : (highlighted ? colour::textPrimary : colour::textSecondary)));
    const auto glyph = static_cast<float> (size::icon);
    icons::draw (g, icon, bounds.withSizeKeepingCentre (glyph, glyph), size::iconStroke);
}

} // namespace thunder::ui
