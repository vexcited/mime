/*
 * InitiateSystemShutdown and AbortSystemShutdown. on windows these are rpc calls to the shutdown service, which checks
 * the caller's privilege and runs ExitWindowsEx after the timeout; here the kernel plays that service.
 */
#include "advapi32_private.h"

#define ERROR_NO_SHUTDOWN_IN_PROGRESS 1116L
#ifndef ERROR_SHUTDOWN_IN_PROGRESS
#define ERROR_SHUTDOWN_IN_PROGRESS 1115L
#endif

/* the flags of InitiateShutdown. */
#define SHUTDOWN_FORCE_OTHERS 0x00000001
#define SHUTDOWN_FORCE_SELF 0x00000002
#define SHUTDOWN_RESTART 0x00000004
#define SHUTDOWN_POWEROFF 0x00000008
#define SHUTDOWN_NOREBOOT 0x00000010
#define SHUTDOWN_GRACE_OVERRIDE 0x00000020
#define SHUTDOWN_INSTALL_UPDATES 0x00000040
#define SHUTDOWN_RESTART_BOOTOPTIONS 0x00000400
#define SHUTDOWN_HYBRID 0x00000200
/* InitiateSystemShutdown's reason: SHTDN_REASON_FLAG_PLANNED with the legacy api as the major reason. */
#define LEGACY_REASON 0x80070000u
/* timeouts of ten years or more are refused. */
#define MAX_TIMEOUT 0x12CC0300u

static BOOL request(MX_SHUTDOWN_REQUEST *r)
{
    NTSTATUS status = NtSetSystemInformation((SYSTEM_INFORMATION_CLASS)MX_SYSTEM_SHUTDOWN_REQUEST, r, sizeof(*r));
    if (NT_SUCCESS(status))
        return TRUE;
    /* the service denies a caller without the privilege, and reports no shutdown when there is none to stop. */
    switch (status) {
    case STATUS_PRIVILEGE_NOT_HELD:
        AdvSetError(ERROR_ACCESS_DENIED);
        break;
    case STATUS_NOT_FOUND:
        AdvSetError(ERROR_NO_SHUTDOWN_IN_PROGRESS);
        break;
    default:
        AdvSetStatus(status);
        break;
    }
    return FALSE;
}

/* the verdict on a request's message and timeout, before the machine is checked. */
typedef enum REQUEST_VERDICT {
    REQUEST_FINE,
    REQUEST_REFUSED_QUIETLY, /* too long to send at all: the call fails and the last error is left unchanged. */
    REQUEST_INVALID,         /* a message of 512 characters or more, or a timeout of ten years. */
} REQUEST_VERDICT;

#define MAX_MESSAGE_BYTES 0xFFFE
#define MAX_MESSAGE_CHARS 511

static REQUEST_VERDICT judge_request(LPCWSTR message, DWORD timeout)
{
    SIZE_T chars = message ? wcslen(message) : 0;
    if (chars * sizeof(WCHAR) > MAX_MESSAGE_BYTES)
        return REQUEST_REFUSED_QUIETLY;
    if (chars > MAX_MESSAGE_CHARS || timeout >= MAX_TIMEOUT)
        return REQUEST_INVALID;
    return REQUEST_FINE;
}

/* client side checks in order: message and timeout, then the machine. */
static BOOL initiate(LPCWSTR machine, LPCWSTR message, DWORD timeout, ULONG flags, DWORD reason)
{
    REQUEST_VERDICT verdict = judge_request(message, timeout);
    if (verdict == REQUEST_REFUSED_QUIETLY)
        return FALSE;
    if (verdict == REQUEST_INVALID) {
        AdvSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!AdvIsLocalSystem(machine)) {
        AdvSetError(RPC_S_SERVER_UNAVAILABLE);
        return FALSE;
    }
    MX_SHUTDOWN_REQUEST r = {flags, reason, timeout, 0};
    return request(&r);
}

/* the timeout runs down before anything starts; after it, apps that do not answer are forced. */
static ULONG ewx_flags(BOOL force, BOOL reboot)
{
    return (reboot ? EWX_REBOOT : EWX_SHUTDOWN) | (force ? EWX_FORCE : EWX_FORCEIFHUNG);
}

ADVAPI BOOL WINAPI InitiateSystemShutdownExW(LPWSTR machine, LPWSTR message, DWORD timeout, BOOL force, BOOL reboot,
                                             DWORD reason)
{
    return initiate(machine, message, timeout, ewx_flags(force, reboot), reason);
}

ADVAPI BOOL WINAPI InitiateSystemShutdownW(LPWSTR machine, LPWSTR message, DWORD timeout, BOOL force, BOOL reboot)
{
    return initiate(machine, message, timeout, ewx_flags(force, reboot), LEGACY_REASON);
}

ADVAPI BOOL WINAPI InitiateSystemShutdownExA(LPSTR machine, LPSTR message, DWORD timeout, BOOL force, BOOL reboot,
                                             DWORD reason)
{
    PWSTR m = AdvAnsiToWide(machine), t = AdvAnsiToWide(message);
    BOOL ok = FALSE;
    if ((!machine || m) && (!message || t))
        ok = InitiateSystemShutdownExW(m, t, timeout, force, reboot, reason);
    AdvFree(m);
    AdvFree(t);
    return ok;
}

ADVAPI BOOL WINAPI InitiateSystemShutdownA(LPSTR machine, LPSTR message, DWORD timeout, BOOL force, BOOL reboot)
{
    return InitiateSystemShutdownExA(machine, message, timeout, force, reboot, LEGACY_REASON);
}

ADVAPI BOOL WINAPI AbortSystemShutdownW(LPWSTR machine)
{
    if (!AdvIsLocalSystem(machine)) {
        AdvSetError(RPC_S_SERVER_UNAVAILABLE);
        return FALSE;
    }
    MX_SHUTDOWN_REQUEST r = {0, 0, 0, MX_SHUTDOWN_ABORT};
    return request(&r);
}

ADVAPI BOOL WINAPI AbortSystemShutdownA(LPSTR machine)
{
    PWSTR m = AdvAnsiToWide(machine);
    BOOL ok = FALSE;
    if (!machine || m)
        ok = AbortSystemShutdownW(m);
    AdvFree(m);
    return ok;
}

/* InitiateShutdown: the flags choose what happens and whether programs are forced; the grace period is the timeout. */
ADVAPI DWORD WINAPI InitiateShutdownW(LPWSTR machine, LPWSTR message, DWORD grace, DWORD flags, DWORD reason)
{
    if (judge_request(message, grace) != REQUEST_FINE)
        return ERROR_INVALID_PARAMETER;
    if (!AdvIsLocalSystem(machine))
        return RPC_S_SERVER_UNAVAILABLE;
    ULONG ewx = EWX_SHUTDOWN;
    if (flags & SHUTDOWN_RESTART)
        ewx = EWX_REBOOT;
    else if (flags & SHUTDOWN_POWEROFF)
        ewx = EWX_POWEROFF;
    if (flags & SHUTDOWN_HYBRID)
        ewx |= EWX_HYBRID_SHUTDOWN;
    if (flags & SHUTDOWN_RESTART_BOOTOPTIONS)
        ewx |= EWX_BOOTOPTIONS;
    ewx |= (flags & (SHUTDOWN_FORCE_OTHERS | SHUTDOWN_FORCE_SELF)) ? EWX_FORCE : EWX_FORCEIFHUNG;
    MX_SHUTDOWN_REQUEST r = {ewx, reason, grace, 0};
    if (request(&r))
        return ERROR_SUCCESS;
    return GetLastError();
}

ADVAPI DWORD WINAPI InitiateShutdownA(LPSTR machine, LPSTR message, DWORD grace, DWORD flags, DWORD reason)
{
    PWSTR m = AdvAnsiToWide(machine), t = AdvAnsiToWide(message);
    DWORD error = ERROR_NOT_ENOUGH_MEMORY;
    if ((!machine || m) && (!message || t))
        error = InitiateShutdownW(m, t, grace, flags, reason);
    AdvFree(m);
    AdvFree(t);
    return error;
}
