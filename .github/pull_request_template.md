## What this changes

<!-- A short description of the change and why it is needed. Link the issue if there is one (e.g. Fixes #12). -->

## How it was tested

<!-- Builds, tests, pluginval, and which DAW(s) / OS you tried it in. -->

## Checklist

- [ ] Builds and all tests pass
- [ ] No memory allocation, locks, file I/O, logging or UI calls on the audio thread
- [ ] New continuous parameters are smoothed (no clicks or zipper noise)
- [ ] No existing parameter IDs renamed or removed
- [ ] Code formatted with the repository's `.clang-format`
- [ ] New dependencies (if any) are licence-compatible with AGPLv3 and listed in `THIRD_PARTY_NOTICES.md`
