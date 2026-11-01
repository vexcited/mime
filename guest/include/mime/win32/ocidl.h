/* the interfaces of an embedded control and of the container that holds it: in place activation, the control's own
 * calls, the view, persistence and the commands. */
#ifndef MIME_WIN32_OCIDL_H
#define MIME_WIN32_OCIDL_H

#include <mime/win32/oleidl.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/olectl.h>

#ifndef CONNECT_E_NOCONNECTION
#define CONNECT_E_NOCONNECTION ((HRESULT)0x80040200L)
#define CONNECT_E_ADVISELIMIT ((HRESULT)0x80040201L)
#define CONNECT_E_CANNOTCONNECT ((HRESULT)0x80040202L)
#endif

#define IOleObject_METHODS(T)                                                                                          \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetClientSite)(T * This, IOleClientSite * site);                                        \
    HRESULT(STDMETHODCALLTYPE *GetClientSite)(T * This, IOleClientSite * *site);                                       \
    HRESULT(STDMETHODCALLTYPE *SetHostNames)(T * This, LPCOLESTR app, LPCOLESTR object);                               \
    HRESULT(STDMETHODCALLTYPE *Close)(T * This, DWORD save_option);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetMoniker)(T * This, DWORD which, IMoniker * moniker);                                 \
    HRESULT(STDMETHODCALLTYPE *GetMoniker)(T * This, DWORD assign, DWORD which, IMoniker * *moniker);                  \
    HRESULT(STDMETHODCALLTYPE *InitFromData)(T * This, IDataObject * data, BOOL creation, DWORD reserved);             \
    HRESULT(STDMETHODCALLTYPE *GetClipboardData)(T * This, DWORD reserved, IDataObject * *data);                       \
    HRESULT(STDMETHODCALLTYPE *DoVerb)(T * This, LONG verb, MSG * message, IOleClientSite * site, LONG index,          \
                                       HWND parent, LPCRECT position);                                                 \
    HRESULT(STDMETHODCALLTYPE *EnumVerbs)(T * This, IEnumOLEVERB * *verbs);                                            \
    HRESULT(STDMETHODCALLTYPE *Update)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *IsUpToDate)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *GetUserClassID)(T * This, CLSID * clsid);                                               \
    HRESULT(STDMETHODCALLTYPE *GetUserType)(T * This, DWORD form, LPOLESTR * type);                                    \
    HRESULT(STDMETHODCALLTYPE *SetExtent)(T * This, DWORD aspect, SIZEL * size);                                       \
    HRESULT(STDMETHODCALLTYPE *GetExtent)(T * This, DWORD aspect, SIZEL * size);                                       \
    HRESULT(STDMETHODCALLTYPE *Advise)(T * This, IAdviseSink * sink, DWORD * connection);                              \
    HRESULT(STDMETHODCALLTYPE *Unadvise)(T * This, DWORD connection);                                                  \
    HRESULT(STDMETHODCALLTYPE *EnumAdvise)(T * This, IEnumSTATDATA * *enumerator);                                     \
    HRESULT(STDMETHODCALLTYPE *GetMiscStatus)(T * This, DWORD aspect, DWORD * status);                                 \
    HRESULT(STDMETHODCALLTYPE *SetColorScheme)(T * This, LOGPALETTE * palette);
MIME_COM_INTERFACE(IOleObject)

#define IOleClientSite_METHODS(T)                                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SaveObject)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *GetMoniker)(T * This, DWORD assign, DWORD which, IMoniker * *moniker);                  \
    HRESULT(STDMETHODCALLTYPE *GetContainer)(T * This, void **container);                                              \
    HRESULT(STDMETHODCALLTYPE *ShowObject)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *OnShowWindow)(T * This, BOOL show);                                                     \
    HRESULT(STDMETHODCALLTYPE *RequestNewObjectLayout)(T * This);
MIME_COM_INTERFACE(IOleClientSite)

#define IOleInPlaceObject_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetWindow)(T * This, HWND * window);                                                    \
    HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)(T * This, BOOL enter);                                            \
    HRESULT(STDMETHODCALLTYPE *InPlaceDeactivate)(T * This);                                                           \
    HRESULT(STDMETHODCALLTYPE *UIDeactivate)(T * This);                                                                \
    HRESULT(STDMETHODCALLTYPE *SetObjectRects)(T * This, LPCRECT position, LPCRECT clip);                              \
    HRESULT(STDMETHODCALLTYPE *ReactivateAndUndo)(T * This);
typedef struct IOleInPlaceObject IOleInPlaceObject;
MIME_COM_INTERFACE(IOleInPlaceObject)

#define IOleInPlaceActiveObject_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetWindow)(T * This, HWND * window);                                                    \
    HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)(T * This, BOOL enter);                                            \
    HRESULT(STDMETHODCALLTYPE *TranslateAccelerator)(T * This, MSG * message);                                         \
    HRESULT(STDMETHODCALLTYPE *OnFrameWindowActivate)(T * This, BOOL activate);                                        \
    HRESULT(STDMETHODCALLTYPE *OnDocWindowActivate)(T * This, BOOL activate);                                          \
    HRESULT(STDMETHODCALLTYPE *ResizeBorder)(T * This, LPCRECT border, void *uiwindow, BOOL frame);                    \
    HRESULT(STDMETHODCALLTYPE *EnableModeless)(T * This, BOOL enable);
MIME_COM_INTERFACE(IOleInPlaceActiveObject)

typedef struct IOleInPlaceSite IOleInPlaceSite;
#define IOleInPlaceSite_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetWindow)(T * This, HWND * window);                                                    \
    HRESULT(STDMETHODCALLTYPE *ContextSensitiveHelp)(T * This, BOOL enter);                                            \
    HRESULT(STDMETHODCALLTYPE *CanInPlaceActivate)(T * This);                                                          \
    HRESULT(STDMETHODCALLTYPE *OnInPlaceActivate)(T * This);                                                           \
    HRESULT(STDMETHODCALLTYPE *OnUIActivate)(T * This);                                                                \
    HRESULT(STDMETHODCALLTYPE *GetWindowContext)(T * This, IOleInPlaceFrame * *frame, void **doc, LPRECT position,     \
                                                 LPRECT clip, LPOLEINPLACEFRAMEINFO info);                             \
    HRESULT(STDMETHODCALLTYPE *Scroll)(T * This, SIZE extent);                                                         \
    HRESULT(STDMETHODCALLTYPE *OnUIDeactivate)(T * This, BOOL undoable);                                               \
    HRESULT(STDMETHODCALLTYPE *OnInPlaceDeactivate)(T * This);                                                         \
    HRESULT(STDMETHODCALLTYPE *DiscardUndoState)(T * This);                                                            \
    HRESULT(STDMETHODCALLTYPE *DeactivateAndUndo)(T * This);                                                           \
    HRESULT(STDMETHODCALLTYPE *OnPosRectChange)(T * This, LPCRECT position);
MIME_COM_INTERFACE(IOleInPlaceSite)

typedef struct CONTROLINFO {
    ULONG cb;
    HACCEL hAccel;
    USHORT cAccel;
    DWORD dwFlags;
} CONTROLINFO;
typedef struct IOleControl IOleControl;
#define IOleControl_METHODS(T)                                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetControlInfo)(T * This, CONTROLINFO * info);                                          \
    HRESULT(STDMETHODCALLTYPE *OnMnemonic)(T * This, MSG * message);                                                   \
    HRESULT(STDMETHODCALLTYPE *OnAmbientPropertyChange)(T * This, DISPID id);                                          \
    HRESULT(STDMETHODCALLTYPE *FreezeEvents)(T * This, BOOL freeze);
MIME_COM_INTERFACE(IOleControl)

typedef struct IViewObject2 IViewObject2;
#define IViewObject2_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Draw)(T * This, DWORD aspect, LONG index, void *aspect_info, DVTARGETDEVICE *device,    \
                                     HDC target, HDC draw, const RECTL *bounds, const RECTL *window_bounds,            \
                                     BOOL(STDMETHODCALLTYPE * keep_going)(ULONG_PTR), ULONG_PTR argument);             \
    HRESULT(STDMETHODCALLTYPE *GetColorSet)(T * This, DWORD aspect, LONG index, void *aspect_info,                     \
                                            DVTARGETDEVICE *device, HDC target, LOGPALETTE **colors);                  \
    HRESULT(STDMETHODCALLTYPE *Freeze)(T * This, DWORD aspect, LONG index, void *aspect_info, DWORD *freeze);          \
    HRESULT(STDMETHODCALLTYPE *Unfreeze)(T * This, DWORD freeze);                                                      \
    HRESULT(STDMETHODCALLTYPE *SetAdvise)(T * This, DWORD aspects, DWORD flags, IAdviseSink *sink);                    \
    HRESULT(STDMETHODCALLTYPE *GetAdvise)(T * This, DWORD *aspects, DWORD *flags, IAdviseSink **sink);                 \
    HRESULT(STDMETHODCALLTYPE *GetExtent)(T * This, DWORD aspect, LONG index, DVTARGETDEVICE *device, SIZEL *size);
MIME_COM_INTERFACE(IViewObject2)

typedef struct IPersistStreamInit IPersistStreamInit;
#define IPersistStreamInit_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetClassID)(T * This, CLSID * clsid);                                                   \
    HRESULT(STDMETHODCALLTYPE *IsDirty)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *Load)(T * This, IStream * stream);                                                      \
    HRESULT(STDMETHODCALLTYPE *Save)(T * This, IStream * stream, BOOL clear_dirty);                                    \
    HRESULT(STDMETHODCALLTYPE *GetSizeMax)(T * This, ULARGE_INTEGER * size);                                           \
    HRESULT(STDMETHODCALLTYPE *InitNew)(T * This);
MIME_COM_INTERFACE(IPersistStreamInit)

typedef struct IProvideClassInfo2 IProvideClassInfo2;
#define IProvideClassInfo2_METHODS(T)                                                                                  \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetClassInfo)(T * This, ITypeInfo * *info);                                             \
    HRESULT(STDMETHODCALLTYPE *GetGUID)(T * This, DWORD kind, GUID * guid);
MIME_COM_INTERFACE(IProvideClassInfo2)

typedef struct OLECMD {
    ULONG cmdID;
    DWORD cmdf;
} OLECMD;
typedef struct OLECMDTEXT {
    DWORD cmdtextf;
    ULONG cwActual;
    ULONG cwBuf;
    WCHAR rgwz[1];
} OLECMDTEXT;
typedef struct IOleCommandTarget IOleCommandTarget;
#define IOleCommandTarget_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *QueryStatus)(T * This, const GUID *group, ULONG count, OLECMD *commands,               \
                                            OLECMDTEXT *text);                                                         \
    HRESULT(STDMETHODCALLTYPE *Exec)(T * This, const GUID *group, DWORD id, DWORD options, VARIANT *in,                \
                                     VARIANT *out);
MIME_COM_INTERFACE(IOleCommandTarget)


MIME_DEFINE_GUID(IID_IOleInPlaceObject, 0x00000113, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IOleInPlaceActiveObject, 0x00000117, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IOleInPlaceSite, 0x00000119, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IOleClientSite, 0x00000118, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IViewObject2, 0x00000127, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IPersistStreamInit, 0x7FD52380, 0x4E07, 0x101B, 0xAE, 0x2D, 0x08, 0x00, 0x2B, 0x2E, 0xC7, 0x13);
MIME_DEFINE_GUID(IID_IOleControl, 0xB196B288, 0xBAB4, 0x101A, 0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07);
MIME_DEFINE_GUID(IID_IProvideClassInfo2, 0xA6BC3AC0, 0xDBAA, 0x11CE, 0x9D, 0xE3, 0x00, 0xAA, 0x00, 0x4B, 0xB8, 0x51);
MIME_DEFINE_GUID(IID_IOleCommandTarget, 0xB722BCCB, 0x4E68, 0x101B, 0xA2, 0xBC, 0x00, 0xAA, 0x00, 0x40, 0x47, 0x70);

#endif
