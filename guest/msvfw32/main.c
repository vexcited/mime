/*
 * msvfw32.dll: Video for Windows. the compressor manager opens the drivers that Drivers32 names, or functions given
 * by the program, and passes messages to them. draw dib draws device independent bitmaps with the usual stretch call.
 * no driver of the system is installed, and the capture window, the MCI window and the compressor dialogs are not here.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>

#define VFWAPI WINAPI
#define VFWAPIV __cdecl

typedef HANDLE HDRVR;
typedef HANDLE HIC;
typedef HANDLE HDRAWDIB;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

__declspec(dllimport) HDRVR WINAPI OpenDriver(LPCWSTR name, LPCWSTR section, LPARAM param);
__declspec(dllimport) LRESULT WINAPI CloseDriver(HDRVR driver, LPARAM param1, LPARAM param2);
__declspec(dllimport) HMODULE WINAPI GetDriverModuleHandle(HDRVR driver);
__declspec(dllimport) BOOL WINAPI GetOpenFileNameA(void *);
__declspec(dllimport) BOOL WINAPI GetOpenFileNameW(void *);
__declspec(dllimport) BOOL WINAPI GetSaveFileNameA(void *);
__declspec(dllimport) BOOL WINAPI GetSaveFileNameW(void *);

#define ICERR_OK 0
#define ICERR_UNSUPPORTED (-1)
#define ICERR_BADFORMAT (-2)
#define ICERR_BADHANDLE (-8)
#define DRV_OPEN 3
#define DRV_CLOSE 4
#define ICM_USER 0x4000
#define ICM_RESERVED 0x5000
#define ICM_GETINFO (ICM_RESERVED + 2)
#define ICM_COMPRESS_QUERY (ICM_USER + 6)
#define ICM_COMPRESS (ICM_USER + 8)
#define ICM_DECOMPRESS_QUERY (ICM_USER + 11)
#define ICM_DECOMPRESS (ICM_USER + 13)
#define ICM_DRAW_QUERY (ICM_USER + 31)
#define ICM_GETDEFAULTQUALITY (ICM_RESERVED + 30)
#define ICM_GETDEFAULTKEYFRAMERATE (ICM_USER + 42)
#define ICMODE_COMPRESS 1
#define ICMODE_DECOMPRESS 2
#define ICMODE_FASTDECOMPRESS 3
#define ICMODE_FASTCOMPRESS 5
#define ICMODE_DRAW 8

#define FOURCC(a, b, c, d) ((DWORD)(BYTE)(a) | (DWORD)(BYTE)(b) << 8 | (DWORD)(BYTE)(c) << 16 | (DWORD)(BYTE)(d) << 24)
#define FCC_VIDC FOURCC('v', 'i', 'd', 'c')
#define FCC_VIDS FOURCC('v', 'i', 'd', 's')

typedef struct {
    DWORD dwSize, fccType, fccHandler, dwFlags, dwVersion, dwVersionICM;
    WCHAR szName[16];
    WCHAR szDescription[128];
    WCHAR szDriver[128];
} ICINFO;

typedef struct {
    DWORD dwSize, fccType, fccHandler, dwVersion, dwFlags;
    LRESULT dwError;
    PVOID pV1Reserved, pV2Reserved;
    DWORD dnDevNode;
} ICOPEN;

typedef struct {
    DWORD dwFlags;
    LPBITMAPINFOHEADER lpbiOutput;
    LPVOID lpOutput;
    LPBITMAPINFOHEADER lpbiInput;
    LPVOID lpInput;
    LPDWORD lpckid;
    LPDWORD lpdwFlags;
    LONG lFrameNum;
    DWORD dwFrameSize;
    DWORD dwQuality;
    LPBITMAPINFOHEADER lpbiPrev;
    LPVOID lpPrev;
} ICCOMPRESS;

typedef struct {
    DWORD dwFlags;
    LPBITMAPINFOHEADER lpbiInput;
    LPVOID lpInput;
    LPBITMAPINFOHEADER lpbiOutput;
    LPVOID lpOutput;
    DWORD ckid;
} ICDECOMPRESS;

typedef struct {
    LONG cbSize;
    DWORD dwFlags;
    HIC hic;
    DWORD fccType, fccHandler;
    LPBITMAPINFO lpbiIn, lpbiOut;
    LPVOID lpBitsOut, lpBitsPrev;
    LONG lFrame, lKey, lDataRate, lQ, lKeyCount;
    LPVOID lpState;
    LONG cbState;
} COMPVARS;

typedef LRESULT(CALLBACK *DRIVERPROC)(DWORD_PTR driver_id, HDRVR driver, UINT message, LPARAM param1, LPARAM param2);

/* ------------------------------------------------------------------ the compressors that are open */

#define HIC_MAGIC 0x6743504D /* "MPCg". */

typedef struct COMPRESSOR {
    struct COMPRESSOR *Next;
    DWORD Magic, Type, Handler;
    HDRVR Driver;
    DWORD_PTR DriverId;
    DRIVERPROC Proc;
} COMPRESSOR;

static SRWLOCK g_lock = SRWLOCK_INIT;
static COMPRESSOR *g_open;

static COMPRESSOR *find_open(HIC hic)
{
    for (COMPRESSOR *c = g_open; c; c = c->Next)
        if (c == (COMPRESSOR *)hic)
            return c;
    return nullptr;
}

static BOOL valid(HIC hic)
{
    AcquireSRWLockShared(&g_lock);
    BOOL ok = find_open(hic) != nullptr;
    ReleaseSRWLockShared(&g_lock);
    return ok;
}

static COMPRESSOR *add_open(DWORD type, DWORD handler)
{
    COMPRESSOR *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c)
        return nullptr;
    c->Magic = HIC_MAGIC;
    c->Type = type;
    c->Handler = handler;
    AcquireSRWLockExclusive(&g_lock);
    c->Next = g_open;
    g_open = c;
    ReleaseSRWLockExclusive(&g_lock);
    return c;
}

static void remove_open(COMPRESSOR *c)
{
    AcquireSRWLockExclusive(&g_lock);
    for (COMPRESSOR **at = &g_open; *at; at = &(*at)->Next)
        if (*at == c) {
            *at = c->Next;
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
    c->Magic = 0;
    HeapFree(GetProcessHeap(), 0, c);
}

static DWORD lower_code(DWORD code)
{
    BYTE bytes[4] = {(BYTE)code, (BYTE)(code >> 8), (BYTE)(code >> 16), (BYTE)(code >> 24)};
    for (int i = 0; i < 4; i++)
        if (bytes[i] >= 'A' && bytes[i] <= 'Z')
            bytes[i] += 'a' - 'A';
    return FOURCC(bytes[0], bytes[1], bytes[2], bytes[3]);
}

LONG_PTR WINAPI ICSendMessage(HIC hic, UINT message, DWORD_PTR param1, DWORD_PTR param2)
{
    AcquireSRWLockShared(&g_lock);
    COMPRESSOR *c = find_open(hic);
    DRIVERPROC proc = c ? c->Proc : nullptr;
    DWORD_PTR id = c ? c->DriverId : 0;
    ReleaseSRWLockShared(&g_lock);
    if (!c)
        return ICERR_BADHANDLE;
    if (!proc)
        return ICERR_UNSUPPORTED;
    LONG_PTR result = proc(id, (HDRVR)(ULONG_PTR)1, message, param1, param2);
    if (result != ICERR_UNSUPPORTED)
        return result;
    /* a driver that does not answer is given the default rates. */
    if (message == ICM_GETDEFAULTKEYFRAMERATE) {
        *(DWORD *)param1 = 15;
        return 0;
    }
    if (message == ICM_GETDEFAULTQUALITY) {
        *(DWORD *)param1 = 10000;
        return 0;
    }
    return result;
}

LONG_PTR WINAPI ICClose(HIC hic)
{
    AcquireSRWLockShared(&g_lock);
    COMPRESSOR *c = find_open(hic);
    HDRVR driver = c ? c->Driver : nullptr;
    BOOL opened = c && c->DriverId && c->Proc;
    ReleaseSRWLockShared(&g_lock);
    if (!c)
        return ICERR_BADHANDLE;
    if (opened)
        ICSendMessage(hic, DRV_CLOSE, 0, 0);
    if (driver)
        CloseDriver(driver, 0, 0);
    remove_open(c);
    return ICERR_OK;
}

static LONG_PTR open_message(HIC hic, DWORD type, DWORD handler, UINT mode)
{
    ICOPEN open = {sizeof(open), type, handler, 0x104, mode};
    return ICSendMessage(hic, DRV_OPEN, 0, (DWORD_PTR)&open);
}

HIC WINAPI ICOpenFunction(DWORD type, DWORD handler, UINT mode, FARPROC function)
{
    if (!function)
        return nullptr;
    type = lower_code(type);
    handler = lower_code(handler);
    COMPRESSOR *c = add_open(type, handler);
    if (!c)
        return nullptr;
    c->Proc = (DRIVERPROC)function;
    LONG_PTR id = open_message((HIC)c, type, handler, mode);
    if (!id) {
        ICClose((HIC)c);
        return nullptr;
    }
    c->DriverId = (DWORD_PTR)id;
    return (HIC)c;
}

/* ------------------------------------------------------------------ the drivers that are installed */

#define DRIVERS32 L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32"

static void code_to_text(DWORD code, WCHAR *text)
{
    for (int i = 0; i < 4; i++)
        text[i] = (WCHAR)(BYTE)(code >> (8 * i));
}

BOOL WINAPI ICInfo(DWORD type, DWORD handler, ICINFO *info)
{
    if (!info || type - 1 <= 0xFE)
        return FALSE;
    type = lower_code(type);
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, DRIVERS32, 0, KEY_READ, &key))
        return FALSE;
    BOOL found = FALSE;
    /* a small handler is an index among the drivers of the type. */
    if (handler <= 0x100) {
        DWORD wanted = handler;
        for (DWORD i = 0;; i++) {
            WCHAR name[64];
            DWORD length = RTL_NUMBER_OF(name);
            if (RegEnumValueW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr))
                break;
            if (length != 9 || name[4] != L'.')
                continue;
            DWORD name_type = 0, name_handler = 0;
            for (int j = 0; j < 4; j++) {
                name_type |= (DWORD)(BYTE)name[j] << (8 * j);
                name_handler |= (DWORD)(BYTE)name[5 + j] << (8 * j);
            }
            if (type && lower_code(name_type) != type)
                continue;
            if (wanted--)
                continue;
            RegCloseKey(key);
            return ICInfo(lower_code(name_type), lower_code(name_handler), info);
        }
        RegCloseKey(key);
        return FALSE;
    }
    handler = lower_code(handler);
    WCHAR value_name[10];
    code_to_text(type, value_name);
    value_name[4] = L'.';
    code_to_text(handler, value_name + 5);
    value_name[9] = 0;
    WCHAR driver[128];
    DWORD size = sizeof(driver) - sizeof(WCHAR), kind;
    if (!RegQueryValueExW(key, value_name, nullptr, &kind, (BYTE *)driver, &size) && kind == REG_SZ) {
        driver[size / sizeof(WCHAR)] = 0;
        ZeroMemory(info, sizeof(*info));
        info->dwSize = sizeof(*info);
        info->fccType = type;
        info->fccHandler = handler;
        info->dwVersionICM = 0x104;
        /* the name stops at the first blank, the rest is an argument for the driver. */
        int i = 0;
        for (; driver[i] && driver[i] != L' ' && i < 127; i++)
            info->szDriver[i] = driver[i];
        found = TRUE;
    }
    RegCloseKey(key);
    return found;
}

HIC WINAPI ICOpen(DWORD type, DWORD handler, UINT mode)
{
    ICINFO info;
    type = lower_code(type);
    handler = lower_code(handler);
    if (!ICInfo(type, handler, &info) || !info.szDriver[0])
        return nullptr;
    COMPRESSOR *c = add_open(type, handler);
    if (!c)
        return nullptr;
    c->Driver = OpenDriver(info.szDriver, nullptr, 0);
    if (c->Driver) {
        HMODULE module = GetDriverModuleHandle(c->Driver);
        c->Proc = module ? (DRIVERPROC)GetProcAddress(module, "DriverProc") : nullptr;
    }
    if (!c->Proc) {
        ICClose((HIC)c);
        return nullptr;
    }
    LONG_PTR id = open_message((HIC)c, type, handler, mode);
    /* a video compressor that refuses the type is asked again as a video codec. */
    if (!id && type == FCC_VIDS) {
        c->Type = FCC_VIDC;
        id = open_message((HIC)c, FCC_VIDC, handler, mode == ICMODE_DRAW ? 2 : mode);
    }
    if (!id) {
        ICClose((HIC)c);
        return nullptr;
    }
    c->DriverId = (DWORD_PTR)id;
    return (HIC)c;
}

LONG_PTR WINAPI ICGetInfo(HIC hic, ICINFO *info, DWORD size)
{
    if (size < sizeof(ICINFO))
        return 0;
    if (!valid(hic))
        return ICERR_BADHANDLE;
    ZeroMemory(info, sizeof(*info));
    info->dwSize = sizeof(*info);
    info->dwVersionICM = 0x104;
    AcquireSRWLockShared(&g_lock);
    COMPRESSOR *c = find_open(hic);
    HDRVR driver = c ? c->Driver : nullptr;
    ReleaseSRWLockShared(&g_lock);
    if (driver) {
        HMODULE module = GetDriverModuleHandle(driver);
        GetModuleFileNameW(module, info->szDriver, RTL_NUMBER_OF(info->szDriver));
    }
    return ICSendMessage(hic, ICM_GETINFO, (DWORD_PTR)info, size);
}

HIC WINAPI ICLocate(DWORD type, DWORD handler, LPBITMAPINFOHEADER in, LPBITMAPINFOHEADER out, WORD flags)
{
    if (!type)
        return nullptr;
    UINT query;
    switch (flags) {
    case ICMODE_COMPRESS:
    case ICMODE_FASTCOMPRESS:
        query = ICM_COMPRESS_QUERY;
        break;
    case ICMODE_DECOMPRESS:
    case ICMODE_FASTDECOMPRESS:
        query = ICM_DECOMPRESS_QUERY;
        break;
    case ICMODE_DRAW:
        query = ICM_DRAW_QUERY;
        break;
    default:
        return nullptr;
    }
    HIC hic;
    if (handler) {
        hic = ICOpen(type, handler, flags);
        if (hic && ICSendMessage(hic, query, (DWORD_PTR)in, (DWORD_PTR)out) == 0)
            return hic;
        if (hic)
            ICClose(hic);
    }
    /* a video type tries the handler that the format of the input names. */
    if (type == FCC_VIDC && in) {
        DWORD compression = in->biCompression;
        if (compression <= 1)
            compression = FOURCC('M', 'R', 'L', 'E');
        if (compression > 0x100) {
            if (compression == FOURCC('C', 'R', 'A', 'M'))
                compression = FOURCC('M', 'S', 'V', 'C');
            hic = ICOpen(FOURCC('v', 'i', 'd', 'c'), compression, flags);
            if (hic && ICSendMessage(hic, query, (DWORD_PTR)in, (DWORD_PTR)out) == 0)
                return hic;
            if (hic)
                ICClose(hic);
        }
    }
    ICINFO info;
    for (DWORD i = 0; ICInfo(type, i, &info); i++) {
        hic = ICOpen(type, info.fccHandler, flags);
        if (!hic)
            continue;
        if (ICSendMessage(hic, query, (DWORD_PTR)in, (DWORD_PTR)out) == 0)
            return hic;
        ICClose(hic);
    }
    return nullptr;
}

DWORD VFWAPIV ICCompress(HIC hic, DWORD flags, LPBITMAPINFOHEADER out_header, LPVOID out, LPBITMAPINFOHEADER in_header,
                                         LPVOID in, LPDWORD ckid, LPDWORD out_flags, LONG frame, DWORD frame_size,
                                         DWORD quality, LPBITMAPINFOHEADER previous_header, LPVOID previous)
{
    ICCOMPRESS compress = {flags, out_header, out, in_header, in, ckid, out_flags, frame, frame_size, quality, previous_header, previous};
    return (DWORD)ICSendMessage(hic, ICM_COMPRESS, (DWORD_PTR)&compress, sizeof(compress));
}

DWORD VFWAPIV ICDecompress(HIC hic, DWORD flags, LPBITMAPINFOHEADER format, LPVOID data, LPBITMAPINFOHEADER header,
                                           LPVOID bits)
{
    ICDECOMPRESS decompress = {flags, format, data, header, bits, 0};
    return (DWORD)ICSendMessage(hic, ICM_DECOMPRESS, (DWORD_PTR)&decompress, sizeof(decompress));
}

/* ------------------------------------------------------------------ the compressor dialog */

static void free_global(LPVOID memory)
{
    if (memory) {
        HGLOBAL handle = GlobalHandle(memory);
        GlobalUnlock(handle);
        GlobalFree(handle);
    }
}

void WINAPI ICCompressorFree(COMPVARS *vars)
{
    if (!vars || vars->cbSize != sizeof(*vars))
        return;
    if (vars->hic) {
        ICClose(vars->hic);
        vars->hic = nullptr;
    }
    free_global(vars->lpbiOut);
    vars->lpbiOut = nullptr;
    free_global(vars->lpBitsOut);
    vars->lpBitsOut = nullptr;
    free_global(vars->lpBitsPrev);
    vars->lpBitsPrev = nullptr;
    free_global(vars->lpState);
    vars->lpState = nullptr;
    vars->dwFlags = 0;
}

/* there is no list of compressors to choose from, so the choice is the one a user gives who cancels. */
BOOL WINAPI ICCompressorChoose(HWND parent, UINT flags, LPVOID input, LPVOID data, COMPVARS *vars, LPSTR title)
{
    if (!vars || vars->cbSize != sizeof(*vars))
        return FALSE;
    if (!(vars->dwFlags & 1)) {
        vars->lQ = -1;
        vars->lKey = -1;
        vars->hic = nullptr;
        vars->fccType = vars->fccHandler = 0;
        vars->lDataRate = 300;
        vars->lpbiOut = nullptr;
        vars->lpBitsOut = vars->lpBitsPrev = nullptr;
        vars->dwFlags = 0;
        vars->lpState = nullptr;
        vars->cbState = 0;
    }
    return FALSE;
}

DWORD WINAPI VideoForWindowsVersion(void)
{
    return 0x010A03BE;
}

/* ------------------------------------------------------------------ draw dib */

#define DRAWDIB_MAGIC 0xAE8

typedef struct {
    DWORD Magic;
} DRAWDIB;

HDRAWDIB WINAPI DrawDibOpen(void)
{
    DRAWDIB *draw = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, DRAWDIB_MAGIC);
    if (draw)
        draw->Magic = DRAWDIB_MAGIC;
    return draw;
}

static DRAWDIB *draw_of(HDRAWDIB handle)
{
    DRAWDIB *draw = handle;
    return draw && draw->Magic == DRAWDIB_MAGIC ? draw : nullptr;
}

BOOL WINAPI DrawDibClose(HDRAWDIB handle)
{
    DRAWDIB *draw = draw_of(handle);
    if (!draw)
        return FALSE;
    draw->Magic = 0;
    LocalFree(draw);
    return TRUE;
}

/* where the bits of a packed dib start, after the header and its colors. */
static void *packed_bits(const BITMAPINFOHEADER *header)
{
    DWORD colors = header->biClrUsed;
    if (!colors && header->biBitCount <= 8)
        colors = 1u << header->biBitCount;
    DWORD masks = header->biCompression == BI_BITFIELDS && header->biSize == sizeof(BITMAPINFOHEADER) ? 3 : 0;
    return (BYTE *)header + header->biSize + (colors + masks) * sizeof(RGBQUAD);
}

#define DDF_DONTDRAW 0x0800

BOOL WINAPI DrawDibDraw(HDRAWDIB handle, HDC dc, int x, int y, int width, int height,
                                        LPBITMAPINFOHEADER header, LPVOID bits, int source_x, int source_y,
                                        int source_width, int source_height, UINT flags)
{
    if (!draw_of(handle) || !header)
        return FALSE;
    if (source_width == -1)
        source_width = header->biWidth;
    if (source_height == -1)
        source_height = header->biHeight < 0 ? -header->biHeight : header->biHeight;
    if (width == -1)
        width = source_width;
    if (height == -1)
        height = source_height;
    if (flags & DDF_DONTDRAW)
        return TRUE;
    if (!bits)
        bits = packed_bits(header);
    return (DWORD)StretchDIBits(dc, x, y, width, height, source_x, source_y, source_width, source_height, bits,
                         (BITMAPINFO *)header, DIB_RGB_COLORS, SRCCOPY) != GDI_ERROR;
}

BOOL WINAPI DrawDibBegin(HDRAWDIB handle, HDC dc, int width, int height, LPBITMAPINFOHEADER header,
                                         int source_width, int source_height, UINT flags)
{
    return draw_of(handle) != nullptr;
}

BOOL WINAPI DrawDibEnd(HDRAWDIB handle)
{
    return draw_of(handle) != nullptr;
}

BOOL WINAPI DrawDibStart(HDRAWDIB handle, LONG rate)
{
    return draw_of(handle) != nullptr;
}

BOOL WINAPI DrawDibStop(HDRAWDIB handle)
{
    return draw_of(handle) != nullptr;
}

BOOL WINAPI DrawDibChangePalette(HDRAWDIB handle, int start, int count, LPPALETTEENTRY entries)
{
    return draw_of(handle) != nullptr;
}

BOOL WINAPI DrawDibSetPalette(HDRAWDIB handle, HPALETTE palette)
{
    return draw_of(handle) != nullptr;
}

HPALETTE WINAPI DrawDibGetPalette(HDRAWDIB handle)
{
    return nullptr;
}

/* no palette is ever installed for a dib to be drawn with, so nothing changes. */
UINT WINAPI DrawDibRealize(HDRAWDIB handle, HDC dc, BOOL background)
{
    return 0;
}

LPVOID WINAPI DrawDibGetBuffer(HDRAWDIB handle, LPBITMAPINFOHEADER header, DWORD size, DWORD flags)
{
    return nullptr;
}

/* ------------------------------------------------------------------ file dialogs with a preview */

/* the preview pane is not shown, so these are the common file dialogs. */
BOOL WINAPI GetOpenFileNamePreviewA(void *ofn)
{
    return GetOpenFileNameA(ofn);
}

BOOL WINAPI GetOpenFileNamePreviewW(void *ofn)
{
    return GetOpenFileNameW(ofn);
}

BOOL WINAPI GetSaveFileNamePreviewA(void *ofn)
{
    return GetSaveFileNameA(ofn);
}

BOOL WINAPI GetSaveFileNamePreviewW(void *ofn)
{
    return GetSaveFileNameW(ofn);
}
