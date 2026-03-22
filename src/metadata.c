/*
 * VitaWave – metadata.c
 * Tag extraction: built-in ID3v2 parser for MP3,
 * libFLAC metadata API for FLAC, libvorbis comments for OGG.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* dr_flac replaces libFLAC for FLAC metadata (DR_FLAC_IMPLEMENTATION is in
 * decoder_flac.c; here we only need the extern declarations).             */
#include "dr_flac.h"
#include <vorbis/vorbisfile.h>
#include <vita2d.h>

#include "metadata.h"
#include "file_browser.h"

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void safe_copy(char *dst, const char *src, size_t dst_size)
{
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/* Convert synchsafe integer (ID3v2) to normal uint32 */
static uint32_t synchsafe_to_int(const uint8_t *b)
{
    return ((uint32_t)(b[0] & 0x7F) << 21) |
           ((uint32_t)(b[1] & 0x7F) << 14) |
           ((uint32_t)(b[2] & 0x7F) <<  7) |
            (uint32_t)(b[3] & 0x7F);
}

/* Strip leading/trailing whitespace in-place */
static void trim(char *s)
{
    if (!s || !*s) return;
    /* leading */
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    /* trailing */
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' ||
                     s[n-1] == '\r' || s[n-1] == '\n')) {
        s[--n] = '\0';
    }
}

/*
 * Decode a text frame byte-string into dst.
 * encoding byte: 0=Latin-1, 1=UTF-16 BOM, 2=UTF-16BE, 3=UTF-8
 */
static void decode_text_frame(const uint8_t *data, uint32_t size,
                               char *dst, size_t dst_size)
{
    if (size == 0) { dst[0] = '\0'; return; }

    uint8_t enc = data[0];
    const uint8_t *txt = data + 1;
    uint32_t txt_len   = size - 1;

    if (enc == 3) {
        /* UTF-8: copy directly */
        size_t copy = txt_len < dst_size - 1 ? txt_len : dst_size - 1;
        memcpy(dst, txt, copy);
        dst[copy] = '\0';
    } else if (enc == 0) {
        /* Latin-1: re-encode each byte as UTF-8 */
        size_t out = 0;
        for (uint32_t i = 0; i < txt_len && out < dst_size - 1; i++) {
            uint8_t b = txt[i];
            if (b < 0x80) {
                dst[out++] = (char)b;
            } else {
                /* Latin-1 U+0080–U+00FF → 2-byte UTF-8 */
                if (out + 2 > dst_size - 1) break;
                dst[out++] = (char)(0xC0 | (b >> 6));
                dst[out++] = (char)(0x80 | (b & 0x3F));
            }
        }
        dst[out] = '\0';
    } else {
        /* UTF-16: skip BOM if present, extract every other byte (ASCII range) */
        const uint8_t *p = txt;
        uint32_t remaining = txt_len;

        /* skip BOM */
        if (remaining >= 2 &&
            ((p[0] == 0xFF && p[1] == 0xFE) || (p[0] == 0xFE && p[1] == 0xFF))) {
            p += 2; remaining -= 2;
        }

        size_t out = 0;
        while (remaining >= 2 && out < dst_size - 1) {
            uint16_t cp;
            /* assume LE (most common) */
            cp = (uint16_t)(p[0] | (p[1] << 8));
            p += 2; remaining -= 2;
            if (cp == 0) break;

            /* Encode codepoint as UTF-8 */
            if (cp < 0x80) {
                if (out + 1 > dst_size - 1) break;
                dst[out++] = (char)cp;
            } else if (cp < 0x800) {
                if (out + 2 > dst_size - 1) break;
                dst[out++] = (char)(0xC0 | (cp >> 6));
                dst[out++] = (char)(0x80 | (cp & 0x3F));
            } else {
                /* Handles U+0800–U+FFFF (covers all BMP: CJK, curly quotes, etc.) */
                if (out + 3 > dst_size - 1) break;
                dst[out++] = (char)(0xE0 | (cp >> 12));
                dst[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                dst[out++] = (char)(0x80 | (cp & 0x3F));
            }
        }
        dst[out] = '\0';
    }
    trim(dst);
}

/* ── metadata_extract_id3 (pure C ID3v2 parser) ─────────────────────────── */

int metadata_extract_id3(TrackMetadata *meta, const char *filepath)
{
    if (!meta || !filepath) return -1;

    FILE *f = fopen(filepath, "rb");
    if (!f) return -2;

    /* ── ID3v2 header: 10 bytes ── */
    uint8_t hdr[10];
    if (fread(hdr, 1, 10, f) != 10 ||
        hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') {
        fclose(f);
        return -3; /* No ID3v2 tag */
    }

    /* uint8_t ver_major = hdr[3]; */
    /* uint8_t flags     = hdr[5]; */
    uint32_t tag_size  = synchsafe_to_int(hdr + 6);

    /* Read entire tag into memory */
    uint8_t *tag_buf = malloc(tag_size);
    if (!tag_buf) { fclose(f); return -4; }
    if (fread(tag_buf, 1, tag_size, f) != tag_size) {
        free(tag_buf); fclose(f); return -5;
    }
    fclose(f);

    /* ── Walk frames ── */
    uint32_t pos = 0;
    while (pos + 10 <= tag_size) {
        const uint8_t *fhdr = tag_buf + pos;

        /* Frame ID: 4 ASCII bytes; if first byte is 0 we've hit padding */
        if (fhdr[0] == 0) break;

        char frame_id[5];
        memcpy(frame_id, fhdr, 4);
        frame_id[4] = '\0';

        /* Frame size: plain int32 in v2.3, synchsafe in v2.4 – use plain */
        uint32_t fsize = ((uint32_t)fhdr[4] << 24) |
                         ((uint32_t)fhdr[5] << 16) |
                         ((uint32_t)fhdr[6] <<  8) |
                          (uint32_t)fhdr[7];

        /* uint16_t fflags = (fhdr[8] << 8) | fhdr[9]; */
        pos += 10;

        if (fsize == 0 || pos + fsize > tag_size) break;

        const uint8_t *fdata = tag_buf + pos;

        if      (strcmp(frame_id, "TIT2") == 0)
            decode_text_frame(fdata, fsize, meta->title,  MAX_TAG_LEN);
        else if (strcmp(frame_id, "TPE1") == 0)
            decode_text_frame(fdata, fsize, meta->artist, MAX_TAG_LEN);
        else if (strcmp(frame_id, "TALB") == 0)
            decode_text_frame(fdata, fsize, meta->album,  MAX_TAG_LEN);
        else if (strcmp(frame_id, "TCON") == 0)
            decode_text_frame(fdata, fsize, meta->genre,  MAX_TAG_LEN);
        else if (strcmp(frame_id, "TDRC") == 0 || strcmp(frame_id, "TYER") == 0)
            decode_text_frame(fdata, fsize, meta->year,   8);
        else if (strcmp(frame_id, "TRCK") == 0) {
            char tmp[16]; tmp[0] = '\0';
            decode_text_frame(fdata, fsize, tmp, sizeof(tmp));
            meta->track_number = atoi(tmp);
        }
        else if (strcmp(frame_id, "APIC") == 0 && !meta->has_album_art) {
            /*
             * APIC layout: encoding(1) | mime(N) | NUL | pic_type(1) |
             *              description(M) | NUL | picture_data
             */
            if (fsize < 4) { pos += fsize; continue; }
            const uint8_t *p = fdata + 1; /* skip encoding */
            uint32_t rem    = fsize - 1;

            /* skip MIME type */
            const uint8_t *mime_end = memchr(p, 0, rem);
            if (!mime_end) { pos += fsize; continue; }
            rem -= (uint32_t)(mime_end - p + 1);
            p    = mime_end + 1;

            /* skip picture type byte */
            if (rem < 1) { pos += fsize; continue; }
            p++; rem--;

            /* skip description (may be empty: just the NUL) */
            const uint8_t *desc_end = memchr(p, 0, rem);
            if (!desc_end) { pos += fsize; continue; }
            rem -= (uint32_t)(desc_end - p + 1);
            p    = desc_end + 1;

            if (rem > 0 && rem <= MAX_ALBUM_ART_SIZE) {
                meta->album_art.data = malloc(rem);
                if (meta->album_art.data) {
                    memcpy(meta->album_art.data, p, rem);
                    meta->album_art.size = rem;
                    meta->has_album_art  = true;
                }
            }
        }

        pos += fsize;
    }

    free(tag_buf);

    /* Default title from filename */
    if (meta->title[0] == '\0') {
        const char *fname = strrchr(filepath, '/');
        safe_copy(meta->title, fname ? fname + 1 : filepath, MAX_TAG_LEN);
    }

    return 0;
}

/* ── metadata_extract_flac ───────────────────────────────────────────────── */
/*
 * Callback context for drflac_open_file_with_metadata.
 * We extract vorbis comments and the first front-cover picture in one pass.
 */
typedef struct {
    TrackMetadata *meta;
    bool           got_art;
} FlacMetaCtx;

static void flac_meta_cb(void *pUserData, drflac_metadata *pBlock)
{
    FlacMetaCtx   *ctx  = (FlacMetaCtx *)pUserData;
    TrackMetadata *meta = ctx->meta;

    if (pBlock->type == DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) {
        drflac_vorbis_comment_iterator it;
        drflac_init_vorbis_comment_iterator(&it,
            pBlock->data.vorbis_comment.commentCount,
            pBlock->data.vorbis_comment.pComments);

        drflac_uint32  len;
        const char    *raw;
        while ((raw = drflac_next_vorbis_comment(&it, &len)) != NULL) {
            /* Comments are NOT null-terminated; copy to a local buffer. */
            char buf[512];
            if (len == 0 || len >= sizeof(buf)) continue;
            memcpy(buf, raw, len);
            buf[len] = '\0';

            if      (strncasecmp(buf, "TITLE=",       6)  == 0) safe_copy(meta->title,  buf + 6,  MAX_TAG_LEN);
            else if (strncasecmp(buf, "ARTIST=",      7)  == 0) safe_copy(meta->artist, buf + 7,  MAX_TAG_LEN);
            else if (strncasecmp(buf, "ALBUM=",       6)  == 0) safe_copy(meta->album,  buf + 6,  MAX_TAG_LEN);
            else if (strncasecmp(buf, "GENRE=",       6)  == 0) safe_copy(meta->genre,  buf + 6,  MAX_TAG_LEN);
            else if (strncasecmp(buf, "DATE=",        5)  == 0) safe_copy(meta->year,   buf + 5,  8);
            else if (strncasecmp(buf, "TRACKNUMBER=", 12) == 0) meta->track_number = atoi(buf + 12);
        }
    }

    if (pBlock->type == DRFLAC_METADATA_BLOCK_TYPE_PICTURE && !ctx->got_art) {
        /* type 3 = front cover */
        if (pBlock->data.picture.type == 3 &&
            pBlock->data.picture.pPictureData != NULL &&
            pBlock->data.picture.pictureDataSize > 0 &&
            pBlock->data.picture.pictureDataSize <= MAX_ALBUM_ART_SIZE) {

            meta->album_art.data = malloc(pBlock->data.picture.pictureDataSize);
            if (meta->album_art.data) {
                memcpy(meta->album_art.data,
                       pBlock->data.picture.pPictureData,
                       pBlock->data.picture.pictureDataSize);
                meta->album_art.size = pBlock->data.picture.pictureDataSize;
                meta->has_album_art  = true;
                ctx->got_art = true;
            }
        }
    }
}

int metadata_extract_flac(TrackMetadata *meta, const char *filepath)
{
    if (!meta || !filepath) return -1;

    FlacMetaCtx ctx = { meta, false };

    /* drflac_open_file_with_metadata fires flac_meta_cb for every metadata
     * block (VORBIS_COMMENT, PICTURE, …) before returning the decoder.
     * All libFLAC metadata calls are gone — dr_flac is already compiled in
     * decoder_flac.c (DR_FLAC_IMPLEMENTATION defined there).              */
    drflac *flac = drflac_open_file_with_metadata(filepath, flac_meta_cb, &ctx, NULL);
    if (!flac) {
        /* Couldn't open file; fall back to filename as title */
        const char *fname = strrchr(filepath, '/');
        safe_copy(meta->title, fname ? fname + 1 : filepath, MAX_TAG_LEN);
        return -2;
    }

    /* Stream info comes from the drflac object itself */
    meta->sample_rate = flac->sampleRate;
    meta->channels    = flac->channels;
    if (flac->sampleRate > 0 && flac->totalPCMFrameCount > 0)
        meta->duration_ms =
            (uint64_t)flac->totalPCMFrameCount * 1000ULL / flac->sampleRate;

    drflac_close(flac);

    if (meta->title[0] == '\0') {
        const char *fname = strrchr(filepath, '/');
        safe_copy(meta->title, fname ? fname + 1 : filepath, MAX_TAG_LEN);
    }
    return 0;
}

/* ── metadata_extract_ogg ────────────────────────────────────────────────── */

int metadata_extract_ogg(TrackMetadata *meta, const char *filepath)
{
    if (!meta || !filepath) return -1;

    OggVorbis_File ovf;
    if (ov_fopen(filepath, &ovf) != 0) return -2;

    double dur_s = ov_time_total(&ovf, -1);
    if (dur_s > 0.0) meta->duration_ms = (uint64_t)(dur_s * 1000.0);

    vorbis_info *vi = ov_info(&ovf, -1);
    if (vi) {
        meta->sample_rate = (uint32_t)vi->rate;
        meta->channels    = (uint32_t)vi->channels;
        meta->bitrate     = (uint32_t)(vi->bitrate_nominal / 1000);
    }

    vorbis_comment *vc = ov_comment(&ovf, -1);
    if (vc) {
        for (int i = 0; i < vc->comments; i++) {
            const char *e = vc->user_comments[i];
            if (!e) continue;
#define OMATCH(tag, field, len) \
            if (strncasecmp(e, tag "=", sizeof(tag)) == 0) \
                safe_copy(meta->field, e + sizeof(tag), len)
            OMATCH("TITLE",  title,  MAX_TAG_LEN);
            else OMATCH("ARTIST", artist, MAX_TAG_LEN);
            else OMATCH("ALBUM",  album,  MAX_TAG_LEN);
            else OMATCH("GENRE",  genre,  MAX_TAG_LEN);
            else OMATCH("DATE",   year,   8);
            else if (strncasecmp(e, "TRACKNUMBER=", 12) == 0)
                meta->track_number = atoi(e + 12);
#undef OMATCH
        }
    }

    ov_clear(&ovf);

    if (meta->title[0] == '\0') {
        const char *fname = strrchr(filepath, '/');
        safe_copy(meta->title, fname ? fname + 1 : filepath, MAX_TAG_LEN);
    }
    return 0;
}

/* ── metadata_load_tags ──────────────────────────────────────────────────── */

int metadata_load_tags(TrackMetadata *meta, const char *filepath)
{
    if (!meta || !filepath) return -1;
    memset(meta, 0, sizeof(*meta));
    safe_copy(meta->filepath, filepath, sizeof(meta->filepath));

    const char *fname = strrchr(filepath, '/');
    safe_copy(meta->title, fname ? fname + 1 : filepath, MAX_TAG_LEN);

    int ret = 0;
    FileType type = file_browser_get_file_type(filepath);
    switch (type) {
        case FILE_TYPE_MP3:  ret = metadata_extract_id3 (meta, filepath); break;
        case FILE_TYPE_FLAC: ret = metadata_extract_flac(meta, filepath); break;
        case FILE_TYPE_OGG:  ret = metadata_extract_ogg (meta, filepath); break;
        default:             break;
    }

    /* Free album art — callers only need title/artist/duration */
    metadata_free_album_art(meta);

    return ret;
}

/* ── metadata_load ───────────────────────────────────────────────────────── */

int metadata_load(TrackMetadata *meta, const char *filepath)
{
    if (!meta || !filepath) return -1;
    memset(meta, 0, sizeof(*meta));
    safe_copy(meta->filepath, filepath, sizeof(meta->filepath));

    const char *fname = strrchr(filepath, '/');
    safe_copy(meta->title, fname ? fname + 1 : filepath, MAX_TAG_LEN);

    int ret = 0;
    FileType type = file_browser_get_file_type(filepath);
    switch (type) {
        case FILE_TYPE_MP3:  ret = metadata_extract_id3 (meta, filepath); break;
        case FILE_TYPE_FLAC: ret = metadata_extract_flac(meta, filepath); break;
        case FILE_TYPE_OGG:  ret = metadata_extract_ogg (meta, filepath); break;
        default:             break;
    }

    /* Folder cover art fallback: cover.jpg / cover.png in the same directory */
    if (!meta->has_album_art && fname) {
        size_t dir_len = (size_t)(fname - filepath);
        char cover_path[512];
        static const char *covers[] = { "cover.jpg", "cover.jpeg", "cover.png", NULL };
        for (int i = 0; covers[i] && !meta->has_album_art; i++) {
            if (dir_len + 1 + strlen(covers[i]) + 1 > sizeof(cover_path)) continue;
            memcpy(cover_path, filepath, dir_len);
            cover_path[dir_len] = '/';
            strcpy(cover_path + dir_len + 1, covers[i]);

            FILE *cf = fopen(cover_path, "rb");
            if (!cf) continue;
            fseek(cf, 0, SEEK_END);
            long fsz = ftell(cf);
            fseek(cf, 0, SEEK_SET);
            if (fsz > 0 && (uint32_t)fsz <= MAX_ALBUM_ART_SIZE) {
                meta->album_art.data = malloc((size_t)fsz);
                if (meta->album_art.data) {
                    if (fread(meta->album_art.data, 1, (size_t)fsz, cf) == (size_t)fsz) {
                        meta->album_art.size = (uint32_t)fsz;
                        meta->has_album_art  = true;
                    } else {
                        free(meta->album_art.data);
                        meta->album_art.data = NULL;
                    }
                }
            }
            fclose(cf);
        }
    }

    return ret;
}

/* ── metadata_free_album_art ─────────────────────────────────────────────── */

void metadata_free_album_art(TrackMetadata *meta)
{
    if (!meta) return;
    if (meta->album_art.texture) {
        vita2d_free_texture(meta->album_art.texture);
        meta->album_art.texture = NULL;
    }
    free(meta->album_art.data);
    meta->album_art.data   = NULL;
    meta->album_art.size   = 0;
    meta->album_art.width  = 0;
    meta->album_art.height = 0;
    meta->has_album_art    = false;
}

/* ── metadata_free ───────────────────────────────────────────────────────── */

void metadata_free(TrackMetadata *meta)
{
    if (!meta) return;
    metadata_free_album_art(meta);
    memset(meta, 0, sizeof(*meta));
}

/* ── metadata_get_album_art_texture ──────────────────────────────────────── */

vita2d_texture *metadata_get_album_art_texture(TrackMetadata *meta)
{
    if (!meta || !meta->has_album_art || !meta->album_art.data) return NULL;
    if (meta->album_art.texture) return meta->album_art.texture;

    const uint8_t *d  = meta->album_art.data;
    uint32_t       sz = meta->album_art.size;

    if (sz >= 2 && d[0] == 0xFF && d[1] == 0xD8)
        meta->album_art.texture = vita2d_load_JPEG_buffer(d, sz);
    else if (sz >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G')
        meta->album_art.texture = vita2d_load_PNG_buffer(d);

    return meta->album_art.texture;
}

/* ── metadata_format_duration ────────────────────────────────────────────── */

void metadata_format_duration(uint64_t duration_ms, char *buf, int buf_size)
{
    if (!buf || buf_size < 6) return;
    uint64_t total_s = duration_ms / 1000ULL;
    uint64_t minutes = total_s / 60ULL;
    uint64_t seconds = total_s % 60ULL;
    snprintf(buf, (size_t)buf_size, "%02llu:%02llu",
             (unsigned long long)minutes,
             (unsigned long long)seconds);
}
