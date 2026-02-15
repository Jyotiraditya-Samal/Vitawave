#ifndef NOTO_H
#define NOTO_H

#include <stdint.h>
#include <vita2d.h>

/*
 * Multi-font text renderer using Noto Sans.
 *
 * Automatically selects the correct font per Unicode script:
 *   - Hangul      → NotoSansKR
 *   - Hiragana / Katakana / CJK ideographs → NotoSansJP
 *   - Everything else (Latin, Cyrillic, Arabic, Thai, …) → NotoSans
 *
 * Drop-in replacement for vita2d_pgf_draw_text / vita2d_pgf_text_width.
 * Sizes are in pixels (integer), not PGF float scale multipliers.
 */

/* Initialise: load font files from app0:/fonts/.
 * Returns 0 on success, negative on failure (falls back gracefully). */
int  noto_init(void);

/* Free all loaded fonts. */
void noto_destroy(void);

/* Draw UTF-8 text at (x, y). Returns the pixel width drawn. */
int noto_draw_text (int x, int y, unsigned int color,
                    unsigned int size, const char *text);

/* Formatted variant. */
int noto_draw_textf(int x, int y, unsigned int color,
                    unsigned int size, const char *fmt, ...);

/* Measure text width without drawing. */
int noto_text_width (unsigned int size, const char *text);

/* Measure text height (ascender + descender) for the given size. */
int noto_text_height(unsigned int size, const char *text);

#endif /* NOTO_H */
