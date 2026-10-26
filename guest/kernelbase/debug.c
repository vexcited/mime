/* exceptions and debugging. */
#include "kernelbase_private.h"

#define STATUS_FAIL_FAST_EXCEPTION ((NTSTATUS)0xC0000602L)
#define EXCEPTION_NESTED_CALL 0x10

static PVOID g_top_filter; /* encoded. */
static LONG g_in_filter;

/* called by RaiseException in raise.S with the caller's captured context. */
void KbRaiseExceptionWithContext(DWORD code, DWORD flags, DWORD count, const ULONG_PTR *arguments, PCONTEXT context)
{
    EXCEPTION_RECORD record;
    memset(&record, 0, sizeof(record));
    record.ExceptionCode = (NTSTATUS)code;
    record.ExceptionFlags = flags & EXCEPTION_NONCONTINUABLE;
    record.ExceptionRecord = nullptr;
#if defined(__x86_64__)
    record.ExceptionAddress = (PVOID)context->Rip;
#else
    record.ExceptionAddress = (PVOID)RaiseException;
#endif
    if (arguments && count) {
        if (count > EXCEPTION_MAXIMUM_PARAMETERS)
            count = EXCEPTION_MAXIMUM_PARAMETERS;
        record.NumberParameters = count;
        memcpy(record.ExceptionInformation, arguments, count * sizeof(ULONG_PTR));
    }
    NTSTATUS status = NtRaiseException(&record, context, TRUE);
    RtlRaiseStatus(status);
}

void WINAPI RaiseFailFastException(PEXCEPTION_RECORD record, PCONTEXT context, DWORD flags)
{
    NTSTATUS code = record ? record->ExceptionCode : STATUS_FAIL_FAST_EXCEPTION;
    DbgPrint("kernelbase: fail fast exception %08x\n", code);
    if (record) {
        DbgPrint("kernelbase: exception address %p\n", record->ExceptionAddress);
        if (code == STATUS_ACCESS_VIOLATION && record->NumberParameters >= 2)
            DbgPrint("kernelbase: access %llu at %p\n", (ULONGLONG)record->ExceptionInformation[0],
                (PVOID)record->ExceptionInformation[1]);
    }
#ifdef __x86_64__
    if (context) {
        DbgPrint("kernelbase: rip %p rsp %p rax %p rcx %p rdx %p\n", (PVOID)context->Rip,
            (PVOID)context->Rsp, (PVOID)context->Rax, (PVOID)context->Rcx, (PVOID)context->Rdx);
        DbgPrint("kernelbase: rbx %p rbp %p rsi %p rdi %p\n", (PVOID)context->Rbx,
            (PVOID)context->Rbp, (PVOID)context->Rsi, (PVOID)context->Rdi);
        DbgPrint("kernelbase: r12 %p r13 %p r14 %p r15 %p\n", (PVOID)context->R12,
            (PVOID)context->R13, (PVOID)context->R14, (PVOID)context->R15);
    }
#endif
    for (;;)
        NtTerminateProcess(NtCurrentProcess(), code);
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter)
{
    PVOID old = __atomic_exchange_n(&g_top_filter, RtlEncodePointer((PVOID)filter), __ATOMIC_ACQ_REL);
    return old ? (LPTOP_LEVEL_EXCEPTION_FILTER)RtlDecodePointer(old) : nullptr;
}

static void describe_address(PVOID address, char *out, SIZE_T size)
{
    PLDR_DATA_TABLE_ENTRY entry;
    if (NT_SUCCESS(LdrFindEntryForAddress(address, &entry))) {
        char name[64];
        int n = WideCharToMultiByte(CP_ACP, 0, entry->BaseDllName.Buffer, entry->BaseDllName.Length / sizeof(WCHAR),
                                    name, sizeof(name) - 1, nullptr, nullptr);
        name[n > 0 ? n : 0] = 0;
        KbFormatA(out, size, "%p (%s+0x%llx)", address, name,
                  (ULONGLONG)((PUCHAR)address - (PUCHAR)entry->DllBase));
    } else {
        KbFormatA(out, size, "%p", address);
    }
}

LONG WINAPI UnhandledExceptionFilter(PEXCEPTION_POINTERS info)
{
    /* a nested exception is not reported or filtered again: the first one ends the process. */
    if (info && info->ExceptionRecord && (info->ExceptionRecord->ExceptionFlags & EXCEPTION_NESTED_CALL))
        return EXCEPTION_CONTINUE_SEARCH;
    /* a zero slot means no filter was ever set, it is not an encoded NULL. */
    LPTOP_LEVEL_EXCEPTION_FILTER filter = g_top_filter ? RtlDecodePointer(g_top_filter) : nullptr;
    if (filter && __atomic_add_fetch(&g_in_filter, 1, __ATOMIC_ACQ_REL) == 1) {
        LONG r = filter(info);
        __atomic_sub_fetch(&g_in_filter, 1, __ATOMIC_ACQ_REL);
        if (r != EXCEPTION_CONTINUE_SEARCH)
            return r;
    } else if (filter) {
        __atomic_sub_fetch(&g_in_filter, 1, __ATOMIC_ACQ_REL);
    }
    if (KbPeb()->BeingDebugged)
        return EXCEPTION_CONTINUE_SEARCH;
    if (info && info->ExceptionRecord) {
        PEXCEPTION_RECORD r = info->ExceptionRecord;
        char where[160];
        describe_address(r->ExceptionAddress, where, sizeof(where));
        DbgPrint("Unhandled exception 0x%08x at %s in process %u, thread %u\n", r->ExceptionCode, where,
                 GetCurrentProcessId(), GetCurrentThreadId());
        if (r->ExceptionCode == STATUS_ACCESS_VIOLATION && r->NumberParameters >= 2)
            DbgPrint("  %s of address %p\n",
                     r->ExceptionInformation[0] == 8 ? "execution" : r->ExceptionInformation[0] ? "write" : "read",
                     (PVOID)r->ExceptionInformation[1]);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

/* ntdll calls this filter for exceptions no handler took. */
static LONG NTAPI top_level_filter(PEXCEPTION_POINTERS info)
{
    return UnhandledExceptionFilter(info);
}

void KbInitializeDebug(void)
{
    RtlSetUnhandledExceptionFilter((PVOID)top_level_filter);
}

PVOID WINAPI AddVectoredExceptionHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler)
{
    return RtlAddVectoredExceptionHandler(first, handler);
}

ULONG WINAPI RemoveVectoredExceptionHandler(PVOID handle)
{
    return RtlRemoveVectoredExceptionHandler(handle);
}

PVOID WINAPI AddVectoredContinueHandler(ULONG first, PVECTORED_EXCEPTION_HANDLER handler)
{
    return RtlAddVectoredContinueHandler(first, handler);
}

ULONG WINAPI RemoveVectoredContinueHandler(PVOID handle)
{
    return RtlRemoveVectoredContinueHandler(handle);
}

/* windows raises DBG_PRINTEXCEPTION_C for a debugger; without one the text goes straight to the terminal. */
void WINAPI OutputDebugStringW(LPCWSTR text)
{
    if (text)
        KbDisplayText(text, wcslen(text));
}

void WINAPI OutputDebugStringA(LPCSTR text)
{
    if (!text)
        return;
    WCHAR wide[512];
    SIZE_T length = strlen(text);
    while (length) {
        int n = length > 256 ? 256 : (int)length;
        int w = MultiByteToWideChar(CP_ACP, 0, text, n, wide, RTL_NUMBER_OF(wide));
        if (w > 0)
            KbDisplayText(wide, (SIZE_T)w);
        text += n;
        length -= (SIZE_T)n;
    }
}

BOOL WINAPI IsDebuggerPresent(void)
{
    return KbPeb()->BeingDebugged;
}

BOOL WINAPI CheckRemoteDebuggerPresent(HANDLE process, PBOOL present)
{
    if (!process || !present) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *present = FALSE;
    return TRUE;
}

void WINAPI DebugBreak(void)
{
    DbgBreakPoint();
}

BOOL WINAPI DebugBreakProcess(HANDLE process)
{
    KbSetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI DebugActiveProcess(DWORD id)
{
    KbSetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

BOOL WINAPI DebugActiveProcessStop(DWORD id)
{
    KbSetError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/*
 * a program that waits for the events of the processes it debugs has to have attached to one, and nothing can attach: the
 * kernel has no debug objects, DebugActiveProcess is not supported. Without a debug object the answer is the one of a thread
 * that has none, an invalid handle.
 */
BOOL WINAPI WaitForDebugEvent(LPDEBUG_EVENT event, DWORD milliseconds)
{
    if (!event) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    KbSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

BOOL WINAPI WaitForDebugEventEx(LPDEBUG_EVENT event, DWORD milliseconds)
{
    return WaitForDebugEvent(event, milliseconds);
}

BOOL WINAPI ContinueDebugEvent(DWORD process, DWORD thread, DWORD status)
{
    KbSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

BOOL WINAPI DebugSetProcessKillOnExit(BOOL kill)
{
    KbSetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

void WINAPI FatalAppExitW(UINT action, LPCWSTR message)
{
    static const WCHAR prefix[] = L"FatalAppExit: ";
    KbDisplayText(prefix, RTL_NUMBER_OF(prefix) - 1);
    if (message)
        KbDisplayText(message, wcslen(message));
    KbDisplayText(L"\n", 1);
    ExitProcess(255);
}

void WINAPI FatalAppExitA(UINT action, LPCSTR message)
{
    PWSTR w = KbAnsiToWideCp(CP_ACP, message);
    FatalAppExitW(action, w ? w : L"");
}

void WINAPI FatalExit(int code)
{
    DbgPrint("FatalExit: %d\n", code);
    ExitProcess((UINT)code);
}

HRESULT WINAPI RegisterApplicationRestart(PCWSTR command_line, DWORD flags)
{
    if (command_line && wcslen(command_line) > 1024)
        return E_INVALIDARG;
    return S_OK;
}

HRESULT WINAPI UnregisterApplicationRestart(void)
{
    return S_OK;
}

HRESULT WINAPI RegisterApplicationRecoveryCallback(APPLICATION_RECOVERY_CALLBACK callback, PVOID parameter, DWORD ping,
                                                   DWORD flags)
{
    return callback ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI UnregisterApplicationRecoveryCallback(void)
{
    return S_OK;
}

HRESULT WINAPI GetApplicationRecoveryCallback(HANDLE process, APPLICATION_RECOVERY_CALLBACK *callback, PVOID *parameter,
                                              PDWORD ping, PDWORD flags)
{
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

HRESULT WINAPI GetApplicationRestartSettings(HANDLE process, PWSTR command_line, PDWORD size, PDWORD flags)
{
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

HRESULT WINAPI ApplicationRecoveryInProgress(PBOOL cancelled)
{
    if (!cancelled)
        return E_INVALIDARG;
    *cancelled = FALSE;
    return S_OK;
}

void WINAPI ApplicationRecoveryFinished(BOOL success)
{
}

DWORD WINAPI WerRegisterRuntimeExceptionModule(PCWSTR dll, PVOID context)
{
    return S_OK;
}

HRESULT WINAPI WerSetFlags(DWORD flags)
{
    return S_OK;
}

HRESULT WINAPI WerRegisterMemoryBlock(PVOID address, DWORD size)
{
    return S_OK;
}

HRESULT WINAPI WerRegisterFile(PCWSTR file, int type, DWORD flags)
{
    return S_OK;
}
