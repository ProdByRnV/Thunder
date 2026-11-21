#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace thunder::ui
{

// Draws JUCE's standard widgets with Thunder's tokens (UI.md §9, the v0.5
// "Functional UI" stage of §15): flat knobs (track and value arc, no glow),
// checkboxes, dropdowns, buttons, menus, text fields and tooltips.
//
// Knobs read two component properties: "bipolar" (the arc starts at 12 o'clock)
// and "large" (the value uses the large type).
class ThunderLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ThunderLookAndFeel();

    static inline const juce::Identifier bipolarProperty{"bipolar"};

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float startAngle,
                           float endAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted,
                               bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool down, int buttonX, int buttonY, int buttonW,
                       int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowTitleFont() override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
};

} // namespace thunder::ui
