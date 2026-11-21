#pragma once

#include "plugin/presets/PresetManager.h"
#include "plugin/ui/components/Buttons.h"
#include "plugin/ui/components/Controls.h"

#include <functional>

namespace thunder::ui
{

// C-PRESETBAR (UI.md §9.11): ◄ name ► Save ⋯. The name opens the list of
// factory and user presets; a modified sound shows "*". Save stores a user
// preset (factory presets and Init open Save as); the ⋯ menu has Init and
// Save as….
class PresetBar final
    : public juce::Component
    , private juce::Timer
{
public:
    explicit PresetBar (PresetManager& presets);
    ~PresetBar() override;

    void resized() override;

    // The editor opens the save form (true = Save as) and shows messages.
    std::function<void (bool saveAs)> onSave;
    std::function<void (const juce::String& message)> onError;

    // [MSG] What the buttons do.
    void stepPreset (int direction);
    void showPresetList();
    void showMoreMenu();
    void requestSave();

    juce::String getDisplayedName() const { return nameField.getButtonText(); }
    void refresh();

private:
    // The preset's name in a field that opens the list (styled like C-DROPDOWN).
    class NameField final : public juce::Button
    {
    public:
        NameField();
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    void timerCallback() override;
    void report (const juce::String& message);

    PresetManager& presets;
    IconButton previous, next, save, more;
    NameField nameField;
};

// R-TOPBAR (UI.md §8.1): logo, preset bar, key (C-KEYSELECT, manual), Settings
// and About. Clicking the logo also opens About.
class TopBar final : public juce::Component
{
public:
    TopBar (juce::AudioProcessorValueTreeState& apvts, PresetManager& presets);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;

    PresetBar& getPresetBar() noexcept { return presetBar; }
    IconButton& getSettingsButton() noexcept { return settingsButton; }
    IconButton& getAboutButton() noexcept { return aboutButton; }

    std::function<void()> onSettings;
    std::function<void()> onAbout;

private:
    juce::Rectangle<int> logoBounds() const;

    PresetBar presetBar;
    ParamDropdown keyRoot;
    ParamDropdown keyScale;
    IconButton settingsButton;
    IconButton aboutButton;
};

} // namespace thunder::ui
