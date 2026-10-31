/*
 * services.exe: service processes. an image is a process started for services. it connects back over the named
 * pipe, which becomes its channel, and gets one control at a time.
 */
#include "services.h"

static IMAGE *g_images;
ULONGLONG g_stamp;
DWORD g_control_timeout = 30000;
DWORD g_handler_timeout = 30000;
DWORD g_autostart_timeout = 45000;
BOOL g_shutdown;
BOOL g_autostart_running;

/* ------------------------------------------------------------------ the channel */

static DWORD wait_io(HANDLE pipe, OVERLAPPED *ov, BOOL started, DWORD timeout, DWORD *transferred)
{
    if (!started) {
        DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING)
            return error;
        if (WaitForSingleObject(ov->hEvent, timeout) == WAIT_TIMEOUT) {
            CancelIoEx(pipe, ov);
            GetOverlappedResult(pipe, ov, transferred, TRUE);
            return ERROR_SERVICE_REQUEST_TIMEOUT;
        }
        if (!GetOverlappedResult(pipe, ov, transferred, FALSE))
            return GetLastError();
        return ERROR_SUCCESS;
    }
    return GetOverlappedResult(pipe, ov, transferred, TRUE) ? ERROR_SUCCESS : GetLastError();
}

static DWORD channel_write(IMAGE *img, const void *data, DWORD size, DWORD timeout)
{
    const BYTE *p = data;
    while (size) {
        OVERLAPPED ov = {};
        ov.hEvent = img->PipeEvent;
        ResetEvent(ov.hEvent);
        DWORD done = 0;
        BOOL started = WriteFile(img->Pipe, p, size, &done, &ov);
        DWORD error = wait_io(img->Pipe, &ov, started, timeout, &done);
        if (error)
            return error;
        p += done;
        size -= done;
    }
    return ERROR_SUCCESS;
}

static DWORD channel_read(IMAGE *img, void *data, DWORD size, DWORD timeout)
{
    BYTE *p = data;
    while (size) {
        OVERLAPPED ov = {};
        ov.hEvent = img->PipeEvent;
        ResetEvent(ov.hEvent);
        DWORD done = 0;
        BOOL started = ReadFile(img->Pipe, p, size, &done, &ov);
        DWORD error = wait_io(img->Pipe, &ov, started, timeout, &done);
        if (error)
            return error;
        if (!done)
            return ERROR_BROKEN_PIPE;
        p += done;
        size -= done;
    }
    return ERROR_SUCCESS;
}

/*
 * returns the error the handler returned. called without the lock, the image's control lock serializes controls.
 */
static DWORD send_control(IMAGE *img, DWORD control, const WCHAR *name, DWORD event_type, const void *event_data,
                          DWORD event_size, UINT64 status_id, DWORD count, const WCHAR *const *arguments, DWORD timeout,
                          DWORD *result)
{
    SCM_WRITER w;
    ScmWriterInit(&w, control);
    ScmPutString(&w, name);
    ScmPutU32(&w, event_type);
    ScmPutBytes(&w, event_data, event_size);
    ScmPutU64(&w, status_id);
    ScmPutU32(&w, count);
    for (DWORD i = 0; i < count; i++)
        ScmPutString(&w, arguments[i]);
    UINT32 size;
    UCHAR *message = ScmWriterFinish(&w, &size);
    if (!message) {
        SvcFree(w.Data);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    EnterCriticalSection(&img->ControlLock);
    DWORD error = ERROR_BROKEN_PIPE;
    if (img->Pipe && !img->Broken) {
        error = channel_write(img, message, size, timeout);
        SCM_HEADER reply;
        if (!error)
            error = channel_read(img, &reply, sizeof(reply), timeout);
        if (!error && reply.Magic != SCM_MAGIC)
            error = ERROR_INVALID_DATA;
        if (!error && reply.Size > sizeof(reply)) {
            /* nothing follows an answer today, anything that does is dropped. */
            BYTE sink[256];
            DWORD left = reply.Size - sizeof(reply);
            while (left && !error) {
                DWORD n = left < sizeof(sink) ? left : sizeof(sink);
                error = channel_read(img, sink, n, timeout);
                left -= n;
            }
        }
        if (!error && result)
            *result = reply.Code;
        if (error)
            img->Broken = TRUE;
    }
    LeaveCriticalSection(&img->ControlLock);
    SvcFree(w.Data);
    return error;
}

/* ------------------------------------------------------------------ images */

static void image_free(IMAGE *img)
{
    for (IMAGE **link = &g_images; *link; link = &(*link)->Next) {
        if (*link == img) {
            *link = img->Next;
            break;
        }
    }
    if (img->Pipe)
        CloseHandle(img->Pipe);
    if (img->PipeEvent)
        CloseHandle(img->PipeEvent);
    if (img->Process)
        CloseHandle(img->Process);
    DeleteCriticalSection(&img->ControlLock);
    SvcFree(img->Key);
    SvcFree(img->Account);
    SvcFree(img);
}

static void image_release(IMAGE *img)
{
    if (img->Refs)
        img->Refs--;
    if (!img->Refs && img->Exited && !img->Services)
        image_free(img);
}

/* ends a process once its last service stops: the dispatcher is told, then the process gets time to exit. */
static void NTAPI quit_thread(PVOID context)
{
    IMAGE *img = context;
    DWORD result;
    send_control(img, SCM_CTRL_QUIT, L"", 0, nullptr, 0, 0, 0, nullptr, 5000, &result);
    HANDLE process = nullptr;
    EnterCriticalSection(&g_lock);
    if (img->Process && !img->Exited)
        DuplicateHandle(GetCurrentProcess(), img->Process, GetCurrentProcess(), &process, SYNCHRONIZE | PROCESS_TERMINATE,
                        FALSE, 0);
    LeaveCriticalSection(&g_lock);
    if (process) {
        /* windows gives the process twenty seconds to exit before ending it. */
        if (WaitForSingleObject(process, 20000) == WAIT_TIMEOUT)
            TerminateProcess(process, 0);
        CloseHandle(process);
    }
    EnterCriticalSection(&g_lock);
    image_release(img);
    LeaveCriticalSection(&g_lock);
}

static DWORD WINAPI quit_entry(LPVOID context)
{
    quit_thread(context);
    return 0;
}

static void image_quit(IMAGE *img)
{
    if (img->Terminating || img->Exited)
        return;
    img->Terminating = TRUE;
    img->Refs++;
    HANDLE thread = CreateThread(nullptr, 0, quit_entry, img, 0, nullptr);
    if (thread)
        CloseHandle(thread);
    else
        img->Refs--;
}

/* ------------------------------------------------------------------ states */

static const char *state_name(DWORD state)
{
    static const char *const names[] = {"?", "stopped", "start pending", "stop pending", "running", "continue pending",
                                        "pause pending", "paused"};
    return state < 8 ? names[state] : "?";
}

void RunStatusSnapshot(SERVICE *s, SERVICE_STATUS_PROCESS *out)
{
    memset(out, 0, sizeof(*out));
    memcpy(out, &s->Status, sizeof(s->Status));
    if ((s->Type & (SERVICE_KERNEL_DRIVER | SERVICE_FILE_SYSTEM_DRIVER | SERVICE_RECOGNIZER_DRIVER | SERVICE_ADAPTER)) &&
        !s->Image) {
        /* a driver runs when the object manager knows it. */
        out->dwCurrentState = DbDriverRunning(s) ? SERVICE_RUNNING : SERVICE_STOPPED;
        out->dwControlsAccepted = out->dwCurrentState == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0;
        if (out->dwCurrentState == SERVICE_STOPPED && out->dwWin32ExitCode == ERROR_SERVICE_NEVER_STARTED)
            out->dwWin32ExitCode = 0;
    }
    if (s->Image && s->Status.dwCurrentState != SERVICE_STOPPED)
        out->dwProcessId = s->Image->Pid;
    out->dwServiceFlags = s->ServiceFlags;
}

/* every state change goes through here: it stamps the record, wakes the waiters and notifies the watchers. */
static void state_changed(SERVICE *s, DWORD old_state)
{
    s->Stamp = ++g_stamp;
    s->StateTick = GetTickCount64();
    WakeAllConditionVariable(&g_changed);
    NotifyServiceChanged(s, old_state);
}

void RunStateChanged(SERVICE *s, DWORD old_state)
{
    state_changed(s, old_state);
}

/* ------------------------------------------------------------------ failures */

typedef struct RESTART {
    WCHAR Name[SVC_NAME_MAX + 1];
    BOOL Own;
    WCHAR *Command;
    DWORD Count;
} RESTART;

static DWORD WINAPI restart_worker(LPVOID context)
{
    RESTART *r = context;
    EnterCriticalSection(&g_lock);
    SERVICE *s = DbFind(r->Name);
    if (s && !g_shutdown && DbLoadRecord(s)) {
        if (r->Command) {
            WCHAR *command = r->Command;
            LeaveCriticalSection(&g_lock);
            /* %1% stands for the failure count. */
            WCHAR text[1024];
            WCHAR count[16];
            _snwprintf(count, RTL_NUMBER_OF(count) - 1, L"%lu", r->Count);
            count[RTL_NUMBER_OF(count) - 1] = 0;
            size_t n = 0;
            for (const WCHAR *c = command; *c && n + 20 < RTL_NUMBER_OF(text);) {
                if (c[0] == L'%' && c[1] == L'1' && c[2] == L'%') {
                    n += (size_t)_snwprintf(text + n, 16, L"%ls", count);
                    c += 3;
                } else {
                    text[n++] = *c++;
                }
            }
            text[n] = 0;
            STARTUPINFOW si = {sizeof(si)};
            PROCESS_INFORMATION pi;
            if (CreateProcessW(nullptr, text, nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
            EnterCriticalSection(&g_lock);
        } else if (s->Status.dwCurrentState == SERVICE_STOPPED) {
            RunStart(s, 0, nullptr, START_RESTART);
        }
    }
    LeaveCriticalSection(&g_lock);
    SvcFree(r->Command);
    SvcFree(r);
    return 0;
}

static void restart_timer(void *context)
{
    HANDLE thread = CreateThread(nullptr, 0, restart_worker, context, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        RESTART *r = context;
        SvcFree(r->Command);
        SvcFree(r);
    }
}

void RunScheduleRestart(SERVICE *s, DWORD delay)
{
    RESTART *r = SvcAllocZero(sizeof(*r));
    if (!r)
        return;
    SvcCopyName(r->Name, s->Name, SVC_NAME_MAX);
    TimerAdd(delay, restart_timer, r);
}

/*
 * the failure count picks the recovery action, the last one repeating. the count starts over after the reset period.
 */
static void handle_failure(SERVICE *s)
{
    if (g_shutdown)
        return;
    SERVICE_FAILURE_ACTIONSW actions;
    SC_ACTION buffer[SVC_MAX_FAILURE_ACTIONS];
    WCHAR *reboot, *command;
    if (DbReadFailureActions(s, &actions, buffer, &reboot, &command)) {
        s->FailureCount++;
        s->LastFailureTick = GetTickCount64();
        return;
    }
    ULONGLONG now = GetTickCount64();
    if (s->FailureCount && actions.dwResetPeriod != INFINITE && now >= s->LastFailureTick + 1000ull * actions.dwResetPeriod)
        s->FailureCount = 0;
    SC_ACTION action = {SC_ACTION_NONE, 0};
    if (actions.cActions) {
        DWORD index = s->FailureCount < actions.cActions ? s->FailureCount : actions.cActions - 1;
        action = actions.lpsaActions[index];
    }
    s->LastFailureTick = now;
    s->FailureCount++;
    SvcLog("service %ls failed %lu times, action %d after %lu ms", s->Name, s->FailureCount, (int)action.Type,
           action.Delay);
    switch (action.Type) {
    case SC_ACTION_RESTART:
    case SC_ACTION_OWN_RESTART:
        RunScheduleRestart(s, action.Delay);
        break;
    case SC_ACTION_RUN_COMMAND:
        if (command && *command) {
            RESTART *r = SvcAllocZero(sizeof(*r));
            if (r) {
                SvcCopyName(r->Name, s->Name, SVC_NAME_MAX);
                r->Command = SvcDup(command);
                r->Count = s->FailureCount;
                TimerAdd(action.Delay, restart_timer, r);
            }
        }
        break;
    case SC_ACTION_REBOOT: {
        WCHAR message[256];
        _snwprintf(message, RTL_NUMBER_OF(message) - 1, L"%ls", reboot ? reboot : L"");
        message[RTL_NUMBER_OF(message) - 1] = 0;
        AccessEnablePrivilege(L"SeShutdownPrivilege");
        InitiateSystemShutdownExW(nullptr, message, action.Delay / 1000, TRUE, TRUE, 0x80000000u | 0x00040000u);
        break;
    }
    default:
        break;
    }
    SvcFree(reboot);
    SvcFree(command);
}

/* ------------------------------------------------------------------ the end of a process */

static void service_gone(SERVICE *s, IMAGE *img, DWORD exit_code, BOOL crash)
{
    DWORD old = s->Status.dwCurrentState;
    s->Status.dwCurrentState = SERVICE_STOPPED;
    s->Status.dwControlsAccepted = 0;
    s->Status.dwCheckPoint = 0;
    s->Status.dwWaitHint = 0;
    s->Status.dwWin32ExitCode = exit_code;
    s->Status.dwServiceSpecificExitCode = 0;
    s->Status.dwServiceType = s->Type;
    s->StatusId = 0;
    s->StopRequested = FALSE;
    s->ShutdownStopping = FALSE;
    s->Image = nullptr;
    if (img && img->Services)
        img->Services--;
    state_changed(s, old);
    if (crash)
        handle_failure(s);
}

static void NTAPI process_wait(PVOID context, BOOLEAN timed_out)
{
    (void)timed_out;
    IMAGE *img = context;
    EnterCriticalSection(&g_lock);
    RunProcessExited(img);
    MainCollect();
    LeaveCriticalSection(&g_lock);
}

void RunProcessExited(IMAGE *img)
{
    if (img->Exited)
        return;
    img->Exited = TRUE;
    img->Broken = TRUE;
    for (SERVICE *s = g_services; s; s = s->Next) {
        if (s->Image == img && s->Status.dwCurrentState != SERVICE_STOPPED) {
            SvcLog("the process of %ls ended while it was %s", s->Name, state_name(s->Status.dwCurrentState));
            service_gone(s, img, ERROR_PROCESS_ABORTED, TRUE);
        } else if (s->Image == img) {
            s->Image = nullptr;
        }
    }
    WakeAllConditionVariable(&g_changed);
    if (!img->Refs && !img->Services)
        image_free(img);
}

/* ------------------------------------------------------------------ setting the status */

/* the types a service may report, ignoring the interactive bit. */
static BOOL reportable_type(DWORD type)
{
    switch (type & ~(DWORD)SERVICE_INTERACTIVE_PROCESS) {
    case SERVICE_WIN32_OWN_PROCESS:
    case SERVICE_WIN32_SHARE_PROCESS:
    case SERVICE_WIN32:
    case SERVICE_USER_OWN_PROCESS:
    case SERVICE_USER_SHARE_PROCESS:
    case 0xD0: /* user own process instance */
    case 0xE0: /* user share process instance */
    case 0x210: /* package own process */
        return TRUE;
    default:
        return type == 11;
    }
}

/* the undefined bits of dwControlsAccepted. some defined ones exist only on the newest systems. */
#define REPORTED_CONTROLS_REFUSED 0xFFFF8800u

static BOOL report_acceptable(const SERVICE_STATUS *report)
{
    return report->dwCurrentState >= SERVICE_STOPPED && report->dwCurrentState <= SERVICE_PAUSED &&
           reportable_type(report->dwServiceType) && !(report->dwControlsAccepted & REPORTED_CONTROLS_REFUSED);
}

static SERVICE *service_of_status(UINT64 status_id, DWORD pid)
{
    for (SERVICE *s = g_services; s; s = s->Next)
        if (status_id && s->StatusId == status_id && s->Image && s->Image->Pid == pid)
            return s;
    return nullptr;
}

/*
 * exit codes only count once the service stops, a checkpoint and wait hint only while it is pending. a stopped
 * service accepts no control and drops its handle, and its process ends once no other service lives in it.
 */
DWORD RunSetStatus(UINT64 status_id, DWORD pid, const SERVICE_STATUS *report)
{
    SERVICE *s = service_of_status(status_id, pid);
    if (!s)
        return ERROR_INVALID_HANDLE;
    if (!report_acceptable(report))
        return ERROR_INVALID_DATA;

    SERVICE_STATUS stored = *report;
    stored.dwServiceType = s->Type;
    BOOL ending = stored.dwCurrentState == SERVICE_STOPPED || stored.dwCurrentState == SERVICE_STOP_PENDING;
    if (!ending)
        stored.dwWin32ExitCode = stored.dwServiceSpecificExitCode = 0;
    BOOL settled = stored.dwCurrentState == SERVICE_STOPPED || stored.dwCurrentState == SERVICE_RUNNING ||
                   stored.dwCurrentState == SERVICE_PAUSED;
    if (settled)
        stored.dwCheckPoint = stored.dwWaitHint = 0;
    if (stored.dwCurrentState == SERVICE_STOPPED)
        stored.dwControlsAccepted = 0;

    DWORD before = s->Status.dwCurrentState;
    s->Status = stored;
    IMAGE *image = s->Image;
    BOOL stopped = stored.dwCurrentState == SERVICE_STOPPED;
    if (stopped) {
        s->StatusId = 0;
        s->StopRequested = FALSE;
        s->ShutdownStopping = FALSE;
        s->Image = nullptr;
        if (image->Services)
            image->Services--;
    }
    if (before != stored.dwCurrentState)
        state_changed(s, before);
    else
        WakeAllConditionVariable(&g_changed);

    if (stopped) {
        if (stored.dwWin32ExitCode != 0 && s->FailureOnNonCrash && !g_shutdown)
            handle_failure(s);
        if (!image->Services)
            image_quit(image);
    }
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ process channel */

/* only a process the manager started and is waiting for may connect. */
DWORD RunOpenChannel(HANDLE pipe, DWORD pid)
{
    for (IMAGE *img = g_images; img; img = img->Next) {
        if (img->Pid == pid && !img->Exited && !img->Connected && !img->FirstSent) {
            img->FirstSent = TRUE; /* reserved while the answer is written. */
            return ERROR_SUCCESS;
        }
    }
    (void)pipe;
    return ERROR_FAILED_SERVICE_CONTROLLER_CONNECT;
}

/* the answer is written, the pipe now belongs to the image. */
void RunChannelOpened(HANDLE pipe, DWORD pid, BOOL ok)
{
    for (IMAGE *img = g_images; img; img = img->Next) {
        if (img->Pid == pid && !img->Exited && !img->Connected && img->FirstSent) {
            if (ok) {
                img->Pipe = pipe;
                img->PipeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                img->Connected = TRUE;
            } else {
                img->FirstSent = FALSE;
            }
            WakeAllConditionVariable(&g_changed);
            return;
        }
    }
    if (ok)
        CloseHandle(pipe);
}

/* ------------------------------------------------------------------ starting */

static BOOL file_exists(const WCHAR *command)
{
    /* the program is quoted, or runs up to the first space that names an existing file. */
    WCHAR path[MAX_PATH_ + 2];
    if (*command == L'"') {
        const WCHAR *end = wcschr(command + 1, L'"');
        size_t n = end ? (size_t)(end - command - 1) : wcslen(command + 1);
        if (n >= MAX_PATH_)
            return FALSE;
        memcpy(path, command + 1, n * sizeof(WCHAR));
        path[n] = 0;
        return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    }
    size_t n = 0;
    for (const WCHAR *c = command;; c++) {
        if ((*c == L' ' || !*c) && n) {
            path[n] = 0;
            DWORD a = GetFileAttributesW(path);
            if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
                return TRUE;
            WCHAR with_extension[MAX_PATH_ + 8];
            _snwprintf(with_extension, RTL_NUMBER_OF(with_extension) - 1, L"%ls.exe", path);
            with_extension[RTL_NUMBER_OF(with_extension) - 1] = 0;
            a = GetFileAttributesW(with_extension);
            if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY))
                return TRUE;
        }
        if (!*c || n >= MAX_PATH_)
            break;
        path[n++] = *c;
    }
    return FALSE;
}

/* a comparable account name: one name for each built-in account, the rest lowercased. */
static WCHAR *account_of(SERVICE *s)
{
    HKEY key = DbOpenKey(s->Name, KEY_READ, FALSE);
    WCHAR *account = key ? DbReadString(key, L"ObjectName", nullptr) : nullptr;
    if (key)
        RegCloseKey(key);
    const WCHAR *form = account;
    if (!account || !*account || !_wcsicmp(account, L"LocalSystem") || !_wcsicmp(account, L"NT AUTHORITY\\SYSTEM") ||
        !_wcsicmp(account, L"NT AUTHORITY\\LocalSystem"))
        form = L"localsystem";
    else if (!_wcsicmp(account, L"LocalService") || !_wcsicmp(account, L"NT AUTHORITY\\LocalService"))
        form = L"localservice";
    else if (!_wcsicmp(account, L"NetworkService") || !_wcsicmp(account, L"NT AUTHORITY\\NetworkService"))
        form = L"networkservice";
    WCHAR *copy = SvcDup(form);
    for (WCHAR *c = copy; c && *c; c++)
        *c = towlower(*c);
    SvcFree(account);
    return copy;
}

static WCHAR *image_key(const WCHAR *command)
{
    WCHAR *key = SvcDup(command);
    for (WCHAR *c = key; c && *c; c++)
        *c = towlower(*c);
    return key;
}

/*
 * nothing is held while the process is created, the lock is taken again after.
 */
static DWORD spawn_image(SERVICE *s, const WCHAR *command, IMAGE **out)
{
    IMAGE *img = SvcAllocZero(sizeof(*img));
    if (!img)
        return ERROR_NOT_ENOUGH_MEMORY;
    InitializeCriticalSection(&img->ControlLock);
    img->Key = image_key(command);
    img->Account = account_of(s);
    img->Type = s->Type;
    img->Refs = 1;
    img->Next = g_images;
    g_images = img;
    *out = img;

    WCHAR *line = SvcDup(command);
    DWORD token_error = 0;
    LeaveCriticalSection(&g_lock);
    HANDLE token = AccessServiceToken(s, &token_error);
    STARTUPINFOW si = {sizeof(si)};
    si.lpDesktop = L"";
    PROCESS_INFORMATION pi = {};
    BOOL ok;
    DWORD flags = DETACHED_PROCESS | CREATE_UNICODE_ENVIRONMENT;
    WCHAR directory[MAX_PATH_];
    UINT n = GetSystemDirectoryW(directory, RTL_NUMBER_OF(directory));
    /* a service has the environment of its account: the profile of the account, not the one the manager started in. */
    LPVOID environment = nullptr;
    HMODULE userenv = nullptr;
    BOOL (WINAPI *create_environment)(LPVOID *, HANDLE, BOOL) = nullptr;
    BOOL (WINAPI *destroy_environment)(LPVOID) = nullptr;
    BOOL made = FALSE;
    if (!token_error) {
        userenv = LoadLibraryW(L"userenv.dll");
        if (userenv) {
            create_environment = (BOOL(WINAPI *)(LPVOID *, HANDLE, BOOL))GetProcAddress(userenv, "CreateEnvironmentBlock");
            destroy_environment = (BOOL(WINAPI *)(LPVOID))GetProcAddress(userenv, "DestroyEnvironmentBlock");
        }
        if (create_environment && destroy_environment) {
            /* the local system account has no token of its own here, the manager's is the system's. */
            HANDLE own = nullptr;
            HANDLE source = token;
            if (!source && OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own))
                source = own;
            made = source && create_environment(&environment, source, FALSE);
            if (own)
                CloseHandle(own);
        }
    }
    if (token_error) {
        ok = FALSE;
        SetLastError(token_error);
    } else if (token) {
        AccessEnablePrivilege(L"SeAssignPrimaryTokenPrivilege");
        AccessEnablePrivilege(L"SeIncreaseQuotaPrivilege");
        ok = CreateProcessAsUserW(token, nullptr, line, nullptr, nullptr, FALSE, flags, made ? environment : nullptr,
                                  n ? directory : nullptr, &si, &pi);
    } else {
        ok = CreateProcessW(nullptr, line, nullptr, nullptr, FALSE, flags, made ? environment : nullptr,
                            n ? directory : nullptr, &si, &pi);
    }
    if (made)
        destroy_environment(environment);
    if (userenv)
        FreeLibrary(userenv);
    DWORD error = ok ? 0 : GetLastError();
    if (token)
        CloseHandle(token);
    SvcFree(line);
    EnterCriticalSection(&g_lock);
    if (!ok) {
        img->Exited = TRUE;
        image_release(img);
        *out = nullptr;
        return error ? error : ERROR_SERVICE_LOGON_FAILED;
    }
    CloseHandle(pi.hThread);
    img->Process = pi.hProcess;
    img->Pid = pi.dwProcessId;
    if (!RegisterWaitForSingleObject(&img->Wait, img->Process, process_wait, img, INFINITE, WT_EXECUTEONLYONCE)) {
        SvcLog("no wait on the process of %ls", s->Name);
    }
    return ERROR_SUCCESS;
}

static ULONGLONG deadline_after(DWORD ms)
{
    return GetTickCount64() + ms;
}

/* the lock is held on entry and on return. */
static BOOL wait_until(ULONGLONG deadline)
{
    ULONGLONG now = GetTickCount64();
    if (now >= deadline)
        return FALSE;
    ULONGLONG left = deadline - now;
    SvcSleepChanged(left > 1000 ? 1000 : (DWORD)left);
    return TRUE;
}

static void start_failed(SERVICE *s, DWORD error)
{
    DWORD old = s->Status.dwCurrentState;
    s->Status.dwCurrentState = SERVICE_STOPPED;
    s->Status.dwControlsAccepted = 0;
    s->Status.dwCheckPoint = 0;
    s->Status.dwWaitHint = 0;
    if (s->Status.dwWin32ExitCode == ERROR_SERVICE_NEVER_STARTED || !s->Status.dwWin32ExitCode)
        s->Status.dwWin32ExitCode = error;
    s->Status.dwServiceType = s->Type;
    s->StartError = error;
    state_changed(s, old);
}

static WCHAR *service_command(SERVICE *s, DWORD *error)
{
    HKEY key = DbOpenKey(s->Name, KEY_READ, FALSE);
    if (!key) {
        *error = ERROR_FILE_NOT_FOUND;
        return nullptr;
    }
    WCHAR *path = DbReadString(key, L"ImagePath", nullptr);
    RegCloseKey(key);
    if (!path || !*path) {
        SvcFree(path);
        *error = ERROR_FILE_NOT_FOUND;
        return nullptr;
    }
    return path;
}

/* whether the command line runs svchost.exe, from any directory. */
static BOOL is_svchost(const WCHAR *command)
{
    const WCHAR *end = command;
    if (*command == L'"') {
        end = wcschr(command + 1, L'"');
        end = end ? end : command + wcslen(command);
    } else {
        while (*end && *end != L' ' && *end != L'\t')
            end++;
    }
    static const WCHAR name[] = L"svchost.exe";
    SIZE_T n = RTL_NUMBER_OF(name) - 1;
    if ((SIZE_T)(end - command) < n)
        return FALSE;
    const WCHAR *base = end - n;
    return !_wcsnicmp(base, name, n) && (base == command || base[-1] == L'\\' || base[-1] == L'"' || base[-1] == L'/');
}

/*
 * an svchost service lives in the dll its Parameters key names, or its own key without one. at boot, a service whose
 * dll is missing is not started, like a missing program: there are hundreds, each would spawn a process just to fail.
 */
static BOOL service_dll_present(SERVICE *s)
{
    HKEY key = DbOpenKey(s->Name, KEY_READ, FALSE);
    if (!key)
        return FALSE;
    HKEY parameters;
    WCHAR path[MAX_PATH_ + 2];
    BOOL there = FALSE;
    HKEY source = key;
    if (RegOpenKeyExW(key, L"Parameters", 0, KEY_READ, &parameters) == ERROR_SUCCESS)
        source = parameters;
    DWORD type = 0, size = sizeof(path) - 2 * sizeof(WCHAR);
    if (RegQueryValueExW(source, L"ServiceDll", nullptr, &type, (BYTE *)path, &size) == ERROR_SUCCESS &&
        type == REG_EXPAND_SZ && size >= sizeof(WCHAR)) {
        WCHAR expanded[MAX_PATH_ + 2];
        path[size / sizeof(WCHAR)] = 0;
        DWORD n = ExpandEnvironmentStringsW(path, expanded, RTL_NUMBER_OF(expanded));
        if (n && n <= RTL_NUMBER_OF(expanded)) {
            DWORD a = GetFileAttributesW(expanded);
            there = a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
        }
    }
    if (source != key)
        RegCloseKey(parameters);
    RegCloseKey(key);
    return there;
}

BOOL RunImageExists(SERVICE *s)
{
    DWORD error = 0;
    WCHAR *command = service_command(s, &error);
    if (!command)
        return FALSE;
    BOOL there = file_exists(command);
    if (there && is_svchost(command) && !(s->Type & SERVICE_DRIVER))
        there = service_dll_present(s);
    SvcFree(command);
    return there;
}

static IMAGE *find_image(const WCHAR *key)
{
    for (IMAGE *img = g_images; img; img = img->Next)
        if (!img->Exited && !img->Terminating && img->Key && !wcscmp(img->Key, key))
            return img;
    return nullptr;
}

static DWORD launch(SERVICE *s, DWORD count, const WCHAR *const *arguments, DWORD reason)
{
    DWORD error = 0;
    WCHAR *command = service_command(s, &error);
    if (!command)
        return error;
    /* a missing program is not started, as on windows. */
    if (!(s->Type & SERVICE_DRIVER) && !file_exists(command)) {
        SvcFree(command);
        return ERROR_FILE_NOT_FOUND;
    }
    if (s->Type & SERVICE_DRIVER) {
        /* kernel drivers cannot be loaded here. */
        BOOL there = file_exists(command) || wcsstr(command, L"\\SystemRoot\\") || wcschr(command, L'\\');
        SvcFree(command);
        return there ? ERROR_NOT_SUPPORTED : ERROR_FILE_NOT_FOUND;
    }
    WCHAR *key = image_key(command);
    BOOL own = (s->Type & SERVICE_WIN32_OWN_PROCESS) != 0;
    IMAGE *img = own ? nullptr : find_image(key);
    SvcFree(key);
    /* services of one process share one account, a service wanting another does not join it. */
    if (img) {
        WCHAR *account = account_of(s);
        BOOL same = account && img->Account && !wcscmp(account, img->Account);
        SvcFree(account);
        if (!same) {
            SvcFree(command);
            return ERROR_DIFFERENT_SERVICE_ACCOUNT;
        }
    }

    DWORD old = s->Status.dwCurrentState;
    s->Status.dwCurrentState = SERVICE_START_PENDING;
    s->Status.dwControlsAccepted = 0;
    s->Status.dwCheckPoint = 0;
    s->Status.dwWaitHint = 0;
    s->Status.dwServiceType = s->Type;
    s->Status.dwWin32ExitCode = 0;
    s->Status.dwServiceSpecificExitCode = 0;
    s->StartError = 0;
    s->StartPending++;
    state_changed(s, old);

    BOOL fresh = FALSE;
    if (!img) {
        error = spawn_image(s, command, &img);
        if (error) {
            s->StartPending--;
            SvcFree(command);
            start_failed(s, error);
            return error;
        }
        fresh = TRUE;
    }
    SvcFree(command);
    img->Refs++;
    s->Image = img;
    img->Services++;
    s->StatusId = SvcNewId();

    /* the process must open its channel in time. one that exits first or never connects is no service. */
    DWORD timeout = reason == 2 ? g_autostart_timeout : g_control_timeout;
    HKEY k = DbOpenKey(s->Name, KEY_READ, FALSE);
    if (k) {
        DWORD own_timeout;
        if (DbReadDword(k, L"ServicesPipeTimeout", &own_timeout) && own_timeout > timeout)
            timeout = own_timeout;
        RegCloseKey(k);
    }
    ULONGLONG deadline = deadline_after(timeout);
    while (!img->Connected && !img->Exited && !g_shutdown) {
        if (!wait_until(deadline))
            break;
    }
    if (!img->Connected) {
        error = ERROR_SERVICE_REQUEST_TIMEOUT;
        SvcLog("%ls did not connect: %s", s->Name, img->Exited ? "its process ended" : "timeout");
        if (fresh && !img->Exited && img->Process)
            TerminateProcess(img->Process, ERROR_SERVICE_REQUEST_TIMEOUT);
        s->Image = nullptr;
        if (img->Services)
            img->Services--;
        s->StartPending--;
        image_release(img);
        start_failed(s, error);
        return error;
    }

    /* the first control starts this service. a shared process takes it for one of its services. */
    UINT64 status_id = s->StatusId;
    WCHAR name[SVC_NAME_MAX + 1];
    SvcCopyName(name, s->Name, SVC_NAME_MAX);
    name[SVC_NAME_MAX] = 0;
    DWORD result = 0;
    LeaveCriticalSection(&g_lock);
    error = send_control(img, own ? SCM_CTRL_START_OWN : SCM_CTRL_START_SHARE, name, 0, nullptr, 0, status_id, count,
                         arguments, g_control_timeout, &result);
    EnterCriticalSection(&g_lock);
    if (!error)
        error = result;
    s->StartPending--;
    if (error) {
        SvcLog("%ls did not start: %lu", s->Name, error);
        if (s->Image == img) {
            s->Image = nullptr;
            if (img->Services)
                img->Services--;
        }
        s->StatusId = 0;
        if (!img->Services && img->Process && !img->Exited) {
            if (error == ERROR_SERVICE_REQUEST_TIMEOUT)
                TerminateProcess(img->Process, error);
            else
                image_quit(img);
        }
        image_release(img);
        start_failed(s, error);
        return error;
    }
    image_release(img);
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ start sets */

/*
 * services.exe starts a service through a start set: the service, the services it depends on and the members of the
 * groups it depends on, all of them in turn with what they depend on. The set is sorted by the position of the group
 * in ServiceGroupOrder, a service after what it depends on in the same group, and is run in batches of one group: the
 * services of a batch are marked, and each is started once everything it depends on runs. A group that a service
 * depends on counts as started when one of its members runs, and as failed, with 1068 for the dependent, when none
 * does, an empty group included.
 */

BOOL g_phase1_running;
DWORD g_start_timeout = 50000;
#define BATCH_MAX 64

typedef struct START_SET {
    SERVICE **Items;
    DWORD Count, Capacity;
    const WCHAR **Groups;
    DWORD GroupCount, GroupCapacity;
} START_SET;

static BOOL driver_of(const SERVICE *s)
{
    return (s->Type & (SERVICE_DRIVER | SERVICE_ADAPTER)) != 0;
}

/* the state of the service as the manager sees it: a driver runs while its object exists. */
static DWORD state_of(SERVICE *s)
{
    if (driver_of(s) && !s->Image)
        return DbDriverRunning(s) ? SERVICE_RUNNING : SERVICE_STOPPED;
    return s->Status.dwCurrentState;
}

void RunStartTimeoutInit(void)
{
    static BOOL done;
    if (done)
        return;
    done = TRUE;
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD timeout;
        if (DbReadDword(key, L"ServiceStartTimeout", &timeout) && timeout)
            g_start_timeout = timeout;
        RegCloseKey(key);
    }
}

static BOOL set_has(const START_SET *set, const SERVICE *s)
{
    for (DWORD i = 0; i < set->Count; i++)
        if (set->Items[i] == s)
            return TRUE;
    return FALSE;
}

/* the records of a set are held, so that a deletion that comes meanwhile waits for the set to be done. */
static void set_push(START_SET *set, SERVICE *s)
{
    if (set->Count == set->Capacity) {
        DWORD capacity = set->Capacity ? set->Capacity * 2 : 16;
        SERVICE **grown = SvcAlloc(capacity * sizeof(*grown));
        if (!grown)
            return;
        if (set->Count)
            memcpy(grown, set->Items, set->Count * sizeof(*grown));
        SvcFree(set->Items);
        set->Items = grown;
        set->Capacity = capacity;
    }
    set->Items[set->Count++] = s;
    s->OpenHandles++;
}

static void set_push_group(START_SET *set, const WCHAR *group)
{
    if (set->GroupCount == set->GroupCapacity) {
        DWORD capacity = set->GroupCapacity ? set->GroupCapacity * 2 : 8;
        const WCHAR **grown = SvcAlloc(capacity * sizeof(*grown));
        if (!grown)
            return;
        if (set->GroupCount)
            memcpy(grown, set->Groups, set->GroupCount * sizeof(*grown));
        SvcFree(set->Groups);
        set->Groups = grown;
        set->GroupCapacity = capacity;
    }
    set->Groups[set->GroupCount++] = group;
}

static BOOL set_has_group(const START_SET *set, const WCHAR *group)
{
    for (DWORD i = 0; i < set->GroupCount; i++)
        if (!_wcsicmp(set->Groups[i], group))
            return TRUE;
    return FALSE;
}

static void set_free(START_SET *set)
{
    for (DWORD i = 0; i < set->Count; i++)
        if (set->Items[i]->OpenHandles)
            set->Items[i]->OpenHandles--;
    SvcFree(set->Items);
    SvcFree(set->Groups);
    *set = (START_SET){};
    MainCollect();
}

/* a service not started yet is marked to start now, one already marked or starting counts as marked. */
BOOL RunMarkForStart(SERVICE *s)
{
    if (s->StartState == SS_NONE) {
        s->StartState = SS_MARKED;
        s->StartError = 0;
        return TRUE;
    }
    return s->StartState == SS_MARKED || s->StartState == SS_BUSY;
}

/* whether a service is down, or a driver is on its way up: what a batch takes in. */
BOOL RunStartableNow(SERVICE *s)
{
    DWORD state = state_of(s);
    return state == SERVICE_STOPPED || (driver_of(s) && state == SERVICE_START_PENDING);
}

/* what a service is when it joins a set, from what it is now and what became of its last start. */
static void state_on_joining(SERVICE *s, DWORD flags)
{
    if (s->Start == SERVICE_DISABLED) {
        s->StartState = SS_FAILED;
        s->StartError = ERROR_SERVICE_DISABLED;
        return;
    }
    if (s->MarkedForDelete) {
        s->StartState = SS_FAILED;
        s->StartError = ERROR_SERVICE_MARKED_FOR_DELETE;
        return;
    }
    DWORD state = state_of(s);
    if (state == SERVICE_STOPPED) {
        if (s->StartState == SS_MARKED)
            return;
        DWORD exit_code = s->Status.dwWin32ExitCode;
        /* the boot does not try again a service that failed already, unless it failed to start or never ran. */
        if (!(flags & START_AUTO) || exit_code == ERROR_GEN_FAILURE || exit_code == ERROR_SERVICE_NEVER_STARTED ||
            !exit_code)
            s->StartState = SS_NONE;
        else
            s->StartState = SS_FAILED;
    } else if (state == SERVICE_START_PENDING) {
        s->StartState = driver_of(s) ? SS_NONE : SS_BUSY;
    } else if (state == SERVICE_STOP_PENDING) {
        s->StartState = SS_FAILED;
        s->StartError = ERROR_SERVICE_NOT_ACTIVE;
    } else {
        s->StartState = SS_STARTED;
    }
}

static void add_to_set(START_SET *set, SERVICE *s, DWORD flags)
{
    if (set_has(set, s))
        return;
    state_on_joining(s, flags);
    set_push(set, s);
    for (const WCHAR *d = s->DepServices; d && *d; d += wcslen(d) + 1) {
        SERVICE *dep = DbFind(d);
        if (dep && DbLoadRecord(dep))
            add_to_set(set, dep, flags);
    }
    for (const WCHAR *g = s->DepGroups; g && *g; g += wcslen(g) + 1) {
        if (set_has_group(set, g))
            continue;
        set_push_group(set, g);
        for (SERVICE *m = g_services; m; m = m->Next)
            if (DbLoadRecord(m) && DbGroupMember(m, g))
                add_to_set(set, m, flags);
    }
}

static BOOL depends_on(SERVICE *a, SERVICE *b)
{
    return a == b || DbDependsOn(a, b, 0);
}

/* by the position of the group, a service after what it depends on in its group, the ones not listed last. */
static SERVICE **sort_set(const START_SET *set)
{
    SERVICE **list = SvcAlloc((set->Count + 1) * sizeof(*list));
    if (!list)
        return nullptr;
    DWORD n = 0;
    for (DWORD i = 0; i < set->Count; i++) {
        SERVICE *x = set->Items[i];
        DWORD gx = DbGroupIndex(x->Group), pos = 0;
        while (pos < n) {
            DWORD gv = DbGroupIndex(list[pos]->Group);
            if (gv > gx || (gv == gx && depends_on(list[pos], x)))
                break;
            pos++;
        }
        memmove(list + pos + 1, list + pos, (n - pos) * sizeof(*list));
        list[pos] = x;
        n++;
    }
    return list;
}

/* a start that fails for a reason of the set, not of the program. */
static void cleanup_failure(SERVICE *s, DWORD error)
{
    s->StartState = SS_FAILED;
    s->StartError = error;
    if (s->Status.dwWin32ExitCode == ERROR_SERVICE_NEVER_STARTED)
        s->Status.dwWin32ExitCode = error;
}

/*
 * whether a dependency on a service or group in another group is on one that loads later. A service in a group
 * depends only on what loads before it; one that is in no listed group depends on anything.
 */
static BOOL later_group(const WCHAR *mine, const WCHAR *dep, BOOL group_dependency)
{
    if (group_dependency) {
        if (mine && (!_wcsicmp(mine, dep) || (!_wcsicmp(dep, L"TDI") && !_wcsicmp(mine, L"PNP_TDI"))))
            return TRUE;
    } else if ((!mine && !dep) || (mine && dep && !_wcsicmp(mine, dep))) {
        return FALSE;
    }
    if (!mine || !dep)
        return FALSE;
    DWORD a = DbGroupIndex(mine), b = DbGroupIndex(dep);
    if (a == (DWORD)-1 || b == (DWORD)-1)
        return FALSE;
    return b > a || (!_wcsicmp(dep, L"TDI") && DbGroupIndex(L"PNP_TDI") >= a);
}

/* the members of a group, and what they say together: 0 while one is still to start, 1 when one runs, else 2. */
static DWORD group_members(const WCHAR *group, SERVICE ***list)
{
    DWORD capacity = 16, n = 0;
    SERVICE **members = SvcAlloc(capacity * sizeof(*members));
    for (SERVICE *m = g_services; m && members; m = m->Next) {
        if (!DbLoadRecord(m) || !DbGroupMember(m, group))
            continue;
        if (n == capacity) {
            capacity *= 2;
            SERVICE **grown = SvcAlloc(capacity * sizeof(*grown));
            if (!grown) {
                SvcFree(members);
                members = nullptr;
                break;
            }
            memcpy(grown, members, n * sizeof(*grown));
            SvcFree(members);
            members = grown;
        }
        members[n++] = m;
    }
    *list = members;
    return members ? n : 0;
}

static DWORD group_state(SERVICE *const *members, DWORD count)
{
    BOOL started = FALSE;
    for (DWORD i = 0; i < count; i++) {
        const SERVICE *m = members[i];
        if (m->StartState == SS_NONE) {
            if (m->Start != SERVICE_DISABLED && !m->MarkedForDelete)
                return 0;
        } else if (m->StartState == SS_MARKED || m->StartState == SS_BUSY) {
            return 0;
        } else if (m->StartState == SS_STARTED) {
            started = TRUE;
        }
    }
    return started ? 1 : 2;
}

/*
 * whether everything the service depends on runs. What is to start is marked. The flags report to the caller that
 * nothing can move without a change (stuck), that something is awaited (waiting) and that the order is a circle.
 */
static BOOL deps_started(SERVICE *s, BOOL *circular, BOOL *stuck, BOOL *waiting)
{
    BOOL ready = TRUE;
    DWORD error = 0;
    for (const WCHAR *d = s->DepServices; d && *d; d += wcslen(d) + 1) {
        SERVICE *dep = DbFind(d);
        if (!dep || !DbLoadRecord(dep)) {
            SvcLog("%ls depends on %ls, which is not there", s->Name, d);
            error = ERROR_SERVICE_DEPENDENCY_DELETED;
            *stuck = FALSE;
            ready = FALSE;
            break;
        }
        if (dep->StartState == SS_FAILED) {
            SvcLog("%ls depends on %ls, which did not start: %lu", s->Name, dep->Name, dep->StartError);
            error = dep->StartError == ERROR_NOT_SAFEBOOT_SERVICE ? ERROR_NOT_SAFEBOOT_SERVICE
                                                                  : ERROR_SERVICE_DEPENDENCY_FAIL;
            *stuck = FALSE;
            ready = FALSE;
            break;
        }
        if (dep->StartState == SS_NONE) {
            *circular = later_group(s->Group, dep->Group, FALSE);
            if (*circular) {
                SvcLog("%ls depends on %ls, which loads later", s->Name, dep->Name);
                error = ERROR_CIRCULAR_DEPENDENCY;
                ready = FALSE;
                break;
            }
            RunMarkForStart(dep);
            *stuck = FALSE;
        }
        if (dep->StartState != SS_STARTED) {
            ready = FALSE;
            if (dep->StartState != SS_FAILED) {
                *waiting = TRUE;
                if (dep->StartState == SS_MARKED || dep->StartState == SS_BUSY)
                    *stuck = FALSE;
            }
        }
    }
    for (const WCHAR *g = s->DepGroups; !error && g && *g; g += wcslen(g) + 1) {
        SERVICE **members;
        DWORD count = group_members(g, &members);
        DWORD state = group_state(members, count);
        if (state == 2) {
            SvcLog("%ls depends on the group %ls, which has no member started", s->Name, g);
            error = ERROR_SERVICE_DEPENDENCY_FAIL;
            *stuck = FALSE;
            ready = FALSE;
        } else if (state == 0) {
            *circular = later_group(s->Group, g, TRUE);
            if (*circular) {
                SvcLog("%ls depends on the group %ls, which loads later", s->Name, g);
                error = ERROR_CIRCULAR_DEPENDENCY;
            } else {
                for (DWORD i = 0; i < count; i++)
                    RunMarkForStart(members[i]);
                *waiting = TRUE;
            }
            ready = FALSE;
        }
        SvcFree(members);
    }
    if (error)
        cleanup_failure(s, error);
    return ready;
}

/* a started service is done starting when it runs, and failed when it stopped. */
static DWORD update_start_state(SERVICE *s)
{
    if (s->StartState != SS_BUSY)
        return s->StartState;
    DWORD state = state_of(s);
    if (state == SERVICE_START_PENDING)
        return s->StartState;
    s->StartState = state == SERVICE_STOPPED || state == SERVICE_STOP_PENDING ? SS_FAILED : SS_STARTED;
    return s->StartState;
}

/* what the manager does to start one service, and where it stands after. */
static void start_service(SERVICE *s, DWORD count, const WCHAR *const *arguments, DWORD flags)
{
    DWORD error;
    DWORD state = state_of(s);
    BOOL boot = (flags & (START_AUTO | START_DELAYED)) != 0;
    if (s->Start == SERVICE_DISABLED) {
        error = ERROR_SERVICE_DISABLED;
    } else if (state == SERVICE_STOPPED || (driver_of(s) && state == SERVICE_START_PENDING)) {
        /* a program the installation does not have is not started at boot, windows logs an event for each. */
        if ((flags & START_AUTO) && !driver_of(s) && !RunImageExists(s)) {
            s->StartError = ERROR_FILE_NOT_FOUND;
            s->StartState = SS_FAILED;
            return;
        }
        error = launch(s, count, arguments, boot ? 2 : 0);
    } else {
        error = ERROR_SERVICE_ALREADY_RUNNING;
    }
    s->StartError = error;
    state = state_of(s);
    if (!error || error == ERROR_SERVICE_ALREADY_RUNNING) {
        if (state == SERVICE_START_PENDING)
            s->StartState = SS_BUSY;
        else if (state == SERVICE_STOPPED || state == SERVICE_STOP_PENDING)
            s->StartState = SS_FAILED;
        else
            s->StartState = SS_STARTED;
        return;
    }
    if (s->Status.dwWin32ExitCode == ERROR_SERVICE_NEVER_STARTED)
        s->Status.dwWin32ExitCode = error;
    s->StartState = SS_FAILED;
}

/*
 * no service changed state for the whole timeout: the ones still starting get as long as their wait hint to show that
 * they go on, and are failed when they do not.
 */
static void look_for_hung(SERVICE *const *list, DWORD n)
{
    for (DWORD i = 0; i < n; i++) {
        SERVICE *s = list[i];
        if (s->StartState != SS_BUSY || s->Status.dwCurrentState != SERVICE_START_PENDING)
            continue;
        DWORD checkpoint = s->Status.dwCheckPoint, hint = s->Status.dwWaitHint, step, steps;
        if (hint <= 100000) {
            steps = 10;
            step = hint > 10 ? hint / 10 : 1;
        } else {
            step = 10000;
            steps = hint / 10000;
        }
        BOOL moving = FALSE;
        for (DWORD j = 0; j < steps && !moving && !g_shutdown; j++) {
            ULONGLONG until = GetTickCount64() + step;
            for (ULONGLONG now = GetTickCount64(); now < until; now = GetTickCount64())
                SvcSleepChanged((DWORD)(until - now));
            moving = s->Status.dwCurrentState != SERVICE_START_PENDING || s->Status.dwCheckPoint > checkpoint;
        }
        if (moving || g_shutdown)
            continue;
        SvcLog("%ls did not finish starting", s->Name);
        cleanup_failure(s, ERROR_SERVICE_START_HANG);
        IMAGE *img = s->Image;
        if (img && img->Services <= 1 && img->Process && !img->Exited)
            TerminateProcess(img->Process, ERROR_SERVICE_START_HANG);
    }
}

/*
 * starts the marked services of a list, the ones whose dependencies run first, and waits while any is on its way up.
 * For a client's request the target is only waited for until it has started, and at boot until it runs.
 */
static DWORD start_marked(SERVICE *target, BOOL wait_target, DWORD flags, DWORD count, const WCHAR *const *arguments,
                          SERVICE *const *list, DWORD n)
{
    RunStartTimeoutInit();
    for (;;) {
        ULONGLONG stamp = g_stamp;
        BOOL stuck = TRUE, waiting = FALSE, circular = FALSE;
        BOOL pass_stuck = TRUE, pass_waiting = FALSE, in_progress = FALSE;
        for (DWORD i = 0; i < n; i++) {
            SERVICE *x = list[i];
            if (x->StartState == SS_MARKED) {
                if (deps_started(x, &circular, &stuck, &waiting)) {
                    start_service(x, x == target ? count : 0, x == target ? arguments : nullptr, flags);
                    if (x == target && !wait_target) {
                        pass_stuck = stuck;
                    } else {
                        stuck = FALSE;
                        pass_stuck = FALSE;
                    }
                    in_progress = x->StartState == SS_BUSY;
                    pass_waiting = waiting;
                } else {
                    pass_stuck = stuck;
                    pass_waiting = waiting;
                }
            } else if (x->StartState == SS_BUSY) {
                if (target) {
                    if (x == target) {
                        if (!wait_target)
                            continue;
                    } else if (target->StartState == SS_FAILED) {
                        continue;
                    }
                }
                DWORD updated = update_start_state(x);
                if (updated == SS_BUSY) {
                    in_progress = TRUE;
                    pass_stuck = FALSE;
                    stuck = FALSE;
                    if (x != target) {
                        pass_waiting = TRUE;
                        waiting = TRUE;
                    }
                } else {
                    if (updated != SS_STARTED)
                        pass_stuck = FALSE;
                    stuck = pass_stuck;
                }
            }
        }
        if (g_shutdown)
            return ERROR_SHUTDOWN_IN_PROGRESS;
        if (!in_progress || (!pass_waiting && !wait_target)) {
            if ((pass_stuck && pass_waiting) || circular) {
                SvcLog("the services to start depend on each other%ls%ls", target ? L" through " : L"",
                       target ? target->Name : L"");
                return ERROR_CIRCULAR_DEPENDENCY;
            }
            if (pass_stuck)
                return ERROR_SUCCESS;
            continue;
        }
        ULONGLONG deadline = deadline_after(g_start_timeout);
        while (g_stamp == stamp && !g_shutdown) {
            if (!wait_until(deadline)) {
                look_for_hung(list, n);
                break;
            }
        }
    }
}

static DWORD start_in_set(SERVICE *target, START_SET *set, DWORD flags, DWORD count, const WCHAR *const *arguments)
{
    SERVICE **list = sort_set(set);
    if (!list)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD n = set->Count, first_error = 0;
    BOOL wait_target = !(flags & START_CLIENT) && (g_phase1_running || (flags & START_WAIT));
    for (DWORD i = 0; i < n;) {
        DWORD group = DbGroupIndex(list[i]->Group), marked = 0;
        while (i < n && marked < BATCH_MAX) {
            SERVICE *x = list[i];
            if (RunStartableNow(x)) {
                if (DbGroupIndex(x->Group) != group)
                    break;
                if (RunMarkForStart(x))
                    marked++;
            }
            i++;
        }
        if (!marked)
            continue;
        DWORD error = start_marked(target, wait_target, flags, count, arguments, list, n);
        if (error && !first_error)
            first_error = error;
    }
    SvcFree(list);
    return first_error;
}

/* the lock is held. */
DWORD RunStart(SERVICE *s, DWORD count, const WCHAR *const *arguments, DWORD flags)
{
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    if (s->Start == SERVICE_DISABLED)
        return ERROR_SERVICE_DISABLED;
    if (s->MarkedForDelete)
        return ERROR_SERVICE_MARKED_FOR_DELETE;
    if ((flags & START_DELAYED) && !s->DelayedStart)
        return ERROR_INVALID_PARAMETER;
    if (!RunStartableNow(s))
        return ERROR_SERVICE_ALREADY_RUNNING;
    START_SET set = {};
    add_to_set(&set, s, flags);
    DWORD error = start_in_set(s, &set, flags, count, arguments);
    if (!error)
        error = s->StartError;
    set_free(&set);
    return error;
}

/* the lock is held. the roots and everything they need are started together, at boot. */
DWORD RunStartMany(SERVICE *const *roots, DWORD count, DWORD flags)
{
    START_SET set = {};
    for (DWORD i = 0; i < count; i++)
        add_to_set(&set, roots[i], flags);
    DWORD error = start_in_set(nullptr, &set, flags, 0, nullptr);
    set_free(&set);
    return error;
}

/* ------------------------------------------------------------------ controls */

/* the dwControlsAccepted bit a control needs, or 0. */
static DWORD accepted_bit_of(DWORD control)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
        return SERVICE_ACCEPT_STOP;
    case SERVICE_CONTROL_PAUSE:
    case SERVICE_CONTROL_CONTINUE:
        return SERVICE_ACCEPT_PAUSE_CONTINUE;
    case SERVICE_CONTROL_PARAMCHANGE:
        return SERVICE_ACCEPT_PARAMCHANGE;
    case SERVICE_CONTROL_NETBINDADD:
    case SERVICE_CONTROL_NETBINDREMOVE:
    case SERVICE_CONTROL_NETBINDENABLE:
    case SERVICE_CONTROL_NETBINDDISABLE:
        return SERVICE_ACCEPT_NETBINDCHANGE;
    default:
        return 0;
    }
}

/* whether a dependent service is running or starting. */
static BOOL has_running_dependents(SERVICE *s)
{
    SERVICE **list = nullptr;
    DWORD count = 0;
    DbDependents(s, &list, &count, TRUE);
    SvcFree(list);
    return count != 0;
}

/*
 * the answer the manager gives without asking the service, or ERROR_SUCCESS to forward the control.
 * answered_here is set when an interrogation is answered locally.
 */
static DWORD judge_control(SERVICE *s, DWORD control, BOOL *answered_here)
{
    *answered_here = FALSE;
    DWORD state = s->Status.dwCurrentState;
    if (state == SERVICE_STOPPED || !s->Image)
        return ERROR_SERVICE_NOT_ACTIVE;
    if (state == SERVICE_STOP_PENDING)
        return ERROR_SERVICE_CANNOT_ACCEPT_CTRL;
    if (state == SERVICE_START_PENDING && control != SERVICE_CONTROL_STOP) {
        if (control != SERVICE_CONTROL_INTERROGATE)
            return ERROR_SERVICE_CANNOT_ACCEPT_CTRL;
        *answered_here = TRUE;
        return ERROR_SUCCESS;
    }
    DWORD needed = accepted_bit_of(control);
    if (needed && !(s->Status.dwControlsAccepted & needed))
        return ERROR_INVALID_SERVICE_CONTROL;
    if (control == SERVICE_CONTROL_STOP && !g_shutdown && has_running_dependents(s))
        return ERROR_DEPENDENT_SERVICES_RUNNING;
    return ERROR_SUCCESS;
}

/* the lock is released while it waits. */
DWORD RunControl(SERVICE *s, DWORD control, DWORD event_type, const void *event_data, DWORD event_size, DWORD *result)
{
    *result = 0;
    BOOL answered_here;
    DWORD verdict = judge_control(s, control, &answered_here);
    if (verdict || answered_here)
        return verdict;

    IMAGE *image = s->Image;
    UINT64 status_id = s->StatusId;
    WCHAR name[SVC_NAME_MAX + 1];
    SvcCopyName(name, s->Name, SVC_NAME_MAX);
    image->Refs++;
    LeaveCriticalSection(&g_lock);
    DWORD error = send_control(image, control, name, event_type, event_data, event_size, status_id, 0, nullptr,
                               g_handler_timeout, result);
    EnterCriticalSection(&g_lock);
    image_release(image);

    if (!error && control == SERVICE_CONTROL_STOP)
        s->StopRequested = TRUE;
    return error;
}

BOOL RunStopSent(SERVICE *s)
{
    return s->StopRequested;
}

/* the lock is held. */
DWORD RunWaitState(SERVICE *s, DWORD wanted, DWORD timeout)
{
    ULONGLONG deadline = deadline_after(timeout);
    while (s->Status.dwCurrentState != wanted) {
        if (!wait_until(deadline))
            return ERROR_SERVICE_REQUEST_TIMEOUT;
    }
    return ERROR_SUCCESS;
}

void RunStopAllForShutdown(void)
{
}
