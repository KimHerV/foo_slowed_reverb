# Changelog

All notable changes to this project are documented here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed
- Cleaner slowdown: the resampler now uses a longer Kaiser-windowed sinc filter (128 taps instead of 32). The response is flat to 20 kHz (was -0.5 dB at 20 kHz; the last 1 kHz below Nyquist is now rolled off) and filter artifacts stay around -120 dB (were as high as -25 dB near 20 kHz). It uses more CPU, still far below what playback needs.
- Releases carry a single `foo_slowed_reverb.fb2k-component`; the version is shown in Preferences > Components.

## [1.0.0] - 2026-10-18

First public release.

### Added
- **Slowed & Reverb DSP** for the playback chain: varispeed slowdown (50–100%, pitch drops with tempo like a slowed-down record) followed by reverb.
- Reverb controls: mix, room size, damping, **pre-delay** (0–200 ms), **low cut** (off–500 Hz) and **high cut** (2 kHz–off) on the reverb send, and **stereo width**.
- Sliders with ticks that snap while dragging (native Windows trackbars), typed values (double-click the value), mouse wheel and keyboard control.
- Presets: Light, Classic, Dreamy, plus user presets (save, rename, delete, revert, import/export `.srpreset` files). The current preset name is shown, marked `*` when changed.
- Live slider changes without restarting playback.
- **Bypass switch** that keeps the DSP and its position in the chain. Available in the DSP settings, the panel, and Playback > Enable Slowed & Reverb (assignable to a keyboard shortcut or toolbar button).
- **Panel** for the Default UI and **Columns UI** (also usable in the Columns UI toolbar area) with the switch, presets and sliders ("Reverb shape" folds away); a one-row compact layout; also available as a popup from View > Slowed & Reverb.
- Native Windows controls that follow dark/light mode (Columns UI's own dark mode setting under Columns UI). In dark mode the sliders are painted in the accent color: foobar2000's highlight color if changed, otherwise the Windows accent color; a custom color can be chosen.
- **Export** from the track context menu (Slowed & Reverb > Export...) or the panel's "Export selected tracks..." button to 32-bit float WAV (the default, no extra software needed) or MP3, FLAC, OGG, M4A (via ffmpeg), with tags and cover art copied (WAV included), an editable title suffix, and automatic clipping protection.
- x86, x64 and ARM64EC builds in one `.fb2k-component`. foobar2000 for ARM uses the native ARM64EC build.

[Unreleased]: ../../compare/v1.0.0...HEAD
[1.0.0]: ../../releases/tag/v1.0.0
