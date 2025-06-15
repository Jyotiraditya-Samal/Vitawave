#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpg123.h>

#include "decoder.h"

/* --- private state --- */
typedef struct {
    mpg123_handle *mh;
} Mp3Priv;

static bool s_mpg123_init = false;

static void mp3_close(void *priv) {
    Mp3Priv *p = (Mp3Priv *)priv;
    if (p && p->mh) { mpg123_close(p->mh); mpg123_delete(p->mh); }
    free(p);
}

static int mp3_decode(void *priv, int16_t *out, uint32_t frames, uint32_t *got,
                      DecoderState *state)
{
    Mp3Priv *p = (Mp3Priv *)priv;
    size_t bytes = 0;
    int rc = mpg123_read(p->mh, (unsigned char *)out,
                         frames * 2 * sizeof(int16_t), &bytes);
    *got = (uint32_t)(bytes / (2 * sizeof(int16_t)));
    if (rc == MPG123_DONE) { *state = DECODER_STATE_EOF; return 1; }
    if (rc != MPG123_OK && rc != MPG123_NEW_FORMAT) {
        *state = DECODER_STATE_ERROR; return -1;
    }
    return 0;
}

Decoder *decoder_mp3_open(const char *filepath)
{
    if (!filepath) return NULL;
    const char *ext = strrchr(filepath, '.');
    if (!ext || strcasecmp(ext, ".mp3") != 0) return NULL;

    if (!s_mpg123_init) { mpg123_init(); s_mpg123_init = true; }

    Mp3Priv *p = (Mp3Priv *)calloc(1, sizeof(Mp3Priv));
    if (!p) return NULL;

    int err;
    p->mh = mpg123_new(NULL, &err);
    if (!p->mh) { free(p); return NULL; }

    if (mpg123_open(p->mh, filepath) != MPG123_OK) {
        mpg123_delete(p->mh); free(p); return NULL;
    }

    long rate; int channels, encoding;
    mpg123_getformat(p->mh, &rate, &channels, &encoding);
    mpg123_format_none(p->mh);
    mpg123_format(p->mh, rate, MPG123_STEREO, MPG123_ENC_SIGNED_16);

    Decoder *d = (Decoder *)calloc(1, sizeof(Decoder));
    if (!d) { mp3_close(p); return NULL; }

    d->priv      = p;
    d->close_fn  = mp3_close;
    d->decode_fn = mp3_decode;
    d->state     = DECODER_STATE_OPEN;
    d->info.sample_rate = (uint32_t)rate;
    d->info.channels    = 2;
    d->info.duration_ms = 0;
    strncpy(d->info.filepath, filepath, sizeof(d->info.filepath) - 1);
    return d;
}
