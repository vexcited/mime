/*
 * data formats: matching an application's objects to the device's as
 * windows does, copying the state into the application's layout, and
 * finding an object by offset or id.
 */
#include "dinput_private.h"

static BOOL object_matches(DIDEV *dev, const DIOBJECTDATAFORMAT *o, const DIOBJECT *obj, DWORD mask)
{
    if (!(o->dwType & obj->type & 0xFF))
        return FALSE;
    if ((o->dwType & 0x1F000000) & ~obj->type)
        return FALSE;
    if (o->pguid) {
        /* a usage and its page in place of a guid; keyboards and mice have none. */
        if (o->dwFlags & DIDOI_GUIDISUSAGE) {
            if (!obj->page || (DWORD)(ULONG_PTR)o->pguid != (DWORD)MAKELONG(obj->usage, obj->page))
                return FALSE;
        } else if (!IsEqualGUID(o->pguid, &GUID_NULL) && !IsEqualGUID(o->pguid, obj->guid)) {
            return FALSE;
        }
    }
    if ((o->dwType & mask) != mask && ((o->dwType ^ obj->type) & mask))
        return FALSE;
    DWORD aspect = o->dwFlags & DIDOI_ASPECTMASK;
    return !aspect || !(obj->flags & DIDOI_ASPECTMASK) || aspect == (obj->flags & DIDOI_ASPECTMASK);
}

/*
 * the previous format is dropped first, so a failure leaves none. before
 * version 7 the instance mask is the old DIDFT_ANYINSTANCE.
 */
HRESULT DiSetDataFormat(DIDEV *dev, const DIDATAFORMAT *df)
{
    if (!df)
        return E_POINTER;
    if (df->dwSize != sizeof(DIDATAFORMAT))
        return E_INVALIDARG;
    if ((df->dwFlags & ~3u) || (df->dwDataSize & 3) || df->dwObjSize != sizeof(DIOBJECTDATAFORMAT))
        return E_INVALIDARG;
    if (!df->rgodf && df->dwNumObjs)
        return E_POINTER;
    if (dev->acquired)
        return DIERR_ACQUIRED;
    dev->formatted = FALSE;
    dev->missing_count = 0;
    BOOL used[DI_MAX_OBJECTS] = {};
    for (DWORD i = 0; i < DI_MAX_OBJECTS; i++) {
        dev->app_ofs[i] = ~0u;
        dev->appdata[i] = 0;
    }
    DWORD mask = dev->di8 || dev->version >= 0x0700 ? DIDFT_ANYINSTANCE : 0xFF00;
    for (DWORD j = 0; j < df->dwNumObjs; j++) {
        const DIOBJECTDATAFORMAT *o = &df->rgodf[j];
        if (o->dwOfs >= df->dwDataSize)
            return E_INVALIDARG;
        DWORD found = ~0u;
        for (DWORD i = 0; i < dev->count && found == ~0u; i++)
            if (!used[i] && object_matches(dev, o, &dev->objects[i], mask))
                found = i;
        /* before dinput 6 a throttle was the z axis, so an unmatched z takes a slider. */
        if (found == ~0u && !dev->di8 && dev->version < 0x0600 && o->pguid &&
            !(o->dwFlags & DIDOI_GUIDISUSAGE) && IsEqualGUID(o->pguid, &GUID_ZAxis)) {
            DIOBJECTDATAFORMAT slider = *o;
            slider.pguid = &GUID_Slider;
            for (DWORD i = 0; i < dev->count && found == ~0u; i++)
                if (!used[i] && object_matches(dev, &slider, &dev->objects[i], mask))
                    found = i;
        }
        if (found != ~0u) {
            if ((dev->objects[found].type & (DIDFT_AXIS | DIDFT_POV)) && (o->dwOfs & 3))
                return E_INVALIDARG;
            used[found] = TRUE;
            dev->app_ofs[found] = o->dwOfs;
            continue;
        }
        if (!(o->dwType & DIDFT_OPTIONAL))
            return E_INVALIDARG;
        if ((o->dwType & DIDFT_POV) && dev->missing_count < RTL_NUMBER_OF(dev->missing_povs))
            dev->missing_povs[dev->missing_count++] = o->dwOfs;
    }
    dev->formatted = TRUE;
    dev->app_size = df->dwDataSize;
    switch (df->dwFlags) {
    case DIDF_ABSAXIS:
        dev->relative = FALSE;
        break;
    case DIDF_RELAXIS:
        dev->relative = TRUE;
        break;
    case DIDF_ABSAXIS | DIDF_RELAXIS:
        /* like windows, keep the installed format but still fail. */
        return E_INVALIDARG;
    }
    return DI_OK;
}

static LONG load32(const BYTE *p)
{
    LONG v;
    memcpy(&v, p, sizeof(v));
    return v;
}

/*
 * the state in the application's layout, zeroed first. relative axes
 * report the motion since the last read, which becomes the new baseline.
 */
void DiReadState(DIDEV *dev, BYTE *out)
{
    BYTE native[DI_MAX_STATE];
    memset(out, 0, dev->app_size);
    dev->cls->state(dev, native);
    for (DWORD i = 0; i < dev->count; i++) {
        const DIOBJECT *obj = &dev->objects[i];
        BOOL wide = (obj->type & (DIDFT_AXIS | DIDFT_POV)) != 0;
        if (dev->relative && (obj->type & DIDFT_RELAXIS) && obj->ofs / 4 < RTL_NUMBER_OF(dev->baseline)) {
            LONG now = load32(native + obj->ofs);
            LONG delta = now - dev->baseline[obj->ofs / 4];
            dev->baseline[obj->ofs / 4] = now;
            memcpy(native + obj->ofs, &delta, sizeof(delta));
        }
        DWORD at = dev->app_ofs[i];
        if (at == ~0u)
            continue;
        if (wide && at + 4 <= dev->app_size)
            memcpy(out + at, native + obj->ofs, 4);
        else if (!wide && at < dev->app_size)
            out[at] = native[obj->ofs];
    }
    for (DWORD i = 0; i < dev->missing_count; i++)
        if (dev->missing_povs[i] + 4 <= dev->app_size)
            memset(out + dev->missing_povs[i], 0xFF, 4);
}

/* by offset in the application's format or by id, searching from the last; -1 for none. */
int DiFindObject(DIDEV *dev, DWORD id, DWORD how)
{
    if (how == DIPH_BYOFFSET) {
        if (!dev->formatted)
            return -1;
        for (DWORD i = 0; i < dev->count; i++)
            if (dev->app_ofs[i] == id)
                return (int)i;
        return -1;
    }
    for (DWORD i = dev->count; i-- > 0;)
        if ((dev->objects[i].type & 0x00FFFFFF) == (id & 0x00FFFFFF))
            return (int)i;
    return -1;
}
