#ifndef MEDIA_DB_H
#define MEDIA_DB_H

/*
 * Flush ux0:mms/music/AVContent.db and rebuild it from ux0:/music/.
 * Calls sceAppUtilMusicMount/Umount around the operation.
 * Runs synchronously — call before the main loop.
 */
void media_db_rebuild(void);

#endif /* MEDIA_DB_H */
