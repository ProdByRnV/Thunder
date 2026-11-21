#include "AboutPanel.h"

#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderIcons.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

AboutPanel::AboutPanel()
{
    setTitle ("About Thunder");
    setWantsKeyboardFocus (true);

    reportButton.setTooltip ("Opens Thunder's issue page in your browser");
    reportButton.onClick = [] { juce::URL (issuesUrl).launchInDefaultBrowser(); };
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (reportButton);
    addAndMakeVisible (closeButton);
}

juce::String AboutPanel::versionLine()
{
    // __DATE__ is "Mmm dd yyyy"; shown as "dd Mmm yyyy".
    const auto date = juce::StringArray::fromTokens (__DATE__, " ", "");
    const auto built = date.size() == 3 ? date[1] + " " + date[0] + " " + date[2] : juce::String (__DATE__);
    return "Version " + juce::String (THUNDER_VERSION_STRING) + juce::String::fromUTF8 (" \xc2\xb7 built ") + built;
}

juce::Rectangle<int> AboutPanel::dialogBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (size::aboutWidth, size::aboutHeight);
}

void AboutPanel::paint (juce::Graphics& g)
{
    g.fillAll (colour::bgOverlay);

    const auto box = dialogBounds();
    paint::drawShadow (g, box, elevation::dialog);
    g.setColour (colour::bgPanel);
    g.fillRoundedRectangle (box.toFloat(), radius::lg);
    g.setColour (colour::borderSubtle);
    g.drawRoundedRectangle (box.toFloat().reduced (border::width * 0.5f), radius::lg, border::width);

    auto area = box.reduced (space::s5);
    auto logo = area.removeFromTop (size::aboutBolt);
    g.setColour (colour::accentPrimary);
    g.fillPath (icons::fitted (icons::bolt(), logo.removeFromLeft (size::aboutBolt).toFloat()));
    logo.removeFromLeft (space::s2);
    g.setColour (colour::textPrimary);
    g.setFont (
        fonts::ui (type::aboutWordmark, fonts::Weight::semiBold).withExtraKerningFactor (type::wordmarkTracking));
    g.drawText ("THUNDER", logo, juce::Justification::centredLeft);

    area.removeFromTop (space::s3);
    g.setColour (colour::textSecondary);
    g.setFont (fonts::mono (type::value));
    g.drawText (versionLine(), area.removeFromTop (size::valueHeight), juce::Justification::centredLeft);

    area.removeFromTop (space::s2);
    g.setFont (fonts::ui (type::body));
    g.drawText ("Turns a melody into an in-key texture layer.", area.removeFromTop (size::valueHeight),
                juce::Justification::centredLeft);
}

void AboutPanel::resized()
{
    auto buttons = dialogBounds().reduced (space::s5).removeFromBottom (size::controlHeight);
    closeButton.setBounds (buttons.removeFromRight (size::dialogButtonWidth));
    buttons.removeFromRight (space::s2);
    reportButton.setBounds (buttons.removeFromRight (size::dialogButtonWidth + space::s7));
}

void AboutPanel::mouseUp (const juce::MouseEvent& e)
{
    if (!dialogBounds().contains (e.getPosition()))
        close();
}

bool AboutPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::returnKey)
    {
        close();
        return true;
    }
    return false;
}

void AboutPanel::close()
{
    if (onClose)
        onClose();
}

} // namespace thunder::ui
