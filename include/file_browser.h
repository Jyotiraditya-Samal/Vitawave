#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <stdint.h>
#include <stdbool.h>

/* ── Limits ───────────────────────────────────────────────────────────────── */
#define MAX_PATH_LEN        512
#define MAX_FILENAME_LEN    256
#define MAX_FILES_PER_DIR   1024
#define MAX_DIRS_DEEP       16
#define MUSIC_ROOT          "ux0:/music/"

/* ── File types ───────────────────────────────────────────────────────────── */
typedef enum {
    FILE_TYPE_UNKNOWN   = 0,
    FILE_TYPE_MP3       = 1,
    FILE_TYPE_FLAC      = 2,
    FILE_TYPE_OGG       = 3,
    FILE_TYPE_WAV       = 4,
    FILE_TYPE_DIRECTORY = 5
} FileType;

/* ── A single directory entry ─────────────────────────────────────────────── */
typedef struct {
    char     name[MAX_FILENAME_LEN];
    char     path[MAX_PATH_LEN];
    FileType type;
    uint64_t size;          /* bytes (0 for directories) */
    bool     is_directory;
} FileEntry;

/* ── A directory listing ──────────────────────────────────────────────────── */
typedef struct {
    FileEntry *entries;                 /* heap-allocated array */
    int        count;
    int        capacity;
    char       current_dir[MAX_PATH_LEN];
    char       parent_dir[MAX_PATH_LEN];
} FileList;

/* ── Result returned by recursive scan ───────────────────────────────────── */
typedef struct {
    int  total_files;
    int  total_dirs;
    bool scan_complete;
    int  error_code;
} ScanResult;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * Allocate and initialise a FileList, set current_dir to MUSIC_ROOT.
 * Returns a valid FileList* on success, NULL on allocation failure.
 */
FileList *file_browser_init(void);

/**
 * Free all memory owned by the list and the list itself.
 */
void file_browser_destroy(FileList *list);

/**
 * Scan the given directory path into list.
 * Entries are sorted: directories first, then audio files, both alphabetically.
 * Returns 0 on success, negative on error.
 */
int file_browser_scan_dir(FileList *list, const char *dirpath);

/**
 * Navigate into a subdirectory given its full path.
 * Use entry->path (not entry->name) — works from both normal and virtual root.
 * Returns 0 on success, negative on error.
 */
int file_browser_navigate_into(FileList *list, const char *fullpath);

/**
 * Navigate up to the parent directory.
 * Returns 0 on success, -1 if already at root.
 */
int file_browser_navigate_up(FileList *list);

/**
 * Return a const pointer to the current file listing.
 */
const FileList *file_browser_get_list(const FileList *list);

/**
 * Return true if the filename has a recognised audio extension.
 */
bool file_browser_is_audio_file(const char *filename);

/**
 * Return the FileType for a given filename (based on extension).
 */
FileType file_browser_get_file_type(const char *filename);

/**
 * Recursively scan dirpath, appending all audio file paths to out_paths.
 * out_paths must point to a char[MAX_PATH_LEN] array of capacity max_count.
 * Returns a ScanResult describing what was found.
 */
ScanResult file_browser_scan_recursive(const char *dirpath,
                                       char (*out_paths)[MAX_PATH_LEN],
                                       int max_count,
                                       int *found_count);

/**
 * Free the entries array inside list (but not the list struct itself).
 */
void file_browser_free_list(FileList *list);

/**
 * Rescan the current directory.
 * Returns 0 on success, negative on error.
 */
int file_browser_refresh(FileList *list);

#endif /* FILE_BROWSER_H */
