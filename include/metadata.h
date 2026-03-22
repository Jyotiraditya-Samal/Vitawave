#ifndef METADATA_H
#define METADATA_H

#include <stdint.h>
#include <stdbool.h>
#include <vita2d.h>

/* ── Limits ───────────────────────────────────────────────────────────────── */
#define MAX_TAG_LEN         256
#define MAX_ALBUM_ART_SIZE  (512 * 512 * 4)   /* RGBA at 512×512 */
#define THUMB_WIDTH         128
#define THUMB_HEIGHT        128

/* ── Album-art container ──────────────────────────────────────────────────── */
typedef struct {
    uint8_t         *data;      /* raw compressed (JPEG/PNG) bytes */
    uint32_t         width;
    uint32_t         height;
    uint32_t         size;      /* byte count of data */
    vita2d_texture  *texture;   /* decoded GPU texture (may be NULL) */
} AlbumArt;

/* ── Per-track metadata ───────────────────────────────────────────────────── */
typedef struct {
    char        title[MAX_TAG_LEN];
    char        artist[MAX_TAG_LEN];
    char        album[MAX_TAG_LEN];
    char        genre[MAX_TAG_LEN];
    char        year[8];
    int         track_number;
    uint64_t    duration_ms;
    uint32_t    bitrate;        /* kbps */
    uint32_t    sample_rate;    /* Hz */
    uint32_t    channels;
    bool        has_album_art;
    AlbumArt    album_art;
    char        filepath[512];
} TrackMetadata;

/* ── Public API ───────────────────────────────────────────────────────────── */

/**
 * Load all available metadata for filepath into meta.
 * Dispatches to the appropriate format-specific extractor.
 * Returns 0 on success, negative on error.
 */
int metadata_load(TrackMetadata *meta, const char *filepath);

/**
 * Load only title/artist/duration tags (no album art).
 * Faster than metadata_load; suitable for populating queue entries.
 * Returns 0 on success, negative on error.
 */
int metadata_load_tags(TrackMetadata *meta, const char *filepath);

/**
 * Free all heap-allocated fields inside meta (album art data, texture).
 */
void metadata_free(TrackMetadata *meta);

/**
 * Ensure meta->album_art.texture is a valid vita2d_texture.
 * Decodes the raw JPEG or PNG bytes stored in album_art.data if needed.
 * Returns the texture pointer, or NULL if no art is available.
 */
vita2d_texture *metadata_get_album_art_texture(TrackMetadata *meta);

/**
 * Format duration_ms as "MM:SS" into the buf (must be ≥ 8 bytes).
 */
void metadata_format_duration(uint64_t duration_ms, char *buf, int buf_size);

/**
 * Extract ID3v1/ID3v2 tags from an MP3/AAC file.
 * Returns 0 on success, negative on error.
 */
int metadata_extract_id3(TrackMetadata *meta, const char *filepath);

/**
 * Extract Vorbis comments and PICTURE block from a FLAC file.
 * Returns 0 on success, negative on error.
 */
int metadata_extract_flac(TrackMetadata *meta, const char *filepath);

/**
 * Extract Vorbis comments from an OGG/Vorbis file.
 * Returns 0 on success, negative on error.
 */
int metadata_extract_ogg(TrackMetadata *meta, const char *filepath);

/**
 * Free the album-art fields inside meta (data pointer and texture).
 */
void metadata_free_album_art(TrackMetadata *meta);

#endif /* METADATA_H */
