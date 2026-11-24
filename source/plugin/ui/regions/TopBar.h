#pragma once

#include "plugin/presets/PresetManager.h"
#include "plugin/ui/components/Buttons.h"
#include "plugin/ui/components/Controls.h"
#include "plugin/ui/components/KeyBadge.h"
#include "plugin/ui/components/MorphControl.h"

#include <functional>

namespace thunder::ui
{

// C-PRESETBAR (UI.md §9.11): ◄ name ► ☆ Save ⋯. The name opens the preset
// browser; a modified sound shows "*". ◄ ► step through the list as the
// browser filters it. ☆ marks the loaded preset as a favourite. Save stores a
// user preset (factory presets and Init open Save as); the ⋯ menu has Init,
// Save as…, Import…, Export… and Open preset folder.
class PresetBar final
    : public juce::Component
    , private juce::Timer
{
public:
    explicit PresetBar (PresetManager& presets);
    ~PresetBar() override;

    void resized() override;

    // The editor opens the save form (true = Save as) and the browser, and shows messages.
    std::function<void (bool saveAs)> onSave;
    std::function<void()> onOpenBrowser;
    std::function<void (const juce::String& message)> onError;
    std::function<void (const juce::String& message)> onInfo;

    // [MSG] What the buttons do.
    void stepPreset (int direction);
    void showMoreMenu();
    void requestSave();
    void toggleFavourite();

    // The ⋯ menu's items (for tests too).
    static constexpr int kMenuInit = 1;
    static constexpr int kMenuSaveAs = 2;
    static constexpr int kMenuImport = 3;
    static constexpr int kMenuExport = 4;
    static constexpr int kMenuOpenFolder = 5;
    juce::PopupMenu buildMoreMenu() const;
    void chooseMoreMenuItem (int itemId);

    juce::String getDisplayedName() const { return nameField.getButtonText(); }
    bool showsFavourite() const { return favourite.getToggleState(); }
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
    void launchImport();
    void launchExport();

    PresetManager& presets;
    IconButton previous, next, favourite, save, more;
    NameField nameField;
    std::unique_ptr<juce::FileChooser> chooser;
};

// R-TOPBAR (UI.md §8.1, §9.17): logo, preset bar, Morph, key (C-KEYSELECT with
// the detected key's badge), Settings and About. Clicking the logo also opens About.
//
// The key menu (the badge's click, UI.md §9.13): what was detected and from
// what, "Use <key>", "Keep <key>", and Auto Detect. Only "Use" changes the key
// (PRD FR-KEY-003).
class TopBar final : public juce::Component
{
public:
    TopBar (juce::AudioProcessorValueTreeState& apvts, PresetManager& presets);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;

    PresetBar& getPresetBar() noexcept { return presetBar; }
    MorphControl& getMorph() noexcept { return morph; }
    IconButton& getSettingsButton() noexcept { return settingsButton; }
    IconButton& getAboutButton() noexcept { return aboutButton; }

    std::function<void()> onSettings;
    std::function<void()> onAbout;

    // The detected key (the editor sets it as detection goes on).
    KeyBadge& getKeyBadge() noexcept { return keyBadge; }

    // Auto Detect (the session's keyAutoDetect), set by the editor.
    std::function<bool()> isAutoDetectOn;
    std::function<void (bool)> setAutoDetect;

    // [MSG] The key menu and what choosing an item does (for tests too).
    static constexpr int kMenuUseDetected = 1;
    static constexpr int kMenuKeepManual = 2;
    static constexpr int kMenuAutoDetect = 3;
    juce::PopupMenu buildKeyMenu() const;
    void chooseKeyMenuItem (int itemId);
    void showKeyMenu();

    // [MSG] Sets Key Root and Scale to the detected key, as one undoable gesture each.
    void useDetectedKey();

private:
    juce::Rectangle<int> logoBounds() const;

    PresetBar presetBar;
    MorphControl morph;
    ParamDropdown keyRoot;
    ParamDropdown keyScale;
    KeyBadge keyBadge;
    juce::AudioProcessorValueTreeState& state;
    IconButton settingsButton;
    IconButton aboutButton;
};

} // namespace thunder::ui
