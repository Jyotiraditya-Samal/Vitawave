# Creating a VitaWave Theme

VitaWave supports custom themes — called skins — that can change every visual aspect of the app: colours, fonts, and background images. Themes live entirely on the Vita's memory card and require no recompilation.

---

## Quick Overview

A theme is a folder placed at:

```
ux0:data/VitaWave/themes/<your-theme-name>/
```

The folder must contain one file: `theme.ini`. Everything else (fonts, background images) is optional. Any value you leave out falls back to the built-in **Classic Dark** defaults.

---

## Step 1 — Create the folder

Using VitaShell (or a FTP client like FileZilla connected to the Vita), navigate to:

```
ux0:data/VitaWave/themes/
```

Create a new folder. The folder name is used internally as a fallback identifier, but the display name shown in Settings comes from `theme.ini`. Use something short with no spaces, e.g. `cyberpunk` or `ocean_blue`.

> The `ux0:data/VitaWave/themes/` directory is created automatically the first time VitaWave runs.

---

## Step 2 — Create theme.ini

Inside your theme folder, create a plain text file called `theme.ini`. This is the only required file.

A minimal `theme.ini` that just sets a name:

```ini
[info]
name = My Theme
```

A full `theme.ini` with every option:

```ini
[info]
name = Cyberpunk

[colors]
bg        = 0D0221
accent    = 1A0438
highlight = 2D0B5A
text      = F0F0FF
text_dim  = A080C0
progress  = FF0066
vis_bar   = 00FFCC
album_art_bg = 1A0438

[sizes]
font_large  = 32
font_medium = 23
font_small  = 19

[layout]
album_art_size  = 200
corner_radius   = 12
list_row_height = 30
```

Everything under `[colors]`, `[sizes]`, and `[layout]` is optional. Missing keys keep their Classic Dark default values.

---

## Color Reference

Colors are written as 6-digit hex — the same format as CSS, **without** the `#`.

```
RRGGBB
```

| Key | What it affects | Classic Dark default |
|-----|----------------|----------------------|
| `bg` | Main background fill | `1C1C1E` |
| `accent` | Cards, elevated surfaces, the mini-player bar | `2C2C2E` |
| `highlight` | Selected row in lists | `3A3A3C` |
| `text` | Primary text (track title, headings) | `FFFFFF` |
| `text_dim` | Secondary text (artist, timestamps, hints) | `8E8E93` |
| `progress` | Progress bar fill, play/pause icon, accent icons | `FC3C44` |
| `vis_bar` | Visualizer bars | `FC3C44` |
| `album_art_bg` | Placeholder shown when a track has no embedded art | `2C2C2E` |
| `vis_overlay` | Semi-transparent bar behind track title/artist on the Visualizer screen. The alpha channel is part of the value — see note below. | `CC000000` (80% opaque black) |
| `album_art_tint` | Flat color wash drawn over album art on Now Playing. Use a low alpha value to tint without fully covering the art. Same 8-digit `AARRGGBB` format as `vis_overlay`. `00000000` = disabled. | `00000000` (off) |

**Tips:**
- Pick `bg` first — it sets the overall mood.
- Keep enough contrast between `bg` and `text` so text is readable.
- `progress` is the most prominent accent colour; it appears on the progress bar, the play state icon, and the repeat/shuffle indicators.
- `highlight` should be visibly different from `bg` but not jarring — it's the row selection tint in every list screen.
- `vis_overlay` is the only color that uses transparency. All other color keys are treated as fully opaque (alpha is forced to `FF`). For `vis_overlay`, write the full 8-digit `AARRGGBB` value — the first two digits are the alpha (e.g. `CC` = 80%, `80` = 50%, `FF` = fully opaque). Example: `vis_overlay = CC001400` for a dark phosphor-green tint at 80% opacity.

---

## Font Sizes Reference

Sizes are in pixels.

| Key | What it affects | Default |
|-----|----------------|---------|
| `font_large` | Track title on Now Playing, screen headings | `32` |
| `font_medium` | List items, artist name, UI labels | `23` |
| `font_small` | Timestamps, badge labels, footer hints | `19` |

Going below `~14` makes text hard to read on the Vita's 5" screen. Going above `~40` for `font_large` may cause titles to overflow their container.

---

## Layout Reference

| Key | What it affects | Default |
|-----|----------------|---------|
| `album_art_size` | Width and height of the album art square on Now Playing (px) | `200` |
| `corner_radius` | Rounded corner radius on album art (px). Set to `0` for sharp corners. | `12` |
| `list_row_height` | Height of each row in list views (browser, playlist, settings) (px) | `30` |
| `album_art_frame_padding` | How many pixels `album_art_frame.png` extends beyond the art edge on each side. `0` = frame sits exactly over the art border. Increase to make the frame protrude outward. | `0` |

---

## Step 3 — Add background images (optional)

Place image files in your theme folder. They are drawn stretched to fill the full 960×544 screen behind the UI.

For static images the app checks each extension in this order: `.png`, `.jpg`, `.jpeg`. The Now Playing screen also supports **animated GIF** backgrounds — `bg_now_playing.gif` is tried before the static formats.

| Base name | Screen it applies to | Animated GIF? |
|-----------|----------------------|---------------|
| `bg_library` | Library / file browser | No |
| `bg_browser` | File browser (same screen as library) | No |
| `bg_now_playing` | Now Playing | **Yes** — `bg_now_playing.gif` |
| `bg_visualizer` | Visualizer (falls back to `bg_now_playing` if absent) | No |
| `bg_playlist` | Queue, Playlist List, Playlist Detail | No |
| `bg_settings` | Settings | No |
| `bg_default` | Any screen that does not have its own file | No |

So `bg_now_playing.gif`, `bg_now_playing.png`, `bg_now_playing.jpg`, and `bg_now_playing.jpeg` are all valid — GIF takes priority if present.

You do not need all of them. A single `bg_default.png` (or `.jpg`) applies everywhere. Screens without a matching file — and without a `bg_default` — show no background image, just the solid `bg` colour.

### Animated GIF notes

- GIF is supported **only** for `bg_now_playing`. Other screens use static images only.
- The GIF loops continuously while the Now Playing screen is visible.
- Keep GIF dimensions at 960×544 and frame count reasonable — large GIFs consume more memory and may affect performance.
- If both `bg_now_playing.gif` and `bg_now_playing.png` exist, the GIF is used.

---

### Exact image specifications

**Dimensions**
- **Recommended:** 960×544 px — matches the Vita screen exactly, no scaling needed.
- Other sizes are accepted and scaled to fill the screen, but may look blurry.
- Maximum: 4096×4096 px (GPU hardware limit). Going above this will cause the image to fail to load silently.

**PNG**
- Bit depth: 8-bit per channel (24-bit RGB or 32-bit RGBA). 16-bit PNG is not supported and will load incorrectly.
- Alpha channel: supported. Transparent or semi-transparent areas blend over the solid `bg` colour. Useful for overlays or vignettes.
- Color space: sRGB. No ICC profile processing is applied.
- Interlaced PNGs are supported.
- File size: no hard limit, but the uncompressed texture always occupies `width × height × 4` bytes of GPU memory (about 2 MB for 960×544). Smaller PNG files load faster but decompress to the same GPU footprint.

**JPEG**
- Standard baseline JPEG. Progressive JPEG is also supported.
- No alpha channel — JPEG does not support transparency.
- Color space: YCbCr (standard JPEG). Loaded as RGB.
- Use JPEG for photographic backgrounds where file size matters. Use PNG when you need transparency or sharp edges.
- Quality 80–90 is a good balance between file size and visual quality at 960×544.

**Performance note:** Background textures are loaded into GPU memory at theme-switch time, not at draw time. Switching to a theme with several large backgrounds may take a moment. At 960×544, each image uses about 2 MB of GPU memory regardless of its compressed file size.

---

## Step 4 — Add custom thumb graphics (optional)

You can replace the default circular thumb indicators on the progress bar and volume bar with any image.

Place PNG files with an alpha channel in your theme folder using these exact filenames:

| Filename | Displayed size | Used on |
|----------|---------------|---------|
| `thumb_progress.png` | 16×16 px | Progress bar scrub handle on Now Playing |
| `thumb_volume.png` | 12×12 px | Volume bar handle on Now Playing |

The images can be any resolution — they are scaled to the display size at draw time. PNG with transparency (RGBA) is required; JPEG is not supported for thumbs.

If either file is absent, the default filled circle is used for that thumb.

**Example:** A 32×32 diamond shape exported as a transparent PNG with a glow effect will be scaled down to 16×16 when drawn. Design at 2× or 4× resolution for sharper results.

---

## Step 5 — Add a cover art overlay (optional)

You can place a graphic over every track's album art — a texture PNG drawn stretched to fill the art square on the Now Playing screen.

Place the file in your theme folder:

| Filename | Purpose |
|----------|---------|
| `album_art_overlay.png` | Drawn over album art on Now Playing |

**Specifications:**
- **Format:** PNG with alpha (RGBA, 8-bit per channel). JPEG is not accepted — transparency is required.
- **Dimensions:** Any size; scaled to fill the art square at draw time. Design as a 1:1 square or it will stretch unevenly.
- **Recommended resolution:** 200×200 px (matches default art size) or 512×512 px for sharper results when using larger `album_art_size` values.
- **Alpha:** Transparent pixels show the art underneath. Opaque pixels cover it. Semi-transparent pixels blend over it.
- **GPU memory:** ~1 MB for a 512×512 image, regardless of the compressed file size.

The overlay is drawn after the art but before the rounded corners are applied, so it is correctly clipped to the corner radius.

**Combined with `album_art_tint`:** You can use both at the same time. The tint is a flat color wash applied first; the overlay texture is composited on top of that.

**Limitations:** The overlay can only add pixels on top of the art. It cannot transform the art itself — desaturation, hue shifts, blur, and contrast adjustments are not possible since vita2d does not expose a shader/filter pipeline.

**Typical uses:**

| Effect | How to make it |
|--------|---------------|
| Scanlines | Alternating transparent / 30%-black 1px horizontal lines |
| CRT mesh | Small repeating dot or grid pattern at ~20% opacity |
| Vignette | Radial gradient, transparent centre, dark edges |
| Film grain | Noise texture at 10–20% opacity |
| Blood splatter | Splatter graphic on a transparent background |
| Scratches / burns | Any graphic effect on a transparent background |

---

## Step 6 — Add a cover art frame (optional)

You can place a decorative frame around or over the album art square.

Place the file in your theme folder:

| Filename | Purpose |
|----------|---------|
| `album_art_frame.png` | Drawn on top of album art (and its overlay) on Now Playing |

**How it works:**
- The frame PNG is scaled to `(album_art_size + 2 × album_art_frame_padding)` and drawn centered on the art, after all other art effects.
- With `album_art_frame_padding = 0` (default), the frame covers exactly the art bounds — design the PNG with a transparent centre to show the art through the middle and a border around the edges.
- With a positive `album_art_frame_padding` (set in `[layout]`), the frame extends that many pixels beyond each edge, creating a proper raised border effect.

**Specifications:**
- **Format:** PNG with alpha (RGBA, 8-bit). JPEG is not accepted.
- **Dimensions:** Any size; scaled at draw time. Design as a square for best results.
- **Recommended resolution:** Match `album_art_size` plus twice the padding — e.g. 216×216 for a 200×200 art with 8px padding.
- The centre of the frame PNG is typically fully transparent so the art shows through. The border area contains the decorative graphic.

**To remove the frame:** simply do not include `album_art_frame.png`. No file means no frame.

**Draw order on Now Playing:**
1. Album art background fill
2. Album art texture
3. `album_art_tint` color wash
4. `album_art_overlay.png`
5. Rounded corner overdraw
6. **`album_art_frame.png`** ← drawn last, on top of everything

Because the frame is drawn after the corner overdraw, it can visually override the corners — useful for frames that have their own shaped edges.

---

## Step 8 — Add custom fonts (optional)

Place TTF font files in your theme folder using these exact filenames:

| Filename | Replaces |
|----------|----------|
| `font.ttf` | The primary Latin / Cyrillic / Greek font (default: Noto Sans) |
| `font_jp.ttf` | Japanese fallback (Hiragana, Katakana, Kanji) |
| `font_kr.ttf` | Korean fallback (Hangul) |
| `font_sc.ttf` | Chinese Simplified fallback |

You only need to supply the fonts you want to change. Any slot you leave empty keeps the bundled Noto Sans font for that script.

**Supported formats:** TTF (TrueType) is recommended. OTF with TrueType outlines (`glyf` table) also works. OTF with CFF outlines requires `libbz2`, which is not available in all builds — convert to TTF using fontTools if unsure (`python3 -m fonttools ttLib font.otf -o font.ttf`). See `docs/changing_fonts.md` for full details.

**File size:** No hard limit. The bundled CJK Noto fonts are 15–20 MB each — custom fonts of similar scope are fine. Fonts are loaded into system RAM (not GPU memory), so large fonts affect startup time more than rendering speed.

**Example:** To use Inter as the Latin font while keeping CJK support, drop `Inter-Regular.ttf` into your theme folder and rename it `font.ttf`. No INI entry is needed; the file is discovered automatically.

---

## Step 9 — Load the theme on the Vita

1. Transfer your finished theme folder to `ux0:data/VitaWave/themes/` using VitaShell or FTP.
2. Launch VitaWave (or if it is already running, close and reopen it so the new theme is discovered).
3. Go to **Settings → Theme** and press **X** to cycle through available themes until yours appears.
4. The theme applies immediately. Your choice is saved automatically and restored on the next launch.

---

## Example folder layout

```
ux0:data/VitaWave/themes/
└── cyberpunk/
    ├── theme.ini
    ├── bg_now_playing.gif    ← animated GIF for Now Playing (takes priority over JPG/PNG)
    ├── bg_visualizer.png     ← separate background for the visualizer screen
    ├── bg_default.png        ← PNG for all other screens (supports transparency)
    ├── thumb_progress.png    ← custom progress bar handle (RGBA PNG)
    ├── thumb_volume.png      ← custom volume bar handle (RGBA PNG)
    └── font.ttf
```

With this layout:
- `theme.ini` sets the name, colour palette, and layout.
- `bg_now_playing.gif` is used on the Now Playing screen as an animated background.
- `bg_visualizer.png` is used on the Visualizer screen; without it, `bg_default.png` is used.
- `bg_default.png` is used on every other screen.
- `thumb_progress.png` and `thumb_volume.png` replace the circular thumb indicators.
- `font.ttf` replaces the Latin font; CJK falls back to the bundled Noto fonts.

---

## Troubleshooting

**Theme does not appear in Settings**
- Make sure the folder is directly inside `ux0:data/VitaWave/themes/` (not nested deeper).
- The folder must contain a `theme.ini` file — even an empty one with just `[info]` and `name = ...`.
- Restart VitaWave; themes are scanned at startup.

**Background image does not show**
- The file must be named exactly as listed (e.g. `bg_now_playing.png`, `bg_now_playing.jpg`, or `bg_now_playing.jpeg`). The extension must be lowercase.
- Filenames are case-sensitive on the Vita.
- PNG must be 8-bit (not 16-bit). Open in an image editor and re-export at 8 bits per channel if unsure.
- Images larger than 4096×4096 will silently fail to load.

**Font does not change**
- The font file must be named exactly `font.ttf` (all lowercase).
- Make sure it is a valid TrueType font. Try opening it on a PC first.

**Colours look wrong**
- Double-check that colour values are exactly 6 hex digits and do not include a `#`.
- Values are `RRGGBB` (red first), not `BBGGRR`.

**App crashes after applying theme**
- A corrupt PNG or an extremely large background image can cause an out-of-memory crash. Try removing the images first to confirm the INI is valid, then re-add images one at a time.

---

## Full theme.ini reference

```ini
; Lines starting with ; or # are comments and are ignored.

[info]
; The display name shown in the Settings screen. Required.
name = Theme Name

[colors]
; All values are 6-digit hex RRGGBB (no # symbol), except vis_overlay
; which is 8-digit AARRGGBB to allow transparency control.
; Any omitted key keeps the Classic Dark default.
bg          = 1C1C1E     ; main background
accent      = 2C2C2E     ; cards, elevated surfaces
highlight   = 3A3A3C     ; selected row tint
text        = FFFFFF     ; primary text
text_dim    = 8E8E93     ; secondary / hint text
progress    = FC3C44     ; progress bar and accent icons
vis_bar     = FC3C44     ; visualizer bars
album_art_bg = 2C2C2E     ; album art placeholder
vis_overlay  = CC000000   ; info bar on Visualizer screen (AA = alpha, then RRGGBB)
album_art_tint = 00000000 ; color wash over album art (00000000 = disabled)

[sizes]
; Font sizes in pixels. Omit to keep defaults.
font_large  = 32
font_medium = 23
font_small  = 19

[layout]
; Layout dimensions in pixels. Omit to keep defaults.
album_art_size          = 200   ; album art square size on Now Playing
corner_radius           = 12    ; album art rounded corner radius (0 = square)
list_row_height         = 30    ; row height in list views
album_art_frame_padding = 0     ; pixels album_art_frame.png extends beyond art edge
```

### Optional asset files

| Filename | Purpose |
|----------|---------|
| `bg_library.png/jpg` | Library screen background |
| `bg_browser.png/jpg` | File browser background |
| `bg_now_playing.png/jpg` | Now Playing screen background |
| `bg_visualizer.png/jpg` | Visualizer screen background |
| `bg_playlist.png/jpg` | Playlist screens background |
| `bg_settings.png/jpg` | Settings screen background |
| `bg_default.png/jpg` | Fallback for any screen without its own file |
| `thumb_progress.png` | Progress bar thumb (RGBA PNG, displayed at 16×16 px) |
| `thumb_volume.png` | Volume bar thumb (RGBA PNG, displayed at 12×12 px) |
| `album_art_overlay.png` | Graphic composited over album art on Now Playing (RGBA PNG, scaled to art size) |
| `album_art_frame.png` | Frame drawn on top of art and overlay on Now Playing (RGBA PNG, scaled to art size + 2× padding) |
| `font.ttf` | Primary (Latin) font |
| `font_jp.ttf` | Japanese fallback font |
| `font_kr.ttf` | Korean fallback font |
| `font_sc.ttf` | Chinese Simplified fallback font |
