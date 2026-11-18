# Third-Party Notices

Thunder is licensed under the GNU Affero General Public License v3.0 (see [LICENSE](LICENSE)). It includes or links the third-party software listed below. Each component remains under its own licence; the full licence texts are included in each component's folder under `external/` and reproduced or referenced here.

| Component | Version | Used for | Licence | Copyright |
|---|---|---|---|---|
| [JUCE](https://github.com/juce-framework/JUCE) | 8.0.15 | Plugin framework, audio, GUI | AGPLv3 (JUCE is dual-licensed; Thunder uses it under the AGPLv3) | Raw Material Software Limited |
| VST3 SDK (bundled with JUCE) | 3.8.0 | VST3 plugin format | MIT | © 2025 Steinberg Media Technologies GmbH |
| [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) | 1.4.0 | Pitch shifting, formant handling | MIT | © 2022 Geraint Luff / Signalsmith Audio Ltd |
| [Signalsmith Linear](https://github.com/Signalsmith-Audio/linear) | 0.6.4 | FFT and vector maths used by Signalsmith Stretch | MIT | © 2025 Signalsmith Audio |
| [CloudSeedCore](https://github.com/GhostNoteAudio/CloudSeedCore) | commit `deb21de` | "Cloud" reverb algorithm | MIT | © 2024 Ghost Note Audio / Ghost Note Engineering Ltd |
| [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) | 0.26.0 | CLAP plugin format (planned) | MIT | © 2019–2020 Paul Walker |
| [CLAP](https://github.com/free-audio/clap) and [clap-helpers](https://github.com/free-audio/clap-helpers) (bundled with clap-juce-extensions) | — | CLAP plugin format (planned) | MIT | © 2021 Alexandre Bique |

## Credits

- **Cloud reverb** — Thunder's "Cloud" reverb mode uses the Cloud Seed algorithm by **Ghost Note Audio**. Thunder is an independent project; it is not created, endorsed or supported by Ghost Note Audio. Cloud Seed 2.0 is available from [ghostnoteaudio.uk](https://ghostnoteaudio.uk/products/cloudseed).
- **Pitch shifting** — Signalsmith Stretch by Geraint Luff / Signalsmith Audio.

## MIT License

The following licence text applies to each MIT-licensed component above, with that component's copyright line as listed in the table:

```
MIT License

Copyright (c) <year> <copyright holders>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## GNU Affero General Public License v3.0

JUCE is used under the AGPLv3, the same licence as Thunder. The full text is in [LICENSE](LICENSE).
