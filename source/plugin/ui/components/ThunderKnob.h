#pragma once

#include "core/params/ParamDefs.h"
#include "plugin/ui/theme/ThunderPaint.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace thunder::ui
{

// C-KNOB (UI.md §9.1; PRD UI-FX-003, UI-GEN-005/006/007): label above, dial,
// value below.
//
// - The value arc "charges" in the accent and glows with the value
//   (glow.medium × value, stronger on hover); a hot spark sits at its tip.
//   Parameters symmetric around zero draw the arc from 12 o'clock.
// - Vertical drag: 200 px covers the whole range; Shift-drag is ten times finer.
//   Movement is relative, so pressing or releasing Shift mid-drag never jumps.
//   The cursor hides while dragging and comes back where it was.
// - Mouse wheel: 1% per notch, or one step for stepped controls.
// - Double-click: back to the default.
// - Click the value to type one; Enter sets it, Esc or an unreadable entry
//   leaves the value as it was.
// - Right-click: Reset to default, Enter value…, Copy value, Paste value.
// - Tooltip: name, what it does, the value and its range.
// The value reads exactly as the host shows it (core/params/ParamText).
class ThunderKnob final : public juce::Component
{
public:
    enum class Size
    {
        small,
        medium,
        large
    };

    ThunderKnob (juce::AudioProcessorValueTreeState& apvts, params::ParamId id, const juce::String& label,
                 Size size = Size::medium);
    ~ThunderKnob() override;

    void resized() override;

    juce::Slider& getSlider() noexcept { return dial; }
    juce::String getValueText() const { return value.getText(); }
    params::ParamId getParamId() const noexcept { return paramId; }

    // [MSG] What the value field and the right-click menu do.
    void showValueEditor();
    bool enterValue (const juce::String& text); // false (and nothing changes) if `text` isn't a value
    void resetToDefault();
    void copyValue() const;
    bool pasteValue();
    void showMenu();

    // [MSG] Shows the value as `format` writes it instead of the host's text
    // (Space's Decay in seconds for its mode). Typing still takes the host's units.
    void setValueFormatter (std::function<juce::String (double)> format);
    void refreshValueText() { updateValueText(); }

    // [MSG] The value Morph sends to the sound (real units), shown as a thin
    // second ring outside the arc (UI.md §9.17); nullopt hides it.
    void setMorphedValue (std::optional<float> value);
    std::optional<float> getMorphedValue() const noexcept { return morphed; }

    // The knob position (0–1) after dragging `pixels` upwards from `proportion`;
    // `fine` (Shift) moves ten times less. Clamped to the range.
    static double dragged (double proportion, double pixels, bool fine) noexcept;

    // The width and height the knob needs at its size.
    static juce::Rectangle<int> preferredBounds (Size size);

    // How strongly the value arc glows (0–1) at knob position `proportion`.
    static float arcGlow (double proportion, bool bipolar, bool hovered) noexcept;

private:
    class Dial final : public juce::Slider
    {
    public:
        explicit Dial (ThunderKnob& owner);

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }

        bool bipolar = false;

    private:
        ThunderKnob& owner;
        float lastY = 0.0f;
        double dragProportion = 0.0;
        bool dragging = false;
        paint::GlowCache arcGlowCache;
        paint::GlowCache sparkGlowCache;
    };

    class Value final : public juce::Label
    {
    public:
        explicit Value (ThunderKnob& owner);
        void mouseDown (const juce::MouseEvent&) override;

    private:
        ThunderKnob& owner;
    };

    void updateValueText();

    juce::AudioProcessorValueTreeState& apvts;
    params::ParamId paramId;
    Size size;
    Dial dial;
    juce::Label label;
    Value value;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::function<juce::String (double)> formatter;
    std::optional<float> morphed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThunderKnob)
};

} // namespace thunder::ui
