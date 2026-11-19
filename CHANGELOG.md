# Changelog

All notable changes to Thunder are listed here. Versions follow [Semantic Versioning](https://semver.org).
Each release's notes on GitHub are taken from its section below.

## Unreleased

Thunder is in early development and has no public release yet.

- Builds as VST3 and a standalone app on Windows, macOS and Linux, plus AU on macOS.
- Continuous integration on Windows, macOS (universal), Linux x86_64 and Linux ARM64, with plugin validation and sanitizer checks.
- The full v0.5 parameter set, every one automatable, smoothed where continuous, and saved with your session (with a versioned state format so older sessions keep loading).
- A temporary generic editor that lists every parameter. Thunder's own interface replaces it in v0.5.
- First sound: the layer is pitch-shifted (Semitones and Fine for now) and sent through a lush "Cloud" reverb (Mix, Decay, Pre-delay, Damping), blended with your melody, kept exactly in time with it, and reported to the host as 80 ms of latency.
- Input and output gain are active. The remaining parameters are wired up as their sound modules arrive.
- Groundwork for in-key harmony: Key Root and Scale (nine scales) are read by the pitch engine, which can now work out the in-key shift for any interval, up or down, and the best single fixed shift as a fallback. A monophonic pitch tracker follows the melody note by note and ignores chords and noise. Interval mode itself arrives next.
