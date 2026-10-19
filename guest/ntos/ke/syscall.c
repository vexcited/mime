/*
 * system service dispatch and user memory access.
 */
#include "../ntos.h"

#define MIME_NT_SERVICES_DEFINE_NAMES
#include "mime_svc_nt.h"
#define MIME_WIN32K_SERVICES_DEFINE_NAMES
#include "mime_svc_win32k.h"

typedef struct KI_SERVICE_TABLE {
    PKSERVICE_DESCRIPTOR Table;
    ULONG Count;
} KI_SERVICE_TABLE;

static KSERVICE_DESCRIPTOR g_nt_services[MIME_NT_SERVICES_COUNT];
static KI_SERVICE_TABLE g_tables[2] = {{g_nt_services, MIME_NT_SERVICES_COUNT}, {nullptr, 0}};
static UCHAR g_reported[2][4096 / 8];
/* calls of each service, for the thread dump; racy counts are good enough for that. */
static ULONG64 g_calls[2][4096];
/* the ticks the kernel lock was held by each service, not counting the time its thread waited, and the longest call */
static ULONG64 g_ticks[2][4096], g_longest[2][4096];

/* holds of the kernel lock that lasted long, by the service or vector that held it, see KiNoteLongHold. */
#define KI_HOLD_REASONS (0x2001 + 256)
static ULONG64 g_long_ticks[KI_HOLD_REASONS], g_long_max[KI_HOLD_REASONS], g_long_calls[KI_HOLD_REASONS];
static ULONG64 g_long_total;
void AfdDumpStats(void);
/* calls of each hypercall, the time they took, and how much of it with the kernel lock held; the x64 hal counts them. */
ULONG64 HalHypercallCalls[64], HalHypercallTicks[64], HalHypercallLongest[64], HalHypercallLockedTicks[64];

/* called as the lock is released after being held for longer than KiLongHoldTicks, with the lock still held. */
void KiNoteLongHold(ULONG64 held)
{
    ULONG reason = KiKernelLock[3];
    if (reason >= KI_HOLD_REASONS)
        reason = 0;
    g_long_ticks[reason] += held;
    g_long_calls[reason]++;
    g_long_total++;
    if (held > g_long_max[reason])
        g_long_max[reason] = held;
}

static WIN32_CALLOUTS g_win32;

void NTAPI PsEstablishWin32Callouts(WIN32_CALLOUTS *callouts)
{
    g_win32 = *callouts;
}

NTSTATUS PsWin32ProcessCallout(PEPROCESS process, BOOLEAN initialize)
{
    return g_win32.ProcessCallout ? g_win32.ProcessCallout(process, initialize) : STATUS_SUCCESS;
}

ULONG PsWin32ShutdownProcess(PEPROCESS process, const MX_SHUTDOWN_CONTEXT *context)
{
    return g_win32.ShutdownProcess ? g_win32.ShutdownProcess(process, context) : MX_SHUTDOWN_PROCESS_PASS;
}

NTSTATUS PsWin32ThreadCallout(PETHREAD thread, BOOLEAN initialize)
{
    return g_win32.ThreadCallout ? g_win32.ThreadCallout(thread, initialize) : STATUS_SUCCESS;
}

static NTSTATUS convert_to_gui_thread(void)
{
    PETHREAD thread = PsCurrentThread();
    PEPROCESS process = PsCurrentProcess();
    if (!g_win32.ThreadCallout)
        return STATUS_INVALID_SYSTEM_SERVICE;
    if (!process->Win32Process) {
        NTSTATUS status = g_win32.ProcessCallout(process, TRUE);
        if (!NT_SUCCESS(status))
            return status;
    }
    return g_win32.ThreadCallout(thread, TRUE);
}

NTSTATUS KeRegisterSystemServices(ULONG table, PKSERVICE_DESCRIPTOR services, ULONG count)
{
    if (table == 0) {
        for (ULONG i = 0; i < count; i++)
            g_nt_services[i] = services[i];
    }
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI KeAddSystemServiceTable(PKSERVICE_DESCRIPTOR table, PVOID unused, ULONG count, PVOID arguments,
                                      ULONG index)
{
    if (index != 1 || g_tables[1].Table)
        return FALSE;
    g_tables[1].Table = table;
    g_tables[1].Count = count;
    return TRUE;
}

void KiRegisterService(ULONG number, PVOID routine, UCHAR stack_args)
{
    if (number < MIME_NT_SERVICES_COUNT) {
        g_nt_services[number].Routine = routine;
        g_nt_services[number].StackArgs = stack_args;
        g_nt_services[number].Implemented = 1;
    }
}

/* the image name whose system calls are logged, from the kernel option trace-syscalls=<name>, or * for all. */
char KiTraceSyscallImage[16];
/* the service whose callers are walked too, from the kernel option trace-stack=<service name>. */
char KiTraceStackService[40];
/* a part of a service name the traced calls must have, from the kernel option trace-filter=<text>. */
char KiTraceFilter[40];

/*
 * the kernel option svc-timeline=<image name> records every system call of that image's processes with when it
 * began, how long it took and how much of that the thread was switched out, to be logged at the next dump.
 */
typedef struct KI_TIMELINE_ENTRY {
    ULONG64 Start, End, Blocked;
    ULONG Process, Thread, Service, Status;
    char Path[32];
} KI_TIMELINE_ENTRY;
#define KI_TIMELINE_ENTRIES (1u << 20)
char KiTimelineImage[16];
static KI_TIMELINE_ENTRY *g_timeline;
static volatile LONG g_timeline_used;

/* the end of the name an OBJECT_ATTRIBUTES points to, as ascii, for the timeline. */
static void timeline_path(ULONG64 attributes, char out[32])
{
    out[0] = 0;
    ULONG64 attrs[3];
    if (!attributes || !NT_SUCCESS(KiCopyFromUser(attrs, (PVOID)attributes, sizeof(attrs))) || !attrs[2])
        return;
    struct {
        USHORT Length, Maximum;
        ULONG Pad;
        ULONG64 Buffer;
    } name;
    if (!NT_SUCCESS(KiCopyFromUser(&name, (PVOID)attrs[2], sizeof(name))) || !name.Length || !name.Buffer)
        return;
    ULONG chars = name.Length / 2, skip = chars > 31 ? chars - 31 : 0, n = chars - skip;
    WCHAR wide[32];
    if (!NT_SUCCESS(KiCopyFromUser(wide, (PVOID)(name.Buffer + skip * 2), n * 2)))
        return;
    for (ULONG i = 0; i < n; i++)
        out[i] = wide[i] < 0x80 && wide[i] >= 0x20 && wide[i] != ' ' ? (char)wide[i] : '?';
    out[n] = 0;
}

/* records a page fault of the traced image as a call to the pseudo service 0x2000 + kind. */
void KiTimelineFault(ULONG64 started, ULONG kind, ULONG address_page)
{
    PKTHREAD thread = KiCurrentThread();
    if (!g_timeline || !thread || strcmp(PsCurrentProcess()->ImageFileName, KiTimelineImage))
        return;
    LONG slot = __atomic_fetch_add(&g_timeline_used, 1, __ATOMIC_RELAXED);
    if ((ULONG)slot >= KI_TIMELINE_ENTRIES)
        return;
    KI_TIMELINE_ENTRY *e = &g_timeline[slot];
    e->Start = started;
    e->End = HalReadCounter();
    e->Blocked = 0;
    e->Process = (ULONG)(ULONG_PTR)PsCurrentProcess()->UniqueProcessId;
    e->Thread = (ULONG)(ULONG_PTR)PsCurrentThread()->Cid.UniqueThread;
    e->Service = 0x2000 + kind;
    e->Status = address_page;
    e->Path[0] = 0;
}

/* appends the timeout of a wait to the timeline's text, in milliseconds, or inf for none. */
static void timeline_timeout(const char *name, const ULONG64 *args, char out[32])
{
    int slot = -1;
    if (!strcmp(name, "NtWaitForSingleObject") || !strcmp(name, "NtDelayExecution"))
        slot = !strcmp(name, "NtDelayExecution") ? 1 : 2;
    else if (!strcmp(name, "NtWaitForMultipleObjects") || !strcmp(name, "NtRemoveIoCompletion") ||
             !strcmp(name, "NtRemoveIoCompletionEx"))
        slot = 4;
    else if (!strcmp(name, "NtWaitForAlertByThreadId"))
        slot = 1;
    else if (!strcmp(name, "NtSignalAndWaitForSingleObject"))
        slot = 3;
    size_t used = strlen(out);
    if (!strcmp(name, "NtUserMsgWaitForMultipleObjectsEx")) {
        mime_format(out + used, 32 - used, "/t%u", (ULONG)args[2]);
        return;
    }
    if (slot < 0)
        return;
    LONG64 value;
    if (!args[slot])
        mime_format(out + used, 32 - used, "/tinf");
    else if (NT_SUCCESS(KiCopyFromUser(&value, (PVOID)args[slot], sizeof(value))))
        mime_format(out + used, 32 - used, value < 0 ? "/t%lld" : "/ta%lld", value < 0 ? -value / 10000 : value / 10000);
}

/* the name a thread is given with NtSetInformationThread, as ascii. */
static void timeline_thread_name(ULONG64 string, char out[32])
{
    struct {
        USHORT Length, Maximum;
        ULONG Pad;
        ULONG64 Buffer;
    } name;
    out[0] = 0;
    if (!string || !NT_SUCCESS(KiCopyFromUser(&name, (PVOID)string, sizeof(name))) || !name.Length || !name.Buffer)
        return;
    ULONG n = name.Length / 2 > 31 ? 31 : name.Length / 2;
    WCHAR wide[32];
    if (!NT_SUCCESS(KiCopyFromUser(wide, (PVOID)name.Buffer, n * 2)))
        return;
    for (ULONG i = 0; i < n; i++)
        out[i] = wide[i] < 0x80 && wide[i] >= 0x20 && wide[i] != ' ' ? (char)wide[i] : '?';
    out[n] = 0;
}

static const char *service_name(ULONG table, ULONG index)
{
    if (table == 2)
        return index == 0 ? "fault-fast" : index == 1 ? "fault-slow" : index == 2 ? "fault-fast-miss" : "fault";
    if (table == 0 && index < MIME_NT_SERVICES_COUNT && MIME_NT_SERVICES_NAMES[index])
        return MIME_NT_SERVICES_NAMES[index];
    if (table == 1 && index < MIME_WIN32K_SERVICES_COUNT && MIME_WIN32K_SERVICES_NAMES[index])
        return MIME_WIN32K_SERVICES_NAMES[index];
    return "?";
}

void KiSystemService(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    thread->TrapFrame = frame;
    thread->PreviousMode = UserMode;
    KiEnableInterrupts();

    ULONG number = KI_SERVICE_NUMBER(frame);
    ULONG table = number >> 12;
    ULONG index = number & 0xFFF;
    ULONG64 result;
    ULONG64 started = HalReadCounter(), blocked = thread->BlockedTicks;
    KiKernelLock[3] = (table << 12 | index) + 1;
    BOOLEAN timeline = g_timeline && table < 2 && !strcmp(PsCurrentProcess()->ImageFileName, KiTimelineImage);
    char tpath[32];
    tpath[0] = 0;
    if (table < 2)
        g_calls[table][index]++;
    KI_SERVICE_TABLE *t = table < 2 ? &g_tables[table] : nullptr;
    if (table == 1 && !thread->Win32Thread && g_win32.ThreadCallout) {
        NTSTATUS status = convert_to_gui_thread();
        if (!NT_SUCCESS(status))
            KiLogPrintf("cannot convert thread to a gui thread: %08x", status);
    }
    if (!t || !t->Table || index >= t->Count || !t->Table[index].Routine) {
        if (table < 2 && !(g_reported[table][index / 8] & (1u << (index % 8)))) {
            g_reported[table][index / 8] |= (UCHAR)(1u << (index % 8));
            KiLogPrintf("unimplemented system service %s (0x%04x) called by %s", service_name(table, index), number,
                        PsCurrentProcess()->ImageFileName);
            /* who calls it, for the first call. */
            KiTraceUserStack(KI_TRAP_PC(frame), KI_TRAP_SP(frame));
        }
        result = table == 1 ? 0 : (ULONG64)(LONG64)STATUS_NOT_IMPLEMENTED;
    } else {
        ULONG64 args[16];
        for (int i = 0; i < KI_SERVICE_REGISTER_ARGS; i++)
            args[i] = KI_SERVICE_REGISTER_ARG(frame, i);
        ULONG stack = t->Table[index].StackArgs;
        if (stack > 16 - KI_SERVICE_REGISTER_ARGS)
            stack = 16 - KI_SERVICE_REGISTER_ARGS;
        BOOLEAN trace = KiTraceSyscallImage[0] &&
                        (KiTraceSyscallImage[0] == '*' || !strcmp(PsCurrentProcess()->ImageFileName, KiTraceSyscallImage)) &&
                        (!KiTraceFilter[0] || strstr(service_name(table, index), KiTraceFilter));
        BOOLEAN copied = !stack || NT_SUCCESS(KiCopyFromUser(&args[KI_SERVICE_REGISTER_ARGS],
                                                             (PVOID)KI_SERVICE_STACK_ARGS(frame),
                                                             stack * sizeof(ULONG64)));
        if (timeline) {
            const char *name = service_name(table, index);
            /* the object a wait or a signal is on, to tell who woke whom. */
            if (!strcmp(name, "NtWaitForSingleObject") || !strcmp(name, "NtSetEvent") || !strcmp(name, "NtResetEvent") ||
                !strcmp(name, "NtReleaseSemaphore") || !strcmp(name, "NtReleaseMutant") || !strcmp(name, "NtClearEvent") ||
                !strcmp(name, "NtPulseEvent") || !strcmp(name, "NtSetTimer") || !strcmp(name, "NtSetTimer2")) {
                PVOID object;
                if (NT_SUCCESS(ObReferenceHandle((HANDLE)args[0], nullptr, UserMode, &object, nullptr))) {
                    mime_format(tpath, sizeof(tpath), "obj%llx", (ULONG64)(ULONG_PTR)object & 0xFFFFFFFFFFFull);
                    ObDereferenceObject(object);
                }
            }
            timeline_timeout(name, args, tpath);
            if (!strcmp(name, "NtSetInformationThread") && args[1] == 38)
                timeline_thread_name(args[2], tpath);
            if (!strcmp(name, "NtCreateFile") || !strcmp(name, "NtOpenFile") || !strcmp(name, "NtOpenKey") ||
                !strcmp(name, "NtOpenKeyEx") || !strcmp(name, "NtCreateKey"))
                timeline_path(args[2], tpath);
            else if (!strcmp(name, "NtQueryAttributesFile") || !strcmp(name, "NtQueryFullAttributesFile") ||
                     !strcmp(name, "NtDeleteFile"))
                timeline_path(args[0], tpath);
        }
        if (trace) {
            /* the first return address on the stack names the caller of the system call stub. */
            char caller[96] = "";
            ULONG64 ret = 0;
            if (!NT_SUCCESS(KiCopyFromUser(&ret, (PVOID)KI_TRAP_SP(frame), sizeof(ret))) ||
                !KiDescribeUserAddress((ULONG_PTR)ret, caller, sizeof(caller)))
                caller[0] = 0;
            ULONG shown = KI_SERVICE_REGISTER_ARGS + stack;
            KiLogPrintf("svc %u.%u %s(%llx, %llx, %llx, %llx, %llx, %llx, %llx) %s",
                        (ULONG)(ULONG_PTR)PsCurrentProcess()->UniqueProcessId,
                        (ULONG)(ULONG_PTR)PsCurrentThread()->Cid.UniqueThread, service_name(table, index), args[0],
                        args[1], args[2], args[3], shown > 4 ? args[4] : 0, shown > 5 ? args[5] : 0,
                        shown > 6 ? args[6] : 0, caller);
            if (KiTraceStackService[0] && !strcmp(service_name(table, index), KiTraceStackService))
                KiTraceUserStack(KI_TRAP_PC(frame), KI_TRAP_SP(frame));
        }
        if (!copied)
            result = (ULONG64)(LONG64)STATUS_ACCESS_VIOLATION;
        else
            result = KiCallService(t->Table[index].Routine, args, stack);
        if (trace) {
            KiLogPrintf("svc %u.%u %s = %llx", (ULONG)(ULONG_PTR)PsCurrentProcess()->UniqueProcessId,
                        (ULONG)(ULONG_PTR)PsCurrentThread()->Cid.UniqueThread, service_name(table, index), result);
            /* the message a queue call returned, as window, message, wparam and lparam. */
            if (table == 1 && (ULONG)result == 1 && !strncmp(service_name(table, index), "NtUser", 6) &&
                (!strcmp(service_name(table, index) + 6, "PeekMessage") || !strcmp(service_name(table, index) + 6, "GetMessage"))) {
                ULONG64 msg[4];
                if (NT_SUCCESS(KiCopyFromUser(msg, (PVOID)args[0], sizeof(msg))))
                    KiLogPrintf("msg %u.%u hwnd %llx message %llx wparam %llx lparam %llx",
                                (ULONG)(ULONG_PTR)PsCurrentProcess()->UniqueProcessId,
                                (ULONG)(ULONG_PTR)PsCurrentThread()->Cid.UniqueThread, msg[0], msg[1] & 0xFFFFFFFF,
                                msg[2], msg[3]);
            }
        }
    }
    if (timeline) {
        LONG slot = __atomic_fetch_add(&g_timeline_used, 1, __ATOMIC_RELAXED);
        if ((ULONG)slot < KI_TIMELINE_ENTRIES) {
            KI_TIMELINE_ENTRY *e = &g_timeline[slot];
            e->Start = started;
            e->End = HalReadCounter();
            e->Blocked = thread->BlockedTicks - blocked;
            e->Process = (ULONG)(ULONG_PTR)PsCurrentProcess()->UniqueProcessId;
            e->Thread = (ULONG)(ULONG_PTR)PsCurrentThread()->Cid.UniqueThread;
            e->Service = table << 12 | index;
            e->Status = (ULONG)result;
            memcpy(e->Path, tpath, sizeof(tpath));
        }
    }
    if (table < 2) {
        ULONG64 held = HalReadCounter() - started - (thread->BlockedTicks - blocked);
        g_ticks[table][index] += held;
        if (held > g_longest[table][index])
            g_longest[table][index] = held;
    }
    /* the level belongs to the processor, so a service that leaves it raised would break the next thread to wait here. */
    PKPCR pcr = KeGetPcr();
    if (pcr->Irql != PASSIVE_LEVEL) {
        KiLogPrintf("service %s returned at irql %u", service_name(table, index), (ULONG)pcr->Irql);
        pcr->Irql = PASSIVE_LEVEL;
    }
    KI_SERVICE_RESULT(frame) = result;
    KiExitToUserMode(frame);
}

BOOLEAN KiIsUserRange(const void *address, SIZE_T length)
{
    ULONG_PTR a = (ULONG_PTR)address;
    return a + length >= a && a + length <= MIME_USER_ADDRESS_LIMIT + 1;
}

/* copies from caller memory; kernel mode callers (Zw* services) pass kernel buffers. */
NTSTATUS KiCopyFromUser(void *dst, const void *user_src, SIZE_T length)
{
    if (!length)
        return STATUS_SUCCESS;
    if (KiCurrentThread()->PreviousMode == KernelMode && !KiIsUserRange(user_src, length)) {
        memcpy(dst, user_src, length);
        return STATUS_SUCCESS;
    }
    if (!KiIsUserRange(user_src, length))
        return STATUS_ACCESS_VIOLATION;
    KJUMP_BUFFER jb;
    if (KI_TRY_USER(jb)) {
        memcpy(dst, user_src, length);
        KI_END_TRY_USER();
        return STATUS_SUCCESS;
    }
    return STATUS_ACCESS_VIOLATION;
}

NTSTATUS KiCopyToUser(void *user_dst, const void *src, SIZE_T length)
{
    if (!length)
        return STATUS_SUCCESS;
    if (KiCurrentThread()->PreviousMode == KernelMode && !KiIsUserRange(user_dst, length)) {
        memcpy(user_dst, src, length);
        return STATUS_SUCCESS;
    }
    if (!KiIsUserRange(user_dst, length))
        return STATUS_ACCESS_VIOLATION;
    KJUMP_BUFFER jb;
    if (KI_TRY_USER(jb)) {
        memcpy(user_dst, src, length);
        KI_END_TRY_USER();
        return STATUS_SUCCESS;
    }
    return STATUS_ACCESS_VIOLATION;
}

/* driver facing copies that always treat the pointer as user memory, with fault recovery. */
NTSTATUS NTAPI MimeCopyFromUser(void *dst, const void *user_src, SIZE_T length)
{
    KPROCESSOR_MODE old = KiCurrentThread()->PreviousMode;
    KiCurrentThread()->PreviousMode = UserMode;
    NTSTATUS status = KiCopyFromUser(dst, user_src, length);
    KiCurrentThread()->PreviousMode = old;
    return status;
}

NTSTATUS NTAPI MimeCopyToUser(void *user_dst, const void *src, SIZE_T length)
{
    KPROCESSOR_MODE old = KiCurrentThread()->PreviousMode;
    KiCurrentThread()->PreviousMode = UserMode;
    NTSTATUS status = KiCopyToUser(user_dst, src, length);
    KiCurrentThread()->PreviousMode = old;
    return status;
}

/* captures a caller supplied UNICODE_STRING into pool, honoring the previous mode. */
NTSTATUS KiCaptureUnicodeString(PUNICODE_STRING copy, PCUNICODE_STRING src)
{
    UNICODE_STRING s;
    copy->Buffer = nullptr;
    copy->Length = copy->MaximumLength = 0;
    if (!src)
        return STATUS_SUCCESS;
    if (KeGetPreviousMode() == UserMode) {
        if (!NT_SUCCESS(KiCopyFromUser(&s, src, sizeof(s))))
            return STATUS_ACCESS_VIOLATION;
    } else {
        s = *src;
    }
    if (s.Length & 1 || s.Length > 0xFFFC)
        return STATUS_OBJECT_NAME_INVALID;
    copy->Buffer = ExAllocatePool2(POOL_FLAG_PAGED, s.Length + sizeof(WCHAR), KI_TAG('C', 'a', 'p', 'S'));
    if (!copy->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = STATUS_SUCCESS;
    if (KeGetPreviousMode() == UserMode)
        status = KiCopyFromUser(copy->Buffer, s.Buffer, s.Length);
    else
        memcpy(copy->Buffer, s.Buffer, s.Length);
    if (!NT_SUCCESS(status)) {
        ExFreePool(copy->Buffer);
        copy->Buffer = nullptr;
        return status;
    }
    copy->Buffer[s.Length / sizeof(WCHAR)] = 0;
    copy->Length = s.Length;
    copy->MaximumLength = (USHORT)(s.Length + sizeof(WCHAR));
    return STATUS_SUCCESS;
}

void KiFreeCapturedString(PUNICODE_STRING s)
{
    if (s->Buffer)
        ExFreePool(s->Buffer);
    s->Buffer = nullptr;
}

void NTAPI ProbeForRead(PVOID address, SIZE_T length, ULONG alignment)
{
    if (length && !KiIsUserRange(address, length))
        KeBugCheckEx(0x0000001E, STATUS_ACCESS_VIOLATION, (ULONG_PTR)address, 0, 0);
}

void NTAPI ProbeForWrite(PVOID address, SIZE_T length, ULONG alignment)
{
    ProbeForRead(address, length, alignment);
}

void KiEnableTimeline(void)
{
    g_timeline = ExAllocatePool2(POOL_FLAG_NON_PAGED, KI_TIMELINE_ENTRIES * sizeof(KI_TIMELINE_ENTRY), KI_TAG('T', 'l', 'i', 'n'));
}

/* one line per call, several to a log write: pid tid start duration blocked status service, times in microseconds. */
static void dump_timeline(void)
{
    ULONG used = (ULONG)g_timeline_used;
    if (used > KI_TIMELINE_ENTRIES)
        used = KI_TIMELINE_ENTRIES;
    char buf[1024];
    int n = 0;
    for (ULONG i = 0; i < used; i++) {
        KI_TIMELINE_ENTRY *e = &g_timeline[i];
        n += mime_format(buf + n, sizeof(buf) - n, "tl %u %u %llu %llu %llu %x %s %s\n", e->Process, e->Thread,
                         HalCounterToHns(e->Start) / 10, HalCounterToHns(e->End - e->Start) / 10,
                         HalCounterToHns(e->Blocked) / 10, e->Status, service_name(e->Service >> 12, e->Service & 0xFFF), e->Path);
        if (n > (int)sizeof(buf) - 160 || i + 1 == used) {
            HalLog(buf, (SIZE_T)n);
            n = 0;
        }
    }
    g_timeline_used = 0;
}

/* the services called most since the last dump, then the counts start over. */
void KiDumpServiceCalls(void)
{
    if (g_timeline)
        dump_timeline();
    MM_FAULT_STATS total = {};
    for (ULONG i = 0; i < MAXIMUM_PROCESSORS; i++) {
        total.Fast += MmFaultStats[i].Fast;
        total.Gated += MmFaultStats[i].Gated;
        total.Rejected += MmFaultStats[i].Rejected;
        total.Slow += MmFaultStats[i].Slow;
    }
    KiLogPrintf("faults: %llu without the kernel lock, %llu turned away at the gate, %llu not demand zero, %llu with it",
                total.Fast, total.Gated, total.Rejected, total.Slow);
    ULONG64 acquires = 0, contended = 0, waited = 0, longest = 0;
    for (ULONG i = 0; i < MAXIMUM_PROCESSORS; i++) {
        acquires += KiLockStats[i][0];
        contended += KiLockStats[i][1];
        waited += KiLockStats[i][2];
        if (KiLockStats[i][3] > longest)
            longest = KiLockStats[i][3];
    }
    KiLogPrintf("kernel lock: %llu taken, %llu after waiting, %llu us waited in all, longest wait %llu us", acquires,
                contended, HalCounterToHns(waited) / 10, HalCounterToHns(longest) / 10);
    /* the services that held the lock longest, with how often and how long at most. */
    for (int round = 0; round < 12; round++) {
        ULONG64 best = 0;
        ULONG bt = 0, bi = 0;
        for (ULONG t = 0; t < 2; t++)
            for (ULONG i = 0; i < 4096; i++)
                if (g_ticks[t][i] > best)
                    best = g_ticks[t][i], bt = t, bi = i;
        if (!best)
            break;
        ULONG64 calls = g_calls[bt][bi];
        KiLogPrintf("calls %s: %llu, held %llu us in all, %llu ns each, longest %llu us", service_name(bt, bi), calls,
                    HalCounterToHns(best) / 10, calls ? HalCounterToHns(best) * 100 / calls : 0,
                    HalCounterToHns(g_longest[bt][bi]) / 10);
        g_ticks[bt][bi] = 0;
    }
    /* the lock as a whole: the time it was held, and what held it for a long time. */
    KiLogPrintf("kernel lock: held %llu us in all, %llu holds over %llu us", HalCounterToHns(KiKernelLock[6] | (ULONG64)KiKernelLock[7] << 32) / 10,
                g_long_total, HalCounterToHns(KiLongHoldTicks) / 10);
    for (int round = 0; round < 10; round++) {
        ULONG64 best = 0;
        ULONG bi = 0;
        for (ULONG i = 0; i < KI_HOLD_REASONS; i++)
            if (g_long_ticks[i] > best)
                best = g_long_ticks[i], bi = i;
        if (!best)
            break;
        const char *name = bi < 0x2001 && bi ? service_name((bi - 1) >> 12, (bi - 1) & 0xFFF) : "trap";
        KiLogPrintf("long holds by %s %u: %llu, %llu us in all, at most %llu us", name,
                    bi < 0x2001 ? 0 : bi - 0x2001, g_long_calls[bi], HalCounterToHns(best) / 10,
                    HalCounterToHns(g_long_max[bi]) / 10);
        g_long_ticks[bi] = 0;
    }
    /* the hypercalls made with the lock held, the host's work that every other processor waited for. */
    for (int round = 0; round < 8; round++) {
        ULONG64 best = 0;
        ULONG bi = 0;
        for (ULONG i = 0; i < 64; i++)
            if (HalHypercallTicks[i] > best)
                best = HalHypercallTicks[i], bi = i;
        if (!best)
            break;
        KiLogPrintf("hypercall 0x%02x: %llu, %llu us in all, %llu us of it with the lock, at most %llu us", bi,
                    HalHypercallCalls[bi], HalCounterToHns(best) / 10, HalCounterToHns(HalHypercallLockedTicks[bi]) / 10,
                    HalCounterToHns(HalHypercallLongest[bi]) / 10);
        HalHypercallTicks[bi] = 0;
    }
    AfdDumpStats();
    memset(HalHypercallCalls, 0, sizeof(HalHypercallCalls));
    memset(HalHypercallLockedTicks, 0, sizeof(HalHypercallLockedTicks));
    memset(HalHypercallLongest, 0, sizeof(HalHypercallLongest));
    memset(g_long_calls, 0, sizeof(g_long_calls));
    memset(g_long_max, 0, sizeof(g_long_max));
    g_long_total = 0;
    KiKernelLock[6] = KiKernelLock[7] = 0;
    memset(g_calls, 0, sizeof(g_calls));
    memset(g_ticks, 0, sizeof(g_ticks));
    memset(g_longest, 0, sizeof(g_longest));
    memset(KiLockStats, 0, sizeof(KiLockStats));
    memset(MmFaultStats, 0, sizeof(MmFaultStats));
}
