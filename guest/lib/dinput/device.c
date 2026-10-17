/*
 * the device object: IDirectInputDevice8 for dinput8.dll, IDirectInputDevice
 * to IDirectInputDevice7 for dinput.dll. what keyboards, mice and game
 * controllers share is here, as windows keeps it in its generic device;
 * the rest is in their classes. the A methods convert and call the W ones.
 */
#include "dinput_private.h"

static const IDirectInputDevice8WVtbl dev8w_vtbl;
static const IDirectInputDevice8AVtbl dev8a_vtbl;
static const IDirectInputDevice7WVtbl dev7w_vtbl;
static const IDirectInputDevice7AVtbl dev7a_vtbl;

DIDEV *DiDeviceFrom(void *iface)
{
    const void *vtbl = *(const void **)iface;
    if (vtbl == &dev8w_vtbl || vtbl == &dev7w_vtbl)
        return CONTAINING_RECORD(iface, DIDEV, vtbl_w);
    return CONTAINING_RECORD(iface, DIDEV, vtbl_a);
}

BOOL DiDeviceWide(void *iface)
{
    const void *vtbl = *(const void **)iface;
    return vtbl == &dev8w_vtbl || vtbl == &dev7w_vtbl;
}

static void lock(DIDEV *dev)
{
    RtlEnterCriticalSection(&dev->lock);
}

static HRESULT unlock(DIDEV *dev, HRESULT hr)
{
    RtlLeaveCriticalSection(&dev->lock);
    return hr;
}

/* ------------------------------------------------------------------ unknown */

static BOOL device_iid(DIDEV *dev, REFIID riid, BOOL *w)
{
    if (dev->di8) {
        *w = IsEqualGUID(riid, &IID_IDirectInputDevice8W);
        return *w || IsEqualGUID(riid, &IID_IDirectInputDevice8A);
    }
    *w = IsEqualGUID(riid, &IID_IDirectInputDeviceW) || IsEqualGUID(riid, &IID_IDirectInputDevice2W) ||
         IsEqualGUID(riid, &IID_IDirectInputDevice7W);
    return *w || IsEqualGUID(riid, &IID_IDirectInputDeviceA) || IsEqualGUID(riid, &IID_IDirectInputDevice2A) ||
           IsEqualGUID(riid, &IID_IDirectInputDevice7A);
}

static HRESULT STDMETHODCALLTYPE dev_query(void *This, REFIID riid, void **out)
{
    DIDEV *dev = DiDeviceFrom(This);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    BOOL w;
    if (IsEqualGUID(riid, &IID_IUnknown))
        *out = &dev->vtbl_w;
    else if (device_iid(dev, riid, &w))
        *out = w ? &dev->vtbl_w : &dev->vtbl_a;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&dev->ref);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE dev_addref(void *This)
{
    return (ULONG)InterlockedIncrement(&DiDeviceFrom(This)->ref);
}

static HRESULT unacquire(DIDEV *dev);

static ULONG STDMETHODCALLTYPE dev_release(void *This)
{
    DIDEV *dev = DiDeviceFrom(This);
    LONG ref = InterlockedDecrement(&dev->ref);
    if (!ref) {
        lock(dev);
        unacquire(dev);
        unlock(dev, DI_OK);
        if (dev->event)
            CloseHandle(dev->event);
        HeapFree(GetProcessHeap(), 0, dev->ring);
        RtlDeleteCriticalSection(&dev->lock);
        HeapFree(GetProcessHeap(), 0, dev);
        InterlockedDecrement(&DiLocks);
    }
    return (ULONG)ref;
}

/* ------------------------------------------------------------------ initialization */

/* the system devices: which class, and whether the guid is an alias of the real one. */
static const struct {
    const GUID *guid;
    BOOL keyboard;
    BOOL alias;
} SYSTEM[] = {
    {&GUID_SysMouse, FALSE, FALSE},    {&GUID_SysMouseEm, FALSE, TRUE},    {&GUID_SysMouseEm2, FALSE, TRUE},
    {&GUID_SysKeyboard, TRUE, FALSE},  {&GUID_SysKeyboardEm, TRUE, TRUE},  {&GUID_SysKeyboardEm2, TRUE, TRUE},
};

/* windows resets a device it initializes again, refusing while acquired. */
static HRESULT initialize(DIDEV *dev, HINSTANCE instance, DWORD version, REFGUID guid)
{
    if (!guid)
        return E_POINTER;
    HRESULT hr = DiCheckVersion(instance, version, dev->di8);
    if (FAILED(hr))
        return hr;
    if (dev->acquired)
        return DIERR_ACQUIRED;
    DWORD which = ~0u;
    for (DWORD i = 0; i < RTL_NUMBER_OF(SYSTEM); i++)
        if (IsEqualGUID(guid, SYSTEM[i].guid))
            which = i;
    if (which == ~0u)
        return DIERR_DEVICENOTREG;
    if (!SYSTEM[which].keyboard && !GetSystemMetrics(SM_MOUSEPRESENT))
        return DIERR_DEVICENOTREG;
    dev->formatted = FALSE;
    dev->missing_count = 0;
    if (dev->event) {
        CloseHandle(dev->event);
        dev->event = nullptr;
    }
    dev->relative = FALSE;
    dev->max_buffer = 1023;
    dev->ffgain = 10000;
    dev->status = DIERR_NOTACQUIRED;
    dev->buffer_prop = 0;
    HeapFree(GetProcessHeap(), 0, dev->ring);
    dev->ring = nullptr;
    dev->slots = dev->head = dev->tail = 0;
    dev->coop = 0;
    dev->window = nullptr;
    dev->em = 0;
    dev->version = version;
    dev->cls = SYSTEM[which].keyboard ? &DiKeyboardClass : &DiMouseClass;
    dev->instance = *guid;
    dev->product = SYSTEM[which].keyboard ? GUID_SysKeyboard : GUID_SysMouse;
    dev->caps = DIDC_ATTACHED | DIDC_EMULATED | (SYSTEM[which].alias ? DIDC_ALIAS : 0);
    for (DWORD i = 0; i < DI_MAX_OBJECTS; i++)
        dev->app_ofs[i] = ~0u;
    return dev->cls->init(dev);
}

static HRESULT STDMETHODCALLTYPE dev_initialize(void *This, HINSTANCE instance, DWORD version, REFGUID guid)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    return unlock(dev, initialize(dev, instance, version, guid));
}

HRESULT DiNewDevice(REFIID riid, void **out, BOOL di8)
{
    DIDEV *dev = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*dev));
    if (!dev)
        return E_OUTOFMEMORY;
    dev->vtbl_w = di8 ? (const void *)&dev8w_vtbl : (const void *)&dev7w_vtbl;
    dev->vtbl_a = di8 ? (const void *)&dev8a_vtbl : (const void *)&dev7a_vtbl;
    dev->di8 = di8;
    dev->ref = 1;
    dev->status = DIERR_NOTACQUIRED;
    dev->max_buffer = 1023;
    dev->ffgain = 10000;
    InitializeListHead(&dev->links);
    RtlInitializeCriticalSection(&dev->lock);
    InterlockedIncrement(&DiLocks);
    HRESULT hr = dev_query(&dev->vtbl_w, riid, out);
    dev_release(&dev->vtbl_w);
    return hr;
}

/* CreateDevice: the interface asked for, then Initialize with the object's version unless aggregated. */
HRESULT DiCreateDevice(DIOBJ *obj, REFGUID guid, REFIID riid, void **out, IUnknown *outer)
{
    *out = nullptr;
    void *iface;
    HRESULT hr = DiNewDevice(outer ? &IID_IUnknown : riid, &iface, obj->di8);
    if (FAILED(hr))
        return hr;
    if (!outer) {
        hr = dev_initialize(iface, DiModule, obj->version, guid);
        if (FAILED(hr)) {
            dev_release(iface);
            return hr;
        }
    }
    *out = iface;
    return DI_OK;
}

/* ------------------------------------------------------------------ capabilities and information */

HRESULT DiDeviceCaps(DIDEV *dev, DIDEVCAPS *caps)
{
    DWORD size = caps->dwSize;
    memset((BYTE *)caps + sizeof(DWORD), 0, size - sizeof(DWORD));
    caps->dwFlags = dev->caps;
    caps->dwDevType = dev->devtype;
    caps->dwAxes = dev->axes;
    caps->dwButtons = dev->buttons;
    caps->dwPOVs = dev->povs;
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE dev_get_capabilities(void *This, LPDIDEVCAPS caps)
{
    DIDEV *dev = DiDeviceFrom(This);
    if (!caps)
        return E_POINTER;
    if (caps->dwSize != sizeof(DIDEVCAPS) && caps->dwSize != sizeof(DIDEVCAPS_DX3))
        return E_INVALIDARG;
    if (!dev->cls)
        return DIERR_NOTINITIALIZED;
    return DiDeviceCaps(dev, caps);
}

HRESULT DiDeviceInfo(DIDEV *dev, DIDEVICEINSTANCEW *info)
{
    DWORD size = info->dwSize;
    memset((BYTE *)info + sizeof(DWORD), 0, size - sizeof(DWORD));
    info->guidInstance = dev->instance;
    info->guidProduct = dev->product;
    info->dwDevType = dev->devtype;
    DiCopyW(info->tszInstanceName, dev->name, DI_MAX_PATH);
    DiCopyW(info->tszProductName, dev->name, DI_MAX_PATH);
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE dev_get_device_info_w(void *This, LPDIDEVICEINSTANCEW info)
{
    DIDEV *dev = DiDeviceFrom(This);
    if (!info)
        return E_POINTER;
    if (info->dwSize != sizeof(DIDEVICEINSTANCEW) && info->dwSize != sizeof(DIDEVICEINSTANCE_DX3W))
        return E_INVALIDARG;
    if (!dev->cls)
        return DIERR_NOTINITIALIZED;
    return DiDeviceInfo(dev, info);
}

static HRESULT STDMETHODCALLTYPE dev_get_device_info_a(void *This, LPDIDEVICEINSTANCEA info)
{
    DIDEV *dev = DiDeviceFrom(This);
    if (!info)
        return E_POINTER;
    DWORD size = info->dwSize;
    if (size != sizeof(DIDEVICEINSTANCEA) && size != sizeof(DIDEVICEINSTANCE_DX3A))
        return E_INVALIDARG;
    if (!dev->cls)
        return DIERR_NOTINITIALIZED;
    DIDEVICEINSTANCEW w = {sizeof(w)};
    DiDeviceInfo(dev, &w);
    memset((BYTE *)info + sizeof(DWORD), 0, size - sizeof(DWORD));
    info->guidInstance = w.guidInstance;
    info->guidProduct = w.guidProduct;
    info->dwDevType = w.dwDevType;
    DiToA(info->tszInstanceName, w.tszInstanceName, sizeof(info->tszInstanceName));
    DiToA(info->tszProductName, w.tszProductName, sizeof(info->tszProductName));
    if (size >= sizeof(DIDEVICEINSTANCEA)) {
        info->guidFFDriver = w.guidFFDriver;
        info->wUsagePage = w.wUsagePage;
        info->wUsage = w.wUsage;
    }
    return DI_OK;
}

/* ------------------------------------------------------------------ objects */

/* an object as EnumObjects and GetObjectInfo describe it; false when it has no name. */
static BOOL describe(DIDEV *dev, DWORD index, DIDEVICEOBJECTINSTANCEW *out)
{
    const DIOBJECT *obj = &dev->objects[index];
    DWORD size = out->dwSize;
    memset((BYTE *)out + sizeof(DWORD), 0, size - sizeof(DWORD));
    out->guidType = *obj->guid;
    out->dwOfs = obj->ofs;
    out->dwType = obj->type;
    out->dwFlags = obj->flags;
    return dev->cls->name(dev, obj, out->tszName);
}

static void object_to_a(const DIDEVICEOBJECTINSTANCEW *w, DIDEVICEOBJECTINSTANCEA *a, DWORD size)
{
    memset((BYTE *)a + sizeof(DWORD), 0, size - sizeof(DWORD));
    a->dwSize = size;
    a->guidType = w->guidType;
    a->dwOfs = w->dwOfs;
    a->dwType = w->dwType;
    a->dwFlags = w->dwFlags;
    DiToA(a->tszName, w->tszName, sizeof(a->tszName));
    if (size >= sizeof(DIDEVICEOBJECTINSTANCEA)) {
        a->dwFFMaxForce = w->dwFFMaxForce;
        a->dwFFForceResolution = w->dwFFForceResolution;
        a->wCollectionNumber = w->wCollectionNumber;
        a->wDesignatorIndex = w->wDesignatorIndex;
        a->wUsagePage = w->wUsagePage;
        a->wUsage = w->wUsage;
        a->dwDimension = w->dwDimension;
        a->wExponent = w->wExponent;
        a->wReportId = w->wReportId;
    }
}

/*
 * windows' filter: alias and vendor objects only when asked, any type of
 * the filter, the attributes it names, then the collection, the first
 * object of another collection ending the walk.
 */
static HRESULT enum_objects(DIDEV *dev, void *callback, void *context, DWORD flags, BOOL w)
{
    if (!callback || (flags & 0xE0000020))
        return E_INVALIDARG;
    if (!dev->cls)
        return DIERR_NOTINITIALIZED;
    DWORD include = flags & (DIDFT_ALIAS | DIDFT_VENDORDEFINED);
    DWORD filter = flags & ~(DIDFT_ALIAS | DIDFT_VENDORDEFINED);
    DWORD collection = (filter >> 8) & 0xFFFF;
    filter &= 0xFF0000FF;
    if (!(filter & 0xFF))
        filter |= 0xDF;
    for (DWORD i = 0; i < dev->count; i++) {
        const DIOBJECT *obj = &dev->objects[i];
        if ((obj->type & DIDFT_ALIAS) && !(include & DIDFT_ALIAS))
            continue;
        if ((obj->type & DIDFT_VENDORDEFINED) && !(include & DIDFT_VENDORDEFINED))
            continue;
        if (!(obj->type & filter & 0xFF) || ((filter & 0xFF000000) & ~obj->type))
            continue;
        if (collection && collection != 0xFFFF)
            break;
        DIDEVICEOBJECTINSTANCEW info = {sizeof(info)};
        if (!describe(dev, i, &info))
            continue;
        BOOL more;
        if (w) {
            more = ((LPDIENUMDEVICEOBJECTSCALLBACKW)callback)(&info, context);
        } else {
            DIDEVICEOBJECTINSTANCEA a;
            object_to_a(&info, &a, sizeof(a));
            more = ((LPDIENUMDEVICEOBJECTSCALLBACKA)callback)(&a, context);
        }
        if (!more)
            break;
    }
    return DI_OK;
}

static HRESULT STDMETHODCALLTYPE dev_enum_objects_w(void *This, LPDIENUMDEVICEOBJECTSCALLBACKW callback,
                                                    LPVOID context, DWORD flags)
{
    return enum_objects(DiDeviceFrom(This), (void *)callback, context, flags, TRUE);
}

static HRESULT STDMETHODCALLTYPE dev_enum_objects_a(void *This, LPDIENUMDEVICEOBJECTSCALLBACKA callback,
                                                    LPVOID context, DWORD flags)
{
    return enum_objects(DiDeviceFrom(This), (void *)callback, context, flags, FALSE);
}

/* an object for DIPH_*: its index, or the error windows gives. */
static HRESULT resolve(DIDEV *dev, DWORD id, DWORD how, int *index)
{
    *index = -1;
    switch (how) {
    case DIPH_DEVICE:
        return id ? E_INVALIDARG : DI_OK;
    case DIPH_BYOFFSET:
    case DIPH_BYID:
        *index = DiFindObject(dev, id, how);
        return *index < 0 ? DIERR_OBJECTNOTFOUND : DI_OK;
    case DIPH_BYUSAGE:
        return DIERR_UNSUPPORTED;
    default:
        return E_INVALIDARG;
    }
}

static HRESULT object_info(DIDEV *dev, DIDEVICEOBJECTINSTANCEW *info, DWORD id, DWORD how)
{
    if (!dev->cls)
        return DIERR_NOTINITIALIZED;
    if (how == DIPH_DEVICE)
        return E_INVALIDARG;
    int index;
    HRESULT hr = resolve(dev, id, how, &index);
    if (FAILED(hr))
        return hr;
    return describe(dev, (DWORD)index, info) ? DI_OK : DIERR_OBJECTNOTFOUND;
}

static HRESULT STDMETHODCALLTYPE dev_get_object_info_w(void *This, LPDIDEVICEOBJECTINSTANCEW info, DWORD id,
                                                       DWORD how)
{
    if (!info)
        return E_POINTER;
    if (info->dwSize != sizeof(DIDEVICEOBJECTINSTANCEW) && info->dwSize != sizeof(DIDEVICEOBJECTINSTANCE_DX3W))
        return E_INVALIDARG;
    return object_info(DiDeviceFrom(This), info, id, how);
}

static HRESULT STDMETHODCALLTYPE dev_get_object_info_a(void *This, LPDIDEVICEOBJECTINSTANCEA info, DWORD id,
                                                       DWORD how)
{
    if (!info)
        return E_POINTER;
    DWORD size = info->dwSize;
    if (size != sizeof(DIDEVICEOBJECTINSTANCEA) && size != sizeof(DIDEVICEOBJECTINSTANCE_DX3A))
        return E_INVALIDARG;
    DIDEVICEOBJECTINSTANCEW w = {sizeof(w)};
    HRESULT hr = object_info(DiDeviceFrom(This), &w, id, how);
    if (SUCCEEDED(hr))
        object_to_a(&w, info, size);
    return hr;
}

/* ------------------------------------------------------------------ properties */

enum {
    PROP_OBJECT_ONLY = 0x01,
    PROP_DEVICE_ONLY = 0x02,
    PROP_READ_ONLY = 0x04,
    PROP_NOT_ACQUIRED = 0x10,
};

typedef struct PROPERTY {
    DWORD id, size, flags;
    BOOL di8_only;
} PROPERTY;

static const PROPERTY PROPERTIES[] = {
    {1, sizeof(DIPROPDWORD), 0x1A},
    {2, sizeof(DIPROPDWORD), 0x1A},
    {3, sizeof(DIPROPDWORD), 0x15},
    {4, sizeof(DIPROPRANGE), 0x11},
    {5, sizeof(DIPROPDWORD), 0x10},
    {6, sizeof(DIPROPDWORD), 0x10},
    {7, sizeof(DIPROPDWORD), 0x02},
    {8, sizeof(DIPROPDWORD), 0x06},
    {9, sizeof(DIPROPDWORD), 0x12},
    {10, sizeof(DIPROPDWORD), 0x12},
    {11, sizeof(DIPROPCAL), 0x11},
    {12, sizeof(DIPROPGUIDANDPATH), 0x06},
    {13, sizeof(DIPROPSTRING), 0x02},
    {14, sizeof(DIPROPSTRING), 0x02},
    {15, sizeof(DIPROPDWORD), 0x12},
    {16, sizeof(DIPROPSTRING), 0x06},
    {18, sizeof(DIPROPRANGE), 0x11, TRUE},
    {19, sizeof(DIPROPRANGE), 0x11, TRUE},
    {20, sizeof(DIPROPSTRING), 0x11, TRUE},
    {21, sizeof(DIPROPCPOINTS), 0x11, TRUE},
    {22, sizeof(DIPROPPOINTER), 0x11, TRUE},
    {23, sizeof(DIPROPDWORD), 0x11, TRUE},
    {24, sizeof(DIPROPDWORD), 0x06, TRUE},
    {25, sizeof(DIPROPSTRING), 0x06, TRUE},
    {26, sizeof(DIPROPSTRING), 0x06, TRUE},
    {0xFFFF, sizeof(DIPROPDWORD), 0x1A},
};

/* windows' checks of a property call, in its order; the property and object when they pass. */
static HRESULT check_property(DIDEV *dev, REFGUID guid, const DIPROPHEADER *h, const PROPERTY **prop, int *index)
{
    if (!h || h->dwHeaderSize != sizeof(DIPROPHEADER) || (h->dwSize & 3) || h->dwSize < sizeof(DIPROPHEADER))
        return E_INVALIDARG;
    if (!dev->cls)
        return DIERR_NOTINITIALIZED;
    HRESULT hr = resolve(dev, h->dwObj, h->dwHow, index);
    if (FAILED(hr))
        return hr;
    ULONG_PTR id = (ULONG_PTR)guid;
    *prop = nullptr;
    for (DWORD i = 0; id <= 0xFFFF && i < RTL_NUMBER_OF(PROPERTIES); i++)
        if (PROPERTIES[i].id == id && (dev->di8 || !PROPERTIES[i].di8_only))
            *prop = &PROPERTIES[i];
    if (!*prop)
        return DIERR_UNSUPPORTED;
    BOOL size_ok = h->dwSize == (*prop)->size || (id == 11 && h->dwSize == sizeof(DIPROPCALPOV));
    if (!size_ok)
        return E_INVALIDARG;
    if ((id == 20 || id == 23) && h->dwHow == DIPH_DEVICE)
        return E_INVALIDARG;
    return DI_OK;
}

static HRESULT level_check(const PROPERTY *prop, int index)
{
    if ((prop->flags & PROP_OBJECT_ONLY) && index < 0)
        return DIERR_UNSUPPORTED;
    if ((prop->flags & PROP_DEVICE_ONLY) && index >= 0)
        return DIERR_UNSUPPORTED;
    return DI_OK;
}

static BOOL format_has_axis(DIDEV *dev)
{
    for (DWORD i = 0; i < dev->count; i++)
        if (dev->app_ofs[i] != ~0u && (dev->objects[i].type & DIDFT_AXIS))
            return TRUE;
    return FALSE;
}

static HRESULT get_property(DIDEV *dev, REFGUID guid, DIPROPHEADER *h)
{
    const PROPERTY *prop;
    int index;
    HRESULT hr = check_property(dev, guid, h, &prop, &index);
    if (FAILED(hr))
        return hr;
    const DIOBJECT *obj = index >= 0 ? &dev->objects[index] : nullptr;
    if (dev->cls->get_property) {
        hr = dev->cls->get_property(dev, prop->id, obj, h);
        if (hr != E_NOTIMPL)
            return hr;
    }
    hr = level_check(prop, index);
    if (FAILED(hr))
        return hr;
    DIPROPDWORD *d = (DIPROPDWORD *)h;
    switch (prop->id) {
    case 1:
        d->dwData = dev->buffer_prop;
        return DI_OK;
    case 2:
        d->dwData = dev->relative ? DIPROPAXISMODE_REL : DIPROPAXISMODE_ABS;
        return DI_OK;
    case 3:
        if (!(obj->type & DIDFT_AXIS))
            return DIERR_UNSUPPORTED;
        d->dwData = 1;
        return DI_OK;
    case 4:
        if (!(obj->type & DIDFT_RELAXIS))
            return DIERR_UNSUPPORTED;
        ((DIPROPRANGE *)h)->lMin = DIPROPRANGE_NOMIN;
        ((DIPROPRANGE *)h)->lMax = DIPROPRANGE_NOMAX;
        return DI_OK;
    case 7:
        d->dwData = dev->ffgain;
        return DI_OK;
    case 22:
        if (!dev->formatted)
            return DIERR_NOTINITIALIZED;
        if (dev->app_ofs[index] == ~0u)
            return DIERR_OBJECTNOTFOUND;
        ((DIPROPPOINTER *)h)->uData = dev->appdata[index];
        return DI_OK;
    case 25:
        DiCopyW(((DIPROPSTRING *)h)->wsz, dev->user, DI_MAX_PATH);
        return dev->user[0] ? DI_OK : S_FALSE;
    case 0xFFFF:
        d->dwData = dev->max_buffer;
        return DI_OK;
    default:
        return DIERR_UNSUPPORTED;
    }
}

/* the ring belongs to the data path's lock, which the input thread buffers under. */
static HRESULT set_buffer(DIDEV *dev, DWORD size)
{
    DWORD slots = size > dev->max_buffer ? dev->max_buffer : size;
    DIEVENT *ring = nullptr;
    if (slots && !(ring = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, slots * sizeof(*ring))))
        return E_OUTOFMEMORY;
    RtlEnterCriticalSection(&DiLock);
    DIEVENT *old = dev->ring;
    dev->ring = ring;
    dev->slots = slots;
    dev->head = dev->tail = 0;
    dev->overflow = FALSE;
    dev->buffer_prop = size;
    RtlLeaveCriticalSection(&DiLock);
    HeapFree(GetProcessHeap(), 0, old);
    return DI_OK;
}

static HRESULT set_property(DIDEV *dev, REFGUID guid, const DIPROPHEADER *h)
{
    const PROPERTY *prop;
    int index;
    HRESULT hr = check_property(dev, guid, h, &prop, &index);
    if (FAILED(hr))
        return hr;
    hr = level_check(prop, index);
    if (FAILED(hr))
        return hr;
    if (prop->flags & PROP_READ_ONLY)
        return DIERR_READONLY;
    if ((prop->flags & PROP_NOT_ACQUIRED) && dev->acquired)
        return DIERR_ACQUIRED;
    DWORD value = ((const DIPROPDWORD *)h)->dwData;
    switch (prop->id) {
    case 1:
        return set_buffer(dev, value);
    case 2:
        if (value > DIPROPAXISMODE_REL)
            return E_INVALIDARG;
        dev->relative = value == DIPROPAXISMODE_REL;
        return format_has_axis(dev) ? DI_OK : DI_PROPNOEFFECT;
    case 5:
    case 6:
    case 10:
        /* applied to every absolute axis, which keyboards and mice have none of. */
        return index < 0 ? DI_OK : DIERR_UNSUPPORTED;
    case 7:
        if (value > 0xFFFF)
            return E_INVALIDARG;
        dev->ffgain = value;
        return DI_OK;
    case 9:
        if (value > 1)
            return E_INVALIDARG;
        return DIERR_UNSUPPORTED;
    case 22:
        if (!dev->formatted)
            return DIERR_NOTINITIALIZED;
        if (dev->app_ofs[index] == ~0u)
            return DIERR_OBJECTNOTFOUND;
        dev->appdata[index] = ((const DIPROPPOINTER *)h)->uData;
        return DI_OK;
    case 0xFFFF:
        dev->max_buffer = value;
        return DI_OK;
    default:
        return DIERR_UNSUPPORTED;
    }
}

static HRESULT STDMETHODCALLTYPE dev_get_property(void *This, REFGUID guid, LPDIPROPHEADER h)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    return unlock(dev, get_property(dev, guid, h));
}

static HRESULT STDMETHODCALLTYPE dev_set_property(void *This, REFGUID guid, LPCDIPROPHEADER h)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    return unlock(dev, set_property(dev, guid, h));
}

/* ------------------------------------------------------------------ cooperation and acquisition */

static HRESULT STDMETHODCALLTYPE dev_set_cooperative_level(void *This, HWND window, DWORD flags)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    if (dev->acquired)
        return unlock(dev, DIERR_ACQUIRED);
    if (flags & ~0x1Fu)
        return unlock(dev, E_INVALIDARG);
    DWORD share = flags & (DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE), focus = flags & (DISCL_FOREGROUND | DISCL_BACKGROUND);
    if ((share != DISCL_EXCLUSIVE && share != DISCL_NONEXCLUSIVE) ||
        (focus != DISCL_FOREGROUND && focus != DISCL_BACKGROUND))
        return unlock(dev, E_INVALIDARG);
    if (window && (!IsWindow(window) || (GetWindowLongW(window, GWL_STYLE) & WS_CHILD)))
        return unlock(dev, E_HANDLE);
    if (!window && (flags & (DISCL_EXCLUSIVE | DISCL_FOREGROUND)))
        return unlock(dev, E_HANDLE);
    if (!dev->di8 && dev->version < 0x0700) {
        if ((flags & DISCL_NOWINKEY) || ((flags & DISCL_EXCLUSIVE) && LOBYTE(dev->devtype) == DIDEVTYPE_KEYBOARD))
            return unlock(dev, E_INVALIDARG);
    }
    if (!dev->cls)
        return unlock(dev, DIERR_NOTINITIALIZED);
    HRESULT hr = dev->cls->cooperate(dev, window, flags);
    if (FAILED(hr))
        return unlock(dev, hr);
    dev->window = window;
    dev->coop = flags;
    return unlock(dev, DI_OK);
}

static BOOL foreground_ok(DIDEV *dev)
{
    return GetForegroundWindow() == dev->window && !IsIconic(dev->window);
}

/* windows' order: the format, the foreground, exclusivity, the class, the data path, then the foreground watch. */
static HRESULT acquire(DIDEV *dev)
{
    dev->status = DIERR_NOTACQUIRED;
    if (!dev->cls)
        return DIERR_NOTINITIALIZED;
    if (!dev->formatted)
        return E_INVALIDARG;
    if (dev->acquired)
        return S_FALSE;
    if (dev->coop & DISCL_FOREGROUND) {
        DWORD pid = 0;
        GetWindowThreadProcessId(dev->window, &pid);
        if (pid != GetCurrentProcessId()) {
            dev->window = (HWND)(LONG_PTR)-1;
            return E_INVALIDARG;
        }
        if (!foreground_ok(dev))
            return DIERR_OTHERAPPHASPRIO;
    }
    HRESULT hr;
    if ((dev->coop & DISCL_EXCLUSIVE) && !DiExclusiveTake(dev, &hr))
        return hr;
    dev->acquired = TRUE;
    dev->lost_iconic = FALSE;
    dev->cls->acquire(dev);
    hr = DiEmulAcquire(dev);
    if (FAILED(hr)) {
        dev->acquired = FALSE;
        dev->cls->unacquire(dev);
        if (dev->coop & DISCL_EXCLUSIVE)
            DiExclusiveRelease(dev);
        return hr;
    }
    /* relative reads start from now. */
    BYTE native[DI_MAX_STATE];
    RtlEnterCriticalSection(&DiLock);
    dev->cls->state(dev, native);
    RtlLeaveCriticalSection(&DiLock);
    for (DWORD i = 0; i < RTL_NUMBER_OF(dev->baseline) && (i + 1) * 4 <= dev->native_size; i++)
        memcpy(&dev->baseline[i], native + i * 4, sizeof(LONG));
    if (dev->coop & DISCL_FOREGROUND) {
        DiEmulForeground(dev);
        if (!foreground_ok(dev)) {
            unacquire(dev);
            return DIERR_OTHERAPPHASPRIO;
        }
    }
    dev->status = DIERR_INPUTLOST;
    return DI_OK;
}

static HRESULT unacquire(DIDEV *dev)
{
    dev->status = DIERR_NOTACQUIRED;
    if (!dev->acquired)
        return S_FALSE;
    dev->acquired = FALSE;
    if (dev->coop & DISCL_EXCLUSIVE)
        DiExclusiveRelease(dev);
    DiEmulUnacquire(dev);
    dev->cls->unacquire(dev);
    return DI_OK;
}

/* acquisition taken away: reads report it, the event says so, a minimized window gets it back. */
void DiDeviceLost(DIDEV *dev)
{
    lock(dev);
    if (dev->acquired) {
        dev->acquired = FALSE;
        if (dev->coop & DISCL_EXCLUSIVE)
            DiExclusiveRelease(dev);
        DiEmulUnacquire(dev);
        dev->cls->unacquire(dev);
        dev->status = DIERR_INPUTLOST;
        dev->lost_iconic = dev->window && IsWindow(dev->window) && IsIconic(dev->window);
        if (dev->event)
            SetEvent(dev->event);
    }
    unlock(dev, DI_OK);
}

static HRESULT STDMETHODCALLTYPE dev_acquire(void *This)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    return unlock(dev, acquire(dev));
}

static HRESULT STDMETHODCALLTYPE dev_unacquire(void *This)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    if (!dev->cls)
        return unlock(dev, DIERR_NOTINITIALIZED);
    return unlock(dev, unacquire(dev));
}

/* a device lost while its window was minimized comes back once the window does. */
static void reacquire(DIDEV *dev)
{
    if (!dev->acquired && dev->lost_iconic && dev->window && IsWindow(dev->window) && !IsIconic(dev->window))
        acquire(dev);
}

/* ------------------------------------------------------------------ state and data */

static HRESULT STDMETHODCALLTYPE dev_get_device_state(void *This, DWORD size, LPVOID data)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    if (!dev->cls)
        return unlock(dev, DIERR_NOTINITIALIZED);
    reacquire(dev);
    if (!dev->acquired)
        return unlock(dev, dev->status);
    if (size != dev->app_size)
        return unlock(dev, E_INVALIDARG);
    RtlEnterCriticalSection(&DiLock);
    DiReadState(dev, data);
    RtlLeaveCriticalSection(&DiLock);
    return unlock(dev, DI_OK);
}

/*
 * a ring of as many slots as the buffer size, where an event filling the
 * last free one drops the oldest: one fewer than the size is kept
 * (guessed: windows' ring arithmetic, worth checking on windows). called
 * with the data path's lock held.
 */
void DiBufferEvent(DIDEV *dev, DWORD native_ofs, DWORD data, DWORD time, DWORD sequence)
{
    if (!dev->slots)
        return;
    DWORD index = ~0u;
    for (DWORD i = 0; i < dev->count && index == ~0u; i++)
        if (dev->objects[i].ofs == native_ofs)
            index = i;
    if (index == ~0u || dev->app_ofs[index] == ~0u)
        return;
    DIEVENT *e = &dev->ring[dev->head];
    *e = (DIEVENT){dev->app_ofs[index], data, time, sequence, dev->appdata[index]};
    dev->head = (dev->head + 1) % dev->slots;
    if (dev->head == dev->tail) {
        dev->tail = (dev->tail + 1) % dev->slots;
        dev->overflow = TRUE;
    }
}

void DiSignal(DIDEV *dev)
{
    if (dev->event)
        SetEvent(dev->event);
}

static HRESULT get_device_data(DIDEV *dev, DWORD size, BYTE *data, DWORD *count, DWORD flags)
{
    DWORD record = dev->di8 ? sizeof(DIDEVICEOBJECTDATA) : sizeof(DIDEVICEOBJECTDATA_DX3);
    if (size != record)
        return dev->di8 ? E_INVALIDARG : DI_OK;
    if (flags & ~3u)
        return E_INVALIDARG;
    if (!dev->cls)
        return DIERR_NOTINITIALIZED;
    if (!dev->buffer_prop)
        return DIERR_NOTBUFFERED;
    DWORD wanted = *count < dev->buffer_prop ? *count : dev->buffer_prop;
    reacquire(dev);
    if (!dev->acquired && !(flags & 2))
        return dev->status;
    RtlEnterCriticalSection(&DiLock);
    DWORD done = 0, at = dev->tail;
    while (done < wanted && at != dev->head) {
        const DIEVENT *e = &dev->ring[at];
        if (data) {
            DIDEVICEOBJECTDATA out = {e->ofs, e->data, e->time, e->sequence, e->appdata};
            memcpy(data + (SIZE_T)done * record, &out, record);
        }
        at = (at + 1) % dev->slots;
        done++;
    }
    if (!(flags & DIGDD_PEEK))
        dev->tail = at;
    BOOL overflow = dev->overflow;
    dev->overflow = FALSE;
    RtlLeaveCriticalSection(&DiLock);
    *count = done;
    return overflow ? DI_BUFFEROVERFLOW : DI_OK;
}

static HRESULT STDMETHODCALLTYPE dev_get_device_data(void *This, DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD count,
                                                     DWORD flags)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    return unlock(dev, get_device_data(dev, size, (BYTE *)data, count, flags));
}

static HRESULT STDMETHODCALLTYPE dev_set_data_format(void *This, LPCDIDATAFORMAT format)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    if (!dev->cls && format && format->dwSize == sizeof(DIDATAFORMAT))
        return unlock(dev, DIERR_NOTINITIALIZED);
    return unlock(dev, DiSetDataFormat(dev, format));
}

/* the event is a duplicate, reset at once; a second one is refused. */
static HRESULT STDMETHODCALLTYPE dev_set_event_notification(void *This, HANDLE event)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    if (dev->acquired)
        return unlock(dev, DIERR_ACQUIRED);
    HANDLE copy = nullptr;
    if (event) {
        if (!DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &copy, EVENT_MODIFY_STATE, FALSE, 0))
            return unlock(dev, (HRESULT)(0x80070000u | (GetLastError() & 0xFFFF)));
        if (!ResetEvent(copy)) {
            CloseHandle(copy);
            return unlock(dev, E_HANDLE);
        }
        if (dev->event) {
            CloseHandle(copy);
            return unlock(dev, DIERR_HANDLEEXISTS);
        }
    }
    if (dev->event)
        CloseHandle(dev->event);
    dev->event = copy;
    return unlock(dev, DI_OK);
}

static HRESULT STDMETHODCALLTYPE dev_poll(void *This)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    if (!dev->cls)
        return unlock(dev, DIERR_NOTINITIALIZED);
    if (!dev->acquired)
        return unlock(dev, dev->status);
    return unlock(dev, !dev->di8 && dev->version < 0x05B2 ? DI_OK : DI_NOEFFECT);
}

static HRESULT STDMETHODCALLTYPE dev_send_device_data(void *This, DWORD size, LPCDIDEVICEOBJECTDATA data,
                                                      LPDWORD count, DWORD flags)
{
    DIDEV *dev = DiDeviceFrom(This);
    if (flags & ~DISDD_CONTINUE)
        return E_INVALIDARG;
    if (size != (dev->di8 ? sizeof(DIDEVICEOBJECTDATA) : sizeof(DIDEVICEOBJECTDATA_DX3)))
        return DI_OK;
    lock(dev);
    if (!dev->cls)
        return unlock(dev, DIERR_NOTINITIALIZED);
    if (!dev->acquired)
        return unlock(dev, dev->status);
    *count = 0;
    return unlock(dev, DIERR_UNSUPPORTED);
}

static HRESULT STDMETHODCALLTYPE dev_run_control_panel(void *This, HWND owner, DWORD flags)
{
    DIDEV *dev = DiDeviceFrom(This);
    if (owner && !IsWindow(owner))
        return E_HANDLE;
    if (flags)
        return E_INVALIDARG;
    if (!dev->cls)
        return DIERR_NOTINITIALIZED;
    WCHAR command[64];
    DiCopyW(command, dev->cls->control_panel, RTL_NUMBER_OF(command));
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return (HRESULT)(0x80070000u | (GetLastError() & 0xFFFF));
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return DI_OK;
}

/* ------------------------------------------------------------------ force feedback: keyboards and mice have none */

static HRESULT STDMETHODCALLTYPE dev_create_effect(void *This, REFGUID guid, LPCDIEFFECT effect,
                                                   LPDIRECTINPUTEFFECT *out, LPUNKNOWN outer)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return DiDeviceFrom(This)->cls ? DIERR_UNSUPPORTED : DIERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE dev_enum_effects(void *This, void *callback, LPVOID context, DWORD type)
{
    if (!callback || (type & 0xFBFFFF00))
        return E_INVALIDARG;
    return DiDeviceFrom(This)->cls ? DI_OK : DIERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE dev_get_effect_info_w(void *This, LPDIEFFECTINFOW info, REFGUID guid)
{
    if (!info)
        return E_POINTER;
    if (info->dwSize != sizeof(DIEFFECTINFOW))
        return E_INVALIDARG;
    return DiDeviceFrom(This)->cls ? DIERR_DEVICENOTREG : DIERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE dev_get_effect_info_a(void *This, LPDIEFFECTINFOA info, REFGUID guid)
{
    if (!info)
        return E_POINTER;
    if (info->dwSize != sizeof(DIEFFECTINFOA))
        return E_INVALIDARG;
    return DiDeviceFrom(This)->cls ? DIERR_DEVICENOTREG : DIERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE dev_get_ff_state(void *This, LPDWORD state)
{
    if (!state)
        return E_POINTER;
    *state = 0;
    return DiDeviceFrom(This)->cls ? DIERR_UNSUPPORTED : DIERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE dev_send_ff_command(void *This, DWORD flags)
{
    if (!flags || (flags & (flags - 1)) || flags > DISFFC_SETACTUATORSOFF)
        return E_INVALIDARG;
    return DiDeviceFrom(This)->cls ? DIERR_UNSUPPORTED : DIERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE dev_enum_created_effects(void *This, LPDIENUMCREATEDEFFECTOBJECTSCALLBACK callback,
                                                          LPVOID context, DWORD flags)
{
    if (!callback || flags)
        return E_INVALIDARG;
    return DiDeviceFrom(This)->cls ? DI_OK : DIERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE dev_escape(void *This, LPDIEFFESCAPE escape)
{
    if (!escape)
        return E_POINTER;
    if (escape->dwSize != sizeof(DIEFFESCAPE))
        return E_INVALIDARG;
    return DiDeviceFrom(This)->cls ? DIERR_UNSUPPORTED : DIERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE dev_enum_effects_in_file_w(void *This, LPCWSTR file,
                                                            LPDIENUMEFFECTSINFILECALLBACK callback, LPVOID context,
                                                            DWORD flags)
{
    return DiEnumEffectsInFile(file, callback, context, flags);
}

static HRESULT STDMETHODCALLTYPE dev_enum_effects_in_file_a(void *This, LPCSTR file,
                                                            LPDIENUMEFFECTSINFILECALLBACK callback, LPVOID context,
                                                            DWORD flags)
{
    WCHAR path[DI_MAX_PATH];
    if (!file || !MultiByteToWideChar(CP_ACP, 0, file, -1, path, DI_MAX_PATH))
        return E_INVALIDARG;
    return DiEnumEffectsInFile(path, callback, context, flags);
}

static HRESULT STDMETHODCALLTYPE dev_write_effect_to_file_w(void *This, LPCWSTR file, DWORD count,
                                                            LPDIFILEEFFECT effects, DWORD flags)
{
    return DiWriteEffectToFile(file, count, effects, flags);
}

static HRESULT STDMETHODCALLTYPE dev_write_effect_to_file_a(void *This, LPCSTR file, DWORD count,
                                                            LPDIFILEEFFECT effects, DWORD flags)
{
    WCHAR path[DI_MAX_PATH];
    if (!file || !MultiByteToWideChar(CP_ACP, 0, file, -1, path, DI_MAX_PATH))
        return E_INVALIDARG;
    return DiWriteEffectToFile(path, count, effects, flags);
}

/* ------------------------------------------------------------------ action mapping */

static HRESULT STDMETHODCALLTYPE dev_build_action_map_w(void *This, LPDIACTIONFORMATW format, LPCWSTR user,
                                                        DWORD flags)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    return unlock(dev, DiBuildActionMap(dev, format, user, flags));
}

static HRESULT STDMETHODCALLTYPE dev_set_action_map_w(void *This, LPDIACTIONFORMATW format, LPCWSTR user,
                                                      DWORD flags)
{
    DIDEV *dev = DiDeviceFrom(This);
    lock(dev);
    return unlock(dev, DiSetActionMap(dev, format, user, flags));
}

static HRESULT map_a(void *This, LPDIACTIONFORMATA format, LPCSTR user, DWORD flags, BOOL set)
{
    DIDEV *dev = DiDeviceFrom(This);
    DIACTIONFORMATW w;
    DIACTIONW *actions = nullptr;
    HRESULT hr = DiActionFormatToW(format, &w, &actions);
    if (FAILED(hr))
        return hr;
    WCHAR wuser[DI_MAX_PATH];
    if (user)
        MultiByteToWideChar(CP_ACP, 0, user, -1, wuser, DI_MAX_PATH);
    lock(dev);
    hr = set ? DiSetActionMap(dev, &w, user ? wuser : nullptr, flags)
             : DiBuildActionMap(dev, &w, user ? wuser : nullptr, flags);
    unlock(dev, hr);
    DiActionFormatBack(format, &w);
    HeapFree(GetProcessHeap(), 0, actions);
    return hr;
}

static HRESULT STDMETHODCALLTYPE dev_build_action_map_a(void *This, LPDIACTIONFORMATA format, LPCSTR user,
                                                        DWORD flags)
{
    return map_a(This, format, user, flags, FALSE);
}

static HRESULT STDMETHODCALLTYPE dev_set_action_map_a(void *This, LPDIACTIONFORMATA format, LPCSTR user,
                                                      DWORD flags)
{
    return map_a(This, format, user, flags, TRUE);
}

/* keyboards and mice have no images registered for their type. */
static HRESULT image_info(DIDEV *dev, DWORD size, DWORD image, DWORD buffer, const void *array, DWORD want)
{
    if (size != sizeof(DIDEVICEIMAGEINFOHEADERW) || image != want)
        return E_INVALIDARG;
    if (buffer && !array)
        return E_POINTER;
    return dev->cls ? DIERR_OBJECTNOTFOUND : DIERR_NOTINITIALIZED;
}

static HRESULT STDMETHODCALLTYPE dev_get_image_info_w(void *This, LPDIDEVICEIMAGEINFOHEADERW h)
{
    if (!h)
        return E_POINTER;
    return image_info(DiDeviceFrom(This), h->dwSize, h->dwSizeImageInfo, h->dwBufferSize, h->lprgImageInfoArray,
                      sizeof(DIDEVICEIMAGEINFOW));
}

static HRESULT STDMETHODCALLTYPE dev_get_image_info_a(void *This, LPDIDEVICEIMAGEINFOHEADERA h)
{
    if (!h)
        return E_POINTER;
    return image_info(DiDeviceFrom(This), h->dwSize, h->dwSizeImageInfo, h->dwBufferSize, h->lprgImageInfoArray,
                      sizeof(DIDEVICEIMAGEINFOA));
}

/* ------------------------------------------------------------------ vtables */

#define DEV_COMMON(OBJ, OBJINFO, DEVINFO, EFFINFO)                                                                     \
    (void *)dev_query, (void *)dev_addref, (void *)dev_release, (void *)dev_get_capabilities, (void *)OBJ,            \
        (void *)dev_get_property, (void *)dev_set_property, (void *)dev_acquire, (void *)dev_unacquire,               \
        (void *)dev_get_device_state, (void *)dev_get_device_data, (void *)dev_set_data_format,                       \
        (void *)dev_set_event_notification, (void *)dev_set_cooperative_level, (void *)OBJINFO, (void *)DEVINFO,      \
        (void *)dev_run_control_panel, (void *)dev_initialize, (void *)dev_create_effect, (void *)dev_enum_effects,   \
        (void *)EFFINFO, (void *)dev_get_ff_state, (void *)dev_send_ff_command, (void *)dev_enum_created_effects,     \
        (void *)dev_escape, (void *)dev_poll, (void *)dev_send_device_data

static const IDirectInputDevice8WVtbl dev8w_vtbl = {
    DEV_COMMON(dev_enum_objects_w, dev_get_object_info_w, dev_get_device_info_w, dev_get_effect_info_w),
    (void *)dev_enum_effects_in_file_w, (void *)dev_write_effect_to_file_w, (void *)dev_build_action_map_w,
    (void *)dev_set_action_map_w, (void *)dev_get_image_info_w};
static const IDirectInputDevice8AVtbl dev8a_vtbl = {
    DEV_COMMON(dev_enum_objects_a, dev_get_object_info_a, dev_get_device_info_a, dev_get_effect_info_a),
    (void *)dev_enum_effects_in_file_a, (void *)dev_write_effect_to_file_a, (void *)dev_build_action_map_a,
    (void *)dev_set_action_map_a, (void *)dev_get_image_info_a};
static const IDirectInputDevice7WVtbl dev7w_vtbl = {
    DEV_COMMON(dev_enum_objects_w, dev_get_object_info_w, dev_get_device_info_w, dev_get_effect_info_w),
    (void *)dev_enum_effects_in_file_w, (void *)dev_write_effect_to_file_w};
static const IDirectInputDevice7AVtbl dev7a_vtbl = {
    DEV_COMMON(dev_enum_objects_a, dev_get_object_info_a, dev_get_device_info_a, dev_get_effect_info_a),
    (void *)dev_enum_effects_in_file_a, (void *)dev_write_effect_to_file_a};
