#ifndef METADATA_H
#define METADATA_H

#include <stdint.h>
#include <stdbool.h>
#include <vita2d.h>

typedef struct {
    uint8_t *data;
    uint32_t size;
    bool     loaded;
} AlbumArt;

typedef struct {
    char     title[256];
    char     artist[256];
    char     album[256];
    uint64_t duration_ms;
    AlbumArt album_art;
} TrackMetadata;

int               metadata_load(TrackMetadata *meta, const char *filepath);
void              metadata_free(TrackMetadata *meta);
vita2d_texture   *metadata_get_album_art_texture(TrackMetadata *meta);

#endif
