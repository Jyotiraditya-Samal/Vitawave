/*
 * VitaWave – playlist.c
 * Playlist management: creation, navigation, shuffle, M3U save/load.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

#include "playlist.h"
#include "file_browser.h"
#include "metadata.h"

/* ── Internal helpers ────────────────────────────────────────────────────── */

#define INITIAL_CAPACITY 256

static int cmp_paths_alpha(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

/* Simple LCG pseudo-random for shuffle (Vita doesn't always seed rand well) */
static uint32_t lcg_state = 12345;
static uint32_t lcg_rand(void)
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

/* ── playlist_create ─────────────────────────────────────────────────────── */

Playlist *playlist_create(const char *name)
{
    Playlist *pl = (Playlist *)calloc(1, sizeof(Playlist));
    if (!pl) return NULL;

    pl->capacity = INITIAL_CAPACITY;
    pl->entries  = (PlaylistEntry *)calloc((size_t)pl->capacity,
                                            sizeof(PlaylistEntry));
    if (!pl->entries) {
        free(pl);
        return NULL;
    }

    pl->shuffle_order = (int *)malloc((size_t)pl->capacity * sizeof(int));
    if (!pl->shuffle_order) {
        free(pl->entries);
        free(pl);
        return NULL;
    }

    for (int i = 0; i < pl->capacity; i++) pl->shuffle_order[i] = i;

    if (name) {
        strncpy(pl->name, name, MAX_PLAYLIST_NAME - 1);
    } else {
        strncpy(pl->name, "Unnamed", MAX_PLAYLIST_NAME - 1);
    }

    pl->count         = 0;
    pl->current_index = -1;
    pl->is_shuffled   = false;
    pl->repeat_mode   = REPEAT_NONE;

    /* Seed LCG from process time */
    lcg_state = (uint32_t)sceKernelGetProcessTimeWide();

    return pl;
}

/* ── playlist_destroy ────────────────────────────────────────────────────── */

void playlist_destroy(Playlist *pl)
{
    if (!pl) return;
    free(pl->entries);
    free(pl->shuffle_order);
    free(pl);
}

/* ── Internal: grow the playlist arrays ─────────────────────────────────── */

static int ensure_capacity(Playlist *pl, int needed)
{
    if (needed <= pl->capacity) return 0;
    if (needed > MAX_PLAYLIST_SIZE) return -1;

    int new_cap = pl->capacity * 2;
    if (new_cap < needed) new_cap = needed;
    if (new_cap > MAX_PLAYLIST_SIZE) new_cap = MAX_PLAYLIST_SIZE;

    PlaylistEntry *ne = (PlaylistEntry *)realloc(pl->entries,
                            (size_t)new_cap * sizeof(PlaylistEntry));
    if (!ne) return -2;
    pl->entries = ne;

    int *ns = (int *)realloc(pl->shuffle_order, (size_t)new_cap * sizeof(int));
    if (!ns) return -3;
    pl->shuffle_order = ns;

    /* Initialise new shuffle slots sequentially */
    for (int i = pl->capacity; i < new_cap; i++) pl->shuffle_order[i] = i;
    pl->capacity = new_cap;
    return 0;
}

/* ── playlist_add_track ──────────────────────────────────────────────────── */

int playlist_add_track(Playlist *pl, const char *filepath,
                       const char *title, const char *artist,
                       uint64_t duration_ms)
{
    if (!pl || !filepath) return -1;
    if (pl->count >= MAX_PLAYLIST_SIZE) return -1;

    if (ensure_capacity(pl, pl->count + 1) < 0) return -2;

    PlaylistEntry *e = &pl->entries[pl->count];
    memset(e, 0, sizeof(*e));
    strncpy(e->filepath, filepath, sizeof(e->filepath) - 1);
    if (title)  strncpy(e->title,  title,  sizeof(e->title)  - 1);
    if (artist) strncpy(e->artist, artist, sizeof(e->artist) - 1);
    e->duration_ms = duration_ms;
    e->loaded      = (title != NULL && title[0] != '\0');

    pl->shuffle_order[pl->count] = pl->count;
    pl->count++;

    /* Set current_index to 0 if this is the first track */
    if (pl->current_index < 0) pl->current_index = 0;

    return 0;
}

/* ── playlist_remove_track ───────────────────────────────────────────────── */

int playlist_remove_track(Playlist *pl, int index)
{
    if (!pl || index < 0 || index >= pl->count) return -1;

    /* Shift entries down */
    for (int i = index; i < pl->count - 1; i++) {
        pl->entries[i] = pl->entries[i + 1];
    }
    pl->count--;

    /* Rebuild shuffle order */
    for (int i = 0; i < pl->count; i++) pl->shuffle_order[i] = i;
    if (pl->is_shuffled) playlist_shuffle(pl);

    /* Adjust current_index */
    if (pl->count == 0) {
        pl->current_index = -1;
    } else if (pl->current_index >= pl->count) {
        pl->current_index = pl->count - 1;
    } else if (pl->current_index > index) {
        pl->current_index--;
    }
    return 0;
}

/* ── playlist_clear ──────────────────────────────────────────────────────── */

void playlist_clear(Playlist *pl)
{
    if (!pl) return;
    pl->count         = 0;
    pl->current_index = -1;
    pl->is_shuffled   = false;
    for (int i = 0; i < pl->capacity; i++) pl->shuffle_order[i] = i;
}

/* ── playlist_next ───────────────────────────────────────────────────────── */

int playlist_next(Playlist *pl)
{
    if (!pl || pl->count == 0) return -1;

    if (pl->repeat_mode == REPEAT_ONE) {
        /* Stay on current track */
        return pl->current_index;
    }

    int next;
    if (pl->is_shuffled) {
        /* Find current position in shuffle_order */
        int pos = -1;
        for (int i = 0; i < pl->count; i++) {
            if (pl->shuffle_order[i] == pl->current_index) {
                pos = i;
                break;
            }
        }
        if (pos < 0) pos = 0;
        int next_pos = pos + 1;
        if (next_pos >= pl->count) {
            if (pl->repeat_mode == REPEAT_ALL) {
                next_pos = 0;
            } else {
                return -1;  /* end of playlist */
            }
        }
        next = pl->shuffle_order[next_pos];
    } else {
        next = pl->current_index + 1;
        if (next >= pl->count) {
            if (pl->repeat_mode == REPEAT_ALL) {
                next = 0;
            } else {
                return -1;  /* end of playlist */
            }
        }
    }

    pl->current_index = next;
    return next;
}

/* ── playlist_prev ───────────────────────────────────────────────────────── */

int playlist_prev(Playlist *pl)
{
    if (!pl || pl->count == 0) return -1;

    if (pl->repeat_mode == REPEAT_ONE) {
        return pl->current_index;
    }

    int prev;
    if (pl->is_shuffled) {
        int pos = -1;
        for (int i = 0; i < pl->count; i++) {
            if (pl->shuffle_order[i] == pl->current_index) {
                pos = i;
                break;
            }
        }
        if (pos < 0) pos = 0;
        int prev_pos = pos - 1;
        if (prev_pos < 0) {
            if (pl->repeat_mode == REPEAT_ALL) {
                prev_pos = pl->count - 1;
            } else {
                return -1;
            }
        }
        prev = pl->shuffle_order[prev_pos];
    } else {
        prev = pl->current_index - 1;
        if (prev < 0) {
            if (pl->repeat_mode == REPEAT_ALL) {
                prev = pl->count - 1;
            } else {
                return -1;
            }
        }
    }

    pl->current_index = prev;
    return prev;
}

/* ── playlist_get_current ────────────────────────────────────────────────── */

PlaylistEntry *playlist_get_current(Playlist *pl)
{
    if (!pl || pl->count == 0 || pl->current_index < 0 ||
        pl->current_index >= pl->count) {
        return NULL;
    }
    return &pl->entries[pl->current_index];
}

/* ── playlist_set_index ──────────────────────────────────────────────────── */

int playlist_set_index(Playlist *pl, int index)
{
    if (!pl || index < 0 || index >= pl->count) return -1;
    pl->current_index = index;
    return 0;
}

/* ── playlist_shuffle ────────────────────────────────────────────────────── */

void playlist_shuffle(Playlist *pl)
{
    if (!pl || pl->count <= 1) return;

    /* Initialise shuffle_order to 0..count-1 */
    for (int i = 0; i < pl->count; i++) pl->shuffle_order[i] = i;

    /* Fisher-Yates */
    for (int i = pl->count - 1; i > 0; i--) {
        int j = (int)(lcg_rand() % (uint32_t)(i + 1));
        int tmp = pl->shuffle_order[i];
        pl->shuffle_order[i] = pl->shuffle_order[j];
        pl->shuffle_order[j] = tmp;
    }
    pl->is_shuffled = true;
}

/* ── playlist_unshuffle ──────────────────────────────────────────────────── */

void playlist_unshuffle(Playlist *pl)
{
    if (!pl) return;
    for (int i = 0; i < pl->capacity; i++) pl->shuffle_order[i] = i;
    pl->is_shuffled = false;
}

/* ── playlist_add_directory ──────────────────────────────────────────────── */

int playlist_add_directory(Playlist *pl, const char *dirpath)
{
    if (!pl || !dirpath) return -1;

    static char paths[MAX_PLAYLIST_SIZE][MAX_PATH_LEN];
    int found = 0;
    file_browser_scan_recursive(dirpath, paths, MAX_PLAYLIST_SIZE, &found);

    /* Sort paths alphabetically so directory playback is always in order */
    qsort(paths, found, MAX_PATH_LEN, cmp_paths_alpha);

    int added = 0;
    for (int i = 0; i < found; i++) {
        TrackMetadata tm;
        metadata_load_tags(&tm, paths[i]);
        if (playlist_add_track(pl, paths[i], tm.title, tm.artist, tm.duration_ms) == 0) {
            added++;
        }
    }
    return added;
}

/* ── playlist_save ───────────────────────────────────────────────────────── */

int playlist_save(const Playlist *pl)
{
    if (!pl) return -1;

    /* Ensure directory exists */
    sceIoMkdir(PLAYLIST_SAVE_DIR, 0777);

    char save_path[MAX_PATH_LEN];
    snprintf(save_path, sizeof(save_path), "%s%s.m3u",
             PLAYLIST_SAVE_DIR, pl->name);

    SceUID fd = sceIoOpen(save_path,
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return (int)fd;

    /* M3U header */
    const char *header = "#EXTM3U\n";
    sceIoWrite(fd, header, strlen(header));

    for (int i = 0; i < pl->count; i++) {
        const PlaylistEntry *e = &pl->entries[i];
        char line[1024];

        /* #EXTINF:<duration_seconds>,<artist> - <title> */
        int dur_s = (int)(e->duration_ms / 1000ULL);
        if (e->title[0]) {
            snprintf(line, sizeof(line), "#EXTINF:%d,%s - %s\n",
                     dur_s, e->artist, e->title);
        } else {
            snprintf(line, sizeof(line), "#EXTINF:%d,\n", dur_s);
        }
        sceIoWrite(fd, line, strlen(line));

        /* filepath */
        snprintf(line, sizeof(line), "%s\n", e->filepath);
        sceIoWrite(fd, line, strlen(line));
    }

    sceIoClose(fd);
    return 0;
}

/* ── playlist_load ───────────────────────────────────────────────────────── */

int playlist_load(Playlist *pl, const char *filepath)
{
    if (!pl || !filepath) return -1;

    SceUID fd = sceIoOpen(filepath, SCE_O_RDONLY, 0);
    if (fd < 0) return (int)fd;

    /* Read whole file */
    SceIoStat st;
    sceIoGetstatByFd(fd, &st);
    int64_t file_size = st.st_size;
    if (file_size <= 0 || file_size > 1024 * 1024) {
        sceIoClose(fd);
        return -2;
    }

    char *buf = (char *)malloc((size_t)file_size + 1);
    if (!buf) { sceIoClose(fd); return -3; }

    sceIoRead(fd, buf, (SceSize)file_size);
    buf[file_size] = '\0';
    sceIoClose(fd);

    /* Parse line by line */
    char last_title[256]  = {0};
    char last_artist[256] = {0};
    int  last_dur_s       = 0;

    char *line = buf;
    while (line && *line) {
        /* Find end of line */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        /* Strip \r */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') line[len - 1] = '\0';

        if (strncmp(line, "#EXTM3U", 7) == 0) {
            /* header – skip */
        } else if (strncmp(line, "#EXTINF:", 8) == 0) {
            /* Parse #EXTINF:<seconds>,<artist> - <title> */
            const char *p = line + 8;
            last_dur_s = atoi(p);
            const char *comma = strchr(p, ',');
            if (comma) {
                /* Try to split "Artist - Title" */
                const char *dash = strstr(comma + 1, " - ");
                if (dash) {
                    size_t artist_len = (size_t)(dash - (comma + 1));
                    if (artist_len >= sizeof(last_artist))
                        artist_len = sizeof(last_artist) - 1;
                    memcpy(last_artist, comma + 1, artist_len);
                    last_artist[artist_len] = '\0';
                    strncpy(last_title, dash + 3, sizeof(last_title) - 1);
                } else {
                    strncpy(last_title, comma + 1, sizeof(last_title) - 1);
                    last_artist[0] = '\0';
                }
            }
        } else if (line[0] != '#' && line[0] != '\0') {
            /* Treat as file path; load tags from file if EXTINF had no title */
            if (!last_title[0]) {
                TrackMetadata tm;
                metadata_load_tags(&tm, line);
                strncpy(last_title,  tm.title,  sizeof(last_title)  - 1);
                strncpy(last_artist, tm.artist, sizeof(last_artist) - 1);
                if (!last_dur_s && tm.duration_ms)
                    last_dur_s = (int)(tm.duration_ms / 1000);
            }
            playlist_add_track(pl, line,
                               last_title[0]  ? last_title  : NULL,
                               last_artist[0] ? last_artist : NULL,
                               (uint64_t)last_dur_s * 1000ULL);
            last_title[0]  = '\0';
            last_artist[0] = '\0';
            last_dur_s     = 0;
        }

        line = nl ? nl + 1 : NULL;
    }

    free(buf);
    return 0;
}

/* ── playlist_move_track ─────────────────────────────────────────────────── */

int playlist_move_track(Playlist *pl, int from_index, int to_index)
{
    if (!pl) return -1;
    if (from_index < 0 || from_index >= pl->count) return -1;
    if (to_index   < 0 || to_index   >= pl->count) return -1;
    if (from_index == to_index) return 0;

    PlaylistEntry tmp = pl->entries[from_index];

    if (from_index < to_index) {
        for (int i = from_index; i < to_index; i++) {
            pl->entries[i] = pl->entries[i + 1];
        }
    } else {
        for (int i = from_index; i > to_index; i--) {
            pl->entries[i] = pl->entries[i - 1];
        }
    }
    pl->entries[to_index] = tmp;

    /* Adjust current_index */
    if (pl->current_index == from_index) {
        pl->current_index = to_index;
    } else if (from_index < to_index) {
        if (pl->current_index > from_index && pl->current_index <= to_index)
            pl->current_index--;
    } else {
        if (pl->current_index >= to_index && pl->current_index < from_index)
            pl->current_index++;
    }

    /* Rebuild shuffle order */
    for (int i = 0; i < pl->count; i++) pl->shuffle_order[i] = i;
    if (pl->is_shuffled) playlist_shuffle(pl);

    return 0;
}

/* ── playlist_get_count ──────────────────────────────────────────────────── */

int playlist_get_count(const Playlist *pl)
{
    if (!pl) return 0;
    return pl->count;
}

/* ── playlist_get_index ──────────────────────────────────────────────────── */

int playlist_get_index(const Playlist *pl)
{
    if (!pl) return -1;
    return pl->current_index;
}

/* ── PlaylistManager ─────────────────────────────────────────────────────── */

int playlist_manager_init(PlaylistManager *mgr)
{
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(*mgr));
    return 0;
}

void playlist_manager_destroy(PlaylistManager *mgr)
{
    if (!mgr) return;
    for (int i = 0; i < mgr->count; i++) {
        if (mgr->lists[i]) {
            playlist_destroy(mgr->lists[i]);
            mgr->lists[i] = NULL;
        }
    }
    memset(mgr, 0, sizeof(*mgr));
}

int playlist_manager_new(PlaylistManager *mgr)
{
    if (!mgr) return -1;
    if (mgr->count >= MAX_PLAYLISTS) return -1;

    char name[MAX_PLAYLIST_NAME];
    snprintf(name, sizeof(name), "Playlist %d", mgr->count + 1);

    Playlist *pl = playlist_create(name);
    if (!pl) return -1;

    int idx = mgr->count;
    mgr->lists[idx] = pl;
    mgr->count++;
    return idx;
}

int playlist_manager_delete(PlaylistManager *mgr, int idx)
{
    if (!mgr) return -1;
    if (idx < 0 || idx >= mgr->count) return -1;

    /* Delete the M3U file */
    if (mgr->lists[idx]) {
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s%s.m3u",
                 PLAYLIST_SAVE_DIR, mgr->lists[idx]->name);
        sceIoRemove(path);
    }

    playlist_destroy(mgr->lists[idx]);

    /* Shift remaining pointers left */
    for (int i = idx; i < mgr->count - 1; i++) {
        mgr->lists[i] = mgr->lists[i + 1];
    }
    mgr->lists[mgr->count - 1] = NULL;
    mgr->count--;
    return 0;
}

int playlist_manager_save(const PlaylistManager *mgr)
{
    if (!mgr) return -1;
    sceIoMkdir(PLAYLIST_SAVE_DIR, 0777);
    for (int i = 0; i < mgr->count; i++) {
        if (mgr->lists[i]) playlist_save(mgr->lists[i]);
    }
    return 0;
}

int playlist_manager_load(PlaylistManager *mgr)
{
    if (!mgr) return -1;

    /* Destroy existing playlists */
    for (int i = 0; i < mgr->count; i++) {
        if (mgr->lists[i]) {
            playlist_destroy(mgr->lists[i]);
            mgr->lists[i] = NULL;
        }
    }
    mgr->count = 0;

    /* Scan PLAYLIST_SAVE_DIR for *.m3u files */
    SceUID dir = sceIoDopen(PLAYLIST_SAVE_DIR);
    if (dir < 0) return 0;  /* directory doesn't exist yet — no playlists */

    SceIoDirent ent;
    memset(&ent, 0, sizeof(ent));

    while (sceIoDread(dir, &ent) > 0 && mgr->count < MAX_PLAYLISTS) {
        const char *fname = ent.d_name;
        int l = (int)strlen(fname);

        if (l <= 4 || strcmp(fname + l - 4, ".m3u") != 0) {
            memset(&ent, 0, sizeof(ent));
            continue;
        }

        /* Playlist name = filename without the .m3u extension */
        char pl_name[MAX_PLAYLIST_NAME];
        int name_len = l - 4;
        if (name_len >= MAX_PLAYLIST_NAME) name_len = MAX_PLAYLIST_NAME - 1;
        memcpy(pl_name, fname, name_len);
        pl_name[name_len] = '\0';

        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s%s", PLAYLIST_SAVE_DIR, fname);

        Playlist *pl = playlist_create(pl_name);
        if (pl) {
            playlist_load(pl, path);
            mgr->lists[mgr->count++] = pl;
        }

        memset(&ent, 0, sizeof(ent));
    }

    sceIoDclose(dir);
    return 0;
}
