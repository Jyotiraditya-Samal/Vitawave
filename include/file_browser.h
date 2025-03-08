#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_FILES_PER_DIR  512
#define MAX_PATH_LEN       256
#define MAX_FILENAME_LEN   128
#define MUSIC_ROOT         "ux0:/music"

typedef enum {
    FILE_TYPE_UNKNOWN = 0,
    FILE_TYPE_DIRECTORY,
    FILE_TYPE_MP3,
} FileType;

typedef struct {
    char     name[MAX_FILENAME_LEN];
    char     path[MAX_PATH_LEN];
    bool     is_directory;
    FileType type;
    uint64_t size;
} FileEntry;

typedef struct {
    FileEntry *entries;
    int        count;
    int        capacity;
    char       current_dir[MAX_PATH_LEN];
    char       parent_dir[MAX_PATH_LEN];
} FileList;

FileList *file_browser_init(void);
void      file_browser_destroy(FileList *list);
int       file_browser_scan_dir(FileList *list, const char *dirpath);
int       file_browser_navigate_into(FileList *list, const char *fullpath);
int       file_browser_navigate_up(FileList *list);
bool      file_browser_is_audio_file(const char *filename);

#endif
