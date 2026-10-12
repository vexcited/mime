/*
 * mimex86.dll: the system dll of x64 processes.
 *
 * the kernel maps it instead of ntdll.dll into a process whose program is
 * not arm64, and enters user mode through its exports. it maps the x64
 * ntdll.dll, then runs every thread through the x86 core:
 *
 *   LdrInitializeThunk         starts a thread in x64 ntdll's LdrInitializeThunk
 *   KiUserExceptionDispatcher  turns a fault of translated code into an x64 exception
 *   KiUserCallbackDispatcher   runs a win32k callback through x64 ntdll
 *   KiUserApcDispatcher        runs an x64 apc routine
 *
 * system calls of the x64 code reach the kernel with the same numbers;
 * the few whose arguments depend on the processor are handled here.
 * native code runs on a stack of its own the kernel gives each thread,
 * the x64 code keeps the thread stack.
 */
#define MIME_NTDLL
#define NTSYSCALLAPI
#include <mime/ntrtl.h>

#include "mime_svc_nt.h"
#include "mime_svc_win32k.h"
#include "x86.h"

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
} MX_FRAME;

typedef struct MX_THREAD {
    MX_FRAME *frame; /* innermost emulation of the thread */
    void *blocks;    /* decoded block cache, shared by the thread's emulations */
    uint64_t trace[X86_TRACE_SIZE]; /* the last blocks run, for crash reports */
    uint32_t trace_pos;
    uint64_t fault_trace[16];       /* the blocks before the last fault */
} MX_THREAD;

static struct {
    PVOID ntdll;
    ULONG64 ldr_initialize_thunk;
    ULONG64 user_thread_start;
    ULONG64 exception_dispatcher;
    ULONG64 callback_dispatcher;
} g_x64;

static volatile uint32_t g_code_epoch = 1;

static X86Env g_env;

/* ------------------------------------------------------------ helpers */

static MX_THREAD *current_thread(void)
{
    return NtCurrentTeb()->WOW32Reserved;
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

/* ------------------------------------------------------------ emulation */

static void init_frame(MX_FRAME *f, MX_THREAD *t)
{
    x86_init_cpu(&f->cpu, &g_env, 1);
    f->cpu.gs_base = (ULONG64)NtCurrentTeb();
    f->cpu.blocks = t->blocks;
    f->cpu.code_epoch = &g_code_epoch;
    f->cpu.blocks_epoch = g_code_epoch;
    f->cpu.exit_rip = MX_EXIT_ADDRESS;
    f->cpu.user = t;
    f->cpu.trace = t->trace;
    f->cpu.trace_pos = &t->trace_pos;
    f->outer = t->frame;
}

/* runs the frame until its code returns to MX_EXIT_ADDRESS. */
static void emulate(MX_THREAD *t, MX_FRAME *f)
{
    t->frame = f;
    mx_setjmp(&f->restart);
    x86_run(&f->cpu);
    t->frame = f->outer;
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
        mx_fatal(record->ExceptionCode, "cannot deliver the exception");
    }
    ULONG64 sp = (context->Rsp - 0x80) & ~15ull;
    sp = (sp - sizeof(EXCEPTION_RECORD)) & ~15ull;
    ULONG64 rec = sp;
    sp = (sp - sizeof(AMD64_CONTEXT)) & ~15ull;
    ULONG64 ctx = sp;
    memcpy((void *)rec, record, sizeof(*record));
    memcpy((void *)ctx, context, sizeof(*context));
    c->r[X86_RSP] = sp - 0x28;
    c->r[X86_RCX] = rec;
    c->r[X86_RDX] = ctx;
    c->rip = g_x64.exception_dispatcher;
    mx_longjmp(&f->restart, 1);
}

static DECLSPEC_NORETURN void raise_from_cpu(MX_FRAME *f, EXCEPTION_RECORD *record)
{
    MX_THREAD *t = f->cpu.user;
    for (ULONG i = 0; i < 16; i++)
        t->fault_trace[i] = t->trace[(t->trace_pos - 16 + i) % X86_TRACE_SIZE];
    AMD64_CONTEXT ctx;
    cpu_to_context(&f->cpu, &ctx);
    record->ExceptionAddress = (PVOID)f->cpu.rip;
    raise_in_x64(f, record, &ctx);
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

static void env_code_changed(X86Cpu *c, uint64_t address, uint64_t size)
{
    __atomic_add_fetch(&g_code_epoch, 1, __ATOMIC_SEQ_CST);
}

/* ------------------------------------------------------------ system calls */

typedef ULONG64 (*SVC16)(ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64,
                         ULONG64, ULONG64, ULONG64, ULONG64, ULONG64, ULONG64);

static ULONG64 call_service(ULONG number, const ULONG64 *a)
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

static NTSTATUS get_context(X86Cpu *c, HANDLE thread, AMD64_CONTEXT *out)
{
    if (thread != NtCurrentThread())
        return STATUS_NOT_SUPPORTED;
    AMD64_CONTEXT ctx;
    cpu_to_context(c, &ctx);
    /* the caller resumes after the system call. */
    ctx.Rip = c->r[X86_RCX];
    ctx.Rax = 0;
    ULONG flags = out->ContextFlags;
    memcpy(out, &ctx, sizeof(ctx));
    out->ContextFlags = flags;
    return STATUS_SUCCESS;
}

static void env_syscall(X86Cpu *c)
{
    MX_THREAD *t = c->user;
    ULONG number = (ULONG)c->r[X86_RAX];
    ULONG64 a[16];
    a[0] = c->r[X86_R10];
    a[1] = c->r[X86_RDX];
    a[2] = c->r[X86_R8];
    a[3] = c->r[X86_R9];
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
        c->r[X86_RAX] = call_service(number, a);
        return;
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
        c->r[X86_RAX] = (ULONG64)(LONG64)STATUS_NOT_SUPPORTED;
        return;
    default:
        break;
    }

    MX_FRAME *frame = t->frame;
    ULONG64 result = call_service(number, a);
    /* a callback run during the call may have entered nested emulations. */
    t->frame = frame;
    c->r[X86_RAX] = result;

    switch (number) {
    case MIME_SVC_NtFlushInstructionCache:
    case MIME_SVC_NtFreeVirtualMemory:
    case MIME_SVC_NtUnmapViewOfSection:
    case MIME_SVC_NtUnmapViewOfSectionEx:
        env_code_changed(c, 0, 0);
        break;
    case MIME_SVC_NtProtectVirtualMemory:
        if (NT_SUCCESS((NTSTATUS)result)) {
            ULONG old = a[4] ? *(PULONG)a[4] : 0;
            if (((ULONG)a[3] | old) & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
                env_code_changed(c, 0, 0);
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
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
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
    UNICODE_STRING path = RTL_CONSTANT_STRING(L"\\SystemRoot\\SysX64\\ntdll.dll");
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
    if (status == STATUS_IMAGE_NOT_AT_BASE)
        relocate(base);
    else if (!NT_SUCCESS(status))
        mx_fatal(status, "cannot map the x64 ntdll.dll");
    g_x64.ntdll = base;
    g_x64.ldr_initialize_thunk = (ULONG64)image_export(base, "LdrInitializeThunk");
    g_x64.user_thread_start = (ULONG64)image_export(base, "RtlUserThreadStart");
    g_x64.exception_dispatcher = (ULONG64)image_export(base, "KiUserExceptionDispatcher");
    g_x64.callback_dispatcher = (ULONG64)image_export(base, "KiUserCallbackDispatcher");
    if (!g_x64.ldr_initialize_thunk || !g_x64.user_thread_start || !g_x64.exception_dispatcher ||
        !g_x64.callback_dispatcher)
        mx_fatal(STATUS_ENTRYPOINT_NOT_FOUND, "the x64 ntdll.dll lacks an entry point");
}

static void process_init(void)
{
    g_env.syscall = env_syscall;
    g_env.fault = env_fault;
    g_env.timestamp = env_timestamp;
    g_env.code_changed = env_code_changed;
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
    NtCurrentTeb()->WOW32Reserved = &thread;

    MX_FRAME frame;
    init_frame(&frame, &thread);

    ULONG64 top = (ULONG64)context & ~15ull;
    ULONG64 ctx_va = (top - sizeof(AMD64_CONTEXT)) & ~15ull;
    AMD64_CONTEXT *ctx = (AMD64_CONTEXT *)ctx_va;
    memset(ctx, 0, sizeof(*ctx));
    ctx->ContextFlags = CONTEXT_AMD64_FULL;
    ctx->Rip = g_x64.user_thread_start;
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
    c->r[X86_RDX] = (ULONG64)g_x64.ntdll;
    c->rip = g_x64.ldr_initialize_thunk;
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
    push64(c, MX_EXIT_ADDRESS);
    c->r[X86_RCX] = api;
    c->r[X86_RDX] = (ULONG64)arguments;
    c->r[X86_R8] = length;
    c->rip = g_x64.callback_dispatcher;
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
    push64(c, MX_EXIT_ADDRESS);
    c->r[X86_RCX] = (ULONG64)context;
    c->r[X86_RDX] = (ULONG64)argument1;
    c->r[X86_R8] = (ULONG64)argument2;
    c->rip = (ULONG64)routine;
    emulate(t, &frame);
    NtContinue((PCONTEXT)interrupted, TRUE);
    mx_fatal(STATUS_UNSUCCESSFUL, "NtContinue failed after an apc");
}
