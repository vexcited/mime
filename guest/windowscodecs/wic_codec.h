/*
 * the codec side of windowscodecs.dll: what a decoder or encoder of one image format supplies to the generic objects
 * that implement IWICBitmapDecoder, IWICBitmapFrameDecode, IWICBitmapEncoder and IWICBitmapFrameEncode.
 */
#ifndef WIC_CODEC_H
#define WIC_CODEC_H

#include "wic_private.h"

/* ------------------------------------------------------------------ components */

typedef struct WicPattern {
    UINT64 position;
    UINT length;
    const BYTE *pattern;
    const BYTE *mask;
    BOOL end_of_stream;
} WicPattern;

typedef struct WicDecoderOps WicDecoderOps;
typedef struct WicEncoderOps WicEncoderOps;

typedef struct WicCodec {
    const GUID *clsid;
    const GUID *container;
    const WCHAR *name;
    const WCHAR *version;
    const WCHAR *spec;
    const WCHAR *mime;
    const WCHAR *extensions;
    const WCHAR *manufacturer;
    const WCHAR *models;
    const WCHAR *color_version;
    BYTE animation;
    BYTE chromakey;
    BYTE lossless;
    BYTE multiframe;
    BYTE versions;
    UINT pattern_count;
    const WicPattern *patterns;
    UINT format_count;
    const GUID *const *formats;
    const void *ops;
} WicCodec;

const WicCodec *WicFindDecoder(const GUID *clsid);
const WicCodec *WicFindEncoder(const GUID *clsid);
const WicCodec *WicDecoderByContainer(const GUID *container, const GUID *vendor);
const WicCodec *WicEncoderByContainer(const GUID *container, const GUID *vendor);
UINT WicDecoderCount(void);
UINT WicEncoderCount(void);
const WicCodec *WicDecoderAt(UINT index);
const WicCodec *WicEncoderAt(UINT index);
/* the codec whose signature the start of the stream matches, or none. The stream position is kept. */
const WicCodec *WicMatchDecoder(const BYTE *data, SIZE_T size);
BOOL WicPatternMatches(const WicPattern *pattern, const BYTE *data, SIZE_T size);

HRESULT WicDecoderInfoCreate(const WicCodec *codec, IWICBitmapDecoderInfo **info);
HRESULT WicEncoderInfoCreate(const WicCodec *codec, IWICBitmapEncoderInfo **info);
HRESULT WicPixelFormatInfoCreate(const WicFormat *format, IWICPixelFormatInfo **info);
HRESULT WicConverterInfoCreate(const GUID *clsid, IWICFormatConverterInfo **info);
HRESULT WicComponentInfo(const GUID *clsid, IWICComponentInfo **info);
HRESULT WicEnumerateComponents(DWORD types, DWORD options, IEnumUnknown **enumerator);

/* ------------------------------------------------------------------ decoders */

typedef struct WicFrameInfo {
    UINT width;
    UINT height;
    GUID format;
    double dpi_x;
    double dpi_y;
    const WICColor *palette;
    UINT palette_count;
    BOOL has_palette;
} WicFrameInfo;

typedef struct WicProfile {
    BOOL exif;
    UINT exif_space;
    const BYTE *data;
    UINT size;
} WicProfile;

struct WicDecoderOps {
    /* parses the file, which stays in memory as long as the state does. */
    HRESULT (*open)(const BYTE *data, SIZE_T size, void **state);
    void (*close)(void *state);
    UINT (*frame_count)(void *state);
    HRESULT (*frame_info)(void *state, UINT index, WicFrameInfo *info);
    /* the frame in its own format, rows of stride bytes. */
    HRESULT (*frame_decode)(void *state, UINT index, BYTE *dest, UINT stride);
    /* the palette of the whole file, or NULL. */
    HRESULT (*container_palette)(void *state, WICColor *colors, UINT *count);
    UINT (*profile_count)(void *state, UINT index);
    HRESULT (*profile)(void *state, UINT index, UINT profile, WicProfile *out);
    /* a smaller picture of a frame as a source, or WINCODEC_ERR_CODECNOTHUMBNAIL. */
    HRESULT (*thumbnail)(void *state, UINT index, IWICBitmapSource **source);
    /* a frame decoded at a fraction 1/scale of its size, scale being 1, 2, 4 or 8, or NULL when not supported. */
    HRESULT (*frame_decode_scaled)(void *state, UINT index, UINT scale, UINT *width, UINT *height, BYTE *dest,
                                   UINT stride, UINT dest_size);
    /* the metadata readers of a frame, or of the container for index ~0, an array to free with WicFree whose
     * readers the caller releases. */
    HRESULT (*metadata)(void *state, UINT index, IWICMetadataReader ***readers, UINT *count);
    BOOL previews;
};

/* ------------------------------------------------------------------ encoders */

typedef struct WicOptionDesc {
    const WCHAR *name;
    VARTYPE type;
    /* defaults and limits as numbers, a float is held in value_f. */
    double def;
    double min;
    double max;
} WicOptionDesc;

typedef struct WicEncFrame {
    UINT width;
    UINT height;
    GUID format;
    double dpi_x;
    double dpi_y;
    BYTE *pixels;
    UINT stride;
    WICColor palette[256];
    UINT palette_count;
    const double *options; /* the values, in the order of the descriptions. */
    UINT options_set;      /* the bits of the options the program set. */
    IWICBitmapSource *thumbnail;
    IWICColorContext **profiles;
    UINT profile_count;
    /* the metadata set on the frame writer, or NULL. */
    IWICMetadataBlockReader *metadata;
    /* the options that carry data, as tables of bytes. */
    const BYTE *luminance;
    const BYTE *chrominance;
} WicEncFrame;

struct WicEncoderOps {
    const WicOptionDesc *options;
    UINT option_count;
    BOOL multiframe;
    /* the format of the codec that a requested one is written as, or FALSE to use the generic choice. */
    BOOL (*closest_format)(GUID *format);
    HRESULT (*begin)(IStream *stream, void **state);
    HRESULT (*write_frame)(void *state, UINT index, WicEncFrame *frame);
    /* the frame is complete: the file may be written. */
    HRESULT (*finish)(void *state, IWICPalette *palette, IWICBitmapSource *thumbnail,
                      IWICMetadataBlockReader *metadata);
    void (*destroy)(void *state);
    /* container level options, such as a global palette or a thumbnail. */
    BOOL container_palette;
    BOOL container_thumbnail;
    /* the frames, or the container itself, take metadata that begins with the blocks a program sets. */
    BOOL frame_metadata;
    BOOL container_metadata;
};

HRESULT WicDecoderCreate(const WicCodec *codec, IWICBitmapDecoder **decoder);
HRESULT WicEncoderCreate(const WicCodec *codec, IWICBitmapEncoder **encoder);
/* decoders by content: the one that matches and opens the stream. */
HRESULT WicDecoderFromStream(IStream *stream, const GUID *vendor, WICDecodeOptions options,
                             IWICBitmapDecoder **decoder);

/* the first stream error code that applies to bad data. */
#define WIC_BAD_IMAGE WINCODEC_ERR_BADIMAGE
#define WIC_BAD_HEADER WINCODEC_ERR_BADHEADER

/* little endian reads. */
static inline UINT WicLe16(const BYTE *p) { return p[0] | ((UINT)p[1] << 8); }
static inline UINT WicLe32(const BYTE *p) { return p[0] | ((UINT)p[1] << 8) | ((UINT)p[2] << 16) | ((UINT)p[3] << 24); }
static inline UINT WicBe16(const BYTE *p) { return ((UINT)p[0] << 8) | p[1]; }
static inline UINT WicBe32(const BYTE *p) { return ((UINT)p[0] << 24) | ((UINT)p[1] << 16) | ((UINT)p[2] << 8) | p[3]; }

#endif
