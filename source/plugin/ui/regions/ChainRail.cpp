#include "ChainRail.h"

#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

// C-CHAINNODE (UI.md §9.3): a pill with the module's name and an on/off dot.
class ChainRail::Node final : public juce::Component
{
public:
    Node (ChainRail& o, int i, juce::AudioProcessorValueTreeState& apvts, const Module& module)
        : owner (o)
        , index (i)
        , name (module.name)
        , dot (apvts, module.onParam)
    {
        setTitle (module.name);
        setTooltip (module.name + ": click to show; the dot switches it on or off");
        dot.button.setTooltip ("Switch " + module.name + " on or off");
        addAndMakeVisible (dot.button);
        watcher = std::make_unique<ParameterWatcher> (apvts, module.onParam,
                                                      [this] (float v)
                                                      {
                                                          on = v >= 0.5f;
                                                          repaint();
                                                      });
    }

    void setTooltip (const juce::String& t) { tooltip.setTooltip (t); }

    void paint (juce::Graphics& g) override
    {
        const auto isSelected = owner.selected == index;
        const auto bounds = getLocalBounds().toFloat().reduced (border::width * 0.5f);
        const auto r = bounds.getHeight() * 0.5f;

        g.setColour (on || isSelected ? colour::bgRaised : colour::bgPanel);
        g.fillRoundedRectangle (bounds, r);
        g.setColour (isSelected ? colour::accentPrimary : (on ? colour::borderStrong : colour::borderSubtle));
        g.drawRoundedRectangle (bounds, r, border::width);

        g.setColour (on ? colour::textPrimary : colour::textSecondary);
        g.setFont (fonts::ui (type::label, fonts::Weight::semiBold).withExtraKerningFactor (type::labelTracking));
        g.drawText (name.toUpperCase(), getLocalBounds().withTrimmedLeft (size::chainDot + space::s3),
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
        tooltip.setBounds (getLocalBounds());
        dot.button.setBounds (getLocalBounds().removeFromLeft (size::chainDot + space::s4));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && !e.mods.isPopupMenu())
            owner.clickNode (index);
    }

    void toggle() { dot.button.setToggleState (!dot.button.getToggleState(), juce::sendNotificationSync); }

private:
    // The dot's button: invisible, the node draws it.
    struct Dot
    {
        Dot (juce::AudioProcessorValueTreeState& apvts, params::ParamId id)
            : attachment (apvts, paramIdString (id), button)
        {
            button.setClickingTogglesState (true);
            button.setAlpha (0.0f);
        }

        juce::ToggleButton button;
        juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
    };

    // Gives the whole node a tooltip without stealing its clicks.
    struct TooltipArea final
        : public juce::Component
        , public juce::SettableTooltipClient
    {
        TooltipArea() { setInterceptsMouseClicks (false, false); }
    };

    ChainRail& owner;
    int index;
    juce::String name;
    Dot dot;
    TooltipArea tooltip;
    bool on = true;
    std::unique_ptr<ParameterWatcher> watcher;

    friend class ChainRail;
};

ChainRail::ChainRail (juce::AudioProcessorValueTreeState& apvts, std::vector<Module> modules)
{
    setTitle ("Module chain");
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

    g.setColour (colour::borderStrong);
    g.drawLine (static_cast<float> (area.getX()), y, static_cast<float> (area.getRight()), y, size::chainWire);

    g.setColour (colour::borderSubtle);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));
}

void ChainRail::resized()
{
    auto area = getLocalBounds().reduced (space::s4, 0);
    area.removeFromLeft (space::s6);
    area.removeFromRight (space::s6);

    const auto count = static_cast<int> (nodes.size());
    const auto slot = count > 0 ? area.getWidth() / count : 0;
    for (int i = 0; i < count; ++i)
    {
        const auto cell = area.withX (area.getX() + i * slot).withWidth (slot);
        nodes[static_cast<std::size_t> (i)]->setBounds (
            cell.withSizeKeepingCentre (size::chainNodeWidth, size::chainNodeHeight));
    }
}

} // namespace thunder::ui
