#pragma once

#include <juce_graphics/juce_graphics.h>

// Thunder's own vector glyphs (UI.md §2.2), drawn in a unit square and scaled
// to fit wherever they're used.

namespace thunder::ui::icons
{

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

// Fits a unit-square glyph into `area`, keeping its proportions.
inline juce::Path fitted (const juce::Path& glyph, juce::Rectangle<float> area)
{
    auto p = glyph;
    p.applyTransform (glyph.getTransformToScaleToFit (area, true));
    return p;
}

} // namespace thunder::ui::icons
