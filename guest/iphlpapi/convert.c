/*
 * interface names, indexes, luids, guids and aliases. a name is only the
 * luid spelled out, "<type>_<index>", so converting between the two never
 * looks at the adapters.
 */
#include "private.h"

static const struct {
    const WCHAR *name;
    ULONG type;
} types[] = {
    {L"atm", 37},      {L"ethernet", 6}, {L"ieee1394", 144}, {L"loopback", 24},  {L"other", 1},
    {L"ppp", 23},      {L"tokenring", 9}, {L"tunnel", 131},  {L"wireless", 71},
};

/* the longest name: 20 characters. */
#define NAME_CHARS 21

DWORD NETIOAPI_API_ ConvertInterfaceLuidToNameW(const NET_LUID *luid, PWSTR name, SIZE_T length)
{
    if (!luid || !name)
        return ERROR_INVALID_PARAMETER;
    const WCHAR *type = nullptr;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(types); i++)
        if (types[i].type == luid->Info.IfType)
            type = types[i].name;
    WCHAR text[64];
    int n = type ? _snwprintf(text, RTL_NUMBER_OF(text), L"%s_%I64u", type, (ULONG64)luid->Info.NetLuidIndex)
                 : _snwprintf(text, RTL_NUMBER_OF(text), L"iftype%I64u_%I64u", (ULONG64)luid->Info.IfType,
                              (ULONG64)luid->Info.NetLuidIndex);
    if (n < 0 || (SIZE_T)n >= length) {
        if (length)
            name[0] = 0;
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    memcpy(name, text, (n + 1) * sizeof(WCHAR));
    return NO_ERROR;
}

DWORD NETIOAPI_API_ ConvertInterfaceLuidToNameA(const NET_LUID *luid, PSTR name, SIZE_T length)
{
    SIZE_T chars = length < NAME_CHARS ? length : NAME_CHARS;
    WCHAR wide[NAME_CHARS];
    DWORD error = ConvertInterfaceLuidToNameW(luid, wide, chars);
    if (error)
        return error;
    for (SIZE_T i = 0;; i++) {
        name[i] = (char)wide[i];
        if (!wide[i])
            break;
    }
    return NO_ERROR;
}

static ULONG64 parse_number(const WCHAR *p)
{
    ULONG64 v = 0;
    while (*p >= L'0' && *p <= L'9')
        v = v * 10 + (ULONG64)(*p++ - L'0');
    return v;
}

DWORD NETIOAPI_API_ ConvertInterfaceNameToLuidW(const WCHAR *name, PNET_LUID luid)
{
    if (!luid)
        return ERROR_INVALID_PARAMETER;
    luid->Value = 0;
    if (!name || wcsnlen(name, NAME_CHARS) >= NAME_CHARS)
        return ERROR_INVALID_NAME;
    const WCHAR *underscore = wcschr(name, L'_');
    if (!underscore)
        return ERROR_INVALID_NAME;
    luid->Info.NetLuidIndex = parse_number(underscore + 1) & 0xFFFFFF;
    SIZE_T prefix = (SIZE_T)(underscore - name);
    if (!wcsncmp(name, L"iftype", 6)) {
        luid->Info.IfType = parse_number(name + 6) & 0xFFFF;
        return NO_ERROR;
    }
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(types); i++) {
        if (!wcsncmp(name, types[i].name, prefix)) {
            luid->Info.IfType = types[i].type;
            return NO_ERROR;
        }
    }
    luid->Value = 0;
    return ERROR_INVALID_NAME;
}

DWORD NETIOAPI_API_ ConvertInterfaceNameToLuidA(const CHAR *name, PNET_LUID luid)
{
    if (!name)
        return ERROR_INVALID_NAME;
    SIZE_T n = 0;
    while (n < NAME_CHARS && name[n])
        n++;
    if (n >= NAME_CHARS)
        return ERROR_INVALID_NAME;
    WCHAR wide[NAME_CHARS];
    for (SIZE_T i = 0; i <= n; i++)
        wide[i] = (BYTE)name[i];
    return ConvertInterfaceNameToLuidW(wide, luid);
}

DWORD NETIOAPI_API_ ConvertInterfaceIndexToLuid(NET_IFINDEX index, PNET_LUID luid)
{
    if (!luid)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *a = IphAdapterByIndex(s, index);
    luid->Value = a ? a->luid.Value : 0;
    IphRelease(s);
    return a ? NO_ERROR : ERROR_FILE_NOT_FOUND;
}

DWORD NETIOAPI_API_ ConvertInterfaceLuidToIndex(const NET_LUID *luid, PNET_IFINDEX index)
{
    if (!luid || !index)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *a = IphAdapterByLuid(s, luid);
    *index = a ? a->index : 0;
    IphRelease(s);
    return a ? NO_ERROR : ERROR_FILE_NOT_FOUND;
}

DWORD NETIOAPI_API_ ConvertInterfaceLuidToGuid(const NET_LUID *luid, GUID *guid)
{
    if (!luid || !guid)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *a = IphAdapterByLuid(s, luid);
    if (a)
        *guid = a->guid;
    else
        memset(guid, 0, sizeof(*guid));
    IphRelease(s);
    return a ? NO_ERROR : ERROR_FILE_NOT_FOUND;
}

DWORD NETIOAPI_API_ ConvertInterfaceGuidToLuid(const GUID *guid, PNET_LUID luid)
{
    if (!guid || !luid)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    luid->Value = 0;
    DWORD error = ERROR_INVALID_PARAMETER;
    for (ULONG i = 0; i < s->adapter_count; i++) {
        if (!memcmp(&s->adapters[i].guid, guid, sizeof(*guid))) {
            luid->Value = s->adapters[i].luid.Value;
            error = NO_ERROR;
            break;
        }
    }
    IphRelease(s);
    return error;
}

DWORD NETIOAPI_API_ ConvertInterfaceLuidToAlias(const NET_LUID *luid, PWSTR alias, SIZE_T length)
{
    if (!luid || !alias)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *a = IphAdapterByLuid(s, luid);
    DWORD error = ERROR_FILE_NOT_FOUND;
    if (a) {
        SIZE_T n = wcslen(a->alias);
        if (n >= length) {
            if (length)
                alias[0] = 0;
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            memcpy(alias, a->alias, (n + 1) * sizeof(WCHAR));
            error = NO_ERROR;
        }
    }
    IphRelease(s);
    return error;
}

/* an exact alias first, ignoring case, and else the first alias the given one begins. */
DWORD NETIOAPI_API_ ConvertInterfaceAliasToLuid(const WCHAR *alias, PNET_LUID luid)
{
    if (!alias || !luid || wcsnlen(alias, 256) >= 256)
        return ERROR_INVALID_PARAMETER;
    IPH_SNAPSHOT *s = IphAcquire();
    const IPH_ADAPTER *found = nullptr;
    for (ULONG i = 0; i < s->adapter_count && !found; i++)
        if (!_wcsicmp(s->adapters[i].alias, alias))
            found = &s->adapters[i];
    for (ULONG i = 0; i < s->adapter_count && !found; i++)
        if (!_wcsnicmp(s->adapters[i].alias, alias, wcslen(alias)))
            found = &s->adapters[i];
    luid->Value = found ? found->luid.Value : 0;
    IphRelease(s);
    return found ? NO_ERROR : ERROR_INVALID_PARAMETER;
}

NET_IFINDEX NETIOAPI_API_ if_nametoindex(PCSTR name)
{
    NET_LUID luid;
    NET_IFINDEX index;
    if (ConvertInterfaceNameToLuidA(name, &luid) || ConvertInterfaceLuidToIndex(&luid, &index))
        return 0;
    return index;
}

PCHAR NETIOAPI_API_ if_indextoname(NET_IFINDEX index, PCHAR name)
{
    NET_LUID luid;
    if (ConvertInterfaceIndexToLuid(index, &luid) || ConvertInterfaceLuidToNameA(&luid, name, IF_NAMESIZE))
        return nullptr;
    return name;
}

DWORD NETIOAPI_API_ ConvertLengthToIpv4Mask(ULONG length, PULONG mask)
{
    if (length > 32) {
        *mask = INADDR_NONE;
        return ERROR_INVALID_PARAMETER;
    }
    *mask = IphIpv4Mask((UINT8)length);
    return NO_ERROR;
}

DWORD NETIOAPI_API_ ConvertIpv4MaskToLength(ULONG mask, PUINT8 length)
{
    for (ULONG i = 0; i <= 32; i++) {
        if (IphIpv4Mask((UINT8)i) == mask) {
            *length = (UINT8)i;
            return NO_ERROR;
        }
    }
    return ERROR_INVALID_PARAMETER;
}
