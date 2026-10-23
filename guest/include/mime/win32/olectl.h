/* oleaut32.dll standard fonts and pictures, their dispatch interface and the picture loading functions. */
#ifndef MIME_WIN32_OLECTL_H
#define MIME_WIN32_OLECTL_H

#include <mime/win32/oleauto.h>
#include <mime/win32/objidl.h>
#include <mime/win32/wingdi.h>

#ifndef WINOLEAUTAPI
#define WINOLEAUTAPI DECLSPEC_IMPORT
#endif

/* ------------------------------------------------------------------ errors */

#ifndef DISP_E_UNKNOWNINTERFACE
#define DISP_E_UNKNOWNINTERFACE ((HRESULT)0x80020001L)
#endif
#ifndef DISP_E_MEMBERNOTFOUND
#define DISP_E_MEMBERNOTFOUND ((HRESULT)0x80020003L)
#endif
#ifndef DISP_E_PARAMNOTFOUND
#define DISP_E_PARAMNOTFOUND ((HRESULT)0x80020004L)
#endif
#ifndef DISP_E_TYPEMISMATCH
#define DISP_E_TYPEMISMATCH ((HRESULT)0x80020005L)
#endif
#ifndef DISP_E_UNKNOWNNAME
#define DISP_E_UNKNOWNNAME ((HRESULT)0x80020006L)
#endif
#ifndef DISP_E_EXCEPTION
#define DISP_E_EXCEPTION ((HRESULT)0x80020009L)
#endif
#ifndef DISP_E_BADPARAMCOUNT
#define DISP_E_BADPARAMCOUNT ((HRESULT)0x8002000EL)
#endif
#ifndef DISP_E_PARAMNOTOPTIONAL
#define DISP_E_PARAMNOTOPTIONAL ((HRESULT)0x8002000FL)
#endif
#ifndef DISP_E_DIVBYZERO
#define DISP_E_DIVBYZERO ((HRESULT)0x80020012L)
#endif
#ifndef TYPE_E_LIBNOTREGISTERED
#define TYPE_E_LIBNOTREGISTERED ((HRESULT)0x8002801DL)
#endif
#ifndef CTL_E_ILLEGALFUNCTIONCALL
#define CTL_E_ILLEGALFUNCTIONCALL ((HRESULT)0x800A0005L)
#endif
#ifndef CTL_E_OVERFLOW
#define CTL_E_OVERFLOW ((HRESULT)0x800A0006L)
#endif
#ifndef CTL_E_FILENOTFOUND
#define CTL_E_FILENOTFOUND ((HRESULT)0x800A0035L)
#endif
#ifndef CTL_E_INVALIDPROPERTYVALUE
#define CTL_E_INVALIDPROPERTYVALUE ((HRESULT)0x800A017CL)
#endif
#ifndef CTL_E_INVALIDPICTURE
#define CTL_E_INVALIDPICTURE ((HRESULT)0x800A01E1L)
#endif
#ifndef INET_E_RESOURCE_NOT_FOUND
#define INET_E_RESOURCE_NOT_FOUND ((HRESULT)0x800C0005L)
#endif

/* ------------------------------------------------------------------ IDispatch */

/* IDispatch and its structures come from oleauto.h. */
typedef IDispatch *LPDISPATCH;
typedef IStream *LPSTREAM;

/* ------------------------------------------------------------------ fonts */

typedef struct IFont IFont, *LPFONT;
typedef IDispatch IFontDisp, *LPFONTDISP;

typedef struct tagFONTDESC {
    UINT cbSizeofstruct;
    LPOLESTR lpstrName;
    CY cySize;
    SHORT sWeight;
    SHORT sCharset;
    BOOL fItalic;
    BOOL fUnderline;
    BOOL fStrikethrough;
} FONTDESC, *LPFONTDESC;

#define IFont_METHODS(T)                                                                                               \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *get_Name)(T * This, BSTR * name);                                                       \
    HRESULT(STDMETHODCALLTYPE *put_Name)(T * This, BSTR name);                                                         \
    HRESULT(STDMETHODCALLTYPE *get_Size)(T * This, CY * size);                                                         \
    HRESULT(STDMETHODCALLTYPE *put_Size)(T * This, CY size);                                                           \
    HRESULT(STDMETHODCALLTYPE *get_Bold)(T * This, BOOL * bold);                                                       \
    HRESULT(STDMETHODCALLTYPE *put_Bold)(T * This, BOOL bold);                                                         \
    HRESULT(STDMETHODCALLTYPE *get_Italic)(T * This, BOOL * italic);                                                   \
    HRESULT(STDMETHODCALLTYPE *put_Italic)(T * This, BOOL italic);                                                     \
    HRESULT(STDMETHODCALLTYPE *get_Underline)(T * This, BOOL * underline);                                             \
    HRESULT(STDMETHODCALLTYPE *put_Underline)(T * This, BOOL underline);                                               \
    HRESULT(STDMETHODCALLTYPE *get_Strikethrough)(T * This, BOOL * strikethrough);                                     \
    HRESULT(STDMETHODCALLTYPE *put_Strikethrough)(T * This, BOOL strikethrough);                                       \
    HRESULT(STDMETHODCALLTYPE *get_Weight)(T * This, SHORT * weight);                                                  \
    HRESULT(STDMETHODCALLTYPE *put_Weight)(T * This, SHORT weight);                                                    \
    HRESULT(STDMETHODCALLTYPE *get_Charset)(T * This, SHORT * charset);                                                \
    HRESULT(STDMETHODCALLTYPE *put_Charset)(T * This, SHORT charset);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_hFont)(T * This, HFONT * font);                                                     \
    HRESULT(STDMETHODCALLTYPE *Clone)(T * This, IFont * *font);                                                        \
    HRESULT(STDMETHODCALLTYPE *IsEqual)(T * This, IFont * other);                                                      \
    HRESULT(STDMETHODCALLTYPE *SetRatio)(T * This, LONG logical, LONG himetric);                                       \
    HRESULT(STDMETHODCALLTYPE *QueryTextMetrics)(T * This, TEXTMETRICW * metrics);                                     \
    HRESULT(STDMETHODCALLTYPE *AddRefHfont)(T * This, HFONT font);                                                     \
    HRESULT(STDMETHODCALLTYPE *ReleaseHfont)(T * This, HFONT font);                                                    \
    HRESULT(STDMETHODCALLTYPE *SetHdc)(T * This, HDC hdc);
MIME_COM_INTERFACE(IFont)

MIME_DEFINE_GUID(IID_IFont, 0xBEF6E002, 0xA874, 0x101A, 0x8B, 0xBA, 0x00, 0xAA, 0x00, 0x30, 0x0C, 0xAB);
MIME_DEFINE_GUID(IID_IFontDisp, 0xBEF6E003, 0xA874, 0x101A, 0x8B, 0xBA, 0x00, 0xAA, 0x00, 0x30, 0x0C, 0xAB);
MIME_DEFINE_GUID(CLSID_StdFont, 0x0BE35203, 0x8F91, 0x11CE, 0x9D, 0xE3, 0x00, 0xAA, 0x00, 0x4B, 0xB8, 0x51);
MIME_DEFINE_GUID(CLSID_OldFont, 0x46763EE0, 0xCAB2, 0x11CE, 0x8C, 0x20, 0x00, 0xAA, 0x00, 0x51, 0xE5, 0xD4);

/* font property dispatch ids. */
#define DISPID_FONT_NAME 0
#define DISPID_FONT_SIZE 2
#define DISPID_FONT_BOLD 3
#define DISPID_FONT_ITALIC 4
#define DISPID_FONT_UNDER 5
#define DISPID_FONT_STRIKE 6
#define DISPID_FONT_WEIGHT 7
#define DISPID_FONT_CHARSET 8

/* ------------------------------------------------------------------ pictures */

typedef DWORD OLE_COLOR;
typedef UINT OLE_HANDLE;
typedef UINT_PTR HHANDLE;
typedef LONG OLE_XPOS_HIMETRIC, OLE_YPOS_HIMETRIC, OLE_XSIZE_HIMETRIC, OLE_YSIZE_HIMETRIC;

#define PICTYPE_UNINITIALIZED (-1)
#define PICTYPE_NONE 0
#define PICTYPE_BITMAP 1
#define PICTYPE_METAFILE 2
#define PICTYPE_ICON 3
#define PICTYPE_ENHMETAFILE 4

#define PICTURE_SCALABLE 0x1
#define PICTURE_TRANSPARENT 0x2

#define LP_DEFAULT 0
#define LP_MONOCHROME 1
#define LP_VGACOLOR 2
#define LP_COLOR 4

typedef struct tagPICTDESC {
    UINT cbSizeofstruct;
    UINT picType;
    union {
        struct {
            HBITMAP hbitmap;
            HPALETTE hpal;
        } bmp;
        struct {
            HMETAFILE hmeta;
            int xExt;
            int yExt;
        } wmf;
        struct {
            HICON hicon;
        } icon;
        struct {
            HENHMETAFILE hemf;
        } emf;
    };
} PICTDESC, *LPPICTDESC;

typedef struct IPicture IPicture, *LPPICTURE;
typedef IDispatch IPictureDisp, *LPPICTUREDISP;
typedef struct IPicture2 IPicture2;

#define IPicture_METHODS(T)                                                                                            \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *get_Handle)(T * This, OLE_HANDLE * handle);                                             \
    HRESULT(STDMETHODCALLTYPE *get_hPal)(T * This, OLE_HANDLE * palette);                                              \
    HRESULT(STDMETHODCALLTYPE *get_Type)(T * This, SHORT * type);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_Width)(T * This, OLE_XSIZE_HIMETRIC * width);                                       \
    HRESULT(STDMETHODCALLTYPE *get_Height)(T * This, OLE_YSIZE_HIMETRIC * height);                                     \
    HRESULT(STDMETHODCALLTYPE *Render)(T * This, HDC hdc, LONG x, LONG y, LONG cx, LONG cy, OLE_XPOS_HIMETRIC xsrc,    \
                                       OLE_YPOS_HIMETRIC ysrc, OLE_XSIZE_HIMETRIC cxsrc, OLE_YSIZE_HIMETRIC cysrc,     \
                                       const RECT *bounds);                                                            \
    HRESULT(STDMETHODCALLTYPE *set_hPal)(T * This, OLE_HANDLE palette);                                                \
    HRESULT(STDMETHODCALLTYPE *get_CurDC)(T * This, HDC * hdc);                                                        \
    HRESULT(STDMETHODCALLTYPE *SelectPicture)(T * This, HDC in, HDC * out, OLE_HANDLE * bitmap);                       \
    HRESULT(STDMETHODCALLTYPE *get_KeepOriginalFormat)(T * This, BOOL * keep);                                         \
    HRESULT(STDMETHODCALLTYPE *put_KeepOriginalFormat)(T * This, BOOL keep);                                           \
    HRESULT(STDMETHODCALLTYPE *PictureChanged)(T * This);                                                              \
    HRESULT(STDMETHODCALLTYPE *SaveAsFile)(T * This, IStream * stream, BOOL save_copy, LONG * size);                   \
    HRESULT(STDMETHODCALLTYPE *get_Attributes)(T * This, DWORD * attributes);
MIME_COM_INTERFACE(IPicture)

/* same, with pointer-sized handles. */
#define IPicture2_METHODS(T)                                                                                           \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *get_Handle)(T * This, HHANDLE * handle);                                                \
    HRESULT(STDMETHODCALLTYPE *get_hPal)(T * This, HHANDLE * palette);                                                 \
    HRESULT(STDMETHODCALLTYPE *get_Type)(T * This, SHORT * type);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_Width)(T * This, OLE_XSIZE_HIMETRIC * width);                                       \
    HRESULT(STDMETHODCALLTYPE *get_Height)(T * This, OLE_YSIZE_HIMETRIC * height);                                     \
    HRESULT(STDMETHODCALLTYPE *Render)(T * This, HDC hdc, LONG x, LONG y, LONG cx, LONG cy, OLE_XPOS_HIMETRIC xsrc,    \
                                       OLE_YPOS_HIMETRIC ysrc, OLE_XSIZE_HIMETRIC cxsrc, OLE_YSIZE_HIMETRIC cysrc,     \
                                       const RECT *bounds);                                                            \
    HRESULT(STDMETHODCALLTYPE *set_hPal)(T * This, HHANDLE palette);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_CurDC)(T * This, HDC * hdc);                                                        \
    HRESULT(STDMETHODCALLTYPE *SelectPicture)(T * This, HDC in, HDC * out, HHANDLE * bitmap);                          \
    HRESULT(STDMETHODCALLTYPE *get_KeepOriginalFormat)(T * This, BOOL * keep);                                         \
    HRESULT(STDMETHODCALLTYPE *put_KeepOriginalFormat)(T * This, BOOL keep);                                           \
    HRESULT(STDMETHODCALLTYPE *PictureChanged)(T * This);                                                              \
    HRESULT(STDMETHODCALLTYPE *SaveAsFile)(T * This, IStream * stream, BOOL save_copy, LONG * size);                   \
    HRESULT(STDMETHODCALLTYPE *get_Attributes)(T * This, DWORD * attributes);
MIME_COM_INTERFACE(IPicture2)

MIME_DEFINE_GUID(IID_IPicture, 0x7BF80980, 0xBF32, 0x101A, 0x8B, 0xBB, 0x00, 0xAA, 0x00, 0x30, 0x0C, 0xAB);
MIME_DEFINE_GUID(IID_IPictureDisp, 0x7BF80981, 0xBF32, 0x101A, 0x8B, 0xBB, 0x00, 0xAA, 0x00, 0x30, 0x0C, 0xAB);
MIME_DEFINE_GUID(IID_IPicture2, 0xF5185DD8, 0x2012, 0x4B0B, 0xAA, 0xD9, 0xF0, 0x52, 0xC6, 0xBD, 0x48, 0x2B);
MIME_DEFINE_GUID(CLSID_StdPicture, 0x0BE35204, 0x8F91, 0x11CE, 0x9D, 0xE3, 0x00, 0xAA, 0x00, 0x4B, 0xB8, 0x51);

/* dispatch ids of the picture properties and Render. */
#define DISPID_PICT_HANDLE 0
#define DISPID_PICT_HPAL 2
#define DISPID_PICT_TYPE 3
#define DISPID_PICT_WIDTH 4
#define DISPID_PICT_HEIGHT 5
#define DISPID_PICT_RENDER 6
#define DISPID_PICT_HANDLE64 7
#define DISPID_PICT_HPAL64 8
#define DISPID_PICT_RENDER64 9

/* ------------------------------------------------------------------ property frames */

typedef struct tagOCPFIPARAMS {
    ULONG cbStructSize;
    HWND hWndOwner;
    int x;
    int y;
    LPCOLESTR lpszCaption;
    ULONG cObjects;
    LPUNKNOWN *lplpUnk;
    ULONG cPages;
    CLSID *lpPages;
    LCID lcid;
    DISPID dispidInitialProperty;
} OCPFIPARAMS, *LPOCPFIPARAMS;

/* ------------------------------------------------------------------ functions */

WINOLEAUTAPI HRESULT WINAPI OleCreateFontIndirect(LPFONTDESC desc, REFIID riid, LPVOID *object);
WINOLEAUTAPI HRESULT WINAPI OleCreatePictureIndirect(LPPICTDESC desc, REFIID riid, BOOL own, LPVOID *object);
WINOLEAUTAPI HRESULT WINAPI OleLoadPicture(LPSTREAM stream, LONG size, BOOL run_mode, REFIID riid, LPVOID *object);
WINOLEAUTAPI HRESULT WINAPI OleLoadPictureEx(LPSTREAM stream, LONG size, BOOL run_mode, REFIID riid,
                                             DWORD cx_desired, DWORD cy_desired, DWORD flags, LPVOID *object);
WINOLEAUTAPI HRESULT WINAPI OleLoadPicturePath(LPOLESTR path, LPUNKNOWN caller, DWORD reserved, OLE_COLOR color,
                                               REFIID riid, LPVOID *object);
WINOLEAUTAPI HRESULT WINAPI OleLoadPictureFile(VARIANT file, LPDISPATCH *picture);
WINOLEAUTAPI HRESULT WINAPI OleLoadPictureFileEx(VARIANT file, DWORD cx_desired, DWORD cy_desired, DWORD flags,
                                                 LPDISPATCH *picture);
WINOLEAUTAPI HRESULT WINAPI OleSavePictureFile(LPDISPATCH picture, BSTR file);
WINOLEAUTAPI HRESULT WINAPI OleTranslateColor(OLE_COLOR color, HPALETTE palette, COLORREF *result);
WINOLEAUTAPI HCURSOR WINAPI OleIconToCursor(HINSTANCE instance, HICON icon);
WINOLEAUTAPI HRESULT WINAPI OleCreatePropertyFrame(HWND owner, UINT x, UINT y, LPCOLESTR caption, ULONG count,
                                                   LPUNKNOWN *objects, ULONG pages, LPCLSID page_ids, LCID lcid,
                                                   DWORD reserved1, LPVOID reserved2);
WINOLEAUTAPI HRESULT WINAPI OleCreatePropertyFrameIndirect(LPOCPFIPARAMS params);

#endif
