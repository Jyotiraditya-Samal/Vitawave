#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <stdint.h>
#include <stdbool.h>

#define PLAYLIST_MAX_ENTRIES 512
#define PLAYLIST_PATH_LEN    512

typedef struct {
    char path[PLAYLIST_PATH_LEN];
    char name[256];
} PlaylistEntry;

typedef struct {
    PlaylistEntry entries[PLAYLIST_MAX_ENTRIES];
    int           count;
    int           current; /* currently playing index */
} Playlist;

void playlist_init(Playlist *pl);
int  playlist_add(Playlist *pl, const char *path);
void playlist_clear(Playlist *pl);
const char *playlist_get_current(Playlist *pl);
const char *playlist_next(Playlist *pl);
const char *playlist_prev(Playlist *pl);

#endif
void playlist_set(Playlist *pl, int index);
void playlist_sort(Playlist *pl);
