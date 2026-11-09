#include "../ntos.h"

/*
 * the routines drivers register to hear of processes, threads and images, called as windows calls them: a process
 * routine of the Ex kind may refuse a creation, an image routine hears of every image mapped into a process and of
 * every driver loaded.
 */

#define NOTIFY_SLOTS 64

typedef struct _PS_CREATE_NOTIFY_INFO {
    SIZE_T Size;
    ULONG Flags;
    HANDLE ParentProcessId;
    CLIENT_ID CreatingThreadId;
    PFILE_OBJECT FileObject;
    PCUNICODE_STRING ImageFileName;
    PCUNICODE_STRING CommandLine;
    NTSTATUS CreationStatus;
} PS_CREATE_NOTIFY_INFO, *PPS_CREATE_NOTIFY_INFO;

typedef struct _IMAGE_INFO {
    ULONG Properties;
    PVOID ImageBase;
    ULONG ImageSelector;
    SIZE_T ImageSize;
    ULONG ImageSectionNumber;
} IMAGE_INFO, *PIMAGE_INFO;

typedef struct _IMAGE_INFO_EX {
    SIZE_T Size;
    IMAGE_INFO ImageInfo;
    PFILE_OBJECT FileObject;
} IMAGE_INFO_EX;

typedef void(NTAPI *PCREATE_PROCESS_NOTIFY_ROUTINE_EX)(PEPROCESS process, HANDLE id, PPS_CREATE_NOTIFY_INFO info);
typedef void(NTAPI *PLOAD_IMAGE_NOTIFY_ROUTINE)(PUNICODE_STRING name, HANDLE process, PIMAGE_INFO info);

#define IMAGE_ADDRESSING_MODE_32BIT 3
#define IMAGE_INFO_SYSTEM_MODE (1u << 8)
#define IMAGE_INFO_MAPPED_TO_ALL_PIDS (1u << 9)
#define IMAGE_INFO_EXTENDED_INFO_PRESENT (1u << 10)

static PVOID g_process[NOTIFY_SLOTS];
static BOOLEAN g_process_ex[NOTIFY_SLOTS];
static PCREATE_THREAD_NOTIFY_ROUTINE g_thread[NOTIFY_SLOTS];
static PLOAD_IMAGE_NOTIFY_ROUTINE g_image[NOTIFY_SLOTS];
static KSPIN_LOCK g_lock;

static NTSTATUS set_routine(PVOID *slots, BOOLEAN *kinds, BOOLEAN kind, PVOID routine, BOOLEAN remove)
{
    NTSTATUS status = remove ? STATUS_PROCEDURE_NOT_FOUND : STATUS_INVALID_PARAMETER;
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    for (ULONG i = 0; i < NOTIFY_SLOTS; i++) {
        if (remove && slots[i] == routine && (!kinds || kinds[i] == kind)) {
            slots[i] = nullptr;
            status = STATUS_SUCCESS;
            break;
        }
        if (!remove && !slots[i]) {
            slots[i] = routine;
            if (kinds)
                kinds[i] = kind;
            status = STATUS_SUCCESS;
            break;
        }
    }
    KeReleaseSpinLock(&g_lock, irql);
    return status;
}

NTSTATUS NTAPI PsSetCreateProcessNotifyRoutine(PCREATE_PROCESS_NOTIFY_ROUTINE routine, BOOLEAN remove)
{
    return set_routine(g_process, g_process_ex, FALSE, (PVOID)routine, remove);
}

NTSTATUS NTAPI PsSetCreateProcessNotifyRoutineEx(PCREATE_PROCESS_NOTIFY_ROUTINE_EX routine, BOOLEAN remove)
{
    return set_routine(g_process, g_process_ex, TRUE, (PVOID)routine, remove);
}

NTSTATUS NTAPI PsSetCreateThreadNotifyRoutine(PCREATE_THREAD_NOTIFY_ROUTINE routine)
{
    return set_routine((PVOID *)g_thread, nullptr, FALSE, (PVOID)routine, FALSE);
}

NTSTATUS NTAPI PsRemoveCreateThreadNotifyRoutine(PCREATE_THREAD_NOTIFY_ROUTINE routine)
{
    return set_routine((PVOID *)g_thread, nullptr, FALSE, (PVOID)routine, TRUE);
}

NTSTATUS NTAPI PsSetLoadImageNotifyRoutine(PLOAD_IMAGE_NOTIFY_ROUTINE routine)
{
    return set_routine((PVOID *)g_image, nullptr, FALSE, (PVOID)routine, FALSE);
}

/* the flags only ask for notifications of every image, which is what every routine gets here. */
NTSTATUS NTAPI PsSetLoadImageNotifyRoutineEx(PLOAD_IMAGE_NOTIFY_ROUTINE routine, ULONG_PTR flags)
{
    if (flags & ~(ULONG_PTR)1)
        return STATUS_INVALID_PARAMETER;
    return set_routine((PVOID *)g_image, nullptr, FALSE, (PVOID)routine, FALSE);
}

NTSTATUS NTAPI PsRemoveLoadImageNotifyRoutine(PLOAD_IMAGE_NOTIFY_ROUTINE routine)
{
    return set_routine((PVOID *)g_image, nullptr, FALSE, (PVOID)routine, TRUE);
}

/* a creation the routines see with its image and command line; an Ex routine's refusal is the result. */
NTSTATUS PspNotifyProcessCreate(PEPROCESS process, PCUNICODE_STRING image, PCUNICODE_STRING command_line)
{
    PS_CREATE_NOTIFY_INFO info = {};
    info.Size = sizeof(info);
    info.ParentProcessId = process->InheritedFromUniqueProcessId;
    info.CreatingThreadId.UniqueProcess = PsGetCurrentProcessId();
    info.CreatingThreadId.UniqueThread = PsGetCurrentThreadId();
    /* like PspCallProcessNotifyRoutines, the image's file object, and the name it was opened by when there is one. */
    info.FileObject = process->ImageSection ? process->ImageSection->File : nullptr;
    info.Flags = image ? 1 : 0; /* FileOpenNameAvailable */
    info.ImageFileName = image;
    info.CommandLine = command_line;
    info.CreationStatus = STATUS_SUCCESS;
    for (ULONG i = 0; i < NOTIFY_SLOTS; i++) {
        PVOID routine = __atomic_load_n(&g_process[i], __ATOMIC_ACQUIRE);
        if (!routine)
            continue;
        if (g_process_ex[i])
            ((PCREATE_PROCESS_NOTIFY_ROUTINE_EX)routine)(process, process->UniqueProcessId, &info);
        else
            ((PCREATE_PROCESS_NOTIFY_ROUTINE)routine)(process->InheritedFromUniqueProcessId, process->UniqueProcessId,
                                                     TRUE);
    }
    return info.CreationStatus;
}

void PspNotifyProcess(PEPROCESS process, BOOLEAN create)
{
    if (create) {
        PspNotifyProcessCreate(process, nullptr, nullptr);
        return;
    }
    for (ULONG i = 0; i < NOTIFY_SLOTS; i++) {
        PVOID routine = __atomic_load_n(&g_process[i], __ATOMIC_ACQUIRE);
        if (!routine)
            continue;
        if (g_process_ex[i])
            ((PCREATE_PROCESS_NOTIFY_ROUTINE_EX)routine)(process, process->UniqueProcessId, nullptr);
        else
            ((PCREATE_PROCESS_NOTIFY_ROUTINE)routine)(process->InheritedFromUniqueProcessId, process->UniqueProcessId,
                                                     FALSE);
    }
}

void PspNotifyThread(PETHREAD thread, BOOLEAN create)
{
    for (ULONG i = 0; i < NOTIFY_SLOTS; i++) {
        PCREATE_THREAD_NOTIFY_ROUTINE routine = __atomic_load_n(&g_thread[i], __ATOMIC_ACQUIRE);
        if (routine)
            routine(thread->Cid.UniqueProcess, thread->Cid.UniqueThread, create);
    }
}

/*
 * an image mapped into a process, or a driver loaded when process is null. like PsCallImageNotifyRoutines, the routines
 * get the info inside an IMAGE_INFO_EX with the image's file object, which drivers reach through ExtendedInfoPresent,
 * and like MiCallImageNotify the addressing mode is IMAGE_ADDRESSING_MODE_32BIT whatever the machine.
 */
void PspNotifyImage(PCUNICODE_STRING name, PEPROCESS process, PVOID base, SIZE_T size, PFILE_OBJECT file)
{
    if (!name || !name->Length)
        return;
    IMAGE_INFO_EX info = {};
    info.Size = sizeof(info);
    info.ImageInfo.Properties = IMAGE_ADDRESSING_MODE_32BIT | IMAGE_INFO_EXTENDED_INFO_PRESENT |
                                (process ? 0 : IMAGE_INFO_SYSTEM_MODE);
    info.ImageInfo.ImageBase = base;
    info.ImageInfo.ImageSize = size;
    info.FileObject = file;
    UNICODE_STRING copy = *name;
    for (ULONG i = 0; i < NOTIFY_SLOTS; i++) {
        PLOAD_IMAGE_NOTIFY_ROUTINE routine = __atomic_load_n(&g_image[i], __ATOMIC_ACQUIRE);
        if (routine)
            routine(&copy, process ? process->UniqueProcessId : nullptr, &info.ImageInfo);
    }
}

BOOLEAN PspImageNotifyRegistered(void)
{
    for (ULONG i = 0; i < NOTIFY_SLOTS; i++)
        if (__atomic_load_n(&g_image[i], __ATOMIC_ACQUIRE))
            return TRUE;
    return FALSE;
}
