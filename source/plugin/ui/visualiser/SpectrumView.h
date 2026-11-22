#pragma once

#include "FilterResponse.h"
#include "VisualModel.h"

#include "core/params/ParamDefs.h"
#include "plugin/ui/theme/ThunderPaint.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace thunder::ui
{

// The spectrum view (PRD UI-VIS-003, FR-TONE-004; UI.md §8.2, §11.6): the dry
// signal as a line and the layer as a glowing filled area, 20 Hz–20 kHz on a
// log axis and −72…0 dB, with the Tone module's filter curve over them. The
// curve's high-pass and low-pass handles drag left and right to set `hpFreq`
// and `lpFreq` (one host gesture per drag) and show their value while hovered.
// The curve's flat part sits on the −12 dB line so it never hides under the
// top edge. With Tone switched off the curve is drawn dimmed and still works,
// like the module's own controls.
//
// The compact version is Tone's mini curve (380 × 80): no labels.
class SpectrumView final : public juce::Component
{
public:
    SpectrumView (const VisualModel& model, juce::AudioProcessorValueTreeState& apvts, bool compact = false);
    ~SpectrumView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void setShowDry (bool show);

    float xForFrequency (float hz) const noexcept;
    float frequencyForX (float x) const noexcept;
    float yForDecibels (float db) const noexcept;
    // Where the filter curve is at `hz`.
    float curveY (float hz) const noexcept;

    FilterResponse getFilter() const noexcept { return filter; }
    juce::Component& getHighPassHandle() noexcept;
    juce::Component& getLowPassHandle() noexcept;

private:
    class Handle;

    void paintGrid (juce::Graphics& g);
    void paintSpectrum (juce::Graphics& g);
    void paintCurve (juce::Graphics& g);
    void paintBubble (juce::Graphics& g);
    juce::Path line (const std::array<float, SpectrumAnalyser::kPoints>& db) const;
    void filterChanged();

    const VisualModel& model;
    juce::AudioProcessorValueTreeState& apvts;
    const bool compact;
    bool showDry = true;
    bool toneOn = true;
    FilterResponse filter;
    std::unique_ptr<Handle> highPass, lowPass;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> watchers;
};

} // namespace thunder::ui
