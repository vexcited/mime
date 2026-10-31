/*
 * ntdsapi.dll: the client of the directory service. only the naming of services is here, which is a string made of
 * the parts it is given: DsMakeSpn builds the service principal name of a service on a host.
 */
#include <mime/win32/winbase.h>
#include <rtl.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

#define MAX_PART 256

static int text_length(const WCHAR *text)
{
    int n = 0;
    while (text[n])
        n++;
    return n;
}

/* the digits of a port, without leading zeros. */
static int port_text(USHORT port, WCHAR *out)
{
    WCHAR digits[5];
    int n = 0;
    do {
        digits[n++] = (WCHAR)(L'0' + port % 10);
        port /= 10;
    } while (port);
    for (int i = 0; i < n; i++)
        out[i] = digits[n - 1 - i];
    return n;
}

static BOOL equal_ignoring_case(const WCHAR *a, const WCHAR *b)
{
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

/*
 * class/instance:port/service, where the instance is the host of the service when it is not given and the service name
 * is left out when it is the same as the instance.
 */
DWORD WINAPI DsMakeSpnW(LPCWSTR service_class, LPCWSTR service_name, LPCWSTR instance_name, USHORT instance_port,
                        LPCWSTR referrer, DWORD *length, LPWSTR spn)
{
    if (!service_class || !service_name || !length || (*length && !spn))
        return ERROR_INVALID_PARAMETER;
    int class_length = text_length(service_class), name_length = text_length(service_name);
    int instance_length = instance_name ? text_length(instance_name) : 0;
    if (!class_length || !name_length || class_length > MAX_PART || name_length > MAX_PART || instance_length > MAX_PART)
        return ERROR_INVALID_PARAMETER;
    /* a trailing dot of a full name is not part of the name. */
    if (service_name[name_length - 1] == L'.' && name_length > 1)
        name_length--;

    WCHAR text[MAX_PART * 3 + 16];
    int at = 0;
    memcpy(text, service_class, class_length * sizeof(WCHAR));
    at = class_length;
    text[at++] = L'/';
    const WCHAR *host = instance_length ? instance_name : service_name;
    int host_length = instance_length ? instance_length : name_length;
    memcpy(text + at, host, host_length * sizeof(WCHAR));
    at += host_length;
    if (instance_port) {
        text[at++] = L':';
        at += port_text(instance_port, text + at);
    }
    if (instance_length && !(instance_length == name_length && equal_ignoring_case(instance_name, service_name))) {
        text[at++] = L'/';
        memcpy(text + at, service_name, name_length * sizeof(WCHAR));
        at += name_length;
    }
    text[at] = 0;
    DWORD needed = at + 1;
    if (*length < needed) {
        *length = needed;
        return ERROR_BUFFER_OVERFLOW;
    }
    memcpy(spn, text, needed * sizeof(WCHAR));
    *length = needed;
    return ERROR_SUCCESS;
}

DWORD WINAPI DsMakeSpnA(LPCSTR service_class, LPCSTR service_name, LPCSTR instance_name, USHORT instance_port,
                        LPCSTR referrer, DWORD *length, LPSTR spn)
{
    if (!service_class || !service_name || !length || (*length && !spn))
        return ERROR_INVALID_PARAMETER;
    WCHAR wide_class[MAX_PART + 1], wide_name[MAX_PART + 1], wide_instance[MAX_PART + 1];
    if (!MultiByteToWideChar(CP_ACP, 0, service_class, -1, wide_class, RTL_NUMBER_OF(wide_class)) ||
        !MultiByteToWideChar(CP_ACP, 0, service_name, -1, wide_name, RTL_NUMBER_OF(wide_name)) ||
        (instance_name && !MultiByteToWideChar(CP_ACP, 0, instance_name, -1, wide_instance, RTL_NUMBER_OF(wide_instance))))
        return ERROR_INVALID_PARAMETER;
    WCHAR wide_spn[MAX_PART * 3 + 16];
    DWORD wide_length = RTL_NUMBER_OF(wide_spn);
    DWORD error = DsMakeSpnW(wide_class, wide_name, instance_name ? wide_instance : nullptr, instance_port, nullptr,
                             &wide_length, wide_spn);
    if (error)
        return error;
    int bytes = WideCharToMultiByte(CP_ACP, 0, wide_spn, -1, nullptr, 0, nullptr, nullptr);
    if ((DWORD)bytes > *length) {
        *length = bytes;
        return ERROR_BUFFER_OVERFLOW;
    }
    WideCharToMultiByte(CP_ACP, 0, wide_spn, -1, spn, bytes, nullptr, nullptr);
    *length = bytes;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ binding to a domain controller */

#ifndef ERROR_NO_SUCH_DOMAIN
#define ERROR_NO_SUCH_DOMAIN 1355L
#endif
#define RPC_S_SERVER_UNAVAILABLE_CODE 1722L

/*
 * a machine of a workgroup is in no domain, so without a controller named there is none to locate, and a named one is
 * not reachable. nothing binds, so no handle is ever handed out.
 */
DWORD WINAPI DsBindWithSpnExW(LPCWSTR controller, LPCWSTR domain, PVOID auth, LPCWSTR spn, DWORD flags, HANDLE *bound)
{
    if (!bound)
        return ERROR_INVALID_PARAMETER;
    *bound = nullptr;
    return controller && *controller ? RPC_S_SERVER_UNAVAILABLE_CODE : ERROR_NO_SUCH_DOMAIN;
}

DWORD WINAPI DsBindWithSpnW(LPCWSTR controller, LPCWSTR domain, PVOID auth, LPCWSTR spn, HANDLE *bound)
{
    return DsBindWithSpnExW(controller, domain, auth, spn, 0, bound);
}

DWORD WINAPI DsBindWithCredW(LPCWSTR controller, LPCWSTR domain, PVOID auth, HANDLE *bound)
{
    return DsBindWithSpnExW(controller, domain, auth, nullptr, 0, bound);
}

DWORD WINAPI DsBindW(LPCWSTR controller, LPCWSTR domain, HANDLE *bound)
{
    return DsBindWithSpnExW(controller, domain, nullptr, nullptr, 0, bound);
}

static DWORD bind_ansi(LPCSTR controller, LPCSTR domain, PVOID auth, LPCSTR spn, DWORD flags, HANDLE *bound)
{
    WCHAR c[MAX_PART + 1], d[MAX_PART + 1], s[MAX_PART + 1];
    if ((controller && !MultiByteToWideChar(CP_ACP, 0, controller, -1, c, RTL_NUMBER_OF(c))) ||
        (domain && !MultiByteToWideChar(CP_ACP, 0, domain, -1, d, RTL_NUMBER_OF(d))) ||
        (spn && !MultiByteToWideChar(CP_ACP, 0, spn, -1, s, RTL_NUMBER_OF(s))))
        return ERROR_INVALID_PARAMETER;
    return DsBindWithSpnExW(controller ? c : nullptr, domain ? d : nullptr, auth, spn ? s : nullptr, flags, bound);
}

DWORD WINAPI DsBindWithSpnExA(LPCSTR controller, LPCSTR domain, PVOID auth, LPCSTR spn, DWORD flags, HANDLE *bound)
{
    return bind_ansi(controller, domain, auth, spn, flags, bound);
}

DWORD WINAPI DsBindWithSpnA(LPCSTR controller, LPCSTR domain, PVOID auth, LPCSTR spn, HANDLE *bound)
{
    return bind_ansi(controller, domain, auth, spn, 0, bound);
}

DWORD WINAPI DsBindWithCredA(LPCSTR controller, LPCSTR domain, PVOID auth, HANDLE *bound)
{
    return bind_ansi(controller, domain, auth, nullptr, 0, bound);
}

DWORD WINAPI DsBindA(LPCSTR controller, LPCSTR domain, HANDLE *bound)
{
    return bind_ansi(controller, domain, nullptr, nullptr, 0, bound);
}

/* there is nothing bound to let go, and the handle is cleared as it is for a bound one. */
DWORD WINAPI DsUnBindW(HANDLE *bound)
{
    if (!bound)
        return ERROR_INVALID_PARAMETER;
    *bound = nullptr;
    return ERROR_SUCCESS;
}

DWORD WINAPI DsUnBindA(HANDLE *bound)
{
    return DsUnBindW(bound);
}

typedef struct DS_NAME_RESULT_ITEMW {
    DWORD status;
    LPWSTR domain;
    LPWSTR name;
} DS_NAME_RESULT_ITEMW;

typedef struct DS_NAME_RESULTW {
    DWORD count;
    DS_NAME_RESULT_ITEMW *items;
} DS_NAME_RESULTW;

/* names are cracked by a controller, and there is no bound handle that could reach one. */
DWORD WINAPI DsCrackNamesW(HANDLE bound, DWORD flags, DWORD offered, DWORD desired, DWORD count, LPCWSTR *names,
                           DS_NAME_RESULTW **result)
{
    if (result)
        *result = nullptr;
    if (!bound || !names || !result || !count)
        return ERROR_INVALID_PARAMETER;
    return ERROR_INVALID_HANDLE;
}

DWORD WINAPI DsCrackNamesA(HANDLE bound, DWORD flags, DWORD offered, DWORD desired, DWORD count, LPCSTR *names,
                           DS_NAME_RESULTW **result)
{
    if (result)
        *result = nullptr;
    if (!bound || !names || !result || !count)
        return ERROR_INVALID_PARAMETER;
    return ERROR_INVALID_HANDLE;
}

/* a result is one block for the items and one for each of their strings. */
void WINAPI DsFreeNameResultW(DS_NAME_RESULTW *result)
{
    if (!result)
        return;
    for (DWORD i = 0; result->items && i < result->count; i++) {
        LocalFree(result->items[i].domain);
        LocalFree(result->items[i].name);
    }
    LocalFree(result->items);
    LocalFree(result);
}

void WINAPI DsFreeNameResultA(DS_NAME_RESULTW *result)
{
    DsFreeNameResultW(result);
}

/* ------------------------------------------------------------------ service principal names */

enum { SPN_DNS_HOST, SPN_DN_HOST, SPN_NB_HOST, SPN_DOMAIN, SPN_NB_DOMAIN, SPN_SERVICE };

static LPWSTR copy_text(const WCHAR *text)
{
    SIZE_T bytes = (text_length(text) + 1) * sizeof(WCHAR);
    LPWSTR copy = LocalAlloc(LMEM_FIXED, bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

/*
 * the names a service goes by: for the host kinds one for each instance name, or the name of this machine when none is
 * given, with the service name after it when there is one. a domain kind gives the one name of the domain, and the
 * service kind needs the service name. the distinguished name of a host is held by a directory, which this machine
 * is not in.
 */
DWORD WINAPI DsGetSpnW(DWORD type, LPCWSTR service_class, LPCWSTR service_name, USHORT port, USHORT instance_count,
                       LPCWSTR *instances, const USHORT *ports, DWORD *count, LPWSTR **spns)
{
    if (count)
        *count = 0;
    if (spns)
        *spns = nullptr;
    if (!service_class || !count || !spns || type > SPN_SERVICE || (instance_count && !instances))
        return ERROR_INVALID_PARAMETER;
    if (type >= SPN_DOMAIN && !service_name)
        return ERROR_INVALID_PARAMETER;
    BOOL domain_kind = type == SPN_DOMAIN || type == SPN_NB_DOMAIN;
    WCHAR local[MAX_PART + 1];
    LPCWSTR local_list[1] = {local};
    DWORD n = instance_count;
    if (domain_kind) {
        instances = nullptr;
        n = 1;
    } else if (!n && type == SPN_SERVICE) {
        n = 1;
    } else if (!n) {
        if (type == SPN_DN_HOST)
            return ERROR_NO_SUCH_DOMAIN;
        DWORD size = RTL_NUMBER_OF(local);
        if (!GetComputerNameExW(type == SPN_NB_HOST ? ComputerNameNetBIOS : ComputerNameDnsFullyQualified, local, &size))
            return GetLastError();
        instances = local_list;
        n = 1;
    }
    LPWSTR *list = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, n * sizeof(LPWSTR));
    if (!list)
        return ERROR_NOT_ENOUGH_MEMORY;
    BOOL named = domain_kind || type == SPN_SERVICE || (service_name && *service_name);
    DWORD error = ERROR_SUCCESS;
    for (DWORD i = 0; i < n && !error; i++) {
        WCHAR text[MAX_PART * 3 + 16];
        DWORD length = RTL_NUMBER_OF(text);
        USHORT use_port = ports && instances ? ports[i] : port;
        LPCWSTR instance = instances ? instances[i] : nullptr;
        if (named)
            error = DsMakeSpnW(service_class, service_name, domain_kind ? nullptr : instance, use_port, nullptr, &length,
                               text);
        else
            error = DsMakeSpnW(service_class, instance, nullptr, use_port, nullptr, &length, text);
        if (!error && !(list[i] = copy_text(text)))
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (error) {
        for (DWORD i = 0; i < n; i++)
            LocalFree(list[i]);
        LocalFree(list);
        return error;
    }
    *count = n;
    *spns = list;
    return ERROR_SUCCESS;
}

void WINAPI DsFreeSpnArrayW(DWORD count, LPWSTR *spns)
{
    if (!spns)
        return;
    for (DWORD i = 0; i < count; i++)
        LocalFree(spns[i]);
    LocalFree(spns);
}

/* the names of the ansi form are the wide ones turned to the code page, in blocks of their own. */
DWORD WINAPI DsGetSpnA(DWORD type, LPCSTR service_class, LPCSTR service_name, USHORT port, USHORT instance_count,
                       LPCSTR *instances, const USHORT *ports, DWORD *count, LPSTR **spns)
{
    if (count)
        *count = 0;
    if (spns)
        *spns = nullptr;
    if (!service_class || !count || !spns || (instance_count && !instances))
        return ERROR_INVALID_PARAMETER;
    WCHAR wide_class[MAX_PART + 1], wide_name[MAX_PART + 1];
    if (!MultiByteToWideChar(CP_ACP, 0, service_class, -1, wide_class, RTL_NUMBER_OF(wide_class)) ||
        (service_name && !MultiByteToWideChar(CP_ACP, 0, service_name, -1, wide_name, RTL_NUMBER_OF(wide_name))))
        return ERROR_INVALID_PARAMETER;
    LPWSTR *wide_instances = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, (instance_count + 1) * sizeof(LPWSTR));
    if (!wide_instances)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD error = ERROR_SUCCESS;
    for (DWORD i = 0; i < instance_count && !error; i++) {
        wide_instances[i] = LocalAlloc(LMEM_FIXED, (MAX_PART + 1) * sizeof(WCHAR));
        if (!wide_instances[i] || !MultiByteToWideChar(CP_ACP, 0, instances[i], -1, wide_instances[i], MAX_PART + 1))
            error = ERROR_INVALID_PARAMETER;
    }
    DWORD wide_count = 0;
    LPWSTR *wide_spns = nullptr;
    if (!error)
        error = DsGetSpnW(type, wide_class, service_name ? wide_name : nullptr, port, instance_count,
                          (LPCWSTR *)wide_instances, ports, &wide_count, &wide_spns);
    LPSTR *list = nullptr;
    if (!error && !(list = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, wide_count * sizeof(LPSTR))))
        error = ERROR_NOT_ENOUGH_MEMORY;
    for (DWORD i = 0; !error && i < wide_count; i++) {
        int bytes = WideCharToMultiByte(CP_ACP, 0, wide_spns[i], -1, nullptr, 0, nullptr, nullptr);
        list[i] = LocalAlloc(LMEM_FIXED, bytes);
        if (!list[i])
            error = ERROR_NOT_ENOUGH_MEMORY;
        else
            WideCharToMultiByte(CP_ACP, 0, wide_spns[i], -1, list[i], bytes, nullptr, nullptr);
    }
    for (DWORD i = 0; i < instance_count; i++)
        LocalFree(wide_instances[i]);
    LocalFree(wide_instances);
    DsFreeSpnArrayW(wide_count, wide_spns);
    if (error) {
        for (DWORD i = 0; list && i < wide_count; i++)
            LocalFree(list[i]);
        LocalFree(list);
        return error;
    }
    *count = wide_count;
    *spns = list;
    return ERROR_SUCCESS;
}

void WINAPI DsFreeSpnArrayA(DWORD count, LPSTR *spns)
{
    if (!spns)
        return;
    for (DWORD i = 0; i < count; i++)
        LocalFree(spns[i]);
    LocalFree(spns);
}

/* the names are written to the account object of a directory, and a handle to one is needed. */
DWORD WINAPI DsWriteAccountSpnW(HANDLE bound, DWORD operation, LPCWSTR account, DWORD count, LPCWSTR *spns)
{
    if (!bound || operation > 2 || !account || (count && !spns))
        return ERROR_INVALID_PARAMETER;
    return ERROR_INVALID_HANDLE;
}

DWORD WINAPI DsWriteAccountSpnA(HANDLE bound, DWORD operation, LPCSTR account, DWORD count, LPCSTR *spns)
{
    if (!bound || operation > 2 || !account || (count && !spns))
        return ERROR_INVALID_PARAMETER;
    return ERROR_INVALID_HANDLE;
}

/* registering the name of a server needs the object of its account in a directory, which a workgroup lacks. */
DWORD WINAPI DsServerRegisterSpnW(DWORD operation, LPCWSTR service_class, LPCWSTR account)
{
    if (operation > 2 || !service_class || !*service_class)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NO_SUCH_DOMAIN;
}

DWORD WINAPI DsServerRegisterSpnA(DWORD operation, LPCSTR service_class, LPCSTR account)
{
    if (operation > 2 || !service_class || !*service_class)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NO_SUCH_DOMAIN;
}

/* the name of a service on a server a client reaches, as the class and the name of the server. */
DWORD WINAPI DsClientMakeSpnForTargetServerW(LPCWSTR service_class, LPCWSTR service_name, DWORD *length, LPWSTR spn)
{
    return DsMakeSpnW(service_class, service_name, nullptr, 0, nullptr, length, spn);
}

DWORD WINAPI DsClientMakeSpnForTargetServerA(LPCSTR service_class, LPCSTR service_name, DWORD *length, LPSTR spn)
{
    return DsMakeSpnA(service_class, service_name, nullptr, 0, nullptr, length, spn);
}
