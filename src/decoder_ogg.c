#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vorbis/vorbisfile.h>

#include "decoder.h"

typedef struct {
    OggVorbis_File vf;
    int            section;
    uint32_t       channels;
} OggPriv;

static void ogg_close(void *priv) {
    OggPriv *p = (OggPriv *)priv;
    if (p) { ov_clear(&p->vf); free(p); }
}

static int ogg_decode(void *priv, int16_t *out, uint32_t frames, uint32_t *got,
                      DecoderState *state)
{
    OggPriv *p = (OggPriv *)priv;
    uint32_t needed = frames * 2 * sizeof(int16_t); /* stereo s16 */
    uint32_t filled = 0;
    unsigned char *dst = (unsigned char *)out;

    while (filled < needed) {
        long ret = ov_read(&p->vf, (char *)(dst + filled),
                           (int)(needed - filled), 0, 2, 1, &p->section);
        if (ret == 0) { *state = DECODER_STATE_EOF; break; }
        if (ret < 0)  { *state = DECODER_STATE_ERROR; break; }
        filled += (uint32_t)ret;
    }

    /* upmix mono — must use current section's channel count, not the cached one,
     * as it can change on chapter/chained stream boundaries */
    vorbis_info *vi = ov_info(&p->vf, -1);
    uint32_t cur_ch = (vi && vi->channels > 0) ? (uint32_t)vi->channels : p->channels;
    *got = filled / (cur_ch * sizeof(int16_t));
    if (cur_ch == 1 && *got > 0) {
        for (int i = (int)*got - 1; i >= 0; i--) {
            out[i*2+1] = out[i];
            out[i*2]   = out[i];
        }
    }
    return (*state == DECODER_STATE_EOF) ? 1 : 0;
}

Decoder *decoder_ogg_open(const char *filepath)
{
    if (!filepath) return NULL;
    const char *ext = strrchr(filepath, '.');
    if (!ext || strcasecmp(ext, ".ogg") != 0) return NULL;

    OggPriv *p = (OggPriv *)calloc(1, sizeof(OggPriv));
    if (!p) return NULL;

    if (ov_fopen(filepath, &p->vf) != 0) { free(p); return NULL; }

    vorbis_info *vi = ov_info(&p->vf, -1);
    if (!vi) { ov_clear(&p->vf); free(p); return NULL; }

    p->channels = (uint32_t)vi->channels;

    Decoder *d = (Decoder *)calloc(1, sizeof(Decoder));
    if (!d) { ogg_close(p); return NULL; }

    d->priv      = p;
    d->close_fn  = ogg_close;
    d->decode_fn = ogg_decode;
    d->state     = DECODER_STATE_OPEN;
    d->info.sample_rate = (uint32_t)vi->rate;
    d->info.channels    = 2;

    ogg_int64_t total_samples = ov_pcm_total(&p->vf, -1);
    d->info.duration_ms = (total_samples > 0 && vi->rate > 0)
        ? (uint64_t)total_samples * 1000ULL / vi->rate : 0;
    strncpy(d->info.filepath, filepath, sizeof(d->info.filepath) - 1);
    return d;
}
