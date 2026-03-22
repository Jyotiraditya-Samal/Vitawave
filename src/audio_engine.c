/*
 * VitaWave – audio_engine.c
 * Manages SceAudioOut port, PCM ring buffers, decode thread, and cross-fade.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/audioout.h>
#include <psp2/appmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

static inline void acquire_bgm_port(void)
{
    sceAppMgrAcquireBgmPort();
}

#include "audio_engine.h"
#include "decoder.h"
#include "playlist.h"
#include "globals.h"
#include "file_browser.h"
#include "equalizer.h"

#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

/* ── Directory fallback helpers ──────────────────────────────────────────── */
/* When the playlist is exhausted, find the next/prev audio file in the same  */
/* directory as the currently playing track, sorted alphabetically.           */

#define FALLBACK_MAX  512

/* ── Background VBR duration-scan thread ────────────────────────────────── */
/*
 * Runs once per track for VBR MP3s that have no Xing header.
 * Locks the decoder mutex (which the audio thread releases for ~23 ms on
 * every sceAudioOutOutput call), does mpg123_seek(SEEK_END) to count frames,
 * then exits.  Keeps the UI thread free so the Now Playing screen appears
 * immediately.
 */
static int duration_scan_thread(SceSize arglen, void *argp)
{
    (void)arglen;
    AudioEngine *e = *(AudioEngine **)argp;

    if (sceKernelLockMutex(e->mutex, 1, NULL) == 0) {
        if (e->decoder && e->duration_ms == 0) {
            decoder_scan_duration(e->decoder);
            e->duration_ms = e->decoder->info.duration_ms;
        }
        sceKernelUnlockMutex(e->mutex, 1);
    }
    return 0;
}

static int dir_fallback(const char *current_path, bool forward,
                        char *out_path, size_t out_size)
{
    const char *sep = strrchr(current_path, '/');
    if (!sep) return -1;

    /* Split into dir (with trailing '/') and filename */
    size_t dir_len = (size_t)(sep - current_path) + 1;
    char dir[MAX_PATH_LEN];
    if (dir_len >= sizeof(dir)) return -1;
    memcpy(dir, current_path, dir_len);
    dir[dir_len] = '\0';
    const char *cur_name = sep + 1;

    /* Collect audio filenames */
    static char names[FALLBACK_MAX][MAX_FILENAME_LEN];
    int count = 0;

    SceUID dh = sceIoDopen(dir);
    if (dh < 0) return -1;

    SceIoDirent ent;
    while (count < FALLBACK_MAX) {
        memset(&ent, 0, sizeof(ent));
        if (sceIoDread(dh, &ent) <= 0) break;
        if (ent.d_name[0] == '.') continue;
        if (!SCE_S_ISDIR(ent.d_stat.st_mode) &&
            file_browser_is_audio_file(ent.d_name)) {
            strncpy(names[count], ent.d_name, MAX_FILENAME_LEN - 1);
            names[count][MAX_FILENAME_LEN - 1] = '\0';
            count++;
        }
    }
    sceIoDclose(dh);

    if (count == 0) return -1;

    /* Find current file, return neighbour with wrap-around (filesystem order) */
    for (int i = 0; i < count; i++) {
        if (strcmp(names[i], cur_name) == 0) {
            int target = forward ? (i + 1) % count
                                 : (i - 1 + count) % count;
            snprintf(out_path, out_size, "%s%s", dir, names[target]);
            return 0;
        }
    }
    return -1;
}

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Number of 16-bit stereo samples per SceAudioOut granule.
 * 960 matches ElevenMPV and is the standard size for the BGM port. */
#define GRANULE_SIZE    960

/* Mix two int16 samples together with simple addition + clamp */
static inline int16_t mix_sample(int16_t a, int16_t b)
{
    int32_t s = (int32_t)a + (int32_t)b;
    if (s >  32767) s =  32767;
    if (s < -32768) s = -32768;
    return (int16_t)s;
}

/* Apply a linear volume scale factor (0–MAX_VOLUME) to a sample */
static inline int16_t apply_volume(int16_t s, int vol)
{
    return (int16_t)(((int32_t)s * vol) / MAX_VOLUME);
}

/* Return the next ring-buffer index */
static inline int ring_next(int idx)
{
    return (idx + 1) % NUM_BUFFERS;
}

/* ── Port sample-rate helper ─────────────────────────────────────────────── */
/*
 * Switch the audio port to new_rate.  Tries sceAudioOutSetConfig first;
 * if that fails falls back to release + reopen (same logic as the original
 * audio_engine_play path).  Safe to call from either the UI thread (while
 * the audio thread is stopped) or from the audio thread itself (no active
 * sceAudioOutOutput call at transition points).
 */
static void port_set_rate(AudioEngine *e, int new_rate)
{
    if (new_rate <= 0 || new_rate == e->port_sample_rate) return;
    int old_rate = e->port_sample_rate;

    if (sceAudioOutSetConfig(e->port, -1, new_rate, -1) == 0) {
        e->port_sample_rate = new_rate;
        return;
    }

    /* Fallback: release and reopen */
    sceAudioOutReleasePort(e->port);
    e->port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
                                  GRANULE_SIZE, new_rate,
                                  SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (e->port >= 0) {
        e->port_sample_rate = new_rate;
    } else {
        e->port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
                                      GRANULE_SIZE, old_rate,
                                      SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
        /* e->port_sample_rate stays old_rate */
    }
    if (e->port >= 0) {
        int vols[2] = { e->volume, e->volume };
        sceAudioOutSetVolume(e->port,
            SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
    }
}

/* ── Allocate / free PCM buffers ─────────────────────────────────────────── */

static int alloc_pcm_buffers(AudioEngine *e)
{
    for (int i = 0; i < NUM_BUFFERS; i++) {
        e->buffers[i].data = (int16_t *)malloc(PCM_BUFFER_SIZE);
        if (!e->buffers[i].data) {
            for (int j = 0; j < i; j++) {
                free(e->buffers[j].data);
                e->buffers[j].data = NULL;
            }
            return -1;
        }
        e->buffers[i].size  = PCM_BUFFER_SIZE / sizeof(int16_t);
        e->buffers[i].used  = 0;
        e->buffers[i].ready = 0;
    }
    return 0;
}

static void free_pcm_buffers(AudioEngine *e)
{
    for (int i = 0; i < NUM_BUFFERS; i++) {
        free(e->buffers[i].data);
        e->buffers[i].data = NULL;
    }
}

/* ── Audio decode / output thread ────────────────────────────────────────── */
/*
 * Strategy:
 *   - Decode PCM from the current Decoder into write-side ring buffer slots.
 *   - The audio output loop pulls from the read-side, sends to sceAudioOutOutput.
 *   - Visualizer buffer is updated from each output chunk under vis_mutex.
 *   - Cross-fade: when within crossfade_duration seconds of the end, open the
 *     next decoder and mix linearly.
 */

static int audio_thread_func(SceSize args, void *argp)
{
    (void)args; (void)argp;

    AudioEngine *e = get_audio_engine();  /* uses global accessor from main.c */

    /* Output granule: GRANULE_SIZE stereo int16 samples = GRANULE_SIZE*4 bytes */
    static int16_t out_buf[GRANULE_SIZE * AUDIO_CHANNELS];
    static int16_t cf_buf [GRANULE_SIZE * AUDIO_CHANNELS];  /* cross-fade next */

    /* e->next_dec is owned by the engine so audio_engine_stop can close it on track change */
    e->next_dec = NULL;
    float    cf_progress = 0.0f;
    int      tick_counter = 0;   /* periodic power-tick counter */

    while (!e->thread_exit) {

        /* ── Power tick: prevent OS auto-suspend while playing ──
         * Called every ~60 iterations (~1 s at 960-sample granules / 48 kHz).
         * Uses DISABLE_AUTO_SUSPEND so CPU stays alive for the audio thread
         * when the screen is off, without preventing display power-off.     */
        if (++tick_counter >= 60) {
            tick_counter = 0;
            if (e->state == PLAYBACK_PLAYING)
                sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        }

        /* ── Wait if paused or stopped ── */
        if (e->state == PLAYBACK_PAUSED || e->state == PLAYBACK_STOPPED) {
            sceKernelDelayThread(5000);
            continue;
        }

        /* ── Lock mutex: protects e->decoder for entire decode cycle ── */
        sceKernelLockMutex(e->mutex, 1, NULL);

        /* ── Re-check state after acquiring lock ── */
        if (e->state == PLAYBACK_PAUSED || e->state == PLAYBACK_STOPPED) {
            sceKernelUnlockMutex(e->mutex, 1);
            sceKernelDelayThread(5000);
            continue;
        }

        /* ── Ensure we have an open decoder ── */
        if (!e->decoder || e->decoder->state == DECODER_STATE_EOF) {
            if (e->decoder && e->decoder->state == DECODER_STATE_EOF)
                e->state = PLAYBACK_STOPPED;
            sceKernelUnlockMutex(e->mutex, 1);
            sceKernelDelayThread(5000);
            continue;
        }

        /* ── Decode a granule ── */
        uint32_t frames_decoded = 0;
        int rc = decoder_decode_frames(e->decoder, out_buf,
                                       GRANULE_SIZE, &frames_decoded);

        if (rc < 0) {
            /* Decoder error (e.g. trailing ID3/APE tags in some MP3s).
             * Treat the same as EOF — transition if e->next_dec is ready.   */
            if (e->next_dec) {
                decoder_close(e->decoder);
                e->decoder  = e->next_dec;
                e->next_dec    = NULL;
                cf_progress = 0.0f;
                e->position_ms = 0;
                DecoderInfo ninfo = decoder_get_info(e->decoder);
                e->duration_ms = ninfo.duration_ms;
                Playlist *pl = get_playlist();
                if (pl) {
                    pl->repeat_mode = e->repeat_mode;
                    playlist_next(pl);
                    PlaylistEntry *ne = playlist_get_current(pl);
                    if (ne) strncpy(e->current_track, ne->filepath,
                                    sizeof(e->current_track) - 1);
                }
                e->track_changed = true;
                port_set_rate(e, (int)ninfo.sample_rate);
            } else {
                e->state        = PLAYBACK_STOPPED;
                e->auto_advance = true;
            }
            sceKernelUnlockMutex(e->mutex, 1);
            sceKernelDelayThread(5000);
            continue;
        }
        if (rc == 1 || frames_decoded == 0) {
            /* EOF — if crossfade pre-opened the next track, transition to it */
            if (e->next_dec) {
                decoder_close(e->decoder);
                e->decoder  = e->next_dec;
                e->next_dec    = NULL;
                cf_progress = 0.0f;
                e->position_ms = 0;
                DecoderInfo ninfo = decoder_get_info(e->decoder);
                e->duration_ms = ninfo.duration_ms;
                /* Advance playlist and update current_track */
                Playlist *pl = get_playlist();
                if (pl) {
                    pl->repeat_mode = e->repeat_mode;
                    playlist_next(pl);
                    PlaylistEntry *ne = playlist_get_current(pl);
                    if (ne) strncpy(e->current_track, ne->filepath,
                                    sizeof(e->current_track) - 1);
                }
                e->track_changed = true;
                /* Reconfigure audio port if sample rate changed */
                port_set_rate(e, (int)ninfo.sample_rate);
            } else {
                e->state        = PLAYBACK_STOPPED;
                e->auto_advance = true;
            }
            sceKernelUnlockMutex(e->mutex, 1);
            continue;
        }

        /* ── Apply volume ── */
        int vol = e->volume;
        for (uint32_t i = 0; i < frames_decoded * AUDIO_CHANNELS; i++)
            out_buf[i] = apply_volume(out_buf[i], vol);

        /* ── Cross-fade ── */
        if (e->crossfade_enabled && e->crossfade_duration > 0.0f) {
            uint64_t pos  = e->position_ms;
            uint64_t dur  = e->duration_ms;
            uint64_t cf_start = dur > (uint64_t)(e->crossfade_duration * 1000.0f)
                              ? dur - (uint64_t)(e->crossfade_duration * 1000.0f) : 0;

            if (pos >= cf_start && dur > 0) {
                if (!e->next_dec) {
                    Playlist *pl = get_playlist();
                    if (pl) {
                        pl->repeat_mode = e->repeat_mode;
                        int saved = pl->current_index;
                        int ni    = playlist_next(pl);
                        pl->current_index = saved;
                        if (ni >= 0)
                            e->next_dec = decoder_open(pl->entries[ni].filepath);
                    }
                }
                if (e->next_dec) {
                    /* Only mix if the next track's sample rate matches the
                     * current port rate.  Mixing at mismatched rates feeds the
                     * next track's PCM to the port at the wrong clock, making
                     * the fade-in portion play at the wrong speed.  When rates
                     * differ, skip the blend — the hard transition at EOF will
                     * call port_set_rate() and fix things correctly.        */
                    uint32_t next_rate = e->next_dec->info.sample_rate;
                    if (next_rate == 0 ||
                        (int)next_rate == e->port_sample_rate) {
                        uint32_t cf_frames = 0;
                        decoder_decode_frames(e->next_dec, cf_buf,
                                             frames_decoded, &cf_frames);
                        float alpha = (float)(pos - cf_start)
                                    / (e->crossfade_duration * 1000.0f);
                        if (alpha > 1.0f) alpha = 1.0f;
                        for (uint32_t i = 0;
                             i < frames_decoded * AUDIO_CHANNELS; i++) {
                            int16_t ms = (int16_t)(out_buf[i] * (1.0f - alpha));
                            int16_t ns = (int16_t)(cf_buf[i]  * alpha);
                            out_buf[i] = mix_sample(ms, ns);
                        }
                        cf_progress = alpha;
                    }
                }
            }
        }

        /* ── Apply EQ ── */
        if (e->eq && e->eq->enabled && frames_decoded > 0) {
            if (e->eq->last_sample_rate != e->port_sample_rate)
                eq_update_coefficients(e->eq, e->port_sample_rate);
            eq_process(e->eq, out_buf, frames_decoded);
        }

        /* ── Update position ── */
        uint32_t sr = (e->decoder && e->decoder->info.sample_rate > 0)
                      ? e->decoder->info.sample_rate : AUDIO_SAMPLE_RATE;
        e->position_ms += (uint64_t)(frames_decoded * 1000ULL) / sr;

        /* ── Copy to visualizer buffer ── */
        sceKernelLockMutex(e->vis_mutex, 1, NULL);
        uint32_t copy_samples = frames_decoded * AUDIO_CHANNELS;
        if (copy_samples > e->vis_buf_size) copy_samples = e->vis_buf_size;
        memcpy(e->visualizer_buffer, out_buf, copy_samples * sizeof(int16_t));
        sceKernelUnlockMutex(e->vis_mutex, 1);

        /* ── Release decoder lock BEFORE blocking audio output ──
         * sceAudioOutOutput blocks ~23 ms; holding the mutex here would
         * make audio_engine_stop() deadlock for that entire duration.     */
        sceKernelUnlockMutex(e->mutex, 1);

        /* ── Send to SceAudioOut ── */
        /* If the port was killed by a suspend/resume cycle, output returns
         * a negative error.  Delay briefly so we don't spin at full CPU.
         * audio_engine_resume() (called by main loop on APP_RESUME) will
         * re-acquire the BGM port and reopen the port if needed.          */
        sceAudioOutOutput(e->port, out_buf);
    }

    /* Clean up cross-fade decoder if still open */
    if (e->next_dec) {
        decoder_close(e->next_dec);
    }

    sceKernelExitThread(0);
    return 0;
}

/* ── audio_engine_init ───────────────────────────────────────────────────── */

int audio_engine_init(AudioEngine *e)
{
    if (!e) return -1;
    memset(e, 0, sizeof(*e));

    /* Default settings */
    e->volume            = MAX_VOLUME;
    e->state             = PLAYBACK_STOPPED;
    e->repeat_mode       = REPEAT_NONE;
    e->crossfade_enabled = false;
    e->crossfade_duration = 3.0f;
    e->scan_thread       = -1;

    /* Allocate PCM buffers */
    if (alloc_pcm_buffers(e) < 0) return -2;

    /* Visualizer buffer */
    e->vis_buf_size     = FFT_SIZE * AUDIO_CHANNELS;
    e->visualizer_buffer = (int16_t *)calloc(e->vis_buf_size, sizeof(int16_t));
    if (!e->visualizer_buffer) {
        free_pcm_buffers(e);
        return -3;
    }

    /* Acquire OS-level BGM port ownership (required before sceAudioOutOpenPort
     * with SCE_AUDIO_OUT_PORT_TYPE_BGM, same as ElevenMPV).               */
    acquire_bgm_port();

    /* Open BGM port — unlike MAIN (48000 Hz only), BGM supports all common
     * sample rates: 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100,
     * 48000.  Start at 48000 Hz; we'll reconfigure per-track as needed.   */
    e->port_sample_rate = AUDIO_PORT_RATE;
    e->port = sceAudioOutOpenPort(
        SCE_AUDIO_OUT_PORT_TYPE_BGM,
        GRANULE_SIZE,
        e->port_sample_rate,
        SCE_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (e->port < 0) {
        free(e->visualizer_buffer);
        free_pcm_buffers(e);
        return e->port;
    }

    /* Set initial volume (SCE_AUDIO_VOLUME_0DB = 32768 = full volume) */
    int vols[2] = { e->volume, e->volume };
    sceAudioOutSetVolume(e->port,
        SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH,
        vols);

    /* Synchronisation primitives */
    e->mutex = sceKernelCreateMutex("AE_mutex", 0, 0, NULL);
    if (e->mutex < 0) {
        sceAudioOutReleasePort(e->port);
        free(e->visualizer_buffer);
        free_pcm_buffers(e);
        return e->mutex;
    }

    e->vis_mutex = sceKernelCreateMutex("AE_vis_mutex", 0, 0, NULL);
    if (e->vis_mutex < 0) {
        sceKernelDeleteMutex(e->mutex);
        sceAudioOutReleasePort(e->port);
        free(e->visualizer_buffer);
        free_pcm_buffers(e);
        return e->vis_mutex;
    }

    e->semaphore = sceKernelCreateSema("AE_sema", 0, 0, NUM_BUFFERS, NULL);
    if (e->semaphore < 0) {
        sceKernelDeleteMutex(e->vis_mutex);
        sceKernelDeleteMutex(e->mutex);
        sceAudioOutReleasePort(e->port);
        free(e->visualizer_buffer);
        free_pcm_buffers(e);
        return e->semaphore;
    }

    /* Start background thread */
    e->thread_exit = false;
    e->thread = sceKernelCreateThread("VitaWave_audio",
                                      audio_thread_func,
                                      0x10000100,   /* priority */
                                      0x10000,       /* stack size 64 KB */
                                      0, 0, NULL);
    if (e->thread < 0) {
        sceKernelDeleteSema(e->semaphore);
        sceKernelDeleteMutex(e->vis_mutex);
        sceKernelDeleteMutex(e->mutex);
        sceAudioOutReleasePort(e->port);
        free(e->visualizer_buffer);
        free_pcm_buffers(e);
        return e->thread;
    }

    int ret = sceKernelStartThread(e->thread, 0, NULL);
    if (ret < 0) {
        sceKernelDeleteThread(e->thread);
        sceKernelDeleteSema(e->semaphore);
        sceKernelDeleteMutex(e->vis_mutex);
        sceKernelDeleteMutex(e->mutex);
        sceAudioOutReleasePort(e->port);
        free(e->visualizer_buffer);
        free_pcm_buffers(e);
        return ret;
    }

    return 0;
}

/* ── audio_engine_destroy ────────────────────────────────────────────────── */

void audio_engine_destroy(AudioEngine *e)
{
    if (!e) return;

    /* Signal thread to stop */
    e->thread_exit = true;
    e->state       = PLAYBACK_STOPPED;

    /* Clean up any running duration-scan thread before touching the mutex */
    if (e->scan_thread >= 0) {
        sceKernelWaitThreadEnd(e->scan_thread, NULL, NULL);
        sceKernelDeleteThread(e->scan_thread);
        e->scan_thread = -1;
    }

    /* Wait for audio thread to finish */
    sceKernelWaitThreadEnd(e->thread, NULL, NULL);
    sceKernelDeleteThread(e->thread);

    /* Close decoder if still open */
    if (e->decoder) {
        decoder_close(e->decoder);
        e->decoder = NULL;
    }

    /* Release SceAudio resources */
    sceAudioOutReleasePort(e->port);
    sceAppMgrReleaseBgmPort();

    /* Synchronisation */
    sceKernelDeleteSema(e->semaphore);
    sceKernelDeleteMutex(e->vis_mutex);
    sceKernelDeleteMutex(e->mutex);

    /* Memory */
    free(e->visualizer_buffer);
    free_pcm_buffers(e);

    memset(e, 0, sizeof(*e));
}

/* ── audio_engine_play ───────────────────────────────────────────────────── */

int audio_engine_play(AudioEngine *e, const char *filepath)
{
    if (!e || !filepath) return -1;

    /* Stop any current playback (state=STOPPED, decoder=NULL) */
    audio_engine_stop(e);

    /* Open decoder – this tells us the file's native sample rate */
    e->decoder = decoder_open(filepath);
    if (!e->decoder) {
        return -2;
    }

    DecoderInfo info = decoder_get_info(e->decoder);
    e->duration_ms = info.duration_ms;   /* 0 if VBR without Xing header */
    e->position_ms = 0;

    /* Switch BGM port to the file's native sample rate if needed.
     *
     * BGM port (unlike MAIN) supports 8000–48000 Hz.  First try the
     * lightweight sceAudioOutSetConfig path; if that fails (shouldn't on
     * BGM), fall back to release-then-reopen.
     *
     * audio_engine_stop() already set state=STOPPED, so the audio thread
     * is sleeping and will not call sceAudioOutOutput concurrently.      */
    int need_rate = (info.sample_rate > 0) ? (int)info.sample_rate
                                           : e->port_sample_rate;
    port_set_rate(e, need_rate);

    strncpy(e->current_track, filepath, sizeof(e->current_track) - 1);
    e->track_changed = false;
    e->state = PLAYBACK_PLAYING;

    /* If duration is still unknown (VBR MP3 without Xing header), scan in
     * the background so the UI thread is not blocked.                    */
    if (e->duration_ms == 0) {
        e->scan_thread = sceKernelCreateThread("VW_dur_scan",
                                               duration_scan_thread,
                                               0x10000100,
                                               0x4000, 0, 0, NULL);
        if (e->scan_thread >= 0)
            sceKernelStartThread(e->scan_thread, sizeof(e), &e);
    }

    return 0;
}

/* ── audio_engine_pause ──────────────────────────────────────────────────── */

void audio_engine_pause(AudioEngine *e)
{
    if (!e) return;
    if (e->state == PLAYBACK_PLAYING) {
        e->state = PLAYBACK_PAUSED;
    } else if (e->state == PLAYBACK_PAUSED) {
        e->state = PLAYBACK_PLAYING;
    }
}

/* ── audio_engine_stop ───────────────────────────────────────────────────── */

void audio_engine_stop(AudioEngine *e)
{
    if (!e) return;

    /* Signal stopped first so the thread stops trying to decode */
    e->state = PLAYBACK_STOPPED;

    /* Wait for any background duration-scan thread to finish before we
     * lock the mutex and close the decoder it may be reading.           */
    if (e->scan_thread >= 0) {
        sceKernelWaitThreadEnd(e->scan_thread, NULL, NULL);
        sceKernelDeleteThread(e->scan_thread);
        e->scan_thread = -1;
    }

    /* Lock the decoder mutex.  The audio thread releases this mutex just
     * before calling sceAudioOutOutput (which blocks ~23 ms), so we will
     * acquire it quickly once any in-progress decode finishes.           */
    sceKernelLockMutex(e->mutex, 1, NULL);

    if (e->next_dec) {
        decoder_close(e->next_dec);
        e->next_dec = NULL;
    }
    if (e->decoder) {
        decoder_close(e->decoder);
        e->decoder = NULL;
    }
    e->position_ms       = 0;
    e->duration_ms       = 0;
    e->current_track[0]  = '\0';

    sceKernelUnlockMutex(e->mutex, 1);
}

/* ── audio_engine_next ───────────────────────────────────────────────────── */

int audio_engine_next(AudioEngine *e, Playlist *pl)
{
    if (!e) return -1;
    e->auto_advance = false;

    /* Try playlist first */
    if (pl) {
        pl->repeat_mode = e->repeat_mode;  /* keep in sync */
        int ni = playlist_next(pl);
        if (ni >= 0) {
            PlaylistEntry *entry = playlist_get_current(pl);
            if (entry) return audio_engine_play(e, entry->filepath);
        }
    }

    /* Playlist exhausted — fall back to next file in same directory only
     * when no playlist is loaded (e.g. cold start before any directory play) */
    if (e->current_track[0] && (!pl || pl->count == 0)) {
        char next_path[MAX_PATH_LEN];
        if (dir_fallback(e->current_track, true, next_path, sizeof(next_path)) == 0)
            return audio_engine_play(e, next_path);
    }
    return -1;
}

/* ── audio_engine_prev ───────────────────────────────────────────────────── */

int audio_engine_prev(AudioEngine *e, Playlist *pl)
{
    if (!e || !pl) return -1;

    /* If we're more than 3 seconds in, restart the current track */
    if (e->position_ms > 3000) {
        e->position_ms = 0;
        if (e->decoder) decoder_seek(e->decoder, 0);
        return 0;
    }

    if (pl) {
        pl->repeat_mode = e->repeat_mode;  /* keep in sync */
        int pi = playlist_prev(pl);
        if (pi >= 0) {
            PlaylistEntry *entry = playlist_get_current(pl);
            if (entry) return audio_engine_play(e, entry->filepath);
        }
    }

    /* Playlist exhausted — fall back to prev file in same directory only
     * when no playlist is loaded */
    if (e->current_track[0] && (!pl || pl->count == 0)) {
        char prev_path[MAX_PATH_LEN];
        if (dir_fallback(e->current_track, false, prev_path, sizeof(prev_path)) == 0)
            return audio_engine_play(e, prev_path);
    }
    return -1;
}

/* ── audio_engine_set_volume ─────────────────────────────────────────────── */

void audio_engine_set_volume(AudioEngine *e, int volume)
{
    if (!e) return;
    if (volume < 0)          volume = 0;
    if (volume > MAX_VOLUME) volume = MAX_VOLUME;
    e->volume = volume;

    int vols[2] = { volume, volume };
    sceAudioOutSetVolume(e->port,
        SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH,
        vols);
}

/* ── audio_engine_set_repeat ─────────────────────────────────────────────── */

void audio_engine_set_repeat(AudioEngine *e, RepeatMode mode)
{
    if (!e) return;
    e->repeat_mode = mode;
}

/* ── audio_engine_toggle_shuffle ─────────────────────────────────────────── */

void audio_engine_toggle_shuffle(AudioEngine *e)
{
    if (!e) return;
    e->shuffle = !e->shuffle;

    Playlist *pl = get_playlist();
    if (pl) {
        if (e->shuffle)
            playlist_shuffle(pl);
        else
            playlist_unshuffle(pl);
    }
}

/* ── audio_engine_get_position ───────────────────────────────────────────── */

uint64_t audio_engine_get_position(const AudioEngine *e)
{
    if (!e) return 0;
    return e->position_ms;
}

/* ── audio_engine_get_duration ───────────────────────────────────────────── */

uint64_t audio_engine_get_duration(const AudioEngine *e)
{
    if (!e) return 0;
    return e->duration_ms;
}

/* ── audio_engine_get_state ──────────────────────────────────────────────── */

PlaybackState audio_engine_get_state(const AudioEngine *e)
{
    if (!e) return PLAYBACK_STOPPED;
    return e->state;
}

/* ── audio_engine_get_visualizer_data ────────────────────────────────────── */

uint32_t audio_engine_get_visualizer_data(AudioEngine *e,
                                          int16_t *out, uint32_t count)
{
    if (!e || !out || count == 0) return 0;

    sceKernelLockMutex(e->vis_mutex, 1, NULL);
    uint32_t n = count < e->vis_buf_size ? count : e->vis_buf_size;
    memcpy(out, e->visualizer_buffer, n * sizeof(int16_t));
    sceKernelUnlockMutex(e->vis_mutex, 1);
    return n;
}

/* ── audio_engine_seek ───────────────────────────────────────────────────── */

int audio_engine_seek(AudioEngine *e, uint64_t position_ms)
{
    if (!e || !e->decoder) return -1;

    /* Pause the audio thread so it stops decoding, then hold the decoder
     * mutex for the seek.  The thread releases the mutex just before its
     * blocking sceAudioOutOutput call, so we acquire it quickly.         */
    PlaybackState saved_state = e->state;
    e->state = PLAYBACK_PAUSED;

    sceKernelLockMutex(e->mutex, 1, NULL);

    int ret = -1;
    if (e->decoder) {
        ret = decoder_seek(e->decoder, position_ms);
        if (ret == 0) {
            e->position_ms        = position_ms;
            e->decoder->state     = DECODER_STATE_OPEN; /* clear any EOF */
        }
    }

    sceKernelUnlockMutex(e->mutex, 1);

    e->state = saved_state;
    return ret;
}

