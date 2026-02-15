/*
 * noto_compat.c — default font path list for noto_init().
 *
 * To switch to a different font set, change the paths here.
 * The renderer tries each font in order for every codepoint;
 * the first font that contains the glyph wins.
 */

#include <stddef.h>
#include "font_renderer.h"

const char * const g_font_paths[] = {
    "app0:/fonts/NotoSans-Regular.ttf",    /* Latin, Cyrillic, Greek, … */
    "app0:/fonts/NotoSansJP-Regular.ttf",  /* Hiragana, Katakana, Kanji */
    "app0:/fonts/NotoSansKR-Regular.ttf",  /* Hangul                     */
    "app0:/fonts/NotoSansSC-Regular.ttf",  /* Chinese Simplified         */
    NULL
};
