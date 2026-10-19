/*
 * cameras: the host's cameras as devices of the guest, the way windows' usbvideo driver shows a usb camera. each is a
 * usb device with a video interface in the plug and play tree, whose interfaces in the camera categories are opened
 * with a reference string, "\global", and answer requests for the camera's name, modes and controls. a stream is
 * one block of memory shared with the host (mime/camera.h), mapped in the process that opened it; the host's frame
 * notifications set the event the process gave when it opened the stream. see docs/camera.md.
 */
#define MIME_KERNEL_PNP
#include "../ntos.h"
#include <mime/abi.h>
#include <mime/camera.h>
#include <mime/pnp.h>

#define MAX_STREAM_BYTES (160u << 20)
#define DEFAULT_SLOTS 4
#define MJPEG_BYTES_PER_PIXEL 1

static const GUID KSCATEGORY_CAPTURE = {0x65E8773D, 0x8F56, 0x11D0, {0xA3, 0xB9, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96}};
static const GUID KSCATEGORY_RENDER = {0x65E8773E, 0x8F56, 0x11D0, {0xA3, 0xB9, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96}};
static const GUID KSCATEGORY_VIDEO = {0x6994AD05, 0x93EF, 0x11D0, {0xA3, 0xCC, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96}};
static const GUID KSCATEGORY_RENDER_EXT = {0xCC7BFB41, 0xF175, 0x11D1, {0xA3, 0x92, 0x00, 0xE0, 0x29, 0x1F, 0x39, 0x59}};
static const GUID KSCATEGORY_VIDEO_CAMERA = {0xE5323777, 0xF976, 0x4F5B, {0x9B, 0x55, 0xB9, 0x46, 0x99, 0xC4, 0x6E, 0x44}};

typedef struct CAMERA {
    BOOLEAN Used;
    MimeCameraInfo Info;
    MimeCameraFormat *Formats;
    ULONG FormatCount;
    WCHAR Instance[96];  /* the video interface */
    WCHAR Parent[96];    /* the usb device */
    LIST_ENTRY Files;
} CAMERA;

typedef struct CAMERA_DEVICE {
    ULONG Slot;
} CAMERA_DEVICE;

typedef struct CAMERA_FILE {
    LIST_ENTRY Links;
    ULONG Slot;
    ULONG HostId;      /* the camera it was opened on, 0 once the camera went */
    ULONG Generation;
    /* the stream, if the file has opened one. */
    BOOLEAN HasStream;
    BOOLEAN Running;
    ULONG64 Stream;    /* the host's handle */
    PVOID Block;       /* the memory, in the kernel's view */
    ULONG64 Pa;
    SIZE_T Size;
    PVOID Section;
    PVOID User;
    PEPROCESS Owner;
    PKEVENT Event;
} CAMERA_FILE;

static CAMERA g_cameras[MIME_CAMERA_MAX];
static PDEVICE_OBJECT g_devices[MIME_CAMERA_MAX];
static PDRIVER_OBJECT g_driver;
static KMUTEX g_lock;
/* a page the host's answers to lists and controls land in, and the formats of up to the most a camera may have. */
static PVOID g_page;
static ULONG64 g_page_pa;
static PVOID g_formats;
static ULONG64 g_formats_pa;

#define FORMATS_PAGES ((MIME_CAMERA_MAX_FORMATS * sizeof(MimeCameraFormat) + 4095) / 4096)

static void lock(void)
{
    KeWaitForSingleObject(&g_lock, Executive, KernelMode, FALSE, nullptr);
}

static void unlock(void)
{
    KeReleaseMutex(&g_lock, FALSE);
}

static NTSTATUS from_host(ULONG64 status)
{
    switch (status) {
    case MIME_HC_STATUS_OK:
        return STATUS_SUCCESS;
    case MIME_HC_STATUS_INVALID:
        return STATUS_INVALID_PARAMETER;
    case MIME_HC_STATUS_NOT_FOUND:
        return STATUS_NO_SUCH_DEVICE;
    case MIME_HC_STATUS_ACCESS_DENIED:
        return STATUS_ACCESS_DENIED;
    case MIME_HC_STATUS_EXISTS:
        return STATUS_DEVICE_BUSY;
    case MIME_HC_STATUS_NO_MEMORY:
        return STATUS_INSUFFICIENT_RESOURCES;
    case MIME_HC_STATUS_UNSUPPORTED:
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_IO_DEVICE_ERROR;
    }
}

/* ------------------------------------------------------------ arrival and removal */

static ULONG hash_text(const char *text)
{
    ULONG h = 2166136261u;
    for (; *text; text++)
        h = (h ^ (UCHAR)*text) * 16777619u;
    return h;
}

static void widen(const char *utf8, WCHAR *out, ULONG chars)
{
    /* the names are what a person reads, so only what the system's fonts show: ascii as it is, the rest as ?. */
    ULONG n = 0;
    for (; *utf8 && n + 1 < chars; utf8++) {
        UCHAR c = (UCHAR)*utf8;
        if (c < 0x80)
            out[n++] = c;
        else if (c >= 0xC0)
            out[n++] = L'?';
    }
    out[n] = 0;
}

/* windows' ids for a usb camera: the usb device, and the video interface of its composite function. */
static void name_camera(CAMERA *c)
{
    ULONG tag = hash_text(c->Info.location) ^ ((ULONG)c->Info.vendor << 16 | c->Info.product);
    _snwprintf(c->Parent, RTL_NUMBER_OF(c->Parent), L"USB\\VID_%04X&PID_%04X\\5&%08X&0&%u", c->Info.vendor, c->Info.product,
               tag, (ULONG)(c - g_cameras) + 1);
    _snwprintf(c->Instance, RTL_NUMBER_OF(c->Instance), L"USB\\VID_%04X&PID_%04X&MI_%02X\\6&%08X&0&%04X", c->Info.vendor,
               c->Info.product, c->Info.interface_number & 0xFF, tag, c->Info.interface_number);
}

static void add_camera(CAMERA *c, const MimeCameraInfo *info)
{
    c->Info = *info;
    c->Used = TRUE;
    InitializeListHead(&c->Files);
    name_camera(c);

    /* the camera's formats, as many as the host lists. */
    c->FormatCount = 0;
    c->Formats = nullptr;
    ULONG64 count = 0;
    memset(g_formats, 0, FORMATS_PAGES * 4096);
    if (HalMimeHypercall(MIME_HC_CAMERA_FORMATS, info->id, g_formats_pa, MIME_CAMERA_MAX_FORMATS, 0, 0, 0, &count) ==
            MIME_HC_STATUS_OK &&
        count) {
        if (count > MIME_CAMERA_MAX_FORMATS)
            count = MIME_CAMERA_MAX_FORMATS;
        c->Formats = ExAllocatePool2(POOL_FLAG_NON_PAGED, count * sizeof(MimeCameraFormat), KI_TAG('C', 'a', 'm', 'F'));
        if (c->Formats) {
            memcpy(c->Formats, g_formats, count * sizeof(MimeCameraFormat));
            c->FormatCount = (ULONG)count;
        }
    }

    WCHAR name[128], location[96];
    widen(info->name, name, RTL_NUMBER_OF(name));
    WCHAR hardware_parent[160], hardware[200], compatible[160];
    ULONG n = (ULONG)_snwprintf(hardware_parent, RTL_NUMBER_OF(hardware_parent), L"USB\\VID_%04X&PID_%04X&REV_0100", info->vendor,
                                info->product);
    n++;
    _snwprintf(hardware_parent + n, RTL_NUMBER_OF(hardware_parent) - n, L"USB\\VID_%04X&PID_%04X", info->vendor, info->product);
    hardware_parent[n + wcslen(hardware_parent + n) + 1] = 0;
    n = (ULONG)_snwprintf(hardware, RTL_NUMBER_OF(hardware), L"USB\\VID_%04X&PID_%04X&REV_0100&MI_%02X", info->vendor,
                          info->product, info->interface_number & 0xFF) + 1;
    n += (ULONG)_snwprintf(hardware + n, RTL_NUMBER_OF(hardware) - n, L"USB\\VID_%04X&PID_%04X&MI_%02X", info->vendor,
                           info->product, info->interface_number & 0xFF) + 1;
    hardware[n] = 0;
    static const WCHAR classes[] = L"USB\\Class_0E&SubClass_03&Prot_00\0USB\\Class_0E&SubClass_03\0USB\\Class_0E\0";
    memcpy(compatible, classes, sizeof(classes));
    widen(info->location, location, RTL_NUMBER_OF(location));

    MIME_DEVICE_PROPERTIES usb = {
        .DeviceDesc = L"USB Composite Device",
        .HardwareIds = hardware_parent,
        .CompatibleIds = L"USB\\COMPOSITE\0",
        .Class = L"USB",
        .ClassGuid = L"{36fc9e60-c465-11cf-8056-444553540000}",
        .Manufacturer = L"(Standard USB Host Controller)",
        .Service = L"usbccgp",
        .Location = location,
    };
    MIME_DEVICE_PROPERTIES video = {
        .DeviceDesc = L"USB Video Device",
        .HardwareIds = hardware,
        .CompatibleIds = compatible,
        .Class = L"Camera",
        .ClassGuid = L"{ca3e7ab9-b4c3-4ae6-8251-579ef933890f}",
        .Manufacturer = L"Microsoft",
        .Service = L"usbvideo",
        .Location = location,
        .FriendlyName = name,
    };
    MimePnpAddDevice(c->Parent, nullptr, &usb);
    MimePnpAddDevice(c->Instance, c->Parent, &video);
    WCHAR device[40];
    _snwprintf(device, RTL_NUMBER_OF(device), L"\\Device\\MimeCamera%u", (ULONG)(c - g_cameras));
    /* the categories usbvideo registers its interface in, each opened as "global". */
    static const GUID *const categories[] = {&KSCATEGORY_CAPTURE, &KSCATEGORY_RENDER, &KSCATEGORY_VIDEO,
                                             &KSCATEGORY_RENDER_EXT, &KSCATEGORY_VIDEO_CAMERA};
    for (ULONG i = 0; i < RTL_NUMBER_OF(categories); i++)
        MimePnpAddInterfaceRef(c->Instance, categories[i], device, L"global");
}

static void signal_file(CAMERA_FILE *f)
{
    if (f->Event)
        KeSetEvent(f->Event, IO_NO_INCREMENT, FALSE);
}

static void remove_camera(CAMERA *c)
{
    MimePnpRemoveDevice(c->Instance);
    MimePnpRemoveDevice(c->Parent);
    /* the programs using it are woken to find the stream lost, which the host told it too. */
    for (PLIST_ENTRY e = c->Files.Flink; e != &c->Files; e = e->Flink) {
        CAMERA_FILE *f = CONTAINING_RECORD(e, CAMERA_FILE, Links);
        f->HostId = 0;
        signal_file(f);
    }
    ExFreePool(c->Formats);
    c->Formats = nullptr;
    c->Used = FALSE;
}

/* looks at the host's cameras again: the ones that came get their devices, the ones that went lose them. */
static void refresh(void)
{
    ULONG64 count = 0;
    memset(g_page, 0, 4096);
    if (HalMimeHypercall(MIME_HC_CAMERA_LIST, g_page_pa, 4096 / sizeof(MimeCameraInfo), 0, 0, 0, 0, &count) !=
        MIME_HC_STATUS_OK)
        return;
    MimeCameraInfo *list = g_page;
    MimeCameraInfo seen[MIME_CAMERA_MAX];
    ULONG n = count > MIME_CAMERA_MAX ? MIME_CAMERA_MAX : (ULONG)count;
    memcpy(seen, list, n * sizeof(seen[0]));
    for (ULONG i = 0; i < MIME_CAMERA_MAX; i++) {
        CAMERA *c = &g_cameras[i];
        if (!c->Used)
            continue;
        BOOLEAN there = FALSE;
        for (ULONG k = 0; k < n; k++)
            there |= seen[k].id == c->Info.id && seen[k].generation == c->Info.generation;
        if (!there)
            remove_camera(c);
    }
    for (ULONG k = 0; k < n; k++) {
        BOOLEAN known = FALSE;
        for (ULONG i = 0; i < MIME_CAMERA_MAX; i++)
            known |= g_cameras[i].Used && g_cameras[i].Info.id == seen[k].id &&
                     g_cameras[i].Info.generation == seen[k].generation;
        if (known)
            continue;
        for (ULONG i = 0; i < MIME_CAMERA_MAX; i++)
            if (!g_cameras[i].Used) {
                add_camera(&g_cameras[i], &seen[k]);
                break;
            }
    }
}

/* a frame is waiting in a stream: the event of the file that opened it is set. */
static void frame(ULONG stream)
{
    for (ULONG i = 0; i < MIME_CAMERA_MAX; i++) {
        CAMERA *c = &g_cameras[i];
        if (!c->Used)
            continue;
        for (PLIST_ENTRY e = c->Files.Flink; e != &c->Files; e = e->Flink) {
            CAMERA_FILE *f = CONTAINING_RECORD(e, CAMERA_FILE, Links);
            if (f->HasStream && f->Stream == stream) {
                /* cleared before the process looks, so that a frame after its look wakes it again. */
                __atomic_store_n(&((MimeCameraHeader *)f->Block)->wake, 0, __ATOMIC_RELEASE);
                signal_file(f);
                return;
            }
        }
    }
}

void NTAPI MimeCameraEvent(const MimeInputEvent *ev)
{
    if (!g_driver || ev->type != MIME_INPUT_CAMERA)
        return;
    lock();
    if (ev->flags == MIME_CAMERA_EVENT_CHANGED)
        refresh();
    else if (ev->flags == MIME_CAMERA_EVENT_FRAME)
        frame((ULONG)ev->x);
    unlock();
}

/* ------------------------------------------------------------ streams */

static void unmap_stream(CAMERA_FILE *f)
{
    if (f->User && f->Owner) {
        /* the process may be gone already, with its address space. */
        if (f->Owner->AddressSpace.Root)
            MiUnmapViewOfSectionEx(f->Owner, f->User, 0);
    }
    f->User = nullptr;
    if (f->Owner) {
        ObDereferenceObject(f->Owner);
        f->Owner = nullptr;
    }
}

/* the host stops using the block before it is given back. */
static void close_stream(CAMERA_FILE *f)
{
    if (!f->HasStream && !f->Block)
        return;
    if (f->HasStream)
        HalMimeHypercall(MIME_HC_CAMERA_CLOSE, f->Stream, 0, 0, 0, 0, 0, nullptr);
    f->HasStream = FALSE;
    f->Running = FALSE;
    unmap_stream(f);
    if (f->Section) {
        ObDereferenceObject(f->Section);
        f->Section = nullptr;
    }
    if (f->Block) {
        MmFreeContiguousPages(f->Block, f->Size);
        f->Block = nullptr;
    }
    if (f->Event) {
        ObDereferenceObject(f->Event);
        f->Event = nullptr;
    }
}

static ULONG slot_bytes_for(const MimeCameraOpen *req, ULONG out)
{
    ULONG frame = mime_camera_frame_bytes(out, req->format.width, req->format.height);
    if (!frame)
        frame = req->format.width * req->format.height * MJPEG_BYTES_PER_PIXEL + 65536;
    return (frame + 4095) & ~4095u;
}

static NTSTATUS open_stream(CAMERA_FILE *f, CAMERA *c, const MimeCameraOpen *req, MimeCameraOpened *opened)
{
    if (f->HasStream)
        return STATUS_INVALID_DEVICE_STATE;
    ULONG out = req->output_fourcc ? req->output_fourcc : req->format.fourcc;
    ULONG slots = req->slots ? req->slots : DEFAULT_SLOTS;
    if (slots < 2 || slots > MIME_CAMERA_MAX_SLOTS || !req->format.width || !req->format.height ||
        req->format.width > 16384 || req->format.height > 16384)
        return STATUS_INVALID_PARAMETER;
    ULONG slot_bytes = slot_bytes_for(req, out);
    SIZE_T size = MIME_CAMERA_HEADER_SIZE + (SIZE_T)slots * slot_bytes;
    if (size > MAX_STREAM_BYTES)
        return STATUS_INVALID_PARAMETER;
    PKEVENT event = nullptr;
    NTSTATUS status = ObReferenceHandle((HANDLE)(ULONG_PTR)req->event, ExEventType, UserMode, (PVOID *)&event, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    PVOID block = MmAllocateContiguousPages(size);
    if (!block) {
        ObDereferenceObject(event);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    memset(block, 0, MIME_CAMERA_HEADER_SIZE);
    MimeCameraHeader *h = block;
    h->source_fourcc = req->format.fourcc;
    h->width = req->format.width;
    h->height = req->format.height;
    h->fps_num = req->format.fps_num;
    h->fps_den = req->format.fps_den;
    h->output_fourcc = req->output_fourcc;
    h->slots = slots;
    h->slot_bytes = slot_bytes;
    h->data_offset = MIME_CAMERA_HEADER_SIZE;
    f->Block = block;
    f->Size = size;
    f->Pa = (ULONG64)MmGetPhysicalAddress(block).QuadPart;
    f->Event = event;
    status = MimeMmCreatePhysicalSection(f->Pa, size, &f->Section);
    if (NT_SUCCESS(status))
        status = MmMapSharedSectionUser(f->Section, PsGetCurrentProcess(), &f->User, TRUE);
    if (NT_SUCCESS(status)) {
        f->Owner = PsGetCurrentProcess();
        ObReferenceObject(f->Owner);
        ULONG64 stream = 0;
        status = from_host(HalMimeHypercall(MIME_HC_CAMERA_OPEN, c->Info.id, f->Pa, size, 0, 0, 0, &stream));
        if (NT_SUCCESS(status)) {
            f->Stream = stream;
            f->HasStream = TRUE;
            opened->base = (ULONG64)(ULONG_PTR)f->User;
            opened->size = (ULONG)size;
            opened->reserved = 0;
        }
    }
    if (!NT_SUCCESS(status))
        close_stream(f);
    return status;
}

/* ------------------------------------------------------------ requests */

static NTSTATUS complete_irp(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static BOOLEAN is_global(PCUNICODE_STRING name)
{
    static const WCHAR global[] = L"\\global";
    return name->Length == sizeof(global) - sizeof(WCHAR) && !_wcsnicmp(name->Buffer, global, RTL_NUMBER_OF(global) - 1);
}

static NTSTATUS NTAPI camera_create(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PFILE_OBJECT file = stack->FileObject;
    CAMERA_DEVICE *d = device->DeviceExtension;
    /* the interface is opened with its reference string, and with nothing else. */
    if (!is_global(&file->FileName))
        return complete_irp(irp, STATUS_OBJECT_NAME_NOT_FOUND, 0);
    CAMERA_FILE *f = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*f), KI_TAG('C', 'a', 'm', 'O'));
    if (!f)
        return complete_irp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    memset(f, 0, sizeof(*f));
    NTSTATUS status = STATUS_SUCCESS;
    lock();
    CAMERA *c = &g_cameras[d->Slot];
    if (!c->Used) {
        status = STATUS_DEVICE_NOT_CONNECTED;
    } else {
        f->Slot = d->Slot;
        f->HostId = c->Info.id;
        f->Generation = c->Info.generation;
        InsertTailList(&c->Files, &f->Links);
        file->FsContext = f;
    }
    unlock();
    if (!NT_SUCCESS(status))
        ExFreePool(f);
    return complete_irp(irp, status, 0);
}

/* the last handle went: the stream ends, and the host lets go of its memory. */
static NTSTATUS NTAPI camera_cleanup(PDEVICE_OBJECT device, PIRP irp)
{
    CAMERA_FILE *f = IoGetCurrentIrpStackLocation(irp)->FileObject->FsContext;
    if (f) {
        lock();
        if (f->Links.Flink)
            RemoveEntryList(&f->Links);
        f->Links.Flink = f->Links.Blink = nullptr;
        close_stream(f);
        unlock();
    }
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI camera_close(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    if (file->FsContext) {
        ExFreePool(file->FsContext);
        file->FsContext = nullptr;
    }
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI camera_control(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    CAMERA_FILE *f = stack->FileObject->FsContext;
    ULONG code = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG in_len = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG out_len = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PUCHAR buf = irp->AssociatedIrp.SystemBuffer;
    if (!f)
        return complete_irp(irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR information = 0;
    lock();
    CAMERA *c = &g_cameras[f->Slot];
    if (!c->Used || !f->HostId || c->Info.id != f->HostId || c->Info.generation != f->Generation) {
        unlock();
        return complete_irp(irp, STATUS_DEVICE_NOT_CONNECTED, 0);
    }
    switch (code) {
    case MIME_CAMERA_IOCTL(MIME_CAMERA_IOCTL_INFO):
        if (out_len < sizeof(MimeCameraInfo)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        memcpy(buf, &c->Info, sizeof(MimeCameraInfo));
        information = sizeof(MimeCameraInfo);
        break;
    case MIME_CAMERA_IOCTL(MIME_CAMERA_IOCTL_FORMATS): {
        if (out_len < offsetof(MimeCameraFormatList, format)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        MimeCameraFormatList *list = (MimeCameraFormatList *)buf;
        ULONG room = (out_len - (ULONG)offsetof(MimeCameraFormatList, format)) / sizeof(MimeCameraFormat);
        ULONG n = c->FormatCount < room ? c->FormatCount : room;
        list->count = c->FormatCount;
        list->returned = n;
        memcpy(list->format, c->Formats, n * sizeof(MimeCameraFormat));
        information = offsetof(MimeCameraFormatList, format) + n * sizeof(MimeCameraFormat);
        break;
    }
    case MIME_CAMERA_IOCTL(MIME_CAMERA_IOCTL_OPEN):
        if (in_len < sizeof(MimeCameraOpen) || out_len < sizeof(MimeCameraOpened)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        status = open_stream(f, c, (const MimeCameraOpen *)buf, (MimeCameraOpened *)buf);
        information = NT_SUCCESS(status) ? sizeof(MimeCameraOpened) : 0;
        break;
    case MIME_CAMERA_IOCTL(MIME_CAMERA_IOCTL_START):
        if (in_len < sizeof(ULONG)) {
            status = STATUS_BUFFER_TOO_SMALL;
        } else if (!f->HasStream) {
            status = STATUS_INVALID_DEVICE_STATE;
        } else {
            BOOLEAN run = *(ULONG *)buf != 0;
            status = from_host(HalMimeHypercall(MIME_HC_CAMERA_START, f->Stream, run, 0, 0, 0, 0, nullptr));
            if (NT_SUCCESS(status))
                f->Running = run;
        }
        break;
    case MIME_CAMERA_IOCTL(MIME_CAMERA_IOCTL_CLOSE):
        close_stream(f);
        break;
    case MIME_CAMERA_IOCTL(MIME_CAMERA_IOCTL_CONTROL): {
        if (in_len < sizeof(MimeCameraControl) || out_len < sizeof(MimeCameraControl)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        MimeCameraControl req;
        memcpy(&req, buf, sizeof(req));
        req.id = c->Info.id;
        memcpy(g_page, &req, sizeof(req));
        status = from_host(HalMimeHypercall(MIME_HC_CAMERA_CONTROL, g_page_pa, 0, 0, 0, 0, 0, nullptr));
        if (NT_SUCCESS(status)) {
            memcpy(buf, g_page, sizeof(req));
            /* the camera's own answer: a control it does not have is a request nothing can serve. */
            status = from_host((ULONG64)((MimeCameraControl *)g_page)->status);
            information = sizeof(MimeCameraControl);
            if (status == STATUS_NO_SUCH_DEVICE)
                status = STATUS_NOT_FOUND;
        }
        break;
    }
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
    }
    unlock();
    return complete_irp(irp, status, information);
}

static NTSTATUS NTAPI CameraDriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = camera_create;
    driver->MajorFunction[IRP_MJ_CLEANUP] = camera_cleanup;
    driver->MajorFunction[IRP_MJ_CLOSE] = camera_close;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL] = camera_control;
    for (ULONG i = 0; i < MIME_CAMERA_MAX; i++) {
        WCHAR buffer[32];
        _snwprintf(buffer, RTL_NUMBER_OF(buffer), L"\\Device\\MimeCamera%u", i);
        UNICODE_STRING name;
        RtlInitUnicodeString(&name, buffer);
        NTSTATUS status = IoCreateDevice(driver, sizeof(CAMERA_DEVICE), &name, MIME_CAMERA_DEVICE_TYPE, 0, FALSE,
                                         &g_devices[i]);
        if (!NT_SUCCESS(status))
            return status;
        ((CAMERA_DEVICE *)g_devices[i]->DeviceExtension)->Slot = i;
    }
    return STATUS_SUCCESS;
}

void IopInitializeCameras(void)
{
    KeInitializeMutex(&g_lock, 0);
    for (ULONG i = 0; i < MIME_CAMERA_MAX; i++)
        InitializeListHead(&g_cameras[i].Files);
    g_page = MmAllocateContiguousPages(4096);
    g_formats = MmAllocateContiguousPages(FORMATS_PAGES * 4096);
    if (!g_page || !g_formats) {
        KiLogPrintf("cameras have no memory");
        return;
    }
    g_page_pa = (ULONG64)MmGetPhysicalAddress(g_page).QuadPart;
    g_formats_pa = (ULONG64)MmGetPhysicalAddress(g_formats).QuadPart;
    PDRIVER_OBJECT driver;
    NTSTATUS status = IoCreateDriver(L"\\Driver\\MimeCamera", CameraDriverEntry, &driver);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("cameras failed to start: %08x", status);
        return;
    }
    g_driver = driver;
}

/* the cameras the host has already, once the registry their devices are written to is there. */
void IopStartCameras(void)
{
    if (!g_driver)
        return;
    lock();
    refresh();
    unlock();
}
