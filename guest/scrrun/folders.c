/*
 * the folders and files of the file system object, the collections of them in a folder and the enumerators of those. A folder
 * or file checks that its path is still there before it answers, and a collection walks the folder every time it is asked.
 */
#include "scrrun_private.h"
#include <mime/win32/shellapi.h>

#define OBJECT_OF(type, member, pointer) ((type *)((char *)(pointer) - offsetof(type, member)))

MIME_DEFINE_GUID(CLSID_ScrFolder, 0xC7C3F5B3, 0x88A3, 0x11D0, 0xAB, 0xCB, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);
MIME_DEFINE_GUID(CLSID_ScrFolders, 0xC7C3F5B4, 0x88A3, 0x11D0, 0xAB, 0xCB, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);
MIME_DEFINE_GUID(CLSID_ScrFile, 0xC7C3F5B5, 0x88A3, 0x11D0, 0xAB, 0xCB, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);
MIME_DEFINE_GUID(CLSID_ScrFiles, 0xC7C3F5B6, 0x88A3, 0x11D0, 0xAB, 0xCB, 0x00, 0xA0, 0xC9, 0x0F, 0xFF, 0xC0);

typedef struct COLLECTION COLLECTION;

typedef struct FOLDER {
    POBJ Base;
    IFolder Folder;
    IProvideClassInfo Info;
    COLLECTION *Files, *Folders;
} FOLDER;

typedef struct FILE_OBJECT {
    POBJ Base;
    IFile File;
    IProvideClassInfo Info;
} FILE_OBJECT;

/* the collection of the files or of the folders of a folder, which keeps the folder alive. */
struct COLLECTION {
    const void *Vtbl;
    IProvideClassInfo Info;
    LONG Refs;
    FOLDER *Parent;
    BOOL IsFiles;
};

typedef struct PATH_ENUM {
    IEnumVARIANT Enum;
    LONG Refs;
    COLLECTION *Collection;
    HANDLE Find;
    BOOL Done;
} PATH_ENUM;

static const IFolderVtbl g_folder_vtbl;
static const IFileVtbl g_file_vtbl;
static const IProvideClassInfoVtbl g_folder_info_vtbl, g_file_info_vtbl, g_folders_info_vtbl, g_files_info_vtbl;
static const IFolderCollectionVtbl g_folders_vtbl;
static const IFileCollectionVtbl g_files_vtbl;
static const IEnumVARIANTVtbl g_path_enum_vtbl;

static SRWLOCK g_collection_lock = SRWLOCK_INIT;

/* an error about a missing file as a folder gives it, a missing path. */
static HRESULT folder_error(HRESULT hr)
{
    return hr == E_VB_FILENOTFOUND ? E_VB_PATHNOTFOUND : hr;
}

/* ------------------------------------------------------------------ getting the objects */

HRESULT FolderObjectGet(const WCHAR *path, IFolder **out)
{
    HRESULT hr = PathFolderExists(path);
    if (FAILED(hr))
        return hr;
    POBJ *base;
    BOOL created;
    hr = ObjectGet(KIND_FOLDER, path, sizeof(FOLDER), &base, &created);
    if (FAILED(hr))
        return hr;
    FOLDER *folder = (FOLDER *)base;
    if (created) {
        folder->Folder.lpVtbl = &g_folder_vtbl;
        folder->Info.lpVtbl = &g_folder_info_vtbl;
        folder->Base.Root = PathIsRoot(path);
    }
    *out = &folder->Folder;
    return S_OK;
}

HRESULT FileObjectGet(const WCHAR *path, IFile **out)
{
    HRESULT hr = PathFileExists(path);
    if (FAILED(hr))
        return hr;
    POBJ *base;
    BOOL created;
    hr = ObjectGet(KIND_FILE, path, sizeof(FILE_OBJECT), &base, &created);
    if (FAILED(hr))
        return hr;
    FILE_OBJECT *file = (FILE_OBJECT *)base;
    if (created) {
        file->File.lpVtbl = &g_file_vtbl;
        file->Info.lpVtbl = &g_file_info_vtbl;
    }
    *out = &file->File;
    return S_OK;
}

HRESULT FsGetFolder(const WCHAR *path, IFolder **folder)
{
    WCHAR *absolute;
    HRESULT hr = PathAbsolute(path, FALSE, &absolute);
    if (SUCCEEDED(hr)) {
        hr = FolderObjectGet(absolute, folder);
        ScrFree(absolute);
    }
    return folder_error(hr);
}

HRESULT FsGetFile(const WCHAR *path, IFile **file)
{
    WCHAR *absolute;
    HRESULT hr = PathAbsolute(path, FALSE, &absolute);
    if (SUCCEEDED(hr)) {
        hr = FileObjectGet(absolute, file);
        ScrFree(absolute);
    }
    return hr == E_INVALIDARG ? E_VB_FILENOTFOUND : hr;
}

HRESULT FsCreateFolder(const WCHAR *path, IFolder **folder)
{
    int length = 0;
    if (PathIsUnc(path) && !PathDrivePart(path, &length))
        return E_VB_BADFILENAME;
    if (!CreateDirectoryW(path, nullptr))
        return TranslateWin32Error(GetLastError());
    return FsGetFolder(path, folder);
}

HRESULT FolderContaining(const WCHAR *path, IFolder **folder)
{
    int length = 0;
    const WCHAR *part = PathParentPart(path, &length);
    if (!part)
        return S_OK;
    WCHAR *parent = ScrCopy(part, (SIZE_T)length);
    if (!parent)
        return E_OUTOFMEMORY;
    HRESULT hr = FolderObjectGet(parent, folder);
    ScrFree(parent);
    return hr;
}

HRESULT FileTypeName(const WCHAR *path, WCHAR **name)
{
    SHFILEINFOW info;
    memset(&info, 0, sizeof(info));
    SHGetFileInfoW(path, 0, &info, sizeof(info), SHGFI_TYPENAME);
    WCHAR text[96];
    if (info.szTypeName[0]) {
        SIZE_T n = 0;
        for (; n < 80 && info.szTypeName[n]; n++)
            text[n] = info.szTypeName[n];
        text[n] = 0;
    } else {
        int length = 0;
        const WCHAR *extension = PathExtensionPart(path, &length);
        SIZE_T n = 0;
        if (extension) {
            if (length > 75)
                length = 75;
            for (; n < (SIZE_T)length; n++)
                text[n] = towupper(extension[n]);
            if (n)
                text[n++] = ' ';
        }
        memcpy(text + n, L"File", 5 * sizeof(WCHAR));
    }
    *name = ScrCopyZ(text);
    return *name ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ what folders and files share */

/* the copy of the path of an object in the form the file system keeps its names, with the case of the file system. */
static HRESULT canonical_text(POBJ *object, BSTR *out)
{
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(object, &path);
    if (FAILED(hr))
        return hr;
    PathMakeCanonical(path, wcslen(path) + 1, FALSE);
    hr = AllocateBstr(path, -1, out);
    ScrFree(path);
    return hr;
}

static HRESULT canonical_name(POBJ *object, BSTR *out)
{
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(object, &path);
    if (FAILED(hr))
        return hr;
    PathMakeCanonical(path, wcslen(path) + 1, FALSE);
    int length = 0;
    const WCHAR *name = PathNamePart(path, &length);
    if (name)
        hr = AllocateBstr(name, length, out);
    ScrFree(path);
    return hr;
}

static HRESULT short_text(POBJ *object, BOOL name_only, BSTR *out)
{
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(object, &path);
    if (FAILED(hr))
        return hr;
    WCHAR *short_path;
    hr = PathCanonicalShort(path, &short_path);
    ScrFree(path);
    if (FAILED(hr))
        return hr;
    if (name_only) {
        int length = 0;
        const WCHAR *name = PathNamePart(short_path, &length);
        hr = name ? AllocateBstr(name, length, out) : S_OK;
    } else {
        hr = AllocateBstr(short_path, -1, out);
    }
    ScrFree(short_path);
    return hr;
}

static HRESULT object_attributes(POBJ *object, LONG *out)
{
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(object, &path);
    if (FAILED(hr))
        return hr;
    DWORD attributes = PathAttributes(path);
    ScrFree(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return TranslateWin32Error(GetLastError());
    *out = (LONG)(attributes & 0xC37);
    return S_OK;
}

static HRESULT object_put_attributes(POBJ *object, LONG attributes)
{
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(object, &path);
    if (FAILED(hr))
        return hr;
    DWORD current = PathAttributes(path);
    if (current == INVALID_FILE_ATTRIBUTES) {
        hr = TranslateWin32Error(GetLastError());
    } else {
        DWORD value = ((DWORD)attributes & 0x27) ^ (current & 0xFFFFFF58);
        if (!value)
            value = FILE_ATTRIBUTE_NORMAL;
        hr = SetFileAttributesW(path, value) ? S_OK : TranslateWin32Error(GetLastError());
    }
    ScrFree(path);
    return hr;
}

enum { DATE_CREATED, DATE_MODIFIED, DATE_ACCESSED };

static HRESULT object_date(POBJ *object, int which, DATE *out)
{
    if (!out)
        return E_POINTER;
    if (object->Root)
        return E_VB_INVALID;
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(object, &path);
    if (FAILED(hr))
        return hr;
    WIN32_FIND_DATAW find;
    memset(&find, 0, sizeof(find));
    HANDLE handle = FindFirstFileW(path, &find);
    ScrFree(path);
    if (handle == INVALID_HANDLE_VALUE)
        return E_VB_FILENOTFOUND;
    FindClose(handle);
    const FILETIME *time = which == DATE_CREATED ? &find.ftCreationTime
                           : which == DATE_MODIFIED ? &find.ftLastWriteTime
                                                    : &find.ftLastAccessTime;
    return ConvertToDate(time, out);
}

/* the new name of an object, which cannot hold separators or wildcards. */
static HRESULT object_put_name(POBJ *object, BSTR name)
{
    if (!name || !SysStringLen(name) || (PathScan(name, -1) & (SCAN_SEPARATOR | SCAN_WILDCARD)))
        return E_INVALIDARG;
    if (object->Root)
        return E_VB_INVALID;
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(object, &path);
    if (FAILED(hr))
        return hr;
    WCHAR *renamed;
    hr = FsRename(path, name, &renamed);
    ScrFree(path);
    if (SUCCEEDED(hr)) {
        ObjectSetPath(object, renamed);
        ScrFree(renamed);
    }
    return hr;
}

/* ------------------------------------------------------------------ folders */

static FOLDER *folder_of(IFolder *This)
{
    return OBJECT_OF(FOLDER, Folder, This);
}

static HRESULT folder_check(FOLDER *folder)
{
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&folder->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = PathFolderExists(path);
        ScrFree(path);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE folder_query_interface(IFolder *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    FOLDER *folder = folder_of(This);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IFolder))
        *object = &folder->Folder;
    else if (IsEqualIID(riid, &IID_IProvideClassInfo))
        *object = &folder->Info;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ObjectAddRef(&folder->Base);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE folder_add_ref(IFolder *This)
{
    return ObjectAddRef(&folder_of(This)->Base);
}

static ULONG STDMETHODCALLTYPE folder_release(IFolder *This)
{
    return ObjectRelease(&folder_of(This)->Base);
}

SCR_DISPATCH_METHODS(folder, IFolder, &IID_IFolder)

static HRESULT STDMETHODCALLTYPE folder_get_Path(IFolder *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FOLDER *folder = folder_of(This);
    HRESULT hr = folder_check(folder);
    if (SUCCEEDED(hr))
        hr = canonical_text(&folder->Base, out);
    return folder_error(hr);
}

static HRESULT STDMETHODCALLTYPE folder_get_Name(IFolder *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FOLDER *folder = folder_of(This);
    HRESULT hr = folder_check(folder);
    if (SUCCEEDED(hr))
        hr = canonical_name(&folder->Base, out);
    return folder_error(hr);
}

static HRESULT STDMETHODCALLTYPE folder_put_Name(IFolder *This, BSTR name)
{
    return folder_error(object_put_name(&folder_of(This)->Base, name));
}

static HRESULT STDMETHODCALLTYPE folder_get_ShortPath(IFolder *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FOLDER *folder = folder_of(This);
    HRESULT hr = folder_check(folder);
    if (SUCCEEDED(hr))
        hr = short_text(&folder->Base, FALSE, out);
    return folder_error(hr);
}

static HRESULT STDMETHODCALLTYPE folder_get_ShortName(IFolder *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FOLDER *folder = folder_of(This);
    HRESULT hr = folder_check(folder);
    if (SUCCEEDED(hr))
        hr = short_text(&folder->Base, TRUE, out);
    return folder_error(hr);
}

static HRESULT STDMETHODCALLTYPE folder_get_Drive(IFolder *This, IDrive **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FOLDER *folder = folder_of(This);
    HRESULT hr = folder_check(folder);
    if (FAILED(hr))
        return hr;
    WCHAR *path;
    hr = ObjectPathCopy(&folder->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = FolderDrive(path, out);
        ScrFree(path);
    }
    return folder_error(hr);
}

static HRESULT STDMETHODCALLTYPE folder_get_ParentFolder(IFolder *This, IFolder **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FOLDER *folder = folder_of(This);
    HRESULT hr = folder_check(folder);
    if (FAILED(hr))
        return hr;
    WCHAR *path;
    hr = ObjectPathCopy(&folder->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = FolderContaining(path, out);
        ScrFree(path);
    }
    return folder_error(hr);
}

static HRESULT STDMETHODCALLTYPE folder_get_Attributes(IFolder *This, LONG *out)
{
    if (!out)
        return E_POINTER;
    return folder_error(object_attributes(&folder_of(This)->Base, out));
}

static HRESULT STDMETHODCALLTYPE folder_put_Attributes(IFolder *This, LONG attributes)
{
    return folder_error(object_put_attributes(&folder_of(This)->Base, attributes));
}

static HRESULT STDMETHODCALLTYPE folder_get_DateCreated(IFolder *This, DATE *out)
{
    return folder_error(object_date(&folder_of(This)->Base, DATE_CREATED, out));
}

static HRESULT STDMETHODCALLTYPE folder_get_DateLastModified(IFolder *This, DATE *out)
{
    return folder_error(object_date(&folder_of(This)->Base, DATE_MODIFIED, out));
}

static HRESULT STDMETHODCALLTYPE folder_get_DateLastAccessed(IFolder *This, DATE *out)
{
    return folder_error(object_date(&folder_of(This)->Base, DATE_ACCESSED, out));
}

static HRESULT STDMETHODCALLTYPE folder_get_Type(IFolder *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FOLDER *folder = folder_of(This);
    HRESULT hr = folder_check(folder);
    if (FAILED(hr))
        return folder_error(hr);
    WCHAR *path;
    hr = ObjectPathCopy(&folder->Base, &path);
    if (SUCCEEDED(hr)) {
        WCHAR *name;
        hr = FileTypeName(path, &name);
        if (SUCCEEDED(hr)) {
            hr = AllocateBstr(name, -1, out);
            ScrFree(name);
        }
        ScrFree(path);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE folder_Delete(IFolder *This, VARIANT_BOOL force)
{
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&folder_of(This)->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = FsDeleteFolder(path, force != VARIANT_FALSE);
        ScrFree(path);
    }
    return folder_error(hr);
}

static HRESULT STDMETHODCALLTYPE folder_Copy(IFolder *This, BSTR destination, VARIANT_BOOL overwrite)
{
    if (!SysStringLen(destination))
        return E_INVALIDARG;
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&folder_of(This)->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = FsRelocate(path, destination, overwrite != VARIANT_FALSE, TRUE, FALSE, nullptr);
        ScrFree(path);
    }
    return folder_error(hr);
}

static HRESULT STDMETHODCALLTYPE folder_Move(IFolder *This, BSTR destination)
{
    if (!SysStringLen(destination))
        return E_INVALIDARG;
    FOLDER *folder = folder_of(This);
    if (folder->Base.Root)
        return E_VB_INVALID;
    WCHAR *path, *final_path = nullptr;
    HRESULT hr = ObjectPathCopy(&folder->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = FsRelocate(path, destination, FALSE, TRUE, TRUE, &final_path);
        ScrFree(path);
    }
    if (SUCCEEDED(hr) && final_path)
        ObjectSetPath(&folder->Base, final_path);
    ScrFree(final_path);
    return folder_error(hr);
}

static HRESULT STDMETHODCALLTYPE folder_get_IsRootFolder(IFolder *This, VARIANT_BOOL *out)
{
    if (!out)
        return E_POINTER;
    FOLDER *folder = folder_of(This);
    HRESULT hr = folder_check(folder);
    if (SUCCEEDED(hr))
        *out = folder->Base.Root ? VARIANT_TRUE : VARIANT_FALSE;
    return hr;
}

static HRESULT STDMETHODCALLTYPE folder_get_Size(IFolder *This, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    out->vt = VT_EMPTY;
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&folder_of(This)->Base, &path);
    if (FAILED(hr))
        return hr;
    ULONGLONG size;
    hr = FsFolderSize(path, &size);
    ScrFree(path);
    if (SUCCEEDED(hr))
        hr = ConvertToVariant(size, out);
    return hr;
}

static HRESULT collection_create(FOLDER *folder, BOOL files, COLLECTION **out);

static HRESULT folder_collection(FOLDER *folder, BOOL files, void **out)
{
    HRESULT hr = folder_check(folder);
    if (FAILED(hr))
        return hr;
    COLLECTION **slot = files ? &folder->Files : &folder->Folders;
    AcquireSRWLockExclusive(&g_collection_lock);
    COLLECTION *collection = *slot;
    if (collection) {
        collection->Refs++;
        ReleaseSRWLockExclusive(&g_collection_lock);
        *out = collection;
        return S_OK;
    }
    ReleaseSRWLockExclusive(&g_collection_lock);
    hr = collection_create(folder, files, &collection);
    if (FAILED(hr))
        return hr;
    AcquireSRWLockExclusive(&g_collection_lock);
    if (*slot) {
        /* another thread made one meanwhile. */
        COLLECTION *other = *slot;
        other->Refs++;
        ReleaseSRWLockExclusive(&g_collection_lock);
        ((IUnknown *)collection)->lpVtbl->Release((IUnknown *)collection);
        *out = other;
        return S_OK;
    }
    *slot = collection;
    ReleaseSRWLockExclusive(&g_collection_lock);
    *out = collection;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE folder_get_SubFolders(IFolder *This, IFolderCollection **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return folder_collection(folder_of(This), FALSE, (void **)out);
}

static HRESULT STDMETHODCALLTYPE folder_get_Files(IFolder *This, IFileCollection **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return folder_collection(folder_of(This), TRUE, (void **)out);
}

static HRESULT STDMETHODCALLTYPE folder_CreateTextFile(IFolder *This, BSTR name, VARIANT_BOOL overwrite,
                                                       VARIANT_BOOL unicode, ITextStream **stream)
{
    if (!stream)
        return E_POINTER;
    *stream = nullptr;
    if (!SysStringLen(name))
        return E_INVALIDARG;
    WCHAR *path, *full;
    HRESULT hr = ObjectPathCopy(&folder_of(This)->Base, &path);
    if (FAILED(hr))
        return hr;
    hr = PathBuild(path, name, &full);
    ScrFree(path);
    if (FAILED(hr))
        return hr;
    hr = TextStreamFromFile(full, GENERIC_WRITE, FILE_SHARE_READ, overwrite != VARIANT_FALSE ? CREATE_ALWAYS : CREATE_NEW,
                            ForWriting, unicode != VARIANT_FALSE ? TristateTrue : TristateFalse, stream);
    ScrFree(full);
    return hr;
}

static const IFolderVtbl g_folder_vtbl = {
    folder_query_interface, folder_add_ref, folder_release, folder_GetTypeInfoCount, folder_GetTypeInfo,
    folder_GetIDsOfNames, folder_Invoke, folder_get_Path, folder_get_Name, folder_put_Name, folder_get_ShortPath,
    folder_get_ShortName, folder_get_Drive, folder_get_ParentFolder, folder_get_Attributes, folder_put_Attributes,
    folder_get_DateCreated, folder_get_DateLastModified, folder_get_DateLastAccessed, folder_get_Type, folder_Delete,
    folder_Copy, folder_Move, folder_get_IsRootFolder, folder_get_Size, folder_get_SubFolders, folder_get_Files,
    folder_CreateTextFile,
};

static HRESULT STDMETHODCALLTYPE folder_info_query_interface(IProvideClassInfo *This, REFIID riid, void **object)
{
    return folder_query_interface(&OBJECT_OF(FOLDER, Info, This)->Folder, riid, object);
}

static ULONG STDMETHODCALLTYPE folder_info_add_ref(IProvideClassInfo *This)
{
    return ObjectAddRef(&OBJECT_OF(FOLDER, Info, This)->Base);
}

static ULONG STDMETHODCALLTYPE folder_info_release(IProvideClassInfo *This)
{
    return ObjectRelease(&OBJECT_OF(FOLDER, Info, This)->Base);
}

static HRESULT STDMETHODCALLTYPE folder_get_class_info(IProvideClassInfo *This, ITypeInfo **info)
{
    return info ? ScrClassInfo(&CLSID_ScrFolder, info) : E_POINTER;
}

static const IProvideClassInfoVtbl g_folder_info_vtbl = {
    folder_info_query_interface, folder_info_add_ref, folder_info_release, folder_get_class_info,
};

/* ------------------------------------------------------------------ files */

static FILE_OBJECT *file_of(IFile *This)
{
    return OBJECT_OF(FILE_OBJECT, File, This);
}

static HRESULT file_check(FILE_OBJECT *file)
{
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&file->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = PathFileExists(path);
        ScrFree(path);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE file_query_interface(IFile *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    FILE_OBJECT *file = file_of(This);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IFile))
        *object = &file->File;
    else if (IsEqualIID(riid, &IID_IProvideClassInfo))
        *object = &file->Info;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ObjectAddRef(&file->Base);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE file_add_ref(IFile *This)
{
    return ObjectAddRef(&file_of(This)->Base);
}

static ULONG STDMETHODCALLTYPE file_release(IFile *This)
{
    return ObjectRelease(&file_of(This)->Base);
}

SCR_DISPATCH_METHODS(file, IFile, &IID_IFile)

static HRESULT STDMETHODCALLTYPE file_get_Path(IFile *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FILE_OBJECT *file = file_of(This);
    HRESULT hr = file_check(file);
    return SUCCEEDED(hr) ? canonical_text(&file->Base, out) : hr;
}

static HRESULT STDMETHODCALLTYPE file_get_Name(IFile *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FILE_OBJECT *file = file_of(This);
    HRESULT hr = file_check(file);
    return SUCCEEDED(hr) ? canonical_name(&file->Base, out) : hr;
}

static HRESULT STDMETHODCALLTYPE file_put_Name(IFile *This, BSTR name)
{
    return object_put_name(&file_of(This)->Base, name);
}

static HRESULT STDMETHODCALLTYPE file_get_ShortPath(IFile *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FILE_OBJECT *file = file_of(This);
    HRESULT hr = file_check(file);
    return SUCCEEDED(hr) ? short_text(&file->Base, FALSE, out) : hr;
}

static HRESULT STDMETHODCALLTYPE file_get_ShortName(IFile *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FILE_OBJECT *file = file_of(This);
    HRESULT hr = file_check(file);
    return SUCCEEDED(hr) ? short_text(&file->Base, TRUE, out) : hr;
}

static HRESULT STDMETHODCALLTYPE file_get_Drive(IFile *This, IDrive **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FILE_OBJECT *file = file_of(This);
    HRESULT hr = file_check(file);
    if (FAILED(hr))
        return hr;
    WCHAR *path;
    hr = ObjectPathCopy(&file->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = FolderDrive(path, out);
        ScrFree(path);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE file_get_ParentFolder(IFile *This, IFolder **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FILE_OBJECT *file = file_of(This);
    HRESULT hr = file_check(file);
    if (FAILED(hr))
        return hr;
    WCHAR *path;
    hr = ObjectPathCopy(&file->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = FolderContaining(path, out);
        ScrFree(path);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE file_get_Attributes(IFile *This, LONG *out)
{
    if (!out)
        return E_POINTER;
    return object_attributes(&file_of(This)->Base, out);
}

static HRESULT STDMETHODCALLTYPE file_put_Attributes(IFile *This, LONG attributes)
{
    return object_put_attributes(&file_of(This)->Base, attributes);
}

static HRESULT STDMETHODCALLTYPE file_get_DateCreated(IFile *This, DATE *out)
{
    return object_date(&file_of(This)->Base, DATE_CREATED, out);
}

static HRESULT STDMETHODCALLTYPE file_get_DateLastModified(IFile *This, DATE *out)
{
    return object_date(&file_of(This)->Base, DATE_MODIFIED, out);
}

static HRESULT STDMETHODCALLTYPE file_get_DateLastAccessed(IFile *This, DATE *out)
{
    return object_date(&file_of(This)->Base, DATE_ACCESSED, out);
}

static HRESULT STDMETHODCALLTYPE file_get_Size(IFile *This, VARIANT *out)
{
    if (!out)
        return E_POINTER;
    out->vt = VT_EMPTY;
    FILE_OBJECT *file = file_of(This);
    HRESULT hr = file_check(file);
    if (FAILED(hr))
        return hr;
    WCHAR *path;
    hr = ObjectPathCopy(&file->Base, &path);
    if (FAILED(hr))
        return hr;
    WIN32_FIND_DATAW find;
    memset(&find, 0, sizeof(find));
    HANDLE handle = FindFirstFileW(path, &find);
    ScrFree(path);
    if (handle == INVALID_HANDLE_VALUE)
        return TranslateWin32Error(GetLastError());
    FindClose(handle);
    return ConvertToVariant(((ULONGLONG)find.nFileSizeHigh << 32) | find.nFileSizeLow, out);
}

static HRESULT STDMETHODCALLTYPE file_get_Type(IFile *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FILE_OBJECT *file = file_of(This);
    HRESULT hr = file_check(file);
    if (FAILED(hr))
        return hr;
    WCHAR *path;
    hr = ObjectPathCopy(&file->Base, &path);
    if (SUCCEEDED(hr)) {
        WCHAR *name;
        hr = FileTypeName(path, &name);
        if (SUCCEEDED(hr)) {
            hr = AllocateBstr(name, -1, out);
            ScrFree(name);
        }
        ScrFree(path);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE file_Delete(IFile *This, VARIANT_BOOL force)
{
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&file_of(This)->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = FsDeleteFile(path, force != VARIANT_FALSE);
        ScrFree(path);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE file_Copy(IFile *This, BSTR destination, VARIANT_BOOL overwrite)
{
    if (!SysStringLen(destination))
        return E_INVALIDARG;
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&file_of(This)->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = FsRelocate(path, destination, overwrite != VARIANT_FALSE, FALSE, FALSE, nullptr);
        ScrFree(path);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE file_Move(IFile *This, BSTR destination)
{
    if (!SysStringLen(destination))
        return E_INVALIDARG;
    FILE_OBJECT *file = file_of(This);
    WCHAR *path, *final_path = nullptr;
    HRESULT hr = ObjectPathCopy(&file->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = FsRelocate(path, destination, FALSE, FALSE, TRUE, &final_path);
        ScrFree(path);
    }
    if (SUCCEEDED(hr) && final_path)
        ObjectSetPath(&file->Base, final_path);
    ScrFree(final_path);
    return hr;
}

HRESULT OpenTextFile(const WCHAR *path, IOMode mode, BOOL create, Tristate format, ITextStream **stream);

static HRESULT STDMETHODCALLTYPE file_OpenAsTextStream(IFile *This, IOMode mode, Tristate format, ITextStream **stream)
{
    if (!stream)
        return E_POINTER;
    *stream = nullptr;
    WCHAR *path;
    HRESULT hr = ObjectPathCopy(&file_of(This)->Base, &path);
    if (SUCCEEDED(hr)) {
        hr = OpenTextFile(path, mode, FALSE, format, stream);
        ScrFree(path);
    }
    return hr;
}

static const IFileVtbl g_file_vtbl = {
    file_query_interface, file_add_ref, file_release, file_GetTypeInfoCount, file_GetTypeInfo, file_GetIDsOfNames,
    file_Invoke, file_get_Path, file_get_Name, file_put_Name, file_get_ShortPath, file_get_ShortName, file_get_Drive,
    file_get_ParentFolder, file_get_Attributes, file_put_Attributes, file_get_DateCreated, file_get_DateLastModified,
    file_get_DateLastAccessed, file_get_Size, file_get_Type, file_Delete, file_Copy, file_Move, file_OpenAsTextStream,
};

static HRESULT STDMETHODCALLTYPE file_info_query_interface(IProvideClassInfo *This, REFIID riid, void **object)
{
    return file_query_interface(&OBJECT_OF(FILE_OBJECT, Info, This)->File, riid, object);
}

static ULONG STDMETHODCALLTYPE file_info_add_ref(IProvideClassInfo *This)
{
    return ObjectAddRef(&OBJECT_OF(FILE_OBJECT, Info, This)->Base);
}

static ULONG STDMETHODCALLTYPE file_info_release(IProvideClassInfo *This)
{
    return ObjectRelease(&OBJECT_OF(FILE_OBJECT, Info, This)->Base);
}

static HRESULT STDMETHODCALLTYPE file_get_class_info(IProvideClassInfo *This, ITypeInfo **info)
{
    return info ? ScrClassInfo(&CLSID_ScrFile, info) : E_POINTER;
}

static const IProvideClassInfoVtbl g_file_info_vtbl = {
    file_info_query_interface, file_info_add_ref, file_info_release, file_get_class_info,
};

/* ------------------------------------------------------------------ the collections of a folder */

static BOOL is_item(const COLLECTION *collection, const WIN32_FIND_DATAW *find)
{
    BOOL directory = (find->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (collection->IsFiles)
        return !directory;
    return directory && !(find->cFileName[0] == '.' && (!find->cFileName[1] || (find->cFileName[1] == '.' && !find->cFileName[2])));
}

/* the item of the collection that has a path, a file or a folder. */
static HRESULT collection_get_item(const COLLECTION *collection, const WCHAR *path, IDispatch **out)
{
    if (collection->IsFiles)
        return FileObjectGet(path, (IFile **)out);
    return FolderObjectGet(path, (IFolder **)out);
}

/* the next item of a walk of the folder: 0 with the item, 1 at the end of the folder, a negative error. */
static HRESULT collection_next(COLLECTION *collection, HANDLE *find, IDispatch **out)
{
    WIN32_FIND_DATAW data;
    memset(&data, 0, sizeof(data));
    WCHAR *parent;
    HRESULT hr = ObjectPathCopy(&collection->Parent->Base, &parent);
    if (FAILED(hr))
        return hr;
    BOOL resume = *find != INVALID_HANDLE_VALUE;
    if (!resume) {
        WCHAR *pattern;
        hr = PathBuild(parent, L"*", &pattern);
        if (FAILED(hr)) {
            ScrFree(parent);
            return hr;
        }
        *find = FindFirstFileW(pattern, &data);
        ScrFree(pattern);
        if (*find == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            ScrFree(parent);
            return error == ERROR_FILE_NOT_FOUND ? 1 : TranslateWin32Error(error);
        }
    }
    for (;;) {
        if (resume) {
            resume = FALSE;
            if (!FindNextFileW(*find, &data))
                break;
        }
        if (is_item(collection, &data)) {
            if (!out) {
                ScrFree(parent);
                return S_OK;
            }
            WCHAR *path;
            hr = PathBuild(parent, data.cFileName, &path);
            if (FAILED(hr)) {
                ScrFree(parent);
                return hr;
            }
            hr = collection_get_item(collection, path, out);
            ScrFree(path);
            if (hr != E_VB_FILENOTFOUND && hr != E_VB_PATHNOTFOUND) {
                ScrFree(parent);
                return hr;
            }
        }
        if (!FindNextFileW(*find, &data))
            break;
    }
    FindClose(*find);
    *find = INVALID_HANDLE_VALUE;
    ScrFree(parent);
    return 1;
}

static HRESULT collection_count(COLLECTION *collection, LONG *count)
{
    if (!count)
        return E_POINTER;
    *count = 0;
    HRESULT hr = folder_check(collection->Parent);
    if (FAILED(hr))
        return hr;
    HANDLE find = INVALID_HANDLE_VALUE;
    LONG n = 0;
    while ((hr = collection_next(collection, &find, nullptr)) == S_OK)
        n++;
    if (hr != 1)
        return hr;
    *count = n;
    return S_OK;
}

static HRESULT collection_item(COLLECTION *collection, VARIANT key, IDispatch **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    BSTR name;
    HRESULT hr = KeyName(key, &name);
    if (FAILED(hr))
        return hr;
    WCHAR *parent, *path;
    hr = ObjectPathCopy(&collection->Parent->Base, &parent);
    if (SUCCEEDED(hr)) {
        hr = PathBuild(parent, name, &path);
        ScrFree(parent);
        if (SUCCEEDED(hr)) {
            hr = collection_get_item(collection, path, out);
            ScrFree(path);
        }
    }
    SysFreeString(name);
    return hr;
}

static HRESULT collection_new_enum(COLLECTION *collection, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    HRESULT hr = folder_check(collection->Parent);
    if (FAILED(hr))
        return hr;
    PATH_ENUM *path_enum = ScrAlloc(sizeof(*path_enum));
    if (!path_enum)
        return E_OUTOFMEMORY;
    path_enum->Enum.lpVtbl = &g_path_enum_vtbl;
    path_enum->Refs = 1;
    path_enum->Collection = collection;
    path_enum->Find = INVALID_HANDLE_VALUE;
    ((IUnknown *)collection)->lpVtbl->AddRef((IUnknown *)collection);
    ScrLockDll();
    *out = (IUnknown *)&path_enum->Enum;
    return S_OK;
}

static COLLECTION *collection_of(void *This)
{
    return (COLLECTION *)This;
}

static HRESULT STDMETHODCALLTYPE collection_query_interface(void *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    COLLECTION *collection = collection_of(This);
    const GUID *own = collection->IsFiles ? &IID_IFileCollection : &IID_IFolderCollection;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, own))
        *object = collection;
    else if (IsEqualIID(riid, &IID_IProvideClassInfo))
        *object = &collection->Info;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&collection->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE collection_add_ref(void *This)
{
    return (ULONG)InterlockedIncrement(&collection_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE collection_release(void *This)
{
    COLLECTION *collection = collection_of(This);
    FOLDER *parent = collection->Parent;
    AcquireSRWLockExclusive(&g_collection_lock);
    LONG refs = --collection->Refs;
    if (!refs) {
        if (parent->Files == collection)
            parent->Files = nullptr;
        if (parent->Folders == collection)
            parent->Folders = nullptr;
    }
    ReleaseSRWLockExclusive(&g_collection_lock);
    if (!refs) {
        ScrFree(collection);
        parent->Folder.lpVtbl->Release(&parent->Folder);
        ScrUnlockDll();
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE collection_GetTypeInfoCount(void *This, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 1;
    return S_OK;
}

static const GUID *collection_iid(void *This)
{
    return collection_of(This)->IsFiles ? &IID_IFileCollection : &IID_IFolderCollection;
}

static HRESULT STDMETHODCALLTYPE collection_GetTypeInfo(void *This, UINT index, LCID lcid, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    *info = nullptr;
    if (index)
        return DISP_E_BADINDEX;
    return ScrTypeInfo(collection_iid(This), info);
}

static HRESULT STDMETHODCALLTYPE collection_GetIDsOfNames(void *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,
                                                          DISPID *ids)
{
    return ScrDispatchGetIDs(collection_iid(This), names, count, ids);
}

static HRESULT STDMETHODCALLTYPE collection_Invoke(void *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                                   DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_error)
{
    return ScrDispatchInvoke(This, collection_iid(This), id, flags, params, result, excep, arg_error);
}

/* the methods both kinds of collections have, as the interface of each kind declares them. */
#define COLLECTION_WRAPPERS(prefix, type)                                                                              \
    static HRESULT STDMETHODCALLTYPE prefix##_query_interface(type *This, REFIID riid, void **object)                  \
    {                                                                                                                  \
        return collection_query_interface(This, riid, object);                                                         \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_add_ref(type *This)                                                         \
    {                                                                                                                  \
        return collection_add_ref(This);                                                                               \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_release(type *This)                                                         \
    {                                                                                                                  \
        return collection_release(This);                                                                               \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_GetTypeInfoCount(type *This, UINT *count)                                 \
    {                                                                                                                  \
        return collection_GetTypeInfoCount(This, count);                                                               \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_GetTypeInfo(type *This, UINT index, LCID lcid, ITypeInfo **info)         \
    {                                                                                                                  \
        return collection_GetTypeInfo(This, index, lcid, info);                                                        \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_GetIDsOfNames(type *This, REFIID riid, LPOLESTR *names, UINT count,      \
                                                            LCID lcid, DISPID *ids)                                    \
    {                                                                                                                  \
        return collection_GetIDsOfNames(This, riid, names, count, lcid, ids);                                          \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_Invoke(type *This, DISPID id, REFIID riid, LCID lcid, WORD flags,        \
                                                     DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep,            \
                                                     UINT *arg_error)                                                  \
    {                                                                                                                  \
        return collection_Invoke(This, id, riid, lcid, flags, params, result, excep, arg_error);                       \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_get_NewEnum(type *This, IUnknown **out)                                   \
    {                                                                                                                  \
        return collection_new_enum(collection_of(This), out);                                                          \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_get_Count(type *This, LONG *count)                                        \
    {                                                                                                                  \
        return collection_count(collection_of(This), count);                                                           \
    }

COLLECTION_WRAPPERS(folders, IFolderCollection)
COLLECTION_WRAPPERS(files, IFileCollection)

static HRESULT STDMETHODCALLTYPE folders_Add(IFolderCollection *This, BSTR name, IFolder **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!SysStringLen(name) || (PathScan(name, -1) & (SCAN_SEPARATOR | SCAN_WILDCARD)))
        return E_INVALIDARG;
    WCHAR *parent, *path;
    HRESULT hr = ObjectPathCopy(&collection_of(This)->Parent->Base, &parent);
    if (FAILED(hr))
        return hr;
    hr = PathBuild(parent, name, &path);
    ScrFree(parent);
    if (SUCCEEDED(hr)) {
        hr = FsCreateFolder(path, out);
        ScrFree(path);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE folders_get_Item(IFolderCollection *This, VARIANT key, IFolder **out)
{
    return collection_item(collection_of(This), key, (IDispatch **)out);
}

static HRESULT STDMETHODCALLTYPE files_get_Item(IFileCollection *This, VARIANT key, IFile **out)
{
    return collection_item(collection_of(This), key, (IDispatch **)out);
}

static const IFolderCollectionVtbl g_folders_vtbl = {
    folders_query_interface, folders_add_ref, folders_release, folders_GetTypeInfoCount, folders_GetTypeInfo,
    folders_GetIDsOfNames, folders_Invoke, folders_Add, folders_get_Item, folders_get_NewEnum, folders_get_Count,
};

static const IFileCollectionVtbl g_files_vtbl = {
    files_query_interface, files_add_ref, files_release, files_GetTypeInfoCount, files_GetTypeInfo,
    files_GetIDsOfNames, files_Invoke, files_get_Item, files_get_NewEnum, files_get_Count,
};

static HRESULT STDMETHODCALLTYPE collection_info_query_interface(IProvideClassInfo *This, REFIID riid, void **object)
{
    return collection_query_interface(OBJECT_OF(COLLECTION, Info, This), riid, object);
}

static ULONG STDMETHODCALLTYPE collection_info_add_ref(IProvideClassInfo *This)
{
    return collection_add_ref(OBJECT_OF(COLLECTION, Info, This));
}

static ULONG STDMETHODCALLTYPE collection_info_release(IProvideClassInfo *This)
{
    return collection_release(OBJECT_OF(COLLECTION, Info, This));
}

static HRESULT STDMETHODCALLTYPE collection_get_class_info(IProvideClassInfo *This, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    COLLECTION *collection = OBJECT_OF(COLLECTION, Info, This);
    return ScrClassInfo(collection->IsFiles ? &CLSID_ScrFiles : &CLSID_ScrFolders, info);
}

static const IProvideClassInfoVtbl g_folders_info_vtbl = {
    collection_info_query_interface, collection_info_add_ref, collection_info_release, collection_get_class_info,
};

static const IProvideClassInfoVtbl g_files_info_vtbl = {
    collection_info_query_interface, collection_info_add_ref, collection_info_release, collection_get_class_info,
};

static HRESULT collection_create(FOLDER *folder, BOOL files, COLLECTION **out)
{
    COLLECTION *collection = ScrAlloc(sizeof(*collection));
    if (!collection)
        return E_OUTOFMEMORY;
    collection->Vtbl = files ? (const void *)&g_files_vtbl : (const void *)&g_folders_vtbl;
    collection->Info.lpVtbl = files ? &g_files_info_vtbl : &g_folders_info_vtbl;
    collection->Refs = 1;
    collection->Parent = folder;
    collection->IsFiles = files;
    folder->Folder.lpVtbl->AddRef(&folder->Folder);
    ScrLockDll();
    *out = collection;
    return S_OK;
}

/* ------------------------------------------------------------------ the enumerator of a collection */

static PATH_ENUM *path_enum_of(IEnumVARIANT *This)
{
    return (PATH_ENUM *)This;
}

static HRESULT STDMETHODCALLTYPE path_enum_query_interface(IEnumVARIANT *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumVARIANT)) {
        *object = This;
        InterlockedIncrement(&path_enum_of(This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE path_enum_add_ref(IEnumVARIANT *This)
{
    return (ULONG)InterlockedIncrement(&path_enum_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE path_enum_release(IEnumVARIANT *This)
{
    PATH_ENUM *path_enum = path_enum_of(This);
    LONG refs = InterlockedDecrement(&path_enum->Refs);
    if (!refs) {
        if (path_enum->Find != INVALID_HANDLE_VALUE)
            FindClose(path_enum->Find);
        ((IUnknown *)path_enum->Collection)->lpVtbl->Release((IUnknown *)path_enum->Collection);
        ScrFree(path_enum);
        ScrUnlockDll();
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE path_enum_next(IEnumVARIANT *This, ULONG count, VARIANT *values, ULONG *fetched)
{
    if (!values)
        return E_POINTER;
    PATH_ENUM *path_enum = path_enum_of(This);
    if (fetched)
        *fetched = 0;
    if (!count)
        return S_OK;
    ULONG got = 0;
    HRESULT hr = 1;
    if (!path_enum->Done) {
        while (got < count) {
            IDispatch *item;
            hr = collection_next(path_enum->Collection, &path_enum->Find, &item);
            if (hr != S_OK)
                break;
            values[got].vt = VT_DISPATCH;
            values[got].pdispVal = item;
            got++;
        }
    }
    if (hr < 0) {
        while (got) {
            got--;
            values[got].pdispVal->lpVtbl->Release(values[got].pdispVal);
        }
        return hr;
    }
    if (hr == 1)
        path_enum->Done = TRUE;
    if (fetched)
        *fetched = got;
    for (ULONG i = got; i < count; i++)
        values[i].vt = VT_EMPTY;
    return got != count ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE path_enum_skip(IEnumVARIANT *This, ULONG count)
{
    PATH_ENUM *path_enum = path_enum_of(This);
    if (!count)
        return S_OK;
    if (path_enum->Done)
        return S_FALSE;
    HRESULT hr = S_OK;
    while (count-- && (hr = collection_next(path_enum->Collection, &path_enum->Find, nullptr)) == S_OK)
        ;
    if (hr == 1) {
        path_enum->Done = TRUE;
        return S_FALSE;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE path_enum_reset(IEnumVARIANT *This)
{
    PATH_ENUM *path_enum = path_enum_of(This);
    if (path_enum->Find != INVALID_HANDLE_VALUE) {
        FindClose(path_enum->Find);
        path_enum->Find = INVALID_HANDLE_VALUE;
    }
    path_enum->Done = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE path_enum_clone(IEnumVARIANT *This, IEnumVARIANT **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    PATH_ENUM *path_enum = path_enum_of(This);
    IUnknown *created;
    HRESULT hr = collection_new_enum(path_enum->Collection, &created);
    if (FAILED(hr))
        return hr;
    *out = (IEnumVARIANT *)created;
    return S_OK;
}

static const IEnumVARIANTVtbl g_path_enum_vtbl = {
    path_enum_query_interface, path_enum_add_ref, path_enum_release, path_enum_next, path_enum_skip, path_enum_reset,
    path_enum_clone,
};
