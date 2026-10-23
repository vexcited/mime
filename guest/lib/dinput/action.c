/*
 * action mapping, dinput8 only: action format checks, the keyboard and
 * mouse maps built from DIKEYBOARD_ and DIMOUSE_ semantics without a
 * mapper, the data format SetActionMap installs, and the device order of
 * EnumDevicesBySemantics.
 */
#include "dinput_private.h"

/* A and W formats differ only in size. */
HRESULT DiCheckActionFormat(const DIACTIONFORMATW *f, BOOL wide)
{
    if (!f)
        return E_POINTER;
    if (f->dwSize != (wide ? sizeof(DIACTIONFORMATW) : sizeof(DIACTIONFORMATA)))
        return E_INVALIDARG;
    if (f->dwActionSize != sizeof(DIACTIONW) || f->dwDataSize != 4 * f->dwNumActions)
        return E_INVALIDARG;
    if (IsEqualGUID(&f->guidActionMap, &GUID_NULL))
        return E_INVALIDARG;
    DWORD genre = f->dwGenre >> 24;
    if (!(genre & 0x7F) || genre > 0x29)
        return E_INVALIDARG;
    if ((f->lAxisMin || f->lAxisMax) && f->lAxisMin > f->lAxisMax)
        return E_INVALIDARG;
    if (!f->dwNumActions || f->dwNumActions >= 0x1000000 || !f->rgoAction)
        return E_INVALIDARG;
    for (DWORD i = 0; i < f->dwNumActions; i++) {
        if (wide) {
            const WCHAR *name = f->rgoAction[i].lptszActionName;
            if (name && !IS_INTRESOURCE(name) && lstrlenW(name) >= 256)
                return E_INVALIDARG;
        } else {
            const CHAR *name = ((const DIACTIONA *)f->rgoAction)[i].lptszActionName;
            if (name && !IS_INTRESOURCE(name) && strlen(name) >= 256)
                return E_INVALIDARG;
        }
    }
    return DI_OK;
}

/* a W copy of an A format; the actions keep the A names, which only matter to the caller. */
HRESULT DiActionFormatToW(const DIACTIONFORMATA *a, DIACTIONFORMATW *w, DIACTIONW **actions)
{
    HRESULT hr = DiCheckActionFormat((const DIACTIONFORMATW *)a, FALSE);
    if (FAILED(hr))
        return hr;
    *actions = HeapAlloc(GetProcessHeap(), 0, a->dwNumActions * sizeof(DIACTIONW));
    if (!*actions)
        return E_OUTOFMEMORY;
    memcpy(*actions, a->rgoAction, a->dwNumActions * sizeof(DIACTIONW));
    memset(w, 0, sizeof(*w));
    w->dwSize = sizeof(*w);
    w->dwActionSize = a->dwActionSize;
    w->dwDataSize = a->dwDataSize;
    w->dwNumActions = a->dwNumActions;
    w->rgoAction = *actions;
    w->guidActionMap = a->guidActionMap;
    w->dwGenre = a->dwGenre;
    w->dwBufferSize = a->dwBufferSize;
    w->lAxisMin = a->lAxisMin;
    w->lAxisMax = a->lAxisMax;
    w->hInstString = a->hInstString;
    w->ftTimeStamp = a->ftTimeStamp;
    w->dwCRC = a->dwCRC;
    MultiByteToWideChar(CP_ACP, 0, a->tszActionMap, -1, w->tszActionMap, DI_MAX_PATH);
    return DI_OK;
}

/* copies what building the map changed back into the caller's A format. */
void DiActionFormatBack(DIACTIONFORMATA *a, const DIACTIONFORMATW *w)
{
    for (DWORD i = 0; i < a->dwNumActions; i++) {
        a->rgoAction[i].guidInstance = w->rgoAction[i].guidInstance;
        a->rgoAction[i].dwObjID = w->rgoAction[i].dwObjID;
        a->rgoAction[i].dwHow = w->rgoAction[i].dwHow;
    }
    a->ftTimeStamp = w->ftTimeStamp;
    a->dwCRC = w->dwCRC;
}

static DWORD crc32(const BYTE *p, SIZE_T n, DWORD crc)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static DWORD format_crc(const DIACTIONFORMATW *f)
{
    DWORD crc = 0;
    for (DWORD i = 0; i < f->dwNumActions; i++) {
        const DIACTIONW *a = &f->rgoAction[i];
        crc = crc32((const BYTE *)&a->dwSemantic, sizeof(a->dwSemantic), crc);
        crc = crc32((const BYTE *)&a->guidInstance, sizeof(a->guidInstance), crc);
        crc = crc32((const BYTE *)&a->dwObjID, sizeof(a->dwObjID), crc);
        crc = crc32((const BYTE *)&a->dwHow, sizeof(a->dwHow), crc);
    }
    return crc;
}

/* the object a keyboard or mouse semantic of the device's genre maps to, or -1. */
static int default_object(DIDEV *dev, DWORD semantic)
{
    DWORD genre = semantic >> 24, low = semantic & 0xFF;
    for (DWORD i = 0; i < dev->count; i++) {
        const DIOBJECT *obj = &dev->objects[i];
        DWORD instance = DIDFT_GETINSTANCE(obj->type);
        if (dev->cls->keyboard) {
            if (genre == 0x81 && instance == low)
                return (int)i;
        } else if (genre == 0x82) {
            if ((obj->type & DIDFT_AXIS) && instance * 4 == low)
                return (int)i;
            if ((obj->type & DIDFT_BUTTON) && low >= 0x0C && instance - 3 == low - 0x0C)
                return (int)i;
        }
    }
    return -1;
}

static int object_by_id(DIDEV *dev, DWORD id)
{
    for (DWORD i = 0; i < dev->count; i++)
        if ((dev->objects[i].type & 0x00FFFFFF) == (id & 0x00FFFFFF))
            return (int)i;
    return -1;
}

static BOOL taken(const DIACTIONFORMATW *f, DIDEV *dev, DWORD id, DWORD except)
{
    for (DWORD i = 0; i < f->dwNumActions; i++)
        if (i != except && f->rgoAction[i].dwHow && !(f->rgoAction[i].dwHow & DIAH_ERROR) &&
            IsEqualGUID(&f->rgoAction[i].guidInstance, &dev->instance) &&
            (f->rgoAction[i].dwObjID & 0x00FFFFFF) == (id & 0x00FFFFFF))
            return TRUE;
    return FALSE;
}

/* no mapper: actions already on the device, then those naming it, then unassigned ones of its genre. */
HRESULT DiBuildActionMap(DIDEV *dev, DIACTIONFORMATW *f, LPCWSTR user, DWORD flags)
{
    if (!dev->cls)
        return DIERR_NOTINITIALIZED;
    HRESULT hr = DiCheckActionFormat(f, TRUE);
    if (FAILED(hr))
        return hr;
    if (flags != DIDBAM_DEFAULT && flags != DIDBAM_PRESERVE && flags != DIDBAM_INITIALIZE &&
        flags != DIDBAM_HWDEFAULTS)
        return E_INVALIDARG;
    DWORD assigned = 0;
    BOOL error = FALSE;
    for (int pass = 0; pass < 3; pass++) {
        for (DWORD i = 0; i < f->dwNumActions; i++) {
            DIACTIONW *a = &f->rgoAction[i];
            if (a->dwFlags & DIA_APPNOMAP)
                continue;
            BOOL ours = IsEqualGUID(&a->guidInstance, &dev->instance);
            BOOL unassigned = IsEqualGUID(&a->guidInstance, &GUID_NULL);
            if (pass == 0 && !(ours && a->dwHow && !(a->dwHow & DIAH_ERROR) && flags == DIDBAM_PRESERVE))
                continue;
            if (pass == 1 && !(ours && (!a->dwHow || flags != DIDBAM_PRESERVE)))
                continue;
            if (pass == 2 && !unassigned)
                continue;
            int index;
            if (pass == 0) {
                assigned++;
                continue;
            }
            if (a->dwFlags & DIA_APPMAPPED) {
                if (!ours)
                    continue;
                index = object_by_id(dev, a->dwObjID);
                if (index < 0) {
                    a->dwHow = DIAH_ERROR;
                    error = TRUE;
                    continue;
                }
                a->dwHow = DIAH_APPREQUESTED;
                assigned++;
                continue;
            }
            DWORD genre = a->dwSemantic >> 24;
            if (pass == 2 && genre != (dev->cls->keyboard ? 0x81u : 0x82u))
                continue;
            index = default_object(dev, a->dwSemantic);
            if (index < 0 || taken(f, dev, dev->objects[index].type, i)) {
                if (ours) {
                    a->dwHow = DIAH_ERROR;
                    error = TRUE;
                }
                continue;
            }
            a->guidInstance = dev->instance;
            a->dwObjID = dev->objects[index].type;
            a->dwHow = DIAH_DEFAULT;
            assigned++;
        }
    }
    if (error)
        return E_INVALIDARG;
    /* no saved configuration, so windows reports a never configured device as new. */
    f->ftTimeStamp.dwLowDateTime = DIAFTS_NEWDEVICELOW;
    f->ftTimeStamp.dwHighDateTime = DIAFTS_NEWDEVICEHIGH;
    f->dwCRC = format_crc(f);
    return assigned ? DI_OK : DI_NOEFFECT;
}

/* each action on this device goes at offset 4 * its index, its uAppData becoming the object's DIPROP_APPDATA. */
HRESULT DiSetActionMap(DIDEV *dev, DIACTIONFORMATW *f, LPCWSTR user, DWORD flags)
{
    if (!dev->cls)
        return DIERR_NOTINITIALIZED;
    if (flags & DIDSAM_NOUSER) {
        if (flags != DIDSAM_NOUSER)
            return E_INVALIDARG;
        dev->user[0] = 0;
        return DI_OK;
    }
    if (flags & ~DIDSAM_FORCESAVE)
        return E_INVALIDARG;
    HRESULT hr = DiCheckActionFormat(f, TRUE);
    if (FAILED(hr))
        return hr;
    if (dev->acquired)
        return DIERR_ACQUIRED;
    DIOBJECTDATAFORMAT *objects = HeapAlloc(GetProcessHeap(), 0, f->dwNumActions * sizeof(*objects));
    if (!objects)
        return E_OUTOFMEMORY;
    DWORD count = 0;
    UINT_PTR data[DI_MAX_OBJECTS] = {};
    for (DWORD i = 0; i < f->dwNumActions; i++) {
        const DIACTIONW *a = &f->rgoAction[i];
        if (!IsEqualGUID(&a->guidInstance, &dev->instance) || !(a->dwHow & 0x3F))
            continue;
        int index = object_by_id(dev, a->dwObjID);
        if (index < 0)
            continue;
        objects[count++] = (DIOBJECTDATAFORMAT){nullptr, i * 4, dev->objects[index].type, 0};
        data[index] = a->uAppData;
    }
    DIDATAFORMAT df = {sizeof(df), sizeof(DIOBJECTDATAFORMAT), 0, f->dwDataSize, count, objects};
    hr = DiSetDataFormat(dev, &df);
    HeapFree(GetProcessHeap(), 0, objects);
    if (FAILED(hr))
        return hr;
    for (DWORD i = 0; i < dev->count; i++)
        if (dev->app_ofs[i] != ~0u)
            dev->appdata[i] = data[i];
    if (!dev->cls->keyboard)
        dev->relative = TRUE;
    if (f->dwBufferSize) {
        DIPROPDWORD buffer = {{sizeof(buffer), sizeof(DIPROPHEADER), 0, DIPH_DEVICE}, f->dwBufferSize};
        IDirectInputDevice8W *iface = (IDirectInputDevice8W *)&dev->vtbl_w;
        iface->lpVtbl->SetProperty(iface, DIPROP_BUFFERSIZE, &buffer.diph);
    }
    if (user)
        DiCopyW(dev->user, user, DI_MAX_PATH);
    else if (!GetEnvironmentVariableW(L"USERNAME", dev->user, DI_MAX_PATH))
        DiCopyW(dev->user, DiString(36), DI_MAX_PATH);
    return DI_OK;
}

/* ------------------------------------------------------------------ devices by semantics */

typedef struct CANDIDATE {
    void *iface;
    DIDEV *dev;
    DWORD score;
    DWORD flags;
} CANDIDATE;

HRESULT DiEnumDevicesBySemantics(DIOBJ *obj, LPCWSTR user, DIACTIONFORMATW *format, void *callback, void *context,
                                 DWORD flags, BOOL wide)
{
    HRESULT hr = DiCheckActionFormat(format, TRUE);
    if (FAILED(hr))
        return hr;
    if (user && lstrlenW(user) >= 257)
        return E_INVALIDARG;
    if (!callback || (flags & 0xFFFF8EEF))
        return E_INVALIDARG;
    hr = DiCheckVersion(DiModule, obj->version, TRUE);
    if (FAILED(hr))
        return hr;
    static const GUID *const DEVICES[] = {&GUID_SysMouse, &GUID_SysKeyboard};
    CANDIDATE found[RTL_NUMBER_OF(DEVICES)];
    DWORD n = 0;
    for (DWORD i = 0; i < RTL_NUMBER_OF(DEVICES); i++) {
        void *iface;
        if (FAILED(DiCreateDevice(obj, DEVICES[i], wide ? &IID_IDirectInputDevice8W : &IID_IDirectInputDevice8A,
                                  &iface, nullptr)))
            continue;
        DIDEV *dev = DiDeviceFrom(iface);
        DIACTIONFORMATW copy = *format;
        DIACTIONW *actions = HeapAlloc(GetProcessHeap(), 0, format->dwNumActions * sizeof(DIACTIONW));
        if (!actions) {
            ((IUnknown *)iface)->lpVtbl->Release(iface);
            continue;
        }
        memcpy(actions, format->rgoAction, format->dwNumActions * sizeof(DIACTIONW));
        copy.rgoAction = actions;
        RtlEnterCriticalSection(&dev->lock);
        HRESULT built = DiBuildActionMap(dev, &copy, user, user ? 0 : DIDBAM_HWDEFAULTS);
        RtlLeaveCriticalSection(&dev->lock);
        BOOL keep = SUCCEEDED(built);
        if (keep && (flags & DIEDBSFL_AVAILABLEDEVICES) && dev->user[0])
            keep = FALSE;
        if (keep && (flags & DIEDBSFL_THISUSER) && (!user || lstrcmpW(dev->user, user)))
            keep = FALSE;
        DWORD mapped = 0, found_flags = 0;
        for (DWORD a = 0; keep && a < copy.dwNumActions; a++) {
            if (!IsEqualGUID(&actions[a].guidInstance, &dev->instance) || !(actions[a].dwHow & 0x3F))
                continue;
            mapped++;
            found_flags |= (actions[a].dwSemantic & 0x4000) ? DIEDBS_MAPPEDPRI2 : DIEDBS_MAPPEDPRI1;
        }
        if (copy.ftTimeStamp.dwLowDateTime == DIAFTS_NEWDEVICELOW &&
            copy.ftTimeStamp.dwHighDateTime == DIAFTS_NEWDEVICEHIGH)
            found_flags |= DIEDBS_NEWDEVICE;
        HeapFree(GetProcessHeap(), 0, actions);
        if (!keep) {
            ((IUnknown *)iface)->lpVtbl->Release(iface);
            continue;
        }
        DWORD priority = dev->cls->keyboard ? 5 : 4;
        found[n++] = (CANDIDATE){iface, dev, priority << 24 | mapped << 8, found_flags};
    }
    /* the highest score first. */
    for (DWORD i = 1; i < n; i++)
        for (DWORD j = i; j > 0 && found[j].score > found[j - 1].score; j--) {
            CANDIDATE t = found[j];
            found[j] = found[j - 1];
            found[j - 1] = t;
        }
    BOOL stop = FALSE;
    for (DWORD i = 0; i < n; i++) {
        if (!stop) {
            DIDEVICEINSTANCEW info = {sizeof(info)};
            DiDeviceInfo(found[i].dev, &info);
            BOOL more;
            if (wide) {
                more = ((LPDIENUMDEVICESBYSEMANTICSCBW)callback)(&info, found[i].iface, found[i].flags,
                                                                  n - i - 1, context);
            } else {
                DIDEVICEINSTANCEA a = {sizeof(a)};
                a.guidInstance = info.guidInstance;
                a.guidProduct = info.guidProduct;
                a.dwDevType = info.dwDevType;
                DiToA(a.tszInstanceName, info.tszInstanceName, sizeof(a.tszInstanceName));
                DiToA(a.tszProductName, info.tszProductName, sizeof(a.tszProductName));
                more = ((LPDIENUMDEVICESBYSEMANTICSCBA)callback)(&a, found[i].iface, found[i].flags, n - i - 1,
                                                                  context);
            }
            stop = !more;
        }
        ((IUnknown *)found[i].iface)->lpVtbl->Release(found[i].iface);
    }
    return DI_OK;
}
