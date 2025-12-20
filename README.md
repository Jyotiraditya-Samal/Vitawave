# VitaWave

A music player homebrew for the PS Vita.

## Features

- Browse music on `ux0:music/`, supports MP3, FLAC, OGG Vorbis
- Album art from ID3 tags or `cover.jpg` in folder
- 10-band parametric equalizer with presets
- Spectrum visualizer (FFT)
- Playlist queue, auto-advance, skip with D-pad
- Named playlists saved as M3U files
- Theme support: load color palette from INI file
- Settings screen with volume control

## Navigation

| Button | Action |
|--------|--------|
| Cross  | Play / Pause |
| Circle | Back |
| Triangle | Open queue |
| Square | Open visualizer |
| L/R triggers | Volume |
| Select | Settings |

## Building

Requires VitaSDK.

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
make
```

Install `VitaWave.vpk` via VitaShell.

## Status

Beta. Themes, EQ, and visualizer are mostly working.
