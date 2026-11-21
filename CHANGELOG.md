# Changelog

All notable changes to Thunder are listed here. Versions follow [Semantic Versioning](https://semver.org).
Each release's notes on GitHub are taken from its section below.

## Unreleased

- Runs as a VST3 and CLAP plugin on Windows, macOS and Linux, and as an AU on macOS.
- Every control can be automated in your DAW and is saved with your project. Changes are smoothed, so moving a control or automating it never clicks, and projects saved with earlier versions keep loading.
- **Thunder's own dark interface**:
  - The module chain shows Pitch, Grains, Space, Tone and Width in processing order. Click a module to see its controls, or its dot to switch it on or off.
  - Output Mode, Mix, Layer, In and Out, Auto Gain and the limiter are always in view.
  - Drag a knob up or down; hold Shift for fine adjustments. The mouse wheel nudges it, and a double-click resets it.
  - Click a knob's value to type an exact one, or right-click the knob to reset, enter, copy or paste its value.
  - ↺ next to a module's switch resets all of that module's controls.
  - Hover over any control to see what it does, with its value and range.
  - Resize the window from its corner in 5% steps, from 75% to 200%, or pick a size in Settings. Each project remembers its size, and a window that wouldn't fit your screen opens smaller.
  - **Settings** (the gear): window size, tooltips on or off, capture format and folder, and whether presets keep your Key, Scale and Output Mode.
  - **About** (ⓘ or the logo) shows the version and a link for reporting an issue.
  - Every control gives screen readers its name, what it does and its value.
- **Presets**:
  - 12 built-in presets, plus Init, which resets every control.
  - ◄ and ► step through the presets; click the name for the full list. A `*` shows when you've changed the sound.
  - Save your own presets, with a name, author, category and description, to `Documents/Thunder/Presets`. Thunder asks before replacing one, and a save never leaves a preset half-written.
  - Loading a preset keeps your Key, Scale and Output Mode (you can turn this off in Settings), and switches smoothly while playing.
  - Your project remembers which preset it was on.
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
- **Capture**: record 1, 2, 4, 8 or 16 bars of the layer, or of Thunder's whole output, to a WAV file and drag it straight into your DAW.
  - Arm it and press play: recording starts at the next bar, and the file starts exactly on that bar.
  - A loop that wraps around on a bar line keeps recording; if playback stops or jumps elsewhere, the capture is cancelled and no file is left behind.
  - 24-bit or 32-bit float at your project's sample rate, saved to `Documents/Thunder/Captures` or a folder you choose, with descriptive names such as `Thunder_A-min_140bpm_4bars_20261121-143005.wav`.
  - Right-click a capture to Save as…, Show in folder or Delete it.
  - Thunder on several tracks can capture at the same time; each capture gets its own file.
- **Installing**: each download includes step-by-step instructions for your system. On Linux, Thunder runs on Ubuntu 22.04 or newer (glibc 2.35+), on x86_64 and ARM64.
