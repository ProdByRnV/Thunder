#pragma once

#include "core/presets/PresetLibrary.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace thunder::ui
{

// Morph in the top bar (UI.md §9.17, PRD FR-PLAY-002): "A ──●── B", a 0–100%
// slider for `morphAmount` between the current settings (A) and sound B.
//
// - Right-click (anywhere on it) or a click on B: "Set B = current sound",
//   "Load B from preset" (the library), "Clear B".
// - With no B the slider is disabled and says "Set B to morph".
// - Its tooltip names B and how far towards it the sound is.
class MorphControl final : public juce::Component
{
public:
    explicit MorphControl (juce::AudioProcessorValueTreeState& apvts);
    ~MorphControl() override;

    // What it asks the editor to do with sound B.
    std::function<std::optional<juce::String>()> getSoundBName; // nullopt = no B
    std::function<void()> setBFromCurrent;
    std::function<void (const presets::Entry& entry)> setBFromPreset;
    std::function<void()> clearB;
    std::function<std::vector<presets::Entry>()> getPresets; // for "Load B from preset"

    // [MSG] Shows whether there is a B (the editor calls it when B may have changed).
    void refresh();

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;

    // [MSG] The menu, and what choosing an item does (for tests too).
    static constexpr int kMenuSetCurrent = 1;
    static constexpr int kMenuClear = 2;
    static constexpr int kMenuFirstPreset = 100;
    juce::PopupMenu buildMenu();
    void chooseMenuItem (int itemId);
    void showMenu();

    juce::Slider& getSlider() noexcept { return slider; }
    bool hasSoundB() const noexcept { return bName.has_value(); }
    juce::String getTooltipText() { return slider.getTooltip(); }

private:
    class MorphSlider final : public juce::Slider
    {
    public:
        explicit MorphSlider (MorphControl& owner);
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;

    private:
        MorphControl& owner;
    };

    juce::Rectangle<int> aArea() const;
    juce::Rectangle<int> bArea() const;
    void describe();

    juce::AudioProcessorValueTreeState& state;
    MorphSlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::optional<juce::String> bName;
    std::vector<presets::Entry> menuPresets; // what the last menu listed

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphControl)
};

} // namespace thunder::ui
