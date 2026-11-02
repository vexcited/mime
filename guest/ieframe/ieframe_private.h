#ifndef MIME_IEFRAME_PRIVATE_H
#define MIME_IEFRAME_PRIVATE_H

#include <mime/win32/exdisp.h>
#include <mime/win32/shldisp.h>
#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>

#define IF_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8)                                                       \
    static const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}

typedef struct BROWSER BROWSER;

/* the browser's side of the WebView2 runtime, in host.c. */
typedef struct HOST HOST;
HOST *HostCreate(BROWSER *browser, HWND parent);
void HostDestroy(HOST *host);
void HostResize(HOST *host, int width, int height);
BOOL HostNavigate(HOST *host, LPCWSTR url);
BOOL HostNavigateHtml(HOST *host, LPCWSTR html);
/* what HostCommand does, the slot of the page in the runtime. */
enum { HOST_RELOAD = 31, HOST_GO_BACK = 40, HOST_GO_FORWARD = 41, HOST_STOP = 43 };
void HostCommand(HOST *host, int command);
void HostSetFocus(HOST *host);

/* what the host reports to the browser, in browser.c. */
void BrowserNavigationStarting(BROWSER *browser, LPCWSTR url, BOOL *cancel);
void BrowserNavigationCompleted(BROWSER *browser, LPCWSTR url, BOOL success);
void BrowserTitleChanged(BROWSER *browser, LPCWSTR title);
void BrowserHostReady(BROWSER *browser);

void BrowserAddRef(BROWSER *browser);
void BrowserRelease(BROWSER *browser);
HRESULT BrowserShowHtml(BROWSER *browser, LPCWSTR html);

/* the document a program writes a page into, in document.c. */
IDispatch *DocumentCreate(BROWSER *browser);

IClassFactory *BrowserFactory(void);
extern HINSTANCE g_ieframe;

#endif
