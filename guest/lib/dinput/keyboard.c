/*
 * the system keyboard: its keys as objects of a 256 byte state, its
 * cooperative levels, the keys windows releases for an exclusive
 * acquisition and the key name and scan code properties of dinput8.
 */
#include "dinput_private.h"

/* the keys of an enhanced 101/102 key keyboard, in DIK order. */
static const BYTE ENHANCED_KEYS[] = {
    0x56, 0x57, 0x58, 0x73, 0x7E, 0x90, 0x99, 0x9C, 0x9D, 0xA0, 0xA1, 0xA2, 0xA4, 0xAE, 0xB0, 0xB2,
    0xB5, 0xB7, 0xB8, 0xC5, 0xC7, 0xC8, 0xC9, 0xCB, 0xCD, 0xCF, 0xD0, 0xD1, 0xD2, 0xD3, 0xDB, 0xDC,
    0xDD, 0xDE, 0xDF, 0xE3, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED,
};

/* windows reads the type of the first keyboard raw input lists, else GetKeyboardType. */
static DWORD keyboard_type(void)
{
    RAWINPUTDEVICELIST list[16];
    UINT count = RTL_NUMBER_OF(list);
    UINT n = GetRawInputDeviceList(list, &count, sizeof(RAWINPUTDEVICELIST));
    for (UINT i = 0; n != (UINT)-1 && i < n; i++) {
        if (list[i].dwType != RIM_TYPEKEYBOARD)
            continue;
        RID_DEVICE_INFO info = {sizeof(info)};
        UINT size = sizeof(info);
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO, &info, &size) != (UINT)-1 &&
            (info.keyboard.dwType == 4 || info.keyboard.dwType == 7 || info.keyboard.dwType == 8))
            return info.keyboard.dwType;
    }
    int type = GetKeyboardType(0);
    return type ? (DWORD)type : 4;
}

static HRESULT keyboard_init(DIDEV *dev)
{
    DWORD type = keyboard_type();
    DWORD sub = type == 1 || type == 3 ? type : 4;
    dev->devtype = (dev->di8 ? DI8DEVTYPE_KEYBOARD : DIDEVTYPE_KEYBOARD) | sub << 8;
    dev->count = 0;
    if (sub == 1) {
        for (DWORD dik = 0x01; dik <= 0x53; dik++)
            dev->objects[dev->count++] = (DIOBJECT){&GUID_Key, dik, DIDFT_MAKEINSTANCE(dik) | DIDFT_PSHBUTTON, 0};
        dev->objects[dev->count++] = (DIOBJECT){&GUID_Key, 0xB7, DIDFT_MAKEINSTANCE(0xB7) | DIDFT_PSHBUTTON, 0};
    } else if (sub == 4) {
        for (DWORD dik = 0x01; dik <= 0x53; dik++)
            dev->objects[dev->count++] = (DIOBJECT){&GUID_Key, dik, DIDFT_MAKEINSTANCE(dik) | DIDFT_PSHBUTTON, 0};
        for (DWORD i = 0; i < RTL_NUMBER_OF(ENHANCED_KEYS); i++) {
            DWORD dik = ENHANCED_KEYS[i];
            dev->objects[dev->count++] = (DIOBJECT){&GUID_Key, dik, DIDFT_MAKEINSTANCE(dik) | DIDFT_PSHBUTTON, 0};
        }
    }
    dev->native_size = 256;
    dev->axes = dev->povs = 0;
    dev->buttons = dev->count;
    DiCopyW(dev->name, DiString(17), DI_MAX_PATH);
    return DI_OK;
}

/* dinput.dll ignores NOWINKEY with exclusive, dinput8 blocks the windows keys then. */
static HRESULT keyboard_cooperate(DIDEV *dev, HWND window, DWORD flags)
{
    if (flags & DISCL_EXCLUSIVE) {
        if (!(flags & DISCL_FOREGROUND))
            return DIERR_UNSUPPORTED;
        dev->em = DIEM_EXCLUSIVE | (dev->di8 ? DIEM_NOWINKEY : 0);
        return DI_OK;
    }
    dev->em = 0;
    if (flags & DISCL_NOWINKEY) {
        if (!(flags & DISCL_FOREGROUND))
            return DIERR_UNSUPPORTED;
        dev->em = DIEM_NOWINKEY;
    }
    return DI_OK;
}

/* an exclusive keyboard lets go of the keys held, so the window sees them released. */
static void keyboard_acquire(DIDEV *dev)
{
    if (!(dev->em & DIEM_EXCLUSIVE))
        return;
    for (UINT vk = 0x08; vk <= 0xFD; vk++) {
        if (vk == VK_CAPITAL || vk == 0x15 || vk == 0x19)
            continue;
        if (GetAsyncKeyState((int)vk) < 0)
            keybd_event((BYTE)vk, 0, KEYEVENTF_KEYUP, 0);
    }
}

static void keyboard_unacquire(DIDEV *dev)
{
}

static void keyboard_state(DIDEV *dev, BYTE *native)
{
    memcpy(native, DiKeyState, 256);
}

/* the scan code of a key: its DIK, numlock and pause swapped as GetKeyNameText swaps them. */
static DWORD scancode(DWORD dik)
{
    if (dik == 0x45)
        return 0xC5;
    if (dik == 0xC5)
        return 0x45;
    return dik;
}

static BOOL keyboard_name(DIDEV *dev, const DIOBJECT *object, WCHAR *name)
{
    DWORD dik = DIDFT_GETINSTANCE(object->type);
    return DiKeyboardKeyName(scancode(dik), dik, name, dev->di8);
}

static HRESULT keyboard_property(DIDEV *dev, DWORD id, const DIOBJECT *object, DIPROPHEADER *h)
{
    if (!dev->di8 || !object)
        return E_NOTIMPL;
    DWORD dik = DIDFT_GETINSTANCE(object->type);
    if (id == 20) {
        DIPROPSTRING *s = (DIPROPSTRING *)h;
        return DiKeyboardKeyName(scancode(dik), dik, s->wsz, TRUE) ? DI_OK : DIERR_OBJECTNOTFOUND;
    }
    if (id == 23) {
        DWORD s = scancode(dik);
        DWORD value = s == 0xC5 ? 0x00451DE1 : (s & 0x80) ? ((s & 0x7F) << 8) | 0xE0 : s;
        ((DIPROPDWORD *)h)->dwData = value;
        return DI_OK;
    }
    return E_NOTIMPL;
}

const DICLASS DiKeyboardClass = {
    .keyboard = TRUE,
    .init = keyboard_init,
    .cooperate = keyboard_cooperate,
    .acquire = keyboard_acquire,
    .unacquire = keyboard_unacquire,
    .state = keyboard_state,
    .name = keyboard_name,
    .get_property = keyboard_property,
    .control_panel = L"control.exe keyboard",
};
