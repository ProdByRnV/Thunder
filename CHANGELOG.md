# Changelog

All notable changes to Thunder are listed here. Versions follow [Semantic Versioning](https://semver.org).
Each release's notes on GitHub are taken from its section below.

## Unreleased

Thunder is in early development and has no public release yet.

- Builds as VST3 and a standalone app on Windows, macOS and Linux, plus AU on macOS.
- Continuous integration on Windows, macOS (universal), Linux x86_64 and Linux ARM64, with plugin validation and sanitizer checks.
- The full v0.5 parameter set, every one automatable, smoothed where continuous, and saved with your session (with a versioned state format so older sessions keep loading).
- A temporary generic editor that lists every parameter. Thunder's own interface replaces it in v0.5.
- Input and output gain are active. The rest of the parameters are wired up as their sound modules arrive.
