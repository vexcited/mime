/*
 * the objects of the windows runtime classes that are written in C: a count of references shared by the
 * interfaces of an object, the queries between them, and the three methods every runtime interface starts with.
 * an object is a WRT_OBJECT first and its own fields after; each of its interfaces is a WRT_IFACE of the object's,
 * whose vtable the object is made with, and the class lists their identifiers in the same order.
 */
#ifndef MIME_WINRT_OBJECT_H
#define MIME_WINRT_OBJECT_H

#include <mime/win32/winbase.h>
#include <mime/win32/wgc.h>

#define WRT_MAX_INTERFACES 8

typedef struct WRT_OBJECT WRT_OBJECT;

typedef struct WRT_IFACE {
    const void *Vtbl;
    WRT_OBJECT *Owner;
} WRT_IFACE;

typedef struct WRT_CLASS {
    LPCWSTR Name;
    ULONG Count;
    const IID *Iids[WRT_MAX_INTERFACES];
    /* the interfaces GetIids does not list: programs ask for them by identifier alone. */
    ULONG Hidden;
    void (*Destroy)(WRT_OBJECT *object);
} WRT_CLASS;

struct WRT_OBJECT {
    volatile LONG Refs;
    const WRT_CLASS *Class;
    WRT_IFACE Ifaces[WRT_MAX_INTERFACES];
};

/* an object with one reference, its interfaces made with these vtables. */
FORCEINLINE void WrtInit(WRT_OBJECT *object, const WRT_CLASS *class, const void *const *vtables)
{
    object->Refs = 1;
    object->Class = class;
    for (ULONG i = 0; i < class->Count; i++) {
        object->Ifaces[i].Vtbl = vtables[i];
        object->Ifaces[i].Owner = object;
    }
}

FORCEINLINE HRESULT WrtQuery(WRT_IFACE *self, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    WRT_OBJECT *owner = self->Owner;
    WRT_IFACE *found = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IInspectable) || IsEqualIID(riid, &IID_IAgileObject))
        found = &owner->Ifaces[0];
    for (ULONG i = 0; i < owner->Class->Count && !found; i++)
        if (IsEqualIID(riid, owner->Class->Iids[i]))
            found = &owner->Ifaces[i];
    if (!found) {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&owner->Refs);
    *object = found;
    return S_OK;
}

FORCEINLINE ULONG WrtAddRef(WRT_IFACE *self)
{
    return (ULONG)InterlockedIncrement(&self->Owner->Refs);
}

FORCEINLINE ULONG WrtRelease(WRT_IFACE *self)
{
    WRT_OBJECT *owner = self->Owner;
    LONG refs = InterlockedDecrement(&owner->Refs);
    if (!refs)
        owner->Class->Destroy(owner);
    return (ULONG)refs;
}

FORCEINLINE HRESULT WrtGetIids(WRT_IFACE *self, ULONG *count, IID **iids)
{
    if (!count || !iids)
        return E_POINTER;
    const WRT_CLASS *class = self->Owner->Class;
    ULONG shown = 0;
    for (ULONG i = 0; i < class->Count; i++)
        shown += !(class->Hidden >> i & 1);
    *count = 0;
    *iids = nullptr;
    if (!shown)
        return S_OK;
    IID *list = HeapAlloc(GetProcessHeap(), 0, shown * sizeof(IID));
    if (!list)
        return E_OUTOFMEMORY;
    ULONG n = 0;
    for (ULONG i = 0; i < class->Count; i++)
        if (!(class->Hidden >> i & 1))
            list[n++] = *class->Iids[i];
    *count = shown;
    *iids = list;
    return S_OK;
}

/* strings are combase's, reached at run time as the dlls that are not core ones reach each other. */
FORCEINLINE HRESULT WrtGetRuntimeClassName(WRT_IFACE *self, HSTRING *name)
{
    typedef HRESULT(WINAPI * CREATE)(LPCWSTR, UINT32, HSTRING *);
    if (!name)
        return E_POINTER;
    *name = nullptr;
    HMODULE combase = LoadLibraryW(L"combase.dll");
    CREATE create = combase ? (CREATE)(void *)GetProcAddress(combase, "WindowsCreateString") : nullptr;
    if (!create)
        return E_FAIL;
    LPCWSTR text = self->Owner->Class->Name;
    return create(text, (UINT32)wcslen(text), name);
}

FORCEINLINE HRESULT WrtGetTrustLevel(WRT_IFACE *self, TrustLevel *level)
{
    if (!level)
        return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

/* the methods of IUnknown and of IInspectable of an interface T, for its vtable. */
#define WRT_UNKNOWN_THUNKS(T)                                                                                          \
    static HRESULT STDMETHODCALLTYPE T##_QueryInterface(T *This, REFIID riid, void **object)                           \
    {                                                                                                                  \
        return WrtQuery((WRT_IFACE *)This, riid, object);                                                              \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE T##_AddRef(T *This)                                                                 \
    {                                                                                                                  \
        return WrtAddRef((WRT_IFACE *)This);                                                                           \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE T##_Release(T *This)                                                                \
    {                                                                                                                  \
        return WrtRelease((WRT_IFACE *)This);                                                                          \
    }

#define WRT_INSPECTABLE_THUNKS(T)                                                                                      \
    WRT_UNKNOWN_THUNKS(T)                                                                                              \
    static HRESULT STDMETHODCALLTYPE T##_GetIids(T *This, ULONG *count, IID **iids)                                    \
    {                                                                                                                  \
        return WrtGetIids((WRT_IFACE *)This, count, iids);                                                             \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetRuntimeClassName(T *This, HSTRING *name)                                   \
    {                                                                                                                  \
        return WrtGetRuntimeClassName((WRT_IFACE *)This, name);                                                        \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetTrustLevel(T *This, TrustLevel *level)                                     \
    {                                                                                                                  \
        return WrtGetTrustLevel((WRT_IFACE *)This, level);                                                             \
    }

#define WRT_UNKNOWN_ENTRIES(T) T##_QueryInterface, T##_AddRef, T##_Release
#define WRT_INSPECTABLE_ENTRIES(T) WRT_UNKNOWN_ENTRIES(T), T##_GetIids, T##_GetRuntimeClassName, T##_GetTrustLevel

#endif
