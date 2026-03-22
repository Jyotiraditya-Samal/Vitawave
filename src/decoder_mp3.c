/*
 * VitaWave – decoder_mp3.c
 * MP3 decoding via libmpg123.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static void mp3_log(const char *fmt, ...)
{
    FILE *f = fopen("ux0:data/VitaWave/debug.log", "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

#include <mpg123.h>
#include <psp2/kernel/threadmgr.h>

#include "decoder.h"
#include "audio_engine.h"  /* for AUDIO_SAMPLE_RATE, AUDIO_CHANNELS */

/* ── Private state ───────────────────────────────────────────────────────── */
typedef struct {
    mpg123_handle *mh;
    int            channels;
    long           rate;
    int            encoding;
    bool           initialised;  /* mpg123_init reference-counted */
} Mpg123State;

/* ── Forward declarations ────────────────────────────────────────────────── */
static int  mp3_open        (Decoder *dec, const char *filepath);
static void mp3_close       (Decoder *dec);
static int  mp3_decode_frames(Decoder *dec, int16_t *out,
                              uint32_t frames_req, uint32_t *frames_decoded);
static int  mp3_seek        (Decoder *dec, uint64_t position_ms);
static void mp3_reset       (Decoder *dec);

/* ── Vtable ──────────────────────────────────────────────────────────────── */
static const DecoderVtable mp3_vtable = {
    mp3_open,
    mp3_close,
    mp3_decode_frames,
    mp3_seek,
    mp3_reset
};

/* ── Public entry point called by decoder_open ───────────────────────────── */
int decoder_mp3_open(Decoder *dec, const char *filepath)
{
    return mp3_open(dec, filepath);
}

/* ── Implementation ──────────────────────────────────────────────────────── */

static int mp3_open(Decoder *dec, const char *filepath)
{
    int err;

    /* Allocate private state */
    Mpg123State *st = (Mpg123State *)calloc(1, sizeof(Mpg123State));
    if (!st) return -1;

    /* Initialise library (idempotent across multiple calls) */
    err = mpg123_init();
    if (err != MPG123_OK && err != MPG123_BAD_TYPES) {
        free(st);
        return -2;
    }
    st->initialised = true;

    /* Create handle */
    st->mh = mpg123_new(NULL, &err);
    if (!st->mh) {
        mpg123_exit();
        free(st);
        return -3;
    }

    mpg123_param(st->mh, MPG123_VERBOSE, 0, 0.0);
    mpg123_param(st->mh, MPG123_FLAGS,
                 MPG123_FORCE_STEREO | MPG123_GAPLESS, 0.0);

    /* Lock the output format to S16 stereo at all common MP3 sample rates
     * BEFORE opening the file.  Setting format after mpg123_open can fail
     * silently on some builds, leaving the encoder free to output floats
     * or other formats that we then misinterpret as int16 → noise/distortion.
     * No software resampler needed: mpg123 outputs at the file's native rate
     * as long as that rate is in the allowed list.                          */
    mpg123_format_none(st->mh);
    {
        const long rates[] = { 8000, 11025, 12000, 16000, 22050, 24000,
                                32000, 44100, 48000 };
        for (int i = 0; i < 9; i++)
            mpg123_format(st->mh, rates[i], MPG123_STEREO, MPG123_ENC_SIGNED_16);
    }

    /* Open file */
    err = mpg123_open(st->mh, filepath);
    if (err != MPG123_OK) {
        mpg123_delete(st->mh);
        if (st->initialised) mpg123_exit();
        free(st);
        return -4;
    }

    /* Read the actual negotiated format (rate depends on the file) */
    err = mpg123_getformat(st->mh, &st->rate, &st->channels, &st->encoding);
    if (err != MPG123_OK) {
        st->rate     = 44100;
        st->channels = 2;
        st->encoding = MPG123_ENC_SIGNED_16;
        mp3_log("mp3_open: getformat FAILED err=%d  using fallback 44100/2/S16", err);
    } else {
        mp3_log("mp3_open: rate=%ld ch=%d enc=0x%x (S16=0x%x) file=%s",
                st->rate, st->channels, st->encoding, MPG123_ENC_SIGNED_16,
                filepath);
    }

    /* Fill DecoderInfo */
    dec->info.sample_rate = (uint32_t)st->rate;
    dec->info.channels    = (uint32_t)st->channels;
    dec->info.bit_depth   = 16;

    /* Attempt to get total PCM frames / duration.
     * mpg123_length works for CBR and VBR with a Xing/VBRI header.
     * VBR files without a header need a full-file scan — that is done
     * lazily via decoder_scan_duration(), called from audio_engine_play()
     * on the UI thread where blocking is safe.                           */
    off_t total_frames = mpg123_length(st->mh);
    if (total_frames > 0) {
        dec->info.total_frames = (uint64_t)total_frames;
        dec->info.duration_ms  = (uint64_t)(total_frames * 1000ULL)
                                 / (uint64_t)st->rate;
    }

    /* Bitrate from frame info */
    struct mpg123_frameinfo fi;
    if (mpg123_info(st->mh, &fi) == MPG123_OK) {
        dec->info.bitrate = (uint32_t)fi.bitrate;
    }

    dec->internal    = st;
    dec->vtable      = mp3_vtable;
    dec->state       = DECODER_STATE_OPEN;

    return 0;
}

static void mp3_close(Decoder *dec)
{
    if (!dec || !dec->internal) return;
    Mpg123State *st = (Mpg123State *)dec->internal;

    mpg123_close(st->mh);
    mpg123_delete(st->mh);
    if (st->initialised) mpg123_exit();
    free(st);
    dec->internal = NULL;
    dec->state    = DECODER_STATE_IDLE;
}

static int mp3_decode_frames(Decoder *dec, int16_t *out,
                              uint32_t frames_req, uint32_t *frames_decoded)
{
    if (!dec || !dec->internal || !out || !frames_decoded) return -1;
    Mpg123State *st = (Mpg123State *)dec->internal;

    *frames_decoded = 0;
    size_t bytes_wanted = (size_t)frames_req * st->channels * sizeof(int16_t);
    size_t bytes_done   = 0;
    uint8_t *ptr        = (uint8_t *)out;

    while (bytes_done < bytes_wanted) {
        size_t chunk = 0;
        int err = mpg123_read(st->mh,
                              ptr + bytes_done,
                              bytes_wanted - bytes_done,
                              &chunk);

        if (err == MPG123_DONE || (err == MPG123_OK && chunk == 0)) {
            dec->state = DECODER_STATE_EOF;
            break;
        }
        if (err != MPG123_OK && err != MPG123_NEW_FORMAT) {
            dec->state = DECODER_STATE_ERROR;
            return -2;
        }
        bytes_done += chunk;
    }

    *frames_decoded = (uint32_t)(bytes_done / (st->channels * sizeof(int16_t)));

    /* Only advance to DECODING if we didn't already mark EOF inside the loop */
    if (dec->state != DECODER_STATE_EOF)
        dec->state = DECODER_STATE_DECODING;

    /* Signal EOF to caller if nothing was decoded */
    if (*frames_decoded == 0)
        return 1;

    return 0;
}

static int mp3_seek(Decoder *dec, uint64_t position_ms)
{
    if (!dec || !dec->internal) return -1;
    Mpg123State *st = (Mpg123State *)dec->internal;

    /* mpg123_seek takes a PCM sample offset; mpg123_seek_frame takes an
     * MPEG frame number (1 frame = 1152 samples) — using the wrong one
     * causes seeks to land at the wrong position or return an error.     */
    off_t target_sample = (off_t)(position_ms * (uint64_t)st->rate / 1000ULL);
    off_t result = mpg123_seek(st->mh, target_sample, SEEK_SET);
    if (result < 0) return -2;

    dec->state = DECODER_STATE_OPEN;
    return 0;
}

static void mp3_reset(Decoder *dec)
{
    mp3_seek(dec, 0);
}

/* ── decoder_scan_duration ───────────────────────────────────────────────── */
/*
 * Full-file scan to get the exact PCM sample count for VBR MP3s that lack a
 * Xing/VBRI header.  This reads the entire file so it MUST only be called
 * from the UI/main thread, never from the audio decode thread.
 */
void decoder_scan_duration(Decoder *dec)
{
    if (!dec || dec->type != DECODER_MP3) return;
    if (dec->info.duration_ms > 0) return;  /* already known */

    Mpg123State *st = (Mpg123State *)dec->internal;
    if (!st || !st->mh) return;

    off_t end = mpg123_seek(st->mh, 0, SEEK_END);
    if (end > 0) {
        dec->info.total_frames = (uint64_t)end;
        dec->info.duration_ms  = (uint64_t)end * 1000ULL / (uint64_t)st->rate;
    }
    mpg123_seek(st->mh, 0, SEEK_SET);  /* rewind regardless */
}

/* ── Shared decoder_open / close / decode dispatcher (in this TU for MP3) ── */
/*
 * NOTE: The dispatcher is defined in decoder.c; each format TU only exports
 * its decoder_XXX_open symbol.  The functions below are the full public API
 * implementations referenced from decoder.h – they live here to avoid a
 * separate decoder.c translation unit.
 */

DecoderType decoder_detect_type(const char *filepath)
{
    if (!filepath) return DECODER_UNKNOWN;
    const char *ext = NULL;
    /* Walk backwards to find last '.' */
    for (const char *p = filepath; *p; p++) {
        if (*p == '.') ext = p + 1;
    }
    if (!ext) return DECODER_UNKNOWN;

    /* Case-insensitive compare */
    char e[8] = {0};
    for (int i = 0; i < 7 && ext[i]; i++)
        e[i] = (ext[i] >= 'A' && ext[i] <= 'Z') ? ext[i] + 32 : ext[i];

    if (strcmp(e, "mp3") == 0) return DECODER_MP3;
    if (strcmp(e, "flac")== 0) return DECODER_FLAC;
    if (strcmp(e, "ogg") == 0) return DECODER_OGG;
    if (strcmp(e, "wav") == 0) return DECODER_WAV;
    return DECODER_UNKNOWN;
}

Decoder *decoder_open(const char *filepath)
{
    if (!filepath) return NULL;

    DecoderType type = decoder_detect_type(filepath);
    if (type == DECODER_UNKNOWN) return NULL;

    Decoder *dec = (Decoder *)calloc(1, sizeof(Decoder));
    if (!dec) return NULL;

    dec->type  = type;
    dec->state = DECODER_STATE_IDLE;
    strncpy(dec->filepath, filepath, sizeof(dec->filepath) - 1);

    dec->mutex = sceKernelCreateMutex("dec_mutex", 0, 0, NULL);

    int ret = -1;
    switch (type) {
        case DECODER_MP3:  ret = decoder_mp3_open (dec, filepath); break;
        case DECODER_FLAC: ret = decoder_flac_open(dec, filepath); break;
        case DECODER_OGG:  ret = decoder_ogg_open (dec, filepath); break;
        default: break;
    }

    if (ret < 0) {
        sceKernelDeleteMutex(dec->mutex);
        free(dec);
        return NULL;
    }

    return dec;
}

void decoder_close(Decoder *dec)
{
    if (!dec) return;
    if (dec->vtable.close) {
        dec->vtable.close(dec);
    }
    sceKernelDeleteMutex(dec->mutex);
    free(dec);
}

int decoder_decode_frames(Decoder *dec, int16_t *out,
                          uint32_t frames_req, uint32_t *frames_decoded)
{
    if (!dec || !dec->vtable.decode_frames) return -1;
    return dec->vtable.decode_frames(dec, out, frames_req, frames_decoded);
}

int decoder_seek(Decoder *dec, uint64_t position_ms)
{
    if (!dec || !dec->vtable.seek) return -1;
    return dec->vtable.seek(dec, position_ms);
}

DecoderInfo decoder_get_info(const Decoder *dec)
{
    static DecoderInfo empty = {0};
    if (!dec) return empty;
    return dec->info;
}

void decoder_reset(Decoder *dec)
{
    if (!dec || !dec->vtable.reset) return;
    dec->vtable.reset(dec);
}
