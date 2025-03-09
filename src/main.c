#include <stdio.h>
#include <stdlib.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <vita2d.h>

#include "file_browser.h"
#include "ui.h"

int main(void)
{
    vita2d_init();
    vita2d_set_clear_color(RGBA8(0x1c, 0x1c, 0x1e, 0xff));
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

    FileList *browser = file_browser_init();
    if (!browser) goto cleanup;

    file_browser_scan_dir(browser, MUSIC_ROOT);

    UIState ui;
    ui_init(&ui);

    while (1) {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_START) break;

        ui_handle_input(&ui, browser);

        vita2d_start_drawing();
        vita2d_clear_screen();
        ui_render(&ui, browser);
        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    ui_destroy(&ui);
cleanup:
    if (browser) file_browser_destroy(browser);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
