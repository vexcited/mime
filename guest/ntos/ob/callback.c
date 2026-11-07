#include "../ntos.h"

/*
 * ObRegisterCallbacks: drivers see each handle to a process or a thread being created or duplicated, before and
 * after, and may take access away from it before. as on windows, only an image built to have its integrity
 * checked may register, an altitude belongs to one registration, and the callbacks run by altitude.
 */

#define OB_OPERATION_HANDLE_CREATE 1
#define OB_OPERATION_HANDLE_DUPLICATE 2
#define OB_FLT_REGISTRATION_VERSION 0x0100
#define STATUS_FLT_INSTANCE_ALTITUDE_COLLISION ((NTSTATUS)0xC01C0011L)
#define IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY 0x0080

typedef struct {
    ACCESS_MASK DesiredAccess;
    ACCESS_MASK OriginalDesiredAccess;
    PVOID SourceProcess;
    PVOID TargetProcess;
} OB_PRE_PARAMETERS;

typedef struct {
    ULONG Operation;
    ULONG Flags;
    PVOID Object;
    POBJECT_TYPE ObjectType;
    PVOID CallContext;
    OB_PRE_PARAMETERS *Parameters;
} OB_PRE_OPERATION_INFORMATION;

typedef struct {
    ACCESS_MASK GrantedAccess;
} OB_POST_PARAMETERS;

typedef struct {
    ULONG Operation;
    ULONG Flags;
    PVOID Object;
    POBJECT_TYPE ObjectType;
    PVOID CallContext;
    NTSTATUS ReturnStatus;
    OB_POST_PARAMETERS *Parameters;
} OB_POST_OPERATION_INFORMATION;

typedef ULONG(NTAPI *OB_PRE_CALLBACK)(PVOID context, OB_PRE_OPERATION_INFORMATION *info);
typedef void(NTAPI *OB_POST_CALLBACK)(PVOID context, OB_POST_OPERATION_INFORMATION *info);

typedef struct {
    POBJECT_TYPE *ObjectType;
    ULONG Operations;
    OB_PRE_CALLBACK PreOperation;
    OB_POST_CALLBACK PostOperation;
} OB_OPERATION_REGISTRATION;

typedef struct {
    USHORT Version;
    USHORT OperationRegistrationCount;
    UNICODE_STRING Altitude;
    PVOID RegistrationContext;
    OB_OPERATION_REGISTRATION *OperationRegistration;
} OB_CALLBACK_REGISTRATION;

typedef struct OB_CALLBACK_ENTRY {
    struct OB_CALLBACK_ENTRY *Next;
    WCHAR Altitude[64];
    USHORT AltitudeLength;
    PVOID Context;
    ULONG Count;
    OB_OPERATION_REGISTRATION Operations[];
} OB_CALLBACK_ENTRY;

static OB_CALLBACK_ENTRY *g_callbacks;
static ERESOURCE g_resource;
static LONG g_ready;

static void ensure_ready(void)
{
    if (__atomic_load_n(&g_ready, __ATOMIC_ACQUIRE) == 2)
        return;
    LONG expected = 0;
    if (__atomic_compare_exchange_n(&g_ready, &expected, 1, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        ExInitializeResourceLite(&g_resource);
        __atomic_store_n(&g_ready, 2, __ATOMIC_RELEASE);
    }
    while (__atomic_load_n(&g_ready, __ATOMIC_ACQUIRE) != 2)
        __builtin_ia32_pause();
}

/* a callback must be code of an image whose integrity is checked. */
static BOOLEAN verified_code(PVOID routine)
{
    if (!routine)
        return TRUE;
    PKLDR_DATA_TABLE_ENTRY module = MiLookupModule((ULONG_PTR)routine);
    if (!module)
        return FALSE;
    PIMAGE_DOS_HEADER dos = module->DllBase;
    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)((PUCHAR)dos + dos->e_lfanew);
    return (nt->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY) != 0;
}

NTSTATUS NTAPI ObRegisterCallbacks(OB_CALLBACK_REGISTRATION *registration, PVOID *handle)
{
    if (!registration || !handle || (registration->Version & 0xFF00) != OB_FLT_REGISTRATION_VERSION ||
        !registration->OperationRegistrationCount || !registration->Altitude.Length ||
        registration->Altitude.Length > 63 * sizeof(WCHAR))
        return STATUS_INVALID_PARAMETER;
    ULONG count = registration->OperationRegistrationCount;
    for (ULONG i = 0; i < count; i++) {
        OB_OPERATION_REGISTRATION *op = &registration->OperationRegistration[i];
        if (!op->ObjectType || (*op->ObjectType != PsProcessTypeObject && *op->ObjectType != PsThreadTypeObject) ||
            !(op->Operations & (OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE)) ||
            (!op->PreOperation && !op->PostOperation))
            return STATUS_INVALID_PARAMETER;
        if (!verified_code((PVOID)op->PreOperation) || !verified_code((PVOID)op->PostOperation))
            return STATUS_ACCESS_DENIED;
    }
    OB_CALLBACK_ENTRY *entry = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                               sizeof(*entry) + count * sizeof(OB_OPERATION_REGISTRATION),
                                               KI_TAG('O', 'b', 'C', 'b'));
    if (!entry)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->Altitude, registration->Altitude.Buffer, registration->Altitude.Length);
    entry->AltitudeLength = registration->Altitude.Length;
    entry->Context = registration->RegistrationContext;
    entry->Count = count;
    memcpy(entry->Operations, registration->OperationRegistration, count * sizeof(OB_OPERATION_REGISTRATION));
    ensure_ready();
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&g_resource, TRUE);
    UNICODE_STRING mine = {entry->AltitudeLength, entry->AltitudeLength, entry->Altitude};
    OB_CALLBACK_ENTRY **at = &g_callbacks;
    for (; *at; at = &(*at)->Next) {
        UNICODE_STRING theirs = {(*at)->AltitudeLength, (*at)->AltitudeLength, (*at)->Altitude};
        LONG order = RtlCompareUnicodeString(&mine, &theirs, TRUE);
        if (!order) {
            ExReleaseResourceLite(&g_resource);
            KeLeaveCriticalRegion();
            ExFreePool(entry);
            return STATUS_FLT_INSTANCE_ALTITUDE_COLLISION;
        }
        if (order > 0)
            break;
    }
    entry->Next = *at;
    __atomic_store_n(at, entry, __ATOMIC_RELEASE);
    ExReleaseResourceLite(&g_resource);
    KeLeaveCriticalRegion();
    KiLogPrintf("object callbacks registered at altitude %wZ", &mine);
    *handle = entry;
    return STATUS_SUCCESS;
}

void NTAPI ObUnRegisterCallbacks(PVOID handle)
{
    ensure_ready();
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&g_resource, TRUE);
    for (OB_CALLBACK_ENTRY **at = &g_callbacks; *at; at = &(*at)->Next) {
        if (*at == handle) {
            *at = (*at)->Next;
            break;
        }
    }
    ExReleaseResourceLite(&g_resource);
    KeLeaveCriticalRegion();
    ExFreePool(handle);
}

USHORT NTAPI ObGetFilterVersion(void)
{
    return OB_FLT_REGISTRATION_VERSION;
}

BOOLEAN ObpObjectCallbacksPresent(PVOID object)
{
    if (!__atomic_load_n(&g_callbacks, __ATOMIC_ACQUIRE))
        return FALSE;
    POBJECT_TYPE type = OBJECT_TO_HEADER(object)->Type;
    return type == PsProcessTypeObject || type == PsThreadTypeObject;
}

/* the access a handle gets after the pre operation callbacks took what they would from it. */
ACCESS_MASK ObpCallPreCallbacks(PVOID object, ACCESS_MASK access, BOOLEAN duplicate, BOOLEAN kernel_handle,
                                PEPROCESS source, PEPROCESS target)
{
    POBJECT_TYPE type = OBJECT_TO_HEADER(object)->Type;
    ULONG operation = duplicate ? OB_OPERATION_HANDLE_DUPLICATE : OB_OPERATION_HANDLE_CREATE;
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&g_resource, TRUE);
    for (OB_CALLBACK_ENTRY *e = g_callbacks; e; e = e->Next) {
        for (ULONG i = 0; i < e->Count; i++) {
            OB_OPERATION_REGISTRATION *op = &e->Operations[i];
            if (*op->ObjectType != type || !(op->Operations & operation) || !op->PreOperation)
                continue;
            OB_PRE_PARAMETERS parameters = {access, access, source, target};
            OB_PRE_OPERATION_INFORMATION info = {operation, kernel_handle ? 1u : 0u, object, type, nullptr, &parameters};
            op->PreOperation(e->Context, &info);
            /* a callback only takes rights away. */
            access &= parameters.DesiredAccess | ~parameters.OriginalDesiredAccess;
        }
    }
    ExReleaseResourceLite(&g_resource);
    KeLeaveCriticalRegion();
    return access;
}

void ObpCallPostCallbacks(PVOID object, ACCESS_MASK granted, NTSTATUS result, BOOLEAN duplicate, BOOLEAN kernel_handle)
{
    POBJECT_TYPE type = OBJECT_TO_HEADER(object)->Type;
    ULONG operation = duplicate ? OB_OPERATION_HANDLE_DUPLICATE : OB_OPERATION_HANDLE_CREATE;
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&g_resource, TRUE);
    for (OB_CALLBACK_ENTRY *e = g_callbacks; e; e = e->Next) {
        for (ULONG i = 0; i < e->Count; i++) {
            OB_OPERATION_REGISTRATION *op = &e->Operations[i];
            if (*op->ObjectType != type || !(op->Operations & operation) || !op->PostOperation)
                continue;
            OB_POST_PARAMETERS parameters = {granted};
            OB_POST_OPERATION_INFORMATION info = {operation, kernel_handle ? 1u : 0u, object, type, nullptr, result,
                                                  &parameters};
            op->PostOperation(e->Context, &info);
        }
    }
    ExReleaseResourceLite(&g_resource);
    KeLeaveCriticalRegion();
}
