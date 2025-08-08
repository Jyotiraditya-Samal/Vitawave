#include <stdio.h>
#include <string.h>
#include <vita2d.h>
#include <psp2/ctrl.h>

#include "ui.h"
#include "file_browser.h"
#include "audio_engine.h"
#include "metadata.h"

#define COLOR_TEXT      RGBA8(0xff, 0xff, 0xff, 0xff)
#define COLOR_SELECTED  RGBA8(0x3a, 0x3a, 0x3c, 0xff)
#define COLOR_DIM       RGBA8(0x8e, 0x8e, 0x93, 0xff)
#define COLOR_ACCENT    RGBA8(0xfc, 0x3c, 0x44, 0xff)
#define COLOR_CURRENT   RGBA8(0x30, 0xd1, 0x58, 0xff)

static vita2d_pgf *s_font = NULL;
static SceCtrlData s_old_pad;

int ui_init(UIState *ui)
{
    if (!ui) return -1;
    memset(ui, 0, sizeof(*ui));
    s_font = vita2d_load_default_pgf();
    return s_font ? 0 : -1;
}

void ui_destroy(UIState *ui)
{
    metadata_free(&ui->current_meta);
    if (ui->album_art_tex) { vita2d_free_texture(ui->album_art_tex); ui->album_art_tex = NULL; }
    if (s_font) vita2d_free_pgf(s_font);
    s_font = NULL;
}

void ui_handle_input(UIState *ui, AudioEngine *engine, FileList *browser)
{
    SceCtrlData pad;
    sceCtrlReadBufferPositive(0, &pad, 1);
    SceCtrlData pressed;
    pressed.buttons = pad.buttons & ~s_old_pad.buttons;
    s_old_pad = pad;

    if (ui->current_screen == UI_SCREEN_BROWSER) {
        int count = browser ? browser->count : 0;
        if (pressed.buttons & SCE_CTRL_DOWN && ui->selected < count - 1) ui->selected++;
        if (pressed.buttons & SCE_CTRL_UP   && ui->selected > 0)         ui->selected--;
        if (pressed.buttons & SCE_CTRL_CIRCLE) {
            file_browser_navigate_up(browser);
            ui->selected = 0; ui->list_offset = 0;
        }
        if (pressed.buttons & SCE_CTRL_CROSS && browser && ui->selected < count) {
            FileEntry *e = &browser->entries[ui->selected];
            if (e->is_directory) {
                file_browser_navigate_into(browser, e->path);
                ui->selected = 0; ui->list_offset = 0;
            } else {
                audio_engine_play(engine, e->path);
                metadata_free(&ui->current_meta);
                metadata_load(&ui->current_meta, e->path);
                if (ui->album_art_tex) {
                    vita2d_free_texture(ui->album_art_tex);
                    ui->album_art_tex = NULL;
                }
                ui->album_art_tex = metadata_get_album_art_texture(&ui->current_meta);
                ui->fill_playlist_request = true;
                ui->prev_screen    = UI_SCREEN_BROWSER;
                ui->current_screen = UI_SCREEN_NOW_PLAYING;
            }
        }
    }
    else if (ui->current_screen == UI_SCREEN_NOW_PLAYING) {
        if (pressed.buttons & SCE_CTRL_CROSS)    audio_engine_pause(engine);
        if (pressed.buttons & SCE_CTRL_TRIANGLE && ui->playlist)
        {
            ui->prev_screen    = UI_SCREEN_NOW_PLAYING;
            ui->current_screen = UI_SCREEN_PLAYLIST;
            /* scroll to current track */
            if (ui->playlist->current >= 0)
                ui->list_offset = ui->playlist->current > 5 ? ui->playlist->current - 5 : 0;
        }
        if (pressed.buttons & SCE_CTRL_CIRCLE) {
            ui->current_screen = ui->prev_screen;
            ui->selected       = 0;
        }
        if (pad.buttons & SCE_CTRL_LTRIGGER) {
            int v = engine->volume - 512;
            audio_engine_set_volume(engine, v < 0 ? 0 : v);
        }
        if (pad.buttons & SCE_CTRL_RTRIGGER) {
            int v = engine->volume + 512;
            audio_engine_set_volume(engine, v > MAX_VOLUME ? MAX_VOLUME : v);
        }
    }
    else if (ui->current_screen == UI_SCREEN_PLAYLIST) {
        Playlist *pl = ui->playlist;
        if (!pl) return;
        if (pressed.buttons & SCE_CTRL_DOWN && ui->selected < pl->count - 1) ui->selected++;
        if (pressed.buttons & SCE_CTRL_UP   && ui->selected > 0)             ui->selected--;
        if (pressed.buttons & SCE_CTRL_CIRCLE) {
            ui->current_screen = ui->prev_screen;
        }
        /* cross in queue: jump to that track */
        if (pressed.buttons & SCE_CTRL_CROSS && ui->playlist && ui->selected < ui->playlist->count) {
            playlist_set(ui->playlist, ui->selected);
            const char *path = playlist_get_current(ui->playlist);
            if (path) {
                audio_engine_play(engine, path);
                metadata_free(&ui->current_meta);
                metadata_load(&ui->current_meta, path);
                ui->prev_screen    = UI_SCREEN_PLAYLIST;
                ui->current_screen = UI_SCREEN_NOW_PLAYING;
            }
        }
    }
}

static void draw_progress(AudioEngine *engine, int x, int y, int w, int h)
{
    uint64_t pos = audio_engine_get_position(engine);
    uint64_t dur = audio_engine_get_duration(engine);
    vita2d_draw_rectangle(x, y, w, h, RGBA8(0x3a, 0x3a, 0x3c, 0xff));
    if (dur > 0) {
        int fill = (int)((float)w * pos / dur);
        if (fill > w) fill = w;
        vita2d_draw_rectangle(x, y, fill, h, COLOR_ACCENT);
    }
}

void ui_render(UIState *ui, AudioEngine *engine, FileList *browser)
{
    ui->anim_frame++;

    if (ui->current_screen == UI_SCREEN_BROWSER) {
        int visible = 15;
        if (ui->selected >= ui->list_offset + visible) ui->list_offset = ui->selected - visible + 1;
        if (ui->selected < ui->list_offset) ui->list_offset = ui->selected;

        vita2d_pgf_draw_text(s_font, 20, 30, COLOR_TEXT, 1.0f, browser ? browser->current_dir : "");

        int count = browser ? browser->count : 0;
        for (int i = ui->list_offset; i < count && i < ui->list_offset + visible; i++) {
            int y = 60 + (i - ui->list_offset) * 30;
            if (i == ui->selected)
                vita2d_draw_rectangle(0, y - 20, 960, 28, COLOR_SELECTED);
            FileEntry *e = &browser->entries[i];
            char display[MAX_FILENAME_LEN + 8];
            if (e->is_directory) snprintf(display, sizeof(display), "[DIR] %s", e->name);
            else                 snprintf(display, sizeof(display), "%s", e->name);
            vita2d_pgf_draw_text(s_font, 20, y, COLOR_TEXT, 0.8f, display);
        }
        if (count == 0)
            vita2d_pgf_draw_text(s_font, 20, 100, COLOR_DIM, 0.8f, "No files found.");
        vita2d_pgf_draw_text(s_font, 20, 530, COLOR_DIM, 0.7f, "X: play/open  O: back");
    }
    else if (ui->current_screen == UI_SCREEN_NOW_PLAYING) {
        vita2d_draw_rectangle(0, 0, 960, 50, RGBA8(0x2c, 0x2c, 0x2e, 0xff));
        vita2d_pgf_draw_text(s_font, 20, 40, COLOR_TEXT, 0.9f, "Now Playing");

        /* album art */
        int art_x = 380, art_y = 70, art_size = 200;
        if (ui->album_art_tex) {
            float sw = (float)art_size / vita2d_texture_get_width(ui->album_art_tex);
            float sh = (float)art_size / vita2d_texture_get_height(ui->album_art_tex);
            vita2d_draw_texture_scale(ui->album_art_tex, art_x, art_y, sw, sh);
        } else {
            vita2d_draw_rectangle(art_x, art_y, art_size, art_size, RGBA8(0x2c, 0x2c, 0x2e, 0xff));
        }

        TrackMetadata *meta = &ui->current_meta;
        const char *title  = meta->title[0]  ? meta->title  : engine->current_track;
        const char *artist = meta->artist[0] ? meta->artist : "Unknown Artist";

        vita2d_pgf_draw_text(s_font, 20, 110, COLOR_TEXT,  1.0f, title);
        vita2d_pgf_draw_text(s_font, 20, 145, COLOR_DIM,   0.85f, artist);

        const char *state_str = (engine->state == PLAYBACK_PAUSED) ? "PAUSED" : "PLAYING";
        unsigned int state_col = (engine->state == PLAYBACK_PAUSED) ? COLOR_DIM : COLOR_CURRENT;
        vita2d_pgf_draw_text(s_font, 20, 175, state_col, 0.8f, state_str);

        draw_progress(engine, 20, 480, 920, 8);

        char timebuf[64];
        uint64_t pos_s = audio_engine_get_position(engine) / 1000;
        uint64_t dur_s = audio_engine_get_duration(engine) / 1000;
        snprintf(timebuf, sizeof(timebuf), "%02llu:%02llu / %02llu:%02llu",
                 pos_s / 60, pos_s % 60, dur_s / 60, dur_s % 60);
        vita2d_pgf_draw_text(s_font, 20, 465, COLOR_DIM, 0.75f, timebuf);
        vita2d_pgf_draw_text(s_font, 20, 530, COLOR_DIM, 0.7f,
                             "X: pause  O: back  T: queue  L/R: vol");
    }
    else if (ui->current_screen == UI_SCREEN_PLAYLIST) {
        vita2d_draw_rectangle(0, 0, 960, 50, RGBA8(0x2c, 0x2c, 0x2e, 0xff));
        vita2d_pgf_draw_text(s_font, 20, 40, COLOR_TEXT, 0.9f, "Queue");

        Playlist *pl = ui->playlist;
        if (!pl) return;

        int visible = 15;
        if (ui->selected >= ui->list_offset + visible) ui->list_offset = ui->selected - visible + 1;
        if (ui->selected < ui->list_offset) ui->list_offset = ui->selected;

        for (int i = ui->list_offset; i < pl->count && i < ui->list_offset + visible; i++) {
            int y = 60 + (i - ui->list_offset) * 30;
            if (i == ui->selected)
                vita2d_draw_rectangle(0, y - 20, 960, 28, COLOR_SELECTED);
            unsigned int col = (i == pl->current) ? COLOR_ACCENT : COLOR_TEXT;
            vita2d_pgf_draw_text(s_font, 20, y, col, 0.8f, pl->entries[i].name);
        }
        if (pl->count == 0)
            vita2d_pgf_draw_text(s_font, 20, 100, COLOR_DIM, 0.8f, "Queue is empty.");
        vita2d_pgf_draw_text(s_font, 20, 530, COLOR_DIM, 0.7f, "O: back");
    }
}
