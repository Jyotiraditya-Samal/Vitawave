#ifndef THEME_H
#define THEME_H

#include <stdint.h>
#include <vita2d.h>

/* ABGR color palette — all COLOR_* macros expand into g_palette fields */
typedef struct {
    unsigned int bg;
    unsigned int accent;
    unsigned int highlight;
    unsigned int text;
    unsigned int text_dim;
    unsigned int progress;
    unsigned int vis_bar;
    unsigned int album_art_bg;
} UIColorPalette;

extern UIColorPalette g_palette;

/* Default dark palette (Apple Music-like) */
#define COLOR_BG         g_palette.bg
#define COLOR_ACCENT_BG  g_palette.accent
#define COLOR_HIGHLIGHT  g_palette.highlight
#define COLOR_TEXT       g_palette.text
#define COLOR_TEXT_DIM   g_palette.text_dim
#define COLOR_PROGRESS   g_palette.progress
#define COLOR_VIS_BAR    g_palette.vis_bar

void theme_set_defaults(UIColorPalette *p);
int  theme_load_ini(UIColorPalette *p, const char *path);

#endif
