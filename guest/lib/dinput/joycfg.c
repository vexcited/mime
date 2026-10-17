/*
 * IDirectInputJoyConfig of dinput.dll, aggregated in a directinput object
 * as windows makes it. what winmm reads through it: the device a joystick
 * id names, and a legacy hardware configuration, from the registry where
 * windows keeps one or else made from the device. see docs/dinput.md.
 */
#include "dinput_private.h"

#define SETTINGS_KEY L"System\\CurrentControlSet\\Control\\MediaResources\\Joystick\\DINPUT.DLL\\CurrentJoystickSettings"
#define OEM_KEY L"System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM\\"
#define DIERR_NOMOREITEMS_ ((HRESULT)0x80070103)

typedef struct JOYCFG {
    const IDirectInputJoyConfigVtbl *vtbl;
    DIOBJ *outer;
    BOOL acquired;
} JOYCFG;

static const IDirectInputJoyConfigVtbl joycfg_vtbl;

void *DiJoyConfigFor(DIOBJ *obj)
{
    RtlEnterCriticalSection(&DiLock);
    if (!obj->joycfg) {
        JOYCFG *j = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*j));
        if (j) {
            j->vtbl = &joycfg_vtbl;
            j->outer = obj;
            obj->joycfg = j;
        }
    }
    void *out = obj->joycfg;
    if (out)
        InterlockedIncrement(&obj->ref);
    RtlLeaveCriticalSection(&DiLock);
    return out;
}

void DiJoyConfigFree(DIOBJ *obj)
{
    HeapFree(GetProcessHeap(), 0, obj->joycfg);
    obj->joycfg = nullptr;
}

static JOYCFG *from(IDirectInputJoyConfig *This)
{
    return (JOYCFG *)This;
}

/* the unknown is the outer object's. */
static HRESULT STDMETHODCALLTYPE jc_query(IDirectInputJoyConfig *This, REFIID riid, void **out)
{
    IUnknown *outer = (IUnknown *)&from(This)->outer->vtbl_w;
    return outer->lpVtbl->QueryInterface(outer, riid, out);
}

static ULONG STDMETHODCALLTYPE jc_addref(IDirectInputJoyConfig *This)
{
    IUnknown *outer = (IUnknown *)&from(This)->outer->vtbl_w;
    return outer->lpVtbl->AddRef(outer);
}

static ULONG STDMETHODCALLTYPE jc_release(IDirectInputJoyConfig *This)
{
    IUnknown *outer = (IUnknown *)&from(This)->outer->vtbl_w;
    return outer->lpVtbl->Release(outer);
}

static HRESULT STDMETHODCALLTYPE jc_acquire(IDirectInputJoyConfig *This)
{
    from(This)->acquired = TRUE;
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE jc_unacquire(IDirectInputJoyConfig *This)
{
    JOYCFG *j = from(This);
    if (!j->acquired)
        return S_FALSE;
    j->acquired = FALSE;
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE jc_cooperate(IDirectInputJoyConfig *This, HWND window, DWORD flags)
{
    if (window && !IsWindow(window))
        return E_HANDLE;
    return DI_OK;
}

/* the configuration changed: every window hears it, as windows' joystick applet tells them. */
static HRESULT STDMETHODCALLTYPE jc_notify(IDirectInputJoyConfig *This)
{
    if (!from(This)->acquired)
        return DIERR_NOTACQUIRED;
    PostMessageW(HWND_BROADCAST, RegisterWindowMessageW(L"MSJSTICK_VJOYD_MSGSTR"), 0, 0);
    return DI_OK;
}

/* no types are registered: the legacy gameport types are gone. */
static HRESULT STDMETHODCALLTYPE jc_enum_types(IDirectInputJoyConfig *This, LPDIJOYTYPECALLBACK callback, LPVOID context)
{
    return callback ? DI_OK : E_POINTER;
}

static HRESULT STDMETHODCALLTYPE jc_get_type(IDirectInputJoyConfig *This, LPCWSTR type, void *info, DWORD flags)
{
    return type && info ? DIERR_NOTFOUND : E_POINTER;
}

static HRESULT changes(JOYCFG *j)
{
    return j->acquired ? DIERR_UNSUPPORTED : DIERR_NOTACQUIRED;
}

static HRESULT STDMETHODCALLTYPE jc_set_type(IDirectInputJoyConfig *This, LPCWSTR type, const void *info, DWORD flags)
{
    return changes(from(This));
}

static HRESULT STDMETHODCALLTYPE jc_delete_type(IDirectInputJoyConfig *This, LPCWSTR type)
{
    return changes(from(This));
}

/* windows' table of the legacy axes and the usages that fill each, the first present winning. */
static const struct {
    USHORT usages[5][2];
    DWORD flag;
} AXES[6] = {
    {{{1, 0x30}, {2, 0xC8}}, 0},
    {{{1, 0x31}, {2, 0xC4}}, 0},
    {{{1, 0x32}, {1, 0x36}, {2, 0xBB}, {2, 0x37}, {1, 0x38}}, JOY_HWS_HASZ},
    {{{2, 0xBA}, {1, 0x35}, {2, 0xC5}}, JOY_HWS_HASR},
    {{{1, 0x34}, {2, 0xBB}, {1, 0x36}}, JOY_HWS_HASU},
    {{{1, 0x33}}, JOY_HWS_HASV},
};

static DWORD *pos_field(JOYPOS *p, int axis)
{
    DWORD *fields[6] = {&p->dwX, &p->dwY, &p->dwZ, &p->dwR, &p->dwU, &p->dwV};
    return fields[axis];
}

/* windows' DIWdm_JoyHidMapping: the legacy configuration of a hid joystick, from its objects. */
static HRESULT make_config(DIOBJ *obj, UINT id, const GUID *instance, USHORT vid, USHORT pid, USHORT usage,
                           JOYREGHWCONFIG *hwc)
{
    void *iface;
    HRESULT hr = DiCreateDevice(obj, instance, &IID_IDirectInputDevice2W, &iface, nullptr);
    if (FAILED(hr))
        return E_FAIL;
    IDirectInputDevice2W *device = iface;
    DIDEV *dev = DiDeviceFrom(iface);
    memset(hwc, 0, sizeof(*hwc));
    DWORD flags = 0x10000000;
    WCHAR path[200];
    HKEY key;
    wsprintfW(path, OEM_KEY L"VID_%04X&PID_%04X", vid, pid);
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &key)) {
        DWORD data[2], size = sizeof(data);
        if (!RegQueryValueExW(key, L"OEMData", nullptr, nullptr, (BYTE *)data, &size) && size >= 4)
            flags |= data[0];
        RegCloseKey(key);
    }
    BOOL used[DI_MAX_OBJECTS] = {}, rudder = FALSE;
    for (int axis = 0; axis < 6; axis++)
        for (int u = 0; u < 5 && AXES[axis].usages[u][0]; u++) {
            int found = -1;
            for (DWORD i = 0; i < dev->count && found < 0; i++)
                if (!used[i] && dev->objects[i].page == AXES[axis].usages[u][0] &&
                    dev->objects[i].usage == AXES[axis].usages[u][1] && (dev->objects[i].type & DIDFT_AXIS))
                    found = (int)i;
            if (found < 0)
                continue;
            used[found] = TRUE;
            flags |= AXES[axis].flag;
            rudder |= axis == 3;
            DIPROPCAL cal = {{sizeof(cal), sizeof(DIPROPHEADER), dev->objects[found].type & 0x00FFFFFF, DIPH_BYID}};
            if (SUCCEEDED(device->lpVtbl->GetProperty(device, DIPROP_CALIBRATION, &cal.diph))) {
                *pos_field(&hwc->hwv.jrvHardware.jpMin, axis) = (DWORD)cal.lMin;
                *pos_field(&hwc->hwv.jrvHardware.jpMax, axis) = (DWORD)cal.lMax;
                *pos_field(&hwc->hwv.jrvHardware.jpCenter, axis) =
                    (DWORD)(cal.lMin + (LONG)((ULONG)(cal.lMax - cal.lMin + 1) >> 1));
            }
            break;
        }
    for (DWORD i = 0; i < dev->count; i++)
        if (dev->objects[i].page == 1 && dev->objects[i].usage == 0x39 && (dev->objects[i].type & DIDFT_POV)) {
            flags |= JOY_HWS_HASPOV;
            DIPROPDWORD granularity = {{sizeof(granularity), sizeof(DIPROPHEADER), dev->objects[i].type & 0x00FFFFFF,
                                        DIPH_BYID}};
            if (SUCCEEDED(device->lpVtbl->GetProperty(device, DIPROP_GRANULARITY, &granularity.diph)) &&
                granularity.dwData >= 9000) {
                static const DWORD FOUR_WAY[4] = {0, 18000, 27000, 9000};
                memcpy(hwc->hwv.dwPOVValues, FOUR_WAY, sizeof(FOUR_WAY));
            }
            break;
        }
    if (usage == 5)
        flags |= JOY_HWS_ISGAMEPAD;
    hwc->hws.dwFlags = flags;
    hwc->hws.dwNumButtons = dev->buttons;
    BOOL gameport = vid == 0x045E && pid >= 0x0102 && pid <= 0x010C;
    hwc->dwUsageSettings = (gameport ? JOY_US_PRESENT | JOY_US_VOLATILE : JOY_US_PRESENT | JOY_US_ISOEM | JOY_US_VOLATILE) |
                           (rudder ? JOY_US_HASRUDDER : 0);
    hwc->dwType = gameport ? 0x100u + (pid - 0x0102u) : JOY_HW_LASTENTRY + id;
    device->lpVtbl->Release(device);
    return DI_OK;
}

/* windows' JoyReg_GetConfig: the device of the id, the configuration stored for it or one made from it, its gain. */
static HRESULT STDMETHODCALLTYPE jc_get_config(IDirectInputJoyConfig *This, UINT id, LPDIJOYCONFIG config, DWORD flags)
{
    JOYCFG *j = from(This);
    if (!config)
        return E_POINTER;
    BOOL dx6 = config->dwSize == sizeof(DIJOYCONFIG);
    if ((!dx6 && config->dwSize != sizeof(DIJOYCONFIG_DX5)) || (flags & ~(dx6 ? 0x1Fu : 0xFu)))
        return E_INVALIDARG;
    GUID instance = GUID_NULL;
    USHORT vid = 0, pid = 0, usage = 0;
    DiHidRebuild(FALSE);
    BOOL present = DiHidByJoystick(id, &instance, &vid, &pid, &usage);
    if (id >= 16)
        return DIERR_NOMOREITEMS_;
    if (flags & DIJC_GUIDINSTANCE)
        config->guidInstance = instance;
    HRESULT hr = DI_OK;
    HKEY key = nullptr;
    RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_READ, &key);
    WCHAR name[64];
    if (flags & DIJC_REGHWCONFIGTYPE) {
        DWORD size = sizeof(config->hwc);
        wsprintfW(name, L"Joystick%uConfiguration", id + 1);
        if (!key || RegQueryValueExW(key, name, nullptr, nullptr, (BYTE *)&config->hwc, &size) ||
            size != sizeof(config->hwc)) {
            if (!present)
                hr = E_FAIL;
            else
                hr = make_config(j->outer, id, &instance, vid, pid, usage, &config->hwc);
        }
        if (SUCCEEDED(hr)) {
            config->wszType[0] = 0;
            if (config->hwc.dwUsageSettings & JOY_US_ISOEM) {
                DWORD bytes = sizeof(config->wszType) - sizeof(WCHAR);
                wsprintfW(name, L"Joystick%uOEMName", id + 1);
                if (!key || RegQueryValueExW(key, name, nullptr, nullptr, (BYTE *)config->wszType, &bytes))
                    wsprintfW(config->wszType, L"VID_%04X&PID_%04X", vid, pid);
            }
        }
    }
    if (SUCCEEDED(hr) && (flags & DIJC_GAIN)) {
        DWORD gain = DI_FFNOMINALMAX, size = sizeof(gain);
        wsprintfW(name, L"Joystick%uFFConfiguration", id + 1);
        if (key && RegQueryValueExW(key, name, nullptr, nullptr, (BYTE *)&gain, &size))
            gain = DI_FFNOMINALMAX;
        config->dwGain = gain;
    }
    if (SUCCEEDED(hr) && (flags & DIJC_CALLOUT))
        config->wszCallout[0] = 0;
    if (key)
        RegCloseKey(key);
    if (FAILED(hr))
        return hr;
    return (flags & DIJC_REGHWCONFIGTYPE) && config->hwc.dwType == JOY_HW_NONE ? S_FALSE : DI_OK;
}

static HRESULT STDMETHODCALLTYPE jc_set_config(IDirectInputJoyConfig *This, UINT id, LPCDIJOYCONFIG config, DWORD flags)
{
    return changes(from(This));
}

static HRESULT STDMETHODCALLTYPE jc_delete_config(IDirectInputJoyConfig *This, UINT id)
{
    return changes(from(This));
}

/* the defaults of a profile that never set them (guessed: windows keeps them under the joystick settings). */
static HRESULT STDMETHODCALLTYPE jc_get_user(IDirectInputJoyConfig *This, LPDIJOYUSERVALUES values, DWORD flags)
{
    if (!values)
        return E_POINTER;
    if (values->dwSize != sizeof(DIJOYUSERVALUES) || (flags & ~7u))
        return E_INVALIDARG;
    if (flags & DIJU_USERVALUES) {
        memset(&values->ruv, 0, sizeof(values->ruv));
        for (int axis = 0; axis < 6; axis++) {
            *pos_field(&values->ruv.jrvRanges.jpMax, axis) = 0xFFFF;
            *pos_field(&values->ruv.jrvRanges.jpCenter, axis) = 0x7FFF;
        }
    }
    if (flags & DIJU_GLOBALDRIVER)
        values->wszGlobalDriver[0] = 0;
    if (flags & DIJU_GAMEPORTEMULATOR)
        values->wszGameportEmulator[0] = 0;
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE jc_set_user(IDirectInputJoyConfig *This, LPCDIJOYUSERVALUES values, DWORD flags)
{
    return changes(from(This));
}

static HRESULT STDMETHODCALLTYPE jc_add_hardware(IDirectInputJoyConfig *This, HWND owner, REFGUID klass)
{
    return DIERR_UNSUPPORTED;
}

static HRESULT open_key(const WCHAR *path, DWORD access, HKEY *key)
{
    if (!key)
        return E_POINTER;
    LSTATUS status = access & (KEY_SET_VALUE | KEY_CREATE_SUB_KEY)
                         ? RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, access, nullptr, key, nullptr)
                         : RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, access, key);
    return status ? (HRESULT)(0x80070000u | (status & 0xFFFF)) : DI_OK;
}

static HRESULT STDMETHODCALLTYPE jc_open_type(IDirectInputJoyConfig *This, LPCWSTR type, DWORD access, HKEY *key)
{
    if (!type)
        return E_POINTER;
    WCHAR path[400];
    wsprintfW(path, OEM_KEY L"%s", type);
    return open_key(path, access, key);
}

static HRESULT STDMETHODCALLTYPE jc_open_config(IDirectInputJoyConfig *This, UINT id, DWORD access, HKEY *key)
{
    if (id >= 16)
        return DIERR_NOMOREITEMS_;
    return open_key(SETTINGS_KEY, access, key);
}

static const IDirectInputJoyConfigVtbl joycfg_vtbl = {
    jc_query,      jc_addref,     jc_release,       jc_acquire,       jc_unacquire, jc_cooperate,
    jc_notify,     jc_enum_types, jc_get_type,      jc_set_type,      jc_delete_type, jc_get_config,
    jc_set_config, jc_delete_config, jc_get_user,   jc_set_user,      jc_add_hardware, jc_open_type,
    jc_open_config,
};
