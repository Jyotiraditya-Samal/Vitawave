#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <psp2/shellutil.h>
#include <vita2d.h>

#include "file_browser.h"
#include "audio_engine.h"
#include "ui.h"
#include "metadata.h"
#include "playlist.h"

static AudioEngine  g_engine;
static Playlist     g_playlist;

/* populate playlist from directory entries in the browser */
static void fill_playlist_from_dir(FileList *browser)
{
    playlist_clear(&g_playlist);
    for (int i = 0; i < browser->count; i++) {
        FileEntry *e = &browser->entries[i];
        if (!e->is_directory)
            playlist_add(&g_playlist, e->path);
    }
    playlist_sort(&g_playlist);
}

int main(void)
{
    vita2d_init();
    vita2d_set_clear_color(RGBA8(0x1c, 0x1c, 0x1e, 0xff));
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

    /* Declare ourselves as a music player — this lets the OS keep our audio
     * threads alive when the screen turns off or the user goes to LiveArea.
     * Only lock MUSIC_PLAYER, not PS_BTN: locking PS_BTN would prevent the
     * user from navigating home, which is not what we want. */
    sceShellUtilInitEvents(0);
    sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_MUSIC_PLAYER);

    memset(&g_engine,   0, sizeof(g_engine));
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

        /* when user plays a file, fill playlist from its directory */
        if (ui.fill_playlist_request) {
            ui.fill_playlist_request = false;
            fill_playlist_from_dir(browser);
        }

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
