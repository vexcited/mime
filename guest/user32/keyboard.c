/*
 * the keyboard layout: the US english layout, the only one the host
 * delivers, for key and character translation. windows does this in
 * win32k with the loaded layout tables; the tables here hold the same
 * KBDUS mappings.
 */
#include "user32_private.h"

#define US_LAYOUT ((HKL)(ULONG_PTR)0x04090409)
#define EXT 0x100 /* extended scan code, sent with the E0 prefix */

/* virtual key of each set 1 scan code without the E0 prefix. */
static const BYTE g_scan_to_vk[0x80] = {
    [0x01] = VK_ESCAPE,     [0x02] = '1',          [0x03] = '2',          [0x04] = '3',
    [0x05] = '4',           [0x06] = '5',          [0x07] = '6',          [0x08] = '7',
    [0x09] = '8',           [0x0A] = '9',          [0x0B] = '0',          [0x0C] = VK_OEM_MINUS,
    [0x0D] = VK_OEM_PLUS,   [0x0E] = VK_BACK,      [0x0F] = VK_TAB,       [0x10] = 'Q',
    [0x11] = 'W',           [0x12] = 'E',          [0x13] = 'R',          [0x14] = 'T',
    [0x15] = 'Y',           [0x16] = 'U',          [0x17] = 'I',          [0x18] = 'O',
    [0x19] = 'P',           [0x1A] = VK_OEM_4,     [0x1B] = VK_OEM_6,     [0x1C] = VK_RETURN,
    [0x1D] = VK_LCONTROL,   [0x1E] = 'A',          [0x1F] = 'S',          [0x20] = 'D',
    [0x21] = 'F',           [0x22] = 'G',          [0x23] = 'H',          [0x24] = 'J',
    [0x25] = 'K',           [0x26] = 'L',          [0x27] = VK_OEM_1,     [0x28] = VK_OEM_7,
    [0x29] = VK_OEM_3,      [0x2A] = VK_LSHIFT,    [0x2B] = VK_OEM_5,     [0x2C] = 'Z',
    [0x2D] = 'X',           [0x2E] = 'C',          [0x2F] = 'V',          [0x30] = 'B',
    [0x31] = 'N',           [0x32] = 'M',          [0x33] = VK_OEM_COMMA, [0x34] = VK_OEM_PERIOD,
    [0x35] = VK_OEM_2,      [0x36] = VK_RSHIFT,    [0x37] = VK_MULTIPLY,  [0x38] = VK_LMENU,
    [0x39] = VK_SPACE,      [0x3A] = VK_CAPITAL,   [0x3B] = VK_F1,        [0x3C] = VK_F2,
    [0x3D] = VK_F3,         [0x3E] = VK_F4,        [0x3F] = VK_F5,        [0x40] = VK_F6,
    [0x41] = VK_F7,         [0x42] = VK_F8,        [0x43] = VK_F9,        [0x44] = VK_F10,
    [0x45] = VK_NUMLOCK,    [0x46] = VK_SCROLL,    [0x47] = VK_HOME,      [0x48] = VK_UP,
    [0x49] = VK_PRIOR,      [0x4A] = VK_SUBTRACT,  [0x4B] = VK_LEFT,      [0x4C] = VK_CLEAR,
    [0x4D] = VK_RIGHT,      [0x4E] = VK_ADD,       [0x4F] = VK_END,       [0x50] = VK_DOWN,
    [0x51] = VK_NEXT,       [0x52] = VK_INSERT,    [0x53] = VK_DELETE,    [0x54] = VK_SNAPSHOT,
    [0x56] = VK_OEM_102,    [0x57] = VK_F11,       [0x58] = VK_F12,       [0x59] = VK_CLEAR,
    [0x64] = VK_F13,        [0x65] = VK_F13 + 1,   [0x66] = VK_F13 + 2,   [0x67] = VK_F13 + 3,
    [0x68] = VK_F13 + 4,    [0x69] = VK_F13 + 5,   [0x6A] = VK_F13 + 6,   [0x6B] = VK_F13 + 7,
    [0x6C] = VK_F13 + 8,    [0x6D] = VK_F13 + 9,   [0x6E] = VK_F13 + 10,  [0x76] = VK_F24,
};

/* keys that only exist with the E0 prefix. */
typedef struct EXT_KEY {
    BYTE Scan;
    BYTE Vk;
} EXT_KEY;

static const EXT_KEY g_extended[] = {
    {0x1C, VK_RETURN}, {0x1D, VK_RCONTROL}, {0x35, VK_DIVIDE}, {0x37, VK_SNAPSHOT}, {0x38, VK_RMENU},
    {0x45, VK_NUMLOCK}, {0x47, VK_HOME},    {0x48, VK_UP},     {0x49, VK_PRIOR},    {0x4B, VK_LEFT},
    {0x4D, VK_RIGHT},  {0x4F, VK_END},      {0x50, VK_DOWN},   {0x51, VK_NEXT},     {0x52, VK_INSERT},
    {0x53, VK_DELETE}, {0x5B, VK_LWIN},     {0x5C, VK_RWIN},   {0x5D, VK_APPS},     {0x5F, VK_SLEEP},
};

/* numeric keypad keys share their scan codes with the navigation keys. */
static const BYTE g_numpad_scan[10] = {0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49};

/* characters of the printable keys: unshifted and shifted. */
typedef struct CHAR_KEY {
    BYTE Vk;
    WCHAR Normal;
    WCHAR Shifted;
    WCHAR Control; /* with ctrl, 0 for none */
} CHAR_KEY;

static const CHAR_KEY g_chars[] = {
    {'1', '1', '!', 0},       {'2', '2', '@', 0},          {'3', '3', '#', 0},        {'4', '4', '$', 0},
    {'5', '5', '%', 0},       {'6', '6', '^', 0},          {'7', '7', '&', 0},        {'8', '8', '*', 0},
    {'9', '9', '(', 0},       {'0', '0', ')', 0},          {VK_OEM_MINUS, '-', '_', 0}, {VK_OEM_PLUS, '=', '+', 0},
    {VK_OEM_4, '[', '{', 0x1B}, {VK_OEM_6, ']', '}', 0x1D}, {VK_OEM_5, '\\', '|', 0x1C}, {VK_OEM_1, ';', ':', 0},
    {VK_OEM_7, '\'', '"', 0}, {VK_OEM_3, '`', '~', 0},     {VK_OEM_COMMA, ',', '<', 0}, {VK_OEM_PERIOD, '.', '>', 0},
    {VK_OEM_2, '/', '?', 0},  {VK_OEM_102, '\\', '|', 0x1C}, {VK_SPACE, ' ', ' ', ' '}, {VK_TAB, '\t', '\t', 0},
    {VK_RETURN, '\r', '\r', '\n'}, {VK_BACK, '\b', '\b', 0x7F}, {VK_ESCAPE, 0x1B, 0x1B, 0x1B}, {VK_CANCEL, 0x03, 0x03, 0x03},
    {VK_MULTIPLY, '*', '*', 0}, {VK_ADD, '+', '+', 0},       {VK_SUBTRACT, '-', '-', 0}, {VK_DECIMAL, '.', '.', 0},
    {VK_DIVIDE, '/', '/', 0},
};

static const CHAR_KEY *char_key(UINT vk)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_chars); i++)
        if (g_chars[i].Vk == vk)
            return &g_chars[i];
    return NULL;
}

static UINT generic_vk(UINT vk)
{
    switch (vk) {
    case VK_LSHIFT:
    case VK_RSHIFT:
        return VK_SHIFT;
    case VK_LCONTROL:
    case VK_RCONTROL:
        return VK_CONTROL;
    case VK_LMENU:
    case VK_RMENU:
        return VK_MENU;
    }
    return vk;
}

/* scan code of a virtual key, with EXT for keys sent with the E0 prefix. */
static UINT vk_to_scan(UINT vk)
{
    switch (vk) {
    case VK_SHIFT:
        return 0x2A;
    case VK_CONTROL:
        return 0x1D;
    case VK_MENU:
        return 0x38;
    case VK_DECIMAL:
        return 0x53;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD0 + 9)
        return g_numpad_scan[vk - VK_NUMPAD0];
    for (UINT s = 1; s < RTL_NUMBER_OF(g_scan_to_vk); s++)
        if (g_scan_to_vk[s] == vk && vk != VK_SNAPSHOT && vk != VK_CLEAR)
            return s;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_extended); i++)
        if (g_extended[i].Vk == vk)
            return g_extended[i].Scan | EXT;
    if (vk == VK_CLEAR)
        return 0x4C;
    return 0;
}

static UINT scan_to_vk(UINT scan, BOOL distinguish)
{
    BOOL ext = (scan & 0xFF00) == 0xE000 || (scan & EXT);
    UINT code = scan & 0xFF;
    UINT vk = 0;
    if (ext) {
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_extended); i++)
            if (g_extended[i].Scan == code)
                vk = g_extended[i].Vk;
    } else if (code < RTL_NUMBER_OF(g_scan_to_vk)) {
        vk = g_scan_to_vk[code];
    }
    return distinguish ? vk : generic_vk(vk);
}

/* the character a key produces with a keyboard state, 0 for none. */
static WCHAR key_char(UINT vk, const BYTE *state)
{
    BOOL shift = state && (state[VK_SHIFT] & 0x80);
    BOOL ctrl = state && (state[VK_CONTROL] & 0x80);
    BOOL alt = state && (state[VK_MENU] & 0x80);
    BOOL caps = state && (state[VK_CAPITAL] & 0x01);
    /* ctrl with alt is altgr, which has no characters on the US layout. */
    if (ctrl && alt)
        return 0;
    if (vk >= 'A' && vk <= 'Z') {
        if (ctrl)
            return (WCHAR)(vk - 'A' + 1);
        return (WCHAR)((shift != caps) ? vk : vk + 32);
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD0 + 9)
        return ctrl ? 0 : (WCHAR)('0' + vk - VK_NUMPAD0);
    const CHAR_KEY *k = char_key(vk);
    if (!k)
        return 0;
    if (ctrl) {
        if (vk == '2' && shift)
            return 0; /* ctrl shift 2 is nul, reported as no character */
        if (vk == '6' && shift)
            return 0x1E;
        if (vk == VK_OEM_MINUS && shift)
            return 0x1F;
        return k->Control;
    }
    return shift ? k->Shifted : k->Normal;
}

WINUSERAPI UINT WINAPI MapVirtualKeyExW(UINT code, UINT type, HKL layout)
{
    switch (type) {
    case MAPVK_VK_TO_VSC:
        return vk_to_scan(code) & 0xFF;
    case MAPVK_VK_TO_VSC_EX: {
        UINT s = vk_to_scan(code);
        return (s & EXT) ? 0xE000 | (s & 0xFF) : s;
    }
    case MAPVK_VSC_TO_VK:
        return scan_to_vk(code, FALSE);
    case MAPVK_VSC_TO_VK_EX:
        return scan_to_vk(code, TRUE);
    case MAPVK_VK_TO_CHAR: {
        if (code >= 'A' && code <= 'Z')
            return code;
        if (code >= VK_NUMPAD0 && code <= VK_NUMPAD0 + 9)
            return '0' + code - VK_NUMPAD0;
        const CHAR_KEY *k = char_key(code);
        return k ? k->Normal : 0;
    }
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
}

WINUSERAPI UINT WINAPI MapVirtualKeyW(UINT code, UINT type)
{
    return MapVirtualKeyExW(code, type, US_LAYOUT);
}

WINUSERAPI UINT WINAPI MapVirtualKeyExA(UINT code, UINT type, HKL layout)
{
    return MapVirtualKeyExW(code, type, layout);
}

WINUSERAPI UINT WINAPI MapVirtualKeyA(UINT code, UINT type)
{
    return MapVirtualKeyExW(code, type, US_LAYOUT);
}

WINUSERAPI int WINAPI ToUnicodeEx(UINT vk, UINT scan, const BYTE *state, LPWSTR buffer, int count, UINT flags,
                                  HKL layout)
{
    /* bit 15 of the scan code marks a key release, which produces nothing. */
    if ((scan & 0x8000) || !buffer || count <= 0)
        return 0;
    WCHAR c = key_char(vk & 0xFF, state);
    if (!c)
        return 0;
    buffer[0] = c;
    if (count > 1)
        buffer[1] = 0;
    return 1;
}

WINUSERAPI int WINAPI ToUnicode(UINT vk, UINT scan, const BYTE *state, LPWSTR buffer, int count, UINT flags)
{
    return ToUnicodeEx(vk, scan, state, buffer, count, flags, US_LAYOUT);
}

WINUSERAPI int WINAPI ToAsciiEx(UINT vk, UINT scan, const BYTE *state, LPWORD chars, UINT flags, HKL layout)
{
    WCHAR w[2];
    int n = ToUnicodeEx(vk, scan, state, w, 2, flags, layout);
    if (n <= 0 || !chars)
        return n;
    char a = 0;
    if (!WideCharToMultiByte(CP_ACP, 0, w, 1, &a, 1, NULL, NULL))
        return 0;
    chars[0] = (WORD)(BYTE)a;
    return 1;
}

WINUSERAPI int WINAPI ToAscii(UINT vk, UINT scan, const BYTE *state, LPWORD chars, UINT flags)
{
    return ToAsciiEx(vk, scan, state, chars, flags, US_LAYOUT);
}

WINUSERAPI SHORT WINAPI VkKeyScanExW(WCHAR ch, HKL layout)
{
    if (ch >= 'a' && ch <= 'z')
        return (SHORT)(ch - 32);
    if (ch >= 'A' && ch <= 'Z')
        return (SHORT)(0x100 | ch);
    if (ch >= 1 && ch <= 26)
        return (SHORT)(0x200 | (ch + 'A' - 1));
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_chars); i++) {
        if (g_chars[i].Normal == ch)
            return (SHORT)g_chars[i].Vk;
        if (g_chars[i].Shifted == ch)
            return (SHORT)(0x100 | g_chars[i].Vk);
        if (g_chars[i].Control == ch)
            return (SHORT)(0x200 | g_chars[i].Vk);
    }
    return -1;
}

WINUSERAPI SHORT WINAPI VkKeyScanW(WCHAR ch)
{
    return VkKeyScanExW(ch, US_LAYOUT);
}

WINUSERAPI int WINAPI GetKeyboardLayoutList(int count, HKL *list)
{
    if (count <= 0)
        return 1;
    if (!list) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    list[0] = US_LAYOUT;
    return 1;
}

WINUSERAPI HKL WINAPI GetKeyboardLayout(DWORD thread)
{
    return US_LAYOUT;
}

WINUSERAPI BOOL WINAPI GetKeyboardLayoutNameW(LPWSTR name)
{
    if (!name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    memcpy(name, L"00000409", sizeof(L"00000409"));
    return TRUE;
}

WINUSERAPI HKL WINAPI ActivateKeyboardLayout(HKL layout, UINT flags)
{
    if (layout != US_LAYOUT && layout != (HKL)HKL_NEXT && layout != (HKL)HKL_PREV) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    return US_LAYOUT;
}

WINUSERAPI HKL WINAPI LoadKeyboardLayoutW(LPCWSTR name, UINT flags)
{
    return US_LAYOUT;
}

/*
 * injected input. a key press that produces a character also produces the
 * character, as the keyboard driver path does for host keys: the kernel
 * delivers characters as their own input, see NtUserTranslateMessage.
 */
WINUSERAPI UINT WINAPI SendInput(UINT count, LPINPUT inputs, int size)
{
    if (size != sizeof(INPUT) || (count && !inputs)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    BYTE state[256];
    GetKeyboardState(state);
    UINT sent = 0;
    for (UINT i = 0; i < count; i++) {
        INPUT in = inputs[i];
        WCHAR ch = 0;
        if (in.type == INPUT_KEYBOARD && !(in.ki.dwFlags & KEYEVENTF_UNICODE)) {
            if (!in.ki.wVk && (in.ki.dwFlags & KEYEVENTF_SCANCODE))
                in.ki.wVk = (WORD)scan_to_vk(in.ki.wScan | ((in.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) ? EXT : 0), TRUE);
            if (!in.ki.wScan) {
                UINT scan = vk_to_scan(in.ki.wVk);
                in.ki.wScan = (WORD)(scan & 0xFF);
                if (scan & EXT)
                    in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            }
            BOOL down = !(in.ki.dwFlags & KEYEVENTF_KEYUP);
            BYTE generic = (BYTE)generic_vk(in.ki.wVk);
            state[in.ki.wVk & 0xFF] = down ? 0x80 : 0;
            state[generic] = down ? 0x80 : 0;
            if (down && !(state[VK_CONTROL] & 0x80) && !(state[VK_MENU] & 0x80))
                ch = key_char(in.ki.wVk, state);
        }
        if (NtUserSendInput(1, &in, sizeof(in)) != 1)
            break;
        if (ch) {
            INPUT c = {INPUT_KEYBOARD};
            c.ki.wScan = ch;
            c.ki.dwFlags = KEYEVENTF_UNICODE;
            NtUserSendInput(1, &c, sizeof(c));
        }
        sent++;
    }
    return sent;
}

WINUSERAPI void WINAPI keybd_event(BYTE vk, BYTE scan, DWORD flags, ULONG_PTR extra)
{
    INPUT in = {INPUT_KEYBOARD};
    in.ki.wVk = vk;
    in.ki.wScan = scan;
    in.ki.dwFlags = flags;
    in.ki.dwExtraInfo = extra;
    SendInput(1, &in, sizeof(in));
}

WINUSERAPI void WINAPI mouse_event(DWORD flags, DWORD dx, DWORD dy, DWORD data, ULONG_PTR extra)
{
    INPUT in = {INPUT_MOUSE};
    in.mi.dx = (LONG)dx;
    in.mi.dy = (LONG)dy;
    in.mi.mouseData = data;
    in.mi.dwFlags = flags;
    in.mi.dwExtraInfo = extra;
    SendInput(1, &in, sizeof(in));
}
