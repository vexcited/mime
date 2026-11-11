/*
 * msdmo.dll: the registry of DirectX media objects and the helpers for their media types. What each call writes,
 * answers and refuses was read from msdmo.dll of Windows 11 with a test program, the odd parts included.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/objbase.h>
#include <rtl.h>

#define DMO_REGISTERF_IS_KEYED 0x1
#define DMO_ENUMF_INCLUDE_KEYED 0x1
#define GUID_CHARS 36

typedef struct DMO_MEDIA_TYPE {
    GUID majortype;
    GUID subtype;
    BOOL bFixedSizeSamples;
    BOOL bTemporalCompression;
    ULONG lSampleSize;
    GUID formattype;
    IUnknown *pUnk;
    ULONG cbFormat;
    BYTE *pbFormat;
} DMO_MEDIA_TYPE;

typedef struct DMO_PARTIAL_MEDIATYPE {
    GUID type;
    GUID subtype;
} DMO_PARTIAL_MEDIATYPE;

typedef struct IEnumDMO IEnumDMO;
typedef struct IEnumDMOVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumDMO *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumDMO *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumDMO *This);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumDMO *This, DWORD count, CLSID *ids, LPWSTR *names, DWORD *fetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumDMO *This, DWORD count);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumDMO *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumDMO *This, IEnumDMO **out);
} IEnumDMOVtbl;
struct IEnumDMO {
    const IEnumDMOVtbl *lpVtbl;
};

static const GUID IID_IEnumDMO = {0x2c3cd98a, 0x2bfa, 0x4a53, {0x9c, 0x27, 0x52, 0x49, 0xba, 0x64, 0xba, 0x0f}};
static const GUID g_null;

static const WCHAR g_objects[] = L"DirectShow\\MediaObjects";
static const WCHAR g_categories[] = L"DirectShow\\MediaObjects\\Categories";

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

static HRESULT failure(LSTATUS error)
{
    return error ? (HRESULT)(0x80070000u | (error & 0xFFFF)) : S_OK;
}

/* ------------------------------------------------------------------ guids as text */

/* the registry names a guid in lower case and without braces. */
static void guid_text(REFGUID id, WCHAR out[GUID_CHARS + 1])
{
    _snwprintf(out, GUID_CHARS + 1, L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", (unsigned)id->Data1, id->Data2,
               id->Data3, id->Data4[0], id->Data4[1], id->Data4[2], id->Data4[3], id->Data4[4], id->Data4[5],
               id->Data4[6], id->Data4[7]);
    out[GUID_CHARS] = 0;
}

static int hex_digit(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* one field as scanf's %x of that width reads it: at least one digit, no more than the width. */
static BOOL hex_field(const WCHAR **text, int width, ULONG *value)
{
    const WCHAR *p = *text;
    int digits = 0;
    ULONG v = 0;
    while (digits < width && hex_digit(*p) >= 0) {
        v = v << 4 | (ULONG)hex_digit(*p++);
        digits++;
    }
    if (!digits)
        return FALSE;
    *text = p;
    *value = v;
    return TRUE;
}

/*
 * windows scans the text field by field into the guid, so the fields before a bad one are written, a short last
 * field is taken, and what follows the last field is not looked at. braces are not accepted.
 */
static BOOL text_guid(const WCHAR *text, GUID *id)
{
    ULONG v;
    if (!hex_field(&text, 8, &v))
        return FALSE;
    id->Data1 = v;
    if (*text++ != '-' || !hex_field(&text, 4, &v))
        return FALSE;
    id->Data2 = (USHORT)v;
    if (*text++ != '-' || !hex_field(&text, 4, &v))
        return FALSE;
    id->Data3 = (USHORT)v;
    if (*text++ != '-')
        return FALSE;
    for (int i = 0; i < 8; i++) {
        if (i == 2 && *text++ != '-')
            return FALSE;
        if (!hex_field(&text, 2, &v))
            return FALSE;
        id->Data4[i] = (UCHAR)v;
    }
    return TRUE;
}

DECLSPEC_EXPORT HRESULT WINAPI DMOGuidToStrW(LPWSTR text, REFGUID id)
{
    guid_text(id, text);
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI DMOGuidToStrA(LPSTR text, REFGUID id)
{
    WCHAR wide[GUID_CHARS + 1];
    guid_text(id, wide);
    for (int i = 0; i <= GUID_CHARS; i++)
        text[i] = (char)wide[i];
    return S_OK;
}

DECLSPEC_EXPORT BOOL WINAPI DMOStrToGuidW(LPCWSTR text, GUID *id)
{
    return text_guid(text, id);
}

DECLSPEC_EXPORT BOOL WINAPI DMOStrToGuidA(LPCSTR text, GUID *id)
{
    WCHAR wide[GUID_CHARS + 2];
    int i = 0;
    for (; i <= GUID_CHARS && text[i]; i++)
        wide[i] = (UCHAR)text[i];
    wide[i] = 0;
    return text_guid(wide, id);
}

/* ------------------------------------------------------------------ the registry of objects */

static void object_path(REFCLSID id, WCHAR *out, SIZE_T chars)
{
    WCHAR text[GUID_CHARS + 1];
    guid_text(id, text);
    _snwprintf(out, chars, L"%ls\\%ls", g_objects, text);
}

static void category_path(REFGUID category, const CLSID *id, WCHAR *out, SIZE_T chars)
{
    WCHAR text[GUID_CHARS + 1], object[GUID_CHARS + 1];
    guid_text(category, text);
    if (id) {
        guid_text(id, object);
        _snwprintf(out, chars, L"%ls\\%ls\\%ls", g_categories, text, object);
    } else {
        _snwprintf(out, chars, L"%ls\\%ls", g_categories, text);
    }
}

static BOOL key_exists(LPCWSTR path)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, KEY_READ, &key))
        return FALSE;
    RegCloseKey(key);
    return TRUE;
}

static LSTATUS set_types(HKEY key, LPCWSTR value, DWORD count, const DMO_PARTIAL_MEDIATYPE *types)
{
    if (!count || !types) {
        RegDeleteValueW(key, value);
        return ERROR_SUCCESS;
    }
    return RegSetValueExW(key, value, 0, REG_BINARY, (const BYTE *)types, count * sizeof(*types));
}

DECLSPEC_EXPORT HRESULT WINAPI DMORegister(LPCWSTR name, REFCLSID id, REFGUID category, DWORD flags, DWORD in_count,
                                           const DMO_PARTIAL_MEDIATYPE *in, DWORD out_count,
                                           const DMO_PARTIAL_MEDIATYPE *out)
{
    if (!name)
        return E_FAIL;
    if (!id || !category || IsEqualGUID(id, &g_null) || IsEqualGUID(category, &g_null))
        return E_INVALIDARG;
    WCHAR path[160];
    HKEY key, sub;
    DWORD made;
    object_path(id, path, RTL_NUMBER_OF(path));
    LSTATUS error = RegCreateKeyExW(HKEY_CLASSES_ROOT, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
    if (error)
        return failure(error);
    error = RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE *)name, (DWORD)((wcslen(name) + 1) * sizeof(WCHAR)));
    if (!error)
        error = set_types(key, L"InputTypes", in_count, in);
    if (!error)
        error = set_types(key, L"OutputTypes", out_count, out);
    if (!error && (flags & DMO_REGISTERF_IS_KEYED)) {
        error = RegCreateKeyExW(key, L"Keyed", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &sub, nullptr);
        if (!error) {
            RegSetValueExW(sub, nullptr, 0, REG_SZ, (const BYTE *)L"", sizeof(WCHAR));
            RegCloseKey(sub);
        }
    } else if (!error) {
        RegDeleteKeyW(key, L"Keyed");
    }
    RegCloseKey(key);
    if (error)
        return failure(error);

    category_path(category, nullptr, path, RTL_NUMBER_OF(path));
    error = RegCreateKeyExW(HKEY_CLASSES_ROOT, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &made);
    if (error)
        return failure(error);
    if (made == REG_CREATED_NEW_KEY) {
        static const WCHAR unknown[] = L"Unknown DMO category";
        RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE *)unknown, sizeof(unknown));
    }
    WCHAR object[GUID_CHARS + 1];
    guid_text(id, object);
    error = RegCreateKeyExW(key, object, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &sub, nullptr);
    if (!error)
        RegCloseKey(sub);
    RegCloseKey(key);
    return failure(error);
}

/* whether any category still lists the object, and with remove, taking it out of all of them. */
static BOOL in_categories(REFCLSID id, BOOL remove)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, g_categories, 0, KEY_READ, &key))
        return FALSE;
    BOOL found = FALSE;
    for (DWORD i = 0;; i++) {
        WCHAR name[64], path[200];
        DWORD chars = RTL_NUMBER_OF(name);
        GUID category;
        if (RegEnumKeyExW(key, i, name, &chars, nullptr, nullptr, nullptr, nullptr))
            break;
        if (chars != GUID_CHARS || !text_guid(name, &category))
            continue;
        category_path(&category, id, path, RTL_NUMBER_OF(path));
        if (!key_exists(path))
            continue;
        found = TRUE;
        if (remove)
            RegDeleteTreeW(HKEY_CLASSES_ROOT, path), RegDeleteKeyW(HKEY_CLASSES_ROOT, path);
    }
    RegCloseKey(key);
    return found;
}

static void delete_object(REFCLSID id)
{
    WCHAR path[160];
    object_path(id, path, RTL_NUMBER_OF(path));
    RegDeleteTreeW(HKEY_CLASSES_ROOT, path);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, path);
}

/* like windows, the object's own key goes with its last category, and nothing to remove is S_FALSE. */
DECLSPEC_EXPORT HRESULT WINAPI DMOUnregister(REFCLSID id, REFGUID category)
{
    if (!id || !category)
        return E_POINTER;
    WCHAR path[200];
    if (IsEqualGUID(category, &g_null)) {
        object_path(id, path, RTL_NUMBER_OF(path));
        BOOL found = in_categories(id, TRUE) | key_exists(path);
        delete_object(id);
        return found ? S_OK : S_FALSE;
    }
    category_path(category, id, path, RTL_NUMBER_OF(path));
    if (!key_exists(path))
        return S_FALSE;
    RegDeleteTreeW(HKEY_CLASSES_ROOT, path);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, path);
    if (!in_categories(id, FALSE))
        delete_object(id);
    return S_OK;
}

/* windows reads the name into 80 bytes, so a name of 40 characters or more comes back empty with S_FALSE. */
static HRESULT object_name(HKEY key, WCHAR name[80])
{
    WCHAR text[40];
    DWORD size = sizeof(text), type;
    name[0] = 0;
    if (RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE *)text, &size) || type != REG_SZ)
        return S_FALSE;
    DWORD chars = size / sizeof(WCHAR);
    if (chars && !text[chars - 1])
        chars--;
    memcpy(name, text, chars * sizeof(WCHAR));
    name[chars] = 0;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI DMOGetName(REFCLSID id, WCHAR name[80])
{
    if (!id || !name)
        return E_POINTER;
    WCHAR path[160];
    HKEY key;
    object_path(id, path, RTL_NUMBER_OF(path));
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, KEY_READ, &key))
        return E_FAIL;
    HRESULT hr = object_name(key, name);
    RegCloseKey(key);
    return hr;
}

/* the types of one direction, allocated; count is how many there are. */
static DMO_PARTIAL_MEDIATYPE *read_types(HKEY key, LPCWSTR value, ULONG *count)
{
    DWORD size = 0, type;
    *count = 0;
    if (RegQueryValueExW(key, value, nullptr, &type, nullptr, &size) || type != REG_BINARY ||
        size < sizeof(DMO_PARTIAL_MEDIATYPE))
        return nullptr;
    DMO_PARTIAL_MEDIATYPE *types = CoTaskMemAlloc(size);
    if (!types)
        return nullptr;
    if (RegQueryValueExW(key, value, nullptr, &type, (BYTE *)types, &size)) {
        CoTaskMemFree(types);
        return nullptr;
    }
    *count = size / sizeof(DMO_PARTIAL_MEDIATYPE);
    return types;
}

static ULONG supply_types(HKEY key, LPCWSTR value, ULONG room, DMO_PARTIAL_MEDIATYPE *out)
{
    ULONG count;
    DMO_PARTIAL_MEDIATYPE *types = read_types(key, value, &count);
    if (count > room)
        count = room;
    if (count && out)
        memcpy(out, types, count * sizeof(*types));
    CoTaskMemFree(types);
    return out ? count : 0;
}

/* S_FALSE says a direction had fewer types than there was room for. */
DECLSPEC_EXPORT HRESULT WINAPI DMOGetTypes(REFCLSID id, ULONG in_room, ULONG *in_count, DMO_PARTIAL_MEDIATYPE *in,
                                           ULONG out_room, ULONG *out_count, DMO_PARTIAL_MEDIATYPE *out)
{
    if (!id)
        return E_POINTER;
    WCHAR path[160];
    HKEY key;
    object_path(id, path, RTL_NUMBER_OF(path));
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, KEY_READ, &key))
        return E_FAIL;
    ULONG got_in = supply_types(key, L"InputTypes", in_room, in);
    ULONG got_out = supply_types(key, L"OutputTypes", out_room, out);
    RegCloseKey(key);
    if (in_count)
        *in_count = got_in;
    if (out_count)
        *out_count = got_out;
    return got_in < in_room || got_out < out_room ? S_FALSE : S_OK;
}

/* ------------------------------------------------------------------ enumeration */

typedef struct ENUM {
    IEnumDMO Interface;
    LONG References;
    CLSID *Ids;
    ULONG Count, Position;
} ENUM;

/* a null guid on either side stands for any type. */
static BOOL type_matches(const DMO_PARTIAL_MEDIATYPE *have, const DMO_PARTIAL_MEDIATYPE *want)
{
    BOOL major = IsEqualGUID(&want->type, &g_null) || IsEqualGUID(&have->type, &g_null) ||
                 IsEqualGUID(&want->type, &have->type);
    BOOL sub = IsEqualGUID(&want->subtype, &g_null) || IsEqualGUID(&have->subtype, &g_null) ||
               IsEqualGUID(&want->subtype, &have->subtype);
    return major && sub;
}

/* any wanted type that the object lists is enough. */
static BOOL direction_matches(HKEY key, LPCWSTR value, DWORD count, const DMO_PARTIAL_MEDIATYPE *wanted)
{
    if (!count)
        return TRUE;
    ULONG have_count;
    DMO_PARTIAL_MEDIATYPE *have = read_types(key, value, &have_count);
    BOOL match = FALSE;
    for (ULONG i = 0; i < have_count && !match; i++)
        for (DWORD k = 0; k < count && !match; k++)
            match = type_matches(&have[i], &wanted[k]);
    CoTaskMemFree(have);
    return match;
}

static BOOL object_matches(REFCLSID id, DWORD flags, DWORD in_count, const DMO_PARTIAL_MEDIATYPE *in, DWORD out_count,
                           const DMO_PARTIAL_MEDIATYPE *out)
{
    WCHAR path[160];
    HKEY key, keyed;
    object_path(id, path, RTL_NUMBER_OF(path));
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, KEY_READ, &key))
        return FALSE;
    BOOL match = TRUE;
    if (!(flags & DMO_ENUMF_INCLUDE_KEYED) && !RegOpenKeyExW(key, L"Keyed", 0, KEY_READ, &keyed)) {
        RegCloseKey(keyed);
        match = FALSE;
    }
    match = match && direction_matches(key, L"InputTypes", in_count, in) &&
            direction_matches(key, L"OutputTypes", out_count, out);
    RegCloseKey(key);
    return match;
}

static HRESULT STDMETHODCALLTYPE enum_query_interface(IEnumDMO *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IEnumDMO))
        return E_NOINTERFACE;
    InterlockedIncrement(&((ENUM *)This)->References);
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE enum_add_ref(IEnumDMO *This)
{
    return (ULONG)InterlockedIncrement(&((ENUM *)This)->References);
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumDMO *This)
{
    ENUM *e = (ENUM *)This;
    ULONG left = (ULONG)InterlockedDecrement(&e->References);
    if (!left) {
        CoTaskMemFree(e->Ids);
        CoTaskMemFree(e);
    }
    return left;
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumDMO *This, DWORD count, CLSID *ids, LPWSTR *names, DWORD *fetched)
{
    ENUM *e = (ENUM *)This;
    if (!ids)
        return E_POINTER;
    if (!fetched && count > 1)
        return E_INVALIDARG;
    DWORD done = 0;
    while (done < count && e->Position < e->Count) {
        ids[done] = e->Ids[e->Position];
        if (names) {
            WCHAR name[80];
            if (DMOGetName(&ids[done], name) < 0)
                name[0] = 0;
            SIZE_T bytes = (wcslen(name) + 1) * sizeof(WCHAR);
            names[done] = CoTaskMemAlloc(bytes);
            if (!names[done])
                break;
            memcpy(names[done], name, bytes);
        }
        e->Position++;
        done++;
    }
    if (fetched)
        *fetched = done;
    return done == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(IEnumDMO *This, DWORD count)
{
    ENUM *e = (ENUM *)This;
    ULONG left = e->Count - e->Position;
    e->Position += count < left ? count : left;
    return count <= left ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumDMO *This)
{
    ((ENUM *)This)->Position = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumDMO *This, IEnumDMO **out)
{
    return E_NOTIMPL;
}

static const IEnumDMOVtbl g_enum_vtbl = {enum_query_interface, enum_add_ref, enum_release, enum_next,
                                         enum_skip,            enum_reset,   enum_clone};

/* the null category lists every object, which are the keys beside Categories. */
DECLSPEC_EXPORT HRESULT WINAPI DMOEnum(REFGUID category, DWORD flags, DWORD in_count, const DMO_PARTIAL_MEDIATYPE *in,
                                       DWORD out_count, const DMO_PARTIAL_MEDIATYPE *out, IEnumDMO **result)
{
    if (!result)
        return E_POINTER;
    if ((in_count && !in) || (out_count && !out))
        return E_INVALIDARG;
    ENUM *e = CoTaskMemAlloc(sizeof(*e));
    if (!e)
        return E_OUTOFMEMORY;
    memset(e, 0, sizeof(*e));
    e->Interface.lpVtbl = &g_enum_vtbl;
    e->References = 1;
    WCHAR path[160];
    HKEY key;
    if (!category || IsEqualGUID(category, &g_null))
        wcscpy(path, g_objects);
    else
        category_path(category, nullptr, path, RTL_NUMBER_OF(path));
    if (!RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, KEY_READ, &key)) {
        ULONG room = 0;
        for (DWORD i = 0;; i++) {
            WCHAR name[64];
            DWORD chars = RTL_NUMBER_OF(name);
            CLSID id;
            if (RegEnumKeyExW(key, i, name, &chars, nullptr, nullptr, nullptr, nullptr))
                break;
            if (chars != GUID_CHARS || !text_guid(name, &id) || !object_matches(&id, flags, in_count, in, out_count, out))
                continue;
            if (e->Count == room) {
                ULONG larger = room ? room * 2 : 16;
                CLSID *ids = CoTaskMemAlloc(larger * sizeof(CLSID));
                if (!ids)
                    break;
                if (e->Count)
                    memcpy(ids, e->Ids, e->Count * sizeof(CLSID));
                CoTaskMemFree(e->Ids);
                e->Ids = ids;
                room = larger;
            }
            e->Ids[e->Count++] = id;
        }
        RegCloseKey(key);
    }
    *result = &e->Interface;
    return S_OK;
}

/* ------------------------------------------------------------------ media types */

/* only the format and the object are set, the other fields are the caller's. */
DECLSPEC_EXPORT HRESULT WINAPI MoInitMediaType(DMO_MEDIA_TYPE *type, DWORD format_size)
{
    if (!type)
        return E_POINTER;
    type->pUnk = nullptr;
    type->pbFormat = nullptr;
    type->cbFormat = 0;
    if (format_size) {
        type->pbFormat = CoTaskMemAlloc(format_size);
        if (!type->pbFormat)
            return E_OUTOFMEMORY;
        type->cbFormat = format_size;
    }
    return S_OK;
}

/* like windows, the size of the freed format is left as it was. */
DECLSPEC_EXPORT HRESULT WINAPI MoFreeMediaType(DMO_MEDIA_TYPE *type)
{
    if (!type)
        return E_POINTER;
    if (type->pbFormat) {
        CoTaskMemFree(type->pbFormat);
        type->pbFormat = nullptr;
    }
    if (type->pUnk) {
        type->pUnk->lpVtbl->Release(type->pUnk);
        type->pUnk = nullptr;
    }
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI MoCopyMediaType(DMO_MEDIA_TYPE *to, const DMO_MEDIA_TYPE *from)
{
    if (!to || !from)
        return E_POINTER;
    *to = *from;
    to->pbFormat = nullptr;
    if (from->cbFormat && from->pbFormat) {
        to->pbFormat = CoTaskMemAlloc(from->cbFormat);
        if (!to->pbFormat) {
            to->cbFormat = 0;
            to->pUnk = nullptr;
            return E_OUTOFMEMORY;
        }
        memcpy(to->pbFormat, from->pbFormat, from->cbFormat);
    }
    if (to->pUnk)
        to->pUnk->lpVtbl->AddRef(to->pUnk);
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI MoCreateMediaType(DMO_MEDIA_TYPE **result, DWORD format_size)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    DMO_MEDIA_TYPE *type = CoTaskMemAlloc(sizeof(*type));
    if (!type)
        return E_OUTOFMEMORY;
    HRESULT hr = MoInitMediaType(type, format_size);
    if (hr < 0) {
        CoTaskMemFree(type);
        return hr;
    }
    *result = type;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI MoDeleteMediaType(DMO_MEDIA_TYPE *type)
{
    if (!type)
        return E_POINTER;
    MoFreeMediaType(type);
    CoTaskMemFree(type);
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI MoDuplicateMediaType(DMO_MEDIA_TYPE **result, const DMO_MEDIA_TYPE *from)
{
    if (!result || !from)
        return E_POINTER;
    *result = nullptr;
    DMO_MEDIA_TYPE *type = CoTaskMemAlloc(sizeof(*type));
    if (!type)
        return E_OUTOFMEMORY;
    HRESULT hr = MoCopyMediaType(type, from);
    if (hr < 0) {
        CoTaskMemFree(type);
        return hr;
    }
    *result = type;
    return S_OK;
}
