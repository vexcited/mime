/*
 * the file system object: the paths, files, folders and drives of the machine, and the text files, as the methods of
 * Scripting.FileSystemObject.
 */
#include "scrrun_private.h"

#define OBJECT_OF(type, member, pointer) ((type *)((char *)(pointer) - offsetof(type, member)))

typedef struct FSO {
    IFileSystem3 Fso;
    IProvideClassInfo Info;
    LONG Refs;
    IDriveCollection *Drives;
} FSO;

static const IFileSystem3Vtbl g_fso_vtbl;
static const IProvideClassInfoVtbl g_fso_info_vtbl;

static FSO *fso_of(IFileSystem3 *This)
{
    return OBJECT_OF(FSO, Fso, This);
}

static HRESULT STDMETHODCALLTYPE fso_query_interface(IFileSystem3 *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    FSO *fso = fso_of(This);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IFileSystem) ||
        IsEqualIID(riid, &IID_IFileSystem3))
        *object = &fso->Fso;
    else if (IsEqualIID(riid, &IID_IProvideClassInfo))
        *object = &fso->Info;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&fso->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE fso_add_ref(IFileSystem3 *This)
{
    return (ULONG)InterlockedIncrement(&fso_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE fso_release(IFileSystem3 *This)
{
    FSO *fso = fso_of(This);
    LONG refs = InterlockedDecrement(&fso->Refs);
    if (!refs) {
        if (fso->Drives)
            fso->Drives->lpVtbl->Release(fso->Drives);
        ScrFree(fso);
        ScrUnlockDll();
    }
    return (ULONG)refs;
}

SCR_DISPATCH_METHODS(fso, IFileSystem3, &IID_IFileSystem3)

static HRESULT STDMETHODCALLTYPE fso_get_Drives(IFileSystem3 *This, IDriveCollection **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FSO *fso = fso_of(This);
    if (!fso->Drives) {
        IDriveCollection *drives;
        HRESULT hr = DriveCollectionCreate(&drives);
        if (FAILED(hr))
            return hr;
        if (InterlockedCompareExchangePointer((void *volatile *)&fso->Drives, drives, nullptr))
            drives->lpVtbl->Release(drives);
    }
    fso->Drives->lpVtbl->AddRef(fso->Drives);
    *out = fso->Drives;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fso_BuildPath(IFileSystem3 *This, BSTR path, BSTR name, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    WCHAR *built;
    HRESULT hr = PathBuild(SysStringLen(path) ? path : L"", SysStringLen(name) ? name : L"", &built);
    if (SUCCEEDED(hr)) {
        hr = AllocateBstr(built, -1, out);
        ScrFree(built);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE fso_GetDriveName(IFileSystem3 *This, BSTR path, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    int length = 0;
    const WCHAR *part;
    if (!SysStringLen(path) || !(part = PathDrivePart(path, &length)))
        return S_OK;
    return AllocateBstr(part, length, out);
}

static HRESULT STDMETHODCALLTYPE fso_GetParentFolderName(IFileSystem3 *This, BSTR path, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    int length = 0;
    const WCHAR *part;
    if (!SysStringLen(path) || !(part = PathParentPart(path, &length)))
        return S_OK;
    return AllocateBstr(part, length, out);
}

static HRESULT STDMETHODCALLTYPE fso_GetFileName(IFileSystem3 *This, BSTR path, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    int length = 0;
    const WCHAR *part;
    if (!SysStringLen(path) || !(part = PathNamePart(path, &length)))
        return S_OK;
    return AllocateBstr(part, length, out);
}

static HRESULT STDMETHODCALLTYPE fso_GetBaseName(IFileSystem3 *This, BSTR path, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    int length = 0;
    const WCHAR *part;
    if (!SysStringLen(path) || !(part = PathNamePart(path, &length)))
        return S_OK;
    int dot = -1;
    for (int i = 0; i < length; i++) {
        if (part[i] == '.')
            dot = i;
    }
    return AllocateBstr(part, dot < 0 ? length : dot, out);
}

static HRESULT STDMETHODCALLTYPE fso_GetExtensionName(IFileSystem3 *This, BSTR path, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    int length = 0;
    const WCHAR *part;
    if (!SysStringLen(path) || !(part = PathExtensionPart(path, &length)))
        return S_OK;
    return AllocateBstr(part, length, out);
}

static HRESULT STDMETHODCALLTYPE fso_GetAbsolutePathName(IFileSystem3 *This, BSTR path, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    WCHAR *canonical;
    HRESULT hr = PathCanonical(SysStringLen(path) ? path : L".", &canonical);
    if (SUCCEEDED(hr)) {
        hr = AllocateBstr(canonical, -1, out);
        ScrFree(canonical);
    }
    return hr;
}

/* a number for names of temporary files, from the clock and the process. */
static ULONG random_next(void)
{
    static ULONG state;
    if (!state) {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        state = (counter.LowPart ^ (GetCurrentProcessId() * 2654435761u) ^ GetTickCount()) | 1;
    }
    state = state * 1103515245u + 12345u;
    return (state >> 8) ^ (state << 7);
}

static HRESULT STDMETHODCALLTYPE fso_GetTempName(IFileSystem3 *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    static const WCHAR digits[] = L"0123456789ABCDEF";
    ULONG first = random_next(), second = random_next();
    WCHAR name[16] = L"rad";
    name[3] = digits[first & 0xF];
    name[4] = digits[second & 0xF];
    name[5] = digits[(second & 0xFF) >> 4];
    name[6] = digits[(second >> 8) & 0xF];
    name[7] = digits[(second >> 12) & 0xF];
    memcpy(name + 8, L".tmp", 5 * sizeof(WCHAR));
    return AllocateBstr(name, -1, out);
}

static HRESULT STDMETHODCALLTYPE fso_DriveExists(IFileSystem3 *This, BSTR spec, VARIANT_BOOL *out)
{
    if (!out)
        return E_POINTER;
    VARIANT_BOOL result = VARIANT_FALSE;
    if (SysStringLen(spec)) {
        WCHAR *canonical;
        if (SUCCEEDED(PathCanonicalDrive(spec, &canonical))) {
            if (SUCCEEDED(PathDriveExists(canonical)))
                result = VARIANT_TRUE;
            ScrFree(canonical);
        }
    }
    *out = result;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fso_FileExists(IFileSystem3 *This, BSTR spec, VARIANT_BOOL *out)
{
    if (!out)
        return E_POINTER;
    *out = SysStringLen(spec) && SUCCEEDED(PathFileExists(spec)) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fso_FolderExists(IFileSystem3 *This, BSTR spec, VARIANT_BOOL *out)
{
    if (!out)
        return E_POINTER;
    *out = SysStringLen(spec) && SUCCEEDED(PathFolderExists(spec)) ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fso_GetDrive(IFileSystem3 *This, BSTR spec, IDrive **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!SysStringLen(spec))
        return E_INVALIDARG;
    return FsGetDrive(spec, out);
}

static HRESULT STDMETHODCALLTYPE fso_GetFile(IFileSystem3 *This, BSTR path, IFile **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!SysStringLen(path))
        return E_INVALIDARG;
    return FsGetFile(path, out);
}

static HRESULT STDMETHODCALLTYPE fso_GetFolder(IFileSystem3 *This, BSTR path, IFolder **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!SysStringLen(path))
        return E_INVALIDARG;
    return FsGetFolder(path, out);
}

static HRESULT STDMETHODCALLTYPE fso_GetSpecialFolder(IFileSystem3 *This, SpecialFolderConst which, IFolder **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    WCHAR *path = nullptr;
    DWORD size = MAX_PATH, length = 0;
    if (which != WindowsFolder && which != SystemFolder && which != TemporaryFolder)
        return E_INVALIDARG;
    for (;;) {
        path = ScrAlloc(size * sizeof(WCHAR));
        if (!path)
            return E_OUTOFMEMORY;
        length = which == WindowsFolder ? GetWindowsDirectoryW(path, size)
                 : which == SystemFolder ? GetSystemDirectoryW(path, size)
                                         : GetTempPath2W(size, path);
        if (length < size)
            break;
        ScrFree(path);
        size = length + 1;
    }
    if (!length) {
        ScrFree(path);
        return E_INVALIDARG;
    }
    if (PathIsSeparator(path[length - 1]))
        path[length - 1] = 0;
    HRESULT hr = FolderObjectGet(path, out);
    ScrFree(path);
    return hr;
}

static HRESULT STDMETHODCALLTYPE fso_DeleteFile(IFileSystem3 *This, BSTR spec, VARIANT_BOOL force)
{
    if (!SysStringLen(spec))
        return E_INVALIDARG;
    return FsDeleteFile(spec, force != VARIANT_FALSE);
}

static HRESULT STDMETHODCALLTYPE fso_DeleteFolder(IFileSystem3 *This, BSTR spec, VARIANT_BOOL force)
{
    if (!SysStringLen(spec))
        return E_INVALIDARG;
    return FsDeleteFolder(spec, force != VARIANT_FALSE);
}

static HRESULT STDMETHODCALLTYPE fso_MoveFile(IFileSystem3 *This, BSTR source, BSTR destination)
{
    if (!SysStringLen(source) || !SysStringLen(destination))
        return E_INVALIDARG;
    return FsRelocate(source, destination, FALSE, FALSE, TRUE, nullptr);
}

static HRESULT STDMETHODCALLTYPE fso_MoveFolder(IFileSystem3 *This, BSTR source, BSTR destination)
{
    if (!SysStringLen(source) || !SysStringLen(destination))
        return E_INVALIDARG;
    return FsRelocate(source, destination, FALSE, TRUE, TRUE, nullptr);
}

static HRESULT STDMETHODCALLTYPE fso_CopyFile(IFileSystem3 *This, BSTR source, BSTR destination, VARIANT_BOOL overwrite)
{
    if (!SysStringLen(source) || !SysStringLen(destination))
        return E_INVALIDARG;
    return FsRelocate(source, destination, overwrite != VARIANT_FALSE, FALSE, FALSE, nullptr);
}

static HRESULT STDMETHODCALLTYPE fso_CopyFolder(IFileSystem3 *This, BSTR source, BSTR destination, VARIANT_BOOL overwrite)
{
    if (!SysStringLen(source) || !SysStringLen(destination))
        return E_INVALIDARG;
    return FsRelocate(source, destination, overwrite != VARIANT_FALSE, TRUE, FALSE, nullptr);
}

static HRESULT STDMETHODCALLTYPE fso_CreateFolder(IFileSystem3 *This, BSTR path, IFolder **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!SysStringLen(path))
        return E_INVALIDARG;
    return FsCreateFolder(path, out);
}

static HRESULT STDMETHODCALLTYPE fso_CreateTextFile(IFileSystem3 *This, BSTR name, VARIANT_BOOL overwrite,
                                                    VARIANT_BOOL unicode, ITextStream **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!SysStringLen(name))
        return E_INVALIDARG;
    return TextStreamFromFile(name, GENERIC_WRITE, FILE_SHARE_READ, overwrite != VARIANT_FALSE ? CREATE_ALWAYS : CREATE_NEW,
                              ForWriting, unicode != VARIANT_FALSE ? TristateTrue : TristateFalse, out);
}

/* a text file opened for a mode: the access, the sharing and what happens when the file is or is not there. */
HRESULT OpenTextFile(const WCHAR *path, IOMode mode, BOOL create, Tristate format, ITextStream **stream)
{
    DWORD access, share, disposition;
    switch (mode) {
    case ForReading:
        access = GENERIC_READ;
        share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        disposition = create ? OPEN_ALWAYS : OPEN_EXISTING;
        break;
    case ForWriting:
        access = GENERIC_WRITE;
        share = FILE_SHARE_READ;
        disposition = create ? CREATE_ALWAYS : TRUNCATE_EXISTING;
        break;
    case ForAppending:
        access = GENERIC_READ | GENERIC_WRITE;
        share = FILE_SHARE_READ;
        disposition = create ? OPEN_ALWAYS : OPEN_EXISTING;
        break;
    default:
        return E_INVALIDARG;
    }
    return TextStreamFromFile(path, access, share, disposition, mode, format, stream);
}

static HRESULT STDMETHODCALLTYPE fso_OpenTextFile(IFileSystem3 *This, BSTR name, IOMode mode, VARIANT_BOOL create,
                                                  Tristate format, ITextStream **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!SysStringLen(name))
        return E_INVALIDARG;
    return OpenTextFile(name, mode, create != VARIANT_FALSE, format, out);
}

static HRESULT STDMETHODCALLTYPE fso_GetStandardStream(IFileSystem3 *This, StandardStreamTypes type, VARIANT_BOOL unicode,
                                                       ITextStream **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    DWORD id = 0;
    if (type == StdIn)
        id = STD_INPUT_HANDLE;
    else if (type == StdOut)
        id = STD_OUTPUT_HANDLE;
    else if (type == StdErr)
        id = STD_ERROR_HANDLE;
    return TextStreamStandard(id, unicode != VARIANT_FALSE ? TristateTrue : TristateFalse, out);
}

static HRESULT STDMETHODCALLTYPE fso_GetFileVersion(IFileSystem3 *This, BSTR name, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!SysStringLen(name))
        return E_INVALIDARG;
    WCHAR full[MAX_PATH];
    WCHAR *file_part = nullptr;
    GetFullPathNameW(name, MAX_PATH, full, &file_part);
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(full, &handle);
    if (!size) {
        DWORD error = GetLastError();
        if (error == ERROR_BAD_FORMAT || error == ERROR_RESOURCE_DATA_NOT_FOUND || error == ERROR_RESOURCE_TYPE_NOT_FOUND)
            return AllocateBstr(L"", 0, out);
        return (LONG)error > 0 ? (HRESULT)(0x80070000u | (error & 0xFFFF)) : (HRESULT)error;
    }
    BYTE *block = ScrAlloc(size);
    if (!block)
        return E_OUTOFMEMORY;
    HRESULT hr;
    VS_FIXEDFILEINFO *fixed = nullptr;
    UINT fixed_size = 0;
    if (!GetFileVersionInfoW(full, handle, size, block)) {
        DWORD error = GetLastError();
        hr = (LONG)error > 0 ? (HRESULT)(0x80070000u | (error & 0xFFFF)) : (HRESULT)error;
    } else if (VerQueryValueW(block, L"\\", (void **)&fixed, &fixed_size) && fixed) {
        WCHAR text[64];
        _snwprintf(text, 64, L"%u.%u.%u.%u", fixed->dwFileVersionMS >> 16, fixed->dwFileVersionMS & 0xFFFF,
                   fixed->dwFileVersionLS >> 16, fixed->dwFileVersionLS & 0xFFFF);
        hr = AllocateBstr(text, -1, out);
    } else {
        hr = E_INVALIDARG;
    }
    ScrFree(block);
    return hr;
}

static const IFileSystem3Vtbl g_fso_vtbl = {
    fso_query_interface, fso_add_ref, fso_release, fso_GetTypeInfoCount, fso_GetTypeInfo, fso_GetIDsOfNames, fso_Invoke,
    fso_get_Drives, fso_BuildPath, fso_GetDriveName, fso_GetParentFolderName, fso_GetFileName, fso_GetBaseName,
    fso_GetExtensionName, fso_GetAbsolutePathName, fso_GetTempName, fso_DriveExists, fso_FileExists, fso_FolderExists,
    fso_GetDrive, fso_GetFile, fso_GetFolder, fso_GetSpecialFolder, fso_DeleteFile, fso_DeleteFolder, fso_MoveFile,
    fso_MoveFolder, fso_CopyFile, fso_CopyFolder, fso_CreateFolder, fso_CreateTextFile, fso_OpenTextFile,
    fso_GetStandardStream, fso_GetFileVersion,
};

static HRESULT STDMETHODCALLTYPE fso_info_query_interface(IProvideClassInfo *This, REFIID riid, void **object)
{
    return fso_query_interface(&OBJECT_OF(FSO, Info, This)->Fso, riid, object);
}

static ULONG STDMETHODCALLTYPE fso_info_add_ref(IProvideClassInfo *This)
{
    return fso_add_ref(&OBJECT_OF(FSO, Info, This)->Fso);
}

static ULONG STDMETHODCALLTYPE fso_info_release(IProvideClassInfo *This)
{
    return fso_release(&OBJECT_OF(FSO, Info, This)->Fso);
}

static HRESULT STDMETHODCALLTYPE fso_get_class_info(IProvideClassInfo *This, ITypeInfo **info)
{
    return info ? ScrClassInfo(&CLSID_FileSystemObject, info) : E_POINTER;
}

static const IProvideClassInfoVtbl g_fso_info_vtbl = {
    fso_info_query_interface, fso_info_add_ref, fso_info_release, fso_get_class_info,
};

HRESULT FsCreate(REFIID riid, void **object)
{
    FSO *fso = ScrAlloc(sizeof(*fso));
    if (!fso)
        return E_OUTOFMEMORY;
    fso->Fso.lpVtbl = &g_fso_vtbl;
    fso->Info.lpVtbl = &g_fso_info_vtbl;
    fso->Refs = 1;
    ScrLockDll();
    HRESULT hr = fso_query_interface(&fso->Fso, riid, object);
    fso_release(&fso->Fso);
    return hr;
}
