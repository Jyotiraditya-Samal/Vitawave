#ifndef FONT_RENDERER_H
#define FONT_RENDERER_H

/*
 * font_renderer.h — custom FreeType glyph renderer for vita2d
 *
 * Renders glyphs into a vita2d texture atlas with SCE_GXM_TEXTURE_FILTER_POINT
 * (nearest-neighbour) sampling, giving crisp text at any pixel size.
 * Works with any TTF/OTF font file; supports multi-font fallback chains for
 * multi-script text (Latin, Japanese, Korean, Chinese, etc.).
 *
 * Public API intentionally matches noto.h so callers need no changes.
 */

#include <stdint.h>

/* ── Initialise ───────────────────────────────────────────────────────────── */

/*
 * Initialise FreeType, open font files, create the glyph atlas.
 * font_paths[] must end with a NULL sentinel; the first font is the
 * primary (Latin) font, subsequent fonts are CJK / script fallbacks.
 *
 * The renderer tries each font in order when looking up a codepoint;
 * the first font that contains the glyph wins.
 *
 * Returns 0 on success, negative on failure.
 */
int  font_renderer_init   (const char * const *font_paths);
void font_renderer_destroy(void);

/* ── Drawing ──────────────────────────────────────────────────────────────── */

/* Draw UTF-8 text at (x, y) with the given pixel size and ABGR color.
 * Returns the pixel width drawn. */
int  font_renderer_draw_text (int x, int y, unsigned int color,
                               unsigned int size, const char *text);

/* printf-style variant. */
int  font_renderer_draw_textf(int x, int y, unsigned int color,
                               unsigned int size, const char *fmt, ...);

/* Measure text width (pixels) without drawing. */
int  font_renderer_text_width (unsigned int size, const char *text);

/* Measure typical text height (pixels) for the given size. */
int  font_renderer_text_height(unsigned int size);

/* ── Convenience aliases matching the old noto_ names ────────────────────── */
#define noto_draw_text(x,y,c,s,t)         font_renderer_draw_text(x,y,c,s,t)
#define noto_draw_textf(x,y,c,s,...)      font_renderer_draw_textf(x,y,c,s,__VA_ARGS__)
#define noto_text_width(s,t)              font_renderer_text_width(s,t)
#define noto_text_height(s,t)             font_renderer_text_height(s)
#define noto_init()                       font_renderer_init(g_font_paths)
#define noto_destroy()                    font_renderer_destroy()

/* Default font path list used by noto_init(). Defined in noto_compat.c */
extern const char * const g_font_paths[];

#endif /* FONT_RENDERER_H */
