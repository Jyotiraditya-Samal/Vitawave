#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/audioout.h>
#include <psp2/appmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

#include "audio_engine.h"
#include "decoder.h"

#define GRANULE_SIZE 960

static AudioEngine *s_engine = NULL;

static int audio_thread_func(SceSize args, void *argp)
{
    (void)args; (void)argp;
    AudioEngine *e = s_engine;
    static int16_t out_buf[GRANULE_SIZE * 2];

    while (!e->thread_exit) {
        if (e->state != PLAYBACK_PLAYING || !e->decoder) {
            sceKernelDelayThread(5000);
            continue;
        }

        sceKernelLockMutex(e->mutex, 1, NULL);

        if (!e->decoder || e->state != PLAYBACK_PLAYING) {
            sceKernelUnlockMutex(e->mutex, 1);
            continue;
        }

        uint32_t frames = 0;
        int rc = decoder_decode_frames(e->decoder, out_buf, GRANULE_SIZE, &frames);

        if (rc == 1 || (rc == 0 && frames == 0)) {
            e->state        = PLAYBACK_STOPPED;
            e->auto_advance = true;
            sceKernelUnlockMutex(e->mutex, 1);
            continue;
        }

        if (rc < 0) {
            sceKernelUnlockMutex(e->mutex, 1);
            continue;
        }

        if (frames < GRANULE_SIZE)
            memset(out_buf + frames * 2, 0, (GRANULE_SIZE - frames) * 2 * sizeof(int16_t));

        uint32_t sr = (e->decoder && e->decoder->info.sample_rate > 0)
                      ? e->decoder->info.sample_rate : 44100;
        e->position_ms += (uint64_t)(frames * 1000ULL) / sr;

        sceKernelUnlockMutex(e->mutex, 1);

        sceAudioOutOutput(e->port, out_buf);
    }

    sceKernelExitThread(0);
    return 0;
}

int audio_engine_init(AudioEngine *e)
{
    if (!e) return -1;
    memset(e, 0, sizeof(*e));

    e->volume = MAX_VOLUME;
    e->state  = PLAYBACK_STOPPED;

    sceAppMgrAcquireBgmPort();

    e->port_sample_rate = AUDIO_PORT_RATE;
    e->port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRANULE_SIZE,
                                   e->port_sample_rate,
                                   SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (e->port < 0) return e->port;

    int vols[2] = { e->volume, e->volume };
    sceAudioOutSetVolume(e->port,
        SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);

    e->mutex = sceKernelCreateMutex("AE_mutex", 0, 0, NULL);
    if (e->mutex < 0) {
        sceAudioOutReleasePort(e->port);
        return e->mutex;
    }

    s_engine       = e;
    e->thread_exit = false;
    e->thread = sceKernelCreateThread("VitaWave_audio", audio_thread_func,
                                       0x10000100, 0x10000, 0, 0, NULL);
    if (e->thread >= 0)
        sceKernelStartThread(e->thread, 0, NULL);

    return 0;
}

void audio_engine_destroy(AudioEngine *e)
{
    if (!e) return;
    e->thread_exit = true;
    e->state       = PLAYBACK_STOPPED;
    sceKernelWaitThreadEnd(e->thread, NULL, NULL);
    sceKernelDeleteThread(e->thread);
    if (e->decoder) decoder_close(e->decoder);
    sceAudioOutReleasePort(e->port);
    sceAppMgrReleaseBgmPort();
    sceKernelDeleteMutex(e->mutex);
}

int audio_engine_play(AudioEngine *e, const char *filepath)
{
    if (!e || !filepath) return -1;
    audio_engine_stop(e);
    e->decoder = decoder_open(filepath);
    if (!e->decoder) return -2;
    DecoderInfo info = decoder_get_info(e->decoder);
    e->duration_ms = info.duration_ms;
    e->position_ms = 0;
    strncpy(e->current_track, filepath, sizeof(e->current_track) - 1);
    e->auto_advance = false;
    e->state = PLAYBACK_PLAYING;
    return 0;
}

void audio_engine_pause(AudioEngine *e)
{
    if (!e) return;
    if (e->state == PLAYBACK_PLAYING)      e->state = PLAYBACK_PAUSED;
    else if (e->state == PLAYBACK_PAUSED)  e->state = PLAYBACK_PLAYING;
}

void audio_engine_stop(AudioEngine *e)
{
    if (!e) return;
    e->state = PLAYBACK_STOPPED;
    sceKernelLockMutex(e->mutex, 1, NULL);
    if (e->decoder) { decoder_close(e->decoder); e->decoder = NULL; }
    sceKernelUnlockMutex(e->mutex, 1);
    e->position_ms = 0;
}

void audio_engine_set_volume(AudioEngine *e, int volume)
{
    if (!e) return;
    e->volume = volume;
    int vols[2] = { volume, volume };
    sceAudioOutSetVolume(e->port,
        SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
}

uint64_t audio_engine_get_position(const AudioEngine *e) { return e ? e->position_ms : 0; }
uint64_t audio_engine_get_duration(const AudioEngine *e) { return e ? e->duration_ms : 0; }
PlaybackState audio_engine_get_state(const AudioEngine *e) { return e ? e->state : PLAYBACK_STOPPED; }
