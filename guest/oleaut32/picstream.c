/*
 * pictures read from a stream: the optional marker of OLE ahead of them, finding out how long the picture is, and
 * choosing the loader by the first two bytes.
 */
#include "picture.h"

/* a buffer is read in steps of this size while the length is not known better. */
#define READ_STEP 65536

#define MAGIC_ICON 0x0000
#define MAGIC_METAFILE 0x0001
#define MAGIC_GIF_UPPER 0x4947
#define MAGIC_GIF_LOWER 0x6967
#define MAGIC_BITMAP 0x4D42
#define MAGIC_PLACEABLE 0xCDD7
#define MAGIC_JPEG 0xD8FF

static ULONG get_u32(const BYTE *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (ULONG)p[3] << 24;
}

/* up to `count` bytes, until the stream has no more. */
static HRESULT read_some(IStream *stream, void *buffer, ULONG count, ULONG *got)
{
    BYTE *at = buffer;
    *got = 0;
    while (*got < count) {
        ULONG step = 0;
        HRESULT hr = stream->lpVtbl->Read(stream, at + *got, count - *got, &step);
        if (FAILED(hr))
            return hr;
        if (!step)
            break;
        *got += step;
    }
    return S_OK;
}

static HRESULT seek_to(IStream *stream, ULONGLONG position)
{
    LARGE_INTEGER to;
    to.QuadPart = (LONGLONG)position;
    return stream->lpVtbl->Seek(stream, to, STREAM_SEEK_SET, nullptr);
}

static HRESULT position_of(IStream *stream, DWORD origin, ULARGE_INTEGER *position)
{
    LARGE_INTEGER zero = {};
    return stream->lpVtbl->Seek(stream, zero, origin, position);
}

static HRESULT build_image(UINT word, BOOL marked, const BYTE *data, ULONG got, ULONG length, DWORD cx, DWORD cy,
                           DWORD flags, PIC_IMAGE *image)
{
    switch (word) {
    case MAGIC_ICON:
        return PicFromIconFile(data, got, cx, cy, flags, image);
    case MAGIC_METAFILE:
        return PicFromMetafile(data, got, image);
    case MAGIC_PLACEABLE:
        return PicFromPlaceable(data, got, image);
    case MAGIC_BITMAP:
        return PicFromBitmapFile(data, got, length, image);
    case MAGIC_GIF_UPPER:
    case MAGIC_GIF_LOWER:
    case MAGIC_JPEG: {
        AUT_IMAGE decoded = {};
        BOOL gif = word != MAGIC_JPEG;
        if (!(gif ? AutDecodeGif(data, got, &decoded) : AutDecodeJpeg(data, got, &decoded)))
            return CTL_E_INVALIDPICTURE;
        HRESULT hr = PicFromDecoded(&decoded, image);
        AutImageFree(&decoded);
        return hr;
    }
    default:
        return marked ? E_UNEXPECTED : CTL_E_INVALIDPICTURE;
    }
}

/*
 * Reads a picture. `length` is what the caller knows of its size, 0 when it does not. A picture kept in its
 * original format hands the bytes back so the picture can write them out as they came.
 */
HRESULT PicReadStream(IStream *stream, ULONG length, BOOL keep, DWORD cx, DWORD cy, DWORD flags, PIC_IMAGE *image,
                      BYTE **original, ULONG *original_size)
{
    if (!stream)
        return E_POINTER;
    memset(image, 0, sizeof(*image));
    image->Kind = PIC_UNSET;
    *original = nullptr;
    *original_size = 0;

    BOOL marked = FALSE;
    UINT word;
    ULARGE_INTEGER start;
    for (;;) {
        HRESULT hr = position_of(stream, STREAM_SEEK_CUR, &start);
        if (FAILED(hr))
            return hr;
        BYTE first[2];
        ULONG got;
        hr = read_some(stream, first, 2, &got);
        if (FAILED(hr) || got < 2)
            return CTL_E_INVALIDPICTURE;
        word = first[0] | first[1] << 8;
        if (word != STREAM_MARK)
            break;
        marked = TRUE;
        BYTE rest[6];
        hr = read_some(stream, rest, sizeof(rest), &got);
        if (FAILED(hr))
            return hr;
        if (got < sizeof(rest))
            return E_UNEXPECTED;
        length = get_u32(rest + 2);
        if (!length) {
            image->Kind = PICTYPE_NONE;
            return S_OK;
        }
    }

    if (!length) {
        ULARGE_INTEGER end;
        HRESULT hr = position_of(stream, STREAM_SEEK_END, &end);
        if (FAILED(hr))
            return hr;
        if (end.HighPart != start.HighPart)
            return E_FAIL;
        length = end.LowPart - start.LowPart;
    }
    HRESULT hr = seek_to(stream, start.QuadPart);
    if (FAILED(hr))
        return hr;
    if (!length) {
        image->Kind = PICTYPE_NONE;
        return S_OK;
    }
    if (length & 0x80000000)
        return E_FAIL;

    BYTE *buffer = nullptr;
    ULONG got = 0;
    while (got < length) {
        ULONG step = length - got < READ_STEP ? length - got : READ_STEP;
        BYTE *grown = got ? HeapReAlloc(GetProcessHeap(), 0, buffer, got + step) : AutAlloc(step);
        if (!grown) {
            AutFree(buffer);
            return E_OUTOFMEMORY;
        }
        buffer = grown;
        ULONG done;
        hr = read_some(stream, buffer + got, step, &done);
        got += done;
        if (FAILED(hr) || done < step)
            break;
    }
    if (FAILED(hr) && !got) {
        AutFree(buffer);
        return hr;
    }
    if (keep && got < length) {
        AutFree(buffer);
        return E_UNEXPECTED;
    }

    hr = build_image(word, marked, buffer, got, length, cx, cy, flags, image);
    if (SUCCEEDED(hr))
        hr = PicMeasure(image);
    if (FAILED(hr)) {
        PicImageFree(image, TRUE, FALSE);
        AutFree(buffer);
        return hr;
    }
    if (marked)
        seek_to(stream, start.QuadPart + length);
    if (keep) {
        *original = buffer;
        *original_size = got;
    } else {
        AutFree(buffer);
    }
    return S_OK;
}
