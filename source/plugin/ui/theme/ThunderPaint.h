#pragma once

#include "ThunderTheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Drawing helpers shared by Thunder's components (UI.md §6, §9, §13): glows,
// the click flash, focus rings, raised surfaces, shadows and the window's
// background texture. Message thread only.

namespace thunder::ui::paint
{

// A soft, tinted glow around a shape (UI.md §6.3). The shape is rendered into
// a small single-channel image on the CPU, blurred and kept until the shape,
// the blur or the display scale changes, so drawing it again is one image
// draw. (Blurring through the GPU renderer would read pixels back each time.)
class GlowCache
{
public:
    // Draws the glow of `shape` (filled) in `colour` at `opacity` (0–1).
    void draw (juce::Graphics& g, const juce::Path& shape, juce::Colour colour, const theme::Glow& glow, float opacity);

    // How many times the blur has been rendered (for tests).
    int getRenderCount() const noexcept { return renders; }

private:
    juce::Image image;
    juce::Path cachedShape;
    juce::Rectangle<float> area;
    float cachedScale = 0.0f;
    float cachedBlur = 0.0f;
    int renders = 0;
};

// The brief flash after a click or a switch turning on (UI.md §9.2, §9.6;
// PRD UI-FX-003): full strength, fading out over `motion::base` with an
// ease-out. Repaints its component while it runs.
class Flash final : private juce::Timer
{
public:
    explicit Flash (juce::Component& owner);
    ~Flash() override;

    void trigger();
    float getLevel() const noexcept; // 0–1
    bool isRunning() const noexcept { return isTimerRunning(); }

private:
    void timerCallback() override;

    juce::Component& owner;
    double startMs = 0.0;
};

// 1 px border.strong and glow.subtle, `border::focusOffset` outside `bounds`.
void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius);

// A raised surface (elevation 2): fill, 1 px border and the 4% inner top highlight.
void fillRaised (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius, juce::Colour fill,
                 juce::Colour border);

// The drop shadow under a popover or dialog (elevation 3 or 4).
void drawShadow (juce::Graphics& g, juce::Rectangle<int> bounds, const theme::Shadow& shadow);

// A layer that never changes at a given size (the window's background, the
// well): painted once into an image at the display's pixel scale and then
// drawn as one image until the size or scale changes.
class CachedLayer
{
public:
    void draw (juce::Graphics& g, juce::Rectangle<int> bounds, const std::function<void (juce::Graphics&)>& painter);
    void invalidate() noexcept { image = {}; }
    int getRenderCount() const noexcept { return renders; }

private:
    juce::Image image;
    juce::Rectangle<int> cachedBounds;
    float cachedScale = 0.0f;
    int renders = 0;
};

// The window's background (UI.md §6.4): base colour, a soft vignette and a
// fixed film grain (the same pattern every time).
void paintBackground (juce::Graphics& g, juce::Rectangle<float> bounds);

// The grain tile paintBackground repeats (for tests).
const juce::Image& grainTile();

} // namespace thunder::ui::paint
