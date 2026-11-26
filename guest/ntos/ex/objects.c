/* executive: KUSER_SHARED_DATA, synchronization objects and waits. */
#include "../ntos.h"

POBJECT_TYPE ExEventType;
POBJECT_TYPE ExMutantType;
POBJECT_TYPE ExSemaphoreType;
POBJECT_TYPE ExTimerType;
static POBJECT_TYPE g_timer2_type; /* IRTimer, the timers of NtCreateTimer2. */
POBJECT_TYPE DbgkDebugObjectType;
#define DEBUG_ALL_ACCESS 0x001F000F
#define DEBUG_READ_EVENT 0x0001
#define DEBUG_SET_INFORMATION 0x0008
#define SEMAPHORE_QUERY_STATE 0x0001
#define TIMER_MODIFY_STATE 0x0002
KUSER_SHARED_DATA *SharedUserData;

void MiSetKuserPage(ULONG64 pa);
void MiInitializeSections(void);

typedef struct ETIMER {
    KTIMER Timer;
    KDPC Dpc;
    /* runs the setting thread's completion routine as a user apc. */
    KAPC Apc;
    PETHREAD ApcThread; /* referenced */
    PVOID ApcRoutine;
    PVOID ApcContext;
    KSPIN_LOCK ApcLock;
} ETIMER;

static void init_shared_data(void)
{
    ULONG64 pa = MmAllocatePage();
    SharedUserData = HalPhysmap(pa);
    MiMapPage(MiKernelRoot(), MIME_KVA_KUSER_SHARED, pa, MM_KERNEL_RW);
    MiSetKuserPage(pa);
    KUSER_SHARED_DATA *k = SharedUserData;
    k->TickCountMultiplier = 0x0FA00000; /* 15.625 ms in 24.8 fixed point, as on windows */
    k->ImageNumberLow = MIME_NATIVE_MACHINE;
    k->ImageNumberHigh = MIME_NATIVE_MACHINE;
    static const WCHAR root[] = L"C:\\Windows"; /* the case of the boot entry, measured on windows 11 26300 */
    memcpy(k->NtSystemRoot, root, sizeof(root));
    k->MaxStackTraceDepth = 0;
    k->TimeZoneId = 0;
    k->LargePageMinimum = 0x200000;
    k->NtBuildNumber = MIME_NT_BUILD_NUMBER;
    k->NtProductType = NtProductWinNt;
    k->ProductTypeIsValid = TRUE;
    k->NativeProcessorArchitecture = MIME_NATIVE_ARCHITECTURE;
    k->NtMajorVersion = MIME_NT_MAJOR_VERSION;
    k->NtMinorVersion = MIME_NT_MINOR_VERSION;
    KiReadProcessorIds();
    KiIdentifyToHypervisor();
    KiSetProcessorFeatures(k->ProcessorFeatures);
#ifdef __x86_64__
    KiSetExtendedState(&k->XState);
    k->CyclesPerYield = KiMeasureCyclesPerYield();
    k->VirtualizationFlags = KiVirtualizationFlags();
#endif
    /* the low halves of the highest user address and of the start of the system range, kept from 32 bit windows. */
    k->Reserved1 = 0x7FFEFFFF;
    k->Reserved3 = 0x80000000;
    k->SuiteMask = 0x110;            /* VER_SUITE_SINGLEUSERTS | VER_SUITE_TERMINAL */
    k->MitigationPolicies = 0x0A;    /* NXSupportPolicy and SEHValidationPolicy both opt in, as an x64 machine boots */
    k->ComPlusPackage = 0xFFFFFFFF;  /* unknown until SystemComPlusPackage is asked for */
    k->SharedDataFlags = 0x100;      /* DbgMultiSessionSku, the licence of the client editions */
    k->TestRetInstruction = 0xC3;
    k->TelemetryCoverageRound = 1;
    k->LangGenerationCount = 1;
    k->ActiveConsoleId = 1;
    k->NumberOfPhysicalPages = (ULONG)MmGetTotalPages();
    k->FullNumberOfPhysicalPages = MmGetTotalPages();
    k->QpcFrequency = (LONGLONG)KiQpcFrequency();
    k->QpcBypassEnabled = MIME_QPC_BYPASS_ENABLED; /* the clock tick adds the hypervisor's page when there is one. */
    k->ActiveProcessorCount = HalBootInfo->cpu_count;
    k->ActiveGroupCount = 1;
    k->UnparkedProcessorCount = (USHORT)HalBootInfo->cpu_count;
    LONG bias = HalBootInfo->timezone_bias;
    LONGLONG bias_hns = (LONGLONG)bias * 60 * 10000000;
    k->TimeZoneBias.High2Time = (LONG)(bias_hns >> 32);
    k->TimeZoneBias.LowPart = (ULONG)bias_hns;
    k->TimeZoneBias.High1Time = (LONG)(bias_hns >> 32);
    k->Cookie = (ULONG)(HalReadCounter() * 0x9E3779B97F4A7C15ull >> 32);
}

static void drop_apc_thread(ETIMER *t)
{
    KIRQL irql;
    KeAcquireSpinLock(&t->ApcLock, &irql);
    PETHREAD thread = t->ApcThread;
    t->ApcThread = nullptr;
    t->ApcRoutine = nullptr;
    KeReleaseSpinLock(&t->ApcLock, irql);
    if (thread)
        ObDereferenceObject(thread);
}

static void timer_delete(PVOID object)
{
    KeCancelTimer(&((ETIMER *)object)->Timer);
    drop_apc_thread(object);
}

/* the user part runs the routine; the timer stays referenced until then. */
static void NTAPI timer_apc_kernel(PKAPC apc, PKNORMAL_ROUTINE *normal, PVOID *context, PVOID *a1, PVOID *a2)
{
    ObDereferenceObject(CONTAINING_RECORD(apc, ETIMER, Apc));
}

static void NTAPI timer_apc_rundown(PKAPC apc)
{
    ObDereferenceObject(CONTAINING_RECORD(apc, ETIMER, Apc));
}

/*
 * on expiry the routine is queued to the setting thread with the expiry time
 * in its two arguments. an apc still queued is not queued twice, so a periodic
 * timer of a thread that never waits alertably delivers once.
 */
static void NTAPI timer_dpc(PKDPC dpc, PVOID context, PVOID arg1, PVOID arg2)
{
    ETIMER *t = context;
    KIRQL irql;
    KeAcquireSpinLock(&t->ApcLock, &irql);
    if (t->ApcThread && t->ApcRoutine && !t->Apc.Inserted) {
        KeInitializeApc(&t->Apc, &t->ApcThread->Tcb, OriginalApcEnvironment, timer_apc_kernel, timer_apc_rundown,
                        (PKNORMAL_ROUTINE)t->ApcRoutine, UserMode, t->ApcContext);
        ObReferenceObject(t);
        LARGE_INTEGER now;
        KeQuerySystemTime(&now);
        if (!KeInsertQueueApc(&t->Apc, (PVOID)(ULONG_PTR)now.LowPart, (PVOID)(ULONG_PTR)(ULONG)now.HighPart, 0))
            ObDereferenceObject(t);
    }
    KeReleaseSpinLock(&t->ApcLock, irql);
}

/*
 * a mutant deleted while owned is abandoned, as windows does when the last handle of an owned mutant closes.
 * it leaves the owner's mutant list, which a thread walks when it ends, and would otherwise hold freed memory.
 */
static void NTAPI mutant_delete(PVOID object)
{
    PKMUTANT m = object;
    if (m->Header.SignalState != 1 && m->OwnerThread)
        KeReleaseMutant(m, 0, TRUE, FALSE);
}

void ExInitialize(void)
{
    init_shared_data();
    ExpInitializeAtoms();
    ExpInitializeWnf();
    ExpInitializeFirmware();
    ExpInitializePower();
    KeInitializeEvent(&MimeInputReadyEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&MimeGpuEvent, NotificationEvent, FALSE);
    OBJECT_TYPE_INITIALIZER init = {};
    init.Waitable = TRUE;
    init.ValidAccessMask = EVENT_ALL_ACCESS;
    ObSetGenericMapping(&init, 0x20001, 0x20002, 0x120000, 0x1F0003);
    ExEventType = ObCreateObjectType(L"Event", &init);
    init.ValidAccessMask = MUTANT_ALL_ACCESS;
    ObSetGenericMapping(&init, 0x20001, 0x20000, 0x120000, 0x1F0001);
    init.DeleteProcedure = mutant_delete;
    ExMutantType = ObCreateObjectType(L"Mutant", &init);
    init.DeleteProcedure = nullptr;
    init.ValidAccessMask = SEMAPHORE_ALL_ACCESS;
    ObSetGenericMapping(&init, 0x20001, 0x20002, 0x120000, 0x1F0003);
    ExSemaphoreType = ObCreateObjectType(L"Semaphore", &init);
    init.ValidAccessMask = TIMER_ALL_ACCESS;
    init.DeleteProcedure = timer_delete;
    ObSetGenericMapping(&init, 0x20001, 0x20002, 0x120000, 0x1F0003);
    ExTimerType = ObCreateObjectType(L"Timer", &init);
    g_timer2_type = ObCreateObjectType(L"IRTimer", &init);
    init.DeleteProcedure = nullptr;
    init.Waitable = FALSE;
    init.ValidAccessMask = DEBUG_ALL_ACCESS;
    ObSetGenericMapping(&init, 0x20001, 0x20002, 0x120000, DEBUG_ALL_ACCESS);
    DbgkDebugObjectType = ObCreateObjectType(L"DebugObject", &init);
    MiInitializeSections();
    ExpInitializeEtw();
}

static NTSTATUS create_named(POBJECT_TYPE type, ULONG size, PHANDLE handle, ACCESS_MASK access,
                             POBJECT_ATTRIBUTES attributes, PVOID *body, void (*init)(PVOID, ULONG_PTR, ULONG_PTR),
                             ULONG_PTR a, ULONG_PTR b)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    PVOID object;
    status = ObCreateObject(type, attributes ? &oa : nullptr, KeGetPreviousMode(), size, &object);
    if (NT_SUCCESS(status)) {
        memset(object, 0, size);
        init(object, a, b);
        HANDLE h;
        status = ObInsertObjectNamed(object, attributes ? &oa : nullptr, access, KeGetPreviousMode(), &h);
        if (NT_SUCCESS(status)) {
            NTSTATUS st = ObStoreHandle(handle, h);
            if (!NT_SUCCESS(st))
                status = st;
        }
    }
    KiFreeCapturedString(&name);
    return status;
}

static NTSTATUS open_named(POBJECT_TYPE type, PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    NTSTATUS status = ObCaptureObjectAttributes(attributes, &oa, &name);
    if (!NT_SUCCESS(status))
        return status;
    HANDLE h;
    status = ObOpenObjectByName(&oa, type, KeGetPreviousMode(), nullptr, access, nullptr, &h);
    if (NT_SUCCESS(status))
        status = ObStoreHandle(handle, h);
    KiFreeCapturedString(&name);
    return status;
}

static void init_event(PVOID o, ULONG_PTR type, ULONG_PTR state)
{
    KeInitializeEvent(o, (EVENT_TYPE)type, (BOOLEAN)state);
}

static void init_mutant(PVOID o, ULONG_PTR owner, ULONG_PTR unused)
{
    KeInitializeMutant(o, (BOOLEAN)owner);
}

static void init_semaphore(PVOID o, ULONG_PTR count, ULONG_PTR limit)
{
    KeInitializeSemaphore(o, (LONG)count, (LONG)limit);
}

static void init_timer(PVOID o, ULONG_PTR type, ULONG_PTR unused)
{
    memset(o, 0, sizeof(ETIMER));
    KeInitializeTimerEx(&((ETIMER *)o)->Timer, (TIMER_TYPE)type);
    KeInitializeSpinLock(&((ETIMER *)o)->ApcLock);
}

NTSTATUS NTAPI NtCreateEvent(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, EVENT_TYPE type,
                             BOOLEAN state)
{
    if (type != NotificationEvent && type != SynchronizationEvent)
        return STATUS_INVALID_PARAMETER;
    return create_named(ExEventType, sizeof(KEVENT), handle, access, attributes, nullptr, init_event, type, state);
}

NTSTATUS NTAPI ZwCreateEvent(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, EVENT_TYPE type,
                             BOOLEAN state)
{
    PKTHREAD t = KiCurrentThread();
    KPROCESSOR_MODE old = t->PreviousMode;
    t->PreviousMode = KernelMode;
    NTSTATUS status = NtCreateEvent(handle, access, attributes, type, state);
    t->PreviousMode = old;
    return status;
}

NTSTATUS NTAPI NtOpenEvent(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    return open_named(ExEventType, handle, access, attributes);
}

/* like ProbeForWriteLong: the caller's value is read and written back, so a bad pointer fails the call before it acts. */
static NTSTATUS probe_previous(PLONG previous)
{
    LONG value;
    if (!previous || KeGetPreviousMode() != UserMode)
        return STATUS_SUCCESS;
    NTSTATUS status = KiCopyFromUser(&value, previous, sizeof(value));
    return NT_SUCCESS(status) ? KiCopyToUser(previous, &value, sizeof(value)) : status;
}

static NTSTATUS event_op(HANDLE handle, PLONG previous, int op)
{
    PKEVENT event;
    NTSTATUS status = probe_previous(previous);
    if (!NT_SUCCESS(status))
        return status;
    status = ObReferenceHandleFor(handle, EVENT_MODIFY_STATE, ExEventType, KeGetPreviousMode(), &event);
    if (!NT_SUCCESS(status))
        return status;
    LONG old = op == 0 ? KeSetEvent(event, 1, FALSE) : op == 1 ? KeResetEvent(event) : KePulseEvent(event, 1, FALSE);
    ObDereferenceObject(event);
    if (previous) {
        if (KeGetPreviousMode() == UserMode)
            KiCopyToUser(previous, &old, sizeof(old));
        else
            *previous = old;
    }
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtSetEvent(HANDLE handle, PLONG previous)
{
    return event_op(handle, previous, 0);
}

NTSTATUS NTAPI ZwSetEvent(HANDLE handle, PLONG previous)
{
    PKTHREAD t = KiCurrentThread();
    KPROCESSOR_MODE old = t->PreviousMode;
    t->PreviousMode = KernelMode;
    NTSTATUS status = NtSetEvent(handle, previous);
    t->PreviousMode = old;
    return status;
}

NTSTATUS NTAPI NtResetEvent(HANDLE handle, PLONG previous)
{
    return event_op(handle, previous, 1);
}

NTSTATUS NTAPI NtClearEvent(HANDLE handle)
{
    return event_op(handle, nullptr, 1);
}

NTSTATUS NTAPI NtPulseEvent(HANDLE handle, PLONG previous)
{
    return event_op(handle, previous, 2);
}

NTSTATUS NTAPI NtCreateMutant(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, BOOLEAN owner)
{
    return create_named(ExMutantType, sizeof(KMUTANT), handle, access, attributes, nullptr, init_mutant, owner, 0);
}

NTSTATUS NTAPI NtOpenMutant(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    return open_named(ExMutantType, handle, access, attributes);
}

NTSTATUS NTAPI NtReleaseMutant(HANDLE handle, PLONG previous)
{
    PKMUTANT m;
    NTSTATUS status = ObReferenceHandle(handle, ExMutantType, KeGetPreviousMode(), (PVOID *)&m, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    LONG old = KeReleaseMutant(m, 1, FALSE, FALSE);
    ObDereferenceObject(m);
    if (old == (LONG)0x80000000)
        return STATUS_MUTANT_NOT_OWNED;
    if (previous)
        KiCopyToUser(previous, &old, sizeof(old));
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtCreateSemaphore(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, LONG initial,
                                 LONG maximum)
{
    if (maximum <= 0 || initial < 0 || initial > maximum)
        return STATUS_INVALID_PARAMETER;
    return create_named(ExSemaphoreType, sizeof(KSEMAPHORE), handle, access, attributes, nullptr, init_semaphore,
                        (ULONG_PTR)initial, (ULONG_PTR)maximum);
}

NTSTATUS NTAPI NtOpenSemaphore(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    return open_named(ExSemaphoreType, handle, access, attributes);
}

NTSTATUS NTAPI NtReleaseSemaphore(HANDLE handle, LONG count, PLONG previous)
{
    PKSEMAPHORE s;
    NTSTATUS status = probe_previous(previous);
    if (!NT_SUCCESS(status))
        return status;
    status = ObReferenceHandleFor(handle, SEMAPHORE_MODIFY_STATE, ExSemaphoreType, KeGetPreviousMode(), &s);
    if (!NT_SUCCESS(status))
        return status;
    LONG old = KeReleaseSemaphore(s, 1, count, FALSE);
    ObDereferenceObject(s);
    if (old < 0)
        return STATUS_SEMAPHORE_LIMIT_EXCEEDED;
    if (previous)
        KiCopyToUser(previous, &old, sizeof(old));
    return STATUS_SUCCESS;
}

/* the basic information of an event or a semaphore is two longs, and only that class and size are taken. */
static NTSTATUS query_basic(HANDLE handle, POBJECT_TYPE type, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    if (klass != 0)
        return STATUS_INVALID_INFO_CLASS;
    if (length != 2 * sizeof(LONG))
        return STATUS_INFO_LENGTH_MISMATCH;
    PDISPATCHER_HEADER object;
    ACCESS_MASK query = type == ExEventType ? EVENT_QUERY_STATE : SEMAPHORE_QUERY_STATE;
    NTSTATUS status = ObReferenceHandleFor(handle, query, type, KeGetPreviousMode(), &object);
    if (!NT_SUCCESS(status))
        return status;
    LONG basic[2];
    if (type == ExEventType) {
        basic[0] = object->Type == EventSynchronizationObject ? 1 : 0;
        basic[1] = object->SignalState;
    } else {
        basic[0] = object->SignalState;
        basic[1] = ((PKSEMAPHORE)object)->Limit;
    }
    ObDereferenceObject(object);
    status = KiCopyToUser(info, basic, sizeof(basic));
    ULONG size = sizeof(basic);
    if (NT_SUCCESS(status) && returned)
        status = KiCopyToUser(returned, &size, sizeof(size));
    return status;
}

NTSTATUS NTAPI NtQuerySemaphore(HANDLE handle, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    return query_basic(handle, ExSemaphoreType, klass, info, length, returned);
}

NTSTATUS NTAPI NtQueryEvent(HANDLE handle, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    return query_basic(handle, ExEventType, klass, info, length, returned);
}

NTSTATUS NTAPI NtCreateTimer(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, TIMER_TYPE type)
{
    return create_named(ExTimerType, sizeof(ETIMER), handle, access, attributes, nullptr, init_timer, type, 0);
}

NTSTATUS NTAPI NtOpenTimer(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    return open_named(ExTimerType, handle, access, attributes);
}

/*
 * SetWaitableTimer and CancelWaitableTimer take the high resolution timers of CreateWaitableTimerEx, which are of
 * the other type: measured with hrt2.c of the research repository.
 */
static NTSTATUS reference_timer(HANDLE handle, ETIMER **timer)
{
    NTSTATUS status = ObReferenceHandleFor(handle, TIMER_MODIFY_STATE, ExTimerType, KeGetPreviousMode(), timer);
    if (status == STATUS_OBJECT_TYPE_MISMATCH)
        status = ObReferenceHandleFor(handle, TIMER_MODIFY_STATE, g_timer2_type, KeGetPreviousMode(), timer);
    return status;
}

NTSTATUS NTAPI NtSetTimer(HANDLE handle, PLARGE_INTEGER due, PVOID apc, PVOID apc_context, BOOLEAN resume,
                          LONG period, PBOOLEAN previous)
{
    ETIMER *t;
    LARGE_INTEGER d;
    if (!NT_SUCCESS(KiCopyFromUser(&d, due, sizeof(d))))
        return STATUS_ACCESS_VIOLATION;
    NTSTATUS status = reference_timer(handle, &t);
    if (!NT_SUCCESS(status))
        return status;
    BOOLEAN state = KeReadStateTimer(&t->Timer);
    KeCancelTimer(&t->Timer);
    drop_apc_thread(t);
    if (apc) {
        PETHREAD thread = PsCurrentThread();
        ObReferenceObject(thread);
        KIRQL irql;
        KeAcquireSpinLock(&t->ApcLock, &irql);
        t->ApcThread = thread;
        t->ApcRoutine = apc;
        t->ApcContext = apc_context;
        KeReleaseSpinLock(&t->ApcLock, irql);
        KeInitializeDpc(&t->Dpc, timer_dpc, t);
    }
    KeSetTimerEx(&t->Timer, d, period, apc ? &t->Dpc : nullptr);
    ObDereferenceObject(t);
    if (previous)
        KiCopyToUser(previous, &state, sizeof(state));
    return STATUS_SUCCESS;
}

/*
 * the timers of the thread pool, measured on windows 11 26300 with scratch/dlls/probe/t2.c of the research
 * repository. they are of a type of their own, IRTimer, have no name, and are waited on: bit 31 of the attributes
 * makes one that stays signaled, bits 2 and 3 are taken, bit 1 is refused, and any other bit or bit 1 with bit 2 or 3
 * is an invalid fourth parameter. the second parameter must be null, and so must the attributes of the object.
 */
NTSTATUS NTAPI NtCreateTimer2(PHANDLE handle, PVOID reserved, POBJECT_ATTRIBUTES attributes, ULONG flags, ACCESS_MASK access)
{
    HANDLE probe;
    if (KeGetPreviousMode() == UserMode &&
        (!NT_SUCCESS(KiCopyFromUser(&probe, handle, sizeof(probe))) || !NT_SUCCESS(KiCopyToUser(handle, &probe, sizeof(probe)))))
        return STATUS_ACCESS_VIOLATION;
    if ((flags & ~0x8000000Eu) || ((flags & 2) && (flags & 0xC)))
        return STATUS_INVALID_PARAMETER_4;
    if (attributes)
        return STATUS_INVALID_PARAMETER_3;
    if (flags & 2) {
        UCHAR byte;
        return reserved && !NT_SUCCESS(KiCopyFromUser(&byte, reserved, sizeof(byte))) ? STATUS_ACCESS_VIOLATION
                                                                                        : STATUS_INVALID_PARAMETER;
    }
    if (reserved)
        return STATUS_INVALID_PARAMETER_2;
    return create_named(g_timer2_type, sizeof(ETIMER), handle, access & (0xFF000000 | TIMER_ALL_ACCESS), nullptr, nullptr,
                        init_timer, flags & 0x80000000 ? NotificationTimer : SynchronizationTimer, 0);
}

/*
 * the due time must be given. the period is a time too, in units of 100 ns, and may be left out, like the parameters,
 * whose version must be 0 and whose tolerance is not used.
 */
/*
 * NtSetTimerEx with TimerSetCoalescableTimer, the one class there is: the arguments of NtSetTimer in a structure, with
 * a delay the timer may take that nothing here uses. kernelbase's SetCoalescableTimer and SetWaitableTimerEx come
 * through it.
 */
NTSTATUS NTAPI NtSetTimerEx(HANDLE handle, ULONG klass, PVOID information, ULONG length)
{
    struct {
        LARGE_INTEGER DueTime;
        PVOID ApcRoutine, ApcContext, WakeContext;
        ULONG Period, TolerableDelay;
        PBOOLEAN PreviousState;
    } info;
    if (klass != 0)
        return STATUS_INVALID_INFO_CLASS;
    if (length != sizeof(info))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (!NT_SUCCESS(KiCopyFromUser(&info, information, sizeof(info))))
        return STATUS_ACCESS_VIOLATION;
    return NtSetTimer(handle, (PLARGE_INTEGER)information, info.ApcRoutine, info.ApcContext, info.WakeContext != nullptr,
                      (LONG)info.Period, info.PreviousState);
}

NTSTATUS NTAPI NtSetTimer2(HANDLE handle, PLARGE_INTEGER due, PLARGE_INTEGER period, PVOID parameters)
{
    if (!due)
        return STATUS_INVALID_PARAMETER_2;
    LARGE_INTEGER d, p = {};
    struct {
        ULONG Version, Reserved;
        LONGLONG NoWakeTolerance;
    } set = {};
    if (!NT_SUCCESS(KiCopyFromUser(&d, due, sizeof(d))) || (period && !NT_SUCCESS(KiCopyFromUser(&p, period, sizeof(p)))) ||
        (parameters && !NT_SUCCESS(KiCopyFromUser(&set, parameters, sizeof(set)))))
        return STATUS_ACCESS_VIOLATION;
    if (set.Version)
        return STATUS_INVALID_PARAMETER_3;
    ETIMER *t;
    NTSTATUS status = ObReferenceHandleFor(handle, TIMER_MODIFY_STATE, g_timer2_type, KeGetPreviousMode(), &t);
    if (!NT_SUCCESS(status))
        return status;
    LONGLONG every = p.QuadPart < 0 ? -p.QuadPart : p.QuadPart;
    LONG milliseconds = every ? (LONG)((every + 9999) / 10000 > 0x7FFFFFFF ? 0x7FFFFFFF : (every + 9999) / 10000) : 0;
    KeCancelTimer(&t->Timer);
    KeSetTimerEx(&t->Timer, d, milliseconds, nullptr);
    ObDereferenceObject(t);
    return STATUS_SUCCESS;
}

/* the parameters are not read. a timer that fired stays as it is. */
NTSTATUS NTAPI NtCancelTimer2(HANDLE handle, PVOID parameters)
{
    ETIMER *t;
    NTSTATUS status = ObReferenceHandleFor(handle, TIMER_MODIFY_STATE, g_timer2_type, KeGetPreviousMode(), &t);
    if (!NT_SUCCESS(status))
        return status;
    KeCancelTimer(&t->Timer);
    ObDereferenceObject(t);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtCancelTimer(HANDLE handle, PBOOLEAN current)
{
    ETIMER *t;
    NTSTATUS status = reference_timer(handle, &t);
    if (!NT_SUCCESS(status))
        return status;
    BOOLEAN state = KeReadStateTimer(&t->Timer);
    KeCancelTimer(&t->Timer);
    drop_apc_thread(t);
    ObDereferenceObject(t);
    if (current)
        KiCopyToUser(current, &state, sizeof(state));
    return STATUS_SUCCESS;
}

/*
 * the objects stay referenced for the duration of the wait. the optional extra object is waited on after the handles
 * and its index is the count, as the message queue of a thread is in a wait for messages.
 */
NTSTATUS ExWaitHandlesAndObject(ULONG count, const HANDLE *handles, WAIT_TYPE type, BOOLEAN alertable,
                                PLARGE_INTEGER timeout, KPROCESSOR_MODE mode, PKEVENT extra)
{
    ULONG total = count + (extra ? 1 : 0);
    if (total == 0 || total > MAXIMUM_WAIT_OBJECTS)
        return STATUS_INVALID_PARAMETER_1;
    PVOID objects[MAXIMUM_WAIT_OBJECTS];
    PVOID waits[MAXIMUM_WAIT_OBJECTS];
    NTSTATUS status = STATUS_SUCCESS;
    ULONG referenced = 0;
    for (; referenced < count; referenced++) {
        ACCESS_MASK granted;
        status = ObReferenceHandle(handles[referenced], nullptr, KeGetPreviousMode(), &objects[referenced], &granted);
        if (!NT_SUCCESS(status))
            break;
        /* an object that cannot be waited on is told as such, whatever its handle allows. */
        waits[referenced] = ObGetWaitObject(objects[referenced]);
        if (!waits[referenced])
            status = STATUS_OBJECT_TYPE_MISMATCH;
        else if (!ObHandleGrants(objects[referenced], granted, SYNCHRONIZE, KeGetPreviousMode()))
            status = STATUS_ACCESS_DENIED;
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(objects[referenced]);
            break;
        }
    }
    if (NT_SUCCESS(status)) {
        if (extra)
            waits[count] = extra;
        PKWAIT_BLOCK blocks = nullptr;
        if (total > THREAD_WAIT_OBJECTS) {
            blocks = ExAllocatePool2(POOL_FLAG_NON_PAGED, total * sizeof(KWAIT_BLOCK), KI_TAG('E', 'x', 'W', 'b'));
            if (!blocks)
                status = STATUS_INSUFFICIENT_RESOURCES;
        }
        if (NT_SUCCESS(status))
            status = KiWaitObjects(total, waits, type, UserRequest, mode, alertable, timeout, blocks);
        if (blocks)
            ExFreePool(blocks);
    }
    for (ULONG i = 0; i < referenced; i++)
        ObDereferenceObject(objects[i]);
    return status;
}

NTSTATUS ExWaitHandles(ULONG count, const HANDLE *handles, WAIT_TYPE type, BOOLEAN alertable, PLARGE_INTEGER timeout,
                       KPROCESSOR_MODE mode)
{
    if (count == 0 || count > MAXIMUM_WAIT_OBJECTS)
        return STATUS_INVALID_PARAMETER_1;
    return ExWaitHandlesAndObject(count, handles, type, alertable, timeout, mode, nullptr);
}

NTSTATUS NTAPI NtWaitForSingleObject(HANDLE handle, BOOLEAN alertable, PLARGE_INTEGER timeout)
{
    LARGE_INTEGER t;
    if (timeout && !NT_SUCCESS(KiCopyFromUser(&t, timeout, sizeof(t))))
        return STATUS_ACCESS_VIOLATION;
    return ExWaitHandles(1, &handle, WaitAny, alertable, timeout ? &t : nullptr, KeGetPreviousMode());
}

NTSTATUS NTAPI ZwWaitForSingleObject(HANDLE handle, BOOLEAN alertable, PLARGE_INTEGER timeout)
{
    PKTHREAD t = KiCurrentThread();
    KPROCESSOR_MODE old = t->PreviousMode;
    t->PreviousMode = KernelMode;
    NTSTATUS status = ExWaitHandles(1, &handle, WaitAny, alertable, timeout, KernelMode);
    t->PreviousMode = old;
    return status;
}

NTSTATUS NTAPI NtWaitForMultipleObjects(ULONG count, PHANDLE handles, WAIT_TYPE type, BOOLEAN alertable,
                                        PLARGE_INTEGER timeout)
{
    HANDLE copy[MAXIMUM_WAIT_OBJECTS];
    LARGE_INTEGER t;
    if (count == 0 || count > MAXIMUM_WAIT_OBJECTS)
        return STATUS_INVALID_PARAMETER_1;
    if (!NT_SUCCESS(KiCopyFromUser(copy, handles, count * sizeof(HANDLE))) ||
        (timeout && !NT_SUCCESS(KiCopyFromUser(&t, timeout, sizeof(t)))))
        return STATUS_ACCESS_VIOLATION;
    return ExWaitHandles(count, copy, type == WaitAll ? WaitAll : WaitAny, alertable, timeout ? &t : nullptr, UserMode);
}

/* the wait of a 32-bit program, which wow64 passes on with the handles as the program has them, 32 bits each. */
NTSTATUS NTAPI NtWaitForMultipleObjects32(ULONG count, LONG *handles, WAIT_TYPE type, BOOLEAN alertable,
                                          PLARGE_INTEGER timeout)
{
    LONG narrow[MAXIMUM_WAIT_OBJECTS];
    HANDLE copy[MAXIMUM_WAIT_OBJECTS];
    LARGE_INTEGER t;
    if (count == 0 || count > MAXIMUM_WAIT_OBJECTS)
        return STATUS_INVALID_PARAMETER_1;
    if (!NT_SUCCESS(KiCopyFromUser(narrow, handles, count * sizeof(LONG))) ||
        (timeout && !NT_SUCCESS(KiCopyFromUser(&t, timeout, sizeof(t)))))
        return STATUS_ACCESS_VIOLATION;
    for (ULONG i = 0; i < count; i++)
        copy[i] = (HANDLE)(LONG_PTR)narrow[i];
    return ExWaitHandles(count, copy, type == WaitAll ? WaitAll : WaitAny, alertable, timeout ? &t : nullptr, UserMode);
}

NTSTATUS NTAPI NtSignalAndWaitForSingleObject(HANDLE signal, HANDLE wait, BOOLEAN alertable, PLARGE_INTEGER timeout)
{
    PVOID object;
    ACCESS_MASK granted;
    NTSTATUS status = ObReferenceHandle(signal, nullptr, KeGetPreviousMode(), &object, &granted);
    if (!NT_SUCCESS(status))
        return status;
    POBJECT_TYPE type = ObGetObjectType(object);
    /* an event and a semaphore need the right to change their state. a mutant needs its owner, and no right. */
    ACCESS_MASK needed = type == ExEventType ? EVENT_MODIFY_STATE : type == ExSemaphoreType ? SEMAPHORE_MODIFY_STATE : 0;
    if (!ObHandleGrants(object, granted, needed, KeGetPreviousMode())) {
        ObDereferenceObject(object);
        return STATUS_ACCESS_DENIED;
    }
    if (type == ExEventType)
        KeSetEvent(object, 1, TRUE);
    else if (type == ExSemaphoreType)
        KeReleaseSemaphore(object, 1, 1, TRUE);
    else if (type == ExMutantType)
        KeReleaseMutant(object, 1, FALSE, TRUE);
    ObDereferenceObject(object);
    return NtWaitForSingleObject(wait, alertable, timeout);
}

/* ------------------------------------------------------------------ debug objects */

/*
 * the object a debugger reads the events of a process from. mime attaches no process to one, so a wait for an event
 * runs into its timeout, as on windows when the debugged process is idle.
 */
typedef struct DEBUG_OBJECT {
    KEVENT EventsPresent;
    ULONG Flags;
} DEBUG_OBJECT;

#define DEBUG_KILL_ON_CLOSE 0x1

static void init_debug_object(PVOID body, ULONG_PTR flags, ULONG_PTR unused)
{
    DEBUG_OBJECT *debug = body;
    KeInitializeEvent(&debug->EventsPresent, NotificationEvent, FALSE);
    debug->Flags = (ULONG)flags;
}

NTSTATUS NTAPI NtCreateDebugObject(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, ULONG flags)
{
    if (flags & ~DEBUG_KILL_ON_CLOSE)
        return STATUS_INVALID_PARAMETER;
    return create_named(DbgkDebugObjectType, sizeof(DEBUG_OBJECT), handle, access, attributes, nullptr,
                        init_debug_object, flags, 0);
}

/* DebugObjectKillProcessOnExitInformation is the one class: a ULONG with the kill on close flag. */
NTSTATUS NTAPI NtSetInformationDebugObject(HANDLE handle, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    if (klass != 1)
        return STATUS_INVALID_PARAMETER;
    if (returned) {
        ULONG need = sizeof(ULONG);
        if (!NT_SUCCESS(KiCopyToUser(returned, &need, sizeof(need))))
            return STATUS_ACCESS_VIOLATION;
    }
    if (length != sizeof(ULONG))
        return STATUS_INFO_LENGTH_MISMATCH;
    ULONG flags;
    if (!NT_SUCCESS(KiCopyFromUser(&flags, info, sizeof(flags))))
        return STATUS_ACCESS_VIOLATION;
    if (flags & ~DEBUG_KILL_ON_CLOSE)
        return STATUS_INVALID_PARAMETER;
    DEBUG_OBJECT *debug;
    NTSTATUS status = ObReferenceHandleFor(handle, DEBUG_SET_INFORMATION, DbgkDebugObjectType, KeGetPreviousMode(), &debug);
    if (!NT_SUCCESS(status))
        return status;
    debug->Flags = flags;
    ObDereferenceObject(debug);
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtWaitForDebugEvent(HANDLE handle, BOOLEAN alertable, PLARGE_INTEGER timeout, PVOID state)
{
    LARGE_INTEGER t;
    if (timeout && !NT_SUCCESS(KiCopyFromUser(&t, timeout, sizeof(t))))
        return STATUS_ACCESS_VIOLATION;
    DEBUG_OBJECT *debug;
    NTSTATUS status = ObReferenceHandleFor(handle, DEBUG_READ_EVENT, DbgkDebugObjectType, KeGetPreviousMode(), &debug);
    if (!NT_SUCCESS(status))
        return status;
    status = KeWaitForSingleObject(&debug->EventsPresent, Executive, KeGetPreviousMode(), alertable,
                                   timeout ? &t : nullptr);
    ObDereferenceObject(debug);
    return status;
}
