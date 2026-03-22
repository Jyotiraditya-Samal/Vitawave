/*
 * VitaWave – equalizer.c
 * 10-band parametric EQ using RBJ Audio EQ Cookbook biquad peak filters.
 * Processed in the audio thread (Direct Form II Transposed, single precision).
 */

#include <math.h>
#include <string.h>
#include <stdio.h>
#include "equalizer.h"

#define EQ_PATH         "ux0:data/VitaWave/eq.dat"
#define EQ_CUSTOM_PATH  "ux0:data/VitaWave/eq_custom.dat"
#define EQ_CUSTOM_MAGIC   0x56574543u   /* "VWEC" */
#define EQ_CUSTOM_VERSION 1u
#define EQ_MAGIC   0x56574551u   /* "VWEQ" */
#define EQ_VERSION 2u
#define EQ_Q       1.41421356f   /* sqrt(2) — standard octave-band Q */

/* ── Public constant tables ──────────────────────────────────────────────── */

const float k_eq_freqs[EQ_BANDS] = {
    31.0f, 62.0f, 125.0f, 250.0f, 500.0f,
    1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};

const char *const k_eq_labels[EQ_BANDS] = {
    "31", "62", "125", "250", "500", "1K", "2K", "4K", "8K", "16K"
};

const EQPreset k_eq_presets[EQ_PRESET_COUNT] = {
    { "Flat",         { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},  0.0f },
    /* Preamp pulled down to give bass boost headroom without clipping */
    { "Bass Boost",   { 7, 5, 3, 1, 0, 0, 0, 0, 0, 0}, -4.0f },
    { "Treble Boost", { 0, 0, 0, 0, 0, 0, 2, 4, 6, 7}, -3.0f },
    { "Rock",         { 4, 3, 2, 1,-1,-1, 1, 2, 3, 4}, -2.0f },
    { "Pop",          {-1, 0, 2, 3, 3, 0,-1,-1,-1,-1},  0.0f },
    { "Jazz",         { 3, 2, 1, 2,-1,-1, 0, 1, 2, 3},  0.0f },
    { "Electronic",   { 5, 4, 0,-2,-2, 0, 2, 4, 5, 5}, -3.0f },
    { "Vocal",        {-2,-3,-3, 0, 3, 5, 5, 3, 0,-2},  0.0f },
    { "Classical",    { 4, 3, 2, 2,-1,-1, 0, 2, 3, 4}, -3.0f },
};

/* ── Internal helpers ────────────────────────────────────────────────────── */

/*
 * Evaluate |H(e^jω)| for a single biquad at normalised digital frequency ω.
 * Coefficients are stored normalised (a0 = 1 absorbed into b0/b2/a1/a2).
 */
static float biquad_mag(const BiquadFilter *f, float omega)
{
    float cosw  = cosf(omega);
    float cos2w = cosf(2.0f * omega);
    float sinw  = sinf(omega);
    float sin2w = sinf(2.0f * omega);

    float nr = f->b0 + f->b1 * cosw  + f->b2 * cos2w;
    float ni = -(f->b1 * sinw + f->b2 * sin2w);
    float dr = 1.0f + f->a1 * cosw  + f->a2 * cos2w;
    float di = -(f->a1 * sinw + f->a2 * sin2w);

    float num2 = nr * nr + ni * ni;
    float den2 = dr * dr + di * di;
    return (den2 > 1e-30f) ? sqrtf(num2 / den2) : 1.0f;
}

/*
 * Sweep 512 log-spaced frequencies 20 Hz – 20 kHz and return the peak
 * linear gain produced by the entire 10-band cascade.
 */
#define SWEEP_POINTS 512

static float compute_max_cascade_gain_lin(const Equalizer *eq, float fs)
{
    float peak = 0.0f;
    float log_lo = logf(20.0f);
    float log_hi = logf(20000.0f);

    for (int k = 0; k < SWEEP_POINTS; k++) {
        float freq  = expf(log_lo + (log_hi - log_lo) * k / (float)(SWEEP_POINTS - 1));
        float omega = 2.0f * 3.14159265f * freq / fs;
        float gain  = 1.0f;
        for (int b = 0; b < EQ_BANDS; b++)
            gain *= biquad_mag(&eq->filters[b], omega);
        if (gain > peak) peak = gain;
    }
    return peak < 1.0f ? 1.0f : peak;  /* never below unity */
}

static void calc_peak(BiquadFilter *f, float freq, float gain_db, float fs)
{
    if (gain_db == 0.0f) {
        f->b0 = 1.0f; f->b1 = 0.0f; f->b2 = 0.0f;
        f->a1 = 0.0f; f->a2 = 0.0f;
        return;
    }
    float A      = powf(10.0f, gain_db / 40.0f);
    float w0     = 2.0f * 3.14159265f * freq / fs;
    float cosw0  = cosf(w0);
    float sinw0  = sinf(w0);
    float alpha  = sinw0 / (2.0f * EQ_Q);
    float a0_inv = 1.0f / (1.0f + alpha / A);
    f->b0 = (1.0f + alpha * A) * a0_inv;
    f->b1 = (-2.0f * cosw0)    * a0_inv;
    f->b2 = (1.0f - alpha * A) * a0_inv;
    f->a1 = (-2.0f * cosw0)    * a0_inv;
    f->a2 = (1.0f - alpha / A) * a0_inv;
}

static inline float biquad_df2t(BiquadFilter *f, int ch, float x)
{
    float y   = f->b0 * x + f->w1[ch];
    f->w1[ch] = f->b1 * x - f->a1 * y + f->w2[ch];
    f->w2[ch] = f->b2 * x - f->a2 * y;
    return y;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void eq_init(Equalizer *eq)
{
    memset(eq, 0, sizeof(*eq));
    eq->enabled          = false;
    eq->last_sample_rate = 0;
    eq->output_gain      = 1.0f;
    eq->preset_idx       = 0;
}

void eq_set_gain(Equalizer *eq, int band, float db)
{
    if (band < 0 || band >= EQ_BANDS) return;
    if (db >  12.0f) db =  12.0f;
    if (db < -12.0f) db = -12.0f;
    eq->gains[band]      = db;
    eq->last_sample_rate = 0;
}

void eq_set_preamp(Equalizer *eq, float db)
{
    if (db >  12.0f) db =  12.0f;
    if (db < -12.0f) db = -12.0f;
    eq->preamp = db;
}

void eq_apply_preset(Equalizer *eq, int idx)
{
    if (idx < 0 || idx >= EQ_PRESET_COUNT) return;
    for (int i = 0; i < EQ_BANDS; i++)
        eq->gains[i] = k_eq_presets[idx].gains[i];
    eq->preamp           = k_eq_presets[idx].preamp;
    eq->last_sample_rate = 0;
}

void eq_update_coefficients(Equalizer *eq, int sample_rate)
{
    if (sample_rate <= 0) return;
    for (int i = 0; i < EQ_BANDS; i++)
        calc_peak(&eq->filters[i], k_eq_freqs[i], eq->gains[i], (float)sample_rate);

    /* Analytical gain compensation: find the worst-case cascade peak across
     * the audible spectrum, then back off preamp so the chain never clips. */
    float preamp_lin      = powf(10.0f, eq->preamp / 20.0f);
    float max_cascade_lin = compute_max_cascade_gain_lin(eq, (float)sample_rate);
    /* output_gain = the single multiplier used in eq_process instead of
     * raw preamp_lin.  It is the smaller of:
     *   a) the user's requested preamp gain, and
     *   b) whatever gain keeps the loudest cascade peak at ≤ 0 dBFS. */
    float headroom_gain   = 1.0f / max_cascade_lin;
    eq->output_gain       = preamp_lin < headroom_gain ? preamp_lin : headroom_gain;

    eq->last_sample_rate = sample_rate;
}

void eq_reset_state(Equalizer *eq)
{
    for (int i = 0; i < EQ_BANDS; i++)
        for (int ch = 0; ch < 2; ch++)
            eq->filters[i].w1[ch] = eq->filters[i].w2[ch] = 0.0f;
}

void eq_process(Equalizer *eq, int16_t *buf, uint32_t frame_count)
{
    if (!eq->enabled) return;

    /* output_gain is pre-computed in eq_update_coefficients: it equals
     * min(preamp_lin, 1/max_cascade_gain) so the analytical worst-case
     * cannot clip.  The two-pass peak scan below is a last-resort safety
     * net for constructive multi-frequency additions that slip past the
     * sweep (e.g. transients hitting multiple boosted bands simultaneously). */
    float gain_lin = eq->output_gain;
    uint32_t total = frame_count * 2;  /* interleaved stereo samples */

    /* ── Pass 1: run filters + output_gain into a float staging buffer ── */
    static float stage[4096];  /* GRANULE_SIZE*2 = 1920 max; 4096 is safe */
    if (total > 4096) total = 4096;

    float peak = 1.0f;  /* minimum 1.0 so scale never exceeds 1 */
    for (uint32_t i = 0; i < total; i++) {
        int   ch = (int)(i & 1);
        float x  = (float)buf[i];
        for (int b = 0; b < EQ_BANDS; b++)
            x = biquad_df2t(&eq->filters[b], ch, x);
        x *= gain_lin;
        stage[i] = x;
        float ax = x < 0.0f ? -x : x;
        if (ax > peak) peak = ax;
    }

    /* ── Pass 2: peak-normalize if over full scale, then soft-knee + convert ── */
    float scale = peak > 32767.0f ? 32767.0f / peak : 1.0f;
    for (uint32_t i = 0; i < total; i++) {
        float x = stage[i] * scale;
        /* Soft-knee safety net above 90 % of full scale */
        if      (x >  29491.0f) x =  29491.0f + (x -  29491.0f) * 0.25f;
        else if (x < -29491.0f) x = -29491.0f + (x + 29491.0f) * 0.25f;
        if      (x >  32767.0f) x =  32767.0f;
        else if (x < -32768.0f) x = -32768.0f;
        buf[i] = (int16_t)x;
    }
}

/* ── Persistence ─────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t magic;
    uint32_t version;
    float    gains[EQ_BANDS];
    float    preamp;
    int32_t  enabled;
    int32_t  preset_idx;
} EQFile;

void eq_save(const Equalizer *eq)
{
    FILE *f = fopen(EQ_PATH, "wb");
    if (!f) return;
    EQFile d;
    d.magic   = EQ_MAGIC;
    d.version = EQ_VERSION;
    for (int i = 0; i < EQ_BANDS; i++) d.gains[i] = eq->gains[i];
    d.preamp     = eq->preamp;
    d.enabled    = eq->enabled ? 1 : 0;
    d.preset_idx = (int32_t)eq->preset_idx;
    fwrite(&d, sizeof(d), 1, f);
    fclose(f);
}

void eq_load(Equalizer *eq)
{
    FILE *f = fopen(EQ_PATH, "rb");
    if (!f) return;
    EQFile d;
    if (fread(&d, sizeof(d), 1, f) == 1 &&
        d.magic == EQ_MAGIC && d.version == EQ_VERSION) {
        for (int i = 0; i < EQ_BANDS; i++) eq->gains[i] = d.gains[i];
        eq->preamp           = d.preamp;
        eq->enabled          = d.enabled != 0;
        eq->preset_idx       = (int)d.preset_idx;
        eq->last_sample_rate = 0;
    }
    fclose(f);
}

/* ── Custom preset persistence ───────────────────────────────────────────── */

typedef struct {
    uint32_t      magic;
    uint32_t      version;
    int32_t       count;
    EQCustomPreset presets[EQ_CUSTOM_PRESET_MAX];
} EQCustomFile;

void eq_apply_custom_preset(Equalizer *eq, const EQCustomPreset *p)
{
    for (int i = 0; i < EQ_BANDS; i++)
        eq->gains[i] = p->gains[i];
    eq->preamp           = p->preamp;
    eq->last_sample_rate = 0;
}

void eq_custom_save(const EQCustomPreset presets[], int count)
{
    FILE *f = fopen(EQ_CUSTOM_PATH, "wb");
    if (!f) return;
    EQCustomFile d;
    d.magic   = EQ_CUSTOM_MAGIC;
    d.version = EQ_CUSTOM_VERSION;
    d.count   = count < 0 ? 0 : (count > EQ_CUSTOM_PRESET_MAX ? EQ_CUSTOM_PRESET_MAX : count);
    for (int i = 0; i < d.count; i++) d.presets[i] = presets[i];
    fwrite(&d, sizeof(d), 1, f);
    fclose(f);
}

void eq_custom_load(EQCustomPreset presets[], int *count)
{
    *count = 0;
    FILE *f = fopen(EQ_CUSTOM_PATH, "rb");
    if (!f) return;
    EQCustomFile d;
    if (fread(&d, sizeof(d), 1, f) == 1 &&
        d.magic == EQ_CUSTOM_MAGIC && d.version == EQ_CUSTOM_VERSION) {
        int n = d.count;
        if (n < 0) n = 0;
        if (n > EQ_CUSTOM_PRESET_MAX) n = EQ_CUSTOM_PRESET_MAX;
        for (int i = 0; i < n; i++) presets[i] = d.presets[i];
        *count = n;
    }
    fclose(f);
}
