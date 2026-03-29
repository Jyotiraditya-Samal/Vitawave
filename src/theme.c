/*
 * VitaWave – theme.c
 * Winamp-skin-style theme system: scan ux0:data/VitaWave/themes/, parse
 * theme.ini, load background PNGs and fonts, apply palette at runtime.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <vita2d.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

#include "theme.h"
#include "font_renderer.h"
#include "gifdec.h"
/* ui.h included after the forward declaration is resolved */
#include "ui.h"

/* ── Default Apple Music dark palette ─────────────────────────────────────── */
UIColorPalette g_palette = {
    0xFF1C1C1E,   /* bg           */
    0xFF2C2C2E,   /* accent       */
    0xFF3A3A3C,   /* highlight    */
    0xFFFFFFFF,   /* text         */
    0xFF8E8E93,   /* text_dim     */
    0xFF443CFC,   /* progress     */
    0xFF443CFC,   /* vis_bar        */
    0xFF2C2C2E,   /* album_art_bg   */
    0xCC000000,   /* vis_overlay    */
    0x00000000    /* album_art_tint */
};

/* ── Default font paths (app0: bundled Noto) ──────────────────────────────── */
static const char *k_default_fonts[THEME_FONT_MAX + 1] = {
    "app0:/fonts/NotoSans-Regular.ttf",
    "app0:/fonts/NotoSansJP-Regular.ttf",
    "app0:/fonts/NotoSansKR-Regular.ttf",
    "app0:/fonts/NotoSansSC-Regular.ttf",
    NULL
};

/* ── Themes directory ─────────────────────────────────────────────────────── */
#define THEMES_ROOT         "ux0:data/VitaWave/themes"  /* user themes        */
#define BUNDLED_THEMES_ROOT "app0:themes"               /* shipped in VPK     */
#define CURRENT_THEME       "ux0:data/VitaWave/current_theme.txt"

/* ── Tiny INI parser helpers ──────────────────────────────────────────────── */

static void str_trim(char *s)
{
    /* trim trailing whitespace */
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n' ||
                     s[n-1] == ' '  || s[n-1] == '\t'))
        s[--n] = '\0';
    /* trim leading whitespace */
    int off = 0;
    while (s[off] && (s[off] == ' ' || s[off] == '\t'))
        off++;
    if (off)
        memmove(s, s + off, strlen(s) - off + 1);
}

/* Convert CSS hex RRGGBB → vita2d ABGR 0xFF00BBGGRR */
static unsigned int css_to_abgr(const char *hex)
{
    unsigned int rgb = (unsigned int)strtoul(hex, NULL, 16);
    unsigned int r = (rgb >> 16) & 0xFF;
    unsigned int g = (rgb >>  8) & 0xFF;
    unsigned int b = (rgb      ) & 0xFF;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

/* 8-digit AARRGGBB → vita2d ABGR. Falls back to css_to_abgr for 6-digit values. */
static unsigned int cssalpha_to_abgr(const char *hex)
{
    if (strlen(hex) < 8) return css_to_abgr(hex);
    unsigned int val = (unsigned int)strtoul(hex, NULL, 16);
    unsigned int a = (val >> 24) & 0xFF;
    unsigned int r = (val >> 16) & 0xFF;
    unsigned int g = (val >>  8) & 0xFF;
    unsigned int b = (val      ) & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

/* ── Check whether a path exists ──────────────────────────────────────────── */
static bool file_exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

/* ── Build NULL-terminated font path array for font_renderer_init ─────────── */
static void build_font_ptr_array(const Theme *t,
                                 const char *ptrs[THEME_FONT_MAX + 1])
{
    for (int i = 0; i < t->font_count; i++)
        ptrs[i] = t->font_paths[i];
    ptrs[t->font_count] = NULL;
}

/* ── Free background textures for a theme ─────────────────────────────────── */
static void theme_free_backgrounds(Theme *t)
{
    if (!t) return;
#define FREE_TEX(x) do { if (x) { vita2d_free_texture(x); (x) = NULL; } } while(0)
    FREE_TEX(t->bg.library);
    FREE_TEX(t->bg.browser);
    /* Free animated now_playing frames */
    if (t->bg.now_playing.frames) {
        for (int i = 0; i < t->bg.now_playing.frame_count; i++)
            if (t->bg.now_playing.frames[i])
                vita2d_free_texture(t->bg.now_playing.frames[i]);
        free(t->bg.now_playing.frames);
        t->bg.now_playing.frames = NULL;
    }
    free(t->bg.now_playing.delays_ms);
    t->bg.now_playing.delays_ms  = NULL;
    t->bg.now_playing.frame_count = 0;
    FREE_TEX(t->bg.visualizer);
    FREE_TEX(t->bg.playlist);
    FREE_TEX(t->bg.settings);
    FREE_TEX(t->bg.fallback);
    FREE_TEX(t->bg.album_art_overlay);
    FREE_TEX(t->bg.album_art_frame);
    FREE_TEX(t->thumbs.progress);
    FREE_TEX(t->thumbs.volume);
#undef FREE_TEX
}

/* ── Load bg_now_playing GIF (scaled to screen size) ─────────────────────── */
static void load_now_playing_gif(Theme *t, const char *path)
{
    gd_GIF *gif = gd_open_gif(path);
    if (!gif) return;

    int gw = gif->width;
    int gh = gif->height;

    uint8_t *frame_buf = malloc((size_t)(gw * gh * 3)); /* gifdec outputs RGB, 3 bytes/pixel */
    vita2d_texture **frames = malloc(THEME_ANIM_FRAME_MAX * sizeof(vita2d_texture *));
    int *delays = malloc(THEME_ANIM_FRAME_MAX * sizeof(int));
    if (!frame_buf || !frames || !delays) {
        free(frame_buf); free(frames); free(delays);
        gd_close_gif(gif);
        return;
    }

    int count = 0;
    while (count < THEME_ANIM_FRAME_MAX && gd_get_frame(gif)) {
        gd_render_frame(gif, frame_buf);

        /* Upload at native GIF resolution — draw_theme_bg scales to screen */
        vita2d_texture *tex = vita2d_create_empty_texture(gw, gh);
        if (!tex) break;

        uint32_t *dst    = vita2d_texture_get_datap(tex);
        int       stride = (int)(vita2d_texture_get_stride(tex) / 4);

        for (int row = 0; row < gh; row++) {
            const uint8_t *src_row = frame_buf + row * gw * 3; /* 3 bytes/pixel (RGB) */
            uint32_t      *dst_row = dst + row * stride;
            for (int col = 0; col < gw; col++) {
                const uint8_t *p = src_row + col * 3;
                /* RGB → ABGR (fully opaque) */
                dst_row[col] = 0xFF000000u           |
                               ((uint32_t)p[2] << 16) |  /* B */
                               ((uint32_t)p[1] <<  8) |  /* G */
                                (uint32_t)p[0];           /* R */
            }
        }

        delays[count] = gif->gce.delay * 10; /* centiseconds → ms */
        if (delays[count] <= 0) delays[count] = 100;
        frames[count++] = tex;
    }

    free(frame_buf);
    gd_close_gif(gif);

    if (count == 0) { free(frames); free(delays); return; }

    t->bg.now_playing.frames      = frames;
    t->bg.now_playing.delays_ms   = delays;
    t->bg.now_playing.frame_count = count;
}

/* ── Parse INI + discover fonts only — no texture I/O ────────────────────── */
static void theme_parse_ini(Theme *t, const char *dir)
{
    char path[THEME_PATH_MAX];

    /* ── Parse theme.ini ── */
    snprintf(path, sizeof(path), "%s/theme.ini", dir);
    FILE *f = fopen(path, "r");

    char section[64] = "";
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            str_trim(line);
            if (line[0] == ';' || line[0] == '#' || line[0] == '\0')
                continue;
            if (line[0] == '[') {
                char *end = strchr(line + 1, ']');
                if (end) {
                    *end = '\0';
                    snprintf(section, sizeof(section), "%s", line + 1);
                }
                continue;
            }
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = '\0';
            char key[64], val[128];
            snprintf(key, sizeof(key), "%s", line);
            snprintf(val, sizeof(val), "%s", eq + 1);
            str_trim(key);
            str_trim(val);

            if (strcmp(section, "info") == 0) {
                if (strcmp(key, "name") == 0)
                    snprintf(t->name, THEME_NAME_MAX, "%s", val);
            } else if (strcmp(section, "colors") == 0) {
                if      (strcmp(key, "bg")           == 0) t->colors.bg           = css_to_abgr(val);
                else if (strcmp(key, "accent")       == 0) t->colors.accent       = css_to_abgr(val);
                else if (strcmp(key, "highlight")    == 0) t->colors.highlight    = css_to_abgr(val);
                else if (strcmp(key, "text")         == 0) t->colors.text         = css_to_abgr(val);
                else if (strcmp(key, "text_dim")     == 0) t->colors.text_dim     = css_to_abgr(val);
                else if (strcmp(key, "progress")     == 0) t->colors.progress     = css_to_abgr(val);
                else if (strcmp(key, "vis_bar")      == 0) t->colors.vis_bar      = css_to_abgr(val);
                else if (strcmp(key, "album_art_bg")  == 0) t->colors.album_art_bg  = css_to_abgr(val);
                else if (strcmp(key, "vis_overlay")    == 0) t->colors.vis_overlay    = cssalpha_to_abgr(val);
                else if (strcmp(key, "album_art_tint") == 0) t->colors.album_art_tint = cssalpha_to_abgr(val);
            } else if (strcmp(section, "sizes") == 0) {
                if      (strcmp(key, "font_large")  == 0) t->sz_large  = (unsigned int)atoi(val);
                else if (strcmp(key, "font_medium") == 0) t->sz_medium = (unsigned int)atoi(val);
                else if (strcmp(key, "font_small")  == 0) t->sz_small  = (unsigned int)atoi(val);
            } else if (strcmp(section, "layout") == 0) {
                if      (strcmp(key, "album_art_size")  == 0) t->layout.album_art_size  = atoi(val);
                else if (strcmp(key, "corner_radius")   == 0) t->layout.corner_radius   = atoi(val);
                else if (strcmp(key, "list_row_height")           == 0) t->layout.list_row_height           = atoi(val);
                else if (strcmp(key, "album_art_frame_padding") == 0) t->layout.album_art_frame_padding = atoi(val);
            }
        }
        fclose(f);
    }

    /* ── Discover font files ── */
    t->font_count = 0;
    const char *font_names[THEME_FONT_MAX] = {
        "font.ttf", "font_jp.ttf", "font_kr.ttf", "font_sc.ttf"
    };
    const char *fallback_fonts[THEME_FONT_MAX] = {
        "app0:/fonts/NotoSans-Regular.ttf",
        "app0:/fonts/NotoSansJP-Regular.ttf",
        "app0:/fonts/NotoSansKR-Regular.ttf",
        "app0:/fonts/NotoSansSC-Regular.ttf"
    };
    for (int i = 0; i < THEME_FONT_MAX; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, font_names[i]);
        if (file_exists(path)) {
            snprintf(t->font_paths[t->font_count], THEME_PATH_MAX, "%s", path);
        } else {
            snprintf(t->font_paths[t->font_count], THEME_PATH_MAX, "%s", fallback_fonts[i]);
        }
        t->font_count++;
    }
}

/* ── Load textures for a theme — called only when the theme is selected ──── */
static void theme_load_textures(Theme *t)
{
    if (t->textures_loaded) return;
    t->textures_loaded = true;
    const char *dir = t->dir;
    char path[THEME_PATH_MAX];

    /* ── Load thumb graphics (PNG with alpha only) ── */
    memset(&t->thumbs, 0, sizeof(t->thumbs));
    {
        struct { const char *file; vita2d_texture **tex; } thumbs[] = {
            { "thumb_progress.png", &t->thumbs.progress },
            { "thumb_volume.png",   &t->thumbs.volume   },
        };
        for (int i = 0; i < (int)(sizeof(thumbs)/sizeof(thumbs[0])); i++) {
            snprintf(path, sizeof(path), "%s/%s", dir, thumbs[i].file);
            if (file_exists(path))
                *thumbs[i].tex = vita2d_load_PNG_file(path);
        }
    }

    /* ── Load background images (PNG preferred, JPEG fallback) ── */
    memset(&t->bg, 0, sizeof(t->bg));

    /* bg_now_playing: try .gif first (animated), then static fallbacks */
    {
        char gif_path[THEME_PATH_MAX];
        snprintf(gif_path, sizeof(gif_path), "%s/bg_now_playing.gif", dir);
        if (file_exists(gif_path)) {
            load_now_playing_gif(t, gif_path);
        } else {
            static const char *exts[] = { ".png", ".jpg", ".jpeg", NULL };
            for (int e = 0; exts[e]; e++) {
                snprintf(path, sizeof(path), "%s/bg_now_playing%s", dir, exts[e]);
                if (!file_exists(path)) continue;
                vita2d_texture *tex = (exts[e][1] == 'p')
                    ? vita2d_load_PNG_file(path)
                    : vita2d_load_JPEG_file(path);
                if (tex) {
                    t->bg.now_playing.frames      = malloc(sizeof(vita2d_texture *));
                    t->bg.now_playing.delays_ms   = malloc(sizeof(int));
                    if (t->bg.now_playing.frames && t->bg.now_playing.delays_ms) {
                        t->bg.now_playing.frames[0]    = tex;
                        t->bg.now_playing.delays_ms[0] = 0;
                        t->bg.now_playing.frame_count  = 1;
                    } else {
                        vita2d_free_texture(tex);
                        free(t->bg.now_playing.frames);
                        free(t->bg.now_playing.delays_ms);
                        t->bg.now_playing.frames    = NULL;
                        t->bg.now_playing.delays_ms = NULL;
                    }
                }
                break;
            }
        }
    }

    struct { const char *base; vita2d_texture **tex; } bgs[] = {
        { "bg_library",    &t->bg.library    },
        { "bg_browser",    &t->bg.browser    },
        { "bg_visualizer", &t->bg.visualizer },
        { "bg_playlist",   &t->bg.playlist   },
        { "bg_settings",   &t->bg.settings   },
        { "bg_default",    &t->bg.fallback   },
    };
    /* album_art_overlay.png / album_art_frame.png — PNG only (need alpha channel) */
    snprintf(path, sizeof(path), "%s/album_art_overlay.png", dir);
    if (file_exists(path))
        t->bg.album_art_overlay = vita2d_load_PNG_file(path);

    snprintf(path, sizeof(path), "%s/album_art_frame.png", dir);
    if (file_exists(path))
        t->bg.album_art_frame = vita2d_load_PNG_file(path);
    for (int i = 0; i < (int)(sizeof(bgs)/sizeof(bgs[0])); i++) {
        static const char *exts[] = { ".png", ".jpg", ".jpeg", NULL };
        for (int e = 0; exts[e]; e++) {
            snprintf(path, sizeof(path), "%s/%s%s", dir, bgs[i].base, exts[e]);
            if (!file_exists(path)) continue;
            if (exts[e][1] == 'p')
                *bgs[i].tex = vita2d_load_PNG_file(path);
            else
                *bgs[i].tex = vita2d_load_JPEG_file(path);
            break;
        }
    }
}


/* ── theme_manager_init ───────────────────────────────────────────────────── */

int theme_manager_init(ThemeManager *mgr)
{
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(*mgr));

    /* ── Index 0: built-in Default theme ── */
    Theme *def = &mgr->themes[0];
    snprintf(def->name, THEME_NAME_MAX, "Classic Dark");
    def->dir[0] = '\0';
    /* Copy default font paths */
    def->font_count = 0;
    for (int i = 0; k_default_fonts[i] != NULL && i < THEME_FONT_MAX; i++) {
        snprintf(def->font_paths[i], THEME_PATH_MAX, "%s", k_default_fonts[i]);
        def->font_count++;
    }
    def->colors       = g_palette;  /* Apple Music dark defaults */
    def->sz_large     = 32;
    def->sz_medium    = 23;
    def->sz_small     = 19;
    def->layout.album_art_size  = 200;
    def->layout.corner_radius   = 12;
    def->layout.list_row_height = 30;
    memset(&def->bg, 0, sizeof(def->bg));

    mgr->count   = 1;
    mgr->current = 0;

    /* ── Scan bundled themes (app0:themes/) then user themes ─────────────── */
    #define SCAN_THEMES_DIR(root) do { \
        SceUID _dd = sceIoDopen(root); \
        if (_dd >= 0) { \
            SceIoDirent _e; \
            while (sceIoDread(_dd, &_e) > 0 && mgr->count < THEME_MAX) { \
                if (_e.d_name[0] == '.') continue; \
                if (!SCE_S_ISDIR(_e.d_stat.st_mode)) continue; \
                char _dir[THEME_PATH_MAX]; \
                snprintf(_dir, sizeof(_dir), "%s/%s", root, _e.d_name); \
                char _ini[THEME_PATH_MAX]; \
                snprintf(_ini, sizeof(_ini), "%s/theme.ini", _dir); \
                if (!file_exists(_ini)) continue; \
                Theme *_t = &mgr->themes[mgr->count]; \
                memset(_t, 0, sizeof(*_t)); \
                _t->colors      = g_palette; \
                _t->sz_large    = 32; \
                _t->sz_medium   = 23; \
                _t->sz_small    = 19; \
                _t->layout.album_art_size  = 200; \
                _t->layout.corner_radius   = 12; \
                _t->layout.list_row_height = 30; \
                snprintf(_t->name, THEME_NAME_MAX, "%s", _e.d_name); \
                snprintf(_t->dir,  THEME_PATH_MAX, "%s", _dir); \
                theme_parse_ini(_t, _dir); \
                mgr->count++; \
            } \
            sceIoDclose(_dd); \
        } \
    } while (0)

    SCAN_THEMES_DIR(BUNDLED_THEMES_ROOT);   /* shipped in VPK    */
    SCAN_THEMES_DIR(THEMES_ROOT);           /* user-added themes */
    #undef SCAN_THEMES_DIR

    return 0;
}

/* ── theme_manager_free ───────────────────────────────────────────────────── */

void theme_manager_free(ThemeManager *mgr)
{
    if (!mgr) return;
    /* Skip index 0 (Default has no loaded textures) */
    for (int i = 1; i < mgr->count; i++) {
        theme_free_backgrounds(&mgr->themes[i]);
    }
    mgr->count   = 0;
    mgr->current = 0;
}

/* ── theme_current ────────────────────────────────────────────────────────── */

Theme *theme_current(ThemeManager *mgr)
{
    if (!mgr || mgr->count == 0) return NULL;
    if (mgr->current < 0 || mgr->current >= mgr->count) return NULL;
    return &mgr->themes[mgr->current];
}

/* ── theme_manager_select ─────────────────────────────────────────────────── */

int theme_manager_select(ThemeManager *mgr, void *ui_ptr, int index)
{
    if (!mgr || index < 0 || index >= mgr->count) return -1;

    /* Free textures of the previously active theme to reclaim CDRAM */
    if (mgr->current != index) {
        Theme *prev = &mgr->themes[mgr->current];
        if (prev->textures_loaded) {
            theme_free_backgrounds(prev);
            prev->textures_loaded = false;
        }
    }

    mgr->current = index;
    Theme *t = &mgr->themes[index];

    /* Load textures for the newly selected theme */
    if (t->dir[0] != '\0')
        theme_load_textures(t);

    /* Apply palette globally */
    g_palette = t->colors;
    vita2d_set_clear_color(g_palette.bg);

    /* Reload fonts */
    font_renderer_destroy();
    {
        const char *ptrs[THEME_FONT_MAX + 1];
        build_font_ptr_array(t, ptrs);
        font_renderer_init(ptrs);
    }

    /* Apply layout and font sizes to UIState */
    if (ui_ptr) {
        UIState *ui = (UIState *)ui_ptr;
        int art  = t->layout.album_art_size;
        int cr   = t->layout.corner_radius;
        int lrh  = t->layout.list_row_height;
        ui->layout.album_art_size          = (art  > 0) ? art  : 200;
        ui->layout.corner_radius           = (cr   > 0) ? cr   : 12;
        ui->layout.list_row_height         = (lrh  > 0) ? lrh  : 30;
        ui->layout.album_art_frame_padding = t->layout.album_art_frame_padding;

        if (t->sz_large  > 0) ui->font_large.size  = t->sz_large;
        if (t->sz_medium > 0) ui->font_medium.size = t->sz_medium;
        if (t->sz_small  > 0) ui->font_small.size  = t->sz_small;
    }

    return 0;
}

/* ── theme_manager_save ───────────────────────────────────────────────────── */

void theme_manager_save(const ThemeManager *mgr)
{
    if (!mgr) return;
    FILE *f = fopen(CURRENT_THEME, "w");
    if (!f) return;
    Theme *t = (Theme *)&mgr->themes[mgr->current];
    fprintf(f, "%s\n", t->name);
    fclose(f);
}

/* ── theme_manager_restore ────────────────────────────────────────────────── */

void theme_manager_restore(ThemeManager *mgr, void *ui)
{
    if (!mgr) return;
    FILE *f = fopen(CURRENT_THEME, "r");
    if (!f) return;
    char name[THEME_NAME_MAX];
    if (!fgets(name, sizeof(name), f)) {
        fclose(f);
        return;
    }
    fclose(f);
    str_trim(name);

    for (int i = 0; i < mgr->count; i++) {
        if (strcmp(mgr->themes[i].name, name) == 0) {
            theme_manager_select(mgr, ui, i);
            return;
        }
    }
    /* not found — stay on Default (index 0) */
    theme_manager_select(mgr, ui, 0);
}
