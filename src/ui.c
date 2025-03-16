#include <stdio.h>
#include <string.h>
#include <vita2d.h>
#include <psp2/ctrl.h>

#include "ui.h"
#include "file_browser.h"

#define COLOR_BG        RGBA8(0x1c, 0x1c, 0x1e, 0xff)
#define COLOR_TEXT      RGBA8(0xff, 0xff, 0xff, 0xff)
#define COLOR_SELECTED  RGBA8(0x3a, 0x3a, 0x3c, 0xff)
#define COLOR_DIM       RGBA8(0x8e, 0x8e, 0x93, 0xff)

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
    (void)ui;
    if (s_font) vita2d_free_pgf(s_font);
    s_font = NULL;
}

void ui_handle_input(UIState *ui, FileList *browser)
{
    SceCtrlData pad;
    sceCtrlReadBufferPositive(0, &pad, 1);
    SceCtrlData pressed;
    pressed.buttons = pad.buttons & ~s_old_pad.buttons;
    s_old_pad = pad;

    if (ui->current_screen == UI_SCREEN_BROWSER) {
        int count = browser ? browser->count : 0;

        if (pressed.buttons & SCE_CTRL_DOWN) {
            if (ui->selected < count - 1) ui->selected++;
        }
        if (pressed.buttons & SCE_CTRL_UP) {
            if (ui->selected > 0) ui->selected--;
        }
        if (pressed.buttons & SCE_CTRL_CIRCLE) {
            file_browser_navigate_up(browser);
            ui->selected    = 0;
            ui->list_offset = 0;
        }
        if (pressed.buttons & SCE_CTRL_CROSS && browser && ui->selected < browser->count) {
            FileEntry *e = &browser->entries[ui->selected];
            if (e->is_directory) {
                file_browser_navigate_into(browser, e->path);
                ui->selected    = 0;
                ui->list_offset = 0;
            }
        }
    }
}

void ui_render(UIState *ui, FileList *browser)
{
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
            if (e->is_directory)
                snprintf(display, sizeof(display), "[DIR] %s", e->name);
            else
                snprintf(display, sizeof(display), "%s", e->name);
            vita2d_pgf_draw_text(s_font, 20, y, COLOR_TEXT, 0.8f, display);
        }

        if (count == 0)
            vita2d_pgf_draw_text(s_font, 20, 100, COLOR_DIM, 0.8f, "No files found.");

        vita2d_pgf_draw_text(s_font, 20, 530, COLOR_DIM, 0.7f, "X: open  O: back  START: menu");
    }
}
