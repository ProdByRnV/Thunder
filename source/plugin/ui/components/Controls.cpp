#include "Controls.h"

#include "plugin/ui/ParamHelp.h"
#include "plugin/ui/ParamUi.h"
#include "plugin/ui/theme/ThunderFonts.h"
#include "plugin/ui/theme/ThunderTheme.h"

namespace thunder::ui
{

using namespace theme;

namespace
{
// Every parameter control: its parameter ID, name, description and tooltip.
void describeControl (juce::Component& c, params::ParamId id)
{
    c.setComponentID (paramIdString (id));
    c.setTitle (help::name (id));
    c.setDescription (help::describe (id));
    c.setMouseClickGrabsKeyboardFocus (false);
    c.setWantsKeyboardFocus (true);
    if (auto* tip = dynamic_cast<juce::SettableTooltipClient*> (&c))
        tip->setTooltip (help::name (id) + ": " + help::describe (id));
}

// The radio-group ID the segments of one control share (unique per control).
int nextRadioGroup()
{
    static int group = 0x7400;
    return ++group;
}
} // namespace

// ---- SegmentedControl
// ------------------------------------------------------------------------------------------------

class SegmentedControl::Segment final : public juce::Button
{
public:
    Segment (SegmentedControl& o, int i, const juce::String& label, int radioGroup)
        : juce::Button (label)
        , owner (o)
        , index (i)
    {
        setClickingTogglesState (false);
        setRadioGroupId (radioGroup, juce::dontSendNotification);
        setTitle (label);
        setMouseClickGrabsKeyboardFocus (false);
        setWantsKeyboardFocus (true);
        onClick = [this] { owner.choose (index); };
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto isSelected = owner.selected == index;
        const auto bounds = getLocalBounds().toFloat().reduced (border::width);

        // Pictures sit in rounded tiles; names in pills.
        const auto pictured = static_cast<bool> (owner.optionPainter);
        const auto corner = pictured ? radius::sm : bounds.getHeight() * 0.5f;

        if (isSelected || highlighted)
        {
            g.setColour (isSelected ? (isEnabled() ? owner.accent : colour::textDisabled) : colour::bgHover);
            g.fillRoundedRectangle (bounds, corner);
        }

        if (hasKeyboardFocus (false))
            paint::drawFocusRing (g, bounds, corner);

        const auto ink =
            isSelected ? colour::textOnAccent : (highlighted ? colour::textPrimary : colour::textSecondary);
        if (pictured)
        {
            owner.optionPainter (g, owner.optionIndices[static_cast<std::size_t> (index)],
                                 bounds.reduced (static_cast<float> (space::s1)), ink);
            return;
        }

        g.setColour (ink);
        g.setFont (fonts::ui (type::label, fonts::Weight::semiBold).withExtraKerningFactor (type::labelTracking));
        g.drawFittedText (getButtonText().toUpperCase(), getLocalBounds().reduced (space::s1, 0),
                          juce::Justification::centred, 1);
    }

    // The control's tooltip, wherever the pointer is on it; a pictured segment
    // names itself first.
    juce::String getTooltip() override
    {
        return owner.optionPainter ? getButtonText() + " - " + owner.getTooltip() : owner.getTooltip();
    }

private:
    SegmentedControl& owner;
    int index;
};

SegmentedControl::SegmentedControl (juce::AudioProcessorValueTreeState& apvts, params::ParamId id,
                                    std::vector<int> options, juce::StringArray labels)
    : optionIndices (std::move (options))
    , accent (colour::accentPrimary)
{
    const auto& def = params::paramDef (id);
    jassert (def.kind == params::Kind::Choice || def.kind == params::Kind::Bool);

    const auto count = def.kind == params::Kind::Bool ? 2 : def.numChoices();
    if (optionIndices.empty())
        for (int i = 0; i < count; ++i)
            optionIndices.push_back (i);

    const auto group = nextRadioGroup();
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
        segments.push_back (std::make_unique<Segment> (*this, static_cast<int> (i), text, group));
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

    describeControl (*this, id);
    setWantsKeyboardFocus (false); // its segments take the focus
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);
}

SegmentedControl::SegmentedControl (const juce::String& titleText, juce::StringArray labels,
                                    std::function<void (int)> onChoose)
    : onChooseCallback (std::move (onChoose))
    , accent (colour::accentPrimary)
{
    const auto group = nextRadioGroup();
    for (int i = 0; i < labels.size(); ++i)
    {
        optionIndices.push_back (i);
        segments.push_back (std::make_unique<Segment> (*this, i, labels[i], group));
        addAndMakeVisible (*segments.back());
    }

    setTitle (titleText);
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);
}

SegmentedControl::~SegmentedControl() = default;

juce::Button& SegmentedControl::getSegment (int index)
{
    return *segments[static_cast<std::size_t> (index)];
}

void SegmentedControl::setOptionPainter (OptionPainter painter)
{
    optionPainter = std::move (painter);
    repaint();
}

void SegmentedControl::setAccent (juce::Colour colour)
{
    accent = colour;
    repaint();
}

void SegmentedControl::setSelectedOption (int index)
{
    selected = index;
    for (std::size_t i = 0; i < segments.size(); ++i)
        segments[i]->setToggleState (static_cast<int> (i) == selected, juce::dontSendNotification);
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> SegmentedControl::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}

void SegmentedControl::choose (int index)
{
    if (index < 0 || index >= getNumSegments())
        return;

    if (attachment == nullptr)
    {
        setSelectedOption (index);
        if (onChooseCallback)
            onChooseCallback (index);
        return;
    }

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
    : flash (*this)
{
    describeControl (*this, id);
    attachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramIdString (id), *this);
    wasOn = getToggleState();
    ready = true; // no flash for the value it opens with
}

ThunderToggle::~ThunderToggle() = default;

void ThunderToggle::buttonStateChanged()
{
    // Flash when it turns on, from a click or from the host (§9.2).
    if (const auto on = getToggleState(); on != wasOn)
    {
        wasOn = on;
        if (on && ready)
            flash.trigger();
    }
}

void ThunderToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    drawPillToggle (g, getLocalBounds().toFloat(), getToggleState(), highlighted, flash.getLevel(),
                    hasKeyboardFocus (false));
}

void drawPillToggle (juce::Graphics& g, juce::Rectangle<float> area, bool on, bool highlighted, float flashLevel,
                     bool focused)
{
    const auto track =
        area.withSizeKeepingCentre (static_cast<float> (size::toggleWidth), static_cast<float> (size::toggleHeight));
    const auto r = track.getHeight() * 0.5f;

    if (flashLevel > 0.0f)
    {
        g.setColour (colour::glowPrimary.withAlpha (glow::medium.opacity * flashLevel));
        g.fillRoundedRectangle (track.expanded (space::s1 * flashLevel), r + space::s1 * flashLevel);
    }

    g.setColour (on ? colour::accentPrimary : (highlighted ? colour::bgHover : colour::bgRaised));
    g.fillRoundedRectangle (track, r);
    g.setColour (on ? colour::accentPrimary : colour::borderStrong);
    g.drawRoundedRectangle (track.reduced (border::width * 0.5f), r, border::width);

    const auto knob = static_cast<float> (size::toggleKnob);
    const auto inset = (track.getHeight() - knob) * 0.5f;
    const auto x = on ? track.getRight() - inset - knob : track.getX() + inset;
    g.setColour (colour::textPrimary);
    g.fillEllipse (x, track.getY() + inset, knob, knob);

    if (focused)
        paint::drawFocusRing (g, track, r);
}

// ---- ParamCheckbox
// -----------------------------------------------------------------------------------------------------

ParamCheckbox::ParamCheckbox (juce::AudioProcessorValueTreeState& apvts, params::ParamId id, const juce::String& label)
    : juce::ToggleButton (label.toUpperCase())
{
    describeControl (*this, id);
    setTitle (label);
    attachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramIdString (id), *this);
}

ParamCheckbox::~ParamCheckbox() = default;

// ---- ParamChip
// ---------------------------------------------------------------------------------------------------------

ParamChip::ParamChip (juce::AudioProcessorValueTreeState& apvts, params::ParamId id, const juce::String& label,
                      juce::Colour accent)
    : juce::ToggleButton (label.toUpperCase())
    , accentColour (accent)
{
    describeControl (*this, id);
    setTitle (label);
    attachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramIdString (id), *this);
}

ParamChip::~ParamChip() = default;

void ParamChip::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto on = getToggleState();
    const auto bounds = getLocalBounds().toFloat().reduced (border::width);
    const auto r = bounds.getHeight() * 0.5f;

    g.setColour (on ? accentColour : (highlighted ? colour::bgHover : colour::bgRaised));
    g.fillRoundedRectangle (bounds, r);
    g.setColour (on ? accentColour : colour::borderStrong);
    g.drawRoundedRectangle (bounds.reduced (border::width * 0.5f), r, border::width);

    if (hasKeyboardFocus (false))
        paint::drawFocusRing (g, bounds, r);

    g.setColour (on ? colour::textOnAccent : (highlighted ? colour::textPrimary : colour::textSecondary));
    g.setFont (fonts::ui (type::label, fonts::Weight::semiBold).withExtraKerningFactor (type::labelTracking));
    g.drawFittedText (getButtonText(), getLocalBounds().reduced (space::s2, 0), juce::Justification::centred, 1);
}

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

    describeControl (*this, id);
    attachment =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramIdString (id), *this);
}

ParamDropdown::~ParamDropdown() = default;

} // namespace thunder::ui
