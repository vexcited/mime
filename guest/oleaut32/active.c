/*
 * the active objects of automation: the object that an application makes available to scripts as the one running, under
 * the class it is. Windows keeps them in the running object table under an item moniker of the exclamation mark and the
 * class identifier. Mime's table of ole32 is of the process and has no monikers to name them, so the active objects are a
 * list of the process of their own, and a script in another process does not see them.
 */
#include "oleaut32_private.h"

#define ACTIVEOBJECT_WEAK 1
#define MK_E_UNAVAILABLE_ ((HRESULT)0x800401E3)
#define MK_S_MONIKERALREADYREGISTERED_ ((HRESULT)0x000401E4)

typedef struct ACTIVE_OBJECT {
    struct ACTIVE_OBJECT *Next;
    DWORD Cookie;
    GUID Class;
    IUnknown *Object;
    BOOL Strong;
} ACTIVE_OBJECT;

static SRWLOCK g_active_lock = SRWLOCK_INIT;
static ACTIVE_OBJECT *g_active;
static DWORD g_next_cookie = 1;

WINOLEAUTAPI HRESULT WINAPI RegisterActiveObject(IUnknown *object, REFCLSID clsid, DWORD flags, DWORD *cookie)
{
    if (!object || !clsid || !cookie || (flags & ~ACTIVEOBJECT_WEAK))
        return E_INVALIDARG;
    ACTIVE_OBJECT *entry = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*entry));
    if (!entry)
        return E_OUTOFMEMORY;
    entry->Class = *clsid;
    entry->Object = object;
    entry->Strong = !(flags & ACTIVEOBJECT_WEAK);
    if (entry->Strong)
        object->lpVtbl->AddRef(object);
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&g_active_lock);
    for (ACTIVE_OBJECT *other = g_active; other; other = other->Next)
        if (IsEqualGUID(&other->Class, clsid))
            hr = MK_S_MONIKERALREADYREGISTERED_;
    entry->Cookie = g_next_cookie++;
    entry->Next = g_active;
    g_active = entry;
    *cookie = entry->Cookie;
    ReleaseSRWLockExclusive(&g_active_lock);
    return hr;
}

WINOLEAUTAPI HRESULT WINAPI RevokeActiveObject(DWORD cookie, void *reserved)
{
    if (reserved)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&g_active_lock);
    ACTIVE_OBJECT **link = &g_active;
    while (*link && (*link)->Cookie != cookie)
        link = &(*link)->Next;
    ACTIVE_OBJECT *entry = *link;
    if (entry)
        *link = entry->Next;
    ReleaseSRWLockExclusive(&g_active_lock);
    if (!entry)
        return E_INVALIDARG;
    if (entry->Strong)
        entry->Object->lpVtbl->Release(entry->Object);
    HeapFree(GetProcessHeap(), 0, entry);
    return S_OK;
}

WINOLEAUTAPI HRESULT WINAPI GetActiveObject(REFCLSID clsid, void *reserved, IUnknown **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!clsid || reserved)
        return E_INVALIDARG;
    AcquireSRWLockShared(&g_active_lock);
    ACTIVE_OBJECT *entry = g_active;
    while (entry && !IsEqualGUID(&entry->Class, clsid))
        entry = entry->Next;
    if (entry) {
        *object = entry->Object;
        entry->Object->lpVtbl->AddRef(entry->Object);
    }
    ReleaseSRWLockShared(&g_active_lock);
    return entry ? S_OK : MK_E_UNAVAILABLE_;
}
