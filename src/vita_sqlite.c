/*
 * VitaWave — vita_sqlite.c
 * SQLite VFS for PS Vita using native sceIo* calls.
 * Adapted from MediaImporter by cnsldv (MIT licence).
 * Only tested against the SQLite API subset used by media_db.c.
 */

#include "sqlite3.h"

#include <stdio.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/rtc.h>

typedef struct VitaFile {
    sqlite3_file base;
    unsigned     fd;
} VitaFile;

/* ── File methods ────────────────────────────────────────────────────────── */

static int vita_xClose(sqlite3_file *pFile)
{
    VitaFile *p = (VitaFile *)pFile;
    sceIoClose(p->fd);
    return SQLITE_OK;
}

static int vita_xRead(sqlite3_file *pFile, void *zBuf, int iAmt,
                      sqlite_int64 iOfst)
{
    VitaFile *p = (VitaFile *)pFile;
    memset(zBuf, 0, iAmt);
    sceIoLseek(p->fd, iOfst, SCE_SEEK_SET);
    int n = sceIoRead(p->fd, zBuf, iAmt);
    if (n == iAmt)  return SQLITE_OK;
    if (n >= 0)     return SQLITE_IOERR_SHORT_READ;
    return SQLITE_IOERR_READ;
}

static int vita_xWrite(sqlite3_file *pFile, const void *zBuf, int iAmt,
                       sqlite_int64 iOfst)
{
    VitaFile *p = (VitaFile *)pFile;
    int ofst = sceIoLseek(p->fd, iOfst, SCE_SEEK_SET);
    if (ofst != iOfst) return SQLITE_IOERR_WRITE;
    int n = sceIoWrite(p->fd, zBuf, iAmt);
    if (n != iAmt) return SQLITE_IOERR_WRITE;
    return SQLITE_OK;
}

static int vita_xTruncate(sqlite3_file *pFile, sqlite_int64 size)
{
    (void)pFile; (void)size;
    return SQLITE_OK;
}

static int vita_xSync(sqlite3_file *pFile, int flags)
{
    (void)pFile; (void)flags;
    return SQLITE_OK;
}

static int vita_xFileSize(sqlite3_file *pFile, sqlite_int64 *pSize)
{
    VitaFile *p = (VitaFile *)pFile;
    SceIoStat st = {0};
    sceIoGetstatByFd(p->fd, &st);
    *pSize = st.st_size;
    return SQLITE_OK;
}

static int vita_xLock(sqlite3_file *pFile, int eLock)
{
    (void)pFile; (void)eLock;
    return SQLITE_OK;
}

static int vita_xUnlock(sqlite3_file *pFile, int eLock)
{
    (void)pFile; (void)eLock;
    return SQLITE_OK;
}

static int vita_xCheckReservedLock(sqlite3_file *pFile, int *pResOut)
{
    (void)pFile;
    *pResOut = 0;
    return SQLITE_OK;
}

static int vita_xFileControl(sqlite3_file *pFile, int op, void *pArg)
{
    (void)pFile; (void)op; (void)pArg;
    return SQLITE_NOTFOUND;
}

static int vita_xSectorSize(sqlite3_file *pFile)
{
    (void)pFile;
    return 512;
}

static int vita_xDeviceCharacteristics(sqlite3_file *pFile)
{
    (void)pFile;
    return 0;
}

/* ── VFS methods ─────────────────────────────────────────────────────────── */

static int vita_xOpen(sqlite3_vfs *vfs, const char *name, sqlite3_file *file,
                      int flags, int *outFlags)
{
    static const sqlite3_io_methods vitaio = {
        1,
        vita_xClose,
        vita_xRead,
        vita_xWrite,
        vita_xTruncate,
        vita_xSync,
        vita_xFileSize,
        vita_xLock,
        vita_xUnlock,
        vita_xCheckReservedLock,
        vita_xFileControl,
        vita_xSectorSize,
        vita_xDeviceCharacteristics,
    };

    (void)vfs;
    VitaFile *p = (VitaFile *)file;
    unsigned oflags = 0;

    if (flags & SQLITE_OPEN_EXCLUSIVE)   oflags |= SCE_O_EXCL;
    if (flags & SQLITE_OPEN_CREATE)      oflags |= SCE_O_CREAT;
    if (flags & SQLITE_OPEN_READONLY)    oflags |= SCE_O_RDONLY;
    if (flags & SQLITE_OPEN_READWRITE)   oflags |= SCE_O_RDWR;
    /* SQLite opens the journal even when it doesn't exist yet */
    if ((flags & SQLITE_OPEN_MAIN_JOURNAL) && !(flags & SQLITE_OPEN_EXCLUSIVE))
        oflags |= SCE_O_CREAT;

    memset(p, 0, sizeof(*p));
    p->fd = sceIoOpen(name, oflags, 0666);
    if ((int)p->fd < 0) return SQLITE_CANTOPEN;

    if (outFlags) *outFlags = flags;
    p->base.pMethods = &vitaio;
    return SQLITE_OK;
}

static int vita_xDelete(sqlite3_vfs *vfs, const char *name, int syncDir)
{
    (void)vfs; (void)syncDir;
    return (sceIoRemove(name) >= 0) ? SQLITE_OK : SQLITE_IOERR_DELETE;
}

static int vita_xAccess(sqlite3_vfs *vfs, const char *name, int flags,
                        int *pResOut)
{
    (void)vfs; (void)flags;
    SceIoStat st;
    *pResOut = (sceIoGetstat(name, &st) >= 0) ? 1 : 0;
    return SQLITE_OK;
}

static int vita_xFullPathname(sqlite3_vfs *vfs, const char *zName,
                              int nOut, char *zOut)
{
    (void)vfs;
    snprintf(zOut, nOut, "%s", zName);
    return SQLITE_OK;
}

static void *vita_xDlOpen(sqlite3_vfs *vfs, const char *z)
{
    (void)vfs; (void)z;
    return NULL;
}
static void vita_xDlError(sqlite3_vfs *vfs, int n, char *z)
{
    (void)vfs; (void)n; (void)z;
}
static void (*vita_xDlSym(sqlite3_vfs *vfs, void *p, const char *z))(void)
{
    (void)vfs; (void)p; (void)z;
    return NULL;
}
static void vita_xDlClose(sqlite3_vfs *vfs, void *p)
{
    (void)vfs; (void)p;
}

static int vita_xRandomness(sqlite3_vfs *vfs, int nByte, char *zOut)
{
    (void)vfs; (void)nByte; (void)zOut;
    return SQLITE_OK;
}

static int vita_xSleep(sqlite3_vfs *vfs, int us)
{
    (void)vfs;
    sceKernelDelayThread(us);
    return SQLITE_OK;
}

static int vita_xCurrentTime(sqlite3_vfs *vfs, double *pTime)
{
    (void)vfs;
    SceDateTime dt = {0};
    sceRtcGetCurrentClock(&dt, 0);
    time_t t = 0;
    sceRtcGetTime_t(&dt, &t);
    *pTime = t / 86400.0 + 2440587.5;
    return SQLITE_OK;
}

static int vita_xGetLastError(sqlite3_vfs *vfs, int e, char *err)
{
    (void)vfs; (void)e; (void)err;
    return 0;
}

static sqlite3_vfs vita_vfs = {
    .iVersion          = 1,
    .szOsFile          = sizeof(VitaFile),
    .mxPathname        = 256,
    .pNext             = NULL,
    .zName             = "psp2",
    .pAppData          = NULL,
    .xOpen             = vita_xOpen,
    .xDelete           = vita_xDelete,
    .xAccess           = vita_xAccess,
    .xFullPathname     = vita_xFullPathname,
    .xDlOpen           = vita_xDlOpen,
    .xDlError          = vita_xDlError,
    .xDlSym            = vita_xDlSym,
    .xDlClose          = vita_xDlClose,
    .xRandomness       = vita_xRandomness,
    .xSleep            = vita_xSleep,
    .xCurrentTime      = vita_xCurrentTime,
    .xGetLastError     = vita_xGetLastError,
};

/* Called automatically by sqlite3_initialize() when SQLITE_OS_OTHER=1 */
int sqlite3_os_init(void)
{
    sqlite3_vfs_register(&vita_vfs, 1);
    return SQLITE_OK;
}

int sqlite3_os_end(void)
{
    return SQLITE_OK;
}
