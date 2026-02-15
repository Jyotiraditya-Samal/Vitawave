/*
 * font_renderer.c — custom FreeType → vita2d glyph atlas renderer
 *
 * Pipeline:
 *   1. FreeType rasterises each glyph with FORCE_AUTOHINT for strong
 *      pixel-grid snapping (crisp edges).
 *   2. The coverage bitmap is packed into a 1024×1024 RGBA vita2d texture
 *      (white pixel, alpha = FreeType coverage value).
 *   3. The atlas texture is set to SCE_GXM_TEXTURE_FILTER_POINT so the GPU
 *      samples exact texels — no bilinear blur.
 *   4. Text drawing tints the white atlas glyph with the desired text colour.
 */

#include "font_renderer.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <vita2d.h>

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Atlas config ─────────────────────────────────────────────────────────── */
#define ATLAS_W     1024
#define ATLAS_H     1024
#define ATLAS_PAD   1       /* gap between packed glyphs (avoids bleed)   */

/* ── Glyph cache ──────────────────────────────────────────────────────────── */
#define CACHE_CAP   8192    /* must be power of two                        */
#define CACHE_MASK  (CACHE_CAP - 1)

typedef struct {
    uint32_t     codepoint;
    uint16_t     px_size;
    uint8_t      face_idx;
    uint8_t      valid;
    /* atlas rect */
    uint16_t     ax, ay, aw, ah;
    /* FreeType metrics (in pixels) */
    int16_t      bx;    /* bearing X: pen → glyph-bitmap left edge        */
    int16_t      by;    /* bearing Y: baseline → glyph-bitmap top edge    */
    int16_t      adv;   /* horizontal advance                              */
} GlyphEntry;

/* ── Module state ─────────────────────────────────────────────────────────── */
#define MAX_FACES 8

static FT_Library  g_ft;
static FT_Face     g_faces[MAX_FACES];
static int         g_num_faces;

static vita2d_texture *g_atlas;
static uint8_t        *g_atlas_data;   /* pointer to raw RGBA bytes        */
static unsigned int    g_atlas_stride; /* bytes per row                    */

/* shelf-packer state */
static int g_shelf_x;
static int g_shelf_y;
static int g_shelf_h;

static GlyphEntry g_cache[CACHE_CAP];

/* ── UTF-8 helpers ────────────────────────────────────────────────────────── */
static uint32_t utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t cp;
    int extra;

    if      (*s < 0x80) { cp = *s;        extra = 0; }
    else if (*s < 0xC0) { (*p)++; return 0xFFFD; }
    else if (*s < 0xE0) { cp = *s & 0x1F; extra = 1; }
    else if (*s < 0xF0) { cp = *s & 0x0F; extra = 2; }
    else                { cp = *s & 0x07; extra = 3; }

    s++;
    for (int i = 0; i < extra; i++) {
        if ((*s & 0xC0) != 0x80) { *p = (const char *)s; return 0xFFFD; }
        cp = (cp << 6) | (*s++ & 0x3F);
    }
    *p = (const char *)s;
    return cp;
}

static uint32_t utf8_peek(const char *p) { return utf8_next(&p); }

static int utf8_char_bytes(const char *p)
{
    const char *q = p;
    utf8_next(&q);
    return (int)(q - p);
}

/* ── Cache helpers ────────────────────────────────────────────────────────── */
static uint32_t cache_hash(uint32_t cp, uint16_t size, uint8_t face)
{
    uint32_t h = cp;
    h ^= h >> 16;
    h *= 0x45d9f3b;
    h ^= (uint32_t)size * 65537u;
    h ^= (uint32_t)face * 2654435761u;
    h ^= h >> 16;
    return h & CACHE_MASK;
}

static GlyphEntry *cache_lookup(uint32_t cp, uint16_t size, uint8_t face)
{
    uint32_t idx = cache_hash(cp, size, face);
    for (int probe = 0; probe < 32; probe++) {
        GlyphEntry *e = &g_cache[(idx + probe) & CACHE_MASK];
        if (!e->valid) return NULL;
        if (e->codepoint == cp && e->px_size == size && e->face_idx == face)
            return e;
    }
    return NULL;
}

static GlyphEntry *cache_insert(uint32_t cp, uint16_t size, uint8_t face)
{
    uint32_t idx = cache_hash(cp, size, face);
    for (int probe = 0; probe < 32; probe++) {
        GlyphEntry *e = &g_cache[(idx + probe) & CACHE_MASK];
        if (!e->valid) return e;
    }
    /* cache is full for this hash chain — evict first slot */
    return &g_cache[idx & CACHE_MASK];
}

/* ── Atlas helpers ────────────────────────────────────────────────────────── */

/* Reset the atlas and glyph cache (called when the atlas fills up). */
static void atlas_reset(void)
{
    memset(g_atlas_data, 0, (size_t)g_atlas_stride * ATLAS_H);
    memset(g_cache, 0, sizeof(g_cache));
    g_shelf_x = 0;
    g_shelf_y = 0;
    g_shelf_h = 0;
}

/*
 * Reserve (w × h) pixels in the atlas using a simple shelf packer.
 * Returns 1 on success, 0 if the atlas is completely full (triggers reset).
 */
static int atlas_alloc(int w, int h, int *out_x, int *out_y)
{
    if (g_shelf_x + w + ATLAS_PAD > ATLAS_W) {
        /* advance to the next shelf */
        g_shelf_y += g_shelf_h + ATLAS_PAD;
        g_shelf_x  = 0;
        g_shelf_h  = 0;
    }
    if (g_shelf_y + h + ATLAS_PAD > ATLAS_H) {
        atlas_reset();
    }
    *out_x     = g_shelf_x;
    *out_y     = g_shelf_y;
    g_shelf_x += w + ATLAS_PAD;
    if (h > g_shelf_h) g_shelf_h = h;
    return 1;
}

/*
 * Write an 8-bit FreeType coverage bitmap into the atlas at (ax, ay).
 * Each source byte becomes a white RGBA pixel whose alpha = coverage value.
 * Vita2d textures are RGBA in memory (R at lowest byte address).
 */
static void atlas_blit(int ax, int ay, FT_Bitmap *bmp)
{
    for (int row = 0; row < (int)bmp->rows; row++) {
        uint8_t *dst = g_atlas_data + (ay + row) * g_atlas_stride
                                    + ax * 4;
        uint8_t *src = bmp->buffer  + row * bmp->pitch;
        for (int col = 0; col < (int)bmp->width; col++) {
            uint8_t a = src[col];
            dst[col * 4 + 0] = 0xFF;   /* R */
            dst[col * 4 + 1] = 0xFF;   /* G */
            dst[col * 4 + 2] = 0xFF;   /* B */
            dst[col * 4 + 3] = a;      /* A = glyph coverage */
        }
    }
}

/* ── Glyph rasterisation ──────────────────────────────────────────────────── */

/*
 * Rasterise codepoint cp at pixel size px_size using face g_faces[face_idx].
 * Stores the result in the atlas and returns a pointer to the cache entry.
 * Returns NULL if rasterisation fails.
 */
static GlyphEntry *rasterise(uint32_t cp, uint16_t px_size, uint8_t face_idx)
{
    FT_Face face = g_faces[face_idx];
    FT_Set_Pixel_Sizes(face, 0, px_size);

    FT_UInt glyph_idx = FT_Get_Char_Index(face, cp);
    if (glyph_idx == 0 && cp != 0) return NULL;   /* not in this font */

    if (FT_Load_Glyph(face, glyph_idx,
                      FT_LOAD_DEFAULT | FT_LOAD_FORCE_AUTOHINT))
        return NULL;

    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL))
        return NULL;

    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap   *bmp  = &slot->bitmap;

    int ax = 0, ay = 0;
    int aw = (int)bmp->width;
    int ah = (int)bmp->rows;

    /* Whitespace / zero-size glyphs: skip atlas upload, still cache metrics */
    if (aw > 0 && ah > 0) {
        if (!atlas_alloc(aw, ah, &ax, &ay)) return NULL;
        atlas_blit(ax, ay, bmp);
    }

    GlyphEntry *e   = cache_insert(cp, px_size, face_idx);
    e->codepoint    = cp;
    e->px_size      = px_size;
    e->face_idx     = face_idx;
    e->valid        = 1;
    e->ax           = (uint16_t)ax;
    e->ay           = (uint16_t)ay;
    e->aw           = (uint16_t)aw;
    e->ah           = (uint16_t)ah;
    e->bx           = (int16_t)(slot->bitmap_left);
    e->by           = (int16_t)(slot->bitmap_top);
    e->adv          = (int16_t)(slot->advance.x >> 6);
    return e;
}

/*
 * Get (or rasterise) the glyph for codepoint cp at px_size.
 * Tries each loaded face in order; returns the first hit.
 */
static const GlyphEntry *get_glyph(uint32_t cp, uint16_t px_size)
{
    for (int fi = 0; fi < g_num_faces; fi++) {
        GlyphEntry *e = cache_lookup(cp, px_size, (uint8_t)fi);
        if (e) return e;
    }
    /* Not cached — rasterise from the first face that has the glyph */
    for (int fi = 0; fi < g_num_faces; fi++) {
        GlyphEntry *e = rasterise(cp, px_size, (uint8_t)fi);
        if (e) return e;
    }
    return NULL;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int font_renderer_init(const char * const *font_paths)
{
    if (FT_Init_FreeType(&g_ft)) return -1;

    g_num_faces = 0;
    for (int i = 0; font_paths[i] && i < MAX_FACES; i++) {
        if (FT_New_Face(g_ft, font_paths[i], 0, &g_faces[g_num_faces]) == 0)
            g_num_faces++;
        /* Silently skip fonts that fail to load */
    }
    if (g_num_faces == 0) { FT_Done_FreeType(g_ft); return -2; }

    /* Create the glyph atlas texture */
    g_atlas = vita2d_create_empty_texture(ATLAS_W, ATLAS_H);
    if (!g_atlas) { FT_Done_FreeType(g_ft); return -3; }

    /* Point (nearest-neighbour) filtering — no bilinear blurring */
    vita2d_texture_set_filters(g_atlas,
                               SCE_GXM_TEXTURE_FILTER_POINT,
                               SCE_GXM_TEXTURE_FILTER_POINT);

    g_atlas_data   = (uint8_t *)vita2d_texture_get_datap(g_atlas);
    g_atlas_stride = vita2d_texture_get_stride(g_atlas);

    memset(g_atlas_data, 0, (size_t)g_atlas_stride * ATLAS_H);
    memset(g_cache, 0, sizeof(g_cache));

    g_shelf_x = g_shelf_y = g_shelf_h = 0;
    return 0;
}

void font_renderer_destroy(void)
{
    for (int i = 0; i < g_num_faces; i++)
        FT_Done_Face(g_faces[i]);
    g_num_faces = 0;

    if (g_atlas) { vita2d_free_texture(g_atlas); g_atlas = NULL; }
    FT_Done_FreeType(g_ft);
}

int font_renderer_draw_text(int x, int y, unsigned int color,
                            unsigned int size, const char *text)
{
    if (!text || !*text || !g_atlas) return 0;

    int cx = x;
    const char *p = text;

    while (*p) {
        uint32_t cp = utf8_peek(p);
        p += utf8_char_bytes(p);

        const GlyphEntry *e = get_glyph(cp, (uint16_t)size);
        if (!e) continue;

        if (e->aw > 0 && e->ah > 0) {
            vita2d_draw_texture_tint_part(
                g_atlas,
                (float)(cx + e->bx),
                (float)(y  - e->by),
                (float)e->ax, (float)e->ay,
                (float)e->aw, (float)e->ah,
                color);
        }
        cx += e->adv;
    }
    return cx - x;
}

int font_renderer_draw_textf(int x, int y, unsigned int color,
                              unsigned int size, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return font_renderer_draw_text(x, y, color, size, buf);
}

int font_renderer_text_width(unsigned int size, const char *text)
{
    if (!text || !*text) return 0;

    int total = 0;
    const char *p = text;
    while (*p) {
        uint32_t cp = utf8_peek(p);
        p += utf8_char_bytes(p);
        const GlyphEntry *e = get_glyph(cp, (uint16_t)size);
        if (e) total += e->adv;
    }
    return total;
}

int font_renderer_text_height(unsigned int size)
{
    /* Measure ascender + descender of the primary face at this size */
    if (g_num_faces == 0) return (int)size;
    FT_Set_Pixel_Sizes(g_faces[0], 0, size);
    FT_Size_Metrics *m = &g_faces[0]->size->metrics;
    return (int)((m->ascender - m->descender) >> 6);
}
