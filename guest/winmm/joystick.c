/*
 * the legacy joystick api, as windows' winmm builds it on directinput 5:
 * dinput.dll's joystick configuration names the device of an id, which is
 * opened once and read through a private data format whose slots winmm
 * assigns from the device's objects. records unused for two to three
 * minutes are closed by a monitor thread. see docs/winmm.md.
 */
#define MIME_BUILD_WINMM
#include "winmm_private.h"
#include <mime/win32/dinput.h>
#include <mime/win32/dinputd.h>

#define MAX_JOYSTICKS 16
#define SLOTS 39
#define CANDIDATES 46
#define STATE_SIZE 60
#define DEAD_ZONE 500
#define WINMM_KEY L"System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\Winmm"

typedef HRESULT(WINAPI *DIRECTINPUTCREATEW)(HINSTANCE, DWORD, IDirectInputW **, IUnknown *);

typedef struct JOYSTICK {
    IDirectInputDevice2W *device;
    DWORD di_buttons;
    DWORD mode;      /* JOY_RETURNRAWDATA and JOY_USEDEADZONE as programmed */
    HWND capture;
    UINT period;
    UINT threshold;
    UINT_PTR timer;
    BOOL changed;
    DWORD capture_index;
    DWORD idle;
    JOYCAPSW caps;
} JOYSTICK;

HINSTANCE WinmmModule;
static RTL_CRITICAL_SECTION g_lock = {nullptr, -1, 0, nullptr, nullptr, 0};
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static JOYSTICK *g_joysticks[MAX_JOYSTICKS];
static HMODULE g_dinput;
static DIRECTINPUTCREATEW g_create;
static IDirectInputW *g_di;
static IDirectInputJoyConfig *g_config;
static UINT g_changed_message;
static LONG g_captures;
static JOYINFO g_previous[2];
static BOOL g_monitor;
static HANDLE g_event;

/* the first call of a process: the message, and the wheel option windows creates in the registry. */
static BOOL CALLBACK init_once(INIT_ONCE *once, void *parameter, void **context)
{
    g_changed_message = RegisterWindowMessageW(L"MSJSTICK_VJOYD_MSGSTR");
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, WINMM_KEY, 0, KEY_READ | KEY_WRITE, &key) &&
        RegCreateKeyExW(HKEY_CURRENT_USER, WINMM_KEY, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr))
        key = nullptr;
    if (key) {
        DWORD wheel, size = sizeof(wheel);
        if (RegQueryValueExW(key, L"wheel", nullptr, nullptr, (BYTE *)&wheel, &size)) {
            wheel = 1;
            RegSetValueExW(key, L"wheel", 0, REG_DWORD, (const BYTE *)&wheel, sizeof(wheel));
        }
        RegCloseKey(key);
    }
    if (!g_event && !(g_event = OpenEventW(SYNCHRONIZE, FALSE, L"DINPUTWINMM")))
        g_event = CreateEventW(nullptr, TRUE, FALSE, L"DINPUTWINMM");
    return TRUE;
}

static BOOL init(void)
{
    return InitOnceExecuteOnce(&g_once, init_once, nullptr, nullptr);
}

/* ------------------------------------------------------------------ the data format */

static DIOBJECTDATAFORMAT g_slots[SLOTS];
static const DIDATAFORMAT g_format = {sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_ABSAXIS, STATE_SIZE, SLOTS,
                                      g_slots};

typedef struct CANDIDATE {
    GUID guid;
    DWORD type, flags;
    BOOL present;
} CANDIDATE;

static CANDIDATE g_candidates[CANDIDATES];

enum {
    C_X, C_Y, C_Z, C_RX, C_RY, C_RZ, C_THROTTLE, C_STEERING, C_ACCELERATOR, C_SLIDER, C_DIAL, C_RUDDER, C_BRAKE, C_POV,
};

static void reset_format(void)
{
    for (int i = 0; i < SLOTS; i++)
        g_slots[i] = (DIOBJECTDATAFORMAT){nullptr, (DWORD)(i < 7 ? i * 4 : 28 + (i - 7)),
                                          i < 7 ? DIDFT_OPTIONAL : 0x80FFFF0C, 0};
    memset(g_candidates, 0, sizeof(g_candidates));
}

/* windows' categories of the objects dinput.dll shows an old version: usages first, then guids. */
static BOOL CALLBACK classify(LPCDIDEVICEOBJECTINSTANCEW o, void *context)
{
    if (o->dwSize < 28)
        return DIENUM_CONTINUE;
    int index = -1;
    if (o->wUsagePage == 1 && o->wUsage == 0x37)
        index = C_DIAL;
    else if (o->wUsagePage == 2)
        index = o->wUsage == 0xBA ? C_RUDDER : o->wUsage == 0xBB ? C_THROTTLE : o->wUsage == 0xC4 ? C_ACCELERATOR
              : o->wUsage == 0xC5 ? C_BRAKE : o->wUsage == 0xC8 ? C_STEERING : -1;
    else if (IsEqualGUID(&o->guidType, &GUID_Button))
        index = o->wUsage < 0x21 ? C_POV + o->wUsage : -1;
    else if (IsEqualGUID(&o->guidType, &GUID_XAxis))
        index = C_X;
    else if (IsEqualGUID(&o->guidType, &GUID_YAxis))
        index = C_Y;
    else if (IsEqualGUID(&o->guidType, &GUID_ZAxis))
        index = C_Z;
    else if (IsEqualGUID(&o->guidType, &GUID_RxAxis))
        index = C_RX;
    else if (IsEqualGUID(&o->guidType, &GUID_RyAxis))
        index = C_RY;
    else if (IsEqualGUID(&o->guidType, &GUID_RzAxis))
        index = C_RZ;
    else if (IsEqualGUID(&o->guidType, &GUID_Slider))
        index = C_SLIDER;
    else if (IsEqualGUID(&o->guidType, &GUID_POV))
        index = C_POV;
    if (index < 0)
        return DIENUM_CONTINUE;
    if (index <= C_RZ && !(o->dwType & DIDFT_AXIS))
        return DIENUM_CONTINUE;
    CANDIDATE *c = &g_candidates[index];
    /* axes and the pov keep the first, buttons the last. */
    if (index <= C_POV && c->present)
        return DIENUM_CONTINUE;
    *c = (CANDIDATE){o->guidType, o->dwType, o->dwFlags, TRUE};
    return DIENUM_CONTINUE;
}

static void assign(int slot, int candidate)
{
    CANDIDATE *c = &g_candidates[candidate];
    g_slots[slot].pguid = &c->guid;
    g_slots[slot].dwType = c->type;
    g_slots[slot].dwFlags = c->flags;
}

/* windows' AssignMappings: the slots each axis may take, in order, and the caps they give. */
static void assign_slots(JOYCAPSW *caps)
{
    BOOL used[CANDIDATES] = {};
    UINT axes = 0, buttons = 0, flags = 0;
    static const int X[] = {C_X, C_STEERING, C_RY}, Y[] = {C_Y, C_ACCELERATOR, C_RX};
    static const int Z[] = {C_Z, C_THROTTLE, C_SLIDER, C_DIAL};
    for (int i = 0; i < 3; i++)
        if (g_candidates[X[i]].present) {
            assign(0, X[i]);
            used[X[i]] = TRUE;
            axes++;
            break;
        }
    for (int i = 0; i < 3; i++)
        if (g_candidates[Y[i]].present) {
            assign(1, Y[i]);
            used[Y[i]] = TRUE;
            axes++;
            break;
        }
    for (int i = 0; i < 4; i++)
        if (g_candidates[Z[i]].present && !used[Z[i]]) {
            assign(2, Z[i]);
            used[Z[i]] = TRUE;
            flags |= JOYCAPS_HASZ;
            axes++;
            break;
        }
    for (int k = 0; k < 3; k++) {
        if (k == 0) {
            static const int R[] = {C_RUDDER, C_RZ, C_BRAKE};
            BOOL found = FALSE;
            for (int i = 0; i < 3 && !found; i++)
                if (g_candidates[R[i]].present) {
                    assign(3, R[i]);
                    flags |= JOYCAPS_HASR;
                    axes++;
                    found = TRUE;
                }
            if (found)
                continue;
        }
        if (k < 2) {
            static const int REST[] = {C_THROTTLE, C_SLIDER, C_DIAL, C_RY, C_RX};
            for (int i = 0; i < 5; i++)
                if (g_candidates[REST[i]].present && !used[REST[i]]) {
                    assign(3 + k, REST[i]);
                    used[REST[i]] = TRUE;
                    flags |= k == 0 ? JOYCAPS_HASR : JOYCAPS_HASU;
                    axes++;
                    break;
                }
        } else if (g_candidates[C_RX].present && !used[C_RX]) {
            assign(5, C_RX);
            used[C_RX] = TRUE;
            flags |= JOYCAPS_HASV;
            axes++;
        }
    }
    if (g_candidates[C_POV].present) {
        assign(6, C_POV);
        flags |= JOYCAPS_HASPOV;
    }
    for (int n = 1; n <= 32; n++)
        if (g_candidates[C_POV + n].present) {
            assign(6 + n, C_POV + n);
            buttons++;
        }
    caps->wNumAxes = axes;
    caps->wNumButtons = buttons;
    caps->wCaps = flags;
}

/* ------------------------------------------------------------------ opening */

static DWORD WINAPI monitor(void *context);

static void close_joystick(UINT id)
{
    JOYSTICK *j = id < MAX_JOYSTICKS ? g_joysticks[id] : nullptr;
    if (!j)
        return;
    if (!g_event || WaitForSingleObject(g_event, 10) != WAIT_OBJECT_0) {
        j->device->lpVtbl->Unacquire(j->device);
        j->device->lpVtbl->Release(j->device);
    }
    LocalFree(j);
    g_joysticks[id] = nullptr;
}

static void close_all(void)
{
    for (UINT i = 0; i < MAX_JOYSTICKS; i++)
        close_joystick(i);
}

/* windows' joyOpen, with the lock held: the cached record, else the device found, mapped and acquired. */
static HRESULT open_joystick(UINT id, JOYCAPSW *caps_out)
{
    if (id >= MAX_JOYSTICKS)
        return E_FAIL;
    JOYSTICK *j = g_joysticks[id];
    if (j) {
        j->idle = 0;
        if (caps_out)
            *caps_out = j->caps;
        return S_OK;
    }
    if (!g_dinput) {
        g_dinput = LoadLibraryExW(L"DINPUT.DLL", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        g_create = g_dinput ? (DIRECTINPUTCREATEW)(void *)GetProcAddress(g_dinput, "DirectInputCreateW") : nullptr;
        if (!g_create) {
            if (g_dinput)
                FreeLibrary(g_dinput);
            g_dinput = nullptr;
            return E_FAIL;
        }
    }
    HRESULT hr;
    if (!g_di && FAILED(hr = g_create(WinmmModule, 0x050A, &g_di, nullptr)))
        return hr;
    if (!g_monitor) {
        HANDLE thread = CreateThread(nullptr, 0, monitor, nullptr, 0, nullptr);
        if (thread) {
            SetThreadPriority(thread, THREAD_PRIORITY_LOWEST);
            CloseHandle(thread);
            g_monitor = TRUE;
        }
    }
    if (!g_config && FAILED(hr = g_di->lpVtbl->QueryInterface(g_di, &IID_IDirectInputJoyConfig, (void **)&g_config)))
        return hr;
    g_config->lpVtbl->SendNotify(g_config);
    DIJOYCONFIG_DX5 config = {sizeof(config)};
    hr = g_config->lpVtbl->GetConfig(g_config, id, (LPDIJOYCONFIG)&config,
                                     DIJC_GUIDINSTANCE | DIJC_REGHWCONFIGTYPE | DIJC_GAIN);
    if (FAILED(hr))
        return hr;
    IDirectInputDeviceW *device;
    if (FAILED(hr = g_di->lpVtbl->CreateDevice(g_di, &config.guidInstance, &device, nullptr)))
        return hr;
    IDirectInputDevice2W *dev;
    hr = device->lpVtbl->QueryInterface(device, &IID_IDirectInputDevice2W, (void **)&dev);
    device->lpVtbl->Release(device);
    if (FAILED(hr))
        return hr;
    reset_format();
    hr = dev->lpVtbl->EnumObjects(dev, classify, nullptr, DIDFT_ALL);
    JOYCAPSW caps = {};
    assign_slots(&caps);
    DIDEVCAPS_DX3 dicaps = {sizeof(dicaps)};
    if (SUCCEEDED(hr))
        hr = dev->lpVtbl->GetCapabilities(dev, (LPDIDEVCAPS)&dicaps);
    if (SUCCEEDED(hr))
        hr = dev->lpVtbl->SetCooperativeLevel(dev, nullptr, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    if (SUCCEEDED(hr))
        hr = dev->lpVtbl->SetDataFormat(dev, &g_format);
    if (FAILED(hr)) {
        dev->lpVtbl->Release(dev);
        return hr;
    }
    j = LocalAlloc(LPTR, sizeof(*j));
    if (!j) {
        dev->lpVtbl->Release(dev);
        return E_OUTOFMEMORY;
    }
    j->device = dev;
    j->di_buttons = dicaps.dwButtons;
    dev->lpVtbl->Acquire(dev);
    DIDEVICEINSTANCEW info = {sizeof(info)};
    if (FAILED(hr = dev->lpVtbl->GetDeviceInfo(dev, &info))) {
        dev->lpVtbl->Release(dev);
        LocalFree(j);
        return hr;
    }
    /* the pedals of a wheel would be combined here: windows' test needs a brake in the v slot, which nothing takes. */
    caps.wMid = LOWORD(info.guidProduct.Data1);
    caps.wPid = HIWORD(info.guidProduct.Data1);
    lstrcpynW(caps.szPname, L"Microsoft PC-joystick driver", MAXPNAMELEN);
    caps.wXmax = caps.wYmax = caps.wZmax = caps.wRmax = caps.wUmax = caps.wVmax = 0xFFFF;
    caps.wPeriodMin = 10;
    caps.wPeriodMax = 1000;
    caps.wMaxAxes = 6;
    caps.wMaxButtons = 32;
    lstrcpynW(caps.szRegKey, L"DINPUT.DLL", MAXPNAMELEN);
    DIPROPDWORD granularity = {{sizeof(granularity), sizeof(DIPROPHEADER), 24, DIPH_BYOFFSET}};
    if (SUCCEEDED(dev->lpVtbl->GetProperty(dev, DIPROP_GRANULARITY, &granularity.diph)))
        caps.wCaps |= JOYCAPS_POV4DIR;
    j->caps = caps;
    if (caps_out)
        *caps_out = caps;
    g_joysticks[id] = j;
    return S_OK;
}

/* a record unused through two passes of a minute goes; with none left the thread ends. */
static DWORD WINAPI monitor(void *context)
{
    for (;;) {
        DWORD r = g_event ? WaitForSingleObject(g_event, 60000) : SleepEx(60000, FALSE);
        if (g_event && r == WAIT_OBJECT_0)
            return 0;
        if (g_event && r != WAIT_TIMEOUT)
            SleepEx(60000, FALSE);
        BOOL alive = FALSE;
        RtlEnterCriticalSection(&g_lock);
        for (UINT i = 0; i < MAX_JOYSTICKS; i++) {
            JOYSTICK *j = g_joysticks[i];
            if (!j)
                continue;
            if (j->idle == 2) {
                close_joystick(i);
            } else {
                j->idle++;
                alive = TRUE;
            }
        }
        if (!alive) {
            close_all();
            g_monitor = FALSE;
            RtlLeaveCriticalSection(&g_lock);
            return 0;
        }
        RtlLeaveCriticalSection(&g_lock);
    }
}

/* ------------------------------------------------------------------ the api */

WINMMAPI UINT WINAPI joyGetNumDevs(void)
{
    return MAX_JOYSTICKS;
}

WINMMAPI MMRESULT WINAPI joyGetDevCapsW(UINT_PTR id, LPJOYCAPSW caps, UINT size)
{
    if (!caps)
        return MMSYSERR_INVALPARAM;
    if (size != sizeof(JOYCAPSW) && size != sizeof(JOYCAPS2W))
        return JOYERR_PARMS;
    if (!init())
        return JOYERR_NOCANDO;
    memset(caps, 0, size);
    if (id == (UINT_PTR)-1) {
        lstrcpynW(caps->szRegKey, L"DINPUT.DLL", MAXPNAMELEN);
        return JOYERR_NOERROR;
    }
    if (id >= MAX_JOYSTICKS)
        return MMSYSERR_NODRIVER;
    RtlEnterCriticalSection(&g_lock);
    HRESULT hr = open_joystick((UINT)id, caps);
    RtlLeaveCriticalSection(&g_lock);
    return FAILED(hr) ? JOYERR_PARMS : JOYERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI joyGetDevCapsA(UINT_PTR id, LPJOYCAPSA caps, UINT size)
{
    if (!caps)
        return MMSYSERR_INVALPARAM;
    if (size != sizeof(JOYCAPSA) && size != sizeof(JOYCAPS2A))
        return JOYERR_PARMS;
    if (id == (UINT_PTR)-1) {
        lstrcpynA(caps->szRegKey, "DINPUT.DLL", MAXPNAMELEN);
        return JOYERR_NOERROR;
    }
    if (id >= MAX_JOYSTICKS)
        return MMSYSERR_NODRIVER;
    memset(caps, 0, size);
    JOYCAPS2W w;
    MMRESULT r = joyGetDevCapsW(id, &w.caps, sizeof(w));
    if (r)
        return r;
    JOYCAPS2A a = {};
    a.caps.wMid = w.caps.wMid;
    a.caps.wPid = w.caps.wPid;
    WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, w.caps.szPname, -1, a.caps.szPname, MAXPNAMELEN, nullptr, nullptr);
    memcpy(&a.caps.wXmin, &w.caps.wXmin, offsetof(JOYCAPSW, szRegKey) - offsetof(JOYCAPSW, wXmin));
    WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, w.caps.szRegKey, -1, a.caps.szRegKey, MAXPNAMELEN, nullptr,
                        nullptr);
    WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, w.caps.szOEMVxD, -1, a.caps.szOEMVxD, MAX_JOYSTICKOEMVXDNAME,
                        nullptr, nullptr);
    a.ManufacturerGuid = w.ManufacturerGuid;
    a.ProductGuid = w.ProductGuid;
    a.NameGuid = w.NameGuid;
    memcpy(caps, &a, size);
    return JOYERR_NOERROR;
}

/*
 * windows' mode switch: raw before the dead zone, each switch leaving the
 * device unacquired. turning one off clears the other's cached bit, so
 * the dead zone can stay programmed while winmm thinks it gone.
 */
static BOOL switch_mode(JOYSTICK *j, DWORD wanted)
{
    IDirectInputDevice2W *d = j->device;
    while ((wanted ^ j->mode) & (JOY_RETURNRAWDATA | JOY_USEDEADZONE)) {
        if (FAILED(d->lpVtbl->Unacquire(d)))
            return FALSE;
        if ((wanted ^ j->mode) & JOY_RETURNRAWDATA) {
            BOOL raw = (wanted & JOY_RETURNRAWDATA) != 0;
            j->mode = raw ? j->mode | JOY_RETURNRAWDATA : 0;
            DIPROPDWORD mode = {{sizeof(mode), sizeof(DIPROPHEADER), 0, DIPH_DEVICE}, raw};
            if (FAILED(d->lpVtbl->SetProperty(d, DIPROP_CALIBRATIONMODE, &mode.diph)))
                return FALSE;
        } else {
            BOOL dz = (wanted & JOY_USEDEADZONE) != 0;
            j->mode = dz ? j->mode | JOY_USEDEADZONE : 0;
            DIPROPDWORD zone = {{sizeof(zone), sizeof(DIPROPHEADER), 0, DIPH_DEVICE}, dz ? DEAD_ZONE : 0};
            if (FAILED(d->lpVtbl->SetProperty(d, DIPROP_DEADZONE, &zone.diph)))
                return FALSE;
        }
    }
    return TRUE;
}

WINMMAPI MMRESULT WINAPI joyGetPosEx(UINT id, LPJOYINFOEX info)
{
    if (!info || info->dwSize < sizeof(JOYINFOEX))
        return JOYERR_PARMS;
    if (!init())
        return JOYERR_NOCANDO;
    RtlEnterCriticalSection(&g_lock);
    MMRESULT result = JOYERR_PARMS;
    JOYSTICK *j;
    if (id < MAX_JOYSTICKS && SUCCEEDED(open_joystick(id, nullptr)) && (j = g_joysticks[id])) {
        result = JOYERR_UNPLUGGED;
        IDirectInputDevice2W *d = j->device;
        BYTE state[STATE_SIZE];
        if (switch_mode(j, info->dwFlags)) {
            d->lpVtbl->Poll(d);
            HRESULT hr = d->lpVtbl->GetDeviceState(d, sizeof(state), state);
            if (FAILED(hr) && SUCCEEDED(d->lpVtbl->Acquire(d))) {
                d->lpVtbl->Poll(d);
                hr = d->lpVtbl->GetDeviceState(d, sizeof(state), state);
            }
            if (SUCCEEDED(hr)) {
                DWORD v[7];
                memcpy(v, state, sizeof(v));
                info->dwXpos = v[0];
                info->dwYpos = v[1];
                info->dwZpos = v[2];
                info->dwRpos = v[3];
                info->dwUpos = v[4];
                info->dwVpos = v[5];
                info->dwPOV = LOWORD(v[6]);
                info->dwButtons = info->dwButtonNumber = 0;
                if (info->dwFlags & JOY_RETURNBUTTONS)
                    for (DWORD b = 0; b < j->di_buttons && b < 32; b++)
                        if (state[28 + b] & 0x80) {
                            info->dwButtons |= 1u << b;
                            info->dwButtonNumber++;
                        }
                result = JOYERR_NOERROR;
            }
        }
    }
    RtlLeaveCriticalSection(&g_lock);
    return result;
}

WINMMAPI MMRESULT WINAPI joyGetPos(UINT id, LPJOYINFO info)
{
    if (!info)
        return MMSYSERR_INVALPARAM;
    JOYINFOEX ex = {sizeof(ex), JOY_RETURNX | JOY_RETURNY | JOY_RETURNZ | JOY_RETURNBUTTONS};
    MMRESULT r = joyGetPosEx(id, &ex);
    if (r)
        return r;
    info->wXpos = ex.dwXpos;
    info->wYpos = ex.dwYpos;
    info->wZpos = ex.dwZpos;
    info->wButtons = ex.dwButtons;
    return JOYERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI joyGetThreshold(UINT id, LPUINT threshold)
{
    if (!threshold || id >= MAX_JOYSTICKS)
        return MMSYSERR_INVALPARAM;
    if (!init())
        return MMSYSERR_INVALPARAM;
    RtlEnterCriticalSection(&g_lock);
    JOYCAPSW scratch;
    MMRESULT r = MMSYSERR_INVALPARAM;
    if (SUCCEEDED(open_joystick(id, &scratch)) && g_joysticks[id]) {
        *threshold = g_joysticks[id]->threshold;
        r = JOYERR_NOERROR;
    }
    RtlLeaveCriticalSection(&g_lock);
    return r;
}

WINMMAPI MMRESULT WINAPI joySetThreshold(UINT id, UINT threshold)
{
    if (id >= MAX_JOYSTICKS || threshold > 0xFFFF)
        return MMSYSERR_INVALPARAM;
    if (!init())
        return MMSYSERR_INVALPARAM;
    RtlEnterCriticalSection(&g_lock);
    MMRESULT r = MMSYSERR_INVALPARAM;
    if (SUCCEEDED(open_joystick(id, nullptr)) && g_joysticks[id]) {
        g_joysticks[id]->threshold = threshold;
        r = JOYERR_NOERROR;
    }
    RtlLeaveCriticalSection(&g_lock);
    return r;
}

WINMMAPI MMRESULT WINAPI joyReleaseCapture(UINT id)
{
    if (id >= MAX_JOYSTICKS)
        return JOYERR_PARMS;
    if (!init())
        return JOYERR_NOCANDO;
    RtlEnterCriticalSection(&g_lock);
    JOYSTICK *j = g_joysticks[id];
    if (j && j->timer)
        KillTimer(nullptr, j->timer);
    RtlLeaveCriticalSection(&g_lock);
    return j ? JOYERR_NOERROR : MMSYSERR_INVALPARAM;
}

static DWORD distance(UINT a, UINT b)
{
    return a > b ? a - b : b - a;
}

/*
 * windows' poll callback: it reads the joystick of the capture's index,
 * not the captured id, and posts buttons one to four, moves and z moves
 * to the capture window.
 */
static void CALLBACK poll(HWND hwnd, UINT msg, UINT_PTR event, DWORD time)
{
    RtlEnterCriticalSection(&g_lock);
    JOYSTICK *j = nullptr;
    for (UINT i = 0; i < MAX_JOYSTICKS && !j; i++)
        if (g_joysticks[i] && g_joysticks[i]->timer == event)
            j = g_joysticks[i];
    UINT c = j ? j->capture_index : 0;
    HWND window = j ? j->capture : nullptr;
    BOOL changed = j && j->changed;
    UINT threshold = j ? j->threshold : 0;
    RtlLeaveCriticalSection(&g_lock);
    if (!j)
        return;
    if (!window || !IsWindow(window))
        joyReleaseCapture(c);
    JOYINFO ji;
    if (joyGetPos(c, &ji))
        return;
    for (UINT b = 1; b <= 8; b <<= 1)
        if ((g_previous[c].wButtons ^ ji.wButtons) & b)
            PostMessageW(window, (ji.wButtons & b ? MM_JOY1BUTTONDOWN : MM_JOY1BUTTONUP) + c, ji.wButtons | b << 8,
                         MAKELONG(LOWORD(ji.wXpos), LOWORD(ji.wYpos)));
    if (!changed || distance(ji.wXpos, g_previous[c].wXpos) > threshold ||
        distance(ji.wYpos, g_previous[c].wYpos) > threshold)
        PostMessageW(window, MM_JOY1MOVE + c, ji.wButtons, MAKELONG(LOWORD(ji.wXpos), LOWORD(ji.wYpos)));
    if (!changed || distance(ji.wZpos, g_previous[c].wZpos) > threshold)
        PostMessageW(window, MM_JOY1ZMOVE + c, ji.wButtons, LOWORD(ji.wZpos));
    g_previous[c] = ji;
}

/* at most two captures in a process's life, as windows counts them. */
WINMMAPI MMRESULT WINAPI joySetCapture(HWND hwnd, UINT id, UINT period, BOOL changed)
{
    if (!hwnd || !IsWindow(hwnd) || id >= MAX_JOYSTICKS)
        return JOYERR_PARMS;
    if (g_captures >= 2)
        return MMSYSERR_NODRIVER;
    if (!init())
        return JOYERR_NOCANDO;
    period = period < 10 ? 10 : period > 1000 ? 1000 : period;
    RtlEnterCriticalSection(&g_lock);
    JOYSTICK *j = g_joysticks[id];
    if (j && j->capture == hwnd) {
        MMRESULT r = JOYERR_NOCANDO;
        if (j->period != period || j->changed != changed) {
            j->period = period;
            j->changed = changed;
            r = JOYERR_NOERROR;
        }
        RtlLeaveCriticalSection(&g_lock);
        return r;
    }
    if (j && j->capture && IsWindow(j->capture)) {
        RtlLeaveCriticalSection(&g_lock);
        return JOYERR_NOCANDO;
    }
    RtlLeaveCriticalSection(&g_lock);
    JOYCAPSW caps;
    if (joyGetDevCapsW(id, &caps, sizeof(caps)))
        return MMSYSERR_NODRIVER;
    period = period < caps.wPeriodMin ? caps.wPeriodMin : period > caps.wPeriodMax ? caps.wPeriodMax : period;
    JOYINFO ji;
    MMRESULT r = joyGetPos(id, &ji);
    if (r)
        return r;
    RtlEnterCriticalSection(&g_lock);
    if (FAILED(open_joystick(id, nullptr)) || !(j = g_joysticks[id])) {
        RtlLeaveCriticalSection(&g_lock);
        return MMSYSERR_NODRIVER;
    }
    UINT_PTR timer = SetTimer(nullptr, 0, period, poll);
    if (!timer) {
        close_joystick(id);
        RtlLeaveCriticalSection(&g_lock);
        return JOYERR_NOCANDO;
    }
    j->timer = timer;
    j->capture = hwnd;
    j->capture_index = (DWORD)g_captures++;
    j->period = period;
    j->changed = changed;
    j->threshold = 0;
    RtlLeaveCriticalSection(&g_lock);
    return JOYERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI joyConfigChanged(DWORD flags)
{
    if (flags)
        return JOYERR_PARMS;
    init();
    RtlEnterCriticalSection(&g_lock);
    close_all();
    RtlLeaveCriticalSection(&g_lock);
    PostMessageW(HWND_BROADCAST, g_changed_message, 0, 0);
    return JOYERR_NOERROR;
}

/* the devices go with the dll, directinput's objects too unless dinput.dll already went. */
void WinmmJoyDetach(void)
{
    if (g_event) {
        close_all();
        if (WaitForSingleObject(g_event, 10) != WAIT_OBJECT_0) {
            if (g_config)
                g_config->lpVtbl->Release(g_config);
            if (g_di)
                g_di->lpVtbl->Release(g_di);
        }
        g_config = nullptr;
        g_di = nullptr;
    }
    if (g_dinput)
        FreeLibrary(g_dinput);
    g_dinput = nullptr;
    if (g_event)
        CloseHandle(g_event);
    g_event = nullptr;
}

#ifdef __i386__
/* the 16-bit thunk of ntvdm, which a 32-bit process under wow64 never has. */
WINMMAPI MMRESULT WINAPI joy32Message(UINT id, UINT msg, DWORD_PTR p1, DWORD_PTR p2, UINT size)
{
    return 97;
}
#endif
