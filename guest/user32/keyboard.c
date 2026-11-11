/*
 * key and character translation. the tables hold the KBDUS mappings windows loads in win32k; the typing keys follow the
 * host's layout once it gave one, which win32k reads from the boot information.
 */
#include "user32_private.h"

#include <mime/abi.h>

#define US_LAYOUT ((HKL)(ULONG_PTR)0x04090409)
#define EXT 0x100 /* extended scan code, sent with the E0 prefix. */

/* virtual key of each set 1 scan code without the E0 prefix. */
static constexpr BYTE g_scan_to_vk[0x80] = {
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
static constexpr BYTE g_numpad_scan[10] = {0x52, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, 0x48, 0x49};

/* the keys a layout changes: digits, letters and punctuation, without the E0 prefix. */
static BOOL typing_key(UINT scan)
{
    return (scan >= 0x02 && scan <= 0x0D) || (scan >= 0x10 && scan <= 0x1B) || (scan >= 0x1E && scan <= 0x29) ||
           (scan >= 0x2B && scan <= 0x35) || scan == 0x56;
}

/* the host's layout, asked again every quarter second, or null for US. two copies, so readers never see one written. */
static MimeKeyboardLayout g_layouts[2];
static MimeKeyboardLayout *volatile g_layout;
static LONG g_layout_next;
static volatile DWORD g_layout_tick;
static volatile LONG g_layout_asked, g_layout_busy;

static const MimeKeyboardLayout *host_layout(void)
{
    DWORD now = GetTickCount();
    if (!g_layout_asked || now - g_layout_tick >= 250) {
        if (!InterlockedExchange(&g_layout_busy, 1)) {
            MimeKeyboardLayout *spare = &g_layouts[g_layout_next];
            ULONG generation = (ULONG)NtUserCallOneParam((ULONG_PTR)spare, MIME_ONEPARAM_GETKEYBOARDLAYOUT);
            if (!generation)
                g_layout = nullptr;
            else if (!g_layout || g_layout->generation != generation) {
                g_layout = spare;
                g_layout_next ^= 1;
            }
            g_layout_tick = now;
            g_layout_asked = 1;
            InterlockedExchange(&g_layout_busy, 0);
        }
    }
    return g_layout;
}

/* the typing key of the layout that gives a virtual key, or 0. */
static UINT layout_scan(const MimeKeyboardLayout *l, UINT vk)
{
    for (UINT s = 1; s < MIME_KEYBOARD_KEYS; s++)
        if (typing_key(s) && l->vk[s] == vk)
            return s;
    return 0;
}

/* the layout's handle: its language in both halves, as windows names the default layout of a language. */
static HKL current_layout(void)
{
    const MimeKeyboardLayout *l = host_layout();
    return l && l->language ? (HKL)(ULONG_PTR)MAKELONG(l->language, l->language) : US_LAYOUT;
}

static WCHAR upper_char(WCHAR c)
{
    return (WCHAR)(ULONG_PTR)CharUpperW((LPWSTR)(ULONG_PTR)c);
}

/* characters of the printable keys, unshifted and shifted. */
typedef struct CHAR_KEY {
    BYTE Vk;
    WCHAR Normal;
    WCHAR Shifted;
    WCHAR Control; /* with ctrl, 0 for none. */
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
    return nullptr;
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
    const MimeKeyboardLayout *l = host_layout();
    if (l) {
        UINT s = layout_scan(l, vk);
        if (s)
            return s;
    }
    for (UINT s = 1; s < RTL_NUMBER_OF(g_scan_to_vk); s++)
        if (g_scan_to_vk[s] == vk && vk != VK_SNAPSHOT && vk != VK_CLEAR && !(l && typing_key(s)))
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
        const MimeKeyboardLayout *l = typing_key(code) ? host_layout() : nullptr;
        vk = l && l->vk[code] ? l->vk[code] : g_scan_to_vk[code];
    }
    return distinguish ? vk : generic_vk(vk);
}

/* the character a key produces for a keyboard state, or 0. */
static WCHAR key_char(UINT vk, const BYTE *state)
{
    BOOL shift = state && (state[VK_SHIFT] & 0x80);
    BOOL ctrl = state && (state[VK_CONTROL] & 0x80);
    BOOL alt = state && (state[VK_MENU] & 0x80);
    BOOL caps = state && (state[VK_CAPITAL] & 0x01);
    const MimeKeyboardLayout *l = host_layout();
    UINT scan = l ? layout_scan(l, vk) : 0;
    /* ctrl with alt is altgr, which has no characters on the US layout. */
    if (ctrl && alt)
        return scan ? l->chars[scan][2] : 0;
    if (scan && !ctrl) {
        WCHAR normal = l->chars[scan][0], shifted = l->chars[scan][1];
        /* caps lock shifts the keys whose shifted character is the capital of the unshifted one. */
        BOOL capital = shifted && shifted != normal && upper_char(normal) == shifted;
        return (shift != (caps && capital)) ? shifted : normal;
    }
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
            return 0; /* ctrl shift 2 is nul, reported as no character. */
        if (vk == '6' && shift)
            return 0x1E;
        if (vk == VK_OEM_MINUS && shift)
            return 0x1F;
        return k->Control;
    }
    return shift ? k->Shifted : k->Normal;
}

/* the control character of a letter typed with Ctrl, which the host does not send as text, or 0. */
WCHAR UserCtrlLetterChar(UINT vk)
{
    BYTE state[256];
    if (vk < 'A' || vk > 'Z' || !GetKeyboardState(state))
        return 0;
    if (!(state[VK_CONTROL] & 0x80) || (state[VK_MENU] & 0x80))
        return 0;
    return key_char(vk, state);
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
        const MimeKeyboardLayout *l = host_layout();
        UINT scan = l ? layout_scan(l, code) : 0;
        if (scan && l->chars[scan][0])
            return upper_char(l->chars[scan][0]);
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

/* key names of the US layout, as in the KBDUS name tables. */
typedef struct KEY_NAME {
    BYTE Scan;
    const WCHAR *Name;
} KEY_NAME;

static const KEY_NAME g_key_names[] = {
    {0x01, L"Esc"},       {0x0E, L"Backspace"},   {0x0F, L"Tab"},         {0x1C, L"Enter"},
    {0x1D, L"Ctrl"},      {0x2A, L"Shift"},       {0x36, L"Right Shift"}, {0x37, L"Num *"},
    {0x38, L"Alt"},       {0x39, L"Space"},       {0x3A, L"Caps Lock"},   {0x3B, L"F1"},
    {0x3C, L"F2"},        {0x3D, L"F3"},          {0x3E, L"F4"},          {0x3F, L"F5"},
    {0x40, L"F6"},        {0x41, L"F7"},          {0x42, L"F8"},          {0x43, L"F9"},
    {0x44, L"F10"},       {0x45, L"Pause"},       {0x46, L"Scroll Lock"}, {0x47, L"Num 7"},
    {0x48, L"Num 8"},     {0x49, L"Num 9"},       {0x4A, L"Num -"},       {0x4B, L"Num 4"},
    {0x4C, L"Num 5"},     {0x4D, L"Num 6"},       {0x4E, L"Num +"},       {0x4F, L"Num 1"},
    {0x50, L"Num 2"},     {0x51, L"Num 3"},       {0x52, L"Num 0"},       {0x53, L"Num Del"},
    {0x54, L"Sys Req"},   {0x57, L"F11"},         {0x58, L"F12"},         {0x7C, L"F13"},
    {0x7D, L"F14"},       {0x7E, L"F15"},         {0x7F, L"F16"},
};

static const KEY_NAME g_key_names_ext[] = {
    {0x1C, L"Num Enter"}, {0x1D, L"Right Ctrl"}, {0x35, L"Num /"},    {0x37, L"Prnt Scrn"},
    {0x38, L"Right Alt"}, {0x45, L"Num Lock"},   {0x46, L"Break"},    {0x47, L"Home"},
    {0x48, L"Up"},        {0x49, L"Page Up"},    {0x4B, L"Left"},     {0x4D, L"Right"},
    {0x4F, L"End"},       {0x50, L"Down"},       {0x51, L"Page Down"}, {0x52, L"Insert"},
    {0x53, L"Delete"},    {0x56, L"Help"},       {0x5B, L"Left Windows"}, {0x5C, L"Right Windows"},
    {0x5D, L"Application"},
};

/* lparam holds the scan code in bits 16 to 23, the extended flag in bit 24 and "do not care" in bit 25. */
WINUSERAPI int WINAPI GetKeyNameTextW(LONG lparam, LPWSTR buffer, int size)
{
    if (!buffer || size <= 0)
        return 0;
    UINT scan = ((ULONG)lparam >> 16) & 0xFF;
    BOOL ext = (lparam & 0x01000000) != 0;
    BOOL any_side = (lparam & 0x02000000) != 0;
    if (any_side) {
        if (scan == 0x36)
            scan = 0x2A;
        if (ext && (scan == 0x1D || scan == 0x38))
            ext = FALSE;
    }
    const WCHAR *name = nullptr;
    WCHAR single[2] = {0, 0};
    const KEY_NAME *table = ext ? g_key_names_ext : g_key_names;
    ULONG count = ext ? RTL_NUMBER_OF(g_key_names_ext) : RTL_NUMBER_OF(g_key_names);
    for (ULONG i = 0; i < count && !name; i++)
        if (table[i].Scan == scan)
            name = table[i].Name;
    if (!name && !ext) {
        UINT ch = MapVirtualKeyExW(scan_to_vk(scan, FALSE), MAPVK_VK_TO_CHAR, US_LAYOUT);
        if (ch) {
            single[0] = upper_char((WCHAR)ch);
            name = single;
        }
    }
    if (!name) {
        buffer[0] = 0;
        return 0;
    }
    int n = 0;
    while (name[n] && n < size - 1) {
        buffer[n] = name[n];
        n++;
    }
    buffer[n] = 0;
    return n;
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
    if (!WideCharToMultiByte(CP_ACP, 0, w, 1, &a, 1, nullptr, nullptr))
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
    const MimeKeyboardLayout *l = host_layout();
    if (l && ch >= 0x20) {
        static const SHORT SHIFTS[3] = {0, 0x100, 0x600};
        for (int level = 0; level < 3; level++)
            for (UINT scan = 1; scan < MIME_KEYBOARD_KEYS; scan++)
                if (typing_key(scan) && l->vk[scan] && l->chars[scan][level] == ch)
                    return (SHORT)(SHIFTS[level] | l->vk[scan]);
    }
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

WINUSERAPI BOOL WINAPI OemToCharBuffW(LPCSTR source, LPWSTR destination, DWORD count);

/* the scan code and the shift state of a character of the OEM code page, the other way round of VkKeyScan. */
WINUSERAPI DWORD WINAPI OemKeyScan(WORD oem)
{
    CHAR oem_char = (CHAR)(oem & 0xFF);
    WCHAR wide;
    if (!OemToCharBuffW(&oem_char, &wide, 1))
        return 0xFFFFFFFF;
    SHORT scan = VkKeyScanW(wide);
    if (scan == -1)
        return 0xFFFFFFFF;
    UINT code = MapVirtualKeyW(scan & 0xFF, MAPVK_VK_TO_VSC);
    return ((DWORD)((scan >> 8) & 0xFF) << 16) | code;
}

WINUSERAPI int WINAPI GetKeyboardLayoutList(int count, HKL *list)
{
    if (count <= 0)
        return 1;
    if (!list) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    list[0] = current_layout();
    return 1;
}

/* the keyboard win32k reports to raw input: an enhanced 101/102 key keyboard. */
WINUSERAPI int WINAPI GetKeyboardType(int which)
{
    switch (which) {
    case 0:
        return 4;
    case 1:
        return 0;
    case 2:
        return 12;
    default:
        return 0;
    }
}

WINUSERAPI HKL WINAPI GetKeyboardLayout(DWORD thread)
{
    return current_layout();
}

WINUSERAPI BOOL WINAPI GetKeyboardLayoutNameW(LPWSTR name)
{
    if (!name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    wsprintfW(name, L"%08X", (UINT)LOWORD((ULONG_PTR)current_layout()));
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetKeyboardLayoutNameA(LPSTR name)
{
    WCHAR wide[KL_NAMELENGTH];
    if (!name || !GetKeyboardLayoutNameW(wide)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (int i = 0; i < KL_NAMELENGTH; i++)
        name[i] = (char)wide[i];
    return TRUE;
}

WINUSERAPI HKL WINAPI ActivateKeyboardLayout(HKL layout, UINT flags)
{
    HKL current = current_layout();
    if (layout != current && layout != (HKL)HKL_NEXT && layout != (HKL)HKL_PREV) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return current;
}

WINUSERAPI HKL WINAPI LoadKeyboardLayoutW(LPCWSTR name, UINT flags)
{
    return current_layout();
}

WINUSERAPI HKL WINAPI LoadKeyboardLayoutA(LPCSTR name, UINT flags)
{
    return current_layout();
}

/* the host layout is the only one and every load returns it, so unloading it leaves it in place. */
WINUSERAPI BOOL WINAPI UnloadKeyboardLayout(HKL layout)
{
    if (layout != current_layout()) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/*
 * like host keys on the keyboard driver path, an injected key press that produces a character also delivers that
 * character, since the kernel sends characters as their own input. see NtUserTranslateMessage.
 */
WINUSERAPI UINT WINAPI SendInput(UINT count, LPINPUT inputs, int size)
{
    if (size != sizeof(INPUT) || (count && !inputs)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    BYTE state[256];
    GetKeyboardState(state);
    /* the keys held by earlier calls of any thread, which the state of this thread has not seen. */
    static const BYTE held[] = {VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU};
    for (UINT i = 0; i < sizeof(held); i++)
        state[held[i]] = (GetAsyncKeyState(held[i]) & 0x8000) ? 0x80 : 0;
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
            /* TranslateMessage makes the control characters from the key itself. */
            if (ch < 0x20 || ch == 0x7F)
                ch = 0;
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
