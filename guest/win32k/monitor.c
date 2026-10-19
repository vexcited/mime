/*
 * the host's monitors. the host lays them out in guest pixels as one virtual screen with the primary at 0, 0, each
 * as many pixels as the display has device pixels, so a monitor's dpi is 96 times its scale. a program sees them
 * as windows' monitors: handles from MonitorHandle, \\.\DISPLAYn devices, and per monitor dpi for the programs
 * that are aware of it. a window is on the monitor most of it covers.
 */
#include "win32k.h"

_Static_assert(MIME_USER_MAX_MONITORS == MIME_MAX_MONITORS, "the monitors user mode can ask for");

static MIME_MONITOR_INFO g_monitors[MIME_MAX_MONITORS];
static ULONG g_count;
static MimeMonitor *g_page;
static BOOLEAN g_changed;

/* \\.\DISPLAYn of a monitor, n counting from 1. */
static void device_name(ULONG index, WCHAR name[32])
{
    static const WCHAR prefix[] = L"\\\\.\\DISPLAY";
    memcpy(name, prefix, sizeof(prefix));
    ULONG n = index + 1, digits = 0;
    WCHAR text[12];
    do {
        text[digits++] = (WCHAR)(L'0' + n % 10);
        n /= 10;
    } while (n);
    ULONG at = RTL_NUMBER_OF(prefix) - 1;
    while (digits)
        name[at++] = text[--digits];
    name[at] = 0;
}

/* the primary one is what the boot information says too, and the display mode of a program replaces its size. */
static void primary(MIME_MONITOR_INFO *m)
{
    m->Rect = (RECT){0, 0, W32Display.Width, W32Display.Height};
    m->Work = W32Display.Work;
    m->Flags |= MONITORINFOF_PRIMARY;
    if (!m->Dpi)
        m->Dpi = W32Display.Dpi ? W32Display.Dpi : 96;
    m->RefreshHz = W32Display.RefreshHz;
}

void MonitorsRefresh(void)
{
    MIME_MONITOR_INFO before[MIME_MAX_MONITORS];
    ULONG before_count = g_count;
    memcpy(before, g_monitors, sizeof(before));
    ULONG count = 0;
    if (!g_page)
        g_page = MmAllocateContiguousPages(4096);
    if (g_page) {
        ULONG64 value = 0;
        memset(g_page, 0, MIME_MAX_MONITORS * sizeof(MimeMonitor));
        if (HalMimeHypercall(MIME_HC_MONITORS, (ULONG64)MmGetPhysicalAddress(g_page).QuadPart, MIME_MAX_MONITORS, 0, 0,
                             0, 0, &value) == MIME_HC_STATUS_OK &&
            value > 0 && value <= MIME_MAX_MONITORS)
            count = (ULONG)value;
    }
    memset(g_monitors, 0, sizeof(g_monitors));
    for (ULONG i = 0; i < count; i++) {
        const MimeMonitor *h = &g_page[i];
        MIME_MONITOR_INFO *m = &g_monitors[i];
        m->Flags = (h->flags & MIME_MONITOR_PRIMARY) ? MONITORINFOF_PRIMARY : 0;
        m->Dpi = h->dpi;
        m->RefreshHz = h->refresh_hz;
        m->Rect = (RECT){h->rect.left, h->rect.top, h->rect.right, h->rect.bottom};
        m->Work = (RECT){h->work.left, h->work.top, h->work.right, h->work.bottom};
        for (ULONG c = 0; c < 31 && h->name[c]; c++)
            m->Name[c] = h->name[c];
        device_name(i, m->Device);
    }
    /* a host that knows one display only. */
    if (!count)
        count = 1;
    g_count = count;
    device_name(0, g_monitors[0].Device);
    primary(&g_monitors[0]);
    if (before_count != g_count || memcmp(before, g_monitors, sizeof(before)))
        g_changed = TRUE;
}

/* whether the monitors differ from what they were when this was last asked. */
BOOLEAN MonitorsTakeChanged(void)
{
    BOOLEAN changed = g_changed;
    g_changed = FALSE;
    return changed;
}

ULONG MonitorCount(void)
{
    if (!g_count)
        MonitorsRefresh();
    return g_count;
}

/* a monitor as it is now; the primary follows the display mode. */
BOOLEAN MonitorGet(ULONG index, MIME_MONITOR_INFO *out)
{
    if (index >= MonitorCount())
        return FALSE;
    *out = g_monitors[index];
    if (index == 0)
        primary(out);
    return TRUE;
}

static LONG64 overlap(const RECT *a, const RECT *b)
{
    LONG w = W32Min(a->right, b->right) - W32Max(a->left, b->left);
    LONG h = W32Min(a->bottom, b->bottom) - W32Max(a->top, b->top);
    return w > 0 && h > 0 ? (LONG64)w * h : 0;
}

static LONG64 distance(const RECT *r, const RECT *m)
{
    LONG dx = r->right <= m->left ? m->left - r->right : r->left >= m->right ? r->left - m->right + 1 : 0;
    LONG dy = r->bottom <= m->top ? m->top - r->bottom : r->top >= m->bottom ? r->top - m->bottom + 1 : 0;
    return (LONG64)dx * dx + (LONG64)dy * dy;
}

/* the monitor most of a rectangle is on, a point counting as inside the monitor it is in; else the nearest one. */
LONG MonitorFromRectIndex(const RECT *rect, BOOLEAN nearest)
{
    MIME_MONITOR_INFO m;
    LONG best = -1;
    LONG64 best_area = 0;
    for (ULONG i = 0; MonitorGet(i, &m); i++) {
        LONG64 area = overlap(rect, &m.Rect);
        if (rect->left == rect->right && rect->top == rect->bottom && rect->left >= m.Rect.left &&
            rect->left < m.Rect.right && rect->top >= m.Rect.top && rect->top < m.Rect.bottom)
            area = 1;
        if (area > best_area) {
            best_area = area;
            best = (LONG)i;
        }
    }
    if (best >= 0 || !nearest)
        return best;
    LONG64 best_distance = -1;
    for (ULONG i = 0; MonitorGet(i, &m); i++) {
        LONG64 d = distance(rect, &m.Rect);
        if (best_distance < 0 || d < best_distance) {
            best_distance = d;
            best = (LONG)i;
        }
    }
    return best;
}

RECT MonitorWorkOfRect(const RECT *rect)
{
    MIME_MONITOR_INFO m;
    LONG index = MonitorFromRectIndex(rect, TRUE);
    if (index < 0 || !MonitorGet((ULONG)index, &m))
        return W32Display.Work;
    return m.Work;
}

UINT MonitorDpiOfRect(const RECT *rect)
{
    MIME_MONITOR_INFO m;
    LONG index = MonitorFromRectIndex(rect, TRUE);
    if (index < 0 || !MonitorGet((ULONG)index, &m))
        return W32SystemDpi();
    return m.Dpi ? m.Dpi : 96;
}

/* the virtual screen: where all of the monitors are. */
RECT MonitorVirtualScreen(void)
{
    MIME_MONITOR_INFO m;
    RECT all = {0, 0, W32Display.Width, W32Display.Height};
    for (ULONG i = 1; MonitorGet(i, &m); i++) {
        all.left = W32Min(all.left, m.Rect.left);
        all.top = W32Min(all.top, m.Rect.top);
        all.right = W32Max(all.right, m.Rect.right);
        all.bottom = W32Max(all.bottom, m.Rect.bottom);
    }
    return all;
}

/* whether a rectangle covers a whole monitor, as a program's own fullscreen window does. */
BOOLEAN MonitorCoveredBy(const RECT *rect)
{
    MIME_MONITOR_INFO m;
    for (ULONG i = 0; MonitorGet(i, &m); i++)
        if (rect->left <= m.Rect.left && rect->top <= m.Rect.top && rect->right >= m.Rect.right &&
            rect->bottom >= m.Rect.bottom)
            return TRUE;
    return FALSE;
}

/* the monitor of a device name, -1 for none. */
LONG MonitorFromDeviceName(PCWSTR name)
{
    for (ULONG i = 0; i < MonitorCount(); i++) {
        WCHAR device[32];
        device_name(i, device);
        if (!_wcsicmp(name, device))
            return (LONG)i;
    }
    return -1;
}

HMONITOR MonitorHandle(ULONG index)
{
    return (HMONITOR)(ULONG_PTR)(0x00010001 + index);
}

LONG MonitorIndexOfHandle(HMONITOR monitor)
{
    ULONG_PTR h = (ULONG_PTR)monitor;
    if (h < 0x00010001 || h - 0x00010001 >= MonitorCount())
        return -1;
    return (LONG)(h - 0x00010001);
}

/* NtUserCallTwoParam's monitor requests. */
ULONG_PTR MonitorCall(ULONG_PTR param1, ULONG_PTR param2, ULONG routine)
{
    MIME_MONITOR_INFO list[MIME_MAX_MONITORS];
    MIME_MONITOR_INFO m;
    ULONG count = MonitorCount();
    switch (routine) {
    case MIME_TWOPARAM_GETMONITORINFO: {
        LONG index = MonitorIndexOfHandle((HMONITOR)param1);
        MONITORINFOEXW mi;
        if (index < 0 || !MonitorGet((ULONG)index, &m))
            return 0;
        if (!NT_SUCCESS(MimeCopyFromUser(&mi.cbSize, (PVOID)param2, sizeof(DWORD))))
            return 0;
        if (mi.cbSize != sizeof(MONITORINFO) && mi.cbSize != sizeof(MONITORINFOEXW))
            return 0;
        mi.rcMonitor = m.Rect;
        mi.rcWork = m.Work;
        mi.dwFlags = m.Flags;
        memcpy(mi.szDevice, m.Device, sizeof(mi.szDevice));
        return NT_SUCCESS(MimeCopyToUser((PVOID)param2, &mi, mi.cbSize));
    }
    case MIME_TWOPARAM_GETMONITORS:
        if (count > param2)
            count = (ULONG)param2;
        for (ULONG i = 0; i < count; i++)
            MonitorGet(i, &list[i]);
        if (count && !NT_SUCCESS(MimeCopyToUser((PVOID)param1, list, count * sizeof(list[0]))))
            return 0;
        return count;
    }
    return 0;
}
