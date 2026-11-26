# Thunder manual

Thunder turns a melody into a textured layer that sits underneath it: shifted in pitch, broken into grains, washed in reverb, filtered and given movement, all in time and in key with your song. You can play the layer live, capture a few bars of it to a WAV file, and drag that file into your arrangement.

This manual goes through Thunder from left to right and top to bottom. For installing, see the [README](../README.md).

Thunder is made by [@ProdByRnV](https://www.instagram.com/prodbyrnv/). Follow on Instagram for new sounds and updates.

- [Getting started](#getting-started)
- [The window](#the-window)
- [Key and scale](#key-and-scale)
- [The modules](#the-modules)
- [Output](#output)
- [Perform](#perform)
- [Morph](#morph)
- [Presets](#presets)
- [Capture and drag](#capture-and-drag)
- [Source files and input capture](#source-files-and-input-capture)
- [Visualiser and meters](#visualiser-and-meters)
- [Settings](#settings)
- [Tips](#tips)
- [Troubleshooting](#troubleshooting)
- [Reporting a problem](#reporting-a-problem)

## Getting started

1. Put Thunder as an effect on the track that plays your melody. A single-note melody works best: a lead, a pluck, a vocal line, a piano phrase.
2. Set **Key** and **Scale** in the top bar to your song's key, or let Thunder listen and suggest one (see [Key and scale](#key-and-scale)).
3. Press play. At the default settings you hear your melody with a layer an octave above it: grainy, wide and reverberant, kept about 6 dB under the melody.
4. Click a module in the chain to shape the layer, or step through the presets with ◄ and ►.
5. When you like what you hear, capture 4 bars and drag them into your arrangement.

Thunder delays its output by about 82 ms (Standard quality) and tells your DAW, which lines everything up again. You don't need to do anything about it.

## The window

From top to bottom:

- **Top bar:** the Thunder logo (opens About), the preset bar (name, ◄ ►, ☆, ⋯), Morph, the key controls with the key badge, the Settings gear and ⓘ About.
- **Chain:** the modules in the order they process the sound: Pitch, Time, Grains, Static, Space, Tone, Width, Shape and Duck. Click a module's name to show its controls; click its dot to switch it on or off.
- **Module panel:** the controls of the selected module, with a live display of what it's doing. ↺ next to the switch resets that module's controls.
- **Output panel:** Output Mode, Mix, Layer, In, Out, Auto Gain, the safety limiter and the meters. Always in view.
- **Visualiser** with the **Perform** panel beside it.
- **Capture bar** at the bottom, with the status line: the latency, the version and @ProdByRnV.

**Using the controls**

- Drag a knob up or down. Hold Shift for fine adjustments. The mouse wheel nudges it; a double-click resets it.
- Click a knob's value to type an exact one.
- Right-click a knob to reset it, enter a value, or copy and paste its value.
- Hover over any control to see what it does, its value and its range.
- Resize the window from its bottom-right corner (75% to 200%, in 5% steps) or in Settings. Each project remembers its size.

Every control can be automated in your DAW and is saved with your project. Moving or automating a control never clicks, and switching modules fades them in and out.

## Key and scale

**Key Root** (C to B) and **Scale** tell Thunder your song's key. Interval mode, Scale Lock on the grains and capture file names use it.

Scales: Major, Natural Minor, Harmonic Minor, Dorian, Phrygian, Mixolydian, Major Pentatonic, Minor Pentatonic and Chromatic. The default is C Natural Minor.

**Key detection.** The badge next to the key controls shows the key Thunder hears in your melody, and how sure it is: cyan when confident, amber when unsure. Click it to **Use** that key or **Keep** yours. Thunder never changes your key by itself. Click the key controls and choose **Detect the key of the input** to turn listening on or off for the project.

Key detection names major and natural minor keys. A modal melody (Dorian, for example) shows as its relative major or minor, so check the suggestion against what you know about your song.

## The modules

The layer passes through the modules in chain order. Each has its own on/off switch. Defaults are shown in brackets.

### Pitch

Shifts the layer's pitch. [On]

| Control | What it does | Range [default] |
|---|---|---|
| Mode | **Octave** shifts by whole octaves. **Interval** adds an in-key interval to each note. **Semitone** shifts by a fixed amount. | [Octave] |
| Octave | How many octaves the layer moves. | −2, −1, +1, +2 [+1] |
| Interval | The in-key interval added above or below each note. | Unison, 2nd … 7th, Octave [3rd] |
| Direction | Whether the interval goes up or down. | Up, Down [Up] |
| Semitones | A fixed shift. | −24 to +24 [0] |
| Fine | Fine tuning. | ±100 cents [0] |
| Preserve Formants | Keeps voices and acoustic sounds natural when the pitch goes up (less "chipmunk"). | [On] |
| Pitch Mix | How much of the shifted sound the layer uses; the rest is unshifted. | 0–100% [100%] |

**Interval mode** follows a single-note melody note by note, giving each note the interval that stays in your key: a 3rd above C in C minor is E♭, a 3rd above D is F. Chords get the one fixed shift that keeps the most notes in key. Each new note takes a moment (10–30 ms) to get its own interval.

Shift and mode changes glide, so you can switch them while playing.

### Time

Halftime in step with your song. [Off]

| Control | What it does | Range [default] |
|---|---|---|
| Halftime | Plays the first half (1/2) or first quarter (1/4) of each bar across the whole bar, at the same pitch. | 1/2, 1/4 [1/2] |
| Smooth | Softens the jump at each bar line. | 0–100% [30%] |

Time adds no latency and follows your song's bars.

### Grains

Short, smoothly faded slices of the last few seconds of the layer, played back as a cloud. [On]

| Control | What it does | Range [default] |
|---|---|---|
| Amount | How much of the layer is made of grains. | 0–100% [50%] |
| Size | The length of each grain. | 10–500 ms [80 ms] |
| Density | How many grains start each second. | 1–100 per second [20] |
| Position Spread | How far back in time grains can reach (up to 4 s). | 0–100% [25%] |
| Pitch Spread | How far each grain's pitch can wander. | 0–12 semitones [0] |
| Pan Spread | How widely grains spread across the stereo field. | 0–100% [40%] |
| Reverse | The chance that a grain plays backwards. | 0–100% [0%] |
| Scale Lock | Keeps grain pitches to intervals that stay in your key: fourths, fifths and octaves. | [On] |
| Tempo Sync | Starts grains in time with your song. | [Off] |
| Freeze | Holds the grains on the sound they have now, so it sustains. | [Off] |

The cloud keeps a steady level however dense or spread out it is. The Grains panel shows the grains playing now: further right reads further back, higher is pitched up, bigger is longer.

### Static

Random movement that makes the layer feel alive. [Off]

| Control | What it does | Range [default] |
|---|---|---|
| Amount | How far Static moves the controls it reaches. | 0–100% [30%] |
| Rate | How fast it moves. | 0.05–20 Hz [0.5 Hz] |
| Sync | Moves in note values of your song's tempo instead. | [Off] |
| Synced Rate | How fast it moves with Sync on. | 4 bars to 1/16 triplet [1 bar] |
| Character | **Smooth** glides, **Stepped** jumps from value to value, **Spark** fires sudden bursts that fade. | [Smooth] |
| Targets | What Static moves: **Position** (how far back the grains reach), **Pitch** (the grains' pitch spread), **Filter**, **Pan** and **Level**. | [Position, Filter, Pan] |

The movement follows your song's position, so a loop moves the same way every time and an export sounds like playback. The Static panel draws the last two seconds of movement, one line per target. Static's Amount also sets how readily the lightning strikes.

### Space

Reverb on the layer. [On]

| Control | What it does | Range [default] |
|---|---|---|
| Mode | **Cloud** (lush and diffuse), **Plate** (bright), **Room** (small) or **Shimmer** (rises in octaves). | [Cloud] |
| Mix | How much reverb the layer gets. | 0–100% [40%] |
| Decay | How long the reverb rings, shown in seconds. Each mode has its own range: Cloud 0.4–20 s, Plate 0.5–8 s, Room 0.2–3 s, Shimmer 1–20 s. | [50%] |
| Pre-delay | The gap before the reverb starts. | 0–250 ms [20 ms] |
| Damping | Darkens the reverb's tail. | 0–100% [50%] |

Switching modes lets the old reverb ring out under the new one.

### Tone

Filters on the layer. [On]

| Control | What it does | Range [default] |
|---|---|---|
| High-pass | Removes the layer's low end below this frequency, so it never muddies your bass. | 20–2000 Hz [200 Hz] |
| HP Slope | How steeply the high-pass cuts. | 12 or 24 dB/oct [24] |
| Low-pass | Removes the layer's high end above this frequency. | 1–20 kHz [16 kHz] |
| LP Slope | How steeply the low-pass cuts. | 12 or 24 dB/oct [12] |

The Tone panel shows the filter curve; **Open in Spectrum** shows it over the live spectrum, where you can drag the filters' handles.

### Width

Stereo width of the layer. [On]

| Control | What it does | Range [default] |
|---|---|---|
| Width | 0% is mono, 100% unchanged, 200% extra wide. Mono playback sounds the same at every width. | 0–200% [100%] |
| Autopan | Moves the layer from side to side in time with your song. | [Off] |
| Autopan Rate | How fast, in note values. | 4 bars to 1/16 triplet [1/4] |
| Autopan Depth | How far it moves. | 0–100% [50%] |

The Width panel shows how the output sums to mono.

### Shape

A rhythmic volume shape on the layer, locked to your song's bars and beats. [Off]

| Control | What it does | Range [default] |
|---|---|---|
| Shape | Pump, Swell, Gate, Ramp Up, Ramp Down or Chop. | [Pump] |
| Rate | How long one shape lasts. | 2 bars to 1/16 triplet, dotted and triplet values included [1/4] |
| Depth | How deep the shape cuts into the layer's volume. | 0–100% [60%] |
| Smooth | Softens the shape's edges. | 0–100% [20%] |

The Shape panel draws one cycle with a playhead at your song's position. Shape keeps time while the transport is stopped.

### Duck

Turns the layer down while your melody plays, so the layer stays behind it. [Off]

| Control | What it does | Range [default] |
|---|---|---|
| Amount | How much quieter the layer gets while your melody plays. | 0–100% [40%] |
| Release | How quickly the layer comes back up. | 10–1000 ms [150 ms] |
| Mode | **Envelope** follows your melody's level. | [Envelope] |

Duck works in Layer Only too: it still listens to your melody. A meter shows how far it is turning the layer down.

## Output

| Control | What it does | Range [default] |
|---|---|---|
| Output Mode | **Blend** plays your melody with the layer; **Layer Only** plays only the layer (for a separate track or a capture). | [Blend] |
| Mix | The balance between melody and layer, in Blend. At 50% both play at full level. | 0–100% [50%] |
| In | The level going into Thunder. | −24 to +24 dB [0] |
| Layer | The layer's level before it is mixed. | −36 to +12 dB [0] |
| Out | The level coming out of Thunder. | −24 to +24 dB [0] |
| Auto Gain | Keeps the layer about 6 dB under your melody, whatever the other settings. | [On] |
| Safety Limiter | Keeps the output from going over −0.3 dBFS. | [On] |

Bypassing Thunder in your DAW fades smoothly and keeps the track in time.

## Perform

The Perform panel, beside the visualiser, is for playing Thunder live and for automation.

- **Storm** (0–100%) builds the whole sound with one knob: denser, wider grains, a bigger reverb, a brighter and slightly louder layer and more Static. At 0% the sound is exactly your settings.
- **FREEZE**, **CLAP** and **SCATTER** act while you hold them. Freeze holds the grains; Thunderclap dives the layer an octave as the reverb swells; Scatter throws the grains as wide as they go. Each glides in and out. Shift-click keeps one on until your next click. Each is a switch your DAW can automate.
- **Riser:** choose 1, 2, 4 or 8 bars and press **RISE**. Storm climbs to 100%, peaks exactly on a bar line, then drops back. The rise counts from the bar line nearest your press, so pressing just before or just after it both work.
- **Variation** (0–100%) changes the grains a little on every bar. The same bar always changes the same way, so loops repeat and an export sounds like playback.

Knobs show where Storm, the strikes, a rise and Variation take them as a thin second ring.

## Morph

Morph (next to the presets) moves the sound from your settings (A) towards a second sound (B).

1. Right-click the Morph slider and choose **Set B = current sound** or **Load B from preset**. (**Clear B** removes it.)
2. Move the slider. Knobs glide in their own way; switches and modes change halfway. Key, Scale, Output Mode and the safety limiter stay as they are.

Each knob shows where Morph has moved it as a thin second ring. Sound B is saved with your project, Morph can be automated, and both ends sound exactly like A and B.

## Presets

- **◄ ►** step through the presets (in the order and filter of the browser). A `*` after the name means you've changed the sound.
- **Click the name** to open the browser: search by name, author, category, bank or tag; filter by category, bank, tag or favourites; sort by any column. Click a preset to hear it, press Enter or double-click to keep it, or Esc to go back to the sound you had. The arrow keys move through the list without loading.
- **☆** marks a preset as a favourite.
- The **save** button saves the sound as a preset; **⋯** has Init, Save as…, Import…, Export… and Open preset folder.

Thunder comes with Init (every control at its default) and 56 presets in seven categories: Atmos, Harmony, Grit, Shimmer, Rhythmic, Lo-Fi and Experimental. They also appear in your DAW's own preset menu for Thunder, grouped by category.

**Your presets** are saved with a name, author, category, bank (a sub-folder), tags and description in `Documents/Thunder/Presets`. Thunder asks before replacing one. In the browser you can rename and delete them.

**Sharing presets:** Export… saves the current sound as a `.thunderpreset` file anywhere. Import… adds presets someone sent you, without ever replacing one of yours: a different preset with the same name is saved as "Name (2)". You can also copy preset files into the preset folder and press **Rescan** in the browser.

Loading a preset keeps your Key, Scale and Output Mode (you can change this in Settings) and switches smoothly while playing. Your project remembers which preset it was on.

## Capture and drag

The capture bar records the layer to a WAV file that you drag straight into your DAW.

1. Choose the length: 1, 2, 4, 8 or 16 bars.
2. Choose what to record: **Layer** (the layer as Layer Only plays it), **Full Mix** (Thunder's whole output) or **Input** (Thunder's input; see below).
3. Arm the capture and press play. Recording starts at the next bar line, and the file starts exactly on that bar.
4. When it's done, drag the capture chip onto a track in your DAW, at the bar where you want it.

If dragging doesn't work in your DAW, right-click the chip: **Save as…** and **Show in folder** give you the file. **Delete** removes it.

**Options in the length menu**

- **Include tail** keeps recording after the last bar until the reverb and grains have died away (up to 10 s). Stopping playback doesn't cut the tail short; pressing Stop ends it and keeps the capture.
- **Loop** folds that tail back onto the start, so the file is exactly the bars you chose and loops seamlessly. If your DAW looped and played the melody again, the start isn't doubled.

**Good to know**

- A loop that wraps around on a bar line keeps recording. If playback stops or jumps elsewhere, the capture is cancelled and no file is left behind.
- A capture armed while your DAW exports the song records during the export.
- Files are 24-bit or 32-bit float (Settings) at your project's sample rate, saved in `Documents/Thunder/Captures` or a folder you choose, with names like `Thunder_A-min_140bpm_4bars_20261121-143005.wav`.
- Thunder on several tracks can capture at the same time; each capture gets its own file.
- **+N** next to the chip opens the capture tray: your last 8 captures in this project, newest first. Drag any of them like a new one, or right-click for Show in folder and Remove from tray.

## Source files and input capture

A **source** is a melody Thunder keeps for reference: it appears across the top of the visualiser with its waveform, bar lines and key, and the key badge shows its key.

- **Drop a file** (WAV, AIFF, FLAC, MP3 or OGG, up to 5 minutes) anywhere on Thunder, from your DAW's browser or your file manager.
- **Capture your input** with the capture bar set to Input: the file starts exactly on the bar and becomes the source.
- **✕** removes the source.

Your project remembers the source. Sources up to about half a minute are also saved inside the project, so they come back even if the file moves. If a file can't be found, Thunder says so and offers **Locate…**.

## Visualiser and meters

- **WAVE** shows your melody (dim) and the layer (glowing) scrolling in time with your song, with bar numbers and beat lines. Grains appear as sparks: further left reads further back, higher is pitched up, violet is panned out.
- **SPECTRUM** shows the layer and your melody from 20 Hz to 20 kHz with the Tone filter curve. Drag its handles to set the high-pass and low-pass.
- Switch with WAVE | SPECTRUM in the corner. Right-click for 1, 2 or 4 bars, Freeze, Show dry and Show grains.
- **Lightning** strikes on the layer's stronger attacks, brighter the louder the output. Static's Amount sets how readily it strikes. Never more than three flashes a second, and only the visualiser lights up. **Reduce motion** in Settings turns it off.

The **meters** in the Output panel show each channel's level with peak hold and clip lights (click to clear), the short-term loudness in LUFS, a correlation line that turns amber when parts of the sound would cancel in mono, and a ⚠ while the safety limiter is working.

## Settings

The gear in the top bar.

| Setting | What it does |
|---|---|
| UI scale | The window size, 75% to 200%. |
| Visualiser | The view new projects open with: Wave or Spectrum. |
| Quality | This project's sound quality. **Eco** uses the least CPU; **Standard** is the default; **High** has the steadiest pitch on low notes but adds 40 ms of latency. |
| Reduce motion | No lightning, flashes or other animation; the waveform, spectrum and meters keep moving. On Windows it starts out following the system's animation setting. |
| Frame rate | How often the visualiser and animations update: 30 or 60 times a second (Eco always uses 30). |
| Tooltips | Show or hide the descriptions when hovering over controls. |
| Capture format and folder | 24-bit or 32-bit float, and where captures are saved. |
| Keep Key, Scale and Output Mode | Whether loading a preset keeps them. |

**About** (ⓘ or the logo) shows the version, the plugin format, your system and DAW, who made Thunder (click **Made by @ProdByRnV** to open Instagram), and a link for reporting an issue. **Copy details** copies them for your report.

## Tips

- **Start from a preset** in the category closest to what you want, then adjust one module at a time.
- **Layer Only on a separate track:** put Thunder on a copy of the melody track (or a send) in Layer Only, so the layer gets its own fader and effects.
- **Bounce, don't hold:** capture the layer you like and drag it in. The file stays in time and frees the CPU.
- **Movement that repeats:** Static, Variation, Shape and Time all follow your song's position, so a loop plays back the same each time.
- **CPU:** if your computer struggles, set Quality to Eco in Settings, switch off modules you don't need, or capture the layer and disable Thunder.

## Troubleshooting

**My DAW doesn't list Thunder.** Rescan your plugins. Check that the VST3, CLAP or AU file is in the folder your DAW scans (see the README). On a Mac, restart if Logic Pro or GarageBand still doesn't list it.

**macOS says Thunder can't be opened.** Open System Settings → Privacy & Security, scroll down and click Open Anyway next to the message about Thunder. If you installed from the zip, run the Terminal command in its `How to install.txt`.

**The layer is out of key.** Check Key and Scale in the top bar, or use the key badge's suggestion. Interval mode needs a single-note melody to follow each note.

**I don't hear the layer.** Check that Mix isn't at 0% and Layer isn't turned right down, and that at least Pitch or Grains is on. Duck turns the layer down while your melody plays.

**The capture didn't start.** Captures start on the next bar line while your DAW is playing. A capture is cancelled if playback stops or jumps before it finishes.

**Dragging the capture doesn't work.** Right-click the chip and use Save as… or Show in folder, then drag the file in from there.

## Reporting a problem

Open **About** in Thunder and click **Copy details**, then open a new issue at [github.com/ProdByRnV/Thunder/issues](https://github.com/ProdByRnV/Thunder/issues) and paste them in. Say what you did, what you expected and what happened.
