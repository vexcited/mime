/*
 * property bags over a registry key and over a profile section, and the property bag helpers that go to propsys.
 */
#include "shlwapi_dynamic.h"
#include <mime/win32/propidl.h>

#define SLOT(object, index) ((*(void ***)(object))[index])
#define CALL(type, object, index, ...) ((type)SLOT(object, index))(object __VA_OPT__(, ) __VA_ARGS__)

MIME_DEFINE_GUID(IID_PropertyBag2, 0x22f55882, 0x280b, 0x11d0, 0xa8, 0xa9, 0x00, 0xa0, 0xc9, 0x0c, 0x20, 0x04);
MIME_DEFINE_GUID(IID_DataProvider, 0x5ebc8d33, 0xd04e, 0x4576, 0xae, 0x40, 0x0a, 0x86, 0x31, 0xf3, 0x19, 0xe6);
MIME_DEFINE_GUID(IID_CachedPrivateProfile, 0xb57046bc, 0x32e5, 0x428a, 0x98, 0x87, 0x19, 0xf7, 0x12, 0xb9, 0x07, 0xbf);
MIME_DEFINE_GUID(CLSID_CachedPrivateProfile, 0x75847177, 0xf077, 0x4171, 0xbd, 0x2c, 0xa6, 0xbb, 0x21, 0x64, 0xfb, 0xd0);
MIME_DEFINE_GUID(CLSID_MruPidlList, 0x42aedc87, 0x2188, 0x41fd, 0xb9, 0xa3, 0x0c, 0x96, 0x6f, 0xea, 0xbe, 0xc1);
MIME_DEFINE_GUID(IID_MruPidlList, 0x2fb499a3, 0xcfce, 0x480f, 0xa5, 0xf3, 0x24, 0x53, 0xdb, 0x7a, 0x2b, 0x7a);
MIME_DEFINE_GUID(IID_PropStream, 0x0000000c, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

typedef struct {
    GUID fmtid;
    DWORD pid;
} SHL_PROPERTYKEY;

#define BAG_ACCESS 3
#define BAG_WRITE_ONLY 1
#define BAG_CREATE 0x1000

HRESULT WINAPI SHCoCreateInstanceAC(REFCLSID clsid, IUnknown *outer, DWORD context, REFIID riid, void **out);
HKEY WINAPI SHGetShellKeyEx(DWORD key_id, LPCWSTR subkey, BOOL create, REGSAM access);

typedef struct BAG BAG;

/* the three faces of a bag: the property bag, the unimplemented second property bag and the data provider. */
struct BAG {
    const void *bag_vtbl;
    const void *bag2_vtbl;
    const void *provider_vtbl;
    LONG refs;
    DWORD flags;
    HKEY key;
    void *profile;
    LPWSTR section;
};

static BAG *bag_of(void *face, int index)
{
    return (BAG *)((char *)face - index * sizeof(void *));
}

static HRESULT status_result(LSTATUS status)
{
    return status > 0 ? (HRESULT)((status & 0xffff) | 0x80070000) : (HRESULT)status;
}

static void bag_free(BAG *bag)
{
    if (bag->key)
        RegCloseKey(bag->key);
    if (bag->profile)
        CALL(ULONG(STDMETHODCALLTYPE *)(void *), bag->profile, 2);
    ShlTaskFree(bag->section);
    HeapFree(GetProcessHeap(), 0, bag);
}

static HRESULT bag_query(BAG *bag, REFIID riid, void **out)
{
    int index = -1;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IPropertyBag))
        index = 0;
    else if (IsEqualIID(riid, &IID_PropertyBag2))
        index = 1;
    else if (IsEqualIID(riid, &IID_DataProvider))
        index = 2;
    *out = nullptr;
    if (index < 0)
        return E_NOINTERFACE;
    *out = (void **)bag + index;
    InterlockedIncrement(&bag->refs);
    return S_OK;
}

static ULONG bag_release(BAG *bag)
{
    LONG refs = InterlockedDecrement(&bag->refs);
    if (!refs)
        bag_free(bag);
    return refs;
}

static HRESULT STDMETHODCALLTYPE bag_qi(void *face, REFIID riid, void **out)
{
    return bag_query(bag_of(face, 0), riid, out);
}
static ULONG STDMETHODCALLTYPE bag_addref(void *face)
{
    return InterlockedIncrement(&bag_of(face, 0)->refs);
}
static ULONG STDMETHODCALLTYPE bag_release0(void *face)
{
    return bag_release(bag_of(face, 0));
}
static HRESULT STDMETHODCALLTYPE bag2_qi(void *face, REFIID riid, void **out)
{
    return bag_query(bag_of(face, 1), riid, out);
}
static ULONG STDMETHODCALLTYPE bag2_addref(void *face)
{
    return InterlockedIncrement(&bag_of(face, 1)->refs);
}
static ULONG STDMETHODCALLTYPE bag2_release(void *face)
{
    return bag_release(bag_of(face, 1));
}
static HRESULT STDMETHODCALLTYPE provider_qi(void *face, REFIID riid, void **out)
{
    return bag_query(bag_of(face, 2), riid, out);
}
static ULONG STDMETHODCALLTYPE provider_addref(void *face)
{
    return InterlockedIncrement(&bag_of(face, 2)->refs);
}
static ULONG STDMETHODCALLTYPE provider_release(void *face)
{
    return bag_release(bag_of(face, 2));
}

static HRESULT STDMETHODCALLTYPE bag2_not_implemented(void)
{
    return E_NOTIMPL;
}

/* reads each name through the property bag, and puts the error of the ones that failed in their place. */
static HRESULT STDMETHODCALLTYPE provider_read(void *face, int unused, UINT count, LPCWSTR const *names,
                                               VARIANT *values)
{
    BAG *bag = bag_of(face, 2);
    HRESULT result = E_INVALIDARG;
    UINT succeeded = 0;
    memset(values, 0, count * sizeof(VARIANT));
    for (UINT i = 0; i < count; i++) {
        result = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, LPCWSTR, VARIANT *, void *), bag, 3, names[i], &values[i],
                      nullptr);
        if (FAILED(result)) {
            V_VT(&values[i]) = VT_ERROR;
            V_ERROR(&values[i]) = result;
        } else {
            succeeded++;
        }
    }
    if (succeeded)
        return succeeded != count ? S_FALSE : S_OK;
    return result;
}

/* ------------------------------------------------------------------ the registry bag */

static HRESULT reg_read_stream(BAG *bag, LPCWSTR name, VARIANT *value, VARTYPE wanted, DWORD size)
{
    if ((wanted != VT_EMPTY && wanted != VT_UNKNOWN) || size < 16)
        return E_FAIL;
    BYTE *data = LocalAlloc(LPTR, size);
    if (!data)
        return E_OUTOFMEMORY;
    DWORD type = 0;
    HRESULT hr = status_result(SHGetValueW(bag->key, nullptr, name, &type, data, &size));
    if (SUCCEEDED(hr)) {
        static const BYTE marker[16] = {};
        hr = E_FAIL;
        if (!memcmp(data, marker, 16)) {
            IStream *(WINAPI * create)(const BYTE *, UINT) =
                (IStream * (WINAPI *)(const BYTE *, UINT)) ShlProc(L"shcore.dll", "SHCreateMemStream");
            IStream *stream = create ? create(data + 16, size - 16) : nullptr;
            if (stream) {
                V_VT(value) = VT_UNKNOWN;
                V_UNKNOWN(value) = (IUnknown *)stream;
                hr = S_OK;
            } else {
                hr = E_OUTOFMEMORY;
            }
        }
    }
    LocalFree(data);
    return hr;
}

static HRESULT convert_read(VARIANT *value, VARTYPE wanted)
{
    VARIANT source = *value, number = {};
    VARIANT *from = &source;
    if (V_VT(&source) == VT_BSTR) {
        LPCWSTR text = V_BSTR(&source) ? V_BSTR(&source) : L"";
        switch (wanted) {
        case VT_UINT:
        case VT_UI1:
        case VT_UI2:
        case VT_UI4:
            if (StrToIntExW(text, STIF_SUPPORT_HEX, &V_INT(&number))) {
                V_VT(&number) = VT_UINT;
                from = &number;
            }
            break;
        case VT_I2:
        case VT_I4:
        case VT_I1:
        case VT_INT:
            if (StrToIntExW(text, STIF_SUPPORT_HEX, &V_INT(&number))) {
                V_VT(&number) = VT_INT;
                from = &number;
            }
            break;
        }
    }
    VariantInit(value);
    HRESULT hr = VariantChangeType(value, from, 0, wanted);
    VariantClear(&source);
    return hr;
}

static HRESULT STDMETHODCALLTYPE reg_read(void *face, LPCWSTR name, VARIANT *value, void *log)
{
    BAG *bag = bag_of(face, 0);
    HRESULT hr;
    VARTYPE wanted = V_VT(value);
    DWORD type = 0, size = 0;
    if ((bag->flags & BAG_ACCESS) == BAG_WRITE_ONLY) {
        VariantInit(value);
        return E_ACCESSDENIED;
    }
    VariantInit(value);
    hr = status_result(SHGetValueW(bag->key, nullptr, name, &type, nullptr, &size));
    if (SUCCEEDED(hr)) {
        if (type == REG_DWORD) {
            DWORD read = 4;
            hr = status_result(SHGetValueW(bag->key, nullptr, name, &type, &V_UI4(value), &read));
            if (SUCCEEDED(hr))
                V_VT(value) = VT_UI4;
        } else if (type == REG_SZ) {
            DWORD read = size;
            BSTR text = SysAllocStringByteLen(nullptr, size);
            if (!text) {
                hr = E_OUTOFMEMORY;
            } else {
                V_BSTR(value) = text;
                V_VT(value) = VT_BSTR;
                DWORD kind = 0;
                hr = status_result(SHGetValueW(bag->key, nullptr, name, &kind, text, &read));
                if (FAILED(hr))
                    VariantClear(value);
            }
        } else if (type == REG_BINARY) {
            hr = reg_read_stream(bag, name, value, wanted, size);
        } else if (type == REG_QWORD) {
            DWORD read = 8;
            hr = status_result(SHGetValueW(bag->key, nullptr, name, &type, &V_UI8(value), &read));
            if (SUCCEEDED(hr))
                V_VT(value) = VT_UI8;
        } else {
            hr = E_FAIL;
        }
    }
    if (SUCCEEDED(hr) && V_VT(value) != wanted && wanted != VT_EMPTY)
        hr = convert_read(value, wanted);
    if (FAILED(hr))
        VariantInit(value);
    return hr;
}

static HRESULT reg_write_stream(BAG *bag, LPCWSTR name, IStream *stream)
{
    HRESULT(WINAPI * size_of)(IStream *, ULARGE_INTEGER *) =
        (HRESULT(WINAPI *)(IStream *, ULARGE_INTEGER *))ShlProc(L"shcore.dll", "IStream_Size");
    HRESULT(WINAPI * reset)(IStream *) = (HRESULT(WINAPI *)(IStream *))ShlProc(L"shcore.dll", "IStream_Reset");
    HRESULT(WINAPI * read)(IStream *, void *, ULONG) =
        (HRESULT(WINAPI *)(IStream *, void *, ULONG))ShlProc(L"shcore.dll", "IStream_Read");
    ULARGE_INTEGER size = {};
    if (!size_of || !reset || !read)
        return E_FAIL;
    HRESULT hr = size_of(stream, &size);
    if (FAILED(hr))
        return hr;
    if (size.HighPart)
        return E_FAIL;
    if (!size.LowPart)
        return S_OK;
    if (size.LowPart + 16 < size.LowPart)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    BYTE *data = LocalAlloc(LPTR, size.LowPart + 16);
    if (!data)
        return E_OUTOFMEMORY;
    hr = reset(stream);
    if (SUCCEEDED(hr))
        hr = read(stream, data + 16, size.LowPart);
    if (SUCCEEDED(hr) && SHSetValueW(bag->key, nullptr, name, REG_BINARY, data, size.LowPart + 16))
        hr = E_FAIL;
    LocalFree(data);
    return hr;
}

static HRESULT STDMETHODCALLTYPE reg_write(void *face, LPCWSTR name, VARIANT *value)
{
    BAG *bag = bag_of(face, 0);
    VARIANT converted = {};
    HRESULT hr = S_OK;
    if ((bag->flags & BAG_ACCESS) == 0)
        return E_ACCESSDENIED;
    switch (V_VT(value)) {
    case VT_EMPTY:
        SHDeleteValueW(bag->key, nullptr, name);
        return S_OK;
    case VT_UI8:
        return SHSetValueW(bag->key, nullptr, name, REG_QWORD, &V_UI8(value), 8) ? E_FAIL : S_OK;
    case VT_UNKNOWN: {
        IStream *stream;
        hr = value->punkVal->lpVtbl->QueryInterface(value->punkVal, &IID_PropStream, (void **)&stream);
        if (SUCCEEDED(hr)) {
            hr = reg_write_stream(bag, name, stream);
            stream->lpVtbl->Release(stream);
        }
        return hr;
    }
    case VT_I2:
    case VT_I4:
    case VT_BOOL:
    case VT_I1:
    case VT_UI1:
    case VT_UI2:
    case VT_UI4:
    case VT_INT:
    case VT_UINT:
        hr = VariantChangeType(&converted, value, 0, VT_UI4);
        if (SUCCEEDED(hr) && SHSetValueW(bag->key, nullptr, name, REG_DWORD, &V_UI4(&converted), 4))
            hr = E_FAIL;
        return hr;
    default:
        hr = VariantChangeType(&converted, value, 0, VT_BSTR);
        if (SUCCEEDED(hr)) {
            if (SHSetValueW(bag->key, nullptr, name, REG_SZ, V_BSTR(&converted),
                            (lstrlenW(V_BSTR(&converted)) + 1) * sizeof(WCHAR)))
                hr = E_FAIL;
            VariantClear(&converted);
        }
        return hr;
    }
}

/* ------------------------------------------------------------------ the profile bag */

static HRESULT profile_names(BAG *bag, LPCWSTR name, WCHAR *section, WCHAR *key)
{
    LPWSTR slash = StrChrW(name, L'\\');
    if (slash) {
        StrCpyNW(section, name, (int)(slash - name) + 1 > 64 ? 64 : (int)(slash - name) + 1);
        name = slash + 1;
    } else if (bag->section) {
        lstrcpynW(section, bag->section, 64);
    } else {
        return E_INVALIDARG;
    }
    lstrcpynW(key, name, 64);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE profile_read(void *face, LPCWSTR name, VARIANT *value, void *log)
{
    typedef HRESULT(STDMETHODCALLTYPE * READ)(void *, LPCWSTR, LPCWSTR, BOOL, VARTYPE, PROPVARIANT *);
    BAG *bag = bag_of(face, 0);
    VARTYPE wanted = V_VT(value);
    WCHAR section[64], key[64];
    VariantInit(value);
    if ((bag->flags & BAG_ACCESS) == BAG_WRITE_ONLY)
        return E_ACCESSDENIED;
    HRESULT hr = profile_names(bag, name, section, key);
    if (FAILED(hr))
        return hr;
    PROPVARIANT property = {};
    BOOL raw = key[0] == L'@';
    hr = CALL(READ, bag->profile, 5, section, raw ? key + 1 : key, raw, wanted, &property);
    if (SUCCEEDED(hr)) {
        HRESULT(WINAPI * convert)(const PROPVARIANT *, VARIANT *) =
            (HRESULT(WINAPI *)(const PROPVARIANT *, VARIANT *))ShlProc(L"propsys.dll", "PropVariantToVariant");
        HRESULT(WINAPI * clear)(PROPVARIANT *) = (HRESULT(WINAPI *)(PROPVARIANT *))ShlProc(L"ole32.dll", "PropVariantClear");
        hr = convert ? convert(&property, value) : E_FAIL;
        if (clear)
            clear(&property);
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE profile_write(void *face, LPCWSTR name, VARIANT *value)
{
    typedef HRESULT(STDMETHODCALLTYPE * WRITE)(void *, LPCWSTR, LPCWSTR, BOOL, PROPVARIANT *);
    BAG *bag = bag_of(face, 0);
    WCHAR section[64], key[64];
    if ((bag->flags & BAG_ACCESS) == 0)
        return E_ACCESSDENIED;
    HRESULT hr = profile_names(bag, name, section, key);
    if (FAILED(hr))
        return hr;
    PROPVARIANT property = {};
    HRESULT(WINAPI * convert)(const VARIANT *, PROPVARIANT *) =
        (HRESULT(WINAPI *)(const VARIANT *, PROPVARIANT *))ShlProc(L"propsys.dll", "VariantToPropVariant");
    HRESULT(WINAPI * clear)(PROPVARIANT *) = (HRESULT(WINAPI *)(PROPVARIANT *))ShlProc(L"ole32.dll", "PropVariantClear");
    hr = convert ? convert(value, &property) : E_FAIL;
    if (SUCCEEDED(hr)) {
        BOOL raw = key[0] == L'@';
        hr = CALL(WRITE, bag->profile, 9, section, raw ? key + 1 : key, raw, &property);
        if (clear)
            clear(&property);
    }
    return hr;
}

/* ------------------------------------------------------------------ creating bags */

typedef struct {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(void *);
    ULONG(STDMETHODCALLTYPE *Release)(void *);
    void *methods[5];
} BAG_VTBL;

static const BAG_VTBL registry_vtbl = {bag_qi, bag_addref, bag_release0, {reg_read, reg_write}};
static const BAG_VTBL profile_vtbl = {bag_qi, bag_addref, bag_release0, {profile_read, profile_write}};
static const BAG_VTBL bag2_vtbl = {bag2_qi,
                                   bag2_addref,
                                   bag2_release,
                                   {bag2_not_implemented, bag2_not_implemented, bag2_not_implemented,
                                    bag2_not_implemented, bag2_not_implemented}};
static const BAG_VTBL provider_vtbl = {provider_qi, provider_addref, provider_release, {provider_read}};

static BAG *bag_create(const BAG_VTBL *vtbl, DWORD flags)
{
    BAG *bag = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BAG));
    if (bag) {
        bag->bag_vtbl = vtbl;
        bag->bag2_vtbl = &bag2_vtbl;
        bag->provider_vtbl = &provider_vtbl;
        bag->refs = 1;
        bag->flags = flags;
    }
    return bag;
}

HRESULT WINAPI SHCreatePropertyBagOnRegKey(HKEY root, LPCWSTR subkey, DWORD flags, REFIID riid, void **out);
HRESULT WINAPI SHCreatePropertyBagOnRegKey(HKEY root, LPCWSTR subkey, DWORD flags, REFIID riid, void **out)
{
    *out = nullptr;
    BAG *bag = bag_create(&registry_vtbl, flags);
    if (!bag)
        return E_OUTOFMEMORY;
    REGSAM access = 0;
    if ((flags & BAG_ACCESS) != BAG_WRITE_ONLY)
        access = KEY_READ;
    if ((flags & BAG_ACCESS) != 0)
        access |= KEY_WRITE;
    LSTATUS status = (flags & BAG_CREATE) ? RegCreateKeyExW(root, subkey, 0, nullptr, 0, access, nullptr, &bag->key, nullptr)
                                          : RegOpenKeyExW(root, subkey, 0, access, &bag->key);
    HRESULT hr = status_result(status);
    if (SUCCEEDED(hr))
        hr = bag_query(bag, riid, out);
    bag_release(bag);
    return hr;
}

static HRESULT create_profile_bag(void *profile, BOOL add_ref, LPCWSTR section, DWORD flags, REFIID riid, void **out)
{
    *out = nullptr;
    BAG *bag = bag_create(&profile_vtbl, flags);
    HRESULT hr = E_OUTOFMEMORY;
    if (!bag)
        return hr;
    bag->profile = profile;
    if (add_ref)
        CALL(ULONG(STDMETHODCALLTYPE *)(void *), profile, 1);
    hr = S_OK;
    if (section) {
        HRESULT(WINAPI * dup)(LPCWSTR, LPWSTR *) = (HRESULT(WINAPI *)(LPCWSTR, LPWSTR *))ShlProc(L"shcore.dll", "SHStrDupW");
        hr = dup ? dup(section, &bag->section) : E_FAIL;
    }
    if (SUCCEEDED(hr))
        hr = bag_query(bag, riid, out);
    bag_release(bag);
    return hr;
}

HRESULT WINAPI SHCreatePropertyBagOnCachedProfileSection(void *profile, LPCWSTR section, DWORD flags, REFIID riid,
                                                         void **out);
HRESULT WINAPI SHCreatePropertyBagOnCachedProfileSection(void *profile, LPCWSTR section, DWORD flags, REFIID riid,
                                                         void **out)
{
    return create_profile_bag(profile, TRUE, section, flags, riid, out);
}

HRESULT WINAPI SHCreatePropertyBagOnProfileSection(LPCWSTR path, LPCWSTR section, DWORD flags, REFIID riid, void **out);
HRESULT WINAPI SHCreatePropertyBagOnProfileSection(LPCWSTR path, LPCWSTR section, DWORD flags, REFIID riid, void **out)
{
    void *created = nullptr, *profile = nullptr;
    *out = nullptr;
    HRESULT hr = SHCoCreateInstanceAC(&CLSID_CachedPrivateProfile, nullptr, CLSCTX_INPROC_SERVER,
                                      &IID_CachedPrivateProfile, &created);
    if (SUCCEEDED(hr)) {
        hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, LPCWSTR, DWORD), created, 3, path, 0);
        if (SUCCEEDED(hr))
            hr = ((IUnknown *)created)->lpVtbl->QueryInterface(created, &IID_CachedPrivateProfile, &profile);
        CALL(ULONG(STDMETHODCALLTYPE *)(void *), created, 2);
    }
    if (SUCCEEDED(hr)) {
        hr = create_profile_bag(profile, FALSE, section, flags, riid, out);
        if (hr == E_OUTOFMEMORY)
            CALL(ULONG(STDMETHODCALLTYPE *)(void *), profile, 2);
    }
    return hr;
}

HRESULT WINAPI SHCreatePropertyBagOnMemory(DWORD mode, REFIID riid, void **out);
HRESULT WINAPI SHCreatePropertyBagOnMemory(DWORD mode, REFIID riid, void **out)
{
    HRESULT(WINAPI * create)(REFIID, void **) = (HRESULT(WINAPI *)(REFIID, void **))ShlProc(L"propsys.dll",
                                                                                          "PSCreateMemoryPropertyStore");
    *out = nullptr;
    return create ? create(riid, out) : E_FAIL;
}

HRESULT WINAPI SHPropertyBag_ReadPropertyKey(IPropertyBag *bag, LPCWSTR name, SHL_PROPERTYKEY *key);
HRESULT WINAPI SHPropertyBag_ReadPropertyKey(IPropertyBag *bag, LPCWSTR name, SHL_PROPERTYKEY *key)
{
    HRESULT(WINAPI * read)(IPropertyBag *, LPCWSTR, SHL_PROPERTYKEY *) =
        (HRESULT(WINAPI *)(IPropertyBag *, LPCWSTR, SHL_PROPERTYKEY *))ShlProc(L"propsys.dll", "PSPropertyBag_ReadPropertyKey");
    return read ? read(bag, name, key) : E_FAIL;
}

HRESULT WINAPI SHPropertyBag_WritePropertyKey(IPropertyBag *bag, LPCWSTR name, const SHL_PROPERTYKEY *key);
HRESULT WINAPI SHPropertyBag_WritePropertyKey(IPropertyBag *bag, LPCWSTR name, const SHL_PROPERTYKEY *key)
{
    HRESULT(WINAPI * write)(IPropertyBag *, LPCWSTR, const SHL_PROPERTYKEY *) =
        (HRESULT(WINAPI *)(IPropertyBag *, LPCWSTR, const SHL_PROPERTYKEY *))ShlProc(L"propsys.dll",
                                                                                 "PSPropertyBag_WritePropertyKey");
    return write ? write(bag, name, key) : E_FAIL;
}

/*
 * the store over an xml document is not made, and a program that needs it gets the answer of a method that is not
 * there.
 */
HRESULT WINAPI SHCreatePropertyStoreOnXML(IStream *stream, DWORD mode, IPropertyBag *bag, REFIID riid, void **out);
HRESULT WINAPI SHCreatePropertyStoreOnXML(IStream *stream, DWORD mode, IPropertyBag *bag, REFIID riid, void **out)
{
    *out = nullptr;
    return E_NOTIMPL;
}

/* ------------------------------------------------------------------ view state bags */

#define CLASS_NOT_REGISTERED ((HRESULT)0x80040154)

/*
 * the bag of a folder view lives in a slot that the list of recent folders hands out, and that list is a class of
 * the shell that is not here, so no bag is made.
 */
HRESULT WINAPI SHGetViewStatePropertyBag(const struct _ITEMIDLIST *pidl, LPCWSTR name, DWORD flags, REFIID riid,
                                         void **out)
{
    void *list = nullptr;
    HRESULT hr = SHCoCreateInstanceAC(&CLSID_MruPidlList, nullptr, CLSCTX_INPROC_SERVER, &IID_MruPidlList, &list);
    if (out)
        *out = nullptr;
    if (SUCCEEDED(hr)) {
        CALL(ULONG(STDMETHODCALLTYPE *)(void *), list, 2);
        hr = CLASS_NOT_REGISTERED;
    }
    return hr;
}

/* a bag that does not exist is cleared by deleting its keys, which is all there is to do without the list. */
HRESULT WINAPI SHClearViewStatePropertyBag(const void *pidl, LPCWSTR name, BOOL forget);
HRESULT WINAPI SHClearViewStatePropertyBag(const void *pidl, LPCWSTR name, BOOL forget)
{
    static const DWORD kinds[] = {0x21, 0x11};
    for (int i = 0; i < 2; i++) {
        HKEY shell = SHGetShellKeyEx(kinds[i], nullptr, TRUE, KEY_READ);
        if (!shell)
            continue;
        if (!pidl) {
            HKEY bags = nullptr;
            if (RegOpenKeyExW(shell, L"Bags", 0, 0x3001f, &bags) == ERROR_SUCCESS) {
                WCHAR slot[15], path[MAX_PATH];
                for (DWORD n = 0; RegEnumKeyW(bags, n, slot, 15) == ERROR_SUCCESS; n++)
                    if (wnsprintfW(path, MAX_PATH, L"%s\\%s", slot, name) >= 0)
                        SHDeleteKeyW(bags, path);
                RegCloseKey(bags);
            }
        }
        RegCloseKey(shell);
    }
    return S_OK;
}
