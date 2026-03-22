#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <stdint.h>
#include <stdbool.h>

/* Forward-declare RepeatMode (defined in audio_engine.h – included by callers) */
#ifndef REPEAT_NONE
typedef enum {
    REPEAT_NONE = 0,
    REPEAT_ONE  = 1,
    REPEAT_ALL  = 2
} RepeatMode;
#endif

/* ── Limits ───────────────────────────────────────────────────────────────── */
#define MAX_PLAYLIST_SIZE   2048
#define MAX_PLAYLIST_NAME   128

/* ── A single playlist entry ──────────────────────────────────────────────── */
typedef struct {
    char     filepath[512];
    char     title[256];
    char     artist[256];
    uint64_t duration_ms;
    bool     loaded;         /* true once metadata has been fetched */
} PlaylistEntry;

/* ── Playlist ─────────────────────────────────────────────────────────────── */
typedef struct {
    PlaylistEntry *entries;                /* heap-allocated */
    int            count;
    int            capacity;
    char           name[MAX_PLAYLIST_NAME];
    int            current_index;
    int           *shuffle_order;          /* heap-allocated index permutation */
    bool           is_shuffled;
    RepeatMode     repeat_mode;
} Playlist;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * Allocate and initialise a new empty playlist.
 * Returns a valid Playlist* on success, NULL on failure.
 */
Playlist *playlist_create(const char *name);

/**
 * Free all resources owned by the playlist.
 */
void playlist_destroy(Playlist *pl);

/**
 * Append a track to the playlist.
 * Copies filepath; title/artist/duration are filled lazily (loaded=false).
 * Returns 0 on success, -1 if the playlist is full.
 */
int playlist_add_track(Playlist *pl, const char *filepath,
                       const char *title, const char *artist,
                       uint64_t duration_ms);

/**
 * Remove the track at index.
 * Adjusts current_index if necessary.
 * Returns 0 on success, -1 on bad index.
 */
int playlist_remove_track(Playlist *pl, int index);

/**
 * Remove all entries.
 */
void playlist_clear(Playlist *pl);

/**
 * Advance to the next track respecting repeat_mode and shuffle.
 * Returns the new current_index, or -1 if playback should stop.
 */
int playlist_next(Playlist *pl);

/**
 * Go back to the previous track.
 * Returns the new current_index, or -1 if at the beginning with REPEAT_NONE.
 */
int playlist_prev(Playlist *pl);

/**
 * Return a pointer to the current PlaylistEntry, or NULL if the playlist is
 * empty.
 */
PlaylistEntry *playlist_get_current(Playlist *pl);

/**
 * Set current_index with bounds checking.
 * Returns 0 on success, -1 on bad index.
 */
int playlist_set_index(Playlist *pl, int index);

/**
 * Save the playlist to ux0:/music/playlists/<name>.m3u
 * Returns 0 on success, negative on error.
 */
int playlist_save(const Playlist *pl);

/**
 * Load a playlist from a .m3u file, appending entries.
 * Returns 0 on success, negative on error.
 */
int playlist_load(Playlist *pl, const char *filepath);

/**
 * Recursively scan dirpath and add every audio file found.
 * Returns the number of tracks added, negative on error.
 */
int playlist_add_directory(Playlist *pl, const char *dirpath);

/**
 * Apply a Fisher-Yates shuffle to shuffle_order.
 */
void playlist_shuffle(Playlist *pl);

/**
 * Reset shuffle_order to sequential order and clear is_shuffled flag.
 */
void playlist_unshuffle(Playlist *pl);

/**
 * Move the entry at from_index to to_index (shifting entries in between).
 * Returns 0 on success, -1 on bad indices.
 */
int playlist_move_track(Playlist *pl, int from_index, int to_index);

/**
 * Return the number of tracks in the playlist.
 */
int playlist_get_count(const Playlist *pl);

/**
 * Return the current_index.
 */
int playlist_get_index(const Playlist *pl);

/* ── Playlist Manager ─────────────────────────────────────────────────────── */
#define MAX_PLAYLISTS        32
#define PLAYLIST_SAVE_DIR    "ux0:/music/playlists/"

typedef struct {
    Playlist *lists[MAX_PLAYLISTS];
    int       count;
} PlaylistManager;

int  playlist_manager_init   (PlaylistManager *mgr);
void playlist_manager_destroy(PlaylistManager *mgr);
/* Creates a new auto-named playlist ("Playlist 1", "Playlist 2", ...).
 * Returns the new playlist index, or -1 on failure. */
int  playlist_manager_new    (PlaylistManager *mgr);
/* Deletes playlist at idx, shifting remaining entries down. */
int  playlist_manager_delete (PlaylistManager *mgr, int idx);
/* Save all playlists to PLAYLISTS_SAVE_PATH. Returns 0 on success. */
int  playlist_manager_save   (const PlaylistManager *mgr);
/* Load playlists from PLAYLISTS_SAVE_PATH (replaces current contents). */
int  playlist_manager_load   (PlaylistManager *mgr);

#endif /* PLAYLIST_H */
