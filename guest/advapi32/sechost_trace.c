/*
 * event tracing sessions: start, control, enable and read them. windows runs the loggers in the kernel; here sessions
 * are rows of a table the processes share, described in mime/etwshare.h, and the providers of ntdll write to the
 * sessions that enable them. real time sessions are read from their ring, file sessions from their log file, which holds the same
 * records, not the format of windows event trace logs. the kernel loggers and the sessions of other machines are not
 * available. the checks of the arguments, the modes and the errors were read from sechost.dll, build 26300.
 */
#define MIME_BUILD_ADVAPI32 1
#include "advapi32_private.h"
#include <mime/ntapi.h>
#include <mime/etwshare.h>

#define ERROR_WMI_INSTANCE_NOT_FOUND_ 4201
#define ERROR_BAD_LENGTH_ 24
#define ERROR_MORE_DATA_ 234
#define ERROR_CANCELLED_ 1223
#define ERROR_CTX_CLOSE_PENDING_ 7007

#define CONTROL_QUERY 0
#define CONTROL_STOP 1
#define CONTROL_UPDATE 2
#define CONTROL_FLUSH 3
#define CONTROL_INCREMENT_FILE 4

#define MODE_SEQUENTIAL 0x1
#define MODE_CIRCULAR 0x2
#define MODE_APPEND 0x4
#define MODE_NEWFILE 0x8
#define MODE_PREALLOCATE 0x20
#define MODE_NONSTOPPABLE 0x40
#define MODE_REAL_TIME 0x100
#define MODE_BUFFERING 0x400
#define MODE_PRIVATE_LOGGER 0x800
#define MODE_KBYTES 0x2000
#define MODE_RELOG 0x10000
#define MODE_PRIVATE_IN_PROC 0x20000

#define PROCESS_MODE_REAL_TIME 0x100
#define PROCESS_MODE_EVENT_RECORD 0x10000000

#define WNODE_FLAG_TRACED_GUID 0x20000
#define WNODE_FLAG_VERSIONED_PROPERTIES 0x00800000

typedef struct WNODE_HEADER_ {
    ULONG BufferSize;
    ULONG ProviderId;
    ULONGLONG HistoricalContext __attribute__((aligned(8)));
    LONGLONG TimeStamp __attribute__((aligned(8)));
    GUID Guid;
    ULONG ClientContext;
    ULONG Flags;
} WNODE_HEADER_;

typedef struct EVENT_TRACE_PROPERTIES_ {
    WNODE_HEADER_ Wnode;
    ULONG BufferSize;
    ULONG MinimumBuffers;
    ULONG MaximumBuffers;
    ULONG MaximumFileSize;
    ULONG LogFileMode;
    ULONG FlushTimer;
    ULONG EnableFlags;
    LONG AgeLimit;
    ULONG NumberOfBuffers;
    ULONG FreeBuffers;
    ULONG EventsLost;
    ULONG BuffersWritten;
    ULONG LogBuffersLost;
    ULONG RealTimeBuffersLost;
    HANDLE LoggerThreadId;
    ULONG LogFileNameOffset;
    ULONG LoggerNameOffset;
} EVENT_TRACE_PROPERTIES_;

#define PROPERTIES_SIZE ((ULONG)sizeof(EVENT_TRACE_PROPERTIES_))

typedef struct ENABLE_TRACE_PARAMETERS_ {
    ULONG Version;
    ULONG EnableProperty;
    ULONG ControlFlags;
    GUID SourceId;
    PVOID EnableFilterDesc;
    ULONG FilterDescCount;
} ENABLE_TRACE_PARAMETERS_;

/* the system's own sessions have these guids and names, and no other session may have them. */
static const GUID g_system_trace_control = {0x9e814aad, 0x3204, 0x11d2, {0x9a, 0x82, 0x00, 0x60, 0x08, 0xa8, 0x69, 0x39}};
static const GUID g_circular_kernel_context = {0x54dea73a, 0xed1f, 0x42a4, {0xaf, 0x71, 0x3e, 0x63, 0xd0, 0x56, 0xf1, 0x74}};

ADVAPI BOOL WINAPI CheckTokenMembership(HANDLE token, PSID sid, PBOOL member);

/* ------------------------------------------------------------------ access */

/* administrators, the system, and the performance log users may start and stop sessions. */
static BOOL may_control(void)
{
    static const struct {
        BYTE authority;
        BYTE count;
        DWORD first, second;
    } groups[] = {{5, 2, 32, 544}, {5, 1, 18, 0}, {5, 2, 32, 559}, {5, 1, 19, 0}, {5, 1, 20, 0}};
    for (int i = 0; i < 5; i++) {
        BYTE storage[SECURITY_MAX_SID_SIZE];
        PSID sid = groups[i].count == 2 ? AdvMakeSid(storage, groups[i].authority, 2, groups[i].first, groups[i].second)
                                        : AdvMakeSid(storage, groups[i].authority, 1, groups[i].first);
        BOOL member = FALSE;
        if (CheckTokenMembership(nullptr, sid, &member) && member)
            return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ properties, in and out of the table */

static ULONG hex_name_length(const WCHAR *s)
{
    return s ? (ULONG)wcslen(s) : 0;
}

static ETWS_SESSION *session_by_handle(ETWS_TABLE *table, ULONGLONG handle)
{
    ULONG id = (USHORT)handle;
    if (id < ETWS_FIRST_ID || id >= ETWS_FIRST_ID + ETWS_MAX_SESSIONS)
        return nullptr;
    ETWS_SESSION *s = &table->Sessions[id - ETWS_FIRST_ID];
    return s->Used && s->Id == id ? s : nullptr;
}

static ETWS_SESSION *session_by_name(ETWS_TABLE *table, PCWSTR name)
{
    for (ULONG i = 0; i < ETWS_MAX_SESSIONS; i++)
        if (table->Sessions[i].Used && !_wcsicmp(table->Sessions[i].Name, name))
            return &table->Sessions[i];
    return nullptr;
}

/* copies a session into the caller's properties, and its names where the caller made room for them. */
static ULONG fill_properties(const ETWS_SESSION *s, EVENT_TRACE_PROPERTIES_ *p)
{
    ULONG size = p->Wnode.BufferSize, flags = p->Wnode.Flags, name_offset = p->LoggerNameOffset, file_offset = p->LogFileNameOffset;
    p->Wnode.ProviderId = 0;
    p->Wnode.HistoricalContext = s->Id;
    p->Wnode.TimeStamp = s->StartTime;
    p->Wnode.Guid = s->Guid;
    p->Wnode.ClientContext = s->ClockType;
    p->Wnode.Flags = (flags & WNODE_FLAG_VERSIONED_PROPERTIES) | WNODE_FLAG_TRACED_GUID;
    p->Wnode.BufferSize = size;
    p->BufferSize = s->BufferSize;
    p->MinimumBuffers = s->MinimumBuffers;
    p->MaximumBuffers = s->MaximumBuffers;
    p->MaximumFileSize = s->MaximumFileSize;
    p->LogFileMode = s->LogFileMode;
    p->FlushTimer = s->FlushTimer;
    p->EnableFlags = s->EnableFlags;
    p->AgeLimit = s->AgeLimit;
    p->NumberOfBuffers = s->NumberOfBuffers;
    p->FreeBuffers = s->FreeBuffers;
    p->EventsLost = s->EventsLost;
    p->BuffersWritten = s->BuffersWritten;
    p->LogBuffersLost = s->LogBuffersLost;
    p->RealTimeBuffersLost = s->RealTimeBuffersLost;
    p->LoggerThreadId = (HANDLE)(ULONG_PTR)s->LoggerThreadId;
    p->LoggerNameOffset = name_offset;
    p->LogFileNameOffset = file_offset;
    ULONG result = ERROR_SUCCESS;
    if (name_offset) {
        ULONG bytes = (hex_name_length(s->Name) + 1) * sizeof(WCHAR);
        if (name_offset + bytes <= size)
            memcpy((BYTE *)p + name_offset, s->Name, bytes);
        else
            result = ERROR_MORE_DATA_;
    }
    if (file_offset) {
        ULONG bytes = (hex_name_length(s->File) + 1) * sizeof(WCHAR);
        if (file_offset + bytes <= size)
            memcpy((BYTE *)p + file_offset, s->File, bytes);
        else
            result = ERROR_MORE_DATA_;
    }
    return result;
}

/* the checks of the caller's properties that every control function makes: the size, the offsets, the extension. */
static ULONG check_properties(const EVENT_TRACE_PROPERTIES_ *p, ULONG *header)
{
    if (p->Wnode.BufferSize < PROPERTIES_SIZE)
        return ERROR_BAD_LENGTH_;
    if (p->LoggerNameOffset && (p->LoggerNameOffset < PROPERTIES_SIZE || p->LoggerNameOffset > p->Wnode.BufferSize))
        return ERROR_INVALID_PARAMETER;
    if (p->LogFileNameOffset && (p->LogFileNameOffset < PROPERTIES_SIZE || p->LogFileNameOffset > p->Wnode.BufferSize))
        return ERROR_INVALID_PARAMETER;
    *header = PROPERTIES_SIZE;
    if (p->Wnode.Flags & WNODE_FLAG_VERSIONED_PROPERTIES) {
        *header = PROPERTIES_SIZE + 24;
        if (p->Wnode.BufferSize < *header)
            return ERROR_BAD_LENGTH_;
    }
    return ERROR_SUCCESS;
}

static ULONG validate_mode(const EVENT_TRACE_PROPERTIES_ *p, BOOL has_file)
{
    ULONG mode = p->LogFileMode;
    if (!(mode & (MODE_REAL_TIME | MODE_BUFFERING)) && !has_file)
        return ERROR_BAD_PATHNAME;
    if ((mode & (MODE_REAL_TIME | MODE_PRIVATE_LOGGER)) == (MODE_REAL_TIME | MODE_PRIVATE_LOGGER) ||
        ((mode & MODE_APPEND) && (mode & (MODE_REAL_TIME | MODE_CIRCULAR))) ||
        ((mode & MODE_PREALLOCATE) && (!p->MaximumFileSize || (mode & MODE_NEWFILE) || !has_file)) ||
        ((mode & MODE_KBYTES) && (!p->MaximumFileSize || !has_file)) ||
        ((mode & MODE_RELOG) && (mode & (MODE_PRIVATE_LOGGER | MODE_NEWFILE | MODE_APPEND | MODE_CIRCULAR)) != MODE_PRIVATE_LOGGER))
        return ERROR_INVALID_PARAMETER;
    if (mode & MODE_NEWFILE) {
        if (!p->MaximumFileSize || (mode & MODE_CIRCULAR) || !has_file || !memcmp(&p->Wnode.Guid, &g_system_trace_control, sizeof(GUID)))
            return ERROR_INVALID_PARAMETER;
    } else if ((mode & MODE_CIRCULAR) && !p->MaximumFileSize) {
        return ERROR_INVALID_PARAMETER;
    }
    if ((mode & MODE_NONSTOPPABLE) || ((mode & MODE_PRIVATE_IN_PROC) && !(mode & MODE_PRIVATE_LOGGER)))
        return ERROR_INVALID_PARAMETER;
    if ((mode & 0x8000800) == 0x8000800)
        return ERROR_INVALID_PARAMETER;
    return ERROR_SUCCESS;
}

/* the header of a log file: the magic and what the session was started with. */
static BOOL write_file_header(PCWSTR path, const ETWS_SESSION *s, BOOL append)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL ok = TRUE;
    if (!append || GetFileSize(file, nullptr) == 0) {
        BYTE header[ETWS_FILE_HEADER] = {};
        memcpy(header, "MIMEETL1", 8);
        memcpy(header + 8, &s->BufferSize, 4);
        memcpy(header + 12, &s->LogFileMode, 4);
        memcpy(header + 16, &s->StartTime, 8);
        memcpy(header + 24, &s->Guid, sizeof(GUID));
        _snwprintf((WCHAR *)(header + 64), 128, L"%ls", s->Name);
        DWORD written;
        ok = WriteFile(file, header, sizeof(header), &written, nullptr);
    } else {
        SetFilePointer(file, 0, nullptr, FILE_END);
    }
    DWORD error = ok ? 0 : GetLastError();
    CloseHandle(file);
    SetLastError(error);
    return ok;
}

/* ------------------------------------------------------------------ starting, controlling and stopping */

static ULONG default_processors(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors ? info.dwNumberOfProcessors : 1;
}

static ULONG start_trace(PULONGLONG handle_out, PCWSTR name, EVENT_TRACE_PROPERTIES_ *p)
{
    if (!p || !handle_out)
        return ERROR_INVALID_PARAMETER;
    if (!name)
        return ERROR_INVALID_NAME;
    ULONG header;
    ULONG error = check_properties(p, &header);
    if (error)
        return error;
    SIZE_T name_length = wcslen(name);
    BOOL kernel = !_wcsicmp(name, L"NT Kernel Logger") || !_wcsicmp(name, L"Circular Kernel Context Logger");
    if (kernel)
        return ERROR_NOT_SUPPORTED;
    if (!memcmp(&p->Wnode.Guid, &g_system_trace_control, sizeof(GUID)) || !memcmp(&p->Wnode.Guid, &g_circular_kernel_context, sizeof(GUID)))
        return ERROR_INVALID_PARAMETER;
    PCWSTR file = p->LogFileNameOffset ? (PCWSTR)((BYTE *)p + p->LogFileNameOffset) : nullptr;
    SIZE_T file_length = file ? wcsnlen(file, (p->Wnode.BufferSize - p->LogFileNameOffset) / sizeof(WCHAR)) : 0;
    SIZE_T needed = header + (name_length + 1) * sizeof(WCHAR) + (file ? (file_length + 1) * sizeof(WCHAR) : 0);
    if (p->Wnode.BufferSize < needed)
        return ERROR_BAD_LENGTH_;
    if (!name_length)
        return ERROR_INVALID_NAME;
    if (file && file_length >= RTL_NUMBER_OF(((ETWS_SESSION *)nullptr)->File))
        return ERROR_FILENAME_EXCED_RANGE;
    error = validate_mode(p, file_length != 0);
    if (error)
        return error;
    if ((p->LogFileMode & MODE_NEWFILE) && (!wcschr(file, L'%') || wcschr(file, L'%') != wcsrchr(file, L'%') || !wcsstr(file, L"%d")))
        return ERROR_INVALID_NAME;
    if (name_length >= RTL_NUMBER_OF(((ETWS_SESSION *)nullptr)->Name))
        return ERROR_INVALID_NAME;
    if (p->BufferSize > 1024 || (p->MaximumBuffers && p->MaximumBuffers < p->MinimumBuffers))
        return ERROR_INVALID_PARAMETER;
    if (!may_control())
        return ERROR_ACCESS_DENIED;
    ETWS_TABLE *table = etws_table(TRUE);
    if (!table)
        return ERROR_NOT_ENOUGH_MEMORY;
    WCHAR full[MAX_PATH] = L"";
    if (file_length) {
        DWORD n = GetFullPathNameW(file, MAX_PATH, full, nullptr);
        if (!n || n >= MAX_PATH)
            return ERROR_FILENAME_EXCED_RANGE;
    }
    etws_lock(table);
    ETWS_SESSION *s = nullptr;
    if (session_by_name(table, name)) {
        error = ERROR_ALREADY_EXISTS;
    } else {
        for (ULONG i = 0; i < ETWS_MAX_SESSIONS && !s; i++)
            if (!table->Sessions[i].Used)
                s = &table->Sessions[i];
        if (!s)
            error = ERROR_NO_SYSTEM_RESOURCES;
    }
    if (!error) {
        ULONG index = (ULONG)(s - table->Sessions);
        memset(s, 0, sizeof(*s));
        s->Id = index + ETWS_FIRST_ID;
        s->ProcessId = GetCurrentProcessId();
        s->Guid = p->Wnode.Guid;
        wcscpy(s->Name, name);
        wcscpy(s->File, full);
        s->BufferSize = p->BufferSize ? p->BufferSize : 4;
        s->MinimumBuffers = p->MinimumBuffers ? p->MinimumBuffers : 2 * default_processors();
        if (s->MinimumBuffers < 2)
            s->MinimumBuffers = 2;
        s->MaximumBuffers = p->MaximumBuffers ? p->MaximumBuffers : s->MinimumBuffers + 20;
        s->MaximumFileSize = p->MaximumFileSize;
        s->LogFileMode = p->LogFileMode;
        s->FlushTimer = p->FlushTimer;
        s->EnableFlags = p->EnableFlags & 0x7FFFFFFF;
        s->AgeLimit = p->AgeLimit;
        s->ClockType = p->Wnode.ClientContext ? p->Wnode.ClientContext : 1;
        s->NumberOfBuffers = s->MinimumBuffers;
        s->FreeBuffers = s->MinimumBuffers - 1;
        s->LoggerThreadId = GetCurrentThreadId();
        LARGE_INTEGER now;
        NtQuerySystemTime(&now);
        s->StartTime = now.QuadPart;
        if (full[0] && !write_file_header(full, s, (p->LogFileMode & MODE_APPEND) != 0)) {
            error = GetLastError();
        } else {
            s->Used = 1;
            etws_changed(table);
            fill_properties(s, p);
            *handle_out = s->Id;
        }
    }
    etws_unlock(table);
    return error;
}

ADVAPI ULONG WINAPI StartTraceW(PULONGLONG handle, PCWSTR name, EVENT_TRACE_PROPERTIES_ *properties)
{
    ULONG error = start_trace(handle, name, properties);
    if (error)
        SetLastError(error);
    return error;
}

/* answers an ansi call in the caller's properties: the numbers, then the names narrowed where the caller has room. */
static ULONG copy_back_ansi(EVENT_TRACE_PROPERTIES_ *p, const BYTE *copy, ULONG header, ULONG size, ULONG name_offset,
                            ULONG file_offset)
{
    const EVENT_TRACE_PROPERTIES_ *w = (const EVENT_TRACE_PROPERTIES_ *)copy;
    ULONG saved = p->Wnode.Flags, error = ERROR_SUCCESS;
    memcpy(p, w, header);
    p->Wnode.BufferSize = size;
    p->Wnode.Flags = (saved & WNODE_FLAG_VERSIONED_PROPERTIES) | WNODE_FLAG_TRACED_GUID;
    p->LoggerNameOffset = name_offset;
    p->LogFileNameOffset = file_offset;
    ULONG offsets[2] = {name_offset, file_offset}, from[2] = {w->LoggerNameOffset, w->LogFileNameOffset};
    for (int i = 0; i < 2; i++) {
        if (!offsets[i])
            continue;
        int n = WideCharToMultiByte(CP_ACP, 0, (const WCHAR *)(copy + from[i]), -1, nullptr, 0, nullptr, nullptr);
        if (offsets[i] + n <= size)
            WideCharToMultiByte(CP_ACP, 0, (const WCHAR *)(copy + from[i]), -1, (PSTR)p + offsets[i], n, nullptr, nullptr);
        else
            error = ERROR_MORE_DATA_;
    }
    return error;
}

#define ANSI_NAMES (128 * sizeof(WCHAR) + 260 * sizeof(WCHAR))

ADVAPI ULONG WINAPI StartTraceA(PULONGLONG handle, PCSTR name, EVENT_TRACE_PROPERTIES_ *properties)
{
    ULONG error = ERROR_SUCCESS, header = 0;
    PWSTR wide_name = name ? AdvAnsiToWide(name) : nullptr;
    BYTE *copy = nullptr;
    if (!properties || !handle)
        error = ERROR_INVALID_PARAMETER;
    else if (!name)
        error = ERROR_INVALID_NAME;
    else if (!(error = check_properties(properties, &header)) && !wide_name)
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error) {
        copy = AdvAllocZero(header + ANSI_NAMES);
        if (!copy)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error) {
        EVENT_TRACE_PROPERTIES_ *w = (EVENT_TRACE_PROPERTIES_ *)copy;
        ULONG size = properties->Wnode.BufferSize, name_offset = properties->LoggerNameOffset, file_offset = properties->LogFileNameOffset;
        memcpy(copy, properties, header);
        w->Wnode.BufferSize = header + ANSI_NAMES;
        w->LoggerNameOffset = name_offset ? header : 0;
        w->LogFileNameOffset = file_offset ? header + 128 * sizeof(WCHAR) : 0;
        if (file_offset) {
            PWSTR file = AdvAnsiToWide((PCSTR)properties + file_offset);
            if (file) {
                _snwprintf((WCHAR *)(copy + w->LogFileNameOffset), 260, L"%ls", file);
                AdvFree(file);
            }
        }
        error = start_trace(handle, wide_name, w);
        if (!error)
            copy_back_ansi(properties, copy, header, size, name_offset, file_offset);
    }
    AdvFree(copy);
    AdvFree(wide_name);
    if (error)
        SetLastError(error);
    return error;
}

static ULONG control_trace(ULONGLONG handle, PCWSTR name, EVENT_TRACE_PROPERTIES_ *p, ULONG code)
{
    if (!p)
        return ERROR_INVALID_PARAMETER;
    ULONG header;
    ULONG error = check_properties(p, &header);
    if (error)
        return error;
    if (!handle && (!name || !*name))
        return ERROR_INVALID_PARAMETER;
    if ((p->LogFileMode & MODE_APPEND) && code == CONTROL_UPDATE && p->LogFileNameOffset)
        return ERROR_INVALID_PARAMETER;
    if (code > CONTROL_INCREMENT_FILE)
        return ERROR_INVALID_PARAMETER;
    if (code != CONTROL_QUERY && !may_control())
        return ERROR_ACCESS_DENIED;
    ETWS_TABLE *table = etws_table(FALSE);
    if (!table)
        return ERROR_WMI_INSTANCE_NOT_FOUND_;
    etws_lock(table);
    ETWS_SESSION *s = handle ? session_by_handle(table, handle) : session_by_name(table, name);
    if (!s) {
        error = ERROR_WMI_INSTANCE_NOT_FOUND_;
    } else {
        switch (code) {
        case CONTROL_UPDATE:
            if (p->FlushTimer || p->Wnode.Flags)
                s->FlushTimer = p->FlushTimer;
            if (p->MaximumBuffers)
                s->MaximumBuffers = p->MaximumBuffers < s->MinimumBuffers ? s->MinimumBuffers : p->MaximumBuffers;
            if (p->EnableFlags)
                s->EnableFlags = p->EnableFlags & 0x7FFFFFFF;
            if (p->AgeLimit)
                s->AgeLimit = p->AgeLimit;
            if (p->MaximumFileSize && (s->LogFileMode & (MODE_CIRCULAR | MODE_NEWFILE)))
                s->MaximumFileSize = p->MaximumFileSize;
            error = fill_properties(s, p);
            break;
        case CONTROL_INCREMENT_FILE:
            if (!(s->LogFileMode & MODE_NEWFILE)) {
                error = ERROR_INVALID_PARAMETER;
                break;
            }
            s->FileCounter++;
            error = fill_properties(s, p);
            break;
        case CONTROL_STOP:
            error = fill_properties(s, p);
            s->Used = 0;
            etws_changed(table);
            break;
        default:
            error = fill_properties(s, p);
            break;
        }
    }
    etws_unlock(table);
    return error;
}

ADVAPI ULONG WINAPI ControlTraceW(ULONGLONG handle, PCWSTR name, EVENT_TRACE_PROPERTIES_ *properties, ULONG code)
{
    ULONG error = control_trace(handle, name, properties, code);
    if (error && error != ERROR_MORE_DATA_)
        SetLastError(error);
    return error;
}

/* an ansi call runs on a wide copy of the properties and answers in the caller's, with the names narrowed. */
static ULONG control_ansi(ULONGLONG handle, PCSTR name, EVENT_TRACE_PROPERTIES_ *p, ULONG code)
{
    if (!p)
        return ERROR_INVALID_PARAMETER;
    ULONG header;
    ULONG error = check_properties(p, &header);
    if (error)
        return error;
    PWSTR wide_name = name ? AdvAnsiToWide(name) : nullptr;
    if (name && !wide_name)
        return ERROR_NOT_ENOUGH_MEMORY;
    ULONG size = p->Wnode.BufferSize;
    ULONG wide_size = header + 2 * 128 * sizeof(WCHAR) + 2 * 260 * sizeof(WCHAR);
    BYTE *copy = AdvAllocZero(wide_size);
    if (!copy) {
        AdvFree(wide_name);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    EVENT_TRACE_PROPERTIES_ *w = (EVENT_TRACE_PROPERTIES_ *)copy;
    memcpy(copy, p, header);
    w->Wnode.BufferSize = wide_size;
    ULONG original_name = p->LoggerNameOffset, original_file = p->LogFileNameOffset;
    w->LoggerNameOffset = original_name ? header : 0;
    w->LogFileNameOffset = original_file ? header + 128 * sizeof(WCHAR) : 0;
    if (original_file && code == CONTROL_UPDATE) {
        PWSTR file = AdvAnsiToWide((PCSTR)p + original_file);
        if (file) {
            wcscpy((WCHAR *)(copy + w->LogFileNameOffset), file);
            AdvFree(file);
        }
    }
    error = control_trace(handle, wide_name, w, code);
    if (!error || error == ERROR_MORE_DATA_)
        error = copy_back_ansi(p, copy, header, size, original_name, original_file);
    AdvFree(copy);
    AdvFree(wide_name);
    return error;
}

ADVAPI ULONG WINAPI ControlTraceA(ULONGLONG handle, PCSTR name, EVENT_TRACE_PROPERTIES_ *properties, ULONG code)
{
    ULONG error = control_ansi(handle, name, properties, code);
    if (error && error != ERROR_MORE_DATA_)
        SetLastError(error);
    return error;
}

ADVAPI ULONG WINAPI StopTraceW(ULONGLONG handle, PCWSTR name, EVENT_TRACE_PROPERTIES_ *properties)
{
    return ControlTraceW(handle, name, properties, CONTROL_STOP);
}

ADVAPI ULONG WINAPI StopTraceA(ULONGLONG handle, PCSTR name, EVENT_TRACE_PROPERTIES_ *properties)
{
    return ControlTraceA(handle, name, properties, CONTROL_STOP);
}

ADVAPI ULONG WINAPI QueryTraceW(ULONGLONG handle, PCWSTR name, EVENT_TRACE_PROPERTIES_ *properties)
{
    return ControlTraceW(handle, name, properties, CONTROL_QUERY);
}

ADVAPI ULONG WINAPI QueryTraceA(ULONGLONG handle, PCSTR name, EVENT_TRACE_PROPERTIES_ *properties)
{
    return ControlTraceA(handle, name, properties, CONTROL_QUERY);
}

ADVAPI ULONG WINAPI UpdateTraceW(ULONGLONG handle, PCWSTR name, EVENT_TRACE_PROPERTIES_ *properties)
{
    return ControlTraceW(handle, name, properties, CONTROL_UPDATE);
}

ADVAPI ULONG WINAPI UpdateTraceA(ULONGLONG handle, PCSTR name, EVENT_TRACE_PROPERTIES_ *properties)
{
    return ControlTraceA(handle, name, properties, CONTROL_UPDATE);
}

ADVAPI ULONG WINAPI FlushTraceW(ULONGLONG handle, PCWSTR name, EVENT_TRACE_PROPERTIES_ *properties)
{
    return ControlTraceW(handle, name, properties, CONTROL_FLUSH);
}

ADVAPI ULONG WINAPI FlushTraceA(ULONGLONG handle, PCSTR name, EVENT_TRACE_PROPERTIES_ *properties)
{
    return ControlTraceA(handle, name, properties, CONTROL_FLUSH);
}

/* each running session answers a query into the next entry of the caller's array. */
static ULONG query_all(EVENT_TRACE_PROPERTIES_ **array, ULONG capacity, PULONG count, BOOL wide)
{
    if (!count || (capacity && (!array || !*array)))
        return ERROR_INVALID_PARAMETER;
    *count = 0;
    ETWS_TABLE *table = etws_table(FALSE);
    ULONG found = 0;
    for (ULONG i = 0; table && i < ETWS_MAX_SESSIONS; i++) {
        EVENT_TRACE_PROPERTIES_ scratch = {};
        EVENT_TRACE_PROPERTIES_ *p;
        if (found >= capacity) {
            scratch.Wnode.BufferSize = PROPERTIES_SIZE;
            p = &scratch;
        } else {
            p = array[found];
        }
        p->Wnode.HistoricalContext = i + ETWS_FIRST_ID;
        ULONG error = wide ? control_trace(p->Wnode.HistoricalContext, nullptr, p, CONTROL_QUERY)
                           : control_ansi(p->Wnode.HistoricalContext, nullptr, p, CONTROL_QUERY);
        if (!error || error == ERROR_MORE_DATA_)
            found++;
    }
    *count = found;
    return capacity < found ? ERROR_MORE_DATA_ : ERROR_SUCCESS;
}

ADVAPI ULONG WINAPI QueryAllTracesW(EVENT_TRACE_PROPERTIES_ **properties, ULONG capacity, PULONG count)
{
    ULONG error = query_all(properties, capacity, count, TRUE);
    if (error && error != ERROR_MORE_DATA_)
        SetLastError(error);
    return error;
}

ADVAPI ULONG WINAPI QueryAllTracesA(EVENT_TRACE_PROPERTIES_ **properties, ULONG capacity, PULONG count)
{
    ULONG error = query_all(properties, capacity, count, FALSE);
    if (error && error != ERROR_MORE_DATA_)
        SetLastError(error);
    return error;
}

/* ------------------------------------------------------------------ enabling providers */

#define CONTROL_DISABLE 0
#define CONTROL_ENABLE 1
#define CONTROL_CAPTURE 2

ADVAPI ULONG WINAPI EnableTraceEx2(ULONGLONG handle, const GUID *provider, ULONG code, UCHAR level, ULONGLONG any, ULONGLONG all,
                                   ULONG timeout, ENABLE_TRACE_PARAMETERS_ *parameters)
{
    ULONG property = 0;
    ULONG error = ERROR_SUCCESS;
    if (parameters) {
        if (parameters->ControlFlags || (parameters->Version != 1 && parameters->Version != 2))
            error = ERROR_INVALID_PARAMETER;
        else
            property = parameters->EnableProperty;
        if (!error && parameters->Version == 2 && parameters->FilterDescCount && !parameters->EnableFilterDesc)
            error = ERROR_INVALID_PARAMETER;
    }
    if (!error && (code > CONTROL_CAPTURE || !provider))
        error = ERROR_INVALID_PARAMETER;
    if (!error && !may_control())
        error = ERROR_ACCESS_DENIED;
    ETWS_TABLE *table = error ? nullptr : etws_table(FALSE);
    if (!error && !table)
        error = ERROR_INVALID_HANDLE;
    if (!error) {
        etws_lock(table);
        ETWS_SESSION *s = session_by_handle(table, handle);
        if (!s) {
            error = ERROR_INVALID_HANDLE;
        } else if (code == CONTROL_CAPTURE) {
            etws_changed(table);
        } else {
            ETWS_PROVIDER *slot = nullptr, *free_slot = nullptr;
            for (ULONG i = 0; i < ETWS_MAX_PROVIDERS; i++) {
                if (s->Providers[i].Used && !memcmp(&s->Providers[i].Guid, provider, sizeof(GUID)))
                    slot = &s->Providers[i];
                else if (!s->Providers[i].Used && !free_slot)
                    free_slot = &s->Providers[i];
            }
            if (code == CONTROL_DISABLE) {
                if (slot)
                    slot->Used = 0;
            } else if (slot || free_slot) {
                if (!slot) {
                    slot = free_slot;
                    slot->Guid = *provider;
                }
                slot->Level = level;
                slot->MatchAny = any;
                slot->MatchAll = all;
                slot->Property = property;
                slot->Used = 1;
            } else {
                error = ERROR_NO_SYSTEM_RESOURCES;
            }
            if (!error)
                etws_changed(table);
        }
        etws_unlock(table);
    }
    if (error)
        SetLastError(error);
    return error;
}

ADVAPI ULONG WINAPI EnableTrace(ULONG enable, ULONG flags, ULONG level, const GUID *provider, ULONGLONG handle)
{
    return EnableTraceEx2(handle, provider, enable ? CONTROL_ENABLE : CONTROL_DISABLE, (UCHAR)level, flags, 0, 0, nullptr);
}

ADVAPI ULONG WINAPI EnableTraceEx(const GUID *provider, const GUID *source, ULONGLONG handle, ULONG enable, UCHAR level, ULONGLONG any,
                                  ULONGLONG all, ULONG property, PVOID filter)
{
    ENABLE_TRACE_PARAMETERS_ parameters = {1, property, 0, {}, filter, 0};
    if (source)
        parameters.SourceId = *source;
    return EnableTraceEx2(handle, provider, enable ? CONTROL_ENABLE : CONTROL_DISABLE, level, any, all, 0, &parameters);
}

/* the providers sessions enable, once each. */
static ULONG enabled_providers(GUID *guids, ULONG capacity)
{
    ETWS_TABLE *table = etws_table(FALSE);
    ULONG count = 0;
    if (!table)
        return 0;
    etws_lock(table);
    for (ULONG s = 0; s < ETWS_MAX_SESSIONS; s++) {
        if (!table->Sessions[s].Used)
            continue;
        for (ULONG p = 0; p < ETWS_MAX_PROVIDERS; p++) {
            const ETWS_PROVIDER *provider = &table->Sessions[s].Providers[p];
            BOOL seen = FALSE;
            if (!provider->Used)
                continue;
            for (ULONG k = 0; k < count && k < capacity; k++)
                seen |= !memcmp(&guids[k], &provider->Guid, sizeof(GUID));
            if (seen)
                continue;
            if (count < capacity)
                guids[count] = provider->Guid;
            count++;
        }
    }
    etws_unlock(table);
    return count;
}

typedef struct TRACE_GUID_PROPERTIES_ {
    GUID Guid;
    ULONG GuidType;
    ULONG LoggerId;
    ULONG EnableLevel;
    ULONG EnableFlags;
    BOOLEAN IsEnable;
} TRACE_GUID_PROPERTIES_;

ADVAPI ULONG WINAPI EnumerateTraceGuids(TRACE_GUID_PROPERTIES_ **properties, ULONG capacity, PULONG registered)
{
    if (!registered || (capacity && (!properties || !*properties))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return ERROR_INVALID_PARAMETER;
    }
    GUID guids[ETWS_MAX_SESSIONS * ETWS_MAX_PROVIDERS];
    ULONG count = enabled_providers(guids, RTL_NUMBER_OF(guids));
    ETWS_TABLE *table = etws_table(FALSE);
    for (ULONG i = 0; i < count && i < capacity; i++) {
        UCHAR level = 0;
        ULONGLONG any = 0, all = 0;
        memset(properties[i], 0, sizeof(*properties[i]));
        properties[i]->Guid = guids[i];
        properties[i]->GuidType = 1;
        properties[i]->IsEnable = table && etws_provider_state(table, &guids[i], &level, &any, &all);
        properties[i]->EnableLevel = level;
        properties[i]->EnableFlags = (ULONG)any;
    }
    *registered = count;
    return capacity < count ? ERROR_MORE_DATA_ : ERROR_SUCCESS;
}

ADVAPI ULONG WINAPI EnumerateTraceGuidsEx(ULONG klass, PVOID in, ULONG in_size, PVOID out, ULONG out_size, PULONG returned)
{
    ULONG error = ERROR_SUCCESS;
    if (!returned || (out_size && !out)) {
        error = ERROR_INVALID_PARAMETER;
    } else if (klass == 0) {
        GUID guids[ETWS_MAX_SESSIONS * ETWS_MAX_PROVIDERS];
        ULONG count = enabled_providers(guids, RTL_NUMBER_OF(guids));
        *returned = count * sizeof(GUID);
        if (out_size < *returned)
            error = ERROR_INSUFFICIENT_BUFFER;
        else
            memcpy(out, guids, *returned);
    } else if (klass == 1) {
        if (!in || in_size < sizeof(GUID)) {
            error = ERROR_INVALID_PARAMETER;
        } else {
            ETWS_TABLE *table = etws_table(FALSE);
            ULONG enables = 0;
            struct {
                ULONG Logger;
                UCHAR Level;
                ULONG Property;
                ULONGLONG Any, All;
            } list[ETWS_MAX_SESSIONS];
            for (ULONG s = 0; table && s < ETWS_MAX_SESSIONS; s++) {
                etws_lock(table);
                if (table->Sessions[s].Used)
                    for (ULONG p = 0; p < ETWS_MAX_PROVIDERS; p++) {
                        const ETWS_PROVIDER *provider = &table->Sessions[s].Providers[p];
                        if (provider->Used && !memcmp(&provider->Guid, in, sizeof(GUID))) {
                            list[enables].Logger = table->Sessions[s].Id;
                            list[enables].Level = provider->Level;
                            list[enables].Property = provider->Property;
                            list[enables].Any = provider->MatchAny;
                            list[enables].All = provider->MatchAll;
                            enables++;
                        }
                    }
                etws_unlock(table);
            }
            ULONG size = 8 + (enables ? 16 + enables * 32 : 0);
            *returned = size;
            if (out_size < size) {
                error = ERROR_INSUFFICIENT_BUFFER;
            } else {
                BYTE *at = out;
                memset(at, 0, size);
                *(ULONG *)at = enables ? 1 : 0;
                if (enables) {
                    *(ULONG *)(at + 8) = 0;
                    *(ULONG *)(at + 12) = enables;
                    for (ULONG i = 0; i < enables; i++) {
                        BYTE *e = at + 24 + i * 32;
                        *(ULONG *)e = 1;
                        e[4] = list[i].Level;
                        *(USHORT *)(e + 6) = (USHORT)list[i].Logger;
                        *(ULONG *)(e + 8) = list[i].Property;
                        *(ULONGLONG *)(e + 16) = list[i].Any;
                        *(ULONGLONG *)(e + 24) = list[i].All;
                    }
                }
            }
        }
    } else if (klass == 2) {
        *returned = 0;
    } else {
        error = ERROR_INVALID_PARAMETER;
    }
    if (error)
        SetLastError(error);
    return error;
}

/* ------------------------------------------------------------------ information about the tracing machinery */

enum { TraceVersionInfo = 8, TraceProviderBinaryTracking = 15, TraceMaxLoggersQuery = 16, TraceMaxPmcCounterQuery = 19,
       TraceStreamCount = 20, TraceInfoClassMax = 31 };

static BOOLEAN g_binary_tracking;

ADVAPI ULONG WINAPI TraceSetInformation(ULONGLONG handle, ULONG klass, PVOID info, ULONG size)
{
    ULONG error = ERROR_SUCCESS;
    if (klass >= TraceInfoClassMax || klass == 0) {
        error = ERROR_INVALID_PARAMETER;
    } else if (klass == TraceProviderBinaryTracking) {
        if (!info || size != sizeof(BOOLEAN))
            error = ERROR_INVALID_PARAMETER;
        else if (!may_control())
            error = ERROR_ACCESS_DENIED;
        else
            g_binary_tracking = *(BOOLEAN *)info != 0;
    } else if (klass == TraceStreamCount) {
        if (!info || size != sizeof(ULONG) || !*(ULONG *)info)
            error = ERROR_INVALID_PARAMETER;
        else if (!may_control())
            error = ERROR_ACCESS_DENIED;
        else {
            ETWS_TABLE *table = etws_table(FALSE);
            etws_lock(table);
            error = table && session_by_handle(table, handle) ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
            etws_unlock(table);
        }
    } else {
        error = ERROR_NOT_SUPPORTED;
    }
    if (error)
        SetLastError(error);
    return error;
}

ADVAPI ULONG WINAPI TraceQueryInformation(ULONGLONG handle, ULONG klass, PVOID info, ULONG size, PULONG returned)
{
    ULONG error = ERROR_SUCCESS, needed = 0;
    switch (klass) {
    case TraceVersionInfo:
        needed = 8;
        break;
    case TraceMaxLoggersQuery:
    case TraceStreamCount:
        needed = sizeof(ULONG);
        break;
    case TraceProviderBinaryTracking:
        needed = sizeof(BOOLEAN);
        break;
    case TraceMaxPmcCounterQuery:
        error = ERROR_NOT_SUPPORTED;
        break;
    default:
        error = klass >= TraceInfoClassMax || klass == 0 ? ERROR_INVALID_PARAMETER : ERROR_NOT_SUPPORTED;
        break;
    }
    if (!error && !info && size)
        error = ERROR_INVALID_PARAMETER;
    if (!error) {
        if (returned)
            *returned = needed;
        if (size < needed) {
            error = ERROR_INSUFFICIENT_BUFFER;
        } else if (klass == TraceVersionInfo) {
            ULONG version[2] = {3, 0};
            memcpy(info, version, sizeof(version));
        } else if (klass == TraceMaxLoggersQuery) {
            *(ULONG *)info = ETWS_MAX_SESSIONS;
        } else if (klass == TraceStreamCount) {
            ETWS_TABLE *table = etws_table(FALSE);
            if (table)
                etws_lock(table);
            BOOL found = table && session_by_handle(table, handle);
            if (table)
                etws_unlock(table);
            if (found)
                *(ULONG *)info = 1;
            else
                error = ERROR_INVALID_HANDLE;
        } else {
            *(BOOLEAN *)info = g_binary_tracking;
        }
    }
    if (error)
        SetLastError(error);
    return error;
}

/* the trace of the last branch records is a feature of the processor's performance monitor. */
ADVAPI ULONG WINAPI TraceConfigureLastBranchRecord(ULONGLONG handle, PVOID a, PVOID b, ULONG c)
{
    (void)handle;
    (void)a;
    (void)b;
    (void)c;
    SetLastError(ERROR_NOT_SUPPORTED);
    return ERROR_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ who may start sessions for a provider */

#define PROVIDER_ACCESS_KEY L"SYSTEM\\CurrentControlSet\\Control\\WMI\\Security"
#define EVENT_SECURITY_SET_DACL 0
#define EVENT_SECURITY_SET_SACL 1
#define EVENT_SECURITY_ADD_DACL 2
#define EVENT_SECURITY_ADD_SACL 3
#define EVENT_SECURITY_DELETE_DACL 4
#define EVENT_SECURITY_DELETE_SACL 5

ADVAPI BOOL WINAPI AddAccessAllowedAce(PACL acl, DWORD revision, DWORD mask, PSID sid);
ADVAPI BOOL WINAPI AddAccessDeniedAce(PACL acl, DWORD revision, DWORD mask, PSID sid);

static void guid_name(const GUID *g, WCHAR out[40])
{
    _snwprintf(out, 40, L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", g->Data1, g->Data2, g->Data3, g->Data4[0],
               g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

ADVAPI ULONG WINAPI EventAccessControl(const GUID *guid, ULONG operation, PSID sid, ULONG rights, BOOLEAN allow)
{
    ULONG error = ERROR_SUCCESS;
    if (!guid || !sid || operation > EVENT_SECURITY_ADD_SACL)
        return ERROR_INVALID_PARAMETER;
    if (!may_control())
        return ERROR_ACCESS_DENIED;
    WCHAR name[40];
    guid_name(guid, name);
    BYTE *old = nullptr;
    DWORD old_size = 0;
    HKEY key;
    if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, PROVIDER_ACCESS_KEY, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr)) {
        DWORD type;
        if (!RegQueryValueExW(key, name, nullptr, &type, nullptr, &old_size) && old_size) {
            old = AdvAlloc(old_size);
            if (old && RegQueryValueExW(key, name, nullptr, &type, old, &old_size))
                old_size = 0;
        }
        DWORD sid_length = AdvSidLength(sid);
        DWORD size = (operation == EVENT_SECURITY_ADD_DACL && old ? old_size : 8) + 8 + sid_length + 8;
        BYTE *acl = AdvAllocZero(size);
        if (acl) {
            RtlCreateAcl((PACL)acl, size, 2);
            /* the new entry first, then the entries that were there. */
            if (allow ? !AddAccessAllowedAce((PACL)acl, 2, rights, sid) : !AddAccessDeniedAce((PACL)acl, 2, rights, sid))
                error = GetLastError();
            if (!error)
                error = RegSetValueExW(key, name, 0, REG_BINARY, acl, ((PACL)acl)->AclSize);
            AdvFree(acl);
        } else {
            error = ERROR_NOT_ENOUGH_MEMORY;
        }
        RegCloseKey(key);
    } else {
        error = ERROR_ACCESS_DENIED;
    }
    AdvFree(old);
    return error;
}

ADVAPI ULONG WINAPI EventAccessQuery(const GUID *guid, PACL acl, PULONG size)
{
    if (!guid || !size)
        return ERROR_INVALID_PARAMETER;
    WCHAR name[40];
    guid_name(guid, name);
    DWORD stored = 0, type;
    HKEY key;
    ULONG error = ERROR_FILE_NOT_FOUND;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, PROVIDER_ACCESS_KEY, 0, KEY_QUERY_VALUE, &key)) {
        if (!RegQueryValueExW(key, name, nullptr, &type, nullptr, &stored)) {
            if (*size < stored || !acl) {
                *size = stored;
                error = ERROR_INSUFFICIENT_BUFFER;
            } else {
                error = RegQueryValueExW(key, name, nullptr, &type, (BYTE *)acl, &stored);
                *size = stored;
            }
        }
        RegCloseKey(key);
    }
    return error;
}

ADVAPI ULONG WINAPI EventAccessRemove(const GUID *guid)
{
    if (!guid)
        return ERROR_INVALID_PARAMETER;
    if (!may_control())
        return ERROR_ACCESS_DENIED;
    WCHAR name[40];
    guid_name(guid, name);
    HKEY key;
    ULONG error = ERROR_SUCCESS;
    if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, PROVIDER_ACCESS_KEY, 0, KEY_SET_VALUE, &key)) {
        error = RegDeleteValueW(key, name);
        RegCloseKey(key);
    } else {
        error = ERROR_FILE_NOT_FOUND;
    }
    return error;
}

/* ------------------------------------------------------------------ consumers */

typedef LONGLONG ALIGNED_LONGLONG __attribute__((aligned(8)));

typedef struct TIME_ZONE_INFORMATION_ {
    LONG Bias;
    WCHAR StandardName[32];
    USHORT StandardDate[8];
    LONG StandardBias;
    WCHAR DaylightName[32];
    USHORT DaylightDate[8];
    LONG DaylightBias;
} TIME_ZONE_INFORMATION_;

typedef struct EVENT_TRACE_HEADER_ {
    USHORT Size;
    USHORT FieldTypeFlags;
    UCHAR Type;
    UCHAR Level;
    USHORT Version;
    ULONG ThreadId;
    ULONG ProcessId;
    ALIGNED_LONGLONG TimeStamp;
    GUID Guid;
    ULONG ClientContext;
    ULONG Flags;
} EVENT_TRACE_HEADER_;

typedef struct EVENT_TRACE_ {
    EVENT_TRACE_HEADER_ Header;
    ULONG InstanceId;
    ULONG ParentInstanceId;
    GUID ParentGuid;
    PVOID MofData;
    ULONG MofLength;
    ULONG ClientContext;
} EVENT_TRACE_;

typedef struct TRACE_LOGFILE_HEADER_ {
    ULONG BufferSize;
    ULONG Version;
    ULONG ProviderVersion;
    ULONG NumberOfProcessors;
    ALIGNED_LONGLONG EndTime;
    ULONG TimerResolution;
    ULONG MaximumFileSize;
    ULONG LogFileMode;
    ULONG BuffersWritten;
    GUID LogInstanceGuid;
    PWSTR LoggerName;
    PWSTR LogFileName;
    TIME_ZONE_INFORMATION_ TimeZone;
    ALIGNED_LONGLONG BootTime;
    ALIGNED_LONGLONG PerfFreq;
    ALIGNED_LONGLONG StartTime;
    ULONG ReservedFlags;
    ULONG BuffersLost;
} TRACE_LOGFILE_HEADER_;

typedef struct EVENT_TRACE_LOGFILEW_ {
    PWSTR LogFileName;
    PWSTR LoggerName;
    ALIGNED_LONGLONG CurrentTime;
    ULONG BuffersRead;
    ULONG ProcessTraceMode;
    EVENT_TRACE_ CurrentEvent;
    TRACE_LOGFILE_HEADER_ LogfileHeader;
    PVOID BufferCallback;
    ULONG BufferSize;
    ULONG Filled;
    ULONG EventsLost;
    PVOID EventCallback;
    ULONG IsKernelTrace;
    PVOID Context;
} EVENT_TRACE_LOGFILEW_;

typedef struct EVENT_RECORD_ {
    USHORT Size;
    USHORT HeaderType;
    USHORT Flags;
    USHORT EventProperty;
    ULONG ThreadId;
    ULONG ProcessId;
    ALIGNED_LONGLONG TimeStamp;
    GUID ProviderId;
    USHORT Id;
    UCHAR Version;
    UCHAR Channel;
    UCHAR Level;
    UCHAR Opcode;
    USHORT Task;
    ULONGLONG Keyword __attribute__((aligned(8)));
    ULONGLONG ProcessorTime __attribute__((aligned(8)));
    GUID ActivityId;
    UCHAR ProcessorNumber;
    UCHAR Alignment;
    USHORT LoggerId;
    USHORT ExtendedDataCount;
    USHORT UserDataLength;
    PVOID ExtendedData;
    PVOID UserData;
    PVOID UserContext;
} EVENT_RECORD_;

#define MAX_CONSUMERS 32
#define INVALID_CONSUMER ((ULONGLONG)-1)
#define CONSUMER_TAG 0x4D450000u

typedef struct CONSUMER {
    BOOL Used;
    BOOL Closed;
    BOOL Processing;
    BOOL Done;
    BOOL RealTime;
    ULONG Mode;
    WCHAR Logger[128];
    WCHAR File[260];
    PVOID RecordCallback;
    PVOID EventCallback;
    PVOID Context;
    PVOID BufferCallback;
    ULONG Session;
    ULONGLONG Cursor;
    ULONGLONG Start;
    LONGLONG LastTime;
    HANDLE FileHandle;
    ULONGLONG FileOffset;
} CONSUMER;

static CONSUMER g_consumers[MAX_CONSUMERS];
static SRWLOCK g_consumer_lock = SRWLOCK_INIT;

static CONSUMER *consumer_of(ULONGLONG handle)
{
    ULONG index = (ULONG)handle - CONSUMER_TAG;
    if ((ULONG)(handle >> 32) || (ULONG)handle < CONSUMER_TAG || index >= MAX_CONSUMERS || !g_consumers[index].Used)
        return nullptr;
    return &g_consumers[index];
}

static ULONGLONG open_failed(ULONG error)
{
    SetLastError(error);
    return INVALID_CONSUMER;
}

static ULONGLONG open_trace(EVENT_TRACE_LOGFILEW_ *logfile)
{
    if (!logfile)
        return open_failed(ERROR_INVALID_PARAMETER);
    BOOL real_time = (logfile->ProcessTraceMode & PROCESS_MODE_REAL_TIME) != 0;
    if (real_time ? !logfile->LoggerName : !logfile->LogFileName)
        return open_failed(real_time ? ERROR_INVALID_NAME : ERROR_INVALID_PARAMETER);
    if (real_time && (logfile->LogFileName && *logfile->LogFileName))
        return open_failed(ERROR_INVALID_PARAMETER);
    ULONG session = 0;
    ULONGLONG cursor = 0;
    HANDLE file = INVALID_HANDLE_VALUE;
    if (real_time) {
        ETWS_TABLE *table = etws_table(FALSE);
        if (!table)
            return open_failed(ERROR_WMI_INSTANCE_NOT_FOUND_);
        etws_lock(table);
        ETWS_SESSION *s = session_by_name(table, logfile->LoggerName);
        if (s) {
            session = s->Id;
            cursor = s->Write;
        }
        etws_unlock(table);
        if (!s)
            return open_failed(ERROR_WMI_INSTANCE_NOT_FOUND_);
    } else {
        file = CreateFileW(logfile->LogFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return open_failed(GetLastError());
        BYTE header[ETWS_FILE_HEADER];
        DWORD read;
        if (!ReadFile(file, header, sizeof(header), &read, nullptr) || read != sizeof(header) || memcmp(header, "MIMEETL1", 8)) {
            CloseHandle(file);
            return open_failed(ERROR_BAD_FORMAT);
        }
    }
    AcquireSRWLockExclusive(&g_consumer_lock);
    ULONG index = 0;
    while (index < MAX_CONSUMERS && g_consumers[index].Used)
        index++;
    if (index == MAX_CONSUMERS) {
        ReleaseSRWLockExclusive(&g_consumer_lock);
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        return open_failed(ERROR_NO_SYSTEM_RESOURCES);
    }
    CONSUMER *c = &g_consumers[index];
    memset(c, 0, sizeof(*c));
    c->Used = TRUE;
    c->RealTime = real_time;
    c->Mode = logfile->ProcessTraceMode;
    if (real_time)
        _snwprintf(c->Logger, RTL_NUMBER_OF(c->Logger), L"%ls", logfile->LoggerName);
    else
        _snwprintf(c->File, RTL_NUMBER_OF(c->File), L"%ls", logfile->LogFileName);
    c->RecordCallback = (logfile->ProcessTraceMode & PROCESS_MODE_EVENT_RECORD) ? logfile->EventCallback : nullptr;
    c->EventCallback = (logfile->ProcessTraceMode & PROCESS_MODE_EVENT_RECORD) ? nullptr : logfile->EventCallback;
    c->BufferCallback = logfile->BufferCallback;
    c->Context = logfile->Context;
    c->Session = session;
    c->Cursor = cursor;
    c->FileHandle = file;
    c->FileOffset = ETWS_FILE_HEADER;
    ReleaseSRWLockExclusive(&g_consumer_lock);
    logfile->LogfileHeader.BufferSize = 0;
    logfile->IsKernelTrace = 0;
    return (ULONGLONG)(CONSUMER_TAG + index);
}

ADVAPI ULONGLONG WINAPI OpenTraceW(EVENT_TRACE_LOGFILEW_ *logfile)
{
    return open_trace(logfile);
}

/* the ansi record has the same fields with the two names as ansi strings. */
ADVAPI ULONGLONG WINAPI OpenTraceA(EVENT_TRACE_LOGFILEW_ *logfile)
{
    if (!logfile)
        return open_failed(ERROR_INVALID_PARAMETER);
    EVENT_TRACE_LOGFILEW_ wide = *logfile;
    PWSTR file = logfile->LogFileName ? AdvAnsiToWide((PCSTR)logfile->LogFileName) : nullptr;
    PWSTR logger = logfile->LoggerName ? AdvAnsiToWide((PCSTR)logfile->LoggerName) : nullptr;
    wide.LogFileName = file;
    wide.LoggerName = logger;
    ULONGLONG handle = open_trace(&wide);
    DWORD error = GetLastError();
    AdvFree(file);
    AdvFree(logger);
    logfile->LogfileHeader = wide.LogfileHeader;
    SetLastError(error);
    return handle;
}

typedef struct READ_RECORD {
    BOOL Have;
    BYTE *Data;
} READ_RECORD;

/* takes the next event of a consumer into a buffer of the caller's, or says there is none. */
static BOOL next_event(CONSUMER *c, BYTE **buffer, ULONG *capacity, BOOL *finished)
{
    *finished = FALSE;
    if (c->RealTime) {
        ETWS_TABLE *table = etws_table(FALSE);
        if (!table) {
            *finished = TRUE;
            return FALSE;
        }
        etws_lock(table);
        ETWS_SESSION *s = session_by_handle(table, c->Session);
        BOOL got = FALSE;
        if (!s) {
            *finished = TRUE;
        } else {
            if (c->Cursor < s->Floor) {
                c->Cursor = s->Floor;
                s->RealTimeBuffersLost++;
            }
            while (c->Cursor < s->Write && !got) {
                const ETWS_RECORD *record = (const ETWS_RECORD *)&s->Ring[c->Cursor % ETWS_RING];
                ULONG length = record->Length;
                if (record->Kind == 1) {
                    if (length > *capacity) {
                        AdvFree(*buffer);
                        *buffer = AdvAlloc(length);
                        *capacity = *buffer ? length : 0;
                    }
                    if (*buffer) {
                        memcpy(*buffer, record, length);
                        got = TRUE;
                    }
                }
                c->Cursor += length;
            }
        }
        etws_unlock(table);
        return got;
    }
    ETWS_RECORD header;
    DWORD read;
    LARGE_INTEGER at;
    at.QuadPart = c->FileOffset;
    if (!SetFilePointerEx(c->FileHandle, at, nullptr, FILE_BEGIN) ||
        !ReadFile(c->FileHandle, &header, sizeof(header), &read, nullptr) || read != sizeof(header) || header.Length < sizeof(header)) {
        *finished = TRUE;
        return FALSE;
    }
    if (header.Length > *capacity) {
        AdvFree(*buffer);
        *buffer = AdvAlloc(header.Length);
        *capacity = *buffer ? header.Length : 0;
    }
    if (!*buffer)
        return FALSE;
    memcpy(*buffer, &header, sizeof(header));
    if (header.Length > sizeof(header) &&
        (!ReadFile(c->FileHandle, *buffer + sizeof(header), header.Length - sizeof(header), &read, nullptr) || read != header.Length - sizeof(header))) {
        *finished = TRUE;
        return FALSE;
    }
    c->FileOffset += header.Length;
    return TRUE;
}

static void deliver_event(CONSUMER *c, const ETWS_RECORD *r)
{
    const BYTE *payload = (const BYTE *)r + sizeof(*r);
    if (c->RecordCallback) {
        EVENT_RECORD_ event = {};
        event.Size = sizeof(event);
        event.Flags = sizeof(PVOID) == 8 ? 0x40 : 0x20;
        event.ThreadId = r->ThreadId;
        event.ProcessId = r->ProcessId;
        event.TimeStamp = r->Time;
        event.ProviderId = r->Provider;
        event.Id = r->Id;
        event.Version = r->Version;
        event.Channel = r->Channel;
        event.Level = r->Level;
        event.Opcode = r->Opcode;
        event.Task = r->Task;
        event.Keyword = r->Keyword;
        event.ActivityId = r->Activity;
        event.LoggerId = (USHORT)c->Session;
        event.UserDataLength = (USHORT)r->PayloadSize;
        event.UserData = (PVOID)payload;
        event.UserContext = c->Context;
        ((void(WINAPI *)(EVENT_RECORD_ *))c->RecordCallback)(&event);
    } else if (c->EventCallback) {
        EVENT_TRACE_ event = {};
        event.Header.Size = sizeof(event);
        event.Header.Type = r->Opcode;
        event.Header.Level = r->Level;
        event.Header.Version = r->Version;
        event.Header.ThreadId = r->ThreadId;
        event.Header.ProcessId = r->ProcessId;
        event.Header.TimeStamp = r->Time;
        event.Header.Guid = r->Provider;
        event.MofData = (PVOID)payload;
        event.MofLength = r->PayloadSize;
        event.ClientContext = (ULONG)(ULONG_PTR)c->Context;
        ((void(WINAPI *)(EVENT_TRACE_ *))c->EventCallback)(&event);
    }
}

ADVAPI ULONG WINAPI ProcessTrace(PULONGLONG handles, ULONG count, LPFILETIME start, LPFILETIME end)
{
    if (!handles || !count || count > MAX_CONSUMERS) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return ERROR_INVALID_PARAMETER;
    }
    CONSUMER *list[MAX_CONSUMERS];
    AcquireSRWLockExclusive(&g_consumer_lock);
    ULONG error = ERROR_SUCCESS;
    for (ULONG i = 0; i < count && !error; i++) {
        list[i] = consumer_of(handles[i]);
        if (!list[i])
            error = ERROR_INVALID_HANDLE;
        else if (list[i]->Processing || (!list[i]->RecordCallback && !list[i]->EventCallback))
            error = ERROR_INVALID_PARAMETER;
    }
    for (ULONG i = 0; i < count && !error; i++)
        list[i]->Processing = TRUE, list[i]->Done = FALSE;
    ReleaseSRWLockExclusive(&g_consumer_lock);
    if (error) {
        SetLastError(error);
        return error;
    }
    BYTE *buffer = nullptr;
    ULONG capacity = 0;
    BOOL cancelled = FALSE;
    for (;;) {
        BOOL any = FALSE, active = FALSE;
        for (ULONG i = 0; i < count; i++) {
            CONSUMER *c = list[i];
            if (c->Done)
                continue;
            if (c->Closed) {
                c->Done = cancelled = TRUE;
                continue;
            }
            BOOL finished;
            while (next_event(c, &buffer, &capacity, &finished)) {
                any = TRUE;
                const ETWS_RECORD *r = (const ETWS_RECORD *)buffer;
                FILETIME time = {(DWORD)r->Time, (DWORD)(r->Time >> 32)};
                if ((start && CompareFileTime(&time, start) < 0) || (end && CompareFileTime(&time, end) > 0))
                    continue;
                deliver_event(c, r);
                if (c->Closed)
                    break;
            }
            if (finished)
                c->Done = TRUE;
            else
                active = TRUE;
        }
        if (!active)
            break;
        if (!any) {
            LARGE_INTEGER interval;
            interval.QuadPart = -100000;
            NtDelayExecution(FALSE, &interval);
        }
    }
    AdvFree(buffer);
    AcquireSRWLockExclusive(&g_consumer_lock);
    for (ULONG i = 0; i < count; i++) {
        list[i]->Processing = FALSE;
        if (list[i]->Closed) {
            if (list[i]->FileHandle != INVALID_HANDLE_VALUE && list[i]->FileHandle)
                CloseHandle(list[i]->FileHandle);
            list[i]->Used = FALSE;
            cancelled = TRUE;
        }
    }
    ReleaseSRWLockExclusive(&g_consumer_lock);
    return cancelled ? ERROR_CANCELLED_ : ERROR_SUCCESS;
}

ADVAPI ULONG WINAPI CloseTrace(ULONGLONG handle)
{
    AcquireSRWLockExclusive(&g_consumer_lock);
    CONSUMER *c = consumer_of(handle);
    ULONG error = ERROR_SUCCESS;
    if (!c) {
        error = ERROR_INVALID_HANDLE;
    } else if (c->Processing) {
        c->Closed = TRUE;
        error = ERROR_CTX_CLOSE_PENDING_;
    } else {
        if (c->FileHandle != INVALID_HANDLE_VALUE && c->FileHandle)
            CloseHandle(c->FileHandle);
        c->Used = FALSE;
    }
    ReleaseSRWLockExclusive(&g_consumer_lock);
    if (error)
        SetLastError(error);
    return error;
}

/* the partition a consumer reads from: the default one, which has no parent. */
ADVAPI ULONG WINAPI QueryTraceProcessingHandle(ULONGLONG handle, ULONG klass, PVOID in, ULONG in_size, PVOID out, ULONG out_size,
                                               PULONG returned)
{
    ULONG error = ERROR_SUCCESS;
    (void)in;
    (void)in_size;
    AcquireSRWLockShared(&g_consumer_lock);
    BOOL valid = consumer_of(handle) != nullptr;
    ReleaseSRWLockShared(&g_consumer_lock);
    if (!valid)
        error = ERROR_INVALID_HANDLE;
    else if (klass != 1 || !returned)
        error = ERROR_INVALID_PARAMETER;
    else {
        *returned = 48;
        if (out_size < 48 || !out)
            error = ERROR_INSUFFICIENT_BUFFER;
        else
            memset(out, 0, 48);
    }
    if (error)
        SetLastError(error);
    return error;
}

/* the callbacks of the guids of old style events. */
static struct {
    GUID Guid;
    PVOID Callback;
} g_callbacks[16];

ADVAPI ULONG WINAPI SetTraceCallback(const GUID *guid, PVOID callback)
{
    if (!guid || !callback) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return ERROR_INVALID_PARAMETER;
    }
    ULONG slot = RTL_NUMBER_OF(g_callbacks);
    AcquireSRWLockExclusive(&g_consumer_lock);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_callbacks); i++) {
        if (g_callbacks[i].Callback && !memcmp(&g_callbacks[i].Guid, guid, sizeof(GUID))) {
            slot = i;
            break;
        }
        if (!g_callbacks[i].Callback && slot == RTL_NUMBER_OF(g_callbacks))
            slot = i;
    }
    if (slot < RTL_NUMBER_OF(g_callbacks)) {
        g_callbacks[slot].Guid = *guid;
        g_callbacks[slot].Callback = callback;
    }
    ReleaseSRWLockExclusive(&g_consumer_lock);
    if (slot == RTL_NUMBER_OF(g_callbacks)) {
        SetLastError(ERROR_NO_SYSTEM_RESOURCES);
        return ERROR_NO_SYSTEM_RESOURCES;
    }
    return ERROR_SUCCESS;
}

ADVAPI ULONG WINAPI RemoveTraceCallback(const GUID *guid)
{
    if (!guid) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return ERROR_INVALID_PARAMETER;
    }
    ULONG error = ERROR_WMI_INSTANCE_NOT_FOUND_;
    AcquireSRWLockExclusive(&g_consumer_lock);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_callbacks); i++)
        if (g_callbacks[i].Callback && !memcmp(&g_callbacks[i].Guid, guid, sizeof(GUID))) {
            g_callbacks[i].Callback = nullptr;
            error = ERROR_SUCCESS;
        }
    ReleaseSRWLockExclusive(&g_consumer_lock);
    if (error)
        SetLastError(error);
    return error;
}

/* the buffers of a consumer that reads buffers it is given are the buffers of windows' logs. */
ADVAPI ULONG WINAPI ProcessTraceAddBufferToBufferStream(PVOID a, PVOID b, ULONG c, PVOID d, ULONG e)
{
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    SetLastError(a ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
    return a ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

ADVAPI ULONG WINAPI ProcessTraceBufferIncrementReference(PVOID a, PVOID b)
{
    (void)b;
    SetLastError(a ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
    return a ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

ADVAPI ULONG WINAPI ProcessTraceBufferDecrementReference(PVOID a)
{
    SetLastError(a ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER);
    return a ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}
