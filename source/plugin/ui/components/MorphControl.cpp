#include "MorphControl.h"

#include "plugin/ui/ParamHelp.h"
#include "plugin/ui/ParamUi.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderPaint.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

// ---- MorphSlider
// -----------------------------------------------------------------------------------------------------

MorphControl::MorphSlider::MorphSlider (MorphControl& o)
    : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox)
    , owner (o)
{
    setTitle ("Morph");
    setMouseClickGrabsKeyboardFocus (false);
    setWantsKeyboardFocus (true);
    setDoubleClickReturnValue (true, 0.0);
}

void MorphControl::MorphSlider::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto y = bounds.getCentreY();
    const auto thumb = static_cast<float> (size::morphThumb);
    const auto left = bounds.getX() + thumb * 0.5f;
    const auto right = bounds.getRight() - thumb * 0.5f;
    const auto enabled = isEnabled();

    // Track, the part towards B in the accent, and the thumb.
    g.setColour (colour::borderStrong);
    g.drawLine (left, y, right, y, size::morphTrack);

    const auto x = left + static_cast<float> (valueToProportionOfLength (getValue())) * (right - left);
    if (enabled)
    {
        g.setColour (colour::accentPrimary);
        g.drawLine (left, y, x, y, size::morphTrack);
        g.setColour (colour::textPrimary);
        g.fillEllipse (juce::Rectangle<float> (thumb, thumb).withCentre ({x, y}));
    }
    else
    {
        // No B yet: say what to do instead of showing a thumb.
        g.setColour (colour::bgPanel);
        g.fillRect (bounds.withSizeKeepingCentre (bounds.getWidth(), static_cast<float> (size::labelHeight)));
        g.setColour (colour::textSecondary);
        g.setFont (fonts::ui (type::caption));
        g.drawText ("Set B to morph", bounds, juce::Justification::centred);
    }

    if (hasKeyboardFocus (false))
        paint::drawFocusRing (g, bounds, radius::sm);
}

void MorphControl::MorphSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || !isEnabled())
    {
        owner.showMenu();
        return;
    }
    juce::Slider::mouseDown (e);
}

// ---- MorphControl
// ----------------------------------------------------------------------------------------------------

MorphControl::MorphControl (juce::AudioProcessorValueTreeState& apvts)
    : state (apvts)
    , slider (*this)
{
    setTitle ("Morph");
    slider.setComponentID (paramIdString (params::ParamId::morphAmount));
    slider.setDescription (help::describe (params::ParamId::morphAmount));
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, paramIdString (params::ParamId::morphAmount), slider);
    slider.onValueChange = [this] { describe(); };
    addAndMakeVisible (slider);
    refresh();
}

MorphControl::~MorphControl() = default;

juce::Rectangle<int> MorphControl::aArea() const
{
    return getLocalBounds().removeFromLeft (size::morphEnd);
}

juce::Rectangle<int> MorphControl::bArea() const
{
    return getLocalBounds().removeFromRight (size::morphEnd);
}

void MorphControl::refresh()
{
    const auto name = getSoundBName ? getSoundBName() : std::optional<juce::String>();
    if (name == bName && slider.isEnabled() == name.has_value())
        return;

    bName = name;
    slider.setEnabled (bName.has_value());
    slider.setInterceptsMouseClicks (bName.has_value(), false); // without B, a click opens the menu
    describe();
    repaint();
}

void MorphControl::describe()
{
    // Like every control: name, what it does, value and range; then sound B.
    auto text = help::tooltip (params::ParamId::morphAmount, parameter (state, params::ParamId::morphAmount));
    text << "\n" << (bName ? "B: " + *bName : juce::String ("Set sound B first (right-click)"));
    slider.setTooltip (text);
}

void MorphControl::paint (juce::Graphics& g)
{
    g.setFont (fonts::ui (type::label, fonts::Weight::semiBold));
    g.setColour (colour::textSecondary);
    g.drawText ("A", aArea(), juce::Justification::centred);
    g.setColour (bName ? colour::textPrimary : colour::textSecondary);
    g.drawText ("B", bArea(), juce::Justification::centred);
}

void MorphControl::resized()
{
    slider.setBounds (getLocalBounds()
                          .reduced (size::morphEnd, 0)
                          .withSizeKeepingCentre (getWidth() - 2 * size::morphEnd, size::controlHeight));
}

void MorphControl::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && (e.mods.isPopupMenu() || !bName || bArea().contains (e.getPosition())))
        showMenu();
}

juce::PopupMenu MorphControl::buildMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (bName ? "Sound B: " + *bName : juce::String ("No sound B"));
    menu.addItem (kMenuSetCurrent, "Set B = current sound");

    menuPresets = getPresets ? getPresets() : std::vector<presets::Entry>{};
    juce::PopupMenu factory, user;
    for (std::size_t i = 0; i < menuPresets.size(); ++i)
    {
        const auto& e = menuPresets[i];
        (e.isFactory ? factory : user).addItem (kMenuFirstPreset + static_cast<int> (i), e.name);
    }
    juce::PopupMenu fromPreset;
    if (factory.getNumItems() > 0)
        fromPreset.addSubMenu (presets::kFactoryBank, factory);
    if (user.getNumItems() > 0)
        fromPreset.addSubMenu ("Your presets", user);
    menu.addSubMenu (juce::String::fromUTF8 ("Load B from preset"), fromPreset, !menuPresets.empty());

    menu.addSeparator();
    menu.addItem (kMenuClear, "Clear B", bName.has_value());
    return menu;
}

void MorphControl::chooseMenuItem (int itemId)
{
    if (itemId == kMenuSetCurrent && setBFromCurrent)
        setBFromCurrent();
    else if (itemId == kMenuClear && clearB)
        clearB();
    else if (itemId >= kMenuFirstPreset && setBFromPreset)
    {
        const auto index = static_cast<std::size_t> (itemId - kMenuFirstPreset);
        if (index < menuPresets.size())
            setBFromPreset (menuPresets[index]);
    }

    refresh();
}

void MorphControl::showMenu()
{
    buildMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                               [safe = juce::Component::SafePointer<MorphControl> (this)] (int result)
                               {
                                   if (safe != nullptr && result != 0)
                                       safe->chooseMenuItem (result);
                               });
}

} // namespace thunder::ui
