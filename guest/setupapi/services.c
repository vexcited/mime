/*
 * installing services from an inf: the AddService and DelService lines of a section, with the service install
 * sections they name. deletes run before adds, as the section lists them.
 */
#include "setup_private.h"
#include <mime/win32/winsvc.h>

WINADVAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(PCWSTR text, DWORD revision,
                                                                           PSECURITY_DESCRIPTOR *descriptor, PULONG size);

#define ERROR_BAD_SERVICE_INSTALLSECT 0xE0000217
#define ERROR_NO_SERVICE_NAME 0xE0000219

#define SPSVCINST_ASSOCSERVICE 0x00000002
#define SPSVCINST_DELETEEVENTLOGENTRY 0x00000004
#define SPSVCINST_NOCLOBBER_DISPLAYNAME 0x00000008
#define SPSVCINST_NOCLOBBER_STARTTYPE 0x00000010
#define SPSVCINST_NOCLOBBER_ERRORCONTROL 0x00000020
#define SPSVCINST_NOCLOBBER_LOADORDERGROUP 0x00000040
#define SPSVCINST_NOCLOBBER_DEPENDENCIES 0x00000080
#define SPSVCINST_NOCLOBBER_DESCRIPTION 0x00000100
#define SPSVCINST_STOPSERVICE 0x00000200
#define SPSVCINST_CLOBBER_SECURITY 0x00000400

#define SERVICES_KEY L"System\\CurrentControlSet\\Services"
#define STOP_WAIT_MS 30000
#define DACL_SECURITY_INFORMATION 0x00000004

/* appends text, cut to fit. */
static void append(PWSTR path, SIZE_T chars, PCWSTR text)
{
    SIZE_T n = wcslen(path);
    if (n + 1 < chars)
        SetupCopyText(path + n, text, chars - n);
}

static void log_key(PWSTR path, SIZE_T chars, PCWSTR type, PCWSTR name)
{
    SetupCopyText(path, SERVICES_KEY L"\\EventLog\\", chars);
    append(path, chars, type);
    append(path, chars, L"\\");
    append(path, chars, name);
}

/* the field of a line as text, or null. */
static PWSTR text_field(PINFCONTEXT line, DWORD field)
{
    DWORD need = 0;
    if (!SetupGetStringFieldW(line, field, nullptr, 0, &need) || need <= 1)
        return nullptr;
    PWSTR text = SetupAlloc(need * sizeof(WCHAR));
    if (text && !SetupGetStringFieldW(line, field, text, need, nullptr)) {
        SetupFree(text);
        text = nullptr;
    }
    return text;
}

/* the first line of a key in the section, with this machine's decoration tried first. */
static BOOL key_line(HINF inf, PCWSTR section, PCWSTR key, INFCONTEXT *line)
{
    WCHAR decorated[256];
    INF_FILE *head = SetupInfOf(inf);
    if (head && SetupDecoratedSection(head, section, decorated, RTL_NUMBER_OF(decorated)) &&
        SetupFindFirstLineW(inf, decorated, key, line))
        return TRUE;
    return SetupFindFirstLineW(inf, section, key, line);
}

static BOOL int_key(HINF inf, PCWSTR section, PCWSTR key, DWORD *value)
{
    INFCONTEXT line;
    INT number;
    if (!key_line(inf, section, key, &line) || !SetupGetIntField(&line, 1, &number))
        return FALSE;
    *value = (DWORD)number;
    return TRUE;
}

static PWSTR text_key(HINF inf, PCWSTR section, PCWSTR key)
{
    INFCONTEXT line;
    return key_line(inf, section, key, &line) ? text_field(&line, 1) : nullptr;
}

/* the names of every Dependencies line as a multi string. */
static PWSTR dependencies_of(HINF inf, PCWSTR section)
{
    INFCONTEXT line;
    if (!key_line(inf, section, L"Dependencies", &line))
        return nullptr;
    SIZE_T chars = 1, capacity = 256;
    PWSTR list = SetupAlloc(capacity * sizeof(WCHAR));
    if (!list)
        return nullptr;
    for (;;) {
        DWORD count = SetupGetFieldCount(&line);
        for (DWORD i = 1; i <= count; i++) {
            PWSTR name = text_field(&line, i);
            if (!name)
                continue;
            SIZE_T length = wcslen(name) + 1;
            if (chars + length + 1 > capacity) {
                capacity = (chars + length + 1) * 2;
                PWSTR bigger = SetupAlloc(capacity * sizeof(WCHAR));
                if (!bigger) {
                    SetupFree(name);
                    SetupFree(list);
                    return nullptr;
                }
                memcpy(bigger, list, chars * sizeof(WCHAR));
                SetupFree(list);
                list = bigger;
            }
            memcpy(list + chars - 1, name, length * sizeof(WCHAR));
            chars += length;
            list[chars - 1] = 0;
            SetupFree(name);
        }
        INFCONTEXT next;
        if (!SetupFindNextMatchLineW(&line, L"Dependencies", &next))
            break;
        line = next;
    }
    if (chars == 1) {
        SetupFree(list);
        return nullptr;
    }
    return list;
}

/* the service control manager, opened once for each call. */
static SC_HANDLE open_manager(DWORD access)
{
    return OpenSCManagerW(nullptr, nullptr, access);
}

static BOOL stop_service(SC_HANDLE service)
{
    SERVICE_STATUS status;
    if (!QueryServiceStatus(service, &status))
        return FALSE;
    if (status.dwCurrentState == SERVICE_STOPPED)
        return TRUE;
    if (status.dwCurrentState != SERVICE_STOP_PENDING && !ControlService(service, SERVICE_CONTROL_STOP, &status))
        return GetLastError() == ERROR_SERVICE_NOT_ACTIVE;
    for (DWORD waited = 0; waited < STOP_WAIT_MS; waited += 250) {
        if (!QueryServiceStatus(service, &status))
            return FALSE;
        if (status.dwCurrentState == SERVICE_STOPPED)
            return TRUE;
        Sleep(250);
    }
    SetLastError(ERROR_SERVICE_REQUEST_TIMEOUT);
    return FALSE;
}

/* runs the AddReg and DelReg lines of a section against a key. */
static BOOL registry_of(HINF inf, PCWSTR section, HKEY key)
{
    return SetupInstallFromInfSectionW(nullptr, inf, section, SPINST_REGISTRY, key, nullptr, 0, nullptr, nullptr, nullptr,
                                       nullptr);
}

static DWORD event_log(HINF inf, INFCONTEXT *line, PCWSTR service)
{
    PWSTR section = text_field(line, 4);
    if (!section)
        return 0;
    PWSTR type = text_field(line, 5), name = text_field(line, 6);
    WCHAR path[512];
    log_key(path, RTL_NUMBER_OF(path), type ? type : L"System", name ? name : service);
    HKEY key;
    DWORD error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr);
    if (!error) {
        if (!registry_of(inf, section, key))
            error = GetLastError();
        RegCloseKey(key);
    }
    SetupFree(section);
    SetupFree(type);
    SetupFree(name);
    return error;
}

/* the image path of a driver is kept under the system root, as the manager reads it. */
static PWSTR binary_path(HINF inf, PCWSTR binary, DWORD type)
{
    INF_FILE *head = SetupInfOf(inf);
    WCHAR expanded[MAX_PATH * 2];
    if (head)
        SetupExpandDirectories(head, binary, expanded, RTL_NUMBER_OF(expanded));
    else
        SetupCopyText(expanded, binary, RTL_NUMBER_OF(expanded));
    if (type & (SERVICE_KERNEL_DRIVER | SERVICE_FILE_SYSTEM_DRIVER)) {
        WCHAR root[MAX_PATH];
        UINT n = GetSystemWindowsDirectoryW(root, RTL_NUMBER_OF(root));
        if (n && n < RTL_NUMBER_OF(root) && !_wcsnicmp(expanded, root, n) && expanded[n] == '\\') {
            WCHAR rebased[MAX_PATH * 2] = L"\\SystemRoot";
            wcscat(rebased, expanded + n);
            return SetupDuplicate(rebased);
        }
    }
    return SetupDuplicate(expanded);
}

static DWORD add_service(HINF inf, INFCONTEXT *line, DWORD call_flags, DWORD *associate)
{
    INT field_flags = 0;
    SetupGetIntField(line, 2, &field_flags);
    DWORD flags = call_flags | (DWORD)field_flags;
    PWSTR name = text_field(line, 1);
    if (!name) {
        if (flags & SPSVCINST_ASSOCSERVICE)
            *associate = 1;
        return flags & SPSVCINST_ASSOCSERVICE ? 0 : ERROR_NO_SERVICE_NAME;
    }
    PWSTR section = text_field(line, 3);
    DWORD error = 0;
    if (!section) {
        SetupFree(name);
        return ERROR_BAD_SERVICE_INSTALLSECT;
    }
    DWORD type, start, control;
    if (!int_key(inf, section, L"ServiceType", &type) || !int_key(inf, section, L"StartType", &start) ||
        !int_key(inf, section, L"ErrorControl", &control)) {
        SetupFree(name);
        SetupFree(section);
        return ERROR_BAD_SERVICE_INSTALLSECT;
    }
    PWSTR binary = text_key(inf, section, L"ServiceBinary");
    PWSTR image = binary ? binary_path(inf, binary, type) : nullptr;
    PWSTR display = text_key(inf, section, L"DisplayName");
    PWSTR description = text_key(inf, section, L"Description");
    PWSTR account = text_key(inf, section, L"StartName");
    PWSTR group = text_key(inf, section, L"LoadOrderGroup");
    PWSTR dependencies = dependencies_of(inf, section);
    PWSTR security = text_key(inf, section, L"Security");
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SC_HANDLE manager = nullptr, service = nullptr;
    BOOL created = FALSE;

    if (security &&
        !ConvertStringSecurityDescriptorToSecurityDescriptorW(security, 1, &descriptor, nullptr)) {
        error = GetLastError();
        goto done;
    }
    manager = open_manager(SC_MANAGER_ALL_ACCESS);
    if (!manager) {
        error = GetLastError();
        goto done;
    }
    service = OpenServiceW(manager, name, SERVICE_ALL_ACCESS);
    if (service) {
        /* a service that exists keeps what the flags protect. */
        if (flags & SPSVCINST_STOPSERVICE)
            stop_service(service);
        if (!ChangeServiceConfigW(service, type,
                                  (flags & SPSVCINST_NOCLOBBER_STARTTYPE) ? SERVICE_NO_CHANGE : start,
                                  (flags & SPSVCINST_NOCLOBBER_ERRORCONTROL) ? SERVICE_NO_CHANGE : control, image,
                                  (flags & SPSVCINST_NOCLOBBER_LOADORDERGROUP) ? nullptr : group, nullptr,
                                  (flags & SPSVCINST_NOCLOBBER_DEPENDENCIES) ? nullptr : dependencies, account, nullptr,
                                  (flags & SPSVCINST_NOCLOBBER_DISPLAYNAME) ? nullptr : display)) {
            error = GetLastError();
            goto done;
        }
        if (descriptor && (flags & SPSVCINST_CLOBBER_SECURITY))
            SetServiceObjectSecurity(service, DACL_SECURITY_INFORMATION, descriptor);
    } else if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
        if (!image) {
            error = ERROR_BAD_SERVICE_INSTALLSECT;
            goto done;
        }
        service = CreateServiceW(manager, name, display ? display : name, SERVICE_ALL_ACCESS, type, start, control, image,
                                 group, nullptr, dependencies, account, nullptr);
        if (!service) {
            error = GetLastError();
            goto done;
        }
        if (descriptor)
            SetServiceObjectSecurity(service, DACL_SECURITY_INFORMATION, descriptor);
    } else {
        error = GetLastError();
        goto done;
    }
    if (description && (created || !(flags & SPSVCINST_NOCLOBBER_DESCRIPTION))) {
        SERVICE_DESCRIPTIONW info = {description};
        ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &info);
    }
    {
        HKEY key;
        WCHAR path[300];
        SetupCopyText(path, SERVICES_KEY L"\\", RTL_NUMBER_OF(path));
        append(path, RTL_NUMBER_OF(path), name);
        error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_ALL_ACCESS, &key);
        if (!error) {
            if (!registry_of(inf, section, key))
                error = GetLastError();
            RegCloseKey(key);
        }
    }
    if (!error)
        error = event_log(inf, line, name);
    if (!error && (flags & SPSVCINST_ASSOCSERVICE))
        *associate = 1;
done:
    if (service)
        CloseServiceHandle(service);
    if (manager)
        CloseServiceHandle(manager);
    if (descriptor)
        LocalFree(descriptor);
    SetupFree(name);
    SetupFree(section);
    SetupFree(binary);
    SetupFree(image);
    SetupFree(display);
    SetupFree(description);
    SetupFree(account);
    SetupFree(group);
    SetupFree(dependencies);
    SetupFree(security);
    return error;
}

static DWORD delete_service(INFCONTEXT *line)
{
    INT flags = 0;
    SetupGetIntField(line, 2, &flags);
    PWSTR name = text_field(line, 1);
    if (!name)
        return ERROR_NO_SERVICE_NAME;
    DWORD error = 0;
    SC_HANDLE manager = open_manager(SC_MANAGER_CONNECT);
    if (!manager) {
        error = GetLastError();
    } else {
        SC_HANDLE service = OpenServiceW(manager, name, SERVICE_ALL_ACCESS);
        if (!service) {
            error = GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST ? 0 : GetLastError();
        } else {
            if (flags & SPSVCINST_STOPSERVICE)
                stop_service(service);
            if (!DeleteService(service) && GetLastError() != ERROR_SERVICE_MARKED_FOR_DELETE)
                error = GetLastError();
            CloseServiceHandle(service);
        }
        CloseServiceHandle(manager);
    }
    if (!error && (flags & SPSVCINST_DELETEEVENTLOGENTRY)) {
        PWSTR type = text_field(line, 3), log = text_field(line, 4);
        WCHAR path[512];
        log_key(path, RTL_NUMBER_OF(path), type ? type : L"System", log ? log : name);
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, path);
        SetupFree(type);
        SetupFree(log);
    }
    SetupFree(name);
    return error;
}

SETUPAPI BOOL WINAPI SetupInstallServicesFromInfSectionExW(HINF inf, PCWSTR section, DWORD flags, HDEVINFO devices,
                                                           PSP_DEVINFO_DATA device, PVOID reserved1, PVOID reserved2)
{
    if (flags & 0xFFFFFD00) {
        SetLastError(ERROR_INVALID_FLAGS);
        return FALSE;
    }
    BOOL has_device = devices != INVALID_HANDLE_VALUE && devices;
    if (((flags & SPSVCINST_ASSOCSERVICE) && (!has_device || !device)) || reserved1 || reserved2 || !section) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    INFCONTEXT line;
    if (!SetupInfOf(inf))
        return FALSE;
    WCHAR decorated[256];
    PCWSTR actual = section;
    if (SetupDecoratedSection(SetupInfOf(inf), section, decorated, RTL_NUMBER_OF(decorated)))
        actual = decorated;
    if (!SetupFindFirstLineW(inf, actual, nullptr, &line)) {
        SetLastError(ERROR_SECTION_NOT_FOUND);
        return FALSE;
    }
    /* a section that adds several services cannot associate one of them. */
    if ((flags & SPSVCINST_ASSOCSERVICE) && SetupFindFirstLineW(inf, actual, L"AddService", &line) &&
        SetupFindNextMatchLineW(&line, L"AddService", &line))
        flags &= ~(DWORD)SPSVCINST_ASSOCSERVICE;

    for (int pass = 0; pass < 2; pass++) {
        PCWSTR key = pass ? L"AddService" : L"DelService";
        if (!SetupFindFirstLineW(inf, actual, key, &line))
            continue;
        for (;;) {
            DWORD associate = 0;
            DWORD error = pass ? add_service(inf, &line, flags, &associate) : delete_service(&line);
            if (error) {
                SetLastError(error);
                return FALSE;
            }
            if (associate && has_device && device) {
                PWSTR name = text_field(&line, 1);
                if (name) {
                    SetupDiSetDeviceRegistryPropertyW(devices, device, SPDRP_SERVICE, (const BYTE *)name,
                                                      (DWORD)((wcslen(name) + 1) * sizeof(WCHAR)));
                    SetupFree(name);
                }
            }
            if (!SetupFindNextMatchLineW(&line, key, &line))
                break;
        }
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupInstallServicesFromInfSectionW(HINF inf, PCWSTR section, DWORD flags)
{
    return SetupInstallServicesFromInfSectionExW(inf, section, flags, INVALID_HANDLE_VALUE, nullptr, nullptr, nullptr);
}

SETUPAPI BOOL WINAPI SetupInstallServicesFromInfSectionExA(HINF inf, PCSTR section, DWORD flags, HDEVINFO devices,
                                                           PSP_DEVINFO_DATA device, PVOID reserved1, PVOID reserved2)
{
    PWSTR wide = section ? SetupToWide(section) : nullptr;
    if (section && !wide)
        return FALSE;
    BOOL ok = SetupInstallServicesFromInfSectionExW(inf, wide, flags, devices, device, reserved1, reserved2);
    DWORD error = GetLastError();
    SetupFree(wide);
    SetLastError(error);
    return ok;
}

SETUPAPI BOOL WINAPI SetupInstallServicesFromInfSectionA(HINF inf, PCSTR section, DWORD flags)
{
    return SetupInstallServicesFromInfSectionExA(inf, section, flags, INVALID_HANDLE_VALUE, nullptr, nullptr, nullptr);
}
