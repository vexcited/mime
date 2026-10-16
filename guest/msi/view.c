#include "msi_private.h"

enum { TOKEN_END, TOKEN_NAME, TOKEN_STRING, TOKEN_NUMBER, TOKEN_SYMBOL, TOKEN_INVALID };
typedef struct SQL_TOKEN {
    PCWSTR Next;
    ULONG Kind;
    WCHAR Text[256];
} SQL_TOKEN;

enum { COMPARE_EQ, COMPARE_NE, COMPARE_LT, COMPARE_LE, COMPARE_GT, COMPARE_GE, COMPARE_NULL, COMPARE_NOT_NULL };
typedef struct MSI_FILTER {
    ULONG Column, Operation, Parameter;
    MIME_MSI_FIELD Value;
} MSI_FILTER;

typedef struct MSI_VIEW {
    MIME_MSI_OBJECT Object;
    MIME_MSI_DATABASE *Database;
    MIME_MSI_TABLE *Table;
    ULONG Columns[64], Count;
    MSI_FILTER Filters[32];
    ULONG FilterCount, Parameters;
    ULONG *Rows, RowCount, Position, Revision;
    BOOL Executed;
} MSI_VIEW;

static BOOL name_character(WCHAR ch)
{
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
           (ch >= L'0' && ch <= L'9') || ch == L'_';
}

static void next_token(SQL_TOKEN *token)
{
    PCWSTR p = token->Next;
    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n')
        p++;
    ULONG length = 0;
    token->Kind = TOKEN_END;
    if (*p == L'`' || *p == L'\'') {
        WCHAR quote = *p++;
        token->Kind = quote == L'`' ? TOKEN_NAME : TOKEN_STRING;
        while (*p && *p != quote && length < RTL_NUMBER_OF(token->Text) - 1)
            token->Text[length++] = *p++;
        if (*p != quote)
            token->Kind = TOKEN_INVALID;
        else
            p++;
    } else if ((*p >= L'0' && *p <= L'9') || *p == L'-') {
        token->Kind = TOKEN_NUMBER;
        token->Text[length++] = *p++;
        while (*p >= L'0' && *p <= L'9' && length < RTL_NUMBER_OF(token->Text) - 1)
            token->Text[length++] = *p++;
    } else if (name_character(*p)) {
        token->Kind = TOKEN_NAME;
        while (name_character(*p) && length < RTL_NUMBER_OF(token->Text) - 1)
            token->Text[length++] = *p++;
    } else if (*p) {
        token->Kind = TOKEN_SYMBOL;
        token->Text[length++] = *p++;
        if ((token->Text[0] == L'<' && (*p == L'>' || *p == L'=')) ||
            (token->Text[0] == L'>' && *p == L'='))
            token->Text[length++] = *p++;
    }
    token->Text[length] = 0;
    token->Next = p;
}

static BOOL take(SQL_TOKEN *token, PCWSTR text)
{
    if (token->Kind == TOKEN_INVALID || token->Kind == TOKEN_STRING || _wcsicmp(token->Text, text))
        return FALSE;
    next_token(token);
    return TRUE;
}

static ULONG find_column(const MIME_MSI_TABLE *table, PCWSTR name)
{
    for (ULONG i = 0; i < table->Count; i++)
        if (!wcscmp(table->Columns[i].Name, name))
            return i;
    return UINT32_MAX;
}

static void destroy_view(MIME_MSI_OBJECT *object)
{
    MSI_VIEW *view = (void *)object;
    for (ULONG i = 0; i < view->FilterCount; i++)
        MimeMsiFree(view->Filters[i].Value.String);
    MimeMsiFree(view->Rows);
    if (view->Database)
        MimeMsiRelease(&view->Database->Object);
    MimeMsiFree(view);
}

static UINT parse_view(MSI_VIEW *view, PCWSTR query)
{
    SQL_TOKEN token = {.Next = query};
    next_token(&token);
    if (!take(&token, L"SELECT"))
        return ERROR_BAD_QUERY_SYNTAX;
    WCHAR names[64][64];
    BOOL all = take(&token, L"*");
    if (!all) {
        do {
            if (token.Kind != TOKEN_NAME || view->Count == 64 || wcslen(token.Text) >= 64)
                return ERROR_BAD_QUERY_SYNTAX;
            wcscpy(names[view->Count++], token.Text);
            next_token(&token);
        } while (take(&token, L","));
    }
    if (!take(&token, L"FROM") || token.Kind != TOKEN_NAME)
        return ERROR_BAD_QUERY_SYNTAX;
    UINT error = MimeMsiLoadTable(view->Database, token.Text, &view->Table);
    if (error)
        return error;
    next_token(&token);
    if (all)
        view->Count = view->Table->Count;
    for (ULONG i = 0; i < view->Count; i++) {
        view->Columns[i] = all ? i : find_column(view->Table, names[i]);
        if (view->Columns[i] == UINT32_MAX)
            return ERROR_BAD_QUERY_SYNTAX;
    }
    if (take(&token, L"WHERE")) {
        do {
            if (token.Kind != TOKEN_NAME || view->FilterCount == 32)
                return ERROR_BAD_QUERY_SYNTAX;
            MSI_FILTER *filter = &view->Filters[view->FilterCount++];
            filter->Column = find_column(view->Table, token.Text);
            if (filter->Column == UINT32_MAX)
                return ERROR_BAD_QUERY_SYNTAX;
            next_token(&token);
            if (take(&token, L"IS")) {
                filter->Operation = take(&token, L"NOT") ? COMPARE_NOT_NULL : COMPARE_NULL;
                if (!take(&token, L"NULL"))
                    return ERROR_BAD_QUERY_SYNTAX;
                continue;
            }
            static const PCWSTR operators[] = {L"=", L"<>", L"<", L"<=", L">", L">="};
            ULONG op;
            for (op = 0; op < RTL_NUMBER_OF(operators); op++)
                if (take(&token, operators[op]))
                    break;
            if (op == RTL_NUMBER_OF(operators))
                return ERROR_BAD_QUERY_SYNTAX;
            filter->Operation = op;
            USHORT type = view->Table->Columns[filter->Column].Type;
            if ((type & 0x0fff) == 0x0900 || ((type & 0x0c00) == 0x0c00 && op > COMPARE_NE))
                return ERROR_BAD_QUERY_SYNTAX;
            if (take(&token, L"?"))
                filter->Parameter = ++view->Parameters;
            else if (token.Kind == TOKEN_STRING || token.Kind == TOKEN_NUMBER) {
                error = MimeMsiFieldString(&filter->Value, token.Text);
                if (error)
                    return error;
                if (token.Kind == TOKEN_NUMBER) {
                    int number = MimeMsiFieldGetInteger(&filter->Value);
                    if (number == MSI_NULL_INTEGER)
                        return ERROR_BAD_QUERY_SYNTAX;
                    MimeMsiFieldInteger(&filter->Value, number);
                }
                next_token(&token);
            } else
                return ERROR_BAD_QUERY_SYNTAX;
        } while (take(&token, L"AND"));
    }
    return token.Kind == TOKEN_END ? ERROR_SUCCESS : ERROR_BAD_QUERY_SYNTAX;
}

UINT WINAPI MsiDatabaseOpenViewW(MSIHANDLE handle, LPCWSTR query, MSIHANDLE *out)
{
    if (!query || !out)
        return ERROR_INVALID_PARAMETER;
    *out = 0;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_DATABASE *database = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
    UINT error = database ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
    MSI_VIEW *view = nullptr;
    if (!error) {
        view = MimeMsiAllocate(sizeof(*view));
        if (!view)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error) {
        view->Object.Kind = MIME_MSI_KIND_VIEW;
        view->Object.Destroy = destroy_view;
        view->Database = database;
        database->Object.References++;
        error = parse_view(view, query);
        if (!error && !(*out = MimeMsiAddHandle(&view->Object)))
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (error && view)
        destroy_view(&view->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

static BOOL matches(const MSI_VIEW *view, ULONG row, const MSI_FILTER *filter, const MIME_MSI_FIELD *value)
{
    ULONG raw = MimeMsiCell(view->Table, row, filter->Column);
    if (filter->Operation == COMPARE_NULL)
        return !raw;
    if (filter->Operation == COMPARE_NOT_NULL)
        return !!raw;
    if (!raw || value->Kind == FIELD_NULL)
        return FALSE;
    const MIME_MSI_COLUMN *column = &view->Table->Columns[filter->Column];
    int order;
    if ((column->Type & 0x0c00) == 0x0c00) {
        PCWSTR actual = view->Database->Strings[raw];
        WCHAR number[16];
        PCWSTR expected = value->String;
        if (value->Kind == FIELD_INTEGER) {
            _snwprintf(number, RTL_NUMBER_OF(number), L"%d", value->Integer);
            expected = number;
        }
        order = wcscmp(actual ? actual : L"", expected ? expected : L"");
    } else {
        int actual = column->Width == 2 ? (int)raw - 0x8000 : (int)(raw ^ 0x80000000u);
        int expected = MimeMsiFieldGetInteger(value);
        if (expected == MSI_NULL_INTEGER)
            return FALSE;
        order = actual < expected ? -1 : actual > expected;
    }
    switch (filter->Operation) {
    case COMPARE_EQ: return order == 0;
    case COMPARE_NE: return order != 0;
    case COMPARE_LT: return order < 0;
    case COMPARE_LE: return order <= 0;
    case COMPARE_GT: return order > 0;
    case COMPARE_GE: return order >= 0;
    default: return FALSE;
    }
}

UINT WINAPI MsiViewExecute(MSIHANDLE handle, MSIHANDLE parameters)
{
    AcquireSRWLockExclusive(&MimeMsiLock);
    MSI_VIEW *view = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_VIEW);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(parameters, MIME_MSI_KIND_RECORD);
    UINT error = !view || (parameters && !record) ? ERROR_INVALID_HANDLE : ERROR_SUCCESS;
    if (!error && view->Parameters && (!record || record->Count < view->Parameters))
        error = ERROR_INVALID_PARAMETER;
    if (!error) {
        MimeMsiFree(view->Rows);
        view->Rows = nullptr;
        view->Executed = FALSE;
        view->RowCount = view->Position = 0;
        SIZE_T bytes = (SIZE_T)view->Table->Rows * sizeof(ULONG);
        if (bytes / sizeof(ULONG) != view->Table->Rows)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else
            view->Rows = MimeMsiAllocate(bytes ? bytes : 1);
        if (!view->Rows)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error) {
        for (ULONG row = 0; row < view->Table->Rows; row++) {
            ULONG i;
            for (i = 0; i < view->FilterCount; i++) {
                MSI_FILTER *filter = &view->Filters[i];
                const MIME_MSI_FIELD *value = filter->Parameter ? &record->Fields[filter->Parameter] : &filter->Value;
                if (!matches(view, row, filter, value))
                    break;
            }
            if (i == view->FilterCount)
                view->Rows[view->RowCount++] = row;
        }
        view->Executed = TRUE;
        view->Revision = view->Database->Revision;
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiViewFetch(MSIHANDLE handle, MSIHANDLE *out)
{
    if (!out)
        return ERROR_INVALID_PARAMETER;
    *out = 0;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MSI_VIEW *view = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_VIEW);
    UINT error = !view ? ERROR_INVALID_HANDLE : !view->Executed || view->Revision != view->Database->Revision ? ERROR_FUNCTION_FAILED :
                 view->Position == view->RowCount ? ERROR_NO_MORE_ITEMS : ERROR_SUCCESS;
    MIME_MSI_RECORD *record = nullptr;
    if (!error && !(record = MimeMsiRecordCreate(view->Count)))
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error) {
        ULONG row = view->Rows[view->Position];
        for (ULONG i = 0; i < view->Count && !error; i++) {
            ULONG index = view->Columns[i];
            const MIME_MSI_COLUMN *column = &view->Table->Columns[index];
            ULONG raw = MimeMsiCell(view->Table, row, index);
            if (!raw)
                continue;
            if ((column->Type & 0x0c00) == 0x0c00)
                error = MimeMsiFieldString(&record->Fields[i + 1], view->Database->Strings[raw]);
            else if ((column->Type & 0x0fff) == 0x0900)
                error = ERROR_CALL_NOT_IMPLEMENTED;
            else
                MimeMsiFieldInteger(&record->Fields[i + 1], column->Width == 2 ? (int)raw - 0x8000 : (int)(raw ^ 0x80000000u));
        }
        if (!error && !(*out = MimeMsiAddHandle(&record->Object)))
            error = ERROR_NOT_ENOUGH_MEMORY;
        if (!error)
            view->Position++;
    }
    if (error && record)
        record->Object.Destroy(&record->Object);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

UINT WINAPI MsiViewClose(MSIHANDLE handle)
{
    AcquireSRWLockExclusive(&MimeMsiLock);
    MSI_VIEW *view = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_VIEW);
    if (view) {
        MimeMsiFree(view->Rows);
        view->Rows = nullptr;
        view->RowCount = view->Position = 0;
        view->Executed = FALSE;
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return view ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}
