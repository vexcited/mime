/*
 * game controllers: the host's controllers, each an xbox 360 controller
 * as windows' xusb22 driver presents one. a usb device with an xusb
 * interface that xinput talks to, its hid child, and the child's hid
 * game controller collection that directinput, raw input and hid clients
 * read. see docs/input-devices.md and docs/hid.md.
 */
#define MIME_KERNEL_PNP
#include "../ntos.h"
#include <mime/pnp.h>

#define MAX_PADS 16
#define REPORT_SIZE 15
#define MAX_INPUT_BUFFERS 512
#define DEFAULT_INPUT_BUFFERS 32
#define XUSB_VERSION 0x0103
#define XUSB_STATE_SIZE 29
#define XUSB_INFO_EX_SIZE 949
#define PAD_REVISION 0x0114

#define FILE_DEVICE_BUS_EXTENDER 0x2A

#define XUSB_GUIDE 0x0400

enum { KIND_HID, KIND_XUSB };

enum {
    IOCTL_HID_GET_DRIVER_CONFIG = 0xB0190,
    IOCTL_HID_SET_FEATURE = 0xB0191,
    IOCTL_HID_GET_FEATURE = 0xB0192,
    IOCTL_HID_GET_COLLECTION_DESCRIPTOR = 0xB0193,
    IOCTL_HID_SET_DRIVER_CONFIG = 0xB0194,
    IOCTL_HID_SET_OUTPUT_REPORT = 0xB0195,
    IOCTL_HID_FLUSH_QUEUE = 0xB0197,
    IOCTL_HID_GET_POLL_FREQUENCY_MSEC = 0xB0198,
    IOCTL_GET_PHYSICAL_DESCRIPTOR = 0xB019A,
    IOCTL_HID_SET_POLL_FREQUENCY_MSEC = 0xB019C,
    IOCTL_HID_GET_HARDWARE_ID = 0xB019E,
    IOCTL_GET_NUM_DEVICE_INPUT_BUFFERS = 0xB01A0,
    IOCTL_HID_GET_INPUT_REPORT = 0xB01A2,
    IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS = 0xB01A4,
    IOCTL_HID_GET_OUTPUT_REPORT = 0xB01A6,
    IOCTL_HID_GET_COLLECTION_INFORMATION = 0xB01A8,
    IOCTL_HID_ENABLE_WAKE_ON_SX = 0xB01AC,
    IOCTL_HID_SET_S0_IDLE_TIMEOUT = 0xB01B0,
    IOCTL_HID_GET_MANUFACTURER_STRING = 0xB01BA,
    IOCTL_HID_GET_PRODUCT_STRING = 0xB01BE,
    IOCTL_HID_GET_SERIALNUMBER_STRING = 0xB01C2,
    IOCTL_HID_GET_INDEXED_STRING = 0xB01E2,
    IOCTL_HID_GET_MS_GENRE_DESCRIPTOR = 0xB01E6,
    IOCTL_HID_ENABLE_SECURE_READ = 0xB020B,
    IOCTL_HID_DISABLE_SECURE_READ = 0xB020F,
    IOCTL_HID_DEVICERESET_NOTIFICATION = 0xB0233,
    IOCTL_HID_POWER_REFERENCE = 0xB025B,
    IOCTL_HID_POWER_RELEASE = 0xB025F,
    IOCTL_HID_WRITE_MDL = 0xB0263,
    IOCTL_HID_PRESENCE_NOTIFICATION = 0xB0267,
    IOCTL_HID_SUPPRESS_INPUT = 0xB0268,
    IOCTL_GET_SYS_BUTTON_CAPS = 0x294140,
    IOCTL_GET_SYS_BUTTON_EVENT = 0x294144,
};

enum {
    IOCTL_XUSB_GET_INFORMATION = 0x80006000,
    IOCTL_XUSB_GET_CAPABILITIES = 0x8000E004,
    IOCTL_XUSB_GET_LED_STATE = 0x8000E008,
    IOCTL_XUSB_GET_STATE = 0x8000E00C,
    IOCTL_XUSB_SET_STATE = 0x8000A010,
    IOCTL_XUSB_WAIT_GUIDE_BUTTON = 0x8000E014,
    IOCTL_XUSB_GET_BATTERY_INFORMATION = 0x8000E018,
    IOCTL_XUSB_POWER_DOWN = 0x8000A01C,
    IOCTL_XUSB_GET_AUDIO_INFORMATION = 0x8000E020,
    IOCTL_XUSB_WAIT_FOR_INPUT = 0x8000E3AC,
    IOCTL_XUSB_GET_INFORMATION_EX = 0x8000E3FC,
};

static const GUID GUID_DEVINTERFACE_HID = {0x4D1E55B2, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
static const GUID GUID_DEVINTERFACE_XUSB = {0xEC87F1E3, 0xC13B, 0x4100, {0xB5, 0xF7, 0x8B, 0x84, 0xD5, 0x42, 0x60, 0xCB}};

/* xusb22's report descriptor for a gamepad. */
static const UCHAR g_descriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0xA1, 0x00, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF,
    0xFF, 0x35, 0x00, 0x46, 0xFF, 0xFF, 0x95, 0x02, 0x75, 0x10, 0x81, 0x02, 0xC0, 0xA1, 0x00, 0x09,
    0x33, 0x09, 0x34, 0x15, 0x00, 0x26, 0xFF, 0xFF, 0x35, 0x00, 0x46, 0xFF, 0xFF, 0x95, 0x02, 0x75,
    0x10, 0x81, 0x02, 0xC0, 0xA1, 0x00, 0x09, 0x32, 0x15, 0x00, 0x26, 0xFF, 0xFF, 0x35, 0x00, 0x46,
    0xFF, 0xFF, 0x95, 0x01, 0x75, 0x10, 0x81, 0x02, 0xC0, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0A, 0x95,
    0x0A, 0x75, 0x01, 0x81, 0x02, 0x05, 0x01, 0x09, 0x39, 0x15, 0x01, 0x25, 0x08, 0x35, 0x00, 0x46,
    0x3B, 0x10, 0x66, 0x0E, 0x00, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x75, 0x02, 0x95, 0x01, 0x81,
    0x03, 0x75, 0x08, 0x95, 0x02, 0x81, 0x03, 0xC0,
};

/* the device descriptor of a wired xbox 360 controller. */
static const UCHAR g_usb_descriptor[18] = {
    0x12, 0x01, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0x08, 0x5E, 0x04, 0x8E, 0x02, 0x14, 0x01, 0x01, 0x02, 0x03, 0x01,
};

static const WCHAR g_product[] = L"Controller (XBOX 360 For Windows)";
static const WCHAR g_hid_hardware_id[] = L"USB\\VID_045E&PID_028E&IG_00\0";

/* xusb22's hat switch from the four d-pad bits, oddities included. */
static const UCHAR g_hat[16] = {0, 1, 5, 0, 7, 8, 6, 1, 3, 2, 4, 3, 6, 1, 5, 0};
/* the xinput button of each hid button. */
static const USHORT g_hid_buttons[10] = {0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0x0020, 0x0010, 0x0040, 0x0080};

typedef struct PAD {
    BOOLEAN Used;
    ULONG HostId;
    ULONG Slot;
    ULONG Generation;
    USHORT Buttons;
    SHORT Axes[MIME_PAD_AXES];
    ULONG Packet;
    UCHAR Led;
    LONG Vport;
    /* the share rules of the hid collection. */
    ULONG Opens, Readers, Writers, NoShareRead, NoShareWrite, Exclusive;
    LIST_ENTRY Files;
    WCHAR UsbInstance[64];
    WCHAR GameInstance[64];
    WCHAR HidInstance[64];
} PAD;

typedef struct PAD_DEVICE {
    ULONG Kind;
    ULONG Slot;
} PAD_DEVICE;

typedef struct PAD_FILE {
    LIST_ENTRY Links;
    ULONG Kind;
    ULONG Slot;
    ULONG Generation;
    BOOLEAN Read, Write;
    USHORT Share;
    ULONG Limit;
    ULONG Head, Count;
    LONG PowerRefs;
    LIST_ENTRY Reads;  /* hid reads waiting for a report */
    LIST_ENTRY Waits;  /* notifications nothing ever completes */
    LIST_ENTRY Guide;  /* xusb waits for the guide button */
    LIST_ENTRY Input;  /* xusb waits for the next state */
    PUCHAR Ring;       /* MAX_INPUT_BUFFERS reports */
} PAD_FILE;

static PAD g_pads[MAX_PADS];
static KMUTEX g_lock;
static PDRIVER_OBJECT g_driver;
static PDEVICE_OBJECT g_hid[MAX_PADS], g_xusb[MAX_PADS];
static ULONG g_vports;
static ULONG g_connections;
static PUCHAR g_preparsed;
static ULONG g_preparsed_length;

static void lock(void)
{
    KeWaitForSingleObject(&g_lock, Executive, KernelMode, FALSE, nullptr);
}

static void unlock(void)
{
    KeReleaseMutex(&g_lock, FALSE);
}

static void put16(PUCHAR p, ULONG v)
{
    p[0] = (UCHAR)v;
    p[1] = (UCHAR)(v >> 8);
}

static void put32(PUCHAR p, ULONG v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

static USHORT get16(const UCHAR *p)
{
    return (USHORT)(p[0] | p[1] << 8);
}

static PAD *find(ULONG host)
{
    for (ULONG i = 0; i < MAX_PADS; i++)
        if (g_pads[i].Used && g_pads[i].HostId == host)
            return &g_pads[i];
    return nullptr;
}

/* the pad a file was opened on, unless it went away since. */
static PAD *live(PAD_FILE *f)
{
    PAD *p = &g_pads[f->Slot];
    return p->Used && p->Generation == f->Generation ? p : nullptr;
}

static UCHAR trigger(SHORT v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (UCHAR)v;
}

/* xusb22's mapping of a state to the hid report, with the id byte hidclass puts first. */
static void build_report(const PAD *p, PUCHAR r)
{
    memset(r, 0, REPORT_SIZE);
    put16(r + 1, (USHORT)(p->Axes[MIME_PAD_LX] + 0x8000));
    put16(r + 3, (USHORT)(0x7FFF - p->Axes[MIME_PAD_LY]));
    put16(r + 5, (USHORT)(p->Axes[MIME_PAD_RX] + 0x8000));
    put16(r + 7, (USHORT)(0x7FFF - p->Axes[MIME_PAD_RY]));
    put16(r + 9, (USHORT)(0x8000 + 128 * trigger(p->Axes[MIME_PAD_LT]) - 128 * trigger(p->Axes[MIME_PAD_RT])));
    ULONG bits = 0;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_hid_buttons); i++)
        if (p->Buttons & g_hid_buttons[i])
            bits |= 1u << i;
    bits |= (ULONG)g_hat[p->Buttons & 15] << 10;
    r[11] = (UCHAR)bits;
    r[12] = (UCHAR)(bits >> 8);
}

/* GET_GAMEPAD_STATE's newer reply. */
static void build_state(const PAD *p, PUCHAR out)
{
    memset(out, 0, XUSB_STATE_SIZE);
    put16(out, XUSB_VERSION);
    out[2] = 1;
    out[4] = 0xFF;
    put32(out + 5, p->Packet);
    out[10] = 0x14;
    put16(out + 11, p->Buttons & ~0x0800);
    out[13] = trigger(p->Axes[MIME_PAD_LT]);
    out[14] = trigger(p->Axes[MIME_PAD_RT]);
    for (ULONG i = 0; i < 4; i++)
        put16(out + 15 + 2 * i, (USHORT)p->Axes[MIME_PAD_LX + i]);
}

static void build_old_state(const PAD *p, PUCHAR out)
{
    memset(out, 0, 20);
    out[0] = 1;
    out[2] = 0xFF;
    put32(out + 3, p->Packet);
    put16(out + 8, p->Buttons & 0xF3FF);
    out[10] = trigger(p->Axes[MIME_PAD_LT]);
    out[11] = trigger(p->Axes[MIME_PAD_RT]);
    for (ULONG i = 0; i < 4; i++)
        put16(out + 12 + 2 * i, (USHORT)p->Axes[MIME_PAD_LX + i]);
}

static void complete_list(LIST_ENTRY *list, NTSTATUS status)
{
    while (!IsListEmpty(list)) {
        IO_USER_REQUEST *r = CONTAINING_RECORD(RemoveHeadList(list), IO_USER_REQUEST, Links);
        IopCompleteUserRequest(r, status, 0);
    }
}

static void complete_file(PAD_FILE *f, NTSTATUS status)
{
    complete_list(&f->Reads, status);
    complete_list(&f->Waits, status);
    complete_list(&f->Guide, status);
    complete_list(&f->Input, status);
}

static void complete_state(LIST_ENTRY *list, const PAD *p)
{
    UCHAR out[XUSB_STATE_SIZE];
    build_state(p, out);
    while (!IsListEmpty(list)) {
        IO_USER_REQUEST *r = CONTAINING_RECORD(RemoveHeadList(list), IO_USER_REQUEST, Links);
        IopCopyToRequester(r, r->Buffer, out, sizeof(out));
        IopCompleteUserRequest(r, STATUS_SUCCESS, sizeof(out));
    }
}

/* a report goes to the oldest waiting read, or into the handle's queue, whose oldest report a full queue drops. */
static void deliver(PAD_FILE *f, const UCHAR *report)
{
    if (!IsListEmpty(&f->Reads)) {
        IO_USER_REQUEST *r = CONTAINING_RECORD(RemoveHeadList(&f->Reads), IO_USER_REQUEST, Links);
        NTSTATUS status = IopCopyToRequester(r, r->Buffer, report, REPORT_SIZE);
        IopCompleteUserRequest(r, status, NT_SUCCESS(status) ? REPORT_SIZE : 0);
        return;
    }
    if (f->Count >= f->Limit) {
        f->Head = (f->Head + 1) % MAX_INPUT_BUFFERS;
        f->Count--;
    }
    memcpy(f->Ring + (f->Head + f->Count) % MAX_INPUT_BUFFERS * REPORT_SIZE, report, REPORT_SIZE);
    f->Count++;
}

/* ------------------------------------------------------------ arrival and removal */

/* windows' instance ids for a controller, its hid child and the child's collection, the prefixes guessed. */
static void name_pad(PAD *p)
{
    ULONG tag = 0x2A3F1B0C ^ (g_connections * 0x9E3779B1);
    _snwprintf(p->UsbInstance, RTL_NUMBER_OF(p->UsbInstance), L"USB\\VID_045E&PID_028E\\5&%08X&0&%u", tag, p->Slot + 1);
    _snwprintf(p->GameInstance, RTL_NUMBER_OF(p->GameInstance), L"USB\\VID_045E&PID_028E&IG_00\\6&%08X&0&00",
               tag + 1);
    _snwprintf(p->HidInstance, RTL_NUMBER_OF(p->HidInstance), L"HID\\VID_045E&PID_028E&IG_00\\7&%08X&0&0000",
               tag + 2);
}

static void arrived(const MimeInputEvent *ev)
{
    if (find(ev->pad))
        return;
    PAD *p = nullptr;
    for (ULONG i = 0; i < MAX_PADS && !p; i++)
        if (!g_pads[i].Used)
            p = &g_pads[i];
    if (!p)
        return;
    p->Used = TRUE;
    p->HostId = ev->pad;
    p->Generation++;
    p->Buttons = 0;
    memset(p->Axes, 0, sizeof(p->Axes));
    p->Packet = 0;
    p->Opens = p->Readers = p->Writers = p->NoShareRead = p->NoShareWrite = p->Exclusive = 0;
    /* xusb22 lights the first free of four system wide ports. */
    p->Vport = -1;
    p->Led = 0;
    for (LONG v = 0; v < 4; v++)
        if (!(g_vports & 1u << v)) {
            g_vports |= 1u << v;
            p->Vport = v;
            p->Led = (UCHAR)(2 + v);
            break;
        }
    g_connections++;
    name_pad(p);

    WCHAR location[32];
    _snwprintf(location, RTL_NUMBER_OF(location), L"XBOX_360_DEVICE_%02X:00", p->Slot);
    static const MIME_DEVICE_PROPERTIES USB = {
        .DeviceDesc = L"Xbox 360 Controller for Windows",
        .HardwareIds = L"USB\\VID_045E&PID_028E&REV_0114\0USB\\VID_045E&PID_028E\0",
        .CompatibleIds = L"USB\\Class_FF&SubClass_FF&Prot_FF\0USB\\Class_FF&SubClass_FF\0USB\\Class_FF\0",
        .Class = L"XnaComposite",
        .ClassGuid = L"{d61ca365-5af4-4486-998b-9db4734c6ca3}",
        .Manufacturer = L"Microsoft",
        .Service = L"xusb22",
    };
    MIME_DEVICE_PROPERTIES game = {
        .DeviceDesc = g_product,
        .HardwareIds = L"USB\\VID_045E&PID_028E&IG_00\0",
        .CompatibleIds = L"USB\\Class_03&Subclass_00&Prot_00\0USB\\Class_03&Subclass_00\0USB\\Class_03\0",
        .Class = L"HIDClass",
        .ClassGuid = L"{745a17a0-74d3-11d0-b6fe-00a0c90f57da}",
        .Manufacturer = L"(Standard system devices)",
        .Service = L"HidUsb",
        .Location = location,
    };
    static const MIME_DEVICE_PROPERTIES HID = {
        .DeviceDesc = L"HID-compliant game controller",
        .HardwareIds = L"HID\\VID_045E&PID_028E&IG_00\0HID\\VID_045E&UP:0001_U:0005\0HID_DEVICE_SYSTEM_GAME\0"
                       L"HID_DEVICE_UP:0001_U:0005\0HID_DEVICE\0",
        .Class = L"HIDClass",
        .ClassGuid = L"{745a17a0-74d3-11d0-b6fe-00a0c90f57da}",
        .Manufacturer = L"(Standard system devices)",
    };
    WCHAR device[32];
    MimePnpAddDevice(p->UsbInstance, nullptr, &USB);
    _snwprintf(device, RTL_NUMBER_OF(device), L"\\Device\\MimePadXusb%u", p->Slot);
    MimePnpAddInterface(p->UsbInstance, &GUID_DEVINTERFACE_XUSB, device);
    MimePnpAddDevice(p->GameInstance, p->UsbInstance, &game);
    MimePnpAddDevice(p->HidInstance, p->GameInstance, &HID);
    _snwprintf(device, RTL_NUMBER_OF(device), L"\\Device\\MimePadHid%u", p->Slot);
    MimePnpAddInterface(p->HidInstance, &GUID_DEVINTERFACE_HID, device);
}

static void removed(const MimeInputEvent *ev)
{
    PAD *p = find(ev->pad);
    if (!p)
        return;
    MimePnpRemoveDevice(p->HidInstance);
    MimePnpRemoveDevice(p->GameInstance);
    MimePnpRemoveDevice(p->UsbInstance);
    for (PLIST_ENTRY e = p->Files.Flink; e != &p->Files; e = e->Flink) {
        PAD_FILE *f = CONTAINING_RECORD(e, PAD_FILE, Links);
        if (f->Generation == p->Generation)
            complete_file(f, STATUS_DEVICE_NOT_CONNECTED);
    }
    if (p->Vport >= 0)
        g_vports &= ~(1u << p->Vport);
    p->Used = FALSE;
}

static void state(const MimeInputEvent *ev)
{
    PAD *p = find(ev->pad);
    if (!p)
        return;
    if (p->Buttons == ev->pad_buttons && !memcmp(p->Axes, ev->pad_axes, sizeof(p->Axes)))
        return;
    BOOLEAN guide = !(p->Buttons & XUSB_GUIDE) && (ev->pad_buttons & XUSB_GUIDE);
    p->Packet++;
    p->Buttons = ev->pad_buttons;
    memcpy(p->Axes, ev->pad_axes, sizeof(p->Axes));
    UCHAR report[REPORT_SIZE];
    build_report(p, report);
    for (PLIST_ENTRY e = p->Files.Flink; e != &p->Files; e = e->Flink) {
        PAD_FILE *f = CONTAINING_RECORD(e, PAD_FILE, Links);
        if (f->Generation != p->Generation)
            continue;
        if (f->Kind == KIND_HID) {
            deliver(f, report);
            continue;
        }
        /* windows 11 never answers a guide wait; the press answers it here, as the older drivers did. */
        if (guide)
            complete_state(&f->Guide, p);
        complete_state(&f->Input, p);
    }
}

void NTAPI MimePadEvent(const MimeInputEvent *ev)
{
    if (!g_driver)
        return;
    lock();
    switch (ev->type) {
    case MIME_INPUT_PAD_ADDED:
        arrived(ev);
        break;
    case MIME_INPUT_PAD_REMOVED:
        removed(ev);
        break;
    case MIME_INPUT_PAD_STATE:
        state(ev);
        break;
    }
    unlock();
}

/* what raw input tells of a controller's hid collection. */
BOOLEAN NTAPI MimePadHidInfo(ULONG host, MIME_PAD_HID *info)
{
    if (!g_driver)
        return FALSE;
    lock();
    PAD *p = find(host);
    if (p) {
        memset(info, 0, sizeof(*info));
        SIZE_T n = wcslen(L"\\\\?\\");
        wcscpy(info->Path, L"\\\\?\\");
        for (PCWSTR s = p->HidInstance; *s && n < RTL_NUMBER_OF(info->Path) - 40; s++)
            info->Path[n++] = *s == L'\\' ? L'#' : (WCHAR)towlower(*s);
        wcscpy(info->Path + n, L"#{4d1e55b2-f16f-11cf-88cb-001111000030}");
        info->Preparsed = g_preparsed;
        info->PreparsedLength = g_preparsed_length;
        info->VendorId = 0x045E;
        info->ProductId = 0x028E;
        info->UsagePage = 1;
        info->Usage = 5;
        build_report(p, info->Report);
    }
    unlock();
    return p != nullptr;
}

/* ------------------------------------------------------------ requests */

BOOLEAN IopIsPad(PFILE_OBJECT file)
{
    return g_driver && file->DeviceObject && file->DeviceObject->DriverObject == g_driver;
}

typedef struct ANSWER {
    NTSTATUS Status;
    ULONG_PTR Information;
    LIST_ENTRY *Queue; /* to pend on */
    ULONG Length;      /* of Out to copy back */
    UCHAR Out[XUSB_INFO_EX_SIZE];
} ANSWER;

static NTSTATUS answer(ANSWER *a, NTSTATUS status, ULONG_PTR information)
{
    a->Status = status;
    a->Information = information;
    a->Length = NT_SUCCESS(status) ? (ULONG)information : 0;
    return status;
}

static NTSTATUS pend(ANSWER *a, LIST_ENTRY *queue)
{
    a->Queue = queue;
    return STATUS_PENDING;
}

/* a product string as hidusb copies it: the text and a null, or nothing when that does not fit. */
static NTSTATUS string_answer(ANSWER *a, PVOID output, ULONG out_len)
{
    ULONG bytes = sizeof(g_product) - sizeof(WCHAR);
    if (!output || out_len < 2)
        return answer(a, STATUS_INVALID_USER_BUFFER, 0);
    if (bytes > out_len - 2)
        return answer(a, STATUS_INVALID_BUFFER_SIZE, 0);
    memcpy(a->Out, g_product, sizeof(g_product));
    return answer(a, STATUS_SUCCESS, sizeof(g_product));
}

static NTSTATUS hid_control(PAD_FILE *f, PAD *p, ULONG code, const UCHAR *in, ULONG in_len, PVOID output, ULONG out_len,
                        ANSWER *a)
{
    switch (code) {
    case IOCTL_HID_GET_COLLECTION_INFORMATION:
        if (!output || !out_len)
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        if (out_len < 12)
            return answer(a, STATUS_INVALID_BUFFER_SIZE, 12);
        put32(a->Out, g_preparsed_length);
        put16(a->Out + 6, 0x045E);
        put16(a->Out + 8, 0x028E);
        return answer(a, STATUS_SUCCESS, 12);
    case IOCTL_HID_GET_COLLECTION_DESCRIPTOR: {
        /* method neither: a short buffer still gets what fits. */
        if (!output)
            return answer(a, STATUS_INVALID_BUFFER_SIZE, 0);
        ULONG n = out_len < g_preparsed_length ? out_len : g_preparsed_length;
        NTSTATUS status = KiCopyToUser(output, g_preparsed, n);
        if (NT_SUCCESS(status) && n < g_preparsed_length)
            status = STATUS_INVALID_BUFFER_SIZE;
        answer(a, status, g_preparsed_length);
        a->Length = 0;
        return status;
    }
    case IOCTL_HID_FLUSH_QUEUE:
        f->Head = f->Count = 0;
        return answer(a, STATUS_SUCCESS, 0);
    case IOCTL_GET_NUM_DEVICE_INPUT_BUFFERS:
        if (out_len < 4)
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        put32(a->Out, f->Limit);
        return answer(a, STATUS_SUCCESS, 4);
    case IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS: {
        if (in_len < 4)
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        ULONG limit = get16(in) | (ULONG)get16(in + 2) << 16;
        if (limit < 2 || limit > MAX_INPUT_BUFFERS)
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        f->Limit = limit;
        return answer(a, STATUS_SUCCESS, 0);
    }
    case IOCTL_HID_GET_DRIVER_CONFIG:
    case IOCTL_HID_SET_DRIVER_CONFIG:
        return answer(a, STATUS_NOT_IMPLEMENTED, 0);
    case IOCTL_HID_GET_POLL_FREQUENCY_MSEC:
    case IOCTL_HID_SET_POLL_FREQUENCY_MSEC:
        return answer(a, STATUS_INVALID_DEVICE_REQUEST, 0);
    case IOCTL_HID_GET_FEATURE:
    case IOCTL_HID_SET_FEATURE:
    case IOCTL_HID_SET_OUTPUT_REPORT:
    case IOCTL_HID_GET_OUTPUT_REPORT:
        /* the collection has no feature or output report. */
        return answer(a, STATUS_INVALID_DEVICE_REQUEST, 0);
    case IOCTL_HID_GET_INPUT_REPORT:
        /* hidusb asks with GET_REPORT, which xusb22 refuses. */
        if (!output || !out_len)
            return answer(a, STATUS_INVALID_USER_BUFFER, 0);
        return answer(a, STATUS_INVALID_PARAMETER, 0);
    case IOCTL_HID_GET_PRODUCT_STRING:
        return string_answer(a, output, out_len);
    case IOCTL_HID_GET_MANUFACTURER_STRING:
    case IOCTL_HID_GET_SERIALNUMBER_STRING:
        if (!output || out_len < 2)
            return answer(a, STATUS_INVALID_USER_BUFFER, 0);
        return answer(a, STATUS_INVALID_PARAMETER, 0);
    case IOCTL_HID_GET_INDEXED_STRING:
        if (in_len < 4)
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        if (!output || out_len < 2)
            return answer(a, STATUS_INVALID_USER_BUFFER, 0);
        if (get16(in) != 1)
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        return string_answer(a, output, out_len);
    case IOCTL_GET_PHYSICAL_DESCRIPTOR:
    case IOCTL_HID_GET_MS_GENRE_DESCRIPTOR:
        return answer(a, STATUS_INVALID_PARAMETER, 0);
    case IOCTL_HID_GET_HARDWARE_ID:
        if (!output || !out_len)
            return answer(a, STATUS_INVALID_USER_BUFFER, 0);
        if (out_len < sizeof(g_hid_hardware_id))
            return answer(a, STATUS_BUFFER_TOO_SMALL, 0);
        memcpy(a->Out, g_hid_hardware_id, sizeof(g_hid_hardware_id));
        return answer(a, STATUS_SUCCESS, sizeof(g_hid_hardware_id));
    case IOCTL_HID_ENABLE_SECURE_READ:
    case IOCTL_HID_DISABLE_SECURE_READ:
        return answer(a, STATUS_PRIVILEGE_NOT_HELD, 0);
    case IOCTL_HID_SET_S0_IDLE_TIMEOUT:
        if (in_len != 4 || (get16(in) | (ULONG)get16(in + 2) << 16) > 100000)
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        return answer(a, STATUS_SUCCESS, 0);
    case IOCTL_HID_ENABLE_WAKE_ON_SX:
    case IOCTL_HID_SUPPRESS_INPUT:
        return answer(a, in_len == 1 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER, 0);
    case IOCTL_HID_DEVICERESET_NOTIFICATION:
    case IOCTL_HID_PRESENCE_NOTIFICATION:
        if (in_len)
            return answer(a, STATUS_INVALID_DEVICE_REQUEST, 0);
        return pend(a, &f->Waits);
    case IOCTL_HID_POWER_REFERENCE:
        f->PowerRefs++;
        return answer(a, STATUS_SUCCESS, 0);
    case IOCTL_HID_POWER_RELEASE:
        if (f->PowerRefs <= 0)
            return answer(a, STATUS_INVALID_DEVICE_STATE, 0);
        f->PowerRefs--;
        return answer(a, STATUS_SUCCESS, 0);
    case IOCTL_GET_SYS_BUTTON_CAPS:
        if (out_len < 4)
            return answer(a, STATUS_INVALID_BUFFER_SIZE, 4);
        put32(a->Out, 0);
        return answer(a, STATUS_SUCCESS, 4);
    case IOCTL_GET_SYS_BUTTON_EVENT:
        return pend(a, &f->Waits);
    default:
        return answer(a, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

/* the requests that carry a version and the index of a pad of the device, of which there is one. */
static BOOLEAN versioned(const UCHAR *in, ULONG in_len, ULONG need, USHORT version)
{
    return in_len >= need && get16(in) == version;
}

static NTSTATUS xusb_control(PAD_FILE *f, PAD *p, ULONG code, const UCHAR *in, ULONG in_len, ULONG out_len, ANSWER *a)
{
    switch (code) {
    case IOCTL_XUSB_GET_INFORMATION:
        if (out_len < 12)
            return answer(a, STATUS_INVALID_PARAMETER, 12);
        put16(a->Out, XUSB_VERSION);
        a->Out[2] = 1;
        put16(a->Out + 8, 0x045E);
        put16(a->Out + 10, 0x028E);
        return answer(a, STATUS_SUCCESS, 12);
    case IOCTL_XUSB_GET_STATE: {
        if (out_len < 2)
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        /* the first protocol sends the index alone; with a version, 0x0100 still means the old reply. */
        USHORT version = in_len >= 2 ? get16(in) : 0x0100;
        if (version > 0x0101)
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        BOOLEAN old = version < 0x0101;
        ULONG size = old ? 20 : XUSB_STATE_SIZE;
        if (out_len < (in_len >= 2 ? XUSB_STATE_SIZE : size))
            return answer(a, STATUS_INVALID_PARAMETER, size);
        if ((old ? in[0] : in[2]) != 0)
            return answer(a, STATUS_INVALID_PARAMETER, size);
        if (old)
            build_old_state(p, a->Out);
        else
            build_state(p, a->Out);
        return answer(a, STATUS_SUCCESS, size);
    }
    case IOCTL_XUSB_SET_STATE: {
        if (in_len < 5 || (in_len >= 7 && (get16(in + 5) != 0x0101 || in_len < 9)))
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        if (in[0] != 0)
            return answer(a, STATUS_DEVICE_NOT_CONNECTED, 0);
        if (in[4] & 2) {
            ULONG right = in[3];
            /* trigger motors fold into the right one. */
            if (in_len >= 9 && right < 153) {
                ULONG boost = (in[7] + in[8]) >> 2, cap = (153 - right) / 2;
                right += boost < cap ? boost : cap;
            }
            ULONG64 v;
            HalHypercall(MIME_HC_PAD_RUMBLE, p->HostId, in[2] * 257u, right * 257u, 0, 0, 0, &v);
        }
        if (in[4] & 1)
            p->Led = in[1];
        return answer(a, STATUS_SUCCESS, 0);
    }
    case IOCTL_XUSB_GET_LED_STATE:
        if (in_len < 3 || out_len < 3 || get16(in) > 0x0101)
            return answer(a, STATUS_INVALID_PARAMETER, 3);
        if (in[2] != 0)
            return answer(a, STATUS_INVALID_PARAMETER, 3);
        put16(a->Out, XUSB_VERSION);
        a->Out[2] = p->Led;
        return answer(a, STATUS_SUCCESS, 3);
    case IOCTL_XUSB_GET_CAPABILITIES: {
        USHORT version = in_len >= 3 ? get16(in) : 0;
        ULONG size = version == 0x0101 ? 24 : version == 0x0102 ? 36 : 0;
        if (!size || out_len < size)
            return answer(a, STATUS_INVALID_PARAMETER, size);
        if (in[2] != 0)
            return answer(a, STATUS_INVALID_PARAMETER, size);
        PUCHAR o = a->Out;
        put16(o, XUSB_VERSION);
        o[3] = 1;
        if (size == 36) {
            /* type gamepad, no flags: no headset or plug in module is bound. */
            o[2] = 1;
            put16(o + 6, 0x045E);
            put16(o + 8, 0x028E);
            put16(o + 10, PAD_REVISION);
            o += 12;
        }
        put16(o + 4, 0xF7FF);
        o[6] = o[7] = 0xFF;
        for (ULONG i = 0; i < 4; i++)
            put16(o + 8 + 2 * i, 0xFFC0);
        o[22] = o[23] = 0xFF;
        return answer(a, STATUS_SUCCESS, size);
    }
    case IOCTL_XUSB_WAIT_GUIDE_BUTTON:
        if (!versioned(in, in_len, 3, 0x0102) || out_len < XUSB_STATE_SIZE)
            return answer(a, STATUS_INVALID_PARAMETER, XUSB_STATE_SIZE);
        if (in[2] != 0)
            return answer(a, STATUS_INVALID_PARAMETER, XUSB_STATE_SIZE);
        return pend(a, &f->Guide);
    case IOCTL_XUSB_WAIT_FOR_INPUT:
        if (!versioned(in, in_len, 3, 0x0102) || out_len < XUSB_STATE_SIZE)
            return answer(a, STATUS_INVALID_PARAMETER, XUSB_STATE_SIZE);
        if (in[2] != 0)
            return answer(a, STATUS_INVALID_PARAMETER, XUSB_STATE_SIZE);
        return pend(a, &f->Input);
    case IOCTL_XUSB_GET_BATTERY_INFORMATION:
        if (!versioned(in, in_len, 4, 0x0102) || out_len < 4 || in[3] > 1)
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        if (in[3] == 0 && in[2] != 0)
            return answer(a, STATUS_DEVICE_NOT_CONNECTED, 0);
        put16(a->Out, XUSB_VERSION);
        /* a wired pad is full; there is no headset. */
        if (in[3] == 0) {
            a->Out[2] = 1;
            a->Out[3] = 3;
        }
        return answer(a, STATUS_SUCCESS, 4);
    case IOCTL_XUSB_POWER_DOWN:
        if (!versioned(in, in_len, 3, 0x0102))
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        return answer(a, in[2] ? STATUS_DEVICE_NOT_CONNECTED : STATUS_SUCCESS, 0);
    case IOCTL_XUSB_GET_AUDIO_INFORMATION:
        if (!versioned(in, in_len, 3, 0x0102) || out_len < 7)
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        put16(a->Out, XUSB_VERSION);
        a->Out[6] = 0xFF;
        return answer(a, STATUS_SUCCESS, 7);
    case IOCTL_XUSB_GET_INFORMATION_EX:
        if (!versioned(in, in_len, 36, 0x0104) || out_len < XUSB_INFO_EX_SIZE)
            return answer(a, STATUS_INVALID_PARAMETER, 0);
        /* the version stays as the caller sent it. */
        put16(a->Out, 0x0104);
        if (in[2] != 2) {
            a->Out[2] = 1;
            return answer(a, STATUS_SUCCESS, XUSB_INFO_EX_SIZE);
        }
        put16(a->Out + 3, 32);
        memcpy(a->Out + 5, g_usb_descriptor, sizeof(g_usb_descriptor));
        put16(a->Out + 23, 0x045E);
        put16(a->Out + 25, 0x028E);
        put16(a->Out + 27, PAD_REVISION);
        return answer(a, STATUS_SUCCESS, XUSB_INFO_EX_SIZE);
    default:
        return answer(a, STATUS_INVALID_PARAMETER, 0);
    }
}

NTSTATUS PadDeviceControl(PFILE_OBJECT file, HANDLE event, PVOID apc, PVOID context, PIO_STATUS_BLOCK iosb, ULONG code,
                          PVOID input, ULONG in_len, PVOID output, ULONG out_len)
{
    PAD_FILE *f = file->FsContext;
    if (!f)
        return STATUS_PRIVILEGE_NOT_HELD;
    ULONG access = code >> 14 & 3;
    if (((access & 1) && !file->ReadAccess) || ((access & 2) && !file->WriteAccess))
        return STATUS_ACCESS_DENIED;
    ANSWER *a = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*a), KI_TAG('P', 'a', 'd', 'A'));
    if (!a)
        return STATUS_INSUFFICIENT_RESOURCES;
    memset(a, 0, sizeof(*a));
    UCHAR in[64] = {};
    NTSTATUS status = STATUS_SUCCESS;
    if (input && in_len && (code & 3) != 3)
        status = KiCopyFromUser(in, input, in_len < sizeof(in) ? in_len : sizeof(in));
    if (!NT_SUCCESS(status)) {
        ExFreePool(a);
        return status;
    }
    IO_USER_REQUEST *r = IopNewUserRequest(file, event, apc, context, iosb, output, out_len, &status);
    if (!r) {
        ExFreePool(a);
        return status;
    }
    lock();
    PAD *p = live(f);
    if (!p)
        answer(a, STATUS_DEVICE_NOT_CONNECTED, 0);
    else if (f->Kind == KIND_HID)
        hid_control(f, p, code, in, in_len, output, out_len, a);
    else
        xusb_control(f, p, code, in, in_len, out_len, a);
    if (a->Queue) {
        InsertTailList(a->Queue, &r->Links);
        unlock();
        ExFreePool(a);
        return IopPendUserRequest(r);
    }
    unlock();
    status = a->Status;
    if (a->Length) {
        NTSTATUS copied = KiCopyToUser(output, a->Out, a->Length < out_len ? a->Length : out_len);
        if (!NT_SUCCESS(copied))
            status = copied;
    }
    ULONG_PTR information = a->Information;
    ExFreePool(a);
    return IopImmediateUserRequest(r, status, information);
}

NTSTATUS PadReadWrite(PFILE_OBJECT file, BOOLEAN write, HANDLE event, PVOID apc, PVOID context, PIO_STATUS_BLOCK iosb,
                      PVOID buffer, ULONG length)
{
    PAD_FILE *f = file->FsContext;
    if (!f)
        return STATUS_PRIVILEGE_NOT_HELD;
    if (f->Kind == KIND_HID && !(write ? file->WriteAccess : file->ReadAccess))
        return STATUS_ACCESS_DENIED;
    NTSTATUS status;
    IO_USER_REQUEST *r = IopNewUserRequest(file, event, apc, context, iosb, buffer, length, &status);
    if (!r)
        return status;
    ULONG_PTR done = 0;
    lock();
    if (f->Kind == KIND_XUSB)
        status = STATUS_INVALID_PARAMETER;
    else if (!live(f))
        status = STATUS_DEVICE_NOT_CONNECTED;
    else if (write)
        status = !buffer || !length ? STATUS_INVALID_USER_BUFFER : STATUS_INVALID_DEVICE_REQUEST;
    else if (length < REPORT_SIZE)
        status = STATUS_INVALID_BUFFER_SIZE;
    else if (!f->Count) {
        InsertTailList(&f->Reads, &r->Links);
        unlock();
        return IopPendUserRequest(r);
    } else {
        /* as many whole queued reports as fit. */
        status = STATUS_SUCCESS;
        while (f->Count && done + REPORT_SIZE <= length) {
            status = KiCopyToUser((PUCHAR)buffer + done, f->Ring + f->Head * REPORT_SIZE, REPORT_SIZE);
            if (!NT_SUCCESS(status))
                break;
            f->Head = (f->Head + 1) % MAX_INPUT_BUFFERS;
            f->Count--;
            done += REPORT_SIZE;
        }
    }
    unlock();
    return IopImmediateUserRequest(r, status, NT_SUCCESS(status) ? done : 0);
}

static BOOLEAN cancel_list(LIST_ENTRY *list, PETHREAD thread, PVOID iosb)
{
    BOOLEAN found = FALSE;
    PLIST_ENTRY e = list->Flink;
    while (e != list) {
        IO_USER_REQUEST *r = CONTAINING_RECORD(e, IO_USER_REQUEST, Links);
        e = e->Flink;
        if (r->Process != PsCurrentProcess() || (thread && r->Thread != thread) || (iosb && ((ULONG_PTR)r->Iosb & ~(ULONG_PTR)1) != (ULONG_PTR)iosb))
            continue;
        RemoveEntryList(&r->Links);
        IopCompleteUserRequest(r, STATUS_CANCELLED, 0);
        found = TRUE;
    }
    return found;
}

/* CancelIo with the thread, CancelIoEx with the status block of one request or neither. */
NTSTATUS PadCancel(PFILE_OBJECT file, PETHREAD thread, PVOID iosb)
{
    PAD_FILE *f = file->FsContext;
    BOOLEAN found = FALSE;
    lock();
    if (f) {
        found |= cancel_list(&f->Reads, thread, iosb);
        found |= cancel_list(&f->Waits, thread, iosb);
        found |= cancel_list(&f->Guide, thread, iosb);
        found |= cancel_list(&f->Input, thread, iosb);
    }
    unlock();
    return found ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

/* ------------------------------------------------------------ the driver */

static NTSTATUS complete_irp(PIRP irp, NTSTATUS status, ULONG_PTR information)
{
    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IofCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

/* hidclass's share rules: only reading and writing data count. */
static BOOLEAN share_conflict(const PAD *p, BOOLEAN read, BOOLEAN write, USHORT share)
{
    return p->Exclusive || (p->Opens && !share) || (p->NoShareRead && read) || (p->NoShareWrite && write) ||
           (p->Readers && !(share & FILE_SHARE_READ)) || (p->Writers && !(share & FILE_SHARE_WRITE));
}

static void count_open(PAD *p, PAD_FILE *f, LONG delta)
{
    p->Opens += delta;
    p->Readers += f->Read ? delta : 0;
    p->Writers += f->Write ? delta : 0;
    p->NoShareRead += f->Share & FILE_SHARE_READ ? 0 : delta;
    p->NoShareWrite += f->Share & FILE_SHARE_WRITE ? 0 : delta;
    p->Exclusive += f->Share ? 0 : delta;
}

static NTSTATUS NTAPI pad_create(PDEVICE_OBJECT device, PIRP irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
    PFILE_OBJECT file = stack->FileObject;
    PAD_DEVICE *d = device->DeviceExtension;
    PAD_FILE *f = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*f), KI_TAG('P', 'a', 'd', 'F'));
    if (!f)
        return complete_irp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    memset(f, 0, sizeof(*f));
    f->Kind = d->Kind;
    f->Slot = d->Slot;
    f->Read = file->ReadAccess;
    f->Write = file->WriteAccess;
    f->Share = stack->Parameters.Create.ShareAccess;
    f->Limit = DEFAULT_INPUT_BUFFERS;
    InitializeListHead(&f->Reads);
    InitializeListHead(&f->Waits);
    InitializeListHead(&f->Guide);
    InitializeListHead(&f->Input);
    if (f->Kind == KIND_HID &&
        !(f->Ring = ExAllocatePool2(POOL_FLAG_NON_PAGED, MAX_INPUT_BUFFERS * REPORT_SIZE, KI_TAG('P', 'a', 'd', 'R')))) {
        ExFreePool(f);
        return complete_irp(irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }
    NTSTATUS status = STATUS_SUCCESS;
    lock();
    PAD *p = &g_pads[d->Slot];
    if (!p->Used)
        status = STATUS_DEVICE_NOT_CONNECTED;
    else if (f->Kind == KIND_HID && share_conflict(p, f->Read, f->Write, f->Share))
        status = STATUS_SHARING_VIOLATION;
    else if (f->Kind == KIND_HID && (stack->Parameters.Create.Options & FILE_DIRECTORY_FILE))
        status = STATUS_NOT_A_DIRECTORY;
    if (NT_SUCCESS(status)) {
        f->Generation = p->Generation;
        if (f->Kind == KIND_HID)
            count_open(p, f, 1);
        InsertTailList(&p->Files, &f->Links);
        file->FsContext = f;
    }
    unlock();
    if (!NT_SUCCESS(status)) {
        if (f->Ring)
            ExFreePool(f->Ring);
        ExFreePool(f);
    }
    return complete_irp(irp, status, 0);
}

/* the last handle went: what is pending ends, and the handle stops counting for sharing. */
static NTSTATUS NTAPI pad_cleanup(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    PAD_FILE *f = file->FsContext;
    if (f) {
        lock();
        complete_file(f, STATUS_CANCELLED);
        PAD *p = live(f);
        if (p && f->Kind == KIND_HID)
            count_open(p, f, -1);
        RemoveEntryList(&f->Links);
        InitializeListHead(&f->Links);
        f->Generation = 0;
        unlock();
    }
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI pad_close(PDEVICE_OBJECT device, PIRP irp)
{
    PFILE_OBJECT file = IoGetCurrentIrpStackLocation(irp)->FileObject;
    PAD_FILE *f = file->FsContext;
    if (f) {
        if (f->Ring)
            ExFreePool(f->Ring);
        ExFreePool(f);
        file->FsContext = nullptr;
    }
    return complete_irp(irp, STATUS_SUCCESS, 0);
}

static NTSTATUS NTAPI PadDriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING registry)
{
    driver->MajorFunction[IRP_MJ_CREATE] = pad_create;
    driver->MajorFunction[IRP_MJ_CLEANUP] = pad_cleanup;
    driver->MajorFunction[IRP_MJ_CLOSE] = pad_close;
    for (ULONG i = 0; i < MAX_PADS; i++) {
        for (ULONG kind = KIND_HID; kind <= KIND_XUSB; kind++) {
            WCHAR buffer[32];
            _snwprintf(buffer, RTL_NUMBER_OF(buffer), kind == KIND_HID ? L"\\Device\\MimePadHid%u" : L"\\Device\\MimePadXusb%u",
                       i);
            UNICODE_STRING name;
            RtlInitUnicodeString(&name, buffer);
            PDEVICE_OBJECT *device = kind == KIND_HID ? &g_hid[i] : &g_xusb[i];
            NTSTATUS status = IoCreateDevice(driver, sizeof(PAD_DEVICE), &name,
                                             kind == KIND_HID ? FILE_DEVICE_UNKNOWN : FILE_DEVICE_BUS_EXTENDER, 0, FALSE,
                                             device);
            if (!NT_SUCCESS(status))
                return status;
            PAD_DEVICE *d = (*device)->DeviceExtension;
            d->Kind = kind;
            d->Slot = i;
        }
    }
    return STATUS_SUCCESS;
}

void IopInitializePads(void)
{
    KeInitializeMutex(&g_lock, 0);
    for (ULONG i = 0; i < MAX_PADS; i++) {
        g_pads[i].Slot = i;
        InitializeListHead(&g_pads[i].Files);
    }
    HIDP_PARSE parse;
    NTSTATUS status = HidpParseDescriptor(g_descriptor, sizeof(g_descriptor), &parse);
    if (!NT_SUCCESS(status) || parse.Collections != 1) {
        KiLogPrintf("game controllers have no preparsed data: %08x", status);
        return;
    }
    g_preparsed = parse.Collection[0].Preparsed;
    g_preparsed_length = parse.Collection[0].PreparsedLength;
    parse.Collection[0].Preparsed = nullptr;
    HidpFreeParse(&parse);
    PDRIVER_OBJECT driver;
    status = IoCreateDriver(L"\\Driver\\MimePad", PadDriverEntry, &driver);
    if (!NT_SUCCESS(status)) {
        KiLogPrintf("game controllers failed to start: %08x", status);
        return;
    }
    g_driver = driver;
}
