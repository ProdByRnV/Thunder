#pragma once

#include "plugin/ui/components/Controls.h"
#include "plugin/ui/visualiser/SpectrumView.h"
#include "plugin/ui/visualiser/VisualModel.h"
#include "plugin/ui/visualiser/WaveformView.h"

#include <functional>
#include <memory>

namespace thunder::ui
{

// R-VISUALISER (UI.md §8.2): the well's contents. The waveform view (default)
// or the spectrum view, switched by the WAVE | SPECTRUM control in the
// bottom-right corner (half-transparent until the pointer is over it).
// Right-click: time window (1, 2 or 4 bars), Freeze display, Show dry, Show
// grains (UI.md §10.1, §9.19). The editor remembers the view per project and
// the menu's choices for every project.
class VisualiserRegion final : public juce::Component
{
public:
    enum class View
    {
        wave,
        spectrum
    };

    struct Options
    {
        int bars = theme::visualiser::defaultBars;
        bool showDry = true;
        bool showGrains = true;
    };

    // Menu items (for tests).
    static constexpr int kMenuBars = 10; // + 1, 2 or 4
    static constexpr int kMenuFreeze = 20;
    static constexpr int kMenuShowDry = 21;
    static constexpr int kMenuShowGrains = 22;

    VisualiserRegion (VisualModel& model, juce::AudioProcessorValueTreeState& apvts);
    ~VisualiserRegion() override;

    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    // [MSG] Without calling onViewChosen (the editor sets the remembered view).
    void setView (View newView);
    View getView() const noexcept { return view; }
    void setOptions (const Options& newOptions);
    const Options& getOptions() const noexcept { return options; }

    // Set by the editor.
    std::function<void (View)> onViewChosen;
    std::function<void (const Options&)> onOptionsChanged;

    // [MSG] The parts of the visual data it needs now (UiBridge::kFrames …).
    std::uint32_t wantedParts() const noexcept;

    // [MSG] After VisualModel::update: repaints the visible view if it changed.
    void refresh (const VisualModel::Changes& changes, double seconds);

    // [MSG] The right-click menu, and what choosing an item does.
    juce::PopupMenu buildMenu() const;
    void chooseMenuItem (int itemId);

    WaveformView& getWaveform() noexcept { return waveform; }
    SpectrumView& getSpectrum() noexcept { return spectrum; }
    SegmentedControl& getViewToggle() noexcept { return toggle; }

private:
    class HoverFade;

    void chooseView (View newView);

    VisualModel& model;
    WaveformView waveform;
    SpectrumView spectrum;
    SegmentedControl toggle;
    std::unique_ptr<HoverFade> hoverFade;
    Options options;
    View view = View::wave;
};

} // namespace thunder::ui
