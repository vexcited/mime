/*
 * wshom.ocx internals shared by its source files: the object model of the Windows Script Host. Its objects are dual
 * interfaces that answer IDispatch from the type library of this dll, a resource of it, and report errors through the error
 * info of automation with the messages of the string table.
 */
#ifndef MIME_WSHOM_PRIVATE_H
#define MIME_WSHOM_PRIVATE_H

#include <mime/win32/scrrun.h>
#include <mime/win32/shellapi.h>
#include <mime/win32/shobjidl.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>
#include <rtl.h>

extern HINSTANCE g_module;

/* declarations that the shared headers do not have. */
#define HOTKEYF_SHIFT 0x01
#define HOTKEYF_CONTROL 0x02
#define HOTKEYF_ALT 0x04
#define HOTKEYF_EXT 0x08
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_F16 0x7F
#define RESOURCE_CONNECTED 0x00000001
#define RESOURCETYPE_DISK 0x00000001
#define RESOURCETYPE_PRINT 0x00000002
#define CONNECT_UPDATE_PROFILE 0x00000001

typedef struct NETRESOURCEW {
    DWORD dwScope;
    DWORD dwType;
    DWORD dwDisplayType;
    DWORD dwUsage;
    LPWSTR lpLocalName;
    LPWSTR lpRemoteName;
    LPWSTR lpComment;
    LPWSTR lpProvider;
} NETRESOURCEW;

DECLSPEC_IMPORT BOOL WINAPI GetUserNameW(LPWSTR buffer, LPDWORD size);
DECLSPEC_IMPORT HANDLE WINAPI RegisterEventSourceW(LPCWSTR machine, LPCWSTR source);
DECLSPEC_IMPORT BOOL WINAPI DeregisterEventSource(HANDLE source);
DECLSPEC_IMPORT BOOL WINAPI ReportEventW(HANDLE log, WORD type, WORD category, DWORD id, PSID sid, WORD count, DWORD data,
                                         LPCWSTR *strings, LPVOID raw);
DECLSPEC_IMPORT BOOL WINAPI EnumThreadWindows(DWORD thread, WNDENUMPROC proc, LPARAM param);

MIME_DEFINE_GUID(LIBID_IWshRuntimeLibrary, 0xF935DC20, 0x1CF0, 0x11D0, 0xAD, 0xB9, 0x00, 0xC0, 0x4F, 0xD5, 0x8A, 0x0B);
MIME_DEFINE_GUID(CLSID_WshShell, 0x72C24DD5, 0xD70A, 0x438B, 0x8A, 0x42, 0x98, 0x42, 0x4B, 0x88, 0xAF, 0xB8);
MIME_DEFINE_GUID(CLSID_WshShellLegacy, 0xF935DC22, 0x1CF0, 0x11D0, 0xAD, 0xB9, 0x00, 0xC0, 0x4F, 0xD5, 0x8A, 0x0B);
MIME_DEFINE_GUID(CLSID_WshNetwork, 0x093FF999, 0x1EA0, 0x4079, 0x95, 0x25, 0x96, 0x14, 0xC3, 0x50, 0x4B, 0x74);
MIME_DEFINE_GUID(CLSID_WshNetworkLegacy, 0xF935DC26, 0x1CF0, 0x11D0, 0xAD, 0xB9, 0x00, 0xC0, 0x4F, 0xD5, 0x8A, 0x0B);
MIME_DEFINE_GUID(CLSID_WshShortcut, 0xA548B8E4, 0x51D5, 0x4661, 0x88, 0x24, 0xDA, 0xA1, 0xD8, 0x93, 0xDF, 0xB2);
MIME_DEFINE_GUID(CLSID_WshURLShortcut, 0x50E13488, 0x6F1E, 0x4450, 0x96, 0xB0, 0x87, 0x37, 0x55, 0x40, 0x39, 0x55);
MIME_DEFINE_GUID(CLSID_WshCollection, 0x387DAFF4, 0xDA03, 0x44D2, 0xB0, 0xD1, 0x80, 0xC9, 0x27, 0xC9, 0x05, 0xAC);
MIME_DEFINE_GUID(CLSID_WshEnvironment, 0xF48229AF, 0xE28C, 0x42B5, 0xBB, 0x92, 0xE1, 0x14, 0xE6, 0x2B, 0xDD, 0x54);
MIME_DEFINE_GUID(CLSID_WshExec, 0x08FED191, 0xBE19, 0x11D3, 0xA2, 0x8B, 0x00, 0x10, 0x4B, 0xD3, 0x50, 0x90);
MIME_DEFINE_GUID(IID_IWshShell, 0xF935DC21, 0x1CF0, 0x11D0, 0xAD, 0xB9, 0x00, 0xC0, 0x4F, 0xD5, 0x8A, 0x0B);
MIME_DEFINE_GUID(IID_IWshShell2, 0x24BE5A30, 0xEDFE, 0x11D2, 0xB9, 0x33, 0x00, 0x10, 0x4B, 0x36, 0x5C, 0x9F);
MIME_DEFINE_GUID(IID_IWshShell3, 0x41904400, 0xBE18, 0x11D3, 0xA2, 0x8B, 0x00, 0x10, 0x4B, 0xD3, 0x50, 0x90);
MIME_DEFINE_GUID(IID_IWshCollection, 0xF935DC27, 0x1CF0, 0x11D0, 0xAD, 0xB9, 0x00, 0xC0, 0x4F, 0xD5, 0x8A, 0x0B);
MIME_DEFINE_GUID(IID_IWshEnvironment, 0xF935DC29, 0x1CF0, 0x11D0, 0xAD, 0xB9, 0x00, 0xC0, 0x4F, 0xD5, 0x8A, 0x0B);
MIME_DEFINE_GUID(IID_IWshShortcut, 0xF935DC23, 0x1CF0, 0x11D0, 0xAD, 0xB9, 0x00, 0xC0, 0x4F, 0xD5, 0x8A, 0x0B);
MIME_DEFINE_GUID(IID_IWshURLShortcut, 0xF935DC2B, 0x1CF0, 0x11D0, 0xAD, 0xB9, 0x00, 0xC0, 0x4F, 0xD5, 0x8A, 0x0B);
MIME_DEFINE_GUID(IID_IWshNetwork, 0xF935DC25, 0x1CF0, 0x11D0, 0xAD, 0xB9, 0x00, 0xC0, 0x4F, 0xD5, 0x8A, 0x0B);
MIME_DEFINE_GUID(IID_IWshNetwork2, 0x24BE5A31, 0xEDFE, 0x11D2, 0xB9, 0x33, 0x00, 0x10, 0x4B, 0x36, 0x5C, 0x9F);
MIME_DEFINE_GUID(IID_IWshExec, 0x08FED190, 0xBE19, 0x11D3, 0xA2, 0x8B, 0x00, 0x10, 0x4B, 0xD3, 0x50, 0x90);

typedef struct IWshCollection IWshCollection;
typedef struct IWshEnvironment IWshEnvironment;
typedef struct IWshShell3 IWshShell3;
typedef struct IWshShortcut IWshShortcut;
typedef struct IWshURLShortcut IWshURLShortcut;
typedef struct IWshNetwork2 IWshNetwork2;
typedef struct IWshExec IWshExec;

typedef enum WshExecStatus { WshRunning = 0, WshFinished = 1, WshFailed = 2 } WshExecStatus;

#define IWshShell3_METHODS(T)                                                                                          \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_SpecialFolders)(T * This, IWshCollection * *folders);                               \
    HRESULT(STDMETHODCALLTYPE *get_Environment)(T * This, VARIANT * type, IWshEnvironment * *environment);             \
    HRESULT(STDMETHODCALLTYPE *Run)(T * This, BSTR command, VARIANT * style, VARIANT * wait, int *result);             \
    HRESULT(STDMETHODCALLTYPE *Popup)(T * This, BSTR text, VARIANT * seconds, VARIANT * title, VARIANT * type,         \
                                      int *button);                                                                    \
    HRESULT(STDMETHODCALLTYPE *CreateShortcut)(T * This, BSTR path, IDispatch * *shortcut);                            \
    HRESULT(STDMETHODCALLTYPE *ExpandEnvironmentStrings)(T * This, BSTR source, BSTR * result);                        \
    HRESULT(STDMETHODCALLTYPE *RegRead)(T * This, BSTR name, VARIANT * value);                                         \
    HRESULT(STDMETHODCALLTYPE *RegWrite)(T * This, BSTR name, VARIANT * value, VARIANT * type);                        \
    HRESULT(STDMETHODCALLTYPE *RegDelete)(T * This, BSTR name);                                                        \
    HRESULT(STDMETHODCALLTYPE *LogEvent)(T * This, VARIANT * type, BSTR message, BSTR target, VARIANT_BOOL * result);  \
    HRESULT(STDMETHODCALLTYPE *AppActivate)(T * This, VARIANT * app, VARIANT * wait, VARIANT_BOOL * result);           \
    HRESULT(STDMETHODCALLTYPE *SendKeys)(T * This, BSTR keys, VARIANT * wait);                                         \
    HRESULT(STDMETHODCALLTYPE *Exec)(T * This, BSTR command, IWshExec * *exec);                                        \
    HRESULT(STDMETHODCALLTYPE *get_CurrentDirectory)(T * This, BSTR * directory);                                      \
    HRESULT(STDMETHODCALLTYPE *put_CurrentDirectory)(T * This, BSTR directory);
MIME_COM_INTERFACE(IWshShell3)

#define IWshCollection_METHODS(T)                                                                                      \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *Item)(T * This, VARIANT * index, VARIANT * value);                                      \
    HRESULT(STDMETHODCALLTYPE *Count)(T * This, LONG * count);                                                         \
    HRESULT(STDMETHODCALLTYPE *get_length)(T * This, LONG * length);                                                   \
    HRESULT(STDMETHODCALLTYPE *_NewEnum)(T * This, IUnknown * *enumerator);
MIME_COM_INTERFACE(IWshCollection)

#define IWshEnvironment_METHODS(T)                                                                                     \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Item)(T * This, BSTR name, BSTR * value);                                           \
    HRESULT(STDMETHODCALLTYPE *put_Item)(T * This, BSTR name, BSTR value);                                             \
    HRESULT(STDMETHODCALLTYPE *Count)(T * This, LONG * count);                                                         \
    HRESULT(STDMETHODCALLTYPE *get_length)(T * This, LONG * length);                                                   \
    HRESULT(STDMETHODCALLTYPE *_NewEnum)(T * This, IUnknown * *enumerator);                                            \
    HRESULT(STDMETHODCALLTYPE *Remove)(T * This, BSTR name);
MIME_COM_INTERFACE(IWshEnvironment)

#define IWshShortcut_METHODS(T)                                                                                        \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_FullName)(T * This, BSTR * name);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_Arguments)(T * This, BSTR * text);                                                  \
    HRESULT(STDMETHODCALLTYPE *put_Arguments)(T * This, BSTR text);                                                    \
    HRESULT(STDMETHODCALLTYPE *get_Description)(T * This, BSTR * text);                                                \
    HRESULT(STDMETHODCALLTYPE *put_Description)(T * This, BSTR text);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_Hotkey)(T * This, BSTR * text);                                                     \
    HRESULT(STDMETHODCALLTYPE *put_Hotkey)(T * This, BSTR text);                                                       \
    HRESULT(STDMETHODCALLTYPE *get_IconLocation)(T * This, BSTR * text);                                               \
    HRESULT(STDMETHODCALLTYPE *put_IconLocation)(T * This, BSTR text);                                                 \
    HRESULT(STDMETHODCALLTYPE *put_RelativePath)(T * This, BSTR text);                                                 \
    HRESULT(STDMETHODCALLTYPE *get_TargetPath)(T * This, BSTR * text);                                                 \
    HRESULT(STDMETHODCALLTYPE *put_TargetPath)(T * This, BSTR text);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_WindowStyle)(T * This, int *style);                                                 \
    HRESULT(STDMETHODCALLTYPE *put_WindowStyle)(T * This, int style);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_WorkingDirectory)(T * This, BSTR * text);                                           \
    HRESULT(STDMETHODCALLTYPE *put_WorkingDirectory)(T * This, BSTR text);                                             \
    HRESULT(STDMETHODCALLTYPE *Load)(T * This, BSTR path);                                                             \
    HRESULT(STDMETHODCALLTYPE *Save)(T * This);
MIME_COM_INTERFACE(IWshShortcut)

#define IWshURLShortcut_METHODS(T)                                                                                     \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_FullName)(T * This, BSTR * name);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_TargetPath)(T * This, BSTR * text);                                                 \
    HRESULT(STDMETHODCALLTYPE *put_TargetPath)(T * This, BSTR text);                                                   \
    HRESULT(STDMETHODCALLTYPE *Load)(T * This, BSTR path);                                                             \
    HRESULT(STDMETHODCALLTYPE *Save)(T * This);
MIME_COM_INTERFACE(IWshURLShortcut)

#define IWshNetwork2_METHODS(T)                                                                                        \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_UserDomain)(T * This, BSTR * text);                                                 \
    HRESULT(STDMETHODCALLTYPE *get_UserName)(T * This, BSTR * text);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_UserProfile)(T * This, BSTR * text);                                                \
    HRESULT(STDMETHODCALLTYPE *get_ComputerName)(T * This, BSTR * text);                                               \
    HRESULT(STDMETHODCALLTYPE *get_Organization)(T * This, BSTR * text);                                               \
    HRESULT(STDMETHODCALLTYPE *get_Site)(T * This, BSTR * text);                                                       \
    HRESULT(STDMETHODCALLTYPE *MapNetworkDrive)(T * This, BSTR local, BSTR remote, VARIANT * update, VARIANT * user,   \
                                                VARIANT * password);                                                   \
    HRESULT(STDMETHODCALLTYPE *RemoveNetworkDrive)(T * This, BSTR name, VARIANT * force, VARIANT * update);            \
    HRESULT(STDMETHODCALLTYPE *EnumNetworkDrives)(T * This, IWshCollection * *drives);                                 \
    HRESULT(STDMETHODCALLTYPE *AddPrinterConnection)(T * This, BSTR local, BSTR remote, VARIANT * update,              \
                                                     VARIANT * user, VARIANT * password);                              \
    HRESULT(STDMETHODCALLTYPE *RemovePrinterConnection)(T * This, BSTR name, VARIANT * force, VARIANT * update);       \
    HRESULT(STDMETHODCALLTYPE *EnumPrinterConnections)(T * This, IWshCollection * *printers);                          \
    HRESULT(STDMETHODCALLTYPE *SetDefaultPrinter)(T * This, BSTR name);                                                \
    HRESULT(STDMETHODCALLTYPE *AddWindowsPrinterConnection)(T * This, BSTR name, BSTR driver, BSTR port);
MIME_COM_INTERFACE(IWshNetwork2)

#define IWshExec_METHODS(T)                                                                                            \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Status)(T * This, WshExecStatus * status);                                          \
    HRESULT(STDMETHODCALLTYPE *get_StdIn)(T * This, ITextStream * *stream);                                            \
    HRESULT(STDMETHODCALLTYPE *get_StdOut)(T * This, ITextStream * *stream);                                           \
    HRESULT(STDMETHODCALLTYPE *get_StdErr)(T * This, ITextStream * *stream);                                           \
    HRESULT(STDMETHODCALLTYPE *get_ProcessID)(T * This, LONG * pid);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_ExitCode)(T * This, LONG * code);                                                   \
    HRESULT(STDMETHODCALLTYPE *Terminate)(T * This);
MIME_COM_INTERFACE(IWshExec)

/* ------------------------------------------------------------------ the common part of the objects */

/* what a kind of object accepts: its interface for the type information, the ones it answers to and its class. */
typedef struct WSH_CLASS {
    const GUID *Iid, *Clsid;
    const GUID *Accepted[6];
} WSH_CLASS;

/* every object starts with its dual interface, then the class information. */
typedef struct WSH_OBJECT {
    const void *Vtbl;
    IProvideClassInfo Info;
    LONG Refs;
    const WSH_CLASS *Class;
    void (*Destroy)(struct WSH_OBJECT *object);
} WSH_OBJECT;

void WshObjectInit(WSH_OBJECT *object, const void *vtbl, const WSH_CLASS *class_info, void (*destroy)(WSH_OBJECT *));
HRESULT WshQueryInterface(void *This, REFIID riid, void **object);
ULONG WshAddRef(void *This);
ULONG WshRelease(void *This);
HRESULT WshGetTypeInfoCount(void *This, UINT *count);
HRESULT WshGetTypeInfo(void *This, UINT index, LCID lcid, ITypeInfo **info);
HRESULT WshGetIDsOfNames(void *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids);
HRESULT WshInvoke(void *This, DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params, VARIANT *result,
                  EXCEPINFO *excep, UINT *arg_error);

/* the methods of IUnknown and IDispatch of an object, as the interface of the type declares them. */
#define WSH_COMMON_METHODS(prefix, type)                                                                               \
    static HRESULT STDMETHODCALLTYPE prefix##_QueryInterface(type *This, REFIID riid, void **object)                   \
    {                                                                                                                  \
        return WshQueryInterface(This, riid, object);                                                                  \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_AddRef(type *This)                                                          \
    {                                                                                                                  \
        return WshAddRef(This);                                                                                        \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE prefix##_Release(type *This)                                                         \
    {                                                                                                                  \
        return WshRelease(This);                                                                                       \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_GetTypeInfoCount(type *This, UINT *count)                                 \
    {                                                                                                                  \
        return WshGetTypeInfoCount(This, count);                                                                       \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_GetTypeInfo(type *This, UINT index, LCID lcid, ITypeInfo **info)         \
    {                                                                                                                  \
        return WshGetTypeInfo(This, index, lcid, info);                                                                \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_GetIDsOfNames(type *This, REFIID riid, LPOLESTR *names, UINT count,      \
                                                            LCID lcid, DISPID *ids)                                    \
    {                                                                                                                  \
        return WshGetIDsOfNames(This, riid, names, count, lcid, ids);                                                  \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_Invoke(type *This, DISPID id, REFIID riid, LCID lcid, WORD flags,        \
                                                     DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep,            \
                                                     UINT *arg_error)                                                  \
    {                                                                                                                  \
        return WshInvoke(This, id, riid, lcid, flags, params, result, excep, arg_error);                               \
    }

HRESULT WshTypeInfo(REFIID iid, ITypeInfo **info);
void WshLockDll(void);
void WshUnlockDll(void);
LONG WshLockCount(void);

/* ------------------------------------------------------------------ errors and strings */

/* the exception of a method: the error info with the text of a string of the table, the arguments put in its places. */
HRESULT WshSignal(const GUID *iid, const WCHAR *source, UINT string_id, const WCHAR *argument);
/* the same for the text Windows has for an error code. */
HRESULT WshSignalHr(const GUID *iid, const WCHAR *source, HRESULT hr);
BSTR WshLoadBstr(UINT id);
HRESULT HrFromLastError(void);

#define DISP_E_EXCEPTION_HR ((HRESULT)0x80020009)
#define MSG_ARGUMENT_EMPTY 0x1000
#define MSG_RUN_WAIT 4107
#define MSG_SHORTCUT_NAME 4100
#define MSG_SHORTCUT_SAVE 4102
#define MSG_SHORTCUT_TARGET 4103
#define MSG_NO_PRINTER 4101
#define MSG_HANDLER_FAILED 4104
#define MSG_BAD_URL 4105
#define MSG_NO_HANDLER 4106
#define MSG_ENV_REMOVE 4108
#define MSG_REG_REMOVE 4109
#define MSG_REG_ROOT 4110
#define MSG_REG_OPEN 4111
#define MSG_WSH_LOG 4112
#define MSG_EXEC_EMPTY 4113
#define MSG_TITLE 4001

/* ------------------------------------------------------------------ variants */

BOOL WshArgumentGiven(const VARIANT *value);
HRESULT WshVariantBstr(VARIANT *value, BSTR *out);
HRESULT WshVariantLong(VARIANT *value, LONG *out);
HRESULT WshVariantBool(VARIANT *value, int *out);

/* ------------------------------------------------------------------ the objects */

HRESULT WshShellCreate(REFIID riid, void **object);
HRESULT WshNetworkCreate(REFIID riid, void **object);
HRESULT WshCollectionCreate(SAFEARRAY *items, SAFEARRAY *names, IWshCollection **out);
HRESULT WshEnvironmentCreate(int type, IWshEnvironment **out);
HRESULT WshExecCreate(HANDLE out_read, HANDLE in_write, HANDLE err_read, IWshExec **out);
HRESULT WshExecSetProcess(IWshExec *exec, const PROCESS_INFORMATION *info);
HRESULT WshShortcutCreate(BSTR path, IDispatch **out);
HRESULT WshUrlShortcutCreate(BSTR path, IDispatch **out);
HRESULT WshSendKeys(const WCHAR *keys, BOOL wait);
/* the process of a command line, the standard handles given to it. */
HRESULT WshCreateProcess(IWshExec *exec, WCHAR *command, HANDLE out_write, HANDLE in_read, HANDLE err_write);
/* the stream of a pipe, exported by scrrun.dll. */
HRESULT WshPopup(const WCHAR *text, const WCHAR *title, UINT type, int seconds, int *button);

#endif
