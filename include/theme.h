#ifndef THEME_H
#define THEME_H

#include <vita2d.h>
#include <stdbool.h>

#define THEME_MAX            32
#define THEME_FONT_MAX        4
#define THEME_NAME_MAX       64
#define THEME_PATH_MAX      256
#define THEME_ANIM_FRAME_MAX 48   /* max GIF frames for now_playing background */

/* Animated (or static) background slot — used for now_playing only.
 * frame_count == 1 means a static image stored as frames[0]. */
typedef struct {
    vita2d_texture **frames;     /* heap array of frame textures */
    int             *delays_ms;  /* per-frame delay in milliseconds */
    int              frame_count;
} ThemeAnimBg;

static inline vita2d_texture *theme_animbg_frame(const ThemeAnimBg *ab, int idx)
{
    if (!ab || !ab->frames || ab->frame_count <= 0) return NULL;
    return ab->frames[idx % ab->frame_count];
}

/* Runtime color palette — vita2d ABGR (0xAABBGGRR).
 * Updated by theme_manager_select(); all COLOR_* macros point here. */
typedef struct {
    unsigned int bg, accent, highlight, text, text_dim,
                 progress, vis_bar, album_art_bg, vis_overlay,
                 album_art_tint;   /* ABGR overlay tint drawn over art; 0x00000000 = disabled */
} UIColorPalette;

extern UIColorPalette g_palette;   /* defined in theme.c, default = Apple Music dark */

/* Per-screen background textures (NULL = none). */
typedef struct {
    vita2d_texture *library;
    vita2d_texture *browser;
    ThemeAnimBg     now_playing;   /* supports animated GIF or static image */
    vita2d_texture *visualizer;
    vita2d_texture *playlist;
    vita2d_texture *settings;
    vita2d_texture *fallback;          /* used when screen-specific bg is NULL */
    vita2d_texture *album_art_overlay; /* album_art_overlay.png — drawn over cover art */
    vita2d_texture *album_art_frame;   /* album_art_frame.png   — drawn around cover art */
} ThemeBackgrounds;

/* Custom thumb graphics for progress and volume bars (NULL = use default circle). */
typedef struct {
    vita2d_texture *progress;   /* thumb_progress.png — drawn centered on thumb pos */
    vita2d_texture *volume;     /* thumb_volume.png   — drawn centered on thumb pos */
} ThemeThumbs;

/* Adjustable layout constants. 0 means "use built-in default". */
typedef struct {
    int album_art_size;         /* default 200 */
    int corner_radius;          /* default 12  */
    int list_row_height;        /* default 30  */
    int album_art_frame_padding; /* pixels frame extends beyond art edge; default 0 */
} ThemeLayout;

typedef struct {
    char            name[THEME_NAME_MAX];
    char            dir [THEME_PATH_MAX];   /* absolute folder path, or "" for Default */

    /* NULL-terminated list of font file paths for font_renderer_init() */
    char            font_paths[THEME_FONT_MAX][THEME_PATH_MAX];
    int             font_count;

    unsigned int    sz_large, sz_medium, sz_small;   /* font pixel sizes */

    UIColorPalette   colors;
    ThemeBackgrounds bg;
    ThemeThumbs      thumbs;
    ThemeLayout      layout;
    bool             textures_loaded;
} Theme;

typedef struct {
    Theme  themes[THEME_MAX];
    int    count;
    int    current;   /* index of active theme */
} ThemeManager;

int    theme_manager_init   (ThemeManager *mgr);
void   theme_manager_free   (ThemeManager *mgr);
Theme *theme_current        (ThemeManager *mgr);
/* ui parameter is UIState* — passed as void* to avoid circular include */
int    theme_manager_select (ThemeManager *mgr, void *ui, int index);
void   theme_manager_save   (const ThemeManager *mgr);
void   theme_manager_restore(ThemeManager *mgr, void *ui);

#endif /* THEME_H */
