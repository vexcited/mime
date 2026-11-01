/*
 * the script hosts, wscript.exe and cscript.exe: one source tree, built once for each. WSH_CONSOLE is 1 for cscript.
 * A script is loaded from a file, handed to the active scripting engine of its file type, and run with one named item,
 * WScript, the host object. The file types, engines and settings come from the registry as they do on Windows.
 */
#ifndef MIME_WSCRIPT_H
#define MIME_WSCRIPT_H

#include <mime/win32/ole2.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/objidl.h>
#include <mime/win32/ocidl.h>
#include <mime/win32/activscp.h>
#include <mime/win32/scrrun.h>
#include <mime/win32/shellapi.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>
#include <mime/win32/winver.h>
#include <rtl.h>

#ifndef WSH_CONSOLE
#define WSH_CONSOLE 0
#endif

DECLSPEC_IMPORT BOOL WINAPI EnumThreadWindows(DWORD thread, WNDENUMPROC proc, LPARAM param);
DECLSPEC_IMPORT BOOL WINAPI IsTextUnicode(const void *buffer, int size, LPINT result);

#define RPC_C_AUTHN_LEVEL_DEFAULT 0
#define RPC_C_IMP_LEVEL_IMPERSONATE 3

MIME_DEFINE_GUID(IID_IPersistFile, 0x0000010B, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

/* the persistence of a file, which GetObject asks of an object to load the file the script names. */
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

/* the string table starts here: 2000 for cscript, 2500 for wscript. */
#define RES_BASE (WSH_CONSOLE ? 2000 : 2500)

/* the codes the host raises in the engine to stop a script, and the one that says it was already reported. */
#define WSH_E_TERMINATE ((HRESULT)0x8004FFFC)
#define WSH_E_QUIT ((HRESULT)0x8004FFFD)
#define WSH_E_TIMEOUT ((HRESULT)0x8004FFFE)
#define WSH_E_REPORTED ((HRESULT)0x8004FFFF)

MIME_DEFINE_GUID(IID_IHost, 0x91AFBD1B, 0x5FEB, 0x43F5, 0xB0, 0x28, 0xE2, 0xCA, 0x96, 0x06, 0x17, 0xEC);
MIME_DEFINE_GUID(IID_IArguments2, 0x2CC5A9D1, 0xB1E5, 0x11D3, 0xA2, 0x86, 0x00, 0x10, 0x4B, 0xD3, 0x50, 0x90);
MIME_DEFINE_GUID(IID_IArguments, 0x60254CA3, 0x953B, 0x11CF, 0x8C, 0x96, 0x00, 0xAA, 0x00, 0xB8, 0x70, 0x8C);
MIME_DEFINE_GUID(IID_IWSHNamedArguments, 0x2CC5A9D0, 0xB1E5, 0x11D3, 0xA2, 0x86, 0x00, 0x10, 0x4B, 0xD3, 0x50, 0x90);
MIME_DEFINE_GUID(IID_IWSHUnnamedArguments, 0xBF64FAF0, 0x5906, 0x426C, 0xB4, 0xBC, 0x7B, 0x75, 0x3C, 0xBE, 0x81, 0x9F);
MIME_DEFINE_GUID(CLSID_IHost_Class, 0x60254CA2, 0x953B, 0x11CF, 0x8C, 0x96, 0x00, 0xAA, 0x00, 0xB8, 0x70, 0x8C);
MIME_DEFINE_GUID(CLSID_IArguments_Class, 0x60254CA4, 0x953B, 0x11CF, 0x8C, 0x96, 0x00, 0xAA, 0x00, 0xB8, 0x70, 0x8C);
MIME_DEFINE_GUID(CLSID_IWSHNamedArguments_Class, 0x54A24970, 0xB1E6, 0x11D3, 0xA2, 0x86, 0x00, 0x10, 0x4B, 0xD3, 0x50, 0x90);
MIME_DEFINE_GUID(CLSID_IWSHUnnamedArguments_Class, 0x2AC0525F, 0x38ED, 0x4B87, 0xB9, 0xA7, 0x78, 0x13, 0x2D, 0x32, 0xD5, 0x8F);
MIME_DEFINE_GUID(IID_IActiveScriptDebug64, 0xBC437E23, 0xF5B8, 0x47F4, 0xBB, 0x79, 0x7D, 0x1C, 0xE5, 0x48, 0x3B, 0x86);
MIME_DEFINE_GUID(IID_IActiveScriptDebug32, 0x51973C10, 0xCB0C, 0x11D0, 0xB5, 0xC9, 0x00, 0xA0, 0x24, 0x4A, 0x0E, 0x7A);
MIME_DEFINE_GUID(IID_IActiveScriptSiteDebug64, 0xD6B96B0A, 0x7463, 0x402C, 0x92, 0xAC, 0x89, 0x98, 0x42, 0x26, 0x94, 0x2F);

/* the host object and the arguments, the interfaces of the type library in the resources of this program. */
typedef struct IHost IHost;
typedef struct IArguments2 IArguments2;
typedef struct IWSHNamedArguments IWSHNamedArguments;
typedef struct IWSHUnnamedArguments IWSHUnnamedArguments;

#define IHost_METHODS(T)                                                                                               \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Name)(T * This, BSTR * value);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_Application)(T * This, IDispatch * *value);                                         \
    HRESULT(STDMETHODCALLTYPE *get_FullName)(T * This, BSTR * value);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_Path)(T * This, BSTR * value);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_Interactive)(T * This, VARIANT_BOOL * value);                                       \
    HRESULT(STDMETHODCALLTYPE *put_Interactive)(T * This, VARIANT_BOOL value);                                         \
    HRESULT(STDMETHODCALLTYPE *Quit)(T * This, int code);                                                              \
    HRESULT(STDMETHODCALLTYPE *get_ScriptName)(T * This, BSTR * value);                                                \
    HRESULT(STDMETHODCALLTYPE *get_ScriptFullName)(T * This, BSTR * value);                                            \
    HRESULT(STDMETHODCALLTYPE *get_Arguments)(T * This, IArguments2 * *value);                                         \
    HRESULT(STDMETHODCALLTYPE *get_Version)(T * This, BSTR * value);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_BuildVersion)(T * This, int *value);                                                \
    HRESULT(STDMETHODCALLTYPE *get_Timeout)(T * This, LONG * value);                                                   \
    HRESULT(STDMETHODCALLTYPE *put_Timeout)(T * This, LONG value);                                                     \
    HRESULT(STDMETHODCALLTYPE *CreateObject)(T * This, BSTR progid, BSTR prefix, IDispatch * *object);                 \
    HRESULT(STDMETHODCALLTYPE *Echo)(T * This, SAFEARRAY * args);                                                      \
    HRESULT(STDMETHODCALLTYPE *GetObject)(T * This, BSTR path, BSTR progid, BSTR prefix, IDispatch * *object);         \
    HRESULT(STDMETHODCALLTYPE *DisconnectObject)(T * This, IDispatch * object);                                        \
    HRESULT(STDMETHODCALLTYPE *Sleep)(T * This, LONG time);                                                            \
    HRESULT(STDMETHODCALLTYPE *ConnectObject)(T * This, IDispatch * object, BSTR prefix);                              \
    HRESULT(STDMETHODCALLTYPE *get_StdIn)(T * This, ITextStream * *stream);                                            \
    HRESULT(STDMETHODCALLTYPE *get_StdOut)(T * This, ITextStream * *stream);                                           \
    HRESULT(STDMETHODCALLTYPE *get_StdErr)(T * This, ITextStream * *stream);
MIME_COM_INTERFACE(IHost)

#define IArguments2_METHODS(T)                                                                                         \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *Item)(T * This, LONG index, BSTR * value);                                              \
    HRESULT(STDMETHODCALLTYPE *Count)(T * This, LONG * count);                                                         \
    HRESULT(STDMETHODCALLTYPE *get_length)(T * This, LONG * length);                                                   \
    HRESULT(STDMETHODCALLTYPE *_NewEnum)(T * This, IUnknown * *enumerator);                                            \
    HRESULT(STDMETHODCALLTYPE *get_Named)(T * This, IWSHNamedArguments * *named);                                      \
    HRESULT(STDMETHODCALLTYPE *get_Unnamed)(T * This, IWSHUnnamedArguments * *unnamed);                                \
    HRESULT(STDMETHODCALLTYPE *ShowUsage)(T * This);
MIME_COM_INTERFACE(IArguments2)

#define IWSHNamedArguments_METHODS(T)                                                                                  \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Item)(T * This, BSTR name, VARIANT * value);                                        \
    HRESULT(STDMETHODCALLTYPE *get_Count)(T * This, LONG * count);                                                     \
    HRESULT(STDMETHODCALLTYPE *get_length)(T * This, LONG * length);                                                   \
    HRESULT(STDMETHODCALLTYPE *Exists)(T * This, BSTR name, VARIANT_BOOL * exists);                                    \
    HRESULT(STDMETHODCALLTYPE *_NewEnum)(T * This, IUnknown * *enumerator);
MIME_COM_INTERFACE(IWSHNamedArguments)

#define IWSHUnnamedArguments_METHODS(T)                                                                                \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_Item)(T * This, LONG index, VARIANT * value);                                       \
    HRESULT(STDMETHODCALLTYPE *get_Count)(T * This, LONG * count);                                                     \
    HRESULT(STDMETHODCALLTYPE *get_length)(T * This, LONG * length);                                                   \
    HRESULT(STDMETHODCALLTYPE *_NewEnum)(T * This, IUnknown * *enumerator);
MIME_COM_INTERFACE(IWSHUnnamedArguments)

/* what the options, the registry and a settings file say about the run. The flags with Option tell that the command line
   gave the value, which a settings file does not override. */
typedef struct SETTINGS {
    DWORD Timeout;
    BOOL TimeoutSet, TimeoutOption;
    BOOL Logo, LogoSet, LogoOption;
    BOOL Batch, BatchSet;
    BOOL Debug, DebugSet, DebugCommand, Unicode, UseCodePage;
    UINT CodePage;
    BOOL Help, Save, SaveAlone, ChangeHost;
    int Host;
    PCWSTR Engine;
    PCWSTR Jobs[16];
    int JobCount;
} SETTINGS;

/* the host: what is known of the run from the start to the end. */
typedef struct HOST {
    BOOL Batch, Logo, Debug, Unicode;
    BOOL TimeoutSet;
    UINT CodePage;
    DWORD Timeout;
    int ExitCode;
    BOOL Quit;
    volatile LONG Pending;
    WCHAR Module[MAX_PATH];
    WCHAR ModuleDirectory[MAX_PATH];
    WORD VersionMajor, VersionMinor, VersionBuild, VersionRevision;
    BSTR ScriptPath;
    BSTR ScriptName;
    int ArgumentCount;
    PWSTR *Arguments;
    struct SCRIPT_ENGINE *Engine;
    struct HOST_OBJECT *Object;
    CRITICAL_SECTION Lock;
    HANDLE TimerThread, TimerEvent, TimerStop;
    DWORD MainThread;
    HINSTANCE Instance;
} HOST;

extern HOST g_host;

/* resources.c: strings, messages and the output of the two hosts. */
BSTR WshLoadString(UINT id);
HRESULT WshFormatMessage(PCWSTR format, BSTR *out, ...);
HRESULT WshFormatHResult(HRESULT code, BSTR *out);
void WshPrintF(UINT prefix, UINT id, ...);
HRESULT WshPrint(PCWSTR text);
HRESULT WshReportError(UINT prefix, UINT id, HRESULT code);
HRESULT WshReportError2(UINT prefix, UINT id, PCWSTR first, PCWSTR second);
HRESULT WshReportScriptError(ULONG line, LONG column, PCWSTR source, PCWSTR description, PCWSTR text, DWORD code);
void WshShowLogo(void);
void WshShowUsage(void);
int WshRunWithoutArgument(void);
void WshOnTimeout(void);
PCWSTR WshLastSeparator(PCWSTR path);
BOOL WshEqualNoCase(PCWSTR a, PCWSTR b, SIZE_T count);

/* host.c: the run of one script. */
HRESULT WshExecute(PCWSTR file, int count, PWSTR *arguments, PCWSTR engine, PCWSTR job);
HRESULT WshInterrupt(HRESULT code);
HRESULT WshStartTimer(DWORD seconds);
void WshStopTimer(void);
HRESULT WshLoadSource(PCWSTR path, BSTR *text);
HRESULT WshConvertText(UINT page, const BYTE *data, UINT size, BSTR *text);

/* object.c: the host object and the arguments. */
HRESULT WshCreateHostObject(int count, PWSTR *arguments, struct HOST_OBJECT **object);
void WshFreeHostObject(struct HOST_OBJECT *object);
IDispatch *WshHostDispatch(struct HOST_OBJECT *object);
HRESULT WshCreateArguments(int count, PWSTR *arguments, IArguments2 **out);
HRESULT WshArgumentsAddRuntimeNamed(IArguments2 *arguments, PCWSTR name, PCWSTR help, int type, BOOL required);
HRESULT WshArgumentsAddRuntimeUnnamed(IArguments2 *arguments, PCWSTR name, PCWSTR help, int count, int required);
HRESULT WshArgumentsSetText(IArguments2 *arguments, int which, PCWSTR text);
HRESULT WshSignal(const GUID *iid, PCWSTR source, UINT id, ...);
HRESULT WshSignalText(const GUID *iid, PCWSTR source, PCWSTR text);
IArguments2 *WshHostArguments(struct HOST_OBJECT *object);
HRESULT WshTypeInfoOf(const GUID *iid, ITypeInfo **info);

/* events.c: WScript.ConnectObject and the prefix of CreateObject. */
HRESULT WshConnectObject(const GUID *clsid, IDispatch *object, PCWSTR prefix);
HRESULT WshDisconnectObject(IDispatch *object);
void WshDisconnectAll(void);

/* settings.c: the registry, the .wsh files and the default host. */
HRESULT WshLoadRegistrySettings(HKEY root, SETTINGS *settings);
HRESULT WshSaveRegistrySettings(const SETTINGS *settings);
HRESULT WshLoadFileSettings(PCWSTR path, SETTINGS *settings, PWSTR script, DWORD capacity);
HRESULT WshChangeDefaultHost(int host);
BOOL WshSecurityEnabled(void);

/* options.c: the command line. */
int WshSplitCommandLine(PWSTR line, PWSTR *out);
HRESULT WshParseOptions(SETTINGS *settings, int *count, PWSTR *arguments);

/* wsf.c: windows script files. */
HRESULT WshRunWsf(PCWSTR path, PCWSTR job, int argc, PWSTR *argv);

/* an object a script file declares, which the engine may ask for by name. */
typedef struct ITEM {
    struct ITEM *Next;
    PWSTR Name;
    IUnknown *Object;
    DWORD Flags;
} ITEM;

/* an engine with its site: the site is the first part so that a pointer to it is a pointer to the engine. */
typedef struct SCRIPT_ENGINE {
    IActiveScriptSite Site;
    IActiveScriptSiteWindow Window;
    LONG Refs;
    IActiveScript *Script;
    IActiveScriptParse *Parse;
    ITEM *Items;
    DWORD Cookie;
    BOOL Closed;
    struct SCRIPT_ENGINE *Next;
} SCRIPT_ENGINE;

HRESULT WshCreateEngine(PCWSTR clsid_text, SCRIPT_ENGINE **engine);
HRESULT WshEngineAddItem(SCRIPT_ENGINE *engine, PCWSTR name, IUnknown *object, DWORD flags);
HRESULT WshEngineParse(SCRIPT_ENGINE *engine, BSTR code, ULONG first_line, DWORD flags);
HRESULT WshEngineRun(SCRIPT_ENGINE *engine);
void WshEngineClose(SCRIPT_ENGINE *engine);
HRESULT WshEngineNameOf(PCWSTR language, WCHAR *out, int capacity);

#endif
