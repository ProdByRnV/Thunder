#pragma once

#include "VisualModel.h"

#include "plugin/ui/theme/ThunderTheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace thunder::ui
{

// The scrolling waveform (PRD UI-VIS-001/004/005/008; UI.md §8.2, §9.19):
// newest audio at the right, 1, 2 or 4 bars across at the song's tempo, with
// bar and beat lines where the host's bars and beats fall.
// - The dry signal: a dim shape with an outline.
// - The layer over it: cyan at 55% with a 1.5 px outline and a soft glow; its
//   loudest 10% turns hot.
// - Live grains (optional): a spark per sounding grain. Left–right is how far
//   back it reads, up–down its pitch (the top is +12 semitones), the colour
//   runs to violet as it pans out, size and brightness follow its window, and
//   a faint trail shows the stretch it has read. Under Reduce Motion they are
//   still dots, updated 10 times a second, without trails.
// Each pixel column shows the largest sample of the frames it covers, mirrored
// about the centre (min/max downsampling, UI-VIS-004). When the transport
// stops the picture freezes, and dims after 2 s. With nothing to show, the
// well says "Play your melody to see the layer". Clicks go through to the
// visualiser.
//
// Cost: the waveform and its grid live in a strip of pixels at the display's
// resolution. Each frame the strip moves left by the columns that have
// arrived (usually one to three) and only those are rendered, straight into
// the pixels (the glow as a vertical halo per column). Painting is then one
// image, the bar numbers and the sparks. The whole strip is rendered again
// only when the size, scale, time window, tempo or options change.
class WaveformView final : public juce::Component
{
public:
    explicit WaveformView (const VisualModel& model);

    void paint (juce::Graphics& g) override;

    void setBars (int bars);
    int getBars() const noexcept { return bars; }
    void setShowDry (bool show);
    void setShowGrains (bool show);
    bool isShowingGrains() const noexcept { return showGrains; }

    // Still dots at 10 updates a second (UI.md §9.19). The Reduce Motion
    // setting itself comes with the motion work.
    void setReduceMotion (bool reduce);

    // [MSG] Called every frame: takes the model's grains (at most 10 times a
    // second under Reduce Motion). Returns true if the sparks changed.
    bool takeGrains (double seconds);

    // Where a grain that reads `ageSeconds` back sits across the view.
    float xForAge (float ageSeconds) const noexcept;
    // … and how high one transposed by `semitones` sits.
    float yForSemitones (float semitones) const noexcept;

    // For tests: what the last paint drew, and how often the whole strip was
    // rendered (as opposed to only its new columns).
    bool wasEmpty() const noexcept { return lastPaintEmpty; }
    int getLastBarLines() const noexcept { return lastBarLines; }
    int getFullRenders() const noexcept { return fullRenders; }
    const visual::GrainSnapshot& getShownGrains() const noexcept { return shownGrains; }

private:
    struct Label
    {
        float x = 0.0f;
        int bar = 0;
    };

    // The strip of pixels and what it was rendered for.
    struct Strip
    {
        juce::Image image;
        float scale = 0.0f;
        double framesPerColumn = 0.0;
        std::int64_t lastColumn = -1; // absolute index of its rightmost column
        int generation = -1;
        bool showDry = true;
        float layerReference = 0.0f; // the level the hot tint is relative to

        // Carried from one column to the next.
        float dryReach = -1.0f, layerReach = -1.0f; // < 0: no neighbour
        int lineLeft = 0;                           // columns of a grid line still to draw
        bool lineIsBar = false;
    };

    void scanWindow();
    void updateStrip (float scale);
    void renderColumns (int firstX, std::int64_t firstColumn, int count);
    void paintGrid (juce::Graphics& g, float alpha);
    void paintLabels (juce::Graphics& g, float alpha);
    void paintGrains (juce::Graphics& g);
    void paintEmpty (juce::Graphics& g);

    const VisualModel& model;
    int bars = theme::visualiser::defaultBars;
    bool showDry = true;
    bool showGrains = true;
    bool reduceMotion = false;
    double sinceGrains = 0.0;
    visual::GrainSnapshot shownGrains;

    Strip strip;
    std::vector<float> glowTable; // the halo's opacity by distance (physical px)
    std::vector<Label> labels;
    float layerPeak = 0.0f;
    bool lastPaintEmpty = true;
    int lastBarLines = 0;
    int fullRenders = 0;
};

} // namespace thunder::ui
