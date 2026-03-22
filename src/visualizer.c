/*
 * VitaWave – visualizer.c
 * FFT-based audio visualizer using KissFFT.
 * Renders spectrum bars, waveform, and polar circle modes via vita2d.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "kiss_fft.h"
#include <vita2d.h>

#include "visualizer.h"
#include "ui.h"   /* for COLOR_VIS_BAR, SCREEN_* */

/* ── Constants ───────────────────────────────────────────────────────────── */
#define M_PI_F 3.14159265358979323846f

/* Gravity for bar fall animation (pixels per frame²) */
#define BAR_GRAVITY   0.5f
/* Spring constant for bar rise */
#define BAR_SPRING    0.35f
/* Damping on bar velocity */
#define BAR_DAMPING   0.75f

/* ── Hanning window ──────────────────────────────────────────────────────── */
static float hanning_window[FFT_SIZE];
static bool  window_initialised = false;

static void init_hanning_window(void)
{
    if (window_initialised) return;
    for (int i = 0; i < FFT_SIZE; i++) {
        hanning_window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI_F * i
                                                   / (float)(FFT_SIZE - 1)));
    }
    window_initialised = true;
}

/* ── Log-frequency band mapping ──────────────────────────────────────────── */
/*
 * Maps FFT_SIZE/2 bins onto NUM_BANDS using logarithmic spacing.
 * low_bin[b] .. high_bin[b] (inclusive) are the FFT bins for band b.
 */
static int low_bin [NUM_BANDS];
static int high_bin[NUM_BANDS];

static void compute_band_mapping(void)
{
    /* Frequency range: 20 Hz – 20 kHz; sample rate = 44100 Hz */
    float freq_min = 20.0f;
    float freq_max = 20000.0f;
    float log_min  = log10f(freq_min);
    float log_max  = log10f(freq_max);
    int   num_bins = FFT_SIZE / 2;

    for (int b = 0; b < NUM_BANDS; b++) {
        float f_lo = powf(10.0f, log_min + (float)b       / NUM_BANDS * (log_max - log_min));
        float f_hi = powf(10.0f, log_min + (float)(b + 1) / NUM_BANDS * (log_max - log_min));
        /* Convert frequency to bin index: bin = freq * FFT_SIZE / sample_rate */
        int lo = (int)(f_lo * FFT_SIZE / 44100.0f);
        int hi = (int)(f_hi * FFT_SIZE / 44100.0f);
        if (lo < 0)          lo = 0;
        if (hi >= num_bins)  hi = num_bins - 1;
        if (hi < lo)         hi = lo;
        low_bin[b]  = lo;
        high_bin[b] = hi;
    }
}

/* ── visualizer_init ─────────────────────────────────────────────────────── */

int visualizer_init(Visualizer *vis)
{
    if (!vis) return -1;
    memset(vis, 0, sizeof(*vis));

    init_hanning_window();
    compute_band_mapping();

    vis->fft_in  = (float *)calloc(FFT_SIZE, sizeof(float));
    vis->fft_out = (kiss_fft_cpx *)calloc(FFT_SIZE, sizeof(kiss_fft_cpx));
    if (!vis->fft_in || !vis->fft_out) goto fail;

    vis->cfg = kiss_fft_alloc(FFT_SIZE, 0, NULL, NULL);
    if (!vis->cfg) goto fail;

    vis->bands          = (float *)calloc(NUM_BANDS, sizeof(float));
    vis->smoothed_bands = (float *)calloc(NUM_BANDS, sizeof(float));
    vis->peak_values    = (float *)calloc(NUM_BANDS, sizeof(float));
    vis->bars           = (VisBar *)calloc(VIS_BAR_COUNT, sizeof(VisBar));
    if (!vis->bands || !vis->smoothed_bands ||
        !vis->peak_values || !vis->bars) goto fail;

    /* Sample buffer: FFT_SIZE stereo frames */
    vis->sample_buffer = (int16_t *)calloc(FFT_SIZE * 2, sizeof(int16_t));
    if (!vis->sample_buffer) goto fail;

    vis->num_bars = VIS_BAR_COUNT;
    vis->mode     = VIS_MODE_BARS;
    return 0;

fail:
    visualizer_destroy(vis);
    return -1;
}

/* ── visualizer_destroy ──────────────────────────────────────────────────── */

void visualizer_destroy(Visualizer *vis)
{
    if (!vis) return;
    kiss_fft_free(vis->cfg);
    free(vis->fft_in);
    free(vis->fft_out);
    free(vis->bands);
    free(vis->smoothed_bands);
    free(vis->peak_values);
    free(vis->bars);
    free(vis->sample_buffer);
    memset(vis, 0, sizeof(*vis));
}

/* ── visualizer_process_samples ─────────────────────────────────────────── */

void visualizer_process_samples(Visualizer *vis,
                                const int16_t *samples, uint32_t count)
{
    if (!vis || !samples || count == 0) return;
    if (!vis->fft_in || !vis->fft_out || !vis->cfg) return;

    /* Copy samples into our ring buffer */
    uint32_t copy = count;
    if (copy > (uint32_t)(FFT_SIZE * 2)) copy = (uint32_t)(FFT_SIZE * 2);
    memcpy(vis->sample_buffer, samples, copy * sizeof(int16_t));
    vis->sample_count = copy;

    /* Build mono FFT input by averaging stereo channels; apply Hanning window */
    uint32_t frames = copy / 2;
    if (frames > FFT_SIZE) frames = FFT_SIZE;

    for (uint32_t i = 0; i < (uint32_t)FFT_SIZE; i++) {
        if (i < frames) {
            float l = (float)samples[i * 2    ] / 32768.0f;
            float r = (float)samples[i * 2 + 1] / 32768.0f;
            vis->fft_in[i] = (l + r) * 0.5f * hanning_window[i];
        } else {
            vis->fft_in[i] = 0.0f;
        }
    }

    /* Build complex input (real FFT via standard complex FFT) */
    kiss_fft_cpx cx_in[FFT_SIZE];
    for (int i = 0; i < FFT_SIZE; i++) {
        cx_in[i].r = vis->fft_in[i];
        cx_in[i].i = 0.0f;
    }

    kiss_fft(vis->cfg, cx_in, vis->fft_out);

    /* Compute magnitude per FFT bin */
    int num_bins = FFT_SIZE / 2;
    float magnitudes[FFT_SIZE / 2];
    for (int i = 0; i < num_bins; i++) {
        float re = vis->fft_out[i].r;
        float im = vis->fft_out[i].i;
        magnitudes[i] = sqrtf(re * re + im * im) / (float)(FFT_SIZE / 2);
    }

    /* Accumulate bins into bands */
    for (int b = 0; b < NUM_BANDS; b++) {
        float sum = 0.0f;
        int   cnt = 0;
        for (int k = low_bin[b]; k <= high_bin[b]; k++) {
            sum += magnitudes[k];
            cnt++;
        }
        vis->bands[b] = (cnt > 0) ? sum / cnt : 0.0f;
    }

    /* Exponential smoothing */
    for (int b = 0; b < NUM_BANDS; b++) {
        vis->smoothed_bands[b] = VIS_SMOOTHING * vis->smoothed_bands[b]
                               + (1.0f - VIS_SMOOTHING) * vis->bands[b];
    }

    /* Peak hold with slow decay */
    for (int b = 0; b < NUM_BANDS; b++) {
        if (vis->smoothed_bands[b] > vis->peak_values[b]) {
            vis->peak_values[b] = vis->smoothed_bands[b];
        } else {
            vis->peak_values[b] *= 0.97f;  /* decay */
        }
    }
}

/* ── visualizer_update ───────────────────────────────────────────────────── */

void visualizer_update(Visualizer *vis)
{
    if (!vis || !vis->bars || !vis->smoothed_bands) return;

    for (int b = 0; b < vis->num_bars && b < NUM_BANDS; b++) {
        VisBar *bar = &vis->bars[b];

        /* Convert band magnitude to target height in pixels */
        float mag = vis->smoothed_bands[b];
        float target = mag * VIS_BAR_MAX_HEIGHT * 8.0f;  /* scale */
        if (target > VIS_BAR_MAX_HEIGHT) target = VIS_BAR_MAX_HEIGHT;

        bar->target_height = target;

        /* Spring physics */
        float diff = bar->target_height - bar->height;
        bar->velocity += diff * BAR_SPRING;
        bar->velocity *= BAR_DAMPING;

        /* Apply velocity */
        bar->height += bar->velocity;

        /* Clamp */
        if (bar->height < 0.0f)                 bar->height = 0.0f;
        if (bar->height > VIS_BAR_MAX_HEIGHT)   bar->height = VIS_BAR_MAX_HEIGHT;
    }
}

/* ── visualizer_set_mode ─────────────────────────────────────────────────── */

void visualizer_set_mode(Visualizer *vis, VisMode mode)
{
    if (!vis) return;
    vis->mode = mode;
}

/* ── visualizer_render ───────────────────────────────────────────────────── */

void visualizer_render(Visualizer *vis, int x, int y, int w, int h)
{
    if (!vis || vis->mode == VIS_MODE_DISABLED) return;
    switch (vis->mode) {
        case VIS_MODE_BARS:     visualizer_render_bars    (vis, x, y, w, h); break;
        case VIS_MODE_WAVEFORM: visualizer_render_waveform(vis, x, y, w, h); break;
        default: break;
    }
}

/* ── visualizer_render_bars ──────────────────────────────────────────────── */

void visualizer_render_bars(Visualizer *vis, int x, int y, int w, int h)
{
    if (!vis || !vis->bars) return;

    int num = vis->num_bars;
    if (num > VIS_BAR_COUNT) num = VIS_BAR_COUNT;

    int bar_w    = VIS_BAR_WIDTH;
    int bar_gap  = VIS_BAR_GAP;
    int total_w  = num * (bar_w + bar_gap) - bar_gap;
    int start_x  = x + (w - total_w) / 2;
    int bottom_y = y + h;

    for (int b = 0; b < num; b++) {
        float height_f = vis->bars[b].height;
        int   bar_h    = (int)height_f;
        if (bar_h < 2) bar_h = 2;
        if (bar_h > h) bar_h = h;

        int bx = start_x + b * (bar_w + bar_gap);
        int by = bottom_y - bar_h;

        /* Apple Music accent color — matches COLOR_VIS_BAR (#FC3C44 in ABGR) */
        vita2d_draw_rectangle(bx, by, bar_w, bar_h, COLOR_VIS_BAR);

        /* Peak dot in white — matches COLOR_TEXT */
        float peak_h = vis->peak_values[b] * VIS_BAR_MAX_HEIGHT * 8.0f;
        if (peak_h > h) peak_h = h;
        int peak_y = bottom_y - (int)peak_h;
        vita2d_draw_rectangle(bx, peak_y - 2, bar_w, 3, COLOR_TEXT);
    }
}

/* ── visualizer_render_waveform ──────────────────────────────────────────── */

void visualizer_render_waveform(Visualizer *vis, int x, int y, int w, int h)
{
    if (!vis || !vis->sample_buffer || vis->sample_count < 2) return;

    int center_y = y + h / 2;
    float x_scale = (float)w / (float)(vis->sample_count / 2);

    int prev_px = x;
    int prev_py = center_y;

    for (uint32_t i = 0; i < vis->sample_count / 2 && i < (uint32_t)w; i++) {
        /* Use left channel only */
        float sample = (float)vis->sample_buffer[i * 2] / 32768.0f;
        int px = x + (int)(i * x_scale);
        int py = center_y - (int)(sample * (h / 2.0f));

        if (py < y)     py = y;
        if (py > y + h) py = y + h;

        if (i > 0) {
            vita2d_draw_line(prev_px, prev_py, px, py, COLOR_VIS_BAR);
        }
        prev_px = px;
        prev_py = py;
    }
}

