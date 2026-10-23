/*
 * shell object interfaces of shobjidl_core.h and shobjidl.h: shortcuts, shell item arrays and enumerators, and
 * the common item dialogs of comdlg32.dll. interfaces are C structs whose first member points to a vtable.
 */
#ifndef MIME_WIN32_SHOBJIDL_H
#define MIME_WIN32_SHOBJIDL_H

#include <mime/win32/objidl.h>
#include <mime/win32/propsys.h>
#include <mime/win32/shlobj.h>

#ifndef TYPE_E_SIZETOOBIG
#define TYPE_E_SIZETOOBIG ((HRESULT)0x800288C5L)
#endif

/* ------------------------------------------------------------------ classes and interface ids */

MIME_DEFINE_GUID(CLSID_ShellLink, 0x00021401, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(CLSID_ShellItem, 0x9AC9FBE1, 0xE0A2, 0x4AD6, 0xB4, 0xEE, 0xE2, 0x12, 0x01, 0x3E, 0xA9, 0x17);
MIME_DEFINE_GUID(CLSID_FileOpenDialog, 0xDC1C5A9C, 0xE88A, 0x4DDE, 0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7);
MIME_DEFINE_GUID(CLSID_FileSaveDialog, 0xC0B4E2F3, 0xBA21, 0x4773, 0x8D, 0xBA, 0x33, 0x5E, 0xC9, 0x46, 0xEB, 0x8B);
MIME_DEFINE_GUID(CLSID_FileOperation, 0x3AD05575, 0x8857, 0x4850, 0x92, 0x77, 0x11, 0xB8, 0x5B, 0xDB, 0x8E, 0x09);
MIME_DEFINE_GUID(CLSID_MyComputer, 0x20D04FE0, 0x3AEA, 0x1069, 0xA2, 0xD8, 0x08, 0x00, 0x2B, 0x30, 0x30, 0x9D);

MIME_DEFINE_GUID(IID_IPersistFile, 0x0000010B, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IShellLinkA, 0x000214EE, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IShellLinkW, 0x000214F9, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IShellLinkDataList, 0x45E2B4AE, 0xB1C3, 0x11D0, 0xB9, 0x2F, 0x00, 0xA0, 0xC9, 0x03, 0x12, 0xE1);
MIME_DEFINE_GUID(IID_IPersistIDList, 0x1079ACFC, 0x29BD, 0x11D3, 0x8E, 0x0D, 0x00, 0xC0, 0x4F, 0x68, 0x37, 0xD5);
MIME_DEFINE_GUID(IID_IOleWindow, 0x00000114, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(CLSID_FileOpenDialogLegacy, 0x725F645B, 0xEAED, 0x4FC5, 0xB1, 0xC5, 0xD9, 0xAD, 0x0A, 0xCC, 0xBA,
                 0x5E);
MIME_DEFINE_GUID(CLSID_FileSaveDialogLegacy, 0xAF02484C, 0xA0A9, 0x4669, 0x90, 0x51, 0x05, 0x8A, 0xB1, 0x2B, 0x91,
                 0x95);
MIME_DEFINE_GUID(IID_IObjectWithSite, 0xFC4801A3, 0x2BA9, 0x11CF, 0xA2, 0x29, 0x00, 0xAA, 0x00, 0x3D, 0x73, 0x52);
MIME_DEFINE_GUID(IID_IShellItemArray, 0xB63EA76D, 0x1F85, 0x456F, 0xA1, 0x9C, 0x48, 0x15, 0x9E, 0xFA, 0x85, 0x8B);
MIME_DEFINE_GUID(IID_IEnumShellItems, 0x70629033, 0xE363, 0x4A28, 0xA5, 0x67, 0x0D, 0xB7, 0x80, 0x06, 0xE6, 0xD7);
MIME_DEFINE_GUID(IID_IShellItemFilter, 0x2659B475, 0xEEB8, 0x48B7, 0x8F, 0x07, 0xB3, 0x78, 0x81, 0x0F, 0x48, 0xCF);
MIME_DEFINE_GUID(IID_IModalWindow, 0xB4DB1657, 0x70D7, 0x485E, 0x8E, 0x3E, 0x6F, 0xCB, 0x5A, 0x5C, 0x18, 0x02);
MIME_DEFINE_GUID(IID_IFileDialog, 0x42F85136, 0xDB7E, 0x439C, 0x85, 0xF1, 0xE4, 0x07, 0x5D, 0x13, 0x5F, 0xC8);
MIME_DEFINE_GUID(IID_IFileDialog2, 0x61744FC7, 0x85B5, 0x4791, 0xA9, 0xB0, 0x27, 0x22, 0x76, 0x30, 0x9B, 0x13);
MIME_DEFINE_GUID(IID_IFileOpenDialog, 0xD57C7288, 0xD4AD, 0x4768, 0xBE, 0x02, 0x9D, 0x96, 0x95, 0x32, 0xD9, 0x60);
MIME_DEFINE_GUID(IID_IFileSaveDialog, 0x84BCCD23, 0x5FDE, 0x4CDB, 0xAE, 0xA4, 0xAF, 0x64, 0xB8, 0x3D, 0x78, 0xAB);
MIME_DEFINE_GUID(IID_IFileDialogEvents, 0x973510DB, 0x7D7F, 0x452B, 0x89, 0x75, 0x74, 0xA8, 0x58, 0x28, 0xD3, 0x54);
MIME_DEFINE_GUID(IID_IFileDialogControlEvents, 0x36116642, 0xD713, 0x4B97, 0x9B, 0x83, 0x74, 0x84, 0xA9, 0xD0, 0x04,
                 0x33);
MIME_DEFINE_GUID(IID_IFileDialogCustomize, 0xE6FDD21A, 0x163F, 0x4975, 0x9C, 0x8C, 0xA6, 0x9F, 0x1B, 0xA3, 0x70, 0x34);
MIME_DEFINE_GUID(IID_IFileOperation, 0x947AAB5F, 0x0A5C, 0x4C13, 0xB4, 0xD6, 0x4B, 0xF7, 0x83, 0x6F, 0xC9, 0xF8);
MIME_DEFINE_GUID(IID_IFileOperationProgressSink, 0x04B0F1A7, 0x9490, 0x44BC, 0x96, 0xE1, 0x42, 0x96, 0xA3, 0x12, 0x52,
                 0xE2);
MIME_DEFINE_GUID(IID_IFileSystemBindData, 0x01E18D10, 0x4D8B, 0x11D2, 0x85, 0x5D, 0x00, 0x60, 0x08, 0x05, 0x93, 0x67);
MIME_DEFINE_GUID(IID_IFileSystemBindData2, 0x3ACF075F, 0x71DB, 0x4AFA, 0x81, 0xF0, 0x3F, 0xC4, 0xFD, 0xF2, 0xA5, 0xB8);
MIME_DEFINE_GUID(BHID_EnumItems, 0x94F60519, 0x2850, 0x4924, 0xAA, 0x5A, 0xD1, 0x5E, 0x84, 0x86, 0x80, 0x39);
MIME_DEFINE_GUID(BHID_Stream, 0x1CEBB3AB, 0x7C10, 0x499A, 0xA4, 0x17, 0x92, 0xCA, 0x16, 0xC4, 0xCB, 0x83);

#define STR_FILE_SYS_BIND_DATA L"File System Bind Data"

/* ------------------------------------------------------------------ persistence */

typedef struct IPersist IPersist;
typedef struct IPersistVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPersist *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPersist *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPersist *This);
    HRESULT(STDMETHODCALLTYPE *GetClassID)(IPersist *This, CLSID *clsid);
} IPersistVtbl;
struct IPersist {
    const IPersistVtbl *lpVtbl;
};

typedef struct IPersistFile IPersistFile;
typedef struct IPersistFileVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPersistFile *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPersistFile *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPersistFile *This);
    HRESULT(STDMETHODCALLTYPE *GetClassID)(IPersistFile *This, CLSID *clsid);
    HRESULT(STDMETHODCALLTYPE *IsDirty)(IPersistFile *This);
    HRESULT(STDMETHODCALLTYPE *Load)(IPersistFile *This, LPCOLESTR file, DWORD mode);
    HRESULT(STDMETHODCALLTYPE *Save)(IPersistFile *This, LPCOLESTR file, BOOL remember);
    HRESULT(STDMETHODCALLTYPE *SaveCompleted)(IPersistFile *This, LPCOLESTR file);
    HRESULT(STDMETHODCALLTYPE *GetCurFile)(IPersistFile *This, LPOLESTR *file);
} IPersistFileVtbl;
struct IPersistFile {
    const IPersistFileVtbl *lpVtbl;
};

typedef struct IPersistIDList IPersistIDList;
typedef struct IPersistIDListVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPersistIDList *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPersistIDList *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPersistIDList *This);
    HRESULT(STDMETHODCALLTYPE *GetClassID)(IPersistIDList *This, CLSID *clsid);
    HRESULT(STDMETHODCALLTYPE *SetIDList)(IPersistIDList *This, PCIDLIST_ABSOLUTE pidl);
    HRESULT(STDMETHODCALLTYPE *GetIDList)(IPersistIDList *This, PIDLIST_ABSOLUTE *pidl);
} IPersistIDListVtbl;
struct IPersistIDList {
    const IPersistIDListVtbl *lpVtbl;
};

typedef struct IOleWindow IOleWindow;
typedef struct IOleWindowVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IOleWindow *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IOleWindow *This);
    ULONG(STDMETHODCALLTYPE *Release)(IOleWindow *This);
    HRESULT(STDMETHODCALLTYPE *GetWindow)(IOleWindow *This, HWND *window);
    HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)(IOleWindow *This, BOOL enter);
} IOleWindowVtbl;
struct IOleWindow {
    const IOleWindowVtbl *lpVtbl;
};

typedef struct IObjectWithSite IObjectWithSite;
typedef struct IObjectWithSiteVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IObjectWithSite *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IObjectWithSite *This);
    ULONG(STDMETHODCALLTYPE *Release)(IObjectWithSite *This);
    HRESULT(STDMETHODCALLTYPE *SetSite)(IObjectWithSite *This, IUnknown *site);
    HRESULT(STDMETHODCALLTYPE *GetSite)(IObjectWithSite *This, REFIID riid, void **site);
} IObjectWithSiteVtbl;
struct IObjectWithSite {
    const IObjectWithSiteVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ shortcuts */

#define SLR_NO_UI 0x0001
#define SLR_ANY_MATCH 0x0002
#define SLR_UPDATE 0x0004
#define SLR_NOUPDATE 0x0008
#define SLR_NOSEARCH 0x0010
#define SLR_NOTRACK 0x0020
#define SLR_NOLINKINFO 0x0040
#define SLR_INVOKE_MSI 0x0080
#define SLR_NO_UI_WITH_MSG_PUMP 0x0101
#define SLR_OFFER_DELETE_WITHOUT_FILE 0x0200
#define SLR_KNOWNFOLDER 0x0400
#define SLR_MACHINE_IN_LOCAL_TARGET 0x0800
#define SLR_UPDATE_MACHINE_AND_SID 0x1000

#define SLGP_SHORTPATH 0x0001
#define SLGP_UNCPRIORITY 0x0002
#define SLGP_RAWPATH 0x0004
#define SLGP_RELATIVEPRIORITY 0x0008

typedef struct IShellLinkA IShellLinkA;
typedef struct IShellLinkAVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellLinkA *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellLinkA *This);
    ULONG(STDMETHODCALLTYPE *Release)(IShellLinkA *This);
    HRESULT(STDMETHODCALLTYPE *GetPath)(IShellLinkA *This, LPSTR file, int chars, WIN32_FIND_DATAA *data, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *GetIDList)(IShellLinkA *This, PIDLIST_ABSOLUTE *pidl);
    HRESULT(STDMETHODCALLTYPE *SetIDList)(IShellLinkA *This, PCIDLIST_ABSOLUTE pidl);
    HRESULT(STDMETHODCALLTYPE *GetDescription)(IShellLinkA *This, LPSTR name, int chars);
    HRESULT(STDMETHODCALLTYPE *SetDescription)(IShellLinkA *This, LPCSTR name);
    HRESULT(STDMETHODCALLTYPE *GetWorkingDirectory)(IShellLinkA *This, LPSTR directory, int chars);
    HRESULT(STDMETHODCALLTYPE *SetWorkingDirectory)(IShellLinkA *This, LPCSTR directory);
    HRESULT(STDMETHODCALLTYPE *GetArguments)(IShellLinkA *This, LPSTR arguments, int chars);
    HRESULT(STDMETHODCALLTYPE *SetArguments)(IShellLinkA *This, LPCSTR arguments);
    HRESULT(STDMETHODCALLTYPE *GetHotkey)(IShellLinkA *This, WORD *hotkey);
    HRESULT(STDMETHODCALLTYPE *SetHotkey)(IShellLinkA *This, WORD hotkey);
    HRESULT(STDMETHODCALLTYPE *GetShowCmd)(IShellLinkA *This, int *show);
    HRESULT(STDMETHODCALLTYPE *SetShowCmd)(IShellLinkA *This, int show);
    HRESULT(STDMETHODCALLTYPE *GetIconLocation)(IShellLinkA *This, LPSTR path, int chars, int *index);
    HRESULT(STDMETHODCALLTYPE *SetIconLocation)(IShellLinkA *This, LPCSTR path, int index);
    HRESULT(STDMETHODCALLTYPE *SetRelativePath)(IShellLinkA *This, LPCSTR path, DWORD reserved);
    HRESULT(STDMETHODCALLTYPE *Resolve)(IShellLinkA *This, HWND hwnd, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *SetPath)(IShellLinkA *This, LPCSTR file);
} IShellLinkAVtbl;
struct IShellLinkA {
    const IShellLinkAVtbl *lpVtbl;
};

typedef struct IShellLinkW IShellLinkW;
typedef struct IShellLinkWVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellLinkW *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellLinkW *This);
    ULONG(STDMETHODCALLTYPE *Release)(IShellLinkW *This);
    HRESULT(STDMETHODCALLTYPE *GetPath)(IShellLinkW *This, LPWSTR file, int chars, WIN32_FIND_DATAW *data,
                                        DWORD flags);
    HRESULT(STDMETHODCALLTYPE *GetIDList)(IShellLinkW *This, PIDLIST_ABSOLUTE *pidl);
    HRESULT(STDMETHODCALLTYPE *SetIDList)(IShellLinkW *This, PCIDLIST_ABSOLUTE pidl);
    HRESULT(STDMETHODCALLTYPE *GetDescription)(IShellLinkW *This, LPWSTR name, int chars);
    HRESULT(STDMETHODCALLTYPE *SetDescription)(IShellLinkW *This, LPCWSTR name);
    HRESULT(STDMETHODCALLTYPE *GetWorkingDirectory)(IShellLinkW *This, LPWSTR directory, int chars);
    HRESULT(STDMETHODCALLTYPE *SetWorkingDirectory)(IShellLinkW *This, LPCWSTR directory);
    HRESULT(STDMETHODCALLTYPE *GetArguments)(IShellLinkW *This, LPWSTR arguments, int chars);
    HRESULT(STDMETHODCALLTYPE *SetArguments)(IShellLinkW *This, LPCWSTR arguments);
    HRESULT(STDMETHODCALLTYPE *GetHotkey)(IShellLinkW *This, WORD *hotkey);
    HRESULT(STDMETHODCALLTYPE *SetHotkey)(IShellLinkW *This, WORD hotkey);
    HRESULT(STDMETHODCALLTYPE *GetShowCmd)(IShellLinkW *This, int *show);
    HRESULT(STDMETHODCALLTYPE *SetShowCmd)(IShellLinkW *This, int show);
    HRESULT(STDMETHODCALLTYPE *GetIconLocation)(IShellLinkW *This, LPWSTR path, int chars, int *index);
    HRESULT(STDMETHODCALLTYPE *SetIconLocation)(IShellLinkW *This, LPCWSTR path, int index);
    HRESULT(STDMETHODCALLTYPE *SetRelativePath)(IShellLinkW *This, LPCWSTR path, DWORD reserved);
    HRESULT(STDMETHODCALLTYPE *Resolve)(IShellLinkW *This, HWND hwnd, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *SetPath)(IShellLinkW *This, LPCWSTR file);
} IShellLinkWVtbl;
struct IShellLinkW {
    const IShellLinkWVtbl *lpVtbl;
};

/* LinkFlags in the shortcut header, SHELL_LINK_DATA_FLAGS. */
#define SLDF_HAS_ID_LIST 0x00000001
#define SLDF_HAS_LINK_INFO 0x00000002
#define SLDF_HAS_NAME 0x00000004
#define SLDF_HAS_RELPATH 0x00000008
#define SLDF_HAS_WORKINGDIR 0x00000010
#define SLDF_HAS_ARGS 0x00000020
#define SLDF_HAS_ICONLOCATION 0x00000040
#define SLDF_UNICODE 0x00000080
#define SLDF_FORCE_NO_LINKINFO 0x00000100
#define SLDF_HAS_EXP_SZ 0x00000200
#define SLDF_RUN_IN_SEPARATE 0x00000400
#define SLDF_HAS_DARWINID 0x00001000
#define SLDF_RUNAS_USER 0x00002000
#define SLDF_HAS_EXP_ICON_SZ 0x00004000
#define SLDF_NO_PIDL_ALIAS 0x00008000
#define SLDF_RUN_WITH_SHIMLAYER 0x00020000
#define SLDF_FORCE_NO_LINKTRACK 0x00040000
#define SLDF_ENABLE_TARGET_METADATA 0x00080000
#define SLDF_DISABLE_LINK_PATH_TRACKING 0x00100000
#define SLDF_DISABLE_KNOWNFOLDER_RELATIVE_TRACKING 0x00200000
#define SLDF_NO_KF_ALIAS 0x00400000
#define SLDF_ALLOW_LINK_TO_LINK 0x00800000
#define SLDF_UNALIAS_ON_SAVE 0x01000000
#define SLDF_PREFER_ENVIRONMENT_PATH 0x02000000
#define SLDF_KEEP_LOCAL_IDLIST_FOR_UNC_TARGET 0x04000000

/* extra data blocks each start with a size and a signature. */
typedef struct tagDATABLOCKHEADER {
    DWORD cbSize;
    DWORD dwSignature;
} DATABLOCK_HEADER, *LPDATABLOCK_HEADER, *LPDBLIST;

#define EXP_SZ_LINK_SIG 0xA0000001
#define NT_CONSOLE_PROPS_SIG 0xA0000002
#define NT_FE_CONSOLE_PROPS_SIG 0xA0000004
#define EXP_SPECIAL_FOLDER_SIG 0xA0000005
#define EXP_DARWIN_ID_SIG 0xA0000006
#define EXP_SZ_ICON_SIG 0xA0000007
#define EXP_PROPERTYSTORAGE_SIG 0xA0000009
#define EXP_KNOWN_FOLDER_SIG 0xA000000B

typedef struct {
    DWORD cbSize;
    DWORD dwSignature;
    CHAR szTarget[MAX_PATH];
    WCHAR swzTarget[MAX_PATH];
} EXP_SZ_LINK;

typedef struct {
    DWORD cbSize;
    DWORD dwSignature;
    DWORD idSpecialFolder;
    DWORD cbOffset;
} EXP_SPECIAL_FOLDER;

typedef struct {
    DWORD cbSize;
    DWORD dwSignature;
    GUID knownFolderID;
    DWORD offset;
} EXP_KNOWN_FOLDER;

typedef struct IShellLinkDataList IShellLinkDataList;
typedef struct IShellLinkDataListVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellLinkDataList *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellLinkDataList *This);
    ULONG(STDMETHODCALLTYPE *Release)(IShellLinkDataList *This);
    HRESULT(STDMETHODCALLTYPE *AddDataBlock)(IShellLinkDataList *This, void *block);
    HRESULT(STDMETHODCALLTYPE *CopyDataBlock)(IShellLinkDataList *This, DWORD signature, void **block);
    HRESULT(STDMETHODCALLTYPE *RemoveDataBlock)(IShellLinkDataList *This, DWORD signature);
    HRESULT(STDMETHODCALLTYPE *GetFlags)(IShellLinkDataList *This, DWORD *flags);
    HRESULT(STDMETHODCALLTYPE *SetFlags)(IShellLinkDataList *This, DWORD flags);
} IShellLinkDataListVtbl;
struct IShellLinkDataList {
    const IShellLinkDataListVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ icons and thumbnails */

MIME_DEFINE_GUID(IID_IShellItemImageFactory, 0xBCC18B79, 0xBA16, 0x442F, 0x80, 0xC4, 0x8A, 0x59, 0xC3, 0x0C, 0x46, 0x3B);
MIME_DEFINE_GUID(IID_IExtractIconA, 0x000214EB, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IExtractIconW, 0x000214FA, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

typedef int SIIGBF;
#define SIIGBF_RESIZETOFIT 0x00000000
#define SIIGBF_BIGGERSIZEOK 0x00000001
#define SIIGBF_MEMORYONLY 0x00000002
#define SIIGBF_ICONONLY 0x00000004
#define SIIGBF_THUMBNAILONLY 0x00000008
#define SIIGBF_INCACHEONLY 0x00000010
#define SIIGBF_CROPTOSQUARE 0x00000020
#define SIIGBF_WIDETHUMBNAILS 0x00000040
#define SIIGBF_ICONBACKGROUND 0x00000080
#define SIIGBF_SCALEUP 0x00000100

/* thumbnail extraction failures, from thumbcache.h. */
#define WTS_E_FAILEDEXTRACTION ((HRESULT)0x8004B200L)
#define WTS_E_EXTRACTIONTIMEDOUT ((HRESULT)0x8004B201L)
#define WTS_E_SURROGATEUNAVAILABLE ((HRESULT)0x8004B202L)
#define WTS_E_FASTEXTRACTIONNOTSUPPORTED ((HRESULT)0x8004B203L)
#define WTS_E_DATAFILEUNAVAILABLE ((HRESULT)0x8004B204L)

typedef struct IShellItemImageFactory IShellItemImageFactory;
typedef struct IShellItemImageFactoryVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellItemImageFactory *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellItemImageFactory *This);
    ULONG(STDMETHODCALLTYPE *Release)(IShellItemImageFactory *This);
    HRESULT(STDMETHODCALLTYPE *GetImage)(IShellItemImageFactory *This, SIZE size, SIIGBF flags, HBITMAP *bitmap);
} IShellItemImageFactoryVtbl;
struct IShellItemImageFactory {
    const IShellItemImageFactoryVtbl *lpVtbl;
};

#define GIL_OPENICON 0x0001
#define GIL_FORSHELL 0x0002
#define GIL_ASYNC 0x0020
#define GIL_DEFAULTICON 0x0040
#define GIL_FORSHORTCUT 0x0080
#define GIL_CHECKSHIELD 0x0200
#define GIL_SIMULATEDOC 0x0001
#define GIL_PERINSTANCE 0x0002
#define GIL_PERCLASS 0x0004
#define GIL_NOTFILENAME 0x0008
#define GIL_DONTCACHE 0x0010
#define GIL_SHIELD 0x0200
#define GIL_FORCENOSHIELD 0x0400

typedef struct IExtractIconW IExtractIconW;
typedef struct IExtractIconWVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IExtractIconW *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IExtractIconW *This);
    ULONG(STDMETHODCALLTYPE *Release)(IExtractIconW *This);
    HRESULT(STDMETHODCALLTYPE *GetIconLocation)(IExtractIconW *This, UINT flags, PWSTR file, UINT chars, int *index,
                                                UINT *out_flags);
    HRESULT(STDMETHODCALLTYPE *Extract)(IExtractIconW *This, PCWSTR file, UINT index, HICON *large, HICON *small,
                                        UINT size);
} IExtractIconWVtbl;
struct IExtractIconW {
    const IExtractIconWVtbl *lpVtbl;
};

typedef struct IExtractIconA IExtractIconA;
typedef struct IExtractIconAVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IExtractIconA *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IExtractIconA *This);
    ULONG(STDMETHODCALLTYPE *Release)(IExtractIconA *This);
    HRESULT(STDMETHODCALLTYPE *GetIconLocation)(IExtractIconA *This, UINT flags, PSTR file, UINT chars, int *index,
                                                UINT *out_flags);
    HRESULT(STDMETHODCALLTYPE *Extract)(IExtractIconA *This, PCSTR file, UINT index, HICON *large, HICON *small,
                                        UINT size);
} IExtractIconAVtbl;
struct IExtractIconA {
    const IExtractIconAVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ shell item arrays and enumerators */

typedef struct IEnumShellItems IEnumShellItems;
typedef struct IEnumShellItemsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumShellItems *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumShellItems *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumShellItems *This);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumShellItems *This, ULONG count, IShellItem **items, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumShellItems *This, ULONG count);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumShellItems *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumShellItems *This, IEnumShellItems **clone);
} IEnumShellItemsVtbl;
struct IEnumShellItems {
    const IEnumShellItemsVtbl *lpVtbl;
};

typedef int GETPROPERTYSTOREFLAGS;
typedef int SIATTRIBFLAGS;
#define SIATTRIBFLAGS_AND 0x1
#define SIATTRIBFLAGS_OR 0x2
#define SIATTRIBFLAGS_APPCOMPAT 0x3

typedef struct IShellItemArray IShellItemArray;
typedef struct IShellItemArrayVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellItemArray *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellItemArray *This);
    ULONG(STDMETHODCALLTYPE *Release)(IShellItemArray *This);
    HRESULT(STDMETHODCALLTYPE *BindToHandler)(IShellItemArray *This, IBindCtx *context, REFGUID handler, REFIID riid,
                                              void **object);
    HRESULT(STDMETHODCALLTYPE *GetPropertyStore)(IShellItemArray *This, GETPROPERTYSTOREFLAGS flags, REFIID riid,
                                                 void **object);
    HRESULT(STDMETHODCALLTYPE *GetPropertyDescriptionList)(IShellItemArray *This, REFPROPERTYKEY key, REFIID riid,
                                                           void **object);
    HRESULT(STDMETHODCALLTYPE *GetAttributes)(IShellItemArray *This, SIATTRIBFLAGS how, SFGAOF mask,
                                              SFGAOF *attributes);
    HRESULT(STDMETHODCALLTYPE *GetCount)(IShellItemArray *This, DWORD *count);
    HRESULT(STDMETHODCALLTYPE *GetItemAt)(IShellItemArray *This, DWORD index, IShellItem **item);
    HRESULT(STDMETHODCALLTYPE *EnumItems)(IShellItemArray *This, IEnumShellItems **enumerator);
} IShellItemArrayVtbl;
struct IShellItemArray {
    const IShellItemArrayVtbl *lpVtbl;
};

typedef DWORD SHCONTF;
#define SHCONTF_FOLDERS 0x0020
#define SHCONTF_NONFOLDERS 0x0040
#define SHCONTF_INCLUDEHIDDEN 0x0080

typedef struct IShellItemFilter IShellItemFilter;
typedef struct IShellItemFilterVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellItemFilter *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellItemFilter *This);
    ULONG(STDMETHODCALLTYPE *Release)(IShellItemFilter *This);
    HRESULT(STDMETHODCALLTYPE *IncludeItem)(IShellItemFilter *This, IShellItem *item);
    HRESULT(STDMETHODCALLTYPE *GetEnumFlagsForItem)(IShellItemFilter *This, IShellItem *item, SHCONTF *flags);
} IShellItemFilterVtbl;
struct IShellItemFilter {
    const IShellItemFilterVtbl *lpVtbl;
};

/* bind context parameter carrying find data for an item that may not exist. */
typedef struct IFileSystemBindData IFileSystemBindData;
typedef struct IFileSystemBindDataVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IFileSystemBindData *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IFileSystemBindData *This);
    ULONG(STDMETHODCALLTYPE *Release)(IFileSystemBindData *This);
    HRESULT(STDMETHODCALLTYPE *SetFindData)(IFileSystemBindData *This, const WIN32_FIND_DATAW *data);
    HRESULT(STDMETHODCALLTYPE *GetFindData)(IFileSystemBindData *This, WIN32_FIND_DATAW *data);
} IFileSystemBindDataVtbl;
struct IFileSystemBindData {
    const IFileSystemBindDataVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ common item dialogs */

typedef struct IModalWindow IModalWindow;
typedef struct IModalWindowVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IModalWindow *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IModalWindow *This);
    ULONG(STDMETHODCALLTYPE *Release)(IModalWindow *This);
    HRESULT(STDMETHODCALLTYPE *Show)(IModalWindow *This, HWND owner);
} IModalWindowVtbl;
struct IModalWindow {
    const IModalWindowVtbl *lpVtbl;
};

typedef struct _COMDLG_FILTERSPEC {
    LPCWSTR pszName;
    LPCWSTR pszSpec;
} COMDLG_FILTERSPEC;

typedef DWORD FILEOPENDIALOGOPTIONS;
#define FOS_OVERWRITEPROMPT 0x00000002
#define FOS_STRICTFILETYPES 0x00000004
#define FOS_NOCHANGEDIR 0x00000008
#define FOS_PICKFOLDERS 0x00000020
#define FOS_FORCEFILESYSTEM 0x00000040
#define FOS_ALLNONSTORAGEITEMS 0x00000080
#define FOS_NOVALIDATE 0x00000100
#define FOS_ALLOWMULTISELECT 0x00000200
#define FOS_PATHMUSTEXIST 0x00000800
#define FOS_FILEMUSTEXIST 0x00001000
#define FOS_CREATEPROMPT 0x00002000
#define FOS_SHAREAWARE 0x00004000
#define FOS_NOREADONLYRETURN 0x00008000
#define FOS_NOTESTFILECREATE 0x00010000
#define FOS_HIDEMRUPLACES 0x00020000
#define FOS_HIDEPINNEDPLACES 0x00040000
#define FOS_NODEREFERENCELINKS 0x00100000
#define FOS_OKBUTTONNEEDSINTERACTION 0x00200000
#define FOS_DONTADDTORECENT 0x02000000
#define FOS_FORCESHOWHIDDEN 0x10000000
#define FOS_DEFAULTNOMINIMODE 0x20000000
#define FOS_FORCEPREVIEWPANEON 0x40000000
#define FOS_SUPPORTSTREAMABLEITEMS 0x80000000

typedef enum _FDAP {
    FDAP_BOTTOM = 0,
    FDAP_TOP = 1,
} FDAP;

typedef enum FDE_SHAREVIOLATION_RESPONSE {
    FDESVR_DEFAULT = 0,
    FDESVR_ACCEPT = 1,
    FDESVR_REFUSE = 2,
} FDE_SHAREVIOLATION_RESPONSE;

typedef enum FDE_OVERWRITE_RESPONSE {
    FDEOR_DEFAULT = 0,
    FDEOR_ACCEPT = 1,
    FDEOR_REFUSE = 2,
} FDE_OVERWRITE_RESPONSE;

typedef int CDCONTROLSTATEF;
#define CDCS_INACTIVE 0x0
#define CDCS_ENABLED 0x1
#define CDCS_VISIBLE 0x2
#define CDCS_ENABLEDVISIBLE 0x3

typedef struct IFileDialog IFileDialog;
typedef struct IFileDialogEvents IFileDialogEvents;
typedef struct IFileDialogEventsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IFileDialogEvents *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IFileDialogEvents *This);
    ULONG(STDMETHODCALLTYPE *Release)(IFileDialogEvents *This);
    HRESULT(STDMETHODCALLTYPE *OnFileOk)(IFileDialogEvents *This, IFileDialog *dialog);
    HRESULT(STDMETHODCALLTYPE *OnFolderChanging)(IFileDialogEvents *This, IFileDialog *dialog, IShellItem *folder);
    HRESULT(STDMETHODCALLTYPE *OnFolderChange)(IFileDialogEvents *This, IFileDialog *dialog);
    HRESULT(STDMETHODCALLTYPE *OnSelectionChange)(IFileDialogEvents *This, IFileDialog *dialog);
    HRESULT(STDMETHODCALLTYPE *OnShareViolation)(IFileDialogEvents *This, IFileDialog *dialog, IShellItem *item,
                                                 FDE_SHAREVIOLATION_RESPONSE *response);
    HRESULT(STDMETHODCALLTYPE *OnTypeChange)(IFileDialogEvents *This, IFileDialog *dialog);
    HRESULT(STDMETHODCALLTYPE *OnOverwrite)(IFileDialogEvents *This, IFileDialog *dialog, IShellItem *item,
                                            FDE_OVERWRITE_RESPONSE *response);
} IFileDialogEventsVtbl;
struct IFileDialogEvents {
    const IFileDialogEventsVtbl *lpVtbl;
};

typedef struct IFileDialogVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IFileDialog *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IFileDialog *This);
    ULONG(STDMETHODCALLTYPE *Release)(IFileDialog *This);
    HRESULT(STDMETHODCALLTYPE *Show)(IFileDialog *This, HWND owner);
    HRESULT(STDMETHODCALLTYPE *SetFileTypes)(IFileDialog *This, UINT count, const COMDLG_FILTERSPEC *types);
    HRESULT(STDMETHODCALLTYPE *SetFileTypeIndex)(IFileDialog *This, UINT index);
    HRESULT(STDMETHODCALLTYPE *GetFileTypeIndex)(IFileDialog *This, UINT *index);
    HRESULT(STDMETHODCALLTYPE *Advise)(IFileDialog *This, IFileDialogEvents *events, DWORD *cookie);
    HRESULT(STDMETHODCALLTYPE *Unadvise)(IFileDialog *This, DWORD cookie);
    HRESULT(STDMETHODCALLTYPE *SetOptions)(IFileDialog *This, FILEOPENDIALOGOPTIONS options);
    HRESULT(STDMETHODCALLTYPE *GetOptions)(IFileDialog *This, FILEOPENDIALOGOPTIONS *options);
    HRESULT(STDMETHODCALLTYPE *SetDefaultFolder)(IFileDialog *This, IShellItem *folder);
    HRESULT(STDMETHODCALLTYPE *SetFolder)(IFileDialog *This, IShellItem *folder);
    HRESULT(STDMETHODCALLTYPE *GetFolder)(IFileDialog *This, IShellItem **folder);
    HRESULT(STDMETHODCALLTYPE *GetCurrentSelection)(IFileDialog *This, IShellItem **item);
    HRESULT(STDMETHODCALLTYPE *SetFileName)(IFileDialog *This, LPCWSTR name);
    HRESULT(STDMETHODCALLTYPE *GetFileName)(IFileDialog *This, LPWSTR *name);
    HRESULT(STDMETHODCALLTYPE *SetTitle)(IFileDialog *This, LPCWSTR title);
    HRESULT(STDMETHODCALLTYPE *SetOkButtonLabel)(IFileDialog *This, LPCWSTR text);
    HRESULT(STDMETHODCALLTYPE *SetFileNameLabel)(IFileDialog *This, LPCWSTR text);
    HRESULT(STDMETHODCALLTYPE *GetResult)(IFileDialog *This, IShellItem **item);
    HRESULT(STDMETHODCALLTYPE *AddPlace)(IFileDialog *This, IShellItem *place, FDAP position);
    HRESULT(STDMETHODCALLTYPE *SetDefaultExtension)(IFileDialog *This, LPCWSTR extension);
    HRESULT(STDMETHODCALLTYPE *Close)(IFileDialog *This, HRESULT result);
    HRESULT(STDMETHODCALLTYPE *SetClientGuid)(IFileDialog *This, REFGUID guid);
    HRESULT(STDMETHODCALLTYPE *ClearClientData)(IFileDialog *This);
    HRESULT(STDMETHODCALLTYPE *SetFilter)(IFileDialog *This, IShellItemFilter *filter);
} IFileDialogVtbl;
struct IFileDialog {
    const IFileDialogVtbl *lpVtbl;
};

typedef struct IFileDialog2 IFileDialog2;
typedef struct IFileDialog2Vtbl {
    IFileDialogVtbl Base;
    HRESULT(STDMETHODCALLTYPE *SetCancelButtonLabel)(IFileDialog2 *This, LPCWSTR text);
    HRESULT(STDMETHODCALLTYPE *SetNavigationRoot)(IFileDialog2 *This, IShellItem *root);
} IFileDialog2Vtbl;
struct IFileDialog2 {
    const IFileDialog2Vtbl *lpVtbl;
};

typedef struct IFileOpenDialog IFileOpenDialog;
typedef struct IFileOpenDialogVtbl {
    IFileDialogVtbl Base;
    HRESULT(STDMETHODCALLTYPE *GetResults)(IFileOpenDialog *This, IShellItemArray **items);
    HRESULT(STDMETHODCALLTYPE *GetSelectedItems)(IFileOpenDialog *This, IShellItemArray **items);
} IFileOpenDialogVtbl;
struct IFileOpenDialog {
    const IFileOpenDialogVtbl *lpVtbl;
};

typedef struct IFileOperationProgressSink IFileOperationProgressSink;

/* IFileOperation::SetOperationFlags values beyond the FOF flags of shellapi.h. */
#define FOF_NO_UI (FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR)
#define FOFX_NOSKIPJUNCTIONS 0x00010000
#define FOFX_PREFERHARDLINK 0x00020000
#define FOFX_SHOWELEVATIONPROMPT 0x00040000
#define FOFX_EARLYFAILURE 0x00100000
#define FOFX_PRESERVEFILEEXTENSIONS 0x00200000
#define FOFX_KEEPNEWERFILE 0x00400000
#define FOFX_NOCOPYHOOKS 0x00800000
#define FOFX_NOMINIMIZEBOX 0x01000000
#define FOFX_MOVEACLSACROSSVOLUMES 0x02000000
#define FOFX_DONTDISPLAYSOURCEPATH 0x04000000
#define FOFX_DONTDISPLAYDESTPATH 0x08000000
#define FOFX_REQUIREELEVATION 0x10000000
#define FOFX_ADDUNDORECORD 0x20000000
#define FOFX_COPYASDOWNLOAD 0x40000000
#define FOFX_DONTDISPLAYLOCATIONS 0x80000000

/* flags passed to the progress sink's Pre and Post calls. */
#define TSF_NORMAL 0
#define TSF_FAIL_EXIST 0
#define TSF_RENAME_EXIST 0x1
#define TSF_OVERWRITE_EXIST 0x2
#define TSF_ALLOW_DECRYPTION 0x4
#define TSF_NO_SECURITY 0x8
#define TSF_COPY_CREATION_TIME 0x10
#define TSF_COPY_WRITE_TIME 0x20
#define TSF_USE_FULL_ACCESS 0x40
#define TSF_DELETE_RECYCLE_IF_POSSIBLE 0x80
#define TSF_COPY_HARD_LINK 0x100
#define TSF_COPY_LOCALIZED_NAME 0x200
#define TSF_MOVE_AS_COPY_DELETE 0x400
#define TSF_SUSPEND_SHELLEVENTS 0x800

/* copy engine success codes a sink may get in a Post call. */
#define COPYENGINE_S_DONT_PROCESS_CHILDREN ((HRESULT)0x00270008L)
#define COPYENGINE_E_USER_CANCELLED ((HRESULT)0x80270000L)

typedef struct IFileOperationProgressSinkVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IFileOperationProgressSink *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IFileOperationProgressSink *This);
    ULONG(STDMETHODCALLTYPE *Release)(IFileOperationProgressSink *This);
    HRESULT(STDMETHODCALLTYPE *StartOperations)(IFileOperationProgressSink *This);
    HRESULT(STDMETHODCALLTYPE *FinishOperations)(IFileOperationProgressSink *This, HRESULT result);
    HRESULT(STDMETHODCALLTYPE *PreRenameItem)(IFileOperationProgressSink *This, DWORD flags, IShellItem *item,
                                              LPCWSTR new_name);
    HRESULT(STDMETHODCALLTYPE *PostRenameItem)(IFileOperationProgressSink *This, DWORD flags, IShellItem *item,
                                               LPCWSTR new_name, HRESULT result, IShellItem *created);
    HRESULT(STDMETHODCALLTYPE *PreMoveItem)(IFileOperationProgressSink *This, DWORD flags, IShellItem *item,
                                            IShellItem *folder, LPCWSTR new_name);
    HRESULT(STDMETHODCALLTYPE *PostMoveItem)(IFileOperationProgressSink *This, DWORD flags, IShellItem *item,
                                             IShellItem *folder, LPCWSTR new_name, HRESULT result, IShellItem *created);
    HRESULT(STDMETHODCALLTYPE *PreCopyItem)(IFileOperationProgressSink *This, DWORD flags, IShellItem *item,
                                            IShellItem *folder, LPCWSTR new_name);
    HRESULT(STDMETHODCALLTYPE *PostCopyItem)(IFileOperationProgressSink *This, DWORD flags, IShellItem *item,
                                             IShellItem *folder, LPCWSTR new_name, HRESULT result, IShellItem *created);
    HRESULT(STDMETHODCALLTYPE *PreDeleteItem)(IFileOperationProgressSink *This, DWORD flags, IShellItem *item);
    HRESULT(STDMETHODCALLTYPE *PostDeleteItem)(IFileOperationProgressSink *This, DWORD flags, IShellItem *item,
                                               HRESULT result, IShellItem *created);
    HRESULT(STDMETHODCALLTYPE *PreNewItem)(IFileOperationProgressSink *This, DWORD flags, IShellItem *folder,
                                           LPCWSTR new_name);
    HRESULT(STDMETHODCALLTYPE *PostNewItem)(IFileOperationProgressSink *This, DWORD flags, IShellItem *folder,
                                            LPCWSTR new_name, LPCWSTR template_name, DWORD attributes, HRESULT result,
                                            IShellItem *item);
    HRESULT(STDMETHODCALLTYPE *UpdateProgress)(IFileOperationProgressSink *This, UINT total, UINT so_far);
    HRESULT(STDMETHODCALLTYPE *ResetTimer)(IFileOperationProgressSink *This);
    HRESULT(STDMETHODCALLTYPE *PauseTimer)(IFileOperationProgressSink *This);
    HRESULT(STDMETHODCALLTYPE *ResumeTimer)(IFileOperationProgressSink *This);
} IFileOperationProgressSinkVtbl;
struct IFileOperationProgressSink {
    const IFileOperationProgressSinkVtbl *lpVtbl;
};

typedef struct IOperationsProgressDialog IOperationsProgressDialog;
typedef struct IPropertyChangeArray IPropertyChangeArray;
typedef struct IFileOperation IFileOperation;
typedef struct IFileOperationVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IFileOperation *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IFileOperation *This);
    ULONG(STDMETHODCALLTYPE *Release)(IFileOperation *This);
    HRESULT(STDMETHODCALLTYPE *Advise)(IFileOperation *This, IFileOperationProgressSink *sink, DWORD *cookie);
    HRESULT(STDMETHODCALLTYPE *Unadvise)(IFileOperation *This, DWORD cookie);
    HRESULT(STDMETHODCALLTYPE *SetOperationFlags)(IFileOperation *This, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *SetProgressMessage)(IFileOperation *This, LPCWSTR message);
    HRESULT(STDMETHODCALLTYPE *SetProgressDialog)(IFileOperation *This, IOperationsProgressDialog *dialog);
    HRESULT(STDMETHODCALLTYPE *SetProperties)(IFileOperation *This, IPropertyChangeArray *properties);
    HRESULT(STDMETHODCALLTYPE *SetOwnerWindow)(IFileOperation *This, HWND owner);
    HRESULT(STDMETHODCALLTYPE *ApplyPropertiesToItem)(IFileOperation *This, IShellItem *item);
    HRESULT(STDMETHODCALLTYPE *ApplyPropertiesToItems)(IFileOperation *This, IUnknown *items);
    HRESULT(STDMETHODCALLTYPE *RenameItem)(IFileOperation *This, IShellItem *item, LPCWSTR new_name,
                                           IFileOperationProgressSink *sink);
    HRESULT(STDMETHODCALLTYPE *RenameItems)(IFileOperation *This, IUnknown *items, LPCWSTR new_name);
    HRESULT(STDMETHODCALLTYPE *MoveItem)(IFileOperation *This, IShellItem *item, IShellItem *folder, LPCWSTR new_name,
                                         IFileOperationProgressSink *sink);
    HRESULT(STDMETHODCALLTYPE *MoveItems)(IFileOperation *This, IUnknown *items, IShellItem *folder);
    HRESULT(STDMETHODCALLTYPE *CopyItem)(IFileOperation *This, IShellItem *item, IShellItem *folder, LPCWSTR copy_name,
                                         IFileOperationProgressSink *sink);
    HRESULT(STDMETHODCALLTYPE *CopyItems)(IFileOperation *This, IUnknown *items, IShellItem *folder);
    HRESULT(STDMETHODCALLTYPE *DeleteItem)(IFileOperation *This, IShellItem *item, IFileOperationProgressSink *sink);
    HRESULT(STDMETHODCALLTYPE *DeleteItems)(IFileOperation *This, IUnknown *items);
    HRESULT(STDMETHODCALLTYPE *NewItem)(IFileOperation *This, IShellItem *folder, DWORD attributes, LPCWSTR name,
                                        LPCWSTR template_name, IFileOperationProgressSink *sink);
    HRESULT(STDMETHODCALLTYPE *PerformOperations)(IFileOperation *This);
    HRESULT(STDMETHODCALLTYPE *GetAnyOperationsAborted)(IFileOperation *This, BOOL *aborted);
} IFileOperationVtbl;
struct IFileOperation {
    const IFileOperationVtbl *lpVtbl;
};

typedef struct IPropertyDescriptionList IPropertyDescriptionList;
typedef struct IFileSaveDialog IFileSaveDialog;
typedef struct IFileSaveDialogVtbl {
    IFileDialogVtbl Base;
    HRESULT(STDMETHODCALLTYPE *SetSaveAsItem)(IFileSaveDialog *This, IShellItem *item);
    HRESULT(STDMETHODCALLTYPE *SetProperties)(IFileSaveDialog *This, IPropertyStore *store);
    HRESULT(STDMETHODCALLTYPE *SetCollectedProperties)(IFileSaveDialog *This, IPropertyDescriptionList *list,
                                                       BOOL append_default);
    HRESULT(STDMETHODCALLTYPE *GetProperties)(IFileSaveDialog *This, IPropertyStore **store);
    HRESULT(STDMETHODCALLTYPE *ApplyProperties)(IFileSaveDialog *This, IShellItem *item, IPropertyStore *store,
                                                HWND owner, IFileOperationProgressSink *sink);
} IFileSaveDialogVtbl;
struct IFileSaveDialog {
    const IFileSaveDialogVtbl *lpVtbl;
};

typedef struct IFileDialogCustomize IFileDialogCustomize;
typedef struct IFileDialogCustomizeVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IFileDialogCustomize *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IFileDialogCustomize *This);
    ULONG(STDMETHODCALLTYPE *Release)(IFileDialogCustomize *This);
    HRESULT(STDMETHODCALLTYPE *EnableOpenDropDown)(IFileDialogCustomize *This, DWORD id);
    HRESULT(STDMETHODCALLTYPE *AddMenu)(IFileDialogCustomize *This, DWORD id, LPCWSTR label);
    HRESULT(STDMETHODCALLTYPE *AddPushButton)(IFileDialogCustomize *This, DWORD id, LPCWSTR label);
    HRESULT(STDMETHODCALLTYPE *AddComboBox)(IFileDialogCustomize *This, DWORD id);
    HRESULT(STDMETHODCALLTYPE *AddRadioButtonList)(IFileDialogCustomize *This, DWORD id);
    HRESULT(STDMETHODCALLTYPE *AddCheckButton)(IFileDialogCustomize *This, DWORD id, LPCWSTR label, BOOL checked);
    HRESULT(STDMETHODCALLTYPE *AddEditBox)(IFileDialogCustomize *This, DWORD id, LPCWSTR text);
    HRESULT(STDMETHODCALLTYPE *AddSeparator)(IFileDialogCustomize *This, DWORD id);
    HRESULT(STDMETHODCALLTYPE *AddText)(IFileDialogCustomize *This, DWORD id, LPCWSTR text);
    HRESULT(STDMETHODCALLTYPE *SetControlLabel)(IFileDialogCustomize *This, DWORD id, LPCWSTR label);
    HRESULT(STDMETHODCALLTYPE *GetControlState)(IFileDialogCustomize *This, DWORD id, CDCONTROLSTATEF *state);
    HRESULT(STDMETHODCALLTYPE *SetControlState)(IFileDialogCustomize *This, DWORD id, CDCONTROLSTATEF state);
    HRESULT(STDMETHODCALLTYPE *GetEditBoxText)(IFileDialogCustomize *This, DWORD id, WCHAR **text);
    HRESULT(STDMETHODCALLTYPE *SetEditBoxText)(IFileDialogCustomize *This, DWORD id, LPCWSTR text);
    HRESULT(STDMETHODCALLTYPE *GetCheckButtonState)(IFileDialogCustomize *This, DWORD id, BOOL *checked);
    HRESULT(STDMETHODCALLTYPE *SetCheckButtonState)(IFileDialogCustomize *This, DWORD id, BOOL checked);
    HRESULT(STDMETHODCALLTYPE *AddControlItem)(IFileDialogCustomize *This, DWORD id, DWORD item, LPCWSTR label);
    HRESULT(STDMETHODCALLTYPE *RemoveControlItem)(IFileDialogCustomize *This, DWORD id, DWORD item);
    HRESULT(STDMETHODCALLTYPE *RemoveAllControlItems)(IFileDialogCustomize *This, DWORD id);
    HRESULT(STDMETHODCALLTYPE *GetControlItemState)(IFileDialogCustomize *This, DWORD id, DWORD item,
                                                    CDCONTROLSTATEF *state);
    HRESULT(STDMETHODCALLTYPE *SetControlItemState)(IFileDialogCustomize *This, DWORD id, DWORD item,
                                                    CDCONTROLSTATEF state);
    HRESULT(STDMETHODCALLTYPE *GetSelectedControlItem)(IFileDialogCustomize *This, DWORD id, DWORD *item);
    HRESULT(STDMETHODCALLTYPE *SetSelectedControlItem)(IFileDialogCustomize *This, DWORD id, DWORD item);
    HRESULT(STDMETHODCALLTYPE *StartVisualGroup)(IFileDialogCustomize *This, DWORD id, LPCWSTR label);
    HRESULT(STDMETHODCALLTYPE *EndVisualGroup)(IFileDialogCustomize *This);
    HRESULT(STDMETHODCALLTYPE *MakeProminent)(IFileDialogCustomize *This, DWORD id);
    HRESULT(STDMETHODCALLTYPE *SetControlItemText)(IFileDialogCustomize *This, DWORD id, DWORD item, LPCWSTR text);
} IFileDialogCustomizeVtbl;
struct IFileDialogCustomize {
    const IFileDialogCustomizeVtbl *lpVtbl;
};

typedef struct IFileDialogControlEvents IFileDialogControlEvents;
typedef struct IFileDialogControlEventsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IFileDialogControlEvents *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IFileDialogControlEvents *This);
    ULONG(STDMETHODCALLTYPE *Release)(IFileDialogControlEvents *This);
    HRESULT(STDMETHODCALLTYPE *OnItemSelected)(IFileDialogControlEvents *This, IFileDialogCustomize *customize,
                                               DWORD id, DWORD item);
    HRESULT(STDMETHODCALLTYPE *OnButtonClicked)(IFileDialogControlEvents *This, IFileDialogCustomize *customize,
                                                DWORD id);
    HRESULT(STDMETHODCALLTYPE *OnCheckButtonToggled)(IFileDialogControlEvents *This,
                                                     IFileDialogCustomize *customize, DWORD id, BOOL checked);
    HRESULT(STDMETHODCALLTYPE *OnControlActivating)(IFileDialogControlEvents *This, IFileDialogCustomize *customize,
                                                    DWORD id);
} IFileDialogControlEventsVtbl;
struct IFileDialogControlEvents {
    const IFileDialogControlEventsVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ shell32 functions */

SHSTDAPI HRESULT WINAPI SHCreateShellItemArray(PCIDLIST_ABSOLUTE parent, void *folder, UINT count,
                                               PCUITEMID_CHILD *items, IShellItemArray **array);
SHSTDAPI HRESULT WINAPI SHCreateShellItemArrayFromIDLists(UINT count, PCIDLIST_ABSOLUTE *lists,
                                                          IShellItemArray **array);
SHSTDAPI HRESULT WINAPI SHCreateShellItemArrayFromShellItem(IShellItem *item, REFIID riid, void **array);
SHSTDAPI HRESULT WINAPI ILSaveToStream(IStream *stream, PCUIDLIST_RELATIVE pidl);
SHSTDAPI HRESULT WINAPI ILLoadFromStreamEx(IStream *stream, PIDLIST_RELATIVE *pidl);
SHSTDAPI HRESULT WINAPI SHGetIDListFromObject(IUnknown *object, PIDLIST_ABSOLUTE *pidl);
SHSTDAPI HRESULT WINAPI SHCreateItemInKnownFolder(REFKNOWNFOLDERID folder, DWORD flags, PCWSTR name, REFIID riid,
                                                  void **object);
SHSTDAPI HRESULT WINAPI SHCreateItemWithParent(PCIDLIST_ABSOLUTE parent_list, void *parent_folder,
                                               PCUITEMID_CHILD child, REFIID riid, void **object);
SHSTDAPI HRESULT WINAPI SHGetKnownFolderItem(REFKNOWNFOLDERID id, DWORD flags, HANDLE token, REFIID riid,
                                             void **object);

#endif
