#ifndef DECODER_H
#define DECODER_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DECODER_STATE_CLOSED = 0,
    DECODER_STATE_OPEN,
    DECODER_STATE_EOF,
    DECODER_STATE_ERROR,
} DecoderState;

typedef struct {
    uint32_t sample_rate;
    uint32_t channels;
    uint64_t duration_ms;
    char     filepath[256];
} DecoderInfo;

/* private-state function signatures */
typedef void (*DecoderCloseFn)(void *priv);
typedef int  (*DecoderDecodeFn)(void *priv, int16_t *out,
                                uint32_t frames_req, uint32_t *frames_decoded,
                                DecoderState *state);

typedef struct Decoder {
    void          *priv;
    DecoderCloseFn  close_fn;
    DecoderDecodeFn decode_fn;
    DecoderInfo     info;
    DecoderState    state;
} Decoder;

Decoder     *decoder_open(const char *filepath);
void         decoder_close(Decoder *dec);
int          decoder_decode_frames(Decoder *dec, int16_t *out,
                                   uint32_t frames, uint32_t *frames_decoded);
DecoderInfo  decoder_get_info(Decoder *dec);
DecoderState decoder_get_state(Decoder *dec);

/* format-specific entry points */
Decoder *decoder_mp3_open(const char *filepath);
Decoder *decoder_flac_open(const char *filepath);
Decoder *decoder_ogg_open(const char *filepath);

#endif
