#include <math.h>
#include <string.h>
#include <vita2d.h>
#include "visualizer.h"

void vis_init(Visualizer *v)
{
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->fft_cfg = kiss_fft_alloc(VIS_FFT_SIZE, 0, NULL, NULL);
}

void vis_destroy(Visualizer *v)
{
    if (!v) return;
    if (v->fft_cfg) { kiss_fft_free(v->fft_cfg); v->fft_cfg = NULL; }
}

void vis_feed(Visualizer *v, const int16_t *samples, uint32_t frames)
{
    if (!v || !v->fft_cfg || !samples) return;

    uint32_t n = frames < VIS_FFT_SIZE ? frames : VIS_FFT_SIZE;
    for (uint32_t i = 0; i < n; i++) {
        /* mix stereo to mono */
        float s = (samples[i * 2] + samples[i * 2 + 1]) * 0.5f / 32768.0f;
        /* Hann window */
        float w = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * i / (VIS_FFT_SIZE - 1)));
        v->fft_in[i].r = s * w;
        v->fft_in[i].i = 0.0f;
    }
    for (uint32_t i = n; i < VIS_FFT_SIZE; i++) { v->fft_in[i].r = 0; v->fft_in[i].i = 0; }

    kiss_fft(v->fft_cfg, v->fft_in, v->fft_out);

    /* compute bar magnitudes */
    int bins_per_bar = (VIS_FFT_SIZE / 2) / VIS_BARS;
    for (int b = 0; b < VIS_BARS; b++) {
        float mag = 0.0f;
        for (int k = 0; k < bins_per_bar; k++) {
            int idx = b * bins_per_bar + k + 1; /* skip DC */
            float re = v->fft_out[idx].r, im = v->fft_out[idx].i;
            mag += sqrtf(re*re + im*im);
        }
        mag = mag / bins_per_bar;
        float db = (mag > 0.0f) ? 20.0f * log10f(mag) + 60.0f : 0.0f;
        if (db < 0.0f) db = 0.0f;
        if (db > 60.0f) db = 60.0f;
        float target = db / 60.0f;
        /* smooth */
        v->bar_heights[b] = v->bar_heights[b] * 0.7f + target * 0.3f;
    }
}

void vis_render(Visualizer *v, int x, int y, int w, int h)
{
    if (!v) return;
    int bar_w = (w - VIS_BARS + 1) / VIS_BARS;
    if (bar_w < 2) bar_w = 2;
    for (int b = 0; b < VIS_BARS; b++) {
        int bx = x + b * (bar_w + 1);
        int bh = (int)(v->bar_heights[b] * h);
        if (bh < 2) bh = 2;
        vita2d_draw_rectangle(bx, y + h - bh, bar_w, bh, RGBA8(0xfc, 0x3c, 0x44, 0xff));
    }
}
