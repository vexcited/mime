/*
 * activeds.dll, the Active Directory service interfaces. The machine has no directory and no provider for the names
 * of one, so the functions that bind to an object fail as a path that leads nowhere, the functions on memory and
 * strings work, and the conversions of values are not made.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/winerror.h>

#define ADSAPI DECLSPEC_EXPORT

#define E_ADS_BAD_PATHNAME ((HRESULT)0x80005000)

typedef struct IEnumVARIANT IEnumVARIANT;
typedef struct IEnumVARIANTVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IEnumVARIANT *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IEnumVARIANT *This);
    ULONG(STDMETHODCALLTYPE *Release)(IEnumVARIANT *This);
    HRESULT(STDMETHODCALLTYPE *Next)(IEnumVARIANT *This, ULONG count, VARIANT *values, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(IEnumVARIANT *This, ULONG count);
    HRESULT(STDMETHODCALLTYPE *Reset)(IEnumVARIANT *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IEnumVARIANT *This, IEnumVARIANT **clone);
} IEnumVARIANTVtbl;
struct IEnumVARIANT {
    const IEnumVARIANTVtbl *lpVtbl;
};

/* at most count characters of the text, and an end. */
static void copy_text(LPWSTR out, LPCWSTR text, SIZE_T count)
{
    SIZE_T i = 0;
    for (; i + 1 < count && text[i]; i++)
        out[i] = text[i];
    out[i] = 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

ADSAPI HRESULT WINAPI ADsGetObject(LPCWSTR path, REFIID iid, void **object)
{
    if (!object)
        return E_INVALIDARG;
    *object = nullptr;
    if (!path || !*path)
        return E_ADS_BAD_PATHNAME;
    /* a name with no provider before the colon is no name; one with a provider has no provider to go to. */
    return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
}

ADSAPI HRESULT WINAPI ADsOpenObject(LPCWSTR path, LPCWSTR user, LPCWSTR password, DWORD flags, REFIID iid, void **object)
{
    return ADsGetObject(path, iid, object);
}

ADSAPI HRESULT WINAPI ADsBuildEnumerator(IUnknown *container, IEnumVARIANT **enumerator)
{
    if (!container || !enumerator)
        return E_INVALIDARG;
    *enumerator = nullptr;
    return E_NOINTERFACE;
}

ADSAPI HRESULT WINAPI ADsFreeEnumerator(IEnumVARIANT *enumerator)
{
    if (enumerator)
        enumerator->lpVtbl->Release(enumerator);
    return S_OK;
}

ADSAPI HRESULT WINAPI ADsEnumerateNext(IEnumVARIANT *enumerator, ULONG count, VARIANT *values, ULONG *fetched)
{
    if (!enumerator)
        return E_INVALIDARG;
    return enumerator->lpVtbl->Next(enumerator, count, values, fetched);
}

ADSAPI HRESULT WINAPI ADsBuildVarArrayStr(LPWSTR *strings, DWORD count, VARIANT *array)
{
    return E_NOTIMPL;
}

ADSAPI HRESULT WINAPI ADsBuildVarArrayInt(LPDWORD numbers, DWORD count, VARIANT *array)
{
    return E_NOTIMPL;
}

/* the last error of the providers: kept for the process, where windows keeps it for the thread. */
static SRWLOCK g_error_lock = SRWLOCK_INIT;
static DWORD g_error;
static WCHAR g_error_text[256], g_error_provider[64];

ADSAPI VOID WINAPI ADsSetLastError(DWORD error, LPCWSTR text, LPCWSTR provider)
{
    AcquireSRWLockExclusive(&g_error_lock);
    g_error = error;
    g_error_text[0] = g_error_provider[0] = 0;
    if (text)
        copy_text(g_error_text, text, RTL_NUMBER_OF(g_error_text));
    if (provider)
        copy_text(g_error_provider, provider, RTL_NUMBER_OF(g_error_provider));
    ReleaseSRWLockExclusive(&g_error_lock);
}

ADSAPI VOID WINAPI ADsGetLastError(LPDWORD error, LPWSTR text, DWORD text_size, LPWSTR provider, DWORD provider_size)
{
    AcquireSRWLockShared(&g_error_lock);
    if (error)
        *error = g_error;
    if (text && text_size)
        copy_text(text, g_error_text, text_size);
    if (provider && provider_size)
        copy_text(provider, g_error_provider, provider_size);
    ReleaseSRWLockShared(&g_error_lock);
}

ADSAPI LPVOID WINAPI AllocADsMem(DWORD size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

ADSAPI BOOL WINAPI FreeADsMem(LPVOID memory)
{
    return memory ? HeapFree(GetProcessHeap(), 0, memory) : FALSE;
}

ADSAPI LPVOID WINAPI ReallocADsMem(LPVOID memory, DWORD old_size, DWORD new_size)
{
    void *bigger = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, new_size);
    if (!bigger)
        return nullptr;
    if (memory) {
        memcpy(bigger, memory, old_size < new_size ? old_size : new_size);
        HeapFree(GetProcessHeap(), 0, memory);
    }
    return bigger;
}

ADSAPI LPWSTR WINAPI AllocADsStr(LPCWSTR text)
{
    if (!text)
        return nullptr;
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    LPWSTR copy = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

ADSAPI BOOL WINAPI FreeADsStr(LPWSTR text)
{
    return text ? HeapFree(GetProcessHeap(), 0, text) : FALSE;
}

ADSAPI BOOL WINAPI ReallocADsStr(LPWSTR *text, LPCWSTR value)
{
    if (!text)
        return FALSE;
    LPWSTR copy = value ? AllocADsStr(value) : nullptr;
    if (value && !copy)
        return FALSE;
    FreeADsStr(*text);
    *text = copy;
    return TRUE;
}

/* binary data as text: each byte as a backslash and two hexadecimal digits, the way a directory writes octets. */
ADSAPI HRESULT WINAPI ADsEncodeBinaryData(PBYTE data, DWORD length, LPWSTR *text)
{
    if (!text || (length && !data))
        return E_INVALIDARG;
    LPWSTR out = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)length * 3 + 1) * sizeof(WCHAR));
    if (!out)
        return E_OUTOFMEMORY;
    static const WCHAR digits[] = L"0123456789ABCDEF";
    for (DWORD i = 0; i < length; i++) {
        out[i * 3] = L'\\';
        out[i * 3 + 1] = digits[data[i] >> 4];
        out[i * 3 + 2] = digits[data[i] & 15];
    }
    out[length * 3] = 0;
    *text = out;
    return S_OK;
}

static int hex_value(WCHAR ch)
{
    return ch >= L'0' && ch <= L'9' ? ch - L'0' : ch >= L'a' && ch <= L'f' ? ch - L'a' + 10 :
           ch >= L'A' && ch <= L'F' ? ch - L'A' + 10 : -1;
}

ADSAPI HRESULT WINAPI ADsDecodeBinaryData(LPCWSTR text, PBYTE *data, ULONG *length)
{
    if (!text || !data || !length)
        return E_INVALIDARG;
    SIZE_T count = wcslen(text);
    if (count % 3)
        return E_INVALIDARG;
    PBYTE out = HeapAlloc(GetProcessHeap(), 0, count / 3 + 1);
    if (!out)
        return E_OUTOFMEMORY;
    for (SIZE_T i = 0; i < count / 3; i++) {
        int high = hex_value(text[i * 3 + 1]), low = hex_value(text[i * 3 + 2]);
        if (text[i * 3] != L'\\' || high < 0 || low < 0) {
            HeapFree(GetProcessHeap(), 0, out);
            return E_INVALIDARG;
        }
        out[i] = (BYTE)(high << 4 | low);
    }
    *data = out;
    *length = (ULONG)(count / 3);
    return S_OK;
}

ADSAPI HRESULT WINAPI PropVariantToAdsType(VARIANT *variant, DWORD count, PVOID *values, PDWORD number) { return E_NOTIMPL; }
ADSAPI HRESULT WINAPI AdsTypeToPropVariant(PVOID values, DWORD count, VARIANT *variant) { return E_NOTIMPL; }
ADSAPI VOID WINAPI AdsFreeAdsValues(PVOID values, DWORD count) {}
ADSAPI HRESULT WINAPI AdsTypeToPropVariant2(PVOID values, DWORD count, VARIANT *variant) { return E_NOTIMPL; }
ADSAPI HRESULT WINAPI PropVariantToAdsType2(VARIANT *variant, DWORD count, PVOID *values, PDWORD number) { return E_NOTIMPL; }
ADSAPI HRESULT WINAPI ConvertSecDescriptorToVariant(LPCWSTR server, LPCWSTR user, PBYTE descriptor, VARIANT *variant, DWORD flags) { return E_NOTIMPL; }
ADSAPI HRESULT WINAPI ConvertSecurityDescriptorToSecDes(LPCWSTR server, LPCWSTR user, PBYTE descriptor, DWORD length, IDispatch **object, DWORD flags) { return E_NOTIMPL; }
ADSAPI HRESULT WINAPI BinarySDToSecurityDescriptor(PBYTE descriptor, VARIANT *variant, LPCWSTR server, LPCWSTR user, DWORD flags) { return E_NOTIMPL; }
ADSAPI HRESULT WINAPI SecurityDescriptorToBinarySD(VARIANT variant, PBYTE *descriptor, PDWORD length, LPCWSTR server, LPCWSTR user, DWORD flags) { return E_NOTIMPL; }
ADSAPI HRESULT WINAPI ConvertTrusteeToSid(LPCWSTR server, LPCWSTR user, LPCWSTR trustee, PSID *sid, PDWORD length) { return E_NOTIMPL; }

ADSAPI HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_OK;
}

ADSAPI HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **object)
{
    if (object)
        *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}
