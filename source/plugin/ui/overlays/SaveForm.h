#pragma once

#include "plugin/presets/PresetManager.h"
#include "plugin/ui/components/Buttons.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace thunder::ui
{

// C-SAVEFORM, v0.5 (UI.md §9.16): Name, Author, Category and Description over a
// dimmed editor.
//
// - Save prefills the loaded preset's details; Save as starts with an empty name.
// - If a user preset with that name exists, Save becomes an inline "already
//   exists" prompt with Replace it; editing the name removes the prompt.
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
    juce::Label nameLabel, authorLabel, categoryLabel, descriptionLabel;
    juce::TextEditor name, author, description;
    juce::ComboBox category;
    ThunderButton cancelButton{"Cancel"};
    ThunderButton saveButton{"Save", ThunderButton::Variant::primary};
    ThunderButton replaceButton{"Replace it", ThunderButton::Variant::danger};
    juce::String message;
    bool messageIsError = false;
    bool askingToReplace = false;
    juce::String replaceName; // the name the prompt is about

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SaveForm)
};

// C-TOAST (UI.md §9.15): a short message at the top right of the visualiser.
// Errors stay until clicked; other messages fade after 4 s.
class Toast final
    : public juce::Component
    , private juce::Timer
{
public:
    Toast();

    void show (const juce::String& text, bool isError);
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::String getText() const { return text; }

private:
    void timerCallback() override;

    juce::String text;
    bool error = false;
};

} // namespace thunder::ui
