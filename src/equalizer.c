#include <math.h>
#include <string.h>
#include "equalizer.h"

void eq_init(Equalizer *eq)
{
    if (!eq) return;
    memset(eq, 0, sizeof(*eq));
    eq->preamp  = 0.0f;
    eq->enabled = false;
    for (int i = 0; i < EQ_BANDS; i++) eq->gains[i] = 0.0f;
}

static void compute_peaking(BiquadFilter *f, float freq, float gain_db,
                             float Q, float sample_rate)
{
    float A  = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * 3.14159265f * freq / sample_rate;
    float cw = cosf(w0);
    float sw = sinf(w0);
    float alpha = sw / (2.0f * Q);

    float b0 =  1.0f + alpha * A;
    float b1 = -2.0f * cw;
    float b2 =  1.0f - alpha * A;
    float a0 =  1.0f + alpha / A;
    float a1 = -2.0f * cw;
    float a2 =  1.0f - alpha / A;

    f->b0 = b0 / a0;  f->b1 = b1 / a0;  f->b2 = b2 / a0;
    f->a1 = a1 / a0;  f->a2 = a2 / a0;
    f->z1 = f->z2 = 0.0f;
}

void eq_update_coefficients(Equalizer *eq, uint32_t sample_rate)
{
    float Q = 1.41421356f; /* sqrt(2) */
    for (int i = 0; i < EQ_BANDS; i++)
        compute_peaking(&eq->bands[i], (float)k_eq_freqs[i],
                        eq->gains[i], Q, (float)sample_rate);
}

void eq_set_gain(Equalizer *eq, int band, float db)
{
    if (!eq || band < 0 || band >= EQ_BANDS) return;
    eq->gains[band] = db;
}

void eq_set_preamp(Equalizer *eq, float db) { if (eq) eq->preamp = db; }

void eq_process(Equalizer *eq, int16_t *buf, uint32_t frames)
{
    if (!eq || !eq->enabled || !buf) return;

    float preamp_lin = powf(10.0f, eq->preamp / 20.0f);

    for (uint32_t i = 0; i < frames * 2; i++) {
        float s = buf[i] * preamp_lin;
        for (int b = 0; b < EQ_BANDS; b++) {
            BiquadFilter *f = &eq->bands[b];
            float out = f->b0 * s + f->z1;
            f->z1 = f->b1 * s - f->a1 * out + f->z2;
            f->z2 = f->b2 * s - f->a2 * out;
            s = out;
        }
        /* clip */
        int32_t v = (int32_t)s;
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

const EQPreset k_eq_presets[EQ_PRESET_COUNT] = {
    { "Flat",       { 0,0,0,0,0,0,0,0,0,0 },     0.0f },
    { "Bass Boost", { 7,5,3,1,0,0,0,0,0,0 },     -3.0f },
    { "Treble",     { 0,0,0,0,0,0,2,4,6,7 },     -3.0f },
    { "Rock",       { 4,3,0,-1,0,1,3,4,4,3 },    -2.0f },
    { "Pop",        { -1,0,2,3,3,0,-1,-1,0,0 },   0.0f },
};

void eq_load_preset(Equalizer *eq, int idx, uint32_t sample_rate)
{
    if (!eq || idx < 0 || idx >= EQ_PRESET_COUNT) return;
    const EQPreset *p = &k_eq_presets[idx];
    for (int i = 0; i < EQ_BANDS; i++) eq->gains[i] = p->gains[i];
    eq->preamp = p->preamp;
    eq_update_coefficients(eq, sample_rate);
}
