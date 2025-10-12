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

    /* analytical gain compensation: sweep frequencies and find worst-case gain,
     * then scale down preamp to ensure we never exceed 0 dBFS */
    float max_gain = 0.0f;
    float sr = (float)sample_rate;
    for (int k = 0; k < 512; k++) {
        float f  = 20.0f * powf(1000.0f, (float)k / 511.0f);
        float w  = 2.0f * 3.14159265f * f / sr;
        float re = 1.0f, im = 0.0f;
        for (int b = 0; b < EQ_BANDS; b++) {
            BiquadFilter *bf = &eq->bands[b];
            /* evaluate H(e^jw) = (b0 + b1*e^-jw + b2*e^-j2w) /
             *                    (1  + a1*e^-jw + a2*e^-j2w)  */
            float c = cosf(w), s = sinf(w);
            float c2 = cosf(2*w), s2 = sinf(2*w);
            float nr = bf->b0 + bf->b1*c  + bf->b2*c2;
            float ni = -bf->b1*s - bf->b2*s2;
            float dr = 1.0f  + bf->a1*c  + bf->a2*c2;
            float di = -bf->a1*s - bf->a2*s2;
            float denom = dr*dr + di*di;
            float outr = (nr*dr + ni*di) / denom;
            float outi = (ni*dr - nr*di) / denom;
            float tmp_re = re*outr - im*outi;
            float tmp_im = re*outi + im*outr;
            re = tmp_re; im = tmp_im;
        }
        float mag = sqrtf(re*re + im*im);
        if (mag > max_gain) max_gain = mag;
    }
    /* store output_gain so eq_process can apply it */
    float preamp_lin = powf(10.0f, eq->preamp / 20.0f);
    if (max_gain > 0.0f) {
        float limit = preamp_lin / max_gain;
        if (limit < preamp_lin) eq->output_gain = limit;
        else                    eq->output_gain = preamp_lin;
    } else {
        eq->output_gain = preamp_lin;
    }
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

    /* use pre-computed output_gain (includes gain compensation) */
    float gain = (eq->output_gain > 0.0f) ? eq->output_gain
                                          : powf(10.0f, eq->preamp / 20.0f);

    for (uint32_t i = 0; i < frames * 2; i++) {
        float s = buf[i] * gain;
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

/* simple binary save/load — just dump the gains and enabled flag */
#pragma pack(push, 1)
typedef struct { uint32_t magic; float gains[EQ_BANDS]; float preamp; uint8_t enabled; } EQFile;
#pragma pack(pop)
#define EQ_MAGIC 0x56574551 /* VWEQ */

void eq_save(const Equalizer *eq, const char *path)
{
    if (!eq || !path) return;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    EQFile ef;
    ef.magic   = EQ_MAGIC;
    ef.preamp  = eq->preamp;
    ef.enabled = eq->enabled ? 1 : 0;
    for (int i = 0; i < EQ_BANDS; i++) ef.gains[i] = eq->gains[i];
    fwrite(&ef, sizeof(ef), 1, f);
    fclose(f);
}

int eq_load(Equalizer *eq, const char *path, uint32_t sample_rate)
{
    if (!eq || !path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    EQFile ef;
    int ok = (fread(&ef, sizeof(ef), 1, f) == 1 && ef.magic == EQ_MAGIC);
    fclose(f);
    if (!ok) return -1;
    eq->preamp  = ef.preamp;
    eq->enabled = ef.enabled != 0;
    for (int i = 0; i < EQ_BANDS; i++) eq->gains[i] = ef.gains[i];
    eq_update_coefficients(eq, sample_rate);
    return 0;
}
