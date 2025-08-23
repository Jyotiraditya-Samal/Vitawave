# VitaWave

A music player homebrew for the PS Vita.

## Features

- Browse music files on `ux0:music/`
- Supports MP3, FLAC, OGG Vorbis
- Now Playing screen with album art, title, and artist from tags
- Playback queue built from current folder
- Skip tracks with D-pad up/down
- Settings screen with volume control

## Building

Requires VitaSDK.

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
make
```

Install `VitaWave.vpk` via VitaShell.

## Status

Beta. Things may crash. Let me know if you find issues.
