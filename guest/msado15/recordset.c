/*
 * the ADO recordset, the fields and the field. A recordset reads one result of a statement, forward only: the row it is
 * on is kept as variants, MoveNext fetches the next, and NextRecordset goes on to the next result of the batch.
 */
#include "adoprivate.h"

#ifndef SQL_C_SLONG
#define SQL_C_SLONG (-16)
#define SQL_C_SSHORT (-15)
#define SQL_C_UTINYINT (-28)
#endif
#ifndef SQL_NO_TOTAL
#define SQL_NO_TOTAL (-4)
#endif
#define SQL_SS_TIME2 (-154)
#define SQL_SS_TIMESTAMPOFFSET (-155)
#define SQL_SS_XML (-152)
#define SQL_SS_VARIANT (-150)

#define RECORDSET_OF(p) ((RECORDSET *)(p))

DISPATCH_STUBS(recordset, ADORecordset)
DISPATCH_STUBS(fields, ADOFields)
DISPATCH_STUBS(field, ADOField)

static BOOL ok(SQLRETURN result)
{
    return result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO;
}

/* ------------------------------------------------------------------ the columns */

static LONG ado_type(SQLSMALLINT type, SQLULEN size)
{
    switch (type) {
    case SQL_CHAR: return 129;
    case SQL_VARCHAR: return size > 8000 ? 201 : 200;
    case SQL_LONGVARCHAR: return 201;
    case SQL_WCHAR: return 130;
    case SQL_WVARCHAR: return size > 4000 ? 203 : 202;
    case SQL_WLONGVARCHAR: return 203;
    case SQL_DECIMAL:
    case SQL_NUMERIC: return 131;
    case SQL_SMALLINT: return 2;
    case SQL_INTEGER: return 3;
    case SQL_REAL: return 4;
    case SQL_FLOAT:
    case SQL_DOUBLE: return 5;
    case SQL_BIGINT: return 20;
    case SQL_TINYINT: return 17;
    case SQL_BIT: return 11;
    case SQL_BINARY: return 128;
    case SQL_VARBINARY: return size > 8000 ? 205 : 204;
    case SQL_LONGVARBINARY: return 205;
    case SQL_TYPE_DATE: return 133;
    case SQL_SS_TIME2:
    case SQL_TYPE_TIME: return 134;
    case SQL_TYPE_TIMESTAMP: return 135;
    case SQL_GUID: return 72;
    case SQL_SS_XML: return 203;
    case SQL_SS_VARIANT: return 12;
    default: return 202;
    }
}

static void columns_free(RECORDSET *rs)
{
    for (LONG i = 0; i < rs->ColumnCount; i++) {
        SysFreeString(rs->Columns[i].Name);
        if (rs->Row)
            VariantClear(&rs->Row[i]);
    }
    HeapFree(GetProcessHeap(), 0, rs->Columns);
    HeapFree(GetProcessHeap(), 0, rs->Row);
    rs->Columns = nullptr;
    rs->Row = nullptr;
    rs->ColumnCount = 0;
}

static BOOL describe_columns(RECORDSET *rs)
{
    SQLSMALLINT count = 0;
    if (!ok(SQLNumResultCols(rs->Stmt->Handle, &count)) || !count)
        return TRUE;
    rs->Columns = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(COLUMN));
    rs->Row = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(VARIANT));
    if (!rs->Columns || !rs->Row) {
        columns_free(rs);
        return FALSE;
    }
    rs->ColumnCount = count;
    for (SQLSMALLINT i = 0; i < count; i++) {
        WCHAR name[256] = L"";
        SQLSMALLINT name_length;
        COLUMN *column = &rs->Columns[i];
        VariantInit(&rs->Row[i]);
        SQLDescribeColW(rs->Stmt->Handle, i + 1, name, RTL_NUMBER_OF(name), &name_length, &column->SqlType, &column->Size,
                        &column->Digits, &column->Nullable);
        column->Name = AdoSysString(name);
        column->AdoType = ado_type(column->SqlType, column->Size);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ the values */

/* reads the whole of a column as text or bytes, whatever its length. */
static BYTE *read_column(SQLHSTMT handle, SQLUSMALLINT column, SQLSMALLINT c_type, SIZE_T *size, BOOL *null)
{
    SIZE_T terminator = c_type == SQL_C_WCHAR ? sizeof(WCHAR) : 0, capacity = 4096, used = 0;
    BYTE *buffer = HeapAlloc(GetProcessHeap(), 0, capacity);
    *null = FALSE;
    *size = 0;
    while (buffer) {
        SQLLEN indicator = 0;
        SIZE_T available = capacity - used;
        SQLRETURN result = SQLGetData(handle, column, c_type, buffer + used, (SQLLEN)available, &indicator);
        if (result == SQL_NO_DATA)
            break;
        if (!ok(result)) {
            HeapFree(GetProcessHeap(), 0, buffer);
            return nullptr;
        }
        if (indicator == SQL_NULL_DATA) {
            *null = TRUE;
            break;
        }
        BOOL complete = indicator != SQL_NO_TOTAL && (SIZE_T)indicator + terminator <= available;
        used += complete ? (SIZE_T)indicator : available - terminator;
        if (complete)
            break;
        capacity *= 2;
        BYTE *grown = HeapReAlloc(GetProcessHeap(), 0, buffer, capacity);
        if (!grown) {
            HeapFree(GetProcessHeap(), 0, buffer);
            return nullptr;
        }
        buffer = grown;
    }
    *size = used;
    return buffer;
}

static HRESULT text_value(SQLHSTMT handle, SQLUSMALLINT column, VARIANT *value)
{
    SIZE_T size;
    BOOL null;
    BYTE *bytes = read_column(handle, column, SQL_C_WCHAR, &size, &null);
    if (!bytes)
        return E_OUTOFMEMORY;
    if (null) {
        value->vt = VT_NULL;
    } else {
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocStringLen((WCHAR *)bytes, (UINT)(size / sizeof(WCHAR)));
    }
    HeapFree(GetProcessHeap(), 0, bytes);
    return value->vt == VT_BSTR && !value->bstrVal ? E_OUTOFMEMORY : S_OK;
}

static HRESULT fixed_value(SQLHSTMT handle, SQLUSMALLINT column, SQLSMALLINT c_type, void *buffer, SQLLEN size, BOOL *null)
{
    SQLLEN indicator = 0;
    SQLRETURN result = SQLGetData(handle, column, c_type, buffer, size, &indicator);
    *null = indicator == SQL_NULL_DATA;
    return ok(result) ? S_OK : E_FAIL;
}

static HRESULT column_value(RECORDSET *rs, LONG index, VARIANT *value)
{
    SQLHSTMT handle = rs->Stmt->Handle;
    SQLUSMALLINT column = (SQLUSMALLINT)(index + 1);
    const COLUMN *info = &rs->Columns[index];
    BOOL null = FALSE;
    HRESULT result = S_OK;
    VariantInit(value);
    switch (info->SqlType) {
    case SQL_INTEGER: {
        LONG number = 0;
        result = fixed_value(handle, column, SQL_C_SLONG, &number, sizeof(number), &null);
        value->vt = VT_I4;
        value->lVal = number;
        break;
    }
    case SQL_SMALLINT: {
        SHORT number = 0;
        result = fixed_value(handle, column, SQL_C_SSHORT, &number, sizeof(number), &null);
        value->vt = VT_I2;
        value->iVal = number;
        break;
    }
    case SQL_TINYINT: {
        BYTE number = 0;
        result = fixed_value(handle, column, SQL_C_UTINYINT, &number, sizeof(number), &null);
        value->vt = VT_UI1;
        value->bVal = number;
        break;
    }
    case SQL_BIGINT: {
        LONG64 number = 0;
        result = fixed_value(handle, column, SQL_C_SBIGINT, &number, sizeof(number), &null);
        value->vt = VT_I8;
        value->llVal = number;
        break;
    }
    case SQL_BIT: {
        BYTE flag = 0;
        result = fixed_value(handle, column, SQL_C_BIT, &flag, sizeof(flag), &null);
        value->vt = VT_BOOL;
        value->boolVal = flag ? VARIANT_TRUE : 0;
        break;
    }
    case SQL_REAL: {
        float number = 0;
        result = fixed_value(handle, column, SQL_C_FLOAT, &number, sizeof(number), &null);
        value->vt = VT_R4;
        value->fltVal = number;
        break;
    }
    case SQL_FLOAT:
    case SQL_DOUBLE: {
        double number = 0;
        result = fixed_value(handle, column, SQL_C_DOUBLE, &number, sizeof(number), &null);
        value->vt = VT_R8;
        value->dblVal = number;
        break;
    }
    case SQL_TYPE_DATE:
    case SQL_TYPE_TIME:
    case SQL_SS_TIME2:
    case SQL_TYPE_TIMESTAMP: {
        TIMESTAMP_STRUCT stamp = {};
        result = fixed_value(handle, column, SQL_C_TYPE_TIMESTAMP, &stamp, sizeof(stamp), &null);
        SYSTEMTIME time = {.wYear = stamp.year, .wMonth = stamp.month, .wDay = stamp.day, .wHour = stamp.hour,
                           .wMinute = stamp.minute, .wSecond = stamp.second, .wMilliseconds = stamp.fraction / 1000000};
        /* a time has no date, which variants count from the day before the first of january 1900. */
        if (info->SqlType == SQL_TYPE_TIME || info->SqlType == SQL_SS_TIME2) {
            time.wYear = 1899;
            time.wMonth = 12;
            time.wDay = 30;
        }
        double date = 0;
        if (!null && !SystemTimeToVariantTime(&time, &date))
            result = E_FAIL;
        value->vt = VT_DATE;
        value->date = date;
        break;
    }
    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY: {
        SIZE_T size;
        BYTE *bytes = read_column(handle, column, SQL_C_BINARY, &size, &null);
        if (!bytes) {
            result = E_OUTOFMEMORY;
            break;
        }
        if (!null) {
            SAFEARRAY *array = SafeArrayCreateVector(VT_UI1, 0, (ULONG)size);
            void *data;
            if (array && SUCCEEDED(SafeArrayAccessData(array, &data))) {
                memcpy(data, bytes, size);
                SafeArrayUnaccessData(array);
                value->vt = VT_ARRAY | VT_UI1;
                value->parray = array;
            } else {
                result = E_OUTOFMEMORY;
            }
        }
        HeapFree(GetProcessHeap(), 0, bytes);
        break;
    }
    case SQL_DECIMAL:
    case SQL_NUMERIC: {
        VARIANT text;
        VariantInit(&text);
        result = text_value(handle, column, &text);
        if (SUCCEEDED(result) && text.vt == VT_BSTR) {
            /* the text has a point whatever the language, so it is read by the language of the United States. */
            result = VariantChangeTypeEx(value, &text, 0x0409, 0, VT_DECIMAL);
        } else {
            null = text.vt == VT_NULL;
        }
        VariantClear(&text);
        break;
    }
    case SQL_GUID: {
        result = text_value(handle, column, value);
        if (SUCCEEDED(result) && value->vt == VT_BSTR) {
            UINT length = SysStringLen(value->bstrVal);
            BSTR braced = SysAllocStringLen(nullptr, length + 2);
            if (braced) {
                braced[0] = L'{';
                memcpy(braced + 1, value->bstrVal, length * sizeof(WCHAR));
                braced[length + 1] = L'}';
                braced[length + 2] = 0;
                SysFreeString(value->bstrVal);
                value->bstrVal = braced;
            }
        }
        return result;
    }
    default:
        return text_value(handle, column, value);
    }
    if (SUCCEEDED(result) && null) {
        VariantClear(value);
        value->vt = VT_NULL;
    }
    return result;
}

/* reads the row the statement is on. */
static HRESULT load_row(RECORDSET *rs)
{
    for (LONG i = 0; i < rs->ColumnCount; i++) {
        VariantClear(&rs->Row[i]);
        HRESULT result = column_value(rs, i, &rs->Row[i]);
        if (FAILED(result))
            return result;
    }
    return S_OK;
}

/* moves to the next row, and leaves the end of the results if there is none. */
static HRESULT fetch(RECORDSET *rs)
{
    SQLRETURN result = SQLFetch(rs->Stmt->Handle);
    if (result == SQL_NO_DATA) {
        rs->HasRow = FALSE;
        rs->Eof = TRUE;
        return S_OK;
    }
    if (!ok(result)) {
        ErrorListAddDiagnostics(rs->Stmt->Connection->Errors, SQL_HANDLE_STMT, rs->Stmt->Handle, DB_E_ERRORSINCOMMAND);
        return DB_E_ERRORSINCOMMAND;
    }
    rs->HasRow = TRUE;
    rs->Position++;
    return load_row(rs);
}

/* ------------------------------------------------------------------ the recordset */

static void recordset_clear(RECORDSET *rs)
{
    columns_free(rs);
    rs->HasRow = FALSE;
    rs->Eof = rs->Bof = TRUE;
    rs->State = ADO_STATE_CLOSED;
    rs->Position = 0;
    if (rs->Stmt) {
        if (rs->Stmt->Current == rs)
            rs->Stmt->Current = nullptr;
        StatementRelease(rs->Stmt);
        rs->Stmt = nullptr;
    }
}

/* the recordset takes the current result of a statement, and the first row of it. */
static HRESULT recordset_attach(RECORDSET *rs, STATEMENT *statement)
{
    recordset_clear(rs);
    InterlockedIncrement(&statement->Refs);
    rs->Stmt = statement;
    statement->Current = rs;
    if (!describe_columns(rs))
        return E_OUTOFMEMORY;
    if (!rs->ColumnCount)
        return S_OK;
    rs->State = ADO_STATE_OPEN;
    rs->Bof = FALSE;
    rs->Eof = FALSE;
    HRESULT result = fetch(rs);
    if (SUCCEEDED(result) && rs->Eof)
        rs->Bof = TRUE;
    return result;
}

static HRESULT STDMETHODCALLTYPE recordset_query_interface(ADORecordset *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) && !IsEqualIID(riid, &IID_ADORecordset) &&
        !IsEqualIID(riid, &IID_ADORecordset21) && !IsEqualIID(riid, &IID_ADORecordset20) &&
        !IsEqualIID(riid, &IID_ADORecordset15))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE recordset_add_ref(ADORecordset *This)
{
    return InterlockedIncrement(&RECORDSET_OF(This)->Refs);
}

static ULONG STDMETHODCALLTYPE recordset_release(ADORecordset *This)
{
    RECORDSET *rs = RECORDSET_OF(This);
    LONG refs = InterlockedDecrement(&rs->Refs);
    if (!refs) {
        recordset_clear(rs);
        HeapFree(GetProcessHeap(), 0, rs);
        InterlockedDecrement(&g_ado_objects);
    }
    return refs;
}

static HRESULT get_long(LONG *out, LONG value)
{
    if (!out)
        return E_POINTER;
    *out = value;
    return S_OK;
}

static HRESULT get_bool(VARIANT_BOOL *out, BOOL value)
{
    if (!out)
        return E_POINTER;
    *out = value ? VARIANT_TRUE : 0;
    return S_OK;
}

/* a recordset that is not open has no current record. */
static HRESULT require_open(RECORDSET *rs)
{
    return rs->State == ADO_STATE_OPEN && rs->Stmt && StatementValid(rs->Stmt) ? S_OK : ADO_E_OBJECT_CLOSED;
}

static HRESULT STDMETHODCALLTYPE recordset_get_bof(ADORecordset *This, VARIANT_BOOL *value)
{
    HRESULT result = require_open(RECORDSET_OF(This));
    return FAILED(result) ? result : get_bool(value, RECORDSET_OF(This)->Bof);
}

static HRESULT STDMETHODCALLTYPE recordset_get_eof(ADORecordset *This, VARIANT_BOOL *value)
{
    HRESULT result = require_open(RECORDSET_OF(This));
    return FAILED(result) ? result : get_bool(value, RECORDSET_OF(This)->Eof);
}

static HRESULT STDMETHODCALLTYPE recordset_get_cursor_type(ADORecordset *This, LONG *value)
{
    return get_long(value, 0);
}

static HRESULT STDMETHODCALLTYPE recordset_get_lock_type(ADORecordset *This, LONG *value)
{
    return get_long(value, 1);
}

static HRESULT STDMETHODCALLTYPE recordset_get_cache_size(ADORecordset *This, LONG *value)
{
    return get_long(value, 1);
}

static HRESULT STDMETHODCALLTYPE recordset_get_record_count(ADORecordset *This, LONG_PTR *value)
{
    HRESULT result = require_open(RECORDSET_OF(This));
    if (FAILED(result))
        return result;
    if (!value)
        return E_POINTER;
    *value = -1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE recordset_get_state(ADORecordset *This, LONG *value)
{
    return get_long(value, RECORDSET_OF(This)->State);
}

static HRESULT STDMETHODCALLTYPE recordset_get_status(ADORecordset *This, LONG *value)
{
    return get_long(value, 0);
}

static HRESULT STDMETHODCALLTYPE recordset_get_edit_mode(ADORecordset *This, LONG *value)
{
    return get_long(value, 0);
}

static HRESULT STDMETHODCALLTYPE recordset_get_cursor_location(ADORecordset *This, LONG *value)
{
    return get_long(value, RECORDSET_OF(This)->CursorLocation);
}

static HRESULT STDMETHODCALLTYPE recordset_put_cursor_location(ADORecordset *This, LONG value)
{
    RECORDSET_OF(This)->CursorLocation = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE recordset_get_page_size(ADORecordset *This, LONG *value)
{
    return get_long(value, 10);
}

static HRESULT STDMETHODCALLTYPE recordset_get_max_records(ADORecordset *This, LONG_PTR *value)
{
    if (!value)
        return E_POINTER;
    *value = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE recordset_close(ADORecordset *This)
{
    RECORDSET *rs = RECORDSET_OF(This);
    if (!rs->Stmt)
        return ADO_E_OBJECT_CLOSED;
    recordset_clear(rs);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE recordset_move_next(ADORecordset *This)
{
    RECORDSET *rs = RECORDSET_OF(This);
    HRESULT result = require_open(rs);
    if (FAILED(result))
        return result;
    if (rs->Eof)
        return ADO_E_NO_CURRENT_RECORD;
    return fetch(rs);
}

static HRESULT STDMETHODCALLTYPE recordset_get_fields(ADORecordset *This, ADOFields **value);

/* the connection a recordset was made on is the one that holds the errors of its statement. */
static HRESULT STDMETHODCALLTYPE recordset_next_recordset(ADORecordset *This, VARIANT *affected, ADORecordset **value)
{
    RECORDSET *rs = RECORDSET_OF(This);
    if (value)
        *value = nullptr;
    if (!rs->Stmt || !StatementValid(rs->Stmt))
        return ADO_E_OBJECT_CLOSED;
    STATEMENT *statement = rs->Stmt;
    ERROR_LIST *errors = statement->Connection->Errors;
    ErrorListClear(errors);
    InterlockedIncrement(&statement->Refs);
    recordset_clear(rs);
    SQLRETURN result = SQLMoreResults(statement->Handle);
    HRESULT status = S_OK;
    if (result == SQL_NO_DATA) {
        StatementRelease(statement);
        return S_OK;
    }
    if (!ok(result)) {
        ErrorListAddDiagnostics(errors, SQL_HANDLE_STMT, statement->Handle, DB_E_ERRORSINCOMMAND);
        StatementRelease(statement);
        return DB_E_ERRORSINCOMMAND;
    }
    if (result == SQL_SUCCESS_WITH_INFO) {
        ErrorListAddDiagnostics(errors, SQL_HANDLE_STMT, statement->Handle, DB_E_ERRORSINCOMMAND);
        ConnectionFireInfo(statement->Connection);
    }
    if (affected) {
        SQLLEN rows = -1;
        SQLRowCount(statement->Handle, &rows);
        VariantClear(affected);
        affected->vt = VT_I4;
        affected->lVal = (LONG)rows;
    }
    if (value)
        status = RecordsetCreate(statement, &IID_ADORecordset, (void **)value);
    StatementRelease(statement);
    return status;
}

static HRESULT STDMETHODCALLTYPE recordset_supports(ADORecordset *This, LONG options, VARIANT_BOOL *value)
{
    return get_bool(value, FALSE);
}

/* Open takes a command and a connection, a connection object or the string of one. */
static HRESULT STDMETHODCALLTYPE recordset_open(ADORecordset *This, VARIANT source, VARIANT connection, LONG cursor,
                                                LONG lock, LONG options)
{
    RECORDSET *rs = RECORDSET_OF(This);
    if (rs->State != ADO_STATE_CLOSED || rs->Stmt)
        return ADO_E_OBJECT_OPEN;
    VARIANT text;
    VariantInit(&text);
    if (FAILED(VariantChangeType(&text, &source, 0, VT_BSTR)))
        return E_INVALIDARG;
    ADOConnection *ado = nullptr;
    HRESULT result = E_INVALIDARG;
    VARIANT *conn = &connection;
    if (conn->vt == (VT_VARIANT | VT_BYREF))
        conn = conn->pvarVal;
    if (conn->vt == VT_DISPATCH || conn->vt == VT_UNKNOWN) {
        if (conn->punkVal)
            result = conn->punkVal->lpVtbl->QueryInterface(conn->punkVal, &IID_ADOConnection, (void **)&ado);
    } else if (conn->vt == VT_BSTR) {
        result = ConnectionCreate(&IID_ADOConnection, (void **)&ado);
        if (SUCCEEDED(result)) {
            result = ado->lpVtbl->Open(ado, conn->bstrVal, nullptr, nullptr, -1);
            if (FAILED(result)) {
                ado->lpVtbl->Release(ado);
                ado = nullptr;
            }
        }
    }
    if (SUCCEEDED(result) && ado) {
        STATEMENT *statement;
        result = ConnectionRun((CONNECTION *)ado, text.bstrVal, &statement, nullptr);
        if (SUCCEEDED(result)) {
            result = recordset_attach(rs, statement);
            StatementRelease(statement);
        }
    }
    if (ado)
        ado->lpVtbl->Release(ado);
    VariantClear(&text);
    return result;
}

#define STUB(name, ...)                                                                                                \
    static HRESULT STDMETHODCALLTYPE name(ADORecordset *This __VA_OPT__(, ) __VA_ARGS__) { return E_NOTIMPL; }
STUB(recordset_get_properties, ADOProperties **value)
STUB(recordset_get_absolute_position, LONG *value)
STUB(recordset_put_absolute_position, LONG value)
STUB(recordset_putref_active_connection, IDispatch *value)
STUB(recordset_put_active_connection, VARIANT value)
STUB(recordset_get_active_connection, VARIANT *value)
STUB(recordset_get_bookmark, VARIANT *value)
STUB(recordset_put_bookmark, VARIANT value)
STUB(recordset_put_cache_size, LONG value)
STUB(recordset_put_cursor_type, LONG value)
STUB(recordset_put_lock_type, LONG value)
STUB(recordset_put_max_records, LONG_PTR value)
STUB(recordset_putref_source, IDispatch *value)
STUB(recordset_put_source, BSTR value)
STUB(recordset_get_source, VARIANT *value)
STUB(recordset_add_new, VARIANT fields, VARIANT values)
STUB(recordset_cancel_update)
STUB(recordset_delete, LONG affect)
STUB(recordset_get_rows, LONG rows, VARIANT start, VARIANT fields, VARIANT *value)
STUB(recordset_move, LONG_PTR rows, VARIANT start)
STUB(recordset_move_previous)
STUB(recordset_move_first)
STUB(recordset_move_last)
STUB(recordset_requery, LONG options)
STUB(recordset_xresync, LONG affect)
STUB(recordset_update, VARIANT fields, VARIANT values)
STUB(recordset_get_absolute_page, LONG *value)
STUB(recordset_put_absolute_page, LONG value)
STUB(recordset_get_filter, VARIANT *value)
STUB(recordset_put_filter, VARIANT value)
STUB(recordset_get_page_count, LONG_PTR *value)
STUB(recordset_put_page_size, LONG value)
STUB(recordset_get_sort, BSTR *value)
STUB(recordset_put_sort, BSTR value)
STUB(recordset_xclone, ADORecordset **value)
STUB(recordset_update_batch, LONG affect)
STUB(recordset_cancel_batch, LONG affect)
STUB(recordset_get_collect, VARIANT index, VARIANT *value)
STUB(recordset_put_collect, VARIANT index, VARIANT value)
STUB(recordset_get_marshal_options, LONG *value)
STUB(recordset_put_marshal_options, LONG value)
STUB(recordset_find, BSTR criteria, LONG skip, LONG direction, VARIANT start)
STUB(recordset_cancel)
STUB(recordset_get_data_source, IUnknown **value)
STUB(recordset_putref_data_source, IUnknown *value)
STUB(recordset_xsave, BSTR file, LONG format)
STUB(recordset_get_active_command, IDispatch **value)
STUB(recordset_put_stay_in_sync, VARIANT_BOOL value)
STUB(recordset_get_stay_in_sync, VARIANT_BOOL *value)
STUB(recordset_get_string, LONG format, LONG rows, BSTR column_delimiter, BSTR row_delimiter, BSTR null_expression, BSTR *value)
STUB(recordset_get_data_member, BSTR *value)
STUB(recordset_put_data_member, BSTR value)
STUB(recordset_compare_bookmarks, VARIANT first, VARIANT second, LONG *value)
STUB(recordset_clone, LONG lock, ADORecordset **value)
STUB(recordset_resync, LONG affect, LONG resync)
STUB(recordset_seek, VARIANT key, LONG seek)
STUB(recordset_put_index, BSTR value)
STUB(recordset_get_index, BSTR *value)
STUB(recordset_save, VARIANT destination, LONG format)

static const ADORecordsetVtbl g_recordset_vtbl = {
    recordset_query_interface, recordset_add_ref, recordset_release, recordset_GetTypeInfoCount, recordset_GetTypeInfo,
    recordset_GetIDsOfNames, recordset_Invoke, recordset_get_properties, recordset_get_absolute_position,
    recordset_put_absolute_position, recordset_putref_active_connection, recordset_put_active_connection,
    recordset_get_active_connection, recordset_get_bof, recordset_get_bookmark, recordset_put_bookmark,
    recordset_get_cache_size, recordset_put_cache_size, recordset_get_cursor_type, recordset_put_cursor_type,
    recordset_get_eof, recordset_get_fields, recordset_get_lock_type, recordset_put_lock_type,
    recordset_get_max_records, recordset_put_max_records, recordset_get_record_count, recordset_putref_source,
    recordset_put_source, recordset_get_source, recordset_add_new, recordset_cancel_update, recordset_close,
    recordset_delete, recordset_get_rows, recordset_move, recordset_move_next, recordset_move_previous,
    recordset_move_first, recordset_move_last, recordset_open, recordset_requery, recordset_xresync, recordset_update,
    recordset_get_absolute_page, recordset_put_absolute_page, recordset_get_edit_mode, recordset_get_filter,
    recordset_put_filter, recordset_get_page_count, recordset_get_page_size, recordset_put_page_size,
    recordset_get_sort, recordset_put_sort, recordset_get_status, recordset_get_state, recordset_xclone,
    recordset_update_batch, recordset_cancel_batch, recordset_get_cursor_location, recordset_put_cursor_location,
    recordset_next_recordset, recordset_supports, recordset_get_collect, recordset_put_collect,
    recordset_get_marshal_options, recordset_put_marshal_options, recordset_find, recordset_cancel,
    recordset_get_data_source, recordset_putref_data_source, recordset_xsave, recordset_get_active_command,
    recordset_put_stay_in_sync, recordset_get_stay_in_sync, recordset_get_string, recordset_get_data_member,
    recordset_put_data_member, recordset_compare_bookmarks, recordset_clone, recordset_resync, recordset_seek,
    recordset_put_index, recordset_get_index, recordset_save,
};

static RECORDSET *recordset_new(void)
{
    RECORDSET *rs = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*rs));
    if (!rs)
        return nullptr;
    rs->Rs.lpVtbl = &g_recordset_vtbl;
    rs->Refs = 1;
    rs->CursorLocation = ADO_USE_SERVER;
    rs->Eof = rs->Bof = TRUE;
    InterlockedIncrement(&g_ado_objects);
    return rs;
}

HRESULT RecordsetCreateEmpty(REFIID riid, void **object)
{
    RECORDSET *rs = recordset_new();
    if (!rs)
        return E_OUTOFMEMORY;
    HRESULT result = rs->Rs.lpVtbl->QueryInterface(&rs->Rs, riid, object);
    rs->Rs.lpVtbl->Release(&rs->Rs);
    return result;
}

HRESULT RecordsetCreate(STATEMENT *statement, REFIID riid, void **object)
{
    RECORDSET *rs = recordset_new();
    if (!rs)
        return E_OUTOFMEMORY;
    HRESULT result = recordset_attach(rs, statement);
    if (SUCCEEDED(result))
        result = rs->Rs.lpVtbl->QueryInterface(&rs->Rs, riid, object);
    rs->Rs.lpVtbl->Release(&rs->Rs);
    return result;
}

/* ------------------------------------------------------------------ a field */

typedef struct FIELD_OBJECT {
    ADOField Field;
    LONG Refs;
    RECORDSET *Recordset;
    LONG Index;
} FIELD_OBJECT;

typedef struct FIELDS_OBJECT {
    ADOFields Fields;
    LONG Refs;
    RECORDSET *Recordset;
} FIELDS_OBJECT;

static HRESULT STDMETHODCALLTYPE field_query_interface(ADOField *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) && !IsEqualIID(riid, &IID_ADOField))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE field_add_ref(ADOField *This)
{
    return InterlockedIncrement(&((FIELD_OBJECT *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE field_release(ADOField *This)
{
    FIELD_OBJECT *field = (FIELD_OBJECT *)This;
    LONG refs = InterlockedDecrement(&field->Refs);
    if (!refs) {
        field->Recordset->Rs.lpVtbl->Release(&field->Recordset->Rs);
        HeapFree(GetProcessHeap(), 0, field);
        InterlockedDecrement(&g_ado_objects);
    }
    return refs;
}

/* the column of a field, which is gone when the recordset was closed or moved to the next result. */
static const COLUMN *field_column(FIELD_OBJECT *field)
{
    return field->Index < field->Recordset->ColumnCount ? &field->Recordset->Columns[field->Index] : nullptr;
}

static HRESULT STDMETHODCALLTYPE field_get_properties(ADOField *This, ADOProperties **value)
{
    if (value)
        *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE field_get_actual_size(ADOField *This, LONG_PTR *value)
{
    FIELD_OBJECT *field = (FIELD_OBJECT *)This;
    if (!value)
        return E_POINTER;
    RECORDSET *rs = field->Recordset;
    if (!field_column(field) || !rs->HasRow)
        return ADO_E_OBJECT_CLOSED;
    const VARIANT *row = &rs->Row[field->Index];
    *value = row->vt == VT_BSTR ? (LONG_PTR)SysStringLen(row->bstrVal) : row->vt == VT_NULL ? 0 : (LONG_PTR)rs->Columns[field->Index].Size;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE field_get_attributes(ADOField *This, LONG *value)
{
    const COLUMN *column = field_column((FIELD_OBJECT *)This);
    if (!value)
        return E_POINTER;
    if (!column)
        return ADO_E_OBJECT_CLOSED;
    /* adFldMayBeNull, adFldIsNullable and adFldFixed for the types of a fixed size. */
    *value = column->Nullable != SQL_NO_NULLS ? 0x60 : 0;
    if (column->AdoType != 129 && column->AdoType != 200 && column->AdoType != 201 && column->AdoType != 202 &&
        column->AdoType != 203 && column->AdoType != 204 && column->AdoType != 205)
        *value |= 0x10;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE field_get_defined_size(ADOField *This, LONG_PTR *value)
{
    const COLUMN *column = field_column((FIELD_OBJECT *)This);
    if (!value)
        return E_POINTER;
    if (!column)
        return ADO_E_OBJECT_CLOSED;
    *value = (LONG_PTR)column->Size;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE field_get_name(ADOField *This, BSTR *value)
{
    const COLUMN *column = field_column((FIELD_OBJECT *)This);
    if (!value)
        return E_POINTER;
    if (!column)
        return ADO_E_OBJECT_CLOSED;
    *value = SysAllocStringLen(column->Name, SysStringLen(column->Name));
    return *value ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE field_get_type(ADOField *This, LONG *value)
{
    const COLUMN *column = field_column((FIELD_OBJECT *)This);
    if (!value)
        return E_POINTER;
    if (!column)
        return ADO_E_OBJECT_CLOSED;
    *value = column->AdoType;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE field_get_value(ADOField *This, VARIANT *value)
{
    FIELD_OBJECT *field = (FIELD_OBJECT *)This;
    if (!value)
        return E_POINTER;
    VariantInit(value);
    if (!field_column(field))
        return ADO_E_OBJECT_CLOSED;
    if (!field->Recordset->HasRow)
        return ADO_E_NO_CURRENT_RECORD;
    return VariantCopy(value, &field->Recordset->Row[field->Index]);
}

static HRESULT STDMETHODCALLTYPE field_get_precision(ADOField *This, BYTE *value)
{
    const COLUMN *column = field_column((FIELD_OBJECT *)This);
    if (!value)
        return E_POINTER;
    if (!column)
        return ADO_E_OBJECT_CLOSED;
    *value = column->AdoType == 131 ? (BYTE)column->Size : 255;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE field_get_numeric_scale(ADOField *This, BYTE *value)
{
    const COLUMN *column = field_column((FIELD_OBJECT *)This);
    if (!value)
        return E_POINTER;
    if (!column)
        return ADO_E_OBJECT_CLOSED;
    *value = column->AdoType == 131 ? (BYTE)column->Digits : 255;
    return S_OK;
}

#define FIELD_STUB(name, ...)                                                                                          \
    static HRESULT STDMETHODCALLTYPE name(ADOField *This __VA_OPT__(, ) __VA_ARGS__) { return E_NOTIMPL; }
FIELD_STUB(field_put_value, VARIANT value)
FIELD_STUB(field_append_chunk, VARIANT data)
FIELD_STUB(field_get_chunk, LONG length, VARIANT *value)
FIELD_STUB(field_get_original_value, VARIANT *value)
FIELD_STUB(field_get_underlying_value, VARIANT *value)
FIELD_STUB(field_get_data_format, IUnknown **value)
FIELD_STUB(field_putref_data_format, IUnknown *value)
FIELD_STUB(field_put_precision, BYTE value)
FIELD_STUB(field_put_numeric_scale, BYTE value)
FIELD_STUB(field_put_type, LONG value)
FIELD_STUB(field_put_defined_size, LONG_PTR value)
FIELD_STUB(field_put_attributes, LONG value)

static const ADOFieldVtbl g_field_vtbl = {
    field_query_interface, field_add_ref, field_release, field_GetTypeInfoCount, field_GetTypeInfo, field_GetIDsOfNames,
    field_Invoke, field_get_properties, field_get_actual_size, field_get_attributes, field_get_defined_size,
    field_get_name, field_get_type, field_get_value, field_put_value, field_get_precision, field_get_numeric_scale,
    field_append_chunk, field_get_chunk, field_get_original_value, field_get_underlying_value, field_get_data_format,
    field_putref_data_format, field_put_precision, field_put_numeric_scale, field_put_type, field_put_defined_size,
    field_put_attributes,
};

/* ------------------------------------------------------------------ the fields */

static HRESULT STDMETHODCALLTYPE fields_query_interface(ADOFields *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) && !IsEqualIID(riid, &IID_ADOFields))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE fields_add_ref(ADOFields *This)
{
    return InterlockedIncrement(&((FIELDS_OBJECT *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE fields_release(ADOFields *This)
{
    FIELDS_OBJECT *fields = (FIELDS_OBJECT *)This;
    LONG refs = InterlockedDecrement(&fields->Refs);
    if (!refs) {
        fields->Recordset->Rs.lpVtbl->Release(&fields->Recordset->Rs);
        HeapFree(GetProcessHeap(), 0, fields);
        InterlockedDecrement(&g_ado_objects);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE fields_get_count(ADOFields *This, LONG *value)
{
    return get_long(value, ((FIELDS_OBJECT *)This)->Recordset->ColumnCount);
}

static HRESULT STDMETHODCALLTYPE fields_new_enum(ADOFields *This, IUnknown **value)
{
    if (value)
        *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE fields_refresh(ADOFields *This)
{
    return S_OK;
}

static HRESULT field_create(RECORDSET *rs, LONG index, ADOField **out)
{
    FIELD_OBJECT *field = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*field));
    if (!field)
        return E_OUTOFMEMORY;
    field->Field.lpVtbl = &g_field_vtbl;
    field->Refs = 1;
    field->Recordset = rs;
    field->Index = index;
    rs->Rs.lpVtbl->AddRef(&rs->Rs);
    InterlockedIncrement(&g_ado_objects);
    *out = &field->Field;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fields_get_item(ADOFields *This, VARIANT index, ADOField **value)
{
    RECORDSET *rs = ((FIELDS_OBJECT *)This)->Recordset;
    if (!value)
        return E_POINTER;
    *value = nullptr;
    LONG found = -1;
    if (index.vt == VT_BSTR) {
        for (LONG i = 0; i < rs->ColumnCount && found < 0; i++)
            if (!_wcsicmp(rs->Columns[i].Name, index.bstrVal))
                found = i;
    } else {
        VARIANT number;
        VariantInit(&number);
        if (SUCCEEDED(VariantChangeType(&number, &index, 0, VT_I4)))
            found = number.lVal;
    }
    if (found < 0 || found >= rs->ColumnCount)
        return ADO_E_ITEM_NOT_FOUND;
    return field_create(rs, found, value);
}

static HRESULT STDMETHODCALLTYPE fields_append(ADOFields *This, BSTR name, LONG type, LONG_PTR size, LONG attributes,
                                               VARIANT value)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE fields_delete(ADOFields *This, VARIANT index)
{
    return E_NOTIMPL;
}

static const ADOFieldsVtbl g_fields_vtbl = {
    fields_query_interface, fields_add_ref, fields_release, fields_GetTypeInfoCount, fields_GetTypeInfo,
    fields_GetIDsOfNames, fields_Invoke, fields_get_count, fields_new_enum, fields_refresh, fields_get_item,
    fields_append, fields_delete,
};

static HRESULT STDMETHODCALLTYPE recordset_get_fields(ADORecordset *This, ADOFields **value)
{
    RECORDSET *rs = RECORDSET_OF(This);
    if (!value)
        return E_POINTER;
    *value = nullptr;
    FIELDS_OBJECT *fields = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*fields));
    if (!fields)
        return E_OUTOFMEMORY;
    fields->Fields.lpVtbl = &g_fields_vtbl;
    fields->Refs = 1;
    fields->Recordset = rs;
    rs->Rs.lpVtbl->AddRef(&rs->Rs);
    InterlockedIncrement(&g_ado_objects);
    *value = &fields->Fields;
    return S_OK;
}
