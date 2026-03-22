# VitaWave

A music player homebrew for the PS Vita. Plays MP3, FLAC, and OGG Vorbis files from `ux0:music/`.

## Features

- File browser with directory navigation
- Supports MP3 (libmpg123), FLAC (dr_flac), OGG Vorbis (libvorbisfile)
- Now Playing screen: album art, title, artist, progress bar, scrolling text
- 10-band parametric equalizer with presets and custom preset storage
- Spectrum visualizer (KissFFT) — bars and waveform modes
- Playlist queue with shuffle, repeat, crossfade between tracks
- Named playlists saved as M3U files
- Theme system: color palettes loaded from INI files, optional background images
- Background audio: keeps playing when screen is off or in LiveArea
- Rebuilds Vita system music database on startup
- Rounded album art corners, button icon rendering

## Building

Requires VitaSDK.

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DVITASDK=$VITASDK
make -j4
```

Install `VitaWave.vpk` via VitaShell.

## Navigation

| Button | Action |
|--------|--------|
| Cross  | Play / Pause / Confirm |
| Circle | Back |
| Triangle | Queue / open submenu |
| Square | Visualizer |
| L/R triggers | Volume / EQ adjust |
| Select | Settings |
| Start | Exit |
