#include <stdlib.h>
#include <string.h>

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#include "decoder.h"

typedef struct {
    drflac   *flac;
    uint32_t  src_channels;
} FlacPriv;

static void flac_close(void *priv) {
    FlacPriv *p = (FlacPriv *)priv;
    if (p) { if (p->flac) drflac_close(p->flac); free(p); }
}

static int flac_decode(void *priv, int16_t *out, uint32_t frames, uint32_t *got,
                       DecoderState *state)
{
    FlacPriv *p = (FlacPriv *)priv;
    drflac_uint64 n = drflac_read_pcm_frames_s16(p->flac, frames, out);
    if (n == 0) { *state = DECODER_STATE_EOF; *got = 0; return 1; }

    /* upmix mono to stereo in-place */
    if (p->src_channels == 1) {
        for (int i = (int)n - 1; i >= 0; i--) {
            out[i * 2 + 1] = out[i];
            out[i * 2]     = out[i];
        }
    }
    *got = (uint32_t)n;
    return 0;
}

Decoder *decoder_flac_open(const char *filepath)
{
    if (!filepath) return NULL;
    const char *ext = strrchr(filepath, '.');
    if (!ext || strcasecmp(ext, ".flac") != 0) return NULL;

    drflac *flac = drflac_open_file(filepath, NULL);
    if (!flac) return NULL;

    FlacPriv *p = (FlacPriv *)calloc(1, sizeof(FlacPriv));
    if (!p) { drflac_close(flac); return NULL; }
    p->flac        = flac;
    p->src_channels = flac->channels;

    Decoder *d = (Decoder *)calloc(1, sizeof(Decoder));
    if (!d) { flac_close(p); return NULL; }

    d->priv      = p;
    d->close_fn  = flac_close;
    d->decode_fn = flac_decode;
    d->state     = DECODER_STATE_OPEN;
    d->info.sample_rate = flac->sampleRate;
    d->info.channels    = 2;
    d->info.duration_ms = (flac->sampleRate > 0)
        ? flac->totalPCMFrameCount * 1000ULL / flac->sampleRate : 0;
    strncpy(d->info.filepath, filepath, sizeof(d->info.filepath) - 1);
    return d;
}

/* shared functions live here since this file is always linked */
Decoder *decoder_open(const char *filepath)
{
    Decoder *d = decoder_mp3_open(filepath);
    if (d) return d;
    d = decoder_flac_open(filepath);
    if (d) return d;
    return decoder_ogg_open(filepath);
}

void decoder_close(Decoder *dec)
{
    if (!dec) return;
    if (dec->close_fn) dec->close_fn(dec->priv);
    free(dec);
}

int decoder_decode_frames(Decoder *dec, int16_t *out, uint32_t frames, uint32_t *frames_decoded)
{
    if (!dec || !dec->decode_fn || dec->state == DECODER_STATE_EOF) {
        *frames_decoded = 0; return 1;
    }
    return dec->decode_fn(dec->priv, out, frames, frames_decoded, &dec->state);
}

DecoderInfo decoder_get_info(Decoder *dec)
{
    return dec ? dec->info : (DecoderInfo){0};
}

DecoderState decoder_get_state(Decoder *dec)
{
    return dec ? dec->state : DECODER_STATE_CLOSED;
}
