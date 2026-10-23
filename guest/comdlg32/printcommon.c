/*
 * printers and settings shared by the print dialogs.
 */
#include "printcommon.h"
#include <stddef.h>

static SPOOLFN g_spool;
static SRWLOCK g_lock = SRWLOCK_INIT;

const SPOOLFN *PrnSpool(void)
{
    AcquireSRWLockExclusive(&g_lock);
    if (!g_spool.Loaded) {
        HMODULE module = LoadLibraryW(L"winspool.drv");
        g_spool.Loaded = TRUE;
        if (module) {
            g_spool.EnumPrinters = (void *)GetProcAddress(module, "EnumPrintersW");
            g_spool.OpenPrinter = (void *)GetProcAddress(module, "OpenPrinterW");
            g_spool.ClosePrinter = (void *)GetProcAddress(module, "ClosePrinter");
            g_spool.GetPrinter = (void *)GetProcAddress(module, "GetPrinterW");
            g_spool.DocumentProperties = (void *)GetProcAddress(module, "DocumentPropertiesW");
            g_spool.DeviceCapabilities = (void *)GetProcAddress(module, "DeviceCapabilitiesW");
            g_spool.GetDefaultPrinter = (void *)GetProcAddress(module, "GetDefaultPrinterW");
            g_spool.Ok = g_spool.EnumPrinters && g_spool.OpenPrinter && g_spool.ClosePrinter && g_spool.GetPrinter &&
                         g_spool.DocumentProperties && g_spool.DeviceCapabilities && g_spool.GetDefaultPrinter;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return g_spool.Ok ? &g_spool : nullptr;
}

/* ------------------------------------------------------------------ the printers */

BOOL PrnEnumerate(PRN_LIST *list)
{
    memset(list, 0, sizeof(*list));
    list->Default = -1;
    const SPOOLFN *spool = PrnSpool();
    if (!spool)
        return FALSE;
    DWORD needed = 0, returned = 0;
    spool->EnumPrinters(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 2, nullptr, 0, &needed, &returned);
    if (!needed)
        return TRUE;
    BYTE *buffer = HeapAlloc(GetProcessHeap(), 0, needed);
    if (!buffer)
        return FALSE;
    if (!spool->EnumPrinters(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 2, buffer, needed, &needed,
                             &returned)) {
        HeapFree(GetProcessHeap(), 0, buffer);
        return FALSE;
    }
    list->Items = returned ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, returned * sizeof(PRN_ITEM)) : nullptr;
    PRINTER_INFO_2W *info = (PRINTER_INFO_2W *)buffer;
    for (DWORD i = 0; list->Items && i < returned; i++) {
        PRN_ITEM *item = &list->Items[list->Count++];
        lstrcpynW(item->Name, info[i].pPrinterName ? info[i].pPrinterName : L"", ARRAYSIZE(item->Name));
        lstrcpynW(item->Driver, info[i].pDriverName ? info[i].pDriverName : L"", ARRAYSIZE(item->Driver));
        lstrcpynW(item->Port, info[i].pPortName ? info[i].pPortName : L"", ARRAYSIZE(item->Port));
        lstrcpynW(item->Comment, info[i].pComment ? info[i].pComment : L"", ARRAYSIZE(item->Comment));
        lstrcpynW(item->Location, info[i].pLocation ? info[i].pLocation : L"", ARRAYSIZE(item->Location));
        item->Attributes = info[i].Attributes;
        item->Jobs = info[i].cJobs;
    }
    HeapFree(GetProcessHeap(), 0, buffer);
    WCHAR name[256];
    DWORD size = ARRAYSIZE(name);
    if (spool->GetDefaultPrinter(name, &size))
        list->Default = PrnFind(list, name);
    if (list->Default < 0 && list->Count)
        list->Default = 0;
    return TRUE;
}

void PrnFreeList(PRN_LIST *list)
{
    if (list->Items)
        HeapFree(GetProcessHeap(), 0, list->Items);
    memset(list, 0, sizeof(*list));
    list->Default = -1;
}

int PrnFind(const PRN_LIST *list, LPCWSTR name)
{
    if (!name)
        return -1;
    for (int i = 0; i < list->Count; i++)
        if (!lstrcmpiW(list->Items[i].Name, name))
            return i;
    return -1;
}

DEVMODEW *PrnDevmode(HWND owner, const PRN_ITEM *printer, const DEVMODEW *input)
{
    const SPOOLFN *spool = PrnSpool();
    HANDLE handle;
    if (!spool || !spool->OpenPrinter((LPWSTR)printer->Name, &handle, nullptr))
        return nullptr;
    LONG size = spool->DocumentProperties(owner, handle, (LPWSTR)printer->Name, nullptr, nullptr, 0);
    DEVMODEW *out = size > 0 ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)size) : nullptr;
    if (out) {
        DWORD mode = DM_OUT_BUFFER | (input ? DM_IN_BUFFER : 0);
        if (spool->DocumentProperties(owner, handle, (LPWSTR)printer->Name, out, (PDEVMODEW)input, mode) < 0) {
            HeapFree(GetProcessHeap(), 0, out);
            out = nullptr;
        }
    }
    spool->ClosePrinter(handle);
    return out;
}

int PrnCapability(const PRN_ITEM *printer, WORD capability)
{
    const SPOOLFN *spool = PrnSpool();
    return spool ? spool->DeviceCapabilities(printer->Name, printer->Port, capability, nullptr, nullptr) : -1;
}

/* ------------------------------------------------------------------ handles */

HGLOBAL PrnNewDevmodeHandle(const DEVMODEW *devmode)
{
    SIZE_T bytes = (SIZE_T)devmode->dmSize + devmode->dmDriverExtra;
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!handle)
        return nullptr;
    void *p = GlobalLock(handle);
    if (!p) {
        GlobalFree(handle);
        return nullptr;
    }
    memcpy(p, devmode, bytes);
    GlobalUnlock(handle);
    return handle;
}

/* the strings follow the four offset words: driver, printer and port, wide. */
HGLOBAL PrnNewDevnamesHandle(const PRN_ITEM *printer, BOOL is_default, BOOL to_file)
{
    static const WCHAR driver[] = L"winspool";
    LPCWSTR output = to_file ? L"FILE:" : printer->Port;
    SIZE_T chars = sizeof(DEVNAMES) / sizeof(WCHAR) + lstrlenW(driver) + 1 + lstrlenW(printer->Name) + 1 +
                   lstrlenW(output) + 1;
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, chars * sizeof(WCHAR));
    if (!handle)
        return nullptr;
    DEVNAMES *names = GlobalLock(handle);
    if (!names) {
        GlobalFree(handle);
        return nullptr;
    }
    WCHAR *text = (WCHAR *)names;
    WORD at = sizeof(DEVNAMES) / sizeof(WCHAR);
    names->wDriverOffset = at;
    lstrcpyW(text + at, driver);
    at += (WORD)(lstrlenW(driver) + 1);
    names->wDeviceOffset = at;
    lstrcpyW(text + at, printer->Name);
    at += (WORD)(lstrlenW(printer->Name) + 1);
    names->wOutputOffset = at;
    lstrcpyW(text + at, output);
    names->wDefault = is_default ? DN_DEFAULTPRN : 0;
    GlobalUnlock(handle);
    return handle;
}

DEVMODEW *PrnCopyDevmode(HGLOBAL handle)
{
    if (!handle)
        return nullptr;
    DEVMODEW *source = GlobalLock(handle);
    if (!source)
        return nullptr;
    SIZE_T size = GlobalSize(handle);
    DEVMODEW *copy = nullptr;
    if (size >= offsetof(DEVMODEW, dmFields) + sizeof(DWORD)) {
        SIZE_T bytes = (SIZE_T)source->dmSize + source->dmDriverExtra;
        if (bytes > size)
            bytes = size;
        copy = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes < sizeof(DEVMODEW) ? sizeof(DEVMODEW) : bytes);
        if (copy)
            memcpy(copy, source, bytes);
    }
    GlobalUnlock(handle);
    return copy;
}

BOOL PrnDevnamesPrinter(HGLOBAL handle, const PRN_LIST *list, int *index, BOOL *to_file)
{
    if (!handle)
        return FALSE;
    DEVNAMES *names = GlobalLock(handle);
    if (!names)
        return FALSE;
    const WCHAR *text = (const WCHAR *)names;
    int found = PrnFind(list, text + names->wDeviceOffset);
    if (to_file)
        *to_file = !lstrcmpiW(text + names->wOutputOffset, L"FILE:");
    GlobalUnlock(handle);
    if (found < 0)
        return FALSE;
    *index = found;
    return TRUE;
}

/* ------------------------------------------------------------------ ANSI */

HGLOBAL PrnDevmodeToAnsi(HGLOBAL wide)
{
    DEVMODEW *w = PrnCopyDevmode(wide);
    if (!w)
        return nullptr;
    SIZE_T extra = w->dmDriverExtra;
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DEVMODEA) + extra);
    DEVMODEA *a = handle ? GlobalLock(handle) : nullptr;
    if (a) {
        DlgToAnsi(w->dmDeviceName, (LPSTR)a->dmDeviceName, CCHDEVICENAME);
        memcpy(&a->dmSpecVersion, &w->dmSpecVersion, offsetof(DEVMODEA, dmFormName) - offsetof(DEVMODEA, dmSpecVersion));
        DlgToAnsi(w->dmFormName, (LPSTR)a->dmFormName, CCHFORMNAME);
        memcpy(&a->dmLogPixels, &w->dmLogPixels,
               offsetof(DEVMODEA, dmPanningHeight) + sizeof(a->dmPanningHeight) - offsetof(DEVMODEA, dmLogPixels));
        a->dmSize = sizeof(DEVMODEA);
        a->dmDriverExtra = (WORD)extra;
        if (extra)
            memcpy((BYTE *)a + sizeof(DEVMODEA), (const BYTE *)w + w->dmSize, extra);
        GlobalUnlock(handle);
    } else if (handle) {
        GlobalFree(handle);
        handle = nullptr;
    }
    HeapFree(GetProcessHeap(), 0, w);
    return handle;
}

HGLOBAL PrnDevmodeFromAnsi(HGLOBAL ansi)
{
    if (!ansi)
        return nullptr;
    DEVMODEA *a = GlobalLock(ansi);
    if (!a)
        return nullptr;
    SIZE_T size = GlobalSize(ansi);
    HGLOBAL handle = nullptr;
    if (size >= offsetof(DEVMODEA, dmFields) + sizeof(DWORD)) {
        SIZE_T extra = a->dmDriverExtra;
        DEVMODEW *w = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DEVMODEW) + extra);
        if (w) {
            DlgToWide((LPCSTR)a->dmDeviceName, w->dmDeviceName, CCHDEVICENAME);
            memcpy(&w->dmSpecVersion, &a->dmSpecVersion,
                   offsetof(DEVMODEA, dmFormName) - offsetof(DEVMODEA, dmSpecVersion));
            DlgToWide((LPCSTR)a->dmFormName, w->dmFormName, CCHFORMNAME);
            memcpy(&w->dmLogPixels, &a->dmLogPixels,
                   offsetof(DEVMODEA, dmPanningHeight) + sizeof(a->dmPanningHeight) - offsetof(DEVMODEA, dmLogPixels));
            w->dmSize = sizeof(DEVMODEW);
            w->dmDriverExtra = (WORD)extra;
            if (extra && size >= (SIZE_T)a->dmSize + extra)
                memcpy((BYTE *)w + sizeof(DEVMODEW), (const BYTE *)a + a->dmSize, extra);
            handle = PrnNewDevmodeHandle(w);
            HeapFree(GetProcessHeap(), 0, w);
        }
    }
    GlobalUnlock(ansi);
    return handle;
}

/* converts a DEVNAMES handle to the other character set, its offsets counting characters of that set. */
static HGLOBAL convert_devnames(HGLOBAL source, BOOL to_ansi)
{
    if (!source)
        return nullptr;
    DEVNAMES *names = GlobalLock(source);
    if (!names)
        return nullptr;
    WCHAR wide[3][260];
    char narrow[3][260];
    WORD offsets[3] = {names->wDriverOffset, names->wDeviceOffset, names->wOutputOffset};
    for (int i = 0; i < 3; i++) {
        if (to_ansi) {
            lstrcpynW(wide[i], (const WCHAR *)names + offsets[i], 260);
        } else {
            lstrcpynA(narrow[i], (const char *)names + offsets[i], 260);
            DlgToWide(narrow[i], wide[i], 260);
        }
    }
    WORD flag = names->wDefault;
    GlobalUnlock(source);
    SIZE_T chars = sizeof(DEVNAMES) / (to_ansi ? 1 : sizeof(WCHAR));
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 4 * 260 * sizeof(WCHAR));
    DEVNAMES *out = handle ? GlobalLock(handle) : nullptr;
    if (!out) {
        if (handle)
            GlobalFree(handle);
        return nullptr;
    }
    WORD at = (WORD)chars;
    WORD *fields[3] = {&out->wDriverOffset, &out->wDeviceOffset, &out->wOutputOffset};
    for (int i = 0; i < 3; i++) {
        *fields[i] = at;
        if (to_ansi) {
            DlgToAnsi(wide[i], (char *)out + at, 260);
            at += (WORD)(lstrlenA((char *)out + at) + 1);
        } else {
            lstrcpyW((WCHAR *)out + at, wide[i]);
            at += (WORD)(lstrlenW(wide[i]) + 1);
        }
    }
    out->wDefault = flag;
    GlobalUnlock(handle);
    return handle;
}

HGLOBAL PrnDevnamesToAnsi(HGLOBAL wide)
{
    return convert_devnames(wide, TRUE);
}

HGLOBAL PrnDevnamesFromAnsi(HGLOBAL ansi)
{
    return convert_devnames(ansi, FALSE);
}

/* ------------------------------------------------------------------ device contexts */

HDC PrnCreateDC(const PRN_ITEM *printer, const DEVMODEW *devmode, BOOL info)
{
    return info ? CreateICW(L"winspool", printer->Name, nullptr, devmode)
                : CreateDCW(L"winspool", printer->Name, nullptr, devmode);
}
