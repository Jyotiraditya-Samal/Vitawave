#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/audioout.h>
#include "decoder.h"

#define AUDIO_PORT_RATE  48000
#define AUDIO_CHANNELS   2
#define MAX_VOLUME       SCE_AUDIO_VOLUME_0DB

typedef enum {
    PLAYBACK_STOPPED = 0,
    PLAYBACK_PLAYING,
    PLAYBACK_PAUSED,
} PlaybackState;

struct Equalizer; /* forward decl */

typedef struct {
    int              port;
    int              port_sample_rate;
    SceUID           thread;
    volatile PlaybackState state;
    int              volume;
    Decoder         *decoder;
    char             current_track[512];
    uint64_t         position_ms;
    uint64_t         duration_ms;
    volatile bool    thread_exit;
    volatile bool    auto_advance;
    SceUID           mutex;
    struct Equalizer   *eq;  /* optional, set by main.c after EQ init */
    struct Visualizer  *vis; /* optional, set by main.c */
} AudioEngine;

int           audio_engine_init(AudioEngine *engine);
void          audio_engine_destroy(AudioEngine *engine);
int           audio_engine_play(AudioEngine *engine, const char *filepath);
void          audio_engine_pause(AudioEngine *engine);
void          audio_engine_stop(AudioEngine *engine);
void          audio_engine_set_volume(AudioEngine *engine, int volume);
uint64_t      audio_engine_get_position(const AudioEngine *engine);
uint64_t      audio_engine_get_duration(const AudioEngine *engine);
PlaybackState audio_engine_get_state(const AudioEngine *engine);

#endif
