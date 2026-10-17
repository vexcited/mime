/*
 * names: of keys, as dinput8 builds them from the keyboard layout and
 * dinput.dll takes them from its string table, and the other strings of
 * the en-US resources of both dlls.
 */
#include "dinput_private.h"

/* string 512 + DIK of both dlls' resources. */
static const WCHAR *const KEY_NAMES[256] = {
    [0x01] = L"Escape",
    [0x02] = L"1",
    [0x03] = L"2",
    [0x04] = L"3",
    [0x05] = L"4",
    [0x06] = L"5",
    [0x07] = L"6",
    [0x08] = L"7",
    [0x09] = L"8",
    [0x0A] = L"9",
    [0x0B] = L"0",
    [0x0C] = L"-",
    [0x0D] = L"=",
    [0x0E] = L"Backspace",
    [0x0F] = L"Tab",
    [0x10] = L"Q",
    [0x11] = L"W",
    [0x12] = L"E",
    [0x13] = L"R",
    [0x14] = L"T",
    [0x15] = L"Y",
    [0x16] = L"U",
    [0x17] = L"I",
    [0x18] = L"O",
    [0x19] = L"P",
    [0x1A] = L"[",
    [0x1B] = L"]",
    [0x1C] = L"Enter",
    [0x1D] = L"Left Ctrl",
    [0x1E] = L"A",
    [0x1F] = L"S",
    [0x20] = L"D",
    [0x21] = L"F",
    [0x22] = L"G",
    [0x23] = L"H",
    [0x24] = L"J",
    [0x25] = L"K",
    [0x26] = L"L",
    [0x27] = L";",
    [0x29] = L"`",
    [0x2A] = L"Left Shift",
    [0x2B] = L"\\\\",
    [0x2C] = L"Z",
    [0x2D] = L"X",
    [0x2E] = L"C",
    [0x2F] = L"V",
    [0x30] = L"B",
    [0x31] = L"N",
    [0x32] = L"M",
    [0x33] = L",",
    [0x34] = L".",
    [0x35] = L"/",
    [0x36] = L"Right Shift",
    [0x37] = L"Numpad *",
    [0x38] = L"Left Alt",
    [0x39] = L"Space",
    [0x3A] = L"CapsLock",
    [0x3B] = L"F1",
    [0x3C] = L"F2",
    [0x3D] = L"F3",
    [0x3E] = L"F4",
    [0x3F] = L"F5",
    [0x40] = L"F6",
    [0x41] = L"F7",
    [0x42] = L"F8",
    [0x43] = L"F9",
    [0x44] = L"F10",
    [0x45] = L"NumLock",
    [0x46] = L"ScrollLock",
    [0x47] = L"Numpad 7",
    [0x48] = L"Numpad 8",
    [0x49] = L"Numpad 9",
    [0x4A] = L"Numpad -",
    [0x4B] = L"Numpad 4",
    [0x4C] = L"Numpad 5",
    [0x4D] = L"Numpad 6",
    [0x4E] = L"Numpad +",
    [0x4F] = L"Numpad 1",
    [0x50] = L"Numpad 2",
    [0x51] = L"Numpad 3",
    [0x52] = L"Numpad 0",
    [0x53] = L"Numpad .",
    [0x56] = L"OEM Key 102",
    [0x57] = L"F11",
    [0x58] = L"F12",
    [0x64] = L"F13",
    [0x65] = L"F14",
    [0x66] = L"F15",
    [0x70] = L"Kana",
    [0x73] = L"Non-US / ?",
    [0x79] = L"Convert",
    [0x7B] = L"No Convert",
    [0x7D] = L"Yen",
    [0x7E] = L"Non-US Numpad .",
    [0x8D] = L"Numpad =",
    [0x90] = L"Prev Track",
    [0x91] = L"@",
    [0x92] = L":",
    [0x93] = L"_",
    [0x94] = L"Xfer",
    [0x95] = L"Stop",
    [0x96] = L"AX",
    [0x97] = L"Unlabeled",
    [0x99] = L"Next Track",
    [0x9C] = L"Numpad Enter",
    [0x9D] = L"Right Ctrl",
    [0xA0] = L"Mute",
    [0xA1] = L"Calculator",
    [0xA2] = L"Play/Pause",
    [0xA4] = L"Media Stop",
    [0xAE] = L"Volume -",
    [0xB0] = L"Volume +",
    [0xB2] = L"Web/Home",
    [0xB3] = L"Numpad ,",
    [0xB5] = L"Numpad /",
    [0xB7] = L"SysRq",
    [0xB8] = L"Right Alt",
    [0xC5] = L"Pause",
    [0xC7] = L"Home",
    [0xC8] = L"Up Arrow",
    [0xC9] = L"PgUp",
    [0xCB] = L"Left Arrow",
    [0xCD] = L"Right Arrow",
    [0xCF] = L"End",
    [0xD0] = L"Down Arrow",
    [0xD1] = L"PgDn",
    [0xD2] = L"Insert",
    [0xD3] = L"Delete",
    [0xDB] = L"Left Win",
    [0xDC] = L"Right Win",
    [0xDD] = L"AppMenu",
    [0xDE] = L"Power",
    [0xDF] = L"Sleep",
    [0xE3] = L"Wake",
    [0xE5] = L"Search",
    [0xE6] = L"Favorites",
    [0xE7] = L"Refresh",
    [0xE8] = L"Web Stop",
    [0xE9] = L"Forward",
    [0xEA] = L"Back",
    [0xEB] = L"My Computer",
    [0xEC] = L"Mail",
    [0xED] = L"Media",
};

const WCHAR *DiKeyFixedName(DWORD dik)
{
    return dik < 256 ? KEY_NAMES[dik] : nullptr;
}

/* the strings both dlls share, those differing named after the dll. */
const WCHAR *DiString(DWORD id)
{
    static const WCHAR *const MOUSE_OBJECTS[] = {L"X-axis",   L"Y-axis",   L"Wheel",    L"Button 0",
                                                 L"Button 1", L"Button 2", L"Button 3", L"Button 4",
                                                 L"Button 5", L"Button 6", L"Button 7"};
    if (id >= 256 && id <= 266)
        return MOUSE_OBJECTS[id - 256];
    switch (id) {
    case 16:
        return L"Mouse";
    case 17:
        return L"Keyboard";
    case 18:
        return L"Joystick %d";
    case 19:
        return DiIs8 ? L"Microsoft DirectInput8" : L"Microsoft DirectInput";
    case 20:
        return DiIs8 ? L"Microsoft DirectInputDevice8" : L"Microsoft DirectInputDevice";
    case 28:
        return L"Button %d";
    case 29:
        return L"Axis %d";
    case 30:
        return L"POV %d";
    case 31:
        return L"Collection %d";
    case 32:
        return L"Collection %d - %s";
    case 35:
        return L"Unknown %d";
    case 36:
        return DiIs8 ? L"Default" : L"Unknown %d";
    case 511:
        return L"Unknown";
    default:
        return L"";
    }
}

/* the keys whose name dinput8 never takes from the layout. */
static BOOL fixed_key(DWORD s)
{
    if (s >= 0x02 && s <= 0x0B)
        return TRUE;
    switch (s) {
    case 0x90:
    case 0x99:
    case 0xA0:
    case 0xA1:
    case 0xA2:
    case 0xA4:
    case 0xAE:
    case 0xB0:
    case 0xB2:
    case 0xDE:
    case 0xDF:
    case 0xE3:
        return TRUE;
    default:
        return s >= 0xE5 && s <= 0xED;
    }
}

static BOOL punctuation(WCHAR c)
{
    WORD type = 0;
    return GetStringTypeW(CT_CTYPE1, &c, 1, &type) && (type & C1_PUNCT);
}

/*
 * windows' DIGetKeyNameText: fixed names for digits and media keys, the
 * character a key of the main block types, uppercased, else the name
 * GetKeyNameText gives, else the fixed one but for the three keys of
 * other countries. dinput.dll has only its fixed names.
 */
BOOL DiKeyboardKeyName(DWORD s, DWORD dik, WCHAR *name, BOOL di8)
{
    name[0] = 0;
    const WCHAR *fixed = DiKeyFixedName(dik);
    if (!di8) {
        DiCopyW(name, fixed, DI_MAX_PATH);
        return TRUE;
    }
    if (fixed_key(s)) {
        DiCopyW(name, fixed, DI_MAX_PATH);
        return name[0] != 0;
    }
    if (s <= 0x53) {
        HWND active = GetActiveWindow();
        HKL hkl = GetKeyboardLayout(active ? GetWindowThreadProcessId(active, nullptr) : 0);
        UINT vk = MapVirtualKeyExW(s, MAPVK_VSC_TO_VK_EX, hkl);
        BYTE state[256] = {};
        WCHAR typed[DI_MAX_PATH];
        int n = vk ? ToUnicodeEx(vk, s, state, typed, DI_MAX_PATH, 0, hkl) : 0;
        if (n == 1 && (IsCharAlphaW(typed[0]) || punctuation(typed[0]))) {
            WCHAR c[2] = {typed[0], 0};
            CharUpperW(c);
            name[0] = c[0];
            name[1] = 0;
            return TRUE;
        }
    }
    if (!name[0])
        GetKeyNameTextW((LONG)(((s & 0x7F) | ((s & 0x80) << 1)) << 16), name, DI_MAX_PATH);
    if (!name[0] && s != 0x56 && s != 0x73 && s != 0x7E)
        DiCopyW(name, fixed, DI_MAX_PATH);
    return name[0] != 0;
}
