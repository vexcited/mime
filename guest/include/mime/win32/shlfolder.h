/*
 * the shell namespace interfaces of shobjidl_core.h: IShellFolder and IShellFolder2 with their enumerator, and the
 * interfaces that give a folder its place, IPersistFolder and IPersistFolder2. interfaces are C structs whose first
 * member points to a vtable.
 */
#ifndef MIME_WIN32_SHLFOLDER_H
#define MIME_WIN32_SHLFOLDER_H

#include <mime/win32/oleauto.h>
#include <mime/win32/shobjidl.h>

MIME_DEFINE_GUID(IID_IEnumIDList, 0x000214F2, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IShellFolder, 0x000214E6, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IShellFolder2, 0x93F2F68C, 0x1D1B, 0x11D3, 0xA3, 0x0E, 0x00, 0xC0, 0x4F, 0x79, 0xAB, 0xD1);
MIME_DEFINE_GUID(IID_IPersistFolder, 0x000214EA, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IPersistFolder2, 0x1AC3D9F0, 0x175C, 0x11D1, 0x95, 0xBE, 0x00, 0x60, 0x97, 0x97, 0xEA, 0x4F);

/* the Compressed (zipped) Folders namespace extension, served by zipfldr.dll. */
MIME_DEFINE_GUID(CLSID_CompressedFolder, 0xE88DCCE0, 0xB7B3, 0x11D1, 0xA9, 0xF0, 0x00, 0xAA, 0x00, 0x60, 0xFA, 0x31);

/* how a name is asked for, from IShellFolder::GetDisplayNameOf. */
typedef DWORD SHGDNF;
#define SHGDN_NORMAL 0x0000
#define SHGDN_INFOLDER 0x0001
#define SHGDN_FOREDITING 0x1000
#define SHGDN_FORADDRESSBAR 0x4000
#define SHGDN_FORPARSING 0x8000

#define SHCONTF_CHECKING_FOR_CHILDREN 0x0010
#define SHCONTF_INIT_ON_FIRST_NEXT 0x0100

typedef DWORD SHCOLSTATEF;
#define SHCOLSTATE_TYPE_STR 0x1
#define SHCOLSTATE_ONBYDEFAULT 0x10


#pragma pack(push, 1)
typedef struct _SHELLDETAILS {
    int fmt;
    int cxChar;
    STRRET str;
} SHELLDETAILS;
#pragma pack(pop)

/* ------------------------------------------------------------------ enumerating a folder */

typedef struct IEnumIDList IEnumIDList;
#define IEnumIDList_METHODS(T)                                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Next)(T * This, ULONG count, PITEMID_CHILD * items, ULONG * fetched);                   \
    HRESULT(STDMETHODCALLTYPE *Skip)(T * This, ULONG count);                                                           \
    HRESULT(STDMETHODCALLTYPE *Reset)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Clone)(T * This, IEnumIDList * *enumerator);
MIME_COM_INTERFACE(IEnumIDList)

/* ------------------------------------------------------------------ folders */

typedef struct IShellFolder IShellFolder;
#define IShellFolder_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *ParseDisplayName)(T * This, HWND hwnd, IBindCtx * context, LPWSTR name, ULONG * eaten,  \
                                                 PIDLIST_RELATIVE * pidl, ULONG * attributes);                         \
    HRESULT(STDMETHODCALLTYPE *EnumObjects)(T * This, HWND hwnd, SHCONTF flags, IEnumIDList * *enumerator);            \
    HRESULT(STDMETHODCALLTYPE *BindToObject)(T * This, PCUIDLIST_RELATIVE pidl, IBindCtx * context, REFIID riid,       \
                                             void **object);                                                           \
    HRESULT(STDMETHODCALLTYPE *BindToStorage)(T * This, PCUIDLIST_RELATIVE pidl, IBindCtx * context, REFIID riid,      \
                                              void **object);                                                          \
    HRESULT(STDMETHODCALLTYPE *CompareIDs)(T * This, LPARAM column, PCUIDLIST_RELATIVE a, PCUIDLIST_RELATIVE b);       \
    HRESULT(STDMETHODCALLTYPE *CreateViewObject)(T * This, HWND hwnd, REFIID riid, void **object);                     \
    HRESULT(STDMETHODCALLTYPE *GetAttributesOf)(T * This, UINT count, PCUITEMID_CHILD const *items,                    \
                                                SFGAOF * attributes);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetUIObjectOf)(T * This, HWND hwnd, UINT count, PCUITEMID_CHILD const *items,           \
                                              REFIID riid, UINT * reserved, void **object);                           \
    HRESULT(STDMETHODCALLTYPE *GetDisplayNameOf)(T * This, PCUITEMID_CHILD pidl, SHGDNF flags, STRRET * name);         \
    HRESULT(STDMETHODCALLTYPE *SetNameOf)(T * This, HWND hwnd, PCUITEMID_CHILD pidl, LPCWSTR name, SHGDNF flags,       \
                                          PITEMID_CHILD * result);
MIME_COM_INTERFACE(IShellFolder)

typedef struct IShellFolder2 IShellFolder2;
#define IShellFolder2_METHODS(T)                                                                                       \
    IShellFolder_METHODS(T)                                                                                            \
    HRESULT(STDMETHODCALLTYPE *GetDefaultSearchGUID)(T * This, GUID * guid);                                           \
    HRESULT(STDMETHODCALLTYPE *EnumSearches)(T * This, IUnknown * *enumerator);                                        \
    HRESULT(STDMETHODCALLTYPE *GetDefaultColumn)(T * This, DWORD reserved, ULONG * sort, ULONG * display);             \
    HRESULT(STDMETHODCALLTYPE *GetDefaultColumnState)(T * This, UINT column, SHCOLSTATEF * state);                     \
    HRESULT(STDMETHODCALLTYPE *GetDetailsEx)(T * This, PCUITEMID_CHILD pidl, const SHCOLUMNID *column,                 \
                                             VARIANT *value);                                                          \
    HRESULT(STDMETHODCALLTYPE *GetDetailsOf)(T * This, PCUITEMID_CHILD pidl, UINT column, SHELLDETAILS * details);     \
    HRESULT(STDMETHODCALLTYPE *MapColumnToSCID)(T * This, UINT column, SHCOLUMNID * id);
MIME_COM_INTERFACE(IShellFolder2)

/* ------------------------------------------------------------------ giving a folder its place */

typedef struct IPersistFolder IPersistFolder;
#define IPersistFolder_METHODS(T)                                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetClassID)(T * This, CLSID * clsid);                                                   \
    HRESULT(STDMETHODCALLTYPE *Initialize)(T * This, PCIDLIST_ABSOLUTE pidl);
MIME_COM_INTERFACE(IPersistFolder)

typedef struct IPersistFolder2 IPersistFolder2;
#define IPersistFolder2_METHODS(T)                                                                                     \
    IPersistFolder_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *GetCurFolder)(T * This, PIDLIST_ABSOLUTE * pidl);
MIME_COM_INTERFACE(IPersistFolder2)

/* ------------------------------------------------------------------ shell32 functions */

SHSTDAPI HRESULT WINAPI SHGetDesktopFolder(IShellFolder **folder);

#endif
