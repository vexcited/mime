/*
 * avifil32.dll: the AVIFile and AVIStream API over the objects behind it. A file is opened by its handler, the class
 * the registry names for the form type or the extension of the file, and every call goes through the interfaces of
 * the objects. The handlers for AVI and wave files, the compressed streams, the edit streams and the default
 * IGetFrame are in this library.
 */
#include "avifil32_private.h"

#define CLSCTX_INPROC_SERVER_HANDLER 3

/* the time conversions use floating point. */
int _fltused = 0x9875;

HINSTANCE AviModule;

/* how many AVIFileInit calls are open, and how many times OLE was started for them. */
static LONG g_init;
static LONG g_ole;
/* the objects alive and the lock of the class factories, which keep the dll loaded. */
static LONG g_use;
static LONG g_locked;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        AviModule = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ memory and strings */

void *AviAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *AviRealloc(void *p, SIZE_T size)
{
    if (!p)
        return AviAlloc(size);
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p, size ? size : 1);
}

void AviFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

void AviUseAdd(LONG delta)
{
    InterlockedExchangeAdd(&g_use, delta);
}

/* the part of a path after the last separator. */
const WCHAR *AviBaseName(const WCHAR *path)
{
    const WCHAR *base = path;
    for (const WCHAR *p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            base = p + 1;
    return base;
}

void AviCopyName(WCHAR *dest, const WCHAR *source, SIZE_T count)
{
    SIZE_T i = 0;
    for (; i + 1 < count && source[i]; i++)
        dest[i] = source[i];
    dest[i] = 0;
}

/* ------------------------------------------------------------------ OLE */

HRESULT AviInitOle(void)
{
    HRESULT hr = OleInitialize(nullptr);
    if (SUCCEEDED(hr))
        InterlockedIncrement(&g_ole);
    return hr == S_FALSE ? S_OK : hr;
}

void AviExitOle(void)
{
    if (g_ole) {
        InterlockedDecrement(&g_ole);
        OleUninitialize();
    }
}

void WINAPI AVIFileInit(void)
{
    InterlockedIncrement(&g_init);
    AviInitOle();
}

void WINAPI AVIFileExit(void)
{
    InterlockedDecrement(&g_init);
    AviExitOle();
}

/* ------------------------------------------------------------------ the class registry */

static int hex_digit(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c |= 0x20;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static BOOL hex_run(const WCHAR **p, DWORD *value, int count, WCHAR stop)
{
    DWORD v = 0;
    for (int i = 0; i < count; i++) {
        int d = hex_digit((*p)[i]);
        if (d < 0)
            return FALSE;
        v = v * 16 + (DWORD)d;
    }
    *p += count;
    if (stop) {
        if (**p != stop)
            return FALSE;
        (*p)++;
    }
    *value = v;
    return TRUE;
}

/* a class id written as {00000000-0000-0000-0000-000000000000}, in the way the AVIFile keys keep them. */
static BOOL guid_from_string(const WCHAR *s, GUID *guid)
{
    DWORD v;
    if (*s != '{')
        return FALSE;
    s++;
    if (!hex_run(&s, &guid->Data1, 8, '-'))
        return FALSE;
    if (!hex_run(&s, &v, 4, '-'))
        return FALSE;
    guid->Data2 = (WORD)v;
    if (!hex_run(&s, &v, 4, '-'))
        return FALSE;
    guid->Data3 = (WORD)v;
    for (int i = 0; i < 8; i++) {
        if (!hex_run(&s, &v, 2, i == 1 ? '-' : 0))
            return FALSE;
        guid->Data4[i] = (BYTE)v;
    }
    return *s == '}';
}

/* the default value of a key below HKEY_CLASSES_ROOT as a class id. */
static BOOL class_from_key(const WCHAR *path, GUID *clsid)
{
    HKEY key;
    WCHAR value[100] = {0};
    DWORD type, size = sizeof(value) - sizeof(WCHAR);
    BOOL ok = FALSE;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return FALSE;
    if (RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE *)value, &size) == ERROR_SUCCESS && type == REG_SZ)
        ok = guid_from_string(value, clsid);
    RegCloseKey(key);
    return ok;
}

static void append_text(WCHAR *dest, SIZE_T capacity, const WCHAR *text)
{
    SIZE_T at = lstrlenW(dest);
    AviCopyName(dest + at, text, capacity - at);
}

/* the handler of a file: the one for the form type of a RIFF file, else the one for the extension. */
static BOOL handler_from_file(const WCHAR *path, GUID *clsid)
{
    static const UINT modes[] = {OF_SHARE_DENY_WRITE, OF_SHARE_DENY_NONE, 0};
    AviIo *io = nullptr;
    for (int i = 0; i < 3 && !io; i++)
        io = AviIoOpen(path, modes[i]);
    if (io) {
        BYTE head[12];
        BOOL riff = AviIoRead(io, 0, head, sizeof(head)) && !memcmp(head, "RIFF", 4);
        AviIoClose(io);
        if (riff) {
            WCHAR key[100] = L"AVIFile\\RIFFHandlers\\";
            WCHAR form[5] = {0};
            int length = 4;
            while (length > 0 && head[8 + length - 1] == ' ')
                length--;
            for (int i = 0; i < length; i++)
                form[i] = head[8 + i];
            append_text(key, 100, form);
            if (class_from_key(key, clsid))
                return TRUE;
        }
    }
    int length = lstrlenW(path);
    if (length < 4 || path[length - 1] == '.')
        return FALSE;
    for (int back = 2; back <= 4; back++) {
        WCHAR c = path[length - back];
        if (c == '/' || c == '\\')
            return FALSE;
        if (c == '.') {
            WCHAR key[100] = L"AVIFile\\Extensions\\";
            WCHAR extension[4] = {0};
            for (int i = 0; i < 3 && path[length - back + 1 + i]; i++)
                extension[i] = path[length - back + 1 + i];
            append_text(key, 100, extension);
            return class_from_key(key, clsid);
        }
    }
    return FALSE;
}

/* ------------------------------------------------------------------ AVIFile */

HRESULT WINAPI AVIFileOpenW(PAVIFILE *file, LPCWSTR name, UINT mode, const CLSID *handler)
{
    IUnknown *unknown = nullptr;
    CLSID clsid = {0};
    *file = nullptr;
    if (handler)
        clsid = *handler;
    else if (!handler_from_file(name, &clsid))
        return REGDB_E_CLASSNOTREG;
    HRESULT hr = CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER_HANDLER, &IID_IUnknown, (void **)&unknown);
    if (FAILED(hr))
        return hr;
    /* writing an AVI file is reading and writing it, and so is creating one. */
    UINT load = mode;
    if (load & OF_READWRITE)
        load &= ~(UINT)OF_WRITE;
    if (load & OF_CREATE)
        load = (load & ~3u) | OF_READWRITE;
    if (load & OF_WRITE)
        load = (load & ~3u) | OF_READWRITE;
    IAVIPersistFile *persist = nullptr;
    hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IAVIPersistFile_, (void **)&persist);
    if (FAILED(hr))
        hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IPersistFile_, (void **)&persist);
    if (SUCCEEDED(hr)) {
        hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IAVIFile, (void **)file);
        if (SUCCEEDED(hr)) {
            hr = persist->lpVtbl->Load(persist, name, load);
            if (FAILED(hr)) {
                (*file)->lpVtbl->Release(*file);
                *file = nullptr;
            }
        }
        persist->lpVtbl->Release(persist);
    }
    unknown->lpVtbl->Release(unknown);
    return hr;
}

static WCHAR *wide_name(const char *name)
{
    int count = MultiByteToWideChar(CP_ACP, 0, name, -1, nullptr, 0);
    WCHAR *wide = AviAlloc((SIZE_T)(count > 0 ? count : 1) * sizeof(WCHAR));
    if (wide && count > 0)
        MultiByteToWideChar(CP_ACP, 0, name, -1, wide, count);
    return wide;
}

HRESULT WINAPI AVIFileOpenA(PAVIFILE *file, LPCSTR name, UINT mode, const CLSID *handler)
{
    WCHAR *wide = wide_name(name);
    if (!wide)
        return AVIERR_MEMORY;
    HRESULT hr = AVIFileOpenW(file, wide, mode, handler);
    AviFree(wide);
    return hr;
}

ULONG WINAPI AVIFileAddRef(PAVIFILE file)
{
    return file->lpVtbl->AddRef(file);
}

ULONG WINAPI AVIFileRelease(PAVIFILE file)
{
    return file->lpVtbl->Release(file);
}

HRESULT WINAPI AVIFileInfoW(PAVIFILE file, AVIFILEINFOW *info, LONG size)
{
    memset(info, 0, (SIZE_T)size);
    return file->lpVtbl->Info(file, info, size);
}

HRESULT WINAPI AVIFileInfoA(PAVIFILE file, AVIFILEINFOA *info, LONG size)
{
    AVIFILEINFOW wide = {0};
    if ((ULONG)size < AVIFILEINFOA_SIZE)
        return AVIERR_BADSIZE;
    HRESULT hr = AVIFileInfoW(file, &wide, AVIFILEINFOW_SIZE);
    memcpy(info, &wide, offsetof(AVIFILEINFOA, szFileType));
    WideCharToMultiByte(CP_ACP, 0, wide.szFileType, -1, info->szFileType, 64, nullptr, nullptr);
    return hr;
}

HRESULT WINAPI AVIFileGetStream(PAVIFILE file, PAVISTREAM *stream, DWORD type, LONG index)
{
    return file->lpVtbl->GetStream(file, stream, type, index);
}

HRESULT WINAPI AVIFileCreateStreamW(PAVIFILE file, PAVISTREAM *stream, AVISTREAMINFOW *info)
{
    *stream = nullptr;
    return file->lpVtbl->CreateStream(file, stream, info);
}

HRESULT WINAPI AVIFileCreateStreamA(PAVIFILE file, PAVISTREAM *stream, AVISTREAMINFOA *info)
{
    AVISTREAMINFOW wide = {0};
    *stream = nullptr;
    memcpy(&wide, info, offsetof(AVISTREAMINFOW, szName));
    MultiByteToWideChar(CP_ACP, 0, info->szName, 64, wide.szName, 64);
    wide.szName[63] = 0;
    return file->lpVtbl->CreateStream(file, stream, &wide);
}

HRESULT WINAPI AVIFileEndRecord(PAVIFILE file)
{
    return file->lpVtbl->EndRecord(file);
}

HRESULT WINAPI AVIFileWriteData(PAVIFILE file, DWORD ckid, void *data, LONG size)
{
    return file->lpVtbl->WriteData(file, ckid, data, size);
}

HRESULT WINAPI AVIFileReadData(PAVIFILE file, DWORD ckid, void *data, LONG *size)
{
    return file->lpVtbl->ReadData(file, ckid, data, size);
}

/* ------------------------------------------------------------------ AVIStream */

ULONG WINAPI AVIStreamAddRef(PAVISTREAM stream)
{
    return stream->lpVtbl->AddRef(stream);
}

ULONG WINAPI AVIStreamRelease(PAVISTREAM stream)
{
    return stream->lpVtbl->Release(stream);
}

HRESULT WINAPI AVIStreamInfoW(PAVISTREAM stream, AVISTREAMINFOW *info, LONG size)
{
    memset(info, 0, (SIZE_T)size);
    return stream->lpVtbl->Info(stream, info, size);
}

HRESULT WINAPI AVIStreamInfoA(PAVISTREAM stream, AVISTREAMINFOA *info, LONG size)
{
    AVISTREAMINFOW wide = {0};
    HRESULT hr = AVIStreamInfoW(stream, &wide, AVISTREAMINFOW_SIZE);
    if ((ULONG)size < AVISTREAMINFOA_SIZE)
        return AVIERR_BADSIZE;
    memcpy(info, &wide, offsetof(AVISTREAMINFOA, szName));
    WideCharToMultiByte(CP_ACP, 0, wide.szName, -1, info->szName, 64, nullptr, nullptr);
    return hr;
}

LONG WINAPI AVIStreamFindSample(PAVISTREAM stream, LONG pos, LONG flags)
{
    if (!(flags & FIND_TYPE))
        flags |= FIND_KEY;
    if (!(flags & FIND_DIR))
        flags |= FIND_PREV;
    return stream->lpVtbl->FindSample(stream, pos, flags);
}

HRESULT WINAPI AVIStreamReadFormat(PAVISTREAM stream, LONG pos, void *format, LONG *size)
{
    return stream->lpVtbl->ReadFormat(stream, pos, format, size);
}

HRESULT WINAPI AVIStreamSetFormat(PAVISTREAM stream, LONG pos, void *format, LONG size)
{
    return stream->lpVtbl->SetFormat(stream, pos, format, size);
}

HRESULT WINAPI AVIStreamRead(PAVISTREAM stream, LONG start, LONG samples, void *buffer, LONG size, LONG *bytes,
                             LONG *samples_read)
{
    return stream->lpVtbl->Read(stream, start, samples, buffer, size, bytes, samples_read);
}

HRESULT WINAPI AVIStreamWrite(PAVISTREAM stream, LONG start, LONG samples, void *buffer, LONG size, DWORD flags,
                              LONG *samples_written, LONG *bytes_written)
{
    return stream->lpVtbl->Write(stream, start, samples, buffer, size, flags, samples_written, bytes_written);
}

HRESULT WINAPI AVIStreamReadData(PAVISTREAM stream, DWORD fcc, void *data, LONG *size)
{
    return stream->lpVtbl->ReadData(stream, fcc, data, size);
}

HRESULT WINAPI AVIStreamWriteData(PAVISTREAM stream, DWORD fcc, void *data, LONG size)
{
    return stream->lpVtbl->WriteData(stream, fcc, data, size);
}

LONG WINAPI AVIStreamStart(PAVISTREAM stream)
{
    AVISTREAMINFOW info = {0};
    stream->lpVtbl->Info(stream, &info, AVISTREAMINFOW_SIZE);
    return (LONG)info.dwStart;
}

LONG WINAPI AVIStreamLength(PAVISTREAM stream)
{
    AVISTREAMINFOW info = {0};
    if (stream->lpVtbl->Info(stream, &info, AVISTREAMINFOW_SIZE))
        return 1;
    return (LONG)info.dwLength;
}

/* samples to milliseconds and back, a sample inside the stream when it is clipped. */
LONG WINAPI AVIStreamSampleToTime(PAVISTREAM stream, LONG sample)
{
    AVISTREAMINFOW info = {0};
    if (stream->lpVtbl->Info(stream, &info, AVISTREAMINFOW_SIZE) || !info.dwRate || !info.dwScale)
        return sample;
    LONG start = (LONG)info.dwStart;
    LONG end = start + (LONG)info.dwLength;
    if ((sample > start ? sample : start) >= end)
        sample = end;
    else if (sample <= start)
        sample = start;
    LONG scale = (LONG)(1000 * info.dwScale);
    if (info.dwRate / info.dwScale >= 1000)
        return (LONG)(((double)scale * (double)sample + (double)(LONG)(info.dwRate - 1)) / (double)(LONG)info.dwRate);
    return (LONG)((double)scale * (double)sample / (double)(LONG)info.dwRate);
}

LONG WINAPI AVIStreamTimeToSample(PAVISTREAM stream, LONG time)
{
    AVISTREAMINFOW info = {0};
    if (time < 0)
        return -1;
    if (stream->lpVtbl->Info(stream, &info, AVISTREAMINFOW_SIZE) || !info.dwScale || !info.dwRate)
        return time;
    double product = (double)(LONG)info.dwRate * (double)time;
    LONG unit = (LONG)(1000 * info.dwScale);
    LONG sample;
    if (info.dwRate / info.dwScale >= 1000)
        sample = (LONG)((product + (double)(unit - 1)) / (double)unit);
    else
        sample = (LONG)(product / (double)unit);
    LONG start = (LONG)info.dwStart;
    LONG end = start + (LONG)info.dwLength;
    if ((sample > start ? sample : start) >= end)
        return end;
    return sample > start ? sample : start;
}

LONG AviSampleToTimeNoClip(PAVISTREAM stream, LONG sample)
{
    AVISTREAMINFOW info = {0};
    if (stream->lpVtbl->Info(stream, &info, AVISTREAMINFOW_SIZE) || !info.dwRate)
        return sample;
    return (LONG)((double)(LONG)(1000 * info.dwScale) * (double)sample / (double)(LONG)info.dwRate);
}

LONG AviTimeToSampleNoClip(PAVISTREAM stream, LONG time)
{
    AVISTREAMINFOW info = {0};
    if (time < 0)
        return -1;
    if (stream->lpVtbl->Info(stream, &info, AVISTREAMINFOW_SIZE) || !info.dwScale)
        return time;
    return (LONG)(((double)(LONG)info.dwRate * (double)time + (double)(LONG)(1000 * info.dwScale - 1)) /
                  (double)(LONG)(1000 * info.dwScale));
}

HRESULT WINAPI AVIStreamBeginStreaming(PAVISTREAM stream, LONG start, LONG end, LONG rate)
{
    IAVIStreaming *streaming = nullptr;
    if (FAILED(stream->lpVtbl->QueryInterface(stream, &IID_IAVIStreaming_, (void **)&streaming)))
        return S_OK;
    HRESULT hr = streaming->lpVtbl->Begin(streaming, start, end, rate);
    streaming->lpVtbl->Release(streaming);
    return hr;
}

HRESULT WINAPI AVIStreamEndStreaming(PAVISTREAM stream)
{
    IAVIStreaming *streaming = nullptr;
    if (FAILED(stream->lpVtbl->QueryInterface(stream, &IID_IAVIStreaming_, (void **)&streaming)))
        return S_OK;
    HRESULT hr = streaming->lpVtbl->End(streaming);
    streaming->lpVtbl->Release(streaming);
    return hr;
}

HRESULT WINAPI AVIStreamCreate(PAVISTREAM *stream, LONG param1, LONG param2, const CLSID *handler)
{
    if (!g_init)
        return E_UNEXPECTED;
    if (!handler)
        return REGDB_E_CLASSNOTREG;
    HRESULT hr = CoCreateInstance(handler, nullptr, CLSCTX_INPROC_SERVER_HANDLER, &IID_IAVIStream, (void **)stream);
    if (FAILED(hr))
        return hr;
    hr = (*stream)->lpVtbl->Create(*stream, (LPARAM)param1, (LPARAM)param2);
    if (FAILED(hr)) {
        (*stream)->lpVtbl->Release(*stream);
        *stream = nullptr;
    }
    return hr;
}

HRESULT WINAPI AVIStreamOpenFromFileW(PAVISTREAM *stream, LPCWSTR name, DWORD type, LONG index, UINT mode,
                                      const CLSID *handler)
{
    PAVIFILE file = nullptr;
    HRESULT hr = AVIFileOpenW(&file, name, mode, handler);
    if (SUCCEEDED(hr)) {
        hr = file->lpVtbl->GetStream(file, stream, type, index);
        file->lpVtbl->Release(file);
    }
    return hr;
}

HRESULT WINAPI AVIStreamOpenFromFileA(PAVISTREAM *stream, LPCSTR name, DWORD type, LONG index, UINT mode,
                                      const CLSID *handler)
{
    PAVIFILE file = nullptr;
    HRESULT hr = AVIFileOpenA(&file, name, mode, handler);
    if (SUCCEEDED(hr)) {
        hr = file->lpVtbl->GetStream(file, stream, type, index);
        file->lpVtbl->Release(file);
    }
    return hr;
}

/* the compressor for a stream is the class the registry names for its type, unless the caller names one. */
HRESULT WINAPI AVIMakeCompressedStream(PAVISTREAM *compressed, PAVISTREAM source, AVICOMPRESSOPTIONS *options,
                                       const CLSID *handler)
{
    CLSID clsid = {0};
    *compressed = nullptr;
    if (handler)
        clsid = *handler;
    else {
        AVISTREAMINFOW info = {0};
        HRESULT hr = AVIStreamInfoW(source, &info, AVISTREAMINFOW_SIZE);
        if (FAILED(hr))
            return hr;
        WCHAR key[100] = L"AVIFile\\Compressors\\";
        WCHAR type[5] = {0};
        for (int i = 0; i < 4; i++)
            type[i] = (BYTE)(info.fccType >> (8 * i));
        append_text(key, 100, type);
        if (!class_from_key(key, &clsid))
            return AVIERR_UNSUPPORTED;
    }
    HRESULT hr = CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER_HANDLER, &IID_IAVIStream,
                                  (void **)compressed);
    if (FAILED(hr))
        return hr;
    hr = (*compressed)->lpVtbl->Create(*compressed, (LPARAM)source, (LPARAM)options);
    if (FAILED(hr)) {
        (*compressed)->lpVtbl->Release(*compressed);
        *compressed = nullptr;
    }
    return hr;
}

/* ------------------------------------------------------------------ GetFrame */

void *WINAPI AVIStreamGetFrame(PGETFRAME frame, LONG pos)
{
    return frame ? frame->lpVtbl->GetFrame(frame, pos) : nullptr;
}

HRESULT WINAPI AVIStreamGetFrameClose(PGETFRAME frame)
{
    if (frame)
        frame->lpVtbl->Release(frame);
    return S_OK;
}

PGETFRAME WINAPI AVIStreamGetFrameOpen(PAVISTREAM stream, BITMAPINFOHEADER *wanted)
{
    IGetFrame *frame = nullptr;
    stream->lpVtbl->QueryInterface(stream, &IID_IGetFrame, (void **)&frame);
    if (!frame && FAILED(AviDefaultFrameCreate(stream, &frame)))
        return nullptr;
    if (frame->lpVtbl->SetFormat(frame, wanted, nullptr, 0, 0, -1, -1) < 0) {
        frame->lpVtbl->Release(frame);
        return nullptr;
    }
    return frame;
}

/* ------------------------------------------------------------------ the class factories */

typedef struct AviFactory {
    const IClassFactoryVtbl *lpVtbl;
    LONG refs;
    CLSID clsid;
} AviFactory;

static HRESULT STDMETHODCALLTYPE factory_qi(IClassFactory *This, REFIID riid, void **object)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IClassFactory)) {
        *object = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_addref(IClassFactory *This)
{
    AviUseAdd(1);
    return (ULONG)InterlockedIncrement(&((AviFactory *)This)->refs);
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    AviFactory *factory = (AviFactory *)This;
    LONG refs = InterlockedDecrement(&factory->refs);
    AviUseAdd(-1);
    if (!refs)
        AviFree(factory);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    const CLSID *clsid = &((AviFactory *)This)->clsid;
    if (IsEqualGUID(clsid, &CLSID_AVIFile_))
        return AviFileCreate(outer, riid, object);
    if (IsEqualGUID(clsid, &CLSID_AVICMStream_))
        return AviCmpStreamCreate(outer, riid, object);
    if (IsEqualGUID(clsid, &CLSID_ACMCmprStream_))
        return AviAcmStreamCreate(outer, riid, object);
    if (IsEqualGUID(clsid, &CLSID_WAVFile_))
        return AviWaveCreate(outer, riid, object);
    if (IsEqualGUID(clsid, &CLSID_AVIEditStream_))
        return AviEditStreamCreate(outer, riid, object);
    *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *This, BOOL lock)
{
    g_locked = lock;
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {factory_qi, factory_addref, factory_release, factory_create,
                                                 factory_lock};

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    *object = nullptr;
    if (!IsEqualGUID(clsid, &CLSID_AVIFile_) && !IsEqualGUID(clsid, &CLSID_ACMCmprStream_) &&
        !IsEqualGUID(clsid, &CLSID_WAVFile_) && !IsEqualGUID(clsid, &CLSID_AVICMStream_))
        return E_UNEXPECTED;
    AviFactory *factory = AviAlloc(sizeof(*factory));
    if (!factory)
        return E_OUTOFMEMORY;
    factory->lpVtbl = &g_factory_vtbl;
    factory->clsid = *clsid;
    HRESULT hr = factory_qi((IClassFactory *)factory, riid, object);
    if (FAILED(hr))
        AviFree(factory);
    return hr;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return g_locked || g_use ? S_FALSE : S_OK;
}

/* ------------------------------------------------------------------ the interface ids windows exports */

const GUID IID_IAVIFile = {0x00020020, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID IID_IAVIStream = {0x00020021, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID IID_IGetFrame = {0x00020023, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID IID_IAVIEditStream = {0x00020024, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
