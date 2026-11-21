#pragma once

#include "core/params/ParamDefs.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace thunder::ui
{

// C-KNOB (UI.md §9.1), the v0.5 flat version: label above, dial, value below.
//
// - Vertical drag: 200 px covers the whole range; Shift-drag is ten times finer.
//   Movement is relative, so pressing or releasing Shift mid-drag never jumps.
// - Mouse wheel: 1% per notch, or one step for stepped controls.
// - Double-click: back to the default.
// - Parameters whose range is symmetric around zero draw their arc from 12 o'clock.
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

    // The knob position (0–1) after dragging `pixels` upwards from `proportion`;
    // `fine` (Shift) moves ten times less. Clamped to the range.
    static double dragged (double proportion, double pixels, bool fine) noexcept;

    // The width and height the knob needs at its size.
    static juce::Rectangle<int> preferredBounds (Size size);

private:
    class Dial final : public juce::Slider
    {
    public:
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        float lastY = 0.0f;
        double dragProportion = 0.0;
    };

    void updateValueText();

    Size size;
    Dial dial;
    juce::Label label;
    juce::Label value;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThunderKnob)
};

} // namespace thunder::ui
