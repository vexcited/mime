/*
 * screen capture. a duplication follows one monitor of the screen as mime composes it in screen.c and hands out
 * frames: rows that changed since the last frame go to the program's buffer, and their rectangles wait to be
 * asked for. the pointer comes separately from the frame, as in dxgi output duplication.
 *
 * the only signals that the screen changed are the guest's own presents, window configurations and the counts of
 * images windows presented through the host, ScreenSerial and ScreenHostSerial. so a frame is composed only when
 * one of them moved, and the program polls at the display's rate.
 *
 * a duplication keeps a copy of the last frame it gave. new frames are compared with it in bands of rows, so the
 * rectangles are exact to the pixel within a band, and touching bands are joined.
 */
#include "win32k.h"

#define CAPTURE_MAX 32
/* assumed: windows refuses a fifth duplication of an output with DXGI_ERROR_NOT_CURRENTLY_AVAILABLE. */
#define CAPTURE_PER_OUTPUT 4
#define CAPTURE_MAX_RECTS 128
#define CAPTURE_BAND 16

#define SHAPE_COLOR 2

typedef struct CAPTURE {
    BOOLEAN Used;
    W32PROCESS *Process;
    ULONG Handle;
    ULONG Monitor;
    RECT Rect; /* the monitor at creation; if it changes, the duplication is lost. */
    HWND Window; /* set for a window's duplication, whose pixels are the window's own. */
    BOOLEAN ClientOnly;
    SURFACE *Prev;
    SURFACE *Now; /* the window's composed pixels, compared with the copy. */
    BOOLEAN Delivered;
    ULONG GuestSerial;
    ULONG64 HostSerial;
    BOOLEAN PointerKnown;
    BOOLEAN PointerVisible;
    POINT PointerPos;
    ULONG ShapeSerial;
    RECT Dirty[CAPTURE_MAX_RECTS];
    ULONG DirtyCount;
} CAPTURE;

static CAPTURE g_captures[CAPTURE_MAX];
static ULONG g_next_handle;

static CAPTURE *find(ULONG handle)
{
    W32PROCESS *p = W32GetProcess();
    for (ULONG i = 0; i < CAPTURE_MAX; i++)
        if (g_captures[i].Used && g_captures[i].Handle == handle && g_captures[i].Process == p)
            return &g_captures[i];
    return nullptr;
}

static void release(CAPTURE *c)
{
    if (c->Prev)
        SurfaceDestroy(c->Prev);
    if (c->Now)
        SurfaceDestroy(c->Now);
    memset(c, 0, sizeof(*c));
}

void CaptureFreeProcess(W32PROCESS *p)
{
    for (ULONG i = 0; i < CAPTURE_MAX; i++)
        if (g_captures[i].Used && g_captures[i].Process == p)
            release(&g_captures[i]);
}

static ULONG64 counter(void)
{
    return (ULONG64)KeQueryPerformanceCounter(nullptr).QuadPart;
}

/* ------------------------------------------------------------------ pointer image */

/* rows of 0 for clear, 1 for outline and 2 for fill. */
typedef struct BUILTIN {
    LONG Width, Height, HotspotX, HotspotY;
    const char *Rows;
} BUILTIN;

static const char ARROW[] =
    "1..........."
    "11.........."
    "121........."
    "1221........"
    "12221......."
    "122221......"
    "1222221....."
    "12222221...."
    "122222221..."
    "1222222221.."
    "12222222221."
    "122222211111"
    "1221221....."
    "121.1221...."
    "11..1221...."
    "1....1221..."
    ".....1221..."
    "......11....";

static const char IBEAM[] =
    "1111111"
    "1222221"
    "1121211"
    "..121.."
    "..121.."
    "..121.."
    "..121.."
    "..121.."
    "..121.."
    "..121.."
    "..121.."
    "..121.."
    "..121.."
    "1121211"
    "1222221"
    "1111111";

static const char CROSS[] =
    ".......121......."
    ".......121......."
    ".......121......."
    ".......121......."
    ".......121......."
    ".......121......."
    ".......121......."
    "1111111121111111."
    "1222222222222221."
    "1111111121111111."
    ".......121......."
    ".......121......."
    ".......121......."
    ".......121......."
    ".......121......."
    ".......121......."
    ".......121.......";

static const BUILTIN ARROW_SHAPE = {12, 18, 0, 0, ARROW};
static const BUILTIN IBEAM_SHAPE = {7, 16, 3, 8, IBEAM};
static const BUILTIN CROSS_SHAPE = {17, 17, 8, 8, CROSS};

static ULONG g_shape_serial = 1;
static ULONG g_host_kind = MIME_CURSOR_ARROW;
static HCURSOR g_shown;

/* the host draws these with its own theme, so captures only approximate them. */
void CaptureCursorKind(ULONG kind)
{
    if (kind == g_host_kind && !g_shown)
        return;
    g_host_kind = kind;
    g_shown = nullptr;
    g_shape_serial++;
}

/* none when the host's own cursor is shown. */
void CaptureCursorShown(HCURSOR cursor)
{
    if (cursor == g_shown)
        return;
    g_shown = cursor;
    g_shape_serial++;
}

/* straight alpha bgra of the program's cursor, else a built in one for the host's. false when hidden. */
static BOOLEAN pointer_image(PUCHAR out, ULONG capacity, MIME_CAPTURE_SHAPE *shape)
{
    SURFACE *image = g_shown ? CursorImage(g_shown) : nullptr;
    LONG width, height, hx = 0, hy = 0;
    const BUILTIN *b = nullptr;
    if (image) {
        width = image->Width;
        height = image->Height;
        CursorHotspot(g_shown, &hx, &hy);
    } else {
        if (g_host_kind == MIME_CURSOR_HIDDEN)
            return FALSE;
        b = g_host_kind == MIME_CURSOR_IBEAM ? &IBEAM_SHAPE : g_host_kind == MIME_CURSOR_CROSS ? &CROSS_SHAPE : &ARROW_SHAPE;
        width = b->Width;
        height = b->Height;
        hx = b->HotspotX;
        hy = b->HotspotY;
    }
    /*
     * the pointer is the size of the system's dpi for every program: a cursor of a program that is not aware of it is
     * stretched to it, as the compositor of Windows shows it.
     */
    UINT from = DpiScaleOf(image ? CursorAwareness(g_shown) : DPI_AWARE_UNAWARE), to = W32SystemDpi();
    LONG source_width = width, source_height = height;
    if (from && from != to) {
        width = DpiScale(width, from, to);
        height = DpiScale(height, from, to);
        hx = DpiScale(hx, from, to);
        hy = DpiScale(hy, from, to);
    }
    shape->Type = SHAPE_COLOR;
    shape->Width = (ULONG)width;
    shape->Height = (ULONG)height;
    shape->Pitch = (ULONG)width * 4;
    shape->HotspotX = hx;
    shape->HotspotY = hy;
    shape->Size = shape->Pitch * (ULONG)height;
    if (!out || capacity < shape->Size)
        return TRUE;
    for (LONG y = 0; y < height; y++) {
        ULONG *row = (ULONG *)(out + (SIZE_T)y * shape->Pitch);
        LONG sy = (LONG)((LONG64)y * source_height / height);
        for (LONG x = 0; x < width; x++) {
            ULONG px;
            LONG sx = (LONG)((LONG64)x * source_width / width);
            if (image) {
                /* our image is premultiplied, windows pointer shapes are not. */
                px = SurfaceGetPixel(image, sx, sy);
                ULONG a = px >> 24;
                if (a && a != 255) {
                    ULONG r = ((px >> 16 & 0xFF) * 255 + a / 2) / a, g = ((px >> 8 & 0xFF) * 255 + a / 2) / a,
                          bl = ((px & 0xFF) * 255 + a / 2) / a;
                    px = a << 24 | W32Min(r, 255) << 16 | W32Min(g, 255) << 8 | W32Min(bl, 255);
                }
            } else {
                char c = b->Rows[sy * source_width + sx];
                px = c == '1' ? 0xFF000000 : c == '2' ? 0xFFFFFFFF : 0;
            }
            row[x] = px;
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------ frames */

/* false when the rows are the same. */
static BOOLEAN row_diff(const ULONG *a, const ULONG *b, LONG width, LONG *first, LONG *last)
{
    LONG x = 0;
    while (x + 1 < width && *(const ULONG64 *)(a + x) == *(const ULONG64 *)(b + x))
        x += 2;
    while (x < width && a[x] == b[x])
        x++;
    if (x >= width)
        return FALSE;
    LONG end = width - 1;
    while (end > x && a[end] == b[end])
        end--;
    *first = x;
    *last = end;
    return TRUE;
}

static void add_rect(CAPTURE *c, const RECT *r)
{
    /* bands that touch and overlap in x merge into one rectangle. */
    if (c->DirtyCount) {
        RECT *last = &c->Dirty[c->DirtyCount - 1];
        if (last->bottom == r->top && r->left < last->right && r->right > last->left) {
            last->left = W32Min(last->left, r->left);
            last->right = W32Max(last->right, r->right);
            last->bottom = r->bottom;
            return;
        }
    }
    if (c->DirtyCount == CAPTURE_MAX_RECTS) {
        /* too many: the last rectangle absorbs the rest, like windows coalesces its own. */
        RECT *last = &c->Dirty[c->DirtyCount - 1];
        RectUnion(last, last, r);
        return;
    }
    c->Dirty[c->DirtyCount++] = *r;
}

/* collects rectangles of rows that differ from the copy, and updates the copy. */
static void diff_frame(CAPTURE *c, const UCHAR *bits, LONG stride)
{
    LONG width = c->Prev->Width, height = c->Prev->Height;
    c->DirtyCount = 0;
    if (!c->Delivered) {
        c->Dirty[c->DirtyCount++] = (RECT){0, 0, width, height};
        for (LONG y = 0; y < height; y++)
            memcpy(c->Prev->Bits + (LONG_PTR)y * c->Prev->Stride, bits + (LONG_PTR)y * stride, (SIZE_T)width * 4);
        return;
    }
    for (LONG top = 0; top < height; top += CAPTURE_BAND) {
        LONG bottom = W32Min(top + CAPTURE_BAND, height), x0 = width, x1 = -1, y0 = -1, y1 = -1;
        for (LONG y = top; y < bottom; y++) {
            const ULONG *now = (const ULONG *)(bits + (LONG_PTR)y * stride);
            ULONG *was = (ULONG *)(c->Prev->Bits + (LONG_PTR)y * c->Prev->Stride);
            LONG first, last;
            if (!row_diff(now, was, width, &first, &last))
                continue;
            x0 = W32Min(x0, first);
            x1 = W32Max(x1, last);
            if (y0 < 0)
                y0 = y;
            y1 = y;
            memcpy(was + first, now + first, (SIZE_T)(last - first + 1) * 4);
        }
        if (y0 >= 0)
            add_rect(c, &(RECT){x0, y0, x1 + 1, y1 + 1});
    }
}

/* copies the dirty rows from the copy to the program's buffer. */
static NTSTATUS write_frame(CAPTURE *c, PUCHAR user, ULONG size)
{
    LONG width = c->Prev->Width;
    for (ULONG i = 0; i < c->DirtyCount; i++) {
        const RECT *r = &c->Dirty[i];
        for (LONG y = r->top; y < r->bottom; y++) {
            NTSTATUS status = MimeCopyToUser(user + ((SIZE_T)y * width + r->left) * 4,
                                             c->Prev->Bits + (LONG_PTR)y * c->Prev->Stride + (LONG_PTR)r->left * 4,
                                             (SIZE_T)(r->right - r->left) * 4);
            if (!NT_SUCCESS(status))
                return status;
        }
    }
    (void)size;
    return STATUS_SUCCESS;
}

static BOOLEAN monitor_is(CAPTURE *c)
{
    MIME_MONITOR_INFO m;
    return MonitorGet(c->Monitor, &m) && !memcmp(&m.Rect, &c->Rect, sizeof(RECT));
}

/* none when the window is gone. */
static WND *capture_window(CAPTURE *c)
{
    WND *w = WndLookup(c->Window);
    return w && !w->Header.Destroyed ? w : nullptr;
}

/* the desktops share one window tree: only threads of the default desktop see the screen. */
static BOOLEAN on_input_desktop(void)
{
    return WinStaOnInputDesktop();
}

static ULONG refresh_hz(void)
{
    MIME_MONITOR_INFO m;
    return MonitorGet(0, &m) && m.RefreshHz ? m.RefreshHz : 60;
}

static LONG op_open_window(MIME_CAPTURE_OPEN *o)
{
    WND *w = WndLookup((HWND)(ULONG_PTR)o->Monitor);
    if (!w || w->Header.Destroyed || !WndIsTopLevel(w) || w == WndDesktop())
        return STATUS_INVALID_HANDLE;
    if (!on_input_desktop())
        return STATUS_ACCESS_DENIED;
    CAPTURE *slot = nullptr;
    for (ULONG i = 0; i < CAPTURE_MAX && !slot; i++)
        if (!g_captures[i].Used)
            slot = &g_captures[i];
    if (!slot)
        return STATUS_INSTANCE_NOT_AVAILABLE;
    BOOLEAN client = (o->Flags & MIME_CAPTURE_TARGET_CLIENT) != 0;
    const RECT *r = client ? &w->ClientRect : &w->WindowRect;
    LONG width = r->right - r->left, height = r->bottom - r->top;
    if (width <= 0 || height <= 0)
        return STATUS_INVALID_PARAMETER;
    SURFACE *prev = SurfaceCreate(width, height, 32, SURF_TOPDOWN), *now = SurfaceCreate(width, height, 32, SURF_TOPDOWN);
    if (!prev || !now) {
        if (prev)
            SurfaceDestroy(prev);
        if (now)
            SurfaceDestroy(now);
        return STATUS_NO_MEMORY;
    }
    memset(slot, 0, sizeof(*slot));
    slot->Used = TRUE;
    slot->Process = W32GetProcess();
    slot->Handle = ++g_next_handle;
    slot->Window = WndHandle(w);
    slot->ClientOnly = client;
    slot->Prev = prev;
    slot->Now = now;
    DbgPrint("win32k: capture: process %u captures window %p, %dx%d\n", (ULONG)(ULONG_PTR)slot->Process->Pid, slot->Window,
             width, height);
    o->Handle = slot->Handle;
    o->Width = (ULONG)width;
    o->Height = (ULONG)height;
    o->X = r->left;
    o->Y = r->top;
    o->RefreshNumerator = refresh_hz();
    o->RefreshDenominator = 1;
    return STATUS_SUCCESS;
}

static LONG op_open(MIME_CAPTURE_OPEN *o)
{
    if (o->Flags & MIME_CAPTURE_WINDOW_TARGET)
        return op_open_window(o);
    MIME_MONITOR_INFO m;
    if (!MonitorGet(o->Monitor, &m))
        return STATUS_NOT_FOUND;
    if (!on_input_desktop())
        return STATUS_ACCESS_DENIED;
    ULONG same = 0;
    CAPTURE *free_slot = nullptr;
    for (ULONG i = 0; i < CAPTURE_MAX; i++) {
        if (!g_captures[i].Used)
            free_slot = free_slot ? free_slot : &g_captures[i];
        else if (g_captures[i].Monitor == o->Monitor)
            same++;
    }
    if (same >= CAPTURE_PER_OUTPUT)
        return STATUS_INSTANCE_NOT_AVAILABLE;
    if (!free_slot)
        return STATUS_INSTANCE_NOT_AVAILABLE;
    LONG width = m.Rect.right - m.Rect.left, height = m.Rect.bottom - m.Rect.top;
    SURFACE *prev = SurfaceCreate(width, height, 32, SURF_TOPDOWN);
    if (!prev)
        return STATUS_NO_MEMORY;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->Used = TRUE;
    free_slot->Process = W32GetProcess();
    free_slot->Handle = ++g_next_handle;
    free_slot->Monitor = o->Monitor;
    free_slot->Rect = m.Rect;
    free_slot->Prev = prev;
    DbgPrint("win32k: capture: process %u duplicates monitor %u, %dx%d\n", (ULONG)(ULONG_PTR)free_slot->Process->Pid,
             o->Monitor + 1, width, height);
    o->Handle = free_slot->Handle;
    o->Width = (ULONG)width;
    o->Height = (ULONG)height;
    o->X = m.Rect.left;
    o->Y = m.Rect.top;
    o->RefreshNumerator = m.RefreshHz ? m.RefreshHz : 60;
    o->RefreshDenominator = 1;
    return STATUS_SUCCESS;
}

/*
 * the window's pixels are composed again when anything may have changed, and given when they differ from the
 * last ones. the window going away ends the capture, and a resized window restarts with whole frames at the new
 * size once the buffer is big enough.
 */
static LONG op_frame_window(CAPTURE *c, MIME_CAPTURE_FRAME *f, PUCHAR user, ULONG size)
{
    WND *w = capture_window(c);
    if (!w)
        return STATUS_INVALID_HANDLE;
    if (!on_input_desktop())
        return STATUS_ACCESS_DENIED;
    const RECT *r = c->ClientOnly ? &w->ClientRect : &w->WindowRect;
    LONG width = r->right - r->left, height = r->bottom - r->top;
    if (width <= 0 || height <= 0)
        return STATUS_TIMEOUT;
    memset(f, 0, sizeof(*f));
    f->Width = (ULONG)width;
    f->Height = (ULONG)height;
    if (width != c->Prev->Width || height != c->Prev->Height) {
        SURFACE *prev = SurfaceCreate(width, height, 32, SURF_TOPDOWN), *now = SurfaceCreate(width, height, 32, SURF_TOPDOWN);
        if (!prev || !now) {
            if (prev)
                SurfaceDestroy(prev);
            if (now)
                SurfaceDestroy(now);
            return STATUS_NO_MEMORY;
        }
        SurfaceDestroy(c->Prev);
        SurfaceDestroy(c->Now);
        c->Prev = prev;
        c->Now = now;
        c->Delivered = FALSE;
    }
    BOOLEAN changed = !c->Delivered || c->GuestSerial != ScreenSerial;
    ULONG64 host = c->HostSerial;
    if (!changed) {
        host = ScreenHostSerial();
        changed = host != c->HostSerial;
    }
    if (!changed)
        return STATUS_TIMEOUT;
    if ((ULONG64)size < (ULONG64)width * height * 4)
        return STATUS_BUFFER_TOO_SMALL;
    ULONG serial = ScreenSerial;
    if (!ScreenWindowPixels(w, c->ClientOnly, TRUE, c->Now->Bits, c->Now->Stride, width, height))
        return STATUS_INVALID_HANDLE;
    diff_frame(c, c->Now->Bits, c->Now->Stride);
    c->GuestSerial = serial;
    c->HostSerial = host;
    c->Delivered = TRUE;
    if (!c->DirtyCount)
        return STATUS_TIMEOUT;
    NTSTATUS status = write_frame(c, user, size);
    if (!NT_SUCCESS(status))
        return status;
    ULONG64 now = counter();
    f->Accumulated = 1;
    f->LastPresentLow = (ULONG)now;
    f->LastPresentHigh = (ULONG)(now >> 32);
    f->RectsCoalesced = c->DirtyCount == CAPTURE_MAX_RECTS;
    f->DirtyBytes = c->DirtyCount * (ULONG)sizeof(RECT);
    return STATUS_SUCCESS;
}

static LONG op_frame(CAPTURE *c, MIME_CAPTURE_FRAME *f, PUCHAR user, ULONG size)
{
    if (c->Window)
        return op_frame_window(c, f, user, size);
    if (!monitor_is(c))
        return STATUS_NOT_FOUND;
    if (!on_input_desktop())
        return STATUS_ACCESS_DENIED;
    LONG width = c->Prev->Width, height = c->Prev->Height;
    memset(f, 0, sizeof(*f));
    f->Width = (ULONG)width;
    f->Height = (ULONG)height;
    if ((ULONG64)size < (ULONG64)width * height * 4)
        return STATUS_BUFFER_TOO_SMALL;

    MIME_CAPTURE_SHAPE shape;
    BOOLEAN shown = pointer_image(nullptr, 0, &shape);
    POINT pos = {InputCursorPos.x - c->Rect.left - (shown ? shape.HotspotX : 0),
                 InputCursorPos.y - c->Rect.top - (shown ? shape.HotspotY : 0)};
    BOOLEAN visible = shown && !InputCursorHidden && InputCursorPos.x >= c->Rect.left &&
                      InputCursorPos.x < c->Rect.right && InputCursorPos.y >= c->Rect.top &&
                      InputCursorPos.y < c->Rect.bottom;
    BOOLEAN pointer_moved = !c->PointerKnown || visible != c->PointerVisible ||
                            (visible && (pos.x != c->PointerPos.x || pos.y != c->PointerPos.y));
    BOOLEAN shape_changed = !c->PointerKnown || c->ShapeSerial != g_shape_serial;

    BOOLEAN screen_changed = !c->Delivered || c->GuestSerial != ScreenSerial;
    ULONG64 host = c->HostSerial;
    if (!screen_changed) {
        host = ScreenHostSerial();
        screen_changed = host != c->HostSerial;
    }
    if (!screen_changed && !pointer_moved && !shape_changed)
        return STATUS_TIMEOUT;

    ULONG64 now = counter();
    if (screen_changed) {
        ULONG serial = ScreenSerial;
        SURFACE *screen = ScreenSurface();
        if (!screen)
            return STATUS_NO_MEMORY;
        POINT origin = ScreenOrigin();
        RECT area = c->Rect;
        RectOffset(&area, -origin.x, -origin.y);
        ScreenCompose(&area);
        BOOLEAN first = !c->Delivered;
        diff_frame(c, screen->Bits + (LONG_PTR)area.top * screen->Stride + (LONG_PTR)area.left * 4, screen->Stride);
        if (c->DirtyCount || first) {
            LONG sent = (LONG)(serial - c->GuestSerial);
            f->Accumulated = first ? 1 : (ULONG)W32Min(W32Max(sent, 1), 0xFFFF);
            f->LastPresentLow = (ULONG)now;
            f->LastPresentHigh = (ULONG)(now >> 32);
        }
        c->GuestSerial = serial;
        c->HostSerial = host;
        c->Delivered = TRUE;
        if (!c->DirtyCount && !pointer_moved && !shape_changed)
            return STATUS_TIMEOUT;
        NTSTATUS status = write_frame(c, user, size);
        if (!NT_SUCCESS(status))
            return status;
    } else {
        c->DirtyCount = 0;
    }
    if (pointer_moved || shape_changed) {
        f->LastMouseLow = (ULONG)now;
        f->LastMouseHigh = (ULONG)(now >> 32);
    }
    f->PointerVisible = visible;
    f->PointerX = pos.x;
    f->PointerY = pos.y;
    f->RectsCoalesced = c->DirtyCount == CAPTURE_MAX_RECTS;
    f->DirtyBytes = c->DirtyCount * (ULONG)sizeof(RECT);
    f->MoveBytes = 0;
    f->ShapeBytes = shape_changed && shown ? shape.Size : 0;
    c->PointerKnown = TRUE;
    c->PointerVisible = visible;
    c->PointerPos = pos;
    c->ShapeSerial = g_shape_serial;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ entry point */

LONG NTAPI NtUserMimeCapture(ULONG op, ULONG id, PUCHAR info, PUCHAR data, ULONG size)
{
    LONG status = STATUS_INVALID_PARAMETER;
    UserEnter();
    switch (op) {
    case MIME_CAPOP_OPEN: {
        MIME_CAPTURE_OPEN o;
        if (!NT_SUCCESS(MimeCopyFromUser(&o, info, sizeof(o))))
            break;
        status = op_open(&o);
        if (NT_SUCCESS(status) && !NT_SUCCESS(MimeCopyToUser(info, &o, sizeof(o)))) {
            release(find(o.Handle));
            status = STATUS_ACCESS_VIOLATION;
        }
        break;
    }
    case MIME_CAPOP_CLOSE: {
        CAPTURE *c = find(id);
        if (c) {
            release(c);
            status = STATUS_SUCCESS;
        }
        break;
    }
    case MIME_CAPOP_FRAME: {
        CAPTURE *c = find(id);
        MIME_CAPTURE_FRAME f;
        if (!c) {
            status = STATUS_NOT_FOUND;
            break;
        }
        status = op_frame(c, &f, data, size);
        if ((NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL) && status != STATUS_TIMEOUT &&
            !NT_SUCCESS(MimeCopyToUser(info, &f, sizeof(f))))
            status = STATUS_ACCESS_VIOLATION;
        break;
    }
    case MIME_CAPOP_DIRTY:
    case MIME_CAPOP_MOVES: {
        CAPTURE *c = find(id);
        if (!c) {
            status = STATUS_NOT_FOUND;
            break;
        }
        ULONG need = op == MIME_CAPOP_DIRTY ? c->DirtyCount * (ULONG)sizeof(RECT) : 0;
        if (info && !NT_SUCCESS(MimeCopyToUser(info, &need, sizeof(need)))) {
            status = STATUS_ACCESS_VIOLATION;
            break;
        }
        if (size < need) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        /* in monitor pixels, like the frame. */
        status = need && !NT_SUCCESS(MimeCopyToUser(data, c->Dirty, need)) ? STATUS_ACCESS_VIOLATION : STATUS_SUCCESS;
        break;
    }
    case MIME_CAPOP_SHAPE: {
        CAPTURE *c = find(id);
        if (!c) {
            status = STATUS_NOT_FOUND;
            break;
        }
        MIME_CAPTURE_SHAPE shape = {};
        PUCHAR image = size ? W32Alloc(size) : nullptr;
        if (size && !image) {
            status = STATUS_NO_MEMORY;
            break;
        }
        if (!pointer_image(image, size, &shape))
            memset(&shape, 0, sizeof(shape));
        status = STATUS_SUCCESS;
        if (shape.Size > size)
            status = STATUS_BUFFER_TOO_SMALL;
        else if (shape.Size && !NT_SUCCESS(MimeCopyToUser(data, image, shape.Size)))
            status = STATUS_ACCESS_VIOLATION;
        if (info && !NT_SUCCESS(MimeCopyToUser(info, &shape, sizeof(shape))) && NT_SUCCESS(status))
            status = STATUS_ACCESS_VIOLATION;
        W32Free(image);
        break;
    }
    case MIME_CAPOP_WINDOW: {
        MIME_CAPTURE_WINDOW w;
        WND *window = WndLookup((HWND)(ULONG_PTR)id);
        if (!window || !NT_SUCCESS(MimeCopyFromUser(&w, info, sizeof(w)))) {
            status = window ? STATUS_ACCESS_VIOLATION : STATUS_INVALID_HANDLE;
            break;
        }
        BOOLEAN client = (w.Flags & MIME_CAPTURE_CLIENT_ONLY) != 0;
        const RECT *r = client ? &window->ClientRect : &window->WindowRect;
        w.Width = (ULONG)(r->right - r->left);
        w.Height = (ULONG)(r->bottom - r->top);
        w.ClientX = client ? 0 : window->ClientRect.left - window->WindowRect.left;
        w.ClientY = client ? 0 : window->ClientRect.top - window->WindowRect.top;
        status = STATUS_SUCCESS;
        if (data) {
            ULONG64 need = (ULONG64)w.Width * w.Height * 4;
            PUCHAR pixels = need && need <= size ? W32Alloc((SIZE_T)need) : nullptr;
            if (need > size)
                status = STATUS_BUFFER_TOO_SMALL;
            else if (need && !pixels)
                status = STATUS_NO_MEMORY;
            else if (need && !ScreenWindowPixels(window, client, (w.Flags & MIME_CAPTURE_FULL_CONTENT) != 0, pixels,
                                                 (LONG)w.Width * 4, (LONG)w.Width, (LONG)w.Height))
                status = STATUS_INVALID_HANDLE;
            else if (need && !NT_SUCCESS(MimeCopyToUser(data, pixels, (SIZE_T)need)))
                status = STATUS_ACCESS_VIOLATION;
            W32Free(pixels);
        }
        if (!NT_SUCCESS(MimeCopyToUser(info, &w, sizeof(w))) && NT_SUCCESS(status))
            status = STATUS_ACCESS_VIOLATION;
        break;
    }
    }
    UserLeave();
    return status;
}
