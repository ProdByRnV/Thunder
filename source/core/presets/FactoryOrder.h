#pragma once

#include <array>
#include <string_view>

// The order of the factory presets in the DAW's own preset menu (PRD
// FR-PRESET-015): program 0 is Init, then these. Projects remember their
// program by number, so once a release ships this list only ever grows at the
// end: never reorder, rename or remove an entry (a renamed preset keeps its
// place under its new name). Every factory preset file must be listed exactly
// once (tested).

namespace thunder::presets
{

inline constexpr std::array<std::string_view, 56> kFactoryProgramOrder{
    // Atmos
    "Night Static", "Cloud Bank", "Low Haze", "Fog Line", "Midnight Drift", "Wide Horizon", "Slow Bloom",
    "Storm Front",
    // Harmony
    "Third Above", "Fourth Below", "Fifth Rain", "Sixth Glow", "Third Below", "Wide Octave", "Octave Under",
    "Choir Fifths",
    // Grit
    "Static Sand", "Grain Crush", "Rust Burst", "Broken Signal", "Dirt Road", "Fracture", "Sand Storm", "Low Grit",
    // Shimmer
    "Glass Choir", "Shimmer Veil", "Ice Halo", "Angel Fifth", "Crystal Rain", "Halo Pad", "Starlight",
    "Rising Light",
    // Rhythmic
    "Pump Layer", "Gate Eighths", "Trance Gate", "Half Bounce", "Quarter Drag", "Tempo Grains", "Duck Under",
    "Ramp Pulse",
    // Lo-Fi
    "Dust Bloom", "Tape Haze", "Vinyl Night", "Dusty Keys", "Lazy Afternoon", "Cassette Swell", "Bedroom Echo",
    "Muffled Rain",
    // Experimental
    "Reverse Tide", "Scatter Field", "Mirror World", "Alien Choir", "Particle Storm", "Deep Space", "Glitch Garden",
    "Phase Shift",
};

} // namespace thunder::presets
