#include <stdio.h>
#include <psp2/io/dirent.h>
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

void playlist_set(Playlist *pl, int index)
{
    if (!pl || index < 0 || index >= pl->count) return;
    pl->current = index;
}

static int entry_cmp(const void *a, const void *b)
{
    const PlaylistEntry *ea = (const PlaylistEntry *)a;
    const PlaylistEntry *eb = (const PlaylistEntry *)b;
    return strcasecmp(ea->name, eb->name);
}

void playlist_sort(Playlist *pl)
{
    if (!pl || pl->count < 2) return;
    qsort(pl->entries, pl->count, sizeof(PlaylistEntry), entry_cmp);
}

/* named playlist: add a name field - note this extends existing struct via the header */
int playlist_save_m3u(const Playlist *pl, const char *path)
{
    if (!pl || !path) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "#EXTM3U\n");
    for (int i = 0; i < pl->count; i++) {
        fprintf(f, "#EXTINF:-1,%s\n%s\n",
                pl->entries[i].name, pl->entries[i].path);
    }
    fclose(f);
    return 0;
}

int playlist_load_m3u(Playlist *pl, const char *path)
{
    if (!pl || !path) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    playlist_clear(pl);
    char line[PLAYLIST_PATH_LEN];
    char pending_name[256] = {0};
    while (fgets(line, sizeof(line), f)) {
        /* strip newline */
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#') {
            if (strncmp(line, "#EXTINF:", 8) == 0) {
                char *comma = strchr(line, ',');
                if (comma) strncpy(pending_name, comma + 1, 255);
            }
            continue;
        }
        if (line[0] == '\0') continue;
        if (pl->count >= PLAYLIST_MAX_ENTRIES) break;
        strncpy(pl->entries[pl->count].path, line, PLAYLIST_PATH_LEN - 1);
        if (pending_name[0]) {
            strncpy(pl->entries[pl->count].name, pending_name, 255);
            pending_name[0] = '\0';
        } else {
            const char *slash = strrchr(line, '/');
            strncpy(pl->entries[pl->count].name, slash ? slash + 1 : line, 255);
        }
        pl->count++;
    }
    fclose(f);
    return 0;
}

void playlist_manager_init(PlaylistManager *mgr)
{
    if (!mgr) return;
    memset(mgr, 0, sizeof(*mgr));
    for (int i = 0; i < PLAYLIST_MANAGER_MAX; i++)
        playlist_init(&mgr->playlists[i]);
}

int playlist_manager_load_all(PlaylistManager *mgr, const char *dir)
{
    if (!mgr || !dir) return -1;
    /* scan dir for .m3u files */
    SceUID d = sceIoDopen(dir);
    if (d < 0) return -1;
    SceIoDirent ent;
    while (sceIoDread(d, &ent) > 0 && mgr->count < PLAYLIST_MANAGER_MAX) {
        if (ent.d_name[0] == '.') continue;
        size_t nlen = strlen(ent.d_name);
        if (nlen < 4 || strcasecmp(ent.d_name + nlen - 4, ".m3u") != 0) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, ent.d_name);
        playlist_load_m3u(&mgr->playlists[mgr->count], path);
        strncpy(mgr->names[mgr->count], ent.d_name, PLAYLIST_NAME_LEN - 1);
        /* strip .m3u from display name */
        size_t nlen2 = strlen(mgr->names[mgr->count]);
        if (nlen2 > 4) mgr->names[mgr->count][nlen2 - 4] = '\0';
        mgr->count++;
    }
    sceIoDclose(d);
    return 0;
}

int playlist_manager_save(PlaylistManager *mgr, int idx, const char *dir)
{
    if (!mgr || idx < 0 || idx >= mgr->count || !dir) return -1;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.m3u", dir, mgr->names[idx]);
    return playlist_save_m3u(&mgr->playlists[idx], path);
}
