/*
 * executive: KUSER_SHARED_DATA, synchronization objects and waits.
 */
#include "../ntos.h"

POBJECT_TYPE ExEventType;
POBJECT_TYPE ExMutantType;
POBJECT_TYPE ExSemaphoreType;
POBJECT_TYPE ExTimerType;
POBJECT_TYPE *ExEventObjectType = &ExEventType;
POBJECT_TYPE *ExSemaphoreObjectType = &ExSemaphoreType;
KUSER_SHARED_DATA *SharedUserData;

void MiSetKuserPage(ULONG64 pa);
void MiInitializeSections(void);

typedef struct ETIMER {
    KTIMER Timer;
    KDPC Dpc;
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
    static const WCHAR root[] = L"C:\\Windows";
    memcpy(k->NtSystemRoot, root, sizeof(root));
    k->MaxStackTraceDepth = 0;
    k->CryptoExponent = 0x10001;
    k->TimeZoneId = 0;
    k->LargePageMinimum = 0x200000;
    k->NtBuildNumber = MIME_NT_BUILD_NUMBER;
    k->NtProductType = NtProductWinNt;
    k->ProductTypeIsValid = TRUE;
    k->NativeProcessorArchitecture = MIME_NATIVE_ARCHITECTURE;
    k->NtMajorVersion = MIME_NT_MAJOR_VERSION;
    k->NtMinorVersion = MIME_NT_MINOR_VERSION;
    KiReadProcessorIds();
    KiSetProcessorFeatures(k->ProcessorFeatures);
    k->SuiteMask = 0x100;            /* VER_SUITE_SINGLEUSERTS */
    k->ActiveConsoleId = 1;
    k->NumberOfPhysicalPages = (ULONG)MmGetTotalPages();
    k->FullNumberOfPhysicalPages = MmGetTotalPages();
    k->QpcFrequency = (LONGLONG)HalBootInfo->timer_frequency;
    k->QpcBypassEnabled = 1;
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

static void timer_delete(PVOID object)
{
    KeCancelTimer(&((ETIMER *)object)->Timer);
}

void ExInitialize(void)
{
    init_shared_data();
    KeInitializeEvent(&MimeInputReadyEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&MimeGpuEvent, NotificationEvent, FALSE);
    OBJECT_TYPE_INITIALIZER init = {0};
    init.Waitable = TRUE;
    init.ValidAccessMask = EVENT_ALL_ACCESS;
    ExEventType = ObCreateObjectType(L"Event", &init);
    init.ValidAccessMask = MUTANT_ALL_ACCESS;
    ExMutantType = ObCreateObjectType(L"Mutant", &init);
    init.ValidAccessMask = SEMAPHORE_ALL_ACCESS;
    ExSemaphoreType = ObCreateObjectType(L"Semaphore", &init);
    init.ValidAccessMask = TIMER_ALL_ACCESS;
    init.DeleteProcedure = timer_delete;
    ExTimerType = ObCreateObjectType(L"Timer", &init);
    MiInitializeSections();
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
    status = ObCreateObject(type, attributes ? &oa : NULL, KeGetPreviousMode(), size, &object);
    if (NT_SUCCESS(status)) {
        memset(object, 0, size);
        init(object, a, b);
        HANDLE h;
        status = ObInsertObjectNamed(object, attributes ? &oa : NULL, access, KeGetPreviousMode(), &h);
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
    status = ObOpenObjectByName(&oa, type, KeGetPreviousMode(), access, NULL, &h);
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
    KeInitializeTimerEx(&((ETIMER *)o)->Timer, (TIMER_TYPE)type);
}

NTSTATUS NTAPI NtCreateEvent(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, EVENT_TYPE type,
                             BOOLEAN state)
{
    if (type != NotificationEvent && type != SynchronizationEvent)
        return STATUS_INVALID_PARAMETER;
    return create_named(ExEventType, sizeof(KEVENT), handle, access, attributes, NULL, init_event, type, state);
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

static NTSTATUS event_op(HANDLE handle, PLONG previous, int op)
{
    PKEVENT event;
    NTSTATUS status = ObReferenceHandle(handle, ExEventType, KeGetPreviousMode(), (PVOID *)&event, NULL);
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
    return event_op(handle, NULL, 1);
}

NTSTATUS NTAPI NtPulseEvent(HANDLE handle, PLONG previous)
{
    return event_op(handle, previous, 2);
}

NTSTATUS NTAPI NtCreateMutant(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, BOOLEAN owner)
{
    return create_named(ExMutantType, sizeof(KMUTANT), handle, access, attributes, NULL, init_mutant, owner, 0);
}

NTSTATUS NTAPI NtOpenMutant(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    return open_named(ExMutantType, handle, access, attributes);
}

NTSTATUS NTAPI NtReleaseMutant(HANDLE handle, PLONG previous)
{
    PKMUTANT m;
    NTSTATUS status = ObReferenceHandle(handle, ExMutantType, KeGetPreviousMode(), (PVOID *)&m, NULL);
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
    return create_named(ExSemaphoreType, sizeof(KSEMAPHORE), handle, access, attributes, NULL, init_semaphore,
                        (ULONG_PTR)initial, (ULONG_PTR)maximum);
}

NTSTATUS NTAPI NtOpenSemaphore(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    return open_named(ExSemaphoreType, handle, access, attributes);
}

NTSTATUS NTAPI NtReleaseSemaphore(HANDLE handle, LONG count, PLONG previous)
{
    PKSEMAPHORE s;
    NTSTATUS status = ObReferenceHandle(handle, ExSemaphoreType, KeGetPreviousMode(), (PVOID *)&s, NULL);
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

NTSTATUS NTAPI NtCreateTimer(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, TIMER_TYPE type)
{
    return create_named(ExTimerType, sizeof(ETIMER), handle, access, attributes, NULL, init_timer, type, 0);
}

NTSTATUS NTAPI NtOpenTimer(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes)
{
    return open_named(ExTimerType, handle, access, attributes);
}

NTSTATUS NTAPI NtSetTimer(HANDLE handle, PLARGE_INTEGER due, PVOID apc, PVOID apc_context, BOOLEAN resume,
                          LONG period, PBOOLEAN previous)
{
    ETIMER *t;
    LARGE_INTEGER d;
    if (!NT_SUCCESS(KiCopyFromUser(&d, due, sizeof(d))))
        return STATUS_ACCESS_VIOLATION;
    NTSTATUS status = ObReferenceHandle(handle, ExTimerType, KeGetPreviousMode(), (PVOID *)&t, NULL);
    if (!NT_SUCCESS(status))
        return status;
    BOOLEAN state = KeReadStateTimer(&t->Timer);
    KeSetTimerEx(&t->Timer, d, period, NULL);
    ObDereferenceObject(t);
    if (previous)
        KiCopyToUser(previous, &state, sizeof(state));
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtCancelTimer(HANDLE handle, PBOOLEAN current)
{
    ETIMER *t;
    NTSTATUS status = ObReferenceHandle(handle, ExTimerType, KeGetPreviousMode(), (PVOID *)&t, NULL);
    if (!NT_SUCCESS(status))
        return status;
    BOOLEAN state = KeReadStateTimer(&t->Timer);
    KeCancelTimer(&t->Timer);
    ObDereferenceObject(t);
    if (current)
        KiCopyToUser(current, &state, sizeof(state));
    return STATUS_SUCCESS;
}

/* waits on handles; the objects are referenced for the duration of the wait. */
NTSTATUS ExWaitHandles(ULONG count, const HANDLE *handles, WAIT_TYPE type, BOOLEAN alertable, PLARGE_INTEGER timeout,
                       KPROCESSOR_MODE mode)
{
    if (count == 0 || count > MAXIMUM_WAIT_OBJECTS)
        return STATUS_INVALID_PARAMETER_1;
    PVOID objects[MAXIMUM_WAIT_OBJECTS];
    PVOID waits[MAXIMUM_WAIT_OBJECTS];
    NTSTATUS status = STATUS_SUCCESS;
    ULONG referenced = 0;
    for (; referenced < count; referenced++) {
        status = ObReferenceHandle(handles[referenced], NULL, KeGetPreviousMode(), &objects[referenced], NULL);
        if (!NT_SUCCESS(status))
            break;
        waits[referenced] = ObGetWaitObject(objects[referenced]);
        if (!waits[referenced]) {
            ObDereferenceObject(objects[referenced]);
            status = STATUS_OBJECT_TYPE_MISMATCH;
            break;
        }
    }
    if (NT_SUCCESS(status)) {
        PKWAIT_BLOCK blocks = NULL;
        if (count > THREAD_WAIT_OBJECTS) {
            blocks = ExAllocatePool2(POOL_FLAG_NON_PAGED, count * sizeof(KWAIT_BLOCK), KI_TAG('E', 'x', 'W', 'b'));
            if (!blocks)
                status = STATUS_INSUFFICIENT_RESOURCES;
        }
        if (NT_SUCCESS(status))
            status = KiWaitObjects(count, waits, type, UserRequest, mode, alertable, timeout, blocks);
        if (blocks)
            ExFreePool(blocks);
    }
    for (ULONG i = 0; i < referenced; i++)
        ObDereferenceObject(objects[i]);
    return status;
}

NTSTATUS NTAPI NtWaitForSingleObject(HANDLE handle, BOOLEAN alertable, PLARGE_INTEGER timeout)
{
    LARGE_INTEGER t;
    if (timeout && !NT_SUCCESS(KiCopyFromUser(&t, timeout, sizeof(t))))
        return STATUS_ACCESS_VIOLATION;
    return ExWaitHandles(1, &handle, WaitAny, alertable, timeout ? &t : NULL, KeGetPreviousMode());
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
    return ExWaitHandles(count, copy, type == WaitAll ? WaitAll : WaitAny, alertable, timeout ? &t : NULL, UserMode);
}

NTSTATUS NTAPI NtSignalAndWaitForSingleObject(HANDLE signal, HANDLE wait, BOOLEAN alertable, PLARGE_INTEGER timeout)
{
    PVOID object;
    NTSTATUS status = ObReferenceHandle(signal, NULL, KeGetPreviousMode(), &object, NULL);
    if (!NT_SUCCESS(status))
        return status;
    POBJECT_TYPE type = ObGetObjectType(object);
    if (type == ExEventType)
        KeSetEvent(object, 1, TRUE);
    else if (type == ExSemaphoreType)
        KeReleaseSemaphore(object, 1, 1, TRUE);
    else if (type == ExMutantType)
        KeReleaseMutant(object, 1, FALSE, TRUE);
    ObDereferenceObject(object);
    return NtWaitForSingleObject(wait, alertable, timeout);
}
