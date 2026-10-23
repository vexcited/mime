/* uxtheme internal interface. */
#ifndef MIME_UXTHEME_PRIVATE_H
#define MIME_UXTHEME_PRIVATE_H

#define MIME_BUILD_UXTHEME
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/uxtheme.h>
#include <mime/win32/vssym32.h>

#include "../lib/msstyles/msstyles.h"

/* E_PROP_ID_UNSUPPORTED, returned by every getter for a missing property. */
#define UX_E_NOTFOUND ((HRESULT)0x80070490L)

#ifndef PRF_CLIENT
#define PRF_CHECKVISIBLE 0x00000001L
#define PRF_NONCLIENT 0x00000002L
#define PRF_CLIENT 0x00000004L
#define PRF_ERASEBKGND 0x00000008L
#define PRF_CHILDREN 0x00000010L
#endif

/* exported by user32 and gdi32 but missing from the shared headers. */
WINGDIAPI HRGN WINAPI ExtCreateRegion(const XFORM *xform, DWORD count, const RGNDATA *data);
WINUSERAPI BOOL WINAPI SetPropW(HWND hwnd, LPCWSTR name, HANDLE data);
WINUSERAPI HANDLE WINAPI GetPropW(HWND hwnd, LPCWSTR name);
WINUSERAPI HANDLE WINAPI RemovePropW(HWND hwnd, LPCWSTR name);
WINUSERAPI HANDLE WINAPI CopyImage(HANDLE image, UINT type, int width, int height, UINT flags);

static inline void *UxAlloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, size ? size : 1);
}

static inline void UxFree(void *p)
{
    if (p)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
}

/* one class of the loaded visual style at one dpi. */
typedef struct UX_THEME {
    int Class;
    UINT Dpi;
    HWND Window;
} UX_THEME;

extern MimeMsstyles UxStyles;
extern const MimeAllocator UxAllocator;

/* false when no visual style is available. */
BOOL UxThemeLoaded(void);

/* NULL with *hr = E_HANDLE when stale. release with UxRelease. */
UX_THEME *UxAcquire(HTHEME handle, HRESULT *hr);
void UxRelease(UX_THEME *theme);

/* a top down 32 bpp premultiplied dib section. */
typedef struct UX_BITMAP {
    struct UX_BITMAP *Next;
    const void *Data;
    COLORREF Key; /* made transparent, CLR_INVALID for none. */
    HBITMAP Bitmap;
    DWORD *Bits;
    int Width, Height;
} UX_BITMAP;

UX_BITMAP *UxGetBitmap(const MimeMsstylesImage *image);

/* false for border fill and parts without an image. */
BOOL UxPartImage(UX_THEME *theme, int part, int state, const RECT *dest, MimeMsstylesImage *image);
int UxBackgroundType(UX_THEME *theme, int part, int state);

static inline int UxScale(UX_THEME *theme, int value)
{
    return theme->Dpi == 96 ? value : MulDiv(value, (int)theme->Dpi, 96);
}

#endif
