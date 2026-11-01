#include "msi_private.h"

typedef struct MSI_TEXT {
    PWSTR Text;
    SIZE_T Length, Capacity;
} MSI_TEXT;

static UINT append_text(MSI_TEXT *text, PCWSTR value, SIZE_T length)
{
    if (length > (SIZE_MAX / sizeof(WCHAR)) - text->Length - 1 || length > UINT32_MAX - text->Length - 1)
        return ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T required = text->Length + length + 1;
    if (required > text->Capacity) {
        SIZE_T capacity = required < 64 ? 64 : required;
        if (text->Capacity <= SIZE_MAX / (2 * sizeof(WCHAR)) && capacity < text->Capacity * 2)
            capacity = text->Capacity * 2;
        PWSTR buffer = MimeMsiAllocate(capacity * sizeof(WCHAR));
        if (!buffer)
            return ERROR_NOT_ENOUGH_MEMORY;
        if (text->Length)
            memcpy(buffer, text->Text, text->Length * sizeof(WCHAR));
        MimeMsiFree(text->Text);
        text->Text = buffer;
        text->Capacity = capacity;
    }
    if (length)
        memcpy(text->Text + text->Length, value, length * sizeof(WCHAR));
    text->Length += length;
    text->Text[text->Length] = 0;
    return ERROR_SUCCESS;
}

static PCWSTR field_text(const MIME_MSI_FIELD *field, WCHAR number[16])
{
    if (field->Kind == FIELD_STRING)
        return field->String;
    if (field->Kind == FIELD_INTEGER) {
        _snwprintf(number, 16, L"%d", field->Integer);
        return number;
    }
    return L"";
}

static UINT substitute(MIME_MSI_PACKAGE *package, const MIME_MSI_RECORD *record, PCWSTR token,
                       BOOL properties, MSI_TEXT *out, BOOL *empty)
{
    WCHAR number[16];
    if (!properties) {
        ULONG index = 0;
        PCWSTR p = token;
        while (*p >= L'0' && *p <= L'9' && index <= (UINT32_MAX - 9) / 10)
            index = index * 10 + *p++ - L'0';
        if (*token && !*p) {
            PCWSTR value = index && index <= record->Count ? field_text(&record->Fields[index], number) : L"";
            return append_text(out, value, wcslen(value));
        }
        UINT error = append_text(out, L"[", 1);
        if (!error)
            error = append_text(out, token, wcslen(token));
        return error ? error : append_text(out, L"]", 1);
    }
    if (*token == L'\\')
        return append_text(out, token + 1, token[1] ? 1 : 0);
    if (!wcscmp(token, L"~"))
        return append_text(out, L"", 1);
    if (*token == L'#' || *token == L'!' || *token == L'$') {
        /* [#file] and [!file] give a file's path, [$component] its directory. */
        PWSTR path = nullptr;
        UINT error = *token == L'$' ? MimeMsiComponentDirectory(package, token + 1, &path) :
                                      MimeMsiFileTargetPath(package, token + 1, &path);
        if (error == ERROR_INSTALL_PACKAGE_INVALID || error == ERROR_DIRECTORY) {
            *empty = TRUE;
            return ERROR_SUCCESS;
        }
        if (error)
            return error;
        error = append_text(out, path, wcslen(path));
        MimeMsiFree(path);
        return error;
    }
    if (*token == L'%') {
        DWORD capacity = GetEnvironmentVariableW(token + 1, nullptr, 0);
        if (!capacity) {
            *empty = TRUE;
            return ERROR_SUCCESS;
        }
        PWSTR value = MimeMsiAllocate((SIZE_T)capacity * sizeof(WCHAR));
        if (!value)
            return ERROR_NOT_ENOUGH_MEMORY;
        DWORD length = GetEnvironmentVariableW(token + 1, value, capacity);
        UINT error = length >= capacity ? ERROR_MORE_DATA : append_text(out, value, length);
        MimeMsiFree(value);
        return error;
    }
    PCWSTR value = MimeMsiGetProperty(package, token);
    if (!*value)
        *empty = TRUE;
    return append_text(out, value, wcslen(value));
}

static UINT format_part(MIME_MSI_PACKAGE *package, const MIME_MSI_RECORD *record, PCWSTR *cursor,
                        BOOL properties, ULONG depth, WCHAR end, MSI_TEXT *out, BOOL *empty)
{
    if (depth > 32)
        return ERROR_INVALID_PARAMETER;
    while (**cursor && **cursor != end) {
        if (properties && **cursor == L'{') {
            /* a {...} group is kept only when all its properties are set. */
            PCWSTR close = wcschr(*cursor, L'}'), open = wcschr(*cursor, L'[');
            if (close && open && open < close) {
                (*cursor)++;
                MSI_TEXT group = {};
                BOOL group_empty = FALSE;
                UINT error = append_text(&group, L"", 0);
                if (!error)
                    error = format_part(package, record, cursor, properties, depth + 1, L'}', &group, &group_empty);
                if (!error && **cursor == L'}')
                    (*cursor)++;
                if (!error && !group_empty)
                    error = append_text(out, group.Text, group.Length);
                MimeMsiFree(group.Text);
                if (error)
                    return error;
                continue;
            }
        }
        if (**cursor != L'[') {
            UINT error = append_text(out, (*cursor)++, 1);
            if (error)
                return error;
            continue;
        }
        (*cursor)++;
        MSI_TEXT token = {};
        BOOL ignored = FALSE;
        UINT error = append_text(&token, L"", 0);
        if (!error)
            error = format_part(package, record, cursor, properties, depth + 1, L']', &token, &ignored);
        if (!error && **cursor == L']') {
            (*cursor)++;
            error = substitute(package, record, token.Text, properties, out, empty);
        } else if (!error) {
            error = append_text(out, L"[", 1);
            if (!error)
                error = append_text(out, token.Text, token.Length);
        }
        MimeMsiFree(token.Text);
        if (error)
            return error;
    }
    return ERROR_SUCCESS;
}

UINT MimeMsiFormatRecord(MIME_MSI_PACKAGE *package, const MIME_MSI_RECORD *record, PWSTR *out, DWORD *length)
{
    MSI_TEXT numeric = {}, formatted = {};
    WCHAR number[16];
    BOOL unused = FALSE;
    PCWSTR template = field_text(&record->Fields[0], number);
    UINT error = append_text(&numeric, L"", 0);
    if (!error && !*template) {
        for (UINT i = 1; i <= record->Count && !error; i++) {
            WCHAR label[24];
            _snwprintf(label, RTL_NUMBER_OF(label), L"%u: ", i);
            error = append_text(&numeric, label, wcslen(label));
            PCWSTR value = field_text(&record->Fields[i], number);
            if (!error)
                error = append_text(&numeric, value, wcslen(value));
            /* every field ends with a space, callers strip the last one. */
            if (!error)
                error = append_text(&numeric, L" ", 1);
        }
    } else if (!error)
        error = format_part(package, record, &template, FALSE, 0, 0, &numeric, &unused);
    if (!error && package) {
        template = numeric.Text;
        error = append_text(&formatted, L"", 0);
        if (!error)
            error = format_part(package, record, &template, TRUE, 0, 0, &formatted, &unused);
        MimeMsiFree(numeric.Text);
        numeric = formatted;
    }
    if (error) {
        MimeMsiFree(numeric.Text);
        return error;
    }
    *out = numeric.Text;
    *length = (DWORD)numeric.Length;
    return ERROR_SUCCESS;
}

UINT WINAPI MsiFormatRecordW(MSIHANDLE install, MSIHANDLE handle, LPWSTR out, LPDWORD size)
{
    MIME_REMOTE(PROXY_FormatRecordW, UINT, MIME_ARG(install), MIME_ARG(handle), MIME_ARG(out), MIME_ARG(size));
    if (!out || !size)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(install, MIME_MSI_KIND_PACKAGE);
    UINT error = !record || (install && !package) ? ERROR_INVALID_HANDLE : ERROR_SUCCESS;
    PWSTR text = nullptr;
    DWORD length;
    if (!error)
        error = MimeMsiFormatRecord(package, record, &text, &length);
    if (!error) {
        DWORD capacity = *size;
        *size = length;
        if (capacity) {
            DWORD copied = length < capacity ? length : capacity - 1;
            memcpy(out, text, copied * sizeof(WCHAR));
            out[copied] = 0;
        }
        if (length >= capacity)
            error = ERROR_MORE_DATA;
    }
    MimeMsiFree(text);
    ReleaseSRWLockShared(&MimeMsiLock);
    return error;
}
