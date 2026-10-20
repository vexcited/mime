/*
 * metafile internals: the metafile image object and what its three modules share. metafile.c loads, converts and
 * answers questions, emfplay.c plays and enumerates, emfrecord.c records.
 */
#ifndef MIME_GDIPLUS_META_PRIVATE_H
#define MIME_GDIPLUS_META_PRIVATE_H

#include "gp_private.h"

/* the life of a metafile: recording ends in the ready state, a playback in progress is the playing state. */
enum {
    GM_INVALID = 0,
    GM_CORRUPT = 1,
    GM_RECORDING = 2,
    GM_READY = 3,
    GM_PLAYING = 5,
};

/* the version every emf+ record stream starts with, and the identifier of the comment that carries it. */
#define EMFPLUS_VERSION 0xdbc01002u
#define EMFPLUS_VERSION_MASK 0xfffff000u
#define EMFPLUS_VERSION_BASE 0xdbc01000u
#define EMFPLUS_SIGNATURE 0x2b464d45u /* 'EMF+' */
#define EMFPLUS_HEADER_RECORD 28      /* the size of the header record: 12 and 16 bytes of data */

#define PLACEABLE_KEY 0x9ac6cdd7u
#define PLACEABLE_SIZE 22
#define WMF_HEADER_SIZE 18
#define EMF_HEADER3_SIZE 88

/* the size in 0.01 mm of one inch. */
#define HIMETRIC_PER_INCH 2540.0f

struct GRecorder;
struct GPlayer;

typedef struct GMetafile {
    GImage image;
    INT state;
    MetafileHeader header;
    /* an enhanced metafile, or for the two windows metafile types a windows metafile */
    HANDLE handle;
    BOOL owns_handle;
    /* the bytes of the enhanced metafile for playing, read from the handle when first needed */
    BYTE *bytes;
    DWORD size;
    /* playing */
    DWORD play_thread;
    struct GPlayer *player;
    /* recording */
    struct GRecorder *rec;
    GGraphics *graphics;
    BOOL graphics_taken;
    WCHAR *file_name;
    IStream *stream;
    INT compat;
} GMetafile;

/* ------------------------------------------------------------------ metafile.c */

GMetafile *gp_meta_cast(const void *p);
GMetafile *gp_meta_new(void);
void gp_meta_free(GMetafile *m);
/* the enhanced metafile bytes, NULL when they cannot be read. valid until the metafile is freed. */
const BYTE *gp_meta_bytes(GMetafile *m, DWORD *size);
/* the header of an enhanced metafile as the sdk reports it. InvalidParameter when the header is no good. */
GpStatus gp_meta_header_of_emf(HENHMETAFILE hemf, MetafileHeader *out, BOOL *damaged);
/* sets the object up for an enhanced metafile, the handle is kept and deleted with it when owned. */
GpStatus gp_meta_init_emf(GMetafile *m, HENHMETAFILE hemf, BOOL owned);
/* the same for a windows metafile with or without a placeable header. */
GpStatus gp_meta_init_wmf(GMetafile *m, HMETAFILE wmf, const WmfPlaceableFileHeader *placeable, BOOL owned);
/* decodes the bytes of a file: an enhanced metafile, a placeable windows metafile or a plain one. */
GpStatus gp_meta_from_memory(const BYTE *data, SIZE_T size, INT compat, GMetafile **out);
/* the header of a metafile that is read from the bytes, without making an object. */
GpStatus gp_meta_header_of_bytes(const BYTE *data, SIZE_T size, MetafileHeader *out);
/* the frame of the metafile in pixels of its own resolution, as GdipGetImageBounds has it. */
void gp_meta_real_bounds(const GMetafile *m, float *x, float *y, float *w, float *h);
/* the checksum of a placeable header and whether the header is good. */
INT16 gp_wmf_placeable_checksum(const WmfPlaceableFileHeader *h);
BOOL gp_wmf_placeable_valid(const WmfPlaceableFileHeader *h);
/* the placeable header that stands for the bounds of a windows metafile. */
BOOL gp_meta_placeable_for(const GMetafile *m, WmfPlaceableFileHeader *out);
/* the clip of a graphics as a region of device pixels, NULL when there is no clip. */
HRGN gp_meta_clip_region(const GGraphics *g);
/* the unit factor of the enumerate calls: how many pixels of the metafile are one unit. */
BOOL gp_meta_unit_factor(const GMetafile *m, GpUnit unit, float *fx, float *fy);
/* a windows metafile turned into an enhanced one, as a bitmap of screen sized frames or by the records. */
HENHMETAFILE gp_emf_from_wmf_data(HMETAFILE wmf, const WmfPlaceableFileHeader *placeable, const BYTE *bits, UINT size,
                                  INT compat);

/* ------------------------------------------------------------------ emfplay.c */

/* the objects an emf+ metafile defines, by the numbers the records name them with. */
enum {
    PLAY_BRUSH = 1,
    PLAY_PEN,
    PLAY_PATH,
    PLAY_REGION,
    PLAY_IMAGE,
    PLAY_FONT,
    PLAY_STRING_FORMAT,
    PLAY_ATTRS,
    PLAY_CUSTOM_CAP,
};

typedef struct PlayObject {
    INT type;
    void *obj;
} PlayObject;

/* a Save or BeginContainer record names its level, the graphics numbers its own. */
typedef struct PlaySave {
    UINT index;
    UINT id;
    INT kind;
} PlaySave;

typedef struct GPlayer {
    GMetafile *mf;
    GGraphics *g;
    EnumerateMetafileProc cb;
    void *cb_data;
    struct GAttrs *attrs;
    /* the pixels of the metafile to the device, and the destination in world units */
    MimeVecMatrix to_device;
    GpRectF dest;
    /* what the emf+ records see: the resolution of the metafile that recorded them */
    float dpi_x, dpi_y;
    /* the device context for the gdi records, and the table the enumeration hands out */
    HDC dc;
    BOOL dc_bitmap;
    BOOL dc_saved;
    HANDLETABLE *table;
    INT table_count;
    BOOL gdi_enabled;
    /* the bitmap and the context are kept apart: the context holds changes that are not in the bitmap yet */
    BOOL gdi_pending, mixed;
    BOOL gdi_drawn;
    BOOL plus_drawn;
    BOOL aborted;
    PlayObject objects[256];
    /* an object that arrives in pieces */
    BYTE *piece;
    DWORD piece_size, piece_got;
    UINT piece_flags;
    PlaySave *saves;
    UINT save_count, save_cap;
    UINT save_base;
    /* the emf+ stream is shown to the callback and played, unless it ends */
    BOOL plus_ended;
    UINT next_section;
    BOOL skip_section;
} GPlayer;

/* draws the part of the metafile in a rectangle of pixels of its own resolution into a rectangle of the page. */
GpStatus gp_meta_play(GGraphics *g, GMetafile *m, const GpRectF *dest, const GpRectF *src, GpUnit src_unit,
                      EnumerateMetafileProc callback, void *data, struct GAttrs *attrs);
void gp_meta_player_free(GMetafile *m);
/* one record that a program got from an enumeration, played now. */
GpStatus gp_meta_play_record(GMetafile *m, UINT type, UINT flags, UINT size, const BYTE *data);

/* the context of a bitmap is brought up to date for gdi records, and its changes go back. */
void gp_play_gdi_begin(GPlayer *p);
void gp_play_gdi_end(GPlayer *p);

/* emfplus.c: one emf+ record played onto the graphics of the player. */
GpStatus gp_plus_record(GPlayer *p, UINT type, UINT flags, UINT size, const BYTE *data);
void gp_plus_player_init(GPlayer *p);
void gp_plus_player_free(GPlayer *p);
/* the graphics states a player pushed are given back. */
void gp_plus_player_unwind(GPlayer *p);

/* ------------------------------------------------------------------ emfrecord.c */

/* the graphics of a recording metafile, handed out once. NULL when there is none. */
GGraphics *gp_meta_recording_graphics(GMetafile *m);
/* the recording ends: the metafile is complete and its graphics is detached. */
void gp_meta_end_recording(GMetafile *m);
void gp_meta_recorder_free(GMetafile *m);

#endif
