#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpg123.h>

#include "decoder.h"

struct Decoder {
    mpg123_handle *mh;
    DecoderInfo    info;
    DecoderState   state;
};

static bool s_mpg123_init = false;

Decoder *decoder_open(const char *filepath)
{
    if (!filepath) return NULL;

    const char *ext = strrchr(filepath, '.');
    if (!ext || strcasecmp(ext + 1, "mp3") != 0) return NULL;

    if (!s_mpg123_init) {
        mpg123_init();
        s_mpg123_init = true;
    }

    Decoder *dec = (Decoder *)calloc(1, sizeof(Decoder));
    if (!dec) return NULL;

    int err;
    dec->mh = mpg123_new(NULL, &err);
    if (!dec->mh) { free(dec); return NULL; }

    if (mpg123_open(dec->mh, filepath) != MPG123_OK) {
        mpg123_delete(dec->mh);
        free(dec);
        return NULL;
    }

    long rate; int channels, encoding;
    mpg123_getformat(dec->mh, &rate, &channels, &encoding);
    mpg123_format_none(dec->mh);
    mpg123_format(dec->mh, rate, MPG123_STEREO, MPG123_ENC_SIGNED_16);

    dec->info.sample_rate = (uint32_t)rate;
    dec->info.channels    = 2;
    dec->info.duration_ms = 0;
    strncpy(dec->info.filepath, filepath, sizeof(dec->info.filepath) - 1);
    dec->state = DECODER_STATE_OPEN;
    return dec;
}

void decoder_close(Decoder *dec)
{
    if (!dec) return;
    if (dec->mh) { mpg123_close(dec->mh); mpg123_delete(dec->mh); }
    free(dec);
}

int decoder_decode_frames(Decoder *dec, int16_t *out, uint32_t frames, uint32_t *frames_decoded)
{
    if (!dec || dec->state != DECODER_STATE_OPEN) return -1;
    size_t bytes = 0;
    int rc = mpg123_read(dec->mh, (unsigned char *)out,
                         frames * 2 * sizeof(int16_t), &bytes);
    *frames_decoded = (uint32_t)(bytes / (2 * sizeof(int16_t)));
    if (rc == MPG123_DONE) { dec->state = DECODER_STATE_EOF; return 1; }
    if (rc != MPG123_OK && rc != MPG123_NEW_FORMAT) {
        dec->state = DECODER_STATE_ERROR; return -1;
    }
    return 0;
}

DecoderInfo decoder_get_info(Decoder *dec)
{
    DecoderInfo i; memset(&i, 0, sizeof(i));
    return dec ? dec->info : i;
}

DecoderState decoder_get_state(Decoder *dec)
{
    return dec ? dec->state : DECODER_STATE_CLOSED;
}
