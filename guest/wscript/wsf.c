/*
 * windows script files: the XML file with jobs, each with its runtime description, objects, references, resources and the
 * scripts that run in the engines of their languages. The file is read by a small parser of its own, the checks that Windows
 * makes before any script runs are made on the whole file, and the errors name the line and column of what they found.
 */
#include "wsh.h"

#define MSG_BASE 0x80040000

/* the message numbers of the parser, the low part of the code that a dialog shows is the number less 1024. */
enum {
    M_UNTERMINATED_XML_DECL = 1026,
    M_UNTERMINATED_COMMENT = 1027,
    M_UNTERMINATED_CDATA = 1028,
    M_UNTERMINATED_VALUE = 1029,
    M_UNTERMINATED_START = 1030,
    M_UNTERMINATED_END = 1031,
    M_UNTERMINATED_ENTITY = 1032,
    M_UNTERMINATED_OPAQUE = 1033,
    M_EXPECT_NAME = 1034,
    M_EXPECT_QUOTES = 1035,
    M_INVALID_ENTITY = 1036,
    M_UNRECOGNIZED_MARKUP = 1037,
    M_UNTERMINATED_PI = 1038,
    M_UNEXPECTED_XML_DECL = 1056,
    M_END_MISMATCH = 1058,
    M_NO_END_TAG = 1059,
    M_MISSING_ATTRIBUTE = 1094,
    M_BAD_VALUE = 1097,
    M_END_WITHOUT_START = 1098,
    M_DUPLICATE_ID = 1101,
    M_CONFLICT = 1102,
    M_CANNOT_CREATE_ENGINE = 1124,
    M_CANNOT_RETRIEVE_URL = 1125,
    M_CANNOT_CREATE_OBJECT = 1126,
    M_NO_TYPELIB = 1128,
    M_NO_JOBS = 1216,
    M_UNEXPECTED_ROOT = 1217,
    M_ONLY_ONE_ROOT = 1218,
    M_DUPLICATE_NAME = 1220,
};

typedef struct XATTR {
    PWSTR Name, Value;
    ULONG NameLine, NameColumn, ValueLine, ValueColumn;
} XATTR;

typedef struct XELEM {
    struct XELEM *Parent, *First, *Last, *Next;
    PWSTR Name;
    XATTR *Attributes;
    int AttributeCount, AttributeCapacity;
    ULONG Line, Column;
    PWSTR Text;
    SIZE_T TextLength, TextCapacity;
    ULONG TextLine;
    BOOL Root;
} XELEM;

typedef struct XDOC {
    PCWSTR At;
    ULONG Line, Column;
    XELEM Top;
    HRESULT Error;
    ULONG ErrorLine, ErrorColumn;
    BSTR ErrorText;
} XDOC;

typedef struct JOB_SCRIPT {
    struct JOB_SCRIPT *Next;
    BSTR Text;
    WCHAR Clsid[40];
    ULONG Line;
} JOB_SCRIPT;

typedef struct JOB {
    XELEM *Element;
    XELEM *Runtime;
    JOB_SCRIPT *Scripts, *LastScript;
    PCWSTR Id;
} JOB;

/* ------------------------------------------------------------------ memory and text */

static void *allocate(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

static void release(void *block)
{
    if (block)
        HeapFree(GetProcessHeap(), 0, block);
}

static PWSTR duplicate_n(PCWSTR text, SIZE_T length)
{
    PWSTR copy = allocate((length + 1) * sizeof(WCHAR));
    if (copy) {
        memcpy(copy, text, length * sizeof(WCHAR));
        copy[length] = 0;
    }
    return copy;
}

static void copy_text(PWSTR out, SIZE_T capacity, PCWSTR text)
{
    SIZE_T n = wcslen(text);
    if (n >= capacity)
        n = capacity - 1;
    memcpy(out, text, n * sizeof(WCHAR));
    out[n] = 0;
}

static int to_int(PCWSTR text)
{
    int value = 0;
    while (*text >= L'0' && *text <= L'9')
        value = value * 10 + (*text++ - L'0');
    return value;
}

/* the file as text: a byte order mark, the encoding of the declaration, or UTF-8. */
static HRESULT load_file_text(PCWSTR path, BSTR *text)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER size;
    BYTE *data = nullptr;
    DWORD read = 0;
    HRESULT hr = S_OK;
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 0x7FFFFFF0)
        hr = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_FILE_TOO_LARGE);
    else {
        data = allocate((SIZE_T)size.QuadPart + 4);
        if (!data)
            hr = E_OUTOFMEMORY;
        else if (size.QuadPart && !ReadFile(file, data, (DWORD)size.QuadPart, &read, nullptr))
            hr = HRESULT_FROM_WIN32(GetLastError());
    }
    CloseHandle(file);
    if (FAILED(hr)) {
        release(data);
        return hr;
    }
    *text = nullptr;
    const BYTE *body = data;
    UINT count = read, page = CP_UTF8;
    if (count >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF))) {
        BOOL big = data[0] == 0xFE;
        UINT length = (count - 2) / 2;
        *text = SysAllocStringLen(nullptr, length);
        for (UINT i = 0; *text && i < length; i++)
            (*text)[i] = big ? (WCHAR)(data[2 + i * 2] << 8 | data[3 + i * 2]) : (WCHAR)(data[2 + i * 2] | data[3 + i * 2] << 8);
        release(data);
        return *text ? S_OK : E_OUTOFMEMORY;
    }
    if (count >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        body += 3;
        count -= 3;
    } else {
        /* the encoding named in the declaration, when it is a single byte one. */
        for (UINT i = 0; i + 8 < count && i < 200; i++) {
            if (!memcmp(body + i, "encoding", 8)) {
                UINT j = i + 8;
                while (j < count && (body[j] == ' ' || body[j] == '=' || body[j] == '"' || body[j] == '\''))
                    j++;
                char name[32];
                UINT n = 0;
                while (j < count && n < 31 && body[j] != '"' && body[j] != '\'' && body[j] != ' ')
                    name[n++] = (char)body[j++];
                name[n] = 0;
                if (!_stricmp(name, "iso-8859-1") || !_stricmp(name, "windows-1252") || !_stricmp(name, "us-ascii") || !_stricmp(name, "ascii"))
                    page = 1252;
                break;
            }
        }
    }
    int length = count ? MultiByteToWideChar(page, 0, (PCSTR)body, (int)count, nullptr, 0) : 0;
    *text = SysAllocStringLen(nullptr, length);
    if (*text && length)
        MultiByteToWideChar(page, 0, (PCSTR)body, (int)count, *text, length);
    release(data);
    return *text ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ errors */

static void set_error(XDOC *doc, UINT id, ULONG line, ULONG column, PCWSTR detail)
{
    BSTR message = WshLoadString(id);
    if (doc->Error || !message) {
        SysFreeString(message);
        return;
    }
    SIZE_T length = SysStringLen(message) + (detail ? wcslen(detail) + 3 : 0);
    doc->ErrorText = SysAllocStringLen(nullptr, (UINT)length);
    if (doc->ErrorText) {
        wcscpy(doc->ErrorText, message);
        if (detail && *detail) {
            wcscat(doc->ErrorText, L" : ");
            wcscat(doc->ErrorText, detail);
        }
    }
    SysFreeString(message);
    doc->Error = (HRESULT)(MSG_BASE + (id - 1024));
    doc->ErrorLine = line;
    doc->ErrorColumn = column;
}

static HRESULT report_error(XDOC *doc)
{
    BSTR source = WshLoadString(RES_BASE + 1);
    WshReportScriptError(doc->ErrorLine, (LONG)doc->ErrorColumn + 1, source ? source : L"", doc->ErrorText ? doc->ErrorText : L"", L"",
                         (DWORD)doc->Error);
    SysFreeString(source);
    return WSH_E_REPORTED;
}

/* ------------------------------------------------------------------ the parser */

static void advance(XDOC *doc, SIZE_T count)
{
    while (count--) {
        if (*doc->At == L'\n') {
            doc->Line++;
            doc->Column = 1;
        } else
            doc->Column++;
        doc->At++;
    }
}

static BOOL starts_with(const XDOC *doc, PCWSTR text)
{
    return !wcsncmp(doc->At, text, wcslen(text));
}

static BOOL is_blank(WCHAR c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

static BOOL is_name_char(WCHAR c)
{
    return c && !is_blank(c) && c != L'=' && c != L'/' && c != L'>' && c != L'<' && c != L'"' && c != L'\'' && c != L'?';
}

static void skip_blanks(XDOC *doc)
{
    while (is_blank(*doc->At))
        advance(doc, 1);
}

static XATTR *add_attribute(XELEM *element)
{
    if (element->AttributeCount == element->AttributeCapacity) {
        int capacity = element->AttributeCapacity ? element->AttributeCapacity * 2 : 4;
        XATTR *grown = allocate(capacity * sizeof(XATTR));
        if (!grown)
            return nullptr;
        if (element->Attributes)
            memcpy(grown, element->Attributes, element->AttributeCount * sizeof(XATTR));
        release(element->Attributes);
        element->Attributes = grown;
        element->AttributeCapacity = capacity;
    }
    return &element->Attributes[element->AttributeCount++];
}

static void append_text(XELEM *element, PCWSTR text, SIZE_T length)
{
    if (element->TextLength + length + 1 > element->TextCapacity) {
        SIZE_T capacity = (element->TextLength + length + 1) * 2;
        PWSTR grown = allocate(capacity * sizeof(WCHAR));
        if (!grown)
            return;
        if (element->Text)
            memcpy(grown, element->Text, element->TextLength * sizeof(WCHAR));
        release(element->Text);
        element->Text = grown;
        element->TextCapacity = capacity;
    }
    memcpy(element->Text + element->TextLength, text, length * sizeof(WCHAR));
    element->TextLength += length;
    element->Text[element->TextLength] = 0;
}

/* an entity or character reference at the cursor, with the cursor on the ampersand. */
static BOOL decode_entity(XDOC *doc, WCHAR *out)
{
    PCWSTR end = wcschr(doc->At, L';');
    SIZE_T length = end ? (SIZE_T)(end - doc->At) : 0;
    if (!end || length > 12) {
        set_error(doc, M_UNTERMINATED_ENTITY, doc->Line, doc->Column, nullptr);
        return FALSE;
    }
    PCWSTR name = doc->At + 1;
    SIZE_T n = length - 1;
    if (n == 2 && !wcsncmp(name, L"lt", 2))
        *out = L'<';
    else if (n == 2 && !wcsncmp(name, L"gt", 2))
        *out = L'>';
    else if (n == 3 && !wcsncmp(name, L"amp", 3))
        *out = L'&';
    else if (n == 4 && !wcsncmp(name, L"quot", 4))
        *out = L'"';
    else if (n == 4 && !wcsncmp(name, L"apos", 4))
        *out = L'\'';
    else if (n > 1 && name[0] == L'#') {
        unsigned long value = 0;
        BOOL hex = name[1] == L'x' || name[1] == L'X';
        for (SIZE_T i = hex ? 2 : 1; i < n; i++) {
            WCHAR c = name[i];
            int digit = c >= L'0' && c <= L'9' ? c - L'0' : hex && (c | 0x20) >= L'a' && (c | 0x20) <= L'f' ? (c | 0x20) - L'a' + 10 : -1;
            if (digit < 0) {
                set_error(doc, M_INVALID_ENTITY, doc->Line, doc->Column, nullptr);
                return FALSE;
            }
            value = value * (hex ? 16 : 10) + (unsigned long)digit;
        }
        *out = (WCHAR)value;
    } else {
        set_error(doc, M_INVALID_ENTITY, doc->Line, doc->Column, nullptr);
        return FALSE;
    }
    advance(doc, length + 1);
    return TRUE;
}

/* the value of an attribute at the cursor: in quotes or a word, with its entities decoded. */
static BOOL parse_value(XDOC *doc, XATTR *attribute)
{
    WCHAR quote = *doc->At;
    XELEM scratch = {};
    if (quote == L'"' || quote == L'\'') {
        advance(doc, 1);
        attribute->ValueLine = doc->Line;
        attribute->ValueColumn = doc->Column;
        for (;;) {
            if (!*doc->At) {
                set_error(doc, M_UNTERMINATED_VALUE, doc->Line, doc->Column, nullptr);
                release(scratch.Text);
                return FALSE;
            }
            if (*doc->At == quote) {
                advance(doc, 1);
                break;
            }
            if (*doc->At == L'&') {
                WCHAR c;
                if (!decode_entity(doc, &c)) {
                    release(scratch.Text);
                    return FALSE;
                }
                append_text(&scratch, &c, 1);
                continue;
            }
            append_text(&scratch, doc->At, 1);
            advance(doc, 1);
        }
    } else {
        if (!is_name_char(*doc->At)) {
            set_error(doc, M_EXPECT_QUOTES, doc->Line, doc->Column, nullptr);
            return FALSE;
        }
        attribute->ValueLine = doc->Line;
        attribute->ValueColumn = doc->Column;
        while (is_name_char(*doc->At)) {
            append_text(&scratch, doc->At, 1);
            advance(doc, 1);
        }
    }
    attribute->Value = scratch.Text ? scratch.Text : duplicate_n(L"", 0);
    return TRUE;
}

static XELEM *new_child(XELEM *parent, PCWSTR name, SIZE_T length, ULONG line, ULONG column)
{
    XELEM *element = allocate(sizeof(XELEM));
    if (!element)
        return nullptr;
    element->Name = duplicate_n(name, length);
    element->Line = line;
    element->Column = column;
    element->Parent = parent;
    if (parent->Last)
        parent->Last->Next = element;
    else
        parent->First = element;
    parent->Last = element;
    return element;
}

static BOOL name_is(const XELEM *element, PCWSTR name)
{
    return !_wcsicmp(element->Name, name);
}

/* the end of the text of a script: the first end tag of it, which is no part of the code. */
static PCWSTR find_script_end(PCWSTR from)
{
    for (PCWSTR scan = from;;) {
        PCWSTR found = wcschr(scan, L'<');
        if (!found)
            return nullptr;
        if (found[1] == L'/' && !_wcsnicmp(found + 2, L"script", 6)) {
            PCWSTR after = found + 8;
            while (is_blank(*after))
                after++;
            if (*after == L'>')
                return found;
        }
        scan = found + 1;
    }
}

/* text up to the next markup, added to the element that holds it. */
static BOOL parse_text(XDOC *doc, XELEM *current)
{
    while (*doc->At && *doc->At != L'<') {
        if (current != &doc->Top && !current->TextLine)
            current->TextLine = doc->Line;
        if (*doc->At == L'&') {
            WCHAR c;
            if (!decode_entity(doc, &c))
                return FALSE;
            if (current != &doc->Top)
                append_text(current, &c, 1);
            continue;
        }
        if (current != &doc->Top)
            append_text(current, doc->At, 1);
        advance(doc, 1);
    }
    return TRUE;
}

static void parse_document(XDOC *doc)
{
    XELEM *current = &doc->Top;
    BOOL seen_markup = FALSE;
    doc->Line = 1;
    doc->Column = 1;
    if (*doc->At == 0xFEFF)
        doc->At++;
    while (*doc->At && !doc->Error) {
        if (*doc->At != L'<') {
            if (!parse_text(doc, current))
                return;
            continue;
        }
        if (starts_with(doc, L"<?")) {
            PCWSTR end = wcsstr(doc->At, L"?>");
            BOOL declaration = !_wcsnicmp(doc->At, L"<?xml", 5) && is_blank(doc->At[5]);
            if (!end) {
                set_error(doc, declaration ? M_UNTERMINATED_XML_DECL : M_UNTERMINATED_PI, doc->Line, doc->Column, nullptr);
                return;
            }
            if (declaration && seen_markup) {
                set_error(doc, M_UNEXPECTED_XML_DECL, doc->Line, doc->Column, nullptr);
                return;
            }
            seen_markup = TRUE;
            advance(doc, (SIZE_T)(end - doc->At) + 2);
            continue;
        }
        if (starts_with(doc, L"<!--")) {
            PCWSTR end = wcsstr(doc->At + 4, L"-->");
            if (!end) {
                set_error(doc, M_UNTERMINATED_COMMENT, doc->Line, doc->Column, nullptr);
                return;
            }
            advance(doc, (SIZE_T)(end - doc->At) + 3);
            continue;
        }
        if (starts_with(doc, L"<![CDATA[")) {
            PCWSTR end = wcsstr(doc->At + 9, L"]]>");
            if (!end) {
                set_error(doc, M_UNTERMINATED_CDATA, doc->Line, doc->Column, nullptr);
                return;
            }
            if (current != &doc->Top) {
                if (!current->TextLine)
                    current->TextLine = doc->Line;
                append_text(current, doc->At + 9, (SIZE_T)(end - doc->At) - 9);
            }
            advance(doc, (SIZE_T)(end - doc->At) + 3);
            continue;
        }
        if (starts_with(doc, L"<!")) {
            PCWSTR end = wcschr(doc->At, L'>');
            if (!end) {
                set_error(doc, M_UNRECOGNIZED_MARKUP, doc->Line, doc->Column, nullptr);
                return;
            }
            advance(doc, (SIZE_T)(end - doc->At) + 1);
            continue;
        }
        seen_markup = TRUE;
        if (starts_with(doc, L"</")) {
            ULONG line = doc->Line, column = doc->Column + 2;
            advance(doc, 2);
            PCWSTR name = doc->At;
            while (is_name_char(*doc->At))
                advance(doc, 1);
            SIZE_T length = (SIZE_T)(doc->At - name);
            skip_blanks(doc);
            if (*doc->At != L'>') {
                set_error(doc, M_UNTERMINATED_END, line, column, nullptr);
                return;
            }
            advance(doc, 1);
            PWSTR copy = duplicate_n(name, length);
            if (current == &doc->Top)
                set_error(doc, M_END_WITHOUT_START, line, column, copy);
            else if (_wcsicmp(current->Name, copy))
                set_error(doc, M_END_MISMATCH, line, column, copy);
            else
                current = current->Parent;
            release(copy);
            continue;
        }
        ULONG line = doc->Line, column = doc->Column + 1;
        advance(doc, 1);
        PCWSTR name = doc->At;
        while (is_name_char(*doc->At))
            advance(doc, 1);
        SIZE_T length = (SIZE_T)(doc->At - name);
        if (!length) {
            set_error(doc, M_EXPECT_NAME, doc->Line, doc->Column, nullptr);
            return;
        }
        XELEM *element = new_child(current, name, length, line, column);
        if (!element) {
            doc->Error = E_OUTOFMEMORY;
            return;
        }
        BOOL empty = FALSE;
        for (;;) {
            skip_blanks(doc);
            if (!*doc->At) {
                set_error(doc, M_UNTERMINATED_START, line, column, nullptr);
                return;
            }
            if (*doc->At == L'>') {
                advance(doc, 1);
                break;
            }
            if (*doc->At == L'/' && doc->At[1] == L'>') {
                advance(doc, 2);
                empty = TRUE;
                break;
            }
            XATTR *attribute = add_attribute(element);
            if (!attribute) {
                doc->Error = E_OUTOFMEMORY;
                return;
            }
            attribute->NameLine = doc->Line;
            attribute->NameColumn = doc->Column;
            PCWSTR attribute_name = doc->At;
            while (is_name_char(*doc->At))
                advance(doc, 1);
            if (doc->At == attribute_name) {
                set_error(doc, M_EXPECT_NAME, doc->Line, doc->Column, nullptr);
                return;
            }
            attribute->Name = duplicate_n(attribute_name, (SIZE_T)(doc->At - attribute_name));
            skip_blanks(doc);
            if (*doc->At != L'=') {
                set_error(doc, M_EXPECT_QUOTES, doc->Line, doc->Column, nullptr);
                return;
            }
            advance(doc, 1);
            skip_blanks(doc);
            if (!parse_value(doc, attribute))
                return;
        }
        if (empty)
            continue;
        if (name_is(element, L"script")) {
            /* the content of a script is no markup: everything up to its end tag is the code. */
            PCWSTR text_end = find_script_end(doc->At);
            element->TextLine = doc->Line;
            if (!text_end) {
                PCWSTR stop = doc->At;
                while (*stop)
                    stop++;
                advance(doc, (SIZE_T)(stop - doc->At));
                set_error(doc, M_UNTERMINATED_OPAQUE, doc->Line, doc->Column, element->Name);
                return;
            }
            append_text(element, doc->At, (SIZE_T)(text_end - doc->At));
            advance(doc, (SIZE_T)(text_end - doc->At));
            advance(doc, (SIZE_T)(wcschr(doc->At, L'>') - doc->At) + 1);
            continue;
        }
        current = element;
    }
    if (!doc->Error && current != &doc->Top)
        set_error(doc, M_NO_END_TAG, current->Line, current->Column, current->Name);
}

static void free_element(XELEM *element)
{
    for (XELEM *child = element->First; child;) {
        XELEM *next = child->Next;
        free_element(child);
        release(child);
        child = next;
    }
    for (int i = 0; i < element->AttributeCount; i++) {
        release(element->Attributes[i].Name);
        release(element->Attributes[i].Value);
    }
    release(element->Attributes);
    release(element->Name);
    release(element->Text);
}

static const XATTR *find_attribute(const XELEM *element, PCWSTR name)
{
    for (int i = 0; i < element->AttributeCount; i++)
        if (!_wcsicmp(element->Attributes[i].Name, name))
            return &element->Attributes[i];
    return nullptr;
}

/* ------------------------------------------------------------------ the check of the document */

static BOOL parse_bool(PCWSTR value, BOOL *out)
{
    if (!_wcsicmp(value, L"true") || !wcscmp(value, L"1")) {
        *out = TRUE;
        return TRUE;
    }
    if (!_wcsicmp(value, L"false") || !wcscmp(value, L"0")) {
        *out = FALSE;
        return TRUE;
    }
    return FALSE;
}

static void error_at_element(XDOC *doc, UINT id, const XELEM *element, PCWSTR detail)
{
    set_error(doc, id, element->Line, element->Column, detail);
}

static void error_at_attribute(XDOC *doc, UINT id, const XATTR *attribute, PCWSTR detail)
{
    set_error(doc, id, attribute->NameLine, attribute->NameColumn, detail);
}

static void error_at_value(XDOC *doc, UINT id, const XATTR *attribute, PCWSTR detail)
{
    set_error(doc, id, attribute->ValueLine, attribute->ValueColumn, detail);
}

static BOOL parse_guid(PCWSTR text, GUID *out)
{
    WCHAR braced[48];
    if (*text == L'{')
        return SUCCEEDED(CLSIDFromString(text, out));
    if (wcslen(text) != 36)
        return FALSE;
    braced[0] = L'{';
    wcscpy(braced + 1, text);
    wcscat(braced, L"}");
    return SUCCEEDED(CLSIDFromString(braced, out));
}

/* the class an object element names, from its progid, clsid or classid. */
static BOOL object_class(XDOC *doc, const XELEM *element, GUID *clsid)
{
    const XATTR *progid = find_attribute(element, L"progid"), *classid = find_attribute(element, L"classid"),
                *clsid_attribute = find_attribute(element, L"clsid");
    int given = (progid != nullptr) + (classid != nullptr) + (clsid_attribute != nullptr);
    if (!given) {
        error_at_element(doc, M_MISSING_ATTRIBUTE, element, L"progid, clsid, or classid");
        return FALSE;
    }
    if (given > 1) {
        const XATTR *first = classid ? classid : clsid_attribute;
        error_at_attribute(doc, M_CONFLICT, first, classid && progid ? L"progid, classid" : clsid_attribute && progid ? L"progid, clsid" : L"clsid, classid");
        return FALSE;
    }
    if (progid) {
        if (FAILED(CLSIDFromProgID(progid->Value, clsid))) {
            error_at_attribute(doc, M_BAD_VALUE, progid, L"progid");
            return FALSE;
        }
        return TRUE;
    }
    const XATTR *attribute = classid ? classid : clsid_attribute;
    PCWSTR value = attribute->Value;
    if (!_wcsnicmp(value, L"clsid:", 6))
        value += 6;
    if (!parse_guid(value, clsid)) {
        error_at_attribute(doc, M_BAD_VALUE, attribute, classid ? L"classid" : L"clsid");
        return FALSE;
    }
    return TRUE;
}

/* a library that a class or a library id names: its id and the version to load. */
static BOOL typelib_of_class(const GUID *clsid, GUID *libid, WORD *major, WORD *minor)
{
    WCHAR text[40], path[128], value[64];
    StringFromGUID2(clsid, text, 40);
    _snwprintf(path, RTL_NUMBER_OF(path), L"CLSID\\%s\\TypeLib", text);
    HKEY key;
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, KEY_READ, &key))
        return FALSE;
    LONG error = RegQueryValueExW(key, nullptr, nullptr, nullptr, (PBYTE)value, &size);
    RegCloseKey(key);
    if (error || FAILED(CLSIDFromString(value, libid)))
        return FALSE;
    *major = 1;
    *minor = 0;
    _snwprintf(path, RTL_NUMBER_OF(path), L"CLSID\\%s\\Version", text);
    size = sizeof(value);
    if (!RegOpenKeyExW(HKEY_CLASSES_ROOT, path, 0, KEY_READ, &key)) {
        if (!RegQueryValueExW(key, nullptr, nullptr, nullptr, (PBYTE)value, &size)) {
            PCWSTR dot = wcschr(value, L'.');
            *major = (WORD)to_int(value);
            *minor = dot ? (WORD)to_int(dot + 1) : 0;
        }
        RegCloseKey(key);
    }
    return TRUE;
}

typedef struct TYPELIB_REF {
    GUID Id;
    WORD Major, Minor;
} TYPELIB_REF;

static BOOL typelib_of_reference(XDOC *doc, const XELEM *element, TYPELIB_REF *ref)
{
    const XATTR *object = find_attribute(element, L"object"), *guid = find_attribute(element, L"guid"),
                *version = find_attribute(element, L"version");
    if (!object && !guid) {
        error_at_element(doc, M_MISSING_ATTRIBUTE, element, L"object or guid");
        return FALSE;
    }
    ref->Major = 1;
    ref->Minor = 0;
    if (guid) {
        if (!parse_guid(guid->Value, &ref->Id)) {
            error_at_attribute(doc, M_BAD_VALUE, guid, L"guid");
            return FALSE;
        }
    } else {
        GUID clsid;
        if (FAILED(CLSIDFromProgID(object->Value, &clsid)) || !typelib_of_class(&clsid, &ref->Id, &ref->Major, &ref->Minor)) {
            error_at_value(doc, M_NO_TYPELIB, object, object->Value);
            return FALSE;
        }
    }
    if (version) {
        PCWSTR dot = wcschr(version->Value, L'.');
        ref->Major = (WORD)to_int(version->Value);
        ref->Minor = dot ? (WORD)to_int(dot + 1) : 0;
    }
    return TRUE;
}

/* the file a src attribute names: a path relative to the script file, a full path, or a file URL. */
static HRESULT read_source(PCWSTR directory, PCWSTR src, BSTR *text)
{
    WCHAR path[MAX_PATH * 2];
    PCWSTR from = src;
    if (!_wcsnicmp(src, L"file:", 5)) {
        from = src + 5;
        while (*from == L'/')
            from++;
        SIZE_T i = 0;
        for (; from[i] && i < RTL_NUMBER_OF(path) - 1; i++)
            path[i] = from[i] == L'/' ? L'\\' : from[i];
        path[i] = 0;
    } else if ((from[0] && from[1] == L':') || (from[0] == L'\\' && from[1] == L'\\'))
        copy_text(path, RTL_NUMBER_OF(path), from);
    else
        _snwprintf(path, RTL_NUMBER_OF(path), L"%s\\%s", directory, from);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    DWORD size = GetFileSize(file, nullptr), read = 0;
    BYTE *data = allocate((SIZE_T)size + 4);
    HRESULT hr = S_OK;
    if (!data)
        hr = E_OUTOFMEMORY;
    else if (size && !ReadFile(file, data, size, &read, nullptr))
        hr = HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(file);
    if (SUCCEEDED(hr))
        hr = WshConvertText(g_host.CodePage ? g_host.CodePage : CP_ACP, data, read, text);
    release(data);
    return hr;
}

/* checks one script element and queues what it runs: the file of its src, then its own text. */
static BOOL check_script(XDOC *doc, JOB *job, const XELEM *element, PCWSTR directory)
{
    const XATTR *language = find_attribute(element, L"language"), *src = find_attribute(element, L"src");
    GUID clsid;
    if (!language) {
        set_error(doc, M_BAD_VALUE, 0, 0, nullptr);
        return FALSE;
    }
    if (FAILED(CLSIDFromProgID(language->Value, &clsid))) {
        error_at_attribute(doc, M_BAD_VALUE, language, L"language");
        return FALSE;
    }
    WCHAR clsid_text[40];
    StringFromGUID2(&clsid, clsid_text, 40);
    for (int pass = 0; pass < 2; pass++) {
        BSTR text = nullptr;
        ULONG line = element->TextLine;
        if (pass == 0) {
            if (!src)
                continue;
            if (FAILED(read_source(directory, src->Value, &text))) {
                error_at_value(doc, M_CANNOT_RETRIEVE_URL, src, src->Value);
                return FALSE;
            }
            line = 1;
        } else {
            if (!element->TextLength)
                continue;
            text = SysAllocStringLen(element->Text, (UINT)element->TextLength);
        }
        JOB_SCRIPT *entry = allocate(sizeof(*entry));
        if (!entry || !text) {
            SysFreeString(text);
            release(entry);
            doc->Error = E_OUTOFMEMORY;
            return FALSE;
        }
        entry->Text = text;
        entry->Line = line;
        wcscpy(entry->Clsid, clsid_text);
        if (job->LastScript)
            job->LastScript->Next = entry;
        else
            job->Scripts = entry;
        job->LastScript = entry;
    }
    return TRUE;
}

static BOOL check_runtime(XDOC *doc, const XELEM *runtime)
{
    for (const XELEM *child = runtime->First; child; child = child->Next) {
        BOOL named = name_is(child, L"named"), unnamed = name_is(child, L"unnamed");
        if (!named && !unnamed)
            continue;
        const XATTR *name = find_attribute(child, L"name"), *help = find_attribute(child, L"helpstring");
        if (!name || !help) {
            error_at_element(doc, M_MISSING_ATTRIBUTE, child, name ? L"helpstring" : L"name");
            return FALSE;
        }
        const XATTR *type = find_attribute(child, L"type"), *required = find_attribute(child, L"required"), *many = find_attribute(child, L"many");
        BOOL flag;
        if (type && _wcsicmp(type->Value, L"string") && _wcsicmp(type->Value, L"boolean") && _wcsicmp(type->Value, L"simple")) {
            error_at_attribute(doc, M_BAD_VALUE, type, L"type");
            return FALSE;
        }
        if (required && named && !parse_bool(required->Value, &flag)) {
            error_at_attribute(doc, M_BAD_VALUE, required, L"required");
            return FALSE;
        }
        if (many && !parse_bool(many->Value, &flag)) {
            error_at_attribute(doc, M_BAD_VALUE, many, L"many");
            return FALSE;
        }
        for (const XELEM *other = runtime->First; other != child; other = other->Next) {
            const XATTR *other_name = find_attribute(other, L"name");
            if ((name_is(other, L"named") || name_is(other, L"unnamed")) && other_name && !_wcsicmp(other_name->Value, name->Value) &&
                name_is(other, L"named") == named) {
                error_at_element(doc, M_DUPLICATE_NAME, child, name->Value);
                return FALSE;
            }
        }
    }
    return TRUE;
}

static BOOL check_job(XDOC *doc, JOB *job, PCWSTR directory)
{
    const XELEM *element = job->Element;
    PWSTR seen[64];
    int seen_count = 0;
    for (const XELEM *child = element->First; child; child = child->Next) {
        if (name_is(child, L"runtime")) {
            if (!check_runtime(doc, child))
                return FALSE;
            job->Runtime = (XELEM *)child;
        } else if (name_is(child, L"object")) {
            const XATTR *id = find_attribute(child, L"id");
            GUID clsid;
            BOOL flag;
            if (!id) {
                error_at_element(doc, M_MISSING_ATTRIBUTE, child, L"id");
                return FALSE;
            }
            if (!object_class(doc, child, &clsid))
                return FALSE;
            const XATTR *events = find_attribute(child, L"events"), *reference = find_attribute(child, L"reference");
            if ((events && !parse_bool(events->Value, &flag)) || (reference && !parse_bool(reference->Value, &flag))) {
                error_at_attribute(doc, M_BAD_VALUE, events ? events : reference, events ? L"events" : L"reference");
                return FALSE;
            }
            for (int i = 0; i < seen_count; i++)
                if (!_wcsicmp(seen[i], id->Value)) {
                    error_at_element(doc, M_DUPLICATE_ID, child, id->Value);
                    return FALSE;
                }
            if (seen_count < 64)
                seen[seen_count++] = id->Value;
        } else if (name_is(child, L"reference")) {
            TYPELIB_REF ref;
            if (!typelib_of_reference(doc, child, &ref))
                return FALSE;
        } else if (name_is(child, L"resource")) {
            const XATTR *id = find_attribute(child, L"id");
            if (id) {
                for (const XELEM *other = element->First; other != child; other = other->Next) {
                    const XATTR *other_id = name_is(other, L"resource") ? find_attribute(other, L"id") : nullptr;
                    if (other_id && !_wcsicmp(other_id->Value, id->Value)) {
                        error_at_element(doc, M_DUPLICATE_ID, child, id->Value);
                        return FALSE;
                    }
                }
            }
        } else if (name_is(child, L"script")) {
            if (!check_script(doc, job, child, directory))
                return FALSE;
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------ the resources of a job */

typedef struct RESOURCES {
    IDispatch Dispatch;
    LONG Refs;
    const XELEM *Job;
} RESOURCES;

static HRESULT STDMETHODCALLTYPE res_query_interface(IDispatch *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch)) {
        *out = This;
        InterlockedIncrement(&((RESOURCES *)This)->Refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE res_add_ref(IDispatch *This)
{
    return (ULONG)InterlockedIncrement(&((RESOURCES *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE res_release(IDispatch *This)
{
    LONG refs = InterlockedDecrement(&((RESOURCES *)This)->Refs);
    if (!refs)
        release(This);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE res_count(IDispatch *This, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE res_type_info(IDispatch *This, UINT index, LCID lcid, ITypeInfo **info)
{
    *info = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE res_ids(IDispatch *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    if (count != 1)
        return DISP_E_UNKNOWNNAME;
    if (!_wcsicmp(names[0], L"getResource")) {
        ids[0] = 1;
        return S_OK;
    }
    ids[0] = DISPID_UNKNOWN;
    return DISP_E_UNKNOWNNAME;
}

/* The function getResource takes the id of a resource of the job and gives its text. */
static HRESULT STDMETHODCALLTYPE res_invoke(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params,
                                            VARIANT *result, EXCEPINFO *excep, UINT *arg_err)
{
    RESOURCES *self = (RESOURCES *)This;
    if (id != 1)
        return DISP_E_MEMBERNOTFOUND;
    if (!(flags & (DISPATCH_METHOD | DISPATCH_PROPERTYGET)))
        return DISP_E_MEMBERNOTFOUND;
    if (params->cArgs != 1)
        return DISP_E_BADPARAMCOUNT;
    VARIANT name;
    VariantInit(&name);
    HRESULT hr = VariantChangeType(&name, &params->rgvarg[0], 0, VT_BSTR);
    if (FAILED(hr))
        return DISP_E_TYPEMISMATCH;
    hr = DISP_E_EXCEPTION;
    for (const XELEM *child = self->Job->First; child; child = child->Next) {
        const XATTR *resource_id = name_is(child, L"resource") ? find_attribute(child, L"id") : nullptr;
        if (resource_id && !_wcsicmp(resource_id->Value, V_BSTR(&name))) {
            if (result) {
                V_VT(result) = VT_BSTR;
                V_BSTR(result) = SysAllocStringLen(child->Text, (UINT)child->TextLength);
            }
            hr = S_OK;
            break;
        }
    }
    VariantClear(&name);
    if (hr == DISP_E_EXCEPTION && excep) {
        memset(excep, 0, sizeof(*excep));
        excep->scode = (SCODE)0x800A0005;
    }
    return hr;
}

static const IDispatchVtbl g_resources_vtbl = {res_query_interface, res_add_ref, res_release, res_count, res_type_info, res_ids, res_invoke};

/* ------------------------------------------------------------------ the run of one job */

typedef struct RUN {
    SCRIPT_ENGINE *Engines[8];
    WCHAR Clsids[8][40];
    int EngineCount;
    BOOL Failed;
    XDOC *Doc;
    JOB *Job;
    IUnknown *Objects[32];
    GUID ObjectClasses[32];
    const XELEM *ObjectElements[32];
    int ObjectCount;
    IDispatch *Resources;
    TYPELIB_REF References[16];
    int ReferenceCount;
} RUN;

static HRESULT start_engine(RUN *run, PCWSTR clsid, SCRIPT_ENGINE **out)
{
    for (int i = 0; i < run->EngineCount; i++)
        if (!_wcsicmp(run->Clsids[i], clsid)) {
            *out = run->Engines[i];
            return S_OK;
        }
    if (run->EngineCount == 8)
        return E_OUTOFMEMORY;
    SCRIPT_ENGINE *engine;
    HRESULT hr = WshCreateEngine(clsid, &engine);
    if (FAILED(hr))
        return hr;
    for (int i = 0; i < run->ObjectCount && SUCCEEDED(hr); i++) {
        const XATTR *id = find_attribute(run->ObjectElements[i], L"id"), *events = find_attribute(run->ObjectElements[i], L"events");
        BOOL source = FALSE;
        if (events)
            parse_bool(events->Value, &source);
        hr = WshEngineAddItem(engine, id->Value, run->Objects[i], SCRIPTITEM_ISVISIBLE | (source ? SCRIPTITEM_ISSOURCE : 0));
    }
    if (SUCCEEDED(hr))
        hr = WshEngineAddItem(engine, L"__WSHResources", (IUnknown *)run->Resources, SCRIPTITEM_ISVISIBLE | SCRIPTITEM_GLOBALMEMBERS);
    for (int i = 0; i < run->ReferenceCount && SUCCEEDED(hr); i++)
        engine->Script->lpVtbl->AddTypeLib(engine->Script, &run->References[i].Id, run->References[i].Major, run->References[i].Minor, 0);
    if (SUCCEEDED(hr))
        hr = engine->Script->lpVtbl->SetScriptState(engine->Script, SCRIPTSTATE_CONNECTED);
    if (FAILED(hr)) {
        WshEngineClose(engine);
        return hr;
    }
    run->Engines[run->EngineCount] = engine;
    wcscpy(run->Clsids[run->EngineCount], clsid);
    *out = engine;
    run->EngineCount++;
    return S_OK;
}

static HRESULT run_runtime(JOB *job)
{
    IArguments2 *arguments = WshHostArguments(g_host.Object);
    for (const XELEM *child = job->Runtime->First; child; child = child->Next) {
        if (name_is(child, L"description"))
            WshArgumentsSetText(arguments, 1, child->Text ? child->Text : L"");
        else if (name_is(child, L"example"))
            WshArgumentsSetText(arguments, 2, child->Text ? child->Text : L"");
        else if (name_is(child, L"usage"))
            WshArgumentsSetText(arguments, 0, child->Text ? child->Text : L"");
        else if (name_is(child, L"named") || name_is(child, L"unnamed")) {
            const XATTR *name = find_attribute(child, L"name"), *help = find_attribute(child, L"helpstring"), *type = find_attribute(child, L"type"),
                        *required = find_attribute(child, L"required"), *many = find_attribute(child, L"many");
            BOOL flag = FALSE, many_flag = FALSE;
            if (required)
                parse_bool(required->Value, &flag);
            if (many)
                parse_bool(many->Value, &many_flag);
            if (name_is(child, L"named")) {
                int kind = !type ? 0 : !_wcsicmp(type->Value, L"string") ? 1 : !_wcsicmp(type->Value, L"boolean") ? 2 : 0;
                WshArgumentsAddRuntimeNamed(arguments, name->Value, help->Value, kind, flag);
            } else {
                int count = required && required->Value[0] >= L'0' && required->Value[0] <= L'9' ? to_int(required->Value) : flag;
                WshArgumentsAddRuntimeUnnamed(arguments, name->Value, help->Value, many_flag, count);
            }
        }
    }
    return S_OK;
}

static HRESULT run_job(XDOC *doc, JOB *job)
{
    RUN run = {};
    HRESULT hr = S_OK;
    run.Doc = doc;
    run.Job = job;
    g_host.InWsf = TRUE;
    WshArgumentsClearRuntime(WshHostArguments(g_host.Object));
    if (job->Runtime) {
        run_runtime(job);
        if (WshArgumentsNamedExists(WshHostArguments(g_host.Object), L"?")) {
            WshHostArguments(g_host.Object)->lpVtbl->ShowUsage(WshHostArguments(g_host.Object));
            return S_OK;
        }
    }
    RESOURCES *resources = allocate(sizeof(*resources));
    if (!resources)
        return E_OUTOFMEMORY;
    resources->Dispatch.lpVtbl = &g_resources_vtbl;
    resources->Refs = 1;
    resources->Job = job->Element;
    run.Resources = &resources->Dispatch;
    /* the objects and the references first, wherever they are in the job. */
    for (const XELEM *child = job->Element->First; child && SUCCEEDED(hr); child = child->Next) {
        if (name_is(child, L"object") && run.ObjectCount < 32) {
            GUID clsid;
            object_class(doc, child, &clsid);
            IUnknown *object = nullptr;
            HRESULT created = CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER, &IID_IUnknown,
                                               (void **)&object);
            if (FAILED(created)) {
                set_error(doc, M_CANNOT_CREATE_OBJECT, 0, 0, nullptr);
                hr = report_error(doc);
                break;
            }
            run.Objects[run.ObjectCount] = object;
            run.ObjectClasses[run.ObjectCount] = clsid;
            run.ObjectElements[run.ObjectCount] = child;
            const XATTR *reference = find_attribute(child, L"reference");
            BOOL flag = FALSE;
            if (reference && parse_bool(reference->Value, &flag) && flag && run.ReferenceCount < 16) {
                TYPELIB_REF ref;
                if (typelib_of_class(&clsid, &ref.Id, &ref.Major, &ref.Minor))
                    run.References[run.ReferenceCount++] = ref;
            }
            run.ObjectCount++;
        } else if (name_is(child, L"reference") && run.ReferenceCount < 16) {
            TYPELIB_REF ref;
            XDOC scratch = {};
            if (typelib_of_reference(&scratch, child, &ref))
                run.References[run.ReferenceCount++] = ref;
            SysFreeString(scratch.ErrorText);
        }
    }
    if (g_host.TimeoutSet)
        WshStartTimer(g_host.Timeout);
    for (JOB_SCRIPT *script = job->Scripts; script && SUCCEEDED(hr); script = script->Next) {
        SCRIPT_ENGINE *engine;
        if (g_host.Quit || InterlockedCompareExchange(&g_host.Pending, 0, 0) == WSH_E_TIMEOUT)
            break;
        HRESULT started = start_engine(&run, script->Clsid, &engine);
        if (FAILED(started)) {
            set_error(doc, M_CANNOT_CREATE_ENGINE, 0, 0, nullptr);
            hr = report_error(doc);
            break;
        }
        g_host.Engine = engine;
        HRESULT parsed = WshEngineParse(engine, script->Text, script->Line, SCRIPTTEXT_HOSTMANAGESSOURCE | SCRIPTTEXT_ISVISIBLE);
        /* an error in the code stops the job, an error in running it only that block. */
        if (FAILED(parsed)) {
            run.Failed = TRUE;
            if (parsed != WSH_E_REPORTED)
                hr = parsed;
            if (parsed != WSH_E_REPORTED || g_host.CompileError)
                break;
        }
    }
    WshDisconnectAll();
    for (int i = 0; i < run.EngineCount; i++)
        WshEngineClose(run.Engines[i]);
    g_host.Engine = nullptr;
    WshStopTimer();
    if (InterlockedCompareExchange(&g_host.Pending, 0, 0) == WSH_E_TIMEOUT)
        WshOnTimeout();
    for (int i = 0; i < run.ObjectCount; i++)
        run.Objects[i]->lpVtbl->Release(run.Objects[i]);
    run.Resources->lpVtbl->Release(run.Resources);
    if (g_host.ScriptFailed || run.Failed || InterlockedCompareExchange(&g_host.Pending, 0, 0) == WSH_E_TIMEOUT)
        hr = FAILED(hr) ? hr : WSH_E_REPORTED;
    return hr;
}

/* ------------------------------------------------------------------ the entry point */

HRESULT WshRunWsf(PCWSTR path, PCWSTR job_id, int argc, PWSTR *argv)
{
    BSTR text = nullptr;
    HRESULT hr = load_file_text(path, &text);
    if (FAILED(hr))
        return WshReportLoadError(hr);
    XDOC doc = {};
    doc.At = text;
    parse_document(&doc);
    JOB jobs[64] = {};
    int count = 0;
    WCHAR directory[MAX_PATH];
    copy_text(directory, MAX_PATH, path);
    PWSTR slash = wcsrchr(directory, L'\\');
    if (slash)
        *slash = 0;
    if (!doc.Error) {
        XELEM *root = doc.Top.First;
        if (root) {
            if (root->Next)
                error_at_element(&doc, M_ONLY_ONE_ROOT, root->Next, nullptr);
            else if (name_is(root, L"job"))
                jobs[count++].Element = root;
            else if (name_is(root, L"package")) {
                for (XELEM *child = root->First; child && count < 64; child = child->Next)
                    if (name_is(child, L"job"))
                        jobs[count++].Element = child;
            } else
                error_at_element(&doc, M_UNEXPECTED_ROOT, root, root->Name);
        }
        if (!doc.Error && !count)
            set_error(&doc, M_NO_JOBS, 0, 0, nullptr);
    }
    for (int i = 0; i < count && !doc.Error; i++) {
        const XATTR *id = find_attribute(jobs[i].Element, L"id");
        jobs[i].Id = id ? id->Value : nullptr;
        if (id)
            for (int j = 0; j < i; j++)
                if (jobs[j].Id && !_wcsicmp(jobs[j].Id, id->Value))
                    error_at_element(&doc, M_DUPLICATE_ID, jobs[i].Element, id->Value);
        if (!doc.Error)
            check_job(&doc, &jobs[i], directory);
    }
    if (doc.Error) {
        hr = report_error(&doc);
    } else {
        JOB *selected = nullptr;
        if (!job_id)
            selected = &jobs[0];
        else
            for (int i = 0; i < count; i++)
                if (jobs[i].Id && !_wcsicmp(jobs[i].Id, job_id))
                    selected = &jobs[i];
        if (!selected) {
            WshPrintF(RES_BASE + 21, 3319, job_id);
            hr = WSH_E_REPORTED;
        } else
            hr = run_job(&doc, selected);
    }
    for (int i = 0; i < count; i++)
        for (JOB_SCRIPT *script = jobs[i].Scripts; script;) {
            JOB_SCRIPT *next = script->Next;
            SysFreeString(script->Text);
            release(script);
            script = next;
        }
    free_element(&doc.Top);
    SysFreeString(doc.ErrorText);
    SysFreeString(text);
    return hr;
}
