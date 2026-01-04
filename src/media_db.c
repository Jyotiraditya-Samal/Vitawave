/*
 * media_db.c - rebuild system music database on startup
 * so songs appear in Vita's built-in Music app
 */
#include <stdio.h>
#include <string.h>
#include <psp2/appmgr.h>
#include "media_db.h"

/* Stub: full implementation requires SQLite VFS */
void media_db_rebuild(const char *music_root)
{
    (void)music_root;
    /* TODO: implement with SQLite to rebuild ux0:mms/music/AVContent.db */
}
