#pragma once

#include "plugin/ui/ParamUi.h"
#include "plugin/ui/components/Buttons.h"
#include "plugin/ui/components/Controls.h"
#include "plugin/ui/components/Meters.h"
#include "plugin/ui/components/ModuleDisplays.h"
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

    // [MSG] The visualiser data this panel's live display needs while it is
    // shown (UiBridge::kFrames | kSpectrum | kGrains; frames always flow for
    // the meters), and a repaint of the display when its data changed.
    virtual std::uint32_t wantedParts() const noexcept { return 0; }
    virtual void refresh (const VisualModel::Changes& changes) { juce::ignoreUnused (changes); }

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
    GrainsPanel (juce::AudioProcessorValueTreeState& apvts, const VisualModel& visuals);

    std::uint32_t wantedParts() const noexcept override;
    void refresh (const VisualModel::Changes& changes) override;
    GrainCloudView& getCloud() noexcept { return cloud; }

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

    GrainCloudView cloud; // row 2, column 4 (UI.md §11.3)
};

// UI.md §11.4, in Static's violet: Character, Amount, Rate (free, or a note
// value while Sync is on) and the target chips.
class StaticPanel final : public ModulePanel
{
public:
    StaticPanel (juce::AudioProcessorValueTreeState& apvts, const VisualModel& visuals);

    void refresh (const VisualModel::Changes& changes) override;
    StaticTraceView& getTrace() noexcept { return trace; }

private:
    void layoutBody (juce::Rectangle<int> area) override;

    SegmentedControl& character;
    ThunderKnob& amount;
    ThunderKnob& rate;
    ThunderKnob& rateSynced;
    ParamCheckbox& sync;
    std::array<ParamChip*, 5> targets{};
    ParameterWatcher syncWatcher;
    StaticTraceView trace;
};

class SpacePanel final : public ModulePanel
{
public:
    explicit SpacePanel (juce::AudioProcessorValueTreeState& apvts);

    // Decay's value: "2.4 s", whole seconds from 10 s.
    static juce::String decayText (double seconds);

private:
    void layoutBody (juce::Rectangle<int> area) override;

    SegmentedControl& mode;
    ThunderKnob& mix;
    ThunderKnob& decay;
    ThunderKnob& preDelay;
    ThunderKnob& damping;
    params::SpaceMode currentMode = params::SpaceMode::Cloud;
    ParameterWatcher modeWatcher; // Decay reads in seconds for the mode
};

// Row 2 (UI.md §11.6): the filter curve over the live spectrum, with the same
// draggable handles as the spectrum view, and "Open in Spectrum".
class TonePanel final : public ModulePanel
{
public:
    TonePanel (juce::AudioProcessorValueTreeState& apvts, const VisualModel& visuals);

    SpectrumView& getCurve() noexcept { return curve; }
    ThunderButton& getOpenSpectrumButton() noexcept { return openSpectrum; }

    std::uint32_t wantedParts() const noexcept override;
    void refresh (const VisualModel::Changes& changes) override;

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
    WidthPanel (juce::AudioProcessorValueTreeState& apvts, const VisualModel& visuals);

    void refresh (const VisualModel::Changes& changes) override;

private:
    void layoutBody (juce::Rectangle<int> area) override;

    ThunderKnob& width;
    ParamCheckbox& autopan;
    ParamDropdown& autopanRate;
    juce::Label autopanRateCaption;
    ThunderKnob& autopanDepth;
    CorrelationMeter correlation; // row 1 (UI.md §11.7 row 3; the body has no third row)
    juce::Label correlationCaption;
};

// UI.md §11.8: the shape picker (a thumbnail of each curve), Rate, Depth and
// Smooth.
class ShapePanel final : public ModulePanel
{
public:
    ShapePanel (juce::AudioProcessorValueTreeState& apvts, const VisualModel& visuals);

    void refresh (const VisualModel::Changes& changes) override;
    ShapeView& getDisplay() noexcept { return display; }

private:
    void layoutBody (juce::Rectangle<int> area) override;

    SegmentedControl& shape;
    ParamDropdown& rate;
    juce::Label rateCaption;
    ThunderKnob& depth;
    ThunderKnob& smooth;
    ShapeView display;
    ParameterWatcher typeWatcher, depthWatcher, smoothWatcher;
    float shownPhase = -1.0f, shownGain = -1.0f;
    const VisualModel& visualModel;
};

// UI.md §11.9: Mode (Envelope; Spectral comes later and isn't shown), Amount,
// Release and the reduction meter.
class DuckPanel final : public ModulePanel
{
public:
    DuckPanel (juce::AudioProcessorValueTreeState& apvts, const VisualModel& visuals);

    void refresh (const VisualModel::Changes& changes) override;
    ReductionMeter& getMeter() noexcept { return meter; }

private:
    void layoutBody (juce::Rectangle<int> area) override;

    SegmentedControl& mode;
    ThunderKnob& amount;
    ThunderKnob& release;
    ReductionMeter meter;
    juce::Label meterCaption;
    juce::String shownReduction;
};

} // namespace thunder::ui
