/*
 * win32 base definitions shared by every user mode module: handle types,
 * geometry and the small helper macros of windef.h and minwindef.h.
 */
#ifndef MIME_WIN32_WINDEF_H
#define MIME_WIN32_WINDEF_H

#include <mime/nt.h>

#define WINUSERAPI DECLSPEC_IMPORT
#define WINGDIAPI DECLSPEC_IMPORT
#define WINBASEAPI DECLSPEC_IMPORT

#define DECLARE_HANDLE(name)                                                                                      \
    struct name##__ {                                                                                             \
        int unused;                                                                                               \
    };                                                                                                            \
    typedef struct name##__ *name

DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HHOOK);
DECLARE_HANDLE(HACCEL);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HCOLORSPACE);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HGLRC);
DECLARE_HANDLE(HDESK);
DECLARE_HANDLE(HENHMETAFILE);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HWINEVENTHOOK);
DECLARE_HANDLE(HMONITOR);
DECLARE_HANDLE(HUMPD);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HRSRC);
DECLARE_HANDLE(HSPRITE);
DECLARE_HANDLE(HLSURF);
DECLARE_HANDLE(HSTR);
DECLARE_HANDLE(HTASK);
DECLARE_HANDLE(HWINSTA);
DECLARE_HANDLE(HKL);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
typedef HICON HCURSOR;
typedef HANDLE HGDIOBJ;
typedef HANDLE HGLOBAL;
typedef HANDLE HLOCAL;
typedef HANDLE GLOBALHANDLE;
typedef HANDLE LOCALHANDLE;
typedef HANDLE HMETAFILE;
typedef int HFILE;
typedef DWORD COLORREF, *LPCOLORREF;
typedef WORD *PATOM;
typedef BYTE *LPBYTE;
typedef float *PFLOAT;
typedef ULONG_PTR HCRYPTPROV;

typedef INT_PTR(WINAPI *FARPROC)(void);
typedef INT_PTR(WINAPI *NEARPROC)(void);
typedef INT_PTR(WINAPI *PROC)(void);

#define MAX_COMPUTERNAME_LENGTH 15
#define HFILE_ERROR ((HFILE)-1)

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *PRECT, *NPRECT, *LPRECT;
typedef const RECT *LPCRECT;

typedef struct _RECTL {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECTL, *PRECTL, *LPRECTL;
typedef const RECTL *LPCRECTL;

typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT, *PPOINT, *NPPOINT, *LPPOINT;

typedef struct _POINTL {
    LONG x;
    LONG y;
} POINTL, *PPOINTL;

typedef struct tagSIZE {
    LONG cx;
    LONG cy;
} SIZE, *PSIZE, *LPSIZE, SIZEL, *PSIZEL, *LPSIZEL;

typedef struct tagPOINTS {
    SHORT x;
    SHORT y;
} POINTS, *PPOINTS, *LPPOINTS;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

#define MAKEWORD(a, b) ((WORD)(((BYTE)((DWORD_PTR)(a) & 0xff)) | ((WORD)((BYTE)((DWORD_PTR)(b) & 0xff))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)((DWORD_PTR)(a) & 0xffff)) | ((DWORD)((WORD)((DWORD_PTR)(b) & 0xffff))) << 16))
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l) ((WORD)((DWORD_PTR)(l) >> 16))
#define LOBYTE(w) ((BYTE)((DWORD_PTR)(w) & 0xff))
#define HIBYTE(w) ((BYTE)((DWORD_PTR)(w) >> 8))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define MAKEWPARAM(l, h) ((WPARAM)(DWORD)MAKELONG(l, h))
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)MAKELONG(l, h))
#define MAKELRESULT(l, h) ((LRESULT)(DWORD)MAKELONG(l, h))

#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(rgb) (LOBYTE(rgb))
#define GetGValue(rgb) (LOBYTE(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) (LOBYTE((rgb) >> 16))

#define IS_INTRESOURCE(r) ((((ULONG_PTR)(r)) >> 16) == 0)
#define MAKEINTRESOURCEA(i) ((LPSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#define MAKEINTATOM(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

#endif
