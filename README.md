# Thunder

**Drop Thunder on a melody and get an in-key, mix-ready texture layer in seconds — free.**

For Windows, macOS and Linux.

Thunder is an audio effect plugin for your DAW that turns a simple melody into a textured background layer that sits musically underneath the original. Instead of stacking a pitch shifter, granulator, reverb, EQ and volume shaper, you insert Thunder and get one layer that already works in the mix:

- **In-key harmony** — shift the layer by octaves, semitones, or by intervals that stay in your song's key.
- **Grains, space and tone** — granular texture, lush reverb, filtering and stereo width.
- **Mix-safe by default** — automatic level matching, a high-pass on the layer, and a safety limiter.
- **Capture and drag** — record a few bars of the layer to WAV and drag it straight into your DAW.
- **Electric Storm UI** — an audio-reactive lightning theme (with a Reduce Motion option).

> **Status: in development.** The first release, v1.0, will be published on the [Releases](https://github.com/ProdByRnV/Thunder/releases) page.

## Formats and platforms

| Platform | Formats |
|---|---|
| Windows 10/11 (x64) | VST3, CLAP |
| macOS 11+ (Apple silicon + Intel) | VST3, AU, CLAP |
| Linux x86_64 and ARM64 (Ubuntu 22.04+ or another distribution with glibc 2.35+) | VST3, CLAP |

Each download includes `How to install.txt` with the steps for your system.

## Building from source

Requirements:

- CMake 3.24 or newer
- A C++20 compiler: Visual Studio 2022/2026 (Windows), Xcode 14+ (macOS), or GCC 12+/Clang 14+ (Linux)
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

The built plugins are in `build/source/Thunder_artefacts/Release/`: `VST3/Thunder.vst3`, `CLAP/Thunder.clap`, and `AU/Thunder.component` on macOS. Copy them to your system plugin folders; configuring with `-DTHUNDER_COPY_AFTER_BUILD=ON` copies the VST3 and AU for you.
