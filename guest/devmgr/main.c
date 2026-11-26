#include "devmgr_private.h"

#define IDC_DEVICES_LABEL 1411
#define IDC_DEVICES 1412
#define IDC_GROUP 1413
#define IDC_STATUS_LABEL 1414
#define IDC_STATUS 1415
#define IDC_MANUFACTURER_LABEL 1416
#define IDC_MANUFACTURER 1417
#define IDC_LOCATION_LABEL 1418
#define IDC_LOCATION 1419
#define IDC_PROPERTIES 1420
#define CLASS_KEY L"SYSTEM\\CurrentControlSet\\Control\\Class\\"
#define DN_HAS_PROBLEM_ 0x00000400

/* what the page keeps: the classes it lists and the devices found, one set per class. */
typedef struct PAGE {
    DWORD ClassCount;
    GUID *Classes;
    HDEVINFO *Sets;
} PAGE;

typedef struct INIT {
    PAGE *Page;
    BOOL Adopted;
} INIT;

HINSTANCE g_instance;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        g_instance = instance;
    return TRUE;
}

/*
 * dialog 1410 of devmgr.dll.mui of windows 11 26300: a child dialog of 210 by 147 units in MS Shell Dlg 8, with the
 * list of the devices, and a group with the manufacturer, the location and the status of the selected one.
 */
static const DM_ITEM g_items[] = {
    {IDC_DEVICES_LABEL, DM_STATIC, 0x50020000, 7, 7, 196, 10, L"&Devices:"},
    {IDC_DEVICES, 0, 0x5081804d, 7, 18, 196, 27, L"", L"SysListView32"},
    {IDC_GROUP, DM_BUTTON, 0x50000007, 7, 52, 196, 88, L"Device Properties"},
    {IDC_MANUFACTURER_LABEL, DM_STATIC, 0x50020000, 12, 66, 50, 10, L"Manufacturer:"},
    {IDC_MANUFACTURER, DM_EDIT, 0x50000880, 64, 66, 132, 10, L""},
    {IDC_LOCATION_LABEL, DM_STATIC, 0x50020000, 12, 80, 50, 10, L"Location:"},
    {IDC_LOCATION, DM_EDIT, 0x50000880, 64, 80, 132, 10, L""},
    {IDC_STATUS_LABEL, DM_STATIC, 0x50020000, 12, 94, 50, 10, L"Device status:"},
    {IDC_STATUS, DM_EDIT, 0x50000804, 64, 94, 132, 20, L""},
    {IDC_PROPERTIES, DM_BUTTON, 0x50010000, 136, 119, 60, 14, L"P&roperties"},
};

static WORD *put_text(WORD *p, const WCHAR *text)
{
    SIZE_T chars = wcslen(text) + 1;
    memcpy(p, text, chars * sizeof(WCHAR));
    return p + chars;
}

static WORD *align4(WORD *p)
{
    return (WORD *)(((ULONG_PTR)p + 3) & ~(ULONG_PTR)3);
}

void *DmBuildTemplate(DWORD style, short cx, short cy, const WCHAR *title, const DM_ITEM *items, DWORD count)
{
    SIZE_T size = 128 + wcslen(title) * 2;
    for (DWORD i = 0; i < count; i++)
        size += 64 + wcslen(items[i].Text) * 2 + (items[i].ClassName ? wcslen(items[i].ClassName) * 2 : 0);
    WORD *buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size), *p = buffer;
    if (!buffer)
        return nullptr;
    *p++ = 1;
    *p++ = 0xFFFF;
    p += 4;
    *(DWORD *)p = style;
    p += 2;
    *p++ = (WORD)count;
    *p++ = 0;
    *p++ = 0;
    *p++ = (WORD)cx;
    *p++ = (WORD)cy;
    *p++ = 0;
    *p++ = 0;
    p = put_text(p, title);
    *p++ = 8;
    *p++ = 0;
    *p++ = 0;
    p = put_text(p, L"MS Shell Dlg");
    for (DWORD i = 0; i < count; i++) {
        const DM_ITEM *item = &items[i];
        p = align4(p);
        p += 4;
        *(DWORD *)p = item->Style;
        p += 2;
        *p++ = (WORD)item->X;
        *p++ = (WORD)item->Y;
        *p++ = (WORD)item->Width;
        *p++ = (WORD)item->Height;
        *(DWORD *)p = item->Id;
        p += 2;
        if (item->Class) {
            *p++ = 0xFFFF;
            *p++ = item->Class;
        } else {
            p = put_text(p, item->ClassName);
        }
        p = put_text(p, item->Text);
        *p++ = 0;
    }
    return buffer;
}

static void add_column(HWND list, int index, const WCHAR *title, int width)
{
    LVCOLUMNW column;
    memset(&column, 0, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = (WCHAR *)title;
    column.cx = width;
    column.iSubItem = index;
    SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)index, (LPARAM)&column);
}

static BOOL device_text(HDEVINFO set, SP_DEVINFO_DATA *device, DWORD property, WCHAR *out, DWORD chars)
{
    out[0] = 0;
    return SetupDiGetDeviceRegistryPropertyW(set, device, property, nullptr, (BYTE *)out, chars * sizeof(WCHAR), nullptr) &&
           out[0];
}

/* the description of a class as its key gives it, or its name. */
static void class_text(const GUID *guid, WCHAR *out, DWORD chars)
{
    WCHAR path[128];
    _snwprintf(path, RTL_NUMBER_OF(path), CLASS_KEY L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", guid->Data1,
               guid->Data2, guid->Data3, guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4],
               guid->Data4[5], guid->Data4[6], guid->Data4[7]);
    out[0] = 0;
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key))
        return;
    DWORD size = (chars - 1) * sizeof(WCHAR);
    if (RegQueryValueExW(key, L"ClassDesc", nullptr, nullptr, (BYTE *)out, &size) || out[0] == L'@' || !out[0]) {
        size = (chars - 1) * sizeof(WCHAR);
        if (RegQueryValueExW(key, L"Class", nullptr, nullptr, (BYTE *)out, &size))
            size = 0;
    }
    out[size / sizeof(WCHAR)] = 0;
    RegCloseKey(key);
}

/* the parameter of a row is the class in the high word and the index of the device in its set in the low one. */
static void fill(HWND dialog, PAGE *page)
{
    HWND list = GetDlgItem(dialog, IDC_DEVICES);
    int row = 0;
    for (DWORD c = 0; c < page->ClassCount; c++) {
        page->Sets[c] = SetupDiGetClassDevsW(&page->Classes[c], nullptr, nullptr, DIGCF_PRESENT | DIGCF_PROFILE);
        if (page->Sets[c] == INVALID_HANDLE_VALUE)
            continue;
        WCHAR type[128];
        class_text(&page->Classes[c], type, RTL_NUMBER_OF(type));
        SP_DEVINFO_DATA device = {sizeof(device)};
        for (DWORD i = 0; i < 0x10000 && SetupDiEnumDeviceInfo(page->Sets[c], i, &device); i++) {
            WCHAR name[256];
            if (!device_text(page->Sets[c], &device, SPDRP_FRIENDLYNAME, name, RTL_NUMBER_OF(name)) &&
                !device_text(page->Sets[c], &device, SPDRP_DEVICEDESC, name, RTL_NUMBER_OF(name)))
                wcscpy(name, L"Unknown device");
            LVITEMW item;
            memset(&item, 0, sizeof(item));
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = row;
            item.pszText = name;
            item.lParam = (LPARAM)(c << 16 | i);
            int at = (int)SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
            if (at < 0)
                continue;
            memset(&item, 0, sizeof(item));
            item.iSubItem = 1;
            item.pszText = type;
            SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)at, (LPARAM)&item);
            row++;
        }
    }
    if (row) {
        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.stateMask = item.state = LVIS_SELECTED | LVIS_FOCUSED;
        SendMessageW(list, LVM_SETITEMSTATE, 0, (LPARAM)&item);
    }
}

static void show_selected(HWND dialog, PAGE *page)
{
    HWND list = GetDlgItem(dialog, IDC_DEVICES);
    int at = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    WCHAR manufacturer[256] = L"", location[256] = L"", status[256] = L"";
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_PARAM;
    item.iItem = at;
    SP_DEVINFO_DATA device = {sizeof(device)};
    if (at >= 0 && SendMessageW(list, LVM_GETITEMW, 0, (LPARAM)&item) && (DWORD)(item.lParam >> 16) < page->ClassCount &&
        SetupDiEnumDeviceInfo(page->Sets[item.lParam >> 16], (DWORD)item.lParam & 0xFFFF, &device)) {
        HDEVINFO set = page->Sets[item.lParam >> 16];
        if (!device_text(set, &device, SPDRP_MFG, manufacturer, RTL_NUMBER_OF(manufacturer)))
            wcscpy(manufacturer, L"Unknown");
        if (!device_text(set, &device, SPDRP_LOCATION_INFORMATION, location, RTL_NUMBER_OF(location)))
            wcscpy(location, L"Unknown");
        ULONG state = 0, problem = 0;
        if (CM_Get_DevNode_Status(&state, &problem, device.DevInst, 0) || (state & DN_HAS_PROBLEM_))
            _snwprintf(status, RTL_NUMBER_OF(status), L"This device has a problem that Windows cannot identify. (Code %lu)",
                       problem);
        else
            wcscpy(status, L"This device is working properly.");
    }
    SetDlgItemTextW(dialog, IDC_MANUFACTURER, manufacturer);
    SetDlgItemTextW(dialog, IDC_LOCATION, location);
    SetDlgItemTextW(dialog, IDC_STATUS, status);
}

/*
 * the page takes the client area of its parent: the list takes what is added, the group below it keeps its height at
 * the bottom, everything gets wider and the button stays at the right. measured on windows 11.
 */
static void layout(HWND dialog)
{
    RECT old, client;
    GetClientRect(dialog, &old);
    GetClientRect(GetParent(dialog), &client);
    int dx = client.right - old.right, dy = client.bottom - old.bottom;
    SetWindowPos(dialog, nullptr, 0, 0, client.right, client.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
    for (DWORD i = 0; i < RTL_NUMBER_OF(g_items); i++) {
        HWND control = GetDlgItem(dialog, g_items[i].Id);
        RECT r;
        GetWindowRect(control, &r);
        MapWindowPoints(nullptr, dialog, (POINT *)&r, 2);
        int x = r.left, y = r.top, width = r.right - r.left, height = r.bottom - r.top;
        if (g_items[i].Id == IDC_PROPERTIES)
            x += dx;
        else
            width += dx;
        if (g_items[i].Id == IDC_DEVICES)
            height += dy;
        else if (g_items[i].Id != IDC_DEVICES_LABEL)
            y += dy;
        SetWindowPos(control, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static void open_selected(HWND dialog, PAGE *page)
{
    HWND list = GetDlgItem(dialog, IDC_DEVICES);
    int at = (int)SendMessageW(list, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_PARAM;
    item.iItem = at;
    SP_DEVINFO_DATA device = {sizeof(device)};
    WCHAR id[MAX_DEVICE_ID_LEN];
    if (at < 0 || !SendMessageW(list, LVM_GETITEMW, 0, (LPARAM)&item) || (DWORD)(item.lParam >> 16) >= page->ClassCount ||
        !SetupDiEnumDeviceInfo(page->Sets[item.lParam >> 16], (DWORD)item.lParam & 0xFFFF, &device) ||
        !SetupDiGetDeviceInstanceIdW(page->Sets[item.lParam >> 16], &device, id, RTL_NUMBER_OF(id), nullptr))
        return;
    DevicePropertiesExW(GetParent(dialog), nullptr, id, 0, FALSE);
}

static void rebuild(HWND dialog, PAGE *page)
{
    HWND list = GetDlgItem(dialog, IDC_DEVICES);
    if (SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) > 0)
        return;
    fill(dialog, page);
    show_selected(dialog, page);
}

/*
 * CHWTab::ParentSubclassProc of devmgr.dll: the page follows the size of its parent, gets the notifications the
 * parent does not answer, a property sheet's PSN_SETACTIVE among them, and the commands of its own controls.
 */
static LRESULT CALLBACK parent_proc(HWND parent, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    HWND dialog = (HWND)data;
    switch (message) {
    case WM_SIZE:
        layout(dialog);
        return 0;
    case WM_NOTIFY: {
        LRESULT result = DefSubclassProc(parent, message, wparam, lparam);
        return result ? result : SendMessageW(dialog, message, wparam, lparam);
    }
    case WM_COMMAND:
        if (lparam && GetParent((HWND)lparam) == dialog)
            return SendMessageW(dialog, message, wparam, lparam);
        return DefSubclassProc(parent, message, wparam, lparam);
    default:
        return DefSubclassProc(parent, message, wparam, lparam);
    }
}

/*
 * CHWTab of devmgr.dll: the columns share the client area without the scroll bar, three quarters to the name, and
 * the list is filled when the page of a property sheet becomes active, not when the page is made.
 */
static INT_PTR CALLBACK page_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    PAGE *page = (PAGE *)GetWindowLongPtrW(dialog, DWLP_USER);
    switch (message) {
    case WM_INITDIALOG: {
        page = ((INIT *)lparam)->Page;
        ((INIT *)lparam)->Adopted = TRUE;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)page);
        layout(dialog);
        HWND list = GetDlgItem(dialog, IDC_DEVICES);
        RECT r;
        GetClientRect(list, &r);
        r.right -= GetSystemMetrics(SM_CXVSCROLL);
        add_column(list, 0, L"Name", 3 * r.right / 4);
        add_column(list, 1, L"Type", r.right - 3 * r.right / 4);
        SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                     LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);
        SetWindowTheme(list, L"Explorer", nullptr);
        if (!SetWindowSubclass(GetParent(dialog), parent_proc, 0, (DWORD_PTR)dialog))
            DestroyWindow(dialog);
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)lparam;
        if (!page)
            return FALSE;
        if (header->code == PSN_SETACTIVE)
            rebuild(dialog, page);
        else if (header->idFrom == IDC_DEVICES && header->code == LVN_ITEMCHANGED)
            show_selected(dialog, page);
        else if (header->idFrom == IDC_DEVICES && header->code == NM_DBLCLK)
            open_selected(dialog, page);
        return FALSE;
    }
    case WM_COMMAND:
        if (page && LOWORD(wparam) == IDC_PROPERTIES)
            open_selected(dialog, page);
        return FALSE;
    case WM_SYSCOLORCHANGE:
    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
        SendMessageW(GetDlgItem(dialog, IDC_DEVICES), message, wparam, lparam);
        return FALSE;
    case WM_DESTROY:
        RemoveWindowSubclass(GetParent(dialog), parent_proc, 0);
        if (page) {
            for (DWORD c = 0; c < page->ClassCount; c++)
                if (page->Sets[c] && page->Sets[c] != INVALID_HANDLE_VALUE)
                    SetupDiDestroyDeviceInfoList(page->Sets[c]);
            HeapFree(GetProcessHeap(), 0, page);
            SetWindowLongPtrW(dialog, DWLP_USER, 0);
        }
        return FALSE;
    default:
        return FALSE;
    }
}

/* without a parent or without a class there is no page, and the last error is left alone. */
HWND WINAPI DeviceCreateHardwarePageEx(HWND parent, const GUID *classes, int count, DWORD display)
{
    if (!parent || !classes || count <= 0)
        return nullptr;
    PAGE *page = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           sizeof(*page) + (SIZE_T)count * (sizeof(GUID) + sizeof(HDEVINFO)));
    void *template = DmBuildTemplate(WS_CHILD | WS_VISIBLE | WS_CAPTION | DS_3DLOOK | DS_SETFONT | DS_CONTROL, 210, 147, L"",
                                     g_items, RTL_NUMBER_OF(g_items));
    if (!page || !template) {
        HeapFree(GetProcessHeap(), 0, page);
        HeapFree(GetProcessHeap(), 0, template);
        return nullptr;
    }
    page->ClassCount = (DWORD)count;
    page->Sets = (HDEVINFO *)(page + 1);
    page->Classes = (GUID *)(page->Sets + count);
    memcpy(page->Classes, classes, (SIZE_T)count * sizeof(GUID));
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    INIT init = {page, FALSE};
    HWND dialog = CreateDialogIndirectParamW(g_instance, template, parent, page_proc, (LPARAM)&init);
    HeapFree(GetProcessHeap(), 0, template);
    if (!init.Adopted)
        HeapFree(GetProcessHeap(), 0, page);
    return dialog;
}

HWND WINAPI DeviceCreateHardwarePage(HWND parent, const GUID *klass)
{
    return DeviceCreateHardwarePageEx(parent, klass, 1, 0);
}
