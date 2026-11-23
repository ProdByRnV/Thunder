#include "KeyBadge.h"

#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderPaint.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

KeyBadge::KeyBadge()
    : juce::Button ("Detected key")
{
    setTitle ("Detected key");
    setMouseClickGrabsKeyboardFocus (false);
    setWantsKeyboardFocus (true);
    describe();
}

juce::String KeyBadge::keyName (const analysis::KeyEstimate& e)
{
    if (!e.valid)
        return {};
    const auto root = juce::String (std::string (params::kKeyRootNames[params::enumIndex (e.root)]));
    return root + (e.scale == params::Scale::Major ? " major" : " minor");
}

void KeyBadge::setShown (const Shown& newShown)
{
    if (newShown == shown)
        return;
    shown = newShown;
    describe();
    repaint();
}

juce::String KeyBadge::getBadgeText() const
{
    if (!shown.estimate.valid)
        return juce::String::fromUTF8 ("\xe2\x80\x93"); // –
    return juce::String (juce::roundToInt (shown.estimate.confidence * 100.0f)) + "%";
}

juce::Colour KeyBadge::getDotColour() const
{
    if (!shown.estimate.valid || shown.estimate.confidence < kUnsure)
        return colour::textDisabled;
    return shown.estimate.confidence >= kConfident ? colour::accentPrimary : colour::stateWarning;
}

void KeyBadge::describe()
{
    juce::String message;
    if (shown.estimate.valid)
        message = "Detected from " + shown.from + ": " + keyName (shown.estimate) + " (" + getBadgeText() +
                  " sure). Click to use it.";
    else if (shown.listening)
        message = "Listening for the key. Click for key detection options.";
    else
        message = "Key detection is off. Click to turn it on.";
    setTooltip (message);
    setDescription (message);
}

void KeyBadge::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (border::width * 0.5f);
    paint::fillRaised (g, bounds, radius::sm, highlighted || down ? colour::bgHover : colour::bgRaised,
                       down ? colour::borderStrong : colour::borderSubtle);
    if (hasKeyboardFocus (false))
        paint::drawFocusRing (g, bounds, radius::sm);

    auto area = getLocalBounds().reduced (space::s2, 0);
    const auto dotSize = static_cast<float> (size::chainDot);
    const auto dot = area.removeFromLeft (size::chainDot).toFloat().withSizeKeepingCentre (dotSize, dotSize);
    g.setColour (getDotColour());
    g.fillEllipse (dot);

    area.removeFromLeft (space::s1);
    g.setColour (shown.estimate.valid ? colour::textPrimary : colour::textSecondary);
    g.setFont (fonts::mono (type::body));
    g.drawText (getBadgeText(), area, juce::Justification::centred);
}

} // namespace thunder::ui
