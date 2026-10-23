/* window, menu, color and message box helpers. */
#include "shlwapi_private.h"

/* user32 exports these but the headers do not declare them. */
WINUSERAPI BOOL WINAPI IsWindowUnicode(HWND window);
WINUSERAPI BOOL WINAPI GetClassInfoW(HINSTANCE instance, LPCWSTR name, WNDCLASSW *wc);
WINUSERAPI BOOL WINAPI UnregisterClassA(LPCSTR name, HINSTANCE instance);
WINUSERAPI LONG_PTR WINAPI SetWindowLongPtrA(HWND window, int index, LONG_PTR value);
WINUSERAPI INT_PTR WINAPI DialogBoxParamA(HINSTANCE instance, LPCSTR name, HWND parent, DLGPROC proc, LPARAM param);

/* ------------------------------------------------------------------ colors */

/* hue, luminance and saturation run from 0 to 240, a gray's hue being 160. */
#define HLS_MAX 240
#define RGB_MAX 255
#define HUE_UNDEFINED 160

static int hue_channel(int low, int high, int hue)
{
    while (hue < 0)
        hue += HLS_MAX;
    while (hue >= HLS_MAX)
        hue -= HLS_MAX;
    if (hue < HLS_MAX / 6)
        return low + ((high - low) * hue + HLS_MAX / 12) / (HLS_MAX / 6);
    if (hue < HLS_MAX / 2)
        return high;
    if (hue < HLS_MAX * 2 / 3)
        return low + ((high - low) * (HLS_MAX * 2 / 3 - hue) + HLS_MAX / 12) / (HLS_MAX / 6);
    return low;
}

static int to_rgb(int value)
{
    return (value * RGB_MAX + HLS_MAX / 2) / HLS_MAX;
}

/* without saturation only the undefined hue gives a gray, any other hue black. */
COLORREF WINAPI ColorHLSToRGB(WORD hue, WORD luminance, WORD saturation)
{
    if (!saturation) {
        if (hue != HUE_UNDEFINED)
            return RGB(0, 0, 0);
        int gray = to_rgb(luminance);
        return RGB(gray, gray, gray);
    }
    int high = luminance <= HLS_MAX / 2 ? (luminance * (HLS_MAX + saturation) + HLS_MAX / 2) / HLS_MAX
                                        : luminance + saturation - (luminance * saturation + HLS_MAX / 2) / HLS_MAX;
    int low = 2 * luminance - high;
    int r = to_rgb(hue_channel(low, high, hue + HLS_MAX / 3));
    int g = to_rgb(hue_channel(low, high, hue));
    int b = to_rgb(hue_channel(low, high, hue - HLS_MAX / 3));
    return RGB(r < 0 ? 0 : r > 255 ? 255 : r, g < 0 ? 0 : g > 255 ? 255 : g, b < 0 ? 0 : b > 255 ? 255 : b);
}

void WINAPI ColorRGBToHLS(COLORREF color, WORD *hue, WORD *luminance, WORD *saturation)
{
    int r = GetRValue(color), g = GetGValue(color), b = GetBValue(color);
    int high = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int low = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int sum = high + low, delta = high - low;
    int l = (sum * HLS_MAX + RGB_MAX) / (2 * RGB_MAX), h, s;
    if (!delta) {
        h = HUE_UNDEFINED;
        s = 0;
    } else {
        if (l <= HLS_MAX / 2)
            s = (delta * HLS_MAX + sum / 2) / sum;
        else
            s = (delta * HLS_MAX + (2 * RGB_MAX - sum) / 2) / (2 * RGB_MAX - sum);
        int rd = ((high - r) * (HLS_MAX / 6) + delta / 2) / delta;
        int gd = ((high - g) * (HLS_MAX / 6) + delta / 2) / delta;
        int bd = ((high - b) * (HLS_MAX / 6) + delta / 2) / delta;
        if (r == high)
            h = bd - gd;
        else if (g == high)
            h = HLS_MAX / 3 + rd - bd;
        else
            h = HLS_MAX * 2 / 3 + gd - rd;
        while (h < 0)
            h += HLS_MAX;
        while (h > HLS_MAX)
            h -= HLS_MAX;
    }
    if (hue)
        *hue = (WORD)h;
    if (luminance)
        *luminance = (WORD)l;
    if (saturation)
        *saturation = (WORD)s;
}

/* luma in tenths of a percent of the whole range when scaling, in luminance units otherwise. */
COLORREF WINAPI ColorAdjustLuma(COLORREF color, int luma, BOOL scale)
{
    if (!luma)
        return color;
    WORD hue, luminance, saturation;
    ColorRGBToHLS(color, &hue, &luminance, &saturation);
    int l = luminance + (scale ? MulDiv(luma, HLS_MAX + 1, 1000) : luma);
    if (l < 0)
        l = 0;
    if (l > HLS_MAX)
        l = HLS_MAX;
    return ColorHLSToRGB(hue, (WORD)l, saturation);
}

COLORREF WINAPI SHFillRectClr(HDC dc, const RECT *rect, COLORREF color);
COLORREF WINAPI SHFillRectClr(HDC dc, const RECT *rect, COLORREF color)
{
    COLORREF old = SetBkColor(dc, color);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, rect, nullptr, 0, nullptr);
    SetBkColor(dc, old);
    return old;
}

/* the halftone palette: a 6 by 6 by 6 color cube, then grays up to 256 entries. */
HPALETTE WINAPI SHCreateShellPalette(HDC dc)
{
    struct {
        WORD version, count;
        PALETTEENTRY entries[256];
    } palette;
    palette.version = 0x300;
    palette.count = 256;
    int n = 0;
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++)
                palette.entries[n++] = (PALETTEENTRY){(BYTE)(r * 51), (BYTE)(g * 51), (BYTE)(b * 51), 0};
    for (int i = 0; n < 256; i++, n++) {
        BYTE gray = (BYTE)(i * 255 / 39);
        palette.entries[n] = (PALETTEENTRY){gray, gray, gray, 0};
    }
    return CreatePalette((const LOGPALETTE *)&palette);
}

int WINAPI SHGetCurColorRes(void);
int WINAPI SHGetCurColorRes(void)
{
    HDC dc = GetDC(nullptr);
    int bits = GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES);
    ReleaseDC(nullptr, dc);
    return bits;
}

/* compares the icons' bits. */
BOOL WINAPI SHAreIconsEqual(HICON a, HICON b);
BOOL WINAPI SHAreIconsEqual(HICON a, HICON b)
{
    if (a == b)
        return TRUE;
    if (!a || !b)
        return FALSE;
    ICONINFO ia, ib;
    if (!GetIconInfo(a, &ia))
        return FALSE;
    if (!GetIconInfo(b, &ib)) {
        DeleteObject(ia.hbmMask);
        DeleteObject(ia.hbmColor);
        return FALSE;
    }
    BOOL equal = TRUE;
    HBITMAP pairs[2][2] = {{ia.hbmMask, ib.hbmMask}, {ia.hbmColor, ib.hbmColor}};
    for (int i = 0; i < 2 && equal; i++) {
        if (!pairs[i][0] || !pairs[i][1]) {
            equal = pairs[i][0] == pairs[i][1];
            continue;
        }
        BITMAP ba, bb;
        GetObjectW(pairs[i][0], sizeof(ba), &ba);
        GetObjectW(pairs[i][1], sizeof(bb), &bb);
        if (ba.bmWidth != bb.bmWidth || ba.bmHeight != bb.bmHeight || ba.bmBitsPixel != bb.bmBitsPixel) {
            equal = FALSE;
            continue;
        }
        LONG bytes = ba.bmWidthBytes * ba.bmHeight;
        BYTE *da = ShlAlloc((SIZE_T)bytes), *db = ShlAlloc((SIZE_T)bytes);
        equal = da && db && GetBitmapBits(pairs[i][0], bytes, da) == bytes &&
                GetBitmapBits(pairs[i][1], bytes, db) == bytes && !memcmp(da, db, (SIZE_T)bytes);
        ShlFree(da);
        ShlFree(db);
    }
    for (int i = 0; i < 2; i++)
        for (int k = 0; k < 2; k++)
            if (pairs[i][k])
                DeleteObject(pairs[i][k]);
    return equal;
}

/* ------------------------------------------------------------------ menus */

int WINAPI GetMenuPosFromID(HMENU menu, UINT id)
{
    int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; i++)
        if (GetMenuItemID(menu, i) == id)
            return i;
    return -1;
}

int WINAPI SHMenuIndexFromID(HMENU menu, UINT id);
int WINAPI SHMenuIndexFromID(HMENU menu, UINT id)
{
    return GetMenuPosFromID(menu, id);
}

HMENU WINAPI SHGetMenuFromID(HMENU menu, UINT id);
HMENU WINAPI SHGetMenuFromID(HMENU menu, UINT id)
{
    MENUITEMINFOW info = {sizeof(info), MIIM_SUBMENU};
    if (!GetMenuItemInfoW(menu, id, FALSE, &info))
        return nullptr;
    return info.hSubMenu;
}

DWORD WINAPI SHRemoveAllSubMenus(HMENU menu);
DWORD WINAPI SHRemoveAllSubMenus(HMENU menu)
{
    int count = GetMenuItemCount(menu);
    if (count < 0)
        return 0;
    for (int i = count - 1; i >= 0; i--)
        DeleteMenu(menu, (UINT)i, MF_BYPOSITION);
    return (DWORD)count;
}

UINT WINAPI SHEnableMenuItem(HMENU menu, UINT id, BOOL enable);
UINT WINAPI SHEnableMenuItem(HMENU menu, UINT id, BOOL enable)
{
    return (UINT)EnableMenuItem(menu, id, MF_BYCOMMAND | (enable ? MF_ENABLED : MF_GRAYED));
}

DWORD WINAPI SHCheckMenuItem(HMENU menu, UINT id, BOOL check);
DWORD WINAPI SHCheckMenuItem(HMENU menu, UINT id, BOOL check)
{
    return CheckMenuItem(menu, id, MF_BYCOMMAND | (check ? MF_CHECKED : MF_UNCHECKED));
}

/* the first popup of a menu resource, detached from its bar. */
HMENU WINAPI SHLoadMenuPopup(HINSTANCE instance, LPCWSTR name);
HMENU WINAPI SHLoadMenuPopup(HINSTANCE instance, LPCWSTR name)
{
    HMENU bar = LoadMenuW(instance, name);
    if (!bar)
        return nullptr;
    HMENU popup = GetSubMenu(bar, 0);
    if (popup)
        RemoveMenu(bar, 0, MF_BYPOSITION);
    DestroyMenu(bar);
    return popup;
}

/* ------------------------------------------------------------------ windows */

HRESULT WINAPI SHAutoComplete(HWND edit, DWORD flags)
{
    return S_OK;
}

/* sets the mask bits from flags and returns the old value. */
LONG WINAPI SHSetWindowBits(HWND window, int index, UINT mask, UINT flags);
LONG WINAPI SHSetWindowBits(HWND window, int index, UINT mask, UINT flags)
{
    LONG old = GetWindowLongW(window, index);
    LONG value = (LONG)(((UINT)old & ~mask) | (flags & mask));
    if (value != old)
        SetWindowLongW(window, index, value);
    return old;
}

/* with no parent the window becomes a popup again. */
HWND WINAPI SHSetParentHwnd(HWND window, HWND parent);
HWND WINAPI SHSetParentHwnd(HWND window, HWND parent)
{
    HWND old = GetParent(window);
    if (old == parent)
        return old;
    if (parent)
        SHSetWindowBits(window, GWL_STYLE, WS_CHILD | WS_POPUP, WS_CHILD);
    HWND result = SetParent(window, parent);
    if (!parent)
        SHSetWindowBits(window, GWL_STYLE, WS_CHILD | WS_POPUP, WS_POPUP);
    return result;
}

typedef struct {
    UINT message;
    WPARAM wparam;
    LPARAM lparam;
    BOOL send;
} PROPAGATE;

static BOOL CALLBACK propagate_one(HWND window, LPARAM data)
{
    const PROPAGATE *p = (const PROPAGATE *)data;
    if (p->send)
        SendMessageW(window, p->message, p->wparam, p->lparam);
    else
        PostMessageW(window, p->message, p->wparam, p->lparam);
    return TRUE;
}

void WINAPI SHPropagateMessage(HWND parent, UINT message, WPARAM wparam, LPARAM lparam, BOOL send);
void WINAPI SHPropagateMessage(HWND parent, UINT message, WPARAM wparam, LPARAM lparam, BOOL send)
{
    PROPAGATE p = {message, wparam, lparam, send};
    if (parent)
        EnumChildWindows(parent, propagate_one, (LPARAM)&p);
}

/* FALSE when child is parent itself or one of its children, TRUE otherwise. */
BOOL WINAPI SHIsChildOrSelf(HWND parent, HWND child)
{
    if (parent == child || IsChild(parent, child))
        return FALSE;
    return TRUE;
}

LRESULT WINAPI SHDefWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT WINAPI SHDefWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (IsWindowUnicode(window))
        return DefWindowProcW(window, message, wparam, lparam);
    return DefWindowProcA(window, message, wparam, lparam);
}

/* skips classes that are already registered. */
ATOM WINAPI SHRegisterClassW(WNDCLASSW *wc);
ATOM WINAPI SHRegisterClassW(WNDCLASSW *wc)
{
    WNDCLASSW existing;
    if (GetClassInfoW(wc->hInstance, wc->lpszClassName, &existing))
        return TRUE;
    return RegisterClassW(wc);
}

ATOM WINAPI SHRegisterClassA(WNDCLASSA *wc);
ATOM WINAPI SHRegisterClassA(WNDCLASSA *wc)
{
    WNDCLASSW existing;
    WCHAR name[256];
    if (IS_INTRESOURCE(wc->lpszClassName) ||
        !MultiByteToWideChar(CP_ACP, 0, wc->lpszClassName, -1, name, RTL_NUMBER_OF(name)))
        return RegisterClassA(wc);
    if (GetClassInfoW(wc->hInstance, name, &existing))
        return TRUE;
    return RegisterClassA(wc);
}

void WINAPI SHUnregisterClassesW(HINSTANCE instance, LPCWSTR *names, UINT count);
void WINAPI SHUnregisterClassesW(HINSTANCE instance, LPCWSTR *names, UINT count)
{
    for (UINT i = 0; names && i < count; i++)
        UnregisterClassW(names[i], instance);
}

void WINAPI SHUnregisterClassesA(HINSTANCE instance, LPCSTR *names, UINT count);
void WINAPI SHUnregisterClassesA(HINSTANCE instance, LPCSTR *names, UINT count)
{
    for (UINT i = 0; names && i < count; i++)
        UnregisterClassA(names[i], instance);
}

/* a "WorkerW" window that keeps extra in its first pointer of window memory, with proc as its procedure
   when one is given. */
HWND WINAPI SHCreateWorkerWindowW(WNDPROC proc, HWND parent, DWORD ex_style, DWORD style, HMENU menu, LONG_PTR extra)
{
    HINSTANCE instance = GetModuleHandleW(L"shlwapi.dll");
    WNDCLASSW wc = {0, DefWindowProcW, 0, sizeof(LONG_PTR) * 2, instance, nullptr, LoadCursorW(nullptr, IDC_ARROW),
                    (HBRUSH)(COLOR_BTNFACE + 1), nullptr, L"WorkerW"};
    SHRegisterClassW(&wc);
    HWND window = CreateWindowExW(ex_style, L"WorkerW", nullptr, style, 0, 0, 0, 0, parent, menu, instance, nullptr);
    if (window) {
        SetWindowLongPtrW(window, 0, extra);
        if (proc)
            SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)proc);
    }
    return window;
}

HWND WINAPI SHCreateWorkerWindowA(WNDPROC proc, HWND parent, DWORD ex_style, DWORD style, HMENU menu,
                                  LONG_PTR extra);
HWND WINAPI SHCreateWorkerWindowA(WNDPROC proc, HWND parent, DWORD ex_style, DWORD style, HMENU menu, LONG_PTR extra)
{
    HINSTANCE instance = GetModuleHandleW(L"shlwapi.dll");
    WNDCLASSA wc = {0, DefWindowProcA, 0, sizeof(LONG_PTR) * 2, instance, nullptr, LoadCursorW(nullptr, IDC_ARROW),
                    (HBRUSH)(COLOR_BTNFACE + 1), nullptr, "WorkerA"};
    SHRegisterClassA(&wc);
    HWND window = CreateWindowExA(ex_style, "WorkerA", nullptr, style, 0, 0, 0, 0, parent, menu, instance, nullptr);
    if (window) {
        SetWindowLongPtrA(window, 0, extra);
        if (proc)
            SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)proc);
    }
    return window;
}

DWORD WINAPI SHSendMessageBroadcastW(UINT message, WPARAM wparam, LPARAM lparam)
{
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, message, wparam, lparam, SMTO_ABORTIFHUNG, 30000, &result);
    return (DWORD)result;
}

DWORD WINAPI SHSendMessageBroadcastA(UINT message, WPARAM wparam, LPARAM lparam)
{
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, message, wparam, lparam, SMTO_ABORTIFHUNG, 30000, &result);
    return (DWORD)result;
}

BOOL WINAPI SHBoolSystemParametersInfo(UINT action, DWORD *value);
BOOL WINAPI SHBoolSystemParametersInfo(UINT action, DWORD *value)
{
    return SystemParametersInfoW(action, 0, value, 0);
}

/* the dialog font is the system's already. */
void WINAPI SHSetDefaultDialogFont(HWND dialog, int id);
void WINAPI SHSetDefaultDialogFont(HWND dialog, int id) {}

void WINAPI SHRemoveDefaultDialogFont(HWND dialog);
void WINAPI SHRemoveDefaultDialogFont(HWND dialog) {}

/* ------------------------------------------------------------------ message boxes */

static const WCHAR dont_show_key[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\DontShowMeThisDialogAgain";

/* a box the user asked not to see again returns its default answer. */
int WINAPI SHMessageBoxCheckW(HWND parent, LPCWSTR text, LPCWSTR caption, UINT type, int fallback, LPCWSTR name)
{
    WCHAR value[16];
    DWORD size = sizeof(value);
    if (name && !RegGetValueW(HKEY_CURRENT_USER, dont_show_key, name, RRF_RT_REG_SZ, nullptr, value, &size) &&
        !lstrcmpiW(value, L"no"))
        return fallback;
    return MessageBoxW(parent, text, caption, type);
}

int WINAPI SHMessageBoxCheckA(HWND parent, LPCSTR text, LPCSTR caption, UINT type, int fallback, LPCSTR name)
{
    LPWSTR t = ShlWide(text), c = ShlWide(caption), n = ShlWide(name);
    int result = SHMessageBoxCheckW(parent, t, c, type, fallback, n);
    ShlFree(t);
    ShlFree(c);
    ShlFree(n);
    return result;
}

/* the dialog template version only knows the remembered answer. */
int WINAPI SHMessageBoxCheckExW(HWND parent, HINSTANCE instance, LPCWSTR dialog, DLGPROC proc, LPARAM param,
                                int fallback, LPCWSTR name);
int WINAPI SHMessageBoxCheckExW(HWND parent, HINSTANCE instance, LPCWSTR dialog, DLGPROC proc, LPARAM param,
                                int fallback, LPCWSTR name)
{
    WCHAR value[16];
    DWORD size = sizeof(value);
    if (name && !RegGetValueW(HKEY_CURRENT_USER, dont_show_key, name, RRF_RT_REG_SZ, nullptr, value, &size) &&
        !lstrcmpiW(value, L"no"))
        return fallback;
    return (int)DialogBoxParamW(instance, dialog, parent, proc, param);
}

int WINAPI SHMessageBoxCheckExA(HWND parent, HINSTANCE instance, LPCSTR dialog, DLGPROC proc, LPARAM param,
                                int fallback, LPCSTR name);
int WINAPI SHMessageBoxCheckExA(HWND parent, HINSTANCE instance, LPCSTR dialog, DLGPROC proc, LPARAM param,
                                int fallback, LPCSTR name)
{
    LPWSTR n = ShlWide(name);
    int result;
    WCHAR value[16];
    DWORD size = sizeof(value);
    if (n && !RegGetValueW(HKEY_CURRENT_USER, dont_show_key, n, RRF_RT_REG_SZ, nullptr, value, &size) &&
        !lstrcmpiW(value, L"no"))
        result = fallback;
    else
        result = (int)DialogBoxParamA(instance, dialog, parent, proc, param);
    ShlFree(n);
    return result;
}

int WINAPI SHRestrictedMessageBox(HWND parent);
int WINAPI SHRestrictedMessageBox(HWND parent)
{
    return MessageBoxW(parent, L"This operation has been cancelled due to restrictions in effect on this computer. "
                               L"Please contact your system administrator.",
                       L"Restrictions", MB_OK | MB_ICONSTOP);
}

/* text and caption may be resource ids. "%1" style inserts come from the arguments. */
static int shell_message_box(HINSTANCE instance, HWND parent, LPCWSTR text, LPCWSTR caption, UINT type, va_list *args)
{
    WCHAR text_buffer[1024], caption_buffer[256];
    if (IS_INTRESOURCE(text)) {
        if (!LoadStringW(instance, LOWORD((ULONG_PTR)text), text_buffer, RTL_NUMBER_OF(text_buffer)))
            return 0;
        text = text_buffer;
    }
    if (!caption) {
        caption_buffer[0] = 0;
        if (parent)
            GetWindowTextW(parent, caption_buffer, RTL_NUMBER_OF(caption_buffer));
        caption = caption_buffer;
    } else if (IS_INTRESOURCE(caption)) {
        if (!LoadStringW(instance, LOWORD((ULONG_PTR)caption), caption_buffer, RTL_NUMBER_OF(caption_buffer)))
            caption_buffer[0] = 0;
        caption = caption_buffer;
    }
    LPWSTR formatted = nullptr;
    FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER, text, 0, 0, (LPWSTR)&formatted, 0,
                   args);
    int result = MessageBoxW(parent, formatted ? formatted : text, caption, type);
    if (formatted)
        LocalFree(formatted);
    return result;
}

int WINAPIV ShellMessageBoxW(HINSTANCE instance, HWND parent, LPCWSTR text, LPCWSTR caption, UINT type, ...);
int WINAPIV ShellMessageBoxW(HINSTANCE instance, HWND parent, LPCWSTR text, LPCWSTR caption, UINT type, ...)
{
    va_list args;
    va_start(args, type);
    int result = shell_message_box(instance, parent, text, caption, type, &args);
    va_end(args);
    return result;
}

int WINAPIV ShellMessageBoxInternal(HINSTANCE instance, HWND parent, LPCWSTR text, LPCWSTR caption, UINT type, ...);
int WINAPIV ShellMessageBoxInternal(HINSTANCE instance, HWND parent, LPCWSTR text, LPCWSTR caption, UINT type, ...)
{
    va_list args;
    va_start(args, type);
    int result = shell_message_box(instance, parent, text, caption, type, &args);
    va_end(args);
    return result;
}

/* the ANSI form widens text and caption, its inserts keep their own types. */
int WINAPIV ShellMessageBoxA(HINSTANCE instance, HWND parent, LPCSTR text, LPCSTR caption, UINT type, ...);
int WINAPIV ShellMessageBoxA(HINSTANCE instance, HWND parent, LPCSTR text, LPCSTR caption, UINT type, ...)
{
    char text_buffer[1024], caption_buffer[256];
    if (IS_INTRESOURCE(text)) {
        if (!LoadStringA(instance, LOWORD((ULONG_PTR)text), text_buffer, sizeof(text_buffer)))
            return 0;
        text = text_buffer;
    }
    if (caption && IS_INTRESOURCE(caption)) {
        if (!LoadStringA(instance, LOWORD((ULONG_PTR)caption), caption_buffer, sizeof(caption_buffer)))
            caption_buffer[0] = 0;
        caption = caption_buffer;
    }
    va_list args;
    va_start(args, type);
    LPSTR formatted = nullptr;
    FormatMessageA(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER, text, 0, 0, (LPSTR)&formatted, 0,
                   &args);
    va_end(args);
    if (!caption) {
        caption_buffer[0] = 0;
        if (parent)
            GetWindowTextA(parent, caption_buffer, sizeof(caption_buffer));
        caption = caption_buffer;
    }
    int result = MessageBoxA(parent, formatted ? formatted : text, caption, type);
    if (formatted)
        LocalFree(formatted);
    return result;
}
