#include "ntos.h"

#define POWER_REQUEST_CREATE 43
#define POWER_REQUEST_ACTION 44
#define POWER_REQUEST_KINDS 9 /* POWER_REQUEST_TYPE_INTERNAL up to the full screen video one. */
#define POWER_REQUEST_INVALID_KINDS ((1u << 6) | (1u << 7)) /* PowerRequestInternalInvalid and Unknown. */

typedef struct POWER_REQUEST {
    LONG Counts[POWER_REQUEST_KINDS];
} POWER_REQUEST;

/* what kernelbase passes for an action: the request, the kind, set or clear, and a process for an execution one. */
typedef struct POWER_REQUEST_ACTION_INPUT {
    HANDLE Request;
    ULONG Kind;
    BOOLEAN Set;
    HANDLE Process;
} POWER_REQUEST_ACTION_INPUT;

static POBJECT_TYPE g_power_request_type;
static KSPIN_LOCK g_power_lock;

void ExpInitializePower(void)
{
    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = 0x1F0000;
    ObSetGenericMapping(&init, 0x20000, 0x20000, 0x20000, 0x1F0000);
    g_power_request_type = ObCreateObjectType(L"PowerRequest", &init);
    KeInitializeSpinLock(&g_power_lock);
}

static NTSTATUS create_request(PVOID input, ULONG input_length, PVOID output, ULONG output_length)
{
    if (!input || !input_length || output_length != sizeof(HANDLE) || !output)
        return STATUS_INVALID_PARAMETER;
    PVOID object;
    NTSTATUS status = ObCreateObject(g_power_request_type, nullptr, KeGetPreviousMode(), sizeof(POWER_REQUEST),
                                     &object);
    if (!NT_SUCCESS(status))
        return status;
    memset(object, 0, sizeof(POWER_REQUEST));
    HANDLE h;
    status = ObInsertObjectNamed(object, nullptr, 0, KeGetPreviousMode(), &h);
    if (!NT_SUCCESS(status))
        return status;
    status = KeGetPreviousMode() == UserMode ? KiCopyToUser(output, &h, sizeof(h))
                                             : (memcpy(output, &h, sizeof(h)), STATUS_SUCCESS);
    if (!NT_SUCCESS(status))
        ObCloseHandle(h, KeGetPreviousMode());
    return status;
}

static NTSTATUS request_action(PVOID input, ULONG input_length)
{
    POWER_REQUEST_ACTION_INPUT a;
    if (!input || input_length < sizeof(a))
        return STATUS_INVALID_PARAMETER;
    NTSTATUS status = KeGetPreviousMode() == UserMode ? KiCopyFromUser(&a, input, sizeof(a))
                                                      : (memcpy(&a, input, sizeof(a)), STATUS_SUCCESS);
    if (!NT_SUCCESS(status))
        return status;
    POWER_REQUEST *r;
    status = ObReferenceObjectByHandle(a.Request, 0, g_power_request_type, KeGetPreviousMode(), (PVOID *)&r,
                                       nullptr);
    if (!NT_SUCCESS(status))
        return status;
    if (a.Kind >= POWER_REQUEST_KINDS || (POWER_REQUEST_INVALID_KINDS & (1u << a.Kind))) {
        ObDereferenceObject(r);
        return STATUS_NOT_SUPPORTED;
    }
    KIRQL irql;
    KeAcquireSpinLock(&g_power_lock, &irql);
    if (a.Set)
        r->Counts[a.Kind]++;
    else if (r->Counts[a.Kind])
        r->Counts[a.Kind]--;
    else
        status = STATUS_NOT_SUPPORTED;
    KeReleaseSpinLock(&g_power_lock, irql);
    ObDereferenceObject(r);
    return status;
}

/*
 * power requests: what PowerCreateRequest and PowerSetRequest hand the kernel through NtPowerInformation. a request
 * counts the times each kind was set; mime keeps the machine awake anyway, so the counts only answer the program.
 * measured on windows 11: a request handle is a "PowerRequest" object granted no access, clearing a kind that is not
 * set and an unknown kind are not supported, a handle of another type with an invalid handle, and the
 * service refuses a missing input, an output that is not a handle and an unknown level with an invalid parameter.
 */
NTSTATUS NTAPI NtPowerInformation(ULONG level, PVOID input, ULONG input_length, PVOID output, ULONG output_length)
{
    switch (level) {
    case POWER_REQUEST_CREATE:
        return create_request(input, input_length, output, output_length);
    case POWER_REQUEST_ACTION:
        return request_action(input, input_length);
    default:
        return STATUS_INVALID_PARAMETER;
    }
}
