/* environment variables. */
#include "kernelbase_private.h"

DWORD WINAPI GetEnvironmentVariableW(LPCWSTR name, LPWSTR buffer, DWORD size)
{
    if (!name || !*name) {
        KbSetError(ERROR_ENVVAR_NOT_FOUND);
        return 0;
    }
    SIZE_T chars = 0;
    NTSTATUS status = RtlQueryEnvironmentVariable(nullptr, name, wcslen(name), buffer, buffer ? size : 0, &chars);
    if (status == STATUS_BUFFER_TOO_SMALL)
        return (DWORD)chars;
    if (!NT_SUCCESS(status)) {
        KbSetError(status == STATUS_VARIABLE_NOT_FOUND ? ERROR_ENVVAR_NOT_FOUND : RtlNtStatusToDosError(status));
        return 0;
    }
    if (!chars)
        KbSetError(ERROR_SUCCESS);
    return (DWORD)chars;
}

DWORD WINAPI GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size)
{
    PWSTR wname = KbAnsiToWideCp(CP_ACP, name);
    if (!wname) {
        if (!name)
            KbSetError(ERROR_ENVVAR_NOT_FOUND);
        return 0;
    }
    DWORD chars = GetEnvironmentVariableW(wname, nullptr, 0);
    DWORD result = 0;
    if (chars) {
        PWSTR value = KbAlloc((SIZE_T)chars * sizeof(WCHAR));
        if (!value) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        } else {
            DWORD got = GetEnvironmentVariableW(wname, value, chars);
            if (got < chars && (got || GetLastError() == ERROR_SUCCESS)) {
                int need = WideCharToMultiByte(CP_ACP, 0, value, -1, nullptr, 0, nullptr, nullptr);
                if ((DWORD)need > size || !buffer) {
                    result = (DWORD)need;
                } else {
                    WideCharToMultiByte(CP_ACP, 0, value, -1, buffer, (int)size, nullptr, nullptr);
                    result = (DWORD)need - 1;
                }
            }
            KbFree(value);
        }
    }
    KbFree(wname);
    return result;
}

static BOOL valid_name(LPCWSTR name)
{
    return name && *name && !wcschr(name + 1, '=');
}

BOOL WINAPI SetEnvironmentVariableW(LPCWSTR name, LPCWSTR value)
{
    if (!valid_name(name)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UNICODE_STRING n, v;
    RtlInitUnicodeString(&n, name);
    if (value)
        RtlInitUnicodeString(&v, value);
    NTSTATUS status = RtlSetEnvironmentVariable(nullptr, &n, value ? &v : nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI SetEnvironmentVariableA(LPCSTR name, LPCSTR value)
{
    PWSTR wname = KbAnsiToWideCp(CP_ACP, name), wvalue = KbAnsiToWideCp(CP_ACP, value);
    BOOL ok = FALSE;
    if (wname && (wvalue || !value))
        ok = SetEnvironmentVariableW(wname, wvalue);
    else if (!name)
        KbSetError(ERROR_INVALID_PARAMETER);
    KbFree(wname);
    KbFree(wvalue);
    return ok;
}

static SIZE_T block_chars(PCWSTR env)
{
    PCWSTR p = env;
    while (*p)
        p += wcslen(p) + 1;
    return (SIZE_T)(p - env) + 1;
}

LPWCH WINAPI GetEnvironmentStringsW(void)
{
    RtlAcquirePebLock();
    PCWSTR env = KbParams()->Environment;
    SIZE_T n = env ? block_chars(env) : 1;
    PWSTR copy = KbAlloc((n + 1) * sizeof(WCHAR));
    if (copy) {
        if (env)
            memcpy(copy, env, n * sizeof(WCHAR));
        else
            copy[0] = 0;
        copy[n] = 0;
    } else {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
    }
    RtlReleasePebLock();
    return copy;
}

LPCH WINAPI GetEnvironmentStringsA(void)
{
    LPWCH w = GetEnvironmentStringsW();
    if (!w)
        return nullptr;
    SIZE_T n = block_chars(w);
    int bytes = WideCharToMultiByte(CP_ACP, 0, w, (int)n, nullptr, 0, nullptr, nullptr);
    LPCH a = KbAlloc((SIZE_T)bytes + 1);
    if (a) {
        WideCharToMultiByte(CP_ACP, 0, w, (int)n, a, bytes, nullptr, nullptr);
        a[bytes] = 0;
    } else {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
    }
    KbFree(w);
    return a;
}

LPCH WINAPI GetEnvironmentStrings(void)
{
    return GetEnvironmentStringsA();
}

BOOL WINAPI FreeEnvironmentStringsW(LPWCH env)
{
    KbFree(env);
    return TRUE;
}

BOOL WINAPI FreeEnvironmentStringsA(LPCH env)
{
    KbFree(env);
    return TRUE;
}

BOOL WINAPI SetEnvironmentStringsW(LPWCH env)
{
    if (!env) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (PCWSTR p = env; *p; p += wcslen(p) + 1) {
        PCWSTR eq = wcschr(p + 1, '=');
        if (!eq) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    }
    SIZE_T n = block_chars(env);
    PWSTR copy = KbAlloc(n * sizeof(WCHAR));
    if (!copy) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memcpy(copy, env, n * sizeof(WCHAR));
    PWSTR old = nullptr;
    RtlSetCurrentEnvironment(copy, &old);
    KbParams()->EnvironmentSize = n * sizeof(WCHAR);
    /* the old block may be the initial one from the kernel, which is never freed. */
    return TRUE;
}

DWORD WINAPI ExpandEnvironmentStringsW(LPCWSTR src, LPWSTR dst, DWORD size)
{
    if (!src) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    SIZE_T chars = 0;
    NTSTATUS status = RtlExpandEnvironmentStrings(nullptr, src, wcslen(src), dst, dst ? size : 0, &chars);
    if (!NT_SUCCESS(status) && status != STATUS_BUFFER_TOO_SMALL) {
        KbSetStatus(status);
        return 0;
    }
    if (chars > 32767) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return (DWORD)chars;
}

DWORD WINAPI ExpandEnvironmentStringsA(LPCSTR src, LPSTR dst, DWORD size)
{
    PWSTR wsrc = KbAnsiToWideCp(CP_ACP, src);
    if (!wsrc) {
        if (!src)
            KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    DWORD chars = ExpandEnvironmentStringsW(wsrc, nullptr, 0);
    DWORD result = 0;
    PWSTR expanded = chars ? KbAlloc((SIZE_T)chars * sizeof(WCHAR)) : nullptr;
    if (expanded && ExpandEnvironmentStringsW(wsrc, expanded, chars)) {
        int need = WideCharToMultiByte(CP_ACP, 0, expanded, -1, nullptr, 0, nullptr, nullptr);
        if (dst && size >= (DWORD)need)
            WideCharToMultiByte(CP_ACP, 0, expanded, -1, dst, (int)size, nullptr, nullptr);
        result = (DWORD)need;
    } else if (chars && !expanded) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
    }
    KbFree(expanded);
    KbFree(wsrc);
    return result;
}
