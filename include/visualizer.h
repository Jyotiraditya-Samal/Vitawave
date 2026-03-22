#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <stdint.h>
#include <stdbool.h>
#include "kiss_fft.h"

/* ── Tuning constants ─────────────────────────────────────────────────────── */
#define FFT_SIZE        1024
#define NUM_BANDS       32
#define VIS_BAR_COUNT   32
#define VIS_BAR_WIDTH   24
#define VIS_BAR_GAP     4
#define VIS_BAR_MAX_HEIGHT 200
#define VIS_SMOOTHING   0.8f

/* ── Visualizer modes ─────────────────────────────────────────────────────── */
typedef enum {
    VIS_MODE_BARS     = 0,
    VIS_MODE_WAVEFORM = 1,
    VIS_MODE_DISABLED = 2
} VisMode;

/* ── Animated bar state (spring physics) ─────────────────────────────────── */
typedef struct {
    float height;         /* current display height (pixels) */
    float target_height;  /* desired height from FFT band */
    float velocity;       /* current velocity for spring animation */
} VisBar;

/* ── Visualizer context ───────────────────────────────────────────────────── */
typedef struct {
    /* KissFFT */
    float           *fft_in;        /* FFT_SIZE floats (windowed samples) */
    kiss_fft_cpx    *fft_out;       /* FFT_SIZE/2+1 complex bins */
    kiss_fft_cfg     cfg;

    /* Frequency bands */
    float           *bands;          /* NUM_BANDS magnitude values */
    float           *smoothed_bands; /* exponentially smoothed */
    float           *peak_values;    /* per-band peak hold */

    /* Animated bars */
    VisBar          *bars;           /* VIS_BAR_COUNT entries */

    /* Current mode */
    VisMode          mode;
    int              num_bars;

    /* Raw sample ring buffer (shared with audio engine) */
    int16_t         *sample_buffer;  /* FFT_SIZE * 2 int16_t (stereo) */
    uint32_t         sample_count;   /* samples currently in buffer */
} Visualizer;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * Allocate all internal buffers, set up KissFFT config.
 * Returns 0 on success, negative on failure.
 */
int visualizer_init(Visualizer *vis);

/**
 * Free all resources.
 */
void visualizer_destroy(Visualizer *vis);

/**
 * Feed new PCM samples into the visualizer.
 * samples is an interleaved stereo int16_t buffer, count is the number of
 * int16_t values (so frames = count/2).
 */
void visualizer_process_samples(Visualizer *vis,
                                const int16_t *samples, uint32_t count);

/**
 * Dispatch to the mode-specific render function.
 * Must be called between vita2d_start_drawing / vita2d_end_drawing.
 * x, y, w, h define the render area.
 */
void visualizer_render(Visualizer *vis, int x, int y, int w, int h);

/**
 * Change the visualizer mode.
 */
void visualizer_set_mode(Visualizer *vis, VisMode mode);

/**
 * Per-frame update: apply spring physics to bar heights.
 */
void visualizer_update(Visualizer *vis);

/* ── Mode-specific renderers ─────────────────────────────────────────────── */

void visualizer_render_bars    (Visualizer *vis, int x, int y, int w, int h);
void visualizer_render_waveform(Visualizer *vis, int x, int y, int w, int h);

#endif /* VISUALIZER_H */
