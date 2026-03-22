#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdbool.h>
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/rtc.h>

#include "audio_engine.h"
#include "file_browser.h"
#include "playlist.h"
#include "visualizer.h"
#include "theme.h"

/* ── Screen dimensions ────────────────────────────────────────────────────── */
#define SCREEN_WIDTH    960
#define SCREEN_HEIGHT   544
#define UI_FPS          60
#define BAR_HEIGHT      40
#define SIDEBAR_WIDTH   200

/* Runtime palette — updated by theme_manager_select(). */
#define COLOR_BG            g_palette.bg
#define COLOR_ACCENT        g_palette.accent
#define COLOR_HIGHLIGHT     g_palette.highlight
#define COLOR_TEXT          g_palette.text
#define COLOR_TEXT_DIM      g_palette.text_dim
#define COLOR_PROGRESS      g_palette.progress
#define COLOR_VIS_BAR       g_palette.vis_bar
#define COLOR_ALBUM_ART_BG   g_palette.album_art_bg
#define COLOR_VIS_OVERLAY    g_palette.vis_overlay
#define COLOR_ALBUM_ART_TINT g_palette.album_art_tint

/* ── Screens ──────────────────────────────────────────────────────────────── */
typedef enum {
    UI_SCREEN_LIBRARY          = 0,
    UI_SCREEN_BROWSER          = 1,
    UI_SCREEN_NOW_PLAYING      = 2,
    UI_SCREEN_VISUALIZER       = 3,
    UI_SCREEN_PLAYLIST         = 4,
    UI_SCREEN_SETTINGS         = 5,
    UI_SCREEN_PLAYLIST_LIST    = 6,
    UI_SCREEN_PLAYLIST_DETAIL  = 7,
    UI_SCREEN_ADD_TO_PLAYLIST  = 8,
    UI_SCREEN_RENAME_PLAYLIST  = 9,
    UI_SCREEN_EQUALIZER        = 10
} UIScreen;

/* ── Font wrapper ─────────────────────────────────────────────────────────── */
typedef struct {
    unsigned int size;   /* pixel size passed to noto_draw_text / noto_text_width */
} UIFont;

/* ── Settings sub-state ───────────────────────────────────────────────────── */
typedef struct {
    int  theme;               /* 0 = dark, 1 = darker */
    bool show_album_art;
    bool crossfade;
    int  crossfade_duration;  /* seconds */
    bool equalizer_enabled;
    bool gif_reactive;        /* GIF background reacts to music energy */
    int  settings_selected;   /* which setting row is highlighted */
} UISettings;

/* ── Main UI state ────────────────────────────────────────────────────────── */
typedef struct {
    UIScreen current_screen;
    UIScreen prev_screen;

    int list_offset;          /* first visible row in list views */
    int list_selected;        /* highlighted row index */

    bool show_visualizer;     /* show vis overlay on Now Playing */
    int  vis_mode;            /* maps to VisMode */

    UISettings settings;

    /* Animation */
    int      anim_frame;
    float    fps;
    uint64_t last_frame_time;  /* microseconds from sceKernelGetProcessTimeWide */

    /* Animated now_playing background */
    int      np_bg_frame;      /* current frame index */
    uint64_t np_bg_last_tick;  /* time current frame was set (microseconds) */

    /* Fonts */
    UIFont font_large;
    UIFont font_medium;
    UIFont font_small;

    /* Cached previous button state for edge detection */
    SceCtrlData prev_pad;

    /* Input repeat timer */
    int  repeat_timer;
    int  repeat_initial_delay;  /* frames before repeat starts */
    int  repeat_rate;           /* frames between repeated inputs */

    /* Multi-playlist manager state */
    int  playlist_mgr_selected;      /* highlighted row in PLAYLIST_LIST */
    int  playlist_detail_idx;        /* which PlaylistManager entry is open in DETAIL */
    char pending_add_path[512];      /* track path waiting to be added (ADD_TO_PLAYLIST) */

    /* Saved browser position — restored when closing ADD_TO_PLAYLIST */
    int  browser_saved_selected;
    int  browser_saved_offset;

    /* Rename screen state */
    int  rename_playlist_idx;        /* which playlist is being renamed     */
    char rename_buf[128];            /* current name being edited           */
    int  rename_cursor;              /* cursor position in rename_buf       */
    int  rename_char_idx;            /* position in the character set for current char */

    /* Equalizer screen state */
    int  eq_selected;        /* 0=preamp, 1..EQ_BANDS=band */
    int  eq_preset_idx;      /* -1=unsaved custom, 0..EQ_PRESET_COUNT-1=built-in,
                              * EQ_PRESET_COUNT+i=user custom preset i           */

    EQCustomPreset eq_custom[EQ_CUSTOM_PRESET_MAX];
    int            eq_custom_count;

    /* EQ inline sub-modes: 0=normal, 1=save-name, 2=rename, 3=manage-menu */
    int  eq_action_mode;
    int  eq_manage_cursor;        /* 0=Rename, 1=Delete in manage menu */
    char eq_name_buf[EQ_CUSTOM_NAME_LEN];
    int  eq_name_cursor;

    /* Theme manager (set by main after theme_manager_init) */
    ThemeManager *theme_mgr;
    int settings_theme_preview;  /* index browsed with L/R in Settings, applied on X */

    /* Active layout parameters (set by theme_manager_select) */
    struct {
        int album_art_size;          /* px, default 200 */
        int corner_radius;           /* px, default 12  */
        int list_row_height;         /* px, default 30  */
        int album_art_frame_padding; /* px, default 0   */
    } layout;
} UIState;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * Initialise the UI, load fonts and set up default state.
 * Returns 0 on success, negative on failure.
 */
int ui_init(UIState *ui);

/**
 * Free all UI resources (fonts, etc.).
 */
void ui_destroy(UIState *ui);

/**
 * Process controller input for the current frame.
 */
void ui_handle_input(UIState *ui,
                     AudioEngine  *engine,
                     Playlist     *playlist,
                     FileList     *browser,
                     Visualizer   *vis);

/**
 * Update per-frame UI logic (animations, FPS counter, etc.).
 */
void ui_update(UIState *ui, Visualizer *vis);

/**
 * Draw the entire UI for the current frame.
 * Must be called between vita2d_start_drawing and vita2d_end_drawing.
 */
void ui_render(const UIState    *ui,
               const AudioEngine *engine,
               const Playlist   *playlist,
               const FileList   *browser,
               Visualizer       *vis);

/**
 * Transition to a new screen.
 */
void ui_switch_screen(UIState *ui, UIScreen screen);

/* ── Individual draw helpers (also usable from outside) ──────────────────── */

void ui_draw_progress_bar(int x, int y, int w, int h,
                          float fraction, uint32_t color_fg, uint32_t color_bg);

void ui_draw_track_info(const UIState *ui, const AudioEngine *engine,
                        const Playlist *playlist, int x, int y);

void ui_draw_file_list(const UIState *ui, const FileList *browser,
                       int x, int y, int w, int h);

void ui_draw_visualizer(const UIState *ui, Visualizer *vis,
                        const AudioEngine *engine, const Playlist *playlist);

void ui_draw_now_playing(const UIState *ui,
                         const AudioEngine *engine,
                         const Playlist   *playlist,
                         Visualizer       *vis);

void ui_draw_settings(const UIState *ui);

void ui_draw_playlist(const UIState *ui, const Playlist *playlist,
                      const AudioEngine *engine);

void ui_draw_playlist_list  (const UIState *ui, const PlaylistManager *mgr);
void ui_draw_playlist_detail(const UIState *ui, const PlaylistManager *mgr,
                              const AudioEngine *engine);
void ui_draw_add_to_playlist  (const UIState *ui, const PlaylistManager *mgr);
void ui_draw_rename_playlist  (const UIState *ui);
void ui_draw_equalizer        (const UIState *ui);

#endif /* UI_H */
