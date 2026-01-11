/*
 * VitaWave — media_db.c
 * Flush and rebuild the PS Vita system music database.
 * Logic copied directly from MediaImporter by cnsldv (MIT licence).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>

#include "sqlite3.h"
#include "media_db.h"

#define MUSIC_DB  "ux0:mms/music/AVContent.db"
#define MUSIC_DIR "ux0:/music"

static const char *select_content_count_sql =
    "SELECT COUNT(*) FROM tbl_Music WHERE content_path=?";
static const char *select_content_sql =
    "SELECT mrid, content_path, title FROM tbl_Music";
static const char *delete_content_sql =
    "DELETE FROM tbl_Music WHERE mrid=?";
static const char *delete_all_sql =
    "DELETE FROM tbl_Music";
static const char *refresh_db_sql =
    "UPDATE tbl_config SET val=0;";

static const char *insert_music_sql =
    "INSERT INTO tbl_Music ("
    "codec_type, track_num, disc_num, size, container_type, status, analyzed, "
    "icon_data_type, artist, title, created_time, last_updated_time, "
    "imported_time, content_path, album_artist, album_name) "
    "VALUES (12,?,1,?,7,2,1,-1,?,?,datetime('now'),datetime('now'),"
    "datetime('now'),?,?,?)";

/* ── helpers ─────────────────────────────────────────────────────────────── */

static int sql_get_count(sqlite3 *db, const char *sql, const char *fname)
{
    int cnt = 0;
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK)
        return 0;
    sqlite3_bind_text(stmt, 1, fname, strlen(fname), NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        cnt = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return cnt;
}

static void sql_insert_music(sqlite3 *db, const char *path, size_t size)
{
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, insert_music_sql, -1, &stmt, 0) != SQLITE_OK)
        return;
    sqlite3_bind_int   (stmt, 1, 0);                           /* track_num */
    sqlite3_bind_int   (stmt, 2, (int)size);                   /* size      */
    sqlite3_bind_text  (stmt, 3, "Unknown", -1, NULL);         /* artist    */
    sqlite3_bind_text  (stmt, 4, "Unknown", -1, NULL);         /* title     */
    sqlite3_bind_text  (stmt, 5, path,      -1, NULL);         /* path      */
    sqlite3_bind_text  (stmt, 6, "Unknown", -1, NULL);         /* album_art */
    sqlite3_bind_text  (stmt, 7, "Unknown", -1, NULL);         /* album     */
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void sql_delete_music(sqlite3 *db, int64_t mrid)
{
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, delete_content_sql, -1, &stmt, 0) != SQLITE_OK)
        return;
    sqlite3_bind_int64(stmt, 1, mrid);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static char *concat_path(const char *parent, const char *child)
{
    int   len  = strlen(parent) + strlen(child) + 2;
    char *p    = malloc(len);
    if (p) { strcpy(p, parent); strcat(p, "/"); strcat(p, child); }
    return p;
}

/* ── add_music_int: recursive scan, insert new MP3s ─────────────────────── */

static int add_music_int(sqlite3 *db, const char *dir, int added)
{
    SceUID did = sceIoDopen(dir);
    if (did < 0)
        return added;

    SceIoDirent dinfo;
    int err = sceIoDread(did, &dinfo);
    while (err > 0) {
        char *new_path = concat_path(dir, dinfo.d_name);
        if (new_path) {
            if (SCE_S_ISDIR(dinfo.d_stat.st_mode)) {
                added = add_music_int(db, new_path, added);
            } else {
                int l = strlen(dinfo.d_name);
                if (l > 4 && dinfo.d_name[0] != '.' &&
                    strcmp(dinfo.d_name + l - 4, ".mp3") == 0) {
                    int c = sql_get_count(db, select_content_count_sql, new_path);
                    if (c == 0) {
                        sql_insert_music(db, new_path, dinfo.d_stat.st_size);
                        added++;
                    }
                }
            }
            free(new_path);
        }
        memset(&dinfo, 0, sizeof(dinfo));
        err = sceIoDread(did, &dinfo);
    }

    sceIoDclose(did);
    return added;
}

/* ── public functions ────────────────────────────────────────────────────── */

/* Empty the entire music table — exact copy of MediaImporter empty_music() */
static void empty_music(void)
{
    sqlite3 *db;
    if (sqlite3_open(MUSIC_DB, &db) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, delete_all_sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

/* Add new tracks — exact copy of MediaImporter add_music() */
static int add_music(const char *dir)
{
    sqlite3 *db;
    if (sqlite3_open(MUSIC_DB, &db) != SQLITE_OK) {
        sqlite3_close(db);
        return 0;
    }
    sqlite3_exec(db, "BEGIN", 0, 0, 0);
    int added = add_music_int(db, dir, 0);
    sqlite3_exec(db, "COMMIT", 0, 0, 0);
    sqlite3_close(db);
    return added;
}

/* Remove DB entries whose files no longer exist — exact copy of
 * MediaImporter clean_music()                                               */
static int clean_music(void)
{
    int removed = 0;
    sqlite3 *db;
    if (sqlite3_open(MUSIC_DB, &db) != SQLITE_OK) {
        sqlite3_close(db);
        return 0;
    }

    sqlite3_exec(db, "BEGIN", 0, 0, 0);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, select_content_sql, -1, &stmt, 0) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t     mrid  = sqlite3_column_int64(stmt, 0);
            const char *path  = (const char *)sqlite3_column_text(stmt, 1);
            FILE       *testf = path ? fopen(path, "rb") : NULL;
            if (!testf) {
                sql_delete_music(db, mrid);
                removed++;
            } else {
                fclose(testf);
            }
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_exec(db, "COMMIT", 0, 0, 0);
    sqlite3_close(db);
    return removed;
}

/* Signal the system to refresh its music library — exact copy of
 * MediaImporter refresh_music_db()                                          */
static void refresh_music_db(void)
{
    sqlite3 *db;
    if (sqlite3_open(MUSIC_DB, &db) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, refresh_db_sql, -1, &stmt, 0) == SQLITE_OK) {
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

/* ── media_db_rebuild: empty → add → clean → refresh ────────────────────── */

void media_db_rebuild(void)
{
    /* 1. Empty (Triangle in MediaImporter) */
    empty_music();

    /* 2. Update (Cross in MediaImporter): add new + remove stale + refresh */
    int madd = add_music(MUSIC_DIR);
    int mrem = clean_music();
    if (madd > 0 || mrem > 0)
        refresh_music_db();
}
