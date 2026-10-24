Slowed & Reverb Changelog
=========================
#### v1.0.1 (2026-10-24)
- Changed release downloads to one file with the version in its name: `foo_slowed_reverb-X.Y.Z.fb2k-component`
- Improved slowdown quality: the resampler is flat to 20 kHz and keeps filter artifacts around -120 dB (was -25 dB near 20 kHz)
- Improved the resampler with a 128-tap Kaiser-windowed filter; it uses more CPU, still far below what playback needs

#### v1.0.0 (2026-10-18)
- Added the Slowed & Reverb DSP for the playback chain: varispeed slowdown (50–100%) followed by reverb
- Added pitch that drops with tempo, like a slowed-down record
- Added reverb controls: mix, room size, damping and stereo width
- Added pre-delay (0–200 ms), low cut (off–500 Hz) and high cut (2 kHz–off) on the reverb send
- Added sliders with ticks that snap while dragging (native Windows trackbars)
- Added typed values (double-click a value), mouse wheel and keyboard control on the sliders
- Added presets Light, Classic and Dreamy, plus user presets you can save, rename, delete and revert
- Added preset import and export as `.srpreset` files; the current preset name shows `*` when changed
- Added live slider changes without restarting playback
- Added a bypass switch that keeps the DSP and its position in the chain
- Added the switch in the DSP settings, the panel and Playback > Enable Slowed & Reverb (assignable to a shortcut)
- Added a panel for the Default UI and Columns UI (also in the Columns UI toolbar area) with the switch, presets and sliders
- Added a foldable "Reverb shape" section, a one-row compact layout and a popup from View > Slowed & Reverb
- Added native Windows controls that follow dark and light mode (Columns UI uses its own setting)
- Added accent-colored sliders in dark mode: foobar2000's highlight color if changed, otherwise the Windows accent
- Added a custom slider color (right-click the panel)
- Added export from the track context menu (Slowed & Reverb > Export...) and the panel's "Export selected tracks..." button
- Added export to 32-bit float WAV (the default, nothing else needed) or MP3, FLAC, OGG and M4A through ffmpeg
- Added tags and cover art on exported files (WAV included), an editable title suffix and automatic clipping protection
- Added x86, x64 and ARM64EC builds in one `.fb2k-component`; foobar2000 for ARM uses the ARM64EC build
