/*
 * Shell_NotifyIcon uses the host notification area through status notifier items on its session bus. the host shows
 * tips and balloons and routes clicks to the application callback. shell32 validates state; win32k owns icons by
 * process and window lifetime. for menus, send a right-click request and intercept TrackPopupMenu for the host to
 * display; return the host selection to the application.
 */
#include "win32k.h"

#define TRAY_MAX 256
#define NIF_MESSAGE 0x01
#define NIF_ICON 0x02
#define NIF_TIP 0x04
#define NIF_STATE 0x08
#define NIF_INFO 0x10
#define NIS_HIDDEN 0x01
#define NIIF_ICON_MASK 0x0F
#define NIIF_NOSOUND 0x10
#define NOTIFYICON_VERSION_4 4
#define NIN_SELECT (WM_USER + 0)
#define NIN_BALLOONSHOW (WM_USER + 2)
#define NIN_BALLOONHIDE (WM_USER + 3)
#define NIN_BALLOONTIMEOUT (WM_USER + 4)
#define NIN_BALLOONUSERCLICK (WM_USER + 5)

#define MENU_ASK_MS 3000      /* time allowed for the application to open its menu. */
#define MENU_ANSWER_MS 60000  /* time allowed for the user to choose an item. */

typedef struct TRAY {
    BOOLEAN Used;
    ULONG HostId;
    W32PROCESS *Process;
    ULONG Key;
    HWND Window;
    UINT Id;
    UINT Callback;
    UINT Version;
} TRAY;

static TRAY g_tray[TRAY_MAX];
static ULONG g_next_host_id;
static MimeTrayCommand *g_command;

/* one active host menu, matching the single pointer. */
static struct {
    int State; /* 0 none, 1 asked for, 2 handed to the host, 3 chosen. */
    ULONG HostId;
    W32PROCESS *Process;
    HWND Window;
    ULONG64 Deadline;
    ULONG Result;
    ULONG Count;
    ULONG Ids[MIME_TRAY_MENU_MAX];
} g_menu;

static ULONG64 now_ms(void)
{
    return KeQueryInterruptTime() / 10000;
}

static MimeTrayCommand *new_command(void)
{
    if (!g_command) {
        PHYSICAL_ADDRESS highest;
        highest.QuadPart = -1;
        g_command = MmAllocateContiguousMemory(sizeof(*g_command), highest);
    }
    if (g_command)
        memset(g_command, 0, offsetof(MimeTrayCommand, pixels));
    return g_command;
}

static ULONG64 send(MimeTrayCommand *c)
{
    return HalMimeHypercall(MIME_HC_TRAY, (ULONG64)MmGetPhysicalAddress(c).QuadPart, sizeof(*c), 0, 0, 0, 0, nullptr);
}

static void app_name(W32PROCESS *p, uint16_t *out)
{
    ULONG start = 0, end = p ? p->ImageChars : 0;
    for (ULONG i = 0; i < end; i++)
        if (p->ImagePath[i] == L'\\' || p->ImagePath[i] == L'/')
            start = i + 1;
    for (ULONG i = end; i > start; i--)
        if (p->ImagePath[i - 1] == L'.') {
            end = i - 1;
            break;
        }
    ULONG n = 0;
    for (ULONG i = start; i < end && n < 63; i++)
        out[n++] = p->ImagePath[i];
    if (!n)
        for (const WCHAR *s = L"Program"; *s; s++)
            out[n++] = *s;
    out[n] = 0;
}

/*
 * host icons use non-premultiplied ARGB with alpha first, capped at MIME_TRAY_PICTURE_MAX per side. subsample larger
 * images.
 */
static BOOLEAN picture(HICON icon, uint8_t *out, uint32_t *width, uint32_t *height)
{
    SURFACE *s = CursorIconImage(icon);
    if (!s || s->Width <= 0 || s->Height <= 0 || s->Bpp != 32)
        return FALSE;
    ULONG step = 1;
    while ((ULONG)s->Width / step > MIME_TRAY_PICTURE_MAX || (ULONG)s->Height / step > MIME_TRAY_PICTURE_MAX)
        step++;
    ULONG w = (ULONG)s->Width / step, h = (ULONG)s->Height / step;
    for (ULONG y = 0; y < h; y++) {
        const UCHAR *row = s->Bits + (SIZE_T)(y * step) * s->Stride;
        for (ULONG x = 0; x < w; x++) {
            const UCHAR *px = row + (SIZE_T)(x * step) * 4;
            ULONG a = px[3], b = px[0], g = px[1], r = px[2];
            if (a && a < 255) {
                b = W32Min(255, (b * 255 + a / 2) / a);
                g = W32Min(255, (g * 255 + a / 2) / a);
                r = W32Min(255, (r * 255 + a / 2) / a);
            }
            UCHAR *o = out + ((SIZE_T)y * w + x) * 4;
            o[0] = (UCHAR)a;
            o[1] = (UCHAR)r;
            o[2] = (UCHAR)g;
            o[3] = (UCHAR)b;
        }
    }
    *width = w;
    *height = h;
    return TRUE;
}

static TRAY *find(W32PROCESS *p, ULONG key)
{
    for (ULONG i = 0; i < TRAY_MAX; i++)
        if (g_tray[i].Used && g_tray[i].Process == p && g_tray[i].Key == key)
            return &g_tray[i];
    return nullptr;
}

static TRAY *find_host(ULONG id)
{
    for (ULONG i = 0; i < TRAY_MAX; i++)
        if (g_tray[i].Used && g_tray[i].HostId == id)
            return &g_tray[i];
    return nullptr;
}

static void copy_text(uint16_t *out, ULONG capacity, const WCHAR *text)
{
    ULONG n = 0;
    while (n + 1 < capacity && text[n]) {
        out[n] = text[n];
        n++;
    }
    out[n] = 0;
}

static void send_balloon(TRAY *t, const MIME_TRAY_REQUEST *r)
{
    MimeTrayCommand *c = new_command();
    if (!c)
        return;
    c->op = MIME_TRAY_BALLOON;
    c->id = t->HostId;
    app_name(t->Process, c->app);
    copy_text(c->balloon_title, RTL_NUMBER_OF(c->balloon_title), r->InfoTitle);
    copy_text(c->balloon_text, RTL_NUMBER_OF(c->balloon_text), r->Info);
    ULONG kind = r->InfoFlags & NIIF_ICON_MASK;
    if (kind == 3)
        c->flags |= MIME_TRAY_ERROR;
    else if (kind == 2)
        c->flags |= MIME_TRAY_WARNING;
    if (r->InfoFlags & NIIF_NOSOUND)
        c->flags |= MIME_TRAY_SILENT;
    if (kind == 4 && r->BalloonIcon)
        picture((HICON)(ULONG_PTR)r->BalloonIcon, c->balloon_pixels, &c->balloon_width, &c->balloon_height);
    send(c);
}

static void send_icon(TRAY *t, const MIME_TRAY_REQUEST *r, BOOLEAN hidden)
{
    MimeTrayCommand *c = new_command();
    if (!c)
        return;
    c->op = MIME_TRAY_SET;
    c->id = t->HostId;
    c->process_id = (ULONG)(ULONG_PTR)PsGetCurrentProcessId();
    app_name(t->Process, c->app);
    if (hidden)
        c->flags |= MIME_TRAY_HIDDEN;
    if ((r->Flags & NIF_TIP)) {
        c->flags |= MIME_TRAY_HAS_TIP;
        copy_text(c->tip, RTL_NUMBER_OF(c->tip), r->Tip);
    }
    if ((r->Flags & NIF_ICON) && picture((HICON)(ULONG_PTR)r->Icon, c->pixels, &c->width, &c->height))
        c->flags |= MIME_TRAY_HAS_PICTURE;
    send(c);
}

ULONG NTAPI NtUserMimeTray(ULONG op, PUCHAR user_request, ULONG size)
{
    MIME_TRAY_REQUEST r;
    if (size != sizeof(r) || !NT_SUCCESS(MimeCopyFromUser(&r, user_request, sizeof(r))))
        return 0;
    ULONG result = 0;
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    TRAY *t = find(p, r.Key);
    switch (op) {
    case MIME_TRAYOP_ADD: {
        WND *w = WndLookup((HWND)(ULONG_PTR)r.Window);
        if (!w || t)
            break;
        for (ULONG i = 0; i < TRAY_MAX && !t; i++)
            if (!g_tray[i].Used)
                t = &g_tray[i];
        if (!t)
            break;
        memset(t, 0, sizeof(*t));
        t->Used = TRUE;
        t->HostId = ++g_next_host_id;
        t->Process = p;
        t->Key = r.Key;
        t->Window = (HWND)(ULONG_PTR)r.Window;
        t->Id = r.Id;
        if (r.Flags & NIF_MESSAGE)
            t->Callback = r.Callback;
        t->Version = r.Version;
        send_icon(t, &r, (r.State & NIS_HIDDEN) != 0);
        if ((r.Flags & NIF_INFO) && r.Info[0])
            send_balloon(t, &r);
        result = 1;
        break;
    }
    case MIME_TRAYOP_MODIFY:
        if (!t)
            break;
        if (r.Flags & NIF_MESSAGE)
            t->Callback = r.Callback;
        if (r.Version)
            t->Version = r.Version;
        /* shell32 retains icon state and passes the current hidden bit. */
        send_icon(t, &r, (r.State & NIS_HIDDEN) != 0);
        if (r.Flags & NIF_INFO)
            send_balloon(t, &r);
        result = 1;
        break;
    case MIME_TRAYOP_DELETE:
        if (!t)
            break;
        {
            MimeTrayCommand *c = new_command();
            if (c) {
                c->op = MIME_TRAY_REMOVE;
                c->id = t->HostId;
                send(c);
            }
        }
        if (g_menu.State && g_menu.HostId == t->HostId)
            g_menu.State = 0;
        memset(t, 0, sizeof(*t));
        result = 1;
        break;
    case MIME_TRAYOP_MENU:
        if (g_menu.State == 1 && g_menu.Process == p && now_ms() < g_menu.Deadline) {
            MimeTrayCommand *c = new_command();
            if (c) {
                c->op = MIME_TRAY_MENU;
                c->id = g_menu.HostId;
                c->menu_count = MenuFlattenForHost((HMENU)(ULONG_PTR)r.Menu, c->menu, g_menu.Ids, MIME_TRAY_MENU_MAX);
                g_menu.Count = c->menu_count;
                g_menu.State = 2;
                g_menu.Deadline = now_ms() + MENU_ANSWER_MS;
                send(c);
                result = 1;
            }
        }
        break;
    case MIME_TRAYOP_MENU_RESULT:
        if (g_menu.State == 3) {
            result = g_menu.Result;
            g_menu.State = 0;
        } else if (g_menu.State != 2 || g_menu.Process != p || now_ms() > g_menu.Deadline) {
            if (g_menu.State == 2)
                g_menu.State = 0;
            result = 1;
        }
        break;
    }
    UserLeave();
    return result;
}

/* format the callback according to the icon version. */
static void callback(TRAY *t, UINT event, int32_t x, int32_t y)
{
    WND *w = WndLookup(t->Window);
    if (!w || !w->Thread || !t->Callback)
        return;
    WPARAM wparam = t->Id;
    LPARAM lparam = event;
    if (t->Version >= NOTIFYICON_VERSION_4) {
        wparam = MAKEWPARAM((WORD)x, (WORD)y);
        lparam = MAKELPARAM((WORD)event, (WORD)t->Id);
    }
    QueuePost(w->Thread, t->Window, t->Callback, wparam, lparam);
}

/* send mouse messages or version 3 and later events, according to the icon version. */
static void click(TRAY *t, UINT select, UINT context, UINT down, UINT up, int32_t x, int32_t y)
{
    if (t->Version >= 3 && select) {
        callback(t, select, x, y);
        return;
    }
    if (t->Version >= 3 && context) {
        callback(t, context, x, y);
        return;
    }
    callback(t, down, x, y);
    callback(t, up, x, y);
}

static void wake(HWND window)
{
    WND *w = WndLookup(window);
    if (w && w->Thread)
        QueuePost(w->Thread, window, WM_NULL, 0, 0);
}

/* approximate the host tray at the work area corner in guest coordinates. */
static void corner(int32_t *x, int32_t *y)
{
    MIME_MONITOR_INFO m;
    if (MonitorGet(0, &m)) {
        *x = m.Work.right - 16;
        *y = m.Work.bottom - 16;
    } else {
        *x = *y = 0;
    }
}

void TrayHostEvent(const MimeInputEvent *ev)
{
    TRAY *t = find_host(ev->pad);
    if (!t)
        return;
    int32_t x, y;
    corner(&x, &y);
    switch (ev->flags) {
    case MIME_TRAY_EVENT_ACTIVATE:
        click(t, NIN_SELECT, 0, WM_LBUTTONDOWN, WM_LBUTTONUP, x, y);
        break;
    case MIME_TRAY_EVENT_CONTEXT:
        click(t, 0, WM_CONTEXTMENU, WM_RBUTTONDOWN, WM_RBUTTONUP, x, y);
        break;
    case MIME_TRAY_EVENT_SECONDARY:
        callback(t, WM_MBUTTONDOWN, x, y);
        callback(t, WM_MBUTTONUP, x, y);
        break;
    case MIME_TRAY_EVENT_MENU_REQUEST:
        /* send a right-click request and forward the resulting popup menu to the host. */
        g_menu.State = 1;
        g_menu.HostId = t->HostId;
        g_menu.Process = t->Process;
        g_menu.Window = t->Window;
        g_menu.Deadline = now_ms() + MENU_ASK_MS;
        click(t, 0, WM_CONTEXTMENU, WM_RBUTTONDOWN, WM_RBUTTONUP, x, y);
        break;
    case MIME_TRAY_EVENT_MENU_CHOSEN:
        if (g_menu.State == 2 && g_menu.HostId == t->HostId) {
            g_menu.Result = ev->ch < g_menu.Count ? 0x80000000u | g_menu.Ids[ev->ch] : 1;
            g_menu.State = 3;
            wake(g_menu.Window);
        }
        break;
    case MIME_TRAY_EVENT_MENU_CLOSED:
        if (g_menu.State == 2 && g_menu.HostId == t->HostId) {
            g_menu.Result = 1;
            g_menu.State = 3;
            wake(g_menu.Window);
        } else if (g_menu.State == 1 && g_menu.HostId == t->HostId) {
            g_menu.State = 0;
        }
        break;
    case MIME_TRAY_EVENT_BALLOON_SHOWN:
        callback(t, NIN_BALLOONSHOW, x, y);
        break;
    case MIME_TRAY_EVENT_BALLOON_CLICKED:
        callback(t, NIN_BALLOONUSERCLICK, x, y);
        break;
    case MIME_TRAY_EVENT_BALLOON_TIMEOUT:
        callback(t, NIN_BALLOONTIMEOUT, x, y);
        break;
    case MIME_TRAY_EVENT_BALLOON_HIDDEN:
        callback(t, NIN_BALLOONHIDE, x, y);
        break;
    }
}

/* destroy icons with their window. */
void TrayWindowDestroyed(WND *w)
{
    HWND handle = WndHandle(w);
    for (ULONG i = 0; i < TRAY_MAX; i++) {
        TRAY *t = &g_tray[i];
        if (!t->Used || t->Window != handle)
            continue;
        MimeTrayCommand *c = new_command();
        if (c) {
            c->op = MIME_TRAY_REMOVE;
            c->id = t->HostId;
            send(c);
        }
        if (g_menu.State && g_menu.HostId == t->HostId)
            g_menu.State = 0;
        memset(t, 0, sizeof(*t));
    }
}
