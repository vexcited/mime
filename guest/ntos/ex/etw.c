/*
 * event tracing: the registration of a provider, which every EventRegister of ntdll makes. read from ntdll of windows
 * 11 26300. a process first gives the kernel an event for its notifications with code 27, a handle in 32 bits, then
 * registers each provider with code 15 and a block of 160 bytes in and out: the guid of the provider, the kind of the
 * registration at 16, a sequence number at 20 and the callback at 32. the kernel answers with a handle of an
 * EtwRegistration object at 24, which ntdll closes at EventUnregister, and from 40 on with what enables the provider.
 * mime has no tracing sessions, so no provider is ever enabled and that part stays zero.
 */
#include "../ntos.h"

#define ETW_REGISTER_PROVIDER 15
#define ETW_SET_NOTIFICATION_EVENT 27
#define ETW_WOW64_CALLER 0x80000000u
#define ETW_REGISTRATION_SIZE 160
#define ETW_REGISTRATION_HANDLE 24
#define ETW_REGISTRATION_ALL_ACCESS 0x0804

typedef struct ETW_REGISTRATION {
    GUID Provider;
    ULONG Kind;
    ULONG64 Callback;
} ETW_REGISTRATION;

static POBJECT_TYPE g_registration_type;

void ExpInitializeEtw(void)
{
    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = ETW_REGISTRATION_ALL_ACCESS;
    ObSetGenericMapping(&init, 0, ETW_REGISTRATION_ALL_ACCESS, 0, ETW_REGISTRATION_ALL_ACCESS);
    g_registration_type = ObCreateObjectType(L"EtwRegistration", &init);
}

static NTSTATUS register_provider(PVOID in, ULONG in_len, PVOID out, ULONG out_len, PULONG returned)
{
    UCHAR block[ETW_REGISTRATION_SIZE];
    ULONG size = sizeof(block);
    if (returned && !NT_SUCCESS(KiCopyToUser(returned, &size, sizeof(size))))
        return STATUS_ACCESS_VIOLATION;
    if (in_len < sizeof(block))
        return STATUS_INVALID_PARAMETER;
    if (out_len < sizeof(block))
        return STATUS_BUFFER_TOO_SMALL;
    if (!NT_SUCCESS(KiCopyFromUser(block, in, sizeof(block))))
        return STATUS_ACCESS_VIOLATION;
    PVOID object;
    NTSTATUS status = ObCreateObject(g_registration_type, nullptr, KeGetPreviousMode(), sizeof(ETW_REGISTRATION), &object);
    if (!NT_SUCCESS(status))
        return status;
    ETW_REGISTRATION *registration = object;
    memcpy(&registration->Provider, block, sizeof(GUID));
    memcpy(&registration->Kind, block + 16, sizeof(ULONG));
    memcpy(&registration->Callback, block + 32, sizeof(ULONG64));
    HANDLE handle;
    status = ObInsertObjectNamed(object, nullptr, ETW_REGISTRATION_ALL_ACCESS, KeGetPreviousMode(), &handle);
    if (!NT_SUCCESS(status))
        return status;
    ULONG64 value = (ULONG64)(ULONG_PTR)handle;
    memset(block + ETW_REGISTRATION_HANDLE, 0, sizeof(block) - ETW_REGISTRATION_HANDLE);
    memcpy(block + ETW_REGISTRATION_HANDLE, &value, sizeof(value));
    memcpy(block + 32, &registration->Callback, sizeof(ULONG64));
    if (!NT_SUCCESS(KiCopyToUser(out, block, sizeof(block)))) {
        NtClose(handle);
        return STATUS_ACCESS_VIOLATION;
    }
    return STATUS_SUCCESS;
}

/* the event is set when a session has something to tell the process. no session exists, so it is only checked. */
static NTSTATUS set_notification_event(PVOID in, ULONG in_len)
{
    ULONG handle;
    if (in_len != sizeof(handle))
        return STATUS_INVALID_PARAMETER;
    if (!NT_SUCCESS(KiCopyFromUser(&handle, in, sizeof(handle))))
        return STATUS_ACCESS_VIOLATION;
    PVOID event;
    NTSTATUS status = ObReferenceObjectByHandle((HANDLE)(ULONG_PTR)handle, EVENT_MODIFY_STATE, ExEventType,
                                                KeGetPreviousMode(), &event, nullptr);
    if (NT_SUCCESS(status))
        ObDereferenceObject(event);
    return status;
}

NTSTATUS NTAPI NtTraceControl(ULONG code, PVOID in, ULONG in_len, PVOID out, ULONG out_len, PULONG returned)
{
    /* wow64 marks the calls of 32-bit programs with the high bit. the two served here have one layout. */
    switch (code & ~ETW_WOW64_CALLER) {
    case ETW_REGISTER_PROVIDER:
        return register_provider(in, in_len, out, out_len, returned);
    case ETW_SET_NOTIFICATION_EVENT:
        return set_notification_event(in, in_len);
    default:
        return STATUS_NOT_SUPPORTED;
    }
}
