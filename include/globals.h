#ifndef GLOBALS_H
#define GLOBALS_H

/*
 * VitaWave – globals.h
 * Forward declarations for the global subsystem accessor functions defined
 * in main.c.  Any translation unit that needs access to the singleton
 * instances should include this header.
 */

#include "audio_engine.h"
#include "equalizer.h"
#include "playlist.h"
#include "file_browser.h"
#include "visualizer.h"
#include "ui.h"
#include "metadata.h"

#ifdef __cplusplus
extern "C" {
#endif

AudioEngine     *get_audio_engine    (void);
Playlist        *get_playlist        (void);
FileList        *get_browser         (void);
UIState         *get_ui_state        (void);
Visualizer      *get_visualizer      (void);
TrackMetadata   *get_current_meta    (void);
PlaylistManager *get_playlist_manager(void);
Equalizer       *get_equalizer       (void);

#ifdef __cplusplus
}
#endif

#endif /* GLOBALS_H */
