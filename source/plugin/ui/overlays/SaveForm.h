#pragma once

#include "plugin/presets/PresetManager.h"
#include "plugin/ui/components/Buttons.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace thunder::ui
{

// C-SAVEFORM (UI.md §9.16): Name, Author, Category, Bank, Tags and Description over a
// dimmed editor.
//
// - Save prefills the loaded preset's details; Save as starts with an empty name.
// - Category and Bank suggest what the library already uses; Bank is a
//   sub-folder of the preset folder ("" = the top level).
// - If a user preset with that name exists in that bank, Save becomes an inline "already
//   exists" prompt with Replace it; editing the name or bank removes the prompt.
// - Refusals show here, where the user is looking.
// - Enter saves, Esc cancels.
class SaveForm final : public juce::Component
{
public:
    SaveForm (PresetManager& presets, bool saveAs);
    ~SaveForm() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    std::function<void (bool saved)> onClose;

    // [MSG] The Save button: saves, asks to replace, or shows why it can't.
    void submit();
    void confirmReplace();
    void cancel();

    juce::TextEditor& getNameField() noexcept { return name; }
    juce::TextEditor& getAuthorField() noexcept { return author; }
    juce::ComboBox& getCategoryField() noexcept { return category; }
    juce::ComboBox& getBankField() noexcept { return bank; }
    juce::TextEditor& getTagsField() noexcept { return tags; }
    juce::TextEditor& getDescriptionField() noexcept { return description; }
    juce::String getMessage() const { return message; }
    // True while the form asks to replace the preset named in the Name field.
    bool isAskingToReplace() const;

private:
    presets::Metadata collect() const;
    void save();
    void setMessage (const juce::String& text, bool isError);
    void setAskingToReplace (bool asking);
    juce::Rectangle<int> panelBounds() const;

    PresetManager& presets;
    juce::Label title;
    juce::Label nameLabel, authorLabel, categoryLabel, bankLabel, tagsLabel, descriptionLabel;
    juce::TextEditor name, author, tags, description;
    juce::ComboBox category, bank;
    ThunderButton cancelButton{"Cancel"};
    ThunderButton saveButton{"Save", ThunderButton::Variant::primary};
    ThunderButton replaceButton{"Replace it", ThunderButton::Variant::danger};
    juce::String message;
    bool messageIsError = false;
    bool askingToReplace = false;
    juce::String replaceName; // the name the prompt is about
    juce::String replaceBank; // ... and its bank

    juce::String chosenBank() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SaveForm)
};

// C-TOAST (UI.md §9.15): a short message at the top right of the visualiser.
// Errors stay until clicked; other messages fade after 4 s. A message can
// offer buttons under it (a missing source: "Locate…" and "Dismiss", §16.4);
// it then stays until one is pressed.
class Toast final
    : public juce::Component
    , private juce::Timer
{
public:
    struct Action
    {
        juce::String label;
        std::function<void()> onClick; // the toast closes first
    };

    Toast();
    ~Toast() override;

    void show (const juce::String& text, bool isError, std::vector<Action> actions = {});
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::String getText() const { return text; }
    int getPreferredHeight() const noexcept;
    int getNumActions() const noexcept { return static_cast<int> (buttons.size()); }
    ThunderButton& getActionButton (int index) { return *buttons[static_cast<std::size_t> (index)]; }

private:
    void timerCallback() override;
    juce::Rectangle<int> textArea() const;

    juce::String text;
    bool error = false;
    std::vector<Action> actions;
    std::vector<std::unique_ptr<ThunderButton>> buttons, retired;
};

} // namespace thunder::ui
