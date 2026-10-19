/*
 * windows.storage internal interface shared by its source files.
 */
#ifndef MIME_WINDOWS_STORAGE_PRIVATE_H
#define MIME_WINDOWS_STORAGE_PRIVATE_H

#include <mime/win32/shobjidl.h>
#include <mime/win32/shellapi.h>
#include <mime/win32/shlwapi.h>
#include <mime/win32/winnls.h>
#include <rtl.h>

/* the shortcut file header. */
#define LNK_HEADER_SIZE 0x4C
#define LNK_STRING_MAX 260

/* the extra data blocks windows.storage knows by signature, besides those in shobjidl.h. */
#define LNK_SIG_TRACKER 0xA0000003
#define LNK_SIG_VISTA_IDLIST 0xA000000C

/* an extra data block, kept as read, the size and signature included in Data. */
typedef struct _LINK_BLOCK {
    struct _LINK_BLOCK *Next;
    DWORD Size;
    BYTE Data[];
} LINK_BLOCK;

/* a shortcut, with the interfaces it answers to. */
typedef struct _LINK {
    IShellLinkW LinkW;
    IShellLinkA LinkA;
    IPersistFile File;
    IPersistStream Stream;
    IShellLinkDataList DataList;
    IPropertyStore Store;
    IObjectWithSite Site;
    LONG Refs;

    DWORD Flags;
    DWORD Attributes;
    DWORD FileSize;
    FILETIME Created, Accessed, Written;
    int IconIndex;
    DWORD ShowCmd;
    WORD HotKey;
    /* the access the file was opened with, which property writes need. */
    DWORD Mode;
    BOOL Dirty;

    PIDLIST_ABSOLUTE Pidl;
    BYTE *Info;
    DWORD InfoSize;
    LPWSTR Name, RelativePath, WorkingDir, Arguments, IconLocation;
    /* the file the relative path is worked out from, and the file the link was loaded from or saved to. */
    LPWSTR RelativeSource, CurFile;
    LINK_BLOCK *Blocks;

    IPropertyStore *Props;
    IPersistSerializedPropStorage *Serialized;
    IUnknown *SiteObject;
} LINK;

/* link.c */
HRESULT LinkCreate(IUnknown *outer, REFIID riid, void **object);

/* linkfile.c */
void *LkAlloc(SIZE_T size);
void LkFree(void *p);
LPWSTR LkDup(LPCWSTR text);
void LkCopy(WCHAR *dst, LPCWSTR src, ULONG chars);
void LkSetString(LPWSTR *field, LPCWSTR text);
LINK_BLOCK *LkFindBlock(const LINK *link, DWORD signature);
void LkRemoveBlock(LINK *link, DWORD signature);
HRESULT LkSetBlock(LINK *link, const void *block);
void LkReset(LINK *link);
HRESULT LkParse(LINK *link, const BYTE *data, SIZE_T size);
BYTE *LkBuild(LINK *link, SIZE_T *size);
BOOL LkInfoPath(const BYTE *info, DWORD size, LPWSTR path, ULONG chars);
BYTE *LkInfoCreate(LPCWSTR path, DWORD *size);

/* dest.c: jump lists and the collections that hold their items. */
HRESULT DestinationListCreate(IUnknown *outer, REFIID riid, void **object);
HRESULT ObjectCollectionCreate(IUnknown *outer, REFIID riid, void **object);
HRESULT ApplicationDestinationsCreate(IUnknown *outer, REFIID riid, void **object);
HRESULT ApplicationDocumentListsCreate(IUnknown *outer, REFIID riid, void **object);

/* fileoperation.c */
HRESULT FileOperationCreate(IUnknown *outer, REFIID riid, void **object);

#endif
