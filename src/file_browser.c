#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

#include "file_browser.h"

static char *str_tolower_copy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n - 1 && src[i]; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
    return dst;
}

static int entry_cmp(const void *a, const void *b)
{
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;
    if (fa->is_directory && !fb->is_directory) return -1;
    if (!fa->is_directory && fb->is_directory) return  1;
    return strcmp(fa->name, fb->name);
}

static void path_join(char *out, const char *dir, const char *name)
{
    size_t dir_len = strlen(dir);
    if (dir_len > 0 && dir[dir_len - 1] == '/')
        snprintf(out, MAX_PATH_LEN, "%s%s", dir, name);
    else
        snprintf(out, MAX_PATH_LEN, "%s/%s", dir, name);
}

static void compute_parent(FileList *list)
{
    strncpy(list->parent_dir, list->current_dir, MAX_PATH_LEN - 1);
    list->parent_dir[MAX_PATH_LEN - 1] = '\0';
    size_t len = strlen(list->parent_dir);
    if (len > 1 && list->parent_dir[len - 1] == '/')
        list->parent_dir[--len] = '\0';
    char *p = strrchr(list->parent_dir, '/');
    if (p) *(p + 1) = '\0';
}

FileList *file_browser_init(void)
{
    FileList *list = (FileList *)calloc(1, sizeof(FileList));
    if (!list) return NULL;
    list->capacity = MAX_FILES_PER_DIR;
    list->entries  = (FileEntry *)calloc((size_t)list->capacity, sizeof(FileEntry));
    if (!list->entries) { free(list); return NULL; }
    list->count = 0;
    strncpy(list->current_dir, MUSIC_ROOT, MAX_PATH_LEN - 1);
    compute_parent(list);
    return list;
}

void file_browser_destroy(FileList *list)
{
    if (!list) return;
    free(list->entries);
    free(list);
}

bool file_browser_is_audio_file(const char *filename)
{
    if (!filename) return false;
    const char *ext = NULL;
    for (const char *p = filename; *p; p++)
        if (*p == '.') ext = p + 1;
    if (!ext) return false;
    char lext[8];
    str_tolower_copy(lext, ext, sizeof(lext));
    return strcmp(lext, "mp3") == 0;
}

int file_browser_scan_dir(FileList *list, const char *dirpath)
{
    if (!list || !dirpath) return -1;
    char scandir[MAX_PATH_LEN];
    strncpy(scandir, dirpath, MAX_PATH_LEN - 1);
    scandir[MAX_PATH_LEN - 1] = '\0';

    SceUID dir = sceIoDopen(scandir);
    if (dir < 0) return dir;

    list->count = 0;
    memset(list->entries, 0, list->capacity * sizeof(FileEntry));

    SceIoDirent dirent;
    memset(&dirent, 0, sizeof(dirent));

    while (sceIoDread(dir, &dirent) > 0 && list->count < list->capacity) {
        const char *name = dirent.d_name;
        if (name[0] == '.') { memset(&dirent, 0, sizeof(dirent)); continue; }

        bool is_dir = SCE_S_ISDIR(dirent.d_stat.st_mode);
        if (!is_dir && !file_browser_is_audio_file(name)) {
            memset(&dirent, 0, sizeof(dirent));
            continue;
        }

        FileEntry *e = &list->entries[list->count];
        strncpy(e->name, name, MAX_FILENAME_LEN - 1);
        path_join(e->path, scandir, name);
        e->is_directory = is_dir;
        e->type         = is_dir ? FILE_TYPE_DIRECTORY : FILE_TYPE_MP3;
        e->size         = is_dir ? 0 : (uint64_t)dirent.d_stat.st_size;
        list->count++;
        memset(&dirent, 0, sizeof(dirent));
    }

    sceIoDclose(dir);

    if (list->count > 1)
        qsort(list->entries, (size_t)list->count, sizeof(FileEntry), entry_cmp);

    strncpy(list->current_dir, scandir, MAX_PATH_LEN - 1);
    list->current_dir[MAX_PATH_LEN - 1] = '\0';
    compute_parent(list);
    return 0;
}

int file_browser_navigate_into(FileList *list, const char *fullpath)
{
    if (!list || !fullpath) return -1;
    return file_browser_scan_dir(list, fullpath);
}

int file_browser_navigate_up(FileList *list)
{
    if (!list) return -1;
    if (!list->parent_dir[0]) return -1;
    if (strcmp(list->current_dir, MUSIC_ROOT) == 0) return -1;
    return file_browser_scan_dir(list, list->parent_dir);
}
