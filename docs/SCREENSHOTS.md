# README screenshots

Retake them whenever the UI changes, before tagging a release. `tools\screenshots.ps1` takes all five
from two portable foobar2000 copies, so the pictures look the same every time.

| File | Shows |
|---|---|
| `main.png` | The whole Default UI window with an album playing. The **Slowed & Reverb** tab is open, preset Classic, effect on, "Reverb shape" expanded. |
| `panel.png` | The same tab, cropped from `main.png`. |
| `panel-compact.png` | The View > Slowed & Reverb popup in the compact layout. |
| `export.png` | The export dialog for the album: playback settings, WAV, empty output folder, default title suffix and file name, no ffmpeg found. |
| `columns-ui.png` | The Columns UI window with the compact panel in the toolbar, the same album playing. |

## Run

```
build.cmd x64
powershell -ExecutionPolicy Bypass -File tools\screenshots.ps1 -DefaultUi <portable copy> -ColumnsUi <portable copy> -Album <album folder>
```

Options: `-Track 2` picks the track that is playing; `-Out <folder>` writes somewhere other than `docs\`
(useful for checking the pictures first).

The script:

1. Closes the two portable copies if they are running (`foobar2000.exe /exit`). Any other foobar2000 is left alone.
2. Copies `build\x64\foo_slowed_reverb.dll` into each copy's `profile\user-components-x64\foo_slowed_reverb\`.
3. Starts each copy with ffmpeg hidden from it (ffmpeg folders removed from `PATH`, `LOCALAPPDATA` pointed into the
   profile), so the export dialog shows the WAV state and no personal path.
4. Moves the window to 40,40 at 1400×860 without taking focus, opens the album's `.flac` files (this replaces the
   playlist and starts playing), mutes the copy's audio session in the Windows mixer, and skips to `-Track`.
5. Sets the panel the way a user would: picks Classic in the preset combo box, ticks the switch, expands "Reverb shape".
6. Waits so the seek bar and visualisations have moved, then captures the windows with `PrintWindow`.
7. Opens the export dialog with `/context_command:Export... <files>`, sets it up as above, captures it and cancels it,
   so nothing is saved.

Check the pictures before committing: nothing personal visible (paths, user names), no stray paint, the preset shows
"Classic" without `*`.

## One-time setup of the portable copies

Both copies are ordinary portable installs of foobar2000 v2 (the installer's "Portable installation" option),
x64, dark mode. Install the component once through Preferences > Components so its folder exists.

**Default UI copy**

1. View > Layout > Quick Setup: Main Layout **Visualisation + Cover Art + Tabs**, Playlist Layout
   **Album art in playlist**, colors left as they are (dark).
2. View > Layout > Enable layout editing mode.
3. Right-click the tab strip (Properties | Playlists) > **Add New Tab**.
4. Right-click inside the new empty tab > Add New UI Element... > Utility > **Slowed & Reverb**.
5. Right-click the new tab's header > **Rename "New Tab" Tab** > `Slowed & Reverb`.
6. Turn layout editing mode off again.
7. Open View > Slowed & Reverb once, right-click it > **Compact layout**, close it. The popup remembers this.
8. Set the volume to the minimum (the script mutes the session too, this is a second safety).

**Columns UI copy**

1. Install Columns UI and pick it as the user interface.
2. Right-click the toolbar area > Panels > **Slowed & Reverb**, then right-click the panel > **Compact layout**.
   Keep it as the second band of the first toolbar row, right after the menu: the script widens band 1 of the
   toolbar (`RB_MAXIMIZEBAND`) so the sliders get the rest of the row.

## Notes from setting this up

- `/context_command:` takes the item name only (`Export...`). The full path `Slowed & Reverb/Export...` does not work.
- A Default UI window restored from minimized must be found again through the process' `MainWindowHandle`;
  the handle right after start can be a tiny placeholder.
- Windows context menus count disabled items and headers when moving with the arrow keys.
- Use PowerShell to start foobar2000 with `/` switches. Git Bash rewrites `/exit` into a path, and foobar2000
  then adds a bogus playlist entry.
