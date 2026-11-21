#pragma once

#include "plugin/ui/ParamUi.h"
#include "plugin/ui/components/Controls.h"
#include "plugin/ui/components/ThunderKnob.h"

#include <memory>
#include <vector>

// R-MODULE (UI.md §8.4) and the module layouts of §11: one panel per module,
// shown one at a time. Each panel has a header (title and on/off switch) and a
// body on a 4-column grid. A switched-off module's controls stay editable but
// dimmed, so they can be set up before switching it on.

namespace thunder::ui
{

class ModulePanel : public juce::Component
{
public:
    ModulePanel (juce::AudioProcessorValueTreeState& apvts, const juce::String& title, params::ParamId onParam);
    ~ModulePanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    const juce::String& getTitleText() const noexcept { return title; }
    bool isBodyDimmed() const noexcept { return body.getAlpha() < 1.0f; }

protected:
    virtual void layoutBody (juce::Rectangle<int> area) = 0;

    ThunderKnob& addKnob (params::ParamId id, const juce::String& label,
                          ThunderKnob::Size size = ThunderKnob::Size::medium);
    SegmentedControl& addSegmented (params::ParamId id, std::vector<int> options = {}, juce::StringArray labels = {});
    ParamCheckbox& addCheckbox (params::ParamId id, const juce::String& label);

    // Cell `column` (0–3) of a row, the full row height.
    static juce::Rectangle<int> cell (juce::Rectangle<int> row, int column, int span = 1);

    juce::AudioProcessorValueTreeState& apvts;
    juce::Component body;

private:
    juce::String title;
    ThunderToggle toggle;
    std::vector<std::unique_ptr<juce::Component>> controls;
    ParameterWatcher onWatcher;
};

class PitchPanel final : public ModulePanel
{
public:
    explicit PitchPanel (juce::AudioProcessorValueTreeState& apvts);

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void showControlsForMode (int mode);

    SegmentedControl& mode;
    ThunderKnob& octave;
    ThunderKnob& interval;
    SegmentedControl& direction;
    ThunderKnob& semitones;
    ThunderKnob& fine;
    ThunderKnob& mix;
    ParamCheckbox& formants;
    int currentMode = 0;
    ParameterWatcher modeWatcher;
};

class GrainsPanel final : public ModulePanel
{
public:
    explicit GrainsPanel (juce::AudioProcessorValueTreeState& apvts);

private:
    void layoutBody (juce::Rectangle<int> area) override;

    ThunderKnob& amount;
    ThunderKnob& grainSize;
    ThunderKnob& density;
    ThunderKnob& position;
    ThunderKnob& pitchSpread;
    ThunderKnob& panSpread;
    ThunderKnob& reverse;
};

class SpacePanel final : public ModulePanel
{
public:
    explicit SpacePanel (juce::AudioProcessorValueTreeState& apvts);

private:
    void layoutBody (juce::Rectangle<int> area) override;

    SegmentedControl& mode;
    ThunderKnob& mix;
    ThunderKnob& decay;
    ThunderKnob& preDelay;
    ThunderKnob& damping;
};

class TonePanel final : public ModulePanel
{
public:
    explicit TonePanel (juce::AudioProcessorValueTreeState& apvts);

private:
    void layoutBody (juce::Rectangle<int> area) override;

    ThunderKnob& highPass;
    SegmentedControl& highPassSlope;
    ThunderKnob& lowPass;
    SegmentedControl& lowPassSlope;
};

class WidthPanel final : public ModulePanel
{
public:
    explicit WidthPanel (juce::AudioProcessorValueTreeState& apvts);

private:
    void layoutBody (juce::Rectangle<int> area) override;

    ThunderKnob& width;
};

} // namespace thunder::ui
