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

#define VCPP_LOAD_FAILED ((NTSTATUS)0xC06D007EL)
#define VCPP_PROC_FAILED ((NTSTATUS)0xC06D007FL)

/* the msvc delay load helper raises its failures with a pointer to this as the only parameter. */
typedef struct {
    DWORD Size;
    PVOID Descriptor;
    PVOID *Thunk;
    LPCSTR Dll;
    BOOL ByName;
    union {
        LPCSTR Name;
        DWORD Ordinal;
    } Procedure;
    HMODULE Module;
    FARPROC Address;
    DWORD LastError;
} DELAY_LOAD_INFO;

/* the program owns these strings, so every byte is checked before it is read. */
static void copy_program_string(LPCSTR source, char *out, SIZE_T size)
{
    SIZE_T n = 0;
    for (; n + 1 < size; n++) {
        if (IsBadReadPtr(source + n, 1) || !source[n])
            break;
        out[n] = source[n];
    }
    out[n] = 0;
}

static void report_delay_load(PEXCEPTION_RECORD r)
{
    if (r->NumberParameters < 1)
        return;
    const DELAY_LOAD_INFO *info = (const DELAY_LOAD_INFO *)r->ExceptionInformation[0];
    if (IsBadReadPtr(info, sizeof(*info)) || info->Size < sizeof(*info))
        return;
    char dll[MAX_PATH];
    copy_program_string(info->Dll, dll, sizeof(dll));
    if (r->ExceptionCode == VCPP_LOAD_FAILED) {
        DbgPrint("  delay load of %s failed, error %u\n", dll, info->LastError);
    } else if (info->ByName) {
        char name[256];
        copy_program_string(info->Procedure.Name, name, sizeof(name));
        DbgPrint("  delay load of %s from %s failed, error %u\n", name, dll, info->LastError);
    } else {
        DbgPrint("  delay load of ordinal %u from %s failed, error %u\n", info->Procedure.Ordinal, dll,
                 info->LastError);
    }
}

#if defined(__x86_64__)
NTSYSAPI PVOID NTAPI RtlVirtualUnwind(ULONG type, ULONG64 base, ULONG64 pc, PVOID function, PCONTEXT context,
                                      PVOID *data, PULONG64 frame, PVOID pointers);

/* the callers of the failing code, which tell what led to it when its own address says little, as inside abort. */
static void report_callers(const CONTEXT *start)
{
    CONTEXT context = *start;
    PTEB teb = NtCurrentTeb();
    for (int depth = 0; depth < 24; depth++) {
        if (context.Rsp < (ULONG64)teb->NtTib.StackLimit || context.Rsp >= (ULONG64)teb->NtTib.StackBase)
            break;
        ULONG64 base;
        PVOID function = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
        if (function) {
            PVOID handler_data;
            ULONG64 frame;
            RtlVirtualUnwind(0, base, context.Rip, function, &context, &handler_data, &frame, nullptr);
        } else {
            /* a leaf without data returns to the address on top of the stack. */
            context.Rip = *(ULONG64 *)context.Rsp;
            context.Rsp += sizeof(ULONG64);
        }
        if (!context.Rip)
            break;
        char where[160];
        describe_address((PVOID)context.Rip, where, sizeof(where));
        DbgPrint("  called from %s\n", where);
    }
}
#endif

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
        if (r->ExceptionCode == VCPP_LOAD_FAILED || r->ExceptionCode == VCPP_PROC_FAILED)
            report_delay_load(r);
#if defined(__x86_64__)
        if (info->ContextRecord)
            report_callers(info->ContextRecord);
#endif
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

/* windows forwards these to ntdll, whose exports programs follow to its internal routines. */
#ifdef __x86_64__
#pragma comment(linker, "/export:AddVectoredExceptionHandler=NTDLL.RtlAddVectoredExceptionHandler")
#pragma comment(linker, "/export:RemoveVectoredExceptionHandler=NTDLL.RtlRemoveVectoredExceptionHandler")
#pragma comment(linker, "/export:AddVectoredContinueHandler=NTDLL.RtlAddVectoredContinueHandler")
#pragma comment(linker, "/export:RemoveVectoredContinueHandler=NTDLL.RtlRemoveVectoredContinueHandler")
#else
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
#endif

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

DWORD WINAPI WerUnregisterRuntimeExceptionModule(PCWSTR dll, PVOID context)
{
    return S_OK;
}

HRESULT WINAPI WerGetFlags(HANDLE process, PDWORD flags)
{
    if (!flags)
        return E_INVALIDARG;
    *flags = 0;
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

HRESULT WINAPI WerRegisterCustomMetadata(PCWSTR key, PCWSTR value)
{
    return key && value ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI WerUnregisterCustomMetadata(PCWSTR key)
{
    return key ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI WerRegisterFile(PCWSTR file, int type, DWORD flags)
{
    return S_OK;
}
