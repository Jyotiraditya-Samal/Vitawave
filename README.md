# VitaWave

A music player homebrew for the PS Vita. Plays MP3, FLAC, and OGG Vorbis files from `ux0:music/`.

## Features

- File browser with directory navigation
- MP3 (libmpg123), FLAC (dr_flac), OGG Vorbis (libvorbisfile)
- Now Playing screen: album art, title, artist, progress bar, scrolling text
- 10-band parametric equalizer with presets and custom preset storage
- Spectrum visualizer (KissFFT) — bars and waveform modes
- Playlist queue with shuffle, repeat, crossfade between tracks
- Named playlists saved as M3U files
- Theme system: color palettes, background images (PNG/JPG/GIF), custom fonts
- Animated GIF support for per-screen background images
- Background audio: keeps playing when screen is off or in LiveArea
- Rebuilds Vita system music database on startup
- Rounded album art corners, custom button icon rendering
- Multi-script text rendering (Latin, Japanese, Korean, Chinese)

## Building

Requires [VitaSDK](https://vitasdk.org). All assets and fonts are included — clone and build directly.

```bash
git clone https://github.com/Jyotiraditya-Samal/Vitawave.git
cd Vitawave
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DVITASDK=$VITASDK
make -j4
```

Install `build/VitaWave.vpk` via VitaShell.

**Title ID:** `VWAV00001` — installs to `ux0:app/VWAV00001/`.

## Navigation

| Button | Action |
|--------|--------|
| Cross | Play / Pause / Confirm |
| Circle | Back |
| Triangle | Queue / open submenu |
| Square | Visualizer |
| L/R triggers | Volume / EQ adjust |
| Select | Settings |
| Start | Exit |

## Themes

Drop a folder into `ux0:data/VitaWave/themes/` containing a `theme.ini`. See [docs/creating_themes.md](docs/creating_themes.md) for the full reference.

## Fonts

Custom fonts can be swapped at build time or per-theme. See [docs/changing_fonts.md](docs/changing_fonts.md).
