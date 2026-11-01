/* the web browser control of ieframe.dll: IWebBrowser2, the events it sends and its class. */
#ifndef MIME_WIN32_EXDISP_H
#define MIME_WIN32_EXDISP_H

#include <mime/win32/ocidl.h>

typedef struct IWebBrowser2 IWebBrowser2;
#define IWebBrowser2_METHODS(T)                                                                                        \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetTypeInfoCount)(T * This, UINT * count);                                              \
    HRESULT(STDMETHODCALLTYPE *GetTypeInfo)(T * This, UINT index, LCID lcid, ITypeInfo * *info);                       \
    HRESULT(STDMETHODCALLTYPE *GetIDsOfNames)(T * This, REFIID riid, LPOLESTR * names, UINT count, LCID lcid,          \
                                              DISPID * ids);                                                           \
    HRESULT(STDMETHODCALLTYPE *Invoke)(T * This, DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS * params,   \
                                       VARIANT * result, EXCEPINFO * excep, UINT * arg_err);                           \
    /* IWebBrowser */                                                                                                  \
    HRESULT(STDMETHODCALLTYPE *GoBack)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *GoForward)(T * This);                                                                   \
    HRESULT(STDMETHODCALLTYPE *GoHome)(T * This);                                                                      \
    HRESULT(STDMETHODCALLTYPE *GoSearch)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *Navigate)(T * This, BSTR url, VARIANT * flags, VARIANT * target, VARIANT * post,        \
                                         VARIANT * headers);                                                           \
    HRESULT(STDMETHODCALLTYPE *Refresh)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *Refresh2)(T * This, VARIANT * level);                                                   \
    HRESULT(STDMETHODCALLTYPE *Stop)(T * This);                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Application)(T * This, IDispatch * *value);                                         \
    HRESULT(STDMETHODCALLTYPE *get_Parent)(T * This, IDispatch * *value);                                              \
    HRESULT(STDMETHODCALLTYPE *get_Container)(T * This, IDispatch * *value);                                           \
    HRESULT(STDMETHODCALLTYPE *get_Document)(T * This, IDispatch * *value);                                            \
    HRESULT(STDMETHODCALLTYPE *get_TopLevelContainer)(T * This, VARIANT_BOOL * value);                                 \
    HRESULT(STDMETHODCALLTYPE *get_Type)(T * This, BSTR * value);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_Left)(T * This, LONG * value);                                                      \
    HRESULT(STDMETHODCALLTYPE *put_Left)(T * This, LONG value);                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Top)(T * This, LONG * value);                                                       \
    HRESULT(STDMETHODCALLTYPE *put_Top)(T * This, LONG value);                                                         \
    HRESULT(STDMETHODCALLTYPE *get_Width)(T * This, LONG * value);                                                     \
    HRESULT(STDMETHODCALLTYPE *put_Width)(T * This, LONG value);                                                       \
    HRESULT(STDMETHODCALLTYPE *get_Height)(T * This, LONG * value);                                                    \
    HRESULT(STDMETHODCALLTYPE *put_Height)(T * This, LONG value);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_LocationName)(T * This, BSTR * value);                                              \
    HRESULT(STDMETHODCALLTYPE *get_LocationURL)(T * This, BSTR * value);                                               \
    HRESULT(STDMETHODCALLTYPE *get_Busy)(T * This, VARIANT_BOOL * value);                                              \
    /* IWebBrowserApp */                                                                                               \
    HRESULT(STDMETHODCALLTYPE *Quit)(T * This);                                                                        \
    HRESULT(STDMETHODCALLTYPE *ClientToWindow)(T * This, int *cx, int *cy);                                            \
    HRESULT(STDMETHODCALLTYPE *PutProperty)(T * This, BSTR name, VARIANT value);                                       \
    HRESULT(STDMETHODCALLTYPE *GetProperty)(T * This, BSTR name, VARIANT * value);                                     \
    HRESULT(STDMETHODCALLTYPE *get_Name)(T * This, BSTR * value);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_HWND)(T * This, LONG_PTR * value);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_FullName)(T * This, BSTR * value);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_Path)(T * This, BSTR * value);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_Visible)(T * This, VARIANT_BOOL * value);                                           \
    HRESULT(STDMETHODCALLTYPE *put_Visible)(T * This, VARIANT_BOOL value);                                             \
    HRESULT(STDMETHODCALLTYPE *get_StatusBar)(T * This, VARIANT_BOOL * value);                                         \
    HRESULT(STDMETHODCALLTYPE *put_StatusBar)(T * This, VARIANT_BOOL value);                                           \
    HRESULT(STDMETHODCALLTYPE *get_StatusText)(T * This, BSTR * value);                                                \
    HRESULT(STDMETHODCALLTYPE *put_StatusText)(T * This, BSTR value);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_ToolBar)(T * This, int *value);                                                     \
    HRESULT(STDMETHODCALLTYPE *put_ToolBar)(T * This, int value);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_MenuBar)(T * This, VARIANT_BOOL * value);                                           \
    HRESULT(STDMETHODCALLTYPE *put_MenuBar)(T * This, VARIANT_BOOL value);                                             \
    HRESULT(STDMETHODCALLTYPE *get_FullScreen)(T * This, VARIANT_BOOL * value);                                        \
    HRESULT(STDMETHODCALLTYPE *put_FullScreen)(T * This, VARIANT_BOOL value);                                          \
    /* IWebBrowser2 */                                                                                                 \
    HRESULT(STDMETHODCALLTYPE *Navigate2)(T * This, VARIANT * url, VARIANT * flags, VARIANT * target, VARIANT * post,  \
                                          VARIANT * headers);                                                          \
    HRESULT(STDMETHODCALLTYPE *QueryStatusWB)(T * This, DWORD cmd, DWORD * status);                                    \
    HRESULT(STDMETHODCALLTYPE *ExecWB)(T * This, DWORD cmd, DWORD options, VARIANT * in, VARIANT * out);               \
    HRESULT(STDMETHODCALLTYPE *ShowBrowserBar)(T * This, VARIANT * clsid, VARIANT * show, VARIANT * size);             \
    HRESULT(STDMETHODCALLTYPE *get_ReadyState)(T * This, LONG * value);                                                \
    HRESULT(STDMETHODCALLTYPE *get_Offline)(T * This, VARIANT_BOOL * value);                                           \
    HRESULT(STDMETHODCALLTYPE *put_Offline)(T * This, VARIANT_BOOL value);                                             \
    HRESULT(STDMETHODCALLTYPE *get_Silent)(T * This, VARIANT_BOOL * value);                                            \
    HRESULT(STDMETHODCALLTYPE *put_Silent)(T * This, VARIANT_BOOL value);                                              \
    HRESULT(STDMETHODCALLTYPE *get_RegisterAsBrowser)(T * This, VARIANT_BOOL * value);                                 \
    HRESULT(STDMETHODCALLTYPE *put_RegisterAsBrowser)(T * This, VARIANT_BOOL value);                                   \
    HRESULT(STDMETHODCALLTYPE *get_RegisterAsDropTarget)(T * This, VARIANT_BOOL * value);                              \
    HRESULT(STDMETHODCALLTYPE *put_RegisterAsDropTarget)(T * This, VARIANT_BOOL value);                                \
    HRESULT(STDMETHODCALLTYPE *get_TheaterMode)(T * This, VARIANT_BOOL * value);                                       \
    HRESULT(STDMETHODCALLTYPE *put_TheaterMode)(T * This, VARIANT_BOOL value);                                         \
    HRESULT(STDMETHODCALLTYPE *get_AddressBar)(T * This, VARIANT_BOOL * value);                                        \
    HRESULT(STDMETHODCALLTYPE *put_AddressBar)(T * This, VARIANT_BOOL value);                                          \
    HRESULT(STDMETHODCALLTYPE *get_Resizable)(T * This, VARIANT_BOOL * value);                                         \
    HRESULT(STDMETHODCALLTYPE *put_Resizable)(T * This, VARIANT_BOOL value);
MIME_COM_INTERFACE(IWebBrowser2)

/* the ids of the events of DWebBrowserEvents2. */
enum {
    DISPID_STATUSTEXTCHANGE = 102,
    DISPID_DOWNLOADCOMPLETE = 104,
    DISPID_COMMANDSTATECHANGE = 105,
    DISPID_DOWNLOADBEGIN = 106,
    DISPID_PROGRESSCHANGE = 108,
    DISPID_TITLECHANGE = 113,
    DISPID_BEFORENAVIGATE2 = 250,
    DISPID_NEWWINDOW2 = 251,
    DISPID_NAVIGATECOMPLETE2 = 252,
    DISPID_DOCUMENTCOMPLETE = 259,
    DISPID_NAVIGATEERROR = 271,
    DISPID_NEWWINDOW3 = 273,
};

enum { READYSTATE_UNINITIALIZED, READYSTATE_LOADING, READYSTATE_LOADED, READYSTATE_INTERACTIVE, READYSTATE_COMPLETE };


MIME_DEFINE_GUID(IID_IWebBrowser, 0xEAB22AC1, 0x30C1, 0x11CF, 0xA7, 0xEB, 0x00, 0x00, 0xC0, 0x5B, 0xAE, 0x0B);
MIME_DEFINE_GUID(IID_IWebBrowserApp, 0x0002DF05, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IWebBrowser2, 0xD30C1661, 0xCDAF, 0x11D0, 0x8A, 0x3E, 0x00, 0xC0, 0x4F, 0xC9, 0xE2, 0x6E);
MIME_DEFINE_GUID(DIID_DWebBrowserEvents2, 0x34A715A0, 0x6587, 0x11D0, 0x92, 0x4A, 0x00, 0x20, 0xAF, 0xC7, 0xAC, 0x4D);
MIME_DEFINE_GUID(DIID_DWebBrowserEvents, 0xEAB22AC2, 0x30C1, 0x11CF, 0xA7, 0xEB, 0x00, 0x00, 0xC0, 0x5B, 0xAE, 0x0B);
MIME_DEFINE_GUID(CLSID_WebBrowser, 0x8856F961, 0x340A, 0x11D0, 0xA9, 0x6B, 0x00, 0xC0, 0x4F, 0xD7, 0x05, 0xA2);
MIME_DEFINE_GUID(CLSID_WebBrowser_V1, 0xEAB22AC3, 0x30C1, 0x11CF, 0xA7, 0xEB, 0x00, 0x00, 0xC0, 0x5B, 0xAE, 0x0B);

#endif
