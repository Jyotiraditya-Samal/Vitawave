/*
 * VitaWave – ui.c
 * Complete UI implementation: input handling, screen rendering, animations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/rtc.h>
#include <psp2/kernel/processmgr.h>

#include "ui.h"
#include "audio_engine.h"
#include "file_browser.h"
#include "metadata.h"
#include "playlist.h"
#include "visualizer.h"
#include "globals.h"
#include "font_renderer.h"
#include "theme.h"
#include "equalizer.h"

/* ── Settings persistence ────────────────────────────────────────────────── */
#define SETTINGS_PATH    "ux0:data/VitaWave/settings.dat"
#define SETTINGS_MAGIC   0x56575354u   /* "VWST" */
#define SETTINGS_VERSION 2u

typedef struct {
    uint32_t magic;
    uint32_t version;
    /* UISettings */
    int32_t  theme;
    int32_t  show_album_art;
    int32_t  crossfade;
    int32_t  crossfade_duration;
    int32_t  equalizer_enabled;
    int32_t  gif_reactive;
    /* AudioEngine */
    int32_t  repeat_mode;
    int32_t  shuffle;
} SettingsFile;

static void ui_settings_save(const UIState *ui, const AudioEngine *engine)
{
    FILE *f = fopen(SETTINGS_PATH, "wb");
    if (!f) return;
    SettingsFile s;
    s.magic              = SETTINGS_MAGIC;
    s.version            = SETTINGS_VERSION;
    s.theme              = ui->settings.theme;
    s.show_album_art     = ui->settings.show_album_art  ? 1 : 0;
    s.crossfade          = ui->settings.crossfade       ? 1 : 0;
    s.crossfade_duration = ui->settings.crossfade_duration;
    s.equalizer_enabled  = ui->settings.equalizer_enabled ? 1 : 0;
    s.gif_reactive       = ui->settings.gif_reactive       ? 1 : 0;
    s.repeat_mode        = engine ? (int32_t)engine->repeat_mode : 0;
    s.shuffle            = engine ? (engine->shuffle    ? 1 : 0) : 0;
    fwrite(&s, sizeof(s), 1, f);
    fclose(f);
}

static void ui_settings_load(UIState *ui, AudioEngine *engine)
{
    FILE *f = fopen(SETTINGS_PATH, "rb");
    if (!f) return;
    SettingsFile s;
    if (fread(&s, sizeof(s), 1, f) != 1 ||
        s.magic != SETTINGS_MAGIC || s.version != SETTINGS_VERSION) {
        fclose(f);
        return;
    }
    fclose(f);
    ui->settings.theme               = s.theme;
    ui->settings.show_album_art      = s.show_album_art     != 0;
    ui->settings.crossfade           = s.crossfade          != 0;
    ui->settings.crossfade_duration  = s.crossfade_duration;
    ui->settings.equalizer_enabled   = s.equalizer_enabled  != 0;
    ui->settings.gif_reactive        = s.gif_reactive        != 0;
    if (engine) {
        engine->repeat_mode = (RepeatMode)s.repeat_mode;
        engine->shuffle     = s.shuffle != 0;
        engine->crossfade_enabled  = ui->settings.crossfade;
        engine->crossfade_duration = (float)ui->settings.crossfade_duration;
    }
}

/* ── Visible rows in list views ──────────────────────────────────────────── */
#define LIST_VISIBLE_ROWS  14
#define LIST_ROW_HEIGHT    30
#define LIST_START_Y       (BAR_HEIGHT + 8)
/* Vertical offset inside a row to baseline of medium-size text (~23px tall) */
#define ROW_TEXT_BASELINE  22

/* ── Button edge-detection helper ────────────────────────────────────────── */
static inline bool btn_pressed(const UIState *ui, uint32_t btn)
{
    return (ui->prev_pad.buttons & btn) == 0 &&
           /* current buttons are stored in a local inside ui_handle_input */ true;
}

/* ── Draw helpers (forward declarations) ────────────────────────────────── */
static void draw_header(const UIState *ui);
static void draw_footer(const UIState *ui, const char *hints);
static void draw_theme_bg(const UIState *ui, UIScreen screen);

/* ── ui_init ─────────────────────────────────────────────────────────────── */

int ui_init(UIState *ui)
{
    if (!ui) return -1;
    memset(ui, 0, sizeof(*ui));

    ui->current_screen = UI_SCREEN_BROWSER;
    ui->prev_screen    = UI_SCREEN_BROWSER;
    ui->list_offset    = 0;
    ui->list_selected  = 0;
    ui->show_visualizer = false;
    ui->vis_mode        = VIS_MODE_BARS;

    ui->settings.show_album_art      = true;
    ui->settings.crossfade           = false;
    ui->settings.crossfade_duration  = 3;
    ui->settings.equalizer_enabled   = false;
    ui->settings.gif_reactive        = true;
    ui->settings.theme               = 0;
    ui->settings.settings_selected   = 0;

    ui->repeat_initial_delay = 30;   /* frames */
    ui->repeat_rate          = 6;
    ui->repeat_timer         = 0;

    /* Initialise Noto multi-font renderer.
     * Sizes are in pixels: large=32, medium=23, small=19. */
    noto_init();
    ui->font_large.size  = 32;
    ui->font_medium.size = 23;
    ui->font_small.size  = 19;

    ui->layout.album_art_size = 200;
    ui->layout.corner_radius  = 12;
    ui->layout.list_row_height = 30;
    ui->theme_mgr = NULL;  /* set by main after theme_manager_init */

    ui->last_frame_time  = sceKernelGetProcessTimeWide();
    ui->np_bg_frame      = 0;
    ui->np_bg_last_tick  = ui->last_frame_time;

    ui->eq_selected   = 1;   /* start on first band */
    ui->eq_preset_idx = 0;   /* Flat */

    /* Load saved settings (overwrites defaults if file exists) */
    ui_settings_load(ui, get_audio_engine());
    return 0;
}

/* ── ui_destroy ──────────────────────────────────────────────────────────── */

void ui_destroy(UIState *ui)
{
    if (!ui) return;
    noto_destroy();
}

/* ── ui_switch_screen ────────────────────────────────────────────────────── */

void ui_switch_screen(UIState *ui, UIScreen screen)
{
    if (!ui) return;
    ui->prev_screen    = ui->current_screen;
    ui->current_screen = screen;
    ui->list_offset    = 0;
    /* Don't reset list_selected when going to NOW_PLAYING */
    if (screen != UI_SCREEN_NOW_PLAYING) {
        ui->list_selected = 0;
    }
    /* Sync theme preview index when entering Settings */
    if (screen == UI_SCREEN_SETTINGS && ui->theme_mgr) {
        ui->settings_theme_preview = ui->theme_mgr->current;
    }
}

/* ── ui_update ───────────────────────────────────────────────────────────── */

void ui_update(UIState *ui, Visualizer *vis)
{
    if (!ui) return;

    uint64_t now = sceKernelGetProcessTimeWide();
    uint64_t delta_us = now - ui->last_frame_time;
    if (delta_us > 0) {
        ui->fps = 1000000.0f / (float)delta_us;
    }
    ui->last_frame_time = now;
    ui->anim_frame++;

    /* Advance now_playing animated background frame */
    if (ui->theme_mgr) {
        Theme *ct = theme_current(ui->theme_mgr);
        if (ct && ct->bg.now_playing.frame_count > 1) {
            uint64_t elapsed_ms = (now - ui->np_bg_last_tick) / 1000ULL;
            int delay = ct->bg.now_playing.delays_ms[ui->np_bg_frame];
            if (delay <= 0) delay = 100;

            /* Reactive mode: scale playback speed by total audio energy */
            if (ui->settings.gif_reactive && vis && vis->smoothed_bands) {
                float energy = 0.0f;
                for (int i = 0; i < NUM_BANDS; i++)
                    energy += vis->smoothed_bands[i];
                /* Normalise: sum of 32 bands at full scale ≈ 800; clamp to 1–5x speed */
                float speed = 1.0f + (energy / 800.0f) * 4.0f;
                if (speed > 5.0f) speed = 5.0f;
                delay = (int)((float)delay / speed);
                if (delay < 16) delay = 16;  /* cap at ~60fps minimum */
            }

            if ((int)elapsed_ms >= delay) {
                ui->np_bg_frame = (ui->np_bg_frame + 1) % ct->bg.now_playing.frame_count;
                ui->np_bg_last_tick = now;
            }
        }
    }
}

/* ── start_rename ────────────────────────────────────────────────────────── */
/* Switch to the custom rename screen for the given playlist.                 */

static void start_rename(UIState *ui, int playlist_idx, const char *current_name)
{
    ui->rename_playlist_idx = playlist_idx;
    strncpy(ui->rename_buf, current_name ? current_name : "",
            sizeof(ui->rename_buf) - 1);
    ui->rename_buf[sizeof(ui->rename_buf) - 1] = '\0';
    /* Place cursor at end of existing name */
    ui->rename_cursor   = (int)strlen(ui->rename_buf);
    ui->rename_char_idx = 0;
    ui_switch_screen(ui, UI_SCREEN_RENAME_PLAYLIST);
}

/* ── ui_handle_input ─────────────────────────────────────────────────────── */

void ui_handle_input(UIState *ui,
                     AudioEngine  *engine,
                     Playlist     *playlist,
                     FileList     *browser,
                     Visualizer   *vis)
{
    if (!ui) return;

    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlReadBufferPositive(0, &pad, 1);

    /* Edge detection */
    uint32_t just_pressed = pad.buttons & ~ui->prev_pad.buttons;
    uint32_t held         = pad.buttons;

    /* Auto-repeat for Up/Down */
    uint32_t nav_bits = SCE_CTRL_UP | SCE_CTRL_DOWN;
    bool doing_repeat = false;
    if (held & nav_bits) {
        ui->repeat_timer++;
        if (ui->repeat_timer == ui->repeat_initial_delay ||
            (ui->repeat_timer > ui->repeat_initial_delay &&
             (ui->repeat_timer % ui->repeat_rate) == 0)) {
            doing_repeat = true;
        }
    } else {
        ui->repeat_timer = 0;
    }

    /* Use just_pressed OR repeat trigger for navigation */
    uint32_t nav_pressed = just_pressed;
    if (doing_repeat) nav_pressed |= (held & nav_bits);

    /* ── Auto-advance: track ended, move to next ── */
    if (engine && engine->auto_advance) {
        audio_engine_next(engine, playlist);
        /* audio_engine_next clears auto_advance and starts the next track.
         * Reload metadata now — track_changed is only set on crossfade paths. */
        TrackMetadata *meta = get_current_meta();
        metadata_free(meta);
        if (engine->current_track[0])
            metadata_load(meta, engine->current_track);
        /* Mask out prev/next so a simultaneous button press in this frame
         * doesn't immediately undo the auto-advance (e.g. REPEAT_ALL
         * prev from index 0 wrapping back to the last track). */
        just_pressed &= ~(uint32_t)(SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
    }

    /* ── Metadata reload when crossfade auto-advances ── */
    if (engine && engine->track_changed) {
        engine->track_changed = false;
        TrackMetadata *meta = get_current_meta();
        metadata_free(meta);
        if (engine->current_track[0])
            metadata_load(meta, engine->current_track);
    }

    /* ── Per-screen input ── */
    switch (ui->current_screen) {

    /* ── LIBRARY / BROWSER (shared input) ────────────────────────────── */
    case UI_SCREEN_LIBRARY:
    case UI_SCREEN_BROWSER: {
        int list_count = browser ? browser->count : 0;

        if (nav_pressed & SCE_CTRL_UP) {
            if (ui->list_selected > 0) {
                ui->list_selected--;
                if (ui->list_selected < ui->list_offset) {
                    ui->list_offset = ui->list_selected;
                }
            }
        }
        if (nav_pressed & SCE_CTRL_DOWN) {
            if (ui->list_selected < list_count - 1) {
                ui->list_selected++;
                if (ui->list_selected >= ui->list_offset + LIST_VISIBLE_ROWS) {
                    ui->list_offset = ui->list_selected - LIST_VISIBLE_ROWS + 1;
                }
            }
        }
        if (just_pressed & SCE_CTRL_CROSS) {
            if (browser && ui->list_selected < browser->count) {
                FileEntry *e = &browser->entries[ui->list_selected];
                if (e->is_directory) {
                    file_browser_navigate_into(browser, e->path);
                    ui->list_selected = 0;
                    ui->list_offset   = 0;
                } else {
                    /* Play the selected file */
                    if (engine) {
                        if (audio_engine_play(engine, e->path) == 0) {
                            /* Switch screen immediately — audio is already
                             * running in the background thread.  Metadata
                             * and playlist are loaded after the switch so
                             * the UI thread doesn't block before rendering
                             * the Now Playing screen.                      */
                            ui_switch_screen(ui, UI_SCREEN_NOW_PLAYING);

                            /* Load metadata for the Now Playing screen */
                            TrackMetadata *meta = get_current_meta();
                            metadata_free(meta);
                            metadata_load(meta, e->path);

                            /* Rebuild playlist from current directory */
                            if (playlist) {
                                playlist_clear(playlist);
                                /* Name the queue after the directory basename */
                                const char *_ds = strrchr(browser->current_dir, '/');
                                strncpy(playlist->name,
                                        _ds ? _ds + 1 : browser->current_dir,
                                        MAX_PLAYLIST_NAME - 1);
                                playlist->name[MAX_PLAYLIST_NAME - 1] = '\0';
                                playlist_add_directory(playlist,
                                                       browser->current_dir);
                                /* Update playlist entry with known metadata */
                                for (int i = 0; i < playlist->count; i++) {
                                    if (strcmp(playlist->entries[i].filepath,
                                               e->path) == 0) {
                                        playlist_set_index(playlist, i);
                                        if (meta->title[0])
                                            strncpy(playlist->entries[i].title,
                                                    meta->title,
                                                    sizeof(playlist->entries[i].title) - 1);
                                        if (meta->artist[0])
                                            strncpy(playlist->entries[i].artist,
                                                    meta->artist,
                                                    sizeof(playlist->entries[i].artist) - 1);
                                        playlist->entries[i].duration_ms = meta->duration_ms;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        if (just_pressed & SCE_CTRL_CIRCLE) {
            if (browser) file_browser_navigate_up(browser);
            ui->list_selected = 0;
            ui->list_offset   = 0;
        }
        if (just_pressed & SCE_CTRL_TRIANGLE) {
            ui_switch_screen(ui, UI_SCREEN_PLAYLIST_LIST);
        }
        if (just_pressed & SCE_CTRL_SQUARE) {
            ui_switch_screen(ui, UI_SCREEN_VISUALIZER);
        }
        if (just_pressed & SCE_CTRL_LTRIGGER) {
            /* Add selected file to a user playlist */
            if (browser && ui->list_selected < browser->count) {
                FileEntry *e = &browser->entries[ui->list_selected];
                if (!e->is_directory) {
                    strncpy(ui->pending_add_path, e->path, sizeof(ui->pending_add_path) - 1);
                    ui->pending_add_path[sizeof(ui->pending_add_path) - 1] = '\0';
                    /* Save browser scroll position to restore on return */
                    ui->browser_saved_selected = ui->list_selected;
                    ui->browser_saved_offset   = ui->list_offset;
                    ui->playlist_mgr_selected  = 0;
                    ui_switch_screen(ui, UI_SCREEN_ADD_TO_PLAYLIST);
                }
            }
        }
        if (just_pressed & SCE_CTRL_START) {
            ui_switch_screen(ui, UI_SCREEN_SETTINGS);
        }
        if (just_pressed & SCE_CTRL_SELECT) {
            if (engine && engine->current_track[0])
                ui_switch_screen(ui, UI_SCREEN_NOW_PLAYING);
        }
        if (just_pressed & SCE_CTRL_RIGHT) {
            if (engine && playlist) {
                audio_engine_next(engine, playlist);
                TrackMetadata *meta = get_current_meta();
                metadata_free(meta);
                if (engine->current_track[0]) metadata_load(meta, engine->current_track);
            }
        }
        if (just_pressed & SCE_CTRL_LEFT) {
            if (engine && playlist) {
                audio_engine_prev(engine, playlist);
                TrackMetadata *meta = get_current_meta();
                metadata_free(meta);
                if (engine->current_track[0]) metadata_load(meta, engine->current_track);
            }
        }
        break;
    }

    /* ── NOW PLAYING ──────────────────────────────────────────────────── */
    case UI_SCREEN_NOW_PLAYING: {
        if (just_pressed & SCE_CTRL_CROSS) {
            if (engine) audio_engine_pause(engine);
        }
        if (just_pressed & SCE_CTRL_LEFT) {
            if (engine && playlist) {
                audio_engine_prev(engine, playlist);
                TrackMetadata *meta = get_current_meta();
                metadata_free(meta);
                if (engine->current_track[0]) metadata_load(meta, engine->current_track);
            }
        }
        if (just_pressed & SCE_CTRL_RIGHT) {
            if (engine && playlist) {
                audio_engine_next(engine, playlist);
                TrackMetadata *meta = get_current_meta();
                metadata_free(meta);
                if (engine->current_track[0]) metadata_load(meta, engine->current_track);
            }
        }
        if (held & SCE_CTRL_UP) {
            if (engine) {
                int vol = engine->volume + 256;
                if (vol > MAX_VOLUME) vol = MAX_VOLUME;
                audio_engine_set_volume(engine, vol);
            }
        }
        if (held & SCE_CTRL_DOWN) {
            if (engine) {
                int vol = engine->volume - 256;
                if (vol < 0) vol = 0;
                audio_engine_set_volume(engine, vol);
            }
        }
        if (just_pressed & SCE_CTRL_SQUARE) {
            if (vis) {
                int mode = (int)vis->mode + 1;
                if (mode >= VIS_MODE_DISABLED) mode = 0;
                visualizer_set_mode(vis, (VisMode)mode);
            }
        }
        if (just_pressed & SCE_CTRL_TRIANGLE) {
            ui_switch_screen(ui, UI_SCREEN_PLAYLIST);
            /* Auto-scroll queue to currently playing track */
            if (playlist && playlist->current_index >= 0) {
                ui->list_selected = playlist->current_index;
                int half = LIST_VISIBLE_ROWS / 2;
                ui->list_offset = ui->list_selected - half;
                if (ui->list_offset < 0) ui->list_offset = 0;
            }
        }
        if (just_pressed & SCE_CTRL_CIRCLE) {
            ui_switch_screen(ui, ui->prev_screen == UI_SCREEN_BROWSER
                                ? UI_SCREEN_BROWSER : UI_SCREEN_LIBRARY);
        }
        if (just_pressed & SCE_CTRL_SELECT) {
            /* Cycle repeat mode */
            if (engine) {
                RepeatMode rm = (RepeatMode)(((int)engine->repeat_mode + 1) % 3);
                audio_engine_set_repeat(engine, rm);
                ui_settings_save(ui, engine);
            }
        }
        if (just_pressed & SCE_CTRL_START) {
            if (engine) {
                audio_engine_toggle_shuffle(engine);
                ui_settings_save(ui, engine);
            }
        }
        /* L/R buttons for seek */
        if (just_pressed & SCE_CTRL_LTRIGGER) {
            if (engine) {
                uint64_t pos = audio_engine_get_position(engine);
                audio_engine_seek(engine, pos > 10000 ? pos - 10000 : 0);
            }
        }
        if (just_pressed & SCE_CTRL_RTRIGGER) {
            if (engine) {
                uint64_t pos  = audio_engine_get_position(engine);
                uint64_t dur  = audio_engine_get_duration(engine);
                uint64_t next = pos + 10000;
                if (next < dur) audio_engine_seek(engine, next);
            }
        }
        break;
    }

    /* ── VISUALIZER ───────────────────────────────────────────────────── */
    case UI_SCREEN_VISUALIZER: {
        if (just_pressed & SCE_CTRL_SQUARE) {
            if (vis) {
                int mode = (int)vis->mode + 1;
                if (mode >= VIS_MODE_DISABLED) mode = 0;
                visualizer_set_mode(vis, (VisMode)mode);
            }
        }
        if (just_pressed & SCE_CTRL_CIRCLE) {
            ui_switch_screen(ui, ui->prev_screen);
        }
        if (just_pressed & SCE_CTRL_CROSS) {
            if (engine) audio_engine_pause(engine);
        }
        if (just_pressed & SCE_CTRL_LEFT) {
            if (engine && playlist) {
                audio_engine_prev(engine, playlist);
                TrackMetadata *meta = get_current_meta();
                metadata_free(meta);
                if (engine->current_track[0]) metadata_load(meta, engine->current_track);
            }
        }
        if (just_pressed & SCE_CTRL_RIGHT) {
            if (engine && playlist) {
                audio_engine_next(engine, playlist);
                TrackMetadata *meta = get_current_meta();
                metadata_free(meta);
                if (engine->current_track[0]) metadata_load(meta, engine->current_track);
            }
        }
        break;
    }

    /* ── PLAYLIST ─────────────────────────────────────────────────────── */
    case UI_SCREEN_PLAYLIST: {
        int count = playlist ? playlist->count : 0;
        if (nav_pressed & SCE_CTRL_UP) {
            if (ui->list_selected > 0) {
                ui->list_selected--;
                if (ui->list_selected < ui->list_offset)
                    ui->list_offset = ui->list_selected;
            }
        }
        if (nav_pressed & SCE_CTRL_DOWN) {
            if (ui->list_selected < count - 1) {
                ui->list_selected++;
                if (ui->list_selected >= ui->list_offset + LIST_VISIBLE_ROWS)
                    ui->list_offset = ui->list_selected - LIST_VISIBLE_ROWS + 1;
            }
        }
        if (just_pressed & SCE_CTRL_CROSS) {
            if (playlist && ui->list_selected < count) {
                playlist_set_index(playlist, ui->list_selected);
                PlaylistEntry *entry = playlist_get_current(playlist);
                if (entry && engine) {
                    audio_engine_play(engine, entry->filepath);
                    TrackMetadata *meta = get_current_meta();
                    metadata_free(meta);
                    metadata_load(meta, entry->filepath);
                    ui_switch_screen(ui, UI_SCREEN_NOW_PLAYING);
                }
            }
        }
        if (just_pressed & SCE_CTRL_CIRCLE) {
            ui_switch_screen(ui, ui->prev_screen);
        }
        break;
    }

    /* ── SETTINGS ─────────────────────────────────────────────────────── */
    case UI_SCREEN_SETTINGS: {
        const int NUM_SETTINGS = 7;
        if (nav_pressed & SCE_CTRL_UP) {
            if (ui->settings.settings_selected > 0)
                ui->settings.settings_selected--;
        }
        if (nav_pressed & SCE_CTRL_DOWN) {
            if (ui->settings.settings_selected < NUM_SETTINGS - 1)
                ui->settings.settings_selected++;
        }
        /* Left/right: cycle theme preview when Theme row is selected */
        if (ui->settings.settings_selected == 5 && ui->theme_mgr && ui->theme_mgr->count > 0) {
            if (nav_pressed & SCE_CTRL_LEFT) {
                ui->settings_theme_preview--;
                if (ui->settings_theme_preview < 0)
                    ui->settings_theme_preview = ui->theme_mgr->count - 1;
            }
            if (nav_pressed & SCE_CTRL_RIGHT) {
                ui->settings_theme_preview++;
                if (ui->settings_theme_preview >= ui->theme_mgr->count)
                    ui->settings_theme_preview = 0;
            }
        }
        if (just_pressed & SCE_CTRL_CROSS) {
            switch (ui->settings.settings_selected) {
                case 0: ui->settings.show_album_art    = !ui->settings.show_album_art;    break;
                case 1: ui->settings.crossfade         = !ui->settings.crossfade;
                        if (engine) engine->crossfade_enabled = ui->settings.crossfade;   break;
                case 2:
                    ui->settings.crossfade_duration++;
                    if (ui->settings.crossfade_duration > 10)
                        ui->settings.crossfade_duration = 1;
                    if (engine)
                        engine->crossfade_duration = (float)ui->settings.crossfade_duration;
                    break;
                case 3: ui->current_screen = UI_SCREEN_EQUALIZER; break;
                case 4: ui->settings.gif_reactive      = !ui->settings.gif_reactive;      break;
                case 5:
                    if (ui->theme_mgr) {
                        theme_manager_select(ui->theme_mgr, ui, ui->settings_theme_preview);
                        theme_manager_save(ui->theme_mgr);
                    }
                    break;
                case 6:
                    ((UIState *)ui)->request_exit = true;
                    break;
            }
            ui_settings_save(ui, engine);
        }
        if (just_pressed & SCE_CTRL_CIRCLE) {
            ui_switch_screen(ui, ui->prev_screen);
        }
        break;
    }

    /* ── PLAYLIST_LIST ────────────────────────────────────────────────── */
    case UI_SCREEN_PLAYLIST_LIST: {
        PlaylistManager *mgr = get_playlist_manager();
        int count = mgr ? mgr->count : 0;
        if (nav_pressed & SCE_CTRL_UP) {
            if (ui->playlist_mgr_selected > 0) {
                ui->playlist_mgr_selected--;
                if (ui->playlist_mgr_selected < ui->list_offset)
                    ui->list_offset = ui->playlist_mgr_selected;
            }
        }
        if (nav_pressed & SCE_CTRL_DOWN) {
            if (ui->playlist_mgr_selected < count - 1) {
                ui->playlist_mgr_selected++;
                if (ui->playlist_mgr_selected >= ui->list_offset + LIST_VISIBLE_ROWS)
                    ui->list_offset = ui->playlist_mgr_selected - LIST_VISIBLE_ROWS + 1;
            }
        }
        if (just_pressed & SCE_CTRL_CROSS) {
            /* Open playlist detail */
            if (mgr && ui->playlist_mgr_selected < count) {
                ui->playlist_detail_idx = ui->playlist_mgr_selected;
                ui->list_selected = 0;
                ui->list_offset   = 0;
                ui_switch_screen(ui, UI_SCREEN_PLAYLIST_DETAIL);
            }
        }
        if (just_pressed & SCE_CTRL_TRIANGLE) {
            /* Create new playlist and immediately open keyboard to name it */
            if (mgr) {
                int idx = playlist_manager_new(mgr);
                if (idx >= 0) {
                    ui->playlist_mgr_selected = idx;
                    ui->playlist_detail_idx   = idx;
                    ui->list_selected = 0;
                    ui->list_offset   = 0;
                    ui_switch_screen(ui, UI_SCREEN_PLAYLIST_DETAIL);
                    if (mgr->lists[idx])
                        start_rename(ui, idx, mgr->lists[idx]->name);
                }
            }
        }
        if (just_pressed & SCE_CTRL_SQUARE) {
            /* Delete selected playlist */
            if (mgr && ui->playlist_mgr_selected < count) {
                playlist_manager_delete(mgr, ui->playlist_mgr_selected);
                if (ui->playlist_mgr_selected >= mgr->count && ui->playlist_mgr_selected > 0)
                    ui->playlist_mgr_selected--;
            }
        }
        if (just_pressed & SCE_CTRL_CIRCLE) {
            /* PLAYLIST_LIST is only entered from BROWSER — always go back there */
            ui_switch_screen(ui, UI_SCREEN_BROWSER);
        }
        break;
    }

    /* ── PLAYLIST_DETAIL ──────────────────────────────────────────────── */
    case UI_SCREEN_PLAYLIST_DETAIL: {
        PlaylistManager *mgr = get_playlist_manager();
        Playlist *pl = (mgr && ui->playlist_detail_idx < mgr->count)
                       ? mgr->lists[ui->playlist_detail_idx] : NULL;
        int count = pl ? pl->count : 0;

        if (nav_pressed & SCE_CTRL_UP) {
            if (ui->list_selected > 0) {
                ui->list_selected--;
                if (ui->list_selected < ui->list_offset)
                    ui->list_offset = ui->list_selected;
            }
        }
        if (nav_pressed & SCE_CTRL_DOWN) {
            if (ui->list_selected < count - 1) {
                ui->list_selected++;
                if (ui->list_selected >= ui->list_offset + LIST_VISIBLE_ROWS)
                    ui->list_offset = ui->list_selected - LIST_VISIBLE_ROWS + 1;
            }
        }
        if (just_pressed & SCE_CTRL_CROSS) {
            /* Play from selected track: copy playlist to g_playlist and start */
            if (pl && engine && ui->list_selected < count) {
                Playlist *gpl = get_playlist();
                if (gpl) {
                    playlist_clear(gpl);
                    strncpy(gpl->name, pl->name, MAX_PLAYLIST_NAME - 1);
                    gpl->name[MAX_PLAYLIST_NAME - 1] = '\0';
                    for (int i = 0; i < pl->count; i++) {
                        playlist_add_track(gpl, pl->entries[i].filepath,
                                           pl->entries[i].title,
                                           pl->entries[i].artist,
                                           pl->entries[i].duration_ms);
                    }
                    playlist_set_index(gpl, ui->list_selected);
                    PlaylistEntry *entry = playlist_get_current(gpl);
                    if (entry) {
                        audio_engine_play(engine, entry->filepath);
                        TrackMetadata *meta = get_current_meta();
                        metadata_free(meta);
                        metadata_load(meta, entry->filepath);
                        ui_switch_screen(ui, UI_SCREEN_NOW_PLAYING);
                    }
                }
            }
        }
        if (just_pressed & SCE_CTRL_SQUARE) {
            /* Remove selected track */
            if (pl && ui->list_selected < count) {
                playlist_remove_track(pl, ui->list_selected);
                if (ui->list_selected >= pl->count && ui->list_selected > 0)
                    ui->list_selected--;
                playlist_save(pl);
            }
        }
        if (just_pressed & SCE_CTRL_LTRIGGER) {
            /* Move track up */
            if (pl && ui->list_selected > 0) {
                playlist_move_track(pl, ui->list_selected, ui->list_selected - 1);
                ui->list_selected--;
                if (ui->list_selected < ui->list_offset)
                    ui->list_offset = ui->list_selected;
                playlist_save(pl);
            }
        }
        if (just_pressed & SCE_CTRL_RTRIGGER) {
            /* Move track down */
            if (pl && ui->list_selected < count - 1) {
                playlist_move_track(pl, ui->list_selected, ui->list_selected + 1);
                ui->list_selected++;
                if (ui->list_selected >= ui->list_offset + LIST_VISIBLE_ROWS)
                    ui->list_offset = ui->list_selected - LIST_VISIBLE_ROWS + 1;
                playlist_save(pl);
            }
        }
        if (just_pressed & SCE_CTRL_TRIANGLE) {
            /* Rename playlist via on-screen keyboard */
            if (pl)
                start_rename(ui, ui->playlist_detail_idx, pl->name);
        }
        if (just_pressed & SCE_CTRL_CIRCLE) {
            /* Go back to list WITHOUT overwriting prev_screen, so that
             * Circle on PLAYLIST_LIST can still return to BROWSER. */
            ui->list_selected  = ui->playlist_mgr_selected;
            ui->list_offset    = 0;
            ui->current_screen = UI_SCREEN_PLAYLIST_LIST;
        }
        break;
    }

    /* ── ADD_TO_PLAYLIST ──────────────────────────────────────────────── */
    case UI_SCREEN_ADD_TO_PLAYLIST: {
        PlaylistManager *mgr = get_playlist_manager();
        int count = mgr ? mgr->count : 0;

        if (nav_pressed & SCE_CTRL_UP) {
            if (ui->playlist_mgr_selected > 0) {
                ui->playlist_mgr_selected--;
                if (ui->playlist_mgr_selected < ui->list_offset)
                    ui->list_offset = ui->playlist_mgr_selected;
            }
        }
        if (nav_pressed & SCE_CTRL_DOWN) {
            if (ui->playlist_mgr_selected < count - 1) {
                ui->playlist_mgr_selected++;
                if (ui->playlist_mgr_selected >= ui->list_offset + LIST_VISIBLE_ROWS)
                    ui->list_offset = ui->playlist_mgr_selected - LIST_VISIBLE_ROWS + 1;
            }
        }
        if (just_pressed & SCE_CTRL_CROSS) {
            /* Toggle: add to (or remove from) selected playlist and stay open */
            if (mgr && ui->playlist_mgr_selected < count && ui->pending_add_path[0]) {
                Playlist *pl = mgr->lists[ui->playlist_mgr_selected];
                /* Check if track already in this playlist */
                bool already_in = false;
                for (int ki = 0; ki < pl->count; ki++) {
                    if (strcmp(pl->entries[ki].filepath, ui->pending_add_path) == 0) {
                        already_in = true;
                        break;
                    }
                }
                if (already_in) {
                    /* Remove it */
                    for (int ki = 0; ki < pl->count; ki++) {
                        if (strcmp(pl->entries[ki].filepath, ui->pending_add_path) == 0) {
                            playlist_remove_track(pl, ki);
                            break;
                        }
                    }
                } else {
                    TrackMetadata _atm;
                    metadata_load_tags(&_atm, ui->pending_add_path);
                    playlist_add_track(pl, ui->pending_add_path,
                                       _atm.title, _atm.artist, _atm.duration_ms);
                }
                playlist_save(pl);
                /* Stay open for multi-playlist selection */
            }
        }
        if (just_pressed & SCE_CTRL_TRIANGLE) {
            /* Create new playlist, add track, open keyboard to name it */
            if (mgr && ui->pending_add_path[0]) {
                int idx = playlist_manager_new(mgr);
                if (idx >= 0) {
                    TrackMetadata _atm2;
                    metadata_load_tags(&_atm2, ui->pending_add_path);
                    playlist_add_track(mgr->lists[idx], ui->pending_add_path,
                                       _atm2.title, _atm2.artist, _atm2.duration_ms);
                    ui->playlist_mgr_selected = idx;
                    if (mgr->lists[idx])
                        start_rename(ui, idx, mgr->lists[idx]->name);
                }
            }
        }
        if (just_pressed & SCE_CTRL_CIRCLE) {
            ui->pending_add_path[0] = '\0';
            ui_switch_screen(ui, UI_SCREEN_BROWSER);
            /* Restore the exact song the user was on when they pressed L */
            ui->list_selected = ui->browser_saved_selected;
            ui->list_offset   = ui->browser_saved_offset;
        }
        break;
    }

    /* ── RENAME_PLAYLIST ──────────────────────────────────────────────── */
    case UI_SCREEN_RENAME_PLAYLIST: {
        /* Character set: space + A-Z + 0-9 + hyphen */
        static const char CHARSET[] =
            " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
        const int CHARSET_LEN = (int)(sizeof(CHARSET) - 1);
        int name_len = (int)strlen(ui->rename_buf);

        if (nav_pressed & SCE_CTRL_UP) {
            /* Cycle character at cursor upward */
            if (ui->rename_cursor < name_len) {
                char c = ui->rename_buf[ui->rename_cursor];
                /* find current in charset */
                int ci = 0;
                for (int k = 0; k < CHARSET_LEN; k++) {
                    if (CHARSET[k] == c) { ci = k; break; }
                }
                ci = (ci + 1) % CHARSET_LEN;
                ui->rename_buf[ui->rename_cursor] = CHARSET[ci];
            }
        }
        if (nav_pressed & SCE_CTRL_DOWN) {
            if (ui->rename_cursor < name_len) {
                char c = ui->rename_buf[ui->rename_cursor];
                int ci = 0;
                for (int k = 0; k < CHARSET_LEN; k++) {
                    if (CHARSET[k] == c) { ci = k; break; }
                }
                ci = (ci - 1 + CHARSET_LEN) % CHARSET_LEN;
                ui->rename_buf[ui->rename_cursor] = CHARSET[ci];
            }
        }
        if (just_pressed & SCE_CTRL_RIGHT) {
            if (ui->rename_cursor < name_len) {
                ui->rename_cursor++;
            } else if (name_len < (int)sizeof(ui->rename_buf) - 1) {
                /* Extend name with a space */
                ui->rename_buf[name_len]     = 'A';
                ui->rename_buf[name_len + 1] = '\0';
                ui->rename_cursor = name_len + 1;
                if (ui->rename_cursor >= (int)sizeof(ui->rename_buf) - 1)
                    ui->rename_cursor = (int)sizeof(ui->rename_buf) - 2;
            }
        }
        if (just_pressed & SCE_CTRL_LEFT) {
            if (ui->rename_cursor > 0)
                ui->rename_cursor--;
        }
        if (just_pressed & SCE_CTRL_SQUARE) {
            /* Backspace: delete char before cursor */
            if (ui->rename_cursor > 0) {
                memmove(&ui->rename_buf[ui->rename_cursor - 1],
                        &ui->rename_buf[ui->rename_cursor],
                        name_len - ui->rename_cursor + 1);
                ui->rename_cursor--;
            }
        }
        if (just_pressed & SCE_CTRL_CROSS) {
            /* Confirm: apply name */
            PlaylistManager *mgr = get_playlist_manager();
            if (mgr && ui->rename_playlist_idx < mgr->count) {
                Playlist *pl = mgr->lists[ui->rename_playlist_idx];
                if (pl) {
                    /* Delete old .m3u if name is changing */
                    char old_path[512];
                    snprintf(old_path, sizeof(old_path), "%s%s.m3u",
                             PLAYLIST_SAVE_DIR, pl->name);
                    sceIoRemove(old_path);
                    strncpy(pl->name, ui->rename_buf, MAX_PLAYLIST_NAME - 1);
                    pl->name[MAX_PLAYLIST_NAME - 1] = '\0';
                    if (pl->name[0] == '\0')
                        strncpy(pl->name, "Untitled", MAX_PLAYLIST_NAME - 1);
                    playlist_save(pl);
                }
            }
            /* Return to whichever screen invoked rename */
            ui->current_screen = ui->prev_screen;
        }
        if (just_pressed & SCE_CTRL_CIRCLE) {
            /* Cancel: save with current (possibly default) name */
            PlaylistManager *mgr = get_playlist_manager();
            if (mgr && ui->rename_playlist_idx < mgr->count) {
                Playlist *pl = mgr->lists[ui->rename_playlist_idx];
                if (pl)
                    playlist_save(pl);
            }
            ui->current_screen = ui->prev_screen;
        }
        break;
    }

    /* ── EQUALIZER ────────────────────────────────────────────────────── */
    case UI_SCREEN_EQUALIZER: {
        AudioEngine *ae  = get_audio_engine();
        Equalizer   *eq  = ae ? ae->eq : NULL;

        static const char EQ_CHARSET[] =
            " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
        const int EQ_CHARSET_LEN = (int)(sizeof(EQ_CHARSET) - 1);

        /* ── Name-entry mode (save new or rename) ── */
        if (ui->eq_action_mode == 1 || ui->eq_action_mode == 2) {
            int name_len = (int)strlen(ui->eq_name_buf);
            if (nav_pressed & SCE_CTRL_UP) {
                if (ui->eq_name_cursor < name_len) {
                    char c = ui->eq_name_buf[ui->eq_name_cursor];
                    int ci = 0;
                    for (int k = 0; k < EQ_CHARSET_LEN; k++)
                        if (EQ_CHARSET[k] == c) { ci = k; break; }
                    ci = (ci + 1) % EQ_CHARSET_LEN;
                    ui->eq_name_buf[ui->eq_name_cursor] = EQ_CHARSET[ci];
                }
            }
            if (nav_pressed & SCE_CTRL_DOWN) {
                if (ui->eq_name_cursor < name_len) {
                    char c = ui->eq_name_buf[ui->eq_name_cursor];
                    int ci = 0;
                    for (int k = 0; k < EQ_CHARSET_LEN; k++)
                        if (EQ_CHARSET[k] == c) { ci = k; break; }
                    ci = (ci - 1 + EQ_CHARSET_LEN) % EQ_CHARSET_LEN;
                    ui->eq_name_buf[ui->eq_name_cursor] = EQ_CHARSET[ci];
                }
            }
            if (just_pressed & SCE_CTRL_RIGHT) {
                if (ui->eq_name_cursor < name_len) {
                    ui->eq_name_cursor++;
                } else if (name_len < EQ_CUSTOM_NAME_LEN - 1) {
                    ui->eq_name_buf[name_len]     = 'A';
                    ui->eq_name_buf[name_len + 1] = '\0';
                    ui->eq_name_cursor = name_len + 1;
                    if (ui->eq_name_cursor >= EQ_CUSTOM_NAME_LEN - 1)
                        ui->eq_name_cursor = EQ_CUSTOM_NAME_LEN - 2;
                }
            }
            if (just_pressed & SCE_CTRL_LEFT) {
                if (ui->eq_name_cursor > 0) ui->eq_name_cursor--;
            }
            if (just_pressed & SCE_CTRL_SQUARE) {
                /* Backspace */
                if (ui->eq_name_cursor > 0) {
                    memmove(&ui->eq_name_buf[ui->eq_name_cursor - 1],
                            &ui->eq_name_buf[ui->eq_name_cursor],
                            (size_t)(name_len - ui->eq_name_cursor + 1));
                    ui->eq_name_cursor--;
                }
            }
            if (just_pressed & SCE_CTRL_CROSS) {
                /* Confirm: trim trailing spaces, validate, commit */
                int len = (int)strlen(ui->eq_name_buf);
                while (len > 0 && ui->eq_name_buf[len - 1] == ' ') len--;
                ui->eq_name_buf[len] = '\0';
                if (len == 0) snprintf(ui->eq_name_buf, EQ_CUSTOM_NAME_LEN, "Custom");

                if (ui->eq_action_mode == 1) {
                    /* Save new custom preset */
                    EQCustomPreset *p = &ui->eq_custom[ui->eq_custom_count];
                    strncpy(p->name, ui->eq_name_buf, EQ_CUSTOM_NAME_LEN - 1);
                    p->name[EQ_CUSTOM_NAME_LEN - 1] = '\0';
                    if (eq) {
                        for (int i = 0; i < EQ_BANDS; i++) p->gains[i] = eq->gains[i];
                        p->preamp = eq->preamp;
                    }
                    ui->eq_preset_idx = EQ_PRESET_COUNT + ui->eq_custom_count;
                    ui->eq_custom_count++;
                } else {
                    /* Rename existing custom preset */
                    int ci = ui->eq_manage_cursor; /* reused as the target custom idx */
                    strncpy(ui->eq_custom[ci].name, ui->eq_name_buf, EQ_CUSTOM_NAME_LEN - 1);
                    ui->eq_custom[ci].name[EQ_CUSTOM_NAME_LEN - 1] = '\0';
                }
                eq_custom_save(ui->eq_custom, ui->eq_custom_count);
                if (eq) {
                    eq->preset_idx = ui->eq_preset_idx;
                    eq_save(eq);
                }
                ui->eq_action_mode = 0;
            }
            if (just_pressed & SCE_CTRL_CIRCLE) {
                ui->eq_action_mode = 0;
            }
            break;
        }

        /* ── Manage menu mode (rename / delete) ── */
        if (ui->eq_action_mode == 3) {
            if (just_pressed & SCE_CTRL_UP)   ui->eq_manage_cursor = 0;
            if (just_pressed & SCE_CTRL_DOWN) ui->eq_manage_cursor = 1;
            if (just_pressed & SCE_CTRL_CROSS) {
                int ci = ui->eq_preset_idx - EQ_PRESET_COUNT;
                if (ui->eq_manage_cursor == 0) {
                    /* Rename: switch to name-entry mode, reuse eq_manage_cursor as target idx */
                    strncpy(ui->eq_name_buf, ui->eq_custom[ci].name, EQ_CUSTOM_NAME_LEN - 1);
                    ui->eq_name_buf[EQ_CUSTOM_NAME_LEN - 1] = '\0';
                    ui->eq_name_cursor  = (int)strlen(ui->eq_name_buf);
                    ui->eq_manage_cursor = ci;   /* target index for rename confirm */
                    ui->eq_action_mode  = 2;
                } else {
                    /* Delete */
                    int n = ui->eq_custom_count;
                    if (ci >= 0 && ci < n) {
                        memmove(&ui->eq_custom[ci], &ui->eq_custom[ci + 1],
                                (size_t)(n - ci - 1) * sizeof(EQCustomPreset));
                        ui->eq_custom_count--;
                        /* Fix up preset index */
                        if (ui->eq_preset_idx == EQ_PRESET_COUNT + ci) {
                            ui->eq_preset_idx = -1;
                        } else if (ui->eq_preset_idx > EQ_PRESET_COUNT + ci) {
                            ui->eq_preset_idx--;
                        }
                        eq_custom_save(ui->eq_custom, ui->eq_custom_count);
                        if (eq) {
                            eq->preset_idx = ui->eq_preset_idx;
                            eq_save(eq);
                        }
                    }
                    ui->eq_action_mode = 0;
                }
            }
            if (just_pressed & SCE_CTRL_CIRCLE) {
                ui->eq_action_mode = 0;
            }
            break;
        }

        /* ── Normal mode ── */

        /* Left/Right: move selected item */
        if (just_pressed & SCE_CTRL_LEFT) {
            if (ui->eq_selected > 0) ui->eq_selected--;
        }
        if (just_pressed & SCE_CTRL_RIGHT) {
            if (ui->eq_selected <= EQ_BANDS) ui->eq_selected++;
            if (ui->eq_selected > EQ_BANDS)  ui->eq_selected = EQ_BANDS;
        }

        /* Up/Down with repeat: ±0.5 dB */
        float delta = 0.0f;
        if (nav_pressed & SCE_CTRL_UP)   delta = +0.5f;
        if (nav_pressed & SCE_CTRL_DOWN) delta = -0.5f;
        if (delta != 0.0f && eq) {
            if (ui->eq_selected == 0) {
                eq_set_preamp(eq, eq->preamp + delta);
            } else {
                int b = ui->eq_selected - 1;
                eq_set_gain(eq, b, eq->gains[b] + delta);
            }
            ui->eq_preset_idx = -1;
            eq->preset_idx    = -1;
            eq_save(eq);
        }

        /* L/R shoulder: ±3 dB */
        if (just_pressed & SCE_CTRL_LTRIGGER && eq) {
            if (ui->eq_selected == 0)
                eq_set_preamp(eq, eq->preamp - 3.0f);
            else {
                int b = ui->eq_selected - 1;
                eq_set_gain(eq, b, eq->gains[b] - 3.0f);
            }
            ui->eq_preset_idx = -1;
            eq->preset_idx    = -1;
            eq_save(eq);
        }
        if (just_pressed & SCE_CTRL_RTRIGGER && eq) {
            if (ui->eq_selected == 0)
                eq_set_preamp(eq, eq->preamp + 3.0f);
            else {
                int b = ui->eq_selected - 1;
                eq_set_gain(eq, b, eq->gains[b] + 3.0f);
            }
            ui->eq_preset_idx = -1;
            eq->preset_idx    = -1;
            eq_save(eq);
        }

        /* Square: cycle all presets (built-in + custom) */
        if (just_pressed & SCE_CTRL_SQUARE) {
            int total = EQ_PRESET_COUNT + ui->eq_custom_count;
            int next  = (ui->eq_preset_idx < 0) ? 0
                       : (ui->eq_preset_idx + 1) % total;
            ui->eq_preset_idx = next;
            if (eq) {
                if (next < EQ_PRESET_COUNT)
                    eq_apply_preset(eq, next);
                else
                    eq_apply_custom_preset(eq, &ui->eq_custom[next - EQ_PRESET_COUNT]);
                eq->preset_idx = next;
                eq_save(eq);
            }
        }

        /* Cross: save current EQ state as new custom preset */
        if (just_pressed & SCE_CTRL_CROSS) {
            if (ui->eq_custom_count < EQ_CUSTOM_PRESET_MAX) {
                ui->eq_name_buf[0]  = '\0';
                ui->eq_name_cursor  = 0;
                ui->eq_action_mode  = 1;
            }
            /* else: silently ignore — footer hint shows limit */
        }

        /* Start: manage selected custom preset */
        if (just_pressed & SCE_CTRL_START) {
            if (ui->eq_preset_idx >= EQ_PRESET_COUNT &&
                ui->eq_preset_idx < EQ_PRESET_COUNT + ui->eq_custom_count) {
                ui->eq_manage_cursor = 0;
                ui->eq_action_mode   = 3;
            }
        }

        /* Triangle: toggle on/off */
        if (just_pressed & SCE_CTRL_TRIANGLE && eq) {
            eq->enabled = !eq->enabled;
            ui->settings.equalizer_enabled = eq->enabled;
            eq->preset_idx = ui->eq_preset_idx;
            eq_save(eq);
        }

        /* Circle: back to settings — set directly to avoid overwriting prev_screen */
        if (just_pressed & SCE_CTRL_CIRCLE)
            ui->current_screen = UI_SCREEN_SETTINGS;

        break;
    }

    default:
        break;
    }

    ui->prev_pad = pad;
}

/* ── draw_rounded_rect ───────────────────────────────────────────────────── */
/* Draws a filled rounded rectangle using three rects + four corner circles. */
static void draw_rounded_rect(float x, float y, float w, float h,
                              float r, unsigned int color)
{
    vita2d_draw_rectangle(x + r, y,         w - 2*r, h,       color);
    vita2d_draw_rectangle(x,     y + r,     r,       h - 2*r, color);
    vita2d_draw_rectangle(x+w-r, y + r,     r,       h - 2*r, color);
    vita2d_draw_fill_circle(x + r,     y + r,     r, color);
    vita2d_draw_fill_circle(x + w - r, y + r,     r, color);
    vita2d_draw_fill_circle(x + r,     y + h - r, r, color);
    vita2d_draw_fill_circle(x + w - r, y + h - r, r, color);
}

/* ── ui_draw_progress_bar ────────────────────────────────────────────────── */

void ui_draw_progress_bar(int x, int y, int w, int h,
                          float fraction, uint32_t color_fg, uint32_t color_bg)
{
    /* Background track */
    vita2d_draw_rectangle(x, y, w, h, color_bg);

    /* Filled portion */
    if (fraction > 0.0f) {
        if (fraction > 1.0f) fraction = 1.0f;
        int fill_w = (int)(fraction * w);
        if (fill_w > 0) {
            vita2d_draw_rectangle(x, y, fill_w, h, color_fg);
        }
    }
}

/* ── draw_text_scroll ────────────────────────────────────────────────────── */
/* Draw text clipped to max_w pixels.  When active=true and text overflows,
 * it scrolls left using ui->anim_frame as the clock.  When active=false,
 * overflow is silently clipped (no movement). */
static void draw_text_scroll(const UIState *ui, int x, int y, int max_w,
                             uint32_t color, unsigned int size, const char *text,
                             bool active)
{
    if (!text || !*text) return;

    int text_w = noto_text_width(size, text);
    if (text_w <= max_w) {
        noto_draw_text(x, y, color, size, text);
        return;
    }

    int clip_top = y - (int)size - 2;
    int clip_bot = y + (int)size / 4 + 2;

    if (!active) {
        /* Just clip — no animation */
        vita2d_enable_clipping();
        vita2d_set_clip_rectangle(x, clip_top, x + max_w, clip_bot);
        noto_draw_text(x, y, color, size, text);
        vita2d_disable_clipping();
        return;
    }

    /* Scroll parameters */
    static const int PAUSE_FRAMES = 90;   /* hold at beginning */
    static const int SPEED_DIV    = 2;    /* frames per pixel  */
    static const int GAP_PX       = 60;   /* gap between end and start of repeat */

    int cycle_px = text_w + GAP_PX;
    int cycle_fr = PAUSE_FRAMES + cycle_px * SPEED_DIV;
    int f        = ui->anim_frame % cycle_fr;
    int offset   = (f < PAUSE_FRAMES) ? 0 : (f - PAUSE_FRAMES) / SPEED_DIV;

    vita2d_enable_clipping();
    vita2d_set_clip_rectangle(x, clip_top, x + max_w, clip_bot);

    noto_draw_text(x - offset, y, color, size, text);
    if (offset > 0)
        noto_draw_text(x - offset + cycle_px, y, color, size, text);

    vita2d_disable_clipping();
}

/* ── draw_header ─────────────────────────────────────────────────────────── */

static void draw_header(const UIState *ui)
{
    /* Background bar */
    vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, BAR_HEIGHT, COLOR_ACCENT);

    /* App name */
    noto_draw_text(12, 28,
                         COLOR_TEXT, ui->font_medium.size, "VitaWave");

    /* Current time */
    SceDateTime now_dt;
    sceRtcGetCurrentClockLocalTime(&now_dt);
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%02d:%02d",
             now_dt.hour, now_dt.minute);
    int time_w = noto_text_width(ui->font_medium.size, time_str);
    noto_draw_text(
                         SCREEN_WIDTH - 12 - time_w, 28,
                         COLOR_TEXT, ui->font_medium.size, time_str);
}

/* ── draw_button_icon ────────────────────────────────────────────────────── */
/* Draws a PS Vita-style controller button at (cx, cy).                       */
/* sym: 'X'=cross  'O'=circle  'T'=triangle  'S'=square                      */
static void draw_button_icon(int cx, int cy, char sym)
{
    const int R = 9;

    /* Per-button accent colours (vita2d ABGR 0xAABBGGRR) */
    uint32_t bg;
    switch (sym) {
        case 'X': bg = 0xFF9B3838u; break;  /* blue-ish cross   */
        case 'O': bg = 0xFF3838C8u; break;  /* red  circle      */
        case 'T': bg = 0xFF38923Eu; break;  /* green triangle   */
        case 'S': bg = 0xFF923892u; break;  /* pink  square     */
        default:  bg = COLOR_ACCENT; break;
    }

    /* Crisp border ring then coloured fill */
    vita2d_draw_fill_circle(cx, cy, R + 1, COLOR_BG);
    vita2d_draw_fill_circle(cx, cy, R,     bg);

    /* Draw symbol geometrically so we don't rely on font glyph support */
    switch (sym) {
        case 'X': {
            /* Two diagonal lines */
            int d = 5;
            vita2d_draw_line(cx - d, cy - d, cx + d, cy + d, COLOR_TEXT);
            vita2d_draw_line(cx + d, cy - d, cx - d, cy + d, COLOR_TEXT);
            break;
        }
        case 'O': {
            /* Ring: white outer circle, punch out with bg inner */
            vita2d_draw_fill_circle(cx, cy, 5, COLOR_TEXT);
            vita2d_draw_fill_circle(cx, cy, 3, bg);
            break;
        }
        case 'T': {
            /* Triangle: three lines */
            vita2d_draw_line(cx,     cy - 5, cx - 5, cy + 4, COLOR_TEXT);
            vita2d_draw_line(cx - 5, cy + 4, cx + 5, cy + 4, COLOR_TEXT);
            vita2d_draw_line(cx + 5, cy + 4, cx,     cy - 5, COLOR_TEXT);
            break;
        }
        case 'S': {
            /* Square: white 8x8 outer, 6x6 punch-out — both centred on (cx,cy) */
            vita2d_draw_rectangle(cx - 4, cy - 4, 8, 8, COLOR_TEXT);
            vita2d_draw_rectangle(cx - 3, cy - 3, 6, 6, bg);
            break;
        }
        default: break;
    }
}

/* ── draw_dpad_icon ──────────────────────────────────────────────────────── */
/* Draws a D-pad cross.  Pass true for each direction that should be lit.     */
static void draw_dpad_icon(int cx, int cy,
                            bool up, bool down, bool left, bool right)
{
    uint32_t dim  = 0xFF4A4A4Au;  /* unlit arm   */
    uint32_t lit  = COLOR_TEXT;   /* highlighted */
    uint32_t ctr  = 0xFF3A3A3Au;  /* centre nub  */

    /* Four 7×7 arms with a 1-px gap around the 8×8 centre square.
     * Total footprint: 22×22 px centred on (cx, cy).                */
    vita2d_draw_rectangle(cx - 3, cy - 11, 7, 7, up    ? lit : dim); /* ↑ */
    vita2d_draw_rectangle(cx - 3, cy +  4, 7, 7, down  ? lit : dim); /* ↓ */
    vita2d_draw_rectangle(cx - 11, cy - 3, 7, 7, left  ? lit : dim); /* ← */
    vita2d_draw_rectangle(cx +  4, cy - 3, 7, 7, right ? lit : dim); /* → */
    vita2d_draw_rectangle(cx -  4, cy - 4, 8, 8, ctr);               /* ● */
}

/* ── draw_pill_icon ──────────────────────────────────────────────────────── */
/* Draws a horizontal pill/oval with a short ALL-CAPS label inside.           */
/* Used for SELECT ([SEL]) and START ([STA]) which are labelled buttons on    */
/* the Vita, not the geometric face buttons.                                  */
static void draw_pill_icon(const UIState *ui, int cx, int cy, int text_y,
                            const char *label)
{
    const int R    = 8;   /* pill half-height */
    const int HALF = 11;  /* half of the flat centre span */
    uint32_t  bg   = 0xFF585858u;  /* neutral dark-gray */

    /* Border ring */
    vita2d_draw_fill_circle(cx - HALF, cy, R + 1, COLOR_BG);
    vita2d_draw_fill_circle(cx + HALF, cy, R + 1, COLOR_BG);
    vita2d_draw_rectangle(cx - HALF, cy - R - 1, HALF * 2, (R + 1) * 2, COLOR_BG);

    /* Filled pill */
    vita2d_draw_fill_circle(cx - HALF, cy, R, bg);
    vita2d_draw_fill_circle(cx + HALF, cy, R, bg);
    vita2d_draw_rectangle(cx - HALF, cy - R, HALF * 2, R * 2, bg);

    /* Centred label */
    int tw = noto_text_width(ui->font_small.size, label);
    noto_draw_text(cx - tw / 2, text_y,
                         COLOR_TEXT, ui->font_small.size, label);
}

/* ── draw_footer ─────────────────────────────────────────────────────────── */
/* Hint strings use [X] [O] [T] [S] for face-button icons,                   */
/* [SEL] and [STA] for the SELECT / START pill icons; plain text otherwise.   */

static void draw_footer(const UIState *ui, const char *hints)
{
    int fy = SCREEN_HEIGHT - BAR_HEIGHT;
    vita2d_draw_rectangle(0, fy, SCREEN_WIDTH, BAR_HEIGHT, COLOR_ACCENT);
    if (!hints) return;

    const int ICON_DIAM = 22;   /* face-button icon slot width  */
    const int PILL_SLOT = 46;   /* SELECT / START pill slot width */
    const int ICON_CY   = fy + BAR_HEIGHT / 2;
    const int TEXT_Y    = fy + 26;  /* pgf baseline */

    int x = 10;
    const char *p = hints;

    /* Accumulate plain-text runs, flush when a token is encountered */
    char tbuf[128];
    int  tlen = 0;

    while (*p) {
#define FLUSH_TEXT() do { \
    if (tlen > 0) { \
        tbuf[tlen] = '\0'; \
        noto_draw_text(x, TEXT_Y, \
                             COLOR_TEXT_DIM, ui->font_small.size, tbuf); \
        x += noto_text_width(\
                                   ui->font_small.size, tbuf); \
        tlen = 0; \
    } \
} while(0)

        /* 5-char tokens: [SEL], [STA], [DUD], [DLR] */
        if (p[0]=='[' && p[4]==']') {
            if ((p[1]=='S'&&p[2]=='E'&&p[3]=='L') ||
                (p[1]=='S'&&p[2]=='T'&&p[3]=='A')) {
                FLUSH_TEXT();
                char label[4] = { p[1], p[2], p[3], '\0' };
                draw_pill_icon(ui, x + PILL_SLOT / 2, ICON_CY, TEXT_Y, label);
                x += PILL_SLOT; p += 5;
            } else if (p[1]=='D' && p[2]=='U' && p[3]=='D') {
                FLUSH_TEXT();
                draw_dpad_icon(x + 13, ICON_CY, true, true, false, false);
                x += 26; p += 5;
            } else if (p[1]=='D' && p[2]=='L' && p[3]=='R') {
                FLUSH_TEXT();
                draw_dpad_icon(x + 13, ICON_CY, false, false, true, true);
                x += 26; p += 5;
            } else { goto footer_plain; }

        /* 4-char D-pad tokens: [DU], [DD], [DL], [DR] */
        } else if (p[0]=='[' && p[3]==']' && p[1]=='D') {
            FLUSH_TEXT();
            draw_dpad_icon(x + 13, ICON_CY,
                           p[2]=='U', p[2]=='D', p[2]=='L', p[2]=='R');
            x += 26; p += 4;

        /* 3-char face-button tokens: [X], [O], [T], [S] */
        } else if (p[0]=='[' && p[2]==']' &&
                   (p[1]=='X'||p[1]=='O'||p[1]=='T'||p[1]=='S')) {
            FLUSH_TEXT();
            draw_button_icon(x + ICON_DIAM / 2, ICON_CY, p[1]);
            x += ICON_DIAM; p += 3;
        } else {
            footer_plain:
            if (tlen < (int)sizeof(tbuf) - 1)
                tbuf[tlen++] = *p;
            p++;
        }
    }
#undef FLUSH_TEXT
    /* Flush any trailing text */
    if (tlen > 0) {
        tbuf[tlen] = '\0';
        noto_draw_text(x, TEXT_Y,
                             COLOR_TEXT_DIM, ui->font_small.size, tbuf);
    }
}

/* ── draw_text_with_icons ────────────────────────────────────────────────── */
/* Like draw_footer but renders at an arbitrary (x, y) for body text.        */
/* [X], [O], [T], [S] tokens are replaced with the matching button graphic.  */

static void draw_text_with_icons(const UIState *ui,
                                 int x, int baseline_y,
                                 uint32_t color, const char *text)
{
    if (!ui || !text) return;

    const int ICON_R    = 9;            /* same radius as footer icons */
    const int ICON_DIAM = (ICON_R + 1) * 2;
    const int ICON_CY   = baseline_y - ICON_R;  /* vertically centred on cap-height */

    char tbuf[128];
    int  tlen = 0;
    const char *p = text;

    while (*p) {
#define FLUSH_BODY() do { \
    if (tlen > 0) { \
        tbuf[tlen] = '\0'; \
        noto_draw_text(x, baseline_y, \
                             color, ui->font_small.size, tbuf); \
        x += noto_text_width(\
                                   ui->font_small.size, tbuf); \
        tlen = 0; \
    } \
} while(0)

        if (p[0]=='[' && p[4]==']') {
            if ((p[1]=='S'&&p[2]=='E'&&p[3]=='L') ||
                (p[1]=='S'&&p[2]=='T'&&p[3]=='A')) {
                FLUSH_BODY();
                const int PS = 46;
                char label[4] = { p[1], p[2], p[3], '\0' };
                draw_pill_icon(ui, x + PS / 2, ICON_CY, baseline_y, label);
                x += PS; p += 5;
            } else if (p[1]=='D' && p[2]=='U' && p[3]=='D') {
                FLUSH_BODY();
                draw_dpad_icon(x + 13, ICON_CY, true, true, false, false);
                x += 26; p += 5;
            } else if (p[1]=='D' && p[2]=='L' && p[3]=='R') {
                FLUSH_BODY();
                draw_dpad_icon(x + 13, ICON_CY, false, false, true, true);
                x += 26; p += 5;
            } else { goto body_plain; }
        } else if (p[0]=='[' && p[3]==']' && p[1]=='D') {
            FLUSH_BODY();
            draw_dpad_icon(x + 13, ICON_CY,
                           p[2]=='U', p[2]=='D', p[2]=='L', p[2]=='R');
            x += 26; p += 4;
        } else if (p[0]=='[' && p[2]==']' &&
                   (p[1]=='X'||p[1]=='O'||p[1]=='T'||p[1]=='S')) {
            FLUSH_BODY();
            draw_button_icon(x + ICON_DIAM / 2, ICON_CY, p[1]);
            x += ICON_DIAM; p += 3;
        } else {
            body_plain:
            if (tlen < (int)sizeof(tbuf) - 1)
                tbuf[tlen++] = *p;
            p++;
        }
    }
#undef FLUSH_BODY
    if (tlen > 0) {
        tbuf[tlen] = '\0';
        noto_draw_text(x, baseline_y,
                             color, ui->font_small.size, tbuf);
    }
}

/* ── ui_draw_file_list ───────────────────────────────────────────────────── */

void ui_draw_file_list(const UIState *ui, const FileList *browser,
                       int x, int y, int w, int h)
{
    if (!ui || !browser) return;

    noto_draw_textf(x + 4, y - 4,
                          COLOR_TEXT_DIM, ui->font_small.size,
                          "%s", browser->current_dir);

    int row_y    = y + 4;
    int max_rows = h / LIST_ROW_HEIGHT;
    if (max_rows > LIST_VISIBLE_ROWS) max_rows = LIST_VISIBLE_ROWS;

    for (int i = 0; i < max_rows; i++) {
        int idx = ui->list_offset + i;
        if (idx >= browser->count) break;

        const FileEntry *e = &browser->entries[idx];
        bool selected = (idx == ui->list_selected);

        if (selected) {
            vita2d_draw_rectangle(x, row_y - 2, w - 20, LIST_ROW_HEIGHT - 2,
                                  COLOR_HIGHLIGHT);
        }

        /* Icon */
        const char *icon = e->is_directory ? ">" : "*";
        noto_draw_textf(
                              x + 6, row_y + ROW_TEXT_BASELINE,
                              selected ? COLOR_PROGRESS : COLOR_TEXT_DIM,
                              ui->font_small.size, "%s", icon);

        /* Name */
        draw_text_scroll(ui, x + 22, row_y + ROW_TEXT_BASELINE,
                         w - 22 - 20,
                         selected ? COLOR_TEXT : COLOR_TEXT_DIM,
                         ui->font_medium.size, e->name, selected);

        row_y += LIST_ROW_HEIGHT;
    }

    /* Scrollbar */
    if (browser->count > max_rows) {
        int sb_x = x + w - 16;
        int sb_h = h - 8;
        int sb_y = y + 4;
        vita2d_draw_rectangle(sb_x, sb_y, 6, sb_h, COLOR_ACCENT);

        float thumb_ratio = (float)max_rows / (float)browser->count;
        float thumb_top   = (float)ui->list_offset / (float)browser->count;
        int   thumb_h     = (int)(thumb_ratio * sb_h);
        int   thumb_y     = sb_y + (int)(thumb_top * sb_h);
        if (thumb_h < 12) thumb_h = 12;
        vita2d_draw_rectangle(sb_x, thumb_y, 6, thumb_h, COLOR_PROGRESS);
    }
}

/* ── ui_draw_track_info ──────────────────────────────────────────────────── */

void ui_draw_track_info(const UIState *ui, const AudioEngine *engine,
                        const Playlist *playlist, int x, int y)
{
    if (!ui || !engine || !playlist) return;

    const PlaylistEntry *entry = NULL;
    if (playlist->current_index >= 0 && playlist->current_index < playlist->count) {
        entry = &playlist->entries[playlist->current_index];
    }

    /* Prefer playlist entry metadata, then current loaded metadata, then filename */
    const char *title  = (entry && entry->title[0])  ? entry->title  : NULL;
    const char *artist = (entry && entry->artist[0]) ? entry->artist : NULL;

    if (!title || !artist) {
        const TrackMetadata *meta = get_current_meta();
        if (meta) {
            if (!title  && meta->title[0])  title  = meta->title;
            if (!artist && meta->artist[0]) artist = meta->artist;
        }
    }

    int info_max_w = SCREEN_WIDTH - x - 10;

    if (title) {
        draw_text_scroll(ui, x, y, info_max_w,
                         COLOR_TEXT, ui->font_medium.size, title, true);
        if (artist) {
            draw_text_scroll(ui, x, y + 22, info_max_w,
                             COLOR_TEXT_DIM, ui->font_small.size, artist, true);
        }
    } else if (engine->current_track[0]) {
        /* Fall back to filename */
        const char *slash = strrchr(engine->current_track, '/');
        const char *fname = slash ? slash + 1 : engine->current_track;
        draw_text_scroll(ui, x, y, info_max_w,
                         COLOR_TEXT, ui->font_medium.size, fname, true);
    }
}

/* ── Repeat / shuffle icon helpers ──────────────────────────────────────── */

/*
 * draw_icon_repeat  — rectangular loop arrow, 20×14 px
 *   Top bar runs left→right with a right-pointing arrowhead on the right.
 *   Bottom bar runs right→left with a left-pointing arrowhead on the left.
 *   Left and right vertical connectors join the bars.
 */
static void draw_icon_repeat(int lx, int cy, unsigned int col)
{
    /* dimensions */
    int w  = 20;  /* outer width  */
    int h  = 14;  /* outer height */
    int bh = 2;   /* bar thickness */
    int aw = 4;   /* arrowhead half-width (pixels from tip to base) */

    int top_y = cy - h / 2;
    int bot_y = cy + h / 2 - bh;

    /* top bar */
    vita2d_draw_rectangle(lx,          top_y, w - aw, bh, col);
    /* bottom bar */
    vita2d_draw_rectangle(lx + aw,     bot_y, w - aw, bh, col);
    /* left connector */
    vita2d_draw_rectangle(lx,          top_y, bh, h, col);
    /* right connector */
    vita2d_draw_rectangle(lx + w - bh, top_y, bh, h, col);

    /* right arrowhead (pointing right, on top bar) */
    for (int i = 0; i <= aw; i++) {
        int tip_x = lx + w - 1;
        vita2d_draw_rectangle(tip_x - aw + i, top_y - i, 1, bh + i * 2, col);
    }

    /* left arrowhead (pointing left, on bottom bar) */
    for (int i = 0; i <= aw; i++) {
        vita2d_draw_rectangle(lx + i, bot_y - (aw - i), 1, bh + (aw - i) * 2, col);
    }
}

/*
 * draw_icon_repeat_one  — same loop + a small "1" digit to its right
 */
static void draw_icon_repeat_one(int lx, int cy, unsigned int col)
{
    draw_icon_repeat(lx, cy, col);

    /* small "1" — vertical bar, 2px wide, 8px tall */
    int ox = lx + 24;
    int oy = cy - 4;
    vita2d_draw_rectangle(ox, oy, 2, 8, col);
    /* tiny serif/hat */
    vita2d_draw_rectangle(ox - 2, oy, 4, 2, col);
}

/*
 * draw_icon_shuffle  — two crossing diagonal arrows, 20×14 px
 *   A: top-left  → bottom-right  (↘ arrowhead at right)
 *   B: bottom-left → top-right   (↗ arrowhead at right)
 *   Line A is broken at the crossing so B passes visually over it.
 */
static void draw_icon_shuffle(int lx, int cy, unsigned int col)
{
    const int LW = 16;   /* diagonal segment width  */
    const int AW = 4;    /* arrowhead depth         */
    const int H  = 14;   /* icon height             */

    int yt = cy - H / 2;
    int yb = cy + H / 2 - 1;

    /* ── Diagonal segments (2 px tall per column) ── */
    for (int i = 0; i < LW; i++) {
        int pyA = yt + (i * (H - 2)) / (LW - 1);
        int pyB = yb - (i * (H - 2)) / (LW - 1);

        /* Line A: break in the middle ~20% so B crosses over it */
        if (i < LW * 2 / 5 || i > LW * 3 / 5)
            vita2d_draw_rectangle(lx + i, pyA,     1, 2, col);

        /* Line B: always drawn */
        vita2d_draw_rectangle(lx + i, pyB - 1, 1, 2, col);
    }

    /* ── Arrowhead A: ↘ — narrows from yb upward toward the tip ── */
    for (int i = 0; i < AW; i++)
        vita2d_draw_rectangle(lx + LW + i, yb - (AW - 1 - i), 1, AW - i, col);

    /* ── Arrowhead B: ↗ — narrows from yt downward toward the tip ── */
    for (int i = 0; i < AW; i++)
        vita2d_draw_rectangle(lx + LW + i, yt, 1, AW - i, col);
}

/* ── ui_draw_now_playing ─────────────────────────────────────────────────── */

void ui_draw_now_playing(const UIState *ui,
                         const AudioEngine *engine,
                         const Playlist   *playlist,
                         Visualizer       *vis)
{
    if (!ui || !engine) return;

    int content_y = BAR_HEIGHT + 10;

    /* ── Album art area ── */
    int art_x = 20;
    int art_y = content_y + 10;
    int art_w = ui->layout.album_art_size;
    int art_h = ui->layout.album_art_size;
    int art_radius = ui->layout.corner_radius;

    /* Use cached metadata loaded when playback started */
    TrackMetadata *meta = get_current_meta();

    if (ui->settings.show_album_art) {
        vita2d_texture *art_tex = meta ? metadata_get_album_art_texture(meta) : NULL;

        /* Rounded background box */
        draw_rounded_rect(art_x, art_y, art_w, art_h, art_radius, COLOR_ALBUM_ART_BG);

        if (art_tex) {
            float sx = (float)art_w / vita2d_texture_get_width (art_tex);
            float sy = (float)art_h / vita2d_texture_get_height(art_tex);
            vita2d_draw_texture_scale(art_tex, art_x, art_y, sx, sy);
        } else {
            /* Music note placeholder */
            noto_draw_text(
                                 art_x + art_w / 2 - 10,
                                 art_y + art_h / 2 + 10,
                                 COLOR_TEXT_DIM, ui->font_large.size, "M");
        }

        /* Album art tint + overlay (drawn before corner overdraw so corners clip them) */
        if (ui->theme_mgr) {
            Theme *ct = theme_current(ui->theme_mgr);
            if (ct) {
                if (ct->colors.album_art_tint & 0xFF000000)
                    vita2d_draw_rectangle(art_x, art_y, art_w, art_h,
                                         ct->colors.album_art_tint);
                if (ct->bg.album_art_overlay) {
                    float sx = (float)art_w / vita2d_texture_get_width (ct->bg.album_art_overlay);
                    float sy = (float)art_h / vita2d_texture_get_height(ct->bg.album_art_overlay);
                    vita2d_draw_texture_scale(ct->bg.album_art_overlay, art_x, art_y, sx, sy);
                }
            }
        }

        /* Round the corners by overpainting the outside-arc region row by row.
           If a bg texture is active, re-blit it so the texture shows through
           rather than a flat solid color. */
        {
            vita2d_texture *corner_bg = NULL;
            if (ui->theme_mgr) {
                Theme *ct = theme_current(ui->theme_mgr);
                if (ct) {
                    corner_bg = theme_animbg_frame(&ct->bg.now_playing, ui->np_bg_frame);
                    if (!corner_bg) corner_bg = ct->bg.fallback;
                }
            }
            /* Scale factors: map screen coords → texture coords.
             * 960×544 textures give 1.0; 480×272 GIF frames give 0.5. */
            float cbg_sx = corner_bg ? (float)vita2d_texture_get_width (corner_bg) / (float)SCREEN_WIDTH  : 1.0f;
            float cbg_sy = corner_bg ? (float)vita2d_texture_get_height(corner_bg) / (float)SCREEN_HEIGHT : 1.0f;
            float cbg_ix = corner_bg ? (float)SCREEN_WIDTH  / (float)vita2d_texture_get_width (corner_bg) : 1.0f;
            float cbg_iy = corner_bg ? (float)SCREEN_HEIGHT / (float)vita2d_texture_get_height(corner_bg) : 1.0f;

            float r = (float)art_radius;
            for (int i = 0; i < art_radius; i++) {
                float dy    = r - 1.0f - (float)i;
                int   cover = art_radius - (int)sqrtf(r * r - dy * dy);
                if (cover <= 0) continue;
                int top_row = art_y + i;
                int bot_row = art_y + art_h - 1 - i;
                if (corner_bg) {
                    /* src coords in texture space; draw scaled back to 1 screen-px tall */
                    float sx_l = (float)art_x               * cbg_sx;
                    float sx_r = (float)(art_x + art_w - cover) * cbg_sx;
                    float sy_t = (float)top_row * cbg_sy;
                    float sy_b = (float)bot_row * cbg_sy;
                    float sw   = (float)cover   * cbg_sx;
                    vita2d_draw_texture_part_scale(corner_bg, (float)art_x,               (float)top_row, sx_l, sy_t, sw, 1.0f, cbg_ix, 1.0f);
                    vita2d_draw_texture_part_scale(corner_bg, (float)art_x,               (float)bot_row, sx_l, sy_b, sw, 1.0f, cbg_ix, 1.0f);
                    vita2d_draw_texture_part_scale(corner_bg, (float)(art_x + art_w - cover), (float)top_row, sx_r, sy_t, sw, 1.0f, cbg_ix, 1.0f);
                    vita2d_draw_texture_part_scale(corner_bg, (float)(art_x + art_w - cover), (float)bot_row, sx_r, sy_b, sw, 1.0f, cbg_ix, 1.0f);
                } else {
                    vita2d_draw_rectangle(art_x,                  top_row, cover, 1, COLOR_BG);
                    vita2d_draw_rectangle(art_x,                  bot_row, cover, 1, COLOR_BG);
                    vita2d_draw_rectangle(art_x + art_w - cover,  top_row, cover, 1, COLOR_BG);
                    vita2d_draw_rectangle(art_x + art_w - cover,  bot_row, cover, 1, COLOR_BG);
                }
            }
        }

        /* Frame drawn last — sits on top of art, overlay, and corners */
        if (ui->theme_mgr) {
            Theme *ct = theme_current(ui->theme_mgr);
            if (ct && ct->bg.album_art_frame) {
                int   fp = ui->layout.album_art_frame_padding;
                float sx = (float)(art_w + 2 * fp) / vita2d_texture_get_width (ct->bg.album_art_frame);
                float sy = (float)(art_h + 2 * fp) / vita2d_texture_get_height(ct->bg.album_art_frame);
                vita2d_draw_texture_scale(ct->bg.album_art_frame,
                                          art_x - fp, art_y - fp, sx, sy);
            }
        }
    }

    /* ── Track text ── */
    /* With art: right of the art box.  Without art: full width from left. */
    /* Vertically centre the 4-line text block (~100px) within art_h area. */
    int text_x = ui->settings.show_album_art ? (art_x + art_w + 20) : art_x;
    int text_y = art_y + (art_h - 100) / 2;

    /* Prefer cached metadata, fall back to playlist entry, then filename */
    const char *title  = "Unknown";
    const char *artist = "Unknown Artist";
    const char *album  = NULL;
    if (meta && meta->title[0])  title  = meta->title;
    else if (engine->current_track[0]) {
        const char *sl = strrchr(engine->current_track, '/');
        title = sl ? sl + 1 : engine->current_track;
    }
    if (meta && meta->artist[0]) artist = meta->artist;
    if (meta && meta->album[0])  album  = meta->album;

    int np_text_max_w = SCREEN_WIDTH - text_x - 10;
    draw_text_scroll(ui, text_x, text_y,
                     np_text_max_w, COLOR_TEXT, ui->font_large.size, title, true);
    draw_text_scroll(ui, text_x, text_y + 28,
                     np_text_max_w, COLOR_TEXT_DIM, ui->font_medium.size, artist, true);
    if (album)
        draw_text_scroll(ui, text_x, text_y + 50,
                         np_text_max_w, COLOR_TEXT_DIM, ui->font_small.size, album, true);

    /* ── State icon + repeat/shuffle row ── */
    {
        int icon_cy = text_y + 82;  /* vertical centre of the icon row */
        int icon_h  = 20;           /* icon height in pixels            */
        int icon_x  = text_x;       /* icon left edge                   */
        PlaybackState ps = audio_engine_get_state(engine);

        if (ps == PLAYBACK_PLAYING) {
            /* Right-pointing filled triangle (scanline) */
            for (int i = 0; i < icon_h; i++) {
                int w = (i <= icon_h / 2) ? i * 2 : (icon_h - i) * 2;
                if (w > 0)
                    vita2d_draw_rectangle(icon_x, icon_cy - icon_h / 2 + i,
                                         w, 1, COLOR_PROGRESS);
            }
        } else if (ps == PLAYBACK_PAUSED) {
            int bar_w = 6, bar_h = icon_h;
            vita2d_draw_rectangle(icon_x,               icon_cy - bar_h / 2,
                                  bar_w, bar_h, COLOR_PROGRESS);
            vita2d_draw_rectangle(icon_x + bar_w + 5,   icon_cy - bar_h / 2,
                                  bar_w, bar_h, COLOR_PROGRESS);
        } else if (ps == PLAYBACK_STOPPED) {
            vita2d_draw_rectangle(icon_x, icon_cy - icon_h / 2,
                                  icon_h, icon_h, COLOR_PROGRESS);
        } else {
            /* Buffering — three dots */
            for (int d = 0; d < 3; d++)
                vita2d_draw_fill_circle(icon_x + d * 10, icon_cy, 3, COLOR_PROGRESS);
        }

        /* Repeat / shuffle badges — right-aligned, same row */
        RepeatMode rm = engine->repeat_mode;
        int bx = SCREEN_WIDTH - 25;   /* right margin */

        if (engine->shuffle) {
            bx -= 20;
            draw_icon_shuffle(bx, icon_cy, COLOR_TEXT_DIM);
            bx -= 8;
        }
        if (rm == REPEAT_ONE) {
            bx -= 26;   /* loop icon (20) + "1" overlay (6) */
            draw_icon_repeat_one(bx, icon_cy, COLOR_TEXT_DIM);
        } else if (rm == REPEAT_ALL) {
            bx -= 20;
            draw_icon_repeat(bx, icon_cy, COLOR_TEXT_DIM);
        }
    }

    /* ── Progress bar ── */
    int pb_x = 20;
    int pb_y = art_y + art_h + 16;
    int pb_w = SCREEN_WIDTH - 40;
    int pb_h = 8;

    uint64_t pos_ms = audio_engine_get_position(engine);
    uint64_t dur_ms = audio_engine_get_duration(engine);
    float    frac   = (dur_ms > 0) ? (float)pos_ms / (float)dur_ms : 0.0f;

    ui_draw_progress_bar(pb_x, pb_y, pb_w, pb_h,
                         frac, COLOR_PROGRESS, COLOR_ACCENT);

    /* Thumb on progress bar — custom texture if theme provides one, else circle */
    {
        int thumb_x = pb_x + (int)(frac * pb_w);
        int thumb_y = pb_y + pb_h / 2;
        vita2d_texture *pthumb = NULL;
        if (ui->theme_mgr) {
            Theme *ct = theme_current(ui->theme_mgr);
            if (ct) pthumb = ct->thumbs.progress;
        }
        if (pthumb) {
            float sz = 16.0f;
            int tw = vita2d_texture_get_width(pthumb);
            int th = vita2d_texture_get_height(pthumb);
            vita2d_draw_texture_scale(pthumb,
                thumb_x - sz / 2.0f, thumb_y - sz / 2.0f,
                sz / (float)tw, sz / (float)th);
        } else {
            vita2d_draw_fill_circle(thumb_x, thumb_y, 8, COLOR_BG);
            vita2d_draw_fill_circle(thumb_x, thumb_y, 7, COLOR_TEXT);
        }
    }

    /* Time labels — 16px below bar bottom */
    char pos_str[16], dur_str[16];
    metadata_format_duration(pos_ms, pos_str, sizeof(pos_str));
    metadata_format_duration(dur_ms, dur_str, sizeof(dur_str));
    int time_y = pb_y + pb_h + 16;
    noto_draw_text(pb_x, time_y,
                         COLOR_TEXT_DIM, ui->font_small.size, pos_str);
    noto_draw_textf(pb_x + pb_w - 40, time_y,
                          COLOR_TEXT_DIM, ui->font_small.size, "%s", dur_str);

    /* ── Volume bar — 28px below time labels ── */
    int vb_y = time_y + 28;
    float vol_frac = (float)engine->volume / MAX_VOLUME;
    noto_draw_text(pb_x, vb_y + 14,
                         COLOR_TEXT_DIM, ui->font_small.size, "Vol");
    ui_draw_progress_bar(pb_x + 36, vb_y + 6, pb_w - 36, 6,
                         vol_frac, COLOR_TEXT_DIM, COLOR_ACCENT);

    /* Thumb on volume bar — custom texture if theme provides one, else circle */
    {
        int vb_x  = pb_x + 36;
        int vb_w  = pb_w - 36;
        int thumb_x = vb_x + (int)(vol_frac * vb_w);
        int thumb_y = vb_y + 6 + 3;
        vita2d_texture *vthumb = NULL;
        if (ui->theme_mgr) {
            Theme *ct = theme_current(ui->theme_mgr);
            if (ct) vthumb = ct->thumbs.volume;
        }
        if (vthumb) {
            float sz = 12.0f;
            int tw = vita2d_texture_get_width(vthumb);
            int th = vita2d_texture_get_height(vthumb);
            vita2d_draw_texture_scale(vthumb,
                thumb_x - sz / 2.0f, thumb_y - sz / 2.0f,
                sz / (float)tw, sz / (float)th);
        } else {
            vita2d_draw_fill_circle(thumb_x, thumb_y, 6, COLOR_BG);
            vita2d_draw_fill_circle(thumb_x, thumb_y, 5, COLOR_TEXT);
        }
    }

    /* ── Playlist info — 24px below volume bar ── */
    if (playlist && playlist->count > 0) {
        noto_draw_textf(
                              pb_x, vb_y + 6 + 6 + 24,
                              COLOR_TEXT_DIM, ui->font_small.size,
                              "Track %d / %d",
                              playlist->current_index + 1, playlist->count);
    }

    /* ── Visualizer preview strip at bottom ── */
    if (vis && vis->mode != VIS_MODE_DISABLED) {
        int vis_y = SCREEN_HEIGHT - BAR_HEIGHT - 60;
        int vis_h = SCREEN_HEIGHT - BAR_HEIGHT - vis_y;   /* fills exactly to footer */
        vita2d_draw_rectangle(0, vis_y, SCREEN_WIDTH, vis_h,
                              (COLOR_ALBUM_ART_BG & 0x00FFFFFFu) | 0x99000000u);
        visualizer_render(vis, 0, vis_y, SCREEN_WIDTH, vis_h);
    }

    /* ── Control hints footer ── */
    draw_footer(ui,
        "[X]Pause  [O]Back  [T]Queue  [S]Vis  L/R:Seek  [DLR]Track  [DUD]Vol  [SEL]Repeat  [STA]Shuffle");
}

/* ── ui_draw_visualizer ──────────────────────────────────────────────────── */

void ui_draw_visualizer(const UIState *ui, Visualizer *vis,
                        const AudioEngine *engine, const Playlist *playlist)
{
    if (!ui || !vis) return;

    /* Main visualizer area */
    int vis_top = BAR_HEIGHT;
    int vis_bot = SCREEN_HEIGHT - BAR_HEIGHT - 60;
    visualizer_render(vis, 0, vis_top, SCREEN_WIDTH, vis_bot - vis_top);

    /* Track overlay at bottom — center title+artist block in the 60-px bar */
    vita2d_draw_rectangle(0, vis_bot, SCREEN_WIDTH, 60, COLOR_VIS_OVERLAY);
    {
        int t_h   = font_renderer_text_height(ui->font_medium.size);
        int a_h   = font_renderer_text_height(ui->font_small.size);
        int t_asc = t_h * 4 / 5;            /* approx ascender of medium font */
        int a_des = a_h / 5;                /* approx descender of small font */
        int block = t_asc + 22 + a_des;     /* visual height: title cap → artist descender */
        int info_y = vis_bot + (60 - block) / 2 + t_asc;
        ui_draw_track_info(ui, engine, playlist, 12, info_y);
    }

    draw_footer(ui, "[X]Pause  [O]Back  [S]Cycle  [DLR]Track");
}

/* ── ui_draw_playlist ────────────────────────────────────────────────────── */

void ui_draw_playlist(const UIState *ui, const Playlist *playlist,
                      const AudioEngine *engine)
{
    if (!ui || !playlist) return;

    int list_y = BAR_HEIGHT + 4;
    int list_h = SCREEN_HEIGHT - BAR_HEIGHT * 2 - 8;

    {
        char q_hdr[MAX_PLAYLIST_NAME + 24];
        snprintf(q_hdr, sizeof(q_hdr), "%s  [%d tracks]",
                 playlist->name, playlist->count);
        draw_text_scroll(ui, 12, list_y + ROW_TEXT_BASELINE,
                         SCREEN_WIDTH - 24, COLOR_TEXT, ui->font_medium.size, q_hdr, true);
    }
    list_y += 30;
    list_h -= 34;

    int max_rows = list_h / LIST_ROW_HEIGHT;
    int cur_idx  = engine ? engine->decoder ? playlist->current_index : -1 : -1;

    for (int i = 0; i < max_rows; i++) {
        int idx = ui->list_offset + i;
        if (idx >= playlist->count) break;

        const PlaylistEntry *e = &playlist->entries[idx];
        bool selected  = (idx == ui->list_selected);
        bool playing   = (idx == cur_idx);

        uint32_t bg_color = 0x00000000;
        if (selected) bg_color = COLOR_HIGHLIGHT;
        else if (playing) bg_color = COLOR_ACCENT;

        if (bg_color) {
            vita2d_draw_rectangle(0, list_y - 2,
                                  SCREEN_WIDTH - 20, LIST_ROW_HEIGHT - 2,
                                  bg_color);
        }

        /* Track number */
        noto_draw_textf(8, list_y + ROW_TEXT_BASELINE,
                              playing ? COLOR_PROGRESS : COLOR_TEXT_DIM,
                              ui->font_small.size, "%3d", idx + 1);

        /* Title / filename */
        const char *title = e->title[0] ? e->title : strrchr(e->filepath, '/');
        if (!title) title = e->filepath;
        else if (title[0] == '/') title++;

        draw_text_scroll(ui, 42, list_y + ROW_TEXT_BASELINE,
                         SCREEN_WIDTH - 42 - 70,
                         selected ? COLOR_TEXT : (playing ? COLOR_PROGRESS : COLOR_TEXT_DIM),
                         ui->font_medium.size, title, selected);

        /* Duration */
        if (e->duration_ms > 0) {
            char dur[16];
            metadata_format_duration(e->duration_ms, dur, sizeof(dur));
            noto_draw_textf(
                                  SCREEN_WIDTH - 60, list_y + ROW_TEXT_BASELINE,
                                  COLOR_TEXT_DIM, ui->font_small.size,
                                  "%s", dur);
        }

        list_y += LIST_ROW_HEIGHT;
    }

    draw_footer(ui, "[X]Play  [O]Back");
}

/* ── ui_draw_settings ────────────────────────────────────────────────────── */

void ui_draw_settings(const UIState *ui)
{
    if (!ui) return;

    typedef struct { const char *label; } SettingRow;
    static const SettingRow rows[] = {
        { "Show Cover Art" },
        { "Crossfade" },
        { "Crossfade Duration" },
        { "Equalizer" },
        { "Reactive GIF Background" },
        { "Theme" },
        { "Exit VitaWave" },
    };
    int num_rows = 7;

    int sy = BAR_HEIGHT + 10;
    noto_draw_text(12, sy + 28,
                         COLOR_TEXT, ui->font_large.size, "Settings");
    sy += 36;

    for (int i = 0; i < num_rows; i++) {
        bool sel = (i == ui->settings.settings_selected);

        if (sel) {
            vita2d_draw_rectangle(0, sy, SCREEN_WIDTH - 40,
                                  LIST_ROW_HEIGHT, COLOR_HIGHLIGHT);
        }

        noto_draw_textf(12, sy + ROW_TEXT_BASELINE,
                              sel ? COLOR_TEXT : COLOR_TEXT_DIM,
                              ui->font_medium.size, "%s", rows[i].label);

        /* Value */
        char val_str[64];
        switch (i) {
            case 0: snprintf(val_str, sizeof(val_str), "%s",
                             ui->settings.show_album_art ? "ON" : "OFF"); break;
            case 1: snprintf(val_str, sizeof(val_str), "%s",
                             ui->settings.crossfade ? "ON" : "OFF"); break;
            case 2: snprintf(val_str, sizeof(val_str), "%ds",
                             ui->settings.crossfade_duration); break;
            case 3: {
                AudioEngine *ae = get_audio_engine();
                bool eq_on = ae && ae->eq && ae->eq->enabled;
                snprintf(val_str, sizeof(val_str), "%s", eq_on ? "ON" : "OFF");
                break;
            }
            case 4: snprintf(val_str, sizeof(val_str), "%s",
                             ui->settings.gif_reactive ? "ON" : "OFF"); break;
            case 5:
                if (ui->theme_mgr && ui->theme_mgr->count > 0) {
                    int preview = ui->settings_theme_preview;
                    if (preview < 0 || preview >= ui->theme_mgr->count)
                        preview = ui->theme_mgr->current;
                    const char *tname = ui->theme_mgr->themes[preview].name;
                    if (sel)
                        snprintf(val_str, sizeof(val_str), "< %s >", tname);
                    else
                        snprintf(val_str, sizeof(val_str), "%s", tname);
                } else {
                    snprintf(val_str, sizeof(val_str), "Classic Dark");
                }
                break;
            case 6: val_str[0] = '\0'; break;
            default: val_str[0] = '\0'; break;
        }
        {
            int vw = noto_text_width(ui->font_medium.size, val_str);
            int vx = SCREEN_WIDTH - 40 - vw;
            noto_draw_textf(vx, sy + ROW_TEXT_BASELINE,
                            COLOR_PROGRESS, ui->font_medium.size,
                            "%s", val_str);
        }

        sy += LIST_ROW_HEIGHT;
    }

    if (ui->settings.settings_selected == 5)
        draw_footer(ui, "[DUD]Select  [DLR]Theme  [X]Apply  [O]Back");
    else if (ui->settings.settings_selected == 6)
        draw_footer(ui, "[DUD]Select  [X]Exit  [O]Back");
    else
        draw_footer(ui, "[DUD]Select  [X]Toggle  [O]Back");
}

/* ── ui_draw_playlist_list ───────────────────────────────────────────────── */

void ui_draw_playlist_list(const UIState *ui, const PlaylistManager *mgr)
{
    if (!ui) return;

    int list_y = BAR_HEIGHT + 8;

    /* Title row */
    noto_draw_text(12, list_y + 28,
                         COLOR_TEXT, ui->font_large.size, "My Playlists");
    list_y += 36;

    int count = mgr ? mgr->count : 0;
    int max_rows = LIST_VISIBLE_ROWS;

    for (int i = 0; i < max_rows; i++) {
        int idx = ui->list_offset + i;
        if (idx >= count) break;

        const Playlist *pl = mgr->lists[idx];
        if (!pl) continue;

        bool selected = (idx == ui->playlist_mgr_selected);

        if (selected) {
            vita2d_draw_rectangle(0, list_y - 2,
                                  SCREEN_WIDTH - 20, LIST_ROW_HEIGHT - 2,
                                  COLOR_HIGHLIGHT);
        }

        /* Playlist name */
        draw_text_scroll(ui, 12, list_y + ROW_TEXT_BASELINE,
                         SCREEN_WIDTH - 130,
                         selected ? COLOR_TEXT : COLOR_TEXT_DIM,
                         ui->font_medium.size, pl->name, selected);

        /* Track count */
        noto_draw_textf(
                              SCREEN_WIDTH - 110, list_y + ROW_TEXT_BASELINE,
                              COLOR_TEXT_DIM, ui->font_small.size,
                              "%d tracks", pl->count);

        list_y += LIST_ROW_HEIGHT;
    }

    if (count == 0) {
        draw_text_with_icons(ui, 12, list_y + ROW_TEXT_BASELINE,
                             COLOR_TEXT_DIM,
                             "No playlists. Press [T] to create one.");
    }

    draw_footer(ui, "[X]Open  [T]New  [S]Delete  [O]Back");
}

/* ── ui_draw_playlist_detail ─────────────────────────────────────────────── */

void ui_draw_playlist_detail(const UIState *ui, const PlaylistManager *mgr,
                              const AudioEngine *engine)
{
    if (!ui) return;
    (void)engine;

    const Playlist *pl = (mgr && ui->playlist_detail_idx < mgr->count)
                         ? mgr->lists[ui->playlist_detail_idx] : NULL;

    int list_y = BAR_HEIGHT + 8;

    /* Header: playlist name */
    const char *pl_name = pl ? pl->name : "Playlist";
    draw_text_scroll(ui, 12, list_y + 28,
                     SCREEN_WIDTH - 24, COLOR_TEXT, ui->font_large.size, pl_name, true);
    if (pl) {
        noto_draw_textf(12, list_y + 50,
                              COLOR_TEXT_DIM, ui->font_small.size,
                              "%d tracks", pl->count);
    }
    list_y += 58;

    int count = pl ? pl->count : 0;
    int max_rows = LIST_VISIBLE_ROWS - 2;   /* header takes ~2 rows */

    for (int i = 0; i < max_rows; i++) {
        int idx = ui->list_offset + i;
        if (idx >= count) break;

        const PlaylistEntry *e = &pl->entries[idx];
        bool selected = (idx == ui->list_selected);

        if (selected) {
            vita2d_draw_rectangle(0, list_y - 2,
                                  SCREEN_WIDTH - 20, LIST_ROW_HEIGHT - 2,
                                  COLOR_HIGHLIGHT);
        }

        /* Track number */
        noto_draw_textf(8, list_y + ROW_TEXT_BASELINE,
                              COLOR_TEXT_DIM, ui->font_small.size,
                              "%3d", idx + 1);

        /* Title or basename fallback */
        const char *slash = strrchr(e->filepath, '/');
        const char *fname = slash ? slash + 1 : e->filepath;
        const char *row_title = e->title[0] ? e->title : fname;

        draw_text_scroll(ui, 42, list_y + ROW_TEXT_BASELINE,
                         SCREEN_WIDTH - 42 - 20,
                         selected ? COLOR_TEXT : COLOR_TEXT_DIM,
                         ui->font_medium.size, row_title, selected);

        list_y += LIST_ROW_HEIGHT;
    }

    if (count == 0) {
        noto_draw_text(12, list_y + ROW_TEXT_BASELINE,
                             COLOR_TEXT_DIM, ui->font_medium.size,
                             "Empty playlist. Add tracks via L in browser.");
    }

    draw_footer(ui, "[X]Play  [T]Rename  L/R:Move  [S]Remove  [O]Back");
}

/* ── ui_draw_add_to_playlist ─────────────────────────────────────────────── */

void ui_draw_add_to_playlist(const UIState *ui, const PlaylistManager *mgr)
{
    if (!ui) return;

    /* Semi-transparent dark overlay */
    vita2d_draw_rectangle(60, 60, SCREEN_WIDTH - 120, SCREEN_HEIGHT - 120,
                          0xEE1C1C1Eu);
    vita2d_draw_rectangle(60, 60, SCREEN_WIDTH - 120, BAR_HEIGHT,
                          COLOR_ACCENT);

    /* Title */
    noto_draw_text(80, 60 + 28,
                         COLOR_TEXT, ui->font_large.size, "Add to Playlist");

    /* Show the file being added (basename) */
    if (ui->pending_add_path[0]) {
        const char *slash = strrchr(ui->pending_add_path, '/');
        const char *fname = slash ? slash + 1 : ui->pending_add_path;
        noto_draw_textf(80, 60 + BAR_HEIGHT + 18,
                              COLOR_TEXT_DIM, ui->font_small.size,
                              "File: %s", fname);
    }

    int list_y = 60 + BAR_HEIGHT + 32;
    int count  = mgr ? mgr->count : 0;
    int max_rows = 10;

    for (int i = 0; i < max_rows; i++) {
        int idx = ui->list_offset + i;
        if (idx >= count) break;

        const Playlist *pl = mgr->lists[idx];
        if (!pl) continue;

        bool selected = (idx == ui->playlist_mgr_selected);

        /* Check if the pending track is already in this playlist */
        bool has_track = false;
        if (ui->pending_add_path[0]) {
            for (int ki = 0; ki < pl->count; ki++) {
                if (strcmp(pl->entries[ki].filepath, ui->pending_add_path) == 0) {
                    has_track = true;
                    break;
                }
            }
        }

        if (selected) {
            vita2d_draw_rectangle(60, list_y - 2,
                                  SCREEN_WIDTH - 120, LIST_ROW_HEIGHT - 2,
                                  COLOR_HIGHLIGHT);
        }

        /* Checkmark if track already in this playlist */
        if (has_track) {
            noto_draw_text(62, list_y + ROW_TEXT_BASELINE,
                                 COLOR_PROGRESS, ui->font_medium.size, "v");
        }

        noto_draw_textf(80, list_y + ROW_TEXT_BASELINE,
                              selected ? COLOR_TEXT : COLOR_TEXT_DIM,
                              ui->font_medium.size, "%s", pl->name);

        noto_draw_textf(
                              SCREEN_WIDTH - 160, list_y + ROW_TEXT_BASELINE,
                              COLOR_TEXT_DIM, ui->font_small.size,
                              "%d", pl->count);

        list_y += LIST_ROW_HEIGHT;
    }

    if (count == 0) {
        draw_text_with_icons(ui, 80, list_y + ROW_TEXT_BASELINE,
                             COLOR_TEXT_DIM,
                             "No playlists. Press [T] to create one.");
    }

    draw_footer(ui, "[X]Toggle  [T]New Playlist  [O]Done");
}

/* ── ui_draw_rename_playlist ─────────────────────────────────────────────── */

void ui_draw_rename_playlist(const UIState *ui)
{
    if (!ui) return;

    static const char CHARSET[] =
        " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    const int CHARSET_LEN = (int)(sizeof(CHARSET) - 1);

    /* Dark overlay panel */
    int px = 80, py = 160, pw = SCREEN_WIDTH - 160, ph = 220;
    vita2d_draw_rectangle(px, py, pw, ph, 0xF02C2C2Eu);
    vita2d_draw_rectangle(px, py, pw, BAR_HEIGHT, COLOR_ACCENT);

    noto_draw_text(px + 20, py + 28,
                         COLOR_TEXT, ui->font_large.size, "Rename Playlist");

    /* Name display with cursor */
    int name_len = (int)strlen(ui->rename_buf);
    int text_y   = py + BAR_HEIGHT + 52;

    /* Draw each character; highlight cursor position */
    int cx = px + 20;
    for (int i = 0; i <= name_len; i++) {
        bool is_cursor = (i == ui->rename_cursor);
        char ch[2] = { i < name_len ? ui->rename_buf[i] : ' ', '\0' };

        if (is_cursor) {
            int cw = noto_text_width(
                                           ui->font_large.size, ch);
            if (cw < 14) cw = 14;
            vita2d_draw_rectangle(cx - 2, text_y - 30, cw + 4, 36,
                                  COLOR_PROGRESS);
        }
        if (i < name_len) {
            uint32_t col = is_cursor ? COLOR_TEXT : COLOR_TEXT_DIM;
            cx += noto_draw_text(cx, text_y,
                                       col, ui->font_large.size, ch);
        }
    }

    /* Charset hint: show adjacent characters for current cursor position */
    if (ui->rename_cursor < name_len) {
        char cur_c = ui->rename_buf[ui->rename_cursor];
        int ci = 0;
        for (int k = 0; k < CHARSET_LEN; k++) {
            if (CHARSET[k] == cur_c) { ci = k; break; }
        }
        char prev_ch[2] = { CHARSET[(ci - 1 + CHARSET_LEN) % CHARSET_LEN], '\0' };
        char cur_ch [2] = { cur_c, '\0' };
        char next_ch[2] = { CHARSET[(ci + 1) % CHARSET_LEN], '\0' };
        int hint_y = text_y + 28;
        noto_draw_textf(px + 20, hint_y,
                              COLOR_TEXT_DIM, ui->font_small.size,
                              "Up/Down: cycle  [ %s | %s | %s ]",
                              prev_ch, cur_ch, next_ch);
    }

    draw_footer(ui, "[DUD]Char  [DLR]Move  [S]Delete  [X]Confirm  [O]Cancel");
}

/* ── ui_draw_equalizer ───────────────────────────────────────────────────── */

#define EQ_Y_TOP  100
#define EQ_Y_BOT  340
#define EQ_Y_MID  220   /* (100+340)/2 */
#define EQ_COL(i) (80 + (i) * 80)

static inline int eq_gain_to_y(float gain)
{
    int y = EQ_Y_MID - (int)(gain / 12.0f * (float)(EQ_Y_MID - EQ_Y_TOP));
    if (y < EQ_Y_TOP) y = EQ_Y_TOP;
    if (y > EQ_Y_BOT) y = EQ_Y_BOT;
    return y;
}

void ui_draw_equalizer(const UIState *ui)
{
    if (!ui) return;
    Equalizer *eq = get_audio_engine() ? get_audio_engine()->eq : NULL;

    draw_theme_bg(ui, UI_SCREEN_EQUALIZER);
    draw_header(ui);

    /* Title */
    noto_draw_text(12, 68, COLOR_TEXT, ui->font_large.size, "Equalizer");

    /* ON/OFF badge */
    bool enabled = eq && eq->enabled;
    const char *onoff    = enabled ? "ON" : "OFF";
    uint32_t    onoff_c  = enabled ? COLOR_PROGRESS : COLOR_TEXT_DIM;
    int         onoff_w  = noto_text_width(ui->font_medium.size, onoff);
    noto_draw_text(SCREEN_WIDTH - 40 - onoff_w, 68, onoff_c,
                   ui->font_medium.size, onoff);

    /* Preset label centred */
    const char *pname;
    if (ui->eq_preset_idx >= EQ_PRESET_COUNT) {
        int ci = ui->eq_preset_idx - EQ_PRESET_COUNT;
        pname = (ci < ui->eq_custom_count) ? ui->eq_custom[ci].name : "Custom";
    } else if (ui->eq_preset_idx >= 0) {
        pname = k_eq_presets[ui->eq_preset_idx].name;
    } else {
        pname = "Custom";
    }
    int pw = noto_text_width(ui->font_small.size, pname);
    noto_draw_text((SCREEN_WIDTH - pw) / 2, 68,
                   COLOR_TEXT_DIM, ui->font_small.size, pname);

    /* Horizontal guide lines */
    unsigned int line_col0 = (COLOR_TEXT_DIM & 0x00FFFFFFu) | 0x30000000u;
    unsigned int line_col1 = (COLOR_TEXT_DIM & 0x00FFFFFFu) | 0x70000000u;
    int y_p6 = EQ_Y_MID - (EQ_Y_MID - EQ_Y_TOP) / 2;  /* +6 dB */
    int y_m6 = EQ_Y_MID + (EQ_Y_BOT - EQ_Y_MID) / 2;  /* -6 dB */
    vita2d_draw_rectangle(50, EQ_Y_TOP, SCREEN_WIDTH - 60, 1, line_col0);
    vita2d_draw_rectangle(50, y_p6,     SCREEN_WIDTH - 60, 1, line_col0);
    vita2d_draw_rectangle(50, EQ_Y_MID, SCREEN_WIDTH - 60, 1, line_col1);
    vita2d_draw_rectangle(50, y_m6,     SCREEN_WIDTH - 60, 1, line_col0);
    vita2d_draw_rectangle(50, EQ_Y_BOT, SCREEN_WIDTH - 60, 1, line_col0);

    /* dB scale labels */
    noto_draw_textf(2, EQ_Y_TOP + 8, COLOR_TEXT_DIM, ui->font_small.size, "+12");
    noto_draw_textf(2, y_p6     + 8, COLOR_TEXT_DIM, ui->font_small.size, " +6");
    noto_draw_textf(2, EQ_Y_MID + 8, COLOR_TEXT_DIM, ui->font_small.size, "  0");
    noto_draw_textf(2, y_m6     + 8, COLOR_TEXT_DIM, ui->font_small.size, " -6");
    noto_draw_textf(2, EQ_Y_BOT + 8, COLOR_TEXT_DIM, ui->font_small.size, "-12");

    /* Separator between Preamp and bands */
    vita2d_draw_rectangle(EQ_COL(0) + 38, EQ_Y_TOP - 10,
                          1, EQ_Y_BOT - EQ_Y_TOP + 30,
                          (COLOR_TEXT_DIM & 0x00FFFFFFu) | 0x50000000u);

    /* Sliders: i=0 = Preamp, i=1..EQ_BANDS = bands */
    for (int i = 0; i <= EQ_BANDS; i++) {
        int   cx  = EQ_COL(i);
        bool  sel = (i == ui->eq_selected);
        float gain = eq ? (i == 0 ? eq->preamp : eq->gains[i - 1]) : 0.0f;
        int   ky  = eq_gain_to_y(gain);

        /* Track */
        unsigned int track_c = sel
            ? ((COLOR_HIGHLIGHT & 0x00FFFFFFu) | 0xD0000000u)
            : ((COLOR_TEXT_DIM  & 0x00FFFFFFu) | 0x50000000u);
        vita2d_draw_rectangle(cx - 1, EQ_Y_TOP, 2, EQ_Y_BOT - EQ_Y_TOP, track_c);

        /* Fill from 0-dB line to knob */
        if (gain > 0.0f) {
            unsigned int fc = enabled
                ? ((COLOR_PROGRESS & 0x00FFFFFFu) | 0x80000000u)
                : ((COLOR_TEXT_DIM & 0x00FFFFFFu) | 0x40000000u);
            vita2d_draw_rectangle(cx - 3, ky, 6, EQ_Y_MID - ky, fc);
        } else if (gain < 0.0f) {
            unsigned int fc = enabled
                ? ((COLOR_VIS_BAR & 0x00FFFFFFu) | 0x80000000u)
                : ((COLOR_TEXT_DIM & 0x00FFFFFFu) | 0x40000000u);
            vita2d_draw_rectangle(cx - 3, EQ_Y_MID, 6, ky - EQ_Y_MID, fc);
        }

        /* Knob */
        unsigned int knob_c = sel
            ? COLOR_PROGRESS
            : (enabled ? COLOR_VIS_BAR : COLOR_TEXT_DIM);
        vita2d_draw_rectangle(cx - 10, ky - 4, 20, 8, knob_c);
        if (sel)
            vita2d_draw_rectangle(cx - 8, ky - 2, 16, 2,
                                  (knob_c & 0x00FFFFFFu) | 0x60000000u);

        /* Frequency / Preamp label */
        const char *label = (i == 0) ? "PRE" : k_eq_labels[i - 1];
        int lw = noto_text_width(ui->font_small.size, label);
        noto_draw_text(cx - lw / 2, EQ_Y_BOT + 20,
                       sel ? COLOR_TEXT : COLOR_TEXT_DIM,
                       ui->font_small.size, label);

        /* Value for selected item */
        if (sel) {
            char vs[16];
            snprintf(vs, sizeof(vs), "%+.1fdB", gain);
            int vw = noto_text_width(ui->font_medium.size, vs);
            noto_draw_text(cx - vw / 2, EQ_Y_BOT + 46,
                           COLOR_PROGRESS, ui->font_medium.size, vs);
        }
    }

    /* ── Footer (mode-dependent) ── */
    if (ui->eq_action_mode == 1 || ui->eq_action_mode == 2) {
        draw_footer(ui, "[DUD]Char  [DLR]Move  [S]Delete  [X]Confirm  [O]Cancel");
    } else if (ui->eq_action_mode == 3) {
        draw_footer(ui, "[DUD]Select  [X]Confirm  [O]Cancel");
    } else {
        bool on_custom = (ui->eq_preset_idx >= EQ_PRESET_COUNT &&
                          ui->eq_preset_idx < EQ_PRESET_COUNT + ui->eq_custom_count);
        if (on_custom)
            draw_footer(ui, "[DUD]Adjust  [DLR]Sel  [LR]±3dB  [S]Preset  [X]Save  [STA]Manage  [T]On/Off  [O]Back");
        else
            draw_footer(ui, "[DUD]Adjust  [DLR]Sel  [LR]±3dB  [S]Preset  [X]Save  [T]On/Off  [O]Back");
    }

    /* ── Name-entry overlay ── */
    if (ui->eq_action_mode == 1 || ui->eq_action_mode == 2) {
        static const char EQ_CHARSET[] =
            " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
        const int EQ_CHARSET_LEN = (int)(sizeof(EQ_CHARSET) - 1);

        int px = 80, py = 160, pw2 = SCREEN_WIDTH - 160, ph = 220;
        vita2d_draw_rectangle(px, py, pw2, ph, 0xF02C2C2Eu);
        vita2d_draw_rectangle(px, py, pw2, BAR_HEIGHT, COLOR_ACCENT);
        const char *title = (ui->eq_action_mode == 1) ? "Save Preset" : "Rename Preset";
        noto_draw_text(px + 20, py + 28, COLOR_TEXT, ui->font_large.size, title);

        int nlen   = (int)strlen(ui->eq_name_buf);
        int text_y = py + BAR_HEIGHT + 52;
        int cx2    = px + 20;
        for (int i = 0; i <= nlen; i++) {
            bool is_cur = (i == ui->eq_name_cursor);
            char ch[2]  = { i < nlen ? ui->eq_name_buf[i] : ' ', '\0' };
            if (is_cur) {
                int cw = noto_text_width(ui->font_large.size, ch);
                if (cw < 14) cw = 14;
                vita2d_draw_rectangle(cx2 - 2, text_y - 30, cw + 4, 36, COLOR_PROGRESS);
            }
            if (i < nlen) {
                uint32_t col = is_cur ? COLOR_TEXT : COLOR_TEXT_DIM;
                cx2 += noto_draw_text(cx2, text_y, col, ui->font_large.size, ch);
            }
        }
        if (ui->eq_name_cursor < nlen) {
            char cur_c = ui->eq_name_buf[ui->eq_name_cursor];
            int ci2 = 0;
            for (int k = 0; k < EQ_CHARSET_LEN; k++)
                if (EQ_CHARSET[k] == cur_c) { ci2 = k; break; }
            char prev_ch[2] = { EQ_CHARSET[(ci2 - 1 + EQ_CHARSET_LEN) % EQ_CHARSET_LEN], '\0' };
            char cur_ch [2] = { cur_c, '\0' };
            char next_ch[2] = { EQ_CHARSET[(ci2 + 1) % EQ_CHARSET_LEN], '\0' };
            noto_draw_textf(px + 20, text_y + 28, COLOR_TEXT_DIM, ui->font_small.size,
                            "Up/Down: cycle  [ %s | %s | %s ]",
                            prev_ch, cur_ch, next_ch);
        }
    }

    /* ── Manage menu overlay ── */
    if (ui->eq_action_mode == 3) {
        int mx = 300, my = 190, mw = 360, mh = 140;
        vita2d_draw_rectangle(mx, my, mw, mh, 0xF02C2C2Eu);
        vita2d_draw_rectangle(mx, my, mw, BAR_HEIGHT, COLOR_ACCENT);
        noto_draw_text(mx + 20, my + 28, COLOR_TEXT, ui->font_large.size, "Preset Options");

        const char *opts[2] = { "Rename", "Delete" };
        for (int i = 0; i < 2; i++) {
            int oy = my + BAR_HEIGHT + 16 + i * 34;
            if (i == ui->eq_manage_cursor)
                vita2d_draw_rectangle(mx + 4, oy - 16, mw - 8, 28, COLOR_HIGHLIGHT);
            uint32_t tc = (i == ui->eq_manage_cursor) ? COLOR_TEXT : COLOR_TEXT_DIM;
            noto_draw_text(mx + 20, oy, tc, ui->font_medium.size, opts[i]);
        }
    }
}

/* ── draw_theme_bg ───────────────────────────────────────────────────────── */

static void draw_theme_bg(const UIState *ui, UIScreen screen)
{
    if (!ui->theme_mgr) return;
    Theme *t = theme_current(ui->theme_mgr);
    if (!t) return;

    vita2d_texture *bg = NULL;
    switch (screen) {
        case UI_SCREEN_LIBRARY:
        case UI_SCREEN_BROWSER:      bg = t->bg.browser; break;
        case UI_SCREEN_NOW_PLAYING:
            bg = theme_animbg_frame(&t->bg.now_playing, ui->np_bg_frame); break;
        case UI_SCREEN_VISUALIZER:
            bg = t->bg.visualizer
               ? t->bg.visualizer
               : theme_animbg_frame(&t->bg.now_playing, ui->np_bg_frame);
            break;
        case UI_SCREEN_PLAYLIST:
        case UI_SCREEN_PLAYLIST_LIST:
        case UI_SCREEN_PLAYLIST_DETAIL: bg = t->bg.playlist; break;
        case UI_SCREEN_SETTINGS:     bg = t->bg.settings;    break;
        default:                     bg = NULL;              break;
    }
    if (!bg) bg = t->bg.fallback;
    if (!bg) return;

    int tw = vita2d_texture_get_width(bg);
    int th = vita2d_texture_get_height(bg);
    if (tw <= 0 || th <= 0) return;
    float sx = (float)SCREEN_WIDTH  / (float)tw;
    float sy = (float)SCREEN_HEIGHT / (float)th;
    vita2d_draw_texture_scale(bg, 0.0f, 0.0f, sx, sy);
}

/* ── ui_render ───────────────────────────────────────────────────────────── */

void ui_render(const UIState    *ui,
               const AudioEngine *engine,
               const Playlist   *playlist,
               const FileList   *browser,
               Visualizer       *vis)
{
    if (!ui) return;

    draw_theme_bg(ui, ui->current_screen);

    /* Header is common to all screens except fullscreen visualizer */
    bool full_vis = (ui->current_screen == UI_SCREEN_VISUALIZER);
    if (!full_vis) {
        draw_header(ui);
    }

    int content_y = full_vis ? 0 : BAR_HEIGHT;
    int content_h = full_vis ? SCREEN_HEIGHT
                              : SCREEN_HEIGHT - BAR_HEIGHT * 2;

    switch (ui->current_screen) {

    case UI_SCREEN_LIBRARY:
        /* Library is the same view as browser for now */
        /* Fall through */
    case UI_SCREEN_BROWSER:
        if (browser) {
            ui_draw_file_list(ui, browser,
                              0, content_y + 30,
                              SCREEN_WIDTH, content_h - 30);
        }
        draw_footer(ui,
            "[X]Play  [O]Up  [T]Playlists  [S]Vis  L:Add  [SEL]Now Playing  [STA]Settings");
        break;

    case UI_SCREEN_NOW_PLAYING:
        ui_draw_now_playing(ui, engine, playlist, vis);
        draw_header(ui);
        break;

    case UI_SCREEN_VISUALIZER:
        ui_draw_visualizer(ui, vis, engine, playlist);
        draw_header(ui);
        break;

    case UI_SCREEN_PLAYLIST:
        ui_draw_playlist(ui, playlist, engine);
        draw_header(ui);
        break;

    case UI_SCREEN_SETTINGS:
        ui_draw_settings(ui);
        draw_header(ui);
        break;

    case UI_SCREEN_PLAYLIST_LIST:
        ui_draw_playlist_list(ui, get_playlist_manager());
        draw_header(ui);
        break;

    case UI_SCREEN_PLAYLIST_DETAIL:
        ui_draw_playlist_detail(ui, get_playlist_manager(), engine);
        draw_header(ui);
        break;

    case UI_SCREEN_ADD_TO_PLAYLIST:
        /* Render the previous screen underneath, then overlay the picker */
        ui_draw_add_to_playlist(ui, get_playlist_manager());
        break;

    case UI_SCREEN_RENAME_PLAYLIST:
        ui_draw_rename_playlist(ui);
        break;

    case UI_SCREEN_EQUALIZER:
        ui_draw_equalizer(ui);
        break;

    default:
        break;
    }
}
