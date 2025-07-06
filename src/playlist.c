#include <string.h>
#include "playlist.h"

void playlist_init(Playlist *pl)
{
    if (!pl) return;
    memset(pl, 0, sizeof(*pl));
    pl->current = -1;
}

int playlist_add(Playlist *pl, const char *path)
{
    if (!pl || !path || pl->count >= PLAYLIST_MAX_ENTRIES) return -1;
    strncpy(pl->entries[pl->count].path, path, PLAYLIST_PATH_LEN - 1);
    /* use filename as display name */
    const char *slash = strrchr(path, '/');
    strncpy(pl->entries[pl->count].name, slash ? slash + 1 : path, 255);
    pl->count++;
    return 0;
}

void playlist_clear(Playlist *pl)
{
    if (!pl) return;
    pl->count   = 0;
    pl->current = -1;
}

const char *playlist_get_current(Playlist *pl)
{
    if (!pl || pl->current < 0 || pl->current >= pl->count) return NULL;
    return pl->entries[pl->current].path;
}

const char *playlist_next(Playlist *pl)
{
    if (!pl || pl->count == 0) return NULL;
    if (pl->current < pl->count - 1) pl->current++;
    return playlist_get_current(pl);
}

const char *playlist_prev(Playlist *pl)
{
    if (!pl || pl->count == 0) return NULL;
    if (pl->current > 0) pl->current--;
    return playlist_get_current(pl);
}
