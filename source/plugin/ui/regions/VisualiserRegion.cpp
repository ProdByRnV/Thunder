#include "VisualiserRegion.h"

#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderPaint.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

// Shows the view toggle at full strength while the pointer is over it.
class VisualiserRegion::HoverFade final : public juce::MouseListener
{
public:
    explicit HoverFade (juce::Component& c)
        : target (c)
    {
        target.addMouseListener (this, true);
        update();
    }

    ~HoverFade() override { target.removeMouseListener (this); }

    void mouseEnter (const juce::MouseEvent&) override { update(); }
    void mouseExit (const juce::MouseEvent&) override { update(); }

    void update() { target.setAlpha (target.isMouseOver (true) ? 1.0f : opacity::viewToggle); }

private:
    juce::Component& target;
};

// The well while a file is dragged over the window (UI.md §16.2): a dashed cyan
// outline and "Drop to load as source", or "File type not supported".
class VisualiserRegion::DropOverlay final : public juce::Component
{
public:
    DropOverlay()
    {
        setInterceptsMouseClicks (false, false);
        setAccessible (false);
    }

    void setHint (DropHint newHint)
    {
        hint = newHint;
        setVisible (hint != DropHint::none);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto supported = hint == DropHint::supported;
        const auto tint = supported ? colour::accentPrimary : colour::stateError;
        const auto bounds = getLocalBounds().toFloat().reduced (static_cast<float> (space::s1));

        g.setColour (tint.withAlpha (visualiser::dropFill));
        g.fillRoundedRectangle (bounds, radius::md);

        juce::Path outline;
        outline.addRoundedRectangle (bounds, radius::md);
        juce::Path dashed;
        const float dashes[] = {visualiser::dropDash, visualiser::dropGap};
        juce::PathStrokeType (2.0f * border::width).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (tint);
        g.fillPath (dashed);

        // The words on a plate, so the waveform under them doesn't get in the way.
        const juce::String text (supported ? "Drop to load as source" : "File type not supported");
        const auto font = fonts::ui (type::title, fonts::Weight::semiBold);
        const auto plateWidth =
            juce::GlyphArrangement::getStringWidth (font, text) + 2.0f * static_cast<float> (space::s4);
        const auto plate = getLocalBounds().toFloat().withSizeKeepingCentre (
            plateWidth, static_cast<float> (size::controlHeight + space::s2));
        paint::fillRaised (g, plate, radius::md, colour::bgRaised, tint);
        g.setColour (tint);
        g.setFont (font);
        g.drawText (text, plate, juce::Justification::centred);
    }

private:
    DropHint hint = DropHint::none;
};

VisualiserRegion::VisualiserRegion (VisualModel& m, juce::AudioProcessorValueTreeState& apvts)
    : model (m)
    , waveform (m)
    , spectrum (m, apvts)
    , toggle ("Visualiser view", {"Wave", "Spectrum"},
              [this] (int index) { chooseView (index == 0 ? View::wave : View::spectrum); })
{
    setTitle ("Visualiser");
    setDescription ("The dry signal and the layer as they play. Right-click for the time window and display options.");

    addAndMakeVisible (waveform);
    addChildComponent (spectrum);
    addAndMakeVisible (lightning);
    addAndMakeVisible (toggle);
    addChildComponent (sourceStrip);
    dropOverlay = std::make_unique<DropOverlay>();
    addChildComponent (*dropOverlay);
    hoverFade = std::make_unique<HoverFade> (toggle);
    setView (View::wave);
}

VisualiserRegion::~VisualiserRegion() = default;

void VisualiserRegion::resized()
{
    // Inside the well's border.
    const auto inner = getLocalBounds().reduced (static_cast<int> (border::width));
    waveform.setBounds (inner);
    spectrum.setBounds (inner);
    lightning.setBounds (inner);
    toggle.setBounds (
        inner.reduced (space::s2).removeFromBottom (size::viewToggleHeight).removeFromRight (size::viewToggleWidth));
    lightning.setKeepClear (toggle.getBounds() - lightning.getPosition());
    sourceStrip.setBounds (inner.withHeight (visualiser::sourceStripHeight));
    dropOverlay->setBounds (inner);
}

void VisualiserRegion::setDropHint (DropHint hint)
{
    dropHint = hint;
    dropOverlay->setHint (hint);
    if (hint != DropHint::none)
        dropOverlay->toFront (false);
}

void VisualiserRegion::mouseDown (const juce::MouseEvent& e)
{
    if (!e.mods.isPopupMenu())
        return;

    buildMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                               [safe = juce::Component::SafePointer<VisualiserRegion> (this)] (int result)
                               {
                                   if (safe != nullptr && result != 0)
                                       safe->chooseMenuItem (result);
                               });
}

void VisualiserRegion::setView (View newView)
{
    view = newView;
    waveform.setVisible (view == View::wave);
    spectrum.setVisible (view == View::spectrum);
    toggle.setSelectedOption (view == View::wave ? 0 : 1);
}

void VisualiserRegion::chooseView (View newView)
{
    if (newView == view)
        return;

    setView (newView);
    if (onViewChosen)
        onViewChosen (view);
}

void VisualiserRegion::setReduceMotion (bool reduce)
{
    waveform.setReduceMotion (reduce);
    if (reduce)
        lightning.clear();
}

void VisualiserRegion::setOptions (const Options& newOptions)
{
    options = newOptions;
    waveform.setBars (options.bars);
    waveform.setShowDry (options.showDry);
    waveform.setShowGrains (options.showGrains);
    spectrum.setShowDry (options.showDry);
}

std::uint32_t VisualiserRegion::wantedParts() const noexcept
{
    // The meters always need frames.
    auto parts = UiBridge::kFrames;
    if (view == View::spectrum)
        parts |= UiBridge::kSpectrum;
    else if (options.showGrains)
        parts |= UiBridge::kGrains;
    return parts;
}

void VisualiserRegion::refresh (const VisualModel::Changes& changes, double seconds)
{
    if (view == View::wave)
    {
        const auto grains = waveform.takeGrains (seconds);
        if (changes.waveform || grains)
            waveform.repaint();
    }
    else if (changes.spectrum)
    {
        spectrum.repaint();
    }

    // Lightning (UI.md §12.3): a transient strikes a bolt as bright as the
    // output is loud; the strikes fade on their own.
    lightning.advance (seconds);
    if (changes.strike)
    {
        const auto& strikes = model.getStrikes();
        lightning.strike (strikes.getStrikePeak(), strikes.getBrightness());
    }
}

juce::PopupMenu VisualiserRegion::buildMenu() const
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Time window");
    for (const auto bars : {1, 2, 4})
        menu.addItem (kMenuBars + bars, bars == 1 ? juce::String ("1 bar") : juce::String (bars) + " bars", true,
                      options.bars == bars);
    menu.addSeparator();
    menu.addItem (kMenuFreeze, "Freeze display", true, model.isFrozen());
    menu.addItem (kMenuShowDry, "Show dry", true, options.showDry);
    menu.addItem (kMenuShowGrains, "Show grains", view == View::wave, options.showGrains);
    return menu;
}

void VisualiserRegion::chooseMenuItem (int itemId)
{
    auto next = options;
    switch (itemId)
    {
        case kMenuBars + 1:
        case kMenuBars + 2:
        case kMenuBars + 4:
            next.bars = itemId - kMenuBars;
            break;
        case kMenuFreeze:
            model.setFrozen (!model.isFrozen());
            return;
        case kMenuShowDry:
            next.showDry = !next.showDry;
            break;
        case kMenuShowGrains:
            next.showGrains = !next.showGrains;
            break;
        default:
            return;
    }

    setOptions (next);
    if (onOptionsChanged)
        onOptionsChanged (options);
}

} // namespace thunder::ui
