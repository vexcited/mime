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
                       BOOL properties, MSI_TEXT *out)
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
    if (*token == L'#' || *token == L'!' || *token == L'$')
        return ERROR_CALL_NOT_IMPLEMENTED;
    if (*token == L'%') {
        DWORD capacity = GetEnvironmentVariableW(token + 1, NULL, 0);
        if (!capacity)
            return ERROR_SUCCESS;
        PWSTR value = MimeMsiAllocate((SIZE_T)capacity * sizeof(WCHAR));
        if (!value)
            return ERROR_NOT_ENOUGH_MEMORY;
        DWORD length = GetEnvironmentVariableW(token + 1, value, capacity);
        UINT error = length >= capacity ? ERROR_MORE_DATA : append_text(out, value, length);
        MimeMsiFree(value);
        return error;
    }
    PCWSTR value = MimeMsiGetProperty(package, token);
    return append_text(out, value, wcslen(value));
}

static UINT format_part(MIME_MSI_PACKAGE *package, const MIME_MSI_RECORD *record, PCWSTR *cursor,
                        BOOL properties, ULONG depth, MSI_TEXT *out)
{
    if (depth > 32)
        return ERROR_INVALID_PARAMETER;
    while (**cursor && (!depth || **cursor != L']')) {
        if (**cursor != L'[') {
            if (properties && **cursor == L'{' && wcschr(*cursor, L'['))
                return ERROR_CALL_NOT_IMPLEMENTED;
            UINT error = append_text(out, (*cursor)++, 1);
            if (error)
                return error;
            continue;
        }
        (*cursor)++;
        MSI_TEXT token = {0};
        UINT error = append_text(&token, L"", 0);
        if (!error)
            error = format_part(package, record, cursor, properties, depth + 1, &token);
        if (!error && **cursor == L']') {
            (*cursor)++;
            error = substitute(package, record, token.Text, properties, out);
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
    MSI_TEXT numeric = {0}, formatted = {0};
    WCHAR number[16];
    PCWSTR template = field_text(&record->Fields[0], number);
    UINT error = append_text(&numeric, L"", 0);
    if (!error && !*template) {
        for (UINT i = 1; i <= record->Count && !error; i++) {
            WCHAR label[24];
            _snwprintf(label, RTL_NUMBER_OF(label), i == 1 ? L"%u: " : L" %u: ", i);
            error = append_text(&numeric, label, wcslen(label));
            PCWSTR value = field_text(&record->Fields[i], number);
            if (!error)
                error = append_text(&numeric, value, wcslen(value));
        }
    } else if (!error)
        error = format_part(package, record, &template, FALSE, 0, &numeric);
    if (!error && package) {
        template = numeric.Text;
        error = append_text(&formatted, L"", 0);
        if (!error)
            error = format_part(package, record, &template, TRUE, 0, &formatted);
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
    if (!out || !size)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockShared(&MimeMsiLock);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(install, MIME_MSI_KIND_PACKAGE);
    UINT error = !record || (install && !package) ? ERROR_INVALID_HANDLE : ERROR_SUCCESS;
    PWSTR text = NULL;
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
