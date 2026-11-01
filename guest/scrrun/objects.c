/*
 * the objects every path gets: one per path and kind while it lives, shared by everyone who asks for it. This file also holds
 * the drives and their collection, and the dates and sizes the objects give back.
 */
#include "scrrun_private.h"

#define OBJECT_OF(type, member, pointer) ((type *)((char *)(pointer) - offsetof(type, member)))

MIME_DEFINE_GUID(CLSID_ScrDrive, 0xC7C3F5B1, 0x88A3, 0x11D0, 0xAB, 0xCB, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);
MIME_DEFINE_GUID(CLSID_ScrDrives, 0xC7C3F5B2, 0x88A3, 0x11D0, 0xAB, 0xCB, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);

static SRWLOCK g_cache_lock = SRWLOCK_INIT;
static POBJ *g_cache;

HRESULT ObjectGet(KIND kind, const WCHAR *path, SIZE_T size, POBJ **out, BOOL *created)
{
    AcquireSRWLockExclusive(&g_cache_lock);
    for (POBJ *p = g_cache; p; p = p->Next) {
        if (p->Kind == kind && !_wcsicmp(p->Path, path)) {
            p->Refs++;
            ReleaseSRWLockExclusive(&g_cache_lock);
            *out = p;
            *created = FALSE;
            return S_OK;
        }
    }
    POBJ *object = ScrAlloc(size);
    WCHAR *copy = ScrCopyZ(path);
    if (!object || !copy) {
        ScrFree(object);
        ScrFree(copy);
        ReleaseSRWLockExclusive(&g_cache_lock);
        return E_OUTOFMEMORY;
    }
    object->Kind = kind;
    object->Refs = 1;
    object->Path = copy;
    object->Next = g_cache;
    g_cache = object;
    ReleaseSRWLockExclusive(&g_cache_lock);
    ScrLockDll();
    *out = object;
    *created = TRUE;
    return S_OK;
}

ULONG ObjectAddRef(POBJ *object)
{
    AcquireSRWLockExclusive(&g_cache_lock);
    LONG refs = ++object->Refs;
    ReleaseSRWLockExclusive(&g_cache_lock);
    return (ULONG)refs;
}

ULONG ObjectRelease(POBJ *object)
{
    AcquireSRWLockExclusive(&g_cache_lock);
    LONG refs = --object->Refs;
    if (!refs) {
        for (POBJ **link = &g_cache; *link; link = &(*link)->Next) {
            if (*link == object) {
                *link = object->Next;
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_cache_lock);
    if (!refs) {
        ScrFree(object->Path);
        ScrFree(object);
        ScrUnlockDll();
    }
    return (ULONG)refs;
}

void ObjectSetPath(POBJ *object, const WCHAR *path)
{
    WCHAR *copy = ScrCopyZ(path);
    if (!copy)
        return;
    AcquireSRWLockExclusive(&g_cache_lock);
    WCHAR *old = object->Path;
    object->Path = copy;
    ReleaseSRWLockExclusive(&g_cache_lock);
    ScrFree(old);
}

HRESULT ObjectPathCopy(POBJ *object, WCHAR **out)
{
    AcquireSRWLockExclusive(&g_cache_lock);
    *out = ScrCopyZ(object->Path);
    ReleaseSRWLockExclusive(&g_cache_lock);
    return *out ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ keys, dates, sizes */

HRESULT KeyName(VARIANT key, BSTR *out)
{
    VARIANT value;
    VariantInit(&value);
    VARIANT *given = ScrDeref(&key);
    if (given->vt == VT_DISPATCH) {
        IDispatch *dispatch = given->pdispVal;
        DISPPARAMS none = {0};
        if (!dispatch ||
            FAILED(dispatch->lpVtbl->Invoke(dispatch, 0, &IID_NULL, 1024, DISPATCH_PROPERTYGET, &none, &value, nullptr, nullptr)))
            return E_INVALIDARG;
        given = &value;
    }
    HRESULT hr = E_INVALIDARG;
    if (given->vt == VT_BSTR && given->bstrVal && !PathScan(given->bstrVal, -1)) {
        *out = SysAllocString(given->bstrVal);
        hr = *out ? S_OK : E_OUTOFMEMORY;
    }
    VariantClear(&value);
    return hr;
}

/* the days from 1899-12-30 of a date of the calendar. */
static LONG days_from_date(LONG year, LONG month, LONG day)
{
    LONG y = month <= 2 ? year - 1 : year;
    LONG era = (y >= 0 ? y : y - 399) / 400;
    LONG yoe = y - era * 400;
    LONG doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    LONG doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468 + 25569;
}

HRESULT ConvertToDate(const FILETIME *time, DATE *date)
{
    if (!time->dwLowDateTime && !time->dwHighDateTime)
        return E_VB_INVALID;
    FILETIME local;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(time, &local) || !FileTimeToSystemTime(&local, &st))
        return E_INVALIDARG;
    if (st.wYear > 9999)
        return E_INVALIDARG;
    LONG days = days_from_date(st.wYear, st.wMonth, st.wDay);
    double fraction = (double)(int)(st.wSecond + 60 * (st.wMinute + 60 * st.wHour)) / 86400.0;
    *date = days < 0 ? (double)days - fraction : (double)days + fraction;
    return S_OK;
}

HRESULT ConvertToVariant(ULONGLONG value, VARIANT *out)
{
    if (value > 0x7FFFFFFF) {
        out->vt = VT_R8;
        out->dblVal = (double)(ULONG)(value >> 32) * 65536.0 * 65536.0 + (double)(ULONG)value;
    } else {
        out->vt = VT_I4;
        out->lVal = (LONG)value;
    }
    return S_OK;
}

/* ------------------------------------------------------------------ drives */

typedef struct DRIVE {
    POBJ Base;
    IDrive Drive;
    IProvideClassInfo Info;
    BOOL Unc;
} DRIVE;

static const IDriveVtbl g_drive_vtbl;
static const IProvideClassInfoVtbl g_drive_info_vtbl;

static DRIVE *drive_of(IDrive *This)
{
    return OBJECT_OF(DRIVE, Drive, This);
}

static HRESULT STDMETHODCALLTYPE drive_query_interface(IDrive *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    DRIVE *drive = drive_of(This);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IDrive))
        *object = &drive->Drive;
    else if (IsEqualIID(riid, &IID_IProvideClassInfo))
        *object = &drive->Info;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ObjectAddRef(&drive->Base);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE drive_add_ref(IDrive *This)
{
    return ObjectAddRef(&drive_of(This)->Base);
}

static ULONG STDMETHODCALLTYPE drive_release(IDrive *This)
{
    return ObjectRelease(&drive_of(This)->Base);
}

SCR_DISPATCH_METHODS(drive, IDrive, &IID_IDrive)

static HRESULT drive_check(DRIVE *drive)
{
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&drive->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = PathDriveExists(path);
        ScrFree(path);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE drive_get_Path(IDrive *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    DRIVE *drive = drive_of(This);
    HRESULT hr = drive_check(drive);
    if (FAILED(hr))
        return hr;
    WCHAR *path;
    hr = ObjectPathCopy(&drive->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = AllocateBstr(path, (int)wcslen(path) - 1, out);
        ScrFree(path);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE drive_get_DriveLetter(IDrive *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    DRIVE *drive = drive_of(This);
    HRESULT hr = drive_check(drive);
    if (FAILED(hr) || drive->Unc)
        return hr;
    WCHAR *path;
    hr = ObjectPathCopy(&drive->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = AllocateBstr(path, 1, out);
        ScrFree(path);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE drive_get_ShareName(IDrive *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    DRIVE *drive = drive_of(This);
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&drive->Base, &path);
    if (FAILED(hr))
        return hr;
    if (drive->Unc) {
        hr = PathFolderExists(path);
        if (SUCCEEDED(hr))
            hr = AllocateBstr(path, (int)wcslen(path) - 1, out);
        ScrFree(path);
        return hr;
    }
    UINT type = GetDriveTypeW(path);
    if (type == DRIVE_NO_ROOT_DIR) {
        ScrFree(path);
        return VBERR(68);
    }
    if (type == DRIVE_REMOTE) {
        WCHAR local[3] = {path[0], ':', 0};
        WCHAR share[MAX_PATH];
        DWORD size = MAX_PATH;
        DWORD error = WNetGetConnectionW(local, share, &size);
        if (!error)
            hr = AllocateBstr(share, -1, out);
        else if (error == ERROR_NOT_CONNECTED)
            hr = S_OK;
        else
            hr = error == ERROR_CONNECTION_UNAVAIL ? VBERR(68) : E_INVALIDARG;
    }
    ScrFree(path);
    return hr;
}

static HRESULT STDMETHODCALLTYPE drive_get_DriveType(IDrive *This, DriveTypeConst *out)
{
    if (!out)
        return E_POINTER;
    DRIVE *drive = drive_of(This);
    if (drive->Unc) {
        *out = Remote;
        return S_OK;
    }
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&drive->Base, &path);
    if (FAILED(hr))
        return hr;
    UINT type = GetDriveTypeW(path);
    ScrFree(path);
    switch (type) {
    case DRIVE_UNKNOWN:
        *out = UnknownType;
        return S_OK;
    case DRIVE_NO_ROOT_DIR:
        return VBERR(68);
    case DRIVE_REMOVABLE:
        *out = Removable;
        break;
    case DRIVE_FIXED:
        *out = Fixed;
        break;
    case DRIVE_REMOTE:
        *out = Remote;
        break;
    case DRIVE_CDROM:
        *out = CDRom;
        break;
    case DRIVE_RAMDISK:
        *out = RamDisk;
        break;
    default:
        *out = UnknownType;
        break;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE drive_get_RootFolder(IDrive *This, IFolder **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&drive_of(This)->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = FolderObjectGet(path, out);
        ScrFree(path);
    }
    return hr;
}

/* the sizes of a drive: what is free for the user, the total and what is free on the whole. */
static HRESULT disk_space(DRIVE *drive, ULONGLONG *available, ULONGLONG *total, ULONGLONG *free_bytes)
{
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&drive->Base, &path);
    if (FAILED(hr))
        return hr;
    ULARGE_INTEGER a = {0}, t = {0}, f = {0};
    if (GetDiskFreeSpaceExW(path, &a, &t, &f)) {
        *available = a.QuadPart;
        *total = t.QuadPart;
        *free_bytes = f.QuadPart;
        hr = S_OK;
    } else {
        hr = TranslateWin32Error(GetLastError());
    }
    ScrFree(path);
    return hr;
}

static HRESULT STDMETHODCALLTYPE drive_get_AvailableSpace(IDrive *This, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    out->vt = VT_EMPTY;
    ULONGLONG available, total, free_bytes;
    HRESULT hr = disk_space(drive_of(This), &available, &total, &free_bytes);
    return SUCCEEDED(hr) ? ConvertToVariant(available, out) : hr;
}

static HRESULT STDMETHODCALLTYPE drive_get_FreeSpace(IDrive *This, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    out->vt = VT_EMPTY;
    ULONGLONG available, total, free_bytes;
    HRESULT hr = disk_space(drive_of(This), &available, &total, &free_bytes);
    return SUCCEEDED(hr) ? ConvertToVariant(free_bytes, out) : hr;
}

static HRESULT STDMETHODCALLTYPE drive_get_TotalSize(IDrive *This, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    out->vt = VT_EMPTY;
    ULONGLONG available, total, free_bytes;
    HRESULT hr = disk_space(drive_of(This), &available, &total, &free_bytes);
    return SUCCEEDED(hr) ? ConvertToVariant(total, out) : hr;
}

/* the volume calls fail quietly when the drive has no medium. */
static HRESULT volume_error(DRIVE *drive)
{
    return drive->Unc ? E_VB_PATHNOTFOUND : E_VB_DISKNOTREADY;
}

static HRESULT STDMETHODCALLTYPE drive_get_VolumeName(IDrive *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    DRIVE *drive = drive_of(This);
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&drive->Base, &path);
    if (FAILED(hr))
        return hr;
    WCHAR name[MAX_PATH];
    UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    if (GetVolumeInformationW(path, name, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0))
        hr = AllocateBstr(name, -1, out);
    else
        hr = GetLastError() ? volume_error(drive) : E_FAIL;
    SetErrorMode(mode);
    ScrFree(path);
    return hr;
}

static HRESULT STDMETHODCALLTYPE drive_put_VolumeName(IDrive *This, BSTR name)
{
    DRIVE *drive = drive_of(This);
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&drive->Base, &path);
    if (FAILED(hr))
        return hr;
    UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    hr = SetVolumeLabelW(path, name ? name : L"") ? S_OK : TranslateWin32Error(GetLastError());
    SetErrorMode(mode);
    ScrFree(path);
    return hr;
}

static HRESULT STDMETHODCALLTYPE drive_get_FileSystem(IDrive *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    DRIVE *drive = drive_of(This);
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&drive->Base, &path);
    if (FAILED(hr))
        return hr;
    WCHAR name[MAX_PATH];
    UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    if (GetVolumeInformationW(path, nullptr, 0, nullptr, nullptr, nullptr, name, MAX_PATH))
        hr = AllocateBstr(name, -1, out);
    else
        hr = GetLastError() ? volume_error(drive) : E_FAIL;
    SetErrorMode(mode);
    ScrFree(path);
    return hr;
}

static HRESULT STDMETHODCALLTYPE drive_get_SerialNumber(IDrive *This, LONG *out)
{
    if (!out)
        return E_POINTER;
    DRIVE *drive = drive_of(This);
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&drive->Base, &path);
    if (FAILED(hr))
        return hr;
    UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    DWORD serial = 0;
    hr = GetVolumeInformationW(path, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0) ? S_OK : volume_error(drive);
    *out = (LONG)serial;
    SetErrorMode(mode);
    ScrFree(path);
    return hr;
}

static HRESULT STDMETHODCALLTYPE drive_get_IsReady(IDrive *This, VARIANT_BOOL *out)
{
    if (!out)
        return E_POINTER;
    *out = VARIANT_TRUE;
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&drive_of(This)->Base, &path);
    if (FAILED(hr))
        return hr;
    UINT mode = SetErrorMode(SEM_FAILCRITICALERRORS);
    if (PathAttributes(path) == INVALID_FILE_ATTRIBUTES)
        *out = VARIANT_FALSE;
    SetErrorMode(mode);
    ScrFree(path);
    return S_OK;
}

static const IDriveVtbl g_drive_vtbl = {
    drive_query_interface, drive_add_ref, drive_release, drive_GetTypeInfoCount, drive_GetTypeInfo,
    drive_GetIDsOfNames, drive_Invoke, drive_get_Path, drive_get_DriveLetter, drive_get_ShareName,
    drive_get_DriveType, drive_get_RootFolder, drive_get_AvailableSpace, drive_get_FreeSpace, drive_get_TotalSize,
    drive_get_VolumeName, drive_put_VolumeName, drive_get_FileSystem, drive_get_SerialNumber, drive_get_IsReady,
};

static HRESULT STDMETHODCALLTYPE drive_info_query_interface(IProvideClassInfo *This, REFIID riid, void **object)
{
    return drive_query_interface(&OBJECT_OF(DRIVE, Info, This)->Drive, riid, object);
}

static ULONG STDMETHODCALLTYPE drive_info_add_ref(IProvideClassInfo *This)
{
    return ObjectAddRef(&OBJECT_OF(DRIVE, Info, This)->Base);
}

static ULONG STDMETHODCALLTYPE drive_info_release(IProvideClassInfo *This)
{
    return ObjectRelease(&OBJECT_OF(DRIVE, Info, This)->Base);
}

static HRESULT STDMETHODCALLTYPE drive_get_class_info(IProvideClassInfo *This, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    return ScrClassInfo(&CLSID_ScrDrive, info);
}

static const IProvideClassInfoVtbl g_drive_info_vtbl = {
    drive_info_query_interface, drive_info_add_ref, drive_info_release, drive_get_class_info,
};

HRESULT DriveObjectGet(const WCHAR *path, IDrive **out)
{
    HRESULT hr = PathDriveExists(path);
    if (FAILED(hr))
        return hr;
    POBJ *base;
    BOOL created;
    hr = ObjectGet(KIND_DRIVE, path, sizeof(DRIVE), &base, &created);
    if (FAILED(hr))
        return hr;
    DRIVE *drive = (DRIVE *)base;
    if (created) {
        drive->Drive.lpVtbl = &g_drive_vtbl;
        drive->Info.lpVtbl = &g_drive_info_vtbl;
        drive->Unc = PathIsUnc(path);
    }
    *out = &drive->Drive;
    return S_OK;
}

HRESULT FsGetDrive(const WCHAR *spec, IDrive **drive)
{
    WCHAR *canonical;
    HRESULT hr = PathCanonicalDrive(spec, &canonical);
    if (SUCCEEDED(hr)) {
        hr = DriveObjectGet(canonical, drive);
        ScrFree(canonical);
    }
    return hr;
}

/* ------------------------------------------------------------------ the drives collection */

typedef struct DRIVES {
    IDriveCollection Drives;
    IProvideClassInfo Info;
    LONG Refs;
} DRIVES;

typedef struct DRIVE_ENUM {
    IEnumVARIANT Enum;
    LONG Refs;
    int Next;
} DRIVE_ENUM;

static const IDriveCollectionVtbl g_drives_vtbl;
static const IProvideClassInfoVtbl g_drives_info_vtbl;
static const IEnumVARIANTVtbl g_drive_enum_vtbl;

static DRIVES *drives_of(IDriveCollection *This)
{
    return OBJECT_OF(DRIVES, Drives, This);
}

static HRESULT STDMETHODCALLTYPE drives_query_interface(IDriveCollection *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    DRIVES *drives = drives_of(This);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IDriveCollection))
        *object = &drives->Drives;
    else if (IsEqualIID(riid, &IID_IProvideClassInfo))
        *object = &drives->Info;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&drives->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE drives_add_ref(IDriveCollection *This)
{
    return (ULONG)InterlockedIncrement(&drives_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE drives_release(IDriveCollection *This)
{
    DRIVES *drives = drives_of(This);
    LONG refs = InterlockedDecrement(&drives->Refs);
    if (!refs) {
        ScrFree(drives);
        ScrUnlockDll();
    }
    return (ULONG)refs;
}

SCR_DISPATCH_METHODS(drives, IDriveCollection, &IID_IDriveCollection)

static HRESULT STDMETHODCALLTYPE drives_get_Item(IDriveCollection *This, VARIANT key, IDrive **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    VARIANT value;
    VariantInit(&value);
    VARIANT *given = ScrDeref(&key);
    if (given->vt == VT_DISPATCH) {
        IDispatch *dispatch = given->pdispVal;
        DISPPARAMS none = {0};
        if (!dispatch ||
            FAILED(dispatch->lpVtbl->Invoke(dispatch, 0, &IID_NULL, 1024, DISPATCH_PROPERTYGET, &none, &value, nullptr, nullptr)))
            return E_INVALIDARG;
        given = &value;
    }
    HRESULT hr = E_INVALIDARG;
    if (given->vt == VT_BSTR && given->bstrVal && !PathIsUnc(given->bstrVal) && SysStringLen(given->bstrVal) <= 3)
        hr = FsGetDrive(given->bstrVal, out);
    VariantClear(&value);
    return hr;
}

static HRESULT STDMETHODCALLTYPE drives_get_NewEnum(IDriveCollection *This, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    DRIVE_ENUM *drive_enum = ScrAlloc(sizeof(*drive_enum));
    if (!drive_enum)
        return E_OUTOFMEMORY;
    drive_enum->Enum.lpVtbl = &g_drive_enum_vtbl;
    drive_enum->Refs = 1;
    ScrLockDll();
    *out = (IUnknown *)&drive_enum->Enum;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE drives_get_Count(IDriveCollection *This, LONG *count)
{
    if (!count)
        return E_POINTER;
    DWORD bits = GetLogicalDrives();
    LONG n = 0;
    for (; bits; bits &= bits - 1)
        n++;
    *count = n;
    return S_OK;
}

static const IDriveCollectionVtbl g_drives_vtbl = {
    drives_query_interface, drives_add_ref, drives_release, drives_GetTypeInfoCount, drives_GetTypeInfo,
    drives_GetIDsOfNames, drives_Invoke, drives_get_Item, drives_get_NewEnum, drives_get_Count,
};

static HRESULT STDMETHODCALLTYPE drives_info_query_interface(IProvideClassInfo *This, REFIID riid, void **object)
{
    return drives_query_interface(&OBJECT_OF(DRIVES, Info, This)->Drives, riid, object);
}

static ULONG STDMETHODCALLTYPE drives_info_add_ref(IProvideClassInfo *This)
{
    return drives_add_ref(&OBJECT_OF(DRIVES, Info, This)->Drives);
}

static ULONG STDMETHODCALLTYPE drives_info_release(IProvideClassInfo *This)
{
    return drives_release(&OBJECT_OF(DRIVES, Info, This)->Drives);
}

static HRESULT STDMETHODCALLTYPE drives_get_class_info(IProvideClassInfo *This, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    return ScrClassInfo(&CLSID_ScrDrives, info);
}

static const IProvideClassInfoVtbl g_drives_info_vtbl = {
    drives_info_query_interface, drives_info_add_ref, drives_info_release, drives_get_class_info,
};

HRESULT DriveCollectionCreate(IDriveCollection **out)
{
    DRIVES *drives = ScrAlloc(sizeof(*drives));
    if (!drives)
        return E_OUTOFMEMORY;
    drives->Drives.lpVtbl = &g_drives_vtbl;
    drives->Info.lpVtbl = &g_drives_info_vtbl;
    drives->Refs = 1;
    ScrLockDll();
    *out = &drives->Drives;
    return S_OK;
}

/* ------------------------------------------------------------------ the enumerator of drives */

static DRIVE_ENUM *drive_enum_of(IEnumVARIANT *This)
{
    return (DRIVE_ENUM *)This;
}

static HRESULT STDMETHODCALLTYPE drive_enum_query_interface(IEnumVARIANT *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumVARIANT)) {
        *object = This;
        InterlockedIncrement(&drive_enum_of(This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE drive_enum_add_ref(IEnumVARIANT *This)
{
    return (ULONG)InterlockedIncrement(&drive_enum_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE drive_enum_release(IEnumVARIANT *This)
{
    DRIVE_ENUM *drive_enum = drive_enum_of(This);
    LONG refs = InterlockedDecrement(&drive_enum->Refs);
    if (!refs) {
        ScrFree(drive_enum);
        ScrUnlockDll();
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE drive_enum_next(IEnumVARIANT *This, ULONG count, VARIANT *values, ULONG *fetched)
{
    if (!values)
        return E_POINTER;
    DRIVE_ENUM *drive_enum = drive_enum_of(This);
    DWORD bits = GetLogicalDrives();
    ULONG got = 0;
    while (got < count && drive_enum->Next < 26) {
        if (bits & (1u << drive_enum->Next)) {
            WCHAR name[4] = {(WCHAR)('A' + drive_enum->Next), ':', '\\', 0};
            IDrive *drive;
            if (SUCCEEDED(DriveObjectGet(name, &drive))) {
                values[got].vt = VT_DISPATCH;
                values[got].pdispVal = (IDispatch *)drive;
                got++;
            }
        }
        drive_enum->Next++;
    }
    if (fetched)
        *fetched = got;
    return got != count ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE drive_enum_skip(IEnumVARIANT *This, ULONG count)
{
    DRIVE_ENUM *drive_enum = drive_enum_of(This);
    DWORD bits = GetLogicalDrives();
    while (count && drive_enum->Next < 26) {
        if (bits & (1u << drive_enum->Next))
            count--;
        drive_enum->Next++;
    }
    return count ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE drive_enum_reset(IEnumVARIANT *This)
{
    drive_enum_of(This)->Next = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE drive_enum_clone(IEnumVARIANT *This, IEnumVARIANT **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    DRIVE_ENUM *copy = ScrAlloc(sizeof(*copy));
    if (!copy)
        return E_OUTOFMEMORY;
    copy->Enum.lpVtbl = &g_drive_enum_vtbl;
    copy->Refs = 1;
    copy->Next = drive_enum_of(This)->Next;
    ScrLockDll();
    *out = &copy->Enum;
    return S_OK;
}

static const IEnumVARIANTVtbl g_drive_enum_vtbl = {
    drive_enum_query_interface, drive_enum_add_ref, drive_enum_release, drive_enum_next, drive_enum_skip,
    drive_enum_reset, drive_enum_clone,
};

HRESULT FolderDrive(const WCHAR *path, IDrive **drive)
{
    int length = 0;
    const WCHAR *part = PathDrivePart(path, &length);
    if (!part)
        return E_INVALIDARG;
    WCHAR *name = ScrCopy(part, (SIZE_T)length + 1);
    if (!name)
        return E_OUTOFMEMORY;
    name[length] = '\\';
    HRESULT hr = DriveObjectGet(name, drive);
    ScrFree(name);
    return hr;
}
