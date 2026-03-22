#ifndef EQUALIZER_H
#define EQUALIZER_H

#include <stdint.h>
#include <stdbool.h>

#define EQ_BANDS        10
#define EQ_PRESET_COUNT  9

extern const float       k_eq_freqs [EQ_BANDS];
extern const char *const k_eq_labels[EQ_BANDS];

typedef struct {
    float b0, b1, b2, a1, a2;   /* normalised biquad coefficients */
    float w1[2], w2[2];          /* delay lines: [0]=L [1]=R       */
} BiquadFilter;

typedef struct {
    float        gains[EQ_BANDS]; /* dB per band, -12..+12 */
    float        preamp;           /* dB preamp,   -12..+12 */
    bool         enabled;
    BiquadFilter filters[EQ_BANDS];
    int          last_sample_rate; /* rate used for last coeff calc      */
    float        output_gain;      /* pre-computed: preamp attenuated by
                                    * cascade peak so output ≤ full scale */
    int          preset_idx;       /* last selected preset; -1 = custom   */
} Equalizer;

typedef struct {
    const char *name;
    float       gains[EQ_BANDS];
    float       preamp;
} EQPreset;

extern const EQPreset k_eq_presets[EQ_PRESET_COUNT];

/* ── Custom (user-saved) presets ─────────────────────────────────────────── */
#define EQ_CUSTOM_PRESET_MAX  8
#define EQ_CUSTOM_NAME_LEN   24

typedef struct {
    char  name[EQ_CUSTOM_NAME_LEN];
    float gains[EQ_BANDS];
    float preamp;
} EQCustomPreset;

void eq_init              (Equalizer *eq);
void eq_set_gain          (Equalizer *eq, int band, float db);
void eq_set_preamp        (Equalizer *eq, float db);
void eq_apply_preset      (Equalizer *eq, int preset_idx);
void eq_update_coefficients(Equalizer *eq, int sample_rate);
void eq_process           (Equalizer *eq, int16_t *buf, uint32_t frame_count);
void eq_reset_state       (Equalizer *eq);
void eq_save              (const Equalizer *eq);
void eq_load              (Equalizer *eq);

void eq_apply_custom_preset(Equalizer *eq, const EQCustomPreset *p);
void eq_custom_save       (const EQCustomPreset presets[], int count);
void eq_custom_load       (EQCustomPreset presets[], int *count);

#endif /* EQUALIZER_H */
