/*
 * runs 32-bit programs on x64. 64-bit ntdll calls Wow64LdrpInitialize for each thread; it maps 32-bit ntdll, enters
 * wow64cpu and never returns. system calls return here for conversion by wow32.c or wow64win. callbacks nest
 * simulations below the suspended 32-bit stack. Wow64PrepareForException redirects 32-bit faults from the native
 * dispatcher to the innermost simulation and then 32-bit ntdll.
 */
#define MIME_WOW64_DLL
#include <mime/ntrtl.h>
#include <mime/wow32.h>
#include <mime/wow64.h>
#include "mime_svc_nt.h"

#define CODE32 0x23
#define DATA32 0x2B
#define TEB32_SEGMENT 0x53

NTSTATUS Mx32SystemCall(ULONG number, const ULONG *arguments);
BOOLEAN MxFillPeb32(PVOID peb);

/* rbx, rbp, rdi, rsi, r12 to r15, rsp, rip, then xmm6 to xmm15, see x64/jump.S. */
typedef struct DECLSPEC_ALIGN(16) WOW64_JUMP {
    ULONG64 Registers[10];
    M128A Xmm[10];
} WOW64_JUMP;

int Wow64SetJump(WOW64_JUMP *jump);
DECLSPEC_NORETURN void Wow64LongJump(WOW64_JUMP *jump, int value);

#define JUMP_EXCEPTION 1
#define JUMP_CALLBACK_RETURN 2

typedef struct WOW64_FRAME {
    WOW64_JUMP Jump;
    struct WOW64_FRAME *Outer;
    BOOLEAN Callback;
    NTSTATUS Status; /* of a callback's NtCallbackReturn. */
    ULONG Result, ResultLength;
} WOW64_FRAME;

typedef struct WOW64_THREAD {
    WOW64_FRAME *Frame; /* the innermost simulation. */
    MIME_EXCEPTION32 Record;
    I386_CONTEXT Context; /* of the exception the innermost simulation delivers. */
} WOW64_THREAD;

static struct {
    MIME_PEB32 *Peb32;
    PVOID Ntdll32;
    ULONG LdrInitializeThunk, UserThreadStart, ExceptionDispatcher, CallbackDispatcher;
    ULONG ReturnAddress; /* the gate calls into 32-bit code return to. */
    PUCHAR Wow64Info;
    void(NTAPI *Simulate)(void);
    WOW64WIN_SYSTEM_CALL Win32kCall;
} g;

/* ------------------------------------------------------------ helpers */

static void wow64_print(const char *format, ...)
{
    char text[256];
    va_list ap;
    va_start(ap, format);
    int n = _vsnprintf(text, sizeof(text) - 2, format, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof(text) - 2)
        n = (int)strlen(text);
    text[n++] = '\n';
    WCHAR wide[256];
    for (int i = 0; i < n; i++)
        wide[i] = (WCHAR)(UCHAR)text[i];
    UNICODE_STRING s = {(USHORT)(n * sizeof(WCHAR)), (USHORT)(n * sizeof(WCHAR)), wide};
    NtDisplayString(&s);
}

static DECLSPEC_NORETURN void fatal(const char *what, NTSTATUS status)
{
    wow64_print("wow64: %s (%08x)", what, (ULONG)status);
    NtTerminateProcess(NtCurrentProcess(), status);
    for (;;)
        ;
}

static WOW64_THREAD *current_thread(void)
{
    return NtCurrentTeb()->TlsSlots[WOW64_TLS_THREAD];
}

PVOID MxCurrentPeb32(void)
{
    return g.Peb32;
}

PVOID MxCurrentTeb32(void)
{
    return Wow64CurrentTeb32();
}

ULONG Mx32Win32kCall(ULONG number, const ULONG *arguments)
{
    return g.Win32kCall ? g.Win32kCall(number, arguments) : 0;
}

void Mx32Unsupported(ULONG number)
{
    static ULONG reported[256];
    if (number < 8192) {
        ULONG bit = 1u << (number & 31);
        if (__atomic_fetch_or(&reported[number / 32], bit, __ATOMIC_RELAXED) & bit)
            return;
    }
    wow64_print("wow64: unsupported 32-bit service %04x", number);
}

/* an export of a mapped image of either width. */
static ULONG_PTR image_export(PVOID base, const char *name)
{
    PIMAGE_DOS_HEADER dos = base;
    PIMAGE_NT_HEADERS32 nt = (PIMAGE_NT_HEADERS32)((PUCHAR)base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC
                                   ? nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT]
                                   : ((PIMAGE_NT_HEADERS64)nt)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress)
        return 0;
    PIMAGE_EXPORT_DIRECTORY exp = (PIMAGE_EXPORT_DIRECTORY)((PUCHAR)base + dir.VirtualAddress);
    PULONG names = (PULONG)((PUCHAR)base + exp->AddressOfNames);
    PUSHORT ordinals = (PUSHORT)((PUCHAR)base + exp->AddressOfNameOrdinals);
    PULONG functions = (PULONG)((PUCHAR)base + exp->AddressOfFunctions);
    for (ULONG i = 0; i < exp->NumberOfNames; i++)
        if (!strcmp((const char *)base + names[i], name))
            return (ULONG_PTR)base + functions[ordinals[i]];
    return 0;
}

static PVOID load_dll(PCWSTR name)
{
    UNICODE_STRING s;
    RtlInitUnicodeString(&s, name);
    PVOID base = nullptr;
    NTSTATUS status = LdrLoadDll(nullptr, nullptr, &s, &base);
    if (!NT_SUCCESS(status))
        fatal("cannot load a wow64 dll", status);
    return base;
}

static PVOID procedure(PVOID dll, const char *name)
{
    PVOID address = (PVOID)image_export(dll, name);
    if (!address)
        fatal("a wow64 dll lacks an entry point", STATUS_ENTRYPOINT_NOT_FOUND);
    return address;
}

/* ------------------------------------------------------------ contexts */

/* initial x87 control word and MXCSR for a 32-bit thread. */
static void initial_extended_state(I386_CONTEXT *context)
{
    memset(context->ExtendedRegisters, 0, sizeof(context->ExtendedRegisters));
    *(USHORT *)context->ExtendedRegisters = 0x27F;
    *(ULONG *)(context->ExtendedRegisters + 24) = 0x1F80;
    *(ULONG *)(context->ExtendedRegisters + 28) = 0xFFFF;
}

/* copies selected 32-bit context fields into the CPU area, which is restored in full on return. */
static void set_context32(PWOW64_CPURESERVED cpu, const I386_CONTEXT *in)
{
    I386_CONTEXT *c = &cpu->Context;
    ULONG flags = in->ContextFlags;
    if ((flags & CONTEXT_I386_INTEGER) == CONTEXT_I386_INTEGER) {
        c->Edi = in->Edi;
        c->Esi = in->Esi;
        c->Ebx = in->Ebx;
        c->Edx = in->Edx;
        c->Ecx = in->Ecx;
        c->Eax = in->Eax;
    }
    if ((flags & CONTEXT_I386_CONTROL) == CONTEXT_I386_CONTROL) {
        c->Ebp = in->Ebp;
        c->Eip = in->Eip;
        c->EFlags = in->EFlags;
        c->Esp = in->Esp;
    }
    if ((flags & CONTEXT_I386_EXTENDED_REGISTERS) == CONTEXT_I386_EXTENDED_REGISTERS)
        memcpy(c->ExtendedRegisters, in->ExtendedRegisters, sizeof(c->ExtendedRegisters));
    cpu->Flags |= WOW64_CPURESERVED_FLAG_RESET_STATE;
}

static void get_context32(PWOW64_CPURESERVED cpu, I386_CONTEXT *out)
{
    ULONG flags = out->ContextFlags;
    memcpy(out, &cpu->Context, sizeof(*out));
    out->ContextFlags = flags;
    out->SegCs = CODE32;
    out->SegDs = out->SegEs = out->SegSs = DATA32;
    out->SegFs = TEB32_SEGMENT;
}

/* converts the kernel's native fault context to the 32-bit layout. */
static void context64_to_32(const CONTEXT *in, I386_CONTEXT *out)
{
    memset(out, 0, sizeof(*out));
    out->ContextFlags = CONTEXT_I386_CONTROL | CONTEXT_I386_INTEGER | CONTEXT_I386_SEGMENTS |
                        CONTEXT_I386_EXTENDED_REGISTERS;
    out->Eax = (ULONG)in->Rax;
    out->Ebx = (ULONG)in->Rbx;
    out->Ecx = (ULONG)in->Rcx;
    out->Edx = (ULONG)in->Rdx;
    out->Esi = (ULONG)in->Rsi;
    out->Edi = (ULONG)in->Rdi;
    out->Ebp = (ULONG)in->Rbp;
    out->Esp = (ULONG)in->Rsp;
    out->Eip = (ULONG)in->Rip;
    out->EFlags = in->EFlags;
    out->SegCs = CODE32;
    out->SegDs = out->SegEs = out->SegSs = DATA32;
    out->SegFs = TEB32_SEGMENT;
    /* the x87 state is still the processor's; the vector registers are the kernel's report. */
    DECLSPEC_ALIGN(16) UCHAR state[512];
    __asm__ volatile("fxsave %0" : "=m"(state));
    memcpy(out->ExtendedRegisters, state, sizeof(state));
    memcpy(out->ExtendedRegisters + 160, in->FltSave.XmmRegisters, 8 * 16);
    *(ULONG *)(out->ExtendedRegisters + 24) = in->MxCsr;
}

static void context32_to_64(const I386_CONTEXT *in, CONTEXT *out)
{
    ULONG flags = in->ContextFlags;
    if ((flags & CONTEXT_I386_INTEGER) == CONTEXT_I386_INTEGER) {
        out->Rax = in->Eax;
        out->Rbx = in->Ebx;
        out->Rcx = in->Ecx;
        out->Rdx = in->Edx;
        out->Rsi = in->Esi;
        out->Rdi = in->Edi;
    }
    if ((flags & CONTEXT_I386_CONTROL) == CONTEXT_I386_CONTROL) {
        out->Rbp = in->Ebp;
        out->Rsp = in->Esp;
        out->Rip = in->Eip;
        out->EFlags = in->EFlags;
        out->SegCs = CODE32;
    }
}

/* ------------------------------------------------------------ simulation */

static void deliver_exception(WOW64_THREAD *t);

/* runs the thread's 32-bit code until the call made into it returns, or its callback does through NtCallbackReturn. */
static void simulate(WOW64_THREAD *t, WOW64_FRAME *f)
{
    f->Outer = t->Frame;
    t->Frame = f;
    switch (Wow64SetJump(&f->Jump)) {
    case JUMP_EXCEPTION:
        deliver_exception(t);
        break;
    case JUMP_CALLBACK_RETURN:
        t->Frame = f->Outer;
        return;
    }
    g.Simulate();
    t->Frame = f->Outer;
}

/* calls stdcall code below the suspended 32-bit stack and restores its state afterwards. returns the callee's EAX. */
static ULONG call32(WOW64_FRAME *f, ULONG function, const ULONG *arguments, ULONG count, ULONG stack)
{
    WOW64_THREAD *t = current_thread();
    PWOW64_CPURESERVED cpu = Wow64CpuArea();
    WOW64_CPURESERVED saved;
    memcpy(&saved, cpu, sizeof(saved));
    PULONG sp = (PULONG)(ULONG_PTR)(stack & ~15u);
    for (ULONG i = count; i > 0; i--)
        *--sp = arguments[i - 1];
    *--sp = g.ReturnAddress;
    cpu->Context.Esp = (ULONG)(ULONG_PTR)sp;
    cpu->Context.Eip = function;
    cpu->Context.EFlags = 0x202;
    cpu->Flags |= WOW64_CPURESERVED_FLAG_RESET_STATE;
    simulate(t, f);
    ULONG result = cpu->Context.Eax;
    memcpy(cpu, &saved, sizeof(saved));
    return result;
}

/* sends the pending exception to the 32-bit ntdll's KiUserExceptionDispatcher(record, context). */
static void deliver_exception(WOW64_THREAD *t)
{
    PWOW64_CPURESERVED cpu = Wow64CpuArea();
    ULONG sp = (t->Context.Esp - sizeof(I386_CONTEXT) - sizeof(MIME_EXCEPTION32) - 32) & ~15u;
    PULONG arguments = (PULONG)(ULONG_PTR)sp;
    MIME_EXCEPTION32 *record = (void *)(arguments + 4);
    I386_CONTEXT *context = (void *)(record + 1);
    memcpy(record, &t->Record, sizeof(*record));
    memcpy(context, &t->Context, sizeof(*context));
    arguments[0] = 0;
    arguments[1] = (ULONG)(ULONG_PTR)record;
    arguments[2] = (ULONG)(ULONG_PTR)context;
    memcpy(&cpu->Context, &t->Context, sizeof(cpu->Context));
    cpu->Context.Esp = sp;
    cpu->Context.Eip = g.ExceptionDispatcher;
    cpu->Flags |= WOW64_CPURESERVED_FLAG_RESET_STATE;
}

/*
 * called first by the native KiUserExceptionDispatcher. redirects 32-bit faults to the innermost simulation and
 * 32-bit ntdll; native faults continue through the usual dispatcher.
 */
WOW64API void NTAPI Wow64PrepareForException(PEXCEPTION_RECORD record, PCONTEXT context)
{
    WOW64_THREAD *t = current_thread();
    if (context->SegCs != CODE32 || !t || !t->Frame)
        return;
    MIME_EXCEPTION32 *out = &t->Record;
    memset(out, 0, sizeof(*out));
    out->Code = record->ExceptionCode;
    out->Flags = record->ExceptionFlags;
    out->Address = (ULONG)(ULONG_PTR)record->ExceptionAddress;
    out->Count = record->NumberParameters < EXCEPTION_MAXIMUM_PARAMETERS ? record->NumberParameters
                                                                          : EXCEPTION_MAXIMUM_PARAMETERS;
    for (ULONG i = 0; i < out->Count; i++)
        out->Information[i] = (ULONG)record->ExceptionInformation[i];
    context64_to_32(context, &t->Context);
    Wow64LongJump(&t->Frame->Jump, JUMP_EXCEPTION);
}

WOW64API NTSTATUS NTAPI Wow64KiUserCallbackDispatcher(ULONG api, ULONG arguments, ULONG length, PULONG result,
                                                      PULONG result_length)
{
    WOW64_FRAME frame;
    memset(&frame, 0, sizeof(frame));
    frame.Callback = TRUE;
    frame.Status = STATUS_UNSUCCESSFUL;
    ULONG args[3] = {api, arguments, length};
    call32(&frame, g.CallbackDispatcher, args, 3, arguments - 32);
    *result = frame.Result;
    *result_length = frame.ResultLength;
    return frame.Status;
}

/*
 * the 32-bit APC address is encoded in the upper half of the first argument. runs the three-argument APC below the
 * 32-bit stack, or below the native dispatcher if it interrupted 32-bit code.
 */
WOW64API void NTAPI Wow64ApcRoutine(PVOID packed, PVOID argument2, PVOID argument3, PCONTEXT interrupted)
{
    WOW64_FRAME frame;
    memset(&frame, 0, sizeof(frame));
    ULONG stack = Wow64CpuArea()->Context.Esp;
    ULONG_PTR here = (ULONG_PTR)&frame;
    if (here < stack)
        stack = (ULONG)here;
    ULONG args[3] = {(ULONG)(ULONG_PTR)packed, (ULONG)(ULONG_PTR)argument2, (ULONG)(ULONG_PTR)argument3};
    call32(&frame, (ULONG)((ULONG_PTR)packed >> 32), args, 3, stack - 0x100);
}

void Mx32ApcRoutine(ULONG routine, ULONG context, PVOID *native, PVOID *argument)
{
    *native = (PVOID)Wow64ApcRoutine;
    *argument = (PVOID)((ULONG64)routine << 32 | context);
}

static NTSTATUS queue_apc32(const ULONG *a)
{
    ULONG64 packed = (ULONG64)a[1] << 32 | a[2];
    /* the routine also takes the context, which user APC delivery passes as a fourth argument. */
    return NtQueueApcThread((HANDLE)(LONG_PTR)(LONG)a[0], (PPS_APC_ROUTINE)(void *)Wow64ApcRoutine, (PVOID)packed,
                            (PVOID)(ULONG_PTR)a[3], (PVOID)(ULONG_PTR)a[4]);
}

/* ------------------------------------------------------------ system calls */

/* gets another thread's 32-bit context from the kernel when it runs 32-bit code, otherwise from its CPU area. */
static NTSTATUS other_thread_cpu(HANDLE thread, ULONG_PTR *area)
{
    THREAD_BASIC_INFORMATION info;
    NTSTATUS status = NtQueryInformationThread(thread, ThreadBasicInformation, &info, sizeof(info), nullptr);
    if (!NT_SUCCESS(status))
        return status;
    PTEB teb = info.TebBaseAddress;
    return NtReadVirtualMemory(NtCurrentProcess(), &teb->TlsSlots[WOW64_TLS_CPURESERVED], area, sizeof(*area),
                               nullptr);
}

static NTSTATUS thread_context32(HANDLE thread, I386_CONTEXT *context, BOOLEAN setting)
{
    if (thread == NtCurrentThread()) {
        if (setting)
            set_context32(Wow64CpuArea(), context);
        else
            get_context32(Wow64CpuArea(), context);
        return STATUS_SUCCESS;
    }
    CONTEXT native;
    memset(&native, 0, sizeof(native));
    native.ContextFlags = CONTEXT_FULL | CONTEXT_SEGMENTS;
    NTSTATUS status = NtGetContextThread(thread, &native);
    if (!NT_SUCCESS(status))
        return status;
    if (native.SegCs == CODE32) {
        if (!setting) {
            ULONG flags = context->ContextFlags;
            context64_to_32(&native, context);
            context->ContextFlags = flags;
            return STATUS_SUCCESS;
        }
        context32_to_64(context, &native);
        return NtSetContextThread(thread, &native);
    }
    ULONG_PTR area;
    status = other_thread_cpu(thread, &area);
    if (!NT_SUCCESS(status))
        return status;
    WOW64_CPURESERVED cpu;
    status = NtReadVirtualMemory(NtCurrentProcess(), (PVOID)area, &cpu, sizeof(cpu), nullptr);
    if (!NT_SUCCESS(status))
        return status;
    if (!setting) {
        get_context32(&cpu, context);
        return STATUS_SUCCESS;
    }
    set_context32(&cpu, context);
    return NtWriteVirtualMemory(NtCurrentProcess(), (PVOID)area, &cpu, sizeof(cpu), nullptr);
}

static DECLSPEC_NORETURN void unhandled32(const MIME_EXCEPTION32 *record, const I386_CONTEXT *context)
{
    wow64_print("wow64: unhandled 32-bit exception %08x at %08x", (ULONG)record->Code, record->Address);
    if (record->Code == STATUS_ACCESS_VIOLATION && record->Count >= 2)
        wow64_print("  %s of %08x", record->Information[0] == 1 ? "write" : record->Information[0] == 8 ? "execution"
                                                                                                        : "read",
                    record->Information[1]);
    wow64_print("  eax %08x ebx %08x ecx %08x edx %08x esi %08x edi %08x ebp %08x esp %08x", context->Eax,
                context->Ebx, context->Ecx, context->Edx, context->Esi, context->Edi, context->Ebp, context->Esp);
    NtTerminateProcess(NtCurrentProcess(), record->Code);
    for (;;)
        ;
}

/* handles calls that replace 32-bit state or are implemented here; returns whether the number matched. */
static BOOLEAN special_service(ULONG number, PULONG a, LONG *result)
{
    PWOW64_CPURESERVED cpu = Wow64CpuArea();
    WOW64_THREAD *t = current_thread();
    switch (number) {
    case MIME_SVC_NtContinue:
        set_context32(cpu, (const I386_CONTEXT *)(ULONG_PTR)a[0]);
        if (a[1])
            NtTestAlert();
        *result = (LONG)cpu->Context.Eax;
        return TRUE;
    case MIME_SVC_NtCallbackReturn: {
        WOW64_FRAME *f = t ? t->Frame : nullptr;
        if (!f || !f->Callback) {
            *result = STATUS_NO_CALLBACK_ACTIVE;
            return TRUE;
        }
        f->Result = a[0];
        f->ResultLength = a[1];
        f->Status = (NTSTATUS)a[2];
        Wow64LongJump(&f->Jump, JUMP_CALLBACK_RETURN);
    }
    case MIME_SVC_NtRaiseException: {
        const MIME_EXCEPTION32 *record = (const void *)(ULONG_PTR)a[0];
        const I386_CONTEXT *context = (const void *)(ULONG_PTR)a[1];
        if (!a[2])
            unhandled32(record, context);
        memcpy(&t->Record, record, sizeof(t->Record));
        memcpy(&t->Context, context, sizeof(t->Context));
        /* a context without the extended registers, as RtlCaptureContext makes, keeps the processor's. */
        if ((context->ContextFlags & CONTEXT_I386_EXTENDED_REGISTERS) != CONTEXT_I386_EXTENDED_REGISTERS) {
            DECLSPEC_ALIGN(16) UCHAR state[512];
            __asm__ volatile("fxsave %0" : "=m"(state));
            memcpy(t->Context.ExtendedRegisters, state, sizeof(state));
        }
        deliver_exception(t);
        *result = (LONG)cpu->Context.Eax;
        return TRUE;
    }
    case MIME_SVC_NtQueueApcThread:
        *result = queue_apc32(a);
        return TRUE;
    case MIME_SVC_NtGetContextThread:
    case MIME_SVC_NtSetContextThread: {
        BOOLEAN setting = number == MIME_SVC_NtSetContextThread;
        HANDLE thread = (HANDLE)(LONG_PTR)(LONG)a[0];
        I386_CONTEXT *context = (void *)(ULONG_PTR)a[1];
        NTSTATUS status = thread_context32(thread, context, setting);
        if (!setting && thread == NtCurrentThread())
            context->Eax = 0;
        *result = NT_SUCCESS(status) && setting && thread == NtCurrentThread() ? (LONG)cpu->Context.Eax : status;
        return TRUE;
    }
    }
    return FALSE;
}

/*
 * dispatches a 32-bit system call. the upper word encodes the Windows fast path and arguments follow the return
 * address. calls replacing CPU state return the new EAX.
 */
WOW64API LONG NTAPI Wow64SystemServiceEx(ULONG number, PULONG a)
{
    number &= 0xFFFF;
    LONG result;
    BOOLEAN handled = FALSE;
    __try {
        handled = special_service(number, a, &result);
    } __except (_exception_code() == STATUS_ACCESS_VIOLATION || _exception_code() == STATUS_IN_PAGE_ERROR ||
                        _exception_code() == STATUS_GUARD_PAGE_VIOLATION
                    ? 1 /* EXCEPTION_EXECUTE_HANDLER */
                    : 0 /* EXCEPTION_CONTINUE_SEARCH */) {
        result = (LONG)_exception_code();
        handled = TRUE;
    }
    if (handled)
        return result;
    PULONG last_error = (PULONG)(Wow64CurrentTeb32() + MIME_TEB32_LAST_ERROR);
    PTEB teb = NtCurrentTeb();
    teb->LastErrorValue = *last_error;
    /*
     * argument conversion reads user pointers in this process. return STATUS_ACCESS_VIOLATION or
     * STATUS_GUARD_PAGE_VIOLATION for faults, as the kernel would, rather than terminating with a native exception
     * no 32-bit handler can catch.
     */
    NTSTATUS status;
    teb->ExceptionCode = 0;
    __try {
        status = Mx32SystemCall(number, a);
    } __except (_exception_code() == STATUS_ACCESS_VIOLATION || _exception_code() == STATUS_IN_PAGE_ERROR ||
                        _exception_code() == STATUS_GUARD_PAGE_VIOLATION
                    ? 1 /* EXCEPTION_EXECUTE_HANDLER */
                    : 0 /* EXCEPTION_CONTINUE_SEARCH */) {
        status = (NTSTATUS)_exception_code();
    }
    *last_error = teb->LastErrorValue;
    if (status == STATUS_NOT_IMPLEMENTED)
        Mx32Unsupported(number);
    /*
     * strict handle checks leave the exception code in the TEB. raise it in the caller's code as if the system call
     * had returned.
     */
    WOW64_THREAD *thread = current_thread();
    if (teb->ExceptionCode && thread) {
        PWOW64_CPURESERVED cpu = Wow64CpuArea();
        memset(&thread->Record, 0, sizeof(thread->Record));
        thread->Record.Code = (ULONG)teb->ExceptionCode;
        thread->Record.Address = cpu->Context.Eip;
        teb->ExceptionCode = 0;
        memcpy(&thread->Context, &cpu->Context, sizeof(thread->Context));
        thread->Context.Eax = (ULONG)status;
        DECLSPEC_ALIGN(16) UCHAR state[512];
        __asm__ volatile("fxsave %0" : "=m"(state));
        memcpy(thread->Context.ExtendedRegisters, state, sizeof(state));
        thread->Context.ContextFlags = CONTEXT_I386_FULL | CONTEXT_I386_EXTENDED_REGISTERS;
        deliver_exception(thread);
        return (LONG)cpu->Context.Eax;
    }
    return status;
}

/* ------------------------------------------------------------ start */

/*
 * locates 32-bit ntdll and its entry points, then installs wow64cpu's gate. the kernel stores its base in a spare
 * PEB pointer; map it here if the kernel did not.
 */
static void map_ntdll32(ULONG transition)
{
    PVOID base = NtCurrentPeb()->SparePointers[0];
    if (!base) {
        UNICODE_STRING path = RTL_CONSTANT_STRING(L"\\SystemRoot\\SysWOW64\\ntdll.dll");
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &path, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
        IO_STATUS_BLOCK io;
        HANDLE file, section;
        NTSTATUS status = NtOpenFile(&file, FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE, &oa, &io, FILE_SHARE_READ,
                                     FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
        if (!NT_SUCCESS(status))
            fatal("cannot open the 32-bit ntdll.dll", status);
        status = NtCreateSection(&section, SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_QUERY, nullptr, nullptr,
                                 PAGE_EXECUTE_READ, SEC_IMAGE, file);
        NtClose(file);
        if (!NT_SUCCESS(status))
            fatal("invalid 32-bit ntdll.dll", status);
        SIZE_T size = 0;
        status = NtMapViewOfSection(section, NtCurrentProcess(), &base, 0, 0, nullptr, &size, ViewShare, 0,
                                    PAGE_EXECUTE_WRITECOPY);
        NtClose(section);
        /* elsewhere than its base, the kernel relocated it. */
        if (!NT_SUCCESS(status))
            fatal("cannot map the 32-bit ntdll.dll", status);
    }
    g.Ntdll32 = base;
    g.LdrInitializeThunk = (ULONG)image_export(base, "LdrInitializeThunk");
    g.UserThreadStart = (ULONG)image_export(base, "RtlUserThreadStart");
    g.ExceptionDispatcher = (ULONG)image_export(base, "KiUserExceptionDispatcher");
    g.CallbackDispatcher = (ULONG)image_export(base, "KiUserCallbackDispatcher");
    PULONG gate = (PULONG)image_export(base, "Wow64Transition");
    if (!g.LdrInitializeThunk || !g.UserThreadStart || !g.ExceptionDispatcher || !g.CallbackDispatcher || !gate)
        fatal("the 32-bit ntdll.dll lacks an entry point", STATUS_ENTRYPOINT_NOT_FOUND);
    *gate = transition;
}

static void process_init(void)
{
    g.Peb32 = (MIME_PEB32 *)(ULONG_PTR) * (PULONG)(Wow64CurrentTeb32() + MIME_TEB32_PEB);
    if (!MxFillPeb32(g.Peb32))
        fatal("cannot create the 32-bit process parameters", STATUS_NO_MEMORY);
    /* the WOW64 info follows the 32-bit PEB, as on windows: the native page size first. */
    g.Wow64Info = (PUCHAR)g.Peb32 + 0x490;
    *(PULONG)g.Wow64Info = 0x1000;

    PVOID cpu = load_dll(L"wow64cpu.dll");
    NTSTATUS(NTAPI * cpu_init)(PCWSTR, SIZE_T *) = procedure(cpu, "BTCpuProcessInit");
    NTSTATUS status = cpu_init(nullptr, nullptr);
    if (!NT_SUCCESS(status))
        fatal("wow64cpu.dll must sit below 4 GiB", status);
    g.Simulate = procedure(cpu, "BTCpuSimulate");
    ULONG(NTAPI * transition)(void) = procedure(cpu, "Wow64CpuGetTransition");
    ULONG(NTAPI * return_address)(void) = procedure(cpu, "Wow64CpuGetReturnAddress");
    g.ReturnAddress = return_address();
    map_ntdll32(transition());

    PVOID win = load_dll(L"wow64win.dll");
    g.Win32kCall = procedure(win, "Wow64WinSystemCall");
    WOW64WIN_INITIALIZE win_init = procedure(win, "Wow64WinInitialize");
    status = win_init();
    if (!NT_SUCCESS(status))
        fatal("cannot initialize wow64win.dll", status);
}

/*
 * native ntdll starts each 32-bit thread here. context holds the native start state, RCX the 32-bit entry point, and
 * RDX its argument. 32-bit ntdll initializes through LdrInitializeThunk and continues into RtlUserThreadStart.
 */
WOW64API DECLSPEC_NORETURN void NTAPI Wow64LdrpInitialize(PCONTEXT context)
{
    static LONG initialized;
    if (!__atomic_exchange_n(&initialized, 1, __ATOMIC_SEQ_CST))
        process_init();
    PTEB teb = NtCurrentTeb();
    WOW64_THREAD thread;
    memset(&thread, 0, sizeof(thread));
    teb->TlsSlots[WOW64_TLS_THREAD] = &thread;
    teb->TlsSlots[WOW64_TLS_WOW64INFO] = g.Wow64Info;
    PWOW64_CPURESERVED cpu = Wow64CpuArea();

    PUCHAR teb32 = Wow64CurrentTeb32();
    ULONG top = *(PULONG)(teb32 + MIME_TEB32_STACK_BASE);
    I386_CONTEXT *start = (void *)(ULONG_PTR)((top - sizeof(I386_CONTEXT)) & ~15u);
    memset(start, 0, sizeof(*start));
    start->ContextFlags = CONTEXT_I386_FULL | CONTEXT_I386_EXTENDED_REGISTERS;
    initial_extended_state(start);
    ULONG parameter = (ULONG)context->Rdx;
    if (context->Rdx == (ULONG64)NtCurrentPeb())
        parameter = (ULONG)(ULONG_PTR)g.Peb32;
    PULONG stack = (PULONG)start;
    *--stack = parameter;
    *--stack = (ULONG)context->Rcx;
    *--stack = 0;
    start->Eip = g.UserThreadStart;
    start->Esp = (ULONG)(ULONG_PTR)stack;
    start->EFlags = 0x202;
    start->SegCs = CODE32;
    start->SegDs = start->SegEs = start->SegSs = DATA32;
    start->SegFs = TEB32_SEGMENT;

    stack -= 16;
    *--stack = (ULONG)(ULONG_PTR)g.Ntdll32;
    *--stack = (ULONG)(ULONG_PTR)start;
    *--stack = 0;
    memset(&cpu->Context, 0, sizeof(cpu->Context));
    initial_extended_state(&cpu->Context);
    cpu->Context.Esp = (ULONG)(ULONG_PTR)stack;
    cpu->Context.Eip = g.LdrInitializeThunk;
    cpu->Context.EFlags = 0x202;
    cpu->Flags |= WOW64_CPURESERVED_FLAG_RESET_STATE;

    /* the thread ends in a system call; a return of its start would have nowhere to go. */
    WOW64_FRAME frame;
    for (;;) {
        memset(&frame, 0, sizeof(frame));
        simulate(&thread, &frame);
        fatal("a 32-bit thread returned from its start", STATUS_UNSUCCESSFUL);
    }
}

BOOL WINAPI DllMain(PVOID instance, ULONG reason, PVOID reserved)
{
    return TRUE;
}
