/*
 * printer dcs. a printer dc is a memory dc whose surface is the page at printer resolution, sent to the printer
 * when the page ends: gdi32 acts as a raster printer driver and winspool's print processor builds the document.
 *
 * winspool.drv is loaded on the first printer dc, gdi32 does not import it.
 */
#include "gdi32_private.h"
#include <mime/printing.h>
#include <mime/stub.h>
#include <stddef.h>

typedef struct MIME_PRINT_DEVICE {
    WCHAR Name[64];
    int XDpi, YDpi;
    /* the page in pixels, and its printable part that GetDeviceCaps calls HORZRES and VERTRES. */
    int PageWidth, PageHeight;
    int WidthPx, HeightPx;
    int OffsetX, OffsetY;
    int PageWidthMm10, PageHeightMm10;
    BOOL Color;
} MIME_PRINT_DEVICE;

typedef struct PRN_DC {
    struct PRN_DC *Next;
    HDC Dc;
    BOOL Info;
    HANDLE Printer;
    MIME_PRINT_DEVICE Device;
    HBITMAP Bitmap;
    HGDIOBJ OldBitmap;
    BYTE *Bits;
    BOOL InDoc, InPage;
    ABORTPROC AbortProc;
    DWORD Job;
    int Pages;
} PRN_DC;

volatile LONG PrnDcCount;
static RTL_SRWLOCK g_lock;
static PRN_DC *g_dcs;

typedef BOOL(WINAPI *OPEN_PRINTER)(LPWSTR, LPHANDLE, LPVOID);
typedef BOOL(WINAPI *CLOSE_PRINTER)(HANDLE);
typedef DWORD(WINAPI *START_DOC_PRINTER)(HANDLE, DWORD, LPBYTE);
typedef BOOL(WINAPI *PRINTER_CALL)(HANDLE);
typedef BOOL(WINAPI *WRITE_PRINTER)(HANDLE, LPVOID, DWORD, LPDWORD);
typedef LONG(WINAPI *DOCUMENT_PROPERTIES)(HWND, HANDLE, LPWSTR, PDEVMODEW, PDEVMODEW, DWORD);
typedef int(WINAPI *DEVICE_CAPABILITIES)(LPCWSTR, LPCWSTR, WORD, LPWSTR, const DEVMODEW *);

static struct {
    BOOL Tried;
    OPEN_PRINTER Open;
    CLOSE_PRINTER Close;
    START_DOC_PRINTER StartDoc;
    PRINTER_CALL StartPage, EndPage, EndDoc, Abort;
    WRITE_PRINTER Write;
    DOCUMENT_PROPERTIES Properties;
    DEVICE_CAPABILITIES Capabilities;
} g_spool;

/* winspool may be missing from the system. */
static BOOL spool_load(void)
{
    if (!g_spool.Tried) {
        HMODULE module = LoadLibraryW(L"winspool.drv");
        if (module) {
            g_spool.Open = (OPEN_PRINTER)GetProcAddress(module, "OpenPrinterW");
            g_spool.Close = (CLOSE_PRINTER)GetProcAddress(module, "ClosePrinter");
            g_spool.StartDoc = (START_DOC_PRINTER)GetProcAddress(module, "StartDocPrinterW");
            g_spool.StartPage = (PRINTER_CALL)GetProcAddress(module, "StartPagePrinter");
            g_spool.EndPage = (PRINTER_CALL)GetProcAddress(module, "EndPagePrinter");
            g_spool.EndDoc = (PRINTER_CALL)GetProcAddress(module, "EndDocPrinter");
            g_spool.Abort = (PRINTER_CALL)GetProcAddress(module, "AbortPrinter");
            g_spool.Write = (WRITE_PRINTER)GetProcAddress(module, "WritePrinter");
            g_spool.Properties = (DOCUMENT_PROPERTIES)GetProcAddress(module, "DocumentPropertiesW");
            g_spool.Capabilities = (DEVICE_CAPABILITIES)GetProcAddress(module, "DeviceCapabilitiesW");
        }
        g_spool.Tried = TRUE;
    }
    return g_spool.Open && g_spool.Close && g_spool.StartDoc && g_spool.StartPage && g_spool.EndPage &&
           g_spool.EndDoc && g_spool.Abort && g_spool.Write && g_spool.Properties && g_spool.Capabilities;
}

static PRN_DC *lookup(HDC dc)
{
    PRN_DC *found = nullptr;
    if (!PrnDcCount)
        return nullptr;
    RtlAcquireSRWLockShared(&g_lock);
    for (PRN_DC *p = g_dcs; p; p = p->Next) {
        if (p->Dc == dc) {
            found = p;
            break;
        }
    }
    RtlReleaseSRWLockShared(&g_lock);
    return found;
}

BOOL PrnIsPrinterDc(HDC dc)
{
    return lookup(dc) != nullptr;
}

/* ------------------------------------------------------------------ creating and deleting */

/* the pixels of a page are kept while a document is open. */
static void surface_free(PRN_DC *p)
{
    if (!p->Bitmap)
        return;
    if (p->Dc) {
        NtGdiSelectBitmap(p->Dc, (HBITMAP)p->OldBitmap);
    }
    NtGdiDeleteObjectApp(p->Bitmap);
    p->Bitmap = nullptr;
    p->OldBitmap = nullptr;
    p->Bits = nullptr;
}

static BOOL surface_make(PRN_DC *p)
{
    if (p->Bitmap)
        return TRUE;
    BITMAPINFO info;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = p->Device.PageWidth;
    info.bmiHeader.biHeight = -p->Device.PageHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(p->Dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap)
            NtGdiDeleteObjectApp(bitmap);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    p->Bitmap = bitmap;
    p->Bits = bits;
    p->OldBitmap = NtGdiSelectBitmap(p->Dc, bitmap);
    return TRUE;
}

static void page_clear(PRN_DC *p)
{
    memset(p->Bits, 0xFF, (SIZE_T)p->Device.PageWidth * 4 * (SIZE_T)p->Device.PageHeight);
}

/* the page size in the units the mapping modes use. */
static void set_resolution(PRN_DC *p)
{
    int mx = MulDiv(p->Device.PageWidthMm10, 1, 10), my = MulDiv(p->Device.PageHeightMm10, 1, 10);
    NtGdiSetVirtualResolution(p->Dc, p->Device.WidthPx, p->Device.HeightPx, mx ? mx : 1, my ? my : 1);
}

static const WCHAR *g_winspool = L"WINSPOOL";

/* in dots per inch. */
static int resolution_of(const DEVMODEW *dm)
{
    int x = 300;
    if ((dm->dmFields & DM_PRINTQUALITY) && dm->dmPrintQuality > 0)
        x = dm->dmPrintQuality;
    else if ((dm->dmFields & DM_PRINTQUALITY) && (dm->dmPrintQuality == DMRES_DRAFT || dm->dmPrintQuality == DMRES_LOW))
        x = 150;
    return x;
}

/*
 * the printer first completes the settings, keeping what it supports. the paper comes from its identifier or
 * its size, turned for landscape.
 */
static BOOL device_info(LPCWSTR name, HANDLE printer, const DEVMODEW *mode, MIME_PRINT_DEVICE *d)
{
    DEVMODEW dm;
    memset(&dm, 0, sizeof(dm));
    if (g_spool.Properties(nullptr, printer, (LPWSTR)name, &dm, mode ? (PDEVMODEW)mode : nullptr,
                           DM_OUT_BUFFER | (mode ? DM_IN_BUFFER : 0)) < 0)
        return FALSE;
    memset(d, 0, sizeof(*d));
    _snwprintf(d->Name, ARRAYSIZE(d->Name), L"%ls", name);
    int w10 = 2100, h10 = 2970;
    if ((dm.dmFields & DM_PAPERSIZE) && dm.dmPaperSize > 0 && dm.dmPaperSize != 256) {
        int n = g_spool.Capabilities(name, nullptr, DC_PAPERS, nullptr, nullptr);
        if (n > 0 && n <= 256) {
            WORD ids[256];
            POINT sizes[256];
            if (g_spool.Capabilities(name, nullptr, DC_PAPERS, (LPWSTR)ids, nullptr) == n &&
                g_spool.Capabilities(name, nullptr, DC_PAPERSIZE, (LPWSTR)sizes, nullptr) == n) {
                for (int i = 0; i < n; i++) {
                    if (ids[i] == (WORD)dm.dmPaperSize) {
                        w10 = sizes[i].x;
                        h10 = sizes[i].y;
                    }
                }
            }
        }
    } else if ((dm.dmFields & DM_PAPERWIDTH) && (dm.dmFields & DM_PAPERLENGTH) && dm.dmPaperWidth > 0 &&
               dm.dmPaperLength > 0) {
        w10 = dm.dmPaperWidth;
        h10 = dm.dmPaperLength;
    }
    if ((dm.dmFields & DM_ORIENTATION) && dm.dmOrientation == DMORIENT_LANDSCAPE) {
        int t = w10;
        w10 = h10;
        h10 = t;
    }
    int x = resolution_of(&dm), y = x;
    if ((dm.dmFields & DM_YRESOLUTION) && dm.dmYResolution > 0 && dm.dmPrintQuality > 0)
        y = dm.dmYResolution;
    d->XDpi = x;
    d->YDpi = y;
    d->PageWidthMm10 = w10;
    d->PageHeightMm10 = h10;
    d->PageWidth = MulDiv(w10, x, 254);
    d->PageHeight = MulDiv(h10, y, 254);
    /* the paper is the page, there are no unprintable margins. */
    d->WidthPx = d->PageWidth;
    d->HeightPx = d->PageHeight;
    d->Color = !((dm.dmFields & DM_COLOR) && dm.dmColor == DMCOLOR_MONOCHROME);
    return TRUE;
}

/* nullptr when no such printer exists. */
static HDC prn_create(LPCWSTR device, const DEVMODEW *mode, BOOL info_only)
{
    if (!spool_load()) {
        SetLastError(ERROR_INVALID_PRINTER_NAME);
        return nullptr;
    }
    PRN_DC *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p)
        return nullptr;
    WCHAR name[128];
    _snwprintf(name, ARRAYSIZE(name), L"%ls", device);
    if (!g_spool.Open(name, &p->Printer, nullptr)) {
        HeapFree(GetProcessHeap(), 0, p);
        SetLastError(ERROR_INVALID_PRINTER_NAME);
        return nullptr;
    }
    if (!device_info(name, p->Printer, mode, &p->Device)) {
        g_spool.Close(p->Printer);
        HeapFree(GetProcessHeap(), 0, p);
        return nullptr;
    }
    p->Info = info_only;
    p->Dc = NtGdiCreateCompatibleDC(nullptr);
    if (!p->Dc) {
        g_spool.Close(p->Printer);
        HeapFree(GetProcessHeap(), 0, p);
        return nullptr;
    }
    set_resolution(p);
    RtlAcquireSRWLockExclusive(&g_lock);
    p->Next = g_dcs;
    g_dcs = p;
    _InterlockedIncrement(&PrnDcCount);
    RtlReleaseSRWLockExclusive(&g_lock);
    return p->Dc;
}

BOOL PrnDeleteDC(HDC dc)
{
    PRN_DC *found = nullptr;
    RtlAcquireSRWLockExclusive(&g_lock);
    for (PRN_DC **link = &g_dcs; *link; link = &(*link)->Next) {
        if ((*link)->Dc == dc) {
            found = *link;
            *link = found->Next;
            _InterlockedDecrement(&PrnDcCount);
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    if (!found)
        return FALSE;
    if (found->InDoc)
        g_spool.Abort(found->Printer);
    surface_free(found);
    g_spool.Close(found->Printer);
    BOOL ok = NtGdiDeleteObjectApp(dc);
    HeapFree(GetProcessHeap(), 0, found);
    return ok;
}

/*
 * nullptr leaves the call to the display. the driver "WINSPOOL" names a printer by its device. a program may
 * also omit the driver and pass a printer name, any other name is for the display.
 */
HDC PrnCreateDC(LPCWSTR driver, LPCWSTR device, const void *mode, BOOL info_only, BOOL *handled)
{
    *handled = FALSE;
    BOOL named = driver && (!_wcsicmp(driver, g_winspool) || !_wcsicmp(driver, L"winspool.drv"));
    if (!device || !*device || (driver && !named))
        return nullptr;
    if (!named) {
        if (!spool_load())
            return nullptr;
        HANDLE probe;
        WCHAR name[128];
        _snwprintf(name, ARRAYSIZE(name), L"%ls", device);
        if (!g_spool.Open(name, &probe, nullptr))
            return nullptr;
        g_spool.Close(probe);
    }
    *handled = TRUE;
    return prn_create(device, (const DEVMODEW *)mode, info_only);
}

static void devmode_a2w(const DEVMODEA *a, DEVMODEW *w)
{
    memset(w, 0, sizeof(*w));
    MultiByteToWideChar(CP_ACP, 0, (LPCSTR)a->dmDeviceName, -1, w->dmDeviceName, CCHDEVICENAME);
    memcpy(&w->dmSpecVersion, &a->dmSpecVersion, offsetof(DEVMODEA, dmFormName) - offsetof(DEVMODEA, dmSpecVersion));
    MultiByteToWideChar(CP_ACP, 0, (LPCSTR)a->dmFormName, -1, w->dmFormName, CCHFORMNAME);
    memcpy(&w->dmLogPixels, &a->dmLogPixels,
           offsetof(DEVMODEA, dmPanningHeight) + sizeof(a->dmPanningHeight) - offsetof(DEVMODEA, dmLogPixels));
    w->dmSize = sizeof(DEVMODEW);
}

HDC PrnCreateDCA(LPCSTR driver, LPCSTR device, const void *mode, BOOL info_only, BOOL *handled)
{
    WCHAR wide_driver[64], wide_device[128];
    DEVMODEW wide_mode;
    LPCWSTR d = nullptr, n = nullptr;
    *handled = FALSE;
    if (driver && MultiByteToWideChar(CP_ACP, 0, driver, -1, wide_driver, ARRAYSIZE(wide_driver)))
        d = wide_driver;
    if (device && MultiByteToWideChar(CP_ACP, 0, device, -1, wide_device, ARRAYSIZE(wide_device)))
        n = wide_device;
    if (!n)
        return nullptr;
    if (mode)
        devmode_a2w(mode, &wide_mode);
    return PrnCreateDC(d, n, mode ? &wide_mode : nullptr, info_only, handled);
}

/* ------------------------------------------------------------------ what the printer answers */

BOOL PrnGetDeviceCaps(HDC dc, int index, int *value)
{
    PRN_DC *p = lookup(dc);
    if (!p)
        return FALSE;
    const MIME_PRINT_DEVICE *d = &p->Device;
    int v = 0;
    switch (index) {
    case DRIVERVERSION:
        v = 0x0401;
        break;
    case TECHNOLOGY:
        v = DT_RASPRINTER;
        break;
    case HORZSIZE:
        v = MulDiv(d->WidthPx, 254, d->XDpi * 10);
        break;
    case VERTSIZE:
        v = MulDiv(d->HeightPx, 254, d->YDpi * 10);
        break;
    case HORZRES:
    case DESKTOPHORZRES:
        v = d->WidthPx;
        break;
    case VERTRES:
    case DESKTOPVERTRES:
        v = d->HeightPx;
        break;
    case BITSPIXEL:
        v = 32;
        break;
    case PLANES:
        v = 1;
        break;
    case NUMBRUSHES:
        v = -1;
        break;
    case NUMPENS:
        v = -1;
        break;
    case NUMMARKERS:
        v = 0;
        break;
    case NUMFONTS:
        v = 0;
        break;
    case NUMCOLORS:
        v = d->Color ? -1 : 2;
        break;
    case PDEVICESIZE:
        v = 0;
        break;
    case CURVECAPS:
        v = 0x00FF;
        break;
    case LINECAPS:
        v = 0x00FF;
        break;
    case POLYGONALCAPS:
        v = 0x00FF;
        break;
    case TEXTCAPS:
        v = 0x7807;
        break;
    case CLIPCAPS:
        v = 1;
        break;
    case RASTERCAPS:
        v = RC_BITBLT | RC_BITMAP64 | RC_GDI20_OUTPUT | RC_DI_BITMAP | RC_DIBTODEV | RC_STRETCHBLT | RC_STRETCHDIB |
            RC_OP_DX_OUTPUT | RC_SCALING;
        break;
    case ASPECTX:
        v = d->XDpi;
        break;
    case ASPECTY:
        v = d->YDpi;
        break;
    case ASPECTXY:
        v = (int)(((long long)d->XDpi * 1414 + 500) / 1000);
        break;
    case LOGPIXELSX:
        v = d->XDpi;
        break;
    case LOGPIXELSY:
        v = d->YDpi;
        break;
    case SIZEPALETTE:
    case NUMRESERVED:
        v = 0;
        break;
    case COLORRES:
        v = d->Color ? 24 : 1;
        break;
    case PHYSICALWIDTH:
        v = d->PageWidth;
        break;
    case PHYSICALHEIGHT:
        v = d->PageHeight;
        break;
    case PHYSICALOFFSETX:
        v = d->OffsetX;
        break;
    case PHYSICALOFFSETY:
        v = d->OffsetY;
        break;
    case SCALINGFACTORX:
    case SCALINGFACTORY:
    case VREFRESH:
    case BLTALIGNMENT:
    case SHADEBLENDCAPS:
    case COLORMGMTCAPS:
        v = 0;
        break;
    }
    *value = v;
    return TRUE;
}

/* ------------------------------------------------------------------ documents and pages */

static int fail(DWORD error)
{
    SetLastError(error);
    return SP_ERROR;
}

static int page_start(PRN_DC *p)
{
    if (!p->InDoc || p->InPage)
        return fail(ERROR_SPL_NO_STARTDOC);
    if (!surface_make(p))
        return SP_OUTOFMEMORY;
    page_clear(p);
    if (!g_spool.StartPage(p->Printer))
        return SP_ERROR;
    p->InPage = TRUE;
    return 1;
}

static int page_end(PRN_DC *p)
{
    if (!p->InDoc || !p->InPage)
        return fail(ERROR_SPL_NO_STARTDOC);
    int result = 1;
    if (p->AbortProc && !p->AbortProc(p->Dc, 0)) {
        /* the program gave up, so the document is dropped. */
        g_spool.Abort(p->Printer);
        p->InDoc = p->InPage = FALSE;
        surface_free(p);
        return SP_APPABORT;
    }
    MIME_PRINT_PAGE header;
    memset(&header, 0, sizeof(header));
    header.Magic = MIME_PRINT_PAGE_MAGIC;
    header.Width = (DWORD)p->Device.PageWidth;
    header.Height = (DWORD)p->Device.PageHeight;
    header.XDpi = (DWORD)p->Device.XDpi;
    header.YDpi = (DWORD)p->Device.YDpi;
    header.Stride = header.Width * 4;
    header.Reserved = p->Device.Color ? 0 : 1;
    DWORD written;
    SIZE_T size = (SIZE_T)header.Stride * header.Height;
    if (!g_spool.Write(p->Printer, &header, sizeof(header), &written) ||
        !g_spool.Write(p->Printer, p->Bits, (DWORD)size, &written) || !g_spool.EndPage(p->Printer))
        result = SP_ERROR;
    p->InPage = FALSE;
    p->Pages++;
    return result;
}

static int doc_start(PRN_DC *p, LPCWSTR name, LPCWSTR output)
{
    if (p->Info)
        return fail(ERROR_INVALID_HANDLE);
    if (p->InDoc)
        return fail(ERROR_INVALID_PARAMETER);
    struct {
        LPWSTR Name, Output, Datatype;
    } info = {(LPWSTR)(name ? name : L"Untitled"), (LPWSTR)output, (LPWSTR)MIME_DATATYPE_BITMAP};
    DWORD job = g_spool.StartDoc(p->Printer, 1, (LPBYTE)&info);
    if (!job) {
        /* a cancelled save dialog is the user's choice, not a program error. */
        if (GetLastError() == ERROR_PRINT_CANCELLED)
            SetLastError(ERROR_CANCELLED);
        return SP_ERROR;
    }
    p->InDoc = TRUE;
    p->Job = job;
    p->Pages = 0;
    return (int)job;
}

static int doc_end(PRN_DC *p)
{
    if (!p->InDoc)
        return fail(ERROR_SPL_NO_STARTDOC);
    int result = 1;
    /* a page left open is the last one. */
    if (p->InPage)
        result = page_end(p);
    if (result == SP_APPABORT || !p->InDoc)
        return result;
    if (!g_spool.EndDoc(p->Printer))
        result = SP_ERROR;
    p->InDoc = FALSE;
    surface_free(p);
    return result;
}

static int doc_abort(PRN_DC *p)
{
    if (!p->InDoc)
        return fail(ERROR_SPL_NO_STARTDOC);
    g_spool.Abort(p->Printer);
    p->InDoc = p->InPage = FALSE;
    surface_free(p);
    return 1;
}

/* the display dc and invalid dcs fail the document calls with no error code, or with an invalid handle. */
static int not_printer(HDC dc)
{
    DWORD v;
    if (!MfActive(dc) && !NtGdiGetDCDword(dc, MIME_DCDW_MAPMODE, &v))
        SetLastError(ERROR_INVALID_HANDLE);
    return SP_ERROR;
}

WINGDIAPI int WINAPI StartDocW(HDC dc, const DOCINFOW *info)
{
    PRN_DC *p = lookup(dc);
    if (!p)
        return not_printer(dc);
    if (!info)
        return fail(ERROR_INVALID_PARAMETER);
    return doc_start(p, info->lpszDocName, info->lpszOutput);
}

WINGDIAPI int WINAPI StartDocA(HDC dc, const DOCINFOA *info)
{
    PRN_DC *p = lookup(dc);
    if (!p)
        return not_printer(dc);
    if (!info)
        return fail(ERROR_INVALID_PARAMETER);
    WCHAR name[260], output[MAX_PATH];
    BOOL has_output = info->lpszOutput != nullptr;
    if (info->lpszDocName)
        MultiByteToWideChar(CP_ACP, 0, info->lpszDocName, -1, name, ARRAYSIZE(name));
    if (has_output)
        MultiByteToWideChar(CP_ACP, 0, info->lpszOutput, -1, output, ARRAYSIZE(output));
    return doc_start(p, info->lpszDocName ? name : nullptr, has_output ? output : nullptr);
}

WINGDIAPI int WINAPI EndDoc(HDC dc)
{
    PRN_DC *p = lookup(dc);
    return p ? doc_end(p) : not_printer(dc);
}

WINGDIAPI int WINAPI StartPage(HDC dc)
{
    PRN_DC *p = lookup(dc);
    return p ? page_start(p) : not_printer(dc);
}

WINGDIAPI int WINAPI EndPage(HDC dc)
{
    PRN_DC *p = lookup(dc);
    return p ? page_end(p) : not_printer(dc);
}

WINGDIAPI int WINAPI AbortDoc(HDC dc)
{
    PRN_DC *p = lookup(dc);
    return p ? doc_abort(p) : not_printer(dc);
}

WINGDIAPI int WINAPI SetAbortProc(HDC dc, ABORTPROC proc)
{
    PRN_DC *p = lookup(dc);
    if (!p)
        return not_printer(dc);
    p->AbortProc = proc;
    return 1;
}

/* only printer dcs can be reset, the page and document follow the new settings. */
WINGDIAPI HDC WINAPI ResetDCW(HDC dc, const void *mode)
{
    PRN_DC *p = lookup(dc);
    if (!p) {
        not_printer(dc);
        return nullptr;
    }
    if (p->InPage) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (!mode)
        return dc;
    MIME_PRINT_DEVICE device;
    if (!device_info(p->Device.Name, p->Printer, (const DEVMODEW *)mode, &device)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    p->Device = device;
    surface_free(p);
    set_resolution(p);
    return dc;
}

WINGDIAPI HDC WINAPI ResetDCA(HDC dc, const void *mode)
{
    if (!mode || !lookup(dc))
        return ResetDCW(dc, nullptr);
    DEVMODEW w;
    devmode_a2w(mode, &w);
    return ResetDCW(dc, &w);
}

/* ------------------------------------------------------------------ escapes */

static BOOL supported(int escape)
{
    switch (escape) {
    case NEWFRAME:
    case ABORTDOC:
    case SETABORTPROC:
    case STARTDOC:
    case ENDDOC:
    case QUERYESCSUPPORT:
    case GETPHYSPAGESIZE:
    case GETPRINTINGOFFSET:
    case GETSCALINGFACTOR:
    case SETCOPYCOUNT:
    case GETTECHNOLOGY:
        return TRUE;
    }
    return FALSE;
}

/* answered here since this file is the driver. -1 when the escape is not a printer one. */
int PrnEscape(HDC dc, int escape, int in_size, LPCSTR in, int out_size, LPSTR out, BOOL *handled)
{
    PRN_DC *p = lookup(dc);
    *handled = FALSE;
    if (!p)
        return 0;
    *handled = TRUE;
    switch (escape) {
    case QUERYESCSUPPORT: {
        if (!in || in_size < 2)
            return 0;
        int query = in_size >= 4 ? *(const int *)in : *(const WORD *)in;
        return supported(query);
    }
    case NEWFRAME: {
        int result = page_end(p);
        if (result <= 0)
            return result;
        return page_start(p) > 0 ? 1 : SP_ERROR;
    }
    case STARTDOC: {
        WCHAR name[260];
        name[0] = 0;
        if (in && in_size > 0)
            MultiByteToWideChar(CP_ACP, 0, in, in_size, name, ARRAYSIZE(name) - 1);
        name[ARRAYSIZE(name) - 1] = 0;
        int result = doc_start(p, name[0] ? name : nullptr, nullptr);
        return result > 0 ? 1 : result;
    }
    case ENDDOC: {
        int result = doc_end(p);
        return result > 0 ? 1 : result;
    }
    case ABORTDOC:
        return doc_abort(p);
    case SETABORTPROC:
        p->AbortProc = nullptr;
        return 1;
    case GETPHYSPAGESIZE:
        if (out && out_size >= (int)sizeof(POINT)) {
            ((POINT *)out)[0].x = p->Device.PageWidth;
            ((POINT *)out)[0].y = p->Device.PageHeight;
            return 1;
        }
        return 0;
    case GETPRINTINGOFFSET:
    case GETSCALINGFACTOR:
        if (out && out_size >= (int)sizeof(POINT)) {
            ((POINT *)out)[0].x = escape == GETPRINTINGOFFSET ? p->Device.OffsetX : 0;
            ((POINT *)out)[0].y = escape == GETPRINTINGOFFSET ? p->Device.OffsetY : 0;
            return 1;
        }
        return 0;
    case SETCOPYCOUNT:
        if (in && in_size >= 2 && out) {
            *(WORD *)out = *(const WORD *)in ? *(const WORD *)in : 1;
            return 1;
        }
        return 0;
    }
    *handled = FALSE;
    return 0;
}
