#pragma once

#include <juce_graphics/juce_graphics.h>

// Thunder's own vector glyphs (UI.md §2.2, §9.7), drawn in a unit square and
// scaled to fit wherever they're used. Line icons are stroked at a fixed width
// (1.5 logical px) whatever their size.

namespace thunder::ui::icons
{

// A glyph: a filled part and a stroked part (either may be empty).
struct Icon
{
    juce::Path fill;
    juce::Path stroke;
};

// The bolt: three straight segments in a zig-zag with a sharp bottom point.
inline juce::Path bolt()
{
    juce::Path p;
    p.startNewSubPath (0.62f, 0.0f);
    p.lineTo (0.18f, 0.56f);
    p.lineTo (0.47f, 0.56f);
    p.lineTo (0.34f, 1.0f);
    p.lineTo (0.82f, 0.40f);
    p.lineTo (0.53f, 0.40f);
    p.closeSubPath();
    return p;
}

// A triangle pointing left (◄) or right (►).
inline juce::Path arrow (bool pointsRight)
{
    juce::Path p;
    if (pointsRight)
        p.addTriangle (0.25f, 0.1f, 0.25f, 0.9f, 0.85f, 0.5f);
    else
        p.addTriangle (0.75f, 0.1f, 0.75f, 0.9f, 0.15f, 0.5f);
    return p;
}

// Three dots (⋯).
inline juce::Path moreDots()
{
    juce::Path p;
    for (const auto x : {0.15f, 0.5f, 0.85f})
        p.addEllipse (x - 0.09f, 0.41f, 0.18f, 0.18f);
    return p;
}

// Settings: a ring with eight teeth and a hub.
inline Icon gear()
{
    Icon icon;
    for (int i = 0; i < 8; ++i)
    {
        juce::Path tooth;
        tooth.addRoundedRectangle (0.44f, 0.03f, 0.12f, 0.19f, 0.03f);
        tooth.applyTransform (juce::AffineTransform::rotation (
            static_cast<float> (i) * juce::MathConstants<float>::pi * 0.25f, 0.5f, 0.5f));
        icon.fill.addPath (tooth);
    }
    icon.fill.addEllipse (0.4f, 0.4f, 0.2f, 0.2f);
    icon.stroke.addEllipse (0.21f, 0.21f, 0.58f, 0.58f);
    return icon;
}

// About: a ringed "i".
inline Icon info()
{
    Icon icon;
    icon.stroke.addEllipse (0.08f, 0.08f, 0.84f, 0.84f);
    icon.fill.addRoundedRectangle (0.453f, 0.44f, 0.094f, 0.29f, 0.04f);
    icon.fill.addEllipse (0.44f, 0.25f, 0.12f, 0.12f);
    return icon;
}

// Reset: an open circle with an arrowhead (↺).
inline Icon reset()
{
    Icon icon;
    icon.stroke.addCentredArc (0.5f, 0.52f, 0.31f, 0.31f, 0.0f, -1.75f, 3.2f, true);
    const auto tip = juce::Point<float> (0.5f, 0.52f).getPointOnCircumference (0.31f, -1.75f);
    icon.stroke.startNewSubPath (tip.translated (-0.16f, -0.13f));
    icon.stroke.lineTo (tip);
    icon.stroke.lineTo (tip.translated (-0.12f, 0.15f));
    return icon;
}

// Folder.
inline Icon folder()
{
    Icon icon;
    icon.stroke.startNewSubPath (0.11f, 0.25f);
    icon.stroke.lineTo (0.39f, 0.25f);
    icon.stroke.lineTo (0.48f, 0.35f);
    icon.stroke.lineTo (0.89f, 0.35f);
    icon.stroke.lineTo (0.89f, 0.79f);
    icon.stroke.lineTo (0.11f, 0.79f);
    icon.stroke.closeSubPath();
    return icon;
}

// Save: a disk with a label.
inline Icon save()
{
    Icon icon;
    icon.stroke.startNewSubPath (0.16f, 0.16f);
    icon.stroke.lineTo (0.72f, 0.16f);
    icon.stroke.lineTo (0.84f, 0.28f);
    icon.stroke.lineTo (0.84f, 0.84f);
    icon.stroke.lineTo (0.16f, 0.84f);
    icon.stroke.closeSubPath();
    icon.stroke.addRectangle (0.31f, 0.56f, 0.38f, 0.28f);
    return icon;
}

// Close (✕).
inline Icon close()
{
    Icon icon;
    icon.stroke.startNewSubPath (0.25f, 0.25f);
    icon.stroke.lineTo (0.75f, 0.75f);
    icon.stroke.startNewSubPath (0.75f, 0.25f);
    icon.stroke.lineTo (0.25f, 0.75f);
    return icon;
}

// Warning (⚠): a filled triangle with the "!" cut out, so it reads at 10 px.
inline juce::Path warning()
{
    juce::Path p;
    p.addTriangle (0.5f, 0.06f, 0.97f, 0.92f, 0.03f, 0.92f);
    p.addRoundedRectangle (0.44f, 0.34f, 0.12f, 0.32f, 0.05f);
    p.addEllipse (0.44f, 0.71f, 0.12f, 0.12f);
    p.setUsingNonZeroWinding (false);
    return p;
}

inline Icon fromPath (const juce::Path& filled)
{
    Icon icon;
    icon.fill = filled;
    return icon;
}

// Previous / next (◄ ►), sized like the line icons.
inline Icon step (bool pointsRight)
{
    Icon icon;
    if (pointsRight)
        icon.fill.addTriangle (0.34f, 0.24f, 0.34f, 0.76f, 0.74f, 0.5f);
    else
        icon.fill.addTriangle (0.66f, 0.24f, 0.66f, 0.76f, 0.26f, 0.5f);
    return icon;
}

// Fits a unit-square glyph into `area`, keeping its proportions.
inline juce::Path fitted (const juce::Path& glyph, juce::Rectangle<float> area)
{
    auto p = glyph;
    p.applyTransform (glyph.getTransformToScaleToFit (area, true));
    return p;
}

// Draws `icon` in `area` (square) in the current colour.
inline void draw (juce::Graphics& g, const Icon& icon, juce::Rectangle<float> area, float strokeWidth)
{
    const auto transform =
        juce::AffineTransform::scale (area.getWidth(), area.getHeight()).translated (area.getPosition());
    if (!icon.fill.isEmpty())
        g.fillPath (icon.fill, transform);
    if (!icon.stroke.isEmpty())
        g.strokePath (icon.stroke,
                      juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      transform);
}

} // namespace thunder::ui::icons
