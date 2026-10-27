/*
 * the window of regedit: the registry as a tree of keys and a list of the values of the selected key.
 *
 * keys and values can be made, changed, renamed and deleted, scripts imported and keys exported, as the original does;
 * the finder, favourites, printing, hive loading, remote registries and the security editor are not here.
 */
#include "regedit.h"
#include "../lib/regfile/regsys.h"
#include <mime/win32/commctrl.h>
#include <mime/win32/commdlg.h>
#include <rtl.h>

#define CLASS_NAME L"RegEdit_RegEdit"
#define VALUE_CLASS L"RegEdit_ValueDialog"

/* the command that reads everything again; the other instances are told with it. */
#define IDM_REFRESH 0x288
enum {
    IDM_IMPORT = 100,
    IDM_EXPORT,
    IDM_EXIT,
    IDM_NEW_KEY,
    IDM_NEW_STRING,
    IDM_NEW_BINARY,
    IDM_NEW_DWORD,
    IDM_NEW_QWORD,
    IDM_NEW_MULTI,
    IDM_NEW_EXPAND,
    IDM_MODIFY,
    IDM_DELETE,
    IDM_RENAME,
    IDM_COPY_NAME,
    IDM_ABOUT,
};

#define ID_TREE 1
#define ID_LIST 2
#define ID_STATUS 3
#define ID_ADDRESS 4

/* a key of the tree: its root and the path below it. */
typedef struct NODE {
    int Root;
    WCHAR Sub[1];
} NODE;

static HWND g_window, g_tree, g_list, g_status, g_address;
static HFONT g_font;
static int g_dpi = 96;
static int g_split;
static BOOL g_dragging;
static BOOL g_list_active; /* the list was used last, so delete and rename mean values. */
static HMENU g_edit_menu;

static int scale(int value)
{
    return MulDiv(value, g_dpi, 96);
}

static NODE *make_node(int root, PCWSTR sub)
{
    SIZE_T length = wcslen(sub);
    NODE *node = HeapAlloc(GetProcessHeap(), 0, sizeof(NODE) + length * sizeof(WCHAR));
    if (!node)
        return nullptr;
    node->Root = root;
    memcpy(node->Sub, sub, (length + 1) * sizeof(WCHAR));
    return node;
}

static NODE *node_of(HTREEITEM item)
{
    TVITEMW tv = {};
    tv.mask = TVIF_PARAM;
    tv.hItem = item;
    if (!SendMessageW(g_tree, TVM_GETITEMW, 0, (LPARAM)&tv))
        return nullptr;
    return (NODE *)tv.lParam;
}

static HTREEITEM selected_item(void)
{
    return (HTREEITEM)SendMessageW(g_tree, TVM_GETNEXTITEM, TVGN_CARET, 0);
}

/* the full name of a node, with its root. */
static void full_name(const NODE *node, WCHAR *out, SIZE_T capacity)
{
    _snwprintf(out, capacity - 1, node->Sub[0] ? L"%s\\%s" : L"%s", MimeRegSysRootName(node->Root), node->Sub);
    out[capacity - 1] = 0;
}

static HKEY open_node(const NODE *node, REGSAM access)
{
    HKEY key;
    if (RegOpenKeyExW(MimeRegSysRootKey(node->Root), node->Sub, 0, access, &key) != ERROR_SUCCESS)
        return nullptr;
    return key;
}

/* ------------------------------------------------------------------ the tree */

static HTREEITEM insert_item(HTREEITEM parent, PCWSTR text, NODE *node)
{
    TVINSERTSTRUCTW insert = {};
    insert.hParent = parent;
    insert.hInsertAfter = parent == TVI_ROOT ? TVI_LAST : TVI_SORT;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    insert.item.pszText = (PWSTR)text;
    insert.item.cChildren = 1;
    insert.item.lParam = (LPARAM)node;
    return (HTREEITEM)SendMessageW(g_tree, TVM_INSERTITEMW, 0, (LPARAM)&insert);
}

/* the subkeys of a node are read the first time it opens. */
static void fill_children(HTREEITEM item)
{
    NODE *node = node_of(item);
    if (!node)
        return;
    HKEY key = open_node(node, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE);
    int added = 0;
    if (key) {
        for (DWORD index = 0;; index++) {
            WCHAR name[256];
            DWORD length = RTL_NUMBER_OF(name);
            if (RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            WCHAR sub[1100];
            _snwprintf(sub, RTL_NUMBER_OF(sub) - 1, node->Sub[0] ? L"%s\\%s" : L"%s%s", node->Sub, name);
            sub[RTL_NUMBER_OF(sub) - 1] = 0;
            NODE *child = make_node(node->Root, sub);
            if (child && insert_item(item, name, child))
                added++;
        }
        RegCloseKey(key);
    }
    if (!added) {
        TVITEMW tv = {};
        tv.mask = TVIF_CHILDREN;
        tv.hItem = item;
        SendMessageW(g_tree, TVM_SETITEMW, 0, (LPARAM)&tv);
    }
}

static void populate_roots(void)
{
    SendMessageW(g_tree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    for (int root = 0; root < MIME_REG_ROOTS; root++)
        insert_item(TVI_ROOT, MimeRegSysRootName(root), make_node(root, L""));
}

/* the item of a path below a root, opened on the way; the last one found. */
static HTREEITEM navigate(PCWSTR path)
{
    PCWSTR slash = wcschr(path, L'\\');
    int root = MimeRegSysFindRoot(path, slash ? (SIZE_T)(slash - path) : wcslen(path));
    if (root < 0)
        return nullptr;
    HTREEITEM item = (HTREEITEM)SendMessageW(g_tree, TVM_GETNEXTITEM, TVGN_ROOT, 0);
    while (item) {
        NODE *node = node_of(item);
        if (node && node->Root == root)
            break;
        item = (HTREEITEM)SendMessageW(g_tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)item);
    }
    PCWSTR rest = slash ? slash + 1 : L"";
    while (item && *rest) {
        SendMessageW(g_tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)item);
        PCWSTR end = wcschr(rest, L'\\');
        SIZE_T length = end ? (SIZE_T)(end - rest) : wcslen(rest);
        HTREEITEM child = (HTREEITEM)SendMessageW(g_tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)item);
        HTREEITEM found = nullptr;
        while (child) {
            WCHAR text[256];
            TVITEMW tv = {};
            tv.mask = TVIF_TEXT;
            tv.hItem = child;
            tv.pszText = text;
            tv.cchTextMax = RTL_NUMBER_OF(text);
            SendMessageW(g_tree, TVM_GETITEMW, 0, (LPARAM)&tv);
            if (wcslen(text) == length && _wcsnicmp(text, rest, length) == 0) {
                found = child;
                break;
            }
            child = (HTREEITEM)SendMessageW(g_tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)child);
        }
        item = found;
        rest = end ? end + 1 : rest + length;
    }
    if (item) {
        SendMessageW(g_tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)item);
        SendMessageW(g_tree, TVM_ENSUREVISIBLE, 0, (LPARAM)item);
    }
    return item;
}

/* ------------------------------------------------------------------ the list */

static void value_text(DWORD type, const BYTE *data, DWORD size, WCHAR *out, SIZE_T capacity)
{
    out[0] = 0;
    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        _snwprintf(out, capacity - 1, L"%s", (const WCHAR *)data);
    } else if (type == REG_DWORD && size == 4) {
        DWORD value;
        memcpy(&value, data, 4);
        _snwprintf(out, capacity - 1, L"0x%08x (%u)", value, value);
    } else if (type == REG_QWORD && size == 8) {
        ULONGLONG value;
        memcpy(&value, data, 8);
        _snwprintf(out, capacity - 1, L"0x%016I64x (%I64u)", value, value);
    } else if (type == REG_MULTI_SZ) {
        SIZE_T used = 0;
        for (const WCHAR *p = (const WCHAR *)data; *p && (const BYTE *)p < data + size; p += wcslen(p) + 1) {
            used += (SIZE_T)_snwprintf(out + used, capacity - 1 - used, used ? L" %s" : L"%s", p);
            if (used >= capacity - 1)
                break;
        }
    } else if (!size) {
        wcscpy(out, L"(zero-length binary value)");
    } else {
        SIZE_T used = 0;
        for (DWORD i = 0; i < size && used + 4 < capacity; i++)
            used += (SIZE_T)_snwprintf(out + used, capacity - 1 - used, L"%02x ", data[i]);
        if (used)
            out[used - 1] = 0;
    }
    out[capacity - 1] = 0;
}

static PCWSTR type_text(DWORD type)
{
    static const WCHAR *const names[] = {L"REG_NONE", L"REG_SZ", L"REG_EXPAND_SZ", L"REG_BINARY", L"REG_DWORD",
                                         L"REG_DWORD_BIG_ENDIAN", L"REG_LINK", L"REG_MULTI_SZ", L"REG_RESOURCE_LIST",
                                         L"REG_FULL_RESOURCE_DESCRIPTOR", L"REG_RESOURCE_REQUIREMENTS_LIST", L"REG_QWORD"};
    return type < RTL_NUMBER_OF(names) ? names[type] : L"REG_UNKNOWN";
}

static void set_cell(int row, int column, PCWSTR text)
{
    LVITEMW item = {};
    item.iSubItem = column;
    item.pszText = (PWSTR)text;
    SendMessageW(g_list, LVM_SETITEMTEXTW, (WPARAM)row, (LPARAM)&item);
}

static void add_row(int index, PCWSTR name, DWORD type, PCWSTR data)
{
    LVITEMW item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = index;
    item.pszText = (PWSTR)(*name ? name : L"(Default)");
    item.lParam = (LPARAM)type;
    int row = ListView_InsertItem(g_list, &item);
    set_cell(row, 1, type_text(type));
    set_cell(row, 2, data);
}

static void fill_list(void)
{
    ListView_DeleteAllItems(g_list);
    HTREEITEM item = selected_item();
    NODE *node = item ? node_of(item) : nullptr;
    if (!node)
        return;
    HKEY key = open_node(node, KEY_QUERY_VALUE);
    if (!key) {
        add_row(0, L"", REG_SZ, L"(value not set)");
        return;
    }
    DWORD values = 0, longest_name = 0, longest_data = 0;
    RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values, &longest_name, &longest_data,
                     nullptr, nullptr);
    WCHAR *name = HeapAlloc(GetProcessHeap(), 0, (longest_name + 2) * sizeof(WCHAR));
    BYTE *data = HeapAlloc(GetProcessHeap(), 0, longest_data + 8);
    BOOL have_default = FALSE;
    int row = 1;
    for (DWORD i = 0; name && data && i < values; i++) {
        DWORD name_size = longest_name + 1, data_size = longest_data, type;
        if (RegEnumValueW(key, i, name, &name_size, nullptr, &type, data, &data_size) != ERROR_SUCCESS)
            continue;
        data[data_size] = data[data_size + 1] = 0;
        WCHAR text[1024];
        value_text(type, data, data_size, text, RTL_NUMBER_OF(text));
        if (!*name) {
            have_default = TRUE;
            add_row(0, name, type, text);
        } else {
            add_row(row++, name, type, text);
        }
    }
    if (!have_default)
        add_row(0, L"", REG_SZ, L"(value not set)");
    if (name)
        HeapFree(GetProcessHeap(), 0, name);
    if (data)
        HeapFree(GetProcessHeap(), 0, data);
    RegCloseKey(key);
}

static int selected_row(void)
{
    return ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
}

/* the name of a list row; the default value is the empty name. */
static BOOL row_name(int row, WCHAR *name, int capacity)
{
    LVITEMW item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = row;
    item.pszText = name;
    item.cchTextMax = capacity;
    if (!ListView_GetItem(g_list, &item))
        return FALSE;
    if (!wcscmp(name, L"(Default)"))
        name[0] = 0;
    return TRUE;
}

/* ------------------------------------------------------------------ the dialog of a value */

typedef struct VALUE_DIALOG {
    HWND Owner;
    PCWSTR Title;
    DWORD Type;
    BOOL NameOnly; /* a new key asks for a name and nothing else. */
    BOOL NameFixed;
    WCHAR Name[256];
    BYTE *Data;
    DWORD Size;
    BOOL Done, Accepted;
    BOOL Decimal;
    HWND NameEdit, DataEdit, Hex, Dec, Ok;
} VALUE_DIALOG;

#define ID_NAME 20
#define ID_DATA 21
#define ID_HEX 22
#define ID_DEC 23

static HWND add_child(HWND parent, PCWSTR class_name, PCWSTR text, DWORD style, int id, int x, int y, int w, int h)
{
    HWND child = CreateWindowExW(class_name[0] == L'E' ? WS_EX_CLIENTEDGE : 0, class_name, text,
                                 WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(w), scale(h), parent,
                                 (HMENU)(UINT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    if (child && g_font)
        SendMessageW(child, WM_SETFONT, (WPARAM)g_font, TRUE);
    return child;
}

static PWSTR data_to_text(const VALUE_DIALOG *d)
{
    SIZE_T capacity = d->Size * 4 + 64;
    PWSTR text = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, capacity * sizeof(WCHAR));
    if (!text)
        return nullptr;
    if (d->Type == REG_SZ || d->Type == REG_EXPAND_SZ) {
        if (d->Data)
            _snwprintf(text, capacity - 1, L"%s", (const WCHAR *)d->Data);
    } else if (d->Type == REG_MULTI_SZ) {
        SIZE_T used = 0;
        if (d->Data)
            for (const WCHAR *p = (const WCHAR *)d->Data; *p && used + 4 < capacity; p += wcslen(p) + 1)
                used += (SIZE_T)_snwprintf(text + used, capacity - 1 - used, L"%s\r\n", p);
    } else if (d->Type == REG_DWORD || d->Type == REG_QWORD) {
        ULONGLONG value = 0;
        if (d->Data)
            memcpy(&value, d->Data, d->Size < 8 ? d->Size : 8);
        if (d->Decimal)
            _snwprintf(text, capacity - 1, L"%I64u", value);
        else
            _snwprintf(text, capacity - 1, d->Type == REG_DWORD ? L"%x" : L"%I64x", d->Type == REG_DWORD ? (ULONGLONG)(DWORD)value : value);
    } else {
        SIZE_T used = 0;
        for (DWORD i = 0; d->Data && i < d->Size; i++)
            used += (SIZE_T)_snwprintf(text + used, capacity - 1 - used, L"%02x ", d->Data[i]);
    }
    return text;
}

/* the text of the edit box made into the bytes of the value; the message of the error, or NULL. */
static PCWSTR text_to_data(VALUE_DIALOG *d, PCWSTR text)
{
    HANDLE heap = GetProcessHeap();
    SIZE_T length = wcslen(text);
    BYTE *data = nullptr;
    DWORD size = 0;
    if (d->Type == REG_SZ || d->Type == REG_EXPAND_SZ) {
        data = HeapAlloc(heap, 0, (length + 1) * sizeof(WCHAR));
        if (!data)
            return L"There is not enough memory.";
        memcpy(data, text, (length + 1) * sizeof(WCHAR));
        size = (DWORD)((length + 1) * sizeof(WCHAR));
    } else if (d->Type == REG_MULTI_SZ) {
        WCHAR *out = HeapAlloc(heap, HEAP_ZERO_MEMORY, (length + 3) * sizeof(WCHAR));
        if (!out)
            return L"There is not enough memory.";
        WCHAR *p = out;
        const WCHAR *line = text;
        while (*line) {
            const WCHAR *end = line;
            while (*end && *end != L'\r' && *end != L'\n')
                end++;
            if (end > line) {
                memcpy(p, line, (SIZE_T)(end - line) * sizeof(WCHAR));
                p += end - line;
                *p++ = 0;
            }
            line = end;
            while (*line == L'\r' || *line == L'\n')
                line++;
        }
        if (p == out)
            *p++ = 0;
        *p++ = 0;
        data = (BYTE *)out;
        size = (DWORD)((p - out) * sizeof(WCHAR));
    } else if (d->Type == REG_DWORD || d->Type == REG_QWORD) {
        ULONGLONG value = 0;
        int digits = 0;
        const WCHAR *p = text;
        while (*p == L' ')
            p++;
        if (!d->Decimal && p[0] == L'0' && (p[1] == L'x' || p[1] == L'X'))
            p += 2;
        for (; *p && *p != L' '; p++, digits++) {
            int digit;
            if (*p >= L'0' && *p <= L'9')
                digit = *p - L'0';
            else if (!d->Decimal && *p >= L'a' && *p <= L'f')
                digit = *p - L'a' + 10;
            else if (!d->Decimal && *p >= L'A' && *p <= L'F')
                digit = *p - L'A' + 10;
            else
                return L"The value is not a number.";
            ULONGLONG base = d->Decimal ? 10 : 16;
            if (value > (0xFFFFFFFFFFFFFFFFull - (ULONGLONG)digit) / base)
                return L"The number is too large.";
            value = value * base + (ULONGLONG)digit;
        }
        if (!digits)
            return L"The value is not a number.";
        if (d->Type == REG_DWORD && value > 0xFFFFFFFF)
            return L"The number is larger than a 32 bit value.";
        size = d->Type == REG_DWORD ? 4 : 8;
        data = HeapAlloc(heap, 0, size);
        if (!data)
            return L"There is not enough memory.";
        memcpy(data, &value, size);
    } else {
        data = HeapAlloc(heap, 0, length / 2 + 2);
        if (!data)
            return L"There is not enough memory.";
        int pending = -1;
        for (const WCHAR *p = text; *p; p++) {
            int digit;
            if (*p == L' ' || *p == L',' || *p == L'\r' || *p == L'\n')
                continue;
            if (*p >= L'0' && *p <= L'9')
                digit = *p - L'0';
            else if (*p >= L'a' && *p <= L'f')
                digit = *p - L'a' + 10;
            else if (*p >= L'A' && *p <= L'F')
                digit = *p - L'A' + 10;
            else {
                HeapFree(heap, 0, data);
                return L"Binary data is written as pairs of hexadecimal digits.";
            }
            if (pending < 0) {
                pending = digit;
            } else {
                data[size++] = (BYTE)(pending * 16 + digit);
                pending = -1;
            }
        }
        if (pending >= 0) {
            HeapFree(heap, 0, data);
            return L"Binary data is written as pairs of hexadecimal digits.";
        }
    }
    if (d->Data)
        HeapFree(heap, 0, d->Data);
    d->Data = data;
    d->Size = size;
    return nullptr;
}

static void dialog_accept(HWND window, VALUE_DIALOG *d)
{
    GetWindowTextW(d->NameEdit, d->Name, RTL_NUMBER_OF(d->Name));
    if (!d->NameOnly) {
        int length = GetWindowTextLengthW(d->DataEdit);
        PWSTR text = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)length + 2) * sizeof(WCHAR));
        if (!text)
            return;
        GetWindowTextW(d->DataEdit, text, length + 1);
        PCWSTR problem = text_to_data(d, text);
        HeapFree(GetProcessHeap(), 0, text);
        if (problem) {
            RegeditBox(window, problem, MB_ICONERROR);
            return;
        }
    }
    d->Accepted = TRUE;
    d->Done = TRUE;
}

static LRESULT CALLBACK value_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    VALUE_DIALOG *d = (VALUE_DIALOG *)GetWindowLongPtrW(window, GWLP_USERDATA);
    switch (message) {
    case WM_CREATE: {
        d = (VALUE_DIALOG *)((CREATESTRUCTW *)lparam)->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)d);
        BOOL numeric = d->Type == REG_DWORD || d->Type == REG_QWORD;
        BOOL multi = d->Type == REG_MULTI_SZ || (!numeric && d->Type != REG_SZ && d->Type != REG_EXPAND_SZ);
        add_child(window, L"STATIC", d->NameOnly ? L"Key name:" : L"Value name:", 0, 0, 12, 10, 300, 16);
        d->NameEdit = add_child(window, L"EDIT", d->Name, ES_AUTOHSCROLL | WS_TABSTOP | (d->NameFixed ? ES_READONLY : 0),
                                ID_NAME, 12, 28, 336, 22);
        int y = 58;
        if (!d->NameOnly) {
            add_child(window, L"STATIC", L"Value data:", 0, 0, 12, y, 300, 16);
            PWSTR text = data_to_text(d);
            d->DataEdit = add_child(window, L"EDIT", text ? text : L"",
                                    WS_TABSTOP | (multi ? WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN : ES_AUTOHSCROLL),
                                    ID_DATA, 12, y + 18, 336, multi ? 110 : 22);
            if (text)
                HeapFree(GetProcessHeap(), 0, text);
            y += multi ? 136 : 48;
            if (numeric) {
                d->Hex = add_child(window, L"BUTTON", L"Hexadecimal", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, ID_HEX, 12, y, 130, 20);
                d->Dec = add_child(window, L"BUTTON", L"Decimal", BS_AUTORADIOBUTTON, ID_DEC, 150, y, 130, 20);
                SendMessageW(d->Hex, BM_SETCHECK, BST_CHECKED, 0);
                y += 28;
            }
        }
        d->Ok = add_child(window, L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 188, y + 6, 76, 24);
        add_child(window, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, 272, y + 6, 76, 24);
        RECT want = {0, 0, scale(360), scale(y + 42)};
        AdjustWindowRect(&want, WS_CAPTION | WS_SYSMENU, FALSE);
        SetWindowPos(window, nullptr, 0, 0, want.right - want.left, want.bottom - want.top, SWP_NOMOVE | SWP_NOZORDER);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDOK:
            dialog_accept(window, d);
            return 0;
        case IDCANCEL:
            d->Done = TRUE;
            return 0;
        case ID_HEX:
        case ID_DEC:
            if (HIWORD(wparam) == BN_CLICKED && d->DataEdit) {
                BOOL decimal = LOWORD(wparam) == ID_DEC;
                if (decimal != d->Decimal) {
                    /* the text in the box is read in the base it was shown in, and shown in the other. */
                    int length = GetWindowTextLengthW(d->DataEdit);
                    PWSTR text = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)length + 2) * sizeof(WCHAR));
                    if (text) {
                        GetWindowTextW(d->DataEdit, text, length + 1);
                        BYTE *keep = d->Data;
                        DWORD keep_size = d->Size;
                        d->Data = nullptr;
                        if (!text_to_data(d, text)) {
                            d->Decimal = decimal;
                            PWSTR shown = data_to_text(d);
                            if (shown) {
                                SetWindowTextW(d->DataEdit, shown);
                                HeapFree(GetProcessHeap(), 0, shown);
                            }
                        } else {
                            SendMessageW(decimal ? d->Hex : d->Dec, BM_SETCHECK, BST_CHECKED, 0);
                            SendMessageW(decimal ? d->Dec : d->Hex, BM_SETCHECK, BST_UNCHECKED, 0);
                        }
                        if (d->Data)
                            HeapFree(GetProcessHeap(), 0, d->Data);
                        d->Data = keep;
                        d->Size = keep_size;
                        HeapFree(GetProcessHeap(), 0, text);
                    }
                }
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        d->Done = TRUE;
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

/* shows the dialog and runs until it is closed; true when the value was accepted. */
static BOOL run_value_dialog(VALUE_DIALOG *d)
{
    static BOOL registered;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSW class = {};
        class.lpfnWndProc = value_proc;
        class.hInstance = instance;
        class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        class.lpszClassName = VALUE_CLASS;
        RegisterClassW(&class);
        registered = TRUE;
    }
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, VALUE_CLASS, d->Title, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  CW_USEDEFAULT, CW_USEDEFAULT, scale(380), scale(240), d->Owner, nullptr, instance, d);
    if (!window)
        return FALSE;
    RECT owner_rect, own;
    GetWindowRect(d->Owner, &owner_rect);
    GetWindowRect(window, &own);
    SetWindowPos(window, nullptr, owner_rect.left + ((owner_rect.right - owner_rect.left) - (own.right - own.left)) / 2,
                 owner_rect.top + ((owner_rect.bottom - owner_rect.top) - (own.bottom - own.top)) / 3, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
    EnableWindow(d->Owner, FALSE);
    ShowWindow(window, SW_SHOW);
    SetFocus(d->NameFixed && d->DataEdit ? d->DataEdit : d->NameEdit);
    MSG msg;
    while (!d->Done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            d->Done = TRUE;
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && msg.hwnd == d->NameEdit) {
            dialog_accept(window, d);
            continue;
        }
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(d->Owner, TRUE);
    DestroyWindow(window);
    SetForegroundWindow(d->Owner);
    return d->Accepted;
}

/* ------------------------------------------------------------------ commands */

static void set_status(void)
{
    HTREEITEM item = selected_item();
    NODE *node = item ? node_of(item) : nullptr;
    WCHAR path[1200];
    if (node)
        full_name(node, path, RTL_NUMBER_OF(path));
    else
        path[0] = 0;
    SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)path);
    SetWindowTextW(g_address, path);
}

static void refresh(void)
{
    HTREEITEM item = selected_item();
    NODE *node = item ? node_of(item) : nullptr;
    WCHAR path[1200];
    path[0] = 0;
    if (node)
        full_name(node, path, RTL_NUMBER_OF(path));
    SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
    populate_roots();
    if (path[0])
        navigate(path);
    SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_tree, nullptr, TRUE);
    fill_list();
    set_status();
}

static void lay_out(void)
{
    RECT client;
    GetClientRect(g_window, &client);
    int address = scale(26), status_height = 0;
    RECT status_rect;
    SendMessageW(g_status, WM_SIZE, 0, 0);
    if (GetWindowRect(g_status, &status_rect))
        status_height = status_rect.bottom - status_rect.top;
    int body = client.bottom - address - status_height;
    if (body < 0)
        body = 0;
    int bar = scale(5);
    if (g_split == 0)
        g_split = client.right * 35 / 100;
    if (g_split < scale(80))
        g_split = scale(80);
    if (g_split > client.right - scale(80))
        g_split = client.right - scale(80);
    MoveWindow(g_address, 0, 0, client.right, address, TRUE);
    MoveWindow(g_tree, 0, address, g_split, body, TRUE);
    MoveWindow(g_list, g_split + bar, address, client.right - g_split - bar, body, TRUE);
}

static HTREEITEM tree_hit(void)
{
    return selected_item();
}

static void after_change(PCWSTR path)
{
    refresh();
    if (path && *path)
        navigate(path);
    fill_list();
    set_status();
}

static void new_key(void)
{
    HTREEITEM item = tree_hit();
    NODE *node = item ? node_of(item) : nullptr;
    if (!node)
        return;
    VALUE_DIALOG d = {};
    d.Owner = g_window;
    d.Title = L"New Key";
    d.NameOnly = TRUE;
    d.Type = REG_SZ;
    HKEY parent = open_node(node, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE);
    for (int n = 1; n < 1000 && parent; n++) {
        _snwprintf(d.Name, RTL_NUMBER_OF(d.Name) - 1, L"New Key #%d", n);
        HKEY probe;
        if (RegOpenKeyExW(parent, d.Name, 0, KEY_READ, &probe) != ERROR_SUCCESS)
            break;
        RegCloseKey(probe);
    }
    if (parent)
        RegCloseKey(parent);
    if (!run_value_dialog(&d) || !d.Name[0])
        return;
    if (wcschr(d.Name, L'\\')) {
        RegeditBox(g_window, L"A key name cannot contain a backslash.", MB_ICONERROR);
        return;
    }
    HKEY key;
    WCHAR sub[1200];
    _snwprintf(sub, RTL_NUMBER_OF(sub) - 1, node->Sub[0] ? L"%s\\%s" : L"%s%s", node->Sub, d.Name);
    sub[RTL_NUMBER_OF(sub) - 1] = 0;
    LSTATUS status = RegCreateKeyExW(MimeRegSysRootKey(node->Root), sub, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key,
                                     nullptr);
    if (status != ERROR_SUCCESS) {
        RegeditBox(g_window, L"The key could not be created.", MB_ICONERROR);
        return;
    }
    RegCloseKey(key);
    WCHAR path[1300];
    full_name(node, path, RTL_NUMBER_OF(path));
    _snwprintf(path + wcslen(path), RTL_NUMBER_OF(path) - wcslen(path) - 1, L"\\%s", d.Name);
    after_change(path);
}

static void new_value(DWORD type)
{
    HTREEITEM item = tree_hit();
    NODE *node = item ? node_of(item) : nullptr;
    if (!node)
        return;
    HKEY key = open_node(node, KEY_READ | KEY_WRITE);
    if (!key) {
        RegeditBox(g_window, L"The value could not be created in this key.", MB_ICONERROR);
        return;
    }
    VALUE_DIALOG d = {};
    d.Owner = g_window;
    d.Title = L"New Value";
    d.Type = type;
    for (int n = 1; n < 1000; n++) {
        _snwprintf(d.Name, RTL_NUMBER_OF(d.Name) - 1, L"New Value #%d", n);
        if (RegQueryValueExW(key, d.Name, nullptr, nullptr, nullptr, nullptr) == ERROR_FILE_NOT_FOUND)
            break;
    }
    if (run_value_dialog(&d)) {
        if (RegQueryValueExW(key, d.Name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            RegeditBox(g_window, L"A value with that name already exists.", MB_ICONERROR);
        } else if (RegSetValueExW(key, d.Name, 0, type, d.Data, d.Size) != ERROR_SUCCESS) {
            RegeditBox(g_window, L"The value could not be written.", MB_ICONERROR);
        }
    }
    if (d.Data)
        HeapFree(GetProcessHeap(), 0, d.Data);
    RegCloseKey(key);
    fill_list();
}

static void modify_value(void)
{
    int row = selected_row();
    HTREEITEM item = selected_item();
    NODE *node = item ? node_of(item) : nullptr;
    WCHAR name[260];
    if (row < 0 || !node || !row_name(row, name, RTL_NUMBER_OF(name)))
        return;
    HKEY key = open_node(node, KEY_READ | KEY_WRITE);
    if (!key) {
        RegeditBox(g_window, L"The value cannot be changed.", MB_ICONERROR);
        return;
    }
    VALUE_DIALOG d = {};
    d.Owner = g_window;
    d.Title = L"Edit Value";
    d.NameFixed = TRUE;
    d.Type = REG_SZ;
    wcscpy(d.Name, name[0] ? name : L"(Default)");
    DWORD size = 0;
    LSTATUS status = RegQueryValueExW(key, name, nullptr, &d.Type, nullptr, &size);
    if (status == ERROR_SUCCESS) {
        d.Data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 8);
        d.Size = size;
        if (d.Data)
            RegQueryValueExW(key, name, nullptr, &d.Type, d.Data, &size);
    } else if (status != ERROR_FILE_NOT_FOUND) {
        RegCloseKey(key);
        return;
    } else {
        d.Type = REG_SZ;
    }
    if (run_value_dialog(&d) && RegSetValueExW(key, name, 0, d.Type, d.Data, d.Size) != ERROR_SUCCESS)
        RegeditBox(g_window, L"The value could not be written.", MB_ICONERROR);
    if (d.Data)
        HeapFree(GetProcessHeap(), 0, d.Data);
    RegCloseKey(key);
    fill_list();
}

static void rename_value(void)
{
    int row = selected_row();
    HTREEITEM item = selected_item();
    NODE *node = item ? node_of(item) : nullptr;
    WCHAR name[260];
    if (row < 0 || !node || !row_name(row, name, RTL_NUMBER_OF(name)))
        return;
    if (!name[0]) {
        RegeditBox(g_window, L"The default value cannot be renamed.", MB_ICONERROR);
        return;
    }
    HKEY key = open_node(node, KEY_READ | KEY_WRITE);
    if (!key)
        return;
    VALUE_DIALOG d = {};
    d.Owner = g_window;
    d.Title = L"Rename Value";
    d.NameOnly = TRUE;
    wcscpy(d.Name, name);
    if (run_value_dialog(&d) && d.Name[0] && _wcsicmp(d.Name, name) != 0) {
        DWORD type, size = 0;
        if (RegQueryValueExW(key, d.Name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            RegeditBox(g_window, L"A value with that name already exists.", MB_ICONERROR);
        } else if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS) {
            BYTE *data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 8);
            if (data && RegQueryValueExW(key, name, nullptr, &type, data, &size) == ERROR_SUCCESS &&
                RegSetValueExW(key, d.Name, 0, type, data, size) == ERROR_SUCCESS)
                RegDeleteValueW(key, name);
            if (data)
                HeapFree(GetProcessHeap(), 0, data);
        }
    }
    RegCloseKey(key);
    fill_list();
}

static void delete_value(void)
{
    int row = selected_row();
    HTREEITEM item = selected_item();
    NODE *node = item ? node_of(item) : nullptr;
    WCHAR name[260];
    if (row < 0 || !node || !row_name(row, name, RTL_NUMBER_OF(name)))
        return;
    if (RegeditBox(g_window, L"Deleting certain registry values could cause system instability. Are you sure you want to "
                             L"permanently delete this value?",
                   MB_YESNO | MB_ICONWARNING) != IDYES)
        return;
    HKEY key = open_node(node, KEY_READ | KEY_WRITE);
    if (!key || (RegDeleteValueW(key, name) != ERROR_SUCCESS && name[0]))
        RegeditBox(g_window, L"The value could not be deleted.", MB_ICONERROR);
    if (key)
        RegCloseKey(key);
    fill_list();
}

static void delete_key(void)
{
    HTREEITEM item = selected_item();
    NODE *node = item ? node_of(item) : nullptr;
    if (!node || !node->Sub[0])
        return;
    if (RegeditBox(g_window, L"Are you sure you want to permanently delete this key and all of its subkeys?",
                   MB_YESNO | MB_ICONWARNING) != IDYES)
        return;
    if (MimeRegSysDeleteTree(MimeRegSysRootKey(node->Root), node->Sub, 0) != ERROR_SUCCESS) {
        RegeditBox(g_window, L"The key could not be deleted.", MB_ICONERROR);
        return;
    }
    WCHAR parent[1200];
    full_name(node, parent, RTL_NUMBER_OF(parent));
    PWSTR slash = wcsrchr(parent, L'\\');
    if (slash)
        *slash = 0;
    after_change(parent);
}

/* copies the values and subkeys of one key into another. */
static void copy_tree(HKEY from, HKEY to)
{
    DWORD values = 0, longest_name = 0, longest_data = 0, subkeys = 0, longest_key = 0;
    RegQueryInfoKeyW(from, nullptr, nullptr, nullptr, &subkeys, &longest_key, nullptr, &values, &longest_name,
                     &longest_data, nullptr, nullptr);
    WCHAR *name = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(longest_name > longest_key ? longest_name : longest_key) * 2 + 8);
    BYTE *data = HeapAlloc(GetProcessHeap(), 0, longest_data + 8);
    for (DWORD i = 0; name && data && i < values; i++) {
        DWORD name_size = longest_name + 1, size = longest_data, type;
        if (RegEnumValueW(from, i, name, &name_size, nullptr, &type, data, &size) == ERROR_SUCCESS)
            RegSetValueExW(to, name, 0, type, data, size);
    }
    for (DWORD i = 0; name && i < subkeys; i++) {
        DWORD length = longest_key + 1;
        HKEY a, b;
        if (RegEnumKeyExW(from, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            continue;
        if (RegOpenKeyExW(from, name, 0, KEY_READ, &a) != ERROR_SUCCESS)
            continue;
        if (RegCreateKeyExW(to, name, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &b, nullptr) == ERROR_SUCCESS) {
            copy_tree(a, b);
            RegCloseKey(b);
        }
        RegCloseKey(a);
    }
    if (name)
        HeapFree(GetProcessHeap(), 0, name);
    if (data)
        HeapFree(GetProcessHeap(), 0, data);
}

static void rename_key(void)
{
    HTREEITEM item = selected_item();
    NODE *node = item ? node_of(item) : nullptr;
    if (!node || !node->Sub[0])
        return;
    PCWSTR slash = wcsrchr(node->Sub, L'\\');
    PCWSTR leaf = slash ? slash + 1 : node->Sub;
    VALUE_DIALOG d = {};
    d.Owner = g_window;
    d.Title = L"Rename Key";
    d.NameOnly = TRUE;
    wcscpy(d.Name, leaf);
    if (!run_value_dialog(&d) || !d.Name[0] || !wcscmp(d.Name, leaf))
        return;
    if (wcschr(d.Name, L'\\')) {
        RegeditBox(g_window, L"A key name cannot contain a backslash.", MB_ICONERROR);
        return;
    }
    WCHAR parent_sub[1100], target[1200];
    SIZE_T parent_length = slash ? (SIZE_T)(slash - node->Sub) : 0;
    memcpy(parent_sub, node->Sub, parent_length * sizeof(WCHAR));
    parent_sub[parent_length] = 0;
    _snwprintf(target, RTL_NUMBER_OF(target) - 1, parent_length ? L"%s\\%s" : L"%s%s", parent_sub, d.Name);
    target[RTL_NUMBER_OF(target) - 1] = 0;
    HKEY root = MimeRegSysRootKey(node->Root), from, to;
    if (RegOpenKeyExW(root, target, 0, KEY_READ, &to) == ERROR_SUCCESS) {
        RegCloseKey(to);
        RegeditBox(g_window, L"A key with that name already exists.", MB_ICONERROR);
        return;
    }
    if (RegOpenKeyExW(root, node->Sub, 0, KEY_READ, &from) != ERROR_SUCCESS ||
        RegCreateKeyExW(root, target, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &to, nullptr) != ERROR_SUCCESS) {
        RegeditBox(g_window, L"The key could not be renamed.", MB_ICONERROR);
        return;
    }
    copy_tree(from, to);
    RegCloseKey(from);
    RegCloseKey(to);
    MimeRegSysDeleteTree(root, node->Sub, 0);
    WCHAR path[1300];
    _snwprintf(path, RTL_NUMBER_OF(path) - 1, L"%s\\%s", MimeRegSysRootName(node->Root), target);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    after_change(path);
}

static void copy_key_name(void)
{
    HTREEITEM item = selected_item();
    NODE *node = item ? node_of(item) : nullptr;
    if (!node || !OpenClipboard(g_window))
        return;
    WCHAR path[1200];
    full_name(node, path, RTL_NUMBER_OF(path));
    SIZE_T bytes = (wcslen(path) + 1) * sizeof(WCHAR);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void *p = GlobalLock(memory);
        if (p) {
            memcpy(p, path, bytes);
            GlobalUnlock(memory);
            EmptyClipboard();
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

static BOOL ask_file(BOOL save, WCHAR *path, DWORD capacity)
{
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_window;
    ofn.lpstrFilter = L"Registration Files (*.reg)\0*.reg\0Win9x/NT4 Registration Files (*.reg)\0*.reg\0All Files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = capacity;
    ofn.lpstrDefExt = L"reg";
    ofn.lpstrTitle = save ? L"Export Registry File" : L"Import Registry File";
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    path[0] = 0;
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (ok && save && ofn.nFilterIndex == 2)
        return 2;
    return ok;
}

static void import_command(void)
{
    WCHAR path[MAX_PATH];
    if (!ask_file(FALSE, path, MAX_PATH))
        return;
    RegeditImportFile(path, FALSE, FALSE);
    refresh();
}

static void export_command(void)
{
    HTREEITEM item = selected_item();
    NODE *node = item ? node_of(item) : nullptr;
    WCHAR path[MAX_PATH];
    int answer = ask_file(TRUE, path, MAX_PATH);
    if (!answer)
        return;
    WCHAR key[1200];
    if (node)
        full_name(node, key, RTL_NUMBER_OF(key));
    RegeditExport(path, node ? key : nullptr, answer == 2);
}

static void update_menu(void)
{
}

static void command(UINT id)
{
    switch (id) {
    case IDM_IMPORT:
        import_command();
        break;
    case IDM_EXPORT:
        export_command();
        break;
    case IDM_EXIT:
        DestroyWindow(g_window);
        break;
    case IDM_NEW_KEY:
        new_key();
        break;
    case IDM_NEW_STRING:
        new_value(REG_SZ);
        break;
    case IDM_NEW_BINARY:
        new_value(REG_BINARY);
        break;
    case IDM_NEW_DWORD:
        new_value(REG_DWORD);
        break;
    case IDM_NEW_QWORD:
        new_value(REG_QWORD);
        break;
    case IDM_NEW_MULTI:
        new_value(REG_MULTI_SZ);
        break;
    case IDM_NEW_EXPAND:
        new_value(REG_EXPAND_SZ);
        break;
    case IDM_MODIFY:
        modify_value();
        break;
    case IDM_DELETE:
        if (g_list_active)
            delete_value();
        else
            delete_key();
        break;
    case IDM_RENAME:
        if (g_list_active)
            rename_value();
        else
            rename_key();
        break;
    case IDM_COPY_NAME:
        copy_key_name();
        break;
    case IDM_REFRESH:
        refresh();
        break;
    case IDM_ABOUT:
        RegeditBox(g_window, L"Registry Editor\n\nMime's registry editor.", MB_ICONINFORMATION);
        break;
    }
    update_menu();
}

static HMENU build_menu(void)
{
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_IMPORT, L"&Import...");
    AppendMenuW(file, MF_STRING, IDM_EXPORT, L"&Export...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"E&xit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    HMENU fresh = CreatePopupMenu();
    AppendMenuW(fresh, MF_STRING, IDM_NEW_KEY, L"&Key");
    AppendMenuW(fresh, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(fresh, MF_STRING, IDM_NEW_STRING, L"&String Value");
    AppendMenuW(fresh, MF_STRING, IDM_NEW_BINARY, L"&Binary Value");
    AppendMenuW(fresh, MF_STRING, IDM_NEW_DWORD, L"&DWORD (32-bit) Value");
    AppendMenuW(fresh, MF_STRING, IDM_NEW_QWORD, L"&QWORD (64-bit) Value");
    AppendMenuW(fresh, MF_STRING, IDM_NEW_MULTI, L"&Multi-String Value");
    AppendMenuW(fresh, MF_STRING, IDM_NEW_EXPAND, L"&Expandable String Value");
    HMENU edit = CreatePopupMenu();
    g_edit_menu = edit;
    AppendMenuW(edit, MF_POPUP, (UINT_PTR)fresh, L"&New");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, IDM_MODIFY, L"&Modify...");
    AppendMenuW(edit, MF_STRING, IDM_DELETE, L"&Delete\tDel");
    AppendMenuW(edit, MF_STRING, IDM_RENAME, L"&Rename\tF2");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, IDM_COPY_NAME, L"&Copy Key Name");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, IDM_REFRESH, L"&Refresh\tF5");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"&About Registry Editor...");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

static void make_font(void)
{
    NONCLIENTMETRICSW metrics = {sizeof(metrics)};
    if (g_font)
        DeleteObject(g_font);
    g_font = nullptr;
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
        g_font = CreateFontIndirectW(&metrics.lfMessageFont);
    HWND all[4] = {g_tree, g_list, g_status, g_address};
    for (int i = 0; i < 4; i++)
        if (all[i] && g_font)
            SendMessageW(all[i], WM_SETFONT, (WPARAM)g_font, TRUE);
}

static LRESULT notify(NMHDR *header)
{
    if (header->code == NM_SETFOCUS || header->code == NM_CLICK || header->code == NM_RCLICK) {
        if (header->hwndFrom == g_list)
            g_list_active = TRUE;
        else if (header->hwndFrom == g_tree)
            g_list_active = FALSE;
    }
    if (header->code == NM_RCLICK && (header->hwndFrom == g_tree || header->hwndFrom == g_list)) {
        POINT p;
        GetCursorPos(&p);
        if (header->hwndFrom == g_tree) {
            TVHITTESTINFO hit = {};
            POINT client = p;
            ScreenToClient(g_tree, &client);
            hit.pt = client;
            HTREEITEM item = (HTREEITEM)SendMessageW(g_tree, TVM_HITTEST, 0, (LPARAM)&hit);
            if (item)
                SendMessageW(g_tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)item);
        }
        TrackPopupMenu(g_edit_menu, TPM_LEFTALIGN, p.x, p.y, 0, g_window, nullptr);
        return 0;
    }
    if (header->hwndFrom == g_tree) {
        if (header->code == TVN_ITEMEXPANDINGW) {
            NMTREEVIEWW *tv = (NMTREEVIEWW *)header;
            if (tv->action == TVE_EXPAND && !SendMessageW(g_tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)tv->itemNew.hItem))
                fill_children(tv->itemNew.hItem);
        } else if (header->code == TVN_SELCHANGEDW) {
            fill_list();
            set_status();
        } else if (header->code == TVN_DELETEITEMW) {
            NMTREEVIEWW *tv = (NMTREEVIEWW *)header;
            if (tv->itemOld.lParam)
                HeapFree(GetProcessHeap(), 0, (void *)tv->itemOld.lParam);
        }
    } else if (header->hwndFrom == g_list) {
        if (header->code == NM_DBLCLK)
            modify_value();
    }
    return 0;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE: {
        g_window = window;
        g_dpi = (int)GetDpiForWindow(window);
        if (g_dpi < 96)
            g_dpi = 96;
        INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&controls);
        HINSTANCE instance = GetModuleHandleW(nullptr);
        g_address = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10,
                                    window, (HMENU)(UINT_PTR)ID_ADDRESS, instance, nullptr);
        g_tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT |
                                     TVS_SHOWSELALWAYS,
                                 0, 0, 10, 10, window, (HMENU)(UINT_PTR)ID_TREE, instance, nullptr);
        g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0,
                                 10, 10, window, (HMENU)(UINT_PTR)ID_LIST, instance, nullptr);
        g_status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, window,
                                   (HMENU)(UINT_PTR)ID_STATUS, instance, nullptr);
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT);
        static const PCWSTR names[3] = {L"Name", L"Type", L"Data"};
        static const int widths[3] = {220, 140, 420};
        for (int i = 0; i < 3; i++) {
            LVCOLUMNW column = {};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = (PWSTR)names[i];
            column.cx = scale(widths[i]);
            ListView_InsertColumn(g_list, i, &column);
        }
        make_font();
        populate_roots();
        return 0;
    }
    case WM_SIZE:
        lay_out();
        return 0;
    case WM_SETFOCUS:
        SetFocus(g_tree);
        return 0;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wparam);
        RECT *suggested = (RECT *)lparam;
        SetWindowPos(window, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        make_font();
        lay_out();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == ID_ADDRESS)
            return 0;
        command(LOWORD(wparam));
        return 0;
    case WM_NOTIFY:
        return notify((NMHDR *)lparam);
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(window, &p);
            if (g_dragging || (p.x >= g_split && p.x < g_split + scale(5) && p.y > scale(26))) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    case WM_LBUTTONDOWN:
        if (LOWORD(lparam) >= g_split && LOWORD(lparam) < g_split + scale(5)) {
            g_dragging = TRUE;
            SetCapture(window);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragging) {
            g_split = (short)LOWORD(lparam);
            lay_out();
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_dragging) {
            g_dragging = FALSE;
            ReleaseCapture();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

/* the program started before this one, if any. */
static HWND other_window(void)
{
    return FindWindowW(CLASS_NAME, nullptr);
}

void RegeditRefresh(void)
{
    HWND other = other_window();
    if (other)
        PostMessageW(other, WM_COMMAND, IDM_REFRESH, 0);
}

void RegeditRun(void)
{
    if (!g_allow_multiple) {
        HWND other = other_window();
        if (other) {
            if (IsIconic(other))
                ShowWindow(other, SW_RESTORE);
            else
                SetWindowPos(other, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetForegroundWindow(other);
            return;
        }
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW class = {};
    class.lpfnWndProc = window_proc;
    class.hInstance = instance;
    class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    class.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    class.lpszClassName = CLASS_NAME;
    if (!RegisterClassW(&class))
        return;
    HWND window = CreateWindowExW(0, CLASS_NAME, REGEDIT_TITLE, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                  CW_USEDEFAULT, 960, 640, nullptr, build_menu(), instance, nullptr);
    if (!window)
        return;
    {
        UINT dpi = GetDpiForWindow(window);
        if (dpi > 96)
            SetWindowPos(window, nullptr, 0, 0, MulDiv(960, (int)dpi, 96), MulDiv(640, (int)dpi, 96), SWP_NOMOVE | SWP_NOZORDER);
    }
    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.hwnd == g_address && msg.wParam == VK_RETURN) {
            WCHAR typed[1200];
            GetWindowTextW(g_address, typed, RTL_NUMBER_OF(typed));
            if (!navigate(typed))
                MessageBeep(MB_ICONERROR);
            continue;
        }
        if (msg.message == WM_KEYDOWN && (msg.hwnd == g_tree || msg.hwnd == g_list)) {
            UINT id = msg.wParam == VK_F5 ? IDM_REFRESH : msg.wParam == VK_DELETE ? IDM_DELETE : msg.wParam == VK_F2 ? IDM_RENAME : 0;
            if (id) {
                command(id);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
