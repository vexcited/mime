/*
 * diagnostics: the records the driver manager posts on its handles and the
 * functions that read them back, plus the string helpers shared by the dll.
 */
#include "odbc32_private.h"

/* sqlstate for odbc 3 applications, for odbc 2 applications, and message. */
static const struct {
    PCWSTR state3;
    PCWSTR state2;
    PCWSTR text;
} g_errors[ODBC_E_COUNT] = {
    [ODBC_E_01004] = {L"01004", L"01004", L"String data, right truncated"},
    [ODBC_E_08002] = {L"08002", L"08002", L"Connection name in use"},
    [ODBC_E_08003] = {L"08003", L"08003", L"Connection not open"},
    [ODBC_E_HY001] = {L"HY001", L"S1001", L"Memory allocation error"},
    [ODBC_E_HY009] = {L"HY009", L"S1009", L"Invalid use of null pointer"},
    [ODBC_E_HY010] = {L"HY010", L"S1010", L"Function sequence error"},
    [ODBC_E_HY011] = {L"HY011", L"S1011", L"Attribute cannot be set now"},
    [ODBC_E_HY012] = {L"HY012", L"S1012", L"Invalid transaction operation code"},
    [ODBC_E_HY024] = {L"HY024", L"S1009", L"Invalid attribute value"},
    [ODBC_E_HY090] = {L"HY090", L"S1090", L"Invalid string or buffer length"},
    [ODBC_E_HY092] = {L"HY092", L"S1092", L"Invalid attribute/option identifier"},
    [ODBC_E_HY095] = {L"HY095", L"S1095", L"Function type out of range"},
    [ODBC_E_HY103] = {L"HY103", L"S1103", L"Invalid retrieval code"},
    [ODBC_E_HY110] = {L"HY110", L"S1110", L"Invalid driver completion"},
    [ODBC_E_HYC00] = {L"HYC00", L"S1C00", L"Optional feature not implemented"},
    [ODBC_E_IM002] = {L"IM002", L"IM002", L"Data source name not found and no default driver specified"},
    [ODBC_E_IM010] = {L"IM010", L"IM010", L"Data source name too long"},
};

static const WCHAR g_prefix[] = L"[Microsoft][ODBC Driver Manager] ";

/* ------------------------------------------------------------------ posting */

void OdbcBegin(OdbcHandle *h)
{
    h->diag_count = 0;
    h->diag_read = 0;
}

SQLRETURN OdbcReturn(OdbcHandle *h, SQLRETURN rc)
{
    h->result = rc;
    return rc;
}

void OdbcPost(OdbcHandle *h, enum OdbcError error)
{
    if (h->diag_count < ODBC_MAX_DIAGS)
        h->diags[h->diag_count++] = (UCHAR)error;
}

SQLRETURN OdbcFail(OdbcHandle *h, enum OdbcError error)
{
    OdbcPost(h, error);
    return OdbcReturn(h, SQL_ERROR);
}

PCWSTR OdbcState(const OdbcHandle *h, enum OdbcError error)
{
    return h->env->version == SQL_OV_ODBC2 ? g_errors[error].state2 : g_errors[error].state3;
}

int OdbcMessage(enum OdbcError error, PWSTR buffer, int size)
{
    return _snwprintf(buffer, size, L"%ls%ls", g_prefix, g_errors[error].text);
}

/* the class and subclass origin of a record, read from odbc32's string table. */
static PCWSTR OdbcOrigin(PCWSTR state)
{
    return state[0] == 'I' && state[1] == 'M' ? L"ODBC 3.0" : L"ISO 9075";
}

/* ------------------------------------------------------------------ strings */

BOOL OdbcCopyW(PCWSTR src, SQLWCHAR *dst, SQLLEN max)
{
    SQLLEN length = (SQLLEN)wcslen(src);
    SQLLEN n = length < max ? length : max - 1;
    memcpy(dst, src, n * sizeof(WCHAR));
    dst[n] = 0;
    return length >= max;
}

BOOL OdbcCopyA(PCSTR src, SQLCHAR *dst, SQLLEN max)
{
    SQLLEN length = (SQLLEN)strlen(src);
    SQLLEN n = length < max ? length : max - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
    return length >= max;
}

PWSTR OdbcToWide(const SQLCHAR *s, SQLLEN length)
{
    if (!s)
        return NULL;
    int n = length == SQL_NTS ? (int)strlen((PCSTR)s) : (int)length;
    int count = n ? MultiByteToWideChar(CP_ACP, 0, (PCSTR)s, n, NULL, 0) : 0;
    PWSTR w = OdbcAlloc((count + 1) * sizeof(WCHAR));
    if (w && count)
        MultiByteToWideChar(CP_ACP, 0, (PCSTR)s, n, w, count);
    return w;
}

PSTR OdbcToAnsi(PCWSTR s)
{
    int count = WideCharToMultiByte(CP_ACP, 0, s, -1, NULL, 0, NULL, NULL);
    PSTR a = OdbcAlloc(count ? count : 1);
    if (a && count)
        WideCharToMultiByte(CP_ACP, 0, s, -1, a, count, NULL, NULL);
    return a;
}

PWSTR OdbcDupW(const SQLWCHAR *s, SQLLEN length)
{
    if (!s)
        return NULL;
    SQLLEN n = length == SQL_NTS ? (SQLLEN)wcslen(s) : length;
    PWSTR w = OdbcAlloc((n + 1) * sizeof(WCHAR));
    if (w)
        memcpy(w, s, n * sizeof(WCHAR));
    return w;
}

/* ------------------------------------------------------------------ records */

typedef struct OdbcRecord {
    WCHAR state[SQL_SQLSTATE_SIZE + 1];
    WCHAR message[ODBC_MESSAGE_MAX];
} OdbcRecord;

static void OdbcRead(const OdbcHandle *h, USHORT index, OdbcRecord *record)
{
    enum OdbcError error = h->diags[index];
    wcscpy(record->state, OdbcState(h, error));
    OdbcMessage(error, record->message, ODBC_MESSAGE_MAX);
}

/* copies a record to the caller in utf-16 or ansi, as SQLGetDiagRec and SQLError do. */
static SQLRETURN OdbcOutputRecord(const OdbcRecord *record, SQLPOINTER state, SQLINTEGER *native, SQLPOINTER message,
                                  SQLSMALLINT message_max, SQLSMALLINT *message_length, BOOL ansi)
{
    if (native)
        *native = 0;
    if (!ansi) {
        if (state)
            wcscpy(state, record->state);
        SQLSMALLINT length = (SQLSMALLINT)wcslen(record->message);
        if (message_length)
            *message_length = length;
        if (!message)
            return SQL_SUCCESS;
        if (message_max <= 0)
            return SQL_SUCCESS_WITH_INFO;
        return OdbcCopyW(record->message, message, message_max) ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
    }
    if (state)
        WideCharToMultiByte(CP_ACP, 0, record->state, -1, state, SQL_SQLSTATE_SIZE + 1, NULL, NULL);
    PSTR text = OdbcToAnsi(record->message);
    if (!text)
        return SQL_ERROR;
    SQLSMALLINT length = (SQLSMALLINT)strlen(text);
    if (message_length)
        *message_length = length;
    SQLRETURN rc = SQL_SUCCESS;
    if (message)
        rc = message_max <= 0 || OdbcCopyA(text, message, message_max) ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
    OdbcFree(text);
    return rc;
}

static SQLRETURN OdbcGetDiagRec(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number, SQLPOINTER state,
                                SQLINTEGER *native, SQLPOINTER message, SQLSMALLINT message_max,
                                SQLSMALLINT *message_length, BOOL ansi)
{
    OdbcLock();
    SQLRETURN rc;
    OdbcHandle *h = OdbcLookup(handle, type);
    if (!h) {
        rc = SQL_INVALID_HANDLE;
    } else if (number <= 0 || message_max < 0) {
        rc = SQL_ERROR;
    } else if (number > h->diag_count) {
        rc = SQL_NO_DATA;
    } else {
        OdbcRecord record;
        OdbcRead(h, number - 1, &record);
        rc = OdbcOutputRecord(&record, state, native, message, message_max, message_length, ansi);
    }
    OdbcUnlock();
    return rc;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetDiagRecW(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT record,
                                                       SQLWCHAR *state, SQLINTEGER *native, SQLWCHAR *message,
                                                       SQLSMALLINT message_max, SQLSMALLINT *message_length)
{
    return OdbcGetDiagRec(type, handle, record, state, native, message, message_max, message_length, FALSE);
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetDiagRecA(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT record,
                                                       SQLCHAR *state, SQLINTEGER *native, SQLCHAR *message,
                                                       SQLSMALLINT message_max, SQLSMALLINT *message_length)
{
    return OdbcGetDiagRec(type, handle, record, state, native, message, message_max, message_length, TRUE);
}

/* odbc 2 style: returns the next unread record of the most specific handle given. */
static SQLRETURN OdbcError(SQLHENV env, SQLHDBC dbc, SQLHSTMT stmt, SQLPOINTER state, SQLINTEGER *native,
                           SQLPOINTER message, SQLSMALLINT message_max, SQLSMALLINT *message_length, BOOL ansi)
{
    OdbcLock();
    OdbcHandle *h;
    if (stmt)
        h = OdbcLookup(stmt, SQL_HANDLE_STMT);
    else if (dbc)
        h = OdbcLookup(dbc, SQL_HANDLE_DBC);
    else
        h = OdbcLookup(env, SQL_HANDLE_ENV);
    SQLRETURN rc;
    if (!h) {
        rc = SQL_INVALID_HANDLE;
    } else if (h->diag_read >= h->diag_count) {
        rc = SQL_NO_DATA;
    } else {
        OdbcRecord record;
        OdbcRead(h, h->diag_read++, &record);
        rc = OdbcOutputRecord(&record, state, native, message, message_max, message_length, ansi);
    }
    OdbcUnlock();
    return rc;
}

__declspec(dllexport) SQLRETURN SQL_API SQLErrorW(SQLHENV env, SQLHDBC dbc, SQLHSTMT stmt, SQLWCHAR *state,
                                                  SQLINTEGER *native, SQLWCHAR *message, SQLSMALLINT message_max,
                                                  SQLSMALLINT *message_length)
{
    return OdbcError(env, dbc, stmt, state, native, message, message_max, message_length, FALSE);
}

__declspec(dllexport) SQLRETURN SQL_API SQLErrorA(SQLHENV env, SQLHDBC dbc, SQLHSTMT stmt, SQLCHAR *state,
                                                  SQLINTEGER *native, SQLCHAR *message, SQLSMALLINT message_max,
                                                  SQLSMALLINT *message_length)
{
    return OdbcError(env, dbc, stmt, state, native, message, message_max, message_length, TRUE);
}

/* ------------------------------------------------------------------ fields */

/* copies a string field; lengths are in bytes for the unicode function. */
static SQLRETURN OdbcOutputField(PCWSTR text, SQLPOINTER value, SQLSMALLINT buffer_length, SQLSMALLINT *length,
                                 BOOL ansi)
{
    if (buffer_length < 0)
        return SQL_ERROR;
    if (!ansi) {
        if (length)
            *length = (SQLSMALLINT)(wcslen(text) * sizeof(WCHAR));
        if (!value)
            return SQL_SUCCESS;
        if (buffer_length < (SQLSMALLINT)sizeof(WCHAR))
            return SQL_SUCCESS_WITH_INFO;
        return OdbcCopyW(text, value, buffer_length / sizeof(WCHAR)) ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
    }
    PSTR a = OdbcToAnsi(text);
    if (!a)
        return SQL_ERROR;
    if (length)
        *length = (SQLSMALLINT)strlen(a);
    SQLRETURN rc = SQL_SUCCESS;
    if (value)
        rc = buffer_length == 0 || OdbcCopyA(a, value, buffer_length) ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
    OdbcFree(a);
    return rc;
}

static SQLRETURN OdbcGetDiagField(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT number, SQLSMALLINT field,
                                  SQLPOINTER value, SQLSMALLINT buffer_length, SQLSMALLINT *length, BOOL ansi)
{
    OdbcHandle *h = OdbcLookup(handle, type);
    if (!h)
        return SQL_INVALID_HANDLE;

    /* header fields ignore the record number; the statement ones need a statement. */
    switch (field) {
    case SQL_DIAG_RETURNCODE:
        if (value)
            *(SQLRETURN *)value = h->result;
        return SQL_SUCCESS;
    case SQL_DIAG_NUMBER:
        if (value)
            *(SQLINTEGER *)value = h->diag_count;
        return SQL_SUCCESS;
    case SQL_DIAG_ROW_COUNT:
    case SQL_DIAG_CURSOR_ROW_COUNT:
    case SQL_DIAG_DYNAMIC_FUNCTION:
    case SQL_DIAG_DYNAMIC_FUNCTION_CODE:
        return SQL_ERROR;
    case SQL_DIAG_SQLSTATE:
    case SQL_DIAG_NATIVE:
    case SQL_DIAG_MESSAGE_TEXT:
    case SQL_DIAG_CLASS_ORIGIN:
    case SQL_DIAG_SUBCLASS_ORIGIN:
    case SQL_DIAG_CONNECTION_NAME:
    case SQL_DIAG_SERVER_NAME:
    case SQL_DIAG_ROW_NUMBER:
    case SQL_DIAG_COLUMN_NUMBER:
        break;
    default:
        return SQL_ERROR;
    }

    if (number <= 0)
        return SQL_ERROR;
    if (number > h->diag_count)
        return SQL_NO_DATA;
    if (field == SQL_DIAG_ROW_NUMBER || field == SQL_DIAG_COLUMN_NUMBER)
        return SQL_ERROR;
    OdbcRecord record;
    OdbcRead(h, number - 1, &record);
    switch (field) {
    case SQL_DIAG_NATIVE:
        if (value)
            *(SQLINTEGER *)value = 0;
        return SQL_SUCCESS;
    case SQL_DIAG_SQLSTATE:
        return OdbcOutputField(record.state, value, buffer_length, length, ansi);
    case SQL_DIAG_MESSAGE_TEXT:
        return OdbcOutputField(record.message, value, buffer_length, length, ansi);
    case SQL_DIAG_CLASS_ORIGIN:
    case SQL_DIAG_SUBCLASS_ORIGIN:
        return OdbcOutputField(OdbcOrigin(record.state), value, buffer_length, length, ansi);
    default:
        return OdbcOutputField(L"", value, buffer_length, length, ansi);
    }
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetDiagFieldW(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT record,
                                                         SQLSMALLINT field, SQLPOINTER value,
                                                         SQLSMALLINT buffer_length, SQLSMALLINT *length)
{
    OdbcLock();
    SQLRETURN rc = OdbcGetDiagField(type, handle, record, field, value, buffer_length, length, FALSE);
    OdbcUnlock();
    return rc;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetDiagFieldA(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT record,
                                                         SQLSMALLINT field, SQLPOINTER value,
                                                         SQLSMALLINT buffer_length, SQLSMALLINT *length)
{
    OdbcLock();
    SQLRETURN rc = OdbcGetDiagField(type, handle, record, field, value, buffer_length, length, TRUE);
    OdbcUnlock();
    return rc;
}
