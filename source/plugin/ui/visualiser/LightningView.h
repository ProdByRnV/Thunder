#pragma once

#include "Lightning.h"

#include "plugin/ui/theme/ThunderTheme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace thunder::ui
{

// The bolts over the visualiser well (UI.md §12.1, §12.4; PRD UI-FX-001). It
// lies over the waveform and spectrum views, takes no clicks, and draws only
// while a strike is fading.
//
// Each bolt is drawn in three passes: a 10 px glow blurred by glow.strong at
// 30%, a 3 px body in the accent at 85%, and a 1 px hot core. Branches are
// dimmer. Each bolt is rendered once into an image (its glow blurred at a
// quarter of the display resolution, where a soft glow loses nothing), and each
// frame draws that image at the strike's brightness. A strike also brightens the well by 6% for 80 ms. At most
// maxBolts bolts at once: a new one replaces the oldest.
class LightningView final : public juce::Component
{
public:
    LightningView();
    ~LightningView() override;

    void paint (juce::Graphics& g) override;

    // [MSG] Strikes a bolt from a random point on the top edge to near the
    // layer's peak at the newest (right-hand) end of the view: `peak` is the
    // layer's level (0 … 1), `brightness` the output level's (0 … 1).
    void strike (float peak, float brightness);

    // [MSG] Moves the strikes on by `seconds`; true if anything needs drawing
    // (repaints what changed).
    bool advance (double seconds);

    // [MSG] An area bolts stay above (the view toggle, which has text: UI.md
    // §12.1 keeps lightning off text).
    void setKeepClear (juce::Rectangle<int> area) noexcept { keepClear = area; }

    int getNumBolts() const noexcept { return static_cast<int> (bolts.size()); }
    const lightning::BoltShape& getBolt (int index) const;
    float getAmbient() const noexcept;
    std::uint64_t getStrikeCount() const noexcept { return strikes; }
    std::uint64_t getDroppedCount() const noexcept { return dropped; } // refused by the flash limit
    const lightning::FlashLimiter& getFlashes() const noexcept { return flashes; }

    // [MSG] Removes every bolt at once (Reduce Motion turned on).
    void clear();

private:
    struct Bolt;

    juce::Rectangle<int> dirtyArea() const;
    static void renderSprite (Bolt& bolt, float scale);

    std::vector<std::unique_ptr<Bolt>> bolts;
    std::uint64_t strikes = 0;
    std::uint64_t dropped = 0;
    double clock = 0.0; // seconds of advance(), for the flash limit
    lightning::FlashLimiter flashes;
    juce::Rectangle<int> keepClear;
    double ambientAge = 1.0e9;
    float ambientBrightness = 0.0f;
};

} // namespace thunder::ui
