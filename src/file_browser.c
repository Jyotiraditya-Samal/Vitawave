/*
 * VitaWave – file_browser.c
 * Directory browsing and audio-file detection using PS Vita IO APIs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

#include "file_browser.h"

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Lower-case string copy, returns dst */
static char *str_tolower_copy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n - 1 && src[i]; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
    return dst;
}

/* qsort comparator: directories first, then alphabetical */
static int entry_cmp(const void *a, const void *b)
{
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;

    /* Directories sort before files */
    if (fa->is_directory && !fb->is_directory) return -1;
    if (!fa->is_directory && fb->is_directory) return  1;

    return strcmp(fa->name, fb->name);
}

/* Build a path by joining dir + "/" + name, clamped to MAX_PATH_LEN */
static void path_join(char *out, const char *dir, const char *name)
{
    size_t dir_len = strlen(dir);
    /* Ensure trailing slash */
    if (dir_len > 0 && dir[dir_len - 1] == '/') {
        snprintf(out, MAX_PATH_LEN, "%s%s", dir, name);
    } else {
        snprintf(out, MAX_PATH_LEN, "%s/%s", dir, name);
    }
}

/* Populate parent_dir from current_dir */
static void compute_parent(FileList *list)
{
    strncpy(list->parent_dir, list->current_dir, MAX_PATH_LEN - 1);
    list->parent_dir[MAX_PATH_LEN - 1] = '\0';

    size_t len = strlen(list->parent_dir);
    /* Strip trailing slash */
    if (len > 1 && list->parent_dir[len - 1] == '/') {
        list->parent_dir[len - 1] = '\0';
        len--;
    }
    /* Walk back to previous slash */
    char *p = strrchr(list->parent_dir, '/');
    if (p) {
        /* Keep the slash */
        *(p + 1) = '\0';
    }
}

/* ── file_browser_init ───────────────────────────────────────────────────── */

FileList *file_browser_init(void)
{
    FileList *list = (FileList *)calloc(1, sizeof(FileList));
    if (!list) return NULL;

    list->capacity = MAX_FILES_PER_DIR;
    list->entries  = (FileEntry *)calloc((size_t)list->capacity,
                                         sizeof(FileEntry));
    if (!list->entries) {
        free(list);
        return NULL;
    }
    list->count = 0;
    strncpy(list->current_dir, MUSIC_ROOT, MAX_PATH_LEN - 1);
    compute_parent(list);
    return list;
}

/* ── file_browser_destroy ────────────────────────────────────────────────── */

void file_browser_destroy(FileList *list)
{
    if (!list) return;
    free(list->entries);
    free(list);
}

/* ── file_browser_free_list ──────────────────────────────────────────────── */

void file_browser_free_list(FileList *list)
{
    if (!list) return;
    free(list->entries);
    list->entries  = NULL;
    list->count    = 0;
    list->capacity = 0;
}

/* ── file_browser_get_file_type ──────────────────────────────────────────── */

FileType file_browser_get_file_type(const char *filename)
{
    if (!filename) return FILE_TYPE_UNKNOWN;

    /* Find last dot */
    const char *ext = NULL;
    for (const char *p = filename; *p; p++) {
        if (*p == '.') ext = p + 1;
    }
    if (!ext) return FILE_TYPE_UNKNOWN;

    char lext[8];
    str_tolower_copy(lext, ext, sizeof(lext));

    if (strcmp(lext, "mp3")  == 0) return FILE_TYPE_MP3;
    if (strcmp(lext, "flac") == 0) return FILE_TYPE_FLAC;
    if (strcmp(lext, "ogg")  == 0) return FILE_TYPE_OGG;
    if (strcmp(lext, "wav")  == 0) return FILE_TYPE_WAV;
    return FILE_TYPE_UNKNOWN;
}

/* ── file_browser_is_audio_file ──────────────────────────────────────────── */

bool file_browser_is_audio_file(const char *filename)
{
    FileType t = file_browser_get_file_type(filename);
    return t == FILE_TYPE_MP3  ||
           t == FILE_TYPE_FLAC ||
           t == FILE_TYPE_OGG  ||
           t == FILE_TYPE_WAV;
}

/* Return true if dirpath contains at least one audio file (recursive).
 * Uses the same iterative stack as file_browser_scan_recursive but stops
 * immediately on the first hit to keep browser navigation fast. */
static bool dir_has_audio(const char *dirpath)
{
    typedef struct { char path[MAX_PATH_LEN]; int depth; } Entry;
    Entry stack[MAX_DIRS_DEEP];
    int top = 0;

    strncpy(stack[0].path, dirpath, MAX_PATH_LEN - 1);
    stack[0].depth = 0;
    top = 1;

    while (top > 0) {
        top--;
        char cur[MAX_PATH_LEN];
        int  depth = stack[top].depth;
        strncpy(cur, stack[top].path, MAX_PATH_LEN - 1);

        SceUID dir = sceIoDopen(cur);
        if (dir < 0) continue;

        SceIoDirent de;
        memset(&de, 0, sizeof(de));
        bool found = false;

        while (sceIoDread(dir, &de) > 0) {
            if (de.d_name[0] == '.') { memset(&de, 0, sizeof(de)); continue; }

            if (SCE_S_ISDIR(de.d_stat.st_mode)) {
                if (depth + 1 < MAX_DIRS_DEEP && top < MAX_DIRS_DEEP) {
                    path_join(stack[top].path, cur, de.d_name);
                    stack[top].depth = depth + 1;
                    top++;
                }
            } else if (file_browser_is_audio_file(de.d_name)) {
                found = true;
                break;
            }
            memset(&de, 0, sizeof(de));
        }

        sceIoDclose(dir);
        if (found) return true;
    }
    return false;
}

/* ── file_browser_scan_dir ───────────────────────────────────────────────── */

int file_browser_scan_dir(FileList *list, const char *dirpath)
{
    if (!list || !dirpath) return -1;

    /* Copy dirpath to a local buffer.  The caller often passes e->path from
     * list->entries[N], which is inside the same array we are about to
     * overwrite.  Without this copy, path_join() would alias src and dst
     * once the scan loop reaches entry N, corrupting all subsequent paths
     * and current_dir.                                                     */
    char scandir[MAX_PATH_LEN];
    strncpy(scandir, dirpath, MAX_PATH_LEN - 1);
    scandir[MAX_PATH_LEN - 1] = '\0';

    /* Open the directory first — if it fails, leave the current listing intact */
    SceUID dir = sceIoDopen(scandir);
    if (dir < 0) return dir;  /* SceUID error code */

    /* Directory opened successfully — safe to reset and repopulate */
    list->count = 0;

    /* Reallocate entries if needed */
    if (!list->entries || list->capacity < MAX_FILES_PER_DIR) {
        free(list->entries);
        list->capacity = MAX_FILES_PER_DIR;
        list->entries  = (FileEntry *)calloc((size_t)list->capacity,
                                              sizeof(FileEntry));
        if (!list->entries) {
            sceIoDclose(dir);
            return -2;
        }
    }

    SceIoDirent dirent;
    memset(&dirent, 0, sizeof(dirent));

    while (sceIoDread(dir, &dirent) > 0 && list->count < list->capacity) {
        const char *name = dirent.d_name;

        /* Skip hidden / navigation entries */
        if (name[0] == '.') {
            memset(&dirent, 0, sizeof(dirent));
            continue;
        }

        bool is_dir = SCE_S_ISDIR(dirent.d_stat.st_mode);

        if (!is_dir && !file_browser_is_audio_file(name)) {
            memset(&dirent, 0, sizeof(dirent));
            continue;
        }

        /* Skip directories that contain no audio files (recursively) */
        if (is_dir) {
            char full[MAX_PATH_LEN];
            path_join(full, scandir, name);
            if (!dir_has_audio(full)) {
                memset(&dirent, 0, sizeof(dirent));
                continue;
            }
        }

        FileEntry *e = &list->entries[list->count];
        strncpy(e->name, name, MAX_FILENAME_LEN - 1);
        path_join(e->path, scandir, name);
        e->is_directory = is_dir;
        e->type         = is_dir ? FILE_TYPE_DIRECTORY
                                 : file_browser_get_file_type(name);
        e->size         = is_dir ? 0
                                 : (uint64_t)dirent.d_stat.st_size;
        list->count++;

        memset(&dirent, 0, sizeof(dirent));
    }

    sceIoDclose(dir);

    /* Sort entries: directories first, then alphabetical */
    if (list->count > 1) {
        qsort(list->entries, (size_t)list->count,
              sizeof(FileEntry), entry_cmp);
    }

    /* Update directory paths */
    strncpy(list->current_dir, scandir, MAX_PATH_LEN - 1);
    list->current_dir[MAX_PATH_LEN - 1] = '\0';
    compute_parent(list);

    return 0;
}

/* ── file_browser_navigate_into ──────────────────────────────────────────── */

int file_browser_navigate_into(FileList *list, const char *fullpath)
{
    if (!list || !fullpath) return -1;
    /* fullpath is always the entry's absolute path (entry->path) */
    return file_browser_scan_dir(list, fullpath);
}

/* ── file_browser_navigate_up ────────────────────────────────────────────── */

int file_browser_navigate_up(FileList *list)
{
    if (!list) return -1;

    /* Already at music root — nowhere to go up */
    if (strcmp(list->current_dir, MUSIC_ROOT) == 0) return -1;

    if (list->parent_dir[0] == '\0') return -1;

    return file_browser_scan_dir(list, list->parent_dir);
}

/* ── file_browser_get_list ───────────────────────────────────────────────── */

const FileList *file_browser_get_list(const FileList *list)
{
    return list;
}

/* ── file_browser_refresh ────────────────────────────────────────────────── */

int file_browser_refresh(FileList *list)
{
    if (!list) return -1;
    return file_browser_scan_dir(list, list->current_dir);
}

/* ── file_browser_scan_recursive ─────────────────────────────────────────── */

ScanResult file_browser_scan_recursive(const char *dirpath,
                                       char (*out_paths)[MAX_PATH_LEN],
                                       int max_count,
                                       int *found_count)
{
    ScanResult result = { 0, 0, false, 0 };
    if (!dirpath || !out_paths || !found_count) {
        result.error_code = -1;
        return result;
    }
    *found_count = 0;

    /* Use a simple iterative stack to avoid deep recursion on Vita */
    typedef struct { char path[MAX_PATH_LEN]; int depth; } DirStackEntry;
    DirStackEntry stack[MAX_DIRS_DEEP];
    int top = 0;

    strncpy(stack[0].path, dirpath, MAX_PATH_LEN - 1);
    stack[0].depth = 0;
    top = 1;

    while (top > 0 && *found_count < max_count) {
        /* Pop */
        top--;
        char cur_path[MAX_PATH_LEN];
        int  cur_depth = stack[top].depth;
        strncpy(cur_path, stack[top].path, MAX_PATH_LEN - 1);

        SceUID dir = sceIoDopen(cur_path);
        if (dir < 0) continue;

        SceIoDirent dirent;
        memset(&dirent, 0, sizeof(dirent));

        while (sceIoDread(dir, &dirent) > 0) {
            const char *name = dirent.d_name;
            if (name[0] == '.') {
                memset(&dirent, 0, sizeof(dirent));
                continue;
            }

            char full[MAX_PATH_LEN];
            path_join(full, cur_path, name);

            if (SCE_S_ISDIR(dirent.d_stat.st_mode)) {
                result.total_dirs++;
                if (cur_depth + 1 < MAX_DIRS_DEEP && top < MAX_DIRS_DEEP) {
                    strncpy(stack[top].path, full, MAX_PATH_LEN - 1);
                    stack[top].depth = cur_depth + 1;
                    top++;
                }
            } else if (file_browser_is_audio_file(name)) {
                if (*found_count < max_count) {
                    strncpy(out_paths[*found_count], full, MAX_PATH_LEN - 1);
                    (*found_count)++;
                    result.total_files++;
                }
            }
            memset(&dirent, 0, sizeof(dirent));
        }
        sceIoDclose(dir);
    }

    result.scan_complete = true;
    return result;
}
