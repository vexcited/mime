/*
 * compound document helpers: the signature test, class helpers over any IStorage or IStream, and file helpers. the
 * documents themselves are in docfile.c.
 */
#include "ole32_private.h"
#include <mime/stub.h>

static constexpr BYTE g_signature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

WINOLEAPI HRESULT WINAPI StgIsStorageFile(LPCOLESTR name)
{
    if (!name)
        return STG_E_INVALIDNAME;
    HANDLE file = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return STG_E_FILENOTFOUND;
    BYTE header[8];
    DWORD read = 0;
    BOOL ok = ReadFile(file, header, sizeof(header), &read, nullptr);
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

/* fewer than 16 bytes is STG_E_READFAULT, a failing stream returns its own error. */
WINOLEAPI HRESULT WINAPI ReadClassStm(IStream *stream, CLSID *clsid)
{
    if (!stream || !clsid)
        return E_INVALIDARG;
    ULONG read = 0;
    CLSID value = {};
    HRESULT hr = stream->lpVtbl->Read(stream, &value, sizeof(value), &read);
    if (FAILED(hr))
        return hr;
    if (read != sizeof(value))
        return STG_E_READFAULT;
    *clsid = value;
    return S_OK;
}

WINOLEAPI HRESULT WINAPI StgIsStorageILockBytes(ILockBytes *bytes)
{
    if (!bytes)
        return STG_E_INVALIDPOINTER;
    BYTE header[8];
    ULONG read = 0;
    ULARGE_INTEGER zero = {};
    HRESULT hr = bytes->lpVtbl->ReadAt(bytes, zero, header, sizeof(header), &read);
    if (FAILED(hr))
        return hr;
    if (read != sizeof(header))
        return S_FALSE;
    return memcmp(header, g_signature, sizeof(header)) ? S_FALSE : S_OK;
}

/* sets only the times given. */
WINOLEAPI HRESULT WINAPI StgSetTimes(LPCOLESTR name, const FILETIME *created, const FILETIME *accessed,
                                     const FILETIME *modified)
{
    if (!name)
        return STG_E_INVALIDNAME;
    HANDLE file = CreateFileW(name, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_ACCESS_DENIED ? STG_E_ACCESSDENIED : error == ERROR_SHARING_VIOLATION
                                                                       ? STG_E_SHAREVIOLATION
                                                                       : STG_E_FILENOTFOUND;
    }
    BOOL ok = SetFileTime(file, created, accessed, modified);
    CloseHandle(file);
    return ok ? S_OK : STG_E_ACCESSDENIED;
}

/*
 * a compound file's storage class when it has one, else the class registered for the extension through HKCR\.ext and
 * its ProgID. HKCR\FileType patterns are ignored. no extension, or an unregistered one, gives MK_E_INVALIDEXTENSION.
 */
WINOLEAPI HRESULT WINAPI GetClassFile(LPCOLESTR name, CLSID *clsid)
{
    if (!name || !clsid)
        return E_INVALIDARG;
    *clsid = CLSID_NULL;
    if (StgIsStorageFile(name) == S_OK) {
        IStorage *storage = nullptr;
        if (SUCCEEDED(StgOpenStorage(name, nullptr, STGM_READ | STGM_SHARE_DENY_WRITE, nullptr, 0, &storage)) &&
            storage) {
            HRESULT hr = ReadClassStg(storage, clsid);
            storage->lpVtbl->Release(storage);
            if (SUCCEEDED(hr) && !IsEqualCLSID(clsid, &CLSID_NULL))
                return hr;
            *clsid = CLSID_NULL;
        }
    }
    const WCHAR *extension = nullptr;
    for (const WCHAR *p = name; *p; p++) {
        if (*p == L'.')
            extension = p;
        else if (*p == L'\\' || *p == L'/' || *p == L':')
            extension = nullptr;
    }
    if (!extension || !extension[1] || wcslen(extension) > 64)
        return MK_E_INVALIDEXTENSION;
    WCHAR progid[256], path[320], text[64];
    DWORD size = sizeof(progid);
    if (RegGetValueW(HKEY_CLASSES_ROOT, extension, nullptr, RRF_RT_REG_SZ, nullptr, progid, &size))
        return MK_E_INVALIDEXTENSION;
    wcscpy(path, progid);
    wcscat(path, L"\\CLSID");
    size = sizeof(text);
    if (RegGetValueW(HKEY_CLASSES_ROOT, path, nullptr, RRF_RT_REG_SZ, nullptr, text, &size) ||
        !OleParseGuid(text, clsid))
        return MK_E_INVALIDEXTENSION;
    return S_OK;
}

WINOLEAPI HRESULT WINAPI WriteClassStm(IStream *stream, REFCLSID clsid)
{
    if (!stream || !clsid)
        return E_INVALIDARG;
    return stream->lpVtbl->Write(stream, clsid, sizeof(CLSID), nullptr);
}

#define STGFMT_STORAGE 0
#define STGFMT_NATIVE 1
#define STGFMT_FILE 3
#define STGFMT_ANY 4
#define STGFMT_DOCFILE 5

/* only the IStorage interface is made here, new or opened docfiles go through StgCreateDocfile. */
WINOLEAPI HRESULT WINAPI StgCreateStorageEx(LPCOLESTR name, DWORD mode, DWORD format, DWORD attributes,
                                            void *options, void *reserved, REFIID iid, void **result)
{
    if (!result)
        return STG_E_INVALIDPOINTER;
    *result = nullptr;
    if (!iid || reserved || (format != STGFMT_STORAGE && format != STGFMT_DOCFILE && format != STGFMT_ANY))
        return STG_E_INVALIDPARAMETER;
    if (!IsEqualIID(iid, &IID_IStorage))
        return E_NOINTERFACE;
    return StgCreateDocfile(name, mode, 0, (IStorage **)result);
}

WINOLEAPI HRESULT WINAPI StgOpenStorageEx(LPCOLESTR name, DWORD mode, DWORD format, DWORD attributes, void *options,
                                          void *reserved, REFIID iid, void **result)
{
    if (!result)
        return STG_E_INVALIDPOINTER;
    *result = nullptr;
    if (!iid || reserved || (format != STGFMT_STORAGE && format != STGFMT_DOCFILE && format != STGFMT_ANY))
        return STG_E_INVALIDPARAMETER;
    if (!IsEqualIID(iid, &IID_IStorage))
        return E_NOINTERFACE;
    return StgOpenStorage(name, nullptr, mode, nullptr, 0, (IStorage **)result);
}
