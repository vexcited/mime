#include "proc.h"
#include <mime/win32/winuser.h>
#include <mime/win32/winsvc.h>

#define PROCESS_QUERY_LIMITED_INFORMATION_ 0x1000
#define TOKEN_QUERY_ 0x0008

WINBASEAPI BOOL WINAPI OpenProcessToken(HANDLE process, DWORD access, PHANDLE token);
WINBASEAPI BOOL WINAPI GetTokenInformation(HANDLE token, int klass, LPVOID info, DWORD length, PDWORD returned);
WINBASEAPI BOOL WINAPI LookupAccountSidW(LPCWSTR system, PSID sid, LPWSTR name, LPDWORD name_size, LPWSTR domain,
                                         LPDWORD domain_size, int *use);
WINUSERAPI BOOL WINAPI IsHungAppWindow(HWND window);
WINBASEAPI SC_HANDLE WINAPI OpenSCManagerW(LPCWSTR machine, LPCWSTR database, DWORD access);

typedef struct TOKEN_USER_ {
    PSID Sid;
    DWORD Attributes;
} TOKEN_USER_;

const WCHAR *proc_session_name(DWORD session)
{
    return session == 0 ? L"Services" : L"Console";
}

BOOL proc_list_load(PROC_LIST *list)
{
    memset(list, 0, sizeof(*list));
    ULONG size = 1 << 18;
    BYTE *buffer = nullptr;
    NTSTATUS status;
    for (;;) {
        buffer = tu_realloc(buffer, size);
        ULONG needed = 0;
        status = NtQuerySystemInformation(SystemProcessInformation, buffer, size, &needed);
        if (status >= 0)
            break;
        if (status != (NTSTATUS)0xC0000004 || size > (1u << 28)) {
            tu_free(buffer);
            return FALSE;
        }
        size = needed > size ? needed + 16384 : size * 2;
    }
    DWORD count = 0;
    for (BYTE *p = buffer;; ) {
        SYSTEM_PROCESS_INFORMATION *i = (SYSTEM_PROCESS_INFORMATION *)p;
        count++;
        if (!i->NextEntryOffset)
            break;
        p += i->NextEntryOffset;
    }
    list->Items = tu_alloc(count * sizeof(PROC_ENTRY));
    for (BYTE *p = buffer;; ) {
        SYSTEM_PROCESS_INFORMATION *i = (SYSTEM_PROCESS_INFORMATION *)p;
        PROC_ENTRY *e = &list->Items[list->Count++];
        e->Pid = (DWORD)(ULONG_PTR)i->UniqueProcessId;
        e->Parent = (DWORD)(ULONG_PTR)i->InheritedFromUniqueProcessId;
        e->Session = i->SessionId;
        e->Threads = i->NumberOfThreads;
        if (e->Pid == 0)
            e->Name = tu_dup(L"System Idle Process");
        else if (i->ImageName.Buffer && i->ImageName.Length)
            e->Name = tu_dup_n(i->ImageName.Buffer, i->ImageName.Length / sizeof(WCHAR));
        else
            e->Name = tu_dup(L"System");
        e->MemoryKb = i->WorkingSetSize / 1024;
        e->CpuSeconds = ((ULONGLONG)i->UserTime.QuadPart + (ULONGLONG)i->KernelTime.QuadPart) / 10000000ull;
        if (!i->NextEntryOffset)
            break;
        p += i->NextEntryOffset;
    }
    tu_free(buffer);
    return TRUE;
}

void proc_list_free(PROC_LIST *list)
{
    for (DWORD i = 0; i < list->Count; i++) {
        tu_free(list->Items[i].Name);
        tu_free(list->Items[i].User);
        tu_free(list->Items[i].Title);
        tu_free(list->Items[i].Services);
    }
    tu_free(list->Items);
    memset(list, 0, sizeof(*list));
}

PROC_ENTRY *proc_find(PROC_LIST *list, DWORD pid)
{
    for (DWORD i = 0; i < list->Count; i++)
        if (list->Items[i].Pid == pid)
            return &list->Items[i];
    return nullptr;
}

#define PROCESS_QUERY_INFORMATION_ 0x0400
#define PROCESS_VM_READ_ 0x0010
#define LIST_MODULES_ALL_ 3

WINBASEAPI BOOL WINAPI K32EnumProcessModulesEx(HANDLE process, HMODULE *modules, DWORD size, LPDWORD needed, DWORD filter);
WINBASEAPI DWORD WINAPI K32GetModuleBaseNameW(HANDLE process, HMODULE module, LPWSTR name, DWORD size);

/* the names of the modules of a process, separated by commas, or null when they cannot be read. */
WCHAR *proc_modules(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION_ | PROCESS_VM_READ_, FALSE, pid);
    if (!process)
        return nullptr;
    DWORD needed = 0;
    HMODULE modules[1024];
    WCHAR *result = nullptr;
    if (K32EnumProcessModulesEx(process, modules, sizeof(modules), &needed, LIST_MODULES_ALL_)) {
        DWORD count = needed / sizeof(HMODULE);
        if (count > TU_ARRAY(modules))
            count = TU_ARRAY(modules);
        TU_STR text = {};
        for (DWORD i = 0; i < count; i++) {
            WCHAR name[260];
            if (!K32GetModuleBaseNameW(process, modules[i], name, TU_ARRAY(name)))
                continue;
            if (text.length)
                tu_str_putc(&text, L',');
            tu_str_puts(&text, name);
        }
        result = tu_dup(tu_str_text(&text));
        tu_str_free(&text);
    }
    CloseHandle(process);
    return result;
}

BOOL proc_list_matches(const WCHAR *list, const WCHAR *pattern)
{
    BOOL found = FALSE;
    WCHAR *copy = tu_dup(list);
    for (WCHAR *s = copy; s;) {
        WCHAR *comma = wcschr(s, L',');
        if (comma)
            *comma = 0;
        if (tu_wild(pattern, s))
            found = TRUE;
        s = comma ? comma + 1 : nullptr;
    }
    tu_free(copy);
    return found;
}

/* ------------------------------------------------------------------ users */

static WCHAR *user_of_process(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION_, FALSE, pid);
    if (!process)
        return nullptr;
    WCHAR *result = nullptr;
    HANDLE token;
    if (OpenProcessToken(process, TOKEN_QUERY_, &token)) {
        DWORD size = 0;
        GetTokenInformation(token, 1, nullptr, 0, &size);
        if (size) {
            BYTE *info = tu_alloc(size);
            if (GetTokenInformation(token, 1, info, size, &size)) {
                WCHAR name[256], domain[256];
                DWORD name_size = TU_ARRAY(name), domain_size = TU_ARRAY(domain);
                int use;
                if (LookupAccountSidW(nullptr, ((TOKEN_USER_ *)info)->Sid, name, &name_size, domain, &domain_size,
                                      &use)) {
                    TU_STR text = {};
                    if (domain[0])
                        tu_str_format(&text, L"%s\\%s", domain, name);
                    else
                        tu_str_puts(&text, name);
                    result = tu_dup(tu_str_text(&text));
                    tu_str_free(&text);
                }
            }
            tu_free(info);
        }
        CloseHandle(token);
    }
    CloseHandle(process);
    return result;
}

void proc_load_users(PROC_LIST *list)
{
    if (list->UsersLoaded)
        return;
    list->UsersLoaded = TRUE;
    for (DWORD i = 0; i < list->Count; i++) {
        PROC_ENTRY *e = &list->Items[i];
        if (e->Pid == 0 || e->Pid == 4) {
            e->User = tu_dup(L"NT AUTHORITY\\SYSTEM");
            continue;
        }
        e->User = user_of_process(e->Pid);
    }
}

/* ------------------------------------------------------------------ windows */

static BOOL CALLBACK window_callback(HWND window, LPARAM parameter)
{
    PROC_LIST *list = (PROC_LIST *)parameter;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    PROC_ENTRY *e = proc_find(list, pid);
    if (!e || !IsWindowVisible(window))
        return TRUE;
    if (GetWindow(window, GW_OWNER))
        return TRUE;
    int length = GetWindowTextLengthW(window);
    if (length <= 0)
        return TRUE;
    if (e->Title)
        return TRUE;
    WCHAR *title = tu_alloc((length + 2) * sizeof(WCHAR));
    GetWindowTextW(window, title, length + 1);
    e->Title = title;
    e->HasWindow = TRUE;
    e->Hung = IsHungAppWindow(window);
    return TRUE;
}

void proc_load_windows(PROC_LIST *list)
{
    if (list->WindowsLoaded)
        return;
    list->WindowsLoaded = TRUE;
    EnumWindows(window_callback, (LPARAM)list);
}

/* ------------------------------------------------------------------ services */

void proc_load_services(PROC_LIST *list)
{
    if (list->ServicesLoaded)
        return;
    list->ServicesLoaded = TRUE;
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, 4);
    if (!manager)
        return;
    DWORD size = 1 << 16, resume = 0;
    BYTE *buffer = tu_alloc(size);
    for (;;) {
        DWORD needed = 0, returned = 0;
        BOOL ok = EnumServicesStatusExW(manager, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, buffer, size,
                                        &needed, &returned, &resume, nullptr);
        DWORD error = ok ? 0 : GetLastError();
        if (error && error != ERROR_MORE_DATA)
            break;
        ENUM_SERVICE_STATUS_PROCESSW *s = (ENUM_SERVICE_STATUS_PROCESSW *)buffer;
        for (DWORD i = 0; i < returned; i++) {
            PROC_ENTRY *e = proc_find(list, s[i].ServiceStatusProcess.dwProcessId);
            if (!e || !s[i].ServiceStatusProcess.dwProcessId)
                continue;
            TU_STR text = {};
            if (e->Services)
                tu_str_format(&text, L"%s,", e->Services);
            tu_str_puts(&text, s[i].lpServiceName);
            tu_free(e->Services);
            e->Services = tu_dup(tu_str_text(&text));
            tu_str_free(&text);
        }
        if (error != ERROR_MORE_DATA)
            break;
        if (!returned) {
            size = needed + 1024;
            buffer = tu_realloc(buffer, size);
        }
    }
    tu_free(buffer);
    CloseServiceHandle(manager);
}

/* ------------------------------------------------------------------ filters */

static const struct {
    const WCHAR *Name;
    int Field;
    BOOL Numeric;
} kFields[] = {
    {L"status", FILTER_STATUS, FALSE},       {L"imagename", FILTER_IMAGENAME, FALSE},
    {L"pid", FILTER_PID, TRUE},              {L"session", FILTER_SESSION, TRUE},
    {L"sessionname", FILTER_SESSIONNAME, FALSE}, {L"cputime", FILTER_CPUTIME, TRUE},
    {L"memusage", FILTER_MEMUSAGE, TRUE},    {L"username", FILTER_USERNAME, FALSE},
    {L"services", FILTER_SERVICES, FALSE},   {L"windowtitle", FILTER_WINDOWTITLE, FALSE},
    {L"modules", FILTER_MODULES, FALSE},
};

static const WCHAR *const kOps[] = {L"eq", L"ne", L"gt", L"lt", L"ge", L"le"};

BOOL proc_name_match(const WCHAR *pattern, const WCHAR *name)
{
    return tu_wild(pattern, name);
}

const WCHAR *proc_status_name(const PROC_ENTRY *process)
{
    if (process->Hung)
        return L"Not Responding";
    return process->HasWindow ? L"Running" : L"Unknown";
}

/* hh:mm:ss to seconds. */
static BOOL parse_cpu_time(const WCHAR *text, ULONGLONG *seconds)
{
    ULONGLONG parts[3] = {};
    int n = 0;
    BOOL digits = FALSE;
    for (;; text++) {
        if (tu_is_digit(*text)) {
            parts[n] = parts[n] * 10 + (*text - L'0');
            digits = TRUE;
        } else if ((*text == L':' || !*text) && digits && n < 3) {
            n++;
            digits = FALSE;
            if (!*text)
                break;
        } else {
            return FALSE;
        }
    }
    if (n != 3 || parts[1] > 59 || parts[2] > 59)
        return FALSE;
    *seconds = parts[0] * 3600 + parts[1] * 60 + parts[2];
    return TRUE;
}

BOOL proc_filter_add(PROC_FILTERS *filters, const WCHAR *text, TU_STR *error)
{
    while (tu_is_space(*text))
        text++;
    const WCHAR *end = text;
    while (*end && !tu_is_space(*end))
        end++;
    SIZE_T name_length = (SIZE_T)(end - text);
    int index = -1;
    for (int i = 0; i < (int)TU_ARRAY(kFields); i++)
        if (wcslen(kFields[i].Name) == name_length && tu_eq_n(text, kFields[i].Name, name_length))
            index = i;
    if (index < 0) {
        WCHAR *bad = tu_dup_n(text, name_length);
        tu_str_format(error, L"Invalid argument/option - '%s'.\n", bad);
        tu_free(bad);
        return FALSE;
    }
    while (tu_is_space(*end))
        end++;
    const WCHAR *op_start = end;
    while (*end && !tu_is_space(*end))
        end++;
    SIZE_T op_length = (SIZE_T)(end - op_start);
    int op = -1;
    for (int i = 0; i < 6; i++)
        if (op_length == 2 && tu_eq_n(op_start, kOps[i], 2))
            op = i;
    if (op < 0) {
        WCHAR *bad = tu_dup_n(op_start, op_length);
        tu_str_format(error, L"Invalid syntax. '%s' operator is not allowed for the '%s' filter.\n", bad,
                      kFields[index].Name);
        tu_free(bad);
        return FALSE;
    }
    if (!kFields[index].Numeric && op > OP_NE) {
        tu_str_format(error, L"Invalid syntax. '%s' operator is not allowed for the '%s' filter.\n", kOps[op],
                      kFields[index].Name);
        return FALSE;
    }
    while (tu_is_space(*end))
        end++;
    SIZE_T value_length = wcslen(end);
    while (value_length && tu_is_space(end[value_length - 1]))
        value_length--;
    if (!value_length) {
        tu_str_format(error, L"Invalid syntax. Value expected for '%s'.\n", kFields[index].Name);
        return FALSE;
    }
    WCHAR *value = tu_dup_n(end, value_length);
    ULONGLONG number = 0;
    int field = kFields[index].Field;
    if (field == FILTER_PID || field == FILTER_SESSION || field == FILTER_MEMUSAGE) {
        DWORD v;
        if (!tu_parse_u32(value, &v)) {
            tu_str_format(error, L"Invalid syntax. Specify valid numeric value for '%s'.\n", kFields[index].Name);
            tu_free(value);
            return FALSE;
        }
        number = v;
    } else if (field == FILTER_CPUTIME) {
        if (!parse_cpu_time(value, &number)) {
            tu_str_format(error, L"Invalid syntax. Specify the CPU time as hh:mm:ss for '%s'.\n", kFields[index].Name);
            tu_free(value);
            return FALSE;
        }
    } else if (field == FILTER_STATUS) {
        if (!tu_eq(value, L"running") && !tu_eq(value, L"not responding") && !tu_eq(value, L"unknown") &&
            !tu_eq(value, L"suspended")) {
            tu_str_format(error, L"Invalid syntax. '%s' value is not allowed for '%s' option.\n", value,
                          kFields[index].Name);
            tu_free(value);
            return FALSE;
        }
    }
    filters->Items = tu_realloc(filters->Items, (filters->Count + 1) * sizeof(PROC_FILTER));
    PROC_FILTER *f = &filters->Items[filters->Count++];
    f->Field = field;
    f->Op = op;
    f->Value = value;
    f->Number = number;
    return TRUE;
}

BOOL proc_filter_needs(const PROC_FILTERS *filters, int field)
{
    for (int i = 0; i < filters->Count; i++)
        if (filters->Items[i].Field == field)
            return TRUE;
    return FALSE;
}

static BOOL compare_number(int op, ULONGLONG a, ULONGLONG b)
{
    switch (op) {
    case OP_EQ: return a == b;
    case OP_NE: return a != b;
    case OP_GT: return a > b;
    case OP_LT: return a < b;
    case OP_GE: return a >= b;
    default: return a <= b;
    }
}

BOOL proc_filter_match(const PROC_FILTERS *filters, PROC_ENTRY *p)
{
    for (int i = 0; i < filters->Count; i++) {
        const PROC_FILTER *f = &filters->Items[i];
        BOOL match;
        switch (f->Field) {
        case FILTER_PID:
            match = compare_number(f->Op, p->Pid, f->Number);
            break;
        case FILTER_SESSION:
            match = compare_number(f->Op, p->Session, f->Number);
            break;
        case FILTER_CPUTIME:
            match = compare_number(f->Op, p->CpuSeconds, f->Number);
            break;
        case FILTER_MEMUSAGE:
            match = compare_number(f->Op, p->MemoryKb, f->Number);
            break;
        default: {
            const WCHAR *subject = L"";
            switch (f->Field) {
            case FILTER_IMAGENAME: subject = p->Name; break;
            case FILTER_SESSIONNAME: subject = proc_session_name(p->Session); break;
            case FILTER_STATUS: subject = proc_status_name(p); break;
            case FILTER_USERNAME: subject = p->User ? p->User : L"N/A"; break;
            case FILTER_WINDOWTITLE: subject = p->Title ? p->Title : L"N/A"; break;
            case FILTER_SERVICES: subject = p->Services ? p->Services : L"N/A"; break;
            default: break;
            }
            BOOL any = FALSE;
            if (f->Field == FILTER_MODULES) {
                WCHAR *modules = proc_modules(p->Pid);
                any = modules && proc_list_matches(modules, f->Value);
                tu_free(modules);
            } else if (f->Field == FILTER_SERVICES && p->Services) {
                /* any one of the services matches. */
                WCHAR *copy = tu_dup(p->Services);
                for (WCHAR *s = copy; s;) {
                    WCHAR *comma = wcschr(s, L',');
                    if (comma)
                        *comma = 0;
                    if (tu_wild(f->Value, s))
                        any = TRUE;
                    s = comma ? comma + 1 : nullptr;
                }
                tu_free(copy);
            } else if (f->Field == FILTER_STATUS) {
                any = tu_eq(f->Value, subject);
            } else {
                any = tu_wild(f->Value, subject);
            }
            match = f->Op == OP_EQ ? any : !any;
            break;
        }
        }
        if (!match)
            return FALSE;
    }
    return TRUE;
}
