/*
 * conhost.exe: the program that gives a console its window. it is started with the number of a console on its command
 * line, attaches to it through \Device\ConDrv\Host and runs the window until the console ends.
 */
#include "conhost.h"

Host g;

/* ------------------------------------------------------------------ the driver */

BOOL HostControl(ULONG code, void *in, ULONG in_size, void *out, ULONG out_size)
{
    IO_STATUS_BLOCK io = {};
    NTSTATUS status = NtDeviceIoControlFile(g.Device, nullptr, nullptr, nullptr, &io, code, in, in_size, out, out_size);
    return NT_SUCCESS(status);
}

static BOOL host_open(void)
{
    UNICODE_STRING name = RTL_CONSTANT_STRING(MIME_CONDRV_DEVICE L"\\" MIME_CONDRV_HOST);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, 0, nullptr, nullptr);
    IO_STATUS_BLOCK io;
    return NT_SUCCESS(NtCreateFile(&g.Device, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io, nullptr, 0,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0));
}

void HostSendRecords(const MIME_CONDRV_RECORD *records, ULONG count)
{
    while (count) {
        ULONG n = count > 256 ? 256 : count;
        HostControl(IOCTL_CONDRV_HOST_INPUT, (void *)records, n * sizeof(*records), nullptr, 0);
        records += n;
        count -= n;
    }
}

void HostResize(int columns, int rows)
{
    MIME_CONDRV_HOST_WINDOW w = {MIME_CONDRV_WINDOW_RESIZE, columns, rows, 0, 0};
    HostControl(IOCTL_CONDRV_HOST_WINDOW, &w, sizeof(w), nullptr, 0);
}

void HostScroll(int left, int top)
{
    MIME_CONDRV_HOST_WINDOW w = {MIME_CONDRV_WINDOW_SCROLL, 0, 0, left, top};
    HostControl(IOCTL_CONDRV_HOST_WINDOW, &w, sizeof(w), nullptr, 0);
}

void HostClose(void)
{
    HostControl(IOCTL_CONDRV_HOST_CLOSE, nullptr, 0, nullptr, 0);
}

void HostSetSelection(void)
{
    MIME_CONDRV_HOST_SELECTION s = {};
    if (g.Select.Active) {
        s.Flags = 1 | 2 | (g.Select.Dragging ? 8 : 0); /* in progress, not empty, mouse down */
        if (g.Select.Anchor.x != g.Select.End.x || g.Select.Anchor.y != g.Select.End.y || g.Select.Dragging)
            s.Flags |= 4; /* by the mouse */
        s.Anchor.X = (SHORT)g.Select.Anchor.x;
        s.Anchor.Y = (SHORT)g.Select.Anchor.y;
        s.Rect.Left = (SHORT)min(g.Select.Anchor.x, g.Select.End.x);
        s.Rect.Right = (SHORT)max(g.Select.Anchor.x, g.Select.End.x);
        s.Rect.Top = (SHORT)min(g.Select.Anchor.y, g.Select.End.y);
        s.Rect.Bottom = (SHORT)max(g.Select.Anchor.y, g.Select.End.y);
    }
    HostControl(IOCTL_CONDRV_HOST_SELECTION, &s, sizeof(s), nullptr, 0);
}

void HostFetchRows(int top, int rows, int left, int width, Cell *out)
{
    MIME_CONDRV_HOST_CELLS rq = {top, (ULONG)rows, left, (ULONG)width};
    ULONG size = (ULONG)rows * (ULONG)width * sizeof(Cell);
    if (size < sizeof(rq))
        size = sizeof(rq);
    /* the request and the cells share one buffer. */
    Cell *buffer = (Cell *)HeapAlloc(GetProcessHeap(), 0, size);
    if (!buffer)
        return;
    memcpy(buffer, &rq, sizeof(rq));
    if (HostControl(IOCTL_CONDRV_HOST_CELLS, buffer, sizeof(rq), buffer, size))
        memcpy(out, buffer, (SIZE_T)rows * (SIZE_T)width * sizeof(Cell));
    HeapFree(GetProcessHeap(), 0, buffer);
}

/* tells the console what the font and the screen allow. */
void HostFont(void)
{
    MIME_CONDRV_FONT_SET f = {};
    f.Size.X = (SHORT)g.CellWidth;
    f.Size.Y = (SHORT)g.CellHeight;
    f.Weight = 400;
    f.Family = 0x36; /* TMPF_TRUETYPE | FF_MODERN | FIXED_PITCH, as the console reports */
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int frame_w = 2 * (GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER)) + GetSystemMetrics(SM_CXVSCROLL);
    int frame_h = 2 * (GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER)) + GetSystemMetrics(SM_CYCAPTION);
    f.MaxColumns = (ULONG)max(1, (work.right - work.left - frame_w) / g.CellWidth);
    f.MaxRows = (ULONG)max(1, (work.bottom - work.top - frame_h) / g.CellHeight);
    lstrcpynW(f.Face, g_font_face, RTL_NUMBER_OF(f.Face));
    HostControl(IOCTL_CONDRV_HOST_FONT, &f, sizeof(f), nullptr, 0);
}

/* reads what changed in the console and brings the window up to date. */
void HostUpdate(void)
{
    MIME_CONDRV_HOST_STATE st;
    if (!HostControl(IOCTL_CONDRV_HOST_QUERY, nullptr, 0, &st, sizeof(st))) {
        g.Closing = TRUE;
        PostQuitMessage(0);
        return;
    }
    MIME_CONDRV_HOST_STATE old = g.State;
    g.State = st;
    if (st.Changed & MIME_CONDRV_CHANGED_END) {
        g.Closing = TRUE;
        if (g.Window)
            DestroyWindow(g.Window);
        PostQuitMessage(0);
        return;
    }
    WindowUpdate(&old, st.Changed, FALSE);
}

/* ------------------------------------------------------------------ start */

static ULONG64 parse_console(const WCHAR *line)
{
    const WCHAR *key = L"-Console";
    size_t n = lstrlenW(key);
    for (const WCHAR *p = line; *p; p++) {
        if (p[0] != L'-' || CompareStringOrdinal(p, (int)n, key, (int)n, TRUE) != CSTR_EQUAL)
            continue;
        const WCHAR *q = p + n;
        while (*q == L' ')
            q++;
        if (q[0] == L'0' && (q[1] == L'x' || q[1] == L'X'))
            q += 2;
        ULONG64 id = 0;
        for (;; q++) {
            int digit;
            if (*q >= L'0' && *q <= L'9')
                digit = *q - L'0';
            else if (*q >= L'a' && *q <= L'f')
                digit = *q - L'a' + 10;
            else if (*q >= L'A' && *q <= L'F')
                digit = *q - L'A' + 10;
            else
                break;
            id = id * 16 + (ULONG64)digit;
        }
        return id;
    }
    return 0;
}

void entry(void)
{
    g.Console = parse_console(GetCommandLineW());
    if (!g.Console || !host_open())
        ExitProcess(1);
    if (!WindowCreate())
        ExitProcess(2);
    g.Event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g.Event)
        ExitProcess(3);
    MIME_CONDRV_HOST_ATTACH attach = {g.Console, (ULONG64)(ULONG_PTR)g.Window, (ULONG64)(ULONG_PTR)g.Event};
    if (!HostControl(IOCTL_CONDRV_HOST_ATTACH, &attach, sizeof(attach), &g.State, sizeof(g.State))) {
        DestroyWindow(g.Window);
        ExitProcess(3);
    }
    HostFont();
    WindowUpdate(&g.State, MIME_CONDRV_CHANGED_ALL, TRUE);
    if (g.State.Changed & MIME_CONDRV_CHANGED_END) {
        DestroyWindow(g.Window);
        ExitProcess(0);
    }
    WindowRun();
    ExitProcess(0);
}
