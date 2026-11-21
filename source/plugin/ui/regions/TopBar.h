#pragma once

#include "plugin/presets/PresetManager.h"
#include "plugin/ui/components/Controls.h"

#include <functional>

namespace thunder::ui
{

// C-PRESETBAR, v0.5 (UI.md §9.11, §15): ◄ name ► Save ⋯. The name opens a
// simple list of factory and user presets; a modified sound shows "*". The ⋯
// menu has Init, Save as… and "keep Key, Scale and Output Mode" (FR-PRESET-006).
class PresetBar final
    : public juce::Component
    , private juce::Timer
{
public:
    explicit PresetBar (PresetManager& presets);
    ~PresetBar() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // The editor opens the save form (true = Save as) and shows messages.
    std::function<void (bool saveAs)> onSave;
    std::function<void (const juce::String& message)> onError;

    // [MSG] What the buttons do.
    void stepPreset (int direction);
    void showPresetList();
    void showMoreMenu();
    void requestSave();

    juce::String getDisplayedName() const { return nameButton.getButtonText(); }
    void refresh();

private:
    class IconButton;

    void timerCallback() override;
    void report (const juce::String& message);

    PresetManager& presets;
    std::unique_ptr<IconButton> previous, next, more;
    juce::TextButton nameButton;
    juce::TextButton saveButton;
};

// R-TOPBAR (UI.md §8.1): logo, preset bar, key (C-KEYSELECT, manual in v0.5).
class TopBar final : public juce::Component
{
public:
    TopBar (juce::AudioProcessorValueTreeState& apvts, PresetManager& presets);

    void paint (juce::Graphics& g) override;
    void resized() override;

    PresetBar& getPresetBar() noexcept { return presetBar; }

private:
    PresetBar presetBar;
    ParamDropdown keyRoot;
    ParamDropdown keyScale;
};

} // namespace thunder::ui
