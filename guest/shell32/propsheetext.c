/* the property sheet extensions a key names, and the item behind an object. */
#include "shell32_private.h"
#include <mime/win32/prsht.h>

typedef BOOL(CALLBACK *ADD_PAGE)(HPROPSHEETPAGE page, LPARAM param);

typedef struct IShellPropSheetExt IShellPropSheetExt;
typedef struct IShellPropSheetExtVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellPropSheetExt *self, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellPropSheetExt *self);
    ULONG(STDMETHODCALLTYPE *Release)(IShellPropSheetExt *self);
    HRESULT(STDMETHODCALLTYPE *AddPages)(IShellPropSheetExt *self, ADD_PAGE add, LPARAM param);
    HRESULT(STDMETHODCALLTYPE *ReplacePage)(IShellPropSheetExt *self, UINT page, ADD_PAGE replace, LPARAM param);
} IShellPropSheetExtVtbl;
struct IShellPropSheetExt {
    const IShellPropSheetExtVtbl *lpVtbl;
};

typedef struct IShellExtInit IShellExtInit;
typedef struct IShellExtInitVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IShellExtInit *self, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IShellExtInit *self);
    ULONG(STDMETHODCALLTYPE *Release)(IShellExtInit *self);
    HRESULT(STDMETHODCALLTYPE *Initialize)(IShellExtInit *self, PCIDLIST_ABSOLUTE folder, IUnknown *data, HKEY key);
} IShellExtInitVtbl;
struct IShellExtInit {
    const IShellExtInitVtbl *lpVtbl;
};

MIME_DEFINE_GUID(IID_IShellPropSheetExt_, 0x000214E9, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IShellExtInit_, 0x000214E8, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

typedef struct EXTENSION_ARRAY {
    UINT Count;
    IShellPropSheetExt *Items[];
} EXTENSION_ARRAY;

/*
 * the handlers under shellex\PropertySheetHandlers of a key, each a subkey that names a class or holds one as its
 * value. measured on windows 11: a key without that list gives no array, one with the list gives an array even for
 * a limit of none.
 */
SHSTDAPI void *WINAPI SHCreatePropSheetExtArray(HKEY key, PCWSTR subkey, UINT limit)
{
    WCHAR path[MAX_PATH + 64] = L"";
    if (subkey) {
        if (lstrlenW(subkey) >= MAX_PATH)
            return nullptr;
        lstrcpyW(path, subkey);
        lstrcatW(path, L"\\");
    }
    lstrcatW(path, L"shellex\\PropertySheetHandlers");
    HKEY handlers;
    if (RegOpenKeyExW(key, path, 0, KEY_READ, &handlers))
        return nullptr;
    EXTENSION_ARRAY *array = ShAllocZero(sizeof(*array) + limit * sizeof(array->Items[0]));
    for (DWORD i = 0; array && array->Count < limit; i++) {
        WCHAR name[128], value[64];
        DWORD chars = RTL_NUMBER_OF(name), bytes = sizeof(value);
        if (RegEnumKeyExW(handlers, i, name, &chars, nullptr, nullptr, nullptr, nullptr))
            break;
        CLSID clsid;
        HRESULT hr = E_FAIL;
        if (!RegGetValueW(handlers, name, nullptr, RRF_RT_REG_SZ, nullptr, value, &bytes))
            hr = CLSIDFromString(value, &clsid);
        if (FAILED(hr))
            hr = CLSIDFromString(name, &clsid);
        IShellPropSheetExt *extension = nullptr;
        if (FAILED(hr) || FAILED(CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER, &IID_IShellPropSheetExt_,
                                                  (void **)&extension)))
            continue;
        IShellExtInit *init = nullptr;
        if (SUCCEEDED(extension->lpVtbl->QueryInterface(extension, &IID_IShellExtInit_, (void **)&init))) {
            hr = init->lpVtbl->Initialize(init, nullptr, nullptr, key);
            init->lpVtbl->Release(init);
            if (FAILED(hr)) {
                extension->lpVtbl->Release(extension);
                continue;
            }
        }
        array->Items[array->Count++] = extension;
    }
    RegCloseKey(handlers);
    return array;
}

typedef struct PAGE_COUNT {
    ADD_PAGE Add;
    LPARAM Param;
    UINT Count;
} PAGE_COUNT;

static BOOL CALLBACK count_page(HPROPSHEETPAGE page, LPARAM param)
{
    PAGE_COUNT *count = (PAGE_COUNT *)param;
    if (!count->Add(page, count->Param))
        return FALSE;
    count->Count++;
    return TRUE;
}

/* the pages the extensions add, counted. */
SHSTDAPI UINT WINAPI SHAddFromPropSheetExtArray(void *handle, ADD_PAGE add, LPARAM param)
{
    EXTENSION_ARRAY *array = handle;
    PAGE_COUNT count = {add, param, 0};
    for (UINT i = 0; i < array->Count; i++)
        array->Items[i]->lpVtbl->AddPages(array->Items[i], count_page, (LPARAM)&count);
    return count.Count;
}

SHSTDAPI UINT WINAPI SHReplaceFromPropSheetExtArray(void *handle, UINT page, ADD_PAGE replace, LPARAM param)
{
    EXTENSION_ARRAY *array = handle;
    PAGE_COUNT count = {replace, param, 0};
    for (UINT i = 0; i < array->Count; i++)
        array->Items[i]->lpVtbl->ReplacePage(array->Items[i], page, count_page, (LPARAM)&count);
    return count.Count;
}

SHSTDAPI void WINAPI SHDestroyPropSheetExtArray(void *handle)
{
    EXTENSION_ARRAY *array = handle;
    if (!array)
        return;
    for (UINT i = 0; i < array->Count; i++)
        array->Items[i]->lpVtbl->Release(array->Items[i]);
    ShFree(array);
}

/*
 * the shell item of an object: the object itself when it is one, else the item of the id list the object stands for.
 * measured on windows 11: no object is E_NOINTERFACE and leaves the output alone.
 */
SHSTDAPI HRESULT WINAPI SHGetItemFromObject(IUnknown *object, REFIID riid, void **out)
{
    if (!object)
        return E_NOINTERFACE;
    IShellItem *item = nullptr;
    if (SUCCEEDED(object->lpVtbl->QueryInterface(object, &IID_IShellItem, (void **)&item))) {
        HRESULT hr = item->lpVtbl->QueryInterface(item, riid, out);
        item->lpVtbl->Release(item);
        return hr;
    }
    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHGetIDListFromObject(object, &pidl);
    if (FAILED(hr))
        return hr;
    hr = SHCreateItemFromIDList(pidl, riid, out);
    ILFree(pidl);
    return hr;
}
