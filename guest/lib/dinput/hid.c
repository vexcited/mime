/*
 * hid collections, as windows' dinput sees them: listed through setupapi
 * and hid.dll, each device's objects made from the caps of its preparsed
 * data, its reports read by the worker thread of the process and cooked
 * through the calibration of each axis. see docs/dinput.md.
 */
#include "dinput_private.h"
#include <mime/win32/hidpi.h>
#include <mime/win32/setupapi.h>

#define MAX_LIST 64
#define REBUILD_MS 20000
#define NO_JOYSTICK 17
#define MAX_INDICES DI_MAX_OBJECTS

#define DI_KEY L"System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\DirectInput\\"
#define OEM_KEY L"System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM\\"


/* ------------------------------------------------------------------ hid.dll and setupapi.dll */

static struct {
    BOOL tried, ok;
    void(NTAPI *GetHidGuid)(GUID *);
    BOOLEAN(NTAPI *GetPreparsedData)(HANDLE, PHIDP_PREPARSED_DATA *);
    BOOLEAN(NTAPI *FreePreparsedData)(PHIDP_PREPARSED_DATA);
    BOOLEAN(NTAPI *FlushQueue)(HANDLE);
    BOOLEAN(NTAPI *GetAttributes)(HANDLE, PHIDD_ATTRIBUTES);
    BOOLEAN(NTAPI *GetProductString)(HANDLE, PVOID, ULONG);
    NTSTATUS(NTAPI *GetCaps)(PHIDP_PREPARSED_DATA, PHIDP_CAPS);
    NTSTATUS(NTAPI *GetButtonCaps)(HIDP_REPORT_TYPE, PHIDP_BUTTON_CAPS, PUSHORT, PHIDP_PREPARSED_DATA);
    NTSTATUS(NTAPI *GetValueCaps)(HIDP_REPORT_TYPE, PHIDP_VALUE_CAPS, PUSHORT, PHIDP_PREPARSED_DATA);
    NTSTATUS(NTAPI *GetLinkCollectionNodes)(PHIDP_LINK_COLLECTION_NODE, PULONG, PHIDP_PREPARSED_DATA);
    ULONG(NTAPI *MaxDataListLength)(HIDP_REPORT_TYPE, PHIDP_PREPARSED_DATA);
    NTSTATUS(NTAPI *GetData)(HIDP_REPORT_TYPE, PHIDP_DATA, PULONG, PHIDP_PREPARSED_DATA, PCHAR, ULONG);
    HDEVINFO(WINAPI *GetClassDevs)(const GUID *, PCWSTR, HWND, DWORD);
    BOOL(WINAPI *EnumInterfaces)(HDEVINFO, PSP_DEVINFO_DATA, const GUID *, DWORD, PSP_DEVICE_INTERFACE_DATA);
    BOOL(WINAPI *InterfaceDetail)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W, DWORD,
                                  PDWORD, PSP_DEVINFO_DATA);
    BOOL(WINAPI *InstanceId)(HDEVINFO, PSP_DEVINFO_DATA, PWSTR, DWORD, PDWORD);
    BOOL(WINAPI *Destroy)(HDEVINFO);
} g_api;

/* windows' dinput loads them itself, and a missing function only fails the calls that need it. */
static BOOL load_api(void)
{
    if (g_api.tried)
        return g_api.ok;
    g_api.tried = TRUE;
    HMODULE hid = LoadLibraryW(L"HID.DLL"), setup = LoadLibraryW(L"SETUPAPI.DLL");
    if (!hid || !setup)
        return FALSE;
#define GET(module, field, name) (*(FARPROC *)&g_api.field = GetProcAddress(module, name))
    GET(hid, GetHidGuid, "HidD_GetHidGuid");
    GET(hid, GetPreparsedData, "HidD_GetPreparsedData");
    GET(hid, FreePreparsedData, "HidD_FreePreparsedData");
    GET(hid, FlushQueue, "HidD_FlushQueue");
    GET(hid, GetAttributes, "HidD_GetAttributes");
    GET(hid, GetProductString, "HidD_GetProductString");
    GET(hid, GetCaps, "HidP_GetCaps");
    GET(hid, GetButtonCaps, "HidP_GetButtonCaps");
    GET(hid, GetValueCaps, "HidP_GetValueCaps");
    GET(hid, GetLinkCollectionNodes, "HidP_GetLinkCollectionNodes");
    GET(hid, MaxDataListLength, "HidP_MaxDataListLength");
    GET(hid, GetData, "HidP_GetData");
    GET(setup, GetClassDevs, "SetupDiGetClassDevsW");
    GET(setup, EnumInterfaces, "SetupDiEnumDeviceInterfaces");
    GET(setup, InterfaceDetail, "SetupDiGetDeviceInterfaceDetailW");
    GET(setup, InstanceId, "SetupDiGetDeviceInstanceIdW");
    GET(setup, Destroy, "SetupDiDestroyDeviceInfoList");
#undef GET
    g_api.ok = g_api.GetHidGuid && g_api.GetPreparsedData && g_api.FreePreparsedData && g_api.FlushQueue &&
               g_api.GetAttributes && g_api.GetProductString && g_api.GetCaps && g_api.GetButtonCaps &&
               g_api.GetValueCaps && g_api.GetLinkCollectionNodes && g_api.MaxDataListLength && g_api.GetData &&
               g_api.GetClassDevs && g_api.EnumInterfaces && g_api.InterfaceDetail && g_api.InstanceId &&
               g_api.Destroy;
    return g_api.ok;
}

/* ------------------------------------------------------------------ the registry */

static void vidpid_name(WCHAR *out, USHORT vid, USHORT pid)
{
    wsprintfW(out, L"VID_%04X&PID_%04X", vid, pid);
}

/* the key of the nth device of a vendor and product, where its guid and joystick id live. */
static HKEY calibration_key(USHORT vid, USHORT pid, DWORD slot)
{
    WCHAR path[200], name[32];
    vidpid_name(name, vid, pid);
    wsprintfW(path, DI_KEY L"%s\\Calibration\\%lu", name, slot);
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr))
        return nullptr;
    return key;
}

/* the oem key of a vendor and product, the user's copy before the machine's. */
static HKEY oem_key(USHORT vid, USHORT pid, BOOL write)
{
    WCHAR path[200], name[32];
    vidpid_name(name, vid, pid);
    wsprintfW(path, OEM_KEY L"%s", name);
    HKEY key = nullptr;
    if (write) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, nullptr))
            return nullptr;
        return key;
    }
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &key) ||
        !RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key))
        return key;
    return nullptr;
}

/* a version 1 style guid from the local time and a shared counter, with dinput's own node. */
static void fake_uuid(GUID *g)
{
    SYSTEMTIME st;
    FILETIME ft;
    GetLocalTime(&st);
    SystemTimeToFileTime(&st, &ft);
    ULONG64 t = ((ULONG64)ft.dwHighDateTime << 32 | ft.dwLowDateTime) + 0x146BF33E42C000ull;
    g->Data1 = (DWORD)t;
    DWORD high = ((DWORD)(t >> 32) & 0x0FFFFFFF) | 0x10000000;
    g->Data2 = (WORD)high;
    g->Data3 = (WORD)(high >> 16);
    LONG sequence = DiNextUuidCounter() & 0x3FFF;
    g->Data4[0] = (BYTE)(sequence >> 8) | 0x80;
    g->Data4[1] = (BYTE)sequence;
    memcpy(g->Data4 + 2, "DEST\0\0", 6);
}

/* ------------------------------------------------------------------ the list */

typedef struct ENTRY {
    WCHAR path[DI_MAX_PATH];
    GUID instance, product;
    USHORT vid, pid, page, usage;
    BYTE type7, type8;
    DWORD joystick;
    DWORD slot;
} ENTRY;

static ENTRY g_list[MAX_LIST];
static DWORD g_count;
static DWORD g_built;
static BOOL g_ever_built;

static BYTE list_type8(USHORT page, USHORT usage)
{
    if (page == 1)
        switch (usage) {
        case 1:
        case 2:
            return DI8DEVTYPE_MOUSE;
        case 4:
            return DI8DEVTYPE_JOYSTICK;
        case 5:
            return DI8DEVTYPE_GAMEPAD;
        case 6:
            return DI8DEVTYPE_KEYBOARD;
        }
    return DI8DEVTYPE_DEVICE;
}

static BYTE list_type7(USHORT page, USHORT usage)
{
    switch (list_type8(page, usage)) {
    case DI8DEVTYPE_MOUSE:
        return DIDEVTYPE_MOUSE;
    case DI8DEVTYPE_JOYSTICK:
    case DI8DEVTYPE_GAMEPAD:
        return DIDEVTYPE_JOYSTICK;
    case DI8DEVTYPE_KEYBOARD:
        return DIDEVTYPE_KEYBOARD;
    default:
        return DIDEVTYPE_DEVICE;
    }
}

static HANDLE open_path(const WCHAR *path, DWORD flags)
{
    return CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                       flags, nullptr);
}

/* a new interface: listed when it opens for reading and writing and its caps read. */
static BOOL add_entry(const WCHAR *path)
{
    if (g_count >= MAX_LIST)
        return FALSE;
    HANDLE file = open_path(path, 0);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    PHIDP_PREPARSED_DATA ppd;
    HIDP_CAPS caps;
    HIDD_ATTRIBUTES attributes = {sizeof(attributes)};
    BOOL ok = g_api.GetPreparsedData(file, &ppd);
    if (ok) {
        ok = g_api.GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS && g_api.GetAttributes(file, &attributes);
        g_api.FreePreparsedData(ppd);
    }
    CloseHandle(file);
    if (!ok)
        return FALSE;
    ENTRY *e = &g_list[g_count];
    memset(e, 0, sizeof(*e));
    DiCopyW(e->path, path, DI_MAX_PATH);
    e->vid = attributes.VendorID;
    e->pid = attributes.ProductID;
    e->page = caps.UsagePage;
    e->usage = caps.Usage;
    e->type8 = list_type8(e->page, e->usage);
    e->type7 = list_type7(e->page, e->usage);
    for (DWORD i = 0; i < g_count; i++)
        if (g_list[i].vid == e->vid && g_list[i].pid == e->pid)
            e->slot++;
    e->product = (GUID){MAKELONG(e->vid, e->pid), 0, 0, {0, 0, 'P', 'I', 'D', 'V', 'I', 'D'}};
    e->joystick = NO_JOYSTICK;
    HKEY key = calibration_key(e->vid, e->pid, e->slot);
    if (key) {
        DWORD size = sizeof(e->instance);
        if (RegQueryValueExW(key, L"GUID", nullptr, nullptr, (BYTE *)&e->instance, &size) || size != sizeof(GUID)) {
            fake_uuid(&e->instance);
            RegSetValueExW(key, L"GUID", 0, REG_BINARY, (const BYTE *)&e->instance, sizeof(GUID));
        }
        size = sizeof(e->joystick);
        if (RegQueryValueExW(key, L"Joystick Id", nullptr, nullptr, (BYTE *)&e->joystick, &size))
            e->joystick = NO_JOYSTICK;
        RegCloseKey(key);
    } else {
        fake_uuid(&e->instance);
    }
    g_count++;
    return TRUE;
}

/* game controllers keep a joystick id below 16 they own alone; the others get the lowest free one. */
static void assign_joystick_ids(void)
{
    DWORD taken = 0;
    for (DWORD i = 0; i < g_count; i++) {
        ENTRY *e = &g_list[i];
        if (e->type8 < DI8DEVTYPE_JOYSTICK)
            continue;
        if (e->joystick < 16 && !(taken & 1u << e->joystick))
            taken |= 1u << e->joystick;
        else
            e->joystick = NO_JOYSTICK;
    }
    for (DWORD i = 0; i < g_count; i++) {
        ENTRY *e = &g_list[i];
        if (e->type8 < DI8DEVTYPE_JOYSTICK || e->joystick != NO_JOYSTICK)
            continue;
        for (DWORD id = 0; id < 16; id++)
            if (!(taken & 1u << id)) {
                taken |= 1u << id;
                e->joystick = id;
                HKEY key = calibration_key(e->vid, e->pid, e->slot);
                if (key) {
                    RegSetValueExW(key, L"Joystick Id", 0, REG_BINARY, (const BYTE *)&id, sizeof(id));
                    RegCloseKey(key);
                }
                break;
            }
    }
}

/* EnumDevices always rebuilds; other callers at most every 20 seconds. */
void DiHidRebuild(BOOL force)
{
    RtlEnterCriticalSection(&DiLock);
    DWORD now = GetTickCount();
    if (!load_api() || (!force && g_ever_built && now - g_built < REBUILD_MS)) {
        RtlLeaveCriticalSection(&DiLock);
        return;
    }
    g_built = now;
    g_ever_built = TRUE;
    /* entries whose path no longer opens go first. */
    DWORD kept = 0;
    for (DWORD i = 0; i < g_count; i++) {
        HANDLE file = open_path(g_list[i].path, 0);
        if (file == INVALID_HANDLE_VALUE)
            continue;
        CloseHandle(file);
        g_list[kept++] = g_list[i];
    }
    g_count = kept;
    GUID hid;
    g_api.GetHidGuid(&hid);
    HDEVINFO set = g_api.GetClassDevs(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set != INVALID_HANDLE_VALUE) {
        union {
            SP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
            BYTE bytes[sizeof(DWORD) + DI_MAX_PATH * sizeof(WCHAR)];
        } u;
        SP_DEVICE_INTERFACE_DATA data = {sizeof(data)};
        for (DWORD i = 0; i < MAX_LIST && g_api.EnumInterfaces(set, nullptr, &hid, i, &data); i++) {
            u.detail.cbSize = sizeof(u.detail);
            if (!g_api.InterfaceDetail(set, &data, &u.detail, sizeof(u), nullptr, nullptr))
                continue;
            BOOL known = FALSE;
            for (DWORD j = 0; j < g_count && !known; j++)
                known = !lstrcmpiW(g_list[j].path, u.detail.DevicePath);
            if (!known)
                add_entry(u.detail.DevicePath);
        }
        g_api.Destroy(set);
    }
    assign_joystick_ids();
    RtlLeaveCriticalSection(&DiLock);
}

DWORD DiHidCount(void)
{
    return g_count;
}

BOOL DiHidEntry(DWORD index, GUID *instance, BYTE *type7, BYTE *type8)
{
    RtlEnterCriticalSection(&DiLock);
    BOOL ok = index < g_count;
    if (ok) {
        *instance = g_list[index].instance;
        *type7 = g_list[index].type7;
        *type8 = g_list[index].type8;
    }
    RtlLeaveCriticalSection(&DiLock);
    return ok;
}

/* an instance guid, a product guid, then GUID_Joystick with a joystick id in its low nibble. */
BOOL DiHidLookup(const GUID *guid, DWORD *index)
{
    BOOL found = FALSE;
    RtlEnterCriticalSection(&DiLock);
    for (DWORD i = 0; i < g_count && !found; i++)
        if (IsEqualGUID(guid, &g_list[i].instance))
            *index = i, found = TRUE;
    for (DWORD i = 0; i < g_count && !found; i++)
        if (IsEqualGUID(guid, &g_list[i].product))
            *index = i, found = TRUE;
    GUID joystick = *guid;
    DWORD id = joystick.Data1 & 0xF;
    joystick.Data1 &= ~0xFu;
    if (!found && IsEqualGUID(&joystick, &GUID_Joystick))
        for (DWORD i = 0; i < g_count && !found; i++)
            if (g_list[i].joystick == id)
                *index = i, found = TRUE;
    RtlLeaveCriticalSection(&DiLock);
    return found;
}

BOOL DiHidFindPath(const WCHAR *path, GUID *instance)
{
    BOOL found = FALSE;
    RtlEnterCriticalSection(&DiLock);
    for (DWORD i = 0; i < g_count && !found; i++)
        if (!lstrcmpiW(path, g_list[i].path)) {
            *instance = g_list[i].instance;
            found = TRUE;
        }
    RtlLeaveCriticalSection(&DiLock);
    return found;
}

/* the device a winmm joystick id names. */
BOOL DiHidByJoystick(DWORD id, GUID *instance, USHORT *vid, USHORT *pid, USHORT *usage)
{
    BOOL found = FALSE;
    RtlEnterCriticalSection(&DiLock);
    for (DWORD i = 0; i < g_count && !found; i++)
        if (g_list[i].joystick == id) {
            *instance = g_list[i].instance;
            *vid = g_list[i].vid;
            *pid = g_list[i].pid;
            *usage = g_list[i].usage;
            found = TRUE;
        }
    RtlLeaveCriticalSection(&DiLock);
    return found;
}

/* ------------------------------------------------------------------ calibration */

typedef struct CAL {
    LONG cmin, cctr, cmax; /* raw calibration */
    LONG rmin, rmax, rctr; /* the range reported */
    DWORD dz, sat, mode;
    BOOL legacy_raw; /* dinput.dll before 7: raw values of a signed range come back from 0 */
    DWORD cpoints;
    CPOINT cp[MAXCPOINTSNUM];
    LONG low_sat, low_len, low_out, low_span, hi_dz, hi_len, hi_out, hi_span;
} CAL;

/* windows' CCal_RecalcRange: each half of the raw range around the center, past the dead zone up to the saturation. */
static void recalc(CAL *c)
{
    LONG s = (LONG)(c->sat > c->dz ? c->sat : c->dz), dz = (LONG)c->dz;
    c->low_sat = c->cctr - MulDiv(c->cctr - c->cmin, s, 10000);
    c->low_len = c->cctr - c->low_sat - MulDiv(c->cctr - c->cmin, dz, 10000);
    c->low_out = c->rmin;
    c->low_span = c->rctr - c->rmin;
    BOOL wide = (ULONG)c->cmax > (ULONG)(c->cctr + 1);
    c->hi_dz = c->cctr + MulDiv(c->cmax - c->cctr, dz, 10000) + (wide ? 1 : 0);
    c->hi_len = MulDiv(c->cmax - c->cctr, s, 10000) + c->cctr - c->hi_dz;
    c->hi_out = c->rctr + (c->rmax > c->rctr && wide ? 1 : 0);
    c->hi_span = c->rmax - c->hi_out;
    if (c->cpoints >= 2) {
        c->cp[0].lP = c->cmin;
        c->cp[c->cpoints - 1].lP = c->cmax;
    }
}

static LONG cook(const CAL *c, LONG x)
{
    if (c->mode == DIPROPCALIBRATIONMODE_RAW)
        return c->legacy_raw ? x - c->cmin : x;
    BOOL hi = x >= c->hi_dz;
    LONG base = hi ? c->hi_dz : c->low_sat, d;
    if (x <= base) {
        d = 0;
    } else {
        LONG off = x - base, len = hi ? c->hi_len : c->low_len, span = hi ? c->hi_span : c->low_span;
        d = (ULONG)off < (ULONG)len ? MulDiv(off, span, len) : span;
    }
    LONG out = (hi ? c->hi_out : c->low_out) + d;
    if (c->cpoints > 2 && x >= c->low_sat && x <= c->hi_dz + c->hi_len &&
        (x <= c->low_sat + c->low_len || x >= c->hi_dz)) {
        for (DWORD i = 0; i + 1 < c->cpoints; i++)
            if (c->cp[i].lP <= x && x < c->cp[i + 1].lP) {
                LONG log = (LONG)c->cp[i].dwLog + MulDiv(x - c->cp[i].lP, (LONG)(c->cp[i + 1].dwLog - c->cp[i].dwLog),
                                                         c->cp[i + 1].lP - c->cp[i].lP);
                out = c->rmin + MulDiv(log, c->rmax - c->rmin + 1, 10000);
                break;
            }
    }
    return out;
}

/* ------------------------------------------------------------------ a device */

enum { OBJ_NONE, OBJ_VALUE, OBJ_BUTTON, OBJ_COLLECTION };

typedef struct HIDOBJ {
    BYTE kind;
    BYTE report_type; /* HidP_* */
    BOOL pov, absolute, calibrated, is_signed;
    USHORT index;     /* the data index */
    BYTE bits;
    LONG lmin, lmax, pmin, pmax;
    WORD granularity;
    CAL cal;
} HIDOBJ;

typedef struct READER READER;

typedef struct HIDDEV {
    ENTRY entry;
    PHIDP_PREPARSED_DATA ppd;
    HIDP_CAPS caps;
    HIDOBJ obj[DI_MAX_OBJECTS];
    int by_index[MAX_INDICES]; /* the input object of each data index, -1 for none */
    ULONG raw[MAX_INDICES];
    BYTE native[DI_MAX_STATE];
    HIDP_DATA *data;
    ULONG max_data;
    DWORD oem_flags;
    BOOL unplugged;
    READER *reader;
} HIDDEV;

/* what a hid value's usage makes of it: its guid, the axis slot it takes and the flag of the final type. */
typedef struct USAGE_INFO {
    USHORT page, usage;
    const GUID *guid;
    int slot;
    DWORD flag;
} USAGE_INFO;

static const USAGE_INFO USAGES[] = {
    {0x01, 0x30, &GUID_XAxis, 0, 0x1},      {0x01, 0x31, &GUID_YAxis, 1, 0x2},      {0x01, 0x32, &GUID_ZAxis, 2, 0x4},
    {0x01, 0x38, &GUID_ZAxis, 2, 0x4},      {0x01, 0x33, &GUID_RxAxis, 3, 0x8},     {0x01, 0x34, &GUID_RyAxis, 4, 0x10},
    {0x01, 0x35, &GUID_RzAxis, 5, 0x20},    {0x01, 0x36, &GUID_Slider, 6, 0x4000},  {0x01, 0x37, &GUID_Slider, 6, 0x4000},
    {0x01, 0x39, &GUID_POV, 7, 0x80},       {0x02, 0xC8, &GUID_XAxis, 0, 0x1000},   {0x02, 0xC4, &GUID_YAxis, 1, 0x100},
    {0x02, 0xC5, &GUID_RzAxis, 5, 0x200},   {0x02, 0xBA, &GUID_RzAxis, 5, 0x2000},  {0x02, 0xBB, &GUID_Slider, 6, 0x40},
    {0x05, 0x20, &GUID_POV, 7, 0x80},
};

static const USAGE_INFO *usage_info(USHORT page, USHORT usage)
{
    for (DWORD i = 0; i < RTL_NUMBER_OF(USAGES); i++)
        if (USAGES[i].page == page && USAGES[i].usage == usage)
            return &USAGES[i];
    return nullptr;
}

/* the pages whose input values are axes. */
static BOOL axis_page(USHORT page, USHORT usage)
{
    switch (page) {
    case 0x01:
        return usage != 0x3A && usage != 0x3B;
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x07:
    case 0x0C:
    case 0x0D:
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL button_page(USHORT page, USHORT usage)
{
    switch (page) {
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x0C:
    case 0x0D:
        return TRUE;
    case 0x09:
        return usage != 0;
    default:
        return FALSE;
    }
}

/* windows' fixups of a value's ranges, false when it cannot be read. */
static BOOL fix_ranges(HIDOBJ *o, const HIDP_VALUE_CAPS *v)
{
    LONG n = v->BitSize ? v->BitSize : 1;
    LONG smin = n >= 32 ? (LONG)0x80000000 : -(1 << (n - 1)), smax = n >= 32 ? 0x7FFFFFFF : (1 << (n - 1)) - 1;
    LONG umax = n >= 32 ? -1 : (LONG)((1u << n) - 1);
    o->bits = (BYTE)n;
    o->lmin = v->LogicalMin;
    o->lmax = v->LogicalMax;
    o->pmin = v->PhysicalMin;
    o->pmax = v->PhysicalMax;
    if (o->lmin >= o->lmax) {
        if (o->pmin < o->pmax) {
            o->lmin = o->pmin;
            o->lmax = o->pmax;
        } else {
            o->lmin = smin;
            o->lmax = smax;
        }
    }
    if (o->pmin > o->pmax) {
        o->lmin = o->pmin = 0;
        o->lmax = o->pmax = umax;
        o->is_signed = FALSE;
        return TRUE;
    }
    if (o->pmin == o->pmax) {
        o->pmin = o->lmin;
        o->pmax = o->lmax;
    }
    if (o->lmin >= smin && o->lmax <= smax) {
        o->is_signed = TRUE;
        return TRUE;
    }
    if (o->lmin >= 0 && (n >= 32 || (ULONG)o->lmax <= (ULONG)umax)) {
        o->is_signed = FALSE;
        return TRUE;
    }
    if (v->UsagePage < 0xFF00)
        return FALSE;
    o->lmin = smin;
    o->lmax = smax;
    o->is_signed = TRUE;
    return TRUE;
}

static LONG extend(const HIDOBJ *o, ULONG raw)
{
    if (o->bits >= 32)
        return (LONG)raw;
    ULONG mask = (1u << o->bits) - 1;
    raw &= mask;
    if (o->is_signed && (raw & (1u << (o->bits - 1))))
        raw |= ~mask;
    return (LONG)raw;
}

/* the calibration a new absolute axis starts with: its logical range, the full default range, no dead zone. */
static void default_cal(HIDOBJ *o)
{
    CAL *c = &o->cal;
    memset(c, 0, sizeof(*c));
    c->cmin = o->lmin;
    c->cmax = o->lmax;
    if (o->lmin >= o->lmax) {
        c->cmin = 0;
        c->cmax = 655;
    }
    c->cctr = c->cmin + (LONG)((ULONG)(c->cmax - c->cmin + 1) >> 1);
    c->rmin = 0;
    c->rmax = 0xFFFF;
    c->rctr = 0x7FFF;
    c->sat = 10000;
    c->cpoints = 2;
    c->cp[0] = (CPOINT){c->cmin, 0};
    c->cp[1] = (CPOINT){c->cmax, 10000};
    recalc(c);
}

/* the value an object's slot of the native state holds for a raw value. */
static LONG value_of(const HIDOBJ *o, ULONG raw)
{
    LONG x = extend(o, raw);
    if (o->pov)
        return x < o->lmin || x > o->lmax ? -1 : (LONG)o->granularity * (x - o->lmin);
    if (o->calibrated)
        return cook(&o->cal, x);
    return x;
}

static void initial_state(DIDEV *dev, HIDDEV *h)
{
    memset(h->native, 0, sizeof(h->native));
    for (DWORD i = 0; i < dev->count; i++) {
        const HIDOBJ *o = &h->obj[i];
        if (o->kind != OBJ_VALUE)
            continue;
        LONG v = o->pov ? -1 : o->calibrated ? (o->cal.mode == DIPROPCALIBRATIONMODE_RAW ? (o->lmin + o->lmax) / 2 : o->cal.rctr) : 0;
        memcpy(h->native + dev->objects[i].ofs, &v, sizeof(v));
    }
}

typedef struct CAPS_SET {
    HIDP_VALUE_CAPS *values;
    USHORT value_count;
    HIDP_BUTTON_CAPS *buttons;
    USHORT button_count;
} CAPS_SET;

static void sort_values(HIDP_VALUE_CAPS *v, USHORT n)
{
    for (USHORT i = 1; i < n; i++)
        for (USHORT j = i; j && (v[j - 1].Range.DataIndexMin > v[j].Range.DataIndexMin ||
                                 (v[j - 1].Range.DataIndexMin == v[j].Range.DataIndexMin && v[j - 1].IsAlias &&
                                  !v[j].IsAlias));
             j--) {
            HIDP_VALUE_CAPS t = v[j];
            v[j] = v[j - 1];
            v[j - 1] = t;
        }
}

static void sort_buttons(HIDP_BUTTON_CAPS *b, USHORT n)
{
    for (USHORT i = 1; i < n; i++)
        for (USHORT j = i; j && (b[j - 1].Range.DataIndexMin > b[j].Range.DataIndexMin ||
                                 (b[j - 1].Range.DataIndexMin == b[j].Range.DataIndexMin && b[j - 1].IsAlias &&
                                  !b[j].IsAlias));
             j--) {
            HIDP_BUTTON_CAPS t = b[j];
            b[j] = b[j - 1];
            b[j - 1] = t;
        }
}

static BOOL read_caps(HIDDEV *h, HIDP_REPORT_TYPE type, USHORT values, USHORT buttons, CAPS_SET *out)
{
    out->value_count = values;
    out->button_count = buttons;
    out->values = values ? HeapAlloc(GetProcessHeap(), 0, values * sizeof(*out->values)) : nullptr;
    out->buttons = buttons ? HeapAlloc(GetProcessHeap(), 0, buttons * sizeof(*out->buttons)) : nullptr;
    if ((values && !out->values) || (buttons && !out->buttons))
        return FALSE;
    if (values && g_api.GetValueCaps(type, out->values, &out->value_count, h->ppd) != HIDP_STATUS_SUCCESS)
        out->value_count = 0;
    if (buttons && g_api.GetButtonCaps(type, out->buttons, &out->button_count, h->ppd) != HIDP_STATUS_SUCCESS)
        out->button_count = 0;
    sort_values(out->values, out->value_count);
    sort_buttons(out->buttons, out->button_count);
    return TRUE;
}

static void free_caps(CAPS_SET *c)
{
    HeapFree(GetProcessHeap(), 0, c->values);
    HeapFree(GetProcessHeap(), 0, c->buttons);
}

/* windows' refusal of a value range that is not one usage, a bug its code has. */
static BOOL bad_ranges(const CAPS_SET *c)
{
    for (USHORT i = 0; i < c->value_count; i++)
        if (c->values[i].IsRange && c->values[i].Range.UsageMax - c->values[i].Range.UsageMin != 0 &&
            c->values[i].Range.UsageMax - c->values[i].Range.UsageMin != -1)
            return TRUE;
    return FALSE;
}

/* the objects of the values of a report type, numbered by their data index from base. */
static void make_values(DIDEV *dev, HIDDEV *h, const CAPS_SET *c, HIDP_REPORT_TYPE type, DWORD base)
{
    for (USHORT i = 0; i < c->value_count; i++) {
        const HIDP_VALUE_CAPS *v = &c->values[i];
        DWORD count = v->IsRange ? (DWORD)(v->Range.DataIndexMax - v->Range.DataIndexMin + 1) : 1;
        for (DWORD k = 0; k < count; k++) {
            DWORD number = base + v->Range.DataIndexMin + k;
            if (number >= DI_MAX_OBJECTS || h->obj[number].kind)
                continue;
            USHORT usage = (USHORT)(v->Range.UsageMin + (v->IsRange ? k : 0));
            HIDOBJ *o = &h->obj[number];
            DIOBJECT *d = &dev->objects[number];
            o->kind = OBJ_VALUE;
            o->report_type = (BYTE)type;
            o->index = (USHORT)(v->Range.DataIndexMin + k);
            o->absolute = v->IsAbsolute;
            if (!fix_ranges(o, v))
                o->kind = OBJ_NONE;
            const USAGE_INFO *info = type == HidP_Input ? usage_info(v->UsagePage, usage) : nullptr;
            d->guid = info ? info->guid : &GUID_Unknown;
            d->type = DIDFT_MAKEINSTANCE(number);
            if (v->IsAlias)
                d->type |= DIDFT_ALIAS;
            if (v->UsagePage >= 0xFF00) {
                d->type |= DIDFT_VENDORDEFINED;
            } else if (info && info->guid == &GUID_POV) {
                d->type |= DIDFT_POV;
                o->pov = TRUE;
                if (!v->IsRange)
                    o->granularity = (WORD)(36000 / (o->lmax - o->lmin + 1));
            } else if (type == HidP_Input && axis_page(v->UsagePage, usage)) {
                d->type |= v->IsAbsolute ? DIDFT_ABSAXIS : DIDFT_RELAXIS;
            }
            if (type == HidP_Output)
                d->type |= DIDFT_OUTPUT | DIDFT_NODATA;
            else if (type == HidP_Feature)
                d->type |= DIDFT_OUTPUT;
            if (type == HidP_Input) {
                if (!v->Units)
                    d->flags = info && info->slot <= 6 ? DIDOI_ASPECTPOSITION : 0;
                else if ((v->Units & 0xF) == 0x1 || (v->Units & 0xF) == 0x2 || (v->Units & 0xF) == 0x3 ||
                         (v->Units & 0xF) == 0x4)
                    switch (v->Units & 0xFF00) {
                    case 0x0000:
                        d->flags = DIDOI_ASPECTPOSITION;
                        break;
                    case 0xF000:
                        d->flags = DIDOI_ASPECTVELOCITY;
                        break;
                    case 0xE000:
                        d->flags = DIDOI_ASPECTACCEL;
                        break;
                    case 0xE100:
                        d->flags = DIDOI_ASPECTFORCE;
                        break;
                    }
            } else {
                d->flags = 0x80008000;
            }
            d->page = v->UsagePage;
            d->usage = usage;
            d->collection = v->LinkCollection;
            d->designator = (WORD)(v->Range.DesignatorMin + k > v->Range.DesignatorMax ? v->Range.DesignatorMax
                                                                                     : v->Range.DesignatorMin + k);
            d->dimension = v->Units;
            d->exponent = (WORD)v->UnitsExp;
            d->report = v->ReportID;
            if (o->kind == OBJ_VALUE && type == HidP_Input && o->absolute && (d->type & DIDFT_ABSAXIS)) {
                o->calibrated = TRUE;
                default_cal(o);
            }
            if (type == HidP_Input && o->index < MAX_INDICES)
                h->by_index[o->index] = (int)number;
        }
    }
}

static void make_buttons(DIDEV *dev, HIDDEV *h, const CAPS_SET *c, HIDP_REPORT_TYPE type, DWORD base)
{
    for (USHORT i = 0; i < c->button_count; i++) {
        const HIDP_BUTTON_CAPS *b = &c->buttons[i];
        DWORD count = b->IsRange ? (DWORD)(b->Range.DataIndexMax - b->Range.DataIndexMin + 1) : 1;
        for (DWORD k = 0; k < count; k++) {
            DWORD number = base + b->Range.DataIndexMin + k;
            if (number >= DI_MAX_OBJECTS || h->obj[number].kind)
                continue;
            USHORT usage = (USHORT)(b->Range.UsageMin + (b->IsRange ? k : 0));
            HIDOBJ *o = &h->obj[number];
            DIOBJECT *d = &dev->objects[number];
            o->kind = OBJ_BUTTON;
            o->report_type = (BYTE)type;
            o->index = (USHORT)(b->Range.DataIndexMin + k);
            d->type = DIDFT_MAKEINSTANCE(number);
            if (b->IsAlias)
                d->type |= DIDFT_ALIAS;
            if (b->UsagePage >= 0xFF00) {
                d->guid = &GUID_Unknown;
                d->type |= DIDFT_VENDORDEFINED;
            } else if (b->UsagePage == 0x07) {
                d->guid = &GUID_Key;
                d->type |= DIDFT_PSHBUTTON;
            } else if (button_page(b->UsagePage, usage)) {
                d->guid = &GUID_Button;
                d->type |= DIDFT_PSHBUTTON;
            } else {
                d->guid = &GUID_Unknown;
            }
            if (type == HidP_Output)
                d->type |= DIDFT_OUTPUT | DIDFT_NODATA;
            else if (type == HidP_Feature)
                d->type |= DIDFT_OUTPUT;
            d->flags = type == HidP_Input ? 0 : 0x80008000;
            d->page = b->UsagePage;
            d->usage = usage;
            d->collection = b->LinkCollection;
            d->designator = (WORD)(b->Range.DesignatorMin + k > b->Range.DesignatorMax ? b->Range.DesignatorMax
                                                                                     : b->Range.DesignatorMin + k);
            d->report = b->ReportID;
            if (type == HidP_Input && o->index < MAX_INDICES)
                h->by_index[o->index] = (int)number;
        }
    }
}

/* windows' CHid_MungeNotKeyboard: instances from usages, the array order kept. */
static void renumber(DIDEV *dev, HIDDEV *h)
{
    DWORD top_button = 0;
    for (DWORD i = 0; i < dev->count; i++)
        if (h->obj[i].kind == OBJ_BUTTON && dev->objects[i].page == 9 && dev->objects[i].usage > top_button)
            top_button = dev->objects[i].usage;
    DWORD next_button = top_button, next_axis = 6, next_pov = 0, next_collection = 0;
    DWORD claimed = 0;
    /* axes of the table first take their slots, then sliders and the rest. */
    for (int pass = 0; pass < 2; pass++)
        for (DWORD i = 0; i < dev->count; i++) {
            DIOBJECT *d = &dev->objects[i];
            if (h->obj[i].kind != OBJ_VALUE || (d->type & DIDFT_ALIAS) || !(d->type & DIDFT_AXIS) || h->obj[i].pov)
                continue;
            const USAGE_INFO *info = usage_info(d->page, d->usage);
            DWORD instance;
            if (pass == 0) {
                if (!info || info->slot > 5 || (claimed & 1u << info->slot) || d->page != 1)
                    continue;
                instance = (DWORD)info->slot;
            } else {
                if (info && info->slot <= 5 && d->page == 1 && DIDFT_GETINSTANCE(d->type) == (DWORD)info->slot &&
                    (claimed & 1u << info->slot))
                    continue;
                if (info && info->slot == 6 && !(claimed & 1u << 2))
                    instance = 2;
                else {
                    while (claimed & 1u << next_axis)
                        next_axis++;
                    instance = next_axis;
                }
            }
            claimed |= 1u << instance;
            d->type = (d->type & ~0x00FFFF00u) | DIDFT_MAKEINSTANCE(instance);
        }
    for (DWORD i = 0; i < dev->count; i++) {
        DIOBJECT *d = &dev->objects[i];
        if (d->type & DIDFT_ALIAS) {
            d->type &= 0x0C000000 | 0xFF | DIDFT_OUTPUT;
            continue;
        }
        DWORD instance;
        if (h->obj[i].kind == OBJ_BUTTON)
            instance = d->page == 9 && d->usage ? d->usage - 1u : next_button++;
        else if (h->obj[i].kind == OBJ_VALUE && h->obj[i].pov)
            instance = next_pov++;
        else if (h->obj[i].kind == OBJ_COLLECTION)
            instance = next_collection++;
        else
            continue;
        d->type = (d->type & ~0x00FFFF00u) | DIDFT_MAKEINSTANCE(instance);
    }
}

/* windows' table of subtypes, minimum buttons and required oem flags by type. */
static const struct {
    BYTE min, max, buttons;
    DWORD flags;
} SUBTYPES[] = {
    {0, 1, 0, 0}, {1, 7, 0, 0}, {0, 13, 0, 0}, {1, 3, 5, 3}, {1, 5, 6, 0}, {1, 6, 4, 0},
    {1, 5, 4, 3}, {1, 5, 4, 0}, {2, 5, 0, 0}, {2, 6, 0, 0}, {2, 3, 0, 0}, {2, 14, 0, 0},
};

static DWORD validate(DWORD type, DWORD buttons, DWORD flags)
{
    BYTE t = (BYTE)type, sub = (BYTE)(type >> 8);
    if (t < 0x11 || t > 0x1C)
        return 0;
    const typeof(SUBTYPES[0]) *s = &SUBTYPES[t - 0x11];
    if (sub < s->min || sub >= s->max)
        return 0;
    if (!s->buttons)
        return type;
    if (buttons >= s->buttons && (flags & s->flags) == s->flags)
        return type;
    return t | 1u << 8;
}

/* dinput8's final type from the axes, buttons and oem data (the car controller rules left out). */
static DWORD final_type8(DIDEV *dev, HIDDEV *h)
{
    BYTE t = h->entry.type8;
    if (t == DI8DEVTYPE_MOUSE)
        return DIDEVTYPE_HID | DI8DEVTYPEMOUSE_UNKNOWN << 8 | DI8DEVTYPE_MOUSE;
    DWORD axes = 0, buttons = 0;
    for (DWORD i = 0; i < dev->count; i++) {
        const DIOBJECT *d = &dev->objects[i];
        if (h->obj[i].kind == OBJ_BUTTON && h->obj[i].report_type == HidP_Input && !(d->type & DIDFT_ALIAS))
            buttons++;
        if (h->obj[i].kind == OBJ_VALUE && h->obj[i].report_type == HidP_Input && !(d->type & DIDFT_ALIAS)) {
            const USAGE_INFO *info = usage_info(d->page, d->usage);
            if (info)
                axes |= info->flag;
        }
    }
    DWORD type;
    if (h->oem_flags & 0x02000000)
        type = 0x41C;
    else if (h->oem_flags & 0x10)
        type = 0x317;
    else if (h->oem_flags & 0x20)
        type = 0x215;
    else if (!(axes & 0x1000) && t != DI8DEVTYPE_DRIVING && (axes & 0x3F) == 0x3F)
        type = 0x318;
    else {
        BYTE base = (axes & 0x1000) || t == DI8DEVTYPE_DRIVING ? DI8DEVTYPE_DRIVING : t;
        switch (base) {
        case DI8DEVTYPE_DEVICE:
            type = (axes & 0x700) == 0x700 ? 0xC1C
                   : (axes & 0x300) == 0x300 ? 0xB1C
                   : (axes & 0x40)          ? 0x81C
                   : (axes & 0x800)         ? 0x71C
                   : (axes & 0x2000)        ? 0xD1C
                                            : DI8DEVTYPE_DEVICE;
            break;
        case DI8DEVTYPE_JOYSTICK:
        case DI8DEVTYPE_GAMEPAD:
            type = (axes & 3) == 3 ? (DWORD)(base | DI8DEVTYPEJOYSTICK_STANDARD << 8) : 0x21C;
            break;
        case DI8DEVTYPE_DRIVING:
            type = (axes & 0x700) == 0x700 ? 0x416 : (axes & 0x300) == 0x300 ? 0x316 : (axes & 2) ? 0x216 : 0x116;
            break;
        default:
            type = base;
            break;
        }
    }
    type = validate(type, buttons, h->oem_flags);
    if (!type)
        type = t;
    return type | DIDEVTYPE_HID;
}

static DWORD final_type7(HIDDEV *h)
{
    switch (h->entry.type7) {
    case DIDEVTYPE_MOUSE:
        return DIDEVTYPE_HID | DIDEVTYPEMOUSE_UNKNOWN << 8 | DIDEVTYPE_MOUSE;
    case DIDEVTYPE_KEYBOARD:
        return DIDEVTYPE_HID | DIDEVTYPE_KEYBOARD;
    case DIDEVTYPE_JOYSTICK: {
        DWORD sub = h->entry.type8 == DI8DEVTYPE_GAMEPAD ? DIDEVTYPEJOYSTICK_GAMEPAD : DIDEVTYPEJOYSTICK_UNKNOWN;
        if (h->oem_flags & 0x10)
            sub = DIDEVTYPEJOYSTICK_FLIGHTSTICK;
        else if (h->oem_flags & 0x20)
            sub = DIDEVTYPEJOYSTICK_GAMEPAD;
        else if (h->oem_flags & 0x40)
            sub = DIDEVTYPEJOYSTICK_WHEEL;
        else if (h->oem_flags & 0x02000000)
            sub = DIDEVTYPEJOYSTICK_HEADTRACKER;
        return DIDEVTYPE_HID | sub << 8 | DIDEVTYPE_JOYSTICK;
    }
    default:
        return DIDEVTYPE_HID | DIDEVTYPE_DEVICE;
    }
}

/* the oem name, else the product string, else a name made of what the device has; kept as the oem name. */
static void make_name(DIDEV *dev, HIDDEV *h, HANDLE file)
{
    WCHAR name[DI_MAX_PATH] = L"";
    HKEY key = oem_key(h->entry.vid, h->entry.pid, FALSE);
    if (key) {
        DWORD size = sizeof(name) - sizeof(WCHAR);
        if (RegQueryValueExW(key, L"OEMName", nullptr, nullptr, (BYTE *)name, &size))
            name[0] = 0;
        RegCloseKey(key);
    }
    if (name[0] && lstrcmpW(name, L"USB Human Interface Device")) {
        DiCopyW(dev->name, name, DI_MAX_PATH);
        return;
    }
    WCHAR product[260] = L"";
    if (g_api.GetProductString(file, product, sizeof(product)) && product[0]) {
        DiCopyW(name, product, DI_MAX_PATH);
    } else if (LOBYTE(dev->devtype) >= DI8DEVTYPE_JOYSTICK || LOBYTE(dev->devtype) == DIDEVTYPE_JOYSTICK) {
        BYTE t = h->entry.type8;
        DWORD kind = t >= DI8DEVTYPE_JOYSTICK && t <= DI8DEVTYPE_FLIGHT ? 0x1FED + t : 0x2006u;
        static const WCHAR *const TYPE_NAMES[] = {L"joystick", L"gamepad", L"driving controller", L"flight controller"};
        const WCHAR *type_name = kind == 0x2006 ? L"device" : TYPE_NAMES[kind - 0x2001];
        wsprintfW(name, L"%d axis %d button %s%s", dev->axes, dev->buttons, type_name,
                  dev->povs ? L" with hat switch" : L"");
    } else {
        DiCopyW(name, h->entry.type8 == DI8DEVTYPE_MOUSE      ? DiString(16)
                      : h->entry.type8 == DI8DEVTYPE_KEYBOARD ? DiString(17)
                                                              : L"device",
                DI_MAX_PATH);
    }
    DiCopyW(dev->name, name, DI_MAX_PATH);
    if (h->entry.type8 >= DI8DEVTYPE_JOYSTICK && (key = oem_key(h->entry.vid, h->entry.pid, TRUE))) {
        RegSetValueExW(key, L"OEMName", 0, REG_SZ, (const BYTE *)name, (DWORD)(wcslen(name) + 1) * sizeof(WCHAR));
        RegCloseKey(key);
    }
}

static void free_hid(HIDDEV *h)
{
    if (!h)
        return;
    if (h->ppd)
        g_api.FreePreparsedData(h->ppd);
    HeapFree(GetProcessHeap(), 0, h->data);
    HeapFree(GetProcessHeap(), 0, h);
}

/* the c_dfDIJoystick offsets old versions report for a joystick's objects, as if it were their format. */
static void legacy_offsets(DIDEV *dev)
{
    BOOL used[DI_MAX_OBJECTS] = {};
    for (DWORD i = 0; i < DI_MAX_OBJECTS; i++)
        dev->legacy_ofs[i] = ~0u;
    for (DWORD j = 0; j < DiJoystickFormat.dwNumObjs; j++) {
        const DIOBJECTDATAFORMAT *f = &DiJoystickFormat.rgodf[j];
        for (DWORD i = 0; i < dev->count; i++) {
            const DIOBJECT *o = &dev->objects[i];
            if (used[i] || !(f->dwType & o->type & 0xFF) || (o->type & (DIDFT_ALIAS | DIDFT_OUTPUT)))
                continue;
            if (f->pguid && !IsEqualGUID(f->pguid, o->guid))
                continue;
            used[i] = TRUE;
            dev->legacy_ofs[i] = f->dwOfs;
            break;
        }
    }
}

HRESULT DiHidInit(DIDEV *dev, DWORD index)
{
    HIDDEV *h = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*h));
    if (!h)
        return E_OUTOFMEMORY;
    RtlEnterCriticalSection(&DiLock);
    h->entry = g_list[index];
    RtlLeaveCriticalSection(&DiLock);
    for (DWORD i = 0; i < MAX_INDICES; i++)
        h->by_index[i] = -1;
    HANDLE file = open_path(h->entry.path, FILE_FLAG_OVERLAPPED);
    if (file == INVALID_HANDLE_VALUE) {
        free_hid(h);
        return E_FAIL;
    }
    HRESULT hr = E_FAIL;
    CAPS_SET input = {}, output = {}, feature = {};
    if (!g_api.GetPreparsedData(file, &h->ppd)) {
        h->ppd = nullptr;
        goto done;
    }
    if (g_api.GetCaps(h->ppd, &h->caps) != HIDP_STATUS_SUCCESS)
        goto done;
    HIDP_CAPS *c = &h->caps;
    if (!read_caps(h, HidP_Input, c->NumberInputValueCaps, c->NumberInputButtonCaps, &input) ||
        !read_caps(h, HidP_Output, c->NumberOutputValueCaps, c->NumberOutputButtonCaps, &output) ||
        !read_caps(h, HidP_Feature, c->NumberFeatureValueCaps, c->NumberFeatureButtonCaps, &feature)) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (bad_ranges(&input) || bad_ranges(&output) || bad_ranges(&feature))
        goto done;
    DWORD in = c->NumberInputDataIndices, feat = c->NumberFeatureDataIndices, out = c->NumberOutputDataIndices;
    DWORD total = c->NumberLinkCollectionNodes + in + feat + out;
    if (total >= DI_MAX_OBJECTS || in > MAX_INDICES)
        goto done;
    dev->count = total;
    memset(dev->objects, 0, sizeof(dev->objects));
    make_values(dev, h, &input, HidP_Input, 0);
    make_values(dev, h, &feature, HidP_Feature, in);
    make_values(dev, h, &output, HidP_Output, in + feat);
    make_buttons(dev, h, &input, HidP_Input, 0);
    make_buttons(dev, h, &feature, HidP_Feature, in);
    make_buttons(dev, h, &output, HidP_Output, in + feat);
    HIDP_LINK_COLLECTION_NODE nodes[64];
    ULONG node_count = RTL_NUMBER_OF(nodes);
    if (c->NumberLinkCollectionNodes <= RTL_NUMBER_OF(nodes) &&
        g_api.GetLinkCollectionNodes(nodes, &node_count, h->ppd) == HIDP_STATUS_SUCCESS) {
        for (ULONG k = 0; k < node_count; k++) {
            DWORD number = in + feat + out + k;
            h->obj[number].kind = OBJ_COLLECTION;
            DIOBJECT *d = &dev->objects[number];
            d->guid = &GUID_Unknown;
            d->type = DIDFT_COLLECTION | DIDFT_NODATA | DIDFT_MAKEINSTANCE(number);
            if (nodes[k].LinkUsagePage >= 0xFF00)
                d->type |= DIDFT_VENDORDEFINED;
            d->page = nodes[k].LinkUsagePage;
            d->usage = nodes[k].LinkUsage;
            d->collection = (WORD)k;
        }
    }
    /* values take four bytes each, input then feature then output, then each button one byte. */
    DWORD at = 0;
    for (DWORD i = 0; i < total; i++)
        if (h->obj[i].kind == OBJ_VALUE) {
            dev->objects[i].ofs = at;
            at += 4;
        }
    for (DWORD i = 0; i < total; i++)
        if (h->obj[i].kind == OBJ_BUTTON)
            dev->objects[i].ofs = at++;
    dev->native_size = (at + 3) & ~3u;
    if (dev->native_size > DI_MAX_STATE)
        goto done;
    for (DWORD i = 0; i < total; i++)
        if (!dev->objects[i].guid)
            dev->objects[i].guid = &GUID_Unknown;
    renumber(dev, h);
    dev->axes = dev->buttons = dev->povs = 0;
    for (DWORD i = 0; i < total; i++) {
        const DIOBJECT *d = &dev->objects[i];
        if ((d->type & DIDFT_ALIAS) || h->obj[i].report_type != HidP_Input)
            continue;
        if (h->obj[i].kind == OBJ_VALUE && h->obj[i].pov)
            dev->povs++;
        else if (h->obj[i].kind == OBJ_VALUE && (d->type & DIDFT_AXIS))
            dev->axes++;
        else if (h->obj[i].kind == OBJ_BUTTON)
            dev->buttons++;
    }
    HKEY key = oem_key(h->entry.vid, h->entry.pid, FALSE);
    if (key) {
        DWORD data[2], size = sizeof(data);
        if (!RegQueryValueExW(key, L"OEMData", nullptr, nullptr, (BYTE *)data, &size) && size >= 4)
            h->oem_flags = data[0];
        RegCloseKey(key);
    }
    dev->devtype = dev->di8 ? final_type8(dev, h) : final_type7(h);
    h->max_data = g_api.MaxDataListLength(HidP_Input, h->ppd);
    h->data = HeapAlloc(GetProcessHeap(), 0, (h->max_data ? h->max_data : 1) * sizeof(HIDP_DATA));
    if (!h->data) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    make_name(dev, h, file);
    dev->instance = h->entry.instance;
    dev->product = h->entry.product;
    dev->usage_page = h->entry.page;
    dev->usage = h->entry.usage;
    dev->caps = DIDC_ATTACHED;
    dev->relative = FALSE;
    dev->hid = h;
    dev->cls = &DiHidClass;
    initial_state(dev, h);
    dev->legacy = !dev->di8 && dev->version < 0x0700 && dev->version != 0x05B2 &&
                  LOBYTE(dev->devtype) == DIDEVTYPE_JOYSTICK;
    if (dev->legacy)
        legacy_offsets(dev);
    hr = DI_OK;
done:
    CloseHandle(file);
    free_caps(&input);
    free_caps(&output);
    free_caps(&feature);
    if (FAILED(hr)) {
        dev->count = 0;
        free_hid(h);
    }
    return hr;
}

/* ------------------------------------------------------------------ reading */

/* a read of one report on its own handle; the completion runs on the worker thread. */
struct READER {
    OVERLAPPED overlapped;
    DIDEV *dev;     /* null once the device let it go */
    HANDLE file;
    ULONG length;
    BYTE *buffer;
};

static void CALLBACK read_done(DWORD error, DWORD bytes, OVERLAPPED *overlapped);

static void free_reader(READER *r)
{
    if (r->file)
        CloseHandle(r->file);
    HeapFree(GetProcessHeap(), 0, r->buffer);
    HeapFree(GetProcessHeap(), 0, r);
}

/* the report's values into the raw state, then each object's new value, an event for each that changed. */
static void parse(DIDEV *dev, HIDDEV *h, BYTE *report, ULONG length)
{
    ULONG n = h->max_data;
    if (g_api.GetData(HidP_Input, h->data, &n, h->ppd, (PCHAR)report, length) != HIDP_STATUS_SUCCESS)
        return;
    for (DWORD i = 0; i < dev->count; i++)
        if (h->obj[i].kind == OBJ_BUTTON && h->obj[i].report_type == HidP_Input && dev->objects[i].report == report[0])
            h->raw[h->obj[i].index] = 0;
    for (ULONG k = 0; k < n; k++) {
        USHORT index = h->data[k].DataIndex;
        if (index >= MAX_INDICES || h->by_index[index] < 0)
            continue;
        h->raw[index] = h->obj[h->by_index[index]].kind == OBJ_BUTTON ? 1 : h->data[k].RawValue;
    }
    DWORD time = GetTickCount(), sequence = 0;
    BOOL changed = FALSE;
    for (DWORD i = 0; i < dev->count; i++) {
        const HIDOBJ *o = &h->obj[i];
        const DIOBJECT *d = &dev->objects[i];
        if (o->report_type != HidP_Input || o->index >= MAX_INDICES)
            continue;
        if (o->kind == OBJ_BUTTON) {
            BYTE now = h->raw[o->index] ? 0x80 : 0;
            if (now == h->native[d->ofs])
                continue;
            h->native[d->ofs] = now;
            if (!sequence)
                sequence = DiNextSequence();
            DiBufferEvent(dev, d->ofs, now, time, sequence);
            changed = TRUE;
        } else if (o->kind == OBJ_VALUE) {
            LONG old, now;
            memcpy(&old, h->native + d->ofs, sizeof(old));
            if (o->absolute || o->pov)
                now = value_of(o, h->raw[o->index]);
            else
                now = old + extend(o, h->raw[o->index]);
            if (now == old)
                continue;
            memcpy(h->native + d->ofs, &now, sizeof(now));
            if (!sequence)
                sequence = DiNextSequence();
            DiBufferEvent(dev, d->ofs, !o->absolute && !o->pov && dev->relative ? (DWORD)(now - old) : (DWORD)now,
                          time, sequence);
            changed = TRUE;
        }
    }
    if (changed)
        DiSignal(dev);
}

static BOOL issue_read(READER *r)
{
    memset(&r->overlapped, 0, sizeof(r->overlapped));
    return ReadFileEx(r->file, r->buffer, r->length, &r->overlapped, read_done);
}

/* the device is gone: the reader is let go of here, as no read of it is left to complete. */
static void reader_lost(READER *r)
{
    DIDEV *dev = r->dev;
    ((HIDDEV *)dev->hid)->reader = nullptr;
    ((HIDDEV *)dev->hid)->unplugged = TRUE;
    r->dev = nullptr;
    InterlockedIncrement(&dev->ref);
    RtlLeaveCriticalSection(&DiLock);
    free_reader(r);
    DiDeviceLost(dev);
    ((IUnknown *)&dev->vtbl_w)->lpVtbl->Release((IUnknown *)&dev->vtbl_w);
}

static void CALLBACK begin_read(ULONG_PTR context)
{
    READER *r = (READER *)context;
    RtlEnterCriticalSection(&DiLock);
    if (!r->dev) {
        RtlLeaveCriticalSection(&DiLock);
        free_reader(r);
    } else if (!issue_read(r)) {
        reader_lost(r);
    } else {
        RtlLeaveCriticalSection(&DiLock);
    }
}

/* a report came, or the read failed: the device is gone then, its instances lose their acquisition. */
static void CALLBACK read_done(DWORD error, DWORD bytes, OVERLAPPED *overlapped)
{
    READER *r = CONTAINING_RECORD(overlapped, READER, overlapped);
    RtlEnterCriticalSection(&DiLock);
    if (!r->dev) {
        RtlLeaveCriticalSection(&DiLock);
        free_reader(r);
        return;
    }
    if (error || bytes < r->length) {
        reader_lost(r);
        return;
    }
    parse(r->dev, r->dev->hid, r->buffer, r->length);
    if (!issue_read(r)) {
        reader_lost(r);
        return;
    }
    RtlLeaveCriticalSection(&DiLock);
}

/* acquisition opens the device again, empties its queue and hands the reading to the worker. */
static HRESULT hid_start(DIDEV *dev)
{
    HIDDEV *h = dev->hid;
    if (!h->caps.InputReportByteLength)
        return DI_OK;
    READER *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*r));
    if (!r)
        return E_OUTOFMEMORY;
    r->length = h->caps.InputReportByteLength;
    r->buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, r->length);
    r->file = open_path(h->entry.path, FILE_FLAG_OVERLAPPED);
    if (r->file == INVALID_HANDLE_VALUE) {
        r->file = nullptr;
        free_reader(r);
        h->unplugged = TRUE;
        dev->caps &= ~DIDC_ATTACHED;
        return DIERR_UNPLUGGED;
    }
    if (!r->buffer || !DiStartWorker()) {
        free_reader(r);
        return E_FAIL;
    }
    h->unplugged = FALSE;
    dev->caps |= DIDC_ATTACHED;
    g_api.FlushQueue(r->file);
    RtlEnterCriticalSection(&DiLock);
    r->dev = dev;
    h->reader = r;
    RtlLeaveCriticalSection(&DiLock);
    if (!QueueUserAPC(begin_read, DiWorker, (ULONG_PTR)r)) {
        RtlEnterCriticalSection(&DiLock);
        h->reader = nullptr;
        RtlLeaveCriticalSection(&DiLock);
        free_reader(r);
        return E_FAIL;
    }
    return DI_OK;
}

/* closing the handle ends the read, whose completion frees the reader. */
static void hid_stop(DIDEV *dev)
{
    HIDDEV *h = dev->hid;
    RtlEnterCriticalSection(&DiLock);
    READER *r = h->reader;
    h->reader = nullptr;
    if (r) {
        r->dev = nullptr;
        CancelIoEx(r->file, &r->overlapped);
    }
    RtlLeaveCriticalSection(&DiLock);
    if (h->unplugged)
        dev->caps &= ~DIDC_ATTACHED;
}

/* ------------------------------------------------------------------ the class */

static HRESULT hid_init(DIDEV *dev)
{
    return DI_OK;
}

/* no exclusivity is kept for hid input; it matters to force feedback alone. */
static HRESULT hid_cooperate(DIDEV *dev, HWND window, DWORD flags)
{
    dev->em = 0;
    return DI_OK;
}

static void hid_acquire(DIDEV *dev)
{
}

static void hid_unacquire(DIDEV *dev)
{
}

static void hid_state(DIDEV *dev, BYTE *native)
{
    memcpy(native, ((HIDDEV *)dev->hid)->native, dev->native_size);
}

static BOOL hid_name(DIDEV *dev, const DIOBJECT *object, WCHAR *name)
{
    HIDDEV *h = dev->hid;
    const HIDOBJ *o = &h->obj[object - dev->objects];
    DWORD instance = DIDFT_GETINSTANCE(object->type);
    const WCHAR *usage = DiHidUsageName(object->page, object->usage);
    if (o->kind == OBJ_COLLECTION) {
        if (usage)
            wsprintfW(name, DiString(32), instance, usage);
        else
            wsprintfW(name, DiString(31), instance);
        return TRUE;
    }
    if (usage) {
        DiCopyW(name, usage, DI_MAX_PATH);
        return TRUE;
    }
    DWORD format = o->kind == OBJ_BUTTON ? 28 : o->pov ? 30 : (object->type & DIDFT_AXIS) ? 29 : 35;
    wsprintfW(name, DiString(format), instance);
    return TRUE;
}

static HIDOBJ *axis_of(DIDEV *dev, const DIOBJECT *object)
{
    if (!object)
        return nullptr;
    HIDOBJ *o = &((HIDDEV *)dev->hid)->obj[object - dev->objects];
    return o->kind == OBJ_VALUE && o->calibrated ? o : nullptr;
}

static HRESULT hid_get_property(DIDEV *dev, DWORD id, const DIOBJECT *object, DIPROPHEADER *h)
{
    HIDDEV *hid = dev->hid;
    DIPROPDWORD *d = (DIPROPDWORD *)h;
    DIPROPRANGE *r = (DIPROPRANGE *)h;
    HIDOBJ *axis = axis_of(dev, object);
    HIDOBJ *o = object ? &hid->obj[object - dev->objects] : nullptr;
    switch (id) {
    case 3: /* GRANULARITY */
        if (o && o->kind == OBJ_VALUE && o->pov) {
            d->dwData = o->granularity;
            return DI_OK;
        }
        return E_NOTIMPL;
    case 4: /* RANGE */
        if (!axis)
            return E_NOTIMPL;
        r->lMin = axis->cal.rmin;
        r->lMax = axis->cal.rmax;
        return DI_OK;
    case 5: /* DEADZONE */
    case 6: /* SATURATION */
    case 10: /* CALIBRATIONMODE */
        if (!axis)
            return object ? DIERR_UNSUPPORTED : E_NOTIMPL;
        d->dwData = id == 5 ? axis->cal.dz : id == 6 ? axis->cal.sat : axis->cal.mode;
        return DI_OK;
    case 11: /* CALIBRATION */
        if (!axis)
            return object ? DIERR_UNSUPPORTED : E_NOTIMPL;
        if (h->dwSize != sizeof(DIPROPCAL))
            return E_INVALIDARG;
        ((DIPROPCAL *)h)->lMin = axis->cal.cmin;
        ((DIPROPCAL *)h)->lCenter = axis->cal.cctr;
        ((DIPROPCAL *)h)->lMax = axis->cal.cmax;
        return DI_OK;
    case 21: /* CPOINTS */
        if (!axis)
            return object ? DIERR_UNSUPPORTED : E_NOTIMPL;
        ((DIPROPCPOINTS *)h)->dwCPointsNum = axis->cal.cpoints;
        memcpy(((DIPROPCPOINTS *)h)->cp, axis->cal.cp, sizeof(axis->cal.cp));
        return DI_OK;
    case 18: /* PHYSICALRANGE */
    case 19: /* LOGICALRANGE */
        if (!o || o->kind != OBJ_VALUE)
            return object ? DIERR_UNSUPPORTED : E_NOTIMPL;
        r->lMin = id == 18 ? o->pmin : o->lmin;
        r->lMax = id == 18 ? o->pmax : o->lmax;
        return DI_OK;
    case 20: { /* KEYNAME */
        const WCHAR *name = object ? DiHidUsageName(object->page, object->usage) : nullptr;
        if (!name)
            return DIERR_OBJECTNOTFOUND;
        DiCopyW(((DIPROPSTRING *)h)->wsz, name, DI_MAX_PATH);
        return DI_OK;
    }
    case 23: /* SCANCODE */
        return DIERR_UNSUPPORTED;
    case 12: /* GUIDANDPATH */
        ((DIPROPGUIDANDPATH *)h)->guidClass = GUID_HIDClass;
        DiCopyW(((DIPROPGUIDANDPATH *)h)->wszPath, hid->entry.path, DI_MAX_PATH);
        return DI_OK;
    case 24: /* VIDPID */
        d->dwData = MAKELONG(hid->entry.vid, hid->entry.pid);
        return DI_OK;
    case 15: /* JOYSTICKID */
        if (hid->entry.type8 < DI8DEVTYPE_JOYSTICK || hid->entry.joystick >= 16)
            return DIERR_UNSUPPORTED;
        d->dwData = hid->entry.joystick;
        return DI_OK;
    case 26: /* TYPENAME */
        if (hid->entry.type8 == DI8DEVTYPE_MOUSE || hid->entry.type8 == DI8DEVTYPE_KEYBOARD)
            return DIERR_UNSUPPORTED;
        vidpid_name(((DIPROPSTRING *)h)->wsz, hid->entry.vid, hid->entry.pid);
        return DI_OK;
    case 16: /* GETPORTDISPLAYNAME, the parent's description in place of its friendly name */
        DiCopyW(((DIPROPSTRING *)h)->wsz, dev->name, DI_MAX_PATH);
        return S_FALSE;
    case 13: /* INSTANCENAME */
    case 14: /* PRODUCTNAME */
        DiCopyW(((DIPROPSTRING *)h)->wsz, dev->name, DI_MAX_PATH);
        return DI_OK;
    default:
        return E_NOTIMPL;
    }
}

/* the calibration a CALIBRATION set stores under the device's key, deleted when all zero. */
static HRESULT store_calibration(HIDDEV *h, DWORD instance, const DIPROPCAL *cal)
{
    HKEY key = calibration_key(h->entry.vid, h->entry.pid, h->entry.slot);
    if (!key)
        return S_FALSE;
    WCHAR path[64];
    wsprintfW(path, L"Type\\Axes\\%lu", instance);
    HKEY axis;
    LONG status = RegCreateKeyExW(key, path, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &axis, nullptr);
    RegCloseKey(key);
    if (status)
        return S_FALSE;
    LONG values[3] = {cal->lMin, cal->lCenter, cal->lMax};
    if (!values[0] && !values[1] && !values[2])
        RegDeleteValueW(axis, L"Calibration");
    else
        RegSetValueExW(axis, L"Calibration", 0, REG_BINARY, (const BYTE *)values, sizeof(values));
    RegCloseKey(axis);
    return DI_OK;
}

static HRESULT set_axis(DIDEV *dev, DWORD id, DWORD index, const DIPROPHEADER *h)
{
    HIDDEV *hid = dev->hid;
    HIDOBJ *axis = &hid->obj[index];
    CAL *c = &axis->cal;
    const DIPROPDWORD *d = (const DIPROPDWORD *)h;
    switch (id) {
    case 4: {
        const DIPROPRANGE *r = (const DIPROPRANGE *)h;
        c->rmin = r->lMin;
        c->rmax = r->lMax;
        c->rctr = r->lMin + (LONG)((ULONG)(r->lMax - r->lMin + 1) >> 1);
        break;
    }
    case 5:
        c->dz = d->dwData;
        break;
    case 6:
        c->sat = d->dwData;
        break;
    case 10:
        c->mode = d->dwData;
        c->legacy_raw = c->mode == DIPROPCALIBRATIONMODE_RAW && !dev->di8 && dev->version < 0x0700 &&
                        dev->version != 0x05B2 && (c->cmin & 0x8000);
        break;
    case 11: {
        const DIPROPCAL *cal = (const DIPROPCAL *)h;
        HRESULT hr = store_calibration(hid, DIDFT_GETINSTANCE(dev->objects[index].type), cal);
        if (cal->lMin || cal->lCenter || cal->lMax) {
            c->cmin = cal->lMin;
            c->cctr = cal->lCenter;
            c->cmax = cal->lMax;
        } else {
            default_cal(axis);
        }
        recalc(c);
        return hr;
    }
    case 21: {
        const DIPROPCPOINTS *p = (const DIPROPCPOINTS *)h;
        if (p->dwCPointsNum > MAXCPOINTSNUM)
            return E_INVALIDARG;
        c->cpoints = p->dwCPointsNum;
        memcpy(c->cp, p->cp, sizeof(c->cp));
        break;
    }
    }
    recalc(c);
    return DI_OK;
}

/* range, dead zone, saturation and calibration, of one axis or of every absolute axis; they apply while acquired. */
static HRESULT hid_set_property(DIDEV *dev, DWORD id, const DIOBJECT *object, const DIPROPHEADER *h)
{
    const DIPROPDWORD *d = (const DIPROPDWORD *)h;
    switch (id) {
    case 4:
        if (((const DIPROPRANGE *)h)->lMin > ((const DIPROPRANGE *)h)->lMax)
            return E_INVALIDARG;
        break;
    case 5:
    case 6:
        if (d->dwData > 10000)
            return E_INVALIDARG;
        break;
    case 10:
        if (d->dwData > DIPROPCALIBRATIONMODE_RAW)
            return E_INVALIDARG;
        break;
    case 11:
        if (!object)
            return E_NOTIMPL;
        if (h->dwSize != sizeof(DIPROPCAL))
            return DIERR_UNSUPPORTED;
        break;
    case 21:
        if (!object)
            return E_NOTIMPL;
        break;
    case 13:
    case 12:
        return E_NOTIMPL;
    case 14:
        return DI_OK;
    default:
        return E_NOTIMPL;
    }
    HRESULT hr = DI_OK;
    RtlEnterCriticalSection(&DiLock);
    if (object) {
        if (!axis_of(dev, object))
            hr = DIERR_UNSUPPORTED;
        else
            hr = set_axis(dev, id, (DWORD)(object - dev->objects), h);
    } else {
        for (DWORD i = 0; i < dev->count; i++)
            if ((dev->objects[i].type & 0x1C000002) == DIDFT_ABSAXIS && axis_of(dev, &dev->objects[i]))
                set_axis(dev, id, i, h);
    }
    RtlLeaveCriticalSection(&DiLock);
    return hr;
}

static int hid_find_usage(DIDEV *dev, DWORD usage)
{
    for (DWORD i = 0; i < dev->count; i++)
        if (dev->objects[i].usage == LOWORD(usage) && dev->objects[i].page == HIWORD(usage) &&
            ((HIDDEV *)dev->hid)->obj[i].kind != OBJ_COLLECTION)
            return (int)i;
    return -1;
}

static void hid_release(DIDEV *dev)
{
    free_hid(dev->hid);
    dev->hid = nullptr;
}

const DICLASS DiHidClass = {
    .keyboard = FALSE,
    .init = hid_init,
    .cooperate = hid_cooperate,
    .acquire = hid_acquire,
    .unacquire = hid_unacquire,
    .state = hid_state,
    .name = hid_name,
    .get_property = hid_get_property,
    .set_property = hid_set_property,
    .start = hid_start,
    .stop = hid_stop,
    .find_usage = hid_find_usage,
    .release = hid_release,
    .control_panel = L"control.exe joy.cpl",
};
