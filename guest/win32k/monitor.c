/*
 * host monitors use device pixels in a virtual screen with the primary at 0, 0. DPI is 96 times the scale. programs
 * see MonitorHandle handles and display devices; DPI-aware programs get per-monitor DPI. a window belongs to the
 * monitor it overlaps most.
 */
#include "win32k.h"

_Static_assert(MIME_USER_MAX_MONITORS == MIME_MAX_MONITORS, "the monitors user mode can ask for");

static MIME_MONITOR_INFO g_monitors[MIME_MAX_MONITORS];
static ULONG g_count;
static MimeMonitor *g_page;
static BOOLEAN g_changed;
static ULONG g_generation;
static USHORT g_uniq[MIME_MAX_MONITORS]; /* the high word of a monitor's handle, new when another monitor takes its place. */
static ULONG g_mm[MIME_MAX_MONITORS][2];  /* the size of each in millimetres, 0 when the host does not know it. */

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

/* the primary monitor matches boot information; an application display mode overrides its size. */
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
    memset(g_mm, 0, sizeof(g_mm));
    for (ULONG i = 0; i < count; i++) {
        const MimeMonitor *h = &g_page[i];
        MIME_MONITOR_INFO *m = &g_monitors[i];
        g_mm[i][0] = h->width_mm;
        g_mm[i][1] = h->height_mm;
        m->Flags = (h->flags & MIME_MONITOR_PRIMARY) ? MONITORINFOF_PRIMARY : 0;
        if (h->flags & MIME_MONITOR_HDR)
            m->Flags |= MIME_MONITORINFOF_HDR;
        _Static_assert(sizeof(m->Color) == sizeof(h->color), "the color of a monitor");
        memcpy(&m->Color, &h->color, sizeof(m->Color));
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
    /* a monitor keeps its handle while the host shows the same one there, as win32k keeps a monitor of the same hdev. */
    for (ULONG i = 0; i < count; i++)
        if (!g_uniq[i] || i >= before_count || wcscmp(before[i].Name, g_monitors[i].Name))
            g_uniq[i] = (USHORT)(g_uniq[i] % 0x7ffe + 1);
    if (before_count != g_count || memcmp(before, g_monitors, sizeof(before))) {
        g_changed = TRUE;
        g_generation++;
    }
    UserSharedMonitors();
    GdiSharedDisplayCaps();
}

/* counts the changes of the monitors, so what was worked out from them can be kept. */
ULONG MonitorGeneration(void)
{
    return g_generation;
}

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

/* the primary monitor follows the display mode. */
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

/* choose the largest overlap, or the nearest monitor if there is none. a point belongs to its containing monitor. */
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

BOOLEAN MonitorCoveredBy(const RECT *rect)
{
    MIME_MONITOR_INFO m;
    for (ULONG i = 0; MonitorGet(i, &m); i++)
        if (rect->left <= m.Rect.left && rect->top <= m.Rect.top && rect->right >= m.Rect.right &&
            rect->bottom >= m.Rect.bottom)
            return TRUE;
    return FALSE;
}

/* returns the monitor index for a device name, or -1. */
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

/* the entries of the handle table under the first of class.c, from index 1, as a session of windows starts them. */
HMONITOR MonitorHandle(ULONG index)
{
    if (index >= MIME_MAX_MONITORS)
        return nullptr;
    if (!g_count)
        MonitorsRefresh();
    return (HMONITOR)(ULONG_PTR)((ULONG)g_uniq[index] << 16 | (index + 1));
}

/* the high word must match or be 0x7fff, the rule of HMValidateSharedHandle. */
LONG MonitorIndexOfHandle(HMONITOR monitor)
{
    ULONG_PTR h = (ULONG_PTR)monitor;
    ULONG index = (ULONG)(h & 0xFFFF), uniq = (ULONG)(h >> 16 & 0x7FFF);
    if (!index || index > MonitorCount() || (uniq != g_uniq[index - 1] && uniq != 0x7FFF))
        return -1;
    return (LONG)(index - 1);
}

/*
 * the size the edid of a monitor tells, as monitor_edid of ntos/cm/boot.c writes it: centimetres in the basic block,
 * 34 by 19 when the host does not know them, and millimetres in the detailed timing, which are the centimetres when
 * it does not either.
 */
void MonitorEdidSize(ULONG index, ULONG mm[2], ULONG cm[2])
{
    static const ULONG unknown[2] = {34, 19};
    for (ULONG i = 0; i < 2; i++) {
        ULONG host = index < MonitorCount() ? g_mm[index][i] : 0;
        cm[i] = host / 10 ? host / 10 : unknown[i];
        mm[i] = host ? host : cm[i] * 10;
    }
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
        DpiRectConvert(&mi.rcMonitor, DPI_PHYSICAL, DpiCurrentAwareness());
        DpiRectConvert(&mi.rcWork, DPI_PHYSICAL, DpiCurrentAwareness());
        mi.dwFlags = m.Flags & MONITORINFOF_PRIMARY;
        memcpy(mi.szDevice, m.Device, sizeof(mi.szDevice));
        return NT_SUCCESS(MimeCopyToUser((PVOID)param2, &mi, mi.cbSize));
    }
    case MIME_TWOPARAM_GETMONITORS:
        if (count > param2)
            count = (ULONG)param2;
        for (ULONG i = 0; i < count; i++) {
            MonitorGet(i, &list[i]);
            DpiRectConvert(&list[i].Rect, DPI_PHYSICAL, DpiCurrentAwareness());
            DpiRectConvert(&list[i].Work, DPI_PHYSICAL, DpiCurrentAwareness());
        }
        if (count && !NT_SUCCESS(MimeCopyToUser((PVOID)param1, list, count * sizeof(list[0]))))
            return 0;
        return count;
    }
    return 0;
}

/* ------------------------------------------------------------------ services of user32 */

/* MONITOR_DPI_TYPE of shellscalingapi.h. */
enum { MDT_EFFECTIVE_DPI, MDT_ANGULAR_DPI, MDT_RAW_DPI };

/*
 * the dcs xxxEnumDisplayMonitors takes are the ones of its cache, from GetDC, GetWindowDC, BeginPaint or
 * CreateDC of DISPLAY. a memory dc or one of a single monitor fails it, measured on windows 11 26100.
 */
static DC *enum_dc(HDC hdc)
{
    DC *dc = hdc ? DcLookup(hdc) : nullptr;
    return dc && (dc->Flags & DC_WINDOW) && !dc->OfMonitor && dc->Surface ? dc : nullptr;
}

/* where device point 0, 0 of a dc is on the screen. the dcs of the screen draw in screen coordinates. */
static POINT enum_dc_screen(const DC *dc)
{
    if (dc->Surface->Window)
        return (POINT){dc->Origin.x + dc->Surface->Window->WindowRect.left,
                       dc->Origin.y + dc->Surface->Window->WindowRect.top};
    return (POINT){0, 0};
}

/* GetMonitorDC: another dc of the same window, its visible region cut to the monitor and its clip to the rectangle. */
static HDC enum_monitor_dc(DC *dc, const RECT *monitor, const RECT *device)
{
    DC *m = DcCreate(DC_WINDOW | DC_CACHE);
    if (!m)
        return nullptr;
    m->Window = dc->Window;
    m->Dpi = dc->Dpi;
    m->Awareness = dc->Awareness;
    DcSetSurface(m, dc->Surface);
    dc->Surface->Header.SelectCount++;
    m->Origin = dc->Origin;
    RECT surface = *monitor;
    RectOffset(&surface, dc->Origin.x, dc->Origin.y);
    RgnCombineRect(&m->VisRgn, &dc->VisRgn, &surface, RGN_AND);
    RgnSetRect(&m->State.ClipRgn, device);
    m->State.HasClip = TRUE;
    return (HDC)m->Header.Handle;
}

/*
 * xxxEnumDisplayMonitors: without a dc the virtual screen of the caller's dpi, cut by the clip rectangle, and the
 * rectangle of each visible monitor in it. with a dc its clip box in device coordinates, which the viewport does not
 * move, cut by the clip rectangle and taken to the screen by the origin of the dc; each monitor gets a dc of its
 * own and the rectangle back in the coordinates of the dc. an empty intersection succeeds without a call, a
 * callback that answers 0 stops it with 0 and no error, a bad clip pointer fails with ERROR_NOACCESS.
 */
BOOL NTAPI NtUserEnumDisplayMonitors(HDC hdc, const RECT *user_clip, MONITORENUMPROC proc, LPARAM lparam)
{
    RECT clip;
    if (user_clip && !NT_SUCCESS(MimeCopyFromUser(&clip, user_clip, sizeof(clip)))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    UserEnter();
    ULONG awareness = DpiCurrentAwareness();
    RECT area;
    POINT org = {0, 0};
    if (hdc) {
        DC *dc = enum_dc(hdc);
        if (!dc) {
            UserLeave();
            return FALSE;
        }
        REGION rgn;
        RgnInit(&rgn);
        BOOLEAN some = DcGetClip(dc, &rgn);
        area = rgn.Extents;
        RgnFree(&rgn);
        if (!some) {
            UserLeave();
            return TRUE;
        }
        RectOffset(&area, -dc->Origin.x, -dc->Origin.y);
        org = enum_dc_screen(dc);
    } else {
        area = DpiVirtualScreen(awareness);
    }
    if (user_clip && !RectIntersect(&area, &area, &clip)) {
        UserLeave();
        return TRUE;
    }
    RectOffset(&area, org.x, org.y);
    BOOL result = TRUE;
    for (ULONG i = 0;; i++) {
        RECT monitors[MIME_USER_MAX_MONITORS], rect;
        ULONG count = DpiVirtualMonitors(awareness, monitors);
        if (i >= count)
            break;
        if (!RectIntersect(&rect, &monitors[i], &area))
            continue;
        HDC mdc = nullptr;
        if (hdc) {
            DC *dc = enum_dc(hdc);
            RECT monitor = monitors[i];
            RectOffset(&monitor, -org.x, -org.y);
            RectOffset(&rect, -org.x, -org.y);
            if (!dc || !(mdc = enum_monitor_dc(dc, &monitor, &rect))) {
                result = FALSE;
                break;
            }
        }
        result = ClientMonitorEnumProc(MonitorHandle(i), mdc, &rect, lparam, (ULONG64)(ULONG_PTR)proc);
        if (mdc) {
            DC *m = DcLookup(mdc);
            if (m)
                PaintReleaseDC(m);
            /* the dc went away while the program ran. */
            if (!enum_dc(hdc))
                result = FALSE;
        }
        if (!result)
            break;
    }
    UserLeave();
    return result;
}

/* the name of the display of a monitor, at most 32 characters with the terminator. */
BOOL NTAPI NtUserGetHDevName(HMONITOR monitor, PWSTR user_name)
{
    UserEnterRead();
    LONG index = MonitorIndexOfHandle(monitor);
    MIME_MONITOR_INFO m;
    BOOLEAN found = index >= 0 && MonitorGet((ULONG)index, &m);
    UserLeave();
    if (!found) {
        W32SetLastError(ERROR_INVALID_MONITOR_HANDLE);
        return FALSE;
    }
    m.Device[RTL_NUMBER_OF(m.Device) - 1] = 0;
    SIZE_T bytes = (wcslen(m.Device) + 1) * sizeof(WCHAR);
    if (!NT_SUCCESS(MimeCopyToUser(user_name, m.Device, bytes))) {
        W32SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    return TRUE;
}

/*
 * NtUserGetDpiForMonitor. the effective dpi is the monitor's for a per monitor aware thread, the system's for a
 * system aware one and 96 for the others. the raw and the angular dpi come from the size in the edid, and are scaled
 * to what the thread sees unless it is per monitor aware. measured on windows 11 26100: an 800 by 600 panel whose
 * edid says 500 by 310 mm in its timing and 50 by 30 cm in its basic block has a raw dpi of 40 by 49, cut from the
 * millimetres, and an angular one of 39 by 49, rounded from the centimetres and taken to 96 of 100. a monitor
 * without an edid fails the raw dpi with ERROR_NOT_SUPPORTED and has 96 as its angular one, but every monitor of
 * mime has one. win32kbase 26300 has FillDpiInfo derive the angular dpi from the raw one by 20000 over 26880
 * instead, which the reference machine does not show.
 */
BOOL NTAPI NtUserGetDpiForMonitor(HMONITOR monitor, ULONG type, UINT *user_x, UINT *user_y)
{
    UserEnter();
    LONG index = MonitorIndexOfHandle(monitor);
    MIME_MONITOR_INFO m;
    ULONG mm[2], cm[2];
    BOOLEAN found = index >= 0 && MonitorGet((ULONG)index, &m);
    if (found)
        MonitorEdidSize((ULONG)index, mm, cm);
    ULONG awareness = DpiCurrentAwareness();
    UINT own = W32SystemDpi();
    UserLeave();
    if (!found) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    UINT dpi = m.Dpi ? m.Dpi : 96;
    UINT seen = awareness >= DPI_AWARE_PER_MONITOR ? dpi : awareness == DPI_AWARE_SYSTEM ? own : 96;
    LONG64 size[2] = {m.Rect.right - m.Rect.left, m.Rect.bottom - m.Rect.top};
    UINT out[2] = {seen, seen};
    for (ULONG i = 0; i < 2; i++) {
        if (type == MDT_RAW_DPI)
            out[i] = (UINT)(size[i] * 254 / ((LONG64)mm[i] * 10));
        else if (type == MDT_ANGULAR_DPI)
            out[i] = (UINT)W32MulDiv((LONG)((size[i] * 254 + cm[i] * 50) / (cm[i] * 100)), 96, 100);
        if (type != MDT_EFFECTIVE_DPI && awareness < DPI_AWARE_PER_MONITOR)
            out[i] = (UINT)W32MulDiv((LONG)out[i], (LONG)seen, (LONG)dpi);
    }
    if (!NT_SUCCESS(MimeCopyToUser(user_x, &out[0], sizeof(UINT))) ||
        !NT_SUCCESS(MimeCopyToUser(user_y, &out[1], sizeof(UINT)))) {
        W32SetLastError(ERROR_INVALID_ADDRESS);
        return FALSE;
    }
    return TRUE;
}
