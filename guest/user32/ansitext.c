/* the ANSI entry points of text functions that user32 implements for Unicode only. */
#include "user32_private.h"

static WORD key_to_wide(WORD key)
{
    WCHAR wide = 0;
    if (MultiByteToWideChar(CP_ACP, 0, (LPCSTR)&key, 1, &wide, 1) <= 0)
        return 0xFFFF;
    return wide;
}

static WORD key_to_ansi(WORD key)
{
    WORD ansi = 0;
    if (WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)&key, 1, (LPSTR)&ansi, 2, nullptr, nullptr) <= 0)
        return 0;
    return ansi;
}

/* a count of -1 or less is the length of the string, as the text functions take it. */
static LPWSTR convert_text(LPCSTR text, int count, int *wide_count)
{
    *wide_count = 0;
    if (!text || !count)
        return nullptr;
    return UserAtoWn(text, count < 0 ? -1 : count, wide_count);
}

WINUSERAPI LONG WINAPI TabbedTextOutA(HDC dc, int x, int y, LPCSTR text, int count, int tabs, const INT *positions,
                                      int origin)
{
    int n;
    LPWSTR wide = convert_text(text, count, &n);
    if (!wide && text && count)
        return 0;
    LONG result = TabbedTextOutW(dc, x, y, wide ? wide : L"", n, tabs, positions, origin);
    UserFree(wide);
    return result;
}

WINUSERAPI DWORD WINAPI GetTabbedTextExtentA(HDC dc, LPCSTR text, int count, int tabs, const INT *positions)
{
    int n;
    LPWSTR wide = convert_text(text, count, &n);
    if (!wide && text && count)
        return 0;
    DWORD result = GetTabbedTextExtentW(dc, wide ? wide : L"", n, tabs, positions);
    UserFree(wide);
    return result;
}

/* without a callback the data is the text. */
WINUSERAPI BOOL WINAPI GrayStringA(HDC dc, HBRUSH brush, GRAYSTRINGPROC proc, LPARAM data, int count, int x, int y,
                                   int cx, int cy)
{
    if (proc || !data)
        return GrayStringW(dc, brush, proc, data, count, x, y, cx, cy);
    int n;
    LPWSTR wide = convert_text((LPCSTR)data, count ? count : -1, &n);
    if (!wide)
        return FALSE;
    BOOL result = GrayStringW(dc, brush, nullptr, (LPARAM)wide, n, x, y, cx, cy);
    UserFree(wide);
    return result;
}

/* text states take the length in wdata, where 0 is the length of the string. */
WINUSERAPI BOOL WINAPI DrawStateA(HDC dc, HBRUSH brush, DRAWSTATEPROC proc, LPARAM ldata, WPARAM wdata, int x, int y,
                                  int cx, int cy, UINT flags)
{
    UINT type = flags & 0x000F;
    if (type != DST_TEXT && type != DST_PREFIXTEXT)
        return DrawStateW(dc, brush, proc, ldata, wdata, x, y, cx, cy, flags);
    int n;
    LPWSTR wide = convert_text((LPCSTR)ldata, wdata ? (int)wdata : -1, &n);
    if (!wide || !n)
        return FALSE;
    BOOL result = DrawStateW(dc, brush, proc, (LPARAM)wide, n, x, y, cx, cy, flags);
    UserFree(wide);
    return result;
}

WINUSERAPI HACCEL WINAPI LoadAcceleratorsA(HINSTANCE instance, LPCSTR name)
{
    LPWSTR wide = UserAtoW(name);
    if (!wide && name)
        return nullptr;
    HACCEL result = LoadAcceleratorsW(instance, wide);
    UserFreeConverted(wide, name);
    return result;
}

WINUSERAPI HBITMAP WINAPI LoadBitmapA(HINSTANCE instance, LPCSTR name)
{
    LPWSTR wide = UserAtoW(name);
    if (!wide && name)
        return nullptr;
    HBITMAP result = LoadBitmapW(instance, wide);
    UserFreeConverted(wide, name);
    return result;
}

/* the key of an entry that is not a virtual key is a character. */
WINUSERAPI HACCEL WINAPI CreateAcceleratorTableA(LPACCEL entries, int count)
{
    if (!entries || count <= 0)
        return CreateAcceleratorTableW(entries, count);
    LPACCEL wide = UserAlloc((SIZE_T)count * sizeof(ACCEL));
    if (!wide)
        return nullptr;
    for (int i = 0; i < count; i++) {
        wide[i] = entries[i];
        if (!(wide[i].fVirt & FVIRTKEY))
            wide[i].key = key_to_wide(wide[i].key);
    }
    HACCEL result = CreateAcceleratorTableW(wide, count);
    UserFree(wide);
    return result;
}

WINUSERAPI int WINAPI CopyAcceleratorTableA(HACCEL accel, LPACCEL entries, int count)
{
    int copied = CopyAcceleratorTableW(accel, entries, count);
    if (entries)
        for (int i = 0; i < copied; i++)
            if (!(entries[i].fVirt & FVIRTKEY))
                entries[i].key = key_to_ansi(entries[i].key);
    return copied;
}

/* a character message holds an ANSI character, which the table compares as Unicode. */
WINUSERAPI int WINAPI TranslateAcceleratorA(HWND hwnd, HACCEL accel, LPMSG msg)
{
    if (!hwnd || !msg || (msg->message != WM_KEYDOWN && msg->message != WM_CHAR && msg->message != WM_SYSKEYDOWN &&
                          msg->message != WM_SYSCHAR))
        return 0;
    if (msg->message != WM_CHAR && msg->message != WM_SYSCHAR)
        return TranslateAcceleratorW(hwnd, accel, msg);
    MSG copy = *msg;
    copy.wParam = key_to_wide((WORD)(msg->wParam & 0xFF));
    return TranslateAcceleratorW(hwnd, accel, &copy);
}

WINUSERAPI int WINAPI GetKeyNameTextA(LONG lparam, LPSTR buffer, int size)
{
    if (!buffer || size <= 0)
        return 0;
    LPWSTR wide = UserAlloc((SIZE_T)size * sizeof(WCHAR));
    if (!wide)
        return 0;
    int length = GetKeyNameTextW(lparam, wide, size);
    int bytes = 0;
    if (length > 0)
        bytes = WideCharToMultiByte(CP_ACP, 0, wide, length, buffer, size - 1, nullptr, nullptr);
    buffer[bytes] = 0;
    UserFree(wide);
    return bytes;
}

WINUSERAPI SHORT WINAPI VkKeyScanA(CHAR ch)
{
    return VkKeyScanExA(ch, GetKeyboardLayout(0));
}
