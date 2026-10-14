/*
 * compound document storage. only the signature test and the class helpers
 * that work over any IStorage or IStream are implemented; mime has no
 * structured storage of its own yet.
 */
#include "ole32_private.h"
#include <mime/stub.h>

static const BYTE g_signature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

WINOLEAPI HRESULT WINAPI StgIsStorageFile(LPCOLESTR name)
{
    if (!name)
        return STG_E_INVALIDNAME;
    HANDLE file = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return STG_E_FILENOTFOUND;
    BYTE header[8];
    DWORD read = 0;
    BOOL ok = ReadFile(file, header, sizeof(header), &read, NULL);
    CloseHandle(file);
    if (!ok || read != sizeof(header))
        return S_FALSE;
    return memcmp(header, g_signature, sizeof(header)) ? S_FALSE : S_OK;
}

WINOLEAPI HRESULT WINAPI ReadClassStg(IStorage *storage, CLSID *clsid)
{
    if (!clsid)
        return E_INVALIDARG;
    memset(clsid, 0, sizeof(*clsid));
    if (!storage)
        return E_INVALIDARG;
    STATSTG stat;
    HRESULT hr = storage->lpVtbl->Stat(storage, &stat, STATFLAG_NONAME);
    if (SUCCEEDED(hr))
        *clsid = stat.clsid;
    return hr;
}

WINOLEAPI HRESULT WINAPI WriteClassStg(IStorage *storage, REFCLSID clsid)
{
    if (!storage || !clsid)
        return E_INVALIDARG;
    return storage->lpVtbl->SetClass(storage, clsid);
}

WINOLEAPI HRESULT WINAPI WriteClassStm(IStream *stream, REFCLSID clsid)
{
    if (!stream || !clsid)
        return E_INVALIDARG;
    return stream->lpVtbl->Write(stream, clsid, sizeof(CLSID), NULL);
}

WINOLEAPI HRESULT WINAPI StgCreateDocfile(LPCOLESTR name, DWORD mode, DWORD reserved, IStorage **storage)
{
    MIME_STUB("ole32");
    if (!storage)
        return STG_E_INVALIDPOINTER;
    *storage = NULL;
    return STG_E_INVALIDFUNCTION;
}

WINOLEAPI HRESULT WINAPI StgOpenStorage(LPCOLESTR name, IStorage *priority, DWORD mode, SNB exclude,
                                        DWORD reserved, IStorage **storage)
{
    MIME_STUB("ole32");
    if (!storage)
        return STG_E_INVALIDPOINTER;
    *storage = NULL;
    return STG_E_INVALIDFUNCTION;
}

WINOLEAPI HRESULT WINAPI StgCreateDocfileOnILockBytes(ILockBytes *bytes, DWORD mode, DWORD reserved,
                                                      IStorage **storage)
{
    MIME_STUB("ole32");
    if (!storage)
        return STG_E_INVALIDPOINTER;
    *storage = NULL;
    return STG_E_INVALIDFUNCTION;
}

WINOLEAPI HRESULT WINAPI StgOpenStorageOnILockBytes(ILockBytes *bytes, IStorage *priority, DWORD mode,
                                                    SNB exclude, DWORD reserved, IStorage **storage)
{
    MIME_STUB("ole32");
    if (!storage)
        return STG_E_INVALIDPOINTER;
    *storage = NULL;
    return STG_E_INVALIDFUNCTION;
}

WINOLEAPI HRESULT WINAPI ReadFmtUserTypeStg(IStorage *storage, CLIPFORMAT *format, LPOLESTR *user_type)
{
    MIME_STUB("ole32");
    if (format)
        *format = 0;
    if (user_type)
        *user_type = NULL;
    return E_NOTIMPL;
}

WINOLEAPI HRESULT WINAPI WriteFmtUserTypeStg(IStorage *storage, CLIPFORMAT format, LPOLESTR user_type)
{
    MIME_STUB("ole32");
    return E_NOTIMPL;
}

WINOLEAPI HRESULT WINAPI SetConvertStg(IStorage *storage, BOOL convert)
{
    MIME_STUB("ole32");
    return E_NOTIMPL;
}
