/*
 * what win32k shares with user32, as windows 11 26300 does, measured there with csrdbgu.exe of scratch/dlls/probe
 * in the research repository. user32 connects to server dll 3 of csrss when it loads, with 0x248 bytes: a version,
 * then the 0x240 bytes it keeps as gSharedInfo. on windows winsrv passes them to NtUserProcessConnect, and win32k
 * maps two sections read-only in the client during that call and fills the bytes with addresses in them:
 *
 *   the server information, 0x8000 bytes: a heap whose block at 0x1040 is the structure user32 calls gpsi, with the
 *   display information at 0x6950, the tables of the messages of each window procedure, and the monitors. what
 *   points inside the section is an offset from its start, and the client is told that start.
 *
 *   the handle table, 0x200000 bytes of which 0xb000 had pages: entries of 0x20 bytes.
 *
 * the sections start as the cells of the reference. the procedures of ntdll, the colours and their brushes and the
 * monitors are mime's own. the rest is still what the reference machine held, a display of 1280 by 800 at 96 dpi:
 * the metrics, the fonts, the atoms of the classes and the handles of the bitmaps.
 */
#include "win32k.h"

typedef struct USER_CELL {
    USHORT Offset;
    UCHAR Base; /* 0 a value, 1 an offset in the server information, 2 one in the handle table. */
    ULONG64 Value;
} USER_CELL;

#include "usershared.inc"

#define USER_CONNECT_SIZE 0x248
#define USER_HANDLES_RESERVED 0x200000
#define USER_HANDLES_COMMITTED 0x100000 /* every handle of mime has its entry. the reference had 0xb000 bytes. */
#define USER_HANDLE_ENTRY 0x20
#define USER_PSI 0x1040
#define PSI_FLAG_API_HOOK 0x10u
#define PSI_HANDLE_ENTRIES 0x8
#define PSI_PFN_ANSI 0x188      /* 24 procedures. */
#define PSI_PFN_UNICODE 0x248   /* 24 procedures. */
#define PSI_PFN_WORKER_HEAD 0x308 /* the first 11 workers. */
#define PSI_COLOR_UNMATCHED 0x115c
#define PSI_COLOR 0x11d8
#define PSI_BRUSH 0x1258
#define PSI_PFN_WORKER 0x1d50   /* 18 workers. */
#define PSI_SYSMET 0x768
#define USER_COLORS 31

/*
 * the display information and the monitors, as user32 reads them for MonitorFromPoint, MonitorFromRect,
 * MonitorFromWindow, GetMonitorInfo and the screen metrics without a call, laid out after SetMonitorData and
 * zzzUpdateUserScreen of win32kbase 26300: the count of visible monitors, the primary, the first, the virtual
 * screen. a monitor is its handle, itself, the next, MONF_* flags, its rectangle and work area, its dpi, the
 * largest dpi of all monitors, the dpi again and its rectangle on the device. what points is an offset in the
 * section. the reference had its monitors at 0x6ff0, 0x7060 and 0x70d0, mime keeps its own after the heap.
 */
#define USER_DISPINFO 0x6950
#define DISPINFO_COUNT 0x00
#define DISPINFO_PRIMARY 0x08
#define DISPINFO_FIRST 0x10
#define DISPINFO_SCREEN 0x18
#define USER_MONITOR_FIRST 0x7200
#define USER_MONITOR_STRIDE 0x70
#define MON_HANDLE 0x00
#define MON_SELF 0x08
#define MON_NEXT 0x10
#define MON_FLAGS 0x18
#define MON_RECT 0x1c
#define MON_WORK 0x2c
#define MON_DPI 0x3c
#define MON_DPI_MAX 0x3e
#define MON_DPI_LOGICAL 0x40
#define MON_DEVICE_RECT 0x44
#define MON_SIZE 0x60
#define MONF_VISIBLE 0x1u
#define MONF_PALETTEDISPLAY 0x2u
#define HANDLE_TYPE 0x18
#define HANDLE_FLAGS 0x19
#define HANDLE_UNIQ 0x1a
#define HANDLE_TYPE_MONITOR 12
#define HANDLE_MONITOR_LIMIT 0x60 /* the handles of windows and the rest start there, class.c. */

static PVOID g_shared_section, g_handle_section;
static PUCHAR g_shared, g_handles;
static NTSTATUS g_status = STATUS_UNSUCCESSFUL;

PUCHAR UserSharedHandles(void)
{
    return g_handles;
}

ULONG UserSharedHandleCount(void)
{
    return USER_HANDLES_COMMITTED / USER_HANDLE_ENTRY;
}

static void cells(PUCHAR base, const USER_CELL *cell, ULONG count)
{
    for (ULONG i = 0; i < count; i++)
        *(PULONG64)(base + cell[i].Offset) = cell[i].Value;
}

/*
 * dwKeyCache and dwAsyncKeyCache of the server information, at 0x1b48 and 0x1b4c in 26300. GetKeyState and
 * GetAsyncKeyState of user32 answer the keys below 0x20, the mouse buttons and the modifiers among them, from a
 * cache in the client information of the thread while its own counter there equals this one, with no call. win32k
 * does not fill that cache, whose counters stay zero: these are never zero, so user32 always asks.
 */
/*
 * the foreground window at 0x1de0 of the server information, which GetForegroundWindow of user32 26300 reads through
 * ABI_Get_ForegroundWindow of win32u with no call. it stayed null, so no program ever saw itself in the foreground.
 */
void UserSharedForeground(HWND window)
{
    if (g_shared)
        *(PULONG64)(g_shared + USER_PSI + 0x1de0) = (ULONG64)(ULONG_PTR)window;
}

void UserSharedKeysChanged(void)
{
    if (!g_shared)
        return;
    PULONG cache = (PULONG)(g_shared + USER_PSI + 0x1b48);
    ULONG next = cache[0] + 1;
    cache[0] = cache[1] = next ? next : 1;
}

/* the monitors of the host where user32 reads them, whenever they change. */
void UserSharedMonitors(void)
{
    if (!g_shared || !g_handles)
        return;
    static const USHORT reference[] = {0x6ff0, 0x7060, 0x70d0};
    for (ULONG i = 0; i < RTL_NUMBER_OF(reference); i++)
        memset(g_shared + reference[i], 0, MON_SIZE);
    for (ULONG i = 1; i < HANDLE_MONITOR_LIMIT; i++) {
        PUCHAR entry = g_handles + (SIZE_T)i * USER_HANDLE_ENTRY;
        if (entry[HANDLE_TYPE] == HANDLE_TYPE_MONITOR)
            memset(entry, 0, USER_HANDLE_ENTRY);
    }
    ULONG count = MonitorCount();
    USHORT dpi_max = 0;
    MIME_MONITOR_INFO m;
    for (ULONG i = 0; MonitorGet(i, &m); i++)
        if (m.Dpi > dpi_max)
            dpi_max = (USHORT)m.Dpi;
    if (!dpi_max)
        dpi_max = 96;
    RECT screen = {0, 0, 0, 0};
    ULONG visible = 0;
    for (ULONG i = 0; i < count && i < MIME_MAX_MONITORS && MonitorGet(i, &m); i++) {
        ULONG offset = USER_MONITOR_FIRST + i * USER_MONITOR_STRIDE;
        PUCHAR mon = g_shared + offset;
        HMONITOR handle = MonitorHandle(i);
        memset(mon, 0, MON_SIZE);
        *(PULONG64)(mon + MON_HANDLE) = (ULONG64)(ULONG_PTR)handle;
        *(PULONG64)(mon + MON_SELF) = offset;
        *(PULONG64)(mon + MON_NEXT) = i + 1 < count ? offset + USER_MONITOR_STRIDE : 0;
        *(PULONG)(mon + MON_FLAGS) = MONF_VISIBLE | (W32Display.Bits == 8 ? MONF_PALETTEDISPLAY : 0);
        memcpy(mon + MON_RECT, &m.Rect, sizeof(RECT));
        memcpy(mon + MON_WORK, &m.Work, sizeof(RECT));
        USHORT dpi = (USHORT)(m.Dpi ? m.Dpi : 96);
        *(PUSHORT)(mon + MON_DPI) = dpi;
        *(PUSHORT)(mon + MON_DPI_MAX) = dpi_max;
        *(PUSHORT)(mon + MON_DPI_LOGICAL) = dpi;
        memcpy(mon + MON_DEVICE_RECT, &m.Rect, sizeof(RECT));
        PUCHAR entry = g_handles + (SIZE_T)((ULONG_PTR)handle & 0xFFFF) * USER_HANDLE_ENTRY;
        *(PULONG64)entry = offset;
        entry[HANDLE_TYPE] = HANDLE_TYPE_MONITOR;
        entry[HANDLE_FLAGS] = 0;
        *(PUSHORT)(entry + HANDLE_UNIQ) = (USHORT)((ULONG_PTR)handle >> 16);
        screen.left = W32Min(screen.left, m.Rect.left);
        screen.top = W32Min(screen.top, m.Rect.top);
        screen.right = W32Max(screen.right, m.Rect.right);
        screen.bottom = W32Max(screen.bottom, m.Rect.bottom);
        visible++;
    }
    PUCHAR info = g_shared + USER_DISPINFO;
    *(PULONG)(info + DISPINFO_COUNT) = visible;
    *(PULONG64)(info + DISPINFO_PRIMARY) = visible ? USER_MONITOR_FIRST : 0;
    *(PULONG64)(info + DISPINFO_FIRST) = visible ? USER_MONITOR_FIRST : 0;
    memcpy(info + DISPINFO_SCREEN, &screen, sizeof(screen));
    PUCHAR psi = g_shared + USER_PSI;
    *(PLONG)(psi + PSI_SYSMET + SM_CMONITORS * 4) = (LONG)visible;
    *(PLONG)(psi + PSI_SYSMET + SM_SAMEDISPLAYFORMAT * 4) = 1;
}

/* called with the lock of the window manager held, once the colours exist. */
static NTSTATUS build(void)
{
    PVOID base;
    NTSTATUS status = MmCreateSharedSection(USER_SHARED_SIZE, &base, &g_shared_section);
    if (!NT_SUCCESS(status))
        return status;
    g_shared = base;
    status = MmCreateReservedSection(USER_HANDLES_RESERVED, USER_HANDLES_COMMITTED, &base, &g_handle_section);
    if (!NT_SUCCESS(status))
        return status;
    g_handles = base;
    memset(g_shared, 0, USER_SHARED_SIZE);
    memset(g_handles, 0, USER_HANDLES_COMMITTED);
    cells(g_shared, g_user_shared, RTL_NUMBER_OF(g_user_shared));
    cells(g_handles, g_user_handles, RTL_NUMBER_OF(g_user_handles));
    PUCHAR psi = g_shared + USER_PSI;
    UserSharedKeysChanged();
    /* GetCapture answers no window with no call while this count of captures is zero. it is kept at one: user32 asks. */
    *(PULONG)(psi + 0x1b50) = 1;
    /* the reference had the api hook of the themes registered. mime registers none, and user32 asks for none. */
    *(PULONG)psi &= ~PSI_FLAG_API_HOOK;
    /* every entry with pages is counted, and all but the monitors are free. */
    *(PULONG64)(psi + PSI_HANDLE_ENTRIES) = USER_HANDLES_COMMITTED / USER_HANDLE_ENTRY;
    ULONG64 pfn[MIME_NTUSER_PFN_COUNT];
    status = PsQueryNtUserPfn(pfn);
    if (!NT_SUCCESS(status)) {
        DbgPrint("csr: ntdll has no table of window procedures: %08x\n", status);
        return status;
    }
    memcpy(psi + PSI_PFN_ANSI, pfn, 24 * 8);
    memcpy(psi + PSI_PFN_UNICODE, pfn + 24, 24 * 8);
    memcpy(psi + PSI_PFN_WORKER_HEAD, pfn + 48, 11 * 8);
    memcpy(psi + PSI_PFN_WORKER, pfn + 48, 18 * 8);
    for (int i = 0; i < USER_COLORS; i++) {
        COLORREF color = MetricsSysColor(i);
        *(PULONG)(psi + PSI_COLOR_UNMATCHED + i * 4) = color;
        *(PULONG)(psi + PSI_COLOR + i * 4) = color;
        *(PULONG64)(psi + PSI_BRUSH + i * 8) = (ULONG64)(ULONG_PTR)MetricsSysColorBrush(i);
    }
    UserSharedMonitors();
    DbgPrint("csr: the server information of win32k keeps the metrics, the fonts, the atoms and the bitmaps of the "
             "reference machine\n");
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI connect(PVOID data, ULONG length)
{
    if (length != USER_CONNECT_SIZE)
        return STATUS_INVALID_PARAMETER;
    UserEnter();
    if (!g_shared_section)
        g_status = build();
    NTSTATUS status = g_status;
    W32PROCESS *p = NT_SUCCESS(status) ? W32GetProcess() : nullptr;
    if (NT_SUCCESS(status) && !p)
        status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(status) && !p->SharedBase)
        status = MmMapReadOnlySection(g_shared_section, p->Process, &p->SharedBase);
    if (NT_SUCCESS(status) && !p->HandleBase)
        status = MmMapReadOnlySection(g_handle_section, p->Process, &p->HandleBase);
    /* the table of gdi comes with the first step into win32k, which this is for a program that loads user32. */
    if (NT_SUCCESS(status))
        status = GdiSharedMap(p);
    /* the thread that connects is one of the window manager from here on. */
    if (NT_SUCCESS(status))
        status = DeskAttachThread(p);
    if (NT_SUCCESS(status)) {
        memset(data, 0, USER_CONNECT_SIZE);
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_user_connect); i++) {
            const USER_CELL *cell = &g_user_connect[i];
            ULONG64 base = cell->Base == 1 ? (ULONG64)p->SharedBase : cell->Base == 2 ? (ULONG64)p->HandleBase : 0;
            *(PULONG64)((PUCHAR)data + cell->Offset) = base + cell->Value;
        }
        DbgPrint("csr: process %u connects to win32k, server information at %p, handle table at %p\n",
                 (ULONG)(ULONG_PTR)p->Pid, p->SharedBase, p->HandleBase);
    }
    UserLeave();
    return status;
}

void UserSharedInitialize(void)
{
    ExRegisterCsrServerDll(3, connect);
}

/*
 * user32 asks for the library of the registered api hook before it calls a hooked function, while the server
 * information says one is registered. windows 11 26100 answers 1 with the hook of the themes loaded. no hook is
 * registered in mime, so nothing is loaded.
 */
BOOL NTAPI NtUserLoadUserApiHook(void)
{
    return FALSE;
}
