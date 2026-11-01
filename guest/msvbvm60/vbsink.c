/*
 * the sink of the events of a form, which is a slot in its object: its vtable is the table of events the compiler wrote, whose first
 * entries come here. the object of the form is found from the slot by the place the table says the sink has in it.
 */
#include "vb.h"

/* the form object a sink pointer sits in, or null. */
static BYTE *owner_of(void *sink)
{
    const DWORD *table = *(const DWORD *const *)sink;
    const BYTE *controls = (const BYTE *)table[1];
    const BYTE *info = (const BYTE *)table[2];
    if (!controls || !info || IsBadReadPtr(info, 0x60))
        return nullptr;
    DWORD count = *(const DWORD *)(info + 0x38 + 0x20);
    for (DWORD i = 0; i < count; i++) {
        const BYTE *entry = controls + i * 0x28;
        if (*(const DWORD *)(entry + 0x18) == (DWORD)(ULONG_PTR)table)
            return (BYTE *)sink - *(const DWORD *)(entry + 4);
    }
    return nullptr;
}

VB_STD LONG vb_EVENT_SINK_AddRef(void *sink)
{
    BYTE *owner = owner_of(sink);
    return owner ? InterlockedIncrement((LONG *)(owner + 4)) : 1;
}

VB_STD LONG vb_EVENT_SINK_Release(void *sink)
{
    BYTE *owner = owner_of(sink);
    return owner ? InterlockedDecrement((LONG *)(owner + 4)) : 0;
}

VB_STD HRESULT vb_EVENT_SINK_QueryInterface(void *sink, const IID *iid, void **out)
{
    *out = nullptr;
    BYTE *owner = owner_of(sink);
    if (owner && (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDispatch)))
        *out = owner;
    if (!*out)
        return E_NOINTERFACE;
    InterlockedIncrement((LONG *)(owner + 4));
    return S_OK;
}

/* the owner is asked to count the references, which for a form object is its own table. */
VB_STD LONG vb_EVENT_SINK2_AddRef(void *sink)
{
    BYTE *owner = owner_of(sink);
    if (!owner)
        return 1;
    void **vtable = *(void ***)owner;
    return ((LONG(STDMETHODCALLTYPE *)(void *))vtable[1])(owner);
}

VB_STD LONG vb_EVENT_SINK2_Release(void *sink)
{
    BYTE *owner = owner_of(sink);
    if (!owner)
        return 0;
    void **vtable = *(void ***)owner;
    return ((LONG(STDMETHODCALLTYPE *)(void *))vtable[2])(owner);
}

/* events of the controls come straight from the windows, so nothing calls a sink by name or number. */
VB_STD HRESULT vb_EVENT_SINK_GetIDsOfNames(void *sink, const IID *iid, WCHAR **names, UINT count, LCID lcid, LONG *ids)
{
    for (UINT i = 0; i < count; i++)
        ids[i] = DISPID_UNKNOWN;
    return DISP_E_UNKNOWNNAME;
}

VB_STD HRESULT vb_EVENT_SINK_Invoke(void *sink, LONG id, const IID *iid, LCID lcid, WORD flags, DISPPARAMS *params, VARIANT *result, EXCEPINFO *info, UINT *bad)
{
    return DISP_E_MEMBERNOTFOUND;
}

/* RaiseEvent tells whoever holds the object with events. nobody does in a program that never makes an object that has them. */
VB_C LONG vb___vbaRaiseEvent(void *object, LONG event_index, LONG argument_bytes, ...)
{
    (void)object;
    (void)event_index;
    (void)argument_bytes;
    return 0;
}
