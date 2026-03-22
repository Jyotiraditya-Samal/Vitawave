/*
 * VitaWave – decoder_ogg.c
 * Ogg/Vorbis decoding via libvorbisfile.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vorbis/vorbisfile.h>

#include "decoder.h"
#include "audio_engine.h"

/* ── Private state ───────────────────────────────────────────────────────── */
typedef struct {
    OggVorbis_File ovf;
    vorbis_info   *vi;
    int            channels;
    long           rate;
    uint64_t       duration_ms;
    int            current_section;
    bool           open;
} OggState;

/* ── Error code mapping ──────────────────────────────────────────────────── */
static int map_ov_error(int err)
{
    switch (err) {
        case OV_FALSE:      return -10;
        case OV_EOF:        return  1;   /* end of stream */
        case OV_HOLE:       return -11;  /* recoverable gap */
        case OV_EREAD:      return -12;
        case OV_EFAULT:     return -13;
        case OV_EIMPL:      return -14;
        case OV_EINVAL:     return -15;
        case OV_ENOTVORBIS: return -16;
        case OV_EBADHEADER: return -17;
        case OV_EVERSION:   return -18;
        case OV_ENOTAUDIO:  return -19;
        case OV_EBADPACKET: return -20;
        case OV_EBADLINK:   return -21;
        case OV_ENOSEEK:    return -22;
        default:            return -99;
    }
}

/* ── Forward declarations ────────────────────────────────────────────────── */
static int  ogg_open        (Decoder *dec, const char *filepath);
static void ogg_close       (Decoder *dec);
static int  ogg_decode_frames(Decoder *dec, int16_t *out,
                              uint32_t frames_req, uint32_t *frames_decoded);
static int  ogg_seek        (Decoder *dec, uint64_t position_ms);
static void ogg_reset       (Decoder *dec);

/* ── Vtable ──────────────────────────────────────────────────────────────── */
static const DecoderVtable ogg_vtable = {
    ogg_open,
    ogg_close,
    ogg_decode_frames,
    ogg_seek,
    ogg_reset
};

/* ── Public entry point ──────────────────────────────────────────────────── */
int decoder_ogg_open(Decoder *dec, const char *filepath)
{
    return ogg_open(dec, filepath);
}

/* ── Implementation ──────────────────────────────────────────────────────── */

static int ogg_open(Decoder *dec, const char *filepath)
{
    OggState *st = (OggState *)calloc(1, sizeof(OggState));
    if (!st) return -1;

    /* Open the Ogg/Vorbis file */
    int err = ov_fopen(filepath, &st->ovf);
    if (err != 0) {
        free(st);
        return map_ov_error(err);
    }
    st->open = true;

    /* Get stream info (logical bitstream 0) */
    st->vi       = ov_info(&st->ovf, -1);
    st->channels = st->vi->channels;  /* source channels (may be 1) */
    st->rate     = st->vi->rate;

    /* Duration */
    double dur_s = ov_time_total(&st->ovf, -1);
    if (dur_s > 0.0) {
        st->duration_ms = (uint64_t)(dur_s * 1000.0);
    }

    /* Fill public info */
    dec->info.sample_rate  = (uint32_t)st->rate;
    dec->info.channels     = 2;   /* always upmix to stereo on decode */
    dec->info.bit_depth    = 16;
    dec->info.duration_ms  = st->duration_ms;
    dec->info.total_frames = (uint64_t)ov_pcm_total(&st->ovf, -1);
    dec->info.bitrate      = (uint32_t)(ov_bitrate(&st->ovf, -1) / 1000);

    dec->internal = st;
    dec->vtable   = ogg_vtable;
    dec->state    = DECODER_STATE_OPEN;
    return 0;
}

static void ogg_close(Decoder *dec)
{
    if (!dec || !dec->internal) return;
    OggState *st = (OggState *)dec->internal;

    if (st->open) {
        ov_clear(&st->ovf);
        st->open = false;
    }
    free(st);
    dec->internal = NULL;
    dec->state    = DECODER_STATE_IDLE;
}

static int ogg_decode_frames(Decoder *dec, int16_t *out,
                              uint32_t frames_req, uint32_t *frames_decoded)
{
    if (!dec || !dec->internal || !out || !frames_decoded) return -1;
    OggState *st    = (OggState *)dec->internal;
    *frames_decoded = 0;

    /* ov_read output parameters */
    const int bigendianp = 0;   /* little-endian */
    const int word       = 2;   /* 16-bit */
    const int sgned      = 1;   /* signed */

    uint32_t bytes_needed  = frames_req * (uint32_t)st->channels * 2u;
    uint32_t bytes_written = 0;
    char    *ptr           = (char *)out;

    while (bytes_written < bytes_needed) {
        long n = ov_read(&st->ovf,
                         ptr + bytes_written,
                         (int)(bytes_needed - bytes_written),
                         bigendianp, word, sgned,
                         &st->current_section);

        if (n == 0) {
            /* EOF */
            dec->state = DECODER_STATE_EOF;
            break;
        }
        if (n == OV_HOLE) {
            /* Corrupt data hole – skip and continue */
            continue;
        }
        if (n < 0) {
            dec->state = DECODER_STATE_ERROR;
            return map_ov_error((int)n);
        }
        bytes_written += (uint32_t)n;
    }

    uint32_t frames = bytes_written / ((uint32_t)st->channels * 2u);

    /* Upmix mono to stereo in-place (iterate backwards to avoid overwrites) */
    if (st->channels == 1 && frames > 0) {
        int16_t *buf = (int16_t *)out;
        for (int32_t i = (int32_t)frames - 1; i >= 0; i--) {
            int16_t s    = buf[i];
            buf[i*2 + 1] = s;
            buf[i*2 + 0] = s;
        }
    }

    *frames_decoded = frames;
    if (*frames_decoded == 0 && dec->state == DECODER_STATE_EOF) {
        return 1;
    }
    dec->state = DECODER_STATE_DECODING;
    return 0;
}

static int ogg_seek(Decoder *dec, uint64_t position_ms)
{
    if (!dec || !dec->internal) return -1;
    OggState *st = (OggState *)dec->internal;

    double target_s = (double)position_ms / 1000.0;
    int err = ov_time_seek(&st->ovf, target_s);
    if (err != 0) {
        return map_ov_error(err);
    }
    dec->state = DECODER_STATE_OPEN;
    return 0;
}

static void ogg_reset(Decoder *dec)
{
    ogg_seek(dec, 0);
}
