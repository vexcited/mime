/* property keys by name and as strings. */
#include "propsys_private.h"
#include "keys_table.h"

HRESULT WINAPI PSGetPropertyKeyFromName(LPCWSTR name, PROPERTYKEY *key)
{
    memset(key, 0, sizeof(*key));
    if (!name)
        return E_INVALIDARG;
    for (UINT i = 0; i < sizeof(g_keys) / sizeof(g_keys[0]); i++)
        if (!_wcsicmp(g_keys[i].name, name)) {
            *key = g_keys[i].key;
            return S_OK;
        }
    return TYPE_E_ELEMENTNOTFOUND;
}

HRESULT WINAPI PSGetNameFromPropertyKey(REFPROPERTYKEY key, LPWSTR *name)
{
    *name = nullptr;
    for (UINT i = 0; i < sizeof(g_keys) / sizeof(g_keys[0]); i++)
        if (g_keys[i].key.pid == key->pid && IsEqualGUID(&g_keys[i].key.fmtid, &key->fmtid))
            return PsAllocString(g_keys[i].name, name);
    return TYPE_E_ELEMENTNOTFOUND;
}

/* "{fmtid} pid": needs at least 41 characters or nothing is written. */
HRESULT WINAPI PSStringFromPropertyKey(REFPROPERTYKEY key, LPWSTR string, UINT length)
{
    if (!string)
        return E_POINTER;
    if (length < GUIDSTRING_MAX + 2)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    *string = 0;
    if (!StringFromGUID2(&key->fmtid, string, (int)length))
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    WCHAR digits[12], *p = digits + 11;
    DWORD pid = key->pid;
    *p = 0;
    do {
        *--p = (WCHAR)('0' + pid % 10);
        pid /= 10;
    } while (pid);
    SIZE_T needed = (SIZE_T)(digits + 11 - p);
    if (GUIDSTRING_MAX + needed > length) {
        *string = 0;
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }
    string[GUIDSTRING_MAX - 1] = ' ';
    memcpy(string + GUIDSTRING_MAX, p, (needed + 1) * sizeof(WCHAR));
    return S_OK;
}

/* like StrToIntW: an optional sign and decimal digits, the rest is ignored. */
static LONG string_to_int(LPCWSTR s)
{
    BOOL negative = *s == '-';
    if (*s == '-' || *s == '+')
        s++;
    LONG value = 0;
    while (*s >= '0' && *s <= '9')
        value = value * 10 + (*s++ - '0');
    return negative ? -value : value;
}

/* the guid, spaces, an optional comma or dash, more spaces, then the pid. */
HRESULT WINAPI PSPropertyKeyFromString(LPCWSTR string, PROPERTYKEY *key)
{
    if (!string || !key)
        return E_POINTER;
    memset(key, 0, sizeof(*key));
    if (!*string || !PsGuidFromString(string, &key->fmtid)) {
        memset(key, 0, sizeof(*key));
        return E_INVALIDARG;
    }
    LPCWSTR p = string + GUIDSTRING_MAX - 1;
    while (*p == ' ')
        p++;
    if (!*p)
        return E_INVALIDARG;
    if (*p == ',' || *p == '-') {
        p++;
        while (*p == ' ')
            p++;
        if (!*p)
            return E_INVALIDARG;
    }
    key->pid = (DWORD)string_to_int(p);
    return S_OK;
}
