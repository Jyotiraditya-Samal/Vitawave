#include <stdio.h>
#include <stdlib.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include <vita2d.h>

int main(void)
{
    vita2d_init();
    vita2d_set_clear_color(RGBA8(0x1c, 0x1c, 0x1e, 0xff));

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

    while (1) {
        SceCtrlData pad;
        sceCtrlReadBufferPositive(0, &pad, 1);

        if (pad.buttons & SCE_CTRL_START)
            break;

        vita2d_start_drawing();
        vita2d_clear_screen();
        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
