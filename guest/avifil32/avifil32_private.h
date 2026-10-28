/* shared declarations of avifil32: the AVIFile interfaces, the AVI structures and the handlers' helpers. */
#ifndef AVIFIL32_PRIVATE_H
#define AVIFIL32_PRIVATE_H

#include <mime/nt.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>
#include <mime/win32/mmreg.h>
#include <mime/win32/objbase.h>
#include <mime/win32/oleidl.h>

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

/* errors of the AVIFile interfaces, which windows keeps in the facility 4. */
#define AVIERR_BASE 0x80044000L
#define AVIERR(n) ((HRESULT)(AVIERR_BASE | (n)))
#define AVIERR_UNSUPPORTED AVIERR(101)
#define AVIERR_BADFORMAT AVIERR(102)
#define AVIERR_MEMORY AVIERR(103)
#define AVIERR_INTERNAL AVIERR(104)
#define AVIERR_BADFLAGS AVIERR(105)
#define AVIERR_BADPARAM AVIERR(106)
#define AVIERR_BADSIZE AVIERR(107)
#define AVIERR_BADHANDLE AVIERR(108)
#define AVIERR_FILEREAD AVIERR(109)
#define AVIERR_FILEWRITE AVIERR(110)
#define AVIERR_FILEOPEN AVIERR(111)
#define AVIERR_COMPRESSOR AVIERR(112)
#define AVIERR_NOCOMPRESSOR AVIERR(113)
#define AVIERR_READONLY AVIERR(114)
#define AVIERR_NODATA AVIERR(115)
#define AVIERR_BUFFERTOOSMALL AVIERR(116)
#define AVIERR_CANTCOMPRESS AVIERR(117)
#define AVIERR_USERABORT AVIERR(198)
#define AVIERR_ERROR AVIERR(199)

#define FOURCC_(a, b, c, d) ((DWORD)(BYTE)(a) | ((DWORD)(BYTE)(b) << 8) | ((DWORD)(BYTE)(c) << 16) | ((DWORD)(BYTE)(d) << 24))
#define FCC_RIFF FOURCC_('R', 'I', 'F', 'F')
#define FCC_LIST FOURCC_('L', 'I', 'S', 'T')
#define FCC_AVI FOURCC_('A', 'V', 'I', ' ')
#define FCC_WAVE FOURCC_('W', 'A', 'V', 'E')
#define FCC_hdrl FOURCC_('h', 'd', 'r', 'l')
#define FCC_avih FOURCC_('a', 'v', 'i', 'h')
#define FCC_strl FOURCC_('s', 't', 'r', 'l')
#define FCC_strh FOURCC_('s', 't', 'r', 'h')
#define FCC_strf FOURCC_('s', 't', 'r', 'f')
#define FCC_strd FOURCC_('s', 't', 'r', 'd')
#define FCC_strn FOURCC_('s', 't', 'r', 'n')
#define FCC_movi FOURCC_('m', 'o', 'v', 'i')
#define FCC_idx1 FOURCC_('i', 'd', 'x', '1')
#define FCC_JUNK FOURCC_('J', 'U', 'N', 'K')
#define FCC_pad FOURCC_('p', 'a', 'd', ' ')
#define FCC_rec FOURCC_('r', 'e', 'c', ' ')
#define FCC_fmt FOURCC_('f', 'm', 't', ' ')
#define FCC_data FOURCC_('d', 'a', 't', 'a')
#define FCC_vids FOURCC_('v', 'i', 'd', 's')
#define FCC_auds FOURCC_('a', 'u', 'd', 's')
#define FCC_DIB FOURCC_('D', 'I', 'B', ' ')
#define FCC_RLE8 FOURCC_('R', 'L', 'E', '8')
#define FCC_RLE FOURCC_('R', 'L', 'E', ' ')
#define FCC_NONE FOURCC_('N', 'O', 'N', 'E')

/* the avih and strh chunks, as they are in the file. */
typedef struct AviMainHeader {
    DWORD dwMicroSecPerFrame;
    DWORD dwMaxBytesPerSec;
    DWORD dwPaddingGranularity;
    DWORD dwFlags;
    DWORD dwTotalFrames;
    DWORD dwInitialFrames;
    DWORD dwStreams;
    DWORD dwSuggestedBufferSize;
    DWORD dwWidth;
    DWORD dwHeight;
    DWORD dwReserved[4];
} AviMainHeader;

typedef struct AviStreamHeader {
    DWORD fccType;
    DWORD fccHandler;
    DWORD dwFlags;
    WORD wPriority;
    WORD wLanguage;
    DWORD dwInitialFrames;
    DWORD dwScale;
    DWORD dwRate;
    DWORD dwStart;
    DWORD dwLength;
    DWORD dwSuggestedBufferSize;
    DWORD dwQuality;
    DWORD dwSampleSize;
    SHORT rcLeft, rcTop, rcRight, rcBottom;
} AviStreamHeader;

typedef struct AviIndexEntry {
    DWORD ckid;
    DWORD dwFlags;
    DWORD dwChunkOffset;
    DWORD dwChunkLength;
} AviIndexEntry;

#define AVIF_HASINDEX 0x00000010
#define AVIF_MUSTUSEINDEX 0x00000020
#define AVIF_ISINTERLEAVED 0x00000100
#define AVIF_TRUSTCKTYPE 0x00000800

#define AVIIF_LIST 0x00000001
#define AVIIF_KEYFRAME 0x00000010
#define AVIIF_NOTIME 0x00000100

/* what AVISTREAMINFO and AVIFILEINFO carry. */
#define AVISTREAMINFO_DISABLED 0x00000001
#define AVISTREAMINFO_FORMATCHANGES 0x00010000

#define AVIFILECAPS_CANREAD 0x00000001
#define AVIFILECAPS_CANWRITE 0x00000002
#define AVIFILECAPS_ALLKEYFRAMES 0x00000010

typedef struct AVISTREAMINFOW {
    DWORD fccType;
    DWORD fccHandler;
    DWORD dwFlags;
    DWORD dwCaps;
    WORD wPriority;
    WORD wLanguage;
    DWORD dwScale;
    DWORD dwRate;
    DWORD dwStart;
    DWORD dwLength;
    DWORD dwInitialFrames;
    DWORD dwSuggestedBufferSize;
    DWORD dwQuality;
    DWORD dwSampleSize;
    RECT rcFrame;
    DWORD dwEditCount;
    DWORD dwFormatChangeCount;
    WCHAR szName[64];
} AVISTREAMINFOW;

typedef struct AVISTREAMINFOA {
    DWORD fccType;
    DWORD fccHandler;
    DWORD dwFlags;
    DWORD dwCaps;
    WORD wPriority;
    WORD wLanguage;
    DWORD dwScale;
    DWORD dwRate;
    DWORD dwStart;
    DWORD dwLength;
    DWORD dwInitialFrames;
    DWORD dwSuggestedBufferSize;
    DWORD dwQuality;
    DWORD dwSampleSize;
    RECT rcFrame;
    DWORD dwEditCount;
    DWORD dwFormatChangeCount;
    char szName[64];
} AVISTREAMINFOA;

typedef struct AVIFILEINFOW {
    DWORD dwMaxBytesPerSec;
    DWORD dwFlags;
    DWORD dwCaps;
    DWORD dwStreams;
    DWORD dwSuggestedBufferSize;
    DWORD dwWidth;
    DWORD dwHeight;
    DWORD dwScale;
    DWORD dwRate;
    DWORD dwLength;
    DWORD dwEditCount;
    WCHAR szFileType[64];
} AVIFILEINFOW;

typedef struct AVIFILEINFOA {
    DWORD dwMaxBytesPerSec;
    DWORD dwFlags;
    DWORD dwCaps;
    DWORD dwStreams;
    DWORD dwSuggestedBufferSize;
    DWORD dwWidth;
    DWORD dwHeight;
    DWORD dwScale;
    DWORD dwRate;
    DWORD dwLength;
    DWORD dwEditCount;
    char szFileType[64];
} AVIFILEINFOA;

#define AVISTREAMINFOW_SIZE 204
#define AVIFILEINFOW_SIZE 172
#define AVISTREAMINFOA_SIZE 140
#define AVIFILEINFOA_SIZE 108

#define AVICOMPRESSF_INTERLEAVE 0x00000001
#define AVICOMPRESSF_DATARATE 0x00000002
#define AVICOMPRESSF_KEYFRAMES 0x00000004
#define AVICOMPRESSF_VALID 0x00000008

typedef struct AVICOMPRESSOPTIONS {
    DWORD fccType;
    DWORD fccHandler;
    DWORD dwKeyFrameEvery;
    DWORD dwQuality;
    DWORD dwBytesPerSecond;
    DWORD dwFlags;
    void *lpFormat;
    DWORD cbFormat;
    void *lpParms;
    DWORD cbParms;
    DWORD dwInterleaveEvery;
} AVICOMPRESSOPTIONS, *LPAVICOMPRESSOPTIONS;

#define FIND_DIR 0x0000000F
#define FIND_NEXT 0x00000001
#define FIND_PREV 0x00000004
#define FIND_FROM_START 0x00000008
#define FIND_TYPE 0x000000F0
#define FIND_KEY 0x00000010
#define FIND_ANY 0x00000020
#define FIND_FORMAT 0x00000040
#define FIND_RET 0x0000F000
#define FIND_POS 0x00000000
#define FIND_LENGTH 0x00001000
#define FIND_OFFSET 0x00002000
#define FIND_SIZE 0x00003000
#define FIND_INDEX 0x00004000

/* ------------------------------------------------------------------ interfaces */

typedef struct IAVIStream IAVIStream, *PAVISTREAM;
typedef struct IAVIFile IAVIFile, *PAVIFILE;
typedef struct IGetFrame IGetFrame, *PGETFRAME;
typedef struct IAVIEditStream IAVIEditStream;
typedef struct IAVIStreaming IAVIStreaming;
typedef struct IAVIPersistFile IAVIPersistFile;

typedef struct IAVIStreamVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IAVIStream *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IAVIStream *This);
    ULONG(STDMETHODCALLTYPE *Release)(IAVIStream *This);
    HRESULT(STDMETHODCALLTYPE *Create)(IAVIStream *This, LPARAM param1, LPARAM param2);
    HRESULT(STDMETHODCALLTYPE *Info)(IAVIStream *This, AVISTREAMINFOW *info, LONG size);
    LONG(STDMETHODCALLTYPE *FindSample)(IAVIStream *This, LONG pos, LONG flags);
    HRESULT(STDMETHODCALLTYPE *ReadFormat)(IAVIStream *This, LONG pos, void *format, LONG *size);
    HRESULT(STDMETHODCALLTYPE *SetFormat)(IAVIStream *This, LONG pos, void *format, LONG size);
    HRESULT(STDMETHODCALLTYPE *Read)(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                     LONG *bytes, LONG *samples_read);
    HRESULT(STDMETHODCALLTYPE *Write)(IAVIStream *This, LONG start, LONG samples, void *buffer, LONG size,
                                      DWORD flags, LONG *samples_written, LONG *bytes_written);
    HRESULT(STDMETHODCALLTYPE *Delete)(IAVIStream *This, LONG start, LONG samples);
    HRESULT(STDMETHODCALLTYPE *ReadData)(IAVIStream *This, DWORD fcc, void *data, LONG *size);
    HRESULT(STDMETHODCALLTYPE *WriteData)(IAVIStream *This, DWORD fcc, void *data, LONG size);
    HRESULT(STDMETHODCALLTYPE *SetInfo)(IAVIStream *This, AVISTREAMINFOW *info, LONG size);
} IAVIStreamVtbl;
struct IAVIStream {
    const IAVIStreamVtbl *lpVtbl;
};

typedef struct IAVIStreamingVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IAVIStreaming *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IAVIStreaming *This);
    ULONG(STDMETHODCALLTYPE *Release)(IAVIStreaming *This);
    HRESULT(STDMETHODCALLTYPE *Begin)(IAVIStreaming *This, LONG start, LONG end, LONG rate);
    HRESULT(STDMETHODCALLTYPE *End)(IAVIStreaming *This);
} IAVIStreamingVtbl;
struct IAVIStreaming {
    const IAVIStreamingVtbl *lpVtbl;
};

typedef struct IAVIFileVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IAVIFile *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IAVIFile *This);
    ULONG(STDMETHODCALLTYPE *Release)(IAVIFile *This);
    HRESULT(STDMETHODCALLTYPE *Info)(IAVIFile *This, AVIFILEINFOW *info, LONG size);
    HRESULT(STDMETHODCALLTYPE *GetStream)(IAVIFile *This, PAVISTREAM *stream, DWORD fcc_type, LONG index);
    HRESULT(STDMETHODCALLTYPE *CreateStream)(IAVIFile *This, PAVISTREAM *stream, AVISTREAMINFOW *info);
    HRESULT(STDMETHODCALLTYPE *WriteData)(IAVIFile *This, DWORD ckid, void *data, LONG size);
    HRESULT(STDMETHODCALLTYPE *ReadData)(IAVIFile *This, DWORD ckid, void *data, LONG *size);
    HRESULT(STDMETHODCALLTYPE *EndRecord)(IAVIFile *This);
    HRESULT(STDMETHODCALLTYPE *DeleteStream)(IAVIFile *This, DWORD fcc_type, LONG index);
} IAVIFileVtbl;
struct IAVIFile {
    const IAVIFileVtbl *lpVtbl;
};

typedef struct IGetFrameVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IGetFrame *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IGetFrame *This);
    ULONG(STDMETHODCALLTYPE *Release)(IGetFrame *This);
    void *(STDMETHODCALLTYPE *GetFrame)(IGetFrame *This, LONG pos);
    HRESULT(STDMETHODCALLTYPE *Begin)(IGetFrame *This, LONG start, LONG end, LONG rate);
    HRESULT(STDMETHODCALLTYPE *End)(IGetFrame *This);
    HRESULT(STDMETHODCALLTYPE *SetFormat)(IGetFrame *This, BITMAPINFOHEADER *bih, void *bits, int x, int y, int dx,
                                          int dy);
} IGetFrameVtbl;
struct IGetFrame {
    const IGetFrameVtbl *lpVtbl;
};

typedef struct IAVIEditStreamVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IAVIEditStream *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IAVIEditStream *This);
    ULONG(STDMETHODCALLTYPE *Release)(IAVIEditStream *This);
    HRESULT(STDMETHODCALLTYPE *Cut)(IAVIEditStream *This, LONG *start, LONG *length, PAVISTREAM *result);
    HRESULT(STDMETHODCALLTYPE *Copy)(IAVIEditStream *This, LONG *start, LONG *length, PAVISTREAM *result);
    HRESULT(STDMETHODCALLTYPE *Paste)(IAVIEditStream *This, LONG *pos, LONG *length, PAVISTREAM source, LONG start,
                                      LONG end);
    HRESULT(STDMETHODCALLTYPE *Clone)(IAVIEditStream *This, PAVISTREAM *result);
    HRESULT(STDMETHODCALLTYPE *SetInfo)(IAVIEditStream *This, AVISTREAMINFOW *info, LONG size);
} IAVIEditStreamVtbl;
struct IAVIEditStream {
    const IAVIEditStreamVtbl *lpVtbl;
};

/* the same methods as IPersistFile has, and one more that is reserved. */
typedef struct IAVIPersistFileVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IAVIPersistFile *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IAVIPersistFile *This);
    ULONG(STDMETHODCALLTYPE *Release)(IAVIPersistFile *This);
    HRESULT(STDMETHODCALLTYPE *GetClassID)(IAVIPersistFile *This, CLSID *clsid);
    HRESULT(STDMETHODCALLTYPE *IsDirty)(IAVIPersistFile *This);
    HRESULT(STDMETHODCALLTYPE *Load)(IAVIPersistFile *This, LPCOLESTR file, DWORD mode);
    HRESULT(STDMETHODCALLTYPE *Save)(IAVIPersistFile *This, LPCOLESTR file, BOOL remember);
    HRESULT(STDMETHODCALLTYPE *SaveCompleted)(IAVIPersistFile *This, LPCOLESTR file);
    HRESULT(STDMETHODCALLTYPE *GetCurFile)(IAVIPersistFile *This, LPOLESTR *file);
    HRESULT(STDMETHODCALLTYPE *Reserved1)(IAVIPersistFile *This);
} IAVIPersistFileVtbl;
struct IAVIPersistFile {
    const IAVIPersistFileVtbl *lpVtbl;
};

MIME_DEFINE_GUID(IID_IPersistFile_, 0x0000010B, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(CLSID_AVIFile_, 0x00020000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(CLSID_AVICMStream_, 0x00020001, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(CLSID_WAVFile_, 0x00020003, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(CLSID_AVIEditStream_, 0x0002000A, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(CLSID_ACMCmprStream_, 0x0002000F, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

/* the interface ids windows exports from the dll. */
extern const GUID IID_IAVIFile;
extern const GUID IID_IAVIStream;
extern const GUID IID_IGetFrame;
extern const GUID IID_IAVIEditStream;
MIME_DEFINE_GUID(IID_IAVIStreaming_, 0x00020022, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IAVIPersistFile_, 0x00020025, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

/* the functions of the API that the handlers use themselves. */
HRESULT WINAPI AVIFileOpenW(PAVIFILE *file, LPCWSTR name, UINT mode, const CLSID *handler);
HRESULT WINAPI AVIFileInfoW(PAVIFILE file, AVIFILEINFOW *info, LONG size);
HRESULT WINAPI AVIStreamInfoW(PAVISTREAM stream, AVISTREAMINFOW *info, LONG size);
HRESULT WINAPI AVIMakeCompressedStream(PAVISTREAM *compressed, PAVISTREAM source, AVICOMPRESSOPTIONS *options,
                                       const CLSID *handler);
HRESULT WINAPI AVIMakeFileFromStreams(PAVIFILE *file, int count, PAVISTREAM *streams);
PGETFRAME WINAPI AVIStreamGetFrameOpen(PAVISTREAM stream, BITMAPINFOHEADER *wanted);
void *WINAPI AVIStreamGetFrame(PGETFRAME frame, LONG pos);
LONG WINAPI AVIStreamStart(PAVISTREAM stream);
LONG WINAPI AVIStreamLength(PAVISTREAM stream);
LONG WINAPI AVIStreamSampleToTime(PAVISTREAM stream, LONG sample);
LONG WINAPI AVIStreamTimeToSample(PAVISTREAM stream, LONG time);
LONG WINAPI AVIStreamFindSample(PAVISTREAM stream, LONG pos, LONG flags);
HRESULT WINAPI AVIStreamRead(PAVISTREAM stream, LONG start, LONG samples, void *buffer, LONG size, LONG *bytes,
                             LONG *samples_read);
HRESULT WINAPI AVIStreamWrite(PAVISTREAM stream, LONG start, LONG samples, void *buffer, LONG size, DWORD flags,
                              LONG *samples_written, LONG *bytes_written);
HRESULT WINAPI AVIStreamReadFormat(PAVISTREAM stream, LONG pos, void *format, LONG *size);

/* ------------------------------------------------------------------ helpers shared by the handlers */

extern HINSTANCE AviModule;

void *AviAlloc(SIZE_T size);
void *AviRealloc(void *p, SIZE_T size);
void AviFree(void *p);
void AviUseAdd(LONG delta);

/* the sample and time conversions of the AVIStream functions, with and without clipping. */
LONG AviSampleToTimeNoClip(PAVISTREAM stream, LONG sample);
LONG AviTimeToSampleNoClip(PAVISTREAM stream, LONG time);

/* the file layer: positional reads and buffered writes of one open file. */
typedef struct AviIo AviIo;
AviIo *AviIoOpen(const WCHAR *path, UINT mode);
void AviIoClose(AviIo *io);
BOOL AviIoRead(AviIo *io, UINT64 offset, void *buffer, DWORD size);
BOOL AviIoWrite(AviIo *io, UINT64 offset, const void *buffer, DWORD size);
BOOL AviIoZero(AviIo *io, UINT64 offset, DWORD size);
BOOL AviIoFlush(AviIo *io);
BOOL AviIoTruncate(AviIo *io, UINT64 offset);
UINT64 AviIoSize(AviIo *io);

/* a growing byte buffer holding the RIFF chunks the handlers keep as they are. */
typedef struct AviExtra {
    BYTE *data;
    DWORD size;
} AviExtra;
HRESULT AviExtraAdd(AviExtra *extra, DWORD ckid, const void *data, DWORD size);
HRESULT AviExtraRead(const AviExtra *extra, DWORD ckid, void *data, LONG *size);
void AviExtraFree(AviExtra *extra);

/* names of the streams and the file types. */
#define AVI_STRING_VIDEO L"Video"
#define AVI_STRING_AUDIO L"Audio"
#define AVI_STRING_AVI_FILE L"AVI Default File Handler"
#define AVI_STRING_WAVE_FILE L"Microsoft Waveform"
#define AVI_STRING_WAVE_STREAM L"Microsoft Waveform: %s"
#define AVI_STRING_UNCOMPRESSED L"Uncompressed"
const WCHAR *AviBaseName(const WCHAR *path);
void AviCopyName(WCHAR *dest, const WCHAR *source, SIZE_T count);

/* the handlers, each one creates its object for CoCreateInstance. */
HRESULT AviFileCreate(IUnknown *outer, REFIID riid, void **object);
HRESULT AviWaveCreate(IUnknown *outer, REFIID riid, void **object);
HRESULT AviCmpStreamCreate(IUnknown *outer, REFIID riid, void **object);
HRESULT AviAcmStreamCreate(IUnknown *outer, REFIID riid, void **object);
HRESULT AviEditStreamCreate(IUnknown *outer, REFIID riid, void **object);

HRESULT AviDefaultFrameCreate(PAVISTREAM stream, IGetFrame **frame);

/* DIB helpers. */
BOOL AviBitmapHeaderValid(const BITMAPINFOHEADER *bih, DWORD size);
DWORD AviDibStride(DWORD width, DWORD bits);

/* the helpers of the clipboard and of the files made of streams. */
HRESULT AviMakeStreamFromMemory(UINT format, const void *data, SIZE_T size, PAVISTREAM *stream);

HRESULT AviInitOle(void);
void AviExitOle(void);

#endif
