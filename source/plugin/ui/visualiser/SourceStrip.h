#pragma once

#include "core/analysis/AnalysisWorker.h"
#include "plugin/ui/components/Buttons.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace thunder::ui
{

// The source strip (UI.md §16.2): the top 40 px of the visualiser well while
// there is a source. SOURCE, its name, its waveform with bar lines, its key,
// and ✕ to remove it. While loading it shows the progress; if loading failed,
// why.
//
// Bar lines: a captured source knows its tempo and meter; a dropped file
// follows the host's (setHostTempo), starting from the file's first sample.
class SourceStrip final : public juce::Component
{
public:
    SourceStrip();

    // [MSG] What to show. Hidden when there is no source.
    void setStatus (const analysis::AnalysisWorker::SourceStatus& status);
    void setHostTempo (double bpm, int numerator, int denominator);

    // Remove the source (✕).
    std::function<void()> onRemove;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // What the strip says, for tests: "melody.wav · 8.0 s · D minor".
    juce::String getCaption() const;

    // The x positions of the bar lines in the waveform area (for tests).
    std::vector<float> barLinePositions() const;
    juce::Rectangle<int> getWaveArea() const noexcept { return waveArea; }
    IconButton& getRemoveButton() noexcept { return removeButton; }

private:
    double barSeconds() const noexcept;

    analysis::AnalysisWorker::SourceStatus status;
    double hostBpm = 120.0;
    int hostNumerator = 4;
    int hostDenominator = 4;

    IconButton removeButton;
    juce::Rectangle<int> labelArea, nameArea, waveArea, keyArea;
};

} // namespace thunder::ui
