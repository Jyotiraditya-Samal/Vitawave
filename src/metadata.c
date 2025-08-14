#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>
#include "metadata.h"

static void read_id3v2(TrackMetadata *meta, FILE *f)
{
    unsigned char hdr[10];
    if (fread(hdr, 1, 10, f) != 10) return;
    if (memcmp(hdr, "ID3", 3) != 0) return;

    uint32_t tag_size = ((hdr[6] & 0x7f) << 21) | ((hdr[7] & 0x7f) << 14) |
                        ((hdr[8] & 0x7f) << 7)  |  (hdr[9] & 0x7f);
    long end = 10 + (long)tag_size;

    while (ftell(f) < end - 10) {
        unsigned char frame[10];
        if (fread(frame, 1, 10, f) != 10) break;
        if (frame[0] == 0) break;

        uint32_t fsz = ((uint32_t)frame[4] << 24) | ((uint32_t)frame[5] << 16) |
                       ((uint32_t)frame[6] << 8)  |  (uint32_t)frame[7];
        if (fsz == 0 || fsz > 512 * 1024) break;

        char *buf = (char *)malloc(fsz + 1);
        if (!buf) break;
        if (fread(buf, 1, fsz, f) != fsz) { free(buf); break; }
        buf[fsz] = '\0';

        char *text = buf + 1;
        size_t tlen = fsz > 1 ? fsz - 1 : 0;

        if (memcmp(frame, "TIT2", 4) == 0 && meta->title[0] == '\0')
            strncpy(meta->title, text, tlen < 255 ? tlen : 255);
        else if (memcmp(frame, "TPE1", 4) == 0 && meta->artist[0] == '\0')
            strncpy(meta->artist, text, tlen < 255 ? tlen : 255);
        else if (memcmp(frame, "TALB", 4) == 0 && meta->album[0] == '\0')
            strncpy(meta->album, text, tlen < 255 ? tlen : 255);
        else if (memcmp(frame, "APIC", 4) == 0 && !meta->album_art.loaded) {
            /* skip encoding byte + mime type + null + pic type + desc + null */
            size_t off = 1;
            while (off < fsz && buf[off]) off++; /* skip mime */
            off++; /* null */
            if (off < fsz) off++; /* pic type */
            while (off < fsz && buf[off]) off++; /* skip desc */
            off++; /* null */
            if (off < fsz) {
                size_t art_size = fsz - off;
                meta->album_art.data = (uint8_t *)malloc(art_size);
                if (meta->album_art.data) {
                    memcpy(meta->album_art.data, buf + off, art_size);
                    meta->album_art.size   = (uint32_t)art_size;
                    meta->album_art.loaded = true;
                }
            }
        }
        free(buf);
    }
}

int metadata_load(TrackMetadata *meta, const char *filepath)
{
    if (!meta || !filepath) return -1;
    memset(meta, 0, sizeof(*meta));

    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;
    read_id3v2(meta, f);
    fclose(f);

    /* fallback: use filename as title */
    if (meta->title[0] == '\0') {
        const char *slash = strrchr(filepath, '/');
        strncpy(meta->title, slash ? slash + 1 : filepath, 255);
    }

    /* try folder cover art if no embedded art.
     * vita FAT32 fs is case-insensitive so cover.jpg matches Cover.JPG etc. */
    if (!meta->album_art.loaded) {
        char cover[512];
        const char *slash = strrchr(filepath, '/');
        size_t dir_len = slash ? (size_t)(slash - filepath + 1) : 0;
        const char *covers[] = { "cover.jpg", "cover.jpeg", "cover.png", "folder.jpg" };
        for (int i = 0; i < 4 && !meta->album_art.loaded; i++) {
            snprintf(cover, sizeof(cover), "%.*s%s", (int)dir_len, filepath, covers[i]);
            FILE *cf = fopen(cover, "rb");
            if (!cf) continue;
            fseek(cf, 0, SEEK_END);
            long sz = ftell(cf);
            rewind(cf);
            if (sz > 0) {
                meta->album_art.data = (uint8_t *)malloc((size_t)sz);
                if (meta->album_art.data) {
                    if (fread(meta->album_art.data, 1, (size_t)sz, cf) == (size_t)sz) {
                        meta->album_art.size   = (uint32_t)sz;
                        meta->album_art.loaded = true;
                    } else {
                        free(meta->album_art.data);
                        meta->album_art.data = NULL;
                    }
                }
            }
            fclose(cf);
        }
    }
    return 0;
}

void metadata_free(TrackMetadata *meta)
{
    if (!meta) return;
    free(meta->album_art.data);
    meta->album_art.data   = NULL;
    meta->album_art.loaded = false;
}

vita2d_texture *metadata_get_album_art_texture(TrackMetadata *meta)
{
    if (!meta || !meta->album_art.loaded || !meta->album_art.data) return NULL;
    return vita2d_load_PNG_buffer(meta->album_art.data);
}
