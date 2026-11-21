#pragma once

#include "plugin/ui/theme/ThunderIcons.h"
#include "plugin/ui/theme/ThunderPaint.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace thunder::ui
{

// C-BUTTON (UI.md §9.6): primary, secondary, ghost or danger. A press draws
// it at 96% and a click flashes its outline (PRD UI-FX-003). Keyboard focus
// shows the focus ring; a click never takes the focus.
class ThunderButton final : public juce::TextButton
{
public:
    enum class Variant
    {
        primary,
        secondary,
        ghost,
        danger
    };

    explicit ThunderButton (const juce::String& buttonText = {}, Variant variant = Variant::secondary);

    void setVariant (Variant v);
    Variant getVariant() const noexcept { return variant; }
    float getFlashLevel() const noexcept { return flash.getLevel(); }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    using juce::TextButton::clicked; // keeps both overloads visible
    void clicked() override;

    Variant variant;
    paint::Flash flash;
};

// C-ICONBUTTON (UI.md §9.7): a 28 × 28 target with a 16 px glyph,
// text.secondary turning text.primary on hover. Its name is its tooltip and
// its accessible title.
class IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& name, icons::Icon glyph);

    void setIcon (icons::Icon glyph);
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    icons::Icon icon;
};

} // namespace thunder::ui
