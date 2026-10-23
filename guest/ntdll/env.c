/*
 * process environment blocks: lookups, updates and expansion.
 *
 * an environment is a sequence of "name=value" strings terminated by an
 * empty string. names compare without case.
 */
#include "ntdll_private.h"

static PWSTR *current_env(PWSTR env)
{
    (void)env;
    return (PWSTR *)&NtCurrentPeb()->ProcessParameters->Environment;
}

static SIZE_T env_size(PCWSTR env)
{
    PCWSTR p = env;
    while (*p)
        p += wcslen(p) + 1;
    return (SIZE_T)(p - env + 1);
}

static PCWSTR find_var(PCWSTR env, PCWSTR name, SIZE_T name_chars)
{
    for (PCWSTR p = env; *p; p += wcslen(p) + 1) {
        /* in entries such as "=C:=C:\" the leading '=' is part of the name. */
        PCWSTR eq = wcschr(p + 1, '=');
        if (eq && (SIZE_T)(eq - p) == name_chars && !_wcsnicmp(p, name, name_chars))
            return p;
    }
    return nullptr;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlQueryEnvironmentVariable(PWSTR env, PCWSTR name, SIZE_T name_chars, PWSTR value,
                                                        SIZE_T value_chars, PSIZE_T return_chars)
{
    BOOLEAN own = !env;
    if (own) {
        RtlAcquirePebLock();
        env = *current_env(nullptr);
    }
    NTSTATUS status = STATUS_VARIABLE_NOT_FOUND;
    PCWSTR entry = env ? find_var(env, name, name_chars) : nullptr;
    if (entry) {
        PCWSTR v = entry + name_chars + 1;
        SIZE_T n = wcslen(v);
        *return_chars = n;
        if (n < value_chars) {
            memcpy(value, v, (n + 1) * sizeof(WCHAR));
            status = STATUS_SUCCESS;
        } else {
            *return_chars = n + 1;
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    if (own)
        RtlReleasePebLock();
    return status;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlQueryEnvironmentVariable_U(PWSTR env, PCUNICODE_STRING name, PUNICODE_STRING value)
{
    SIZE_T chars = 0;
    NTSTATUS status = RtlQueryEnvironmentVariable(env, name->Buffer, name->Length / sizeof(WCHAR), value->Buffer,
                                                  value->MaximumLength / sizeof(WCHAR), &chars);
    if (status == STATUS_SUCCESS)
        value->Length = (USHORT)(chars * sizeof(WCHAR));
    else if (status == STATUS_BUFFER_TOO_SMALL)
        value->Length = (USHORT)((chars - 1) * sizeof(WCHAR));
    return status;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlCreateEnvironment(BOOLEAN clone, PWSTR *out)
{
    PVOID heap = NtCurrentPeb()->ProcessHeap;
    if (clone) {
        RtlAcquirePebLock();
        PCWSTR env = *current_env(nullptr);
        SIZE_T n = env ? env_size(env) : 1;
        PWSTR copy = RtlAllocateHeap(heap, 0, n * sizeof(WCHAR));
        if (copy) {
            if (env)
                memcpy(copy, env, n * sizeof(WCHAR));
            else
                copy[0] = 0;
        }
        RtlReleasePebLock();
        *out = copy;
        return copy ? STATUS_SUCCESS : STATUS_NO_MEMORY;
    }
    PWSTR env = RtlAllocateHeap(heap, HEAP_ZERO_MEMORY, 2 * sizeof(WCHAR));
    *out = env;
    return env ? STATUS_SUCCESS : STATUS_NO_MEMORY;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlDestroyEnvironment(PWSTR env)
{
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, env);
    return STATUS_SUCCESS;
}

/*
 * a NULL value removes the variable. the process environment is replaced
 * by a heap copy; the original block came from the kernel and is never
 * freed.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlSetEnvironmentVariable(PWSTR *env_ptr, PCUNICODE_STRING name, PCUNICODE_STRING value)
{
    SIZE_T name_chars = name->Length / sizeof(WCHAR);
    if (!name_chars)
        return STATUS_INVALID_PARAMETER;
    PVOID heap = NtCurrentPeb()->ProcessHeap;
    BOOLEAN own = !env_ptr;
    if (own) {
        RtlAcquirePebLock();
        env_ptr = current_env(nullptr);
    }
    PCWSTR env = *env_ptr ? *env_ptr : L"";
    SIZE_T old_size = env_size(env);
    PCWSTR entry = find_var(env, name->Buffer, name_chars);
    SIZE_T entry_chars = entry ? wcslen(entry) + 1 : 0;
    SIZE_T value_chars = value ? value->Length / sizeof(WCHAR) : 0;
    SIZE_T add = value ? name_chars + 1 + value_chars + 1 : 0;
    SIZE_T new_size = old_size - entry_chars + add;
    PWSTR fresh = RtlAllocateHeap(heap, 0, new_size * sizeof(WCHAR));
    if (!fresh) {
        if (own)
            RtlReleasePebLock();
        return STATUS_NO_MEMORY;
    }
    /* keep the block sorted like Windows: insert before the first greater name. */
    PWSTR out = fresh;
    BOOLEAN inserted = !value;
    for (PCWSTR p = env; *p; p += wcslen(p) + 1) {
        if (p == entry)
            continue;
        if (!inserted && p[0] != '=' && _wcsnicmp(p, name->Buffer, name_chars) > 0) {
            memcpy(out, name->Buffer, name_chars * sizeof(WCHAR));
            out += name_chars;
            *out++ = '=';
            memcpy(out, value->Buffer, value_chars * sizeof(WCHAR));
            out += value_chars;
            *out++ = 0;
            inserted = TRUE;
        }
        SIZE_T n = wcslen(p) + 1;
        memcpy(out, p, n * sizeof(WCHAR));
        out += n;
    }
    if (!inserted) {
        memcpy(out, name->Buffer, name_chars * sizeof(WCHAR));
        out += name_chars;
        *out++ = '=';
        memcpy(out, value->Buffer, value_chars * sizeof(WCHAR));
        out += value_chars;
        *out++ = 0;
    }
    *out = 0;
    static PWSTR g_heap_env;
    PWSTR old = *env_ptr;
    *env_ptr = fresh;
    if (own) {
        if (old && old == g_heap_env)
            RtlFreeHeap(heap, 0, old);
        g_heap_env = fresh;
        NtCurrentPeb()->ProcessParameters->EnvironmentSize = new_size * sizeof(WCHAR);
        RtlReleasePebLock();
    } else if (old) {
        RtlFreeHeap(heap, 0, old);
    }
    return STATUS_SUCCESS;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlSetCurrentEnvironment(PWSTR env, PWSTR *old)
{
    RtlAcquirePebLock();
    PWSTR *slot = current_env(nullptr);
    if (old)
        *old = *slot;
    *slot = env;
    RtlReleasePebLock();
    return STATUS_SUCCESS;
}

/* unknown %name% references stay as written. */
NTDLL_EXPORT NTSTATUS NTAPI RtlExpandEnvironmentStrings(PWSTR env, PCWSTR src, SIZE_T src_chars, PWSTR dst,
                                                        SIZE_T dst_chars, PSIZE_T return_chars)
{
    SIZE_T out = 0;
    SIZE_T i = 0;
    WCHAR value[1024];
    while (i < src_chars && src[i]) {
        if (src[i] == '%') {
            SIZE_T j = i + 1;
            while (j < src_chars && src[j] && src[j] != '%')
                j++;
            if (j < src_chars && src[j] == '%' && j > i + 1) {
                SIZE_T got;
                if (RtlQueryEnvironmentVariable(env, src + i + 1, j - i - 1, value, RTL_NUMBER_OF(value), &got) ==
                    STATUS_SUCCESS) {
                    for (SIZE_T k = 0; k < got; k++, out++)
                        if (out < dst_chars)
                            dst[out] = value[k];
                    i = j + 1;
                    continue;
                }
            }
        }
        if (out < dst_chars)
            dst[out] = src[i];
        out++;
        i++;
    }
    if (out < dst_chars)
        dst[out] = 0;
    out++;
    if (return_chars)
        *return_chars = out;
    return out <= dst_chars ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlExpandEnvironmentStrings_U(PWSTR env, PCUNICODE_STRING src, PUNICODE_STRING dst,
                                                          PULONG return_bytes)
{
    SIZE_T chars = 0;
    NTSTATUS status = RtlExpandEnvironmentStrings(env, src->Buffer, src->Length / sizeof(WCHAR), dst->Buffer,
                                                  dst->MaximumLength / sizeof(WCHAR), &chars);
    if (return_bytes)
        *return_bytes = (ULONG)(chars * sizeof(WCHAR));
    if (NT_SUCCESS(status))
        dst->Length = (USHORT)((chars - 1) * sizeof(WCHAR));
    return status;
}
