/*
 * VitaWave – decoder_flac.c
 * FLAC decoding via dr_flac (single-header library).
 *
 * dr_flac is a pure-C, header-only decoder with no internal complexity
 * beyond what we need, uses standard fopen/fread/fseek internally, and
 * avoids the libFLAC internal-state issues seen on Vita.
 */

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG          /* we don't need Ogg-FLAC support */
#include "dr_flac.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "decoder.h"
#include "audio_engine.h"

/* ── Private state ───────────────────────────────────────────────────────── */
typedef struct {
    drflac   *flac;
    uint32_t  src_channels;  /* native channel count in the file */
    bool      eof;
} FlacState;

/* ── Forward declarations ────────────────────────────────────────────────── */
static int  flac_open        (Decoder *dec, const char *filepath);
static void flac_close       (Decoder *dec);
static int  flac_decode_frames(Decoder *dec, int16_t *out,
                               uint32_t frames_req, uint32_t *frames_decoded);
static int  flac_seek        (Decoder *dec, uint64_t position_ms);
static void flac_reset       (Decoder *dec);

/* ── Vtable ──────────────────────────────────────────────────────────────── */
static const DecoderVtable flac_vtable = {
    flac_open,
    flac_close,
    flac_decode_frames,
    flac_seek,
    flac_reset
};

/* ── Public entry point ──────────────────────────────────────────────────── */
int decoder_flac_open(Decoder *dec, const char *filepath)
{
    return flac_open(dec, filepath);
}

/* ── Implementation ──────────────────────────────────────────────────────── */

static int flac_open(Decoder *dec, const char *filepath)
{
    FlacState *st = (FlacState *)calloc(1, sizeof(FlacState));
    if (!st) return -1;

    /* dr_flac uses standard fopen internally; VitaSDK newlib fopen handles
     * ux0: paths, so this works the same as our other decoders.           */
    st->flac = drflac_open_file(filepath, NULL);
    if (!st->flac) {
        free(st);
        return -2;
    }

    st->src_channels = st->flac->channels;

    /* Fill public DecoderInfo – always report stereo output */
    dec->info.sample_rate  = st->flac->sampleRate;
    dec->info.channels     = 2;
    dec->info.bit_depth    = st->flac->bitsPerSample;
    dec->info.total_frames = (uint64_t)st->flac->totalPCMFrameCount;
    if (st->flac->sampleRate > 0 && st->flac->totalPCMFrameCount > 0) {
        dec->info.duration_ms =
            (uint64_t)st->flac->totalPCMFrameCount * 1000ULL
            / st->flac->sampleRate;
    }

    dec->internal = st;
    dec->vtable   = flac_vtable;
    dec->state    = DECODER_STATE_OPEN;
    return 0;
}

static void flac_close(Decoder *dec)
{
    if (!dec || !dec->internal) return;
    FlacState *st = (FlacState *)dec->internal;

    drflac_close(st->flac);
    free(st);
    dec->internal = NULL;
    dec->state    = DECODER_STATE_IDLE;
}

static int flac_decode_frames(Decoder *dec, int16_t *out,
                               uint32_t frames_req, uint32_t *frames_decoded)
{
    if (!dec || !dec->internal || !out || !frames_decoded) return -1;
    FlacState *st = (FlacState *)dec->internal;
    *frames_decoded = 0;

    if (st->eof) {
        dec->state = DECODER_STATE_EOF;
        return 1;
    }

    drflac_uint64 frames_read = 0;

    if (st->src_channels == 2) {
        /* Stereo: decode directly into the caller's output buffer */
        frames_read = drflac_read_pcm_frames_s16(st->flac, frames_req, out);

    } else if (st->src_channels == 1) {
        /* Mono: decode into the first half of the (stereo-sized) output
         * buffer, then upmix in-place by iterating backwards.            */
        frames_read = drflac_read_pcm_frames_s16(st->flac, frames_req, out);
        for (int32_t i = (int32_t)frames_read - 1; i >= 0; i--) {
            int16_t s    = out[i];
            out[i*2 + 1] = s;
            out[i*2 + 0] = s;
        }

    } else {
        /* Multi-channel (rare): decode to a temporary heap buffer,
         * then extract only the first two channels.                      */
        int16_t *tmp = (int16_t *)malloc(
            frames_req * st->src_channels * sizeof(int16_t));
        if (!tmp) return -1;

        frames_read = drflac_read_pcm_frames_s16(st->flac, frames_req, tmp);
        uint32_t ch = st->src_channels;
        for (drflac_uint64 i = 0; i < frames_read; i++) {
            out[i*2 + 0] = tmp[i * ch + 0];
            out[i*2 + 1] = tmp[i * ch + 1];
        }
        free(tmp);
    }

    if (frames_read == 0) {
        st->eof    = true;
        dec->state = DECODER_STATE_EOF;
        return 1;
    }

    *frames_decoded = (uint32_t)frames_read;
    dec->state = DECODER_STATE_DECODING;
    return 0;
}

static int flac_seek(Decoder *dec, uint64_t position_ms)
{
    if (!dec || !dec->internal) return -1;
    FlacState *st = (FlacState *)dec->internal;

    if (st->flac->sampleRate == 0) return -2;
    drflac_uint64 target =
        (drflac_uint64)position_ms * st->flac->sampleRate / 1000ULL;

    if (!drflac_seek_to_pcm_frame(st->flac, target)) return -2;

    st->eof    = false;
    dec->state = DECODER_STATE_OPEN;
    return 0;
}

static void flac_reset(Decoder *dec)
{
    flac_seek(dec, 0);
}
