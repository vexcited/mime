/*
 * mimex86.dll runs x64 and x86 user mode on the native arm64 kernel.
 * it maps the matching ntdll, interprets guest instructions, and delivers
 * exceptions, callbacks and apcs through that runtime. x86 system calls
 * convert pointer widths and structure layouts before entering the kernel.
 * each thread keeps separate native and interpreted stacks.
 */
#define MIME_NTDLL
#define NTSYSCALLAPI
#include <mime/ntrtl.h>
#include <mime/wow32.h>

#include "mime_svc_nt.h"
#include "mime_svc_win32k.h"
#include "native.h"
#include "x86.h"
#include "wow32_callback.h"

#include <mime/mx64.h>

PVOID MxCreatePeb32(void);
PVOID MxCreateTeb32(PVOID peb);
NTSTATUS Mx32SystemCall(ULONG number, const ULONG *arguments);
ULONG64 MxCallService(ULONG number, const ULONG64 *arguments);

#define MX_EXPORT __declspec(dllexport)

/* where translated code returns to its native caller: never mapped. */
#define MX_EXIT_ADDRESS 0x00007FFFFFFE0000ull

/* x19 to x30, sp and d8 to d15, see mx_setjmp. */
typedef struct MX_JUMP {
    ULONG64 regs[21];
} MX_JUMP;

int mx_setjmp(MX_JUMP *jump);
DECLSPEC_NORETURN void mx_longjmp(MX_JUMP *jump, int value);
extern UCHAR mx_svc_nt[];
extern UCHAR mx_svc_win32k[];

/* one emulation started by the kernel entering user mode: a thread start, a callback, an apc. */
typedef struct MX_FRAME {
    X86Cpu cpu;
    MX_JUMP restart; /* where an exception redirected to x64 code resumes emulation */
    struct MX_FRAME *outer;
    PVOID CallbackResult;
    ULONG CallbackLength;
    NTSTATUS CallbackStatus;
} MX_FRAME;

typedef struct MX_THREAD {
    MX_SUSPEND_GATE suspend;
    MX_FRAME *frame; /* innermost emulation of the thread */
    void *blocks;    /* decoded block cache, shared by the thread's emulations */
    PVOID teb32;
    uint64_t trace[X86_TRACE_SIZE]; /* the last blocks run, for crash reports */
    uint32_t trace_pos;
    uint64_t fault_trace[16];       /* the blocks before the last fault */
} MX_THREAD;

static struct {
    PVOID ntdll;
    PVOID peb32;
    BOOLEAN is32;
    ULONG64 ldr_initialize_thunk;
    ULONG64 user_thread_start;
    ULONG64 exception_dispatcher;
    ULONG64 callback_dispatcher;
} g_guest;

static volatile uint32_t g_code_epoch = 1;

static X86Env g_env;

/* ------------------------------------------------------------ helpers */

static MX_THREAD *current_thread(void)
{
    return NtCurrentTeb()->WOW32Reserved;
}

static void suspend_gate(MX_THREAD *t, ULONG critical)
{
    __atomic_store_n(&t->suspend.Critical, critical, __ATOMIC_RELEASE);
}

void MxSetSuspendCritical(X86Cpu *c, ULONG critical)
{
    suspend_gate(c->user, critical);
}

static void env_suspend(X86Cpu *c)
{
    MX_THREAD *t = c->user;
    suspend_gate(t, 0);
    NtTestAlert();
    suspend_gate(t, 1);
}

static ULONG64 call_service(X86Cpu *c, ULONG number, ULONG64 *arguments)
{
    MX_THREAD *t = c->user;
    suspend_gate(t, 0);
    ULONG64 result = MxCallService(number, arguments);
    suspend_gate(t, 1);
    return result;
}

PVOID MxCurrentPeb32(void)
{
    return g_guest.peb32;
}

PVOID MxCurrentTeb32(void)
{
    return current_thread()->teb32;
}

static void mx_print(const char *format, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, format);
    int n = _vsnprintf(buf, sizeof(buf) - 1, format, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof(buf) - 2)
        n = (int)strlen(buf);
    buf[n++] = '\n';
    WCHAR wide[512];
    for (int i = 0; i < n; i++)
        wide[i] = (WCHAR)(UCHAR)buf[i];
    UNICODE_STRING s = {(USHORT)(n * sizeof(WCHAR)), (USHORT)(n * sizeof(WCHAR)), wide};
    NtDisplayString(&s);
}

void Mx32Unsupported(ULONG number)
{
    static ULONG reported[256];
    if (number < 8192) {
        ULONG bit = 1u << (number & 31);
        if (__atomic_fetch_or(&reported[number / 32], bit, __ATOMIC_RELAXED) & bit)
            return;
    }
    mx_print("mimex86: unsupported x86 service %04x", number);
}

static DECLSPEC_NORETURN void mx_fatal(NTSTATUS status, const char *what)
{
    mx_print("mimex86: %s (%08x)", what, (ULONG)status);
    NtTerminateProcess(NtCurrentProcess(), status);
    for (;;)
        ;
}

void *x86_alloc(size_t size)
{
    PVOID base = NULL;
    SIZE_T n = size;
    if (!NT_SUCCESS(NtAllocateVirtualMemory(NtCurrentProcess(), &base, 0, &n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)))
        mx_fatal(STATUS_NO_MEMORY, "out of memory");
    return base;
}

void x86_free(void *p, size_t size)
{
    PVOID base = p;
    SIZE_T n = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), &base, &n, MEM_RELEASE);
    (void)size;
}

/* ------------------------------------------------------------ x64 contexts */

static void cpu_to_context(X86Cpu *c, AMD64_CONTEXT *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->ContextFlags = CONTEXT_AMD64_FULL | CONTEXT_AMD64_SEGMENTS;
    memcpy(&ctx->Rax, c->r, sizeof(c->r));
    ctx->Rip = c->rip;
    ctx->EFlags = x86_get_flags(c);
    ctx->SegCs = 0x33;
    ctx->SegDs = ctx->SegEs = ctx->SegSs = ctx->SegGs = 0x2B;
    ctx->SegFs = 0x53;
    x86_fxsave(c, (uint8_t *)&ctx->FltSave);
    ctx->MxCsr = c->mxcsr;
}

static void context_to_cpu(const AMD64_CONTEXT *ctx, X86Cpu *c)
{
    ULONG flags = ctx->ContextFlags;
    if ((flags & CONTEXT_AMD64_INTEGER) == CONTEXT_AMD64_INTEGER) {
        /* every general register but rsp, which belongs to the control part. */
        ULONG64 rsp = c->r[X86_RSP];
        memcpy(c->r, &ctx->Rax, sizeof(c->r));
        c->r[X86_RSP] = rsp;
    }
    if ((flags & CONTEXT_AMD64_CONTROL) == CONTEXT_AMD64_CONTROL) {
        c->r[X86_RSP] = ctx->Rsp;
        c->rip = ctx->Rip;
        x86_set_flags(c, ctx->EFlags);
    }
    if ((flags & CONTEXT_AMD64_FLOATING_POINT) == CONTEXT_AMD64_FLOATING_POINT) {
        x86_fxrstor(c, (const uint8_t *)&ctx->FltSave);
        c->mxcsr = ctx->MxCsr;
    }
}

static void cpu_to_context32(X86Cpu *c, I386_CONTEXT *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->ContextFlags = CONTEXT_I386_CONTROL | CONTEXT_I386_INTEGER | CONTEXT_I386_SEGMENTS |
                        CONTEXT_I386_EXTENDED_REGISTERS;
    ctx->Eax = c->r[X86_RAX];
    ctx->Ebx = c->r[X86_RBX];
    ctx->Ecx = c->r[X86_RCX];
    ctx->Edx = c->r[X86_RDX];
    ctx->Esi = c->r[X86_RSI];
    ctx->Edi = c->r[X86_RDI];
    ctx->Ebp = c->r[X86_RBP];
    ctx->Esp = c->r[X86_RSP];
    ctx->Eip = c->rip;
    ctx->EFlags = x86_get_flags(c);
    ctx->SegCs = 0x23;
    ctx->SegDs = ctx->SegEs = ctx->SegSs = 0x2B;
    ctx->SegFs = 0x53;
    x86_fxsave(c, ctx->ExtendedRegisters);
}

static void context32_to_cpu(const I386_CONTEXT *ctx, X86Cpu *c)
{
    if ((ctx->ContextFlags & CONTEXT_I386_INTEGER) == CONTEXT_I386_INTEGER) {
        c->r[X86_RAX] = ctx->Eax;
        c->r[X86_RBX] = ctx->Ebx;
        c->r[X86_RCX] = ctx->Ecx;
        c->r[X86_RDX] = ctx->Edx;
        c->r[X86_RSI] = ctx->Esi;
        c->r[X86_RDI] = ctx->Edi;
    }
    if ((ctx->ContextFlags & CONTEXT_I386_CONTROL) == CONTEXT_I386_CONTROL) {
        c->r[X86_RSP] = ctx->Esp;
        c->r[X86_RBP] = ctx->Ebp;
        c->rip = ctx->Eip;
        x86_set_flags(c, ctx->EFlags);
    }
    if ((ctx->ContextFlags & CONTEXT_I386_EXTENDED_REGISTERS) == CONTEXT_I386_EXTENDED_REGISTERS)
        x86_fxrstor(c, ctx->ExtendedRegisters);
}

/* ------------------------------------------------------------ emulation */

static void init_frame(MX_FRAME *f, MX_THREAD *t)
{
    x86_init_cpu(&f->cpu, &g_env, !g_guest.is32);
    f->cpu.fs_base = (ULONG_PTR)t->teb32;
    f->cpu.gs_base = (ULONG64)NtCurrentTeb();
    f->cpu.blocks = t->blocks;
    f->cpu.code_epoch = &g_code_epoch;
    f->cpu.blocks_epoch = g_code_epoch;
    f->cpu.exit_rip = g_guest.is32 ? 0xFFFF0000u : MX_EXIT_ADDRESS;
    f->cpu.user = t;
    f->cpu.suspend_pending = &t->suspend.Pending;
    f->cpu.trace = t->trace;
    f->cpu.trace_pos = &t->trace_pos;
    f->outer = t->frame;
}

/* runs the frame until its code returns to MX_EXIT_ADDRESS. */
static void emulate(MX_THREAD *t, MX_FRAME *f)
{
    ULONG critical = t->suspend.Critical;
    t->frame = f;
    __atomic_store_n(&t->suspend.Critical, 1, __ATOMIC_RELEASE);
    mx_setjmp(&f->restart);
    x86_run(&f->cpu);
    t->frame = f->outer;
    suspend_gate(t, critical);
}

/* a stack pointer for code nested in an emulation that is in a system call. */
static ULONG64 nested_stack(MX_FRAME *outer)
{
    ULONG64 sp = outer ? outer->cpu.r[X86_RSP] : (ULONG64)NtCurrentTeb()->NtTib.StackBase;
    return (sp - 0x100) & ~15ull;
}

static void push64(X86Cpu *c, ULONG64 v)
{
    c->r[X86_RSP] -= 8;
    *(ULONG64 *)c->r[X86_RSP] = v;
}

/* sends an exception to x64 ntdll's KiUserExceptionDispatcher in the frame's emulation. */
static DECLSPEC_NORETURN void raise_in_x64(MX_FRAME *f, const EXCEPTION_RECORD *record, const AMD64_CONTEXT *context)
{
    X86Cpu *c = &f->cpu;
    PNT_TIB tib = &NtCurrentTeb()->NtTib;
    if (context->Rsp <= (ULONG64)tib->StackLimit + 0x1000 || context->Rsp > (ULONG64)tib->StackBase) {
        mx_print("mimex86: exception %08x at %p with an invalid stack pointer %p", (ULONG)record->ExceptionCode,
                 record->ExceptionAddress, (PVOID)context->Rsp);
        if (record->ExceptionCode == STATUS_ACCESS_VIOLATION)
            mx_print("  address %p", (PVOID)record->ExceptionInformation[1]);
        { /* DEBUGJIT */
            MX_THREAD *dt = c->user;
            MEMORY_BASIC_INFORMATION mbi = {0};
            NtQueryVirtualMemory(NtCurrentProcess(), record->ExceptionAddress, MemoryBasicInformation, &mbi, sizeof(mbi), NULL);
            mx_print("DEBUGJIT base %p limit %p stackbase %p rip %p", mbi.AllocationBase, tib->StackLimit, tib->StackBase,
                     (PVOID)context->Rip);
            mx_print("DEBUGJIT rax %p rbx %p rcx %p rdx %p rsi %p rdi %p rbp %p", (PVOID)context->Rax, (PVOID)context->Rbx,
                     (PVOID)context->Rcx, (PVOID)context->Rdx, (PVOID)context->Rsi, (PVOID)context->Rdi, (PVOID)context->Rbp);
        }
        mx_fatal(record->ExceptionCode, "cannot deliver the exception");
    }
    /*
     * the layout KiUserExceptionDispatcher's unwind codes describe: from its
     * stack pointer, 0x30 bytes, the context, the record, then a machine
     * frame with the faulting rip and rsp, so unwinding goes on there.
     */
    { /* DEBUGJIT */
        static LONG debug_count;
        if (__atomic_add_fetch(&debug_count, 1, __ATOMIC_RELAXED) < 30) {
            MX_THREAD *dt = c->user;
            mx_print("DEBUGJIT raise %08x at %p p0 %p p1 %p rsp %p tid %p", (ULONG)record->ExceptionCode,
                     record->ExceptionAddress, (PVOID)record->ExceptionInformation[0],
                     (PVOID)record->ExceptionInformation[1], (PVOID)context->Rsp, NtCurrentTeb()->ClientId.UniqueThread);
            for (ULONG i = 8; i < 16; i++)
                mx_print("DEBUGJIT   %p", (PVOID)dt->trace[(dt->trace_pos - 16 + i) % X86_TRACE_SIZE]);
        }
    }
    ULONG64 machine = ((context->Rsp - 0x80) & ~15ull) - 0x30;
    ULONG64 rec = machine - 0xA0;
    ULONG64 ctx = rec - 0x4D0;
    MIME_STATIC_ASSERT(sizeof(EXCEPTION_RECORD) <= 0xA0 && sizeof(AMD64_CONTEXT) == 0x4D0, "dispatcher layout");
    ULONG64 *frame = (ULONG64 *)machine;
    frame[0] = context->Rip;
    frame[1] = context->SegCs;
    frame[2] = context->EFlags;
    frame[3] = context->Rsp;
    frame[4] = context->SegSs;
    memcpy((void *)rec, record, sizeof(*record));
    memcpy((void *)ctx, context, sizeof(*context));
    c->r[X86_RSP] = ctx - 0x30;
    c->r[X86_RCX] = rec;
    c->r[X86_RDX] = ctx;
    c->rip = g_guest.exception_dispatcher;
    mx_longjmp(&f->restart, 1);
}

static DECLSPEC_NORETURN void raise_in_x86(MX_FRAME *f, const MIME_EXCEPTION32 *record,
                                          const I386_CONTEXT *context)
{
    X86Cpu *c = &f->cpu;
    PNT_TIB tib = &NtCurrentTeb()->NtTib;
    if (context->Esp <= (ULONG_PTR)tib->StackLimit + 0x1000 || context->Esp > (ULONG_PTR)tib->StackBase)
        mx_fatal(record->Code, "cannot deliver x86 exception on an invalid stack");
    ULONG sp = (context->Esp - sizeof(*context) - sizeof(*record) - 32) & ~15u;
    ULONG *arguments = (PULONG)(ULONG_PTR)sp;
    MIME_EXCEPTION32 *out = (void *)(arguments + 4);
    I386_CONTEXT *ctx = (void *)(out + 1);
    memcpy(out, record, sizeof(*out));
    memcpy(ctx, context, sizeof(*ctx));
    arguments[0] = (ULONG)c->exit_rip;
    arguments[1] = (ULONG)(ULONG_PTR)out;
    arguments[2] = (ULONG)(ULONG_PTR)ctx;
    c->r[X86_RSP] = sp;
    c->rip = g_guest.exception_dispatcher;
    mx_longjmp(&f->restart, 1);
}

static DECLSPEC_NORETURN void raise_from_cpu(MX_FRAME *f, EXCEPTION_RECORD *record)
{
    MX_THREAD *t = f->cpu.user;
    if (!f->cpu.mode64) {
        I386_CONTEXT ctx;
        cpu_to_context32(&f->cpu, &ctx);
        MIME_EXCEPTION32 out = {.Code = record->ExceptionCode, .Flags = record->ExceptionFlags,
                                .Address = ctx.Eip, .Count = record->NumberParameters};
        for (ULONG i = 0; i < out.Count && i < EXCEPTION_MAXIMUM_PARAMETERS; i++)
            out.Information[i] = (ULONG)record->ExceptionInformation[i];
        raise_in_x86(f, &out, &ctx);
    }
    for (ULONG i = 0; i < 16; i++)
        t->fault_trace[i] = t->trace[(t->trace_pos - 16 + i) % X86_TRACE_SIZE];
    AMD64_CONTEXT ctx;
    cpu_to_context(&f->cpu, &ctx);
    record->ExceptionAddress = (PVOID)f->cpu.rip;
    raise_in_x64(f, record, &ctx);
}

/* an exception raised by an export running natively; the x64 context is the export's entry. */
DECLSPEC_NORETURN void NTAPI MxRaiseStatus(NTSTATUS status)
{
    EXCEPTION_RECORD record;
    memset(&record, 0, sizeof(record));
    record.ExceptionCode = status;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    raise_from_cpu(current_thread()->frame, &record);
}

/* a fault detected by the core, cpu->rip is the faulting instruction. */
static void env_fault(X86Cpu *c, uint32_t code, uint64_t info0, uint64_t info1)
{
    MX_THREAD *t = c->user;
    EXCEPTION_RECORD record;
    memset(&record, 0, sizeof(record));
    record.ExceptionCode = (NTSTATUS)code;
    if (code == (uint32_t)STATUS_ACCESS_VIOLATION) {
        record.NumberParameters = 2;
        record.ExceptionInformation[0] = info0;
        record.ExceptionInformation[1] = info1;
    } else if (code == (uint32_t)STATUS_STACK_BUFFER_OVERRUN) {
        record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
        record.NumberParameters = 1;
        record.ExceptionInformation[0] = info0;
    }
    raise_from_cpu(t->frame, &record);
}

static uint64_t env_timestamp(X86Cpu *c)
{
    ULONG64 count, frequency;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(count));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    /* the reported processor runs at 3.2 GHz. */
    return frequency ? count * (3200000000ull / frequency) : count;
}

/*
 * the memory regions decoded blocks came from. freeing, unmapping or
 * reprotecting memory only drops the blocks when it touches one of them,
 * so a program releasing heap memory keeps its translations.
 */
#define CODE_REGIONS 256
static struct {
    ULONG64 Start, End;
} g_code[CODE_REGIONS];
static volatile LONG g_code_count;
static volatile LONG g_code_lock;
static volatile BOOLEAN g_code_overflow; /* too many regions: every change drops the blocks */

static void code_lock(void)
{
    while (__atomic_exchange_n(&g_code_lock, 1, __ATOMIC_ACQUIRE)) {
        /* let a lower priority owner finish on a single virtual cpu. */
        LARGE_INTEGER delay = {.QuadPart = -10000};
        NtDelayExecution(FALSE, &delay);
    }
}

static void code_unlock(void)
{
    __atomic_store_n(&g_code_lock, 0, __ATOMIC_RELEASE);
}

static void env_code_changed(X86Cpu *c, uint64_t address, uint64_t size)
{
    code_lock();
    g_code_count = 0;
    g_code_overflow = FALSE;
    __atomic_add_fetch(&g_code_epoch, 1, __ATOMIC_SEQ_CST);
    code_unlock();
}

static void env_code_seen(X86Cpu *c, uint64_t address)
{
    LONG n = __atomic_load_n(&g_code_count, __ATOMIC_ACQUIRE);
    for (LONG i = 0; i < n; i++)
        if (address >= g_code[i].Start && address < g_code[i].End)
            return;
    MEMORY_BASIC_INFORMATION info;
    if (!NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(), (PVOID)address, MemoryBasicInformation, &info,
                                         sizeof(info), NULL))) {
        g_code_overflow = TRUE;
        return;
    }
    code_lock();
    if (g_code_count < CODE_REGIONS) {
        g_code[g_code_count].Start = (ULONG64)info.BaseAddress;
        g_code[g_code_count].End = (ULONG64)info.BaseAddress + info.RegionSize;
        __atomic_store_n(&g_code_count, g_code_count + 1, __ATOMIC_RELEASE);
    } else {
        g_code_overflow = TRUE;
    }
    code_unlock();
}

/* drops the blocks when [start, end) holds decoded code. */
static void code_range_changed(X86Cpu *c, ULONG64 start, ULONG64 end)
{
    BOOLEAN hit = g_code_overflow;
    LONG n = __atomic_load_n(&g_code_count, __ATOMIC_ACQUIRE);
    for (LONG i = 0; i < n && !hit; i++)
        hit = start < g_code[i].End && g_code[i].Start < end;
    if (hit)
        env_code_changed(c, start, end - start);
}

/* the whole allocation a free or an unmap at base releases. */
static void allocation_range(ULONG64 base, ULONG64 *start, ULONG64 *end)
{
    MEMORY_BASIC_INFORMATION info;
    *start = *end = base;
    if (!NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(), (PVOID)base, MemoryBasicInformation, &info, sizeof(info),
                                         NULL)))
        return;
    ULONG64 allocation = (ULONG64)info.AllocationBase;
    *start = allocation ? allocation : (ULONG64)info.BaseAddress;
    *end = (ULONG64)info.BaseAddress + info.RegionSize;
    while (NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(), (PVOID)*end, MemoryBasicInformation, &info,
                                           sizeof(info), NULL)) &&
           (ULONG64)info.AllocationBase == allocation && allocation && info.RegionSize)
        *end = (ULONG64)info.BaseAddress + info.RegionSize;
}

/* ------------------------------------------------------------ system calls */

typedef ULONG64 (*SVC16)(ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64,
                         ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64);

ULONG64 MxCallService(ULONG number, const ULONG64 *a)
{
    ULONG table = number >> 12, index = number & 0xFFF;
    UCHAR *stub;
    if (table == 0 && index < MIME_NT_SERVICES_COUNT)
        stub = mx_svc_nt + 8 * index;
    else if (table == 1 && index < MIME_WIN32K_SERVICES_COUNT)
        stub = mx_svc_win32k + 8 * index;
    else
        return (ULONG64)(LONG64)STATUS_INVALID_SYSTEM_SERVICE;
    return ((SVC16)(void *)stub)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12],
                                 a[13], a[14], a[15]);
}

static NTSTATUS other_cpu(HANDLE handle, BOOLEAN setting, X86Cpu **out)
{
    ARM64_NT_CONTEXT native = {0};
    native.ContextFlags = CONTEXT_ARM64_CONTROL;
    NTSTATUS status;
    if (setting) {
        native.ContextFlags = 0;
        status = NtSetContextThread(handle, &native);
    } else {
        status = NtGetContextThread(handle, &native);
    }
    if (!NT_SUCCESS(status))
        return status;
    THREAD_BASIC_INFORMATION info;
    status = NtQueryInformationThread(handle, ThreadBasicInformation, &info, sizeof(info), NULL);
    if (!NT_SUCCESS(status))
        return status;
    if (info.ClientId.UniqueProcess != NtCurrentTeb()->ClientId.UniqueProcess || !info.TebBaseAddress)
        return STATUS_NOT_SUPPORTED;
    MX_THREAD *target = info.TebBaseAddress->WOW32Reserved;
    if (!target || !target->frame)
        return STATUS_NOT_SUPPORTED;
    if (target->suspend.Critical)
        return STATUS_UNSUCCESSFUL;
    *out = &target->frame->cpu;
    return STATUS_SUCCESS;
}

static NTSTATUS get_context(X86Cpu *c, HANDLE thread, AMD64_CONTEXT *out)
{
    BOOLEAN current = thread == NtCurrentThread();
    if (!current) {
        NTSTATUS status = other_cpu(thread, FALSE, &c);
        if (!NT_SUCCESS(status))
            return status;
    }
    AMD64_CONTEXT ctx;
    cpu_to_context(c, &ctx);
    /* the caller resumes after the system call. */
    if (current) {
        ctx.Rip = c->r[X86_RCX];
        ctx.Rax = 0;
    }
    ULONG flags = out->ContextFlags;
    memcpy(out, &ctx, sizeof(ctx));
    out->ContextFlags = flags;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------ native calls */

static const MX_NATIVE_TABLE *g_native[256];

static PVOID image_export(PVOID base, const char *name);

/* maps a native system dll; its own preferred base keeps it apart from the x64 images. */
static NTSTATUS native_load(ULONG table, PCWSTR file_name, const char *export_name)
{
    if (table >= RTL_NUMBER_OF(g_native) || !file_name || !export_name)
        return STATUS_INVALID_PARAMETER;
    if (g_native[table])
        return STATUS_SUCCESS;
    WCHAR buffer[MAX_PATH];
    static const WCHAR dir[] = L"\\SystemRoot\\System32\\";
    SIZE_T n = wcslen(file_name);
    if (n + RTL_NUMBER_OF(dir) > RTL_NUMBER_OF(buffer))
        return STATUS_NAME_TOO_LONG;
    memcpy(buffer, dir, sizeof(dir));
    memcpy(buffer + RTL_NUMBER_OF(dir) - 1, file_name, (n + 1) * sizeof(WCHAR));
    UNICODE_STRING path;
    RtlInitUnicodeString(&path, buffer);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    IO_STATUS_BLOCK iosb;
    HANDLE file, section;
    NTSTATUS status = NtOpenFile(&file, FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(status))
        return status;
    status = NtCreateSection(&section, SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_QUERY, NULL, NULL,
                             PAGE_EXECUTE_READ, SEC_IMAGE, file);
    NtClose(file);
    if (!NT_SUCCESS(status))
        return status;
    PVOID base = NULL;
    SIZE_T size = 0;
    status = NtMapViewOfSection(section, NtCurrentProcess(), &base, 0, 0, NULL, &size, ViewShare, 0,
                                PAGE_EXECUTE_WRITECOPY);
    NtClose(section);
    if (!NT_SUCCESS(status))
        return status;
    const MX_NATIVE_TABLE *t = image_export(base, export_name);
    if (!t)
        return STATUS_ENTRYPOINT_NOT_FOUND;
    g_native[table] = t;
    return STATUS_SUCCESS;
}

static void native_call(X86Cpu *c, ULONG number, const ULONG64 *a)
{
    if (number >= MX_NATIVE_LOAD) {
        c->r[X86_RAX] = (ULONG64)(LONG64)native_load((ULONG)a[0], (PCWSTR)a[1], (const char *)a[2]);
        return;
    }
    const MX_NATIVE_TABLE *t = g_native[(number >> 16) & 0xFF];
    ULONG index = number & 0xFFFF;
    if (!t || index >= t->Count) {
        c->r[X86_RAX] = (ULONG64)(LONG64)STATUS_INVALID_SYSTEM_SERVICE;
        return;
    }
    MX_NATIVE_CALL call = {
        .gpr = {a[0], a[1], a[2], a[3]},
        .xmm = {c->xmm[0].q[0], c->xmm[1].q[0], c->xmm[2].q[0], c->xmm[3].q[0]},
        .stack = (const ULONG64 *)(c->r[X86_RSP] + 0x28),
    };
    suspend_gate(c->user, 0);
    t->Functions[index](&call);
    suspend_gate(c->user, 1);
    c->r[X86_RAX] = call.rax;
    c->xmm[0].q[0] = call.xmm0;
    c->xmm[0].q[1] = 0;
}

static void env_syscall(X86Cpu *c)
{
    MX_THREAD *t = c->user;
    ULONG number = (ULONG)c->r[X86_RAX];
    if (!c->mode64) {
        const ULONG *a = (const ULONG *)(ULONG_PTR)(ULONG)c->r[X86_RDX] + 1;
        if (number == MIME_SVC_NtGetContextThread || number == MIME_SVC_NtSetContextThread) {
            HANDLE handle = (HANDLE)(LONG_PTR)(LONG)a[0];
            I386_CONTEXT *context = (void *)(ULONG_PTR)a[1];
            X86Cpu *target = c;
            BOOLEAN setting = number == MIME_SVC_NtSetContextThread;
            NTSTATUS status = handle == NtCurrentThread() ? STATUS_SUCCESS : other_cpu(handle, setting, &target);
            if (NT_SUCCESS(status)) {
                if (setting) {
                    context32_to_cpu(context, target);
                } else {
                    ULONG flags = context->ContextFlags;
                    cpu_to_context32(target, context);
                    context->ContextFlags = flags;
                    if (handle == NtCurrentThread())
                        context->Eax = 0;
                }
            }
            if (!(NT_SUCCESS(status) && setting && target == c))
                c->r[X86_RAX] = (ULONG)status;
            return;
        }
        if (number == MIME_SVC_NtContinue) {
            context32_to_cpu((const I386_CONTEXT *)(ULONG_PTR)a[0], c);
            if (a[1])
                NtTestAlert();
            return;
        }
        if (number == MIME_SVC_NtRaiseException) {
            MIME_EXCEPTION32 record;
            I386_CONTEXT ctx;
            memcpy(&record, (PVOID)(ULONG_PTR)a[0], sizeof(record));
            memcpy(&ctx, (PVOID)(ULONG_PTR)a[1], sizeof(ctx));
            if (a[2])
                raise_in_x86(t->frame, &record, &ctx);
            mx_print("mimex86: unhandled x86 exception %08x at %08x", (ULONG)record.Code, record.Address);
            /* words of the x86 stack that look like return addresses into images, for the report. */
            for (ULONG p = ctx.Esp & ~3u, n = 0; n < 96; p += 4, n++) {
                MEMORY_BASIC_INFORMATION info;
                if (NtQueryVirtualMemory(NtCurrentProcess(), (PVOID)(ULONG_PTR)p, MemoryBasicInformation, &info,
                                         sizeof(info), NULL) || info.State != MEM_COMMIT)
                    break;
                ULONG v = *(const ULONG *)(ULONG_PTR)p;
                if (v > 0x10000 && !NtQueryVirtualMemory(NtCurrentProcess(), (PVOID)(ULONG_PTR)v,
                                                         MemoryBasicInformation, &info, sizeof(info), NULL) &&
                    info.Type == MEM_IMAGE && (info.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
                    mx_print("  [esp+%x] %08x", p - ctx.Esp, v);
            }
            mx_fatal(record.Code, "unhandled x86 exception");
        }
        if (number == MIME_SVC_NtCallbackReturn) {
            t->frame->CallbackResult = (PVOID)(ULONG_PTR)a[0];
            t->frame->CallbackLength = a[1];
            t->frame->CallbackStatus = (NTSTATUS)a[2];
            c->rip = c->exit_rip;
            return;
        }
        PULONG last_error = (PULONG)((PUCHAR)t->teb32 + 0x34);
        NtCurrentTeb()->LastErrorValue = *last_error;
        suspend_gate(t, 0);
        NTSTATUS status = Mx32SystemCall(number, a);
        suspend_gate(t, 1);
        *last_error = NtCurrentTeb()->LastErrorValue;
        c->r[X86_RAX] = (ULONG)status;
        if (status == STATUS_NOT_IMPLEMENTED)
            Mx32Unsupported(number);
        if (number == MIME_SVC_NtFreeVirtualMemory || number == MIME_SVC_NtUnmapViewOfSection ||
            number == MIME_SVC_NtProtectVirtualMemory || number == MIME_SVC_NtFlushInstructionCache)
            env_code_changed(c, 0, 0);
        return;
    }
    ULONG64 a[16];
    a[0] = c->r[X86_R10];
    a[1] = c->r[X86_RDX];
    a[2] = c->r[X86_R8];
    a[3] = c->r[X86_R9];
    if ((number & 0xFF000000u) == MX_NATIVE_SYSCALL) {
        native_call(c, number, a);
        return;
    }
    for (int i = 4; i < 16; i++)
        a[i] = *(ULONG64 *)(c->r[X86_RSP] + 0x28 + 8 * (i - 4));

    switch (number) {
    case MIME_SVC_NtContinue: {
        AMD64_CONTEXT ctx;
        memcpy(&ctx, (void *)a[0], sizeof(ctx));
        context_to_cpu(&ctx, c);
        if ((BOOLEAN)a[1])
            NtTestAlert();
        return;
    }
    case MIME_SVC_NtRaiseException: {
        EXCEPTION_RECORD record;
        AMD64_CONTEXT ctx;
        memcpy(&record, (void *)a[0], sizeof(record));
        memcpy(&ctx, (void *)a[1], sizeof(ctx));
        if ((BOOLEAN)a[2])
            raise_in_x64(t->frame, &record, &ctx);
        mx_print("mimex86: unhandled exception %08x at %p, rsp %p", (ULONG)record.ExceptionCode,
                 record.ExceptionAddress, (PVOID)ctx.Rsp);
        if (record.ExceptionCode == STATUS_ACCESS_VIOLATION && record.NumberParameters >= 2)
            mx_print("  %s of address %p", record.ExceptionInformation[0] == 1 ? "write" :
                     record.ExceptionInformation[0] == 8 ? "execute" : "read", (PVOID)record.ExceptionInformation[1]);
        mx_print("  rax %p rbx %p rcx %p rdx %p", (PVOID)ctx.Rax, (PVOID)ctx.Rbx, (PVOID)ctx.Rcx, (PVOID)ctx.Rdx);
        mx_print("  rsi %p rdi %p rbp %p r8 %p", (PVOID)ctx.Rsi, (PVOID)ctx.Rdi, (PVOID)ctx.Rbp, (PVOID)ctx.R8);
        mx_print("  blocks before the last processor fault:");
        for (ULONG i = 0; i < 16; i++)
            mx_print("    %p", (PVOID)t->fault_trace[i]);
        PNT_TIB tib = &NtCurrentTeb()->NtTib;
        for (ULONG64 p = ctx.Rsp & ~7ull, n = 0; p < (ULONG64)tib->StackBase && n < 24; p += 8, n++) {
            ULONG64 v = *(ULONG64 *)p;
            /* return addresses into images. */
            if (v >= 0x10000 && v < 0x800000000000ull && v > 0x400000 && (v < (ULONG64)tib->StackLimit || v > (ULONG64)tib->StackBase))
                mx_print("  [rsp+%x] %p", (ULONG)(p - ctx.Rsp), (PVOID)v);
        }
        NtTerminateProcess(NtCurrentProcess(), record.ExceptionCode);
        return;
    }
    case MIME_SVC_NtCallbackReturn:
        /* the callback's emulation ends here: the kernel resumes the system call that started it. */
        t->frame = t->frame->outer;
        c->r[X86_RAX] = call_service(c, number, a);
        return;
    case MIME_SVC_NtQueryPerformanceCounter: {
        /* the counter user mode reads directly, so the call never enters the kernel. */
        *(ULONG64 *)a[0] = MimeReadQpc();
        if (a[1])
            *(ULONG64 *)a[1] = USER_SHARED_DATA->QpcFrequency;
        c->r[X86_RAX] = 0;
        return;
    }
    case MIME_SVC_NtGetCurrentProcessorNumber: {
        ULONG64 v;
        __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(v));
        c->r[X86_RAX] = v;
        return;
    }
    case MIME_SVC_NtGetContextThread:
        c->r[X86_RAX] = (ULONG64)(LONG64)get_context(c, (HANDLE)a[0], (AMD64_CONTEXT *)a[1]);
        return;
    case MIME_SVC_NtSetContextThread:
        if ((HANDLE)a[0] == NtCurrentThread()) {
            AMD64_CONTEXT ctx;
            memcpy(&ctx, (void *)a[1], sizeof(ctx));
            context_to_cpu(&ctx, c);
            return;
        }
        {
            X86Cpu *target;
            NTSTATUS status = other_cpu((HANDLE)a[0], TRUE, &target);
            if (NT_SUCCESS(status))
                context_to_cpu((AMD64_CONTEXT *)a[1], target);
            c->r[X86_RAX] = (ULONG64)(LONG64)status;
        }
        return;
    default:
        break;
    }

    if ((number == MIME_SVC_NtUnmapViewOfSection || number == MIME_SVC_NtUnmapViewOfSectionEx) &&
        (HANDLE)a[0] == NtCurrentProcess())
        MxImageUnmapping((PVOID)a[1]);
    /* the range a free, unmap or protection change touches, taken before the call changes it. */
    ULONG64 range_start = 0, range_end = 0;
    if ((HANDLE)a[0] == NtCurrentProcess()) {
        if (number == MIME_SVC_NtFreeVirtualMemory && a[1] && a[2]) {
            ULONG64 base = *(ULONG64 *)a[1], size = *(ULONG64 *)a[2];
            if (size)
                range_start = base & ~0xFFFull, range_end = (base + size + 0xFFF) & ~0xFFFull;
            else
                allocation_range(base, &range_start, &range_end);
        } else if (number == MIME_SVC_NtUnmapViewOfSection || number == MIME_SVC_NtUnmapViewOfSectionEx) {
            allocation_range(a[1], &range_start, &range_end);
        } else if ((number == MIME_SVC_NtProtectVirtualMemory) && a[1] && a[2]) {
            ULONG64 base = *(ULONG64 *)a[1], size = *(ULONG64 *)a[2];
            range_start = base & ~0xFFFull, range_end = (base + size + 0xFFF) & ~0xFFFull;
        } else if (number == MIME_SVC_NtFlushInstructionCache) {
            range_start = a[1], range_end = a[1] + (a[2] ? a[2] : 1);
        }
    }
    MX_FRAME *frame = t->frame;
    ULONG64 result = call_service(c, number, a);
    /* a callback run during the call may have entered nested emulations. */
    t->frame = frame;
    c->r[X86_RAX] = result;

    if (number == MIME_SVC_NtMapViewOfSection && NT_SUCCESS((NTSTATUS)result) && (HANDLE)a[1] == NtCurrentProcess())
        MxImageMapped(*(PVOID *)a[2]);
    BOOLEAN other = (HANDLE)a[0] != NtCurrentProcess();
    switch (number) {
    case MIME_SVC_NtFlushInstructionCache:
    case MIME_SVC_NtFreeVirtualMemory:
    case MIME_SVC_NtUnmapViewOfSection:
    case MIME_SVC_NtUnmapViewOfSectionEx:
        if (other)
            break;
        if (range_end > range_start)
            code_range_changed(c, range_start, range_end);
        else
            env_code_changed(c, 0, 0);
        break;
    case MIME_SVC_NtProtectVirtualMemory:
        if (NT_SUCCESS((NTSTATUS)result) && !other) {
            ULONG old = a[4] ? *(PULONG)a[4] : 0;
            if (((ULONG)a[3] | old) & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
                code_range_changed(c, range_start, range_end);
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------ process start */

static PVOID image_export(PVOID base, const char *name)
{
    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)((PUCHAR)base + ((PIMAGE_DOS_HEADER)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC ?
        ((PIMAGE_NT_HEADERS32)nt)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] :
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress)
        return NULL;
    PIMAGE_EXPORT_DIRECTORY exp = (PIMAGE_EXPORT_DIRECTORY)((PUCHAR)base + dir.VirtualAddress);
    PULONG names = (PULONG)((PUCHAR)base + exp->AddressOfNames);
    PUSHORT ordinals = (PUSHORT)((PUCHAR)base + exp->AddressOfNameOrdinals);
    PULONG functions = (PULONG)((PUCHAR)base + exp->AddressOfFunctions);
    for (ULONG i = 0; i < exp->NumberOfNames; i++)
        if (!strcmp((const char *)base + names[i], name))
            return (PUCHAR)base + functions[ordinals[i]];
    return NULL;
}

/* applies the base relocations of an image that did not load at its preferred base. */
static void relocate(PVOID base)
{
    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)((PUCHAR)base + ((PIMAGE_DOS_HEADER)base)->e_lfanew);
    LONG64 delta = (LONG64)((ULONG64)base - nt->OptionalHeader.ImageBase);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    PUCHAR p = (PUCHAR)base + dir.VirtualAddress, end = p + dir.Size;
    SIZE_T size = nt->OptionalHeader.SizeOfImage;
    PVOID all = base;
    ULONG old;
    NtProtectVirtualMemory(NtCurrentProcess(), &all, &size, PAGE_READWRITE, &old);
    while (p < end) {
        PIMAGE_BASE_RELOCATION block = (PIMAGE_BASE_RELOCATION)p;
        if (!block->SizeOfBlock)
            break;
        PUSHORT entry = (PUSHORT)(block + 1);
        ULONG count = (block->SizeOfBlock - sizeof(*block)) / 2;
        for (ULONG i = 0; i < count; i++) {
            if ((entry[i] >> 12) == IMAGE_REL_BASED_DIR64) {
                PULONG64 target = (PULONG64)((PUCHAR)base + block->VirtualAddress + (entry[i] & 0xFFF));
                *target += (ULONG64)delta;
            }
        }
        p += block->SizeOfBlock;
    }
}

static void map_x64_ntdll(void)
{
    UNICODE_STRING path;
    RtlInitUnicodeString(&path, g_guest.is32 ? L"\\SystemRoot\\SysWOW64\\ntdll.dll" : L"\\SystemRoot\\SysX64\\ntdll.dll");
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    IO_STATUS_BLOCK iosb;
    HANDLE file, section;
    NTSTATUS status = NtOpenFile(&file, FILE_READ_DATA | FILE_EXECUTE | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(status))
        mx_fatal(status, "cannot open the x64 ntdll.dll");
    status = NtCreateSection(&section, SECTION_MAP_READ | SECTION_MAP_EXECUTE | SECTION_QUERY, NULL, NULL,
                             PAGE_EXECUTE_READ, SEC_IMAGE, file);
    NtClose(file);
    if (!NT_SUCCESS(status))
        mx_fatal(status, "invalid x64 ntdll.dll");
    PVOID base = NULL;
    SIZE_T size = 0;
    status = NtMapViewOfSection(section, NtCurrentProcess(), &base, 0, 0, NULL, &size, ViewShare, 0,
                                PAGE_EXECUTE_WRITECOPY);
    NtClose(section);
    if (status == STATUS_IMAGE_NOT_AT_BASE && !g_guest.is32)
        relocate(base);
    else if (!NT_SUCCESS(status))
        mx_fatal(status, "cannot map the x64 ntdll.dll");
    g_guest.ntdll = base;
    g_guest.ldr_initialize_thunk = (ULONG64)image_export(base, "LdrInitializeThunk");
    g_guest.user_thread_start = (ULONG64)image_export(base, "RtlUserThreadStart");
    g_guest.exception_dispatcher = (ULONG64)image_export(base, "KiUserExceptionDispatcher");
    g_guest.callback_dispatcher = (ULONG64)image_export(base, "KiUserCallbackDispatcher");
    if (!g_guest.is32)
        MxInstallNatives(base, TRUE);
    if (!g_guest.ldr_initialize_thunk || !g_guest.user_thread_start || !g_guest.exception_dispatcher ||
        (!g_guest.is32 && !g_guest.callback_dispatcher))
        mx_fatal(STATUS_ENTRYPOINT_NOT_FOUND, "the x64 ntdll.dll lacks an entry point");
}

static void process_init(void)
{
    g_env.syscall = env_syscall;
    g_env.fault = env_fault;
    g_env.timestamp = env_timestamp;
    g_env.code_changed = env_code_changed;
    g_env.code_seen = env_code_seen;
    g_env.suspend = env_suspend;
    PIMAGE_DOS_HEADER dos = NtCurrentPeb()->ImageBaseAddress;
    PIMAGE_NT_HEADERS32 nt = (void *)((PUCHAR)dos + dos->e_lfanew);
    g_guest.is32 = nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386;
    if (g_guest.is32) {
        g_guest.peb32 = MxCreatePeb32();
        if (!g_guest.peb32)
            mx_fatal(STATUS_NO_MEMORY, "cannot allocate the x86 process environment");
    }
    map_x64_ntdll();
}

/* ------------------------------------------------------------ entry points */

/*
 * the first instruction of a thread: context is the arm64 context the
 * kernel built on top of the thread stack, X0 the x64 start address and
 * X1 its parameter. the x64 ntdll initializes the thread, then continues
 * into its RtlUserThreadStart.
 */
MX_EXPORT DECLSPEC_NORETURN void LdrInitializeThunk(ARM64_NT_CONTEXT *context, PVOID base)
{
    static LONG initialized;
    if (!__atomic_exchange_n(&initialized, 1, __ATOMIC_SEQ_CST))
        process_init();

    MX_THREAD thread;
    memset(&thread, 0, sizeof(thread));
    thread.blocks = x86_new_blocks();
    if (g_guest.is32) {
        thread.teb32 = MxCreateTeb32(g_guest.peb32);
        if (!thread.teb32)
            mx_fatal(STATUS_NO_MEMORY, "cannot allocate the x86 thread environment");
    }
    NtCurrentTeb()->WOW32Reserved = &thread;
    thread.suspend.Critical = 1;
    MX_SUSPEND_GATE *gate = &thread.suspend;
    NTSTATUS gate_status = NtSetInformationThread(NtCurrentThread(), (THREADINFOCLASS)MX_THREAD_SUSPEND_GATE,
                                                  &gate, sizeof(gate));
    if (!NT_SUCCESS(gate_status))
        mx_fatal(gate_status, "cannot register the translated suspension gate");

    MX_FRAME frame;
    init_frame(&frame, &thread);

    if (g_guest.is32) {
        ULONG top = (ULONG)(ULONG_PTR)context & ~15u;
        I386_CONTEXT *ctx = (void *)(ULONG_PTR)((top - sizeof(*ctx)) & ~15u);
        memset(ctx, 0, sizeof(*ctx));
        ctx->ContextFlags = CONTEXT_I386_FULL;
        ctx->Eip = (ULONG)g_guest.user_thread_start;
        ULONG *stack = (void *)ctx;
        *--stack = context->X1 == (ULONG_PTR)NtCurrentPeb() ? (ULONG)(ULONG_PTR)g_guest.peb32 : (ULONG)context->X1;
        *--stack = (ULONG)context->X0;
        *--stack = 0xFFFF0000u;
        ctx->Esp = (ULONG)(ULONG_PTR)stack;
        ctx->EFlags = 0x202;
        ctx->SegCs = 0x23;
        ctx->SegSs = ctx->SegDs = ctx->SegEs = 0x2B;
        ctx->SegFs = 0x53;
        stack -= 16;
        *--stack = (ULONG)(ULONG_PTR)g_guest.ntdll;
        *--stack = (ULONG)(ULONG_PTR)ctx;
        *--stack = 0xFFFF0000u;
        frame.cpu.r[X86_RSP] = (ULONG)(ULONG_PTR)stack;
        frame.cpu.rip = g_guest.ldr_initialize_thunk;
        emulate(&thread, &frame);
        mx_fatal(STATUS_UNSUCCESSFUL, "the x86 loader returned");
    }

    ULONG64 top = (ULONG64)context & ~15ull;
    ULONG64 ctx_va = (top - sizeof(AMD64_CONTEXT)) & ~15ull;
    AMD64_CONTEXT *ctx = (AMD64_CONTEXT *)ctx_va;
    memset(ctx, 0, sizeof(*ctx));
    ctx->ContextFlags = CONTEXT_AMD64_FULL;
    ctx->Rip = g_guest.user_thread_start;
    ctx->Rcx = context->X0;
    ctx->Rdx = context->X1;
    ctx->Rsp = ctx_va - 8;
    ctx->EFlags = 0x202;
    ctx->SegCs = 0x33;
    ctx->SegDs = ctx->SegEs = ctx->SegSs = ctx->SegGs = 0x2B;
    ctx->SegFs = 0x53;
    ctx->MxCsr = 0x1F80;
    ctx->FltSave.ControlWord = 0x27F;
    ctx->FltSave.MxCsr = 0x1F80;
    ctx->FltSave.MxCsr_Mask = 0xFFFF;

    X86Cpu *c = &frame.cpu;
    c->r[X86_RSP] = (ctx_va - 0x80) & ~15ull;
    push64(c, MX_EXIT_ADDRESS);
    c->r[X86_RCX] = ctx_va;
    c->r[X86_RDX] = (ULONG64)g_guest.ntdll;
    c->rip = g_guest.ldr_initialize_thunk;
    emulate(&thread, &frame);
    mx_fatal(STATUS_UNSUCCESSFUL, "the x64 LdrInitializeThunk returned");
}

/* exported as RtlUserThreadStart, never used: x64 threads start in the x64 ntdll's. */
DECLSPEC_NORETURN void MxUserThreadStart(PVOID start, PVOID parameter)
{
    mx_fatal(STATUS_UNSUCCESSFUL, "native RtlUserThreadStart reached");
}

/* a fault of native code, normally a memory access of translated code. */
MX_EXPORT DECLSPEC_NORETURN void KiUserExceptionDispatcher(EXCEPTION_RECORD *record, ARM64_NT_CONTEXT *context)
{
    MX_THREAD *t = current_thread();
    if (!t || !t->frame) {
        mx_print("mimex86: exception %08x at %p outside of x64 code", (ULONG)record->ExceptionCode,
                 (PVOID)context->Pc);
        mx_fatal(record->ExceptionCode, "unhandled native exception");
    }
    MX_FRAME *f = t->frame;
    EXCEPTION_RECORD x64;
    memcpy(&x64, record, sizeof(x64));
    x64.ExceptionRecord = NULL;
    /* the faulting instruction runs again if the program resumes. */
    x86_jit_fault(&f->cpu, context->Pc);
    f->cpu.rip = f->cpu.insn_rip;
    if (x64.ExceptionCode == STATUS_ACCESS_VIOLATION && x64.NumberParameters >= 2 && x64.ExceptionInformation[0] == 8)
        x64.ExceptionInformation[0] = 0; /* the translator executes nothing from the faulting page */
    raise_from_cpu(f, &x64);
}

/* a win32k callback: x64 ntdll's KiUserCallbackDispatcher(api, arguments, length). */
MX_EXPORT DECLSPEC_NORETURN void KiUserCallbackDispatcher(ULONG api, PVOID arguments, ULONG length)
{
    MX_THREAD *t = current_thread();
    MX_FRAME frame;
    init_frame(&frame, t);
    X86Cpu *c = &frame.cpu;
    c->r[X86_RSP] = nested_stack(t->frame);
    if (g_guest.is32) {
        MX_CALLBACK32 callback;
        ULONG sp = (ULONG)c->r[X86_RSP];
        if (!Mx32MarshalCallback(&sp, api, arguments, length, &callback))
            NtCallbackReturn(NULL, 0, STATUS_NOT_SUPPORTED);
        c->r[X86_RSP] = sp;
        ULONG *stack = (void *)(ULONG_PTR)c->r[X86_RSP];
        *--stack = callback.Length;
        *--stack = (ULONG)(ULONG_PTR)callback.Guest;
        *--stack = api;
        *--stack = 0xFFFF0000u;
        c->r[X86_RSP] = (ULONG_PTR)stack;
        c->rip = g_guest.callback_dispatcher;
        frame.CallbackResult = NULL;
        frame.CallbackLength = 0;
        frame.CallbackStatus = STATUS_UNSUCCESSFUL;
        emulate(t, &frame);
        PVOID result = Mx32CallbackResult(&callback, frame.CallbackResult, &frame.CallbackLength);
        NtCallbackReturn(result, result ? frame.CallbackLength : 0, frame.CallbackStatus);
        mx_fatal(STATUS_UNSUCCESSFUL, "x86 callback returned");
    }
    push64(c, MX_EXIT_ADDRESS);
    c->r[X86_RCX] = api;
    c->r[X86_RDX] = (ULONG64)arguments;
    c->r[X86_R8] = length;
    c->rip = g_guest.callback_dispatcher;
    emulate(t, &frame);
    /* the x64 dispatcher returns through NtCallbackReturn and never comes back. */
    NtCallbackReturn(NULL, 0, STATUS_UNSUCCESSFUL);
    mx_fatal(STATUS_UNSUCCESSFUL, "callback returned");
}

/* a user apc: routine(context, argument1, argument2), then the interrupted native code resumes. */
MX_EXPORT DECLSPEC_NORETURN void KiUserApcDispatcher(PVOID context, PVOID argument1, PVOID argument2, PVOID routine,
                                                     ARM64_NT_CONTEXT *interrupted)
{
    MX_THREAD *t = current_thread();
    MX_FRAME frame;
    init_frame(&frame, t);
    X86Cpu *c = &frame.cpu;
    c->r[X86_RSP] = nested_stack(t->frame);
    if (g_guest.is32) {
        c->r[X86_RSP] -= 16;
        ULONG *args = (PULONG)(ULONG_PTR)c->r[X86_RSP];
        args[0] = (ULONG)c->exit_rip;
        args[1] = (ULONG)(ULONG_PTR)context;
        args[2] = (ULONG)(ULONG_PTR)argument1;
        args[3] = (ULONG)(ULONG_PTR)argument2;
    } else {
        push64(c, MX_EXIT_ADDRESS);
        c->r[X86_RCX] = (ULONG64)context;
        c->r[X86_RDX] = (ULONG64)argument1;
        c->r[X86_R8] = (ULONG64)argument2;
    }
    c->rip = (ULONG64)routine;
    emulate(t, &frame);
    NtContinue((PCONTEXT)interrupted, TRUE);
    mx_fatal(STATUS_UNSUCCESSFUL, "NtContinue failed after an apc");
}
