#pragma once

#include "plugin/ui/ParamUi.h"
#include "plugin/ui/components/Buttons.h"
#include "plugin/ui/components/Controls.h"
#include "plugin/ui/components/ThunderKnob.h"
#include "plugin/ui/visualiser/SpectrumView.h"

#include <array>
#include <functional>
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

    // [MSG] The header's ↺: every control of this module back to its default, as
    // host gestures (so a host's undo can take it back). The on/off switch stays.
    void resetToDefaults();
    IconButton& getResetButton() noexcept { return resetButton; }

protected:
    virtual void layoutBody (juce::Rectangle<int> area) = 0;

    // The header between the title and the reset button, for controls that
    // belong in it (Grains' extras). Nothing by default.
    virtual void layoutHeader (juce::Rectangle<int> area) { juce::ignoreUnused (area); }

    ThunderKnob& addKnob (params::ParamId id, const juce::String& label,
                          ThunderKnob::Size size = ThunderKnob::Size::medium);
    SegmentedControl& addSegmented (params::ParamId id, std::vector<int> options = {}, juce::StringArray labels = {});
    ParamCheckbox& addCheckbox (params::ParamId id, const juce::String& label);
    ParamChip& addChip (params::ParamId id, const juce::String& label, juce::Colour accent);
    ParamDropdown& addDropdown (params::ParamId id);

    // Cell `column` (0–3) of a row, the full row height.
    static juce::Rectangle<int> cell (juce::Rectangle<int> row, int column, int span = 1);

    juce::AudioProcessorValueTreeState& apvts;
    juce::Component body;

private:
    juce::String title;
    ThunderToggle toggle;
    IconButton resetButton;
    params::ParamId moduleOnParam;
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

// UI.md §11.2. (Reverse, `timeReverse`, is optional and not part of v1.0.)
class TimePanel final : public ModulePanel
{
public:
    explicit TimePanel (juce::AudioProcessorValueTreeState& apvts);

private:
    void layoutBody (juce::Rectangle<int> area) override;

    SegmentedControl& speed;
    ThunderKnob& smooth;
};

class GrainsPanel final : public ModulePanel
{
public:
    explicit GrainsPanel (juce::AudioProcessorValueTreeState& apvts);

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void layoutHeader (juce::Rectangle<int> area) override;

    ThunderKnob& amount;
    ThunderKnob& grainSize;
    ThunderKnob& density;
    ThunderKnob& position;
    ThunderKnob& pitchSpread;
    ThunderKnob& panSpread;
    ThunderKnob& reverse;

    // In the header (UI.md §11.3 row 3, PRD DEC-048): not dimmed with the body.
    ParamCheckbox scaleLock;
    ParamCheckbox tempoSync;
    ParamChip freeze;
};

// UI.md §11.4, in Static's violet: Character, Amount, Rate (free, or a note
// value while Sync is on) and the target chips.
class StaticPanel final : public ModulePanel
{
public:
    explicit StaticPanel (juce::AudioProcessorValueTreeState& apvts);

private:
    void layoutBody (juce::Rectangle<int> area) override;

    SegmentedControl& character;
    ThunderKnob& amount;
    ThunderKnob& rate;
    ThunderKnob& rateSynced;
    ParamCheckbox& sync;
    std::array<ParamChip*, 5> targets{};
    ParameterWatcher syncWatcher;
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

// Row 2 (UI.md §11.6): the filter curve over the live spectrum, with the same
// draggable handles as the spectrum view, and "Open in Spectrum".
class TonePanel final : public ModulePanel
{
public:
    TonePanel (juce::AudioProcessorValueTreeState& apvts, const VisualModel& visuals);

    SpectrumView& getCurve() noexcept { return curve; }
    ThunderButton& getOpenSpectrumButton() noexcept { return openSpectrum; }

    // Set by the editor: show the spectrum view in the visualiser.
    std::function<void()> onOpenSpectrum;

private:
    void layoutBody (juce::Rectangle<int> area) override;

    ThunderKnob& highPass;
    SegmentedControl& highPassSlope;
    ThunderKnob& lowPass;
    SegmentedControl& lowPassSlope;
    SpectrumView curve;
    ThunderButton openSpectrum{juce::String ("Open in Spectrum"), ThunderButton::Variant::ghost};
};

class WidthPanel final : public ModulePanel
{
public:
    explicit WidthPanel (juce::AudioProcessorValueTreeState& apvts);

private:
    void layoutBody (juce::Rectangle<int> area) override;

    ThunderKnob& width;
    ParamCheckbox& autopan;
    ParamDropdown& autopanRate;
    juce::Label autopanRateCaption;
    ThunderKnob& autopanDepth;
};

// UI.md §11.8: the shape picker (a thumbnail of each curve), Rate, Depth and
// Smooth.
class ShapePanel final : public ModulePanel
{
public:
    explicit ShapePanel (juce::AudioProcessorValueTreeState& apvts);

private:
    void layoutBody (juce::Rectangle<int> area) override;

    SegmentedControl& shape;
    ParamDropdown& rate;
    juce::Label rateCaption;
    ThunderKnob& depth;
    ThunderKnob& smooth;
};

// UI.md §11.9: Mode (Envelope; Spectral comes later and isn't shown), Amount
// and Release. The reduction meter comes with the module panel task (T-0.8-10).
class DuckPanel final : public ModulePanel
{
public:
    explicit DuckPanel (juce::AudioProcessorValueTreeState& apvts);

private:
    void layoutBody (juce::Rectangle<int> area) override;

    SegmentedControl& mode;
    ThunderKnob& amount;
    ThunderKnob& release;
};

} // namespace thunder::ui
