/*
 * display modes. the host's display is the one real mode; a program may
 * change to the others windows lists, smaller sizes and 8, 16 or 32 bits,
 * which then become the screen windows see. the host shows a fullscreen
 * window of a smaller mode scaled to its screen, as a monitor would, and
 * drawing stays 32 bits. a mode set with CDS_FULLSCREEN goes back when the
 * process that set it ends, as windows does. the gamma ramps are kept
 * here too.
 */
#include "win32k.h"
#include <mime/win32/winspool.h>
#include <mime/display_modes.h>

static struct {
    BOOLEAN active;
    DWORD width, height, bits;
    HANDLE owner; /* the process a temporary mode goes back with */
} g_mode;

static LONG g_native_width, g_native_height;

/* the modes in windows' order: by bits, then width, then height; each size up to the native one. */
static BOOLEAN mode_at(DWORD index, DWORD *width, DWORD *height, DWORD *bits)
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
                *width = w;
                *height = h;
                *bits = BITS[b];
                return TRUE;
            }
        }
    }
    return FALSE;
}

static BOOLEAN mode_exists(DWORD width, DWORD height, DWORD bits)
{
    DWORD w, h, b;
    for (DWORD i = 0; mode_at(i, &w, &h, &b); i++)
        if (w == width && h == height && b == bits)
            return TRUE;
    return FALSE;
}

/* W32Display holds the host's display: the mode set replaces its size, its work area the whole screen. */
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

/* the monitor a device name is of, the primary one for none; -1 for a name that is no monitor's. */
static LONG device_monitor(PUNICODE_STRING user_device)
{
    PWSTR device;
    SIZE_T chars;
    if (!NT_SUCCESS(W32CaptureString(user_device, &device, &chars)))
        return -1;
    LONG index = -1;
    if (!device || !chars)
        index = 0;
    else if (chars < 32)
        index = MonitorFromDeviceName(device);
    W32Free(device);
    return index;
}

static void fill(DEVMODEW *m, LONG monitor, DWORD width, DWORD height, DWORD bits)
{
    MIME_MONITOR_INFO info;
    memset(m, 0, sizeof(*m));
    if (MonitorGet((ULONG)monitor, &info))
        memcpy(m->dmDeviceName, info.Device, sizeof(m->dmDeviceName) < sizeof(info.Device) ? sizeof(m->dmDeviceName) : sizeof(info.Device));
    m->dmSpecVersion = DM_SPECVERSION;
    m->dmSize = sizeof(*m);
    m->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY | DM_POSITION |
                  DM_DISPLAYORIENTATION | DM_DISPLAYFIXEDOUTPUT;
    m->dmBitsPerPel = bits;
    m->dmPelsWidth = width;
    m->dmPelsHeight = height;
    m->dmDisplayFrequency = info.RefreshHz ? info.RefreshHz : W32Display.RefreshHz;
    m->dmPosition = (POINTL){info.Rect.left, info.Rect.top};
}

/*
 * a mode by index, ENUM_CURRENT_SETTINGS for the one set now, or
 * ENUM_REGISTRY_SETTINGS for the one the system starts in. what is
 * written stops at the caller's dmSize.
 */
NTSTATUS NTAPI NtUserEnumDisplaySettings(PUNICODE_STRING device, DWORD index, DEVMODEW *user_mode, DWORD flags)
{
    WORD size;
    if (!user_mode || !NT_SUCCESS(MimeCopyFromUser(&size, &user_mode->dmSize, sizeof(size))))
        return STATUS_INVALID_PARAMETER;
    LONG monitor = device_monitor(device);
    if (size < offsetof(DEVMODEW, dmICMMethod) || monitor < 0)
        return STATUS_INVALID_PARAMETER;
    UserEnter();
    DEVMODEW m;
    NTSTATUS status = STATUS_SUCCESS;
    DWORD width, height, bits;
    MIME_MONITOR_INFO info;
    if (!MonitorGet((ULONG)monitor, &info)) {
        status = STATUS_INVALID_PARAMETER;
    } else if (monitor > 0) {
        /* the other monitors have the mode they have, their own size at 32 bits. */
        if (index == ENUM_CURRENT_SETTINGS || index == ENUM_REGISTRY_SETTINGS || index == 0)
            fill(&m, monitor, (DWORD)(info.Rect.right - info.Rect.left), (DWORD)(info.Rect.bottom - info.Rect.top), 32);
        else
            status = STATUS_INVALID_PARAMETER;
    } else if (index == ENUM_CURRENT_SETTINGS)
        fill(&m, 0, (DWORD)W32Display.Width, (DWORD)W32Display.Height, W32Display.Bits);
    else if (index == ENUM_REGISTRY_SETTINGS)
        fill(&m, 0, (DWORD)g_native_width, (DWORD)g_native_height, 32);
    else if (mode_at(index, &width, &height, &bits))
        fill(&m, 0, width, height, bits);
    else
        status = STATUS_INVALID_PARAMETER;
    UserLeave();
    if (!NT_SUCCESS(status))
        return status;
    m.dmSize = size < sizeof(m) ? size : sizeof(m);
    return MimeCopyToUser(user_mode, &m, m.dmSize);
}

/*
 * windows' ChangeDisplaySettingsEx. fields a mode leaves out keep the
 * current mode's; a null mode goes back to the registry's. the native
 * size at 32 bits is no mode of its own, only the display.
 */
LONG NTAPI NtUserChangeDisplaySettings(PUNICODE_STRING device, DEVMODEW *user_mode, DWORD flags, PVOID lparam)
{
    if (flags & ~(CDS_UPDATEREGISTRY | CDS_TEST | CDS_FULLSCREEN | CDS_GLOBAL | CDS_SET_PRIMARY | CDS_VIDEOPARAMETERS |
                  CDS_ENABLE_UNSAFE_MODES | CDS_DISABLE_UNSAFE_MODES | CDS_RESET | CDS_RESET_EX | CDS_NORESET))
        return DISP_CHANGE_BADFLAGS;
    if ((flags & CDS_GLOBAL) && !(flags & CDS_UPDATEREGISTRY))
        return DISP_CHANGE_BADFLAGS;
    LONG monitor = device_monitor(device);
    if (monitor < 0)
        return DISP_CHANGE_BADPARAM;
    DEVMODEW m = {};
    BOOLEAN reset = !user_mode;
    if (user_mode) {
        WORD size;
        if (!NT_SUCCESS(MimeCopyFromUser(&size, &user_mode->dmSize, sizeof(size))))
            return DISP_CHANGE_BADPARAM;
        if (size < offsetof(DEVMODEW, dmICMMethod))
            return DISP_CHANGE_BADMODE;
        if (!NT_SUCCESS(MimeCopyFromUser(&m, user_mode, size < sizeof(m) ? size : sizeof(m))))
            return DISP_CHANGE_BADPARAM;
    }
    UserEnter();
    LONG result = DISP_CHANGE_SUCCESSFUL;
    if (monitor > 0) {
        /* the other monitors keep their mode: it is only the one they have that is accepted. */
        MIME_MONITOR_INFO info;
        if (!reset && MonitorGet((ULONG)monitor, &info) &&
            (((m.dmFields & DM_PELSWIDTH) && m.dmPelsWidth != (DWORD)(info.Rect.right - info.Rect.left)) ||
             ((m.dmFields & DM_PELSHEIGHT) && m.dmPelsHeight != (DWORD)(info.Rect.bottom - info.Rect.top)) ||
             ((m.dmFields & DM_BITSPERPEL) && m.dmBitsPerPel != 32)))
            result = DISP_CHANGE_BADMODE;
        UserLeave();
        return result;
    }
    DWORD width = (DWORD)W32Display.Width, height = (DWORD)W32Display.Height, bits = W32Display.Bits;
    if (reset) {
        width = (DWORD)g_native_width;
        height = (DWORD)g_native_height;
        bits = 32;
    } else {
        if (m.dmFields & DM_PELSWIDTH)
            width = m.dmPelsWidth;
        if (m.dmFields & DM_PELSHEIGHT)
            height = m.dmPelsHeight;
        if (m.dmFields & DM_BITSPERPEL)
            bits = m.dmBitsPerPel;
        if ((m.dmFields & DM_DISPLAYFREQUENCY) && m.dmDisplayFrequency > 1 &&
            m.dmDisplayFrequency != W32Display.RefreshHz)
            result = DISP_CHANGE_BADMODE;
        if (!mode_exists(width, height, bits))
            result = DISP_CHANGE_BADMODE;
    }
    if (result == DISP_CHANGE_SUCCESSFUL && !(flags & CDS_TEST) && !(flags & CDS_NORESET)) {
        BOOLEAN native = (LONG)width == g_native_width && (LONG)height == g_native_height && bits == 32;
        HANDLE owner = (flags & CDS_FULLSCREEN) ? PsGetCurrentProcessId() : nullptr;
        set_mode(!native, width, height, bits, owner);
    }
    UserLeave();
    (void)lparam;
    return result;
}

/* ------------------------------------------------------------------ gamma */

/*
 * the display's gamma as windows composes it: gdi's ramp belongs to the
 * session and stays when its process ends, and a fullscreen program's
 * device ramp replaces it until the program drops it or ends. the host
 * shows everything through the result.
 */
#define RAMP_VALUES (3 * 256)

static USHORT g_gdi_ramp[RAMP_VALUES];
static BOOLEAN g_gdi_ramp_set;
static USHORT g_device_ramp[RAMP_VALUES];
static HANDLE g_device_owner;
static USHORT *g_host_ramp; /* contiguous, as the host reads it */

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
