#include "devmgr_private.h"

#define IDC_GENERAL_ICON 218
#define IDC_GENERAL_NAME 219
#define IDC_GENERAL_TYPE 220
#define IDC_GENERAL_MANUFACTURER 221
#define IDC_GENERAL_STATUS 223
#define IDC_GENERAL_TROUBLESHOOT 216
#define IDC_GENERAL_SETTINGS 217
#define IDC_GENERAL_LOCATION 358
#define IDC_DRIVER_ICON 226
#define IDC_DRIVER_NAME 227
#define IDC_DRIVER_PROVIDER 385
#define IDC_DRIVER_DATE 387
#define IDC_DRIVER_VERSION 389
#define IDC_DRIVER_SIGNER 393
#define IDC_DRIVER_DETAILS 390
#define IDC_DRIVER_DETAILS_TEXT 391
#define IDC_DRIVER_UPDATE 380
#define IDC_DRIVER_UPDATE_TEXT 381
#define IDC_DRIVER_ROLLBACK 394
#define IDC_DRIVER_DISABLE 378
#define IDC_DRIVER_DISABLE_TEXT 379
#define IDC_DRIVER_UNINSTALL 382
#define IDC_DRIVER_UNINSTALL_TEXT 383
#define IDC_DETAILS_ICON 373
#define IDC_DETAILS_NAME 374
#define IDC_DETAILS_PROPERTY 375
#define IDC_DETAILS_VALUE 376
#define IDC_EVENTS_ICON 261
#define IDC_EVENTS_NAME 262
#define IDC_EVENTS_LIST 263
#define IDC_EVENTS_VIEW 265
#define PAGE_STYLE 0x48c00040
#define DN_DISABLEABLE_ 0x00002000
#define DN_HAS_PROBLEM_ 0x00000400
#define CR_SUCCESS_ 0
#define MAX_KEYS 512

/* the device a sheet shows, which its pages share. */
typedef struct DEVICE {
    HDEVINFO Set;
    SP_DEVINFO_DATA Data;
    WCHAR Name[256];
    GUID Class;
    DEVPROPKEY *Keys;
    ULONG KeyCount, ClassKeyCount;
} DEVICE;

/* the four pages of a device of windows 11 26300, dialogs 103, 106, 372 and 260 of devmgr.dll.mui. */
static const DM_ITEM g_general[] = {
    {IDC_GENERAL_ICON, DM_STATIC, 0x50000003, 7, 7, 20, 20, L""},
    {IDC_GENERAL_NAME, DM_STATIC, 0x50020080, 37, 9, 208, 17, L""},
    {354, DM_STATIC, 0x50020080, 37, 36, 60, 8, L"Device type:"},
    {IDC_GENERAL_TYPE, DM_EDIT, 0x50000880, 100, 36, 146, 12, L""},
    {355, DM_STATIC, 0x50020080, 37, 50, 60, 8, L"Manufacturer:"},
    {IDC_GENERAL_MANUFACTURER, DM_EDIT, 0x50000880, 100, 50, 145, 12, L""},
    {357, DM_STATIC, 0x50020080, 37, 64, 60, 8, L"Location:"},
    {IDC_GENERAL_LOCATION, DM_EDIT, 0x50000804, 100, 64, 145, 16, L""},
    {356, DM_BUTTON, 0x50000007, 7, 85, 238, 108, L"Device status"},
    {IDC_GENERAL_STATUS, DM_EDIT, 0x50a10844, 14, 96, 224, 70, L""},
    {IDC_GENERAL_TROUBLESHOOT, DM_BUTTON, 0x50010000, 148, 171, 90, 15, L"&Troubleshoot..."},
    {IDC_GENERAL_SETTINGS, DM_BUTTON, 0x50010000, 7, 195, 84, 14, L"Change settings"},
};

static const DM_ITEM g_driver[] = {
    {IDC_DRIVER_ICON, DM_STATIC, 0x50000003, 7, 7, 20, 20, L""},
    {IDC_DRIVER_NAME, DM_STATIC, 0x50020080, 37, 9, 208, 17, L""},
    {384, DM_STATIC, 0x50020080, 37, 36, 55, 8, L"Driver Provider:"},
    {IDC_DRIVER_PROVIDER, DM_EDIT, 0x50000880, 100, 36, 150, 12, L""},
    {386, DM_STATIC, 0x50020080, 37, 50, 55, 8, L"Driver Date:"},
    {IDC_DRIVER_DATE, DM_EDIT, 0x50000880, 100, 50, 150, 12, L""},
    {388, DM_STATIC, 0x50020080, 37, 64, 55, 8, L"Driver Version:"},
    {IDC_DRIVER_VERSION, DM_EDIT, 0x50000880, 100, 64, 150, 12, L""},
    {392, DM_STATIC, 0x50020080, 37, 78, 55, 8, L"Digital Signer:"},
    {IDC_DRIVER_SIGNER, DM_EDIT, 0x50000804, 100, 78, 150, 16, L""},
    {IDC_DRIVER_DETAILS, DM_BUTTON, 0x50030000, 7, 100, 70, 15, L"Dr&iver Details"},
    {IDC_DRIVER_DETAILS_TEXT, DM_STATIC, 0x50020000, 91, 104, 154, 16, L"View details about the installed driver files."},
    {IDC_DRIVER_UPDATE, DM_BUTTON, 0x50030000, 7, 123, 70, 15, L"U&pdate Driver"},
    {IDC_DRIVER_UPDATE_TEXT, DM_STATIC, 0x50020000, 91, 127, 154, 16, L"Update the driver for this device."},
    {IDC_DRIVER_ROLLBACK, DM_BUTTON, 0x50030000, 7, 146, 70, 15, L"&Roll Back Driver"},
    {395, DM_STATIC, 0x50020000, 91, 146, 154, 16,
     L"If the device fails after updating the driver, roll back to the previously installed driver."},
    {IDC_DRIVER_DISABLE, DM_BUTTON, 0x50030000, 7, 171, 70, 15, L"D&isable Device"},
    {IDC_DRIVER_DISABLE_TEXT, DM_STATIC, 0x50020000, 91, 175, 154, 16, L"Disable the device."},
    {IDC_DRIVER_UNINSTALL, DM_BUTTON, 0x50030000, 7, 196, 70, 15, L"&Uninstall Device"},
    {IDC_DRIVER_UNINSTALL_TEXT, DM_STATIC, 0x50020000, 91, 200, 154, 16, L"Uninstall the device from the system (Advanced)."},
};

static const DM_ITEM g_details[] = {
    {IDC_DETAILS_ICON, DM_STATIC, 0x50000003, 7, 7, 21, 20, L""},
    {IDC_DETAILS_NAME, DM_STATIC, 0x50020080, 37, 9, 208, 17, L"Device description"},
    {0xFFFF, DM_STATIC, 0x50020000, 7, 35, 238, 8, L"&Property"},
    {IDC_DETAILS_PROPERTY, DM_COMBOBOX, 0x50210003, 7, 46, 238, 72, L""},
    {0xFFFF, DM_STATIC, 0x50020000, 7, 67, 238, 8, L"&Value"},
    {IDC_DETAILS_VALUE, 0, 0x5081c041, 7, 78, 238, 132, L"List1", L"SysListView32"},
};

static const DM_ITEM g_events[] = {
    {IDC_EVENTS_ICON, DM_STATIC, 0x50000003, 7, 7, 21, 20, L""},
    {IDC_EVENTS_NAME, DM_STATIC, 0x50020080, 37, 9, 208, 17, L"Device description"},
    {0xFFFF, DM_STATIC, 0x50020000, 7, 35, 238, 8, L"&Events"},
    {IDC_EVENTS_LIST, 0, 0x50818045, 7, 46, 238, 80, L"List1", L"SysListView32"},
    {0xFFFF, DM_STATIC, 0x50020000, 7, 130, 238, 8, L"&Information"},
    {264, DM_EDIT, 0x50a10844, 7, 141, 238, 49, L""},
    {IDC_EVENTS_VIEW, DM_BUTTON, 0x50010000, 7, 196, 70, 15, L"&View All Events..."},
};

#include "names.inc"

MIME_DEVPROPKEY(DEVPKEY_Device_DriverDate_, 0xA8B865DD, 0x2E3D, 0x4094, 0xAD, 0x97, 0xE5, 0x93, 0xA7, 0x0C, 0x75, 0xD6, 2);
MIME_DEVPROPKEY(DEVPKEY_Device_DriverVersion_, 0xA8B865DD, 0x2E3D, 0x4094, 0xAD, 0x97, 0xE5, 0x93, 0xA7, 0x0C, 0x75, 0xD6, 3);
MIME_DEVPROPKEY(DEVPKEY_Device_DriverProvider_, 0xA8B865DD, 0x2E3D, 0x4094, 0xAD, 0x97, 0xE5, 0x93, 0xA7, 0x0C, 0x75, 0xD6, 9);
MIME_DEVPROPKEY(DEVPKEY_Device_Capabilities_, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0, 17);
MIME_DEVPROPKEY(DEVPKEY_Device_ConfigFlags_, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0, 12);
MIME_DEVPROPKEY(DEVPKEY_Device_PowerData_, 0xA45C254E, 0xDF1C, 0x4EFD, 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0, 32);
MIME_DEVPROPKEY(DEVPKEY_Device_DevNodeStatus_, 0x4340A6C5, 0x93FA, 0x4706, 0x97, 0x2C, 0x7B, 0x64, 0x80, 0x08, 0xA5, 0xA7, 2);
MIME_DEVPROPKEY(DEVPKEY_DeviceClass_Name_, 0x259ABFFC, 0x50A7, 0x47CE, 0xAF, 0x08, 0x68, 0xC9, 0xA7, 0xD7, 0x33, 0x66, 2);
MIME_DEVPROPKEY(DEVPKEY_DeviceClass_IconPath_, 0x259ABFFC, 0x50A7, 0x47CE, 0xAF, 0x08, 0x68, 0xC9, 0xA7, 0xD7, 0x33, 0x66, 12);

static const WCHAR *const g_status_flags[] = {
    L"DN_ROOT_ENUMERATED", L"DN_DRIVER_LOADED", L"DN_ENUM_LOADED", L"DN_STARTED", L"DN_MANUAL", L"DN_NEED_TO_ENUM",
    L"DN_DRIVER_BLOCKED", L"DN_HARDWARE_ENUM", L"DN_NEED_RESTART", L"DN_HAS_MARK", L"DN_HAS_PROBLEM", L"DN_FILTERED",
    L"DN_MOVED", L"DN_DISABLEABLE", L"DN_REMOVABLE", L"DN_PRIVATE_PROBLEM", L"DN_QUERY_REMOVE_PENDING",
    L"DN_QUERY_REMOVE_ACTIVE", L"DN_WILL_BE_REMOVED", L"DN_NOT_FIRST_TIMEE", L"DN_STOP_FREE_RES",
    L"DN_REBAL_CANDIADATE", L"DN_BAD_PARTIAL", L"DN_NT_ENUMERATOR", L"DN_NT_DRIVER", L"DN_DEVICE_DISCONNECTED",
    L"DN_ARM_WAKEUP", L"DN_APM_ENUMERATOR", L"DN_APM_DRIVER", L"DN_SILENT_INSTALL", L"DN_NO_SHOW_IN_DM",
    L"DN_BOOT_LOG_PROB",
};

static const WCHAR *const g_capability_flags[] = {
    L"CM_DEVCAP_LOCKSUPPORTED", L"CM_DEVCAP_EJECTSUPPORTED", L"CM_DEVCAP_REMOVABLE", L"CM_DEVCAP_DOCKDEVICE",
    L"CM_DEVCAP_UNIQUEID", L"CM_DEVCAP_SILENTINSTALL", L"CM_DEVCAP_RAWDEVICEOK", L"CM_DEVCAP_SURPRISEREMOVALOK",
    L"CM_DEVCAP_HARDWAREDISABLED", L"CM_DEVCAP_NONDYNAMIC", L"CM_DEVCAP_SECUREDEVICE",
};

static const WCHAR *const g_config_flags[] = {
    L"CONFIGFLAG_DISABLED", L"CONFIGFLAG_REMOVED", L"CONFIGFLAG_MANUAL_INSTALL", L"CONFIGFLAG_IGNORE_BOOT_LC",
    L"CONFIGFLAG_NET_BOOT", L"CONFIGFLAG_REINSTALL", L"CONFIGFLAG_FAILEDINSTALL", L"CONFIGFLAG_CANTSTOPACHILD",
    L"CONFIGFLAG_OKREMOVEROM", L"CONFIGFLAG_NOREMOVEEXIT", L"CONFIGFLAG_FINISH_INSTALL",
    L"CONFIGFLAG_NEEDS_FORCED_CONFIG", L"CONFIGFLAG_NETBOOT_CARD", L"CONFIGFLAG_PARTIAL_LOG_CONF",
    L"CONFIGFLAG_SUPPRESS_SURPRISE", L"CONFIGFLAG_VERIFY_HARDWARE", L"CONFIGFLAG_FINISHINSTALL_UI",
    L"CONFIGFLAG_FINISHINSTALL_ACTION", L"CONFIGFLAG_BOOT_DEVICE", L"CONFIGFLAG_NEEDS_CLASS_CONFIG",
};

static const WCHAR *const g_power_flags[] = {
    L"PDCAP_D0_SUPPORTED", L"PDCAP_D1_SUPPORTED", L"PDCAP_D2_SUPPORTED", L"PDCAP_D3_SUPPORTED",
    L"PDCAP_WAKE_FROM_D0_SUPPORTED", L"PDCAP_WAKE_FROM_D1_SUPPORTED", L"PDCAP_WAKE_FROM_D2_SUPPORTED",
    L"PDCAP_WAKE_FROM_D3_SUPPORTED", L"PDCAP_WARM_EJECT_SUPPORTED",
};

static BOOL same_key(const DEVPROPKEY *a, const DEVPROPKEY *b)
{
    return a->pid == b->pid && !memcmp(&a->fmtid, &b->fmtid, sizeof(GUID));
}

/* a property of the device, or of its class when the index says so, in memory of the heap. */
static BYTE *get_property(DEVICE *d, const DEVPROPKEY *key, BOOL klass, DEVPROPTYPE *type, ULONG *size)
{
    *size = 0;
    *type = 0;
    CONFIGRET cr = klass ? CM_Get_Class_PropertyW(&d->Class, key, type, nullptr, size, 0)
                         : CM_Get_DevNode_PropertyW(d->Data.DevInst, key, type, nullptr, size, 0);
    if (cr != CR_BUFFER_SMALL || !*size)
        return nullptr;
    BYTE *buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *size + sizeof(WCHAR) * 2);
    if (!buffer)
        return nullptr;
    cr = klass ? CM_Get_Class_PropertyW(&d->Class, key, type, buffer, size, 0)
               : CM_Get_DevNode_PropertyW(d->Data.DevInst, key, type, buffer, size, 0);
    if (cr != CR_SUCCESS_) {
        HeapFree(GetProcessHeap(), 0, buffer);
        return nullptr;
    }
    return buffer;
}

static BOOL property_text(DEVICE *d, const DEVPROPKEY *key, BOOL klass, WCHAR *out, DWORD chars)
{
    DEVPROPTYPE type;
    ULONG size;
    out[0] = 0;
    BYTE *value = get_property(d, key, klass, &type, &size);
    if (value && (type == DEVPROP_TYPE_STRING || type == DEVPROP_TYPE_STRING_LIST))
        lstrcpynW(out, (WCHAR *)value, (int)chars);
    HeapFree(GetProcessHeap(), 0, value);
    return out[0] != 0;
}

static void date_text(const FILETIME *time, BOOL with_time, WCHAR *out, DWORD chars)
{
    SYSTEMTIME utc, local;
    out[0] = 0;
    if (!FileTimeToSystemTime(time, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local))
        return;
    /* a driver date has no time: it is the day in universal time. */
    const SYSTEMTIME *shown = with_time ? &local : &utc;
    int n = GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, shown, nullptr, out, (int)chars, nullptr);
    if (n > 0 && with_time && (DWORD)n < chars) {
        out[n - 1] = L' ';
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, shown, nullptr, out + n, (int)(chars - n));
    }
}

/* the icon of the class, from the path of its icon: a library and a negative resource number after a comma. */
static HICON class_icon(DEVICE *d)
{
    WCHAR path[MAX_PATH], expanded[MAX_PATH];
    if (!property_text(d, &DEVPKEY_DeviceClass_IconPath_, TRUE, path, RTL_NUMBER_OF(path)))
        return nullptr;
    WCHAR *comma = wcsrchr(path, L',');
    if (!comma)
        return nullptr;
    *comma = 0;
    int id = 0;
    for (const WCHAR *c = comma[1] == L'-' ? comma + 2 : comma + 1; *c >= L'0' && *c <= L'9'; c++)
        id = id * 10 + (*c - L'0');
    if (!ExpandEnvironmentStringsW(path, expanded, RTL_NUMBER_OF(expanded)))
        return nullptr;
    HMODULE module = LoadLibraryExW(expanded, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module)
        return nullptr;
    HICON icon = LoadImageW(module, MAKEINTRESOURCEW(id), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                            GetSystemMetrics(SM_CYICON), 0);
    FreeLibrary(module);
    return icon;
}

static void header(HWND page, DEVICE *d, int icon_id, int name_id)
{
    HICON icon = class_icon(d);
    if (icon) {
        HICON old = (HICON)SendDlgItemMessageW(page, icon_id, STM_SETICON, (WPARAM)icon, 0);
        if (old)
            DestroyIcon(old);
    }
    SetDlgItemTextW(page, name_id, d->Name);
}

static void status_text(DEVICE *d, WCHAR *out, DWORD chars)
{
    ULONG status = 0, problem = 0;
    if (CM_Get_DevNode_Status(&status, &problem, d->Data.DevInst, 0) != CR_SUCCESS_ || !(status & DN_HAS_PROBLEM_))
        lstrcpynW(out, L"This device is working properly.", (int)chars);
    else
        _snwprintf(out, chars, L"This device is not working properly. (Code %lu)", problem);
    out[chars - 1] = 0;
}

/* SetupDiGetClassDescription: the description of the class key, its default value, or the name of the class. */
static BOOL class_description(const GUID *g, WCHAR *out, DWORD chars)
{
    WCHAR path[128];
    _snwprintf(path, RTL_NUMBER_OF(path),
               L"SYSTEM\\CurrentControlSet\\Control\\Class\\{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g->Data1,
               g->Data2, g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5],
               g->Data4[6], g->Data4[7]);
    HKEY key;
    out[0] = 0;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key))
        return FALSE;
    DWORD needed = 0;
    if (RegLoadMUIStringW(key, L"ClassDesc", out, chars * sizeof(WCHAR), &needed, 0, nullptr))
        out[0] = 0;
    static const WCHAR *const values[] = {L"", L"Class"};
    for (DWORD i = 0; !out[0] && i < RTL_NUMBER_OF(values); i++) {
        DWORD size = (chars - 1) * sizeof(WCHAR), type = 0;
        if (RegQueryValueExW(key, values[i], nullptr, &type, (BYTE *)out, &size) || type != REG_SZ)
            size = 0;
        out[size / sizeof(WCHAR)] = 0;
    }
    RegCloseKey(key);
    return out[0] != 0;
}

static INT_PTR CALLBACK general_proc(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message != WM_INITDIALOG)
        return FALSE;
    DEVICE *d = (DEVICE *)((PROPSHEETPAGEW *)lparam)->lParam;
    WCHAR text[512];
    header(page, d, IDC_GENERAL_ICON, IDC_GENERAL_NAME);
    if (!property_text(d, &DEVPKEY_DeviceClass_Name_, TRUE, text, RTL_NUMBER_OF(text)) &&
        !class_description(&d->Class, text, RTL_NUMBER_OF(text)))
        wcscpy(text, L"Unknown");
    SetDlgItemTextW(page, IDC_GENERAL_TYPE, text);
    if (!property_text(d, &DEVPKEY_Device_Manufacturer, FALSE, text, RTL_NUMBER_OF(text)))
        wcscpy(text, L"Unknown");
    SetDlgItemTextW(page, IDC_GENERAL_MANUFACTURER, text);
    if (!property_text(d, &DEVPKEY_Device_LocationInfo, FALSE, text, RTL_NUMBER_OF(text)))
        wcscpy(text, L"Unknown");
    SetDlgItemTextW(page, IDC_GENERAL_LOCATION, text);
    status_text(d, text, RTL_NUMBER_OF(text));
    SetDlgItemTextW(page, IDC_GENERAL_STATUS, text);
    /* troubleshooting and the settings of an installation are not offered for a device that works. */
    ShowWindow(GetDlgItem(page, IDC_GENERAL_TROUBLESHOOT), SW_HIDE);
    ShowWindow(GetDlgItem(page, IDC_GENERAL_SETTINGS), SW_HIDE);
    return TRUE;
}

static void enable(HWND page, int id, BOOL on)
{
    EnableWindow(GetDlgItem(page, id), on);
}

/*
 * CDeviceDriverPage::UpdateControls of devmgr.dll: the button of the state says disable or enable, and the actions
 * that change the installation are off. mime installs no driver, so updating, rolling back, uninstalling and the
 * list of the files of the driver are off with their texts, and disabling is off as for a device that cannot be.
 */
static INT_PTR CALLBACK driver_proc(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message != WM_INITDIALOG)
        return FALSE;
    DEVICE *d = (DEVICE *)((PROPSHEETPAGEW *)lparam)->lParam;
    WCHAR text[256];
    header(page, d, IDC_DRIVER_ICON, IDC_DRIVER_NAME);
    property_text(d, &DEVPKEY_Device_DriverProvider_, FALSE, text, RTL_NUMBER_OF(text));
    SetDlgItemTextW(page, IDC_DRIVER_PROVIDER, text);
    DEVPROPTYPE type;
    ULONG size;
    BYTE *value = get_property(d, &DEVPKEY_Device_DriverDate_, FALSE, &type, &size);
    text[0] = 0;
    if (value && type == DEVPROP_TYPE_FILETIME)
        date_text((FILETIME *)value, FALSE, text, RTL_NUMBER_OF(text));
    HeapFree(GetProcessHeap(), 0, value);
    SetDlgItemTextW(page, IDC_DRIVER_DATE, text);
    property_text(d, &DEVPKEY_Device_DriverVersion_, FALSE, text, RTL_NUMBER_OF(text));
    SetDlgItemTextW(page, IDC_DRIVER_VERSION, text);
    SetDlgItemTextW(page, IDC_DRIVER_SIGNER, L"Not digitally signed");
    SetDlgItemTextW(page, IDC_DRIVER_DISABLE, L"&Disable Device");
    SetDlgItemTextW(page, IDC_DRIVER_DISABLE_TEXT, L"Disable the device.");
    static const int off[] = {IDC_DRIVER_DETAILS,  IDC_DRIVER_DETAILS_TEXT,   IDC_DRIVER_UPDATE,
                              IDC_DRIVER_UPDATE_TEXT, IDC_DRIVER_ROLLBACK,   IDC_DRIVER_DISABLE,
                              IDC_DRIVER_DISABLE_TEXT, IDC_DRIVER_UNINSTALL, IDC_DRIVER_UNINSTALL_TEXT};
    for (DWORD i = 0; i < RTL_NUMBER_OF(off); i++)
        enable(page, off[i], FALSE);
    return TRUE;
}

static const WCHAR *key_name(const DEVPROPKEY *key, WCHAR *buffer, DWORD chars)
{
    for (DWORD i = 0; i < RTL_NUMBER_OF(g_names); i++)
        if (same_key(&g_names[i].Key, key))
            return g_names[i].Name;
    const GUID *g = &key->fmtid;
    _snwprintf(buffer, chars, L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}[%lu]", g->Data1, g->Data2, g->Data3,
               g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7],
               key->pid);
    buffer[chars - 1] = 0;
    return buffer;
}

static void add_value(HWND list, const WCHAR *text)
{
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT;
    item.iItem = (int)SendMessageW(list, LVM_GETITEMCOUNT, 0, 0);
    item.pszText = (WCHAR *)text;
    SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
}

static void add_flags(HWND list, ULONG value, const WCHAR *const *names, DWORD count)
{
    WCHAR text[16];
    _snwprintf(text, RTL_NUMBER_OF(text), L"%08lX", value);
    add_value(list, text);
    for (DWORD bit = 0; bit < count; bit++)
        if (value & (1u << bit))
            add_value(list, names[bit]);
}

static const WCHAR *power_state(ULONG state)
{
    static const WCHAR *const names[] = {L"Unspecified", L"D0", L"D1", L"D2", L"D3"};
    return state < RTL_NUMBER_OF(names) ? names[state] : names[0];
}

/* CM_POWER_DATA: the size, the last state, the capabilities, three latencies, the mappings of S0 to S5 and more. */
static void add_power(HWND list, const BYTE *data, ULONG size)
{
    ULONG v[16] = {};
    memcpy(v, data, size < sizeof(v) ? size : sizeof(v));
    add_value(list, L"Current power state:");
    add_value(list, power_state(v[1]));
    add_value(list, L"");
    add_value(list, L"Power capabilities:");
    add_flags(list, v[2], g_power_flags, RTL_NUMBER_OF(g_power_flags));
    add_value(list, L"");
    add_value(list, L"Power state mappings:");
    for (DWORD s = 0; s < 6; s++) {
        WCHAR text[48];
        _snwprintf(text, RTL_NUMBER_OF(text), L"S%lu -> %ls", s, power_state(v[6 + s + 1]));
        add_value(list, text);
    }
}

static void show_value(HWND page, DEVICE *d, DWORD index)
{
    HWND list = GetDlgItem(page, IDC_DETAILS_VALUE);
    SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    if (index >= d->KeyCount + d->ClassKeyCount)
        return;
    const DEVPROPKEY *key = &d->Keys[index];
    DEVPROPTYPE type;
    ULONG size;
    BYTE *value = get_property(d, key, index >= d->KeyCount, &type, &size);
    if (!value)
        return;
    WCHAR text[160];
    if (same_key(key, &DEVPKEY_Device_DevNodeStatus_) && type == DEVPROP_TYPE_UINT32) {
        add_flags(list, *(ULONG *)value, g_status_flags, RTL_NUMBER_OF(g_status_flags));
    } else if (same_key(key, &DEVPKEY_Device_Capabilities_) && type == DEVPROP_TYPE_UINT32) {
        add_flags(list, *(ULONG *)value, g_capability_flags, RTL_NUMBER_OF(g_capability_flags));
    } else if (same_key(key, &DEVPKEY_Device_ConfigFlags_) && type == DEVPROP_TYPE_UINT32) {
        add_flags(list, *(ULONG *)value, g_config_flags, RTL_NUMBER_OF(g_config_flags));
    } else if (same_key(key, &DEVPKEY_Device_PowerData_)) {
        add_power(list, value, size);
    } else {
        switch (type) {
        case DEVPROP_TYPE_STRING:
            add_value(list, (WCHAR *)value);
            break;
        case DEVPROP_TYPE_STRING_LIST:
            for (const WCHAR *s = (WCHAR *)value; *s; s += wcslen(s) + 1)
                add_value(list, s);
            break;
        case DEVPROP_TYPE_UINT32:
        case DEVPROP_TYPE_INT32:
            _snwprintf(text, RTL_NUMBER_OF(text), L"%08lX", *(ULONG *)value);
            add_value(list, text);
            break;
        case DEVPROP_TYPE_UINT64:
            _snwprintf(text, RTL_NUMBER_OF(text), L"%016llX", *(ULONGLONG *)value);
            add_value(list, text);
            break;
        case DEVPROP_TYPE_BOOLEAN:
            add_value(list, *value ? L"<true>" : L"<false>");
            break;
        case DEVPROP_TYPE_GUID: {
            const GUID *g = (GUID *)value;
            _snwprintf(text, RTL_NUMBER_OF(text), L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g->Data1,
                       g->Data2, g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5],
                       g->Data4[6], g->Data4[7]);
            add_value(list, text);
            break;
        }
        case DEVPROP_TYPE_FILETIME:
            date_text((FILETIME *)value, !same_key(key, &DEVPKEY_Device_DriverDate_), text, RTL_NUMBER_OF(text));
            add_value(list, text);
            break;
        default:
            /* bytes, twenty to a line, each with a space after it. */
            for (ULONG at = 0; at < size; at += 20) {
                WCHAR line[64];
                DWORD n = 0;
                for (ULONG i = at; i < size && i < at + 20; i++)
                    n += _snwprintf(line + n, RTL_NUMBER_OF(line) - n, L"%02X ", value[i]);
                add_value(list, line);
            }
            if (!size)
                add_value(list, L"<empty>");
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, value);
    LVITEMW first;
    memset(&first, 0, sizeof(first));
    first.stateMask = first.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(list, LVM_SETITEMSTATE, 0, (LPARAM)&first);
}

static int compare_keys(const DEVPROPKEY *a, const DEVPROPKEY *b)
{
    WCHAR one[96], two[96];
    return lstrcmpiW(key_name(a, one, RTL_NUMBER_OF(one)), key_name(b, two, RTL_NUMBER_OF(two)));
}

static void sort_keys(DEVPROPKEY *keys, ULONG count)
{
    for (ULONG i = 1; i < count; i++) {
        DEVPROPKEY key = keys[i];
        ULONG j = i;
        for (; j > 0 && compare_keys(&keys[j - 1], &key) > 0; j--)
            keys[j] = keys[j - 1];
        keys[j] = key;
    }
}

/*
 * the keys of the details page: those of the head list of devmgr.dll the device has, in its order, then the other
 * keys of the device by name, then the keys of its class by name. names come from the table of devmgr.dll; a key
 * without one shows as its guid and number.
 */
static void read_keys(DEVICE *d)
{
    ULONG count = 0, class_count = 0;
    CM_Get_DevNode_Property_Keys(d->Data.DevInst, nullptr, &count, 0);
    CM_Get_Class_Property_Keys(&d->Class, nullptr, &class_count, 0);
    if (count > MAX_KEYS)
        count = MAX_KEYS;
    if (class_count > MAX_KEYS)
        class_count = MAX_KEYS;
    DEVPROPKEY *found = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count + 1) * sizeof(DEVPROPKEY));
    d->Keys = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count + class_count + 1) * sizeof(DEVPROPKEY));
    if (!found || !d->Keys) {
        HeapFree(GetProcessHeap(), 0, found);
        return;
    }
    ULONG got = count;
    if (count && CM_Get_DevNode_Property_Keys(d->Data.DevInst, found, &got, 0) != CR_SUCCESS_)
        got = 0;
    ULONG n = 0;
    for (DWORD f = 0; f < RTL_NUMBER_OF(g_first); f++)
        for (ULONG i = 0; i < got; i++)
            if (same_key(&found[i], &g_first[f])) {
                d->Keys[n++] = found[i];
                found[i].pid = 0xFFFFFFFF;
                break;
            }
    ULONG rest = n;
    for (ULONG i = 0; i < got; i++)
        if (found[i].pid != 0xFFFFFFFF)
            d->Keys[n++] = found[i];
    sort_keys(d->Keys + rest, n - rest);
    d->KeyCount = n;
    ULONG class_got = class_count;
    if (class_count && CM_Get_Class_Property_Keys(&d->Class, d->Keys + n, &class_got, 0) != CR_SUCCESS_)
        class_got = 0;
    sort_keys(d->Keys + n, class_got);
    d->ClassKeyCount = class_got;
    HeapFree(GetProcessHeap(), 0, found);
}

static INT_PTR CALLBACK details_proc(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    DEVICE *d = (DEVICE *)GetWindowLongPtrW(page, DWLP_USER);
    switch (message) {
    case WM_INITDIALOG: {
        d = (DEVICE *)((PROPSHEETPAGEW *)lparam)->lParam;
        SetWindowLongPtrW(page, DWLP_USER, (LONG_PTR)d);
        header(page, d, IDC_DETAILS_ICON, IDC_DETAILS_NAME);
        HWND list = GetDlgItem(page, IDC_DETAILS_VALUE);
        RECT r;
        GetClientRect(list, &r);
        LVCOLUMNW column;
        memset(&column, 0, sizeof(column));
        column.mask = LVCF_WIDTH;
        column.cx = r.right;
        SendMessageW(list, LVM_INSERTCOLUMNW, 0, (LPARAM)&column);
        HWND combo = GetDlgItem(page, IDC_DETAILS_PROPERTY);
        if (!d->Keys)
            read_keys(d);
        for (ULONG i = 0; d->Keys && i < d->KeyCount + d->ClassKeyCount; i++) {
            WCHAR buffer[96];
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)key_name(&d->Keys[i], buffer, RTL_NUMBER_OF(buffer)));
        }
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
        show_value(page, d, 0);
        return TRUE;
    }
    case WM_COMMAND:
        if (d && LOWORD(wparam) == IDC_DETAILS_PROPERTY && HIWORD(wparam) == CBN_SELCHANGE)
            show_value(page, d, (DWORD)SendDlgItemMessageW(page, IDC_DETAILS_PROPERTY, CB_GETCURSEL, 0, 0));
        return FALSE;
    default:
        return FALSE;
    }
}

/*
 * the events windows keeps of the configuration of a device, in the log of Microsoft-Windows-Kernel-PnP. mime keeps
 * no such log, so the list is empty and there is no event viewer to open.
 */
static INT_PTR CALLBACK events_proc(HWND page, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message != WM_INITDIALOG)
        return FALSE;
    DEVICE *d = (DEVICE *)((PROPSHEETPAGEW *)lparam)->lParam;
    header(page, d, IDC_EVENTS_ICON, IDC_EVENTS_NAME);
    HWND list = GetDlgItem(page, IDC_EVENTS_LIST);
    RECT r;
    GetClientRect(list, &r);
    int width = r.right - GetSystemMetrics(SM_CXVSCROLL);
    LVCOLUMNW column;
    memset(&column, 0, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = (WCHAR *)L"Timestamp";
    column.cx = 2 * width / 5;
    SendMessageW(list, LVM_INSERTCOLUMNW, 0, (LPARAM)&column);
    column.pszText = (WCHAR *)L"Description";
    column.cx = width - 2 * width / 5;
    column.iSubItem = 1;
    SendMessageW(list, LVM_INSERTCOLUMNW, 1, (LPARAM)&column);
    SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);
    enable(page, IDC_EVENTS_VIEW, FALSE);
    return TRUE;
}

static BOOL is_local(const WCHAR *machine)
{
    if (!machine || !machine[0])
        return TRUE;
    WCHAR self[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD chars = RTL_NUMBER_OF(self);
    while (*machine == L'\\')
        machine++;
    return GetComputerNameW(self, &chars) && !lstrcmpiW(machine, self);
}

/*
 * DevicePropertiesEx of devmgr.dll: the sheet of one device, modal to the parent, titled with the name of the device.
 * a device that is not there answers -1 and leaves no error. flags 1 asks for the page of the resources, which a
 * device of mime does not have, and the tree of the devices is not shown.
 */
int WINAPI DevicePropertiesExW(HWND parent, const WCHAR *machine, const WCHAR *id, DWORD flags, BOOL show_tree)
{
    if (!id || !id[0] || !is_local(machine))
        return -1;
    DEVICE d;
    memset(&d, 0, sizeof(d));
    d.Set = SetupDiCreateDeviceInfoList(nullptr, parent);
    d.Data.cbSize = sizeof(d.Data);
    if (d.Set == INVALID_HANDLE_VALUE || !SetupDiOpenDeviceInfoW(d.Set, id, parent, 0, &d.Data)) {
        if (d.Set != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList(d.Set);
        SetLastError(0);
        return -1;
    }
    d.Class = d.Data.ClassGuid;
    if (!property_text(&d, &DEVPKEY_NAME, FALSE, d.Name, RTL_NUMBER_OF(d.Name)) &&
        !property_text(&d, &DEVPKEY_Device_DeviceDesc, FALSE, d.Name, RTL_NUMBER_OF(d.Name)))
        wcscpy(d.Name, L"Unknown device");
    struct {
        const DM_ITEM *Items;
        DWORD Count;
        const WCHAR *Title;
        DLGPROC Proc;
    } const pages[] = {
        {g_general, RTL_NUMBER_OF(g_general), L"General", general_proc},
        {g_driver, RTL_NUMBER_OF(g_driver), L"Driver", driver_proc},
        {g_details, RTL_NUMBER_OF(g_details), L"Details", details_proc},
        {g_events, RTL_NUMBER_OF(g_events), L"Events", events_proc},
    };
    void *templates[RTL_NUMBER_OF(pages)] = {};
    HPROPSHEETPAGE handles[RTL_NUMBER_OF(pages)] = {};
    UINT made = 0;
    for (UINT i = 0; i < RTL_NUMBER_OF(pages); i++) {
        templates[i] = DmBuildTemplate(PAGE_STYLE, 252, 218, pages[i].Title, pages[i].Items, pages[i].Count);
        if (!templates[i])
            continue;
        PROPSHEETPAGEW page;
        memset(&page, 0, sizeof(page));
        page.dwSize = sizeof(page);
        page.dwFlags = PSP_DLGINDIRECT;
        page.hInstance = g_instance;
        page.pResource = templates[i];
        page.pfnDlgProc = pages[i].Proc;
        page.lParam = (LPARAM)&d;
        handles[made] = CreatePropertySheetPageW(&page);
        if (handles[made])
            made++;
    }
    INT_PTR result = -1;
    if (made) {
        PROPSHEETHEADERW sheet;
        memset(&sheet, 0, sizeof(sheet));
        sheet.dwSize = sizeof(sheet);
        sheet.dwFlags = PSH_PROPTITLE | PSH_NOAPPLYNOW;
        sheet.hwndParent = parent;
        sheet.hInstance = g_instance;
        sheet.pszCaption = d.Name;
        sheet.nPages = made;
        sheet.phpage = handles;
        result = PropertySheetW(&sheet);
    }
    for (UINT i = 0; i < RTL_NUMBER_OF(pages); i++)
        HeapFree(GetProcessHeap(), 0, templates[i]);
    HeapFree(GetProcessHeap(), 0, d.Keys);
    SetupDiDestroyDeviceInfoList(d.Set);
    return (int)result;
}

static WCHAR *wide(const char *text)
{
    if (!text)
        return nullptr;
    int chars = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    WCHAR *out = chars > 0 ? HeapAlloc(GetProcessHeap(), 0, chars * sizeof(WCHAR)) : nullptr;
    if (out)
        MultiByteToWideChar(CP_ACP, 0, text, -1, out, chars);
    return out;
}

int WINAPI DevicePropertiesExA(HWND parent, const char *machine, const char *id, DWORD flags, BOOL show_tree)
{
    WCHAR *wmachine = wide(machine), *wid = wide(id);
    int result = DevicePropertiesExW(parent, wmachine, wid, flags, show_tree);
    HeapFree(GetProcessHeap(), 0, wmachine);
    HeapFree(GetProcessHeap(), 0, wid);
    return result;
}

int WINAPI DevicePropertiesW(HWND parent, const WCHAR *machine, const WCHAR *id, BOOL show_tree)
{
    return DevicePropertiesExW(parent, machine, id, 1, show_tree);
}

int WINAPI DevicePropertiesA(HWND parent, const char *machine, const char *id, BOOL show_tree)
{
    return DevicePropertiesExA(parent, machine, id, 1, show_tree);
}

/* the value after an option of the command line, quoted or up to the next space. */
static BOOL option(const WCHAR *line, const WCHAR *name, WCHAR *out, DWORD chars)
{
    SIZE_T n = wcslen(name);
    for (const WCHAR *p = line; *p; p++) {
        if ((p != line && p[-1] != L' ') || _wcsnicmp(p, name, n) || (p[n] != L' ' && p[n] != L'\t'))
            continue;
        p += n;
        while (*p == L' ' || *p == L'\t')
            p++;
        BOOL quoted = *p == L'"';
        p += quoted;
        DWORD at = 0;
        while (*p && at + 1 < chars && (quoted ? *p != L'"' : *p != L' '))
            out[at++] = *p++;
        out[at] = 0;
        return at != 0;
    }
    return FALSE;
}

/* rundll32 devmgr.dll,DeviceProperties_RunDLL /DeviceID <id> [/MachineName <name>]. */
void WINAPI DeviceProperties_RunDLLW(HWND parent, HINSTANCE instance, const WCHAR *line, int show)
{
    WCHAR id[MAX_DEVICE_ID_LEN], machine[MAX_PATH];
    if (!line || !option(line, L"/DeviceID", id, RTL_NUMBER_OF(id)))
        return;
    DevicePropertiesExW(parent, option(line, L"/MachineName", machine, RTL_NUMBER_OF(machine)) ? machine : nullptr, id, 0,
                        FALSE);
}

void WINAPI DeviceProperties_RunDLLA(HWND parent, HINSTANCE instance, const char *line, int show)
{
    WCHAR *wline = wide(line);
    DeviceProperties_RunDLLW(parent, instance, wline, show);
    HeapFree(GetProcessHeap(), 0, wline);
}
