/*
 * scrrun.dll internals shared by its source files: the scripting runtime of Windows, the file system object with its drives,
 * folders, files and text streams, and the dictionary. The objects are dual interfaces that answer IDispatch from the type
 * library of this dll, a resource of it.
 */
#ifndef MIME_SCRRUN_PRIVATE_H
#define MIME_SCRRUN_PRIVATE_H

#include <mime/win32/scrrun.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>
#include <mime/win32/winver.h>
#include <rtl.h>

/* the run time errors of Visual Basic, which the objects return as the scodes of automation. */
#define VBERR(number) ((HRESULT)(0x800A0000u + (number)))
#define E_VB_INVALID VBERR(5)
#define E_VB_BADFILENAME VBERR(52)
#define E_VB_FILENOTFOUND VBERR(53)
#define E_VB_BADFILEMODE VBERR(54)
#define E_VB_ALREADYOPEN VBERR(55)
#define E_VB_PASTEOF VBERR(62)
#define E_VB_FILEEXISTS VBERR(58)
#define E_VB_DISKFULL VBERR(61)
#define E_VB_DISKNOTREADY VBERR(71)
#define E_VB_DEVUNAVAILABLE VBERR(68)
#define E_VB_PERMISSION VBERR(70)
#define E_VB_PATHFILEACCESS VBERR(75)
#define E_VB_PATHNOTFOUND VBERR(76)
#define E_VB_NOOBJECT VBERR(91)
#define E_VB_TYPEMISMATCH VBERR(13)
#define E_VB_OVERFLOW VBERR(6)
#define E_VB_ELEMENTEXISTS VBERR(457)
#define E_VB_ELEMENTMISSING VBERR(32811)

extern HINSTANCE g_module;

/* the connections of drives to network shares, from mpr.dll. */
DECLSPEC_IMPORT DWORD WINAPI WNetGetConnectionW(LPCWSTR local, LPWSTR remote, LPDWORD size);
#ifndef ERROR_NOT_CONNECTED
#define ERROR_NOT_CONNECTED 2250
#endif
#ifndef ERROR_CONNECTION_UNAVAIL
#define ERROR_CONNECTION_UNAVAIL 1201
#endif

/* ------------------------------------------------------------------ memory, locks, strings */

void *ScrAlloc(SIZE_T size);
void ScrFree(void *memory);
void ScrLockDll(void);
void ScrUnlockDll(void);
LONG ScrLockCount(void);

/* a copy of text with a length, in memory of the dll. */
WCHAR *ScrCopy(const WCHAR *text, SIZE_T length);
WCHAR *ScrCopyZ(const WCHAR *text);
/* a string of automation holding length characters of text, the whole text for a length of -1. */
HRESULT AllocateBstr(const WCHAR *text, int length, BSTR *out);
/* the scode a win32 error means to the objects. */
HRESULT TranslateWin32Error(DWORD error);

/* ------------------------------------------------------------------ type information */

HRESULT ScrTypeInfo(REFIID iid, ITypeInfo **info);
HRESULT ScrClassInfo(REFCLSID clsid, ITypeInfo **info);
HRESULT ScrDispatchGetIDs(REFIID iid, LPOLESTR *names, UINT count, DISPID *ids);
HRESULT ScrDispatchInvoke(void *self, REFIID iid, DISPID id, WORD flags, DISPPARAMS *params, VARIANT *result,
                          EXCEPINFO *excep, UINT *arg_error);

/* the dispatch methods of an object whose type information is the interface iid of the library. */
#define SCR_DISPATCH_METHODS(prefix, type, iid)                                                                        \
    static HRESULT STDMETHODCALLTYPE prefix##_GetTypeInfoCount(type *This, UINT *count)                                 \
    {                                                                                                                  \
        if (!count)                                                                                                    \
            return E_POINTER;                                                                                          \
        *count = 1;                                                                                                    \
        return S_OK;                                                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_GetTypeInfo(type *This, UINT index, LCID lcid, ITypeInfo **info)         \
    {                                                                                                                  \
        if (!info)                                                                                                     \
            return E_POINTER;                                                                                          \
        *info = nullptr;                                                                                               \
        if (index)                                                                                                     \
            return DISP_E_BADINDEX;                                                                                    \
        return ScrTypeInfo(iid, info);                                                                                 \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_GetIDsOfNames(type *This, REFIID riid, LPOLESTR *names, UINT count,      \
                                                            LCID lcid, DISPID *ids)                                    \
    {                                                                                                                  \
        return ScrDispatchGetIDs(iid, names, count, ids);                                                              \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_Invoke(type *This, DISPID id, REFIID riid, LCID lcid, WORD flags,        \
                                                     DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep,            \
                                                     UINT *arg_error)                                                  \
    {                                                                                                                  \
        return ScrDispatchInvoke(This, iid, id, flags, params, result, excep, arg_error);                              \
    }

/* ------------------------------------------------------------------ paths */

BOOL PathIsSeparator(int c);
BOOL PathIsUnc(const WCHAR *path);
BOOL PathIsDriveLetter(const WCHAR *path);
/* the parts of a path: the drive or share, the last name, the folder it is in and the extension, the part and its length
 * or nullptr when the path has none. */
const WCHAR *PathDrivePart(const WCHAR *path, int *length);
const WCHAR *PathNamePart(const WCHAR *path, int *length);
const WCHAR *PathParentPart(const WCHAR *path, int *length);
const WCHAR *PathExtensionPart(const WCHAR *path, int *length);
BOOL PathIsRoot(const WCHAR *path);
/* what a text holds: 1 nothing, 2 a character a name cannot have, 4 a separator, 0x10 a wildcard. */
#define SCAN_EMPTY 1
#define SCAN_INVALID 2
#define SCAN_SEPARATOR 4
#define SCAN_WILDCARD 0x10
DWORD PathScan(const WCHAR *text, int length);
HRESULT PathBuild(const WCHAR *path, const WCHAR *name, WCHAR **out);
HRESULT PathAbsolute(const WCHAR *path, BOOL short_form, WCHAR **out);
/* the full path with the case of the names the file system has, and the same in the short form. */
HRESULT PathCanonical(const WCHAR *path, WCHAR **out);
HRESULT PathCanonicalShort(const WCHAR *path, WCHAR **out);
void PathMakeCanonical(WCHAR *path, SIZE_T capacity, BOOL short_form);
DWORD PathAttributes(const WCHAR *path);
HRESULT PathFileExists(const WCHAR *path);
HRESULT PathFolderExists(const WCHAR *path);
HRESULT PathDriveExists(const WCHAR *path);
HRESULT PathCanonicalDrive(const WCHAR *path, WCHAR **out);

/* ------------------------------------------------------------------ the file system */

typedef struct ENUM_DATA ENUM_DATA;
typedef HRESULT (*ENUM_PROC)(ENUM_DATA *data);

/* a walk over the files that a path with wildcards names, and the folders under it. */
struct ENUM_DATA {
    int Depth, Files, Folders;
    SIZE_T NameStart;
    WCHAR *Path;
    SIZE_T Capacity;
    void *Context;
    ENUM_PROC FileProc, FolderProc;
    WIN32_FIND_DATAW Find;
};

HRESULT EnumPath(ENUM_DATA *data);
HRESULT EnumStandardFolder(ENUM_DATA *data);
HRESULT EnumReserve(ENUM_DATA *data, SIZE_T length);

HRESULT FsDeleteFile(const WCHAR *spec, BOOL force);
HRESULT FsDeleteFolder(const WCHAR *spec, BOOL force);
HRESULT FsRelocate(const WCHAR *source, const WCHAR *destination, BOOL overwrite, BOOL folder, BOOL move,
                   WCHAR **final_path);
HRESULT FsRename(const WCHAR *path, const WCHAR *name, WCHAR **new_path);
HRESULT FsFolderSize(const WCHAR *path, ULONGLONG *size);

/* ------------------------------------------------------------------ the objects of paths */

typedef enum KIND { KIND_DRIVE, KIND_FOLDER, KIND_FILE } KIND;

/* the part every object of a path has: it is shared by everyone who asks for the same path while it lives. */
typedef struct POBJ {
    struct POBJ *Next;
    KIND Kind;
    LONG Refs;
    WCHAR *Path;
    BOOL Root;
} POBJ;

HRESULT ObjectGet(KIND kind, const WCHAR *path, SIZE_T size, POBJ **out, BOOL *created);
ULONG ObjectAddRef(POBJ *object);
ULONG ObjectRelease(POBJ *object);
void ObjectSetPath(POBJ *object, const WCHAR *path);
HRESULT ObjectPathCopy(POBJ *object, WCHAR **out);
/* the name an item of a collection is asked for by, which must be a string with nothing in it a name cannot have. */
HRESULT KeyName(VARIANT key, BSTR *out);
/* dates and sizes as the objects give them. */
HRESULT ConvertToDate(const FILETIME *time, DATE *date);
HRESULT ConvertToVariant(ULONGLONG value, VARIANT *out);

HRESULT FsGetFolder(const WCHAR *path, IFolder **folder);
HRESULT FsGetFile(const WCHAR *path, IFile **file);
HRESULT FsGetDrive(const WCHAR *spec, IDrive **drive);
HRESULT FsCreateFolder(const WCHAR *path, IFolder **folder);
/* a folder, file or drive for a path that is already in the form the file system object keeps. */
HRESULT FolderObjectGet(const WCHAR *path, IFolder **folder);
HRESULT FileObjectGet(const WCHAR *path, IFile **file);
HRESULT DriveObjectGet(const WCHAR *path, IDrive **drive);
HRESULT DriveCollectionCreate(IDriveCollection **drives);
HRESULT FolderDrive(const WCHAR *path, IDrive **drive);
HRESULT FolderContaining(const WCHAR *path, IFolder **folder);
HRESULT FileTypeName(const WCHAR *path, WCHAR **name);
HRESULT FsCreate(REFIID riid, void **object);

/* ------------------------------------------------------------------ text streams */

typedef enum STREAM_KIND { STREAM_FILE = 0, STREAM_CONSOLE = 1, STREAM_PIPE = 2 } STREAM_KIND;

typedef struct BYTE_STREAM BYTE_STREAM;
HRESULT ByteStreamFromHandle(HANDLE handle, STREAM_KIND kind, BOOL wide_console, BYTE_STREAM **stream);
void ByteStreamRelease(BYTE_STREAM *stream);

HRESULT TextStreamCreate(BYTE_STREAM *stream, IOMode mode, Tristate format, STREAM_KIND kind, ITextStream **out);
HRESULT TextStreamFromFile(const WCHAR *path, DWORD access, DWORD share, DWORD disposition, IOMode mode,
                           Tristate format, ITextStream **out);
HRESULT TextStreamStandard(DWORD handle_id, Tristate format, ITextStream **out);

/* ------------------------------------------------------------------ the dictionary */

HRESULT DictionaryCreate(REFIID riid, void **object);

/* the value a variant holds, with references to variants followed. */
VARIANT *ScrDeref(VARIANT *value);

#endif
