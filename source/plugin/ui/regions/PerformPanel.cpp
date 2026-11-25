#include "PerformPanel.h"

#include "plugin/ThunderProcessor.h"
#include "plugin/ui/Motion.h"
#include "plugin/ui/ParamHelp.h"
#include "plugin/ui/ParamUi.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderPaint.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;
using params::ParamId;

namespace
{
// A pad's or RISE's face: raised, outlined in the accent and glowing while on.
void paintPadFace (juce::Graphics& g, juce::Rectangle<float> bounds, bool on, bool highlighted, bool down)
{
    if (down)
        bounds = bounds.withSizeKeepingCentre (bounds.getWidth() * size::pressedScale,
                                               bounds.getHeight() * size::pressedScale);

    if (on)
    {
        juce::DropShadow (colour::glowPrimary.withAlpha (glow::strong.opacity), juce::roundToInt (glow::strong.blur),
                          {})
            .drawForRectangle (g, bounds.toNearestInt());
    }

    g.setColour (on ? colour::accentPrimaryDim : (highlighted ? colour::bgHover : colour::bgRaised));
    g.fillRoundedRectangle (bounds, radius::sm);
    g.setColour (on ? colour::accentPrimary : colour::borderSubtle);
    g.drawRoundedRectangle (bounds.reduced (border::width * 0.5f), radius::sm, border::width);
}

void paintPadLabel (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& text, bool on)
{
    g.setColour (on ? colour::textPrimary : colour::textSecondary);
    g.setFont (fonts::ui (type::label, fonts::Weight::semiBold).withExtraKerningFactor (type::labelTracking));
    g.drawText (text, bounds, juce::Justification::centred, false);
}
} // namespace

// ---- Pad ------------------------------------------------------------------------------------------------------------

PerformPanel::Pad::Pad (juce::AudioProcessorValueTreeState& apvts, ParamId id, const juce::String& label,
                        const juce::String& accessibleName)
    : juce::Button (label)
    , parameter (ui::parameter (apvts, id))
    , paramId (id)
{
    setTitle (accessibleName);
    setDescription (help::describe (id));
    setTooltip (help::tooltip (id, parameter));
    setWantsKeyboardFocus (true);
    setComponentID (paramIdString (id));
}

bool PerformPanel::Pad::isOn() const
{
    return parameter.getValue() >= 0.5f;
}

void PerformPanel::Pad::set (bool on)
{
    if (isOn() == on)
        return;
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (on ? 1.0f : 0.0f);
    parameter.endChangeGesture();
    repaint();
}

void PerformPanel::Pad::press (bool latch)
{
    if (latched)
    {
        // The next click lets a latched pad go.
        latched = false;
        holding = false;
        set (false);
        return;
    }

    latched = latch;
    holding = true;
    set (true);
}

void PerformPanel::Pad::release()
{
    if (!holding)
        return;
    holding = false;
    if (!latched)
        set (false);
}

void PerformPanel::Pad::mouseDown (const juce::MouseEvent& e)
{
    fromMouse = true;
    juce::Button::mouseDown (e);
    if (!e.mods.isPopupMenu())
        press (e.mods.isShiftDown());
}

void PerformPanel::Pad::mouseUp (const juce::MouseEvent& e)
{
    juce::Button::mouseUp (e);
    release();
    fromMouse = false;
}

void PerformPanel::Pad::clicked()
{
    // Mouse clicks are handled by press/release; this is the keyboard's or a
    // screen reader's press, which latches.
    if (fromMouse)
        return;
    if (latched || isOn())
    {
        latched = false;
        set (false);
    }
    else
    {
        latched = true;
        set (true);
    }
}

void PerformPanel::Pad::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto on = isOn();
    const auto bounds = getLocalBounds().toFloat().reduced (border::width * 0.5f);
    paintPadFace (g, bounds, on, highlighted, down);
    paintPadLabel (g, bounds, getButtonText(), on);
    if (hasKeyboardFocus (false))
        paint::drawFocusRing (g, bounds, radius::sm);
}

// ---- RISE -----------------------------------------------------------------------------------------------------------

PerformPanel::RiseButton::RiseButton (ThunderProcessor& p, juce::AudioProcessorValueTreeState& apvts)
    : juce::Button ("RISE")
    , processor (p)
    , go (ui::parameter (apvts, ParamId::riserGo))
{
    setTitle ("Riser Go");
    setDescription (help::describe (ParamId::riserGo));
    setTooltip (help::tooltip (ParamId::riserGo, go));
    setWantsKeyboardFocus (true);
    setComponentID (paramIdString (ParamId::riserGo));
}

void PerformPanel::RiseButton::setGo (bool on)
{
    if ((go.getValue() >= 0.5f) == on)
        return;
    go.beginChangeGesture();
    go.setValueNotifyingHost (on ? 1.0f : 0.0f);
    go.endChangeGesture();
}

void PerformPanel::RiseButton::rise()
{
    // The trigger reaches the engine even if the click is shorter than a host block.
    processor.triggerRiser();
    setGo (true);
}

void PerformPanel::RiseButton::mouseDown (const juce::MouseEvent& e)
{
    fromMouse = true;
    juce::Button::mouseDown (e);
    if (e.mods.isPopupMenu())
        return;
    held = true;
    rise();
}

void PerformPanel::RiseButton::mouseUp (const juce::MouseEvent& e)
{
    juce::Button::mouseUp (e);
    if (held)
    {
        held = false;
        setGo (false);
    }
    fromMouse = false;
}

void PerformPanel::RiseButton::clicked()
{
    if (fromMouse)
        return;
    rise();
    setGo (false);
}

void PerformPanel::RiseButton::setProgress (float p)
{
    if (juce::exactlyEqual (p, progress))
        return;
    progress = p;
    repaint();
}

void PerformPanel::RiseButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (border::width * 0.5f);
    const auto rising = progress >= 0.0f;
    paintPadFace (g, bounds, false, highlighted, down);

    if (rising)
    {
        // Fills left to right up to the bar line the rise ends on.
        g.setColour (colour::accentPrimaryDim);
        g.fillRoundedRectangle (bounds.withWidth (bounds.getWidth() * juce::jlimit (0.0f, 1.0f, progress)),
                                radius::sm);
        g.setColour (colour::accentPrimary);
        g.drawRoundedRectangle (bounds.reduced (border::width * 0.5f), radius::sm, border::width);
    }

    paintPadLabel (g, bounds, getButtonText(), rising);
    if (hasKeyboardFocus (false))
        paint::drawFocusRing (g, bounds, radius::sm);
}

// ---- The panel ------------------------------------------------------------------------------------------------------

PerformPanel::PerformPanel (ThunderProcessor& p)
    : processor (p)
    , storm (p.getValueTreeState(), ParamId::stormAmount, "STORM", ThunderKnob::Size::large)
    , variation (p.getValueTreeState(), ParamId::variationAmount, "VARIATION", ThunderKnob::Size::small)
    , freeze (p.getValueTreeState(), ParamId::grainFreeze, "FREEZE", "Freeze")
    , clap (p.getValueTreeState(), ParamId::strikeClap, "CLAP", "Thunderclap")
    , scatter (p.getValueTreeState(), ParamId::strikeScatter, "SCATTER", "Scatter")
    , riserBars (p.getValueTreeState(), ParamId::riserBars, {}, {"1", "2", "4", "8"})
    , rise (p, p.getValueTreeState())
{
    setTitle ("Perform");
    for (auto* c : std::initializer_list<juce::Component*>{&storm, &variation, &freeze, &clap, &scatter, &riserBars,
                                                           &rise})
        addAndMakeVisible (c);

    startTimerHz (timing::uiRefreshHz);
}

PerformPanel::~PerformPanel() = default;

void PerformPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colour::bgPanel);
    g.fillRoundedRectangle (bounds, radius::md);
    g.setColour (colour::borderSubtle);
    g.drawRoundedRectangle (bounds.reduced (border::width * 0.5f), radius::md, border::width);
}

void PerformPanel::resized()
{
    auto area = getLocalBounds().reduced (space::s3);

    auto riser = area.removeFromBottom (size::riserHeight);
    riserBars.setBounds (riser.removeFromLeft (riser.getWidth() / 2 - space::s1));
    riser.removeFromLeft (space::s2);
    rise.setBounds (riser);
    area.removeFromBottom (space::s2);

    auto pads = area.removeFromBottom (size::strikePadHeight);
    const auto padWidth = (pads.getWidth() - 2 * space::s2) / 3;
    freeze.setBounds (pads.removeFromLeft (padWidth));
    pads.removeFromLeft (space::s2);
    clap.setBounds (pads.removeFromLeft (padWidth));
    pads.removeFromLeft (space::s2);
    scatter.setBounds (pads);
    area.removeFromBottom (space::s2);

    // Storm is the biggest knob in the window; Variation sits beside it.
    const auto stormSize = ThunderKnob::preferredBounds (ThunderKnob::Size::large);
    const auto varSize = ThunderKnob::preferredBounds (ThunderKnob::Size::small);
    storm.setBounds (area.removeFromLeft (stormSize.getWidth()).withSizeKeepingCentre (stormSize.getWidth(),
                                                                                       std::min (area.getHeight(), stormSize.getHeight())));
    variation.setBounds (area.withSizeKeepingCentre (varSize.getWidth(), std::min (area.getHeight(), varSize.getHeight())));
}

void PerformPanel::timerCallback()
{
    refresh();
}

void PerformPanel::refresh()
{
    for (auto* pad : {&freeze, &clap, &scatter})
        pad->repaint();

    const auto progress = processor.getRiserProgress();
    rise.setProgress (progress);
    wasRising = progress >= 0.0f;
}

} // namespace thunder::ui
