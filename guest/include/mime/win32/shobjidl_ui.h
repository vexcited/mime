/*
 * shell interfaces around a window: drag image helpers, the taskbar button and its jump list, from
 * shobjidl_core.h and shlobj_core.h. interfaces are C structs whose first member points to a vtable.
 */
#ifndef MIME_WIN32_SHOBJIDL_UI_H
#define MIME_WIN32_SHOBJIDL_UI_H

#include <mime/win32/objidl.h>
#include <mime/win32/oleidl.h>
#include <mime/win32/propsys.h>
#include <mime/win32/shobjidl.h>

MIME_DEFINE_GUID(CLSID_DragDropHelper, 0x4657278A, 0x411B, 0x11D2, 0x83, 0x9A, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0);
MIME_DEFINE_GUID(IID_IDropTargetHelper, 0x4657278B, 0x411B, 0x11D2, 0x83, 0x9A, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0);
MIME_DEFINE_GUID(IID_IDragSourceHelper, 0xDE5BF786, 0x477A, 0x11D2, 0x83, 0x9D, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0xD0);
MIME_DEFINE_GUID(IID_IDragSourceHelper2, 0x83E07D0D, 0x0C5F, 0x4163, 0xBF, 0x1A, 0x60, 0xB2, 0x74, 0x05, 0x1E, 0x40);

MIME_DEFINE_GUID(CLSID_TaskbarList, 0x56FDF344, 0xFD6D, 0x11D0, 0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90);
MIME_DEFINE_GUID(IID_ITaskbarList, 0x56FDF342, 0xFD6D, 0x11D0, 0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90);
MIME_DEFINE_GUID(IID_ITaskbarList2, 0x602D4995, 0xB13A, 0x429B, 0xA6, 0x6E, 0x19, 0x35, 0xE4, 0x4F, 0x43, 0x17);
MIME_DEFINE_GUID(IID_ITaskbarList3, 0xEA1AFB91, 0x9E28, 0x4B86, 0x90, 0xE9, 0x9E, 0x9F, 0x8A, 0x5E, 0xEF, 0xAF);
MIME_DEFINE_GUID(IID_ITaskbarList4, 0xC43DC798, 0x95D1, 0x4BEA, 0x90, 0x30, 0xBB, 0x99, 0xE2, 0x98, 0x3A, 0x1A);

MIME_DEFINE_GUID(CLSID_DestinationList, 0x77F10CF0, 0x3DB5, 0x4966, 0xB5, 0x20, 0xB7, 0xC5, 0x4F, 0xD3, 0x5E, 0xD6);
MIME_DEFINE_GUID(IID_ICustomDestinationList, 0x6332DEBF, 0x87B5, 0x4670, 0x90, 0xC0, 0x5E, 0x57, 0xB4, 0x08, 0xA4,
                 0x9E);
MIME_DEFINE_GUID(CLSID_EnumerableObjectCollection, 0x2D3468C1, 0x36A7, 0x43B6, 0xAC, 0x24, 0xD3, 0xF0, 0x2F, 0xD9, 0x60,
                 0x7A);
MIME_DEFINE_GUID(IID_IObjectArray, 0x92CA9DCD, 0x5622, 0x4BBA, 0xA8, 0x05, 0x5E, 0x9F, 0x54, 0x1B, 0xD8, 0xC9);
MIME_DEFINE_GUID(IID_IObjectCollection, 0x5632B1A4, 0xE38A, 0x400A, 0x92, 0x8A, 0xD4, 0xCD, 0x63, 0x23, 0x02, 0x95);
MIME_DEFINE_GUID(CLSID_ApplicationDestinations, 0x86C14003, 0x4D6B, 0x4EF3, 0xA7, 0xB4, 0x05, 0x06, 0x66, 0x3B, 0x2E,
                 0x68);
MIME_DEFINE_GUID(IID_IApplicationDestinations, 0x12337D35, 0x94C6, 0x48A0, 0xBC, 0xE7, 0x6A, 0x9C, 0x69, 0xD4, 0xD6,
                 0x00);
MIME_DEFINE_GUID(CLSID_ApplicationDocumentLists, 0x86BEC222, 0x30F2, 0x47E0, 0x9F, 0x25, 0x60, 0xD1, 0x1C, 0xD7, 0x5C,
                 0x28);
MIME_DEFINE_GUID(IID_IApplicationDocumentLists, 0x3C594F9F, 0x9F30, 0x47A1, 0x97, 0x9A, 0xC9, 0xE8, 0x3D, 0x3D, 0x0A,
                 0x06);

typedef struct SHDRAGIMAGE {
    SIZE sizeDragImage;
    POINT ptOffset;
    HBITMAP hbmpDragImage;
    COLORREF crColorKey;
} SHDRAGIMAGE, *LPSHDRAGIMAGE;

#define DSH_ALLOWDROPDESCRIPTIONTEXT 0x0001

typedef struct IDragSourceHelper IDragSourceHelper;
typedef struct IDragSourceHelperVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDragSourceHelper *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDragSourceHelper *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDragSourceHelper *This);
    HRESULT(STDMETHODCALLTYPE *InitializeFromBitmap)(IDragSourceHelper *This, LPSHDRAGIMAGE image, IDataObject *data);
    HRESULT(STDMETHODCALLTYPE *InitializeFromWindow)(IDragSourceHelper *This, HWND window, POINT *point,
                                                     IDataObject *data);
} IDragSourceHelperVtbl;
struct IDragSourceHelper {
    const IDragSourceHelperVtbl *lpVtbl;
};

typedef struct IDragSourceHelper2 IDragSourceHelper2;
typedef struct IDragSourceHelper2Vtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDragSourceHelper2 *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDragSourceHelper2 *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDragSourceHelper2 *This);
    HRESULT(STDMETHODCALLTYPE *InitializeFromBitmap)(IDragSourceHelper2 *This, LPSHDRAGIMAGE image, IDataObject *data);
    HRESULT(STDMETHODCALLTYPE *InitializeFromWindow)(IDragSourceHelper2 *This, HWND window, POINT *point,
                                                     IDataObject *data);
    HRESULT(STDMETHODCALLTYPE *SetFlags)(IDragSourceHelper2 *This, DWORD flags);
} IDragSourceHelper2Vtbl;
struct IDragSourceHelper2 {
    const IDragSourceHelper2Vtbl *lpVtbl;
};

typedef struct IDropTargetHelper IDropTargetHelper;
typedef struct IDropTargetHelperVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDropTargetHelper *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDropTargetHelper *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDropTargetHelper *This);
    HRESULT(STDMETHODCALLTYPE *DragEnter)(IDropTargetHelper *This, HWND target, IDataObject *data, POINT *point,
                                          DWORD effect);
    HRESULT(STDMETHODCALLTYPE *DragLeave)(IDropTargetHelper *This);
    HRESULT(STDMETHODCALLTYPE *DragOver)(IDropTargetHelper *This, POINT *point, DWORD effect);
    HRESULT(STDMETHODCALLTYPE *Drop)(IDropTargetHelper *This, IDataObject *data, POINT *point, DWORD effect);
    HRESULT(STDMETHODCALLTYPE *Show)(IDropTargetHelper *This, BOOL show);
} IDropTargetHelperVtbl;
struct IDropTargetHelper {
    const IDropTargetHelperVtbl *lpVtbl;
};

typedef enum TBPFLAG {
    TBPF_NOPROGRESS = 0,
    TBPF_INDETERMINATE = 0x1,
    TBPF_NORMAL = 0x2,
    TBPF_ERROR = 0x4,
    TBPF_PAUSED = 0x8
} TBPFLAG;

typedef enum STPFLAG {
    STPF_NONE = 0,
    STPF_USEAPPTHUMBNAILALWAYS = 0x1,
    STPF_USEAPPTHUMBNAILWHENACTIVE = 0x2,
    STPF_USEAPPPEEKALWAYS = 0x4,
    STPF_USEAPPPEEKWHENACTIVE = 0x8
} STPFLAG;

typedef enum THUMBBUTTONFLAGS {
    THBF_ENABLED = 0,
    THBF_DISABLED = 0x1,
    THBF_DISMISSONCLICK = 0x2,
    THBF_NOBACKGROUND = 0x4,
    THBF_HIDDEN = 0x8,
    THBF_NONINTERACTIVE = 0x10
} THUMBBUTTONFLAGS;

typedef enum THUMBBUTTONMASK { THB_BITMAP = 0x1, THB_ICON = 0x2, THB_TOOLTIP = 0x4, THB_FLAGS = 0x8 } THUMBBUTTONMASK;

#define THBN_CLICKED 0x1800
#define THB_MAX_BUTTONS 7

typedef struct THUMBBUTTON {
    DWORD dwMask;
    UINT iId;
    UINT iBitmap;
    HICON hIcon;
    WCHAR szTip[260];
    DWORD dwFlags;
} THUMBBUTTON, *LPTHUMBBUTTON;

typedef struct ITaskbarList3 ITaskbarList3;
typedef struct ITaskbarList3Vtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ITaskbarList3 *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(ITaskbarList3 *This);
    ULONG(STDMETHODCALLTYPE *Release)(ITaskbarList3 *This);
    HRESULT(STDMETHODCALLTYPE *HrInit)(ITaskbarList3 *This);
    HRESULT(STDMETHODCALLTYPE *AddTab)(ITaskbarList3 *This, HWND window);
    HRESULT(STDMETHODCALLTYPE *DeleteTab)(ITaskbarList3 *This, HWND window);
    HRESULT(STDMETHODCALLTYPE *ActivateTab)(ITaskbarList3 *This, HWND window);
    HRESULT(STDMETHODCALLTYPE *SetActiveAlt)(ITaskbarList3 *This, HWND window);
    HRESULT(STDMETHODCALLTYPE *MarkFullscreenWindow)(ITaskbarList3 *This, HWND window, BOOL fullscreen);
    HRESULT(STDMETHODCALLTYPE *SetProgressValue)(ITaskbarList3 *This, HWND window, ULONGLONG completed,
                                                 ULONGLONG total);
    HRESULT(STDMETHODCALLTYPE *SetProgressState)(ITaskbarList3 *This, HWND window, TBPFLAG flags);
    HRESULT(STDMETHODCALLTYPE *RegisterTab)(ITaskbarList3 *This, HWND tab, HWND mdi);
    HRESULT(STDMETHODCALLTYPE *UnregisterTab)(ITaskbarList3 *This, HWND tab);
    HRESULT(STDMETHODCALLTYPE *SetTabOrder)(ITaskbarList3 *This, HWND tab, HWND insert_before);
    HRESULT(STDMETHODCALLTYPE *SetTabActive)(ITaskbarList3 *This, HWND tab, HWND mdi, DWORD reserved);
    HRESULT(STDMETHODCALLTYPE *ThumbBarAddButtons)(ITaskbarList3 *This, HWND window, UINT count,
                                                   LPTHUMBBUTTON buttons);
    HRESULT(STDMETHODCALLTYPE *ThumbBarUpdateButtons)(ITaskbarList3 *This, HWND window, UINT count,
                                                      LPTHUMBBUTTON buttons);
    HRESULT(STDMETHODCALLTYPE *ThumbBarSetImageList)(ITaskbarList3 *This, HWND window, HANDLE images);
    HRESULT(STDMETHODCALLTYPE *SetOverlayIcon)(ITaskbarList3 *This, HWND window, HICON icon, LPCWSTR description);
    HRESULT(STDMETHODCALLTYPE *SetThumbnailTooltip)(ITaskbarList3 *This, HWND window, LPCWSTR tip);
    HRESULT(STDMETHODCALLTYPE *SetThumbnailClip)(ITaskbarList3 *This, HWND window, RECT *clip);
} ITaskbarList3Vtbl;
struct ITaskbarList3 {
    const ITaskbarList3Vtbl *lpVtbl;
};

typedef struct ITaskbarList4 ITaskbarList4;
typedef struct ITaskbarList4Vtbl {
    ITaskbarList3Vtbl base;
    HRESULT(STDMETHODCALLTYPE *SetTabProperties)(ITaskbarList4 *This, HWND tab, STPFLAG flags);
} ITaskbarList4Vtbl;
struct ITaskbarList4 {
    const ITaskbarList4Vtbl *lpVtbl;
};

typedef struct IObjectArray IObjectArray;
typedef struct IObjectArrayVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IObjectArray *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IObjectArray *This);
    ULONG(STDMETHODCALLTYPE *Release)(IObjectArray *This);
    HRESULT(STDMETHODCALLTYPE *GetCount)(IObjectArray *This, UINT *count);
    HRESULT(STDMETHODCALLTYPE *GetAt)(IObjectArray *This, UINT index, REFIID riid, void **object);
} IObjectArrayVtbl;
struct IObjectArray {
    const IObjectArrayVtbl *lpVtbl;
};

typedef struct IObjectCollection IObjectCollection;
typedef struct IObjectCollectionVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IObjectCollection *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IObjectCollection *This);
    ULONG(STDMETHODCALLTYPE *Release)(IObjectCollection *This);
    HRESULT(STDMETHODCALLTYPE *GetCount)(IObjectCollection *This, UINT *count);
    HRESULT(STDMETHODCALLTYPE *GetAt)(IObjectCollection *This, UINT index, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *AddObject)(IObjectCollection *This, IUnknown *object);
    HRESULT(STDMETHODCALLTYPE *AddFromArray)(IObjectCollection *This, IObjectArray *source);
    HRESULT(STDMETHODCALLTYPE *RemoveObjectAt)(IObjectCollection *This, UINT index);
    HRESULT(STDMETHODCALLTYPE *Clear)(IObjectCollection *This);
} IObjectCollectionVtbl;
struct IObjectCollection {
    const IObjectCollectionVtbl *lpVtbl;
};

typedef enum KNOWNDESTCATEGORY { KDC_FREQUENT = 1, KDC_RECENT } KNOWNDESTCATEGORY;

typedef struct ICustomDestinationList ICustomDestinationList;
typedef struct ICustomDestinationListVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ICustomDestinationList *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(ICustomDestinationList *This);
    ULONG(STDMETHODCALLTYPE *Release)(ICustomDestinationList *This);
    HRESULT(STDMETHODCALLTYPE *SetAppID)(ICustomDestinationList *This, LPCWSTR app_id);
    HRESULT(STDMETHODCALLTYPE *BeginList)(ICustomDestinationList *This, UINT *min_slots, REFIID riid, void **removed);
    HRESULT(STDMETHODCALLTYPE *AppendCategory)(ICustomDestinationList *This, LPCWSTR category, IObjectArray *items);
    HRESULT(STDMETHODCALLTYPE *AppendKnownCategory)(ICustomDestinationList *This, KNOWNDESTCATEGORY category);
    HRESULT(STDMETHODCALLTYPE *AddUserTasks)(ICustomDestinationList *This, IObjectArray *tasks);
    HRESULT(STDMETHODCALLTYPE *CommitList)(ICustomDestinationList *This);
    HRESULT(STDMETHODCALLTYPE *GetRemovedDestinations)(ICustomDestinationList *This, REFIID riid, void **removed);
    HRESULT(STDMETHODCALLTYPE *DeleteList)(ICustomDestinationList *This, LPCWSTR app_id);
    HRESULT(STDMETHODCALLTYPE *AbortList)(ICustomDestinationList *This);
} ICustomDestinationListVtbl;
struct ICustomDestinationList {
    const ICustomDestinationListVtbl *lpVtbl;
};

typedef struct IApplicationDestinations IApplicationDestinations;
typedef struct IApplicationDestinationsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IApplicationDestinations *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IApplicationDestinations *This);
    ULONG(STDMETHODCALLTYPE *Release)(IApplicationDestinations *This);
    HRESULT(STDMETHODCALLTYPE *SetAppID)(IApplicationDestinations *This, LPCWSTR app_id);
    HRESULT(STDMETHODCALLTYPE *RemoveDestination)(IApplicationDestinations *This, IUnknown *destination);
    HRESULT(STDMETHODCALLTYPE *RemoveAllDestinations)(IApplicationDestinations *This);
} IApplicationDestinationsVtbl;
struct IApplicationDestinations {
    const IApplicationDestinationsVtbl *lpVtbl;
};

typedef enum APPDOCLISTTYPE { ADLT_RECENT = 0, ADLT_FREQUENT } APPDOCLISTTYPE;

typedef struct IApplicationDocumentLists IApplicationDocumentLists;
typedef struct IApplicationDocumentListsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IApplicationDocumentLists *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IApplicationDocumentLists *This);
    ULONG(STDMETHODCALLTYPE *Release)(IApplicationDocumentLists *This);
    HRESULT(STDMETHODCALLTYPE *SetAppID)(IApplicationDocumentLists *This, LPCWSTR app_id);
    HRESULT(STDMETHODCALLTYPE *GetList)(IApplicationDocumentLists *This, APPDOCLISTTYPE type, UINT items, REFIID riid,
                                        void **list);
} IApplicationDocumentListsVtbl;
struct IApplicationDocumentLists {
    const IApplicationDocumentListsVtbl *lpVtbl;
};

SHSTDAPI HRESULT WINAPI SHGetPropertyStoreForWindow(HWND window, REFIID riid, void **store);
SHSTDAPI HRESULT WINAPI SHDoDragDrop(HWND window, IDataObject *data, IDropSource *source, DWORD effects,
                                     DWORD *effect);
SHSTDAPI HRESULT WINAPI SHCreateStdEnumFmtEtc(UINT count, const FORMATETC formats[], IEnumFORMATETC **enumerator);

/* taskbar window messages a program registers. */
#define TASKBAR_CREATED_MESSAGE L"TaskbarCreated"
#define TASKBAR_BUTTON_CREATED_MESSAGE L"TaskbarButtonCreated"

#endif
