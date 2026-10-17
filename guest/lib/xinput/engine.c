/*
 * the xinput engine: pads found through the xusb interfaces, given user
 * indices by the led the driver keeps, and asked with the xusb device
 * controls. every call but XInputEnable runs under one lock, and a port
 * without a pad looks for one on each call while communication is on.
 * see docs/xinput.md.
 */
#include "xinput_private.h"
#include <mime/win32/dsound.h>

#define IOCTL_XUSB_GET_INFORMATION 0x80006000
#define IOCTL_XUSB_GET_CAPABILITIES 0x8000E004
#define IOCTL_XUSB_GET_LED_STATE 0x8000E008
#define IOCTL_XUSB_GET_STATE 0x8000E00C
#define IOCTL_XUSB_SET_STATE 0x8000A010
#define IOCTL_XUSB_WAIT_GUIDE_BUTTON 0x8000E014
#define IOCTL_XUSB_GET_BATTERY_INFORMATION 0x8000E018
#define IOCTL_XUSB_POWER_DOWN 0x8000A01C
#define IOCTL_XUSB_GET_AUDIO_INFORMATION 0x8000E020
#define IOCTL_XUSB_GET_INFORMATION_EX 0x8000E3FC

#define MAX_PORTS 4
#define MAX_BUSES 16
#define GUIDE 0x0400

/* the guide button wait gave the lock back already. */
#define XI_UNLOCKED ((HRESULT)2)

enum { SETTING_LED = 0x1, SETTING_LED_OFF = 0x2, SETTING_STOP_RUMBLE = 0x4 };

static const GUID XUSB = {0xEC87F1E3, 0xC13B, 0x4100, {0xB5, 0xF7, 0x8B, 0x84, 0xD5, 0x42, 0x60, 0xCB}};
static const UCHAR PORT_TO_LED[MAX_PORTS] = {6, 7, 8, 9};
static const UCHAR LED_TO_PORT[15] = {0xFF, 0xFF, 0, 1, 2, 3, 0, 1, 2, 3, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct PAD {
    BOOL Used;
    BOOL Connected;
    HANDLE Handle;
    HANDLE Overlapped; /* for the guide button wait, opened when first needed */
    WCHAR *Path;
    UCHAR Sub;
    USHORT Version;
    USHORT Vid, Pid;
    LONG Bus;
    XINPUT_STATE Last;
    XINPUT_VIBRATION Vibration;
    UCHAR Headset;
    WORD HeldButtons;
    UCHAR HeldTriggers;
    WORD LeftVk, RightVk;
} PAD;

typedef struct BUS {
    BOOL Used;
    HANDLE Handle;
    WCHAR *Path;
    USHORT Version, Vid, Pid;
} BUS;

USHORT XiRevision;
static RTL_CRITICAL_SECTION g_lock;
static BOOL g_ready;
static BOOL g_enabled = TRUE;
static ULONG g_settings = SETTING_LED | SETTING_STOP_RUMBLE;
static PAD g_ports[MAX_PORTS];
static BUS g_buses[MAX_BUSES];
static SP_DEVICE_INTERFACE_DETAIL_DATA_W *g_detail;
static DWORD g_detail_size;
/* the keystroke repeat, shared by every pad. */
static WORD g_repeat_vk;
static DWORD g_repeat_time;
static BOOL g_repeat_first;

static void lock(void)
{
    RtlEnterCriticalSection(&g_lock);
}

static void unlock(void)
{
    RtlLeaveCriticalSection(&g_lock);
}

static USHORT get16(const UCHAR *p)
{
    return (USHORT)(p[0] | p[1] << 8);
}

static DWORD code_of(HRESULT hr)
{
    if (hr == S_FALSE)
        return ERROR_DEVICE_NOT_CONNECTED;
    if (SUCCEEDED(hr))
        return ERROR_SUCCESS;
    if (hr == E_NOTIMPL && XiRevision >= 0x0104)
        return ERROR_CALL_NOT_IMPLEMENTED;
    if (hr == E_OUTOFMEMORY)
        return ERROR_OUTOFMEMORY;
    if (hr == E_INVALIDARG)
        return ERROR_BAD_ARGUMENTS;
    return (DWORD)hr;
}

/* the caller's output starts zeroed, as the dlls send it. */
static HRESULT control(HANDLE device, DWORD code, const void *in, DWORD in_len, void *out, DWORD out_len,
                       OVERLAPPED *overlapped)
{
    if (out)
        memset(out, 0, out_len);
    DWORD done;
    if (DeviceIoControl(device, code, (void *)in, in_len, out, out_len, &done, overlapped))
        return S_OK;
    DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING)
        return overlapped ? (HRESULT)0x8000000A : E_FAIL;
    if (error == ERROR_BAD_COMMAND)
        return (HRESULT)0x80070006;
    return E_FAIL;
}

static WCHAR *copy_path(const WCHAR *path)
{
    SIZE_T bytes = (wcslen(path) + 1) * sizeof(WCHAR);
    WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (copy)
        memcpy(copy, path, bytes);
    return copy;
}

static HANDLE duplicate(HANDLE handle)
{
    HANDLE copy = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return nullptr;
    return copy;
}

/* ------------------------------------------------------------ requests to a pad */

static HRESULT send_led(PAD *p, UCHAR led)
{
    UCHAR in[5] = {p->Sub, led, 0, 0, 0x01};
    return control(p->Handle, IOCTL_XUSB_SET_STATE, in, sizeof(in), nullptr, 0, nullptr);
}

static HRESULT send_vibration(PAD *p, const XINPUT_VIBRATION *v)
{
    UCHAR in[5] = {p->Sub, 0, (UCHAR)(v->wLeftMotorSpeed >> 8), (UCHAR)(v->wRightMotorSpeed >> 8), 0x02};
    return control(p->Handle, IOCTL_XUSB_SET_STATE, in, sizeof(in), nullptr, 0, nullptr);
}

/* GET_GAMEPAD_STATE in the form the driver speaks; the pad's state, connection and headset follow it. */
static HRESULT read_state(PAD *p)
{
    UCHAR out[29];
    HRESULT hr;
    const UCHAR *s;
    if (p->Version == 0x0100) {
        hr = control(p->Handle, IOCTL_XUSB_GET_STATE, &p->Sub, 1, out, 20, nullptr);
        if (FAILED(hr))
            return hr;
        p->Connected = out[0] == 1;
        p->Headset = out[2];
        p->Last.dwPacketNumber = out[3] | out[4] << 8 | out[5] << 16 | (DWORD)out[6] << 24;
        s = out + 8;
    } else {
        UCHAR in[3] = {0x01, 0x01, p->Sub};
        hr = control(p->Handle, IOCTL_XUSB_GET_STATE, in, sizeof(in), out, 29, nullptr);
        if (FAILED(hr))
            return hr;
        p->Connected = out[2] == 1;
        p->Headset = out[4];
        p->Last.dwPacketNumber = out[5] | out[6] << 8 | out[7] << 16 | (DWORD)out[8] << 24;
        s = out + 11;
    }
    WORD buttons = get16(s);
    /* xinput1_1 and 1_2 drop the guide button too. */
    p->Last.Gamepad.wButtons = buttons & (XiRevision < 0x0103 ? 0xF3FF : 0xF7FF);
    p->Last.Gamepad.bLeftTrigger = s[2];
    p->Last.Gamepad.bRightTrigger = s[3];
    p->Last.Gamepad.sThumbLX = (SHORT)get16(s + 4);
    p->Last.Gamepad.sThumbLY = (SHORT)get16(s + 6);
    p->Last.Gamepad.sThumbRX = (SHORT)get16(s + 8);
    p->Last.Gamepad.sThumbRY = (SHORT)get16(s + 10);
    return S_OK;
}

static void forget_bus(LONG index)
{
    if (index < 0 || index >= MAX_BUSES || !g_buses[index].Used)
        return;
    BUS *b = &g_buses[index];
    CloseHandle(b->Handle);
    HeapFree(GetProcessHeap(), 0, b->Path);
    memset(b, 0, sizeof(*b));
    for (ULONG i = 0; i < MAX_PORTS; i++)
        if (g_ports[i].Used && g_ports[i].Bus == index) {
            g_ports[i].Connected = FALSE;
            g_ports[i].Bus = -1;
        }
}

/* a failed request: the pad is gone, and with xinput1_4 every pad of its device. */
static void disconnect(PAD *p)
{
    p->Connected = FALSE;
    if (XiRevision >= 0x0104)
        forget_bus(p->Bus);
}

static void destroy(PAD *p)
{
    if (p->Handle)
        CloseHandle(p->Handle);
    if (p->Overlapped)
        CloseHandle(p->Overlapped);
    HeapFree(GetProcessHeap(), 0, p->Path);
    memset(p, 0, sizeof(*p));
}

/* ------------------------------------------------------------ enumeration */

static LONG remember_bus(const WCHAR *path, HANDLE handle, USHORT version, USHORT vid, USHORT pid)
{
    LONG free = -1;
    for (LONG i = 0; i < MAX_BUSES; i++) {
        if (g_buses[i].Used && !wcscmp(g_buses[i].Path, path))
            return i;
        if (!g_buses[i].Used && free < 0)
            free = i;
    }
    if (free < 0)
        return -1;
    BUS *b = &g_buses[free];
    b->Handle = duplicate(handle);
    b->Path = copy_path(path);
    if (!b->Handle || !b->Path) {
        if (b->Handle)
            CloseHandle(b->Handle);
        HeapFree(GetProcessHeap(), 0, b->Path);
        memset(b, 0, sizeof(*b));
        return -1;
    }
    b->Used = TRUE;
    b->Version = version;
    b->Vid = vid;
    b->Pid = pid;
    return free;
}

/* a pad found: its led says which user index it had, else it takes the first free one. */
static void assign(PAD *found)
{
    if (!found->Connected)
        return;
    for (ULONG i = 0; i < MAX_PORTS; i++) {
        PAD *p = &g_ports[i];
        if (p->Used && !wcscmp(p->Path, found->Path) && (XiRevision == 0x0101 || p->Sub == found->Sub))
            return;
    }
    LONG port = -1;
    if (XiRevision >= 0x0103 && found->Version >= 0x0101) {
        UCHAR in[3] = {0x01, 0x01, found->Sub}, out[3];
        if (SUCCEEDED(control(found->Handle, IOCTL_XUSB_GET_LED_STATE, in, sizeof(in), out, sizeof(out), nullptr)) &&
            out[2] < RTL_NUMBER_OF(LED_TO_PORT) && LED_TO_PORT[out[2]] != 0xFF) {
            port = LED_TO_PORT[out[2]];
            if (g_ports[port].Used) {
                send_led(found, 13);
                port = -1;
            }
        }
    }
    for (LONG i = 0; i < MAX_PORTS && port < 0; i++)
        if (!g_ports[i].Used)
            port = i;
    if (port < 0)
        return;
    PAD *p = &g_ports[port];
    *p = *found;
    p->Handle = duplicate(found->Handle);
    p->Path = copy_path(found->Path);
    if (!p->Handle || !p->Path) {
        destroy(p);
        return;
    }
    p->Used = TRUE;
    if (g_settings & SETTING_LED)
        send_led(p, PORT_TO_LED[port]);
}

/* the path of each xusb interface, in a buffer kept between calls. */
static const WCHAR *interface_path(HDEVINFO set, SP_DEVICE_INTERFACE_DATA *data)
{
    for (;;) {
        DWORD needed = 0;
        if (g_detail)
            g_detail->cbSize = sizeof(*g_detail);
        if (SetupDiGetDeviceInterfaceDetailW(set, data, g_detail, g_detail_size, &needed, nullptr))
            return g_detail->DevicePath;
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !needed)
            return nullptr;
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *bigger = HeapAlloc(GetProcessHeap(), 0, needed);
        if (!bigger)
            return nullptr;
        HeapFree(GetProcessHeap(), 0, g_detail);
        g_detail = bigger;
        g_detail_size = needed;
    }
}

typedef BOOL (*XI_VISIT)(const WCHAR *path, HANDLE device, const UCHAR *info, void *context);

/* every xusb interface that opens and answers GET_INFORMATION, until the visitor is done. */
static void walk(XI_VISIT visit, void *context)
{
    HDEVINFO set = SetupDiGetClassDevsW(&XUSB, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE)
        return;
    SP_DEVICE_INTERFACE_DATA data = {sizeof(data)};
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &XUSB, i, &data); i++) {
        const WCHAR *path = interface_path(set, &data);
        if (!path)
            continue;
        HANDLE device = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (device == INVALID_HANDLE_VALUE)
            continue;
        UCHAR info[12];
        BOOL done = FALSE;
        if (SUCCEEDED(control(device, IOCTL_XUSB_GET_INFORMATION, nullptr, 0, info, sizeof(info), nullptr)) &&
            !(info[4] & 0x80))
            done = visit(path, device, info, context);
        CloseHandle(device);
        if (done)
            break;
    }
    SetupDiDestroyDeviceInfoList(set);
}

static BOOL enumerate_one(const WCHAR *path, HANDLE device, const UCHAR *info, void *context)
{
    USHORT version = get16(info), vid = get16(info + 8), pid = get16(info + 10);
    LONG bus = XiRevision >= 0x0104 ? remember_bus(path, device, version, vid, pid) : -1;
    for (UCHAR sub = 0; sub < info[2]; sub++) {
        PAD found = {};
        found.Handle = device;
        found.Path = (WCHAR *)path;
        found.Sub = sub;
        found.Version = version;
        found.Vid = vid;
        found.Pid = pid;
        found.Bus = bus;
        if (SUCCEEDED(read_state(&found)))
            assign(&found);
    }
    return FALSE;
}

static void enumerate(void)
{
    walk(enumerate_one, nullptr);
}

/* one api call on a port: looks for a pad when it has none, and forgets one that fails, once retrying. */
typedef HRESULT (*XI_OP)(PAD *p, void *context);

static DWORD request(DWORD index, XI_OP op, void *context)
{
    if (!g_ready)
        return (DWORD)E_FAIL;
    lock();
    HRESULT hr = S_FALSE;
    for (int attempt = 0; attempt < 2; attempt++) {
        PAD *p = &g_ports[index];
        if (!p->Used && g_enabled)
            enumerate();
        if (!p->Used) {
            hr = S_FALSE;
            break;
        }
        hr = op(p, context);
        if (hr == XI_UNLOCKED)
            return ERROR_SUCCESS;
        if (FAILED(hr))
            disconnect(p);
        if (p->Connected)
            break;
        destroy(p);
        hr = S_FALSE;
    }
    unlock();
    return code_of(hr);
}

/* ------------------------------------------------------------ state and vibration */

typedef struct STATE_CALL {
    XINPUT_STATE *State;
} STATE_CALL;

static HRESULT op_state(PAD *p, void *context)
{
    STATE_CALL *c = context;
    if (!g_enabled) {
        /* disabled: neutral input, the packet one past the last real one. */
        memset(c->State, 0, sizeof(*c->State));
        c->State->dwPacketNumber = p->Last.dwPacketNumber + 1;
        return S_OK;
    }
    HRESULT hr = read_state(p);
    if (SUCCEEDED(hr))
        *c->State = p->Last;
    return hr;
}

DWORD XiGetState(DWORD index, XINPUT_STATE *state, BOOL guide)
{
    if (index >= XUSER_MAX_COUNT || !state)
        return ERROR_BAD_ARGUMENTS;
    STATE_CALL c = {state};
    DWORD result = request(index, op_state, &c);
    if (result == ERROR_SUCCESS && !guide)
        state->Gamepad.wButtons &= ~GUIDE;
    return result;
}

static HRESULT op_vibration(PAD *p, void *context)
{
    p->Vibration = *(XINPUT_VIBRATION *)context;
    return g_enabled ? send_vibration(p, &p->Vibration) : S_OK;
}

DWORD XiSetState(DWORD index, XINPUT_VIBRATION *vibration)
{
    if (index >= XUSER_MAX_COUNT || !vibration)
        return ERROR_BAD_ARGUMENTS;
    return request(index, op_vibration, vibration);
}

/* no lock: the dispatchers swap, and pads already known get their motors stopped or restarted. */
void XiEnable(BOOL enable)
{
    enable = enable != FALSE;
    if (enable == g_enabled)
        return;
    g_enabled = enable;
    static const XINPUT_VIBRATION STILL = {};
    for (ULONG i = 0; i < MAX_PORTS; i++)
        if (g_ports[i].Used)
            send_vibration(&g_ports[i], enable ? &g_ports[i].Vibration : &STILL);
}

/* ------------------------------------------------------------ capabilities */

typedef struct CAPS_CALL {
    XINPUT_CAPABILITIES_EX *Caps;
} CAPS_CALL;

static void caps_gamepad(XINPUT_GAMEPAD *g, const UCHAR *s)
{
    g->wButtons = get16(s);
    g->bLeftTrigger = s[2];
    g->bRightTrigger = s[3];
    g->sThumbLX = (SHORT)get16(s + 4);
    g->sThumbLY = (SHORT)get16(s + 6);
    g->sThumbRX = (SHORT)get16(s + 8);
    g->sThumbRY = (SHORT)get16(s + 10);
}

static HRESULT op_caps(PAD *p, void *context)
{
    XINPUT_CAPABILITIES_EX *x = ((CAPS_CALL *)context)->Caps;
    XINPUT_CAPABILITIES *c = &x->Capabilities;
    memset(x, 0, sizeof(*x));
    if (p->Version == 0x0100) {
        /* the first driver has no caps request. */
        *c = (XINPUT_CAPABILITIES){XINPUT_DEVTYPE_GAMEPAD, XINPUT_DEVSUBTYPE_GAMEPAD, XINPUT_CAPS_VOICE_SUPPORTED,
                                   {0xF3FF, 1, 1, 1, 1, 1, 1}, {1, 1}};
        x->VendorId = p->Vid;
        x->ProductId = p->Pid;
        return S_OK;
    }
    UCHAR out[36];
    HRESULT hr;
    if (XiRevision >= 0x0104 && p->Version >= 0x0103) {
        UCHAR in[3] = {0x02, 0x01, p->Sub};
        hr = control(p->Handle, IOCTL_XUSB_GET_CAPABILITIES, in, sizeof(in), out, 36, nullptr);
        if (FAILED(hr))
            return hr;
        c->Type = out[2];
        c->SubType = out[3];
        c->Flags = out[4] & 0x1F;
        x->VendorId = get16(out + 6);
        x->ProductId = get16(out + 8);
        x->VersionNumber = get16(out + 10);
        x->unk2 = out[12] | out[13] << 8 | out[14] << 16 | (DWORD)out[15] << 24;
        caps_gamepad(&c->Gamepad, out + 16);
        c->Vibration.wLeftMotorSpeed = out[34];
        c->Vibration.wRightMotorSpeed = out[35];
    } else {
        UCHAR in[3] = {0x01, 0x01, p->Sub};
        hr = control(p->Handle, IOCTL_XUSB_GET_CAPABILITIES, in, sizeof(in), out, 24, nullptr);
        if (FAILED(hr))
            return hr;
        /* xinput1_4 calls an unset type a gamepad; xinput1_3 and older copy it. */
        c->Type = out[2] || XiRevision < 0x0104 ? out[2] : XINPUT_DEVTYPE_GAMEPAD;
        c->SubType = out[3];
        c->Flags = XiRevision >= 0x0104 ? 0x000C : XiRevision == 0x0103 ? XINPUT_CAPS_VOICE_SUPPORTED : 0;
        caps_gamepad(&c->Gamepad, out + 4);
        c->Vibration.wLeftMotorSpeed = out[22];
        c->Vibration.wRightMotorSpeed = out[23];
        if (XiRevision >= 0x0104 && p->Vid == 0x045E && p->Pid == 0x0719)
            c->Flags = 0x000E;
        else {
            x->VendorId = p->Vid;
            x->ProductId = p->Pid;
        }
    }
    c->Gamepad.wButtons &= 0xF7FF;
    return S_OK;
}

DWORD XiGetCapabilitiesEx(DWORD one, DWORD index, DWORD flags, XINPUT_CAPABILITIES_EX *caps)
{
    if (index >= XUSER_MAX_COUNT || flags >= 2 || !caps)
        return ERROR_BAD_ARGUMENTS;
    if (one != 1)
        return ERROR_INVALID_DATA;
    CAPS_CALL c = {caps};
    DWORD result = request(index, op_caps, &c);
    if (result == ERROR_SUCCESS)
        caps->Capabilities.Gamepad.wButtons &= ~GUIDE;
    return result;
}

DWORD XiGetCapabilities(DWORD index, DWORD flags, XINPUT_CAPABILITIES *caps)
{
    if (!caps)
        return ERROR_BAD_ARGUMENTS;
    XINPUT_CAPABILITIES_EX ex;
    DWORD result = XiGetCapabilitiesEx(1, index, flags, &ex);
    if (result == ERROR_SUCCESS)
        *caps = ex.Capabilities;
    return result;
}

/* ------------------------------------------------------------ battery, audio and power */

typedef struct DEVICE_CALL {
    DWORD Index;
    BYTE Type;
    void *Out;
    BOOL Found;
} DEVICE_CALL;

static BOOL battery_any(const WCHAR *path, HANDLE device, const UCHAR *info, void *context)
{
    DEVICE_CALL *c = context;
    if (get16(info) < 0x0102)
        return FALSE;
    UCHAR in[4] = {0x02, 0x01, (UCHAR)c->Index, 1}, out[4];
    if (FAILED(control(device, IOCTL_XUSB_GET_BATTERY_INFORMATION, in, sizeof(in), out, sizeof(out), nullptr)))
        return FALSE;
    XINPUT_BATTERY_INFORMATION *b = c->Out;
    b->BatteryType = out[2];
    b->BatteryLevel = out[3];
    c->Found = TRUE;
    return TRUE;
}

static HRESULT op_battery(PAD *p, void *context)
{
    DEVICE_CALL *c = context;
    XINPUT_BATTERY_INFORMATION *b = c->Out;
    if (p->Version >= 0x0102) {
        UCHAR in[4] = {0x02, 0x01, p->Sub, c->Type}, out[4];
        HRESULT hr = control(p->Handle, IOCTL_XUSB_GET_BATTERY_INFORMATION, in, sizeof(in), out, sizeof(out), nullptr);
        if (SUCCEEDED(hr)) {
            b->BatteryType = out[2];
            b->BatteryLevel = out[3];
        }
        return hr;
    }
    if (c->Type == BATTERY_DEVTYPE_GAMEPAD) {
        b->BatteryType = 1;
        b->BatteryLevel = 3;
    }
    return S_OK;
}

DWORD XiGetBatteryInformation(DWORD index, BYTE type, XINPUT_BATTERY_INFORMATION *battery)
{
    if (index >= XUSER_MAX_COUNT || !battery)
        return ERROR_BAD_ARGUMENTS;
    battery->BatteryType = battery->BatteryLevel = 0;
    DEVICE_CALL c = {index, type, battery, FALSE};
    if (type == BATTERY_DEVTYPE_HEADSET) {
        /* headsets are found by their own port across every device; xinput1_3 then reports no pad anyway. */
        lock();
        walk(battery_any, &c);
        unlock();
        if (c.Found)
            return XiRevision >= 0x0104 ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
    }
    return request(index, op_battery, &c);
}

static HRESULT op_power(PAD *p, void *context)
{
    if (p->Version < 0x0102)
        return E_FAIL;
    UCHAR in[3] = {0x02, 0x01, p->Sub};
    return control(p->Handle, IOCTL_XUSB_POWER_DOWN, in, sizeof(in), nullptr, 0, nullptr);
}

DWORD XiPowerOffController(DWORD index)
{
    if (index >= XUSER_MAX_COUNT)
        return ERROR_BAD_ARGUMENTS;
    return request(index, op_power, nullptr);
}

/* a headset's directsound devices, found among dsound's private enumeration by their usb path. */
typedef struct HEADSET_MATCH {
    WCHAR Prefix[64];
    GUID *Render, *Capture;
} HEADSET_MATCH;

static BOOL CALLBACK match_headset(PDSPROPERTY_DIRECTSOUNDDEVICE_DESCRIPTION_W_DATA d, void *context)
{
    HEADSET_MATCH *m = context;
    if (d->Type != DIRECTSOUNDDEVICE_TYPE_WDM || !d->Interface || wcsncmp(d->Interface, m->Prefix, wcslen(m->Prefix)))
        return TRUE;
    if (d->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_RENDER)
        *m->Render = d->DeviceId;
    else if (d->DataFlow == DIRECTSOUNDDEVICE_DATAFLOW_CAPTURE)
        *m->Capture = d->DeviceId;
    return TRUE;
}

static HRESULT dsound_guids(USHORT vid, USHORT pid, UCHAR headset, GUID *render, GUID *capture)
{
    HMODULE dsound = LoadLibraryW(L"dsound.dll");
    if (!dsound)
        return E_FAIL;
    HRESULT(WINAPI * get_class)(REFCLSID, REFIID, void **) = (void *)GetProcAddress(dsound, "DllGetClassObject");
    HRESULT hr = E_FAIL;
    IClassFactory *factory = nullptr;
    IKsPropertySet *props = nullptr;
    if (get_class && SUCCEEDED(get_class(&CLSID_DirectSoundPrivate, &IID_IClassFactory, (void **)&factory)) &&
        SUCCEEDED(factory->lpVtbl->CreateInstance(factory, nullptr, &IID_IKsPropertySet, (void **)&props))) {
        HEADSET_MATCH m = {.Render = render, .Capture = capture};
        wsprintfW(m.Prefix, L"\\\\?\\USB#VID_%04X&PID_%04X&IA_%02X", vid, pid, headset);
        DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W_DATA data = {match_headset, &m};
        ULONG returned;
        hr = props->lpVtbl->Get(props, &DSPROPSETID_DirectSoundDevice, DSPROPERTY_DIRECTSOUNDDEVICE_ENUMERATE_W,
                                nullptr, 0, &data, sizeof(data), &returned);
    }
    if (props)
        props->lpVtbl->Release(props);
    if (factory)
        factory->lpVtbl->Release(factory);
    FreeLibrary(dsound);
    return SUCCEEDED(hr) ? hr : E_FAIL;
}

typedef struct GUIDS_CALL {
    DWORD Index;
    GUID *Render, *Capture;
} GUIDS_CALL;

static BOOL guids_any(const WCHAR *path, HANDLE device, const UCHAR *info, void *context)
{
    GUIDS_CALL *c = context;
    if (get16(info) < 0x0102)
        return FALSE;
    UCHAR in[3] = {0x02, 0x01, (UCHAR)c->Index}, out[7];
    if (FAILED(control(device, IOCTL_XUSB_GET_AUDIO_INFORMATION, in, sizeof(in), out, sizeof(out), nullptr)))
        return FALSE;
    return SUCCEEDED(dsound_guids(get16(out + 2), get16(out + 4), out[6], c->Render, c->Capture));
}

static HRESULT op_guids(PAD *p, void *context)
{
    GUIDS_CALL *c = context;
    if (p->Version < 0x0102) {
        HRESULT hr = read_state(p);
        if (FAILED(hr) || p->Headset == 0xFF)
            return hr;
        return dsound_guids(p->Vid, p->Pid, p->Headset, c->Render, c->Capture);
    }
    UCHAR in[3] = {0x02, 0x01, p->Sub}, out[7];
    HRESULT hr = control(p->Handle, IOCTL_XUSB_GET_AUDIO_INFORMATION, in, sizeof(in), out, sizeof(out), nullptr);
    if (FAILED(hr))
        return E_FAIL;
    return dsound_guids(get16(out + 2), get16(out + 4), out[6], c->Render, c->Capture);
}

DWORD XiGetDSoundAudioDeviceGuids(DWORD index, GUID *render, GUID *capture)
{
    if (index >= XUSER_MAX_COUNT || !render || !capture)
        return ERROR_BAD_ARGUMENTS;
    *render = *capture = GUID_NULL;
    GUIDS_CALL c = {index, render, capture};
    if (XiRevision >= 0x0103) {
        BOOL found;
        lock();
        found = FALSE;
        /* the first device whose headset port answers wins. */
        walk(guids_any, &c);
        found = !IsEqualGUID(render, &GUID_NULL) || !IsEqualGUID(capture, &GUID_NULL);
        unlock();
        if (found)
            return ERROR_SUCCESS;
    }
    return request(index, op_guids, &c);
}

typedef struct IDS_CALL {
    DWORD Index;
    BOOL Found;
} IDS_CALL;

static BOOL ids_any(const WCHAR *path, HANDLE device, const UCHAR *info, void *context)
{
    IDS_CALL *c = context;
    if (get16(info) < 0x0102)
        return FALSE;
    UCHAR in[3] = {0x02, 0x01, (UCHAR)c->Index}, out[7];
    if (FAILED(control(device, IOCTL_XUSB_GET_AUDIO_INFORMATION, in, sizeof(in), out, sizeof(out), nullptr)))
        return FALSE;
    /* a headset would be looked up among the audio endpoints of its container; mime's pads have none. */
    c->Found = TRUE;
    return TRUE;
}

static HRESULT op_ids(PAD *p, void *context)
{
    if (p->Version < 0x0102) {
        HRESULT hr = read_state(p);
        return FAILED(hr) || p->Headset == 0xFF ? hr : E_FAIL;
    }
    UCHAR in[3] = {0x02, 0x01, p->Sub}, out[7];
    if (FAILED(control(p->Handle, IOCTL_XUSB_GET_AUDIO_INFORMATION, in, sizeof(in), out, sizeof(out), nullptr)))
        return E_FAIL;
    if (p->Headset == 0xFF)
        return E_FAIL;
    return out[6] == 0xFF ? S_OK : E_FAIL;
}

DWORD XiGetAudioDeviceIds(DWORD index, LPWSTR render, UINT *render_count, LPWSTR capture, UINT *capture_count)
{
    if (index >= XUSER_MAX_COUNT || (!render_count && !capture_count) || (render && !render_count) ||
        (capture && !capture_count))
        return ERROR_BAD_ARGUMENTS;
    if (render_count) {
        if (render)
            render[0] = 0;
        else
            *render_count = 0;
    }
    if (capture_count) {
        if (capture)
            capture[0] = 0;
        else
            *capture_count = 0;
    }
    IDS_CALL c = {index, FALSE};
    lock();
    walk(ids_any, &c);
    unlock();
    if (c.Found)
        return ERROR_SUCCESS;
    return request(index, op_ids, &c);
}

/* ------------------------------------------------------------ keystrokes */

static const WORD BUTTON_VK[16] = {0x5810, 0x5811, 0x5812, 0x5813, 0x5814, 0x5815, 0x5816, 0x5817,
                                   0x5805, 0x5804, 0x5805, 0x5806, 0x5800, 0x5801, 0x5802, 0x5803};

/* the eight directions of a stick past 20000, from base: up, down, right, left, then the diagonals. */
static WORD stick_vk(SHORT x, SHORT y, WORD base)
{
    if (y > 20000)
        return x < -20000 ? base + 4 : x > 20000 ? base + 5 : base;
    if (y < -20000)
        return x > 20000 ? base + 6 : x < -20000 ? base + 7 : base + 1;
    if (x > 20000)
        return base + 2;
    if (x < -20000)
        return base + 3;
    return 0;
}

static DWORD report_key(XINPUT_KEYSTROKE *k, WORD vk, WORD flags)
{
    k->VirtualKey = vk;
    k->Flags = flags;
    return ERROR_SUCCESS;
}

/* one stick: a changed direction ends the old one first. */
static BOOL stick_step(WORD *held, WORD now, WORD *new_key, WORD *candidate, XINPUT_KEYSTROKE *k, DWORD *result)
{
    if (now != *held) {
        if (*held) {
            *result = report_key(k, *held, XINPUT_KEYSTROKE_KEYUP);
            *held = 0;
            g_repeat_vk = 0;
            return TRUE;
        }
        *held = now;
        *new_key = now;
    } else if (now) {
        *candidate = now;
    }
    return FALSE;
}

static DWORD keystroke(PAD *p, const XINPUT_GAMEPAD *g, XINPUT_KEYSTROKE *k)
{
    WORD new_key = 0, candidate = 0;
    DWORD result;
    if (stick_step(&p->LeftVk, stick_vk(g->sThumbLX, g->sThumbLY, 0x5820), &new_key, &candidate, k, &result) ||
        stick_step(&p->RightVk, stick_vk(g->sThumbRX, g->sThumbRY, 0x5830), &new_key, &candidate, k, &result))
        return result;
    UCHAR triggers = (g->bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 1 : 0) |
                     (g->bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? 2 : 0);
    if (triggers == 3)
        triggers = 2;
    for (ULONG t = 0; t < 2; t++) {
        UCHAR bit = (UCHAR)(1u << t);
        WORD vk = (WORD)(0x5806 + t);
        if ((p->HeldTriggers & bit) && !(triggers & bit)) {
            p->HeldTriggers &= (UCHAR)~bit;
            return report_key(k, vk, XINPUT_KEYSTROKE_KEYUP);
        }
        if (triggers & bit) {
            if (!(p->HeldTriggers & bit))
                new_key = vk;
            else
                candidate = vk;
            p->HeldTriggers |= bit;
        }
    }
    WORD buttons = g->wButtons & ~GUIDE;
    for (ULONG b = 0; b < 16; b++) {
        WORD bit = (WORD)(1u << b);
        if ((p->HeldButtons & bit) && !(buttons & bit)) {
            p->HeldButtons &= (WORD)~bit;
            return report_key(k, BUTTON_VK[b], XINPUT_KEYSTROKE_KEYUP);
        }
        if (buttons & bit) {
            if (!(p->HeldButtons & bit))
                new_key = BUTTON_VK[b];
            else
                candidate = BUTTON_VK[b];
            p->HeldButtons |= bit;
        }
    }
    DWORD now = GetTickCount();
    if (new_key) {
        g_repeat_vk = new_key;
        g_repeat_time = now;
        g_repeat_first = TRUE;
        return report_key(k, new_key, XINPUT_KEYSTROKE_KEYDOWN);
    }
    if (candidate && candidate == g_repeat_vk && now - g_repeat_time >= (g_repeat_first ? 400u : 100u)) {
        g_repeat_time = now;
        g_repeat_first = FALSE;
        return report_key(k, candidate, XINPUT_KEYSTROKE_KEYDOWN | XINPUT_KEYSTROKE_REPEAT);
    }
    return ERROR_EMPTY;
}

typedef struct KEY_CALL {
    XINPUT_KEYSTROKE *Key;
    DWORD Result;
} KEY_CALL;

static HRESULT op_keystroke(PAD *p, void *context)
{
    KEY_CALL *c = context;
    XINPUT_STATE state;
    STATE_CALL s = {&state};
    HRESULT hr = op_state(p, &s);
    if (FAILED(hr)) {
        c->Result = ERROR_EMPTY;
        return hr;
    }
    c->Key->Unicode = 0;
    c->Result = keystroke(p, &state.Gamepad, c->Key);
    return S_OK;
}

DWORD XiGetKeystroke(DWORD index, DWORD reserved, XINPUT_KEYSTROKE *key)
{
    if ((index >= XUSER_MAX_COUNT && index != XUSER_INDEX_ANY) || !key)
        return ERROR_BAD_ARGUMENTS;
    KEY_CALL c = {key, ERROR_EMPTY};
    if (index != XUSER_INDEX_ANY) {
        DWORD result = request(index, op_keystroke, &c);
        if (result == ERROR_DEVICE_NOT_CONNECTED && XiRevision == 0x0103) {
            /* xinput1_3 takes the empty port for success and writes the index alone. */
            key->UserIndex = (BYTE)index;
            return ERROR_SUCCESS;
        }
        if (result != ERROR_SUCCESS)
            return result;
        if (c.Result == ERROR_SUCCESS)
            key->UserIndex = (BYTE)index;
        return c.Result;
    }
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
        c.Result = ERROR_EMPTY;
        if (request(i, op_keystroke, &c) == ERROR_SUCCESS && c.Result == ERROR_SUCCESS) {
            key->UserIndex = (BYTE)i;
            return ERROR_SUCCESS;
        }
    }
    return ERROR_EMPTY;
}

/* ------------------------------------------------------------ the guide button */

typedef struct GUIDE_WAIT {
    HANDLE Device;
    UCHAR Sub;
    XINPUT_LISTEN_STATE *Listen;
    HANDLE Event;
} GUIDE_WAIT;

/* the wait on the overlapped handle; closing that handle is what cancels it. */
static void wait_guide(GUIDE_WAIT *w)
{
    UCHAR in[3] = {0x02, 0x01, w->Sub}, out[29];
    memset(out, 0, sizeof(out));
    out[2] = 5;
    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD done = 0, status;
    BOOL ok = DeviceIoControl(w->Device, IOCTL_XUSB_WAIT_GUIDE_BUTTON, in, sizeof(in), out, sizeof(out), nullptr,
                              &overlapped);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
        ok = GetOverlappedResult(w->Device, &overlapped, &done, TRUE);
    else if (ok)
        done = sizeof(out);
    if (!ok)
        status = GetLastError() == ERROR_OPERATION_ABORTED ? ERROR_CANCELLED : GetLastError();
    else if (done != sizeof(out))
        status = ERROR_CANCELLED;
    else if (out[2] != 1)
        status = ERROR_DEVICE_NOT_CONNECTED;
    else {
        status = ERROR_SUCCESS;
        XINPUT_STATE *s = &w->Listen->State;
        s->dwPacketNumber = out[5] | out[6] << 8 | out[7] << 16 | (DWORD)out[8] << 24;
        caps_gamepad(&s->Gamepad, out + 11);
    }
    w->Listen->Status = status;
    if (overlapped.hEvent)
        CloseHandle(overlapped.hEvent);
}

static DWORD WINAPI guide_thread(void *context)
{
    GUIDE_WAIT *w = context;
    wait_guide(w);
    SetEvent(w->Event);
    HeapFree(GetProcessHeap(), 0, w);
    return 0;
}

typedef struct LISTEN_CALL {
    HANDLE Event;
    XINPUT_LISTEN_STATE *Listen;
} LISTEN_CALL;

static HRESULT op_listen(PAD *p, void *context)
{
    LISTEN_CALL *c = context;
    if (!p->Connected) {
        c->Listen->Status = ERROR_DEVICE_NOT_CONNECTED;
        return E_FAIL;
    }
    if (!p->Overlapped) {
        p->Overlapped = CreateFileW(p->Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_ATTRIBUTE_NORMAL, nullptr);
        if (p->Overlapped == INVALID_HANDLE_VALUE) {
            p->Overlapped = nullptr;
            return E_FAIL;
        }
    }
    GUIDE_WAIT w = {p->Overlapped, p->Sub, c->Listen, c->Event};
    if (!c->Event) {
        unlock();
        wait_guide(&w);
        return XI_UNLOCKED;
    }
    GUIDE_WAIT *copy = HeapAlloc(GetProcessHeap(), 0, sizeof(*copy));
    if (!copy)
        return E_OUTOFMEMORY;
    *copy = w;
    c->Listen->Status = ERROR_IO_PENDING;
    HANDLE thread = CreateThread(nullptr, 0, guide_thread, copy, 0, nullptr);
    if (!thread) {
        HeapFree(GetProcessHeap(), 0, copy);
        return E_FAIL;
    }
    CloseHandle(thread);
    return S_OK;
}

DWORD XiWaitForGuideButton(DWORD index, HANDLE event, XINPUT_LISTEN_STATE *listen)
{
    if (index >= XUSER_MAX_COUNT || !listen)
        return ERROR_BAD_ARGUMENTS;
    LISTEN_CALL c = {event == INVALID_HANDLE_VALUE ? nullptr : event, listen};
    return request(index, op_listen, &c);
}

static HRESULT op_cancel_guide(PAD *p, void *context)
{
    if (p->Overlapped) {
        CloseHandle(p->Overlapped);
        p->Overlapped = nullptr;
    }
    return S_OK;
}

DWORD XiCancelGuideButtonWait(DWORD index)
{
    if (index >= XUSER_MAX_COUNT)
        return ERROR_BAD_ARGUMENTS;
    return request(index, op_cancel_guide, nullptr);
}

/* ------------------------------------------------------------ xinput1_4's extras */

DWORD XiGetBaseBusInformation(DWORD index, XINPUT_BASE_BUS_INFORMATION *info)
{
    if (index >= MAX_BUSES || !info)
        return ERROR_BAD_ARGUMENTS;
    if (!g_ready)
        return (DWORD)E_FAIL;
    lock();
    BUS *b = &g_buses[index];
    DWORD result = ERROR_SUCCESS;
    memset(info, 0, sizeof(*info));
    if (!b->Used)
        result = ERROR_DEVICE_NOT_CONNECTED;
    else {
        info->VendorId = b->Vid;
        info->ProductId = b->Pid;
        if (b->Version >= 0x0103) {
            UCHAR in[36] = {0x04, 0x01, 2}, out[949];
            HRESULT hr = control(b->Handle, IOCTL_XUSB_GET_INFORMATION_EX, in, sizeof(in), out, sizeof(out), nullptr);
            if (FAILED(hr)) {
                forget_bus((LONG)index);
                result = code_of(hr);
            } else if (out[2]) {
                result = ERROR_DEVICE_NOT_CONNECTED;
            } else {
                info->VendorId = get16(out + 23);
                info->ProductId = get16(out + 25);
                info->VersionNumber = get16(out + 27);
                if (out[29]) {
                    info->BindId = (DWORD)out[30] << 24 | out[31] << 16 | out[32] << 8 | out[33];
                    memcpy(info->Bind, out + 34, 3);
                }
            }
        }
    }
    unlock();
    return result;
}

static HRESULT op_system_buttons(PAD *p, void *context)
{
    /* filled only by a staged feature that is off. */
    memset(context, 0, 32);
    return p->Connected ? S_OK : E_FAIL;
}

DWORD XiGetSystemButtons(DWORD index, void *out)
{
    if (index >= XUSER_MAX_COUNT || !out)
        return ERROR_BAD_ARGUMENTS;
    return request(index, op_system_buttons, out);
}

/* ------------------------------------------------------------ load and unload */

void XiAttach(void)
{
    RtlInitializeCriticalSection(&g_lock);
    g_enabled = TRUE;
    g_ready = TRUE;
}

void XiDetach(void)
{
    g_ready = FALSE;
    XiEnable(FALSE);
    static const XINPUT_VIBRATION STILL = {};
    for (ULONG i = 0; i < MAX_PORTS; i++) {
        PAD *p = &g_ports[i];
        if (!p->Used)
            continue;
        if (g_settings & SETTING_LED_OFF)
            send_led(p, 0);
        if (g_settings & SETTING_STOP_RUMBLE)
            send_vibration(p, &STILL);
        destroy(p);
    }
    for (LONG i = 0; i < MAX_BUSES; i++)
        forget_bus(i);
    HeapFree(GetProcessHeap(), 0, g_detail);
    g_detail = nullptr;
    RtlDeleteCriticalSection(&g_lock);
}
