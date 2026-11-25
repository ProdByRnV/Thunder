#pragma once

#include "plugin/ui/components/Controls.h"
#include "plugin/ui/components/ThunderKnob.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace thunder
{
class ThunderProcessor;
}

namespace thunder::ui
{

// The PERFORM panel (UI.md §9.17, PRD FR-PLAY-001, 003, 004, 005) beside the
// visualiser: Storm (the big knob), Variation, the strike pads FREEZE, CLAP
// and SCATTER, and the Riser (1 | 2 | 4 | 8 bars and RISE).
class PerformPanel final
    : public juce::Component
    , private juce::Timer
{
public:
    explicit PerformPanel (ThunderProcessor& processor);
    ~PerformPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // A strike pad: on only while held (the host sees an automatable switch).
    // Shift-click latches it on until the next click. It lights up whenever its
    // switch is on, from the mouse or from automation.
    class Pad final : public juce::Button
    {
    public:
        Pad (juce::AudioProcessorValueTreeState& apvts, params::ParamId id, const juce::String& label,
             const juce::String& accessibleName);

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;

        // [MSG] Holding and letting go, as the mouse does (tests too).
        void press (bool latch = false);
        void release();
        bool isLatched() const noexcept { return latched; }
        bool isOn() const;
        params::ParamId getParamId() const noexcept { return paramId; }

    private:
        using juce::Button::clicked; // keeps both overloads visible
        void clicked() override;     // keyboard and screen readers: latch on / off
        void set (bool on);

        juce::RangedAudioParameter& parameter;
        params::ParamId paramId;
        bool latched = false;
        bool holding = false;
        bool fromMouse = false; // a mouse click also calls clicked(); only keys and screen readers latch there
    };

    // RISE: starts a rise (riserGo); fills towards the bar line while rising.
    class RiseButton final : public juce::Button
    {
    public:
        RiseButton (ThunderProcessor& processor, juce::AudioProcessorValueTreeState& apvts);

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;

        // [MSG] A press, as the mouse makes one.
        void rise();
        void setProgress (float p); // −1 = not rising

    private:
        using juce::Button::clicked; // keeps both overloads visible
        void clicked() override;     // keyboard and screen readers
        void setGo (bool on);

        ThunderProcessor& processor;
        juce::RangedAudioParameter& go;
        float progress = -1.0f;
        bool held = false;
        bool fromMouse = false;
    };

    ThunderKnob& getStorm() noexcept { return storm; }
    ThunderKnob& getVariation() noexcept { return variation; }
    Pad& getFreeze() noexcept { return freeze; }
    Pad& getClap() noexcept { return clap; }
    Pad& getScatter() noexcept { return scatter; }
    SegmentedControl& getRiserBars() noexcept { return riserBars; }
    RiseButton& getRise() noexcept { return rise; }

    // [MSG] Reads the pads' switches and the Riser's progress (the timer calls it).
    void refresh();

private:
    void timerCallback() override;

    ThunderProcessor& processor;
    ThunderKnob storm;
    ThunderKnob variation;
    Pad freeze, clap, scatter;
    SegmentedControl riserBars;
    RiseButton rise;
    bool wasRising = false;
};

} // namespace thunder::ui
