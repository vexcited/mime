/*
 * the directinput object: IDirectInput8 for dinput8.dll, IDirectInput
 * to IDirectInput7 for dinput.dll, its creation and version checks, the
 * enumeration of devices and the class factory. the A methods convert
 * and call the W ones.
 */
#include "dinput_private.h"

BOOL DiIs8;
HINSTANCE DiModule;
LONG DiLocks;
RTL_CRITICAL_SECTION DiLock;

/* dinput8 accepts 0x800 only, dinput the versions of directx 3 to 7. */
HRESULT DiCheckVersion(HINSTANCE instance, DWORD version, BOOL di8)
{
    WCHAR path[3];
    if (instance) {
        if (!GetModuleFileNameW(instance, path, RTL_NUMBER_OF(path)))
            return E_INVALIDARG;
    } else if (di8 || version != 0x0300) {
        return E_INVALIDARG;
    }
    if (di8) {
        if (version == DI8_VERSION)
            return DI_OK;
        if (!version)
            return DIERR_NOTINITIALIZED;
        return version < DI8_VERSION ? DIERR_BETADIRECTINPUTVERSION : DIERR_OLDDIRECTINPUTVERSION;
    }
    switch (version) {
    case 0x0300:
    case 0x0500:
    case 0x050A:
    case 0x05B2:
    case 0x0602:
    case 0x061A:
    case 0x0700:
        return DI_OK;
    case 0:
        return DIERR_NOTINITIALIZED;
    default:
        return version < 0x0700 ? DIERR_BETADIRECTINPUTVERSION : DIERR_OLDDIRECTINPUTVERSION;
    }
}

/* ------------------------------------------------------------------ the object */

static const IDirectInput8WVtbl obj8w_vtbl;
static const IDirectInput8AVtbl obj8a_vtbl;
static const IDirectInput7WVtbl obj7w_vtbl;
static const IDirectInput7AVtbl obj7a_vtbl;

static DIOBJ *obj_from(void *iface)
{
    const void *vtbl = *(const void **)iface;
    if (vtbl == &obj8w_vtbl || vtbl == &obj7w_vtbl)
        return CONTAINING_RECORD(iface, DIOBJ, vtbl_w);
    return CONTAINING_RECORD(iface, DIOBJ, vtbl_a);
}

static BOOL wide(void *iface)
{
    const void *vtbl = *(const void **)iface;
    return vtbl == &obj8w_vtbl || vtbl == &obj7w_vtbl;
}

static HRESULT STDMETHODCALLTYPE obj_query(void *This, REFIID riid, void **out)
{
    DIOBJ *obj = obj_from(This);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown))
        *out = &obj->vtbl_w;
    else if (obj->di8 && IsEqualGUID(riid, &IID_IDirectInput8W))
        *out = &obj->vtbl_w;
    else if (obj->di8 && IsEqualGUID(riid, &IID_IDirectInput8A))
        *out = &obj->vtbl_a;
    else if (!obj->di8 && (IsEqualGUID(riid, &IID_IDirectInputW) || IsEqualGUID(riid, &IID_IDirectInput2W) ||
                           IsEqualGUID(riid, &IID_IDirectInput7W)))
        *out = &obj->vtbl_w;
    else if (!obj->di8 && (IsEqualGUID(riid, &IID_IDirectInputA) || IsEqualGUID(riid, &IID_IDirectInput2A) ||
                           IsEqualGUID(riid, &IID_IDirectInput7A)))
        *out = &obj->vtbl_a;
    else if (!obj->di8 && IsEqualGUID(riid, &IID_IDirectInputJoyConfig)) {
        /* an object aggregated in this one, made on first use. */
        *out = DiJoyConfigFor(obj);
        if (!*out)
            return E_OUTOFMEMORY;
    } else
        return E_NOINTERFACE;
    InterlockedIncrement(&obj->ref);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE obj_addref(void *This)
{
    return (ULONG)InterlockedIncrement(&obj_from(This)->ref);
}

static ULONG STDMETHODCALLTYPE obj_release(void *This)
{
    DIOBJ *obj = obj_from(This);
    LONG ref = InterlockedDecrement(&obj->ref);
    if (!ref) {
        DiJoyConfigFree(obj);
        HeapFree(GetProcessHeap(), 0, obj);
        InterlockedDecrement(&DiLocks);
    }
    return (ULONG)ref;
}

static const IID *device_iid(DIOBJ *obj, BOOL w)
{
    if (obj->di8)
        return w ? &IID_IDirectInputDevice8W : &IID_IDirectInputDevice8A;
    return w ? &IID_IDirectInputDeviceW : &IID_IDirectInputDeviceA;
}

static HRESULT STDMETHODCALLTYPE obj_create_device(void *This, REFGUID guid, void **out, IUnknown *outer)
{
    DIOBJ *obj = obj_from(This);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return DiCreateDevice(obj, guid, device_iid(obj, wide(This)), out, outer);
}

static HRESULT STDMETHODCALLTYPE obj_create_device_ex(void *This, REFGUID guid, REFIID riid, void **out,
                                                      IUnknown *outer)
{
    DIOBJ *obj = obj_from(This);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return DiCreateDevice(obj, guid, riid, out, outer);
}

/* ------------------------------------------------------------------ enumeration */

/* the system devices in windows' order, which is also that of enumeration. */
static const GUID *const SYSTEM_DEVICES[] = {&GUID_SysMouse,    &GUID_SysMouseEm,    &GUID_SysMouseEm2,
                                             &GUID_SysKeyboard, &GUID_SysKeyboardEm, &GUID_SysKeyboardEm2};

/* the type byte of a static entry, which dinput.dll filters on before making the device. */
static BYTE system_type(DWORD index, BOOL di8)
{
    BOOL keyboard = index >= 3;
    if (di8)
        return keyboard ? DI8DEVTYPE_KEYBOARD : DI8DEVTYPE_MOUSE;
    return keyboard ? DIDEVTYPE_KEYBOARD : DIDEVTYPE_MOUSE;
}

/* dinput8's class filters on the type a device reports. */
static BOOL type_matches(DWORD filter, BYTE type)
{
    switch (filter) {
    case DI8DEVCLASS_ALL:
        return TRUE;
    case DI8DEVCLASS_DEVICE:
        return type == DI8DEVTYPE_DEVICE;
    case DI8DEVCLASS_POINTER:
        return type == DI8DEVTYPE_MOUSE || type == DI8DEVTYPE_SCREENPOINTER;
    case DI8DEVCLASS_KEYBOARD:
        return type == DI8DEVTYPE_KEYBOARD;
    case DI8DEVCLASS_GAMECTRL:
        return type < 32 && (0x15F00000u >> type) & 1;
    default:
        return type == filter;
    }
}

static void instance_to_a(const DIDEVICEINSTANCEW *w, DIDEVICEINSTANCEA *a)
{
    memset(a, 0, sizeof(*a));
    a->dwSize = sizeof(*a);
    a->guidInstance = w->guidInstance;
    a->guidProduct = w->guidProduct;
    a->dwDevType = w->dwDevType;
    DiToA(a->tszInstanceName, w->tszInstanceName, sizeof(a->tszInstanceName));
    DiToA(a->tszProductName, w->tszProductName, sizeof(a->tszProductName));
    a->guidFFDriver = w->guidFFDriver;
    a->wUsagePage = w->wUsagePage;
    a->wUsage = w->wUsage;
}

/* a device made only to look at it, released before the next one. */
static DIDEV *probe(DIOBJ *obj, const GUID *guid)
{
    void *iface;
    if (FAILED(DiCreateDevice(obj, guid, device_iid(obj, TRUE), &iface, nullptr)))
        return nullptr;
    return DiDeviceFrom(iface);
}

static void unprobe(DIDEV *dev)
{
    ((IUnknown *)&dev->vtbl_w)->lpVtbl->Release((IUnknown *)&dev->vtbl_w);
}

static HRESULT enum_devices(DIOBJ *obj, DWORD type, void *callback, void *context, DWORD flags, BOOL w)
{
    if (!callback)
        return E_INVALIDARG;
    BYTE filter = LOBYTE(type);
    if (obj->di8) {
        if ((filter >= 5 && filter <= 0x10) || filter >= 0x1D || (type & 0xFFFEFF00) || (flags & 0xFFF8FEFE))
            return E_INVALIDARG;
    } else if (filter >= 5 || (type & 0xFFFEFF00) || (flags & 0xFFFCFEFE)) {
        return E_INVALIDARG;
    }
    HRESULT hr = DiCheckVersion(DiModule, obj->version, obj->di8);
    if (FAILED(hr))
        return hr;
    /* the system devices, then the hid collections in the order setupapi lists them. */
    DiHidRebuild(TRUE);
    DWORD hid_count = DiHidCount();
    DWORD wanted = flags ^ 0x00FF0000;
    for (DWORD i = 0; i < RTL_NUMBER_OF(SYSTEM_DEVICES) + hid_count; i++) {
        GUID guid;
        BYTE type7, type8;
        if (i < RTL_NUMBER_OF(SYSTEM_DEVICES)) {
            if (type & DIDEVTYPE_HID)
                continue;
            guid = *SYSTEM_DEVICES[i];
            type7 = system_type(i, FALSE);
        } else if (!DiHidEntry(i - RTL_NUMBER_OF(SYSTEM_DEVICES), &guid, &type7, &type8)) {
            continue;
        }
        if (!obj->di8 && filter && filter != type7)
            continue;
        /* directx 3 had no joysticks. */
        if (!obj->di8 && type7 == DIDEVTYPE_JOYSTICK && obj->version <= 0x0300)
            continue;
        DIDEV *dev = probe(obj, &guid);
        if (!dev)
            continue;
        DIDEVCAPS caps = {sizeof(DIDEVCAPS_DX3)};
        DiDeviceCaps(dev, &caps);
        DIDEVICEINSTANCEW info = {sizeof(info)};
        BOOL keep = (wanted & (caps.dwFlags ^ 0x00FF0000)) == wanted;
        HRESULT got = keep ? DiDeviceInfo(dev, &info) : DI_OK;
        unprobe(dev);
        if (!keep)
            continue;
        if (FAILED(got)) {
            if (obj->di8)
                break;
            continue;
        }
        if (obj->di8 && !type_matches(filter, LOBYTE(info.dwDevType)))
            continue;
        BOOL more;
        if (w) {
            more = ((LPDIENUMDEVICESCALLBACKW)callback)(&info, context);
        } else {
            DIDEVICEINSTANCEA a;
            instance_to_a(&info, &a);
            more = ((LPDIENUMDEVICESCALLBACKA)callback)(&a, context);
        }
        if (!more)
            break;
    }
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE obj_enum_devices(void *This, DWORD type, void *callback, void *context, DWORD flags)
{
    return enum_devices(obj_from(This), type, callback, context, flags, wide(This));
}

static HRESULT STDMETHODCALLTYPE obj_get_device_status(void *This, REFGUID guid)
{
    DIOBJ *obj = obj_from(This);
    void *iface;
    HRESULT hr = DiCreateDevice(obj, guid, device_iid(obj, TRUE), &iface, nullptr);
    if (FAILED(hr))
        return hr;
    DIDEV *dev = DiDeviceFrom(iface);
    DIDEVCAPS caps = {sizeof(DIDEVCAPS_DX3)};
    DiDeviceCaps(dev, &caps);
    unprobe(dev);
    return (caps.dwFlags & DIDC_ATTACHED) ? DI_OK : DI_NOTATTACHED;
}

/* opens the control panel; windows' game controllers applet no longer exists. */
static HRESULT STDMETHODCALLTYPE obj_run_control_panel(void *This, HWND owner, DWORD flags)
{
    DIOBJ *obj = obj_from(This);
    if (owner && !IsWindow(owner))
        return E_HANDLE;
    if (flags)
        return E_INVALIDARG;
    HRESULT hr = DiCheckVersion(DiModule, obj->version, obj->di8);
    if (FAILED(hr))
        return hr;
    WCHAR command[] = L"control.exe ";
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return (HRESULT)(0x80070000u | (GetLastError() & 0xFFFF));
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE obj_initialize(void *This, HINSTANCE instance, DWORD version)
{
    DIOBJ *obj = obj_from(This);
    HRESULT hr = DiCheckVersion(instance, version, obj->di8);
    if (SUCCEEDED(hr))
        obj->version = version;
    return hr;
}

static SIZE_T length_w(const WCHAR *s, SIZE_T max)
{
    SIZE_T n = 0;
    while (n < max && s[n])
        n++;
    return n;
}

/* game controllers are found by the path of their hid interface, the list rebuilt once on a miss. */
static HRESULT find_device(DIOBJ *obj, REFGUID class_guid, const WCHAR *name, LPGUID out)
{
    if (!class_guid)
        return E_POINTER;
    if (!name || length_w(name, DI_MAX_PATH) >= DI_MAX_PATH)
        return E_INVALIDARG;
    if (!out)
        return E_POINTER;
    if (!IsEqualGUID(class_guid, &GUID_HIDClass))
        return DIERR_DEVICENOTREG;
    if (DiHidFindPath(name, out))
        return DI_OK;
    DiHidRebuild(FALSE);
    return DiHidFindPath(name, out) ? DI_OK : DIERR_DEVICENOTREG;
}

static HRESULT STDMETHODCALLTYPE obj_find_device_w(void *This, REFGUID class_guid, LPCWSTR name, LPGUID out)
{
    return find_device(obj_from(This), class_guid, name, out);
}

static HRESULT STDMETHODCALLTYPE obj_find_device_a(void *This, REFGUID class_guid, LPCSTR name, LPGUID out)
{
    WCHAR wname[DI_MAX_PATH];
    if (!name || strlen(name) >= DI_MAX_PATH)
        return class_guid ? E_INVALIDARG : E_POINTER;
    MultiByteToWideChar(CP_ACP, 0, name, -1, wname, DI_MAX_PATH);
    return find_device(obj_from(This), class_guid, wname, out);
}

/* ------------------------------------------------------------------ action mapping, dinput8 only */

static HRESULT STDMETHODCALLTYPE obj_enum_semantics_w(void *This, LPCWSTR user, LPDIACTIONFORMATW format,
                                                      LPDIENUMDEVICESBYSEMANTICSCBW callback, LPVOID context,
                                                      DWORD flags)
{
    return DiEnumDevicesBySemantics(obj_from(This), user, format, (void *)callback, context, flags, TRUE);
}

static HRESULT STDMETHODCALLTYPE obj_enum_semantics_a(void *This, LPCSTR user, LPDIACTIONFORMATA format,
                                                      LPDIENUMDEVICESBYSEMANTICSCBA callback, LPVOID context,
                                                      DWORD flags)
{
    DIACTIONFORMATW w;
    DIACTIONW *actions = nullptr;
    HRESULT hr = DiActionFormatToW(format, &w, &actions);
    if (FAILED(hr))
        return hr;
    WCHAR wuser[257];
    if (user) {
        if (strlen(user) >= 257) {
            HeapFree(GetProcessHeap(), 0, actions);
            return E_INVALIDARG;
        }
        MultiByteToWideChar(CP_ACP, 0, user, -1, wuser, RTL_NUMBER_OF(wuser));
    }
    hr = DiEnumDevicesBySemantics(obj_from(This), user ? wuser : nullptr, &w, (void *)callback, context, flags,
                                  FALSE);
    DiActionFormatBack(format, &w);
    HeapFree(GetProcessHeap(), 0, actions);
    return hr;
}

/*
 * windows checks the arguments, then looks for the configuration ui of
 * diactfrm.dll, which current windows no longer has.
 */
static HRESULT configure(void *callback, DWORD size, DWORD users, const void *names, DWORD formats,
                         const void *list, IUnknown *target, DWORD flags)
{
    if (size != sizeof(DICONFIGUREDEVICESPARAMSW))
        return E_INVALIDARG;
    if (!list && formats)
        return E_POINTER;
    if (flags & ~DICD_EDIT)
        return E_INVALIDARG;
    if (callback ? !target : target != nullptr)
        return E_INVALIDARG;
    (void)users;
    (void)names;
    return DIERR_DEVICENOTREG;
}

static HRESULT STDMETHODCALLTYPE obj_configure_w(void *This, LPDICONFIGUREDEVICESCALLBACK callback,
                                                 LPDICONFIGUREDEVICESPARAMSW params, DWORD flags, LPVOID context)
{
    if (!params)
        return E_POINTER;
    for (DWORD i = 0; params->lprgFormats && i < params->dwcFormats; i++) {
        HRESULT hr = DiCheckActionFormat(&params->lprgFormats[i], TRUE);
        if (FAILED(hr))
            return hr;
    }
    return configure((void *)callback, params->dwSize, params->dwcUsers, params->lptszUserNames, params->dwcFormats,
                     params->lprgFormats, params->lpUnkDDSTarget, flags);
}

static HRESULT STDMETHODCALLTYPE obj_configure_a(void *This, LPDICONFIGUREDEVICESCALLBACK callback,
                                                 LPDICONFIGUREDEVICESPARAMSA params, DWORD flags, LPVOID context)
{
    if (!params)
        return E_POINTER;
    for (DWORD i = 0; params->lprgFormats && i < params->dwcFormats; i++) {
        HRESULT hr = DiCheckActionFormat((const DIACTIONFORMATW *)&params->lprgFormats[i], FALSE);
        if (FAILED(hr))
            return hr;
    }
    return configure((void *)callback, params->dwSize, params->dwcUsers, params->lptszUserNames, params->dwcFormats,
                     params->lprgFormats, params->lpUnkDDSTarget, flags);
}

/* ------------------------------------------------------------------ vtables */

#define OBJ_COMMON                                                                                                     \
    (void *)obj_query, (void *)obj_addref, (void *)obj_release, (void *)obj_create_device, (void *)obj_enum_devices,  \
        (void *)obj_get_device_status, (void *)obj_run_control_panel, (void *)obj_initialize

static const IDirectInput8WVtbl obj8w_vtbl = {OBJ_COMMON, (void *)obj_find_device_w, (void *)obj_enum_semantics_w,
                                              (void *)obj_configure_w};
static const IDirectInput8AVtbl obj8a_vtbl = {OBJ_COMMON, (void *)obj_find_device_a, (void *)obj_enum_semantics_a,
                                              (void *)obj_configure_a};
static const IDirectInput7WVtbl obj7w_vtbl = {OBJ_COMMON, (void *)obj_find_device_w, (void *)obj_create_device_ex};
static const IDirectInput7AVtbl obj7a_vtbl = {OBJ_COMMON, (void *)obj_find_device_a, (void *)obj_create_device_ex};

HRESULT DiCreateObject(REFIID riid, void **out, BOOL di8)
{
    DIOBJ *obj = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*obj));
    if (!obj)
        return E_OUTOFMEMORY;
    obj->vtbl_w = di8 ? (const void *)&obj8w_vtbl : (const void *)&obj7w_vtbl;
    obj->vtbl_a = di8 ? (const void *)&obj8a_vtbl : (const void *)&obj7a_vtbl;
    obj->di8 = di8;
    obj->ref = 1;
    InterlockedIncrement(&DiLocks);
    HRESULT hr = obj_query(&obj->vtbl_w, riid, out);
    obj_release(&obj->vtbl_w);
    return hr;
}

/*
 * with an outer unknown, dinput8 makes an object it never hands back and
 * returns success with a null pointer; dinput.dll returns its inner
 * unknown, uninitialized.
 */
HRESULT DiCreateHelper(HINSTANCE instance, DWORD version, REFIID riid, void **out, IUnknown *outer, BOOL di8)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (outer && di8)
        return DI_OK;
    if (outer)
        return IsEqualGUID(riid, &IID_IUnknown) ? DiCreateObject(riid, out, FALSE) : CLASS_E_NOAGGREGATION;
    void *iface;
    HRESULT hr = DiCreateObject(riid, &iface, di8);
    if (FAILED(hr))
        return hr;
    hr = obj_initialize(iface, instance, version);
    if (FAILED(hr)) {
        obj_release(iface);
        return hr;
    }
    *out = iface;
    return DI_OK;
}

/* ------------------------------------------------------------------ the class factory */

typedef struct FACTORY {
    const IClassFactoryVtbl *vtbl;
    BOOL device;
} FACTORY;

static HRESULT STDMETHODCALLTYPE factory_query(IClassFactory *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IClassFactory)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_addref(IClassFactory *This)
{
    InterlockedIncrement(&DiLocks);
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    InterlockedDecrement(&DiLocks);
    return 1;
}

/* objects made here are uninitialized: their methods fail until Initialize. */
static HRESULT STDMETHODCALLTYPE factory_create(IClassFactory *This, IUnknown *outer, REFIID riid, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return ((FACTORY *)This)->device ? DiNewDevice(riid, out, DiIs8) : DiCreateObject(riid, out, DiIs8);
}

static HRESULT STDMETHODCALLTYPE factory_lock(IClassFactory *This, BOOL locked)
{
    if (locked)
        InterlockedIncrement(&DiLocks);
    else
        InterlockedDecrement(&DiLocks);
    return S_OK;
}

static const IClassFactoryVtbl factory_vtbl = {factory_query, factory_addref, factory_release, factory_create,
                                               factory_lock};
static FACTORY object_factory = {&factory_vtbl, FALSE};
static FACTORY device_factory = {&factory_vtbl, TRUE};

HRESULT DiGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    FACTORY *factory = nullptr;
    if (IsEqualGUID(clsid, DiIs8 ? &CLSID_DirectInput8 : &CLSID_DirectInput))
        factory = &object_factory;
    else if (IsEqualGUID(clsid, DiIs8 ? &CLSID_DirectInputDevice8 : &CLSID_DirectInputDevice))
        factory = &device_factory;
    if (!factory)
        return CLASS_E_CLASSNOTAVAILABLE;
    return factory_query((IClassFactory *)factory, riid, out);
}
