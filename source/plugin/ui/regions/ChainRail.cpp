#include "ChainRail.h"

#include "plugin/ui/ParamHelp.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderPaint.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

// C-CHAINNODE (UI.md §9.3): a pill with the module's name and an on/off dot.
// The selected node is outlined in the accent with a soft glow and a notch
// pointing down at the module panel. Screen readers see a button ("Pitch
// module") that shows the module, and a switch ("Pitch on").
class ChainRail::Node final : public juce::Component
{
public:
    Node (ChainRail& o, int i, juce::AudioProcessorValueTreeState& apvts, const Module& module)
        : owner (o)
        , index (i)
        , name (module.name)
        , dot (apvts, module.onParam)
    {
        setTitle (module.name + " module");
        setDescription ("Shows the " + module.name + " controls");
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (false);
        tooltip.setTooltip (module.name + ": click to show its controls; the dot switches it on or off");
        dot.button.setTitle (module.name + " on");
        dot.button.setDescription (help::describe (module.onParam));
        dot.button.setTooltip ("Switch " + module.name + " on or off");
        addAndMakeVisible (tooltip);
        addAndMakeVisible (dot.button);
        watcher = std::make_unique<ParameterWatcher> (apvts, module.onParam,
                                                      [this] (float v)
                                                      {
                                                          on = v >= 0.5f;
                                                          repaint();
                                                          owner.repaint(); // the lit wire
                                                      });
    }

    void paint (juce::Graphics& g) override
    {
        const auto isSelected = owner.selected == index;
        const auto bounds = pill();
        const auto r = bounds.getHeight() * 0.5f;

        if (isSelected)
        {
            juce::Path outline;
            outline.addRoundedRectangle (bounds, r);
            selectedGlow.draw (g, outline, colour::glowPrimary, glow::subtle, glow::subtle.opacity);
        }

        g.setColour (on || isSelected ? colour::bgRaised : colour::bgPanel);
        g.fillRoundedRectangle (bounds, r);
        g.setColour (isSelected ? colour::accentPrimary : (on ? colour::borderStrong : colour::borderSubtle));
        g.drawRoundedRectangle (bounds, r, border::width);

        if (isSelected)
        {
            // The notch under the node, pointing at the module panel.
            const auto n = size::chainNotch;
            juce::Path notch;
            notch.addTriangle (bounds.getCentreX() - n, bounds.getBottom() + border::width, bounds.getCentreX() + n,
                               bounds.getBottom() + border::width, bounds.getCentreX(),
                               bounds.getBottom() + border::width + n);
            g.setColour (colour::accentPrimary);
            g.fillPath (notch);
        }

        if (hasKeyboardFocus (false))
            paint::drawFocusRing (g, bounds, r);

        g.setColour (on || isSelected ? colour::textPrimary : colour::textSecondary);
        g.setFont (fonts::ui (type::label, fonts::Weight::semiBold).withExtraKerningFactor (type::labelTracking));
        g.drawText (name.toUpperCase(), bounds.toNearestInt().withTrimmedLeft (size::chainDot + space::s3),
                    juce::Justification::centred);

        // The dot: filled = on, hollow = off (not colour alone; UI.md §14).
        const auto d = static_cast<float> (size::chainDot);
        const auto dotArea = juce::Rectangle<float> (d, d).withCentre (dot.button.getBounds().toFloat().getCentre());
        if (on)
        {
            g.setColour (colour::accentPrimary);
            g.fillEllipse (dotArea);
        }
        else
        {
            g.setColour (colour::textSecondary);
            g.drawEllipse (dotArea.reduced (border::width * 0.5f), border::width);
        }
    }

    void resized() override
    {
        const auto bounds = pill().toNearestInt();
        tooltip.setBounds (bounds);
        dot.button.setBounds (bounds.withWidth (size::chainDot + space::s4));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && !e.mods.isPopupMenu())
            owner.clickNode (index);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey)
        {
            owner.clickNode (index);
            return true;
        }
        return false;
    }

    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    void toggle() { dot.button.setToggleState (!dot.button.getToggleState(), juce::sendNotificationSync); }

    // The node's pill: the component leaves room around it for the glow and notch.
    juce::Rectangle<float> pill() const
    {
        return getLocalBounds()
            .toFloat()
            .withSizeKeepingCentre (static_cast<float> (size::chainNodeWidth),
                                    static_cast<float> (size::chainNodeHeight))
            .reduced (border::width * 0.5f);
    }

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler> (
            *this, juce::AccessibilityRole::button,
            juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press,
                                                    [this] { owner.clickNode (index); }));
    }

    // The dot's button: invisible, the node draws it.
    struct Dot
    {
        Dot (juce::AudioProcessorValueTreeState& apvts, params::ParamId id)
            : attachment (apvts, paramIdString (id), button)
        {
            button.setClickingTogglesState (true);
            button.setAlpha (0.0f);
            button.setMouseClickGrabsKeyboardFocus (false);
        }

        juce::ToggleButton button;
        juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
    };

    // Gives the whole node a tooltip without stealing its clicks.
    struct TooltipArea final
        : public juce::Component
        , public juce::SettableTooltipClient
    {
        TooltipArea()
        {
            setInterceptsMouseClicks (false, false);
            setAccessible (false);
        }
    };

    ChainRail& owner;
    int index;
    juce::String name;
    TooltipArea tooltip;
    Dot dot;
    bool on = true;
    paint::GlowCache selectedGlow;
    std::unique_ptr<ParameterWatcher> watcher;

    friend class ChainRail;
};

ChainRail::ChainRail (juce::AudioProcessorValueTreeState& apvts, std::vector<Module> modules)
{
    setTitle ("Module chain");
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);
    for (std::size_t i = 0; i < modules.size(); ++i)
    {
        nodes.push_back (std::make_unique<Node> (*this, static_cast<int> (i), apvts, modules[i]));
        addAndMakeVisible (*nodes.back());
    }
}

ChainRail::~ChainRail() = default;

void ChainRail::setSelected (int index)
{
    selected = juce::jlimit (0, static_cast<int> (nodes.size()) - 1, index);
    repaint();
    for (auto& n : nodes)
        n->repaint();
}

void ChainRail::clickNode (int index)
{
    setSelected (index);
    if (onSelect)
        onSelect (selected);
}

void ChainRail::clickDot (int index)
{
    if (index >= 0 && index < static_cast<int> (nodes.size()))
        nodes[static_cast<std::size_t> (index)]->toggle();
}

void ChainRail::paint (juce::Graphics& g)
{
    g.fillAll (colour::bgPanel);

    const auto y = static_cast<float> (getHeight()) * 0.5f;
    auto area = getLocalBounds().reduced (space::s4, 0);

    g.setColour (colour::textSecondary);
    g.setFont (fonts::ui (type::label, fonts::Weight::semiBold).withExtraKerningFactor (type::labelTracking));
    const auto in = area.removeFromLeft (space::s6);
    const auto out = area.removeFromRight (space::s6);
    g.drawText ("IN", in, juce::Justification::centredLeft);
    g.drawText ("OUT", out, juce::Justification::centredRight);

    // The wire: the stretch between two switched-on modules is lit (static
    // until the energy pulse arrives with the motion work, UI.md §8.3).
    g.setColour (colour::borderStrong);
    g.drawLine (static_cast<float> (area.getX()), y, static_cast<float> (area.getRight()), y, size::chainWire);

    for (std::size_t i = 0; i + 1 < nodes.size(); ++i)
        if (nodes[i]->on && nodes[i + 1]->on)
        {
            const auto from = nodes[i]->getBounds().toFloat().getCentreX() + size::chainNodeWidth * 0.5f;
            const auto to = nodes[i + 1]->getBounds().toFloat().getCentreX() - size::chainNodeWidth * 0.5f;
            g.setColour (colour::accentPrimaryDim);
            g.drawLine (from, y, to, y, size::chainWire);
        }

    g.setColour (colour::borderSubtle);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));
}

void ChainRail::resized()
{
    auto area = getLocalBounds().reduced (space::s4, 0);
    area.removeFromLeft (space::s6);
    area.removeFromRight (space::s6);

    // Each node gets room around its pill for the selection glow and notch.
    const auto count = static_cast<int> (nodes.size());
    const auto slot = count > 0 ? area.getWidth() / count : 0;
    for (int i = 0; i < count; ++i)
    {
        const auto cell = area.withX (area.getX() + i * slot).withWidth (slot);
        nodes[static_cast<std::size_t> (i)]->setBounds (
            cell.withSizeKeepingCentre (size::chainNodeWidth + 2 * space::s3, size::chainNodeHeight + 2 * space::s3));
    }
}

} // namespace thunder::ui
