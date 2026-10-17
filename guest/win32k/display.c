/*
 * display modes. the host's display is the one real mode; a program may
 * change to the others windows lists, smaller sizes and 8, 16 or 32 bits,
 * which then become the screen windows see. the host shows a fullscreen
 * window of a smaller mode scaled to its screen, as a monitor would, and
 * drawing stays 32 bits. a mode set with CDS_FULLSCREEN goes back when the
 * process that set it ends, as windows does.
 */
#include "win32k.h"
#include <mime/win32/winspool.h>
#include <mime/display_modes.h>

static const WCHAR DISPLAY_NAME[] = L"\\\\.\\DISPLAY1";

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

void DisplayProcessExited(HANDLE pid)
{
    if (g_mode.active && g_mode.owner && g_mode.owner == pid)
        set_mode(FALSE, 0, 0, 0, nullptr);
}

static BOOLEAN our_device(PUNICODE_STRING user_device)
{
    PWSTR device;
    SIZE_T chars;
    if (!NT_SUCCESS(W32CaptureString(user_device, &device, &chars)))
        return FALSE;
    BOOLEAN ok = !device || !chars || (chars == RTL_NUMBER_OF(DISPLAY_NAME) - 1 && !_wcsicmp(device, DISPLAY_NAME));
    W32Free(device);
    return ok;
}

static void fill(DEVMODEW *m, DWORD width, DWORD height, DWORD bits)
{
    memset(m, 0, sizeof(*m));
    memcpy(m->dmDeviceName, DISPLAY_NAME, sizeof(DISPLAY_NAME));
    m->dmSpecVersion = DM_SPECVERSION;
    m->dmSize = sizeof(*m);
    m->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFLAGS | DM_DISPLAYFREQUENCY | DM_POSITION |
                  DM_DISPLAYORIENTATION | DM_DISPLAYFIXEDOUTPUT;
    m->dmBitsPerPel = bits;
    m->dmPelsWidth = width;
    m->dmPelsHeight = height;
    m->dmDisplayFrequency = W32Display.RefreshHz;
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
    if (size < offsetof(DEVMODEW, dmICMMethod) || !our_device(device))
        return STATUS_INVALID_PARAMETER;
    UserEnter();
    DEVMODEW m;
    NTSTATUS status = STATUS_SUCCESS;
    DWORD width, height, bits;
    if (index == ENUM_CURRENT_SETTINGS)
        fill(&m, (DWORD)W32Display.Width, (DWORD)W32Display.Height, W32Display.Bits);
    else if (index == ENUM_REGISTRY_SETTINGS)
        fill(&m, (DWORD)g_native_width, (DWORD)g_native_height, 32);
    else if (mode_at(index, &width, &height, &bits))
        fill(&m, width, height, bits);
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
    if (!our_device(device))
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
