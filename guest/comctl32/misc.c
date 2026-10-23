/* common controls initialization and version. */
#include "comctl32_private.h"

#define MAKEDLLVERULL(major, minor, build, qfe)                                                                   \
    (((ULONGLONG)(major) << 48) | ((ULONGLONG)(minor) << 32) | ((ULONGLONG)(build) << 16) | (ULONGLONG)(qfe))

/* version 6 registers nothing here, the classes are registered at load. */
WINCOMMCTRLAPI void WINAPI InitCommonControls(void)
{
}

WINCOMMCTRLAPI BOOL WINAPI InitCommonControlsEx(const INITCOMMONCONTROLSEX *init)
{
    if (!init || init->dwSize != sizeof(*init) || (init->dwICC & 0x7FFF0000))
        return FALSE;
    RegisterControlClasses(init->dwICC);
    return TRUE;
}

/* the windows 11 comctl32 6 side by side version, with mime's own build number. */
WINCOMMCTRLAPI HRESULT WINAPI DllGetVersion(DLLVERSIONINFO *info)
{
    if (!info || (info->cbSize != sizeof(DLLVERSIONINFO) && info->cbSize != sizeof(DLLVERSIONINFO2)))
        return E_INVALIDARG;
    info->dwMajorVersion = 6;
    info->dwMinorVersion = 10;
    info->dwBuildNumber = 26300;
    info->dwPlatformID = DLLVER_PLATFORM_NT;
    if (info->cbSize == sizeof(DLLVERSIONINFO2)) {
        DLLVERSIONINFO2 *info2 = (DLLVERSIONINFO2 *)info;
        info2->dwFlags = 0;
        info2->ullVersion = MAKEDLLVERULL(6, 10, 26300, 0);
    }
    return S_OK;
}

WINCOMMCTRLAPI BOOL WINAPI _TrackMouseEvent(LPTRACKMOUSEEVENT track)
{
    return TrackMouseEvent(track);
}

/* ImageList_AddIcon is a macro in the headers but an export too. */
int WINAPI CcImageListAddIcon(HIMAGELIST himl, HICON icon)
{
    return ImageList_ReplaceIcon(himl, -1, icon);
}

static LANGID g_mui_language;

WINCOMMCTRLAPI void WINAPI InitMUILanguage(LANGID language)
{
    g_mui_language = language;
}

WINCOMMCTRLAPI LANGID WINAPI GetMUILanguage(void)
{
    return g_mui_language;
}

/* the client rectangle minus the visible controls named by the pairs in info, skipping the first pair. */
WINCOMMCTRLAPI void WINAPI GetEffectiveClientRect(HWND hwnd, LPRECT rect, const INT *info)
{
    GetClientRect(hwnd, rect);
    if (!info)
        return;
    for (info += 2; info[0]; info += 2) {
        HWND child = GetDlgItem(hwnd, info[1]);
        if (!child || !(GetWindowLongW(child, GWL_STYLE) & WS_VISIBLE))
            continue;
        RECT r, out;
        GetWindowRect(child, &r);
        MapWindowPoints(nullptr, hwnd, (POINT *)&r, 2);
        if (SubtractRect(&out, rect, &r))
            *rect = out;
    }
}

/* system icons come from imageres, whose artwork windows uses at any size. */
static WORD imageres_id(LPCWSTR name)
{
    switch ((ULONG_PTR)name) {
    case 32512:
    case 32517:
        return 15;
    case 32513:
        return 98;
    case 32514:
        return 99;
    case 32515:
        return 84;
    case 32516:
        return 81;
    case 32518:
        return 78;
    }
    return 0;
}

WINCOMMCTRLAPI HRESULT WINAPI LoadIconWithScaleDown(HINSTANCE inst, PCWSTR name, int cx, int cy, HICON *icon)
{
    if (!icon)
        return E_INVALIDARG;
    *icon = nullptr;
    if (!name)
        return E_INVALIDARG;
    if (!inst && IS_INTRESOURCE(name)) {
        WORD id = imageres_id(name);
        if (id) {
            HMODULE res = LoadLibraryExW(L"imageres.dll", nullptr, LOAD_LIBRARY_AS_DATAFILE);
            if (res) {
                *icon = LoadImageW(res, MAKEINTRESOURCEW(id), IMAGE_ICON, cx, cy, 0);
                FreeLibrary(res);
            }
        }
        if (!*icon)
            *icon = CopyImage(LoadIconW(nullptr, name), IMAGE_ICON, cx, cy, 0);
    } else {
        *icon = LoadImageW(inst, name, IMAGE_ICON, cx, cy, 0);
    }
    if (*icon)
        return S_OK;
    DWORD error = GetLastError();
    return HRESULT_FROM_WIN32(error ? error : ERROR_RESOURCE_NAME_NOT_FOUND);
}

WINCOMMCTRLAPI HRESULT WINAPI LoadIconMetric(HINSTANCE inst, PCWSTR name, int metric, HICON *icon)
{
    if (metric != LIM_SMALL && metric != LIM_LARGE) {
        if (icon)
            *icon = nullptr;
        return E_INVALIDARG;
    }
    int cx = GetSystemMetrics(metric == LIM_SMALL ? SM_CXSMICON : SM_CXICON);
    int cy = GetSystemMetrics(metric == LIM_SMALL ? SM_CYSMICON : SM_CYICON);
    return LoadIconWithScaleDown(inst, name, cx, cy, icon);
}

WINCOMMCTRLAPI int WINAPI DrawShadowText(HDC dc, LPCWSTR text, UINT count, RECT *rect, DWORD flags,
                                         COLORREF text_color, COLORREF shadow, int dx, int dy)
{
    int mode = SetBkMode(dc, TRANSPARENT);
    COLORREF old = SetTextColor(dc, shadow);
    RECT r = *rect;
    OffsetRect(&r, dx, dy);
    DrawTextW(dc, text, (int)count, &r, flags);
    SetTextColor(dc, text_color);
    int h = DrawTextW(dc, text, (int)count, rect, flags);
    SetTextColor(dc, old);
    SetBkMode(dc, mode);
    return h;
}
