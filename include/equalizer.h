#ifndef EQUALIZER_H
#define EQUALIZER_H

#include <stdint.h>
#include <stdbool.h>

#define EQ_BANDS        10
#define EQ_PRESET_COUNT  5

typedef struct {
    /* RBJ peaking EQ biquad - direct form II transposed */
    float b0, b1, b2, a1, a2;
    float z1, z2; /* state */
} BiquadFilter;

typedef struct {
    BiquadFilter bands[EQ_BANDS];
    float        gains[EQ_BANDS]; /* dB, per band */
    float        preamp;          /* dB */
    float        output_gain;     /* linear, computed by update_coefficients */
    bool         enabled;
} Equalizer;

typedef struct {
    const char *name;
    float       gains[EQ_BANDS];
    float       preamp;
} EQPreset;

static const int k_eq_freqs[EQ_BANDS] = {
    31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
};

extern const EQPreset k_eq_presets[EQ_PRESET_COUNT];

void eq_init(Equalizer *eq);
void eq_set_gain(Equalizer *eq, int band, float db);
void eq_set_preamp(Equalizer *eq, float db);
void eq_update_coefficients(Equalizer *eq, uint32_t sample_rate);
void eq_process(Equalizer *eq, int16_t *buf, uint32_t frames);
void eq_load_preset(Equalizer *eq, int preset_idx, uint32_t sample_rate);

#endif
