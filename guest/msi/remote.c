#include "msi_private.h"

/*
 * the host for custom action libraries the installer cannot load: 32-bit libraries under a 64-bit installer. the
 * installer starts the 32-bit msiexec.exe with a pipe name and asks it to load the library and run its entry point for
 * a session. the library calls msi.dll as usual, and the host's msi.dll, which holds no installation, forwards each
 * call over the pipe to the installer, which runs it on the real handles and sends back the results. both sides
 * share the table below, so a function is known by its index.
 *
 * a message is a length, a kind and the content. a call holds the function number and its arguments in signature
 * order, a reply the result and the outputs in the same order. every value is written as 32-bit numbers, so the two
 * sides need not share a processor. a signature has one letter per argument: i a number or handle, W an input string,
 * M a number or an input string, f an input time, o an output number, H an output handle, F an output time, T an
 * output string the length of a product code, B and S an output string and its in/out buffer size, b and S the same
 * for bytes. an output the caller leaves out is left out in the call, and one the function does not set keeps the
 * value the caller had.
 *
 * the handles a call returns belong to the installer, which keeps them until the library closes them or its call ends.
 */

enum { MESSAGE_CALL = 1, MESSAGE_REPLY, MESSAGE_RUN, MESSAGE_RESULT, MESSAGE_QUIT };

typedef UINT_PTR (WINAPI *PROXY_FUNCTION)(UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);

typedef struct PROXY_ENTRY {
    PCSTR Name, Signature;
    PVOID Function;
    /* the result is a handle. */
    BOOL Handle;
} PROXY_ENTRY;

static UINT_PTR WINAPI close_given_handles(void);

static const PROXY_ENTRY g_table[PROXY_COUNT] = {
    [PROXY_GetPropertyW] = {"MsiGetPropertyW", "iWBS", MsiGetPropertyW},
    [PROXY_SetPropertyW] = {"MsiSetPropertyW", "iWW", MsiSetPropertyW},
    [PROXY_RecordSetStringW] = {"MsiRecordSetStringW", "iiW", MsiRecordSetStringW},
    [PROXY_CreateRecord] = {"MsiCreateRecord", "i", MsiCreateRecord, TRUE},
    [PROXY_ProcessMessage] = {"MsiProcessMessage", "iii", MsiProcessMessage},
    [PROXY_ViewExecute] = {"MsiViewExecute", "ii", MsiViewExecute},
    [PROXY_ViewFetch] = {"MsiViewFetch", "iH", MsiViewFetch},
    [PROXY_EnumRelatedProductsW] = {"MsiEnumRelatedProductsW", "WiiT", MsiEnumRelatedProductsW},
    [PROXY_GetActiveDatabase] = {"MsiGetActiveDatabase", "i", MsiGetActiveDatabase, TRUE},
    [PROXY_GetProductInfoW] = {"MsiGetProductInfoW", "WWBS", MsiGetProductInfoW},
    [PROXY_CloseHandle] = {"MsiCloseHandle", "i", MsiCloseHandle},
    [PROXY_RecordGetStringW] = {"MsiRecordGetStringW", "iiBS", MsiRecordGetStringW},
    [PROXY_DatabaseOpenViewW] = {"MsiDatabaseOpenViewW", "iWH", MsiDatabaseOpenViewW},
    [PROXY_SetMode] = {"MsiSetMode", "iii", MsiSetMode},
    [PROXY_EvaluateConditionW] = {"MsiEvaluateConditionW", "iW", MsiEvaluateConditionW},
    [PROXY_GetMode] = {"MsiGetMode", "ii", MsiGetMode},
    [PROXY_RecordIsNull] = {"MsiRecordIsNull", "ii", MsiRecordIsNull},
    [PROXY_GetSourcePathW] = {"MsiGetSourcePathW", "iWBS", MsiGetSourcePathW},
    [PROXY_GetTargetPathW] = {"MsiGetTargetPathW", "iWBS", MsiGetTargetPathW},
    [PROXY_DoActionW] = {"MsiDoActionW", "iW", MsiDoActionW},
    [PROXY_FormatRecordW] = {"MsiFormatRecordW", "iiBS", MsiFormatRecordW},
    [PROXY_ViewGetErrorW] = {"MsiViewGetErrorW", "iBS", MsiViewGetErrorW},
    [PROXY_ViewModify] = {"MsiViewModify", "iii", MsiViewModify},
    [PROXY_ViewGetColumnInfo] = {"MsiViewGetColumnInfo", "iiH", MsiViewGetColumnInfo},
    [PROXY_DatabaseGetPrimaryKeysW] = {"MsiDatabaseGetPrimaryKeysW", "iWH", MsiDatabaseGetPrimaryKeysW},
    [PROXY_RecordSetInteger] = {"MsiRecordSetInteger", "iii", MsiRecordSetInteger},
    [PROXY_RecordGetInteger] = {"MsiRecordGetInteger", "ii", MsiRecordGetInteger},
    [PROXY_RecordReadStream] = {"MsiRecordReadStream", "iibS", MsiRecordReadStream},
    [PROXY_GetComponentStateW] = {"MsiGetComponentStateW", "iWoo", MsiGetComponentStateW},
    [PROXY_GetFeatureStateW] = {"MsiGetFeatureStateW", "iWoo", MsiGetFeatureStateW},
    [PROXY_SetFeatureStateW] = {"MsiSetFeatureStateW", "iWi", MsiSetFeatureStateW},
    [PROXY_SetComponentStateW] = {"MsiSetComponentStateW", "iWi", MsiSetComponentStateW},
    [PROXY_SetFeatureAttributesW] = {"MsiSetFeatureAttributesW", "iWi", MsiSetFeatureAttributesW},
    [PROXY_GetFeatureCostW] = {"MsiGetFeatureCostW", "iWiio", MsiGetFeatureCostW},
    [PROXY_EnumComponentCostsW] = {"MsiEnumComponentCostsW", "iWiiBSoo", MsiEnumComponentCostsW},
    [PROXY_GetFeatureValidStatesW] = {"MsiGetFeatureValidStatesW", "iWo", MsiGetFeatureValidStatesW},
    [PROXY_SetInstallLevel] = {"MsiSetInstallLevel", "ii", MsiSetInstallLevel},
    [PROXY_SetTargetPathW] = {"MsiSetTargetPathW", "iWW", MsiSetTargetPathW},
    [PROXY_GetLanguage] = {"MsiGetLanguage", "i", MsiGetLanguage},
    [PROXY_VerifyDiskSpace] = {"MsiVerifyDiskSpace", "i", MsiVerifyDiskSpace},
    [PROXY_SequenceW] = {"MsiSequenceW", "iWi", MsiSequenceW},
    [PROXY_GetLastErrorRecord] = {"MsiGetLastErrorRecord", "", MsiGetLastErrorRecord, TRUE},
    [PROXY_DatabaseIsTablePersistentW] = {"MsiDatabaseIsTablePersistentW", "iW", MsiDatabaseIsTablePersistentW},
    [PROXY_DatabaseCommit] = {"MsiDatabaseCommit", "i", MsiDatabaseCommit},
    [PROXY_OpenDatabaseW] = {"MsiOpenDatabaseW", "WMH", MsiOpenDatabaseW},
    [PROXY_GetSummaryInformationW] = {"MsiGetSummaryInformationW", "iWiH", MsiGetSummaryInformationW},
    [PROXY_SummaryInfoGetPropertyCount] = {"MsiSummaryInfoGetPropertyCount", "io", MsiSummaryInfoGetPropertyCount},
    [PROXY_SummaryInfoGetPropertyW] = {"MsiSummaryInfoGetPropertyW", "iiooFBS", MsiSummaryInfoGetPropertyW},
    [PROXY_SummaryInfoSetPropertyW] = {"MsiSummaryInfoSetPropertyW", "iiiifW", MsiSummaryInfoSetPropertyW},
    [PROXY_SummaryInfoPersist] = {"MsiSummaryInfoPersist", "i", MsiSummaryInfoPersist},
    [PROXY_RecordGetFieldCount] = {"MsiRecordGetFieldCount", "i", MsiRecordGetFieldCount},
    [PROXY_RecordDataSize] = {"MsiRecordDataSize", "ii", MsiRecordDataSize},
    [PROXY_RecordClearData] = {"MsiRecordClearData", "i", MsiRecordClearData},
    [PROXY_RecordSetStreamW] = {"MsiRecordSetStreamW", "iiW", MsiRecordSetStreamW},
    [PROXY_ViewClose] = {"MsiViewClose", "i", MsiViewClose},
    [PROXY_CloseAllHandles] = {"MsiCloseAllHandles", "", close_given_handles},
};

/* ------------------------------------------------------------------ messages */

typedef struct MESSAGE {
    BYTE *Data;
    SIZE_T Length, Capacity, Position;
    BOOL Failed;
} MESSAGE;

static void message_reserve(MESSAGE *message, SIZE_T more)
{
    if (message->Failed || message->Length + more <= message->Capacity)
        return;
    SIZE_T capacity = message->Capacity ? message->Capacity : 256;
    while (capacity < message->Length + more)
        capacity *= 2;
    BYTE *bigger = MimeMsiAllocate(capacity);
    if (!bigger) {
        message->Failed = TRUE;
        return;
    }
    if (message->Length)
        memcpy(bigger, message->Data, message->Length);
    MimeMsiFree(message->Data);
    message->Data = bigger;
    message->Capacity = capacity;
}

static void put_bytes(MESSAGE *message, const void *data, SIZE_T length)
{
    message_reserve(message, length);
    if (message->Failed)
        return;
    if (length)
        memcpy(message->Data + message->Length, data, length);
    message->Length += length;
}

static void put_u32(MESSAGE *message, ULONG value)
{
    put_bytes(message, &value, sizeof(value));
}

static void put_text(MESSAGE *message, PCWSTR text)
{
    put_u32(message, text ? 1 : 0);
    if (text) {
        ULONG length = (ULONG)wcslen(text);
        put_u32(message, length);
        put_bytes(message, text, length * sizeof(WCHAR));
    }
}

static ULONG get_u32(MESSAGE *message)
{
    ULONG value = 0;
    if (message->Position + sizeof(value) > message->Length) {
        message->Failed = TRUE;
        return 0;
    }
    memcpy(&value, message->Data + message->Position, sizeof(value));
    message->Position += sizeof(value);
    return value;
}

static const BYTE *get_bytes(MESSAGE *message, SIZE_T length)
{
    if (message->Position + length > message->Length) {
        message->Failed = TRUE;
        return nullptr;
    }
    const BYTE *at = message->Data + message->Position;
    message->Position += length;
    return at;
}

/* returns an allocated copy of the next string, or NULL when it was absent. */
static PWSTR get_text(MESSAGE *message)
{
    if (!get_u32(message))
        return nullptr;
    ULONG length = get_u32(message);
    const BYTE *source = get_bytes(message, (SIZE_T)length * sizeof(WCHAR));
    if (!source)
        return nullptr;
    PWSTR text = MimeMsiAllocate(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (text)
        memcpy(text, source, (SIZE_T)length * sizeof(WCHAR));
    else
        message->Failed = TRUE;
    return text;
}

static BOOL write_all(HANDLE pipe, const void *data, SIZE_T length)
{
    const BYTE *p = data;
    while (length) {
        DWORD wanted = length < 0x10000 ? (DWORD)length : 0x10000, written;
        if (!WriteFile(pipe, p, wanted, &written, nullptr) || !written)
            return FALSE;
        p += written;
        length -= written;
    }
    return TRUE;
}

static BOOL read_all(HANDLE pipe, void *data, SIZE_T length)
{
    BYTE *p = data;
    while (length) {
        DWORD wanted = length < 0x10000 ? (DWORD)length : 0x10000, read;
        if (!ReadFile(pipe, p, wanted, &read, nullptr) || !read)
            return FALSE;
        p += read;
        length -= read;
    }
    return TRUE;
}

/* the kind and the content of a message are sent after its length. */
static BOOL send_message(HANDLE pipe, ULONG kind, const MESSAGE *content)
{
    ULONG header[2] = {(ULONG)(content ? content->Length : 0) + 4, kind};
    return write_all(pipe, header, sizeof(header)) && (!content || write_all(pipe, content->Data, content->Length));
}

static BOOL receive_message(HANDLE pipe, ULONG *kind, MESSAGE *content)
{
    ULONG header[2];
    memset(content, 0, sizeof(*content));
    if (!read_all(pipe, header, sizeof(header)) || header[0] < 4 || header[0] > 64 * 1024 * 1024)
        return FALSE;
    *kind = header[1];
    SIZE_T length = header[0] - 4;
    content->Data = MimeMsiAllocate(length ? length : 1);
    if (!content->Data || !read_all(pipe, content->Data, length)) {
        MimeMsiFree(content->Data);
        content->Data = nullptr;
        return FALSE;
    }
    content->Length = content->Capacity = length;
    return TRUE;
}

/* ------------------------------------------------------------------ library side */

static HANDLE g_client_pipe;
static SRWLOCK g_client_lock = SRWLOCK_INIT;

BOOL MimeMsiProxyActive(void)
{
    return g_client_pipe != nullptr;
}

/* what a call returns when it could not be made, for the functions whose result is no error code. */
static UINT_PTR failure_result(ULONG id)
{
    switch (id) {
    case PROXY_GetMode: case PROXY_GetLanguage: case PROXY_GetLastErrorRecord: case PROXY_CreateRecord:
    case PROXY_GetActiveDatabase: case PROXY_RecordGetFieldCount: case PROXY_RecordDataSize: case PROXY_RecordIsNull:
        return 0;
    case PROXY_RecordGetInteger: return (UINT_PTR)MSI_NULL_INTEGER;
    case PROXY_DatabaseIsTablePersistentW: return MSICONDITION_ERROR;
    default: return ERROR_FUNCTION_FAILED;
    }
}

UINT_PTR MimeMsiProxyCall(ULONG id, ULONG count, const UINT_PTR *arguments)
{
    UINT_PTR args[8] = {};
    for (ULONG i = 0; i < count && i < RTL_NUMBER_OF(args); i++)
        args[i] = arguments[i];
    const PROXY_ENTRY *entry = &g_table[id];
    MESSAGE request = {}, reply = {};
    ULONG kind;
    UINT_PTR result = failure_result(id);
    DWORD sizes[2] = {};
    ULONG size_count = 0;
    put_u32(&request, id);
    ULONG a = 0;
    for (PCSTR p = entry->Signature; *p; p++) {
        switch (*p) {
        case 'i': put_u32(&request, (ULONG)args[a++]); break;
        case 'W': put_text(&request, (PCWSTR)args[a++]); break;
        case 'M': {
            UINT_PTR value = args[a++];
            put_u32(&request, value < 0x10000 ? 1 : 2);
            if (value < 0x10000)
                put_u32(&request, (ULONG)value);
            else
                put_text(&request, (PCWSTR)value);
            break;
        }
        case 'f': {
            const FILETIME *time = (const FILETIME *)args[a++];
            put_u32(&request, time != nullptr);
            if (time) {
                put_u32(&request, time->dwLowDateTime);
                put_u32(&request, time->dwHighDateTime);
            }
            break;
        }
        case 'o': case 'H': {
            const ULONG *value = (const ULONG *)args[a++];
            put_u32(&request, value != nullptr);
            put_u32(&request, value ? *value : 0);
            break;
        }
        case 'F': {
            const FILETIME *time = (const FILETIME *)args[a++];
            put_u32(&request, time != nullptr);
            put_u32(&request, time ? time->dwLowDateTime : 0);
            put_u32(&request, time ? time->dwHighDateTime : 0);
            break;
        }
        case 'T': a++; break;
        case 'B': case 'b': put_u32(&request, args[a++] != 0); break;
        case 'S': {
            DWORD *size = (DWORD *)args[a++];
            if (size_count < RTL_NUMBER_OF(sizes))
                sizes[size_count++] = size ? *size : 0;
            put_u32(&request, size != nullptr);
            put_u32(&request, size ? *size : 0);
            break;
        }
        }
    }
    ULONG s_index = 0;
    AcquireSRWLockExclusive(&g_client_lock);
    BOOL ok = !request.Failed && send_message(g_client_pipe, MESSAGE_CALL, &request) &&
              receive_message(g_client_pipe, &kind, &reply) && kind == MESSAGE_REPLY;
    ReleaseSRWLockExclusive(&g_client_lock);
    MimeMsiFree(request.Data);
    if (ok) {
        result = get_u32(&reply);
        a = 0;
        PVOID buffer = nullptr;
        BOOL buffer_bytes = FALSE;
        for (PCSTR p = entry->Signature; *p; p++) {
            switch (*p) {
            case 'i': case 'W': case 'M': case 'f': a++; break;
            case 'o': case 'H': {
                ULONG value = get_u32(&reply);
                if (args[a])
                    *(ULONG *)args[a] = value;
                a++;
                break;
            }
            case 'F': {
                ULONG low = get_u32(&reply), high = get_u32(&reply);
                if (args[a]) {
                    ((FILETIME *)args[a])->dwLowDateTime = low;
                    ((FILETIME *)args[a])->dwHighDateTime = high;
                }
                a++;
                break;
            }
            case 'T': {
                const BYTE *text = get_bytes(&reply, 39 * sizeof(WCHAR));
                if (text && args[a])
                    memcpy((PVOID)args[a], text, 39 * sizeof(WCHAR));
                a++;
                break;
            }
            case 'B': case 'b':
                buffer_bytes = *p == 'b';
                buffer = (PVOID)args[a++];
                break;
            case 'S': {
                DWORD *size = (DWORD *)args[a++];
                ULONG value = get_u32(&reply);
                ULONG bytes = get_u32(&reply);
                const BYTE *data = get_bytes(&reply, bytes);
                /* no more than the caller's buffer holds: a size of 0 asks only for the length, with a buffer that may be
                   a constant. */
                ULONGLONG capacity = size ? (ULONGLONG)sizes[s_index] * (buffer_bytes ? 1 : sizeof(WCHAR)) : 0;
                s_index++;
                if (bytes > capacity)
                    bytes = (ULONG)capacity;
                if (buffer && data && bytes)
                    memcpy(buffer, data, bytes);
                if (size)
                    *size = value;
                buffer = nullptr;
                break;
            }
            }
        }
        if (reply.Failed)
            result = failure_result(id);
    }
    MimeMsiFree(reply.Data);
    return result;
}

/* runs in the host: loads the requested libraries and runs their entry points for an installer session. */
UINT WINAPI MsiCustomActionServer(LPCWSTR pipe_name)
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (ULONG attempt = 0; attempt < 300 && pipe == INVALID_HANDLE_VALUE; attempt++) {
        pipe = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
            WaitNamedPipeW(pipe_name, 100);
    }
    if (pipe == INVALID_HANDLE_VALUE)
        return ERROR_BROKEN_PIPE;
    g_client_pipe = pipe;
    UINT status = ERROR_SUCCESS;
    for (;;) {
        MESSAGE request;
        ULONG kind;
        if (!receive_message(pipe, &kind, &request))
            break;
        if (kind == MESSAGE_QUIT) {
            MimeMsiFree(request.Data);
            break;
        }
        if (kind != MESSAGE_RUN) {
            MimeMsiFree(request.Data);
            status = ERROR_INVALID_DATA;
            break;
        }
        MSIHANDLE session = get_u32(&request);
        PWSTR path = get_text(&request), entry_name = get_text(&request);
        /* a library asked to register itself is called with nothing, and answers with an HRESULT. */
        BOOL registering = get_u32(&request) != 0;
        ULONG load_error = ERROR_SUCCESS, result = 0;
        HMODULE library = path ? LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
        char entry[256] = {};
        if (entry_name)
            WideCharToMultiByte(CP_ACP, 0, entry_name, -1, entry, sizeof(entry), nullptr, nullptr);
        typedef UINT (WINAPI *CA_ENTRY)(MSIHANDLE);
        CA_ENTRY function = library ? (CA_ENTRY)GetProcAddress(library, entry) : nullptr;
        if (!library) {
            load_error = path ? GetLastError() : ERROR_INVALID_DATA;
            DbgPrint("msi: the custom action library %ws does not load, error %u\n", path ? path : L"", load_error);
        }
        else if (!function)
            load_error = ERROR_PROC_NOT_FOUND;
        else if (registering)
            result = (ULONG)((HRESULT (WINAPI *)(void))function)();
        else
            result = function(session);
        if (library)
            FreeLibrary(library);
        MimeMsiFree(path);
        MimeMsiFree(entry_name);
        MimeMsiFree(request.Data);
        MESSAGE reply = {};
        put_u32(&reply, load_error);
        put_u32(&reply, result);
        AcquireSRWLockExclusive(&g_client_lock);
        BOOL sent = send_message(pipe, MESSAGE_RESULT, &reply);
        ReleaseSRWLockExclusive(&g_client_lock);
        MimeMsiFree(reply.Data);
        if (!sent)
            break;
    }
    g_client_pipe = nullptr;
    CloseHandle(pipe);
    return status;
}

/* ------------------------------------------------------------------ installer side */

/* a host for each processor a library may be built for, the 64-bit one first. */
enum { HOST_NATIVE64, HOST_X86, HOSTS };
static HANDLE g_host_pipe[HOSTS], g_host_process[HOSTS];
static SRWLOCK g_host_lock = SRWLOCK_INIT;

static void shut_down_host(ULONG host)
{
    if (g_host_pipe[host]) {
        send_message(g_host_pipe[host], MESSAGE_QUIT, nullptr);
        CloseHandle(g_host_pipe[host]);
        g_host_pipe[host] = nullptr;
    }
    if (g_host_process[host]) {
        if (WaitForSingleObject(g_host_process[host], 2000) == WAIT_TIMEOUT)
            TerminateProcess(g_host_process[host], 0);
        CloseHandle(g_host_process[host]);
        g_host_process[host] = nullptr;
    }
}

void MimeMsiShutDownHost(void)
{
    AcquireSRWLockExclusive(&g_host_lock);
    for (ULONG host = 0; host < HOSTS; host++)
        shut_down_host(host);
    ReleaseSRWLockExclusive(&g_host_lock);
}

typedef struct HOST_WATCH {
    HANDLE Process;
    PCWSTR Pipe;
    volatile LONG Connected;
} HOST_WATCH;

/* wakes the pending connect if the host dies before connecting. */
static DWORD WINAPI watch_host(PVOID parameter)
{
    HOST_WATCH *watch = parameter;
    for (ULONG waited = 0; waited < 600 && !watch->Connected; waited++) {
        if (WaitForSingleObject(watch->Process, 100) != WAIT_TIMEOUT)
            break;
    }
    if (!watch->Connected) {
        HANDLE client = CreateFileW(watch->Pipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (client != INVALID_HANDLE_VALUE)
            CloseHandle(client);
    }
    return 0;
}

/* the folder of the msiexec.exe built for a host. */
static UINT host_directory(ULONG host, WCHAR directory[MAX_PATH])
{
    UINT length = host == HOST_X86 ? GetSystemWow64DirectoryW(directory, MAX_PATH) : GetSystemDirectoryW(directory, MAX_PATH);
    return !length || length + 16 >= MAX_PATH ? ERROR_NOT_SUPPORTED : ERROR_SUCCESS;
}

static UINT start_host(ULONG host)
{
    if (g_host_pipe[host])
        return ERROR_SUCCESS;
    WCHAR name[96], directory[MAX_PATH], command[MAX_PATH + 160];
    _snwprintf(name, RTL_NUMBER_OF(name), L"\\\\.\\pipe\\msihost-%u-%u-%u", GetCurrentProcessId(), host, GetTickCount());
    UINT error = host_directory(host, directory);
    if (error)
        return error;
    HANDLE pipe = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536,
                                   0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        return GetLastError();
    _snwprintf(command, RTL_NUMBER_OF(command), L"\"%s\\msiexec.exe\" /ca-server %s", directory, name);
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    /* a 32-bit installer starts the native host from the real System32. */
    PVOID redirection = nullptr;
    BOOL unredirected = host == HOST_NATIVE64 && Wow64DisableWow64FsRedirection(&redirection);
    BOOL started = CreateProcessW(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process);
    error = started ? ERROR_SUCCESS : GetLastError();
    if (unredirected)
        Wow64RevertWow64FsRedirection(redirection);
    if (!started) {
        CloseHandle(pipe);
        return error;
    }
    CloseHandle(process.hThread);
    HOST_WATCH watch = {process.hProcess, name, 0};
    HANDLE watcher = CreateThread(nullptr, 0, watch_host, &watch, 0, nullptr);
    BOOL connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
    watch.Connected = TRUE;
    if (watcher) {
        WaitForSingleObject(watcher, INFINITE);
        CloseHandle(watcher);
    }
    /* the client may be the watcher rather than the host, so a dead host is a failure. */
    if (!connected || WaitForSingleObject(process.hProcess, 0) != WAIT_TIMEOUT) {
        CloseHandle(pipe);
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hProcess);
        return ERROR_BROKEN_PIPE;
    }
    g_host_pipe[host] = pipe;
    g_host_process[host] = process.hProcess;
    return ERROR_SUCCESS;
}

/* the handles the library was given, which stay the installer's until the library closes them or its call ends. */
static MSIHANDLE *g_given;
static ULONG g_given_count, g_given_capacity;

static void give_handle(MSIHANDLE handle)
{
    if (!handle)
        return;
    if (g_given_count == g_given_capacity) {
        ULONG capacity = g_given_capacity ? g_given_capacity * 2 : 32;
        MSIHANDLE *bigger = MimeMsiAllocate((SIZE_T)capacity * sizeof(*bigger));
        if (!bigger)
            return;
        if (g_given_count)
            memcpy(bigger, g_given, (SIZE_T)g_given_count * sizeof(*bigger));
        MimeMsiFree(g_given);
        g_given = bigger;
        g_given_capacity = capacity;
    }
    g_given[g_given_count++] = handle;
}

static void take_handle(MSIHANDLE handle)
{
    for (ULONG i = 0; i < g_given_count; i++) {
        if (g_given[i] == handle) {
            g_given[i] = g_given[--g_given_count];
            return;
        }
    }
}

static UINT_PTR WINAPI close_given_handles(void)
{
    while (g_given_count)
        MsiCloseHandle(g_given[--g_given_count]);
    return ERROR_SUCCESS;
}

/*
 * calls the function with as many arguments as its signature has. a function that takes fewer may not be called with
 * more: on x86 it removes its own arguments from the stack.
 */
static UINT_PTR call_proxy(const PROXY_ENTRY *entry, const UINT_PTR *a)
{
    typedef UINT_PTR(WINAPI * F0)(void);
    typedef UINT_PTR(WINAPI * F1)(UINT_PTR);
    typedef UINT_PTR(WINAPI * F2)(UINT_PTR, UINT_PTR);
    typedef UINT_PTR(WINAPI * F3)(UINT_PTR, UINT_PTR, UINT_PTR);
    typedef UINT_PTR(WINAPI * F4)(UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);
    typedef UINT_PTR(WINAPI * F5)(UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);
    typedef UINT_PTR(WINAPI * F6)(UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);
    typedef UINT_PTR(WINAPI * F7)(UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);
    typedef UINT_PTR(WINAPI * F8)(UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR, UINT_PTR);
    PVOID f = entry->Function;
    switch (strlen(entry->Signature)) {
    case 0: return ((F0)f)();
    case 1: return ((F1)f)(a[0]);
    case 2: return ((F2)f)(a[0], a[1]);
    case 3: return ((F3)f)(a[0], a[1], a[2]);
    case 4: return ((F4)f)(a[0], a[1], a[2], a[3]);
    case 5: return ((F5)f)(a[0], a[1], a[2], a[3], a[4]);
    case 6: return ((F6)f)(a[0], a[1], a[2], a[3], a[4], a[5]);
    case 7: return ((F7)f)(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
    default: return ((F8)f)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    }
}

/* runs a call from the library on the installer's real handles. */
static BOOL serve_call(MESSAGE *request, MESSAGE *reply)
{
    ULONG id = get_u32(request);
    if (request->Failed || id >= PROXY_COUNT)
        return FALSE;
    const PROXY_ENTRY *entry = &g_table[id];
    UINT_PTR args[8] = {};
    ULONG outputs[4] = {}, sizes[2] = {};
    FILETIME input_times[2] = {}, output_times[2] = {};
    WCHAR product[40] = {};
    PWSTR strings[4] = {};
    BOOL has_size[2] = {}, present[2] = {}, bytes[2] = {}, handle_output[4] = {};
    ULONG buffer_argument[2] = {};
    PVOID buffers[2] = {};
    ULONG a = 0, output_count = 0, string_count = 0, buffer_count = 0, size_count = 0, input_time_count = 0,
          output_time_count = 0;
    for (PCSTR p = entry->Signature; *p && !request->Failed && a < RTL_NUMBER_OF(args); p++) {
        switch (*p) {
        case 'i': args[a++] = (UINT_PTR)(INT_PTR)(int)get_u32(request); break;
        case 'W':
            if (string_count >= RTL_NUMBER_OF(strings)) {
                request->Failed = TRUE;
                break;
            }
            strings[string_count] = get_text(request);
            args[a++] = (UINT_PTR)strings[string_count++];
            break;
        case 'M':
            if (get_u32(request) == 1) {
                args[a++] = get_u32(request);
            } else if (string_count < RTL_NUMBER_OF(strings)) {
                strings[string_count] = get_text(request);
                args[a++] = (UINT_PTR)strings[string_count++];
            } else
                request->Failed = TRUE;
            break;
        case 'f':
            if (input_time_count >= RTL_NUMBER_OF(input_times)) {
                request->Failed = TRUE;
                break;
            }
            if (get_u32(request)) {
                input_times[input_time_count].dwLowDateTime = get_u32(request);
                input_times[input_time_count].dwHighDateTime = get_u32(request);
                args[a++] = (UINT_PTR)&input_times[input_time_count];
            } else
                args[a++] = 0;
            input_time_count++;
            break;
        case 'o': case 'H': {
            if (output_count >= RTL_NUMBER_OF(outputs)) {
                request->Failed = TRUE;
                break;
            }
            BOOL given = get_u32(request) != 0;
            outputs[output_count] = get_u32(request);
            handle_output[output_count] = *p == 'H';
            args[a++] = given ? (UINT_PTR)&outputs[output_count] : 0;
            output_count++;
            break;
        }
        case 'F':
            if (output_time_count >= RTL_NUMBER_OF(output_times)) {
                request->Failed = TRUE;
                break;
            }
            {
                BOOL given = get_u32(request) != 0;
                output_times[output_time_count].dwLowDateTime = get_u32(request);
                output_times[output_time_count].dwHighDateTime = get_u32(request);
                args[a++] = given ? (UINT_PTR)&output_times[output_time_count] : 0;
                output_time_count++;
            }
            break;
        case 'T': args[a++] = (UINT_PTR)product; break;
        case 'B': case 'b':
            present[buffer_count] = get_u32(request) != 0;
            bytes[buffer_count] = *p == 'b';
            buffer_argument[buffer_count++] = a++;
            break;
        case 'S':
            has_size[size_count] = get_u32(request) != 0;
            sizes[size_count] = get_u32(request);
            args[a++] = has_size[size_count] ? (UINT_PTR)&sizes[size_count] : 0;
            size_count++;
            break;
        }
    }
    /* buffers are as large as the caller's size, plus room for a terminator. a string buffer starts with a mark that
       a function which writes nothing leaves in place. */
    ULONG capacities[2] = {};
    for (ULONG k = 0; k < buffer_count && !request->Failed; k++) {
        if (!present[k])
            continue;
        SIZE_T capacity = k < size_count && has_size[k] ? sizes[k] : 0;
        capacities[k] = (ULONG)capacity;
        buffers[k] = MimeMsiAllocate((capacity + 1) * (bytes[k] ? 1 : sizeof(WCHAR)) + sizeof(WCHAR));
        if (!buffers[k])
            request->Failed = TRUE;
        else if (!bytes[k])
            *(WCHAR *)buffers[k] = 0xFFFF;
        args[buffer_argument[k]] = (UINT_PTR)buffers[k];
    }
    UINT_PTR result = ERROR_FUNCTION_FAILED;
    if (!request->Failed)
        result = call_proxy(entry, args);
    if (!request->Failed) {
        if (entry->Handle && (ULONG)result)
            give_handle((MSIHANDLE)result);
        for (ULONG i = 0; i < output_count; i++)
            if (handle_output[i] && (ULONG)result == ERROR_SUCCESS && outputs[i])
                give_handle(outputs[i]);
        if (id == PROXY_CloseHandle && (ULONG)result == ERROR_SUCCESS)
            take_handle((MSIHANDLE)args[0]);
    }
    put_u32(reply, (ULONG)result);
    ULONG o = 0, t = 0, b = 0, s = 0;
    for (PCSTR p = entry->Signature; *p; p++) {
        switch (*p) {
        case 'o': case 'H': put_u32(reply, outputs[o++]); break;
        case 'F':
            put_u32(reply, output_times[t].dwLowDateTime);
            put_u32(reply, output_times[t++].dwHighDateTime);
            break;
        case 'T': put_bytes(reply, product, 39 * sizeof(WCHAR)); break;
        case 'B': case 'b': b++; break;
        case 'S': {
            ULONG value = has_size[s] ? sizes[s] : 0;
            put_u32(reply, value);
            /* the preceding buffer's content goes along: a string up to its terminator, unless the function left it
               alone, and bytes up to the size. */
            ULONG data_bytes = 0;
            const void *data = b ? buffers[b - 1] : nullptr;
            if (data && bytes[b - 1])
                data_bytes = (ULONG)result == ERROR_SUCCESS ? (value < capacities[b - 1] ? value : capacities[b - 1]) : 0;
            else if (data && *(const WCHAR *)data != 0xFFFF)
                data_bytes = (ULONG)((wcslen(data) + 1) * sizeof(WCHAR));
            put_u32(reply, data_bytes);
            put_bytes(reply, data, data_bytes);
            s++;
            break;
        }
        }
    }
    for (ULONG i = 0; i < RTL_NUMBER_OF(strings); i++)
        MimeMsiFree(strings[i]);
    for (ULONG i = 0; i < RTL_NUMBER_OF(buffers); i++)
        MimeMsiFree(buffers[i]);
    return !reply->Failed;
}

static UINT remote_run(MIME_MSI_PACKAGE *package, MSIHANDLE session, PCWSTR dll, PCSTR entry, WORD machine,
                       BOOL registering, UINT *result)
{
    ULONG host;
    if (machine == IMAGE_FILE_MACHINE_I386)
        host = HOST_X86;
    else if (machine == IMAGE_FILE_MACHINE_AMD64 || machine == IMAGE_FILE_MACHINE_ARM64)
        host = HOST_NATIVE64;
    else
        return ERROR_BAD_EXE_FORMAT;
    AcquireSRWLockExclusive(&g_host_lock);
    UINT error = start_host(host);
    if (error) {
        ReleaseSRWLockExclusive(&g_host_lock);
        return error;
    }
    WCHAR entry_name[256];
    MultiByteToWideChar(CP_ACP, 0, entry, -1, entry_name, RTL_NUMBER_OF(entry_name));
    MESSAGE request = {};
    put_u32(&request, session);
    put_text(&request, dll);
    put_text(&request, entry_name);
    put_u32(&request, registering);
    BOOL ok = !request.Failed && send_message(g_host_pipe[host], MESSAGE_RUN, &request);
    MimeMsiFree(request.Data);
    error = ok ? ERROR_SUCCESS : ERROR_BROKEN_PIPE;
    while (!error) {
        MESSAGE message;
        ULONG kind;
        if (!receive_message(g_host_pipe[host], &kind, &message)) {
            error = ERROR_BROKEN_PIPE;
            break;
        }
        if (kind == MESSAGE_CALL) {
            MESSAGE reply = {};
            BOOL served = serve_call(&message, &reply);
            MimeMsiFree(message.Data);
            if (!served || !send_message(g_host_pipe[host], MESSAGE_REPLY, &reply))
                error = ERROR_BROKEN_PIPE;
            MimeMsiFree(reply.Data);
        } else if (kind == MESSAGE_RESULT) {
            ULONG load_error = get_u32(&message);
            *result = get_u32(&message);
            MimeMsiFree(message.Data);
            error = load_error;
            break;
        } else {
            MimeMsiFree(message.Data);
            error = ERROR_INVALID_DATA;
        }
    }
    close_given_handles();
    if (error == ERROR_BROKEN_PIPE)
        shut_down_host(host);
    ReleaseSRWLockExclusive(&g_host_lock);
    return error;
}

UINT MimeMsiRemoteRun(MIME_MSI_PACKAGE *package, MSIHANDLE session, PCWSTR dll, PCSTR entry, WORD machine, UINT *result)
{
    return remote_run(package, session, dll, entry, machine, FALSE, result);
}

UINT MimeMsiRemoteRegister(MIME_MSI_PACKAGE *package, PCWSTR dll, PCSTR entry, WORD machine, UINT *result)
{
    return remote_run(package, 0, dll, entry, machine, TRUE, result);
}
