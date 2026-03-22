#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <stdint.h>
#include <stdbool.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/audioout.h>
#include "decoder.h"
#include "playlist.h"
#include "equalizer.h"

/* Audio configuration macros */
#define AUDIO_SAMPLE_RATE   44100          /* reference rate for duration math */
#define AUDIO_PORT_RATE     48000          /* SceAudio port rate (Vita native)  */
#define AUDIO_CHANNELS      2
#define AUDIO_BIT_DEPTH     16
#define PCM_BUFFER_SIZE     (4096 * 4)
#define NUM_BUFFERS         4
#define MAX_VOLUME          SCE_AUDIO_VOLUME_0DB   /* 32768 = 0 dB */

/* Playback state */
typedef enum {
    PLAYBACK_STOPPED   = 0,
    PLAYBACK_PLAYING   = 1,
    PLAYBACK_PAUSED    = 2,
    PLAYBACK_BUFFERING = 3
} PlaybackState;

/* PCM ring buffer */
typedef struct {
    int16_t *data;
    uint32_t size;
    uint32_t used;
    volatile int ready;
} PCMBuffer;

/* Main audio engine structure */
typedef struct {
    /* SceAudio port handle and its current sample rate */
    int port;
    int port_sample_rate;

    /* Background decode/output thread */
    SceUID thread;

    /* Playback state */
    volatile PlaybackState state;

    /* Repeat and shuffle modes */
    RepeatMode repeat_mode;
    bool shuffle;

    /* Volume: 0 – MAX_VOLUME */
    int volume;

    /* Currently open decoder */
    Decoder *decoder;

    /* Path of the currently playing track */
    char current_track[512];

    /* Double-buffered PCM ring */
    PCMBuffer buffers[NUM_BUFFERS];
    volatile int buf_read_idx;
    volatile int buf_write_idx;

    /* Synchronisation */
    SceUID mutex;
    SceUID semaphore;

    /* Cross-fade */
    bool crossfade_enabled;
    float crossfade_duration;    /* seconds */
    Decoder *next_dec;           /* pre-opened decoder for crossfade; closed on stop */

    /* Visualizer shared buffer (raw PCM, guarded by vis_mutex) */
    int16_t *visualizer_buffer;
    uint32_t vis_buf_size;       /* number of int16_t samples */
    SceUID vis_mutex;

    /* Elapsed-time tracking */
    uint64_t position_ms;
    uint64_t duration_ms;

    /* Internal flag: thread should exit */
    volatile bool thread_exit;

    /* Background VBR duration-scan thread (spawned per-track, < 0 = none) */
    SceUID scan_thread;

    /* Set by audio thread when crossfade auto-advances to next track.
     * UI thread reads this, reloads metadata, then clears it.           */
    volatile bool track_changed;

    /* Set by audio thread when a track ends with no pre-queued next track.
     * UI thread picks this up, calls audio_engine_next, then clears it.  */
    volatile bool auto_advance;

    /* Equalizer (owned by main.c, pointer set after audio_engine_init) */
    Equalizer *eq;
} AudioEngine;

/* ---------- public API ---------- */

/**
 * Initialise the audio engine.
 * Allocates PCM buffers, opens the SceAudioOut port and starts the
 * background decode/output thread.
 */
int audio_engine_init(AudioEngine *engine);

/**
 * Tear down the audio engine.
 * Signals the background thread to stop, waits for it to finish, releases
 * the SceAudioOut port and frees all allocated memory.
 */
void audio_engine_destroy(AudioEngine *engine);

/**
 * Start (or resume) playback of the given file path.
 * Opens the appropriate decoder and sets state to PLAYBACK_PLAYING.
 */
int audio_engine_play(AudioEngine *engine, const char *filepath);

/**
 * Toggle between PLAYBACK_PLAYING and PLAYBACK_PAUSED.
 */
void audio_engine_pause(AudioEngine *engine);

/**
 * Stop playback and close the current decoder.
 */
void audio_engine_stop(AudioEngine *engine);

/**
 * Advance to the next track in the linked playlist.
 */
int audio_engine_next(AudioEngine *engine, Playlist *playlist);

/**
 * Go back to the previous track in the linked playlist.
 */
int audio_engine_prev(AudioEngine *engine, Playlist *playlist);

/**
 * Set output volume (0 – MAX_VOLUME).
 * Calls sceAudioOutSetVolume internally.
 */
void audio_engine_set_volume(AudioEngine *engine, int volume);

/**
 * Set the repeat mode.
 */
void audio_engine_set_repeat(AudioEngine *engine, RepeatMode mode);

/**
 * Toggle shuffle on/off.
 */
void audio_engine_toggle_shuffle(AudioEngine *engine);

/**
 * Return elapsed playback time in milliseconds.
 */
uint64_t audio_engine_get_position(const AudioEngine *engine);

/**
 * Return total track duration in milliseconds (from decoder info).
 */
uint64_t audio_engine_get_duration(const AudioEngine *engine);

/**
 * Return the current PlaybackState.
 */
PlaybackState audio_engine_get_state(const AudioEngine *engine);

/**
 * Copy the latest visualizer samples into the caller-supplied buffer.
 * @param out   Destination buffer (must hold at least `count` int16_t values).
 * @param count Number of samples to copy.
 * Returns the number of samples actually copied.
 */
uint32_t audio_engine_get_visualizer_data(AudioEngine *engine,
                                          int16_t *out, uint32_t count);

/**
 * Seek to the given position (milliseconds) within the current track.
 */
int audio_engine_seek(AudioEngine *engine, uint64_t position_ms);


#endif /* AUDIO_ENGINE_H */
