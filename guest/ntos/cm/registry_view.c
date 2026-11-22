#include "cm.h"
#include "shared_keys.h"

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

/* whether a path below SOFTWARE lies in a subtree the two views share. */
static BOOLEAN shared_software(PCWSTR path, ULONG length)
{
#define CM_WIDE_ENTRY(path) L"" path,
    static const PCWSTR shared[] = {CM_SHARED_SOFTWARE_KEYS(CM_WIDE_ENTRY)};
#undef CM_WIDE_ENTRY
    for (ULONG i = 0; i < RTL_NUMBER_OF(shared); i++)
        if (prefix(path, length, shared[i]))
            return TRUE;
    return FALSE;
}

static ULONG redirect_point(PCWSTR path, ULONG length)
{
    ULONG classes = classes_root(path, length);
    if (classes) {
#define CM_WIDE_CLASS(name) L"\\" name,
        static const PCWSTR redirected[] = {CM_REDIRECTED_CLASSES(CM_WIDE_CLASS)};
#undef CM_WIDE_CLASS
        for (ULONG i = 0; i < RTL_NUMBER_OF(redirected); i++)
            if (prefix(path + classes, length - classes, redirected[i]))
                return classes;
        return 0;
    }
    ULONG software = prefix(path, length, L"\\Machine\\Software");
    if (!software)
        return 0;
    /* modern windows shares these subtrees across the two software views. */
    return shared_software(path + software, length - software) ? 0 : software;
}

/* the caller holds the registry lock while node ancestry is read. */
NTSTATUS CmpMapRegistryView(CM_KEY_BODY *root, ULONG view, PWCHAR path, ULONG *length, ULONG capacity,
                            CM_NODE **start, BOOLEAN *physical, BOOLEAN *container)
{
    *start = root->Node;
    *physical = root->PhysicalView;
    *container = FALSE;
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
    BOOLEAN classes = point && prefix(full + point, chars - point, marker);
    if (!classes)
        point = prefix(full, chars, L"\\Machine\\Software");
    BOOLEAN named = point && prefix(full + point, chars - point, marker);
    /*
     * windows keeps a link below Wow6432Node for each subtree the views share, which leads to the one key both have,
     * however the node was reached: the wow64 of a 32-bit program writes the node into every path below SOFTWARE and
     * leaves the rest to the kernel. measured with reg32.c and nt32.c of the research repository. the classes have
     * a node of their own.
     */
    BOOLEAN link = FALSE;
    if (named && !classes) {
        PCWSTR below = full + point + marker_length;
        ULONG below_length = chars - point - marker_length;
        link = !prefix(below, below_length, L"\\Classes") && shared_software(below, below_length);
    }
    /* a path that names the node, or goes on from a key reached by such a path, is taken as written. */
    if (!link && (*physical || (named && point >= root_length))) {
        *physical = TRUE;
        point = root_length;
        goto copy_path;
    }
    *physical = FALSE;
    if (named) {
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
