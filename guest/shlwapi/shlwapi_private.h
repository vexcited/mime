/*
 * shlwapi internal interface shared by its source files: the com interfaces its helpers call
 * through and the functions of other dlls it loads on demand.
 */
#ifndef MIME_SHLWAPI_PRIVATE_H
#define MIME_SHLWAPI_PRIVATE_H

#define LWSTDAPI
#include <mime/win32/shlwapi.h>
#include <mime/win32/winnls.h>
#include <mime/win32/oleauto.h>
#include <rtl.h>

/* exported entry points without an implementation, like mime/stubexport.h but exported by the def. */
#include <mime/stub.h>
#define SHLWAPI_STUB_ARGS_0 void
#define SHLWAPI_STUB_ARGS_1 ULONG_PTR a1
#define SHLWAPI_STUB_ARGS_2 SHLWAPI_STUB_ARGS_1, ULONG_PTR a2
#define SHLWAPI_STUB_ARGS_3 SHLWAPI_STUB_ARGS_2, ULONG_PTR a3
#define SHLWAPI_STUB_ARGS_4 SHLWAPI_STUB_ARGS_3, ULONG_PTR a4
#define SHLWAPI_STUB_ARGS_5 SHLWAPI_STUB_ARGS_4, ULONG_PTR a5
#define SHLWAPI_STUB_ARGS_6 SHLWAPI_STUB_ARGS_5, ULONG_PTR a6
#define SHLWAPI_STUB_ARGS_7 SHLWAPI_STUB_ARGS_6, ULONG_PTR a7
#define SHLWAPI_STUB_ARGS_8 SHLWAPI_STUB_ARGS_7, ULONG_PTR a8
#define SHLWAPI_STUB_ARGS_9 SHLWAPI_STUB_ARGS_8, ULONG_PTR a9
#define SHLWAPI_STUB_ARGS_12 SHLWAPI_STUB_ARGS_9, ULONG_PTR a10, ULONG_PTR a11, ULONG_PTR a12
#define SHLWAPI_STUB(type, name, args, value)                                                                         \
    type WINAPI name(SHLWAPI_STUB_ARGS_##args);                                                                        \
    type WINAPI name(SHLWAPI_STUB_ARGS_##args)                                                                         \
    {                                                                                                                  \
        MIME_STUB("shlwapi");                                                                                          \
        return value;                                                                                                  \
    }
#define SHLWAPI_STUB_VOID(name, args)                                                                                  \
    void WINAPI name(SHLWAPI_STUB_ARGS_##args);                                                                        \
    void WINAPI name(SHLWAPI_STUB_ARGS_##args) { MIME_STUB("shlwapi"); }

/* a function of another dll, loaded the first time it is asked for. */
FARPROC ShlProc(LPCWSTR dll, LPCSTR name);

/* ansi strings widened into heap memory, freed with ShlFree. */
LPWSTR ShlWide(LPCSTR s);
void ShlFree(void *p);
void *ShlAlloc(SIZE_T size);

/* task memory of ole32, which the caller frees with CoTaskMemFree. */
void *ShlTaskAlloc(SIZE_T size);
void ShlTaskFree(void *p);

/* the private ordinals other shell dlls use. */
HRESULT WINAPI SHLoadRegUIStringW(HKEY key, LPCWSTR value, LPWSTR out, DWORD size);
BOOL WINAPI SHIsChildOrSelf(HWND parent, HWND child);

/* ------------------------------------------------------------------ interfaces */

#ifndef STREAM_SEEK_SET
#define STREAM_SEEK_SET 0
#define STREAM_SEEK_CUR 1
#define STREAM_SEEK_END 2
#endif

#define SHL_INTERFACE(name)                                                                                            \
    typedef struct name name;                                                                                          \
    struct name {                                                                                                      \
        const struct name##Vtbl *lpVtbl;                                                                               \
    }

#define SHL_UNKNOWN(name)                                                                                              \
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(name * This, REFIID riid, void **object);                               \
    ULONG(STDMETHODCALLTYPE *AddRef)(name * This);                                                                     \
    ULONG(STDMETHODCALLTYPE *Release)(name * This)

typedef struct {
    ULONG cmdID;
    DWORD cmdf;
} OLECMD;

typedef struct {
    DWORD cmdtextf;
    ULONG cwActual;
    ULONG cwBuf;
    WCHAR rgwz[1];
} OLECMDTEXT;

SHL_INTERFACE(IOleCommandTarget);
struct IOleCommandTargetVtbl {
    SHL_UNKNOWN(IOleCommandTarget);
    HRESULT(STDMETHODCALLTYPE *QueryStatus)(IOleCommandTarget *This, const GUID *group, ULONG count, OLECMD *commands,
                                            OLECMDTEXT *text);
    HRESULT(STDMETHODCALLTYPE *Exec)(IOleCommandTarget *This, const GUID *group, DWORD id, DWORD options,
                                     VARIANT *in, VARIANT *out);
};

SHL_INTERFACE(IOleWindow);
struct IOleWindowVtbl {
    SHL_UNKNOWN(IOleWindow);
    HRESULT(STDMETHODCALLTYPE *GetWindow)(IOleWindow *This, HWND *window);
    HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)(IOleWindow *This, BOOL enter);
};

SHL_INTERFACE(IServiceProvider);
struct IServiceProviderVtbl {
    SHL_UNKNOWN(IServiceProvider);
    HRESULT(STDMETHODCALLTYPE *QueryService)(IServiceProvider *This, REFGUID service, REFIID riid, void **out);
};

SHL_INTERFACE(IPersist);
struct IPersistVtbl {
    SHL_UNKNOWN(IPersist);
    HRESULT(STDMETHODCALLTYPE *GetClassID)(IPersist *This, CLSID *clsid);
};

/* IShellService, IInputObject, IInputObjectSite and IObjectWithSite share the first method slot. */
SHL_INTERFACE(IShellService);
struct IShellServiceVtbl {
    SHL_UNKNOWN(IShellService);
    HRESULT(STDMETHODCALLTYPE *SetOwner)(IShellService *This, IUnknown *owner);
};

SHL_INTERFACE(IInputObject);
struct IInputObjectVtbl {
    SHL_UNKNOWN(IInputObject);
    HRESULT(STDMETHODCALLTYPE *UIActivateIO)(IInputObject *This, BOOL activate, MSG *msg);
    HRESULT(STDMETHODCALLTYPE *HasFocusIO)(IInputObject *This);
    HRESULT(STDMETHODCALLTYPE *TranslateAcceleratorIO)(IInputObject *This, MSG *msg);
};

SHL_INTERFACE(IInputObjectSite);
struct IInputObjectSiteVtbl {
    SHL_UNKNOWN(IInputObjectSite);
    HRESULT(STDMETHODCALLTYPE *OnFocusChangeIS)(IInputObjectSite *This, IUnknown *object, BOOL focus);
};

SHL_INTERFACE(IProfferService);
struct IProfferServiceVtbl {
    SHL_UNKNOWN(IProfferService);
    HRESULT(STDMETHODCALLTYPE *ProfferService)(IProfferService *This, REFGUID service, IServiceProvider *provider,
                                               DWORD *cookie);
    HRESULT(STDMETHODCALLTYPE *RevokeService)(IProfferService *This, DWORD cookie);
};

SHL_INTERFACE(IOleInPlaceActiveObject);
struct IOleInPlaceActiveObjectVtbl {
    SHL_UNKNOWN(IOleInPlaceActiveObject);
    HRESULT(STDMETHODCALLTYPE *GetWindow)(IOleInPlaceActiveObject *This, HWND *window);
    HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)(IOleInPlaceActiveObject *This, BOOL enter);
    HRESULT(STDMETHODCALLTYPE *TranslateAccelerator)(IOleInPlaceActiveObject *This, MSG *msg);
    HRESULT(STDMETHODCALLTYPE *OnFrameWindowActivate)(IOleInPlaceActiveObject *This, BOOL activate);
    HRESULT(STDMETHODCALLTYPE *OnDocWindowActivate)(IOleInPlaceActiveObject *This, BOOL activate);
    HRESULT(STDMETHODCALLTYPE *ResizeBorder)(IOleInPlaceActiveObject *This, const RECT *border, IUnknown *site,
                                             BOOL frame);
    HRESULT(STDMETHODCALLTYPE *EnableModeless)(IOleInPlaceActiveObject *This, BOOL enable);
};

SHL_INTERFACE(IConnectionPoint);
SHL_INTERFACE(IConnectionPointContainer);
struct IConnectionPointVtbl {
    SHL_UNKNOWN(IConnectionPoint);
    HRESULT(STDMETHODCALLTYPE *GetConnectionInterface)(IConnectionPoint *This, IID *iid);
    HRESULT(STDMETHODCALLTYPE *GetConnectionPointContainer)(IConnectionPoint *This,
                                                            IConnectionPointContainer **container);
    HRESULT(STDMETHODCALLTYPE *Advise)(IConnectionPoint *This, IUnknown *sink, DWORD *cookie);
    HRESULT(STDMETHODCALLTYPE *Unadvise)(IConnectionPoint *This, DWORD cookie);
    HRESULT(STDMETHODCALLTYPE *EnumConnections)(IConnectionPoint *This, void **connections);
};
struct IConnectionPointContainerVtbl {
    SHL_UNKNOWN(IConnectionPointContainer);
    HRESULT(STDMETHODCALLTYPE *EnumConnectionPoints)(IConnectionPointContainer *This, void **points);
    HRESULT(STDMETHODCALLTYPE *FindConnectionPoint)(IConnectionPointContainer *This, REFIID riid,
                                                    IConnectionPoint **point);
};

SHL_INTERFACE(IPropertyBag);
struct IPropertyBagVtbl {
    SHL_UNKNOWN(IPropertyBag);
    HRESULT(STDMETHODCALLTYPE *Read)(IPropertyBag *This, LPCOLESTR name, VARIANT *value, IUnknown *log);
    HRESULT(STDMETHODCALLTYPE *Write)(IPropertyBag *This, LPCOLESTR name, VARIANT *value);
};

MIME_DEFINE_GUID(IID_IOleCommandTarget, 0xb722bccb, 0x4e68, 0x101b, 0xa2, 0xbc, 0x00, 0xaa, 0x00, 0x40, 0x47, 0x70);
MIME_DEFINE_GUID(IID_IOleWindow, 0x00000114, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IServiceProvider, 0x6d5140c1, 0x7436, 0x11ce, 0x80, 0x34, 0x00, 0xaa, 0x00, 0x60, 0x09, 0xfa);
MIME_DEFINE_GUID(IID_IShellService, 0x5836fb00, 0x8187, 0x11cf, 0xa1, 0x2b, 0x00, 0xaa, 0x00, 0x4a, 0xe8, 0x37);
MIME_DEFINE_GUID(IID_IInputObject, 0x68284faa, 0x6a48, 0x11d0, 0x8c, 0x78, 0x00, 0xc0, 0x4f, 0xd9, 0x18, 0xb4);
MIME_DEFINE_GUID(IID_IInputObjectSite, 0xf1db8392, 0x7331, 0x11d0, 0x8c, 0x99, 0x00, 0xa0, 0xc9, 0x2d, 0xbf, 0xe8);
MIME_DEFINE_GUID(IID_IProfferService, 0xcb728b20, 0xf786, 0x11ce, 0x92, 0xad, 0x00, 0xaa, 0x00, 0xa7, 0x4c, 0xd0);
MIME_DEFINE_GUID(IID_IOleInPlaceActiveObject, 0x00000117, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x46);
MIME_DEFINE_GUID(IID_IConnectionPointContainer, 0xb196b284, 0xbab4, 0x101a, 0xb6, 0x9c, 0x00, 0xaa, 0x00, 0x34, 0x1d,
                 0x07);
MIME_DEFINE_GUID(IID_IPropertyBag, 0x55272a00, 0x42cb, 0x11ce, 0x81, 0x35, 0x00, 0xaa, 0x00, 0x4b, 0xb8, 0x51);
MIME_DEFINE_GUID(IID_IInternetSecurityMgrSite, 0x79eac9ed, 0xbaf9, 0x11ce, 0x8c, 0x82, 0x00, 0xaa, 0x00, 0x4b, 0xa9,
                 0x0b);
MIME_DEFINE_GUID(IID_IShellView, 0x000214e3, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_ShlWindowSource, 0x45d64a29, 0xa63e, 0x4cb6, 0xb4, 0x98, 0x57, 0x81, 0xd2, 0x98, 0xcb, 0x4f);
MIME_DEFINE_GUID(IID_IObjectWithSite, 0xfc4801a3, 0x2ba9, 0x11cf, 0xa2, 0x29, 0x00, 0xaa, 0x00, 0x3d, 0x73, 0x52);

#endif
