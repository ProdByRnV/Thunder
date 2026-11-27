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

Open the [Releases](https://github.com/ProdByRnV/Thunder/releases) page and, under the newest release, download the file for your computer:

| Your computer | Download |
|---|---|
| Windows | `Thunder-<version>-windows-x86_64.zip` |
| Mac (Apple silicon or Intel) | `Thunder-<version>-macos-universal.zip` |
| Linux (64-bit PC) | `Thunder-<version>-linux-x86_64.zip` |
| Linux (64-bit ARM) | `Thunder-<version>-linux-arm64.zip` |

Unzip it (on Windows: right-click → **Extract All**; on a Mac: double-click). The folder has the plugins and `How to install.txt`, the same steps as below.

### 2. Copy the plugins into your plugin folders

**Windows**

| Copy | Into |
|---|---|
| the `Thunder.vst3` folder (inside `VST3`) | `C:\Program Files\Common Files\VST3` |
| `Thunder.clap` (inside `CLAP`) | `C:\Program Files\Common Files\CLAP` |

Create the CLAP folder if it isn't there. Windows asks for administrator permission to copy into these folders.

**macOS**

In Finder, choose **Go → Go to Folder…** and enter each folder below. Create a folder if it isn't there.

| Copy | Into |
|---|---|
| `Thunder.component` (AU, for Logic Pro and GarageBand) | `~/Library/Audio/Plug-Ins/Components` |
| `Thunder.vst3` | `~/Library/Audio/Plug-Ins/VST3` |
| `Thunder.clap` | `~/Library/Audio/Plug-Ins/CLAP` |

Then allow Thunder to open, because macOS holds back plugins downloaded from the internet. Open **Terminal** (Applications → Utilities), paste this line and press Return:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Thunder.component ~/Library/Audio/Plug-Ins/VST3/Thunder.vst3 ~/Library/Audio/Plug-Ins/CLAP/Thunder.clap
```

It changes nothing but these three plugins. If your DAW still says Thunder can't be opened, open **System Settings → Privacy & Security**, scroll down and click **Open Anyway** next to the message about Thunder.

**Linux**

| Copy | Into |
|---|---|
| `Thunder.vst3` (inside `VST3`) | `~/.vst3` |
| `Thunder.clap` (inside `CLAP`) | `~/.clap` |
| `Thunder.lv2` (inside `LV2`) | `~/.lv2` |

### 3. Open your DAW

Rescan your plugins, then add Thunder as an effect on the track with your melody.

- **FL Studio:** Options → Manage plugins → **Find installed plugins**.
- **Logic Pro or GarageBand:** if Thunder isn't listed, restart your Mac.

Your presets and captures are saved in `Documents/Thunder`.

### Uninstalling

Delete the Thunder files you copied from your plugin folders.
