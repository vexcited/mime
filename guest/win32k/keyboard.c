/*
 * key and character translation, which user32 leaves to win32k: MapVirtualKey, ToUnicode, VkKeyScan, GetKeyNameText
 * and ActivateKeyboardLayout. the tables hold the KBDUS mappings windows loads in win32k. the typing keys follow the
 * host's layout once it gave one, in the boot information. a program that takes its keys from raw input asks
 * MapVirtualKey for each of them.
 */
#include "win32k.h"
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
static BOOLEAN typing_key(UINT scan)
{
    return (scan >= 0x02 && scan <= 0x0D) || (scan >= 0x10 && scan <= 0x1B) || (scan >= 0x1E && scan <= 0x29) ||
           (scan >= 0x2B && scan <= 0x35) || scan == 0x56;
}
/* the host's layout, or null for US. the host may be writing a new one: a copy counts once the generation held still. */
static MimeKeyboardLayout g_layout;

static const MimeKeyboardLayout *host_layout(void)
{
    const volatile MimeBootInfo *boot = HalMimeGetBootInfo();
    for (int tries = 0; tries < 4; tries++) {
        memcpy(&g_layout, (const void *)&boot->keyboard, sizeof(g_layout));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (g_layout.generation == boot->keyboard.generation)
            break;
    }
    return g_layout.generation ? &g_layout : nullptr;
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
    return RtlUpcaseUnicodeChar(c);
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

static UINT scan_to_vk(UINT scan, BOOLEAN distinguish)
{
    BOOLEAN ext = (scan & 0xFF00) == 0xE000 || (scan & EXT);
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
    BOOLEAN shift = state && (state[VK_SHIFT] & 0x80);
    BOOLEAN ctrl = state && (state[VK_CONTROL] & 0x80);
    BOOLEAN alt = state && (state[VK_MENU] & 0x80);
    BOOLEAN caps = state && (state[VK_CAPITAL] & 0x01);
    const MimeKeyboardLayout *l = host_layout();
    UINT scan = l ? layout_scan(l, vk) : 0;
    /* ctrl with alt is altgr, which has no characters on the US layout. */
    if (ctrl && alt)
        return scan ? l->chars[scan][2] : 0;
    if (scan && !ctrl) {
        WCHAR normal = l->chars[scan][0], shifted = l->chars[scan][1];
        /* caps lock shifts the keys whose shifted character is the capital of the unshifted one. */
        BOOLEAN capital = shifted && shifted != normal && upper_char(normal) == shifted;
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
static UINT map_virtual_key(UINT code, UINT type)
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
    W32SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
}

/* MapVirtualKey passes no layout and MapVirtualKeyEx one with use_layout set: there is one layout here, the host's. */
UINT NTAPI NtUserMapVirtualKeyEx(UINT code, UINT type, HKL layout, BOOL use_layout)
{
    (void)layout;
    (void)use_layout;
    UserEnter();
    UINT r = map_virtual_key(code, type);
    UserLeave();
    return r;
}

int NTAPI NtUserToUnicodeEx(UINT vk, UINT scan, const BYTE *user_state, LPWSTR user_buffer, int count, UINT flags,
                            HKL layout)
{
    (void)flags;
    (void)layout;
    BYTE state[256];
    /* bit 15 of the scan code marks a key release, which produces nothing. */
    if ((scan & 0x8000) || !user_buffer || count <= 0)
        return 0;
    if (user_state && !NT_SUCCESS(MimeCopyFromUser(state, user_state, sizeof(state))))
        return 0;
    UserEnter();
    WCHAR out[2] = {key_char(vk & 0xFF, user_state ? state : nullptr), 0};
    UserLeave();
    if (!out[0] || !NT_SUCCESS(MimeCopyToUser(user_buffer, out, (count > 1 ? 2 : 1) * sizeof(WCHAR))))
        return 0;
    return 1;
}

DWORD NTAPI NtUserVkKeyScanEx(WCHAR ch, HKL layout, BOOL use_layout)
{
    (void)layout;
    (void)use_layout;
    UserEnter();
    SHORT r = -1;
    const MimeKeyboardLayout *l = host_layout();
    if (l && ch >= 0x20) {
        static const SHORT SHIFTS[3] = {0, 0x100, 0x600};
        for (int level = 0; level < 3 && r == -1; level++)
            for (UINT scan = 1; scan < MIME_KEYBOARD_KEYS && r == -1; scan++)
                if (typing_key(scan) && l->vk[scan] && l->chars[scan][level] == ch)
                    r = (SHORT)(SHIFTS[level] | l->vk[scan]);
    }
    if (r != -1) {
    } else if (ch >= 'a' && ch <= 'z') {
        r = (SHORT)(ch - 32);
    } else if (ch >= 'A' && ch <= 'Z') {
        r = (SHORT)(0x100 | ch);
    } else if (ch >= 1 && ch <= 26) {
        r = (SHORT)(0x200 | (ch + 'A' - 1));
    } else {
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_chars) && r == -1; i++) {
            if (g_chars[i].Normal == ch)
                r = (SHORT)g_chars[i].Vk;
            else if (g_chars[i].Shifted == ch)
                r = (SHORT)(0x100 | g_chars[i].Vk);
            else if (g_chars[i].Control == ch)
                r = (SHORT)(0x200 | g_chars[i].Vk);
        }
    }
    UserLeave();
    return (DWORD)(LONG)r;
}

/* lparam holds the scan code in bits 16 to 23, the extended flag in bit 24 and "do not care" in bit 25. */
DWORD NTAPI NtUserGetKeyNameText(LONG lparam, LPWSTR user_buffer, int size)
{
    if (!user_buffer || size <= 0)
        return 0;
    UINT scan = ((ULONG)lparam >> 16) & 0xFF;
    BOOLEAN ext = (lparam & 0x01000000) != 0;
    if (lparam & 0x02000000) {
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
    UserEnter();
    if (!name && !ext) {
        UINT ch = map_virtual_key(scan_to_vk(scan, FALSE), MAPVK_VK_TO_CHAR);
        if (ch) {
            single[0] = upper_char((WCHAR)ch);
            name = single;
        }
    }
    UserLeave();
    WCHAR out[32] = {};
    int n = 0;
    while (name && name[n] && n < size - 1 && n < 31) {
        out[n] = name[n];
        n++;
    }
    if (!NT_SUCCESS(MimeCopyToUser(user_buffer, out, (SIZE_T)(n + 1) * sizeof(WCHAR))))
        return 0;
    return (DWORD)n;
}

/* one layout is loaded, the host's: activating any names it as the one that was active. */
HKL NTAPI NtUserActivateKeyboardLayout(HKL layout, ULONG flags)
{
    (void)layout;
    (void)flags;
    UserEnter();
    HKL current = current_layout();
    UserLeave();
    return current;
}
