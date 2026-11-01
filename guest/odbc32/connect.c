/*
 * connection handles: attributes, connecting, information, transactions and the data source and driver lists. no
 * odbc driver is installed, so a connection can never be opened.
 */
#include "odbc32_private.h"

/* ------------------------------------------------------------------ attributes */

static BOOL OdbcStringAttr(SQLINTEGER attribute)
{
    return attribute == SQL_ATTR_TRACEFILE || attribute == SQL_ATTR_TRANSLATE_LIB ||
           attribute == SQL_ATTR_CURRENT_CATALOG;
}

/* attributes whose value is a pointer rather than a 32-bit integer. */
static BOOL OdbcPointerAttr(SQLINTEGER attribute)
{
    return attribute == SQL_ATTR_QUIET_MODE || attribute == SQL_ATTR_ENLIST_IN_DTC ||
           attribute == SQL_ATTR_ENLIST_IN_XA || attribute == SQL_ATTR_ASYNC_DBC_EVENT ||
           attribute == SQL_ATTR_DBC_INFO_TOKEN;
}

/* checks the values the driver manager itself validates; others go to the driver. */
static BOOL OdbcValidAttrValue(SQLINTEGER attribute, SQLULEN value)
{
    switch (attribute) {
    case SQL_ATTR_ACCESS_MODE:
    case SQL_ATTR_AUTOCOMMIT:
    case SQL_ATTR_TRACE:
    case SQL_ATTR_ASYNC_ENABLE:
    case SQL_ATTR_ASYNC_DBC_FUNCTIONS_ENABLE:
    case SQL_ATTR_METADATA_ID:
        return value <= 1;
    case SQL_ATTR_ODBC_CURSORS:
        return value <= SQL_CUR_USE_DRIVER;
    }
    return TRUE;
}

static OdbcAttr *OdbcFindAttr(OdbcDbc *dbc, SQLINTEGER attribute)
{
    for (ULONG i = 0; i < dbc->attr_count; i++) {
        if (dbc->attrs[i].id == attribute)
            return &dbc->attrs[i];
    }
    return nullptr;
}

void OdbcFreeDbcAttrs(OdbcDbc *dbc)
{
    for (ULONG i = 0; i < dbc->attr_count; i++)
        OdbcFree(dbc->attrs[i].string);
    dbc->attr_count = 0;
}

static SQLRETURN OdbcSetAttr(OdbcDbc *dbc, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length, BOOL ansi)
{
    if (attribute == SQL_ATTR_CONNECTION_DEAD || attribute == SQL_ATTR_AUTO_IPD)
        return OdbcFail(&dbc->h, ODBC_E_HY092);
    PWSTR string = nullptr;
    if (OdbcStringAttr(attribute)) {
        if (!value)
            return OdbcFail(&dbc->h, ODBC_E_HY009);
        if (OdbcBadLength(length))
            return OdbcFail(&dbc->h, ODBC_E_HY090);
        if (ansi)
            string = OdbcToWide(value, length);
        else
            string = OdbcDupW(value, length == SQL_NTS ? SQL_NTS : length / (SQLINTEGER)sizeof(WCHAR));
        if (!string)
            return OdbcFail(&dbc->h, ODBC_E_HY001);
    } else if (!OdbcValidAttrValue(attribute, (SQLULEN)value)) {
        return OdbcFail(&dbc->h, ODBC_E_HY024);
    }
    OdbcAttr *attr = OdbcFindAttr(dbc, attribute);
    if (!attr) {
        if (dbc->attr_count == ODBC_MAX_ATTRS) {
            OdbcFree(string);
            return OdbcFail(&dbc->h, ODBC_E_HY001);
        }
        attr = &dbc->attrs[dbc->attr_count++];
        attr->id = attribute;
    }
    OdbcFree(attr->string);
    attr->string = string;
    attr->value = (SQLULEN)value;
    return OdbcReturn(&dbc->h, SQL_SUCCESS);
}

/* an open connection answers for its attributes itself, except the ones the manager traces with. */
static BOOL OdbcDriverAttr(OdbcDbc *dbc, SQLINTEGER attribute)
{
    return dbc->connected && attribute != SQL_ATTR_TRACE && attribute != SQL_ATTR_TRACEFILE;
}

typedef SQLRETURN(SQL_API *SET_ATTR_FN)(SQLHDBC, SQLINTEGER, SQLPOINTER, SQLINTEGER);
typedef SQLRETURN(SQL_API *GET_ATTR_FN)(SQLHDBC, SQLINTEGER, SQLPOINTER, SQLINTEGER, SQLINTEGER *);

SQLRETURN OdbcSetConnectAttr(SQLHDBC handle, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length, BOOL ansi)
{
    OdbcLock();
    SQLRETURN rc = SQL_INVALID_HANDLE;
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    if (dbc) {
        OdbcBegin(&dbc->h);
        if (OdbcDriverAttr(dbc, attribute)) {
            SET_ATTR_FN fn = OdbcDrvFn(dbc->driver, ansi ? "SQLSetConnectAttr" : "SQLSetConnectAttrW");
            SQLHDBC drv = dbc->drv_dbc;
            OdbcUnlock();
            rc = fn ? fn(drv, attribute, value, length) : SQL_ERROR;
            OdbcLock();
            dbc = OdbcLookupDbc(handle);
            if (dbc)
                dbc->diag_driver = TRUE;
            OdbcUnlock();
            return rc;
        }
        rc = OdbcSetAttr(dbc, attribute, value, length, ansi);
    }
    OdbcUnlock();
    return rc;
}

/* lengths are in bytes for unicode, in characters for ansi. */
static SQLRETURN OdbcOutputAttrString(OdbcDbc *dbc, PCWSTR text, SQLPOINTER value, SQLINTEGER buffer_length,
                                      SQLINTEGER *length, BOOL ansi)
{
    if (buffer_length < 0 && value)
        return OdbcFail(&dbc->h, ODBC_E_HY090);
    BOOL truncated = FALSE;
    if (!ansi) {
        if (length)
            *length = (SQLINTEGER)(wcslen(text) * sizeof(WCHAR));
        if (value) {
            SQLINTEGER max = buffer_length / (SQLINTEGER)sizeof(WCHAR);
            truncated = max ? OdbcCopyW(text, value, max) : TRUE;
        }
    } else {
        PSTR a = OdbcToAnsi(text);
        if (!a)
            return OdbcFail(&dbc->h, ODBC_E_HY001);
        if (length)
            *length = (SQLINTEGER)strlen(a);
        if (value)
            truncated = buffer_length ? OdbcCopyA(a, value, buffer_length) : TRUE;
        OdbcFree(a);
    }
    if (truncated) {
        OdbcPost(&dbc->h, ODBC_E_01004);
        return OdbcReturn(&dbc->h, SQL_SUCCESS_WITH_INFO);
    }
    return OdbcReturn(&dbc->h, SQL_SUCCESS);
}

static SQLRETURN OdbcGetAttr(OdbcDbc *dbc, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER buffer_length,
                             SQLINTEGER *length, BOOL ansi)
{
    OdbcAttr *attr = OdbcFindAttr(dbc, attribute);
    SQLULEN number;
    if (OdbcStringAttr(attribute)) {
        if (attr)
            return OdbcOutputAttrString(dbc, attr->string, value, buffer_length, length, ansi);
        if (attribute != SQL_ATTR_TRACEFILE)
            return OdbcFail(&dbc->h, ODBC_E_08003);
        /* the driver manager traces to SQL.LOG in the temporary directory by default. */
        WCHAR path[MAX_PATH + 8];
        DWORD n = GetTempPathW(MAX_PATH, path);
        if (!n || n >= MAX_PATH)
            n = 0;
        wcscpy(path + n, L"SQL.LOG");
        return OdbcOutputAttrString(dbc, path, value, buffer_length, length, ansi);
    }
    if (attr)
        number = attr->value;
    else if (attribute == SQL_ATTR_TRACE)
        number = SQL_OPT_TRACE_OFF;
    else if (attribute == SQL_ATTR_ODBC_CURSORS)
        number = SQL_CUR_USE_DRIVER;
    else
        return OdbcFail(&dbc->h, ODBC_E_08003);
    if (value) {
        if (OdbcPointerAttr(attribute))
            *(SQLULEN *)value = number;
        else
            *(SQLUINTEGER *)value = (SQLUINTEGER)number;
    }
    return OdbcReturn(&dbc->h, SQL_SUCCESS);
}

SQLRETURN OdbcGetConnectAttr(SQLHDBC handle, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER buffer_length,
                             SQLINTEGER *length, BOOL ansi)
{
    OdbcLock();
    SQLRETURN rc = SQL_INVALID_HANDLE;
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    if (dbc) {
        OdbcBegin(&dbc->h);
        if (OdbcDriverAttr(dbc, attribute)) {
            GET_ATTR_FN fn = OdbcDrvFn(dbc->driver, ansi ? "SQLGetConnectAttr" : "SQLGetConnectAttrW");
            SQLHDBC drv = dbc->drv_dbc;
            OdbcUnlock();
            rc = fn ? fn(drv, attribute, value, buffer_length, length) : SQL_ERROR;
            OdbcLock();
            dbc = OdbcLookupDbc(handle);
            if (dbc)
                dbc->diag_driver = TRUE;
            OdbcUnlock();
            return rc;
        }
        rc = OdbcGetAttr(dbc, attribute, value, buffer_length, length, ansi);
    }
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetConnectAttrW(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value,
                                                           SQLINTEGER length)
{
    return OdbcSetConnectAttr(dbc, attribute, value, length, FALSE);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetConnectAttrA(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value,
                                                           SQLINTEGER length)
{
    return OdbcSetConnectAttr(dbc, attribute, value, length, TRUE);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetConnectAttrW(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value,
                                                           SQLINTEGER buffer_length, SQLINTEGER *length)
{
    return OdbcGetConnectAttr(dbc, attribute, value, buffer_length, length, FALSE);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetConnectAttrA(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value,
                                                           SQLINTEGER buffer_length, SQLINTEGER *length)
{
    return OdbcGetConnectAttr(dbc, attribute, value, buffer_length, length, TRUE);
}

/* odbc 2 options: strings are null terminated and read into SQL_MAX_OPTION_STRING_LENGTH buffers. */
DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetConnectOptionW(SQLHDBC dbc, SQLUSMALLINT option, SQLULEN value)
{
    return OdbcSetConnectAttr(dbc, option, (SQLPOINTER)value, SQL_NTS, FALSE);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetConnectOptionA(SQLHDBC dbc, SQLUSMALLINT option, SQLULEN value)
{
    return OdbcSetConnectAttr(dbc, option, (SQLPOINTER)value, SQL_NTS, TRUE);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetConnectOptionW(SQLHDBC dbc, SQLUSMALLINT option, SQLPOINTER value)
{
    return OdbcGetConnectAttr(dbc, option, value, SQL_MAX_OPTION_STRING_LENGTH * sizeof(WCHAR), nullptr, FALSE);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetConnectOptionA(SQLHDBC dbc, SQLUSMALLINT option, SQLPOINTER value)
{
    return OdbcGetConnectAttr(dbc, option, value, SQL_MAX_OPTION_STRING_LENGTH, nullptr, TRUE);
}

/* ------------------------------------------------------------------ connecting */

/* a connection string or a data source the registry does not know has no driver: IM002, and IM003 when it will not load. */
static SQLRETURN OdbcConnectResult(SQLHDBC handle, SQLRETURN rc)
{
    if (rc != ODBC_NO_DRIVER && rc != ODBC_DRIVER_NOT_LOADED)
        return rc;
    OdbcLock();
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    if (!dbc) {
        OdbcUnlock();
        return SQL_INVALID_HANDLE;
    }
    OdbcBegin(&dbc->h);
    rc = OdbcFail(&dbc->h, rc == ODBC_NO_DRIVER ? ODBC_E_IM002 : ODBC_E_IM003);
    OdbcUnlock();
    return rc;
}

static SQLRETURN OdbcConnect(SQLHDBC handle, BOOL ansi, const void *dsn, SQLSMALLINT dsn_length, const void *user,
                             SQLSMALLINT user_length, const void *password, SQLSMALLINT password_length)
{
    SQLLEN dsn_chars = !dsn ? 0 : dsn_length != SQL_NTS ? dsn_length : ansi ? (SQLLEN)strlen(dsn) : (SQLLEN)wcslen(dsn);
    OdbcLock();
    SQLRETURN rc = SQL_INVALID_HANDLE;
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    if (dbc) {
        OdbcBegin(&dbc->h);
        if (OdbcBadLength(dsn_length) || OdbcBadLength(user_length) || OdbcBadLength(password_length))
            rc = OdbcFail(&dbc->h, ODBC_E_HY090);
        else if (dsn_chars > SQL_MAX_DSN_LENGTH)
            rc = OdbcFail(&dbc->h, ODBC_E_IM010);
        else if (dbc->connected)
            rc = OdbcFail(&dbc->h, ODBC_E_08002);
        else if (!dsn)
            rc = OdbcFail(&dbc->h, ODBC_E_IM002);
        else
            rc = SQL_SUCCESS;
    }
    OdbcUnlock();
    if (rc != SQL_SUCCESS)
        return rc;
    rc = OdbcConnectToDriver(handle, 0, ansi, nullptr, dsn, dsn_length, nullptr, 0, nullptr, 0, user, user_length, password,
                             password_length);
    return OdbcConnectResult(handle, rc);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLConnectW(SQLHDBC dbc, SQLWCHAR *dsn, SQLSMALLINT dsn_length,
                                                    SQLWCHAR *user, SQLSMALLINT user_length, SQLWCHAR *password,
                                                    SQLSMALLINT password_length)
{
    return OdbcConnect(dbc, FALSE, dsn, dsn_length, user, user_length, password, password_length);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLConnectA(SQLHDBC dbc, SQLCHAR *dsn, SQLSMALLINT dsn_length, SQLCHAR *user,
                                                    SQLSMALLINT user_length, SQLCHAR *password,
                                                    SQLSMALLINT password_length)
{
    return OdbcConnect(dbc, TRUE, dsn, dsn_length, user, user_length, password, password_length);
}

static SQLRETURN OdbcDriverConnect(SQLHDBC handle, BOOL ansi, int kind, SQLHWND window, const void *in,
                                   SQLSMALLINT in_length, void *out, SQLSMALLINT out_max, SQLSMALLINT *out_length,
                                   SQLUSMALLINT completion)
{
    OdbcLock();
    SQLRETURN rc = SQL_INVALID_HANDLE;
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    if (dbc) {
        OdbcBegin(&dbc->h);
        if (OdbcBadLength(in_length) || out_max < 0)
            rc = OdbcFail(&dbc->h, ODBC_E_HY090);
        else if (completion > SQL_DRIVER_COMPLETE_REQUIRED)
            rc = OdbcFail(&dbc->h, ODBC_E_HY110);
        else if (dbc->connected)
            rc = OdbcFail(&dbc->h, ODBC_E_08002);
        else if (!in)
            rc = OdbcFail(&dbc->h, ODBC_E_IM002);
        else
            rc = SQL_SUCCESS;
    }
    OdbcUnlock();
    if (rc != SQL_SUCCESS)
        return rc;
    rc = OdbcConnectToDriver(handle, kind, ansi, window, in, in_length, out, out_max, out_length, completion, nullptr, 0,
                             nullptr, 0);
    return OdbcConnectResult(handle, rc);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLDriverConnectW(SQLHDBC dbc, SQLHWND window, SQLWCHAR *in,
                                                          SQLSMALLINT in_length, SQLWCHAR *out, SQLSMALLINT out_max,
                                                          SQLSMALLINT *out_length, SQLUSMALLINT completion)
{
    return OdbcDriverConnect(dbc, FALSE, 1, window, in, in_length, out, (SQLSMALLINT)(out_max * 1), out_length, completion);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLDriverConnectA(SQLHDBC dbc, SQLHWND window, SQLCHAR *in,
                                                          SQLSMALLINT in_length, SQLCHAR *out, SQLSMALLINT out_max,
                                                          SQLSMALLINT *out_length, SQLUSMALLINT completion)
{
    return OdbcDriverConnect(dbc, TRUE, 1, window, in, in_length, out, out_max, out_length, completion);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLBrowseConnectW(SQLHDBC dbc, SQLWCHAR *in, SQLSMALLINT in_length,
                                                          SQLWCHAR *out, SQLSMALLINT out_max,
                                                          SQLSMALLINT *out_length)
{
    return OdbcDriverConnect(dbc, FALSE, 2, nullptr, in, in_length, out, out_max, out_length, SQL_DRIVER_NOPROMPT);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLBrowseConnectA(SQLHDBC dbc, SQLCHAR *in, SQLSMALLINT in_length,
                                                          SQLCHAR *out, SQLSMALLINT out_max, SQLSMALLINT *out_length)
{
    return OdbcDriverConnect(dbc, TRUE, 2, nullptr, in, in_length, out, out_max, out_length, SQL_DRIVER_NOPROMPT);
}

static SQLRETURN OdbcDbcFail(SQLHDBC handle, enum OdbcError error)
{
    OdbcLock();
    SQLRETURN rc = SQL_INVALID_HANDLE;
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    if (dbc) {
        OdbcBegin(&dbc->h);
        rc = OdbcFail(&dbc->h, error);
    }
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLDisconnect(SQLHDBC handle)
{
    OdbcLock();
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    BOOL open = dbc && dbc->connected;
    OdbcUnlock();
    return open ? OdbcDisconnectDriver(handle) : OdbcDbcFail(handle, ODBC_E_08003);
}

/* ------------------------------------------------------------------ information */

typedef SQLRETURN(SQL_API *GET_INFO_FN)(SQLHDBC, SQLUSMALLINT, SQLPOINTER, SQLSMALLINT, SQLSMALLINT *);

static SQLRETURN OdbcGetInfo(SQLHDBC handle, SQLUSMALLINT type, SQLPOINTER value, SQLSMALLINT buffer_length,
                             SQLSMALLINT *length, BOOL ansi)
{
    OdbcLock();
    SQLRETURN rc = SQL_INVALID_HANDLE;
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    if (dbc) {
        OdbcBegin(&dbc->h);
        if (dbc->connected && type != SQL_ODBC_VER) {
            GET_INFO_FN fn = OdbcDrvFn(dbc->driver, ansi ? "SQLGetInfo" : "SQLGetInfoW");
            SQLHDBC drv = dbc->drv_dbc;
            dbc->diag_driver = TRUE;
            OdbcUnlock();
            return fn ? fn(drv, type, value, buffer_length, length) : SQL_ERROR;
        }
        if (type != SQL_ODBC_VER) {
            rc = OdbcFail(&dbc->h, ODBC_E_08003);
        } else {
            SQLINTEGER full;
            rc = OdbcOutputAttrString(dbc, L"03.80.0000", value, buffer_length, &full, ansi);
            if (length && rc != SQL_ERROR)
                *length = (SQLSMALLINT)full;
        }
    }
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetInfoW(SQLHDBC dbc, SQLUSMALLINT type, SQLPOINTER value,
                                                    SQLSMALLINT buffer_length, SQLSMALLINT *length)
{
    return OdbcGetInfo(dbc, type, value, buffer_length, length, FALSE);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetInfoA(SQLHDBC dbc, SQLUSMALLINT type, SQLPOINTER value,
                                                    SQLSMALLINT buffer_length, SQLSMALLINT *length)
{
    return OdbcGetInfo(dbc, type, value, buffer_length, length, TRUE);
}

typedef SQLRETURN(SQL_API *GET_FUNCTIONS_FN)(SQLHDBC, SQLUSMALLINT, SQLUSMALLINT *);

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetFunctions(SQLHDBC handle, SQLUSMALLINT function, SQLUSMALLINT *supported)
{
    OdbcLock();
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    if (dbc && dbc->connected) {
        GET_FUNCTIONS_FN fn = OdbcDrvFn(dbc->driver, "SQLGetFunctions");
        SQLHDBC drv = dbc->drv_dbc;
        OdbcBegin(&dbc->h);
        dbc->diag_driver = TRUE;
        OdbcUnlock();
        return fn ? fn(drv, function, supported) : SQL_ERROR;
    }
    OdbcUnlock();
    return OdbcDbcFail(handle, ODBC_E_HY010);
}

typedef SQLRETURN(SQL_API *NATIVE_SQL_FN)(SQLHDBC, const void *, SQLINTEGER, void *, SQLINTEGER, SQLINTEGER *);

static SQLRETURN OdbcNativeSql(SQLHDBC handle, const void *in, SQLINTEGER in_length, void *out, SQLINTEGER out_max,
                               SQLINTEGER *out_length, BOOL ansi)
{
    OdbcLock();
    SQLRETURN rc = SQL_INVALID_HANDLE;
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    if (dbc) {
        OdbcBegin(&dbc->h);
        if (!in)
            rc = OdbcFail(&dbc->h, ODBC_E_HY009);
        else if (OdbcBadLength(in_length) || (out && out_max < 0))
            rc = OdbcFail(&dbc->h, ODBC_E_HY090);
        else if (dbc->connected) {
            NATIVE_SQL_FN fn = OdbcDrvFn(dbc->driver, ansi ? "SQLNativeSql" : "SQLNativeSqlW");
            SQLHDBC drv = dbc->drv_dbc;
            dbc->diag_driver = TRUE;
            OdbcUnlock();
            return fn ? fn(drv, in, in_length, out, out_max, out_length) : SQL_ERROR;
        } else
            rc = OdbcFail(&dbc->h, ODBC_E_08003);
    }
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLNativeSqlW(SQLHDBC dbc, SQLWCHAR *in, SQLINTEGER in_length, SQLWCHAR *out,
                                                      SQLINTEGER out_max, SQLINTEGER *out_length)
{
    return OdbcNativeSql(dbc, in, in_length, out, out_max, out_length, FALSE);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLNativeSqlA(SQLHDBC dbc, SQLCHAR *in, SQLINTEGER in_length, SQLCHAR *out,
                                                      SQLINTEGER out_max, SQLINTEGER *out_length)
{
    return OdbcNativeSql(dbc, in, in_length, out, out_max, out_length, TRUE);
}

/* ------------------------------------------------------------------ transactions and asynchronous calls */

static SQLRETURN OdbcEndTran(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT completion)
{
    OdbcLock();
    SQLRETURN rc = SQL_INVALID_HANDLE;
    if (type == SQL_HANDLE_ENV || type == SQL_HANDLE_SENV) {
        OdbcEnv *env = OdbcLookupEnv(handle);
        if (env) {
            OdbcBegin(&env->h);
            if (!env->version)
                rc = OdbcFail(&env->h, ODBC_E_HY010);
            else if ((SQLUSMALLINT)completion > SQL_ROLLBACK)
                rc = OdbcFail(&env->h, ODBC_E_HY012);
            else
                rc = OdbcReturn(&env->h, SQL_SUCCESS);
        }
    } else if (type == SQL_HANDLE_DBC) {
        OdbcDbc *dbc = OdbcLookupDbc(handle);
        if (dbc) {
            OdbcBegin(&dbc->h);
            if (dbc->connected && (SQLUSMALLINT)completion <= SQL_ROLLBACK) {
                SQLRETURN(SQL_API * fn)(SQLSMALLINT, SQLHANDLE, SQLSMALLINT) = OdbcDrvFn(dbc->driver, "SQLEndTran");
                SQLHDBC drv = dbc->drv_dbc;
                dbc->diag_driver = TRUE;
                OdbcUnlock();
                return fn ? fn(SQL_HANDLE_DBC, drv, completion) : SQL_ERROR;
            }
            rc = OdbcFail(&dbc->h, (SQLUSMALLINT)completion > SQL_ROLLBACK ? ODBC_E_HY012 : ODBC_E_08003);
        }
    }
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLEndTran(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT completion)
{
    return OdbcEndTran(type, handle, completion);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLTransact(SQLHENV env, SQLHDBC dbc, SQLUSMALLINT completion)
{
    if (dbc)
        return OdbcEndTran(SQL_HANDLE_DBC, dbc, (SQLSMALLINT)completion);
    if (env)
        return OdbcEndTran(SQL_HANDLE_ENV, env, (SQLSMALLINT)completion);
    return SQL_INVALID_HANDLE;
}

typedef SQLRETURN(SQL_API *HANDLE_CALL_FN)(SQLSMALLINT, SQLHANDLE);

DECLSPEC_EXPORT SQLRETURN SQL_API SQLCancelHandle(SQLSMALLINT type, SQLHANDLE handle)
{
    OdbcLock();
    SQLRETURN rc = SQL_INVALID_HANDLE;
    OdbcSub *sub = OdbcFindSub(handle);
    if (sub && sub->type == type) {
        HANDLE_CALL_FN fn = OdbcDrvFn(sub->dbc->driver, "SQLCancelHandle");
        OdbcUnlock();
        return fn ? fn(type, handle) : SQL_ERROR;
    }
    OdbcHandle *h = OdbcLookup(handle, type == SQL_HANDLE_SENV ? SQL_HANDLE_ENV : type);
    if (h) {
        OdbcBegin(h);
        if (h->type == SQL_HANDLE_DBC && ((OdbcDbc *)h)->connected) {
            OdbcDbc *dbc = (OdbcDbc *)h;
            HANDLE_CALL_FN fn = OdbcDrvFn(dbc->driver, "SQLCancelHandle");
            SQLHDBC drv = dbc->drv_dbc;
            dbc->diag_driver = TRUE;
            OdbcUnlock();
            return fn ? fn(type, drv) : SQL_ERROR;
        }
        /* an unopened connection has nothing to cancel. */
        rc = h->type == SQL_HANDLE_DBC ? OdbcReturn(h, SQL_SUCCESS) : OdbcFail(h, ODBC_E_HY092);
    }
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLCompleteAsync(SQLSMALLINT type, SQLHANDLE handle, RETCODE *result)
{
    OdbcLock();
    SQLRETURN rc = SQL_INVALID_HANDLE;
    OdbcSub *sub = OdbcFindSub(handle);
    if (sub && sub->type == type) {
        SQLRETURN(SQL_API * fn)(SQLSMALLINT, SQLHANDLE, RETCODE *) = OdbcDrvFn(sub->dbc->driver, "SQLCompleteAsync");
        OdbcUnlock();
        return fn ? fn(type, handle, result) : SQL_ERROR;
    }
    OdbcHandle *h = type == SQL_HANDLE_DBC ? OdbcLookup(handle, SQL_HANDLE_DBC) : nullptr;
    if (h) {
        OdbcBegin(h);
        OdbcDbc *dbc = (OdbcDbc *)h;
        if (dbc->connected) {
            SQLRETURN(SQL_API * fn)(SQLSMALLINT, SQLHANDLE, RETCODE *) = OdbcDrvFn(dbc->driver, "SQLCompleteAsync");
            SQLHDBC drv = dbc->drv_dbc;
            dbc->diag_driver = TRUE;
            OdbcUnlock();
            return fn ? fn(type, drv, result) : SQL_ERROR;
        }
        rc = OdbcFail(h, ODBC_E_HY010);
    }
    OdbcUnlock();
    return rc;
}

/* ------------------------------------------------------------------ data sources and drivers */

/* both lists are empty; only the arguments are checked. */
static SQLRETURN OdbcEnumerate(SQLHENV handle, SQLUSMALLINT direction, SQLSMALLINT first_max,
                               SQLSMALLINT second_max, BOOL data_sources)
{
    OdbcLock();
    SQLRETURN rc = SQL_INVALID_HANDLE;
    OdbcEnv *env = OdbcLookupEnv(handle);
    if (env) {
        OdbcBegin(&env->h);
        BOOL valid = direction == SQL_FETCH_NEXT || direction == SQL_FETCH_FIRST ||
                     (data_sources && (direction == SQL_FETCH_FIRST_USER || direction == SQL_FETCH_FIRST_SYSTEM));
        if (!env->version)
            rc = OdbcFail(&env->h, ODBC_E_HY010);
        else if (!valid)
            rc = OdbcFail(&env->h, ODBC_E_HY103);
        else if (first_max < 0 || second_max < 0)
            rc = OdbcFail(&env->h, ODBC_E_HY090);
        else
            rc = OdbcReturn(&env->h, SQL_NO_DATA);
    }
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLDataSourcesW(SQLHENV env, SQLUSMALLINT direction, SQLWCHAR *name,
                                                        SQLSMALLINT name_max, SQLSMALLINT *name_length,
                                                        SQLWCHAR *description, SQLSMALLINT description_max,
                                                        SQLSMALLINT *description_length)
{
    return OdbcEnumerate(env, direction, name_max, description_max, TRUE);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLDataSourcesA(SQLHENV env, SQLUSMALLINT direction, SQLCHAR *name,
                                                        SQLSMALLINT name_max, SQLSMALLINT *name_length,
                                                        SQLCHAR *description, SQLSMALLINT description_max,
                                                        SQLSMALLINT *description_length)
{
    return OdbcEnumerate(env, direction, name_max, description_max, TRUE);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLDriversW(SQLHENV env, SQLUSMALLINT direction, SQLWCHAR *description,
                                                    SQLSMALLINT description_max, SQLSMALLINT *description_length,
                                                    SQLWCHAR *attributes, SQLSMALLINT attributes_max,
                                                    SQLSMALLINT *attributes_length)
{
    return OdbcEnumerate(env, direction, description_max, attributes_max, FALSE);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLDriversA(SQLHENV env, SQLUSMALLINT direction, SQLCHAR *description,
                                                    SQLSMALLINT description_max, SQLSMALLINT *description_length,
                                                    SQLCHAR *attributes, SQLSMALLINT attributes_max,
                                                    SQLSMALLINT *attributes_length)
{
    return OdbcEnumerate(env, direction, description_max, attributes_max, FALSE);
}
