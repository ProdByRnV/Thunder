# Changelog

All notable changes to Thunder are listed here. Versions follow [Semantic Versioning](https://semver.org).
Each release's notes on GitHub are taken from its section below.

## Unreleased

- Runs as a VST3 plugin on Windows, macOS and Linux, and as an AU on macOS.
- Every control can be automated in your DAW and is saved with your project. Changes are smoothed, so moving a control or automating it never clicks, and projects saved with earlier versions keep loading.
- A temporary editor lists every control until Thunder's own interface arrives.
- **Pitch**, in three modes:
  - **Octave:** −2, −1, +1 or +2 octaves (+1 by default).
  - **Semitone:** −24 to +24 semitones, plus Fine (±100 cents).
  - **Interval:** an interval up or down that stays in your key (Key Root and Scale, nine scales). Each note of a single-note melody gets its own in-key interval; chords get the one fixed shift that keeps the most notes in key.
  - Shift and mode changes glide, so switching during playback is click-free.
  - **Preserve Formants** keeps upward shifts from sounding "chipmunky".
  - **Pitch Mix** blends the shifted layer with the unshifted one.
- **Grains**: short, smoothly faded slices of the last few seconds of the layer.
  - Size, Density and Amount.
  - Position Spread (reaching up to 4 s back), Pitch Spread (up to ±12 semitones), Pan Spread and Reverse Probability.
  - The grain cloud keeps a steady level however dense or spread out it is.
- **Space**: a lush "Cloud" reverb with Mix, Decay, Pre-delay and Damping.
- **Tone**: a high-pass (on by default, at 200 Hz) and a low-pass on the layer, each 12 or 24 dB/oct.
- **Width**: stereo width from mono to 200%. Mono playback sounds the same at every width.
- Every module has its own on/off switch, which fades in and out without clicks.
- **Output**:
  - **Blend** (your melody plus the layer) or **Layer Only**. At the default Mix of 50% both play at full level.
  - **Auto Gain** keeps the layer 6 dB under your melody.
  - **Layer Level**, **Input Gain** and **Output Gain**.
  - A **safety limiter** keeps the output under −0.3 dBFS.
- The layer stays exactly in time with your melody: Thunder reports its latency (about 82 ms) to your DAW, which compensates for it.
- Offline bounces sound exactly like playback.
