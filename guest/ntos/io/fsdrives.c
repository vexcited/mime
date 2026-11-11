#include "ntos.h"

static PDEVICE_OBJECT g_devices[26];
static BOOLEAN g_linked[26];
static KEVENT g_wake;

static void link_name(ULONG letter, PWCHAR out)
{
    wcscpy(out, L"\\GLOBAL??\\X:");
    out[10] = (WCHAR)('A' + letter);
}

static void sync(void)
{
    MimeFsDrive *drives = ExAllocatePool2(POOL_FLAG_NON_PAGED, MIME_FS_MAX_DRIVES * sizeof(MimeFsDrive),
                                          KI_TAG('F', 's', 'D', 'r'));
    if (!drives)
        return;
    ULONG64 count = 0;
    if (HalHypercall(MIME_HC_FS_DRIVES, HalPhysicalOf(drives), MIME_FS_MAX_DRIVES, 0, 0, 0, 0, &count) !=
        MIME_HC_STATUS_OK) {
        ExFreePool(drives);
        return;
    }
    if (count > MIME_FS_MAX_DRIVES)
        count = MIME_FS_MAX_DRIVES;
    for (ULONG letter = MIME_FS_DRIVE_FIRST - 'A'; letter <= MIME_FS_DRIVE_LAST - 'A'; letter++) {
        const MimeFsDrive *drive = nullptr;
        for (ULONG i = 0; i < count && !drive; i++)
            if (drives[i].volume == MIME_FS_VOLUME_DRIVE + letter)
                drive = &drives[i];
        WCHAR link[16];
        link_name(letter, link);
        UNICODE_STRING l;
        RtlInitUnicodeString(&l, link);
        if (drive) {
            if (!NT_SUCCESS(MimeFsDriveVolume(drive, FALSE, &g_devices[letter])) || g_linked[letter])
                continue;
            WCHAR target[40];
            MimeFsVolumeDeviceName(drive->volume, target, RTL_NUMBER_OF(target));
            UNICODE_STRING t;
            RtlInitUnicodeString(&t, target);
            NTSTATUS status = IoCreateSymbolicLink(&l, &t);
            g_linked[letter] = NT_SUCCESS(status);
            KiLogPrintf(g_linked[letter] ? "io: host drive %c:" : "io: host drive %c: has a letter in use, %08x",
                        'A' + letter, status);
        } else if (g_devices[letter]) {
            MimeFsDrive gone = {.volume = MIME_FS_VOLUME_DRIVE + letter};
            MimeFsDriveVolume(&gone, TRUE, &g_devices[letter]);
            if (g_linked[letter]) {
                IoDeleteSymbolicLink(&l);
                g_linked[letter] = FALSE;
                KiLogPrintf("io: host drive %c: is gone", 'A' + letter);
            }
        }
    }
    ExFreePool(drives);
}

static void worker(PVOID context)
{
    (void)context;
    for (;;) {
        sync();
        KeWaitForSingleObject(&g_wake, Executive, KernelMode, FALSE, nullptr);
    }
}

void IopInitializeDrives(void)
{
    KeInitializeEvent(&g_wake, SynchronizationEvent, FALSE);
    sync();
}

/* a change between boot and the start of the thread is caught by its first look. */
void IopStartDrives(void)
{
    PsCreateSystemThread(nullptr, 0, nullptr, nullptr, nullptr, worker, nullptr);
}

void IopDrivesInterrupt(void)
{
    KeSetEvent(&g_wake, IO_NO_INCREMENT, FALSE);
}
