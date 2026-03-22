/*
 * VitaWave – PS Vita Homebrew Music Player
 * main.c – entry point, global subsystem instances, main loop
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/stat.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <vita2d.h>

#include "audio_engine.h"
#include "decoder.h"
#include "equalizer.h"
#include "file_browser.h"
#include "media_db.h"
#include "metadata.h"
#include "playlist.h"
#include "ui.h"
#include "visualizer.h"
#include "globals.h"
#include "theme.h"

static Equalizer g_eq;

/* ── Background DB rebuild thread ───────────────────────────────────────── */
static int db_rebuild_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    media_db_rebuild();
    sceKernelExitThread(0);
    return 0;
}

/* ── Global subsystem instances ─────────────────────────────────────────── */
static AudioEngine    g_engine;
static Playlist      *g_playlist     = NULL;
static FileList      *g_browser      = NULL;
static UIState        g_ui;
static Visualizer     g_vis;
static TrackMetadata  g_current_meta;
static PlaylistManager g_playlist_manager;
static ThemeManager   g_theme_mgr;


/* ── Global accessors ────────────────────────────────────────────────────── */
AudioEngine     *get_audio_engine    (void) { return &g_engine;            }
Playlist        *get_playlist        (void) { return g_playlist;            }
FileList        *get_browser         (void) { return g_browser;             }
UIState         *get_ui_state        (void) { return &g_ui;                 }
Visualizer      *get_visualizer      (void) { return &g_vis;                }
TrackMetadata   *get_current_meta    (void) { return &g_current_meta;       }
PlaylistManager *get_playlist_manager(void) { return &g_playlist_manager;   }
ThemeManager    *get_theme_manager   (void) { return &g_theme_mgr;          }
Equalizer       *get_equalizer       (void) { return &g_eq;                 }

/* ── FPS limiter helper ───────────────────────────────────────────────────── */
static void fps_limit(uint64_t frame_start_us)
{
    const uint64_t TARGET_FRAME_US = 1000000ULL / UI_FPS;
    uint64_t now = sceKernelGetProcessTimeWide();
    uint64_t elapsed = now - frame_start_us;
    if (elapsed < TARGET_FRAME_US) {
        sceKernelDelayThread((unsigned int)(TARGET_FRAME_US - elapsed));
    }
}

/* ── Application entry point ─────────────────────────────────────────────── */
int main(void)
{
    int ret;

    /* ── AppUtil init (required before sceAppUtilMusicMount) ── */
    {
        SceAppUtilInitParam  init_param;
        SceAppUtilBootParam  boot_param;
        memset(&init_param, 0, sizeof(init_param));
        memset(&boot_param, 0, sizeof(boot_param));
        sceAppUtilInit(&init_param, &boot_param);
    }
    sceAppUtilMusicMount();

    /* ── Power management ── */
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    /* ── vita2d init ── */
    vita2d_init();
    vita2d_set_clear_color(COLOR_BG);  /* off-white, matches Apple Music theme */

    /* ── Controller ── */
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

    /* ── Audio engine ── */
    memset(&g_engine, 0, sizeof(g_engine));
    ret = audio_engine_init(&g_engine);
    if (ret < 0) {
        /* non-fatal – we can still browse files */
        vita2d_set_clear_color(0xFF000033);
    }

    /* Equalizer */
    eq_init(&g_eq);
    eq_load(&g_eq);
    g_engine.eq = &g_eq;

    /* ── File browser ── */
    g_browser = file_browser_init();
    if (!g_browser) {
        goto cleanup;
    }

    /* Rebuild system music DB in the background — only affects the Vita's
     * system Music app, not VitaWave's own file browser.                    */
    {
        SceUID db_tid = sceKernelCreateThread("VitaWave_db",
                            db_rebuild_thread, 0x10000060, 0x10000, 0, 0, NULL);
        if (db_tid >= 0) sceKernelStartThread(db_tid, 0, NULL);
    }

    /* Scan music directory */
    file_browser_scan_dir(g_browser, MUSIC_ROOT);

    /* ── Playlist ── */
    g_playlist = playlist_create("Default");
    if (!g_playlist) {
        goto cleanup;
    }

    playlist_manager_init(&g_playlist_manager);
    playlist_manager_load(&g_playlist_manager);

    /* ── Theme system ── */
    sceIoMkdir("ux0:data/VitaWave",        0777);
    sceIoMkdir("ux0:data/VitaWave/themes", 0777);
    theme_manager_init(&g_theme_mgr);

    /* ── Visualizer ── */
    memset(&g_vis, 0, sizeof(g_vis));
    ret = visualizer_init(&g_vis);
    if (ret < 0) {
        /* non-fatal */
    }

    /* ── UI ── */
    memset(&g_ui, 0, sizeof(g_ui));
    ret = ui_init(&g_ui);
    if (ret < 0) {
        goto cleanup;
    }

    g_ui.theme_mgr = &g_theme_mgr;

    /* Restore custom EQ presets, then validate the saved preset index */
    eq_custom_load(g_ui.eq_custom, &g_ui.eq_custom_count);
    if (g_eq.preset_idx >= EQ_PRESET_COUNT + g_ui.eq_custom_count)
        g_eq.preset_idx = -1;
    g_ui.eq_preset_idx = g_eq.preset_idx;

    theme_manager_restore(&g_theme_mgr, &g_ui);

    /* ── Main loop ── */
    while (1) {
        uint64_t frame_start = sceKernelGetProcessTimeWide();

        /* Handle input */
        ui_handle_input(&g_ui, &g_engine, g_playlist, g_browser, &g_vis);

        /* Update logic */
        ui_update(&g_ui, &g_vis);
        visualizer_update(&g_vis);

        /* Feed latest PCM samples to the visualizer */
        {
            static int16_t vis_samples[FFT_SIZE * 2];
            uint32_t n = audio_engine_get_visualizer_data(&g_engine,
                             vis_samples, FFT_SIZE * 2);
            if (n > 0) {
                visualizer_process_samples(&g_vis, vis_samples, n);
            }
        }

        /* Render */
        vita2d_start_drawing();
        vita2d_clear_screen();
        ui_render(&g_ui, &g_engine, g_playlist, g_browser, &g_vis);
        vita2d_end_drawing();
        vita2d_swap_buffers();
        fps_limit(frame_start);
    }

cleanup:
    theme_manager_free(&g_theme_mgr);
    ui_destroy(&g_ui);
    visualizer_destroy(&g_vis);
    playlist_manager_save(&g_playlist_manager);
    playlist_manager_destroy(&g_playlist_manager);
    if (g_playlist) playlist_destroy(g_playlist);
    if (g_browser)  file_browser_destroy(g_browser);
    audio_engine_destroy(&g_engine);
    metadata_free(&g_current_meta);
    sceAppUtilMusicUmount();
    vita2d_fini();

    sceKernelExitProcess(0);
    return 0;
}
