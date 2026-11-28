/*
 * display modes and gamma ramps. the host display is the one real mode, but a program may switch to the smaller
 * sizes and 8, 16 or 32 bit modes windows lists. the host scales a fullscreen window of a smaller mode to its
 * screen like a monitor would, and drawing stays 32 bits. like windows, a CDS_FULLSCREEN mode reverts when the
 * process that set it ends.
 *
 * the displays are the host monitors, then the adapters of the video map that drive none of them. what a display
 * lists follows its driver, as measured on windows 11 26300: the basic display adapter has the one mode it booted
 * in at 64 hz, and an indirect display has the modes of its driver.
 */
#include "win32k.h"
#include <mime/win32/winspool.h>
#include <mime/display_modes.h>

NTKERNELAPI NTSTATUS NTAPI ZwOpenKey(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTKERNELAPI NTSTATUS NTAPI ZwQueryValueKey(HANDLE key, PUNICODE_STRING name, ULONG klass, PVOID info, ULONG length,
                                           PULONG returned);
NTKERNELAPI NTSTATUS NTAPI ZwEnumerateKey(HANDLE key, ULONG index, ULONG klass, PVOID info, ULONG length,
                                          PULONG returned);

#ifndef DMDO_DEFAULT
#define DMDO_DEFAULT 0
#define DMDO_270 3
#define DMDFO_DEFAULT 0
#define DM_INTERLACED 0x00000002
#endif

static struct {
    BOOLEAN active;
    DWORD width, height, bits;
    HANDLE owner; /* a temporary mode reverts when this process ends. */
} g_mode;

static LONG g_native_width, g_native_height;

/* ------------------------------------------------------------------ the video map */

enum { DRIVER_GPU, DRIVER_BASIC, DRIVER_INDIRECT, DRIVER_MONITOR, DRIVER_NONE };

#define MAX_SPARES 4

typedef struct {
    DWORD width, height, hz;
} SIZE_MODE;

static struct {
    BOOLEAN read;
    UCHAR primary;
    ULONG spares;
    UCHAR spare[MAX_SPARES];
    UCHAR spare_object[MAX_SPARES];   /* the video object of each one. */
    SIZE_MODE spare_mode[MAX_SPARES]; /* the mode each one is in. */
} g_video;

/* a text of the machine's registry. of a list of texts, the first one. */
static BOOLEAN machine_value(PCWSTR key, PCWSTR value, ULONG type, WCHAR *text, ULONG chars)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    HANDLE handle;
    RtlInitUnicodeString(&name, key);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    text[0] = 0;
    if (!NT_SUCCESS(ZwOpenKey(&handle, KEY_QUERY_VALUE, &oa)))
        return FALSE;
    /* KEY_VALUE_PARTIAL_INFORMATION: the title index, the type and the size, then the data. */
    struct {
        ULONG title, type, size;
        WCHAR data[256];
    } info;
    ULONG got = 0;
    RtlInitUnicodeString(&name, value);
    NTSTATUS status = ZwQueryValueKey(handle, &name, 2, &info, sizeof(info) - sizeof(WCHAR), &got);
    ZwClose(handle);
    if (type == REG_MULTI_SZ && status == STATUS_BUFFER_OVERFLOW)
        info.size = sizeof(info.data) - sizeof(WCHAR);
    else if (!NT_SUCCESS(status) || info.size > sizeof(info.data) - sizeof(WCHAR))
        return FALSE;
    if (info.type != type)
        return FALSE;
    info.data[info.size / sizeof(WCHAR)] = 0;
    if (wcslen(info.data) >= chars)
        return FALSE;
    wcscpy(text, info.data);
    return TRUE;
}

static BOOLEAN machine_text(PCWSTR key, PCWSTR value, WCHAR *text, ULONG chars)
{
    return machine_value(key, value, REG_SZ, text, chars);
}

/* the driver of a video object: the service of its video key, when the map holds an adapter there and no service. */
static UCHAR video_driver(ULONG object)
{
    static const WCHAR video[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\";
    WCHAR name[24] = L"\\Device\\Video", key[160], service[64];
    name[13] = (WCHAR)(L'0' + object);
    name[14] = 0;
    if (!machine_text(L"\\Registry\\Machine\\HARDWARE\\DEVICEMAP\\VIDEO", name, key, RTL_NUMBER_OF(key) - 8) ||
        _wcsnicmp(key, video, RTL_NUMBER_OF(video) - 1))
        return DRIVER_NONE;
    /* the adapter is {id}\0000 and its driver stands in {id}\Video. */
    WCHAR *last = wcsrchr(key, L'\\');
    wcscpy(last + 1, L"Video");
    if (!machine_text(key, L"Service", service, RTL_NUMBER_OF(service)))
        return DRIVER_GPU;
    if (!_wcsicmp(service, L"BasicDisplay"))
        return DRIVER_BASIC;
    if (!_wcsicmp(service, L"IndirectKmd"))
        return DRIVER_INDIRECT;
    return DRIVER_GPU;
}

/* the registry is read once and outside the user lock. */
static void video_read(void)
{
    if (g_video.read)
        return;
    UCHAR primary = video_driver(0), spare[MAX_SPARES], objects[MAX_SPARES];
    ULONG spares = 0;
    for (ULONG object = 1; object < 10 && spares < MAX_SPARES; object++) {
        UCHAR driver = video_driver(object);
        if (driver != DRIVER_NONE) {
            objects[spares] = (UCHAR)object;
            spare[spares++] = driver;
        }
    }
    UserEnter();
    if (!g_video.read) {
        g_video.primary = primary == DRIVER_BASIC ? DRIVER_BASIC : DRIVER_GPU;
        for (ULONG i = 0; i < spares; i++) {
            g_video.spare[i] = spare[i];
            g_video.spare_object[i] = objects[i];
            g_video.spare_mode[i] = (SIZE_MODE){800, 600, 30};
        }
        g_video.spares = spares;
        g_video.read = TRUE;
    }
    UserLeave();
}

/* ------------------------------------------------------------------ the modes of a display */

/* what the virtual display driver of the reference machine lists, as it lists it. */
static const SIZE_MODE INDIRECT_MODES[] = {
    {800, 600, 30},    {800, 600, 60},    {800, 600, 90},    {800, 600, 120},   {800, 600, 144},   {800, 600, 165},
    {1280, 720, 30},   {1280, 720, 60},   {1280, 720, 90},   {1280, 720, 130},  {1280, 720, 144},  {1280, 720, 165},
    {1366, 768, 30},   {1366, 768, 60},   {1366, 768, 90},   {1366, 768, 120},  {1366, 768, 144},  {1366, 768, 165},
    {1920, 1080, 30},  {1920, 1080, 60},  {1920, 1080, 90},  {1920, 1080, 120}, {1920, 1080, 144}, {1920, 1080, 165},
    {2560, 1440, 30},  {2560, 1440, 60},  {2560, 1440, 90},  {2560, 1440, 120}, {2560, 1440, 144}, {2560, 1440, 165},
    {3840, 2160, 30},  {3840, 2160, 60},  {3840, 2160, 90},  {3840, 2160, 120}, {3840, 2160, 144}, {3840, 2160, 165},
};

/* the basic display adapter gives every mode this rate. */
#define BASIC_HZ 64

typedef struct {
    DWORD width, height, bits, hz;
    DWORD orientation;
    POINTL position;
} DISPLAY_MODE;

static UCHAR display_driver(ULONG display)
{
    ULONG monitors = MonitorCount();
    if (display == 0)
        return g_video.primary;
    if (display < monitors)
        return DRIVER_MONITOR;
    return display - monitors < g_video.spares ? g_video.spare[display - monitors] : DRIVER_NONE;
}

static DWORD primary_hz(void)
{
    MIME_MONITOR_INFO info;
    if (g_video.primary == DRIVER_BASIC)
        return BASIC_HZ;
    return MonitorGet(0, &info) && info.RefreshHz ? info.RefreshHz : W32Display.RefreshHz;
}

/* the mode a display is in, and the one it starts in. */
static BOOLEAN display_current(ULONG display, BOOLEAN registry, DISPLAY_MODE *m)
{
    MIME_MONITOR_INFO info;
    memset(m, 0, sizeof(*m));
    m->bits = 32;
    switch (display_driver(display)) {
    case DRIVER_GPU:
    case DRIVER_BASIC:
        m->width = registry ? (DWORD)g_native_width : (DWORD)W32Display.Width;
        m->height = registry ? (DWORD)g_native_height : (DWORD)W32Display.Height;
        m->bits = registry ? 32 : W32Display.Bits;
        m->hz = primary_hz();
        return TRUE;
    case DRIVER_MONITOR:
        if (!MonitorGet(display, &info))
            return FALSE;
        m->width = (DWORD)(info.Rect.right - info.Rect.left);
        m->height = (DWORD)(info.Rect.bottom - info.Rect.top);
        m->hz = info.RefreshHz ? info.RefreshHz : W32Display.RefreshHz;
        m->position = (POINTL){info.Rect.left, info.Rect.top};
        return TRUE;
    case DRIVER_INDIRECT: {
        const SIZE_MODE *mode = &g_video.spare_mode[display - MonitorCount()];
        m->width = mode->width;
        m->height = mode->height;
        m->hz = mode->hz;
        /* it stands to the right of the monitors. */
        m->position = (POINTL){MonitorVirtualScreen().right, 0};
        return TRUE;
    }
    default:
        return FALSE;
    }
}

/* the host display's modes in windows' order: by bits, then width, then height, each size up to the native one. */
static BOOLEAN gpu_mode_at(DWORD index, DISPLAY_MODE *m)
{
    static const DWORD BITS[] = {8, 16, 32};
    DWORD n = 0;
    for (ULONG b = 0; b < RTL_NUMBER_OF(BITS); b++) {
        BOOLEAN native_done = FALSE;
        for (ULONG i = 0; i <= RTL_NUMBER_OF(MIME_DISPLAY_RESOLUTIONS); i++) {
            DWORD w, h;
            if (i < RTL_NUMBER_OF(MIME_DISPLAY_RESOLUTIONS)) {
                w = MIME_DISPLAY_RESOLUTIONS[i].width;
                h = MIME_DISPLAY_RESOLUTIONS[i].height;
                if ((LONG)w > g_native_width || (LONG)h > g_native_height)
                    continue;
                if ((LONG)w == g_native_width && (LONG)h == g_native_height)
                    native_done = TRUE;
            } else {
                if (native_done)
                    break;
                w = (DWORD)g_native_width;
                h = (DWORD)g_native_height;
            }
            if (n++ == index) {
                m->width = w;
                m->height = h;
                m->bits = BITS[b];
                return TRUE;
            }
        }
    }
    return FALSE;
}

static BOOLEAN display_mode_at(ULONG display, DWORD index, DISPLAY_MODE *m)
{
    memset(m, 0, sizeof(*m));
    m->bits = 32;
    switch (display_driver(display)) {
    case DRIVER_GPU:
        m->hz = primary_hz();
        return gpu_mode_at(index, m);
    case DRIVER_BASIC:
        m->width = (DWORD)g_native_width;
        m->height = (DWORD)g_native_height;
        m->hz = BASIC_HZ;
        return index == 0;
    case DRIVER_MONITOR:
        /* the other monitors only have their own size at 32 bits. */
        return index == 0 && display_current(display, TRUE, m);
    case DRIVER_INDIRECT:
        if (index >= RTL_NUMBER_OF(INDIRECT_MODES))
            return FALSE;
        m->width = INDIRECT_MODES[index].width;
        m->height = INDIRECT_MODES[index].height;
        m->hz = INDIRECT_MODES[index].hz;
        return TRUE;
    default:
        return FALSE;
    }
}

/*
 * whether a display lists the mode. the size is the one of the orientation asked, 24 bits are the 32 bit mode, and
 * a rate of 0 or 1 is the default one where the driver has such a rate: the host display's and the basic display's
 * 1, not the indirect display's.
 */
static BOOLEAN display_has_mode(ULONG display, const DISPLAY_MODE *want, BOOLEAN any_hz)
{
    UCHAR driver = display_driver(display);
    DISPLAY_MODE m, w = *want;
    if (w.orientation > DMDO_270)
        return FALSE;
    if (w.orientation & 1) {
        w.width = want->height;
        w.height = want->width;
    }
    if (w.bits == 24)
        w.bits = 32;
    if (!any_hz && w.hz == 1 && driver != DRIVER_INDIRECT)
        any_hz = TRUE;
    if (!any_hz && w.hz == 0 && driver == DRIVER_GPU)
        any_hz = TRUE;
    for (DWORD i = 0; display_mode_at(display, i, &m); i++)
        if (m.width == w.width && m.height == w.height && m.bits == w.bits && (any_hz || m.hz == w.hz))
            return TRUE;
    return FALSE;
}

/* W32Display comes in as the host display. a set mode replaces its size, and the work area becomes the whole screen. */
void DisplayApplyMode(void)
{
    g_native_width = W32Display.Width;
    g_native_height = W32Display.Height;
    if (g_mode.active && ((LONG)g_mode.width > g_native_width || (LONG)g_mode.height > g_native_height))
        g_mode.active = FALSE;
    W32Display.Bits = 32;
    if (!g_mode.active)
        return;
    W32Display.Width = (LONG)g_mode.width;
    W32Display.Height = (LONG)g_mode.height;
    W32Display.Work = (RECT){0, 0, W32Display.Width, W32Display.Height};
    W32Display.Bits = g_mode.bits;
}

static void set_mode(BOOLEAN active, DWORD width, DWORD height, DWORD bits, HANDLE owner)
{
    BOOLEAN changed = active != g_mode.active ||
                      (active && (width != g_mode.width || height != g_mode.height || bits != g_mode.bits));
    g_mode.active = active;
    g_mode.width = width;
    g_mode.height = height;
    g_mode.bits = bits;
    g_mode.owner = owner;
    if (changed)
        MetricsDisplayChanged();
}

static void gamma_process_exited(HANDLE pid);

void DisplayProcessExited(HANDLE pid)
{
    if (g_mode.active && g_mode.owner && g_mode.owner == pid)
        set_mode(FALSE, 0, 0, 0, nullptr);
    gamma_process_exited(pid);
}

/*
 * the number of the display in a device name, counting from 1, and 0 when the name is no display's. no name at all
 * is the primary one, an empty name is none. the names are \\.\DISPLAYn, in any case and with no zero before the
 * number.
 */
#define NUMBER_DISCONNECTED 0xFFFFFFFFu /* WinDisc, the display of a disconnected session. */

static ULONG device_number(PUNICODE_STRING user_device)
{
    static const WCHAR prefix[] = L"\\\\.\\DISPLAY";
    PWSTR device;
    SIZE_T chars;
    if (!user_device)
        return 1;
    if (!NT_SUCCESS(W32CaptureString(user_device, &device, &chars)))
        return 0;
    ULONG number = 0;
    if (chars == 7 && !_wcsicmp(device, L"WinDisc")) {
        W32Free(device);
        return NUMBER_DISCONNECTED;
    }
    PCWSTR digits = device + RTL_NUMBER_OF(prefix) - 1;
    if (chars >= RTL_NUMBER_OF(prefix) && chars < 32 && wcslen(device) == chars &&
        !_wcsnicmp(device, prefix, RTL_NUMBER_OF(prefix) - 1) && *digits != L'0') {
        for (; *digits >= L'0' && *digits <= L'9' && number < 1000; digits++)
            number = number * 10 + (ULONG)(*digits - L'0');
        if (*digits)
            number = 0;
    }
    W32Free(device);
    return number;
}

/* the display of that number, -1 when there is none. */
static LONG numbered_display(ULONG number)
{
    if (!number || number == NUMBER_DISCONNECTED || number - 1 >= MonitorCount() + g_video.spares || display_driver(number - 1) == DRIVER_NONE)
        return -1;
    return (LONG)(number - 1);
}

#define MODE_SIZE ((WORD)sizeof(DEVMODEW))
#define MODE_SIZE_MIN ((WORD)offsetof(DEVMODEW, dmICMMethod))

/* the driver every display draws through names the modes: in capitals the mode a display is in, in small letters a listed one. */
static void fill(DEVMODEW *m, const DISPLAY_MODE *mode, BOOLEAN listed)
{
    memset(m, 0, sizeof(*m));
    memcpy(m->dmDeviceName, listed ? L"cdd" : L"CDD", sizeof(L"cdd"));
    m->dmSpecVersion = DM_SPECVERSION;
    m->dmDriverVersion = DM_SPECVERSION;
    m->dmSize = sizeof(*m);
    m->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY |
                  DM_DISPLAYORIENTATION;
    if (!listed) {
        m->dmFields |= DM_POSITION | DM_DISPLAYFIXEDOUTPUT;
        m->dmPosition = mode->position;
    }
    m->dmDisplayOrientation = mode->orientation;
    m->dmBitsPerPel = mode->bits;
    m->dmPelsWidth = mode->width;
    m->dmPelsHeight = mode->height;
    m->dmDisplayFrequency = mode->hz;
}

#define DRIVER_EXTRA 24

/*
 * the display a disconnected session draws on is in no list and has no current mode, but its name gives the two
 * modes of its stub driver, and the whole desktop as the mode it starts in.
 */
static NTSTATUS disconnected_mode(DWORD index, DEVMODEW *m)
{
    if (index == ENUM_CURRENT_SETTINGS || index == (DWORD)-3)
        return STATUS_INVALID_PARAMETER_1;
    if (index != ENUM_REGISTRY_SETTINGS && index > 1)
        return STATUS_INVALID_PARAMETER_2;
    RECT all = MonitorVirtualScreen();
    DISPLAY_MODE mode = {.bits = 32};
    if (index == ENUM_REGISTRY_SETTINGS) {
        mode.width = (DWORD)(all.right - all.left);
        mode.height = (DWORD)(all.bottom - all.top);
    } else {
        mode.width = index ? 640 : 1024;
        mode.height = index ? 480 : 768;
        mode.hz = 60;
    }
    fill(m, &mode, TRUE);
    if (index == ENUM_REGISTRY_SETTINGS) {
        m->dmDeviceName[0] = 0;
        m->dmFields = DM_POSITION | DM_DISPLAYORIENTATION | DM_LOGPIXELS | DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT |
                      DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY | DM_PANNINGWIDTH | DM_PANNINGHEIGHT;
        m->dmLogPixels = 96;
    } else {
        memcpy(m->dmDeviceName, L"StubDisp", sizeof(L"StubDisp"));
    }
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ the display devices */

#define MACHINE_KEY L"\\Registry\\Machine\\"
#define ENUM_KEY MACHINE_KEY L"System\\CurrentControlSet\\Enum\\"
#define CLASS_KEY MACHINE_KEY L"System\\CurrentControlSet\\Control\\Class\\"

/* the nth key under a key. */
static BOOLEAN machine_subkey(HANDLE key, ULONG index, WCHAR *name, ULONG chars)
{
    /* KEY_BASIC_INFORMATION: the time of the last write, the title index and the size of the name, then the name. */
    struct {
        LARGE_INTEGER written;
        ULONG title, size;
        WCHAR name[128];
    } info;
    ULONG got = 0;
    if (!NT_SUCCESS(ZwEnumerateKey(key, index, 0, &info, sizeof(info), &got)) || info.size / sizeof(WCHAR) >= chars)
        return FALSE;
    memcpy(name, info.name, info.size);
    name[info.size / sizeof(WCHAR)] = 0;
    return TRUE;
}

static HANDLE machine_open(PCWSTR key)
{
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    HANDLE handle;
    RtlInitUnicodeString(&name, key);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    return NT_SUCCESS(ZwOpenKey(&handle, KEY_READ, &oa)) ? handle : nullptr;
}

/*
 * the first hardware id of the device a bus enumerated with this video id, which windows gives as the adapter's id,
 * and when asked the instance of that device as bus\device\instance.
 */
static BOOLEAN adapter_hardware(PCWSTR bus, PCWSTR id, WCHAR hardware[128], WCHAR *instance_path, ULONG chars)
{
    WCHAR path[400], device[128], instance[128], found[64];
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, ENUM_KEY L"%ls", bus);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    HANDLE root = machine_open(path);
    if (!root)
        return FALSE;
    BOOLEAN ok = FALSE;
    for (ULONG i = 0; !ok && machine_subkey(root, i, device, RTL_NUMBER_OF(device)); i++) {
        _snwprintf(path, RTL_NUMBER_OF(path) - 1, ENUM_KEY L"%ls\\%ls", bus, device);
        HANDLE devices = machine_open(path);
        if (!devices)
            continue;
        for (ULONG j = 0; !ok && machine_subkey(devices, j, instance, RTL_NUMBER_OF(instance)); j++) {
            _snwprintf(path, RTL_NUMBER_OF(path) - 1, ENUM_KEY L"%ls\\%ls\\%ls\\Device Parameters", bus, device, instance);
            if (!machine_text(path, L"VideoID", found, RTL_NUMBER_OF(found)) || _wcsicmp(found, id))
                continue;
            _snwprintf(path, RTL_NUMBER_OF(path) - 1, ENUM_KEY L"%ls\\%ls\\%ls", bus, device, instance);
            /* the list holds several ids, the most specific first. */
            ok = machine_value(path, L"HardwareID", REG_MULTI_SZ, hardware, 128);
            if (ok && instance_path) {
                _snwprintf(instance_path, chars - 1, L"%ls\\%ls\\%ls", bus, device, instance);
                instance_path[chars - 1] = 0;
            }
        }
        ZwClose(devices);
    }
    ZwClose(root);
    return ok;
}

/* the adapter of a video object: what it is called, its id and its key. */
static BOOLEAN adapter_device(ULONG object, DISPLAY_DEVICEW *d)
{
    static const WCHAR video[] = MACHINE_KEY L"System\\CurrentControlSet\\Control\\Video\\";
    /* the device tree does not change while the machine runs: an adapter is looked for once. */
    static struct {
        BOOLEAN known;
        WCHAR key[128], string[128], hardware[128];
    } cache[10];
    if (object >= RTL_NUMBER_OF(cache))
        return FALSE;
    UserEnter();
    BOOLEAN known = cache[object].known;
    if (known) {
        wcscpy(d->DeviceKey, cache[object].key);
        wcscpy(d->DeviceString, cache[object].string);
        wcscpy(d->DeviceID, cache[object].hardware);
    }
    UserLeave();
    if (known)
        return TRUE;
    WCHAR name[24] = L"\\Device\\Video", id[64] = {};
    name[13] = (WCHAR)(L'0' + object);
    name[14] = 0;
    if (!machine_text(MACHINE_KEY L"HARDWARE\\DEVICEMAP\\VIDEO", name, d->DeviceKey, RTL_NUMBER_OF(d->DeviceKey)) ||
        _wcsnicmp(d->DeviceKey, video, RTL_NUMBER_OF(video) - 1))
        return FALSE;
    machine_text(d->DeviceKey, L"DriverDesc", d->DeviceString, RTL_NUMBER_OF(d->DeviceString));
    PCWSTR start = wcschr(d->DeviceKey, L'{'), end = start ? wcschr(start, L'}') : nullptr;
    if (end && (SIZE_T)(end - start) + 1 < RTL_NUMBER_OF(id))
        memcpy(id, start, (SIZE_T)(end - start + 1) * sizeof(WCHAR));
    if (!adapter_hardware(L"PCI", id, d->DeviceID, nullptr, 0) &&
        !adapter_hardware(L"ROOT", id, d->DeviceID, nullptr, 0))
        machine_text(d->DeviceKey, L"MatchingDeviceId", d->DeviceID, RTL_NUMBER_OF(d->DeviceID));
    UserEnter();
    wcscpy(cache[object].key, d->DeviceKey);
    wcscpy(cache[object].string, d->DeviceString);
    wcscpy(cache[object].hardware, d->DeviceID);
    cache[object].known = TRUE;
    UserLeave();
    return TRUE;
}

/*
 * the monitor of a display, the nth device of the monitor driver: its name, its id, which is its first hardware id
 * with its driver or the interface of its device, and its key in the class of the monitors.
 */
static BOOLEAN monitor_device(ULONG display, BOOLEAN by_interface, DISPLAY_DEVICEW *d)
{
    WCHAR value[12], path[300], instance[128], hardware[128], driver[64];
    _snwprintf(value, RTL_NUMBER_OF(value) - 1, L"%u", display);
    value[RTL_NUMBER_OF(value) - 1] = 0;
    if (!machine_text(MACHINE_KEY L"System\\CurrentControlSet\\Services\\monitor\\Enum", value, instance,
                      RTL_NUMBER_OF(instance)))
        return FALSE;
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, ENUM_KEY L"%ls", instance);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    if (!machine_value(path, L"HardwareID", REG_MULTI_SZ, hardware, RTL_NUMBER_OF(hardware)) ||
        !machine_text(path, L"Driver", driver, RTL_NUMBER_OF(driver)))
        return FALSE;
    _snwprintf(d->DeviceName, RTL_NUMBER_OF(d->DeviceName) - 1, L"\\\\.\\DISPLAY%u\\Monitor0", display + 1);
    _snwprintf(d->DeviceKey, RTL_NUMBER_OF(d->DeviceKey) - 1, CLASS_KEY L"%ls", driver);
    if (!machine_text(d->DeviceKey, L"DriverDesc", d->DeviceString, RTL_NUMBER_OF(d->DeviceString)))
        wcscpy(d->DeviceString, L"Generic PnP Monitor");
    if (by_interface) {
        for (WCHAR *c = instance; *c; c++)
            if (*c == L'\\')
                *c = L'#';
        _snwprintf(d->DeviceID, RTL_NUMBER_OF(d->DeviceID) - 1, L"\\\\?\\%ls#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}", instance);
    } else {
        _snwprintf(d->DeviceID, RTL_NUMBER_OF(d->DeviceID) - 1, L"%ls\\%ls", hardware, driver);
    }
    d->StateFlags = DISPLAY_DEVICE_ATTACHED | DISPLAY_DEVICE_ACTIVE;
    return TRUE;
}

/*
 * one display per host monitor, each with one monitor, all on the first adapter of the video map. the further
 * adapters of the map follow them with a display of their own. measured on windows 11 26100: no device names the
 * adapters by index, a display's name its one monitor at index 0, in any case. every failure is STATUS_UNSUCCESSFUL,
 * a bad pointer too. only the fields the size holds whole are filled, the size comes back cut to them, and the
 * bytes of a field cut short are cleared. an adapter has no id when the interface name is asked.
 */
NTSTATUS NTAPI NtUserEnumDisplayDevices(PUNICODE_STRING device, DWORD index, DISPLAY_DEVICEW *user_info, DWORD flags)
{
    static const DWORD ENDS[] = {sizeof(DWORD), offsetof(DISPLAY_DEVICEW, DeviceString), offsetof(DISPLAY_DEVICEW, StateFlags),
                                 offsetof(DISPLAY_DEVICEW, DeviceID), offsetof(DISPLAY_DEVICEW, DeviceKey), sizeof(DISPLAY_DEVICEW)};
    DWORD cb;
    if (!user_info || !NT_SUCCESS(MimeCopyFromUser(&cb, user_info, sizeof(cb))) || !cb)
        return STATUS_UNSUCCESSFUL;
    DISPLAY_DEVICEW d = {};
    video_read();
    if (!device) {
        MIME_MONITOR_INFO monitor;
        UserEnter();
        ULONG monitors = MonitorCount(), object = 0;
        BOOLEAN found = TRUE;
        if (index < monitors) {
            found = MonitorGet(index, &monitor);
            d.StateFlags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | (index == 0 ? DISPLAY_DEVICE_PRIMARY_DEVICE : 0);
        } else if (index - monitors < g_video.spares) {
            /* the reference machine has the display of its second adapter on the desktop. */
            object = g_video.spare_object[index - monitors];
            d.StateFlags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
        } else {
            found = FALSE;
        }
        UserLeave();
        if (!found || !adapter_device(object, &d))
            return STATUS_UNSUCCESSFUL;
        if (index < monitors)
            memcpy(d.DeviceName, monitor.Device, sizeof(d.DeviceName));
        else
            _snwprintf(d.DeviceName, RTL_NUMBER_OF(d.DeviceName) - 1, L"\\\\.\\DISPLAY%u", index + 1);
        d.DeviceName[RTL_NUMBER_OF(d.DeviceName) - 1] = 0;
        if (flags & EDD_GET_DEVICE_INTERFACE_NAME)
            memset(d.DeviceID, 0, sizeof(d.DeviceID));
    } else {
        ULONG number = device_number(device);
        UserEnter();
        LONG display = numbered_display(number);
        UserLeave();
        if (display < 0 || index != 0 || !monitor_device((ULONG)display, (flags & EDD_GET_DEVICE_INTERFACE_NAME) != 0, &d))
            return STATUS_UNSUCCESSFUL;
    }
    DWORD whole = 0, fields = 0;
    for (ULONG i = 0; i < RTL_NUMBER_OF(ENDS) && cb >= ENDS[i]; i++) {
        whole = ENDS[i];
        fields = i;
    }
    /* what the size does not hold whole stays clear. */
    DISPLAY_DEVICEW out = {};
    if (fields >= 1)
        memcpy(out.DeviceName, d.DeviceName, sizeof(out.DeviceName));
    if (fields >= 2)
        memcpy(out.DeviceString, d.DeviceString, sizeof(out.DeviceString));
    if (fields >= 3)
        out.StateFlags = d.StateFlags;
    if (fields >= 4)
        memcpy(out.DeviceID, d.DeviceID, sizeof(out.DeviceID));
    if (fields >= 5)
        memcpy(out.DeviceKey, d.DeviceKey, sizeof(out.DeviceKey));
    out.cb = whole;
    /* a size under a whole field leaves the bytes after the size alone, and a size of four writes the size alone. */
    SIZE_T length = cb < sizeof(out) ? cb : sizeof(out);
    if (length < sizeof(DWORD))
        return NT_SUCCESS(MimeCopyToUser(user_info, &out, sizeof(DWORD))) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    return NT_SUCCESS(MimeCopyToUser(user_info, &out, length)) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/*
 * ENUM_REGISTRY_SETTINGS is the mode the system starts in. the mode is taken at its full size only, user32 cuts it
 * to the caller's. EDS_ROTATEDMODE lists every mode in its four orientations, the size turned with it. index -3 is
 * not documented and gives the size of the monitor with the rate of the primary display, under no name.
 */
NTSTATUS NTAPI NtUserEnumDisplaySettings(PUNICODE_STRING device, DWORD index, DEVMODEW *user_mode, DWORD flags)
{
    WORD sizes[2];
    if (!user_mode || !NT_SUCCESS(MimeCopyFromUser(sizes, &user_mode->dmSize, sizeof(sizes))))
        return STATUS_ACCESS_VIOLATION;
    if (sizes[0] != MODE_SIZE)
        return STATUS_BUFFER_TOO_SMALL;
    ULONG number = device_number(device);
    video_read();
    UserEnter();
    LONG display = numbered_display(number);
    struct {
        DEVMODEW m;
        UCHAR extra[DRIVER_EXTRA];
    } out = {};
    DISPLAY_MODE mode;
    NTSTATUS status = STATUS_SUCCESS;
    if (number == NUMBER_DISCONNECTED) {
        status = disconnected_mode(index, &out.m);
    } else if (display < 0) {
        status = STATUS_INVALID_PARAMETER_1;
    } else if (index == ENUM_CURRENT_SETTINGS || index == ENUM_REGISTRY_SETTINGS) {
        if (!display_current((ULONG)display, index == ENUM_REGISTRY_SETTINGS, &mode))
            status = STATUS_INVALID_PARAMETER_1;
        else {
            fill(&out.m, &mode, FALSE);
            if (index == ENUM_CURRENT_SETTINGS)
                out.m.dmLogPixels = 96;
        }
    } else if (index == (DWORD)-3) {
        if (!display_current((ULONG)display, TRUE, &mode))
            status = STATUS_INVALID_PARAMETER_1;
        else {
            memset(&out.m, 0, sizeof(out.m));
            out.m.dmSize = sizeof(out.m);
            out.m.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
            BOOLEAN indirect = display_driver((ULONG)display) == DRIVER_INDIRECT;
            out.m.dmPelsWidth = indirect ? 1920 : mode.width;
            out.m.dmPelsHeight = indirect ? 1080 : mode.height;
            out.m.dmDisplayFrequency = primary_hz();
        }
    } else {
        DWORD orientation = (flags & EDS_ROTATEDMODE) ? index % 4 : 0;
        if (!display_mode_at((ULONG)display, (flags & EDS_ROTATEDMODE) ? index / 4 : index, &mode))
            status = STATUS_INVALID_PARAMETER_2;
        else {
            if (orientation & 1) {
                DWORD width = mode.width;
                mode.width = mode.height;
                mode.height = width;
            }
            mode.orientation = orientation;
            fill(&out.m, &mode, TRUE);
        }
    }
    UserLeave();
    if (!NT_SUCCESS(status))
        return status;
    /* the driver has bytes of its own after a mode, and gives as many of them as the caller left room for. */
    out.m.dmDriverExtra = sizes[1] < DRIVER_EXTRA ? sizes[1] : DRIVER_EXTRA;
    return MimeCopyToUser(user_mode, &out, sizeof(out.m) + out.m.dmDriverExtra);
}

/*
 * fields a mode leaves out keep the current values, and a null mode goes back to the registry mode. the native
 * size at 32 bits is not a mode of its own, just the display. the checks come in the order windows makes them:
 * the flags, the size of the mode, the display, then the mode against the list of the display.
 */
LONG NTAPI NtUserChangeDisplaySettings(PUNICODE_STRING device, DEVMODEW *user_mode, DWORD flags, PVOID lparam)
{
    /* the result code for a refusal is a guess. */
    if (W32UiLimited(W32_UILIMIT_DISPLAYSETTINGS))
        return DISP_CHANGE_FAILED;
    /* 0x40, 0x80, 0x400 and 0x04000000 have no name and pass, 0x08000000 has none and fails this way. */
    if (flags & 0x08000000) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return DISP_CHANGE_FAILED;
    }
    if (flags & ~(CDS_UPDATEREGISTRY | CDS_TEST | CDS_FULLSCREEN | CDS_GLOBAL | CDS_SET_PRIMARY | CDS_VIDEOPARAMETERS |
                  0x40 | 0x80 | 0x400 | 0x04000000 | CDS_RESET | CDS_RESET_EX | CDS_NORESET | 0x80000000))
        return DISP_CHANGE_BADFLAGS;
    if ((flags & (CDS_GLOBAL | CDS_NORESET)) && !(flags & CDS_UPDATEREGISTRY))
        return DISP_CHANGE_BADFLAGS;
    if ((flags & CDS_RESET) && (flags & (CDS_RESET_EX | CDS_NORESET)))
        return DISP_CHANGE_BADFLAGS;
    DEVMODEW m = {};
    BOOLEAN reset = !user_mode;
    if (user_mode) {
        WORD size;
        if (!NT_SUCCESS(MimeCopyFromUser(&size, &user_mode->dmSize, sizeof(size))))
            return DISP_CHANGE_BADPARAM;
        if (size < MODE_SIZE_MIN || size > MODE_SIZE)
            return DISP_CHANGE_BADMODE;
        if (!NT_SUCCESS(MimeCopyFromUser(&m, user_mode, size)))
            return DISP_CHANGE_BADPARAM;
    }
    ULONG number = device_number(device);
    video_read();
    UserEnter();
    LONG display = numbered_display(number);
    DISPLAY_MODE current, want;
    if (display < 0 || !display_current((ULONG)display, FALSE, &current)) {
        UserLeave();
        return DISP_CHANGE_BADPARAM;
    }
    if ((flags & CDS_VIDEOPARAMETERS) && !lparam) {
        UserLeave();
        return DISP_CHANGE_BADFLAGS;
    }
    UCHAR driver = display_driver((ULONG)display);
    LONG result = DISP_CHANGE_SUCCESSFUL;
    BOOLEAN any_hz = TRUE;
    want = current;
    if (reset) {
        display_current((ULONG)display, TRUE, &want);
    } else {
        if (m.dmFields & DM_PELSWIDTH)
            want.width = m.dmPelsWidth;
        if (m.dmFields & DM_PELSHEIGHT)
            want.height = m.dmPelsHeight;
        if (m.dmFields & DM_BITSPERPEL)
            want.bits = m.dmBitsPerPel;
        if (m.dmFields & DM_DISPLAYORIENTATION)
            want.orientation = m.dmDisplayOrientation;
        if (m.dmFields & DM_DISPLAYFREQUENCY) {
            want.hz = m.dmDisplayFrequency;
            any_hz = FALSE;
        }
        if (!display_has_mode((ULONG)display, &want, any_hz))
            result = DISP_CHANGE_BADMODE;
        else if ((m.dmFields & DM_DISPLAYFLAGS) && (m.dmDisplayFlags & DM_INTERLACED))
            result = DISP_CHANGE_BADMODE;
        /* the host display scales in the host, the two drivers of the reference machine take the default only. */
        else if (driver != DRIVER_GPU && (m.dmFields & DM_DISPLAYFIXEDOUTPUT) && m.dmDisplayFixedOutput != DMDFO_DEFAULT)
            result = DISP_CHANGE_BADMODE;
        /* the primary display stays at the origin. */
        else if (display == 0 && (m.dmFields & DM_POSITION) && (m.dmPosition.x || m.dmPosition.y))
            result = DISP_CHANGE_FAILED;
    }
    if (result == DISP_CHANGE_SUCCESSFUL && !(flags & CDS_TEST) && !(flags & CDS_NORESET)) {
        if (want.orientation != DMDO_DEFAULT) {
            /* listed like windows lists them, but nothing here turns the picture. */
            result = DISP_CHANGE_FAILED;
        } else if (driver == DRIVER_GPU) {
            DWORD bits = want.bits == 24 ? 32 : want.bits;
            BOOLEAN native = (LONG)want.width == g_native_width && (LONG)want.height == g_native_height && bits == 32;
            HANDLE owner = (flags & CDS_FULLSCREEN) ? PsGetCurrentProcessId() : nullptr;
            set_mode(!native, want.width, want.height, bits, owner);
        } else if (driver == DRIVER_INDIRECT) {
            SIZE_MODE *mode = &g_video.spare_mode[(ULONG)display - MonitorCount()];
            mode->width = want.width;
            mode->height = want.height;
            if (!any_hz)
                mode->hz = want.hz;
        }
    }
    UserLeave();
    return result;
}

/* ------------------------------------------------------------------ display configuration */

/*
 * QueryDisplayConfig: win32k answers a record of 216 bytes for each path, which
 * QdcCompat::ConvertPathModalityToDisplayConfig of user32 26300 turns into the paths and modes a program sees. the
 * fields it reads, from IDA: the flags, the adapter, the source and the target, the video signal of the target, its
 * technology, its scaling for the active paths and for the database, the position and the size of the source, the
 * size of the desktop image, and a status that keeps the path. mime has one path a monitor, all on the adapter of the
 * gpu, source n to target n, as the instances of the monitors end with UIDn. measured on windows 11 26100.
 */
#define QDC_RECORD 216
#define QDC_ACTIVE_PATH (1ull << 63)
#define QDC_SOURCE_IN_USE (1ull << 62)
#define QDC_TARGET_IN_USE (1ull << 61)
#define QDC_TARGET_AVAILABLE (1ull << 56)
#define QDC_DESKTOP_IMAGE 0x800000ull
#define QDC_SCALING_ACTIVE 0x40000000000ull
#define QDC_SCALING_DATABASE 0x10000ull
#define QDC_SOURCE_MODE 0x20000ull
#define QDC_SOURCE_POSITION 0x800ull
#define QDC_VSYNC_REFRESH 0x2ull
#define QDC_ALL_PATHS 0x1u
#define QDC_ONLY_ACTIVE_PATHS 0x2u
#define QDC_DATABASE_CURRENT 0x4u
#define QDC_KNOWN 0x40000077u /* the three kinds, virtual mode aware, hmd, virtual refresh rate aware and 0x40000000. */
#define DISPLAYCONFIG_SCALING_IDENTITY 1
#define DISPLAYCONFIG_SCALING_ASPECTRATIOCENTEREDMAX 4
#define DISPLAYCONFIG_TOPOLOGY_INTERNAL 1
#define DISPLAYCONFIG_TOPOLOGY_EXTEND 4
#define DISPLAYCONFIG_TOPOLOGY_EXTERNAL 8
#define DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15 0
#define DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI 4
#define DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI 5
#define DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL 10
#define DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED 11

/* the luid dxgi gives its first gpu, guest/dxgi/main.c. */
#define QDC_ADAPTER_LUID 0x0000D3D0u

typedef struct QDC_SIGNAL {
    ULONG64 PixelRate;
    ULONG HSyncNumerator, HSyncDenominator;
    ULONG VSyncNumerator, VSyncDenominator;
    ULONG ActiveWidth, ActiveHeight;
    ULONG TotalWidth, TotalHeight;
    ULONG VideoStandard; /* the standard in the low word, the divider of the vertical rate in the next six bits. */
    ULONG ScanLineOrdering;
} QDC_SIGNAL;
MIME_STATIC_ASSERT(sizeof(QDC_SIGNAL) == 48, "DISPLAYCONFIG_VIDEO_SIGNAL_INFO");

#define REC_FLAGS 0
#define REC_ADAPTER 16
#define REC_SOURCE 24
#define REC_TARGET 28
#define REC_SIGNAL 32
#define REC_TECHNOLOGY 80
#define REC_IMAGE_SIZE 88
#define REC_DESKTOP_IMAGE 101
#define REC_SCALING_ACTIVE 108
#define REC_SCALING_DATABASE 112
#define REC_POSITION 116
#define REC_SOURCE_SIZE 124
#define REC_IMAGE_REGION 140

/* the connector the host names a monitor by: eDP-1, DP-2, HDMI-A-1, DVI-D-1, VGA-1. others are displayport. */
static ULONG output_technology(PCWSTR name)
{
    if (!_wcsnicmp(name, L"eDP", 3))
        return DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED;
    if (!_wcsnicmp(name, L"HDMI", 4))
        return DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
    if (!_wcsnicmp(name, L"DVI", 3))
        return DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI;
    if (!_wcsnicmp(name, L"VGA", 3))
        return DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15;
    return DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL;
}

/* the signal of a mode as the virtual display of the reference machine tells it: no blanking, one divider. */
static QDC_SIGNAL signal_of(ULONG width, ULONG height, ULONG hz)
{
    return (QDC_SIGNAL){(ULONG64)width * height * hz, height * hz, 1, hz, 1, width, height, width, height,
                        1u << 16 | 255, 1};
}

static ULONG config_paths(void)
{
    ULONG count = MonitorCount();
    return count < MIME_MAX_MONITORS ? count : MIME_MAX_MONITORS;
}

static void config_record(ULONG display, PUCHAR r)
{
    DISPLAY_MODE mode;
    MIME_MONITOR_INFO info;
    memset(r, 0, QDC_RECORD);
    display_current(display, FALSE, &mode);
    if (!MonitorGet(display, &info))
        memset(&info, 0, sizeof(info));
    *(PULONG64)(r + REC_FLAGS) = QDC_ACTIVE_PATH | QDC_SOURCE_IN_USE | QDC_TARGET_IN_USE | QDC_TARGET_AVAILABLE |
                                 QDC_DESKTOP_IMAGE | QDC_SCALING_ACTIVE | QDC_SCALING_DATABASE | QDC_SOURCE_MODE |
                                 QDC_SOURCE_POSITION | QDC_VSYNC_REFRESH;
    *(PULONG64)(r + REC_ADAPTER) = QDC_ADAPTER_LUID;
    *(PULONG)(r + REC_SOURCE) = display;
    *(PULONG)(r + REC_TARGET) = display;
    QDC_SIGNAL s = signal_of(mode.width, mode.height, mode.hz ? mode.hz : 60);
    memcpy(r + REC_SIGNAL, &s, sizeof(s));
    *(PULONG)(r + REC_TECHNOLOGY) = output_technology(info.Name);
    *(PULONG)(r + REC_IMAGE_SIZE) = mode.width;
    *(PULONG)(r + REC_IMAGE_SIZE + 4) = mode.height;
    r[REC_DESKTOP_IMAGE] = 1;
    *(PULONG)(r + REC_SCALING_ACTIVE) = DISPLAYCONFIG_SCALING_IDENTITY;
    *(PULONG)(r + REC_SCALING_DATABASE) = DISPLAYCONFIG_SCALING_ASPECTRATIOCENTEREDMAX;
    *(PLONG)(r + REC_POSITION) = mode.position.x;
    *(PLONG)(r + REC_POSITION + 4) = mode.position.y;
    *(PULONG)(r + REC_SOURCE_SIZE) = mode.width;
    *(PULONG)(r + REC_SOURCE_SIZE + 4) = mode.height;
    RECT region = {0, 0, (LONG)mode.width, (LONG)mode.height};
    memcpy(r + REC_IMAGE_REGION, &region, sizeof(region));
}

/* the low three bits name one kind. GetDisplayConfigBufferSizes takes any other bit, QueryDisplayConfig does not. */
static BOOLEAN config_kind(ULONG flags)
{
    ULONG kind = flags & 7;
    return kind == QDC_ALL_PATHS || kind == QDC_ONLY_ACTIVE_PATHS || kind == QDC_DATABASE_CURRENT;
}

/* a bad kind still writes 0 paths. */
NTSTATUS NTAPI NtUserGetDisplayConfigBufferSizes(ULONG flags, PUINT user_paths)
{
    UINT count = 0;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    if (config_kind(flags)) {
        video_read();
        UserEnter();
        count = config_paths();
        UserLeave();
        status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(MimeCopyToUser(user_paths, &count, sizeof(count))))
        return STATUS_ACCESS_VIOLATION;
    return status;
}

/*
 * the topology is asked with QDC_DATABASE_CURRENT and refused otherwise. a room of no path is refused, one too small
 * for the paths gives STATUS_BUFFER_TOO_SMALL, ERROR_INSUFFICIENT_BUFFER for the program.
 */
NTSTATUS NTAPI NtUserQueryDisplayConfig(ULONG flags, PUINT user_paths, PVOID user_records, PULONG user_topology,
                                        ULONG reserved)
{
    if (!config_kind(flags) || (flags & ~QDC_KNOWN) || !user_paths || !user_records ||
        ((flags & 7) == QDC_DATABASE_CURRENT) != (user_topology != nullptr))
        return STATUS_INVALID_PARAMETER;
    UINT room;
    if (!NT_SUCCESS(MimeCopyFromUser(&room, user_paths, sizeof(room))))
        return STATUS_ACCESS_VIOLATION;
    if (!room)
        return STATUS_INVALID_PARAMETER;
    UCHAR records[MIME_MAX_MONITORS][QDC_RECORD];
    video_read();
    UserEnter();
    ULONG count = config_paths();
    for (ULONG i = 0; i < count && i < room; i++)
        config_record(i, records[i]);
    /* one monitor is the internal topology when it is a panel, the external one otherwise. */
    MIME_MONITOR_INFO first;
    BOOLEAN internal = MonitorGet(0, &first) &&
                       output_technology(first.Name) == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED;
    UserLeave();
    if (room < count)
        return STATUS_BUFFER_TOO_SMALL;
    if (!NT_SUCCESS(MimeCopyToUser(user_records, records, (SIZE_T)count * QDC_RECORD)) ||
        !NT_SUCCESS(MimeCopyToUser(user_paths, &count, sizeof(count))))
        return STATUS_ACCESS_VIOLATION;
    if (user_topology) {
        ULONG topology = count > 1 ? DISPLAYCONFIG_TOPOLOGY_EXTEND
                         : internal ? DISPLAYCONFIG_TOPOLOGY_INTERNAL
                                    : DISPLAYCONFIG_TOPOLOGY_EXTERNAL;
        if (!NT_SUCCESS(MimeCopyToUser(user_topology, &topology, sizeof(topology))))
            return STATUS_ACCESS_VIOLATION;
    }
    return STATUS_SUCCESS;
}

/* the edid the registry keeps for the monitor of a display, written by ntos/cm/boot.c. */
static BOOLEAN monitor_edid(ULONG display, UCHAR edid[128])
{
    WCHAR value[12], instance[128], path[300];
    _snwprintf(value, RTL_NUMBER_OF(value) - 1, L"%u", display);
    value[RTL_NUMBER_OF(value) - 1] = 0;
    if (!machine_text(MACHINE_KEY L"System\\CurrentControlSet\\Services\\monitor\\Enum", value, instance,
                      RTL_NUMBER_OF(instance)))
        return FALSE;
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, ENUM_KEY L"%ls\\Device Parameters", instance);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    HANDLE key = machine_open(path);
    if (!key)
        return FALSE;
    struct {
        ULONG title, type, size;
        UCHAR data[128];
    } info;
    UNICODE_STRING name;
    ULONG got = 0;
    RtlInitUnicodeString(&name, L"EDID");
    NTSTATUS status = ZwQueryValueKey(key, &name, 2, &info, sizeof(info), &got);
    ZwClose(key);
    if (!NT_SUCCESS(status) || info.type != REG_BINARY || info.size < 128)
        return FALSE;
    memcpy(edid, info.data, 128);
    return TRUE;
}

/* the name a monitor gives in the descriptor 0xfc of its edid. */
static void edid_name(const UCHAR edid[128], WCHAR name[64])
{
    name[0] = 0;
    for (ULONG d = 54; d <= 108; d += 18) {
        const UCHAR *t = edid + d;
        if (t[0] || t[1] || t[2] || t[3] != 0xFC)
            continue;
        ULONG n = 0;
        for (ULONG i = 5; i < 18 && t[i] != 0x0A; i++)
            name[n++] = t[i];
        while (n && name[n - 1] == L' ')
            n--;
        name[n] = 0;
        return;
    }
}

#define DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME 1
#define DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME 2
#define DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE 3
#define DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME 4
#define DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE 6
#define DISPLAYCONFIG_DEVICE_INFO_GET_SUPPORT_VIRTUAL_RESOLUTION 7
#define DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO 9
#define DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL 11

typedef struct QDC_HEADER {
    ULONG Type, Size;
    ULONG AdapterLow;
    LONG AdapterHigh;
    ULONG Id;
} QDC_HEADER;

/*
 * DisplayConfigGetDeviceInfo, measured on windows 11 26100: a size other than the one of the type is
 * STATUS_INVALID_PARAMETER, a header that cannot be read or is shorter than itself STATUS_UNSUCCESSFUL. a source
 * of another adapter or number is STATUS_UNSUCCESSFUL, a target or an adapter STATUS_INVALID_PARAMETER, and so is a
 * type that is not asked for, a setting among them.
 */
/*
 * the name of a monitor is read from six keys of the registry, which a game asks for every frame. it is kept while
 * the monitors stay as they are, see MonitorGeneration.
 */
static struct {
    ULONG Generation;
    BOOLEAN Valid;
    UCHAR Body[420 - sizeof(QDC_HEADER)];
} g_target_names[MIME_MAX_MONITORS];

NTSTATUS NTAPI NtUserDisplayConfigGetDeviceInfo(PVOID user_packet)
{
    QDC_HEADER h;
    if (!user_packet || !NT_SUCCESS(MimeCopyFromUser(&h, user_packet, sizeof(h))) || h.Size < sizeof(h))
        return STATUS_UNSUCCESSFUL;
    static const struct {
        ULONG Type, Size;
    } sizes[] = {
        {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, 84},
        {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, 420},
        {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE, 80},
        {DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME, 276},
        {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE, 24},
        {DISPLAYCONFIG_DEVICE_INFO_GET_SUPPORT_VIRTUAL_RESOLUTION, 24},
        {DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO, 32},
        {DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, 24},
    };
    ULONG want = 0;
    for (ULONG i = 0; i < RTL_NUMBER_OF(sizes); i++)
        if (sizes[i].Type == h.Type)
            want = sizes[i].Size;
    if (!want || h.Size != want)
        return STATUS_INVALID_PARAMETER;
    BOOLEAN ours = h.AdapterLow == QDC_ADAPTER_LUID && !h.AdapterHigh;
    video_read();
    UserEnter();
    ULONG count = config_paths();
    DISPLAY_MODE native;
    MIME_MONITOR_INFO info;
    BOOLEAN known = ours && h.Id < count && display_current(h.Id, TRUE, &native) && MonitorGet(h.Id, &info);
    ULONG generation = MonitorGeneration();
    UCHAR packet[420] = {};
    BOOLEAN cached = known && h.Type == DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME && g_target_names[h.Id].Valid &&
                     g_target_names[h.Id].Generation == generation;
    if (cached)
        memcpy(packet + sizeof(h), g_target_names[h.Id].Body, sizeof(g_target_names[h.Id].Body));
    UserLeave();
    if (h.Type == DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME && !known)
        return STATUS_UNSUCCESSFUL;
    if (h.Type == DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME ? !ours : !known)
        return STATUS_INVALID_PARAMETER;
    memcpy(packet, &h, sizeof(h));
    PUCHAR body = packet + sizeof(h);
    if (cached)
        return NT_SUCCESS(MimeCopyToUser(user_packet, packet, want)) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    switch (h.Type) {
    case DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME:
        info.Device[31] = 0;
        memcpy(body, info.Device, (wcslen(info.Device) + 1) * sizeof(WCHAR));
        break;
    case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME: {
        /* the name and the ids come from the edid: friendlyNameFromEdid and edidIdsValid. */
        UCHAR edid[128];
        DISPLAY_DEVICEW d = {};
        BOOLEAN have = monitor_edid(h.Id, edid);
        *(PULONG)body = have ? 5 : 0;
        *(PULONG)(body + 4) = output_technology(info.Name);
        if (have) {
            *(PUSHORT)(body + 8) = (USHORT)(edid[8] | edid[9] << 8);
            *(PUSHORT)(body + 10) = (USHORT)(edid[10] | edid[11] << 8);
            edid_name(edid, (PWCHAR)(body + 16));
        }
        if (monitor_device(h.Id, TRUE, &d))
            memcpy(body + 16 + 128, d.DeviceID, sizeof(d.DeviceID));
        UserEnter();
        memcpy(g_target_names[h.Id].Body, body, sizeof(g_target_names[h.Id].Body));
        g_target_names[h.Id].Generation = generation;
        g_target_names[h.Id].Valid = TRUE;
        UserLeave();
        break;
    }
    case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE: {
        /* the width and the height, then the target mode at the next eight bytes. */
        *(PULONG)body = native.width;
        *(PULONG)(body + 4) = native.height;
        QDC_SIGNAL s = signal_of(native.width, native.height, native.hz ? native.hz : 60);
        memcpy(packet + 32, &s, sizeof(s));
        break;
    }
    case DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME: {
        WCHAR id[64] = {}, key[128], hardware[128], instance[200];
        static const WCHAR video[] = MACHINE_KEY L"System\\CurrentControlSet\\Control\\Video\\";
        if (!machine_text(MACHINE_KEY L"HARDWARE\\DEVICEMAP\\VIDEO", L"\\Device\\Video0", key, RTL_NUMBER_OF(key)) ||
            _wcsnicmp(key, video, RTL_NUMBER_OF(video) - 1))
            return STATUS_INVALID_PARAMETER;
        PCWSTR start = wcschr(key, L'{'), end = start ? wcschr(start, L'}') : nullptr;
        if (end && (SIZE_T)(end - start) + 1 < RTL_NUMBER_OF(id))
            memcpy(id, start, (SIZE_T)(end - start + 1) * sizeof(WCHAR));
        if (!adapter_hardware(L"PCI", id, hardware, instance, RTL_NUMBER_OF(instance)) &&
            !adapter_hardware(L"ROOT", id, hardware, instance, RTL_NUMBER_OF(instance)))
            return STATUS_INVALID_PARAMETER;
        /* the interface of the adapter: \\?\ and the instance with # for \, then GUID_DISPLAY_DEVICE_ARRIVAL. */
        for (WCHAR *c = instance; *c; c++)
            if (*c == L'\\')
                *c = L'#';
        _snwprintf((PWCHAR)body, 127, L"\\\\?\\%ls#{5b45201d-f2f2-4f3b-85bb-30ff1f953599}", instance);
        break;
    }
    case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE:
        *(PULONG)body = output_technology(info.Name);
        break;
    case DISPLAYCONFIG_DEVICE_INFO_GET_SUPPORT_VIRTUAL_RESOLUTION:
        break;
    case DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO: {
        /* advancedColorSupported and advancedColorEnabled while the host shows high dynamic range. */
        BOOLEAN hdr = (info.Flags & MIME_MONITORINFOF_HDR) != 0;
        *(PULONG)body = hdr ? 3 : 0;
        *(PULONG)(body + 8) = hdr ? 10 : 8;
        break;
    }
    case DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL: {
        /*
         * in thousandths of 80 nits: 80 nits, or where the host shows the white of standard content while the monitor
         * is in high dynamic range, which is where programs then draw their white in scRGB.
         */
        ULONG white = 1000;
        if (info.Flags & MIME_MONITORINFOF_HDR)
            white = (ULONG)((ULONG64)(info.Color.WhiteNits ? info.Color.WhiteNits : 2030000) / 800);
        *(PULONG)body = white;
        break;
    }
    }
    return NT_SUCCESS(MimeCopyToUser(user_packet, packet, want)) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/* ------------------------------------------------------------------ rotation */

/*
 * CRotationMgr::QueryAutoRotationState with no sensor, rotation not turned off and a local session: AR_NOSENSOR, as
 * a desktop of windows 11 answers. a null pointer fails with ERROR_INVALID_PARAMETER.
 */
#define AR_NOSENSOR 0x10

BOOL NTAPI NtUserGetAutoRotationState(PDWORD user_state)
{
    DWORD state = AR_NOSENSOR;
    if (!user_state || !NT_SUCCESS(MimeCopyToUser(user_state, &state, sizeof(state)))) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

/* the preference of the calling process, 0 until it sets one. */
BOOL NTAPI NtUserGetDisplayAutoRotationPreferences(PDWORD user_preference)
{
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    DWORD preference = p ? p->RotationPreference : 0;
    UserLeave();
    if (!NT_SUCCESS(MimeCopyToUser(user_preference, &preference, sizeof(preference)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    return TRUE;
}

/* any of the four orientations together, bits 4 to 29 refused with ERROR_INVALID_PARAMETER. nothing turns here. */
BOOL NTAPI NtUserSetDisplayAutoRotationPreferences(DWORD preference)
{
    if (preference & 0x3FFFFFF0) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    if (p)
        p->RotationPreference = preference;
    UserLeave();
    return TRUE;
}

/* ------------------------------------------------------------------ gamma */

/*
 * gamma composes like on windows: the gdi ramp belongs to the session and outlives its process, and a fullscreen
 * program's device ramp replaces it until the program drops it or ends. the host applies the result.
 */
#define RAMP_VALUES (3 * 256)

static USHORT g_gdi_ramp[RAMP_VALUES];
static BOOLEAN g_gdi_ramp_set;
static USHORT g_device_ramp[RAMP_VALUES];
static HANDLE g_device_owner;
static USHORT *g_host_ramp; /* contiguous, as the host reads it. */

static void identity_ramp(USHORT *ramp)
{
    for (ULONG c = 0; c < 3; c++)
        for (ULONG i = 0; i < 256; i++)
            ramp[c * 256 + i] = (USHORT)(i * 257);
}

static void apply_gamma(void)
{
    const USHORT *ramp = g_device_owner ? g_device_ramp : g_gdi_ramp_set ? g_gdi_ramp : nullptr;
    if (ramp && !g_host_ramp) {
        PHYSICAL_ADDRESS highest;
        highest.QuadPart = -1;
        g_host_ramp = MmAllocateContiguousMemory(RAMP_VALUES * sizeof(USHORT), highest);
    }
    ULONG64 pa = 0;
    if (ramp && g_host_ramp) {
        memcpy(g_host_ramp, ramp, RAMP_VALUES * sizeof(USHORT));
        pa = (ULONG64)MmGetPhysicalAddress(g_host_ramp).QuadPart;
    }
    HalMimeHypercall(MIME_HC_GAMMA, pa, 0, 0, 0, 0, 0, nullptr);
}

static void gamma_process_exited(HANDLE pid)
{
    if (g_device_owner && g_device_owner == pid) {
        g_device_owner = nullptr;
        apply_gamma();
    }
}

NTSTATUS NTAPI NtGdiMimeGamma(ULONG op, PUSHORT user_ramp)
{
    USHORT ramp[RAMP_VALUES];
    NTSTATUS status;
    if (op == MIME_GAMMA_GET) {
        UserEnter();
        if (g_gdi_ramp_set)
            memcpy(ramp, g_gdi_ramp, sizeof(ramp));
        else
            identity_ramp(ramp);
        UserLeave();
        return MimeCopyToUser(user_ramp, ramp, sizeof(ramp));
    }
    if (op != MIME_GAMMA_SET && op != MIME_GAMMA_DEVICE)
        return STATUS_INVALID_PARAMETER;
    if (op == MIME_GAMMA_SET && !user_ramp)
        return STATUS_INVALID_PARAMETER;
    if (user_ramp && !NT_SUCCESS(status = MimeCopyFromUser(ramp, user_ramp, sizeof(ramp))))
        return status;
    UserEnter();
    HANDLE pid = PsGetCurrentProcessId();
    if (op == MIME_GAMMA_SET) {
        memcpy(g_gdi_ramp, ramp, sizeof(ramp));
        g_gdi_ramp_set = TRUE;
        apply_gamma();
    } else if (user_ramp) {
        memcpy(g_device_ramp, ramp, sizeof(ramp));
        g_device_owner = pid;
        apply_gamma();
    } else {
        gamma_process_exited(pid);
    }
    UserLeave();
    return STATUS_SUCCESS;
}
