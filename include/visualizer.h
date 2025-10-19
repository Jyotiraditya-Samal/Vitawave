#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <stdint.h>
#include "kiss_fft.h"

#define VIS_FFT_SIZE  1024
#define VIS_BARS      32

typedef struct {
    kiss_fft_cfg    fft_cfg;
    kiss_fft_cpx    fft_in[VIS_FFT_SIZE];
    kiss_fft_cpx    fft_out[VIS_FFT_SIZE];
    float           bar_heights[VIS_BARS];   /* current bar heights (smoothed) */
    float           bar_peaks[VIS_BARS];     /* falling peaks */
} Visualizer;

void vis_init(Visualizer *v);
void vis_destroy(Visualizer *v);
void vis_feed(Visualizer *v, const int16_t *samples, uint32_t frames);
void vis_render(Visualizer *v, int x, int y, int w, int h);

#endif
