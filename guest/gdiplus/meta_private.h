/*
 * metafile internals shared by metafile.c, which loads, converts and answers queries, emfplay.c, which plays and
 * enumerates, and emfrecord.c, which records.
 */
#ifndef MIME_GDIPLUS_META_PRIVATE_H
#define MIME_GDIPLUS_META_PRIVATE_H

#include "gp_private.h"

/* recording ends in GM_READY, a playback in progress is GM_PLAYING. */
enum {
    GM_INVALID = 0,
    GM_CORRUPT = 1,
    GM_RECORDING = 2,
    GM_READY = 3,
    GM_PLAYING = 5,
};

/* the version every emf+ stream starts with, and the signature of the comment that carries it. */
#define EMFPLUS_VERSION 0xdbc01002u
#define EMFPLUS_VERSION_MASK 0xfffff000u
#define EMFPLUS_VERSION_BASE 0xdbc01000u
#define EMFPLUS_SIGNATURE 0x2b464d45u /* 'EMF+'. */
#define EMFPLUS_HEADER_RECORD 28      /* header record size: 12 bytes, then 16 bytes of data. */

#define PLACEABLE_KEY 0x9ac6cdd7u
#define PLACEABLE_SIZE 22
#define WMF_HEADER_SIZE 18
#define EMF_HEADER3_SIZE 88

#define HIMETRIC_PER_INCH 2540.0f

struct GRecorder;
struct GPlayer;

typedef struct GMetafile {
    GImage image;
    INT state;
    MetafileHeader header;
    /* an enhanced metafile, or a windows metafile for the two windows metafile types. */
    HANDLE handle;
    BOOL owns_handle;
    /* the enhanced metafile bytes for playing, read from the handle on first use. */
    BYTE *bytes;
    DWORD size;
    /* playing. */
    DWORD play_thread;
    struct GPlayer *player;
    /* recording. */
    struct GRecorder *rec;
    GGraphics *graphics;
    BOOL graphics_taken;
    WCHAR *file_name;
    IStream *stream;
    INT compat;
} GMetafile;

/* metafile.c. */

GMetafile *gp_meta_cast(const void *p);
GMetafile *gp_meta_new(void);
void gp_meta_free(GMetafile *m);
/* NULL when they cannot be read. valid until the metafile is freed. */
const BYTE *gp_meta_bytes(GMetafile *m, DWORD *size);
/* the header as the sdk reports it. InvalidParameter when the header is bad. */
GpStatus gp_meta_header_of_emf(HENHMETAFILE hemf, MetafileHeader *out, BOOL *damaged);
/* the handle is kept, and deleted with the object when owned. */
GpStatus gp_meta_init_emf(GMetafile *m, HENHMETAFILE hemf, BOOL owned);
/* placeable may be NULL. */
GpStatus gp_meta_init_wmf(GMetafile *m, HMETAFILE wmf, const WmfPlaceableFileHeader *placeable, BOOL owned);
/* the bytes of a file: an enhanced metafile, a placeable windows metafile or a plain one. */
GpStatus gp_meta_from_memory(const BYTE *data, SIZE_T size, INT compat, GMetafile **out);
/* without making an object. */
GpStatus gp_meta_header_of_bytes(const BYTE *data, SIZE_T size, MetafileHeader *out);
/* the frame in pixels of the metafile's own resolution, as GdipGetImageBounds reports it. */
void gp_meta_real_bounds(const GMetafile *m, float *x, float *y, float *w, float *h);
INT16 gp_wmf_placeable_checksum(const WmfPlaceableFileHeader *h);
BOOL gp_wmf_placeable_valid(const WmfPlaceableFileHeader *h);
/* a placeable header for the bounds of a windows metafile. */
BOOL gp_meta_placeable_for(const GMetafile *m, WmfPlaceableFileHeader *out);
/* device pixels, NULL when there is no clip. */
HRGN gp_meta_clip_region(const GGraphics *g);
/* for the enumerate calls: how many metafile pixels make one unit. */
BOOL gp_meta_unit_factor(const GMetafile *m, GpUnit unit, float *fx, float *fy);
/* converts a windows metafile to an enhanced one, as a bitmap of screen sized frames or by the records. */
HENHMETAFILE gp_emf_from_wmf_data(HMETAFILE wmf, const WmfPlaceableFileHeader *placeable, const BYTE *bits, UINT size,
                                  INT compat);

/* emfplay.c. */

/* emf+ object types, numbered as in the records. */
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

/* a Save or BeginContainer record names its own level, the graphics numbers its own. */
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
    /* metafile pixels to device, and the destination in world units. */
    MimeVecMatrix to_device;
    GpRectF dest;
    /* emf+ records see the resolution of the metafile that recorded them. */
    float dpi_x, dpi_y;
    /* the device context for gdi records, and the handle table the enumeration hands out. */
    HDC dc;
    BOOL dc_bitmap;
    BOOL dc_saved;
    HANDLETABLE *table;
    INT table_count;
    BOOL gdi_enabled;
    /* the context may hold changes that are not in the bitmap yet. */
    BOOL gdi_pending, mixed;
    BOOL gdi_drawn;
    BOOL plus_drawn;
    BOOL aborted;
    PlayObject objects[256];
    /* an object that arrives in pieces. */
    BYTE *piece;
    DWORD piece_size, piece_got;
    UINT piece_flags;
    PlaySave *saves;
    UINT save_count, save_cap;
    UINT save_base;
    /* the emf+ stream is shown to the callback and played until it ends. */
    BOOL plus_ended;
    UINT next_section;
    BOOL skip_section;
} GPlayer;

/* draws src, in pixels of the metafile's own resolution, into dest on the page. */
GpStatus gp_meta_play(GGraphics *g, GMetafile *m, const GpRectF *dest, const GpRectF *src, GpUnit src_unit,
                      EnumerateMetafileProc callback, void *data, struct GAttrs *attrs);
void gp_meta_player_free(GMetafile *m);
/* plays one record a program got from an enumeration. */
GpStatus gp_meta_play_record(GMetafile *m, UINT type, UINT flags, UINT size, const BYTE *data);

/* syncs a bitmap's context before gdi records, and copies its changes back after. */
void gp_play_gdi_begin(GPlayer *p);
void gp_play_gdi_end(GPlayer *p);

/* emfplus.c: plays one emf+ record onto the player's graphics. */
GpStatus gp_plus_record(GPlayer *p, UINT type, UINT flags, UINT size, const BYTE *data);
void gp_plus_player_init(GPlayer *p);
void gp_plus_player_free(GPlayer *p);
/* pops the graphics states the player pushed. */
void gp_plus_player_unwind(GPlayer *p);

/* emfrecord.c. */

/* handed out once. NULL when there is none. */
GGraphics *gp_meta_recording_graphics(GMetafile *m);
/* completes the metafile and detaches its graphics. */
void gp_meta_end_recording(GMetafile *m);
void gp_meta_recorder_free(GMetafile *m);

#endif
