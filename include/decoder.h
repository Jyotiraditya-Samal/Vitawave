#ifndef DECODER_H
#define DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include <psp2/kernel/threadmgr.h>

/* ── Decoder type ─────────────────────────────────────────────────────────── */
typedef enum {
    DECODER_UNKNOWN = 0,
    DECODER_MP3     = 1,
    DECODER_FLAC    = 2,
    DECODER_OGG     = 3,
    DECODER_WAV     = 4
} DecoderType;

/* ── Decoder state ────────────────────────────────────────────────────────── */
typedef enum {
    DECODER_STATE_IDLE     = 0,
    DECODER_STATE_OPEN     = 1,
    DECODER_STATE_DECODING = 2,
    DECODER_STATE_EOF      = 3,
    DECODER_STATE_ERROR    = 4
} DecoderState;

/* ── Track info provided by the decoder ──────────────────────────────────── */
typedef struct {
    uint32_t sample_rate;     /* e.g. 44100 */
    uint32_t channels;        /* 1 or 2 */
    uint32_t bit_depth;       /* bits per sample (16, 24 …) */
    uint64_t duration_ms;     /* total duration in milliseconds */
    uint32_t bitrate;         /* kbps */
    uint64_t total_frames;    /* total PCM frames */
} DecoderInfo;

/* ── Decoder vtable (function pointers per format) ───────────────────────── */
struct Decoder;

typedef struct {
    int      (*open)         (struct Decoder *dec, const char *filepath);
    void     (*close)        (struct Decoder *dec);
    int      (*decode_frames)(struct Decoder *dec, int16_t *out,
                              uint32_t frames_requested, uint32_t *frames_decoded);
    int      (*seek)         (struct Decoder *dec, uint64_t position_ms);
    void     (*reset)        (struct Decoder *dec);
} DecoderVtable;

/* ── Main Decoder structure ───────────────────────────────────────────────── */
typedef struct Decoder {
    DecoderType   type;
    DecoderState  state;
    DecoderInfo   info;

    char          filepath[512];

    /* Format-specific private data (allocated by open, freed by close) */
    void         *internal;

    /* Vtable filled in by decoder_open */
    DecoderVtable vtable;

    /* Per-decoder mutex */
    SceUID        mutex;
} Decoder;

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * Detect the decoder type for a given filepath (by file extension).
 */
DecoderType decoder_detect_type(const char *filepath);

/**
 * Allocate and open a decoder for filepath.
 * Returns a valid Decoder* on success, NULL on failure.
 */
Decoder *decoder_open(const char *filepath);

/**
 * Close and free all resources held by the decoder.
 * After this call the pointer must not be used.
 */
void decoder_close(Decoder *dec);

/**
 * Decode up to frames_requested stereo PCM frames into out.
 * frames_decoded is set to the number actually written.
 * Returns 0 on success, negative on error, 1 at end-of-stream.
 */
int decoder_decode_frames(Decoder *dec,
                          int16_t *out,
                          uint32_t frames_requested,
                          uint32_t *frames_decoded);

/**
 * Seek to position_ms within the stream.
 * Returns 0 on success, negative on error.
 */
int decoder_seek(Decoder *dec, uint64_t position_ms);

/**
 * Retrieve the filled DecoderInfo.
 */
DecoderInfo decoder_get_info(const Decoder *dec);

/**
 * Reset the decoder to the beginning of the stream (equivalent to seek(0)).
 */
void decoder_reset(Decoder *dec);

/**
 * Scan the file to fill in duration_ms if it is not yet known.
 * Only needed for VBR MP3s without a Xing/VBRI header.
 * MUST be called from the UI/main thread — it reads the whole file.
 * Safe to call even if duration is already known (returns immediately).
 */
void decoder_scan_duration(Decoder *dec);

/* ── Format-specific open functions (called internally by decoder_open) ──── */
int decoder_mp3_open (Decoder *dec, const char *filepath);
int decoder_flac_open(Decoder *dec, const char *filepath);
int decoder_ogg_open (Decoder *dec, const char *filepath);

#endif /* DECODER_H */
