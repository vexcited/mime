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
