#pragma once

#include "core/params/ParamDefs.h"
#include "plugin/ui/theme/ThunderPaint.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

// The smaller controls (UI.md §9.2, §9.4, §9.5, §9.8), each connected to one
// host parameter. Every one has the parameter's name as its accessible title
// and what it does as its tooltip and accessible description (PRD UI-GEN-005,
// UI-A11Y-001). Clicks never take the keyboard focus; Tab does, and shows the
// focus ring.

namespace thunder::ui
{

// C-SEGMENTED: one segment per option of a choice parameter (or a chosen
// subset), the selected one filled with the accent. Screen readers see a group
// of radio buttons.
class SegmentedControl final
    : public juce::Component
    , public juce::SettableTooltipClient
{
public:
    // `options` are indices into the parameter's choices; empty = all of them.
    // `labels` override the option names (same length as the options shown).
    SegmentedControl (juce::AudioProcessorValueTreeState& apvts, params::ParamId id, std::vector<int> options = {},
                      juce::StringArray labels = {});

    // Not tied to a parameter: `onChoose` gets the chosen segment's index.
    SegmentedControl (const juce::String& title, juce::StringArray labels, std::function<void (int)> onChoose);
    ~SegmentedControl() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    int getSelectedOption() const noexcept { return selected; }
    int getNumSegments() const noexcept { return static_cast<int> (segments.size()); }
    juce::Button& getSegment (int index);

    // [MSG] As a click on segment `index` would.
    void choose (int index);
    void setSelectedOption (int index);

private:
    class Segment;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    std::vector<int> optionIndices;
    std::vector<std::unique_ptr<Segment>> segments;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int selected = -1;
    std::function<void (int)> onChooseCallback;

    JUCE_DECLARE_NON_COPYABLE (SegmentedControl)
};

// Draws C-TOGGLE's pill (UI.md §9.2) centred in `area`.
void drawPillToggle (juce::Graphics& g, juce::Rectangle<float> area, bool on, bool highlighted, float flashLevel,
                     bool focused);

// C-TOGGLE: the pill switch used for a module's on/off. Turning it on flashes
// (UI.md §9.2).
class ThunderToggle final : public juce::ToggleButton
{
public:
    ThunderToggle (juce::AudioProcessorValueTreeState& apvts, params::ParamId id);
    ~ThunderToggle() override;

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    float getFlashLevel() const noexcept { return flash.getLevel(); }

private:
    void buttonStateChanged() override;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    paint::Flash flash;
    bool wasOn = false;
    bool ready = false;
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
