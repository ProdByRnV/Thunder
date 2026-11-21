#include "Controls.h"

#include "plugin/ui/ParamUi.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

// ---- SegmentedControl
// ------------------------------------------------------------------------------------------------

class SegmentedControl::Segment final : public juce::Button
{
public:
    Segment (SegmentedControl& o, int i, const juce::String& label)
        : juce::Button (label)
        , owner (o)
        , index (i)
    {
        setClickingTogglesState (false);
        onClick = [this] { owner.choose (index); };
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto isSelected = owner.selected == index;
        const auto bounds = getLocalBounds().toFloat().reduced (border::width);

        if (isSelected || highlighted)
        {
            g.setColour (isSelected ? (isEnabled() ? colour::accentPrimary : colour::textDisabled) : colour::bgHover);
            g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
        }

        g.setColour (isSelected ? colour::textOnAccent : (highlighted ? colour::textPrimary : colour::textSecondary));
        g.setFont (fonts::ui (type::label, fonts::Weight::semiBold));
        g.drawFittedText (getButtonText().toUpperCase(), getLocalBounds().reduced (space::s1, 0),
                          juce::Justification::centred, 1);
    }

private:
    SegmentedControl& owner;
    int index;
};

SegmentedControl::SegmentedControl (juce::AudioProcessorValueTreeState& apvts, params::ParamId id,
                                    std::vector<int> options, juce::StringArray labels)
    : optionIndices (std::move (options))
{
    const auto& def = params::paramDef (id);
    jassert (def.kind == params::Kind::Choice || def.kind == params::Kind::Bool);

    const auto count = def.kind == params::Kind::Bool ? 2 : def.numChoices();
    if (optionIndices.empty())
        for (int i = 0; i < count; ++i)
            optionIndices.push_back (i);

    for (std::size_t i = 0; i < optionIndices.size(); ++i)
    {
        const auto option = optionIndices[i];
        auto text = labels.size() == static_cast<int> (optionIndices.size())
                        ? labels[static_cast<int> (i)]
                        : (def.kind == params::Kind::Choice
                               ? juce::String::fromUTF8 (
                                     def.choices[static_cast<std::size_t> (option)].data(),
                                     static_cast<int> (def.choices[static_cast<std::size_t> (option)].size()))
                               : juce::String (option == 0 ? "Off" : "On"));
        segments.push_back (std::make_unique<Segment> (*this, static_cast<int> (i), text));
        addAndMakeVisible (*segments.back());
    }

    attachment = std::make_unique<juce::ParameterAttachment> (
        parameter (apvts, id),
        [this] (float value)
        {
            selected = -1;
            for (std::size_t i = 0; i < optionIndices.size(); ++i)
                if (optionIndices[i] == juce::roundToInt (value))
                    selected = static_cast<int> (i);
            for (std::size_t i = 0; i < segments.size(); ++i)
                segments[i]->setToggleState (static_cast<int> (i) == selected, juce::dontSendNotification);
            repaint();
        },
        nullptr);
    attachment->sendInitialUpdate();

    setComponentID (paramIdString (id));
    setTitle (juce::String::fromUTF8 (def.name.data(), static_cast<int> (def.name.size())));
}

SegmentedControl::~SegmentedControl() = default;

void SegmentedControl::choose (int index)
{
    if (index < 0 || index >= getNumSegments())
        return;

    attachment->setValueAsCompleteGesture (static_cast<float> (optionIndices[static_cast<std::size_t> (index)]));
}

void SegmentedControl::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colour::bgRaised);
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
    g.setColour (colour::borderSubtle);
    g.drawRoundedRectangle (bounds.reduced (border::width * 0.5f), bounds.getHeight() * 0.5f, border::width);
}

void SegmentedControl::resized()
{
    auto area = getLocalBounds();
    const auto count = std::max (1, getNumSegments());
    const auto width = area.getWidth() / count;
    for (auto& s : segments)
        s->setBounds (s == segments.back() ? area : area.removeFromLeft (width));
}

// ---- ThunderToggle
// -----------------------------------------------------------------------------------------------------

ThunderToggle::ThunderToggle (juce::AudioProcessorValueTreeState& apvts, params::ParamId id)
{
    const auto& def = params::paramDef (id);
    setComponentID (paramIdString (id));
    setTitle (juce::String::fromUTF8 (def.name.data(), static_cast<int> (def.name.size())));
    setTooltip (getTitle());
    attachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramIdString (id), *this);
}

ThunderToggle::~ThunderToggle() = default;

void ThunderToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto track = getLocalBounds().toFloat().withSizeKeepingCentre (static_cast<float> (size::toggleWidth),
                                                                         static_cast<float> (size::toggleHeight));
    const auto on = getToggleState();

    g.setColour (on ? colour::accentPrimary : (highlighted ? colour::bgHover : colour::bgRaised));
    g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
    g.setColour (on ? colour::accentPrimary : colour::borderStrong);
    g.drawRoundedRectangle (track.reduced (border::width * 0.5f), track.getHeight() * 0.5f, border::width);

    const auto knob = static_cast<float> (size::toggleKnob);
    const auto inset = (track.getHeight() - knob) * 0.5f;
    const auto x = on ? track.getRight() - inset - knob : track.getX() + inset;
    g.setColour (colour::textPrimary);
    g.fillEllipse (x, track.getY() + inset, knob, knob);
}

// ---- ParamCheckbox
// -----------------------------------------------------------------------------------------------------

ParamCheckbox::ParamCheckbox (juce::AudioProcessorValueTreeState& apvts, params::ParamId id, const juce::String& label)
    : juce::ToggleButton (label.toUpperCase())
{
    setComponentID (paramIdString (id));
    setTitle (label);
    attachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramIdString (id), *this);
}

ParamCheckbox::~ParamCheckbox() = default;

// ---- ParamDropdown
// -----------------------------------------------------------------------------------------------------

ParamDropdown::ParamDropdown (juce::AudioProcessorValueTreeState& apvts, params::ParamId id)
{
    const auto& def = params::paramDef (id);
    jassert (def.kind == params::Kind::Choice);

    for (int i = 0; i < def.numChoices(); ++i)
    {
        const auto& choice = def.choices[static_cast<std::size_t> (i)];
        addItem (juce::String::fromUTF8 (choice.data(), static_cast<int> (choice.size())), i + 1);
    }

    setComponentID (paramIdString (id));
    setTitle (juce::String::fromUTF8 (def.name.data(), static_cast<int> (def.name.size())));
    setTooltip (getTitle());
    attachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramIdString (id), *this);
}

ParamDropdown::~ParamDropdown() = default;

} // namespace thunder::ui
