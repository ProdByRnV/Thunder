# Thunder

**Drop Thunder on a melody and get an in-key, mix-ready texture layer in seconds — free.**

Built in C++ with JUCE 8, for Windows, macOS and Linux.

Thunder is an audio effect plugin for your DAW (there is no standalone app) that turns a simple melody into a textured background layer that sits musically underneath the original. Instead of stacking a pitch shifter, granulator, reverb, EQ and volume shaper, you insert Thunder and get one layer that already works in the mix:

- **In-key harmony** — shift the layer by octaves, semitones, or by intervals that stay in your song's key.
- **Grains, space and tone** — granular texture, lush reverb, filtering and stereo width.
- **Mix-safe by default** — automatic level matching, a high-pass on the layer, and a safety limiter.
- **Capture and drag** — record a few bars of the layer to WAV and drag it straight into your DAW.
- **Electric Storm UI** — an audio-reactive lightning theme (with a Reduce Motion option).

> **Status: early development.** Thunder is not usable yet. The first public pre-release (v0.5) will be published on the [Releases](https://github.com/ProdByRnV/Thunder/releases) page.

## Formats and platforms

| Platform | Formats |
|---|---|
| Windows 10/11 (x64) | VST3 (CLAP planned) |
| macOS 11+ (Apple silicon + Intel) | VST3, AU (CLAP planned) |
| Linux x86_64 | VST3 (LV2 and CLAP planned) |

## Building from source

Requirements:

- CMake 3.24 or newer
- A C++20 compiler: Visual Studio 2022/2026 (Windows), Xcode 14+ (macOS), or GCC 11+/Clang 14+ (Linux)
- Git
- On Linux, the JUCE dependencies (ALSA, FreeType, X11 and friends) — see [JUCE's Linux dependencies](https://github.com/juce-framework/JUCE/blob/master/docs/Linux%20Dependencies.md)

```bash
git clone https://github.com/ProdByRnV/Thunder.git
cd Thunder
git submodule update --init --recursive --depth 1

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

On Windows, if the submodule step fails with "Filename too long", enable long paths once with `git config --global core.longpaths true` and run it again (or clone into a shorter folder such as `C:\src`).

Presets are available too, e.g. `cmake --preset vs2026` then `cmake --build --preset vs2026-release` on Windows.

The built plugins are in `build/source/Thunder_artefacts/Release/` (for example `VST3/Thunder.vst3`). Copy them to your system plugin folder, or configure with `-DTHUNDER_COPY_AFTER_BUILD=ON` to have the build do it.
