/*
 * activscp.h: the interfaces of the active scripting engines and their hosts: IActiveScript with its parsers, the site
 * that hosts implement, and the error a site is told about.
 */
#ifndef MIME_WIN32_ACTIVSCP_H
#define MIME_WIN32_ACTIVSCP_H

#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>

MIME_DEFINE_GUID(IID_IActiveScript, 0xBB1A2AE1, 0xA4F9, 0x11CF, 0x8F, 0x20, 0x00, 0x80, 0x5F, 0x2C, 0xD0, 0x64);
MIME_DEFINE_GUID(IID_IActiveScriptParse, 0xBB1A2AE2, 0xA4F9, 0x11CF, 0x8F, 0x20, 0x00, 0x80, 0x5F, 0x2C, 0xD0, 0x64);
MIME_DEFINE_GUID(IID_IActiveScriptParse64, 0xC7EF7658, 0xE1EE, 0x480E, 0x97, 0xEA, 0xD5, 0x2C, 0xB4, 0xD7, 0x6D, 0x17);
MIME_DEFINE_GUID(IID_IActiveScriptParseProcedureOld, 0x1CFF0050, 0x6FDD, 0x11D0, 0x93, 0x28, 0x00, 0xA0, 0xC9, 0x0D, 0xCA, 0xA9);
MIME_DEFINE_GUID(IID_IActiveScriptParseProcedure, 0xAA5B6A80, 0xB834, 0x11D0, 0x93, 0x2F, 0x00, 0xA0, 0xC9, 0x0D, 0xCA, 0xA9);
MIME_DEFINE_GUID(IID_IActiveScriptParseProcedure2, 0x71EE5B20, 0xFB04, 0x11D1, 0xB3, 0xA8, 0x00, 0xA0, 0xC9, 0x11, 0xE8, 0xB2);
MIME_DEFINE_GUID(IID_IActiveScriptParseProcedure2_64, 0xFE7C4271, 0x210C, 0x448D, 0x9F, 0x54, 0x76, 0xDA, 0xB7, 0x04, 0x7D, 0x28);
MIME_DEFINE_GUID(IID_IActiveScriptError64, 0xB21FB2A1, 0x5B8F, 0x4963, 0x8C, 0x21, 0x21, 0x45, 0x0F, 0x84, 0xED, 0x7F);
MIME_DEFINE_GUID(IID_IActiveScriptSite, 0xDB01A1E3, 0xA42B, 0x11CF, 0x8F, 0x20, 0x00, 0x80, 0x5F, 0x2C, 0xD0, 0x64);
MIME_DEFINE_GUID(IID_IActiveScriptSiteWindow, 0xD10F6761, 0x83E9, 0x11CF, 0x8F, 0x20, 0x00, 0x80, 0x5F, 0x2C, 0xD0, 0x64);
MIME_DEFINE_GUID(IID_IActiveScriptSiteInterruptPoll, 0x539698A0, 0xCDCA, 0x11CF, 0xA5, 0xEB, 0x00, 0xAA, 0x00, 0x47, 0xA0, 0x63);
MIME_DEFINE_GUID(IID_IActiveScriptError, 0xEAE1BA61, 0xA4ED, 0x11CF, 0x8F, 0x20, 0x00, 0x80, 0x5F, 0x2C, 0xD0, 0x64);
MIME_DEFINE_GUID(IID_IActiveScriptGarbageCollector, 0x6AA2C4A0, 0x2B53, 0x11D4, 0xA2, 0xA0, 0x00, 0x10, 0x4B, 0xD3, 0x50, 0x90);
MIME_DEFINE_GUID(IID_IActiveScriptProperty, 0x4954E0D0, 0xFBC7, 0x11D1, 0x84, 0x10, 0x00, 0x60, 0x08, 0xC3, 0xFB, 0xFC);
MIME_DEFINE_GUID(IID_IActiveScriptStats, 0xB8DA6310, 0xE19B, 0x11D0, 0x93, 0x3C, 0x00, 0xA0, 0xC9, 0x0D, 0xCA, 0xA9);
MIME_DEFINE_GUID(IID_IObjectSafetyScript, 0xCB5BDC81, 0x93C1, 0x11CF, 0x8F, 0x20, 0x00, 0x80, 0x5F, 0x2C, 0xD0, 0x64);
MIME_DEFINE_GUID(IID_IDispatchEx, 0xA6EF9860, 0xC720, 0x11D0, 0x93, 0x37, 0x00, 0xA0, 0xC9, 0x0D, 0xCA, 0xA9);
MIME_DEFINE_GUID(IID_IProvideMultipleClassInfo, 0xA7ABA9C1, 0x8983, 0x11CF, 0x8F, 0x20, 0x00, 0x80, 0x5F, 0x2C, 0xD0, 0x64);

#define SCRIPTITEM_ISVISIBLE 0x00000002
#define SCRIPTITEM_ISSOURCE 0x00000004
#define SCRIPTITEM_GLOBALMEMBERS 0x00000008
#define SCRIPTITEM_ISPERSISTENT 0x00000040
#define SCRIPTITEM_CODEONLY 0x00000200
#define SCRIPTITEM_NOCODE 0x00000400
#define SCRIPTITEM_ALL_FLAGS 0x0000064E

#define SCRIPTTYPELIB_ISCONTROL 0x00000010
#define SCRIPTTYPELIB_ISPERSISTENT 0x00000040

#define SCRIPTTEXT_DELAYEXECUTION 0x00000001
#define SCRIPTTEXT_ISVISIBLE 0x00000002
#define SCRIPTTEXT_ISEXPRESSION 0x00000020
#define SCRIPTTEXT_ISPERSISTENT 0x00000040
#define SCRIPTTEXT_HOSTMANAGESSOURCE 0x00000080
#define SCRIPTTEXT_ALL_FLAGS 0x000000E3

#define SCRIPTPROC_ISEXPRESSION 0x00000020
#define SCRIPTPROC_HOSTMANAGESSOURCE 0x00000080
#define SCRIPTPROC_IMPLICIT_THIS 0x00000100
#define SCRIPTPROC_IMPLICIT_PARENTS 0x00000200
#define SCRIPTPROC_ALL_FLAGS 0x000003A0

#define SCRIPTINFO_IUNKNOWN 0x00000001
#define SCRIPTINFO_ITYPEINFO 0x00000002
#define SCRIPTINFO_ALL_FLAGS 0x00000003

#define SCRIPTINTERRUPT_DEBUG 0x00000001
#define SCRIPTINTERRUPT_RAISEEXCEPTION 0x00000002
#define SCRIPTINTERRUPT_ALL_FLAGS 0x00000003

#define SCRIPTTHREADID_CURRENT ((DWORD)-1)
#define SCRIPTTHREADID_BASE ((DWORD)-2)
#define SCRIPTTHREADID_ALL ((DWORD)-3)

#define SCRIPT_E_REPORTED ((HRESULT)0x80020101L)
#define SCRIPT_E_RECORDED ((HRESULT)0x86664004L)
#define SCRIPT_E_PROPAGATE ((HRESULT)0x80020102L)

#define INTERFACESAFE_FOR_UNTRUSTED_CALLER 0x00000001
#define INTERFACESAFE_FOR_UNTRUSTED_DATA 0x00000002
#define INTERFACE_USES_DISPEX 0x00000004
#define INTERFACE_USES_SECURITY_MANAGER 0x00000008

#define SCRIPTPROP_INTEGERMODE 0x00004000
#define SCRIPTPROP_ABBREVIATE_GLOBALNAME_RESOLUTION 0x70000002
#define SCRIPTPROP_HACK_FIBERSUPPORT 0x70000003
#define SCRIPTPROP_HACK_TRIDENTEVENTSINK 0x70000004
#define SCRIPTPROP_INVOKEVERSIONING 0x00004004
#define SCRIPTPROP_CATCHEXCEPTION 0x00004001
#define SCRIPTPROP_CONVERSIONLCID 0x00004002

typedef enum tagSCRIPTSTATE {
    SCRIPTSTATE_UNINITIALIZED = 0,
    SCRIPTSTATE_STARTED = 1,
    SCRIPTSTATE_CONNECTED = 2,
    SCRIPTSTATE_DISCONNECTED = 3,
    SCRIPTSTATE_CLOSED = 4,
    SCRIPTSTATE_INITIALIZED = 5
} SCRIPTSTATE;

typedef enum tagSCRIPTTHREADSTATE {
    SCRIPTTHREADSTATE_NOTINSCRIPT = 0,
    SCRIPTTHREADSTATE_RUNNING = 1
} SCRIPTTHREADSTATE;

typedef DWORD SCRIPTTHREADID;

typedef struct IActiveScriptError IActiveScriptError;
typedef struct IActiveScriptSite IActiveScriptSite;
typedef struct IActiveScript IActiveScript;

#define IActiveScriptError_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetExceptionInfo)(T * This, EXCEPINFO * info);                                          \
    HRESULT(STDMETHODCALLTYPE *GetSourcePosition)(T * This, DWORD * context, ULONG * line, LONG * character);          \
    HRESULT(STDMETHODCALLTYPE *GetSourceLineText)(T * This, BSTR * text);
MIME_COM_INTERFACE(IActiveScriptError)

#define IActiveScriptSite_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetLCID)(T * This, LCID * lcid);                                                        \
    HRESULT(STDMETHODCALLTYPE *GetItemInfo)(T * This, LPCOLESTR name, DWORD mask, IUnknown * *item, ITypeInfo * *info); \
    HRESULT(STDMETHODCALLTYPE *GetDocVersionString)(T * This, BSTR * version);                                         \
    HRESULT(STDMETHODCALLTYPE *OnScriptTerminate)(T * This, const VARIANT *result, const EXCEPINFO *info);             \
    HRESULT(STDMETHODCALLTYPE *OnStateChange)(T * This, SCRIPTSTATE state);                                            \
    HRESULT(STDMETHODCALLTYPE *OnScriptError)(T * This, IActiveScriptError * error);                                   \
    HRESULT(STDMETHODCALLTYPE *OnEnterScript)(T * This);                                                               \
    HRESULT(STDMETHODCALLTYPE *OnLeaveScript)(T * This);
MIME_COM_INTERFACE(IActiveScriptSite)

typedef struct IActiveScriptSiteWindow IActiveScriptSiteWindow;
#define IActiveScriptSiteWindow_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetWindow)(T * This, HWND * window);                                                    \
    HRESULT(STDMETHODCALLTYPE *EnableModeless)(T * This, BOOL enable);
MIME_COM_INTERFACE(IActiveScriptSiteWindow)

typedef struct IActiveScriptSiteInterruptPoll IActiveScriptSiteInterruptPoll;
#define IActiveScriptSiteInterruptPoll_METHODS(T)                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *QueryContinue)(T * This);
MIME_COM_INTERFACE(IActiveScriptSiteInterruptPoll)

#define IActiveScript_METHODS(T)                                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetScriptSite)(T * This, IActiveScriptSite * site);                                     \
    HRESULT(STDMETHODCALLTYPE *GetScriptSite)(T * This, REFIID riid, void **site);                                     \
    HRESULT(STDMETHODCALLTYPE *SetScriptState)(T * This, SCRIPTSTATE state);                                           \
    HRESULT(STDMETHODCALLTYPE *GetScriptState)(T * This, SCRIPTSTATE * state);                                         \
    HRESULT(STDMETHODCALLTYPE *Close)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *AddNamedItem)(T * This, LPCOLESTR name, DWORD flags);                                   \
    HRESULT(STDMETHODCALLTYPE *AddTypeLib)(T * This, REFGUID lib, DWORD major, DWORD minor, DWORD flags);              \
    HRESULT(STDMETHODCALLTYPE *GetScriptDispatch)(T * This, LPCOLESTR item, IDispatch * *dispatch);                    \
    HRESULT(STDMETHODCALLTYPE *GetCurrentScriptThreadID)(T * This, SCRIPTTHREADID * id);                               \
    HRESULT(STDMETHODCALLTYPE *GetScriptThreadID)(T * This, DWORD thread, SCRIPTTHREADID * id);                        \
    HRESULT(STDMETHODCALLTYPE *GetScriptThreadState)(T * This, SCRIPTTHREADID id, SCRIPTTHREADSTATE * state);          \
    HRESULT(STDMETHODCALLTYPE *InterruptScriptThread)(T * This, SCRIPTTHREADID id, const EXCEPINFO *info, DWORD flags); \
    HRESULT(STDMETHODCALLTYPE *Clone)(T * This, IActiveScript * *clone);
MIME_COM_INTERFACE(IActiveScript)

typedef struct IActiveScriptParse IActiveScriptParse;
#define IActiveScriptParse_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *InitNew)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *AddScriptlet)(T * This, LPCOLESTR default_name, LPCOLESTR code, LPCOLESTR item_name,    \
                                             LPCOLESTR subitem_name, LPCOLESTR event_name, LPCOLESTR delimiter,        \
                                             DWORD_PTR context, ULONG line, DWORD flags, BSTR * name,                  \
                                             EXCEPINFO * info);                                                        \
    HRESULT(STDMETHODCALLTYPE *ParseScriptText)(T * This, LPCOLESTR code, LPCOLESTR item_name, IUnknown * context,     \
                                                LPCOLESTR delimiter, DWORD_PTR cookie, ULONG line, DWORD flags,        \
                                                VARIANT * result, EXCEPINFO * info);
MIME_COM_INTERFACE(IActiveScriptParse)

typedef struct IActiveScriptParseProcedure2 IActiveScriptParseProcedure2;
#define IActiveScriptParseProcedure2_METHODS(T)                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *ParseProcedureText)(T * This, LPCOLESTR code, LPCOLESTR params, LPCOLESTR name,         \
                                                   LPCOLESTR item_name, IUnknown * context, LPCOLESTR delimiter,       \
                                                   DWORD_PTR cookie, ULONG line, DWORD flags, IDispatch * *out);
MIME_COM_INTERFACE(IActiveScriptParseProcedure2)

typedef struct IActiveScriptGarbageCollector IActiveScriptGarbageCollector;
typedef enum tagSCRIPTGCTYPE {
    SCRIPTGCTYPE_NORMAL = 0,
    SCRIPTGCTYPE_EXHAUSTIVE = 1
} SCRIPTGCTYPE;
#define IActiveScriptGarbageCollector_METHODS(T)                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CollectGarbage)(T * This, SCRIPTGCTYPE type);
MIME_COM_INTERFACE(IActiveScriptGarbageCollector)

typedef struct IActiveScriptProperty IActiveScriptProperty;
#define IActiveScriptProperty_METHODS(T)                                                                               \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetProperty)(T * This, DWORD id, VARIANT * arg, VARIANT * value);                       \
    HRESULT(STDMETHODCALLTYPE *SetProperty)(T * This, DWORD id, VARIANT * arg, VARIANT * value);
MIME_COM_INTERFACE(IActiveScriptProperty)

typedef struct IObjectSafety IObjectSafety;
#define IObjectSafety_METHODS(T)                                                                                       \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetInterfaceSafetyOptions)(T * This, REFIID riid, DWORD * supported, DWORD * enabled);  \
    HRESULT(STDMETHODCALLTYPE *SetInterfaceSafetyOptions)(T * This, REFIID riid, DWORD mask, DWORD options);
MIME_COM_INTERFACE(IObjectSafety)

#endif
