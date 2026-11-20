# Changelog

All notable changes to Thunder are listed here. Versions follow [Semantic Versioning](https://semver.org).
Each release's notes on GitHub are taken from its section below.

## Unreleased

Thunder is in early development and has no public release yet.

- Builds as a VST3 plugin on Windows, macOS and Linux, plus AU on macOS. Thunder is a plugin only: there is no standalone app.
- Continuous integration on Windows, macOS (universal), Linux x86_64 and Linux ARM64, with plugin validation and sanitizer checks.
- The full v0.5 parameter set, every one automatable, smoothed where continuous, and saved with your session (with a versioned state format so older sessions keep loading).
- A temporary generic editor that lists every parameter. Thunder's own interface replaces it in v0.5.
- First sound: the layer is pitch-shifted and sent through a lush "Cloud" reverb (Mix, Decay, Pre-delay, Damping), blended with your melody, kept exactly in time with it, and reported to the host as 80 ms of latency.
- Input and output gain are active. The remaining parameters are wired up as their sound modules arrive.
- Groundwork for in-key harmony: Key Root and Scale (nine scales) are read by the pitch engine, which can now work out the in-key shift for any interval, up or down, and the best single fixed shift as a fallback. A monophonic pitch tracker follows the melody note by note and ignores chords and noise.
- All three pitch modes:
  - **Octave:** −2, −1, +1 or +2 octaves. The default, +1, is now active.
  - **Semitone:** −24 to +24 semitones plus Fine (±100 cents).
  - **Interval:** an interval up or down that stays in your key. Each note of a single-note melody gets its own in-key interval, landing about 20 ms into the note. Chords and other material without one clear note get the single fixed shift that keeps the most of the scale in key. A short hold stops it flipping between the two.
- Every change of shift glides over 15 ms. Switching modes during playback is click-free.
- Preserve Formants keeps upward shifts from sounding "chipmunky". It applies to upward shifts only, so downward layers keep their low end.
- Pitch Mix blends the shifted layer with the unshifted one.
- Groundwork for Grains:
  - a granular engine that plays short, smoothly faded grains of the last few seconds of the layer, at an exact density;
  - up to 64 grains at once, with no clicks when grains start or end;
  - it isn't in the signal chain yet. Its controls and its place after Pitch arrive next.
