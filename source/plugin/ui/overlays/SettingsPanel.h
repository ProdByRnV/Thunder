#pragma once

#include "plugin/ui/components/Buttons.h"
#include "plugin/ui/components/Controls.h"

#include <functional>
#include <memory>

namespace thunder
{
class ThunderProcessor;
}

namespace thunder::ui
{

// The Settings popover (UI.md §10.3), opened by the gear in the top bar. It
// covers the editor so a click anywhere else, or Esc, closes it. Each setting
// is global (every Thunder instance and session):
//   UI scale · Visualiser (the view new projects open with) · Show tooltips ·
//   Capture format · Captures folder · Keep Key, Scale and Output Mode when
//   loading presets.
// Reduce Motion and the frame rate join it with the motion work, Quality with
// the Eco mode.
class SettingsPanel final : public juce::Component
{
public:
    SettingsPanel (ThunderProcessor& processor, float currentScale);
    ~SettingsPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;

    // Set by the editor.
    std::function<void (float scale)> onScaleChosen;
    std::function<void (bool show)> onShowTooltips;
    std::function<void()> onClose;

    // [MSG] What the controls do (also for tests).
    void chooseScale (float scale);
    void setShowTooltips (bool show);
    void setSpectrumByDefault (bool spectrum);
    void setCaptureFormat (bool float32);
    void setCapturesFolder (const juce::File& folder); // juce::File() = the default
    void setPreserveSession (bool preserve);
    void close();

    // Where the popover sits: under the gear, at the editor's right edge.
    juce::Rectangle<int> popoverBounds() const;

    juce::ComboBox& getScaleBox() noexcept { return scaleBox; }
    juce::String getFolderText() const { return folderText.getText(); }

private:
    class PillToggle final : public juce::ToggleButton
    {
    public:
        explicit PillToggle (const juce::String& title);
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    void refresh();
    void chooseFolder();
    juce::Label& addLabel (const juce::String& text);

    ThunderProcessor& processor;

    juce::OwnedArray<juce::Label> labels;
    juce::ComboBox scaleBox;
    SegmentedControl viewControl;
    PillToggle tooltipsToggle{"Show tooltips"};
    SegmentedControl formatControl;
    juce::Label folderText;
    ThunderButton changeFolderButton{juce::String::fromUTF8 ("Change\xe2\x80\xa6")};
    IconButton defaultFolderButton{"Use the default captures folder", icons::reset()};
    PillToggle preserveToggle{"Keep Key, Scale and Output Mode when loading presets"};
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};

} // namespace thunder::ui
