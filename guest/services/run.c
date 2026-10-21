/*
 * services.exe: the processes of services and what runs in them. an image is a process the manager started for
 * services; it connects to the manager through the named pipe, which becomes its channel, and the manager sends
 * it one control at a time and waits for the answer of each.
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
 * one control to the process of a service and the answer to it, which is the error the handler returned. the
 * lock is not held; the control lock of the image makes the controls of a process go one at a time.
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
            /* nothing follows an answer today; whatever does is dropped. */
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

/* ends a process once its last service has stopped: the dispatcher is told, and the process is given time. */
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
        /* windows waits twenty seconds for the process to leave of itself, and then ends it. */
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
        /* a driver is running when the object manager knows it. */
        out->dwCurrentState = DbDriverRunning(s) ? SERVICE_RUNNING : SERVICE_STOPPED;
        out->dwControlsAccepted = out->dwCurrentState == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP : 0;
        if (out->dwCurrentState == SERVICE_STOPPED && out->dwWin32ExitCode == ERROR_SERVICE_NEVER_STARTED)
            out->dwWin32ExitCode = 0;
    }
    if (s->Image && s->Status.dwCurrentState != SERVICE_STOPPED)
        out->dwProcessId = s->Image->Pid;
    out->dwServiceFlags = s->ServiceFlags;
}

/* every change of state goes through here: it stamps the record, wakes the waiters and tells the watchers. */
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
            /* %1% stands for the number of failures. */
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
            RunStart(s, 0, nullptr, 1);
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
 * the recovery action a failure calls for. the count of failures picks the action, the last one repeating; it
 * starts over after the reset period (read: CWin32ServiceRecord::GetNextRecoveryAction and QueueRecoveryAction).
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

/* the types a service may report, with the interactive bit ignored. */
static BOOL reportable_type(DWORD type)
{
    switch (type & ~(DWORD)SERVICE_INTERACTIVE_PROCESS) {
    case SERVICE_WIN32_OWN_PROCESS:
    case SERVICE_WIN32_SHARE_PROCESS:
    case SERVICE_WIN32:
    case SERVICE_USER_OWN_PROCESS:
    case SERVICE_USER_SHARE_PROCESS:
    case 0xD0: /* a user own process instance */
    case 0xE0: /* a user share process instance */
    case 0x210: /* an own process of a package */
        return TRUE;
    default:
        return type == 11;
    }
}

/* the bits of dwControlsAccepted that are defined, a few of them for the newest systems only. */
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
 * what a service process reports. exit codes only mean something once the service stops, and a checkpoint with a wait
 * hint only while it is on its way somewhere. a stopped service accepts no control. a service that has stopped lets go of its handle, and its process
 * is ended once no other service lives in it.
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

/* ------------------------------------------------------------------ the channel of a process */

/* a process the manager started connects: it must be one it is waiting for. */
DWORD RunOpenChannel(HANDLE pipe, DWORD pid)
{
    for (IMAGE *img = g_images; img; img = img->Next) {
        if (img->Pid == pid && !img->Exited && !img->Connected && !img->FirstSent) {
            img->FirstSent = TRUE; /* reserved while the answer is written */
            return ERROR_SUCCESS;
        }
    }
    (void)pipe;
    return ERROR_FAILED_SERVICE_CONTROLLER_CONNECT;
}

/* the answer is written; the pipe is the image's from now on. */
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
    /* the program of a command line: quoted, or up to the first space that makes a file. */
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

/* the account of a service as it compares: the names of the three built in ones are one each, the rest in lower case. */
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
 * starts the process of a service. nothing is held while the process is made, and the lock again once it is.
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
    if (token_error) {
        ok = FALSE;
        SetLastError(token_error);
    } else if (token) {
        AccessEnablePrivilege(L"SeAssignPrimaryTokenPrivilege");
        AccessEnablePrivilege(L"SeIncreaseQuotaPrivilege");
        ok = CreateProcessAsUserW(token, nullptr, line, nullptr, nullptr, FALSE, flags, nullptr, n ? directory : nullptr, &si, &pi);
    } else {
        ok = CreateProcessW(nullptr, line, nullptr, nullptr, FALSE, flags, nullptr, n ? directory : nullptr, &si, &pi);
    }
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

/* waits on the condition until the deadline; the lock is held on entry and on return. */
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

/* the program of a service that is in the database, its command line. */
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

/* whether the command line starts svchost.exe, whatever its directory. */
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
 * a service of svchost.exe lives in the dll its Parameters key names (or its own key, when it has no Parameters). the
 * manager does not start the ones whose dll is not in the installation at the boot, as it does not start a program
 * that is not there: the installation has hundreds of them, each of which would make a process to say it has no dll.
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

/* whether the program of a service is there to be started. */
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
    /* a program that is not there is not started: the answer windows gives. */
    if (!(s->Type & SERVICE_DRIVER) && !file_exists(command)) {
        SvcFree(command);
        return ERROR_FILE_NOT_FOUND;
    }
    if (s->Type & SERVICE_DRIVER) {
        /* a kernel driver cannot be loaded here. */
        BOOL there = file_exists(command) || wcsstr(command, L"\\SystemRoot\\") || wcschr(command, L'\\');
        SvcFree(command);
        return there ? ERROR_NOT_SUPPORTED : ERROR_FILE_NOT_FOUND;
    }
    WCHAR *key = image_key(command);
    BOOL own = (s->Type & SERVICE_WIN32_OWN_PROCESS) != 0;
    IMAGE *img = own ? nullptr : find_image(key);
    SvcFree(key);
    /* the services of one process run as one account: a service that wants another does not join it. */
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

    /* the process has to open its channel in time; one that ends first, or does not come, is not a service. */
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

    /* the first control: this service starts. a process of many services takes it for one of them. */
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

/* waits for a service that is on its way up: running, or paused, or failed. */
static DWORD wait_running(SERVICE *s)
{
    DWORD last_check = s->Status.dwCheckPoint;
    ULONGLONG deadline = deadline_after(s->Status.dwWaitHint > 2000 ? s->Status.dwWaitHint : 30000);
    for (;;) {
        DWORD state = s->Status.dwCurrentState;
        if (state == SERVICE_RUNNING || state == SERVICE_PAUSED || state == SERVICE_PAUSE_PENDING ||
            state == SERVICE_CONTINUE_PENDING)
            return ERROR_SUCCESS;
        if (state == SERVICE_STOPPED || state == SERVICE_STOP_PENDING)
            return ERROR_SERVICE_DEPENDENCY_FAIL;
        if (s->Status.dwCheckPoint != last_check) {
            last_check = s->Status.dwCheckPoint;
            DWORD hint = s->Status.dwWaitHint;
            deadline = deadline_after(hint > 2000 ? hint : 2000);
        }
        if (!wait_until(deadline))
            return ERROR_SERVICE_REQUEST_TIMEOUT;
        if (g_shutdown)
            return ERROR_SHUTDOWN_IN_PROGRESS;
    }
}

static DWORD start_dependencies(SERVICE *s, DWORD depth, DWORD reason);

static DWORD start_with_dependencies(SERVICE *s, DWORD count, const WCHAR *const *arguments, DWORD depth, DWORD reason)
{
    if (depth > 32)
        return ERROR_CIRCULAR_DEPENDENCY;
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    DWORD state = s->Status.dwCurrentState;
    if (state == SERVICE_STOPPED) {
        DWORD error = start_dependencies(s, depth, reason);
        if (error)
            return error;
        /* the dependencies took time: another thread may have started it meanwhile. */
        if (s->Status.dwCurrentState != SERVICE_STOPPED)
            return depth ? wait_running(s) : ERROR_SERVICE_ALREADY_RUNNING;
        return launch(s, count, arguments, reason);
    }
    if (!depth)
        return ERROR_SERVICE_ALREADY_RUNNING;
    return ERROR_SUCCESS;
}

/* the services a service depends on, started first and waited for. */
static DWORD start_dependencies(SERVICE *s, DWORD depth, DWORD reason)
{
    s->StartPending++;
    DWORD result = ERROR_SUCCESS;
    for (const WCHAR *d = s->DepServices; d && *d && !result; d += wcslen(d) + 1) {
        SERVICE *dep = DbFind(d);
        if (!dep || !DbLoadRecord(dep) || dep->MarkedForDelete) {
            result = ERROR_SERVICE_DEPENDENCY_DELETED;
            break;
        }
        if (dep->Start == SERVICE_DISABLED && dep->Status.dwCurrentState == SERVICE_STOPPED) {
            result = ERROR_SERVICE_DEPENDENCY_FAIL;
            break;
        }
        /* one that is going down is waited for, then started again. */
        ULONGLONG deadline = deadline_after(30000);
        while (dep->Status.dwCurrentState == SERVICE_STOP_PENDING && wait_until(deadline))
            ;
        DWORD state = dep->Status.dwCurrentState;
        if (state == SERVICE_STOPPED) {
            if (dep->Type & SERVICE_DRIVER) {
                if (!DbDriverRunning(dep))
                    result = ERROR_SERVICE_DEPENDENCY_FAIL;
                continue;
            }
            DWORD error = start_with_dependencies(dep, 0, nullptr, depth + 1, reason);
            if (error == ERROR_SERVICE_ALREADY_RUNNING)
                error = ERROR_SUCCESS;
            if (error) {
                result = error == ERROR_SERVICE_DEPENDENCY_DELETED || error == ERROR_CIRCULAR_DEPENDENCY ? error
                                                                                                        : ERROR_SERVICE_DEPENDENCY_FAIL;
                break;
            }
            state = dep->Status.dwCurrentState;
        }
        if (state == SERVICE_START_PENDING)
            result = wait_running(dep) ? ERROR_SERVICE_DEPENDENCY_FAIL : ERROR_SUCCESS;
        else if (dep->Status.dwCurrentState == SERVICE_STOPPED)
            result = ERROR_SERVICE_DEPENDENCY_FAIL;
    }
    for (const WCHAR *g = s->DepGroups; g && *g && !result; g += wcslen(g) + 1) {
        /* a group is there when one of its members runs: they are started, and the first that comes up will do. */
        BOOL members = FALSE, running = FALSE;
        for (SERVICE *m = g_services; m; m = m->Next) {
            if (m == s || !DbLoadRecord(m) || !m->Group || _wcsicmp(m->Group, g))
                continue;
            members = TRUE;
            if (m->Status.dwCurrentState == SERVICE_RUNNING || m->Status.dwCurrentState == SERVICE_PAUSED)
                running = TRUE;
        }
        if (!members || running)
            continue;
        for (SERVICE *m = g_services; m; m = m->Next) {
            if (m == s || !DbLoadRecord(m) || !m->Group || _wcsicmp(m->Group, g) || m->Start == SERVICE_DISABLED ||
                (m->Type & SERVICE_DRIVER))
                continue;
            if (m->Status.dwCurrentState == SERVICE_STOPPED) {
                if (start_with_dependencies(m, 0, nullptr, depth + 1, reason) == ERROR_SUCCESS &&
                    m->Status.dwCurrentState == SERVICE_START_PENDING)
                    wait_running(m);
            }
            if (m->Status.dwCurrentState == SERVICE_RUNNING || m->Status.dwCurrentState == SERVICE_PAUSED) {
                running = TRUE;
                break;
            }
        }
        if (!running)
            result = ERROR_SERVICE_DEPENDENCY_FAIL;
    }
    s->StartPending--;
    return result;
}

/* StartService: the lock is held. */
DWORD RunStart(SERVICE *s, DWORD count, const WCHAR *const *arguments, DWORD reason)
{
    if (g_shutdown)
        return ERROR_SHUTDOWN_IN_PROGRESS;
    if (s->Start == SERVICE_DISABLED)
        return ERROR_SERVICE_DISABLED;
    if (s->MarkedForDelete)
        return ERROR_SERVICE_MARKED_FOR_DELETE;
    if (s->Status.dwCurrentState != SERVICE_STOPPED && !(s->Type & SERVICE_DRIVER))
        return ERROR_SERVICE_ALREADY_RUNNING;
    DWORD error = start_with_dependencies(s, count, arguments, 0, reason);
    /* a service that never ran says why it did not: the exit code it has until then is "never started". */
    if (error && error != ERROR_SERVICE_ALREADY_RUNNING && s->Status.dwCurrentState == SERVICE_STOPPED &&
        s->Status.dwWin32ExitCode == ERROR_SERVICE_NEVER_STARTED)
        s->Status.dwWin32ExitCode = error;
    return error;
}

/* ------------------------------------------------------------------ controls */

/* the bit of dwControlsAccepted a control needs, or 0 for a control that needs none. */
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

/* whether a service that depends on this one is up, or on its way. */
static BOOL has_running_dependents(SERVICE *s)
{
    SERVICE **list = nullptr;
    DWORD count = 0;
    DbDependents(s, &list, &count, TRUE);
    SvcFree(list);
    return count != 0;
}

/*
 * what state a control meets: the answer the manager gives without asking the service, or ERROR_SUCCESS when the
 * control is to go to the process. the second value tells that an interrogation is answered here.
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

/* sends a control to the process of a service, with the lock released while it waits. */
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

/* waits for a state; the lock is held. */
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
