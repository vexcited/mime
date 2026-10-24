#include "cm.h"

static BOOLEAN equal(PCWSTR left, PCWSTR right, ULONG count)
{
    for (ULONG i = 0; i < count; i++)
        if (towupper(left[i]) != towupper(right[i]))
            return FALSE;
    return TRUE;
}

static ULONG prefix(PCWSTR path, ULONG length, PCWSTR name)
{
    ULONG count = (ULONG)wcslen(name);
    return length >= count && equal(path, name, count) && (length == count || path[count] == L'\\') ? count : 0;
}

static ULONG classes_root(PCWSTR path, ULONG length)
{
    ULONG end = prefix(path, length, L"\\Machine\\Software\\Classes");
    if (end)
        return end;
    if (length < 6 || !equal(path, L"\\User\\", 6))
        return 0;
    end = 6;
    while (end < length && path[end] != L'\\')
        end++;
    if (end >= 14 && equal(path + end - 8, L"_Classes", 8))
        return end;
    ULONG suffix = prefix(path + end, length - end, L"\\Software\\Classes");
    return suffix ? end + suffix : 0;
}

static ULONG redirect_point(PCWSTR path, ULONG length)
{
    ULONG classes = classes_root(path, length);
    if (classes) {
        static const PCWSTR redirected[] = {L"\\CLSID", L"\\DirectShow", L"\\Interface", L"\\Media Type",
                                             L"\\MediaFoundation"};
        for (ULONG i = 0; i < RTL_NUMBER_OF(redirected); i++)
            if (prefix(path + classes, length - classes, redirected[i]))
                return classes;
        return 0;
    }
    ULONG software = prefix(path, length, L"\\Machine\\Software");
    if (!software)
        return 0;
    /* modern windows shares these subtrees across the two software views. */
    static const PCWSTR shared[] = {
        L"\\Classes", L"\\Clients", L"\\Policies", L"\\RegisteredApplications",
        L"\\Microsoft\\COM3", L"\\Microsoft\\Cryptography\\Calais\\Current",
        L"\\Microsoft\\Cryptography\\Calais\\Readers", L"\\Microsoft\\Cryptography\\Services",
        L"\\Microsoft\\CTF\\SystemShared", L"\\Microsoft\\CTF\\TIP", L"\\Microsoft\\DFS",
        L"\\Microsoft\\Driver Signing", L"\\Microsoft\\EnterpriseCertificates", L"\\Microsoft\\EventSystem",
        L"\\Microsoft\\MSMQ", L"\\Microsoft\\Non-Driver Signing", L"\\Microsoft\\Notepad\\DefaultFonts",
        L"\\Microsoft\\OLE", L"\\Microsoft\\RAS", L"\\Microsoft\\RPC",
        L"\\Microsoft\\SOFTWARE\\Microsoft\\Shared Tools\\MSInfo", L"\\Microsoft\\SystemCertificates",
        L"\\Microsoft\\TermServLicensing", L"\\Microsoft\\TransactionServer",
        L"\\Microsoft\\Windows\\CurrentVersion\\App Paths",
        L"\\Microsoft\\Windows\\CurrentVersion\\Control Panel\\Cursors\\Schemes",
        L"\\Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers",
        L"\\Microsoft\\Windows\\CurrentVersion\\Explorer\\DriveIcons",
        L"\\Microsoft\\Windows\\CurrentVersion\\Explorer\\KindMap",
        L"\\Microsoft\\Windows\\CurrentVersion\\Group Policy",
        L"\\Microsoft\\Windows\\CurrentVersion\\Policies",
        L"\\Microsoft\\Windows\\CurrentVersion\\PreviewHandlers", L"\\Microsoft\\Windows\\CurrentVersion\\Setup",
        L"\\Microsoft\\Windows\\CurrentVersion\\Telephony\\Locations",
        L"\\Microsoft\\Windows NT\\CurrentVersion\\Console", L"\\Microsoft\\Windows NT\\CurrentVersion\\FontDpi",
        L"\\Microsoft\\Windows NT\\CurrentVersion\\FontLink", L"\\Microsoft\\Windows NT\\CurrentVersion\\FontMapper",
        L"\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", L"\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",
        L"\\Microsoft\\Windows NT\\CurrentVersion\\Gre_Initialize",
        L"\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options",
        L"\\Microsoft\\Windows NT\\CurrentVersion\\Language Pack",
        L"\\Microsoft\\Windows NT\\CurrentVersion\\NetworkCards", L"\\Microsoft\\Windows NT\\CurrentVersion\\Perflib",
        L"\\Microsoft\\Windows NT\\CurrentVersion\\Ports", L"\\Microsoft\\Windows NT\\CurrentVersion\\Print",
        L"\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList", L"\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones",
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(shared); i++)
        if (prefix(path + software, length - software, shared[i]))
            return 0;
    return software;
}

/* the caller holds the registry lock while node ancestry is read. */
NTSTATUS CmpMapRegistryView(CM_KEY_BODY *root, ULONG view, PWCHAR path, ULONG *length, ULONG capacity,
                            CM_NODE **start, BOOLEAN *physical, BOOLEAN *container)
{
    *start = root->Node;
    *physical = root->PhysicalView;
    *container = FALSE;
    if (*physical)
        return STATUS_SUCCESS;
    ULONG root_length = 0;
    for (CM_NODE *node = root->Node; node != CmpRoot; node = node->Parent) {
        if (!node || node->NameLength + 1u >= capacity - root_length)
            return STATUS_NAME_TOO_LONG;
        root_length += node->NameLength + 1u;
    }
    ULONG separator = *length && path[0] != L'\\' ? 1 : 0;
    if (root_length + separator >= capacity || *length >= capacity - root_length - separator)
        return STATUS_NAME_TOO_LONG;
    PWCHAR full = ExAllocatePool2(POOL_FLAG_NON_PAGED, capacity * sizeof(WCHAR), KI_TAG('C', 'm', 'V', 'w'));
    if (!full)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG cursor = root_length;
    for (CM_NODE *node = root->Node; node != CmpRoot; node = node->Parent) {
        cursor -= node->NameLength;
        memcpy(full + cursor, node->Name, node->NameLength * sizeof(WCHAR));
        full[--cursor] = L'\\';
    }
    if (separator)
        full[root_length] = L'\\';
    memcpy(full + root_length + separator, path, *length * sizeof(WCHAR));
    ULONG chars = root_length + separator + *length;
    ULONG compact = root_length;
    for (ULONG i = root_length; i < chars; i++) {
        if (full[i] == L'\\' && compact && full[compact - 1] == L'\\')
            continue;
        full[compact++] = full[i];
    }
    chars = compact;
    memcpy(path, full, root_length * sizeof(WCHAR));

    static const WCHAR marker[] = L"\\Wow6432Node";
    const ULONG marker_length = RTL_NUMBER_OF(marker) - 1;
    ULONG point = classes_root(full, chars);
    if (!point || !prefix(full + point, chars - point, marker))
        point = prefix(full, chars, L"\\Machine\\Software");
    if (point && prefix(full + point, chars - point, marker)) {
        if (point >= root_length) {
            *physical = TRUE;
            goto copy_path;
        }
        memmove(full + point, full + point + marker_length, (chars - point - marker_length) * sizeof(WCHAR));
        chars -= marker_length;
    }
    point = view == 32 ? redirect_point(full, chars) : 0;
    if (point) {
        if (chars + marker_length >= capacity) {
            ExFreePool(full);
            return STATUS_NAME_TOO_LONG;
        }
        memmove(full + point + marker_length, full + point, (chars - point) * sizeof(WCHAR));
        memcpy(full + point, marker, marker_length * sizeof(WCHAR));
        chars += marker_length;
        *container = TRUE;
    }
copy_path:
    if (chars >= root_length && equal(full, path, root_length) &&
        (chars == root_length || !root_length || full[root_length] == L'\\')) {
        *length = chars - root_length;
        memcpy(path, full + root_length, *length * sizeof(WCHAR));
        if (point < root_length)
            *container = FALSE;
    } else {
        *start = CmpRoot;
        *length = chars;
        memcpy(path, full, chars * sizeof(WCHAR));
    }
    ExFreePool(full);
    return STATUS_SUCCESS;
}
