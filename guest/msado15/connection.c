/*
 * the ADO connection. It opens an ODBC connection from the connection string, runs commands on statements of it and
 * keeps the errors and messages of the driver for the caller, who is told of them by the events of its sink.
 */
#include "adoprivate.h"

#define CONNECTION_OF(p, member) ((CONNECTION *)((char *)(p) - offsetof(CONNECTION, member)))

DISPATCH_STUBS(connection, ADOConnection)

LONG g_ado_objects;

/* ------------------------------------------------------------------ the connection string */

typedef struct SETTINGS {
    WCHAR Provider[128], Server[256], Database[256], User[128], Password[256], Application[128], Workstation[128];
    WCHAR Driver[256], Dsn[128], Extended[1024], Other[512];
    BOOL Trusted, HasUser, HasPassword;
    LONG Timeout;
} SETTINGS;

static BOOL name_is(LPCWSTR name, SIZE_T length, LPCWSTR wanted)
{
    return wcslen(wanted) == length && !_wcsnicmp(name, wanted, length);
}

static void copy_into(WCHAR *to, SIZE_T size, LPCWSTR from, SIZE_T length)
{
    if (length >= size)
        length = size - 1;
    memcpy(to, from, length * sizeof(WCHAR));
    to[length] = 0;
}

static void add_other(SETTINGS *settings, LPCWSTR key, SIZE_T key_length, LPCWSTR value)
{
    SIZE_T used = wcslen(settings->Other);
    if (used + key_length + wcslen(value) + 4 < RTL_NUMBER_OF(settings->Other)) {
        memcpy(settings->Other + used, key, key_length * sizeof(WCHAR));
        _snwprintf(settings->Other + used + key_length, RTL_NUMBER_OF(settings->Other) - used - key_length, L"={%s};", value);
    }
}

/* reads a connection string of ADO into the settings. */
static void parse_connection_string(LPCWSTR text, SETTINGS *settings)
{
    while (*text) {
        while (*text == L';' || *text == L' ')
            text++;
        LPCWSTR key = text;
        while (*text && *text != L'=' && *text != L';')
            text++;
        SIZE_T key_length = text - key;
        while (key_length && key[key_length - 1] == L' ')
            key_length--;
        if (*text != L'=')
            continue;
        text++;
        while (*text == L' ')
            text++;
        WCHAR value[1024];
        SIZE_T length = 0;
        if (*text == L'\'' || *text == L'"') {
            WCHAR quote = *text++;
            while (*text && !(*text == quote && text[1] != quote)) {
                if (*text == quote)
                    text++;
                if (length + 1 < RTL_NUMBER_OF(value))
                    value[length++] = *text;
                text++;
            }
            if (*text == quote)
                text++;
        } else {
            while (*text && *text != L';') {
                if (length + 1 < RTL_NUMBER_OF(value))
                    value[length++] = *text;
                text++;
            }
            while (length && value[length - 1] == L' ')
                length--;
        }
        value[length] = 0;
        if (name_is(key, key_length, L"Provider"))
            copy_into(settings->Provider, RTL_NUMBER_OF(settings->Provider), value, length);
        else if (name_is(key, key_length, L"Data Source") || name_is(key, key_length, L"Server") ||
                 name_is(key, key_length, L"Address") || name_is(key, key_length, L"Addr") ||
                 name_is(key, key_length, L"Network Address"))
            copy_into(settings->Server, RTL_NUMBER_OF(settings->Server), value, length);
        else if (name_is(key, key_length, L"Initial Catalog") || name_is(key, key_length, L"Database"))
            copy_into(settings->Database, RTL_NUMBER_OF(settings->Database), value, length);
        else if (name_is(key, key_length, L"User ID") || name_is(key, key_length, L"UID") || name_is(key, key_length, L"User")) {
            copy_into(settings->User, RTL_NUMBER_OF(settings->User), value, length);
            settings->HasUser = TRUE;
        } else if (name_is(key, key_length, L"Password") || name_is(key, key_length, L"PWD")) {
            copy_into(settings->Password, RTL_NUMBER_OF(settings->Password), value, length);
            settings->HasPassword = TRUE;
        } else if (name_is(key, key_length, L"Integrated Security") || name_is(key, key_length, L"Trusted_Connection"))
            settings->Trusted = !_wcsicmp(value, L"SSPI") || !_wcsicmp(value, L"yes") || !_wcsicmp(value, L"true");
        else if (name_is(key, key_length, L"Application Name") || name_is(key, key_length, L"App"))
            copy_into(settings->Application, RTL_NUMBER_OF(settings->Application), value, length);
        else if (name_is(key, key_length, L"Workstation ID") || name_is(key, key_length, L"WSID"))
            copy_into(settings->Workstation, RTL_NUMBER_OF(settings->Workstation), value, length);
        else if (name_is(key, key_length, L"Driver"))
            copy_into(settings->Driver, RTL_NUMBER_OF(settings->Driver), value, length);
        else if (name_is(key, key_length, L"DSN"))
            copy_into(settings->Dsn, RTL_NUMBER_OF(settings->Dsn), value, length);
        else if (name_is(key, key_length, L"Extended Properties"))
            copy_into(settings->Extended, RTL_NUMBER_OF(settings->Extended), value, length);
        else if (name_is(key, key_length, L"Connect Timeout"))
        {
            settings->Timeout = 0;
            for (LPCWSTR digit = value; *digit >= L'0' && *digit <= L'9'; digit++)
                settings->Timeout = settings->Timeout * 10 + (*digit - L'0');
        }
        else if (name_is(key, key_length, L"Encrypt") || name_is(key, key_length, L"TrustServerCertificate") ||
                 name_is(key, key_length, L"Language") || name_is(key, key_length, L"ApplicationIntent"))
            add_other(settings, key, key_length, value);
        else if (name_is(key, key_length, L"MultipleActiveResultSets"))
            add_other(settings, L"MARS_Connection", 15, value);
    }
}

static BOOL driver_installed(LPCWSTR name)
{
    WCHAR key[300];
    HKEY handle;
    _snwprintf(key, RTL_NUMBER_OF(key), L"SOFTWARE\\ODBC\\ODBCINST.INI\\%s", name);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &handle))
        return FALSE;
    RegCloseKey(handle);
    return TRUE;
}

/* the newest ODBC driver for SQL Server that is installed, which stands in for the OLE DB providers of SQL Server. */
static LPCWSTR sql_server_driver(void)
{
    static const LPCWSTR names[] = {
        L"ODBC Driver 18 for SQL Server", L"ODBC Driver 17 for SQL Server", L"ODBC Driver 13.1 for SQL Server",
        L"ODBC Driver 13 for SQL Server", L"ODBC Driver 11 for SQL Server", L"SQL Server Native Client 11.0",
        L"SQL Server Native Client 10.0", L"SQL Server",
    };
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(names); i++)
        if (driver_installed(names[i]))
            return names[i];
    return nullptr;
}

static BOOL provider_is_sql_server(LPCWSTR provider)
{
    return !_wcsnicmp(provider, L"SQLOLEDB", 8) || !_wcsnicmp(provider, L"SQLNCLI", 7) || !_wcsnicmp(provider, L"MSOLEDBSQL", 10);
}

static void append_pair(WCHAR *out, SIZE_T size, LPCWSTR key, LPCWSTR value)
{
    SIZE_T used = wcslen(out);
    _snwprintf(out + used, size - used, L"%s={%s};", key, value);
}

/* the connection string of the ODBC driver, or the error of a provider that is not here. */
static HRESULT build_odbc_string(const SETTINGS *settings, WCHAR *out, SIZE_T size)
{
    out[0] = 0;
    if (provider_is_sql_server(settings->Provider)) {
        LPCWSTR driver = sql_server_driver();
        if (!driver)
            return ADO_E_PROVIDER_NOT_FOUND;
        append_pair(out, size, L"Driver", driver);
        append_pair(out, size, L"Server", settings->Server);
        if (settings->Database[0])
            append_pair(out, size, L"Database", settings->Database);
        if (settings->Trusted)
            wcscat(out, L"Trusted_Connection=yes;");
        else if (settings->HasUser) {
            append_pair(out, size, L"UID", settings->User);
            append_pair(out, size, L"PWD", settings->Password);
        }
        if (settings->Application[0])
            append_pair(out, size, L"APP", settings->Application);
        if (settings->Workstation[0])
            append_pair(out, size, L"WSID", settings->Workstation);
        /* the newest drivers encrypt unless told not to, which the providers they replace did not. */
        if (wcsstr(driver, L" 18 ") && !wcsstr(settings->Other, L"Encrypt=") && !wcsstr(settings->Other, L"TrustServerCertificate="))
            wcscat(out, L"TrustServerCertificate=yes;");
        wcscat(out, settings->Other);
        return S_OK;
    }
    /* MSDASQL, the provider for ODBC, and a string that names a driver or a data source and no provider. */
    if (settings->Provider[0] && _wcsicmp(settings->Provider, L"MSDASQL") && _wcsnicmp(settings->Provider, L"MSDASQL.", 8))
        return ADO_E_PROVIDER_NOT_FOUND;
    if (settings->Driver[0])
        append_pair(out, size, L"Driver", settings->Driver);
    if (settings->Dsn[0])
        append_pair(out, size, L"DSN", settings->Dsn);
    else if (!settings->Driver[0] && settings->Server[0])
        append_pair(out, size, L"DSN", settings->Server);
    if (settings->Database[0])
        append_pair(out, size, L"Database", settings->Database);
    if (settings->HasUser)
        append_pair(out, size, L"UID", settings->User);
    if (settings->HasPassword)
        append_pair(out, size, L"PWD", settings->Password);
    if (settings->Trusted)
        wcscat(out, L"Trusted_Connection=yes;");
    if (settings->Extended[0]) {
        wcscat(out, settings->Extended);
        wcscat(out, L";");
    }
    return out[0] ? S_OK : ADO_E_PROVIDER_NOT_FOUND;
}

/* ------------------------------------------------------------------ events */

static ADOConnectionEventsVt *sink_of(CONNECTION *connection)
{
    return connection->Sink;
}

/* the first of the errors as an object, for the events that carry one. */
static ADOError *first_error(CONNECTION *connection)
{
    ADOError *error = nullptr;
    if (connection->Errors->Count)
        ErrorCreate(&connection->Errors->Items[0], &error);
    return error;
}

void ConnectionFireInfo(CONNECTION *connection)
{
    ADOConnectionEventsVt *sink = sink_of(connection);
    if (!sink || !connection->Errors->Count)
        return;
    ADOError *error = first_error(connection);
    LONG status = ADO_STATUS_OK;
    sink->lpVtbl->InfoMessage(sink, error, &status, &connection->Conn);
    if (error)
        error->lpVtbl->Release(error);
}

static void fire_connect_complete(CONNECTION *connection, LONG status)
{
    ADOConnectionEventsVt *sink = sink_of(connection);
    if (!sink)
        return;
    ADOError *error = first_error(connection);
    sink->lpVtbl->ConnectComplete(sink, error, &status, &connection->Conn);
    if (error)
        error->lpVtbl->Release(error);
}

/* ------------------------------------------------------------------ the object */

void ConnectionAddRef(CONNECTION *connection)
{
    InterlockedIncrement(&connection->Refs);
}

static void connection_disconnect(CONNECTION *connection)
{
    if (connection->Open) {
        SQLDisconnect(connection->Dbc);
        connection->Open = FALSE;
        connection->Generation++;
    }
    if (connection->Dbc)
        SQLFreeHandle(SQL_HANDLE_DBC, connection->Dbc);
    if (connection->Env)
        SQLFreeHandle(SQL_HANDLE_ENV, connection->Env);
    connection->Dbc = nullptr;
    connection->Env = nullptr;
}

LONG ConnectionRelease(CONNECTION *connection)
{
    LONG refs = InterlockedDecrement(&connection->Refs);
    if (refs)
        return refs;
    connection_disconnect(connection);
    if (connection->Sink)
        connection->Sink->lpVtbl->Release(connection->Sink);
    ErrorListRelease(connection->Errors);
    HeapFree(GetProcessHeap(), 0, connection->ConnectionString);
    HeapFree(GetProcessHeap(), 0, connection->Provider);
    HeapFree(GetProcessHeap(), 0, connection->DefaultDatabase);
    HeapFree(GetProcessHeap(), 0, connection);
    InterlockedDecrement(&g_ado_objects);
    return 0;
}

static HRESULT STDMETHODCALLTYPE connection_query_interface(ADOConnection *This, REFIID riid, void **object)
{
    CONNECTION *connection = (CONNECTION *)This;
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_ADOConnection15) ||
        IsEqualIID(riid, &IID_ADOConnection))
        *object = &connection->Conn;
    else if (IsEqualIID(riid, &IID_IConnectionPointContainer))
        *object = &connection->Container;
    else
        return E_NOINTERFACE;
    ConnectionAddRef(connection);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE connection_add_ref(ADOConnection *This)
{
    return InterlockedIncrement(&((CONNECTION *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE connection_release(ADOConnection *This)
{
    return ConnectionRelease((CONNECTION *)This);
}

static HRESULT replace_string(LPWSTR *to, BSTR value)
{
    LPWSTR copy = nullptr;
    if (value) {
        UINT length = SysStringLen(value);
        copy = HeapAlloc(GetProcessHeap(), 0, (length + 1) * sizeof(WCHAR));
        if (!copy)
            return E_OUTOFMEMORY;
        memcpy(copy, value, length * sizeof(WCHAR));
        copy[length] = 0;
    }
    HeapFree(GetProcessHeap(), 0, *to);
    *to = copy;
    return S_OK;
}

static HRESULT string_out(BSTR *out, LPCWSTR text)
{
    if (!out)
        return E_POINTER;
    *out = AdoSysString(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE connection_get_properties(ADOConnection *This, ADOProperties **value)
{
    if (value)
        *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE connection_get_connection_string(ADOConnection *This, BSTR *value)
{
    return string_out(value, ((CONNECTION *)This)->ConnectionString);
}

static HRESULT STDMETHODCALLTYPE connection_put_connection_string(ADOConnection *This, BSTR value)
{
    CONNECTION *connection = (CONNECTION *)This;
    return connection->Open ? ADO_E_OBJECT_OPEN : replace_string(&connection->ConnectionString, value);
}

static HRESULT get_long(LONG *out, LONG value)
{
    if (!out)
        return E_POINTER;
    *out = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE connection_get_command_timeout(ADOConnection *This, LONG *value)
{
    return get_long(value, ((CONNECTION *)This)->CommandTimeout);
}

static HRESULT STDMETHODCALLTYPE connection_put_command_timeout(ADOConnection *This, LONG value)
{
    if (value < 0)
        return E_INVALIDARG;
    ((CONNECTION *)This)->CommandTimeout = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE connection_get_connection_timeout(ADOConnection *This, LONG *value)
{
    return get_long(value, ((CONNECTION *)This)->ConnectionTimeout);
}

static HRESULT STDMETHODCALLTYPE connection_put_connection_timeout(ADOConnection *This, LONG value)
{
    if (value < 0)
        return E_INVALIDARG;
    CONNECTION *connection = (CONNECTION *)This;
    return connection->Open ? ADO_E_OBJECT_OPEN : (connection->ConnectionTimeout = value, S_OK);
}

static HRESULT STDMETHODCALLTYPE connection_get_version(ADOConnection *This, BSTR *value)
{
    return string_out(value, L"6.1");
}

static HRESULT STDMETHODCALLTYPE connection_close(ADOConnection *This)
{
    CONNECTION *connection = (CONNECTION *)This;
    if (!connection->Open)
        return ADO_E_OBJECT_CLOSED;
    ErrorListClear(connection->Errors);
    connection_disconnect(connection);
    connection->TransactionLevel = 0;
    ADOConnectionEventsVt *sink = sink_of(connection);
    if (sink) {
        LONG status = ADO_STATUS_OK;
        sink->lpVtbl->Disconnect(sink, &status, &connection->Conn);
    }
    return S_OK;
}

STATEMENT *StatementCreate(CONNECTION *connection, SQLHSTMT handle)
{
    STATEMENT *statement = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*statement));
    if (!statement)
        return nullptr;
    statement->Refs = 1;
    statement->Connection = connection;
    ConnectionAddRef(connection);
    statement->Generation = connection->Generation;
    statement->Handle = handle;
    return statement;
}

BOOL StatementValid(const STATEMENT *statement)
{
    return statement->Connection->Open && statement->Generation == statement->Connection->Generation;
}

void StatementRelease(STATEMENT *statement)
{
    if (InterlockedDecrement(&statement->Refs))
        return;
    if (StatementValid(statement))
        SQLFreeHandle(SQL_HANDLE_STMT, statement->Handle);
    ConnectionRelease(statement->Connection);
    HeapFree(GetProcessHeap(), 0, statement);
}

static BOOL ok(SQLRETURN result)
{
    return result == SQL_SUCCESS || result == SQL_SUCCESS_WITH_INFO;
}

/* runs a command on a statement of its own and tells the caller how many rows it changed. */
HRESULT ConnectionRun(CONNECTION *connection, BSTR command, STATEMENT **out, LONG *affected)
{
    *out = nullptr;
    if (!connection->Open)
        return ADO_E_OBJECT_CLOSED;
    if (!command || !SysStringLen(command))
        return E_INVALIDARG;
    ErrorListClear(connection->Errors);
    SQLHSTMT handle;
    if (!ok(SQLAllocHandle(SQL_HANDLE_STMT, connection->Dbc, &handle))) {
        ErrorListAddDiagnostics(connection->Errors, SQL_HANDLE_DBC, connection->Dbc, DB_E_ERRORSINCOMMAND);
        return DB_E_ERRORSINCOMMAND;
    }
    if (connection->CommandTimeout)
        SQLSetStmtAttrW(handle, SQL_ATTR_QUERY_TIMEOUT, (SQLPOINTER)(ULONG_PTR)connection->CommandTimeout, 0);
    SQLRETURN result = SQLExecDirectW(handle, command, (SQLINTEGER)SysStringLen(command));
    if (!ok(result) && result != SQL_NO_DATA) {
        ErrorListAddDiagnostics(connection->Errors, SQL_HANDLE_STMT, handle, DB_E_ERRORSINCOMMAND);
        SQLFreeHandle(SQL_HANDLE_STMT, handle);
        ADOConnectionEventsVt *sink = sink_of(connection);
        if (sink) {
            ADOError *error = first_error(connection);
            LONG status = ADO_STATUS_ERRORS_OCCURRED;
            sink->lpVtbl->ExecuteComplete(sink, -1, error, &status, nullptr, nullptr, &connection->Conn);
            if (error)
                error->lpVtbl->Release(error);
        }
        return DB_E_ERRORSINCOMMAND;
    }
    if (result == SQL_SUCCESS_WITH_INFO) {
        ErrorListAddDiagnostics(connection->Errors, SQL_HANDLE_STMT, handle, DB_E_ERRORSINCOMMAND);
        ConnectionFireInfo(connection);
    }
    if (affected) {
        SQLLEN rows = -1;
        SQLRowCount(handle, &rows);
        *affected = (LONG)rows;
    }
    *out = StatementCreate(connection, handle);
    if (!*out) {
        SQLFreeHandle(SQL_HANDLE_STMT, handle);
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE connection_execute(ADOConnection *This, BSTR command, VARIANT *affected, LONG options,
                                                    ADORecordset **value)
{
    STATEMENT *statement;
    LONG rows = -1;
    if (value)
        *value = nullptr;
    HRESULT status = ConnectionRun((CONNECTION *)This, command, &statement, &rows);
    if (FAILED(status))
        return status;
    if (affected) {
        VariantClear(affected);
        affected->vt = VT_I4;
        affected->lVal = rows;
    }
    if (value)
        status = RecordsetCreate(statement, &IID_ADORecordset, (void **)value);
    StatementRelease(statement);
    return status;
}

static HRESULT STDMETHODCALLTYPE connection_begin_trans(ADOConnection *This, LONG *level)
{
    CONNECTION *connection = (CONNECTION *)This;
    if (!connection->Open)
        return ADO_E_OBJECT_CLOSED;
    ErrorListClear(connection->Errors);
    if (!connection->TransactionLevel &&
        !ok(SQLSetConnectAttrW(connection->Dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0))) {
        ErrorListAddDiagnostics(connection->Errors, SQL_HANDLE_DBC, connection->Dbc, E_FAIL);
        return E_FAIL;
    }
    connection->TransactionLevel++;
    if (level)
        *level = connection->TransactionLevel;
    ADOConnectionEventsVt *sink = sink_of(connection);
    if (sink) {
        LONG status = ADO_STATUS_OK;
        sink->lpVtbl->BeginTransComplete(sink, connection->TransactionLevel, nullptr, &status, &connection->Conn);
    }
    return S_OK;
}

static HRESULT end_transaction(CONNECTION *connection, SQLSMALLINT how)
{
    if (!connection->Open)
        return ADO_E_OBJECT_CLOSED;
    if (!connection->TransactionLevel)
        return E_FAIL;
    ErrorListClear(connection->Errors);
    if (--connection->TransactionLevel)
        return S_OK;
    HRESULT result = S_OK;
    if (!ok(SQLEndTran(SQL_HANDLE_DBC, connection->Dbc, how))) {
        ErrorListAddDiagnostics(connection->Errors, SQL_HANDLE_DBC, connection->Dbc, E_FAIL);
        result = E_FAIL;
    }
    SQLSetConnectAttrW(connection->Dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
    ADOConnectionEventsVt *sink = sink_of(connection);
    if (sink) {
        LONG status = SUCCEEDED(result) ? ADO_STATUS_OK : ADO_STATUS_ERRORS_OCCURRED;
        if (how == SQL_COMMIT)
            sink->lpVtbl->CommitTransComplete(sink, nullptr, &status, &connection->Conn);
        else
            sink->lpVtbl->RollbackTransComplete(sink, nullptr, &status, &connection->Conn);
    }
    return result;
}

static HRESULT STDMETHODCALLTYPE connection_commit_trans(ADOConnection *This)
{
    return end_transaction((CONNECTION *)This, SQL_COMMIT);
}

static HRESULT STDMETHODCALLTYPE connection_rollback_trans(ADOConnection *This)
{
    return end_transaction((CONNECTION *)This, SQL_ROLLBACK);
}

static HRESULT STDMETHODCALLTYPE connection_open(ADOConnection *This, BSTR connection_string, BSTR user, BSTR password,
                                                 LONG options)
{
    CONNECTION *connection = (CONNECTION *)This;
    if (connection->Open)
        return ADO_E_OBJECT_OPEN;
    ErrorListClear(connection->Errors);
    if (connection_string && SysStringLen(connection_string))
        replace_string(&connection->ConnectionString, connection_string);
    SETTINGS *settings = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*settings));
    if (!settings)
        return E_OUTOFMEMORY;
    settings->Timeout = connection->ConnectionTimeout;
    if (connection->Provider)
        copy_into(settings->Provider, RTL_NUMBER_OF(settings->Provider), connection->Provider, wcslen(connection->Provider));
    parse_connection_string(connection->ConnectionString ? connection->ConnectionString : L"", settings);
    /* the user and password of the call win over the ones in the string. */
    if (user && SysStringLen(user)) {
        copy_into(settings->User, RTL_NUMBER_OF(settings->User), user, SysStringLen(user));
        settings->HasUser = TRUE;
    }
    if (password && SysStringLen(password)) {
        copy_into(settings->Password, RTL_NUMBER_OF(settings->Password), password, SysStringLen(password));
        settings->HasPassword = TRUE;
    }
    ADOConnectionEventsVt *sink = sink_of(connection);
    if (sink) {
        BSTR string = AdoSysString(connection->ConnectionString), user_name = AdoSysString(settings->User),
             secret = AdoSysString(settings->Password);
        LONG status = ADO_STATUS_OK, flags = options;
        sink->lpVtbl->WillConnect(sink, &string, &user_name, &secret, &flags, &status, &connection->Conn);
        SysFreeString(string);
        SysFreeString(user_name);
        SysFreeString(secret);
        if (status == ADO_STATUS_CANCEL) {
            HeapFree(GetProcessHeap(), 0, settings);
            return E_FAIL;
        }
    }
    WCHAR odbc[2048];
    HRESULT result = build_odbc_string(settings, odbc, RTL_NUMBER_OF(odbc));
    LONG timeout = settings->Timeout;
    if (settings->Provider[0] && FAILED(result)) {
        /* the providers that are not here: the error is that of ADO, with the message it gives. */
        ErrorListAdd(connection->Errors, result, 0, L"ADODB.Connection", L"Provider cannot be found. It may not be properly installed.", L"");
        HeapFree(GetProcessHeap(), 0, settings);
        fire_connect_complete(connection, ADO_STATUS_ERRORS_OCCURRED);
        return result;
    }
    HeapFree(GetProcessHeap(), 0, settings);
    if (FAILED(result))
        return result;
    SQLAllocHandle(SQL_HANDLE_ENV, nullptr, &connection->Env);
    SQLSetEnvAttr(connection->Env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, connection->Env, &connection->Dbc);
    if (timeout)
        SQLSetConnectAttrW(connection->Dbc, SQL_ATTR_LOGIN_TIMEOUT, (SQLPOINTER)(ULONG_PTR)timeout, 0);
    WCHAR completed[1024];
    SQLSMALLINT completed_length;
    SQLRETURN rc = SQLDriverConnectW(connection->Dbc, nullptr, odbc, SQL_NTS, completed, RTL_NUMBER_OF(completed),
                                     &completed_length, SQL_DRIVER_NOPROMPT);
    if (!ok(rc)) {
        ErrorListAddDiagnostics(connection->Errors, SQL_HANDLE_DBC, connection->Dbc, E_FAIL);
        result = connection->Errors->Count ? connection->Errors->Items[0].Number : E_FAIL;
        if (!result)
            result = E_FAIL;
        connection_disconnect(connection);
        fire_connect_complete(connection, ADO_STATUS_ERRORS_OCCURRED);
        return result;
    }
    connection->Open = TRUE;
    connection->Generation++;
    if (rc == SQL_SUCCESS_WITH_INFO) {
        ErrorListAddDiagnostics(connection->Errors, SQL_HANDLE_DBC, connection->Dbc, E_FAIL);
        ConnectionFireInfo(connection);
    }
    WCHAR catalog[256] = L"";
    SQLINTEGER catalog_length = 0;
    if (ok(SQLGetConnectAttrW(connection->Dbc, SQL_ATTR_CURRENT_CATALOG, catalog, sizeof(catalog), &catalog_length))) {
        BSTR name = AdoSysString(catalog);
        replace_string(&connection->DefaultDatabase, name);
        SysFreeString(name);
    }
    fire_connect_complete(connection, ADO_STATUS_OK);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE connection_get_errors(ADOConnection *This, ADOErrors **value)
{
    if (!value)
        return E_POINTER;
    return ErrorsCreate(((CONNECTION *)This)->Errors, value);
}

static HRESULT STDMETHODCALLTYPE connection_get_default_database(ADOConnection *This, BSTR *value)
{
    return string_out(value, ((CONNECTION *)This)->DefaultDatabase);
}

static HRESULT STDMETHODCALLTYPE connection_put_default_database(ADOConnection *This, BSTR value)
{
    CONNECTION *connection = (CONNECTION *)This;
    if (!connection->Open)
        return ADO_E_OBJECT_CLOSED;
    ErrorListClear(connection->Errors);
    if (!ok(SQLSetConnectAttrW(connection->Dbc, SQL_ATTR_CURRENT_CATALOG, value, SQL_NTS))) {
        ErrorListAddDiagnostics(connection->Errors, SQL_HANDLE_DBC, connection->Dbc, E_FAIL);
        return E_FAIL;
    }
    return replace_string(&connection->DefaultDatabase, value);
}

static HRESULT STDMETHODCALLTYPE connection_get_isolation_level(ADOConnection *This, LONG *value)
{
    return get_long(value, ((CONNECTION *)This)->Isolation);
}

static HRESULT STDMETHODCALLTYPE connection_put_isolation_level(ADOConnection *This, LONG value)
{
    ((CONNECTION *)This)->Isolation = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE connection_get_attributes(ADOConnection *This, LONG *value)
{
    return get_long(value, ((CONNECTION *)This)->Attributes);
}

static HRESULT STDMETHODCALLTYPE connection_put_attributes(ADOConnection *This, LONG value)
{
    ((CONNECTION *)This)->Attributes = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE connection_get_cursor_location(ADOConnection *This, LONG *value)
{
    return get_long(value, ((CONNECTION *)This)->CursorLocation);
}

static HRESULT STDMETHODCALLTYPE connection_put_cursor_location(ADOConnection *This, LONG value)
{
    ((CONNECTION *)This)->CursorLocation = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE connection_get_mode(ADOConnection *This, LONG *value)
{
    return get_long(value, ((CONNECTION *)This)->Mode);
}

static HRESULT STDMETHODCALLTYPE connection_put_mode(ADOConnection *This, LONG value)
{
    CONNECTION *connection = (CONNECTION *)This;
    return connection->Open ? ADO_E_OBJECT_OPEN : (connection->Mode = value, S_OK);
}

static HRESULT STDMETHODCALLTYPE connection_get_provider(ADOConnection *This, BSTR *value)
{
    return string_out(value, ((CONNECTION *)This)->Provider ? ((CONNECTION *)This)->Provider : L"MSDASQL");
}

static HRESULT STDMETHODCALLTYPE connection_put_provider(ADOConnection *This, BSTR value)
{
    CONNECTION *connection = (CONNECTION *)This;
    return connection->Open ? ADO_E_OBJECT_OPEN : replace_string(&connection->Provider, value);
}

static HRESULT STDMETHODCALLTYPE connection_get_state(ADOConnection *This, LONG *value)
{
    return get_long(value, ((CONNECTION *)This)->Open ? ADO_STATE_OPEN : ADO_STATE_CLOSED);
}

static HRESULT STDMETHODCALLTYPE connection_open_schema(ADOConnection *This, LONG schema, VARIANT restrictions,
                                                        VARIANT schema_id, ADORecordset **value)
{
    if (value)
        *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE connection_cancel(ADOConnection *This)
{
    return E_NOTIMPL;
}

static const ADOConnectionVtbl g_connection_vtbl = {
    connection_query_interface, connection_add_ref, connection_release, connection_GetTypeInfoCount,
    connection_GetTypeInfo, connection_GetIDsOfNames, connection_Invoke, connection_get_properties,
    connection_get_connection_string, connection_put_connection_string, connection_get_command_timeout,
    connection_put_command_timeout, connection_get_connection_timeout, connection_put_connection_timeout,
    connection_get_version, connection_close, connection_execute, connection_begin_trans, connection_commit_trans,
    connection_rollback_trans, connection_open, connection_get_errors, connection_get_default_database,
    connection_put_default_database, connection_get_isolation_level, connection_put_isolation_level,
    connection_get_attributes, connection_put_attributes, connection_get_cursor_location,
    connection_put_cursor_location, connection_get_mode, connection_put_mode, connection_get_provider,
    connection_put_provider, connection_get_state, connection_open_schema, connection_cancel,
};

/* ------------------------------------------------------------------ connection points */

static HRESULT STDMETHODCALLTYPE container_query_interface(IConnectionPointContainer *This, REFIID riid, void **object)
{
    return connection_query_interface(&CONNECTION_OF(This, Container)->Conn, riid, object);
}

static ULONG STDMETHODCALLTYPE container_add_ref(IConnectionPointContainer *This)
{
    return InterlockedIncrement(&CONNECTION_OF(This, Container)->Refs);
}

static ULONG STDMETHODCALLTYPE container_release(IConnectionPointContainer *This)
{
    return ConnectionRelease(CONNECTION_OF(This, Container));
}

static HRESULT STDMETHODCALLTYPE container_enum_connection_points(IConnectionPointContainer *This, IEnumConnectionPoints **enumerator)
{
    if (enumerator)
        *enumerator = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE container_find_connection_point(IConnectionPointContainer *This, REFIID riid,
                                                                 IConnectionPoint **point)
{
    CONNECTION *connection = CONNECTION_OF(This, Container);
    if (!point)
        return E_POINTER;
    *point = nullptr;
    if (!IsEqualIID(riid, &IID_ConnectionEvents))
        return CONNECT_E_NOCONNECTION;
    *point = &connection->Point;
    ConnectionAddRef(connection);
    return S_OK;
}

static const IConnectionPointContainerVtbl g_container_vtbl = {
    container_query_interface, container_add_ref, container_release, container_enum_connection_points,
    container_find_connection_point,
};

static HRESULT STDMETHODCALLTYPE point_query_interface(IConnectionPoint *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IConnectionPoint))
        return E_NOINTERFACE;
    *object = This;
    ConnectionAddRef(CONNECTION_OF(This, Point));
    return S_OK;
}

static ULONG STDMETHODCALLTYPE point_add_ref(IConnectionPoint *This)
{
    return InterlockedIncrement(&CONNECTION_OF(This, Point)->Refs);
}

static ULONG STDMETHODCALLTYPE point_release(IConnectionPoint *This)
{
    return ConnectionRelease(CONNECTION_OF(This, Point));
}

static HRESULT STDMETHODCALLTYPE point_get_connection_interface(IConnectionPoint *This, IID *iid)
{
    if (!iid)
        return E_POINTER;
    *iid = IID_ConnectionEvents;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE point_get_container(IConnectionPoint *This, IConnectionPointContainer **container)
{
    CONNECTION *connection = CONNECTION_OF(This, Point);
    if (!container)
        return E_POINTER;
    *container = &connection->Container;
    ConnectionAddRef(connection);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE point_advise(IConnectionPoint *This, IUnknown *sink, DWORD *cookie)
{
    CONNECTION *connection = CONNECTION_OF(This, Point);
    if (!sink || !cookie)
        return E_POINTER;
    if (connection->Sink)
        return CONNECT_E_ADVISELIMIT;
    ADOConnectionEventsVt *events;
    /* only a sink of the vtable form is called, one that is a dispatch interface is not. */
    if (FAILED(sink->lpVtbl->QueryInterface(sink, &IID_ConnectionEventsVt, (void **)&events)))
        return CONNECT_E_CANNOTCONNECT;
    connection->Sink = events;
    connection->Cookie = 1;
    *cookie = connection->Cookie;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE point_unadvise(IConnectionPoint *This, DWORD cookie)
{
    CONNECTION *connection = CONNECTION_OF(This, Point);
    if (!connection->Sink || cookie != connection->Cookie)
        return CONNECT_E_NOCONNECTION;
    ADOConnectionEventsVt *sink = connection->Sink;
    connection->Sink = nullptr;
    sink->lpVtbl->Release(sink);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE point_enum_connections(IConnectionPoint *This, IEnumConnections **enumerator)
{
    if (enumerator)
        *enumerator = nullptr;
    return E_NOTIMPL;
}

static const IConnectionPointVtbl g_point_vtbl = {
    point_query_interface, point_add_ref, point_release, point_get_connection_interface, point_get_container,
    point_advise, point_unadvise, point_enum_connections,
};

HRESULT ConnectionCreate(REFIID riid, void **object)
{
    CONNECTION *connection = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*connection));
    if (!connection)
        return E_OUTOFMEMORY;
    connection->Conn.lpVtbl = &g_connection_vtbl;
    connection->Container.lpVtbl = &g_container_vtbl;
    connection->Point.lpVtbl = &g_point_vtbl;
    connection->Refs = 1;
    connection->CommandTimeout = 30;
    connection->ConnectionTimeout = 15;
    connection->CursorLocation = ADO_USE_SERVER;
    connection->Isolation = 4096;
    connection->Errors = ErrorListCreate();
    if (!connection->Errors) {
        HeapFree(GetProcessHeap(), 0, connection);
        return E_OUTOFMEMORY;
    }
    InterlockedIncrement(&g_ado_objects);
    HRESULT result = connection->Conn.lpVtbl->QueryInterface(&connection->Conn, riid, object);
    connection->Conn.lpVtbl->Release(&connection->Conn);
    return result;
}
