#pragma once

#include "core/params/ParamDefs.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

// The smaller controls of the v0.5 interface (UI.md §9.2, §9.4, §9.5, §9.8),
// each connected to one host parameter.

namespace thunder::ui
{

// C-SEGMENTED: one segment per option of a choice parameter (or a chosen
// subset), the selected one filled with the accent.
class SegmentedControl final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    // `options` are indices into the parameter's choices; empty = all of them.
    // `labels` override the option names (same length as the options shown).
    SegmentedControl (juce::AudioProcessorValueTreeState& apvts, params::ParamId id, std::vector<int> options = {},
                      juce::StringArray labels = {});
    ~SegmentedControl() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    int getSelectedOption() const noexcept { return selected; }
    int getNumSegments() const noexcept { return static_cast<int> (segments.size()); }

    // [MSG] As a click on segment `index` would.
    void choose (int index);

private:
    class Segment;

    std::vector<int> optionIndices;
    std::vector<std::unique_ptr<Segment>> segments;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int selected = -1;

    JUCE_DECLARE_NON_COPYABLE (SegmentedControl)
};

// C-TOGGLE: the pill switch used for a module's on/off.
class ThunderToggle final : public juce::ToggleButton
{
public:
    ThunderToggle (juce::AudioProcessorValueTreeState& apvts, params::ParamId id);
    ~ThunderToggle() override;

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// C-CHECKBOX with its label.
class ParamCheckbox final : public juce::ToggleButton
{
public:
    ParamCheckbox (juce::AudioProcessorValueTreeState& apvts, params::ParamId id, const juce::String& label);
    ~ParamCheckbox() override;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// C-DROPDOWN for a choice parameter.
class ParamDropdown final : public juce::ComboBox
{
public:
    ParamDropdown (juce::AudioProcessorValueTreeState& apvts, params::ParamId id);
    ~ParamDropdown() override;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

} // namespace thunder::ui
