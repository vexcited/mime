/*
 * environment and connection handles: allocation, validation, release and
 * environment attributes.
 */
#include "odbc32_private.h"

static SRWLOCK g_lock = SRWLOCK_INIT;
static OdbcHandle *g_handles;
/* pooling set with SQLSetEnvAttr on a null environment, inherited by new environments. */
static SQLUINTEGER g_pooling;

void OdbcLock(void)
{
    AcquireSRWLockExclusive(&g_lock);
}

void OdbcUnlock(void)
{
    ReleaseSRWLockExclusive(&g_lock);
}

OdbcHandle *OdbcLookup(SQLHANDLE handle, SQLSMALLINT type)
{
    if (!handle)
        return nullptr;
    for (OdbcHandle *h = g_handles; h; h = h->next) {
        if (h == handle)
            return h->type == type ? h : nullptr;
    }
    return nullptr;
}

OdbcEnv *OdbcLookupEnv(SQLHANDLE handle)
{
    return (OdbcEnv *)OdbcLookup(handle, SQL_HANDLE_ENV);
}

OdbcDbc *OdbcLookupDbc(SQLHANDLE handle)
{
    return (OdbcDbc *)OdbcLookup(handle, SQL_HANDLE_DBC);
}

static void OdbcLink(OdbcHandle *h)
{
    h->next = g_handles;
    g_handles = h;
}

static void OdbcUnlink(OdbcHandle *h)
{
    for (OdbcHandle **link = &g_handles; *link; link = &(*link)->next) {
        if (*link == h) {
            *link = h->next;
            return;
        }
    }
}

/* ------------------------------------------------------------------ allocation */

static SQLRETURN OdbcAllocEnv(SQLHANDLE *output, SQLUINTEGER version)
{
    if (!output)
        return SQL_ERROR;
    OdbcEnv *env = OdbcAlloc(sizeof(*env));
    if (!env) {
        *output = SQL_NULL_HANDLE;
        return SQL_ERROR;
    }
    env->h.type = SQL_HANDLE_ENV;
    env->h.env = env;
    env->version = version;
    env->pooling = g_pooling;
    env->match = SQL_CP_STRICT_MATCH;
    OdbcLink(&env->h);
    *output = env;
    return SQL_SUCCESS;
}

static SQLRETURN OdbcAllocDbc(SQLHANDLE input, SQLHANDLE *output)
{
    OdbcEnv *env = OdbcLookupEnv(input);
    if (!env)
        return SQL_INVALID_HANDLE;
    OdbcBegin(&env->h);
    if (!output)
        return OdbcFail(&env->h, ODBC_E_HY009);
    if (!env->version) {
        *output = SQL_NULL_HANDLE;
        return OdbcFail(&env->h, ODBC_E_HY010);
    }
    OdbcDbc *dbc = OdbcAlloc(sizeof(*dbc));
    if (!dbc) {
        *output = SQL_NULL_HANDLE;
        return OdbcFail(&env->h, ODBC_E_HY001);
    }
    dbc->h.type = SQL_HANDLE_DBC;
    dbc->h.env = env;
    env->connections++;
    OdbcLink(&dbc->h);
    *output = dbc;
    return OdbcReturn(&env->h, SQL_SUCCESS);
}

/* statements and descriptors need an open connection, and are the driver's own handles. */
static SQLRETURN OdbcAllocOnDbc(SQLHANDLE input, SQLHANDLE *output, SQLSMALLINT type)
{
    OdbcDbc *dbc = OdbcLookupDbc(input);
    if (!dbc)
        return SQL_INVALID_HANDLE;
    OdbcBegin(&dbc->h);
    if (!output)
        return OdbcFail(&dbc->h, ODBC_E_HY009);
    *output = SQL_NULL_HANDLE;
    if (dbc->connected)
        return OdbcDriverAlloc(dbc, type, output);
    return OdbcFail(&dbc->h, ODBC_E_08003);
}

static SQLRETURN OdbcAllocHandle(SQLSMALLINT type, SQLHANDLE input, SQLHANDLE *output, SQLUINTEGER version)
{
    switch (type) {
    case SQL_HANDLE_ENV:
    case SQL_HANDLE_SENV:
        if (input)
            return SQL_INVALID_HANDLE;
        return OdbcAllocEnv(output, version);
    case SQL_HANDLE_DBC:
        return OdbcAllocDbc(input, output);
    case SQL_HANDLE_STMT:
    case SQL_HANDLE_DESC:
        return OdbcAllocOnDbc(input, output, type);
    }
    OdbcHandle *h = OdbcLookup(input, SQL_HANDLE_ENV);
    if (!h)
        h = OdbcLookup(input, SQL_HANDLE_DBC);
    if (!h)
        return SQL_INVALID_HANDLE;
    OdbcBegin(h);
    return OdbcFail(h, ODBC_E_HY092);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT type, SQLHANDLE input, SQLHANDLE *output)
{
    OdbcLock();
    SQLRETURN rc = OdbcAllocHandle(type, input, output, 0);
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLAllocHandleStd(SQLSMALLINT type, SQLHANDLE input, SQLHANDLE *output)
{
    OdbcLock();
    SQLRETURN rc = OdbcAllocHandle(type, input, output, SQL_OV_ODBC3);
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLAllocEnv(SQLHENV *env)
{
    OdbcLock();
    SQLRETURN rc = OdbcAllocEnv(env, SQL_OV_ODBC2);
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLAllocConnect(SQLHENV env, SQLHDBC *dbc)
{
    OdbcLock();
    SQLRETURN rc = OdbcAllocDbc(env, dbc);
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLAllocStmt(SQLHDBC dbc, SQLHSTMT *stmt)
{
    OdbcLock();
    SQLRETURN rc = OdbcAllocOnDbc(dbc, stmt, SQL_HANDLE_STMT);
    OdbcUnlock();
    return rc;
}

/* ------------------------------------------------------------------ release */

static SQLRETURN OdbcFreeEnv(SQLHANDLE handle)
{
    OdbcEnv *env = OdbcLookupEnv(handle);
    if (!env)
        return SQL_INVALID_HANDLE;
    OdbcBegin(&env->h);
    if (env->connections)
        return OdbcFail(&env->h, ODBC_E_HY010);
    OdbcUnlink(&env->h);
    OdbcFree(env);
    return SQL_SUCCESS;
}

static SQLRETURN OdbcFreeDbc(SQLHANDLE handle)
{
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    if (!dbc)
        return SQL_INVALID_HANDLE;
    OdbcBegin(&dbc->h);
    if (dbc->connected)
        return OdbcFail(&dbc->h, ODBC_E_HY010);
    OdbcCloseDriverHandles(dbc);
    dbc->h.env->connections--;
    OdbcUnlink(&dbc->h);
    OdbcFreeDbcAttrs(dbc);
    OdbcFree(dbc);
    return SQL_SUCCESS;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT type, SQLHANDLE handle)
{
    OdbcLock();
    SQLRETURN rc = SQL_INVALID_HANDLE;
    if (type == SQL_HANDLE_ENV || type == SQL_HANDLE_SENV)
        rc = OdbcFreeEnv(handle);
    else if (type == SQL_HANDLE_DBC)
        rc = OdbcFreeDbc(handle);
    else if (type == SQL_HANDLE_STMT || type == SQL_HANDLE_DESC) {
        OdbcUnlock();
        return OdbcDriverFree(type, handle);
    }
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLFreeEnv(SQLHENV env)
{
    OdbcLock();
    SQLRETURN rc = OdbcFreeEnv(env);
    OdbcUnlock();
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLFreeConnect(SQLHDBC dbc)
{
    OdbcLock();
    SQLRETURN rc = OdbcFreeDbc(dbc);
    OdbcUnlock();
    return rc;
}

/* ------------------------------------------------------------------ environment attributes */

static SQLRETURN OdbcSetEnvAttr(SQLHENV handle, SQLINTEGER attribute, SQLPOINTER value)
{
    SQLULEN v = (SQLULEN)value;
    if (!handle) {
        if (attribute != SQL_ATTR_CONNECTION_POOLING)
            return SQL_INVALID_HANDLE;
        if (v > SQL_CP_DRIVER_AWARE)
            return SQL_ERROR;
        g_pooling = (SQLUINTEGER)v;
        return SQL_SUCCESS;
    }
    OdbcEnv *env = OdbcLookupEnv(handle);
    if (!env)
        return SQL_INVALID_HANDLE;
    OdbcBegin(&env->h);
    if (!env->version && attribute != SQL_ATTR_ODBC_VERSION)
        return OdbcFail(&env->h, ODBC_E_HY011);
    if (env->connections)
        return OdbcFail(&env->h, ODBC_E_HY010);
    switch (attribute) {
    case SQL_ATTR_ODBC_VERSION:
        if (v != SQL_OV_ODBC2 && v != SQL_OV_ODBC3 && v != SQL_OV_ODBC3_80)
            return OdbcFail(&env->h, ODBC_E_HY024);
        env->version = (SQLUINTEGER)v;
        break;
    case SQL_ATTR_CONNECTION_POOLING:
        if (v > SQL_CP_DRIVER_AWARE)
            return OdbcFail(&env->h, ODBC_E_HY024);
        env->pooling = (SQLUINTEGER)v;
        break;
    case SQL_ATTR_CP_MATCH:
        if (v != SQL_CP_STRICT_MATCH && v != SQL_CP_RELAXED_MATCH)
            return OdbcFail(&env->h, ODBC_E_HY024);
        env->match = (SQLUINTEGER)v;
        break;
    case SQL_ATTR_OUTPUT_NTS:
        if (v != SQL_TRUE)
            return OdbcFail(&env->h, ODBC_E_HYC00);
        break;
    default:
        return OdbcFail(&env->h, ODBC_E_HY092);
    }
    return OdbcReturn(&env->h, SQL_SUCCESS);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV env, SQLINTEGER attribute, SQLPOINTER value,
                                                      SQLINTEGER length)
{
    OdbcLock();
    SQLRETURN rc = OdbcSetEnvAttr(env, attribute, value);
    OdbcUnlock();
    return rc;
}

static SQLRETURN OdbcGetEnvAttr(SQLHENV handle, SQLINTEGER attribute, SQLPOINTER value)
{
    if (!handle) {
        if (attribute != SQL_ATTR_CONNECTION_POOLING)
            return SQL_INVALID_HANDLE;
        if (value)
            *(SQLULEN *)value = g_pooling;
        return SQL_SUCCESS;
    }
    OdbcEnv *env = OdbcLookupEnv(handle);
    if (!env)
        return SQL_INVALID_HANDLE;
    OdbcBegin(&env->h);
    if (!env->version)
        return OdbcFail(&env->h, ODBC_E_HY010);
    if (!value)
        return OdbcReturn(&env->h, SQL_SUCCESS);
    switch (attribute) {
    case SQL_ATTR_ODBC_VERSION:
        *(SQLUINTEGER *)value = env->version;
        break;
    case SQL_ATTR_CONNECTION_POOLING:
        *(SQLUINTEGER *)value = env->pooling;
        break;
    case SQL_ATTR_CP_MATCH:
        *(SQLUINTEGER *)value = env->match;
        break;
    case SQL_ATTR_OUTPUT_NTS:
        *(SQLINTEGER *)value = SQL_TRUE;
        break;
    default:
        return OdbcFail(&env->h, ODBC_E_HY092);
    }
    return OdbcReturn(&env->h, SQL_SUCCESS);
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetEnvAttr(SQLHENV env, SQLINTEGER attribute, SQLPOINTER value,
                                                      SQLINTEGER buffer_length, SQLINTEGER *length)
{
    OdbcLock();
    SQLRETURN rc = OdbcGetEnvAttr(env, attribute, value);
    OdbcUnlock();
    return rc;
}

/* ------------------------------------------------------------------ connection pooling */

DECLSPEC_EXPORT DWORD SQL_API ODBCGetTryWaitValue(void)
{
    return 120;
}

DECLSPEC_EXPORT BOOL SQL_API ODBCSetTryWaitValue(DWORD value)
{
    return TRUE;
}
