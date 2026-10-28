/*
 * the shell's windows and views, and its automation objects: IServiceProvider, IShellBrowser and IShellView, IShellWindows
 * of the shell windows list that programs find a running shell by, and the dual interfaces of Shell.Application. The
 * folder objects the automation interfaces return are IDispatch pointers here. interfaces are C structs whose first member
 * points to a vtable.
 */
#ifndef MIME_WIN32_SHLDISP_H
#define MIME_WIN32_SHLDISP_H

#include <mime/win32/oleauto.h>
#include <mime/win32/oleidl.h>
#include <mime/win32/shobjidl.h>

/* ------------------------------------------------------------------ classes, interfaces and services */

MIME_DEFINE_GUID(IID_IServiceProvider, 0x6D5140C1, 0x7436, 0x11CE, 0x80, 0x34, 0x00, 0xAA, 0x00, 0x60, 0x09, 0xFA);
MIME_DEFINE_GUID(SID_STopLevelBrowser, 0x4C96BE40, 0x915C, 0x11CF, 0x99, 0xD3, 0x00, 0xAA, 0x00, 0x4A, 0xE8, 0x37);
MIME_DEFINE_GUID(IID_IShellBrowser, 0x000214E2, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IShellView, 0x000214E3, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(CLSID_ShellWindows, 0x9BA05972, 0xF6A8, 0x11CF, 0xA4, 0x42, 0x00, 0xA0, 0xC9, 0x0A, 0x8F, 0x39);
MIME_DEFINE_GUID(IID_IShellWindows, 0x85CB6900, 0x4D95, 0x11CF, 0x96, 0x0C, 0x00, 0x80, 0xC7, 0xF4, 0xEE, 0x85);
MIME_DEFINE_GUID(CLSID_ShellBrowserWindow, 0xC08AFD90, 0xF2A1, 0x11D1, 0x84, 0x55, 0x00, 0xA0, 0xC9, 0x1F, 0x38, 0x80);
MIME_DEFINE_GUID(CLSID_Shell, 0x13709620, 0xC279, 0x11CE, 0xA4, 0x9E, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(CLSID_ShellFolderView, 0x62112AA1, 0xEBE4, 0x11CF, 0xA5, 0xFB, 0x00, 0x20, 0xAF, 0xE7, 0x29, 0x2D);
MIME_DEFINE_GUID(IID_IShellDispatch, 0xD8F015C0, 0xC278, 0x11CE, 0xA4, 0x9E, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_IShellDispatch2, 0xA4C6892C, 0x3BA9, 0x11D2, 0x9D, 0xEA, 0x00, 0xC0, 0x4F, 0xB1, 0x61, 0x62);
MIME_DEFINE_GUID(IID_Folder, 0xBBCBDE60, 0xC3FF, 0x11CE, 0x83, 0x50, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_FolderItem, 0xFAC32C80, 0xCBE4, 0x11CE, 0x83, 0x50, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_FolderItems, 0x744129E0, 0xCBE5, 0x11CE, 0x83, 0x50, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
MIME_DEFINE_GUID(IID_IShellFolderViewDual, 0xE7A1AF80, 0x4D96, 0x11CF, 0x96, 0x0C, 0x00, 0x80, 0xC7, 0xF4, 0xEE, 0x85);
MIME_DEFINE_GUID(LIBID_Shell32, 0x50A7E9B0, 0x70EF, 0x11D1, 0xB7, 0x5A, 0x00, 0xA0, 0xC9, 0x05, 0x64, 0xFE);
MIME_DEFINE_GUID(LIBID_SHDocVw, 0xEAB22AC0, 0x30C1, 0x11CF, 0xA7, 0xEB, 0x00, 0x00, 0xC0, 0x5B, 0xAE, 0x0B);

/* the kinds of windows a list of shell windows holds, and the options of a search in it. */
#define SWC_EXPLORER 0x0
#define SWC_BROWSER 0x1
#define SWC_3RDPARTY 0x2
#define SWC_CALLBACK 0x4
#define SWC_DESKTOP 0x8
#define SWFO_NEEDDISPATCH 0x1
#define SWFO_INCLUDEPENDING 0x2
#define SWFO_COOKIEPASSED 0x4

/* what IShellView::GetItemObject is asked for. */
#define SVGIO_BACKGROUND 0x0
#define SVGIO_SELECTION 0x1
#define SVGIO_ALLVIEW 0x2
#define SVGIO_CHECKED 0x3
#define SVGIO_TYPE_MASK 0xF
#define SVGIO_FLAG_VIEWORDER 0x80000000

/* ------------------------------------------------------------------ services and views */

typedef struct IServiceProvider IServiceProvider;
typedef struct IServiceProviderVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IServiceProvider *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IServiceProvider *This);
    ULONG(STDMETHODCALLTYPE *Release)(IServiceProvider *This);
    HRESULT(STDMETHODCALLTYPE *QueryService)(IServiceProvider *This, REFGUID service, REFIID riid, void **object);
} IServiceProviderVtbl;
struct IServiceProvider {
    const IServiceProviderVtbl *lpVtbl;
};

typedef struct IShellView IShellView;
typedef struct IShellBrowser IShellBrowser;

typedef struct IShellBrowserVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellBrowser *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellBrowser *This);
    ULONG(STDMETHODCALLTYPE *Release)(IShellBrowser *This);
    HRESULT(STDMETHODCALLTYPE *GetWindow)(IShellBrowser *This, HWND *window);
    HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)(IShellBrowser *This, BOOL enter);
    HRESULT(STDMETHODCALLTYPE *InsertMenusSB)(IShellBrowser *This, HMENU shared, LPOLEMENUGROUPWIDTHS widths);
    HRESULT(STDMETHODCALLTYPE *SetMenuSB)(IShellBrowser *This, HMENU shared, HOLEMENU descriptor, HWND object);
    HRESULT(STDMETHODCALLTYPE *RemoveMenusSB)(IShellBrowser *This, HMENU shared);
    HRESULT(STDMETHODCALLTYPE *SetStatusTextSB)(IShellBrowser *This, LPCOLESTR text);
    HRESULT(STDMETHODCALLTYPE *EnableModelessSB)(IShellBrowser *This, BOOL enable);
    HRESULT(STDMETHODCALLTYPE *TranslateAcceleratorSB)(IShellBrowser *This, MSG *message, WORD id);
    HRESULT(STDMETHODCALLTYPE *BrowseObject)(IShellBrowser *This, PCUIDLIST_RELATIVE pidl, UINT flags);
    HRESULT(STDMETHODCALLTYPE *GetViewStateStream)(IShellBrowser *This, DWORD mode, IStream **stream);
    HRESULT(STDMETHODCALLTYPE *GetControlWindow)(IShellBrowser *This, UINT id, HWND *window);
    HRESULT(STDMETHODCALLTYPE *SendControlMsg)(IShellBrowser *This, UINT id, UINT message, WPARAM wparam, LPARAM lparam,
                                               LRESULT *result);
    HRESULT(STDMETHODCALLTYPE *QueryActiveShellView)(IShellBrowser *This, IShellView **view);
    HRESULT(STDMETHODCALLTYPE *OnViewWindowActive)(IShellBrowser *This, IShellView *view);
    HRESULT(STDMETHODCALLTYPE *SetToolbarItems)(IShellBrowser *This, void *buttons, UINT count, UINT flags);
} IShellBrowserVtbl;
struct IShellBrowser {
    const IShellBrowserVtbl *lpVtbl;
};

typedef struct IShellViewVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellView *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellView *This);
    ULONG(STDMETHODCALLTYPE *Release)(IShellView *This);
    HRESULT(STDMETHODCALLTYPE *GetWindow)(IShellView *This, HWND *window);
    HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)(IShellView *This, BOOL enter);
    HRESULT(STDMETHODCALLTYPE *TranslateAccelerator)(IShellView *This, MSG *message);
    HRESULT(STDMETHODCALLTYPE *EnableModeless)(IShellView *This, BOOL enable);
    HRESULT(STDMETHODCALLTYPE *UIActivate)(IShellView *This, UINT state);
    HRESULT(STDMETHODCALLTYPE *Refresh)(IShellView *This);
    HRESULT(STDMETHODCALLTYPE *CreateViewWindow)(IShellView *This, IShellView *previous, const void *settings,
                                                 IShellBrowser *owner, RECT *bounds, HWND *window);
    HRESULT(STDMETHODCALLTYPE *DestroyViewWindow)(IShellView *This);
    HRESULT(STDMETHODCALLTYPE *GetCurrentInfo)(IShellView *This, void *settings);
    HRESULT(STDMETHODCALLTYPE *AddPropertySheetPages)(IShellView *This, DWORD reserved, void *add_page, LPARAM lparam);
    HRESULT(STDMETHODCALLTYPE *SaveViewState)(IShellView *This);
    HRESULT(STDMETHODCALLTYPE *SelectItem)(IShellView *This, PCUITEMID_CHILD pidl, UINT flags);
    HRESULT(STDMETHODCALLTYPE *GetItemObject)(IShellView *This, UINT item, REFIID riid, void **object);
} IShellViewVtbl;
struct IShellView {
    const IShellViewVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ the list of shell windows */

typedef struct IShellWindows IShellWindows;
typedef struct IShellWindowsVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellWindows *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellWindows *This);
    ULONG(STDMETHODCALLTYPE *Release)(IShellWindows *This);
    HRESULT(STDMETHODCALLTYPE *GetTypeInfoCount)(IShellWindows *This, UINT *count);
    HRESULT(STDMETHODCALLTYPE *GetTypeInfo)(IShellWindows *This, UINT index, LCID lcid, ITypeInfo **info);
    HRESULT(STDMETHODCALLTYPE *GetIDsOfNames)(IShellWindows *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,
                                              DISPID *ids);
    HRESULT(STDMETHODCALLTYPE *Invoke)(IShellWindows *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                       DISPPARAMS *params, VARIANT *result, EXCEPINFO *exception, UINT *argument);
    HRESULT(STDMETHODCALLTYPE *get_Count)(IShellWindows *This, LONG *count);
    HRESULT(STDMETHODCALLTYPE *Item)(IShellWindows *This, VARIANT index, IDispatch **window);
    HRESULT(STDMETHODCALLTYPE *_NewEnum)(IShellWindows *This, IUnknown **enumerator);
    HRESULT(STDMETHODCALLTYPE *Register)(IShellWindows *This, IDispatch *window, LONG hwnd, int kind, LONG *cookie);
    HRESULT(STDMETHODCALLTYPE *RegisterPending)(IShellWindows *This, LONG thread, VARIANT *location, VARIANT *root, int kind,
                                                LONG *cookie);
    HRESULT(STDMETHODCALLTYPE *Revoke)(IShellWindows *This, LONG cookie);
    HRESULT(STDMETHODCALLTYPE *OnNavigate)(IShellWindows *This, LONG cookie, VARIANT *location);
    HRESULT(STDMETHODCALLTYPE *OnActivated)(IShellWindows *This, LONG cookie, VARIANT_BOOL active);
    HRESULT(STDMETHODCALLTYPE *FindWindowSW)(IShellWindows *This, VARIANT *location, VARIANT *root, int kind, LONG *hwnd,
                                             int options, IDispatch **window);
    HRESULT(STDMETHODCALLTYPE *OnCreated)(IShellWindows *This, LONG cookie, IUnknown *object);
    HRESULT(STDMETHODCALLTYPE *ProcessAttachDetach)(IShellWindows *This, VARIANT_BOOL attach);
} IShellWindowsVtbl;
struct IShellWindows {
    const IShellWindowsVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ the automation objects */

typedef struct IShellFolderViewDual IShellFolderViewDual;
typedef struct IShellFolderViewDualVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellFolderViewDual *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellFolderViewDual *This);
    ULONG(STDMETHODCALLTYPE *Release)(IShellFolderViewDual *This);
    HRESULT(STDMETHODCALLTYPE *GetTypeInfoCount)(IShellFolderViewDual *This, UINT *count);
    HRESULT(STDMETHODCALLTYPE *GetTypeInfo)(IShellFolderViewDual *This, UINT index, LCID lcid, ITypeInfo **info);
    HRESULT(STDMETHODCALLTYPE *GetIDsOfNames)(IShellFolderViewDual *This, REFIID riid, LPOLESTR *names, UINT count,
                                              LCID lcid, DISPID *ids);
    HRESULT(STDMETHODCALLTYPE *Invoke)(IShellFolderViewDual *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                       DISPPARAMS *params, VARIANT *result, EXCEPINFO *exception, UINT *argument);
    HRESULT(STDMETHODCALLTYPE *get_Application)(IShellFolderViewDual *This, IDispatch **application);
    HRESULT(STDMETHODCALLTYPE *get_Parent)(IShellFolderViewDual *This, IDispatch **parent);
    HRESULT(STDMETHODCALLTYPE *get_Folder)(IShellFolderViewDual *This, IDispatch **folder);
    HRESULT(STDMETHODCALLTYPE *SelectedItems)(IShellFolderViewDual *This, IDispatch **items);
    HRESULT(STDMETHODCALLTYPE *get_FocusedItem)(IShellFolderViewDual *This, IDispatch **item);
    HRESULT(STDMETHODCALLTYPE *SelectItem)(IShellFolderViewDual *This, VARIANT *item, int flags);
    HRESULT(STDMETHODCALLTYPE *PopupItemMenu)(IShellFolderViewDual *This, IDispatch *item, VARIANT x, VARIANT y,
                                              BSTR *command);
    HRESULT(STDMETHODCALLTYPE *get_Script)(IShellFolderViewDual *This, IDispatch **script);
    HRESULT(STDMETHODCALLTYPE *get_ViewOptions)(IShellFolderViewDual *This, LONG *options);
} IShellFolderViewDualVtbl;
struct IShellFolderViewDual {
    const IShellFolderViewDualVtbl *lpVtbl;
};

/* the folder, its items and one item of the namespace, the objects NameSpace gives scripts. */
typedef struct Folder Folder;
#define Folder_METHODS(T)                                                                                              \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Title)(T * This, BSTR * title);                                                     \
    HRESULT(STDMETHODCALLTYPE *get_Application)(T * This, IDispatch * *application);                                   \
    HRESULT(STDMETHODCALLTYPE *get_Parent)(T * This, IDispatch * *parent);                                             \
    HRESULT(STDMETHODCALLTYPE *get_ParentFolder)(T * This, IDispatch * *folder);                                       \
    HRESULT(STDMETHODCALLTYPE *Items)(T * This, IDispatch * *items);                                                   \
    HRESULT(STDMETHODCALLTYPE *ParseName)(T * This, BSTR name, IDispatch * *item);                                     \
    HRESULT(STDMETHODCALLTYPE *NewFolder)(T * This, BSTR name, VARIANT options);                                       \
    HRESULT(STDMETHODCALLTYPE *MoveHere)(T * This, VARIANT item, VARIANT options);                                     \
    HRESULT(STDMETHODCALLTYPE *CopyHere)(T * This, VARIANT item, VARIANT options);                                     \
    HRESULT(STDMETHODCALLTYPE *GetDetailsOf)(T * This, VARIANT item, int column, BSTR * details);
MIME_COM_INTERFACE(Folder)

typedef struct FolderItems FolderItems;
#define FolderItems_METHODS(T)                                                                                         \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Count)(T * This, LONG * count);                                                     \
    HRESULT(STDMETHODCALLTYPE *get_Application)(T * This, IDispatch * *application);                                   \
    HRESULT(STDMETHODCALLTYPE *get_Parent)(T * This, IDispatch * *parent);                                             \
    HRESULT(STDMETHODCALLTYPE *Item)(T * This, VARIANT index, IDispatch * *item);                                      \
    HRESULT(STDMETHODCALLTYPE *_NewEnum)(T * This, IUnknown * *enumerator);
MIME_COM_INTERFACE(FolderItems)

typedef struct FolderItem FolderItem;
#define FolderItem_METHODS(T)                                                                                          \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Application)(T * This, IDispatch * *application);                                   \
    HRESULT(STDMETHODCALLTYPE *get_Parent)(T * This, IDispatch * *parent);                                             \
    HRESULT(STDMETHODCALLTYPE *get_Name)(T * This, BSTR * name);                                                       \
    HRESULT(STDMETHODCALLTYPE *put_Name)(T * This, BSTR name);                                                         \
    HRESULT(STDMETHODCALLTYPE *get_Path)(T * This, BSTR * path);                                                       \
    HRESULT(STDMETHODCALLTYPE *get_GetLink)(T * This, IDispatch * *link);                                              \
    HRESULT(STDMETHODCALLTYPE *get_GetFolder)(T * This, IDispatch * *folder);                                          \
    HRESULT(STDMETHODCALLTYPE *get_IsLink)(T * This, VARIANT_BOOL * value);                                            \
    HRESULT(STDMETHODCALLTYPE *get_IsFolder)(T * This, VARIANT_BOOL * value);                                          \
    HRESULT(STDMETHODCALLTYPE *get_IsFileSystem)(T * This, VARIANT_BOOL * value);                                      \
    HRESULT(STDMETHODCALLTYPE *get_IsBrowsable)(T * This, VARIANT_BOOL * value);                                       \
    HRESULT(STDMETHODCALLTYPE *get_ModifyDate)(T * This, DATE * date);                                                 \
    HRESULT(STDMETHODCALLTYPE *put_ModifyDate)(T * This, DATE date);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_Size)(T * This, LONG * size);                                                       \
    HRESULT(STDMETHODCALLTYPE *get_Type)(T * This, BSTR * type);                                                       \
    HRESULT(STDMETHODCALLTYPE *Verbs)(T * This, IDispatch * *verbs);                                                   \
    HRESULT(STDMETHODCALLTYPE *InvokeVerb)(T * This, VARIANT verb);
MIME_COM_INTERFACE(FolderItem)

typedef struct IShellDispatch2 IShellDispatch2;
typedef struct IShellDispatch2Vtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellDispatch2 *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellDispatch2 *This);
    ULONG(STDMETHODCALLTYPE *Release)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *GetTypeInfoCount)(IShellDispatch2 *This, UINT *count);
    HRESULT(STDMETHODCALLTYPE *GetTypeInfo)(IShellDispatch2 *This, UINT index, LCID lcid, ITypeInfo **info);
    HRESULT(STDMETHODCALLTYPE *GetIDsOfNames)(IShellDispatch2 *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,
                                              DISPID *ids);
    HRESULT(STDMETHODCALLTYPE *Invoke)(IShellDispatch2 *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                       DISPPARAMS *params, VARIANT *result, EXCEPINFO *exception, UINT *argument);
    /* IShellDispatch. */
    HRESULT(STDMETHODCALLTYPE *get_Application)(IShellDispatch2 *This, IDispatch **application);
    HRESULT(STDMETHODCALLTYPE *get_Parent)(IShellDispatch2 *This, IDispatch **parent);
    HRESULT(STDMETHODCALLTYPE *NameSpace)(IShellDispatch2 *This, VARIANT directory, IDispatch **folder);
    HRESULT(STDMETHODCALLTYPE *BrowseForFolder)(IShellDispatch2 *This, LONG hwnd, BSTR title, LONG options, VARIANT root,
                                                IDispatch **folder);
    HRESULT(STDMETHODCALLTYPE *Windows)(IShellDispatch2 *This, IDispatch **windows);
    HRESULT(STDMETHODCALLTYPE *Open)(IShellDispatch2 *This, VARIANT directory);
    HRESULT(STDMETHODCALLTYPE *Explore)(IShellDispatch2 *This, VARIANT directory);
    HRESULT(STDMETHODCALLTYPE *MinimizeAll)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *UndoMinimizeALL)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *FileRun)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *CascadeWindows)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *TileVertically)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *TileHorizontally)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *ShutdownWindows)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *Suspend)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *EjectPC)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *SetTime)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *TrayProperties)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *Help)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *FindFiles)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *FindComputer)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *RefreshMenu)(IShellDispatch2 *This);
    HRESULT(STDMETHODCALLTYPE *ControlPanelItem)(IShellDispatch2 *This, BSTR directory);
    /* IShellDispatch2. */
    HRESULT(STDMETHODCALLTYPE *IsRestricted)(IShellDispatch2 *This, BSTR group, BSTR restriction, LONG *value);
    HRESULT(STDMETHODCALLTYPE *ShellExecute)(IShellDispatch2 *This, BSTR file, VARIANT arguments, VARIANT directory,
                                             VARIANT operation, VARIANT show);
    HRESULT(STDMETHODCALLTYPE *FindPrinter)(IShellDispatch2 *This, BSTR name, BSTR location, BSTR model);
    HRESULT(STDMETHODCALLTYPE *GetSystemInformation)(IShellDispatch2 *This, BSTR name, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *ServiceStart)(IShellDispatch2 *This, BSTR service, VARIANT persistent, VARIANT *result);
    HRESULT(STDMETHODCALLTYPE *ServiceStop)(IShellDispatch2 *This, BSTR service, VARIANT persistent, VARIANT *result);
    HRESULT(STDMETHODCALLTYPE *IsServiceRunning)(IShellDispatch2 *This, BSTR service, VARIANT *result);
    HRESULT(STDMETHODCALLTYPE *CanStartStopService)(IShellDispatch2 *This, BSTR service, VARIANT *result);
    HRESULT(STDMETHODCALLTYPE *ShowBrowserBar)(IShellDispatch2 *This, BSTR clsid, VARIANT show, VARIANT *result);
} IShellDispatch2Vtbl;
struct IShellDispatch2 {
    const IShellDispatch2Vtbl *lpVtbl;
};

#endif
