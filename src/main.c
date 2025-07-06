#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <vita2d.h>

#include "file_browser.h"
#include "audio_engine.h"
#include "ui.h"
#include "metadata.h"
#include "playlist.h"

static AudioEngine  g_engine;
static Playlist     g_playlist;

int main(void)
{
    vita2d_init();
    vita2d_set_clear_color(RGBA8(0x1c, 0x1c, 0x1e, 0xff));
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

    memset(&g_engine,   0, sizeof(g_engine));
    memset(&g_playlist, 0, sizeof(g_playlist));
    playlist_init(&g_playlist);

    audio_engine_init(&g_engine);

    FileList *browser = file_browser_init();
    if (!browser) goto cleanup;
    file_browser_scan_dir(browser, "ux0:music");

    UIState ui;
    ui_init(&ui);
    ui.playlist = &g_playlist;

    while (1) {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_START) break;

        if (g_engine.auto_advance) {
            g_engine.auto_advance = false;
            const char *next = playlist_next(&g_playlist);
            if (next) {
                audio_engine_play(&g_engine, next);
                metadata_free(&ui.current_meta);
                metadata_load(&ui.current_meta, next);
            } else {
                audio_engine_stop(&g_engine);
            }
        }

        ui_handle_input(&ui, &g_engine, browser);

        vita2d_start_drawing();
        vita2d_clear_screen();
        ui_render(&ui, &g_engine, browser);
        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    ui_destroy(&ui);
cleanup:
    if (browser) file_browser_destroy(browser);
    audio_engine_destroy(&g_engine);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
