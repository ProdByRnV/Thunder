# Thunder

**Drop Thunder on a melody and get an in-key, mix-ready texture layer in seconds — free.**

For Windows, macOS and Linux.

Thunder is an audio effect plugin for your DAW that turns a simple melody into a textured background layer that sits musically underneath the original. Instead of stacking a pitch shifter, granulator, reverb, EQ and volume shaper, you insert Thunder and get one layer that already works in the mix:

- **In-key harmony** — shift the layer by octaves, semitones, or by intervals that stay in your song's key.
- **Grains, space and tone** — granular texture, lush reverb, filtering and stereo width.
- **Rhythm and movement** — halftime, tempo-synced volume shapes, and random "static" movement locked to your song.
- **Mix-safe by default** — automatic level matching, a high-pass on the layer, and a safety limiter.
- **Perform** — one Storm knob that builds the whole sound, hold-to-play strikes, a Riser that peaks on the bar line, and Morph between two sounds.
- **Capture and drag** — record a few bars of the layer to WAV and drag it straight into your DAW.
- **Electric Storm UI** — an audio-reactive lightning theme (with a Reduce Motion option).

Made by **[@ProdByRnV](https://www.instagram.com/prodbyrnv/)** — follow on Instagram for new sounds and updates.

> **Status: in development.** The first release, v1.0, will be published on the [Releases](https://github.com/ProdByRnV/Thunder/releases) page.

## Formats and platforms

| Platform | Formats |
|---|---|
| Windows 10/11 (x64) | VST3, CLAP |
| macOS 11+ (Apple silicon + Intel) | VST3, AU, CLAP |
| Linux x86_64 and ARM64 (Ubuntu 22.04+ or another distribution with glibc 2.35+) | VST3, CLAP, LV2 |

## Setting up Thunder

### 1. Download

Open the [Releases](https://github.com/ProdByRnV/Thunder/releases) page and, under the newest release, download the installer for your computer:

| Your computer | Download |
|---|---|
| Windows | `Thunder-<version>-windows-x86_64-setup.exe` |
| Mac (Apple silicon or Intel) | `Thunder-<version>-macos-universal.pkg` |
| Linux (64-bit PC) | `Thunder-<version>-linux-x86_64.zip` |
| Linux (64-bit ARM) | `Thunder-<version>-linux-arm64.zip` |

### 2. Install

**Windows:** double-click the installer and follow its steps. It installs the VST3 and CLAP plugins into `C:Program FilesCommon Files`. If Windows shows "Windows protected your PC", click **More info**, then **Run anyway**.

**macOS:** double-click the package and follow its steps. It installs the AU, VST3 and CLAP plugins for all users; click **Customize** to leave a format out. If macOS says it can't open the package, open **System Settings → Privacy & Security**, scroll down and click **Open Anyway** next to the message about Thunder, then open the package again.

**Linux:** unzip the download and copy the plugins into your plugin folders (`How to install.txt` in the folder has the terminal commands):

| Copy | Into |
|---|---|
| `Thunder.vst3` (inside `VST3`) | `~/.vst3` |
| `Thunder.clap` (inside `CLAP`) | `~/.clap` |
| `Thunder.lv2` (inside `LV2`) | `~/.lv2` |

Prefer copying the plugins yourself on Windows or macOS? Each release also has a zip for every system, with `How to install.txt` inside.

### 3. Open your DAW

Rescan your plugins, then add Thunder as an effect on the track with your melody.

- **FL Studio:** Options → Manage plugins → **Find installed plugins**.
- **Logic Pro or GarageBand:** if Thunder isn't listed, restart your Mac.

Your presets and captures are saved in `Documents/Thunder`.

The [manual](docs/MANUAL.md) explains every control.

### Uninstalling

- **Windows:** Settings → Apps → **Thunder (audio plugin)** → Uninstall.
- **macOS:** delete `Thunder.component`, `Thunder.vst3` and `Thunder.clap` from `/Library/Audio/Plug-Ins/Components`, `/VST3` and `/CLAP`.
- **Linux, or a zip install:** delete the Thunder files you copied.

Uninstalling leaves your presets and captures in `Documents/Thunder`.
