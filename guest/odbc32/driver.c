/*
 * the drivers behind the driver manager: a data source or a driver named in a connection string is found in the
 * registry under ODBC.INI and ODBCINST.INI, its library is loaded, and the connection is made with the driver's own
 * handles. the statements and descriptors a connection allocates are the driver's handles, which the manager only
 * remembers as belonging to the connection, so every call on one goes to the driver that made it.
 */
#include "odbc32_private.h"

struct OdbcDriver {
    struct OdbcDriver *next;
    WCHAR path[MAX_PATH];
    HMODULE module;
};

static OdbcDriver *g_drivers;
static OdbcSub *g_subs;

void *OdbcDrvFn(OdbcDriver *driver, const char *name)
{
    return driver ? (void *)GetProcAddress(driver->module, name) : nullptr;
}

/* the library of a driver, loaded once for the process. */
static OdbcDriver *OdbcLoadDriver(PCWSTR path)
{
    OdbcLock();
    for (OdbcDriver *d = g_drivers; d; d = d->next) {
        if (!_wcsicmp(d->path, path)) {
            OdbcUnlock();
            return d;
        }
    }
    OdbcUnlock();
    HMODULE module = LoadLibraryW(path);
    if (!module)
        return nullptr;
    OdbcDriver *driver = OdbcAlloc(sizeof(*driver));
    if (!driver)
        return nullptr;
    lstrcpynW(driver->path, path, MAX_PATH);
    driver->module = module;
    OdbcLock();
    driver->next = g_drivers;
    g_drivers = driver;
    OdbcUnlock();
    return driver;
}

/* ------------------------------------------------------------------ the registry */

static BOOL RegistryString(HKEY root, PCWSTR path, PCWSTR value, PWSTR out, DWORD chars)
{
    HKEY key;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &key))
        return FALSE;
    DWORD size = chars * sizeof(WCHAR), type;
    BOOL ok = !RegQueryValueExW(key, value, nullptr, &type, (LPBYTE)out, &size) && (type == REG_SZ || type == REG_EXPAND_SZ);
    RegCloseKey(key);
    if (ok)
        out[chars - 1] = 0;
    return ok;
}

/* the library of an installed driver, by the name it was installed under. */
static BOOL OdbcDriverPathByName(PCWSTR name, PWSTR path)
{
    WCHAR key[300];
    _snwprintf(key, RTL_NUMBER_OF(key), L"SOFTWARE\\ODBC\\ODBCINST.INI\\%ls", name);
    return RegistryString(HKEY_LOCAL_MACHINE, key, L"Driver", path, MAX_PATH);
}

/* the driver of a data source: the user's list first, then the machine's. */
static BOOL OdbcDriverPathByDsn(PCWSTR dsn, PWSTR path)
{
    WCHAR key[300], value[MAX_PATH];
    _snwprintf(key, RTL_NUMBER_OF(key), L"Software\\ODBC\\ODBC.INI\\%ls", dsn);
    if (!RegistryString(HKEY_CURRENT_USER, key, L"Driver", value, MAX_PATH) &&
        !RegistryString(HKEY_LOCAL_MACHINE, key, L"Driver", value, MAX_PATH))
        return FALSE;
    /* a library, or the name of an installed driver. */
    if (wcschr(value, L'\\') || wcschr(value, L'.')) {
        lstrcpynW(path, value, MAX_PATH);
        return TRUE;
    }
    return OdbcDriverPathByName(value, path);
}

/* the value of a keyword of a connection string, braces removed. */
static BOOL OdbcKeyword(PCWSTR text, PCWSTR keyword, PWSTR out, DWORD chars)
{
    SIZE_T length = wcslen(keyword);
    while (*text) {
        while (*text == L' ' || *text == L';')
            text++;
        PCWSTR start = text;
        while (*text && *text != L'=' && *text != L';')
            text++;
        SIZE_T name_length = text - start;
        while (name_length && start[name_length - 1] == L' ')
            name_length--;
        if (*text != L'=')
            continue;
        text++;
        while (*text == L' ')
            text++;
        DWORD n = 0;
        if (*text == L'{') {
            text++;
            while (*text && !(*text == L'}' && text[1] != L'}')) {
                if (*text == L'}' && text[1] == L'}')
                    text++;
                if (n + 1 < chars)
                    out[n++] = *text;
                text++;
            }
            if (*text == L'}')
                text++;
        } else {
            while (*text && *text != L';') {
                if (n + 1 < chars)
                    out[n++] = *text;
                text++;
            }
            while (n && out[n - 1] == L' ')
                n--;
        }
        out[n] = 0;
        if (name_length == length && !_wcsnicmp(start, keyword, length))
            return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ statements and descriptors */

OdbcSub *OdbcFindSub(SQLHANDLE handle)
{
    for (OdbcSub *s = g_subs; s; s = s->next)
        if (s->handle == handle)
            return s;
    return nullptr;
}

static void OdbcAddSub(OdbcDbc *dbc, SQLHANDLE handle, SQLSMALLINT type)
{
    if (OdbcFindSub(handle))
        return;
    OdbcSub *sub = OdbcAlloc(sizeof(*sub));
    if (!sub)
        return;
    sub->handle = handle;
    sub->type = type;
    sub->dbc = dbc;
    sub->next = g_subs;
    g_subs = sub;
}

void OdbcRemoveSub(SQLHANDLE handle)
{
    for (OdbcSub **link = &g_subs; *link; link = &(*link)->next) {
        if ((*link)->handle == handle) {
            OdbcSub *gone = *link;
            *link = gone->next;
            OdbcFree(gone);
            return;
        }
    }
}

/* the handles of the connection's statements and descriptors go with the connection. */
static void OdbcDropSubs(OdbcDbc *dbc)
{
    OdbcSub **link = &g_subs;
    while (*link) {
        if ((*link)->dbc == dbc) {
            OdbcSub *gone = *link;
            *link = gone->next;
            OdbcFree(gone);
        } else {
            link = &(*link)->next;
        }
    }
}

/* a descriptor a statement hands out is a handle of the driver too. */
void OdbcNoteDescriptor(SQLHANDLE statement, SQLHANDLE descriptor)
{
    OdbcLock();
    OdbcSub *stmt = OdbcFindSub(statement);
    if (stmt && descriptor)
        OdbcAddSub(stmt->dbc, descriptor, SQL_HANDLE_DESC);
    OdbcUnlock();
}

/* the driver of a statement or descriptor handle, and the handle to give it. */
BOOL OdbcSubDriver(SQLHANDLE handle, OdbcDriver **driver, SQLSMALLINT *type)
{
    OdbcLock();
    OdbcSub *sub = OdbcFindSub(handle);
    if (sub) {
        *driver = sub->dbc->driver;
        if (type)
            *type = sub->type;
    }
    OdbcUnlock();
    return sub != nullptr;
}

typedef SQLRETURN(SQL_API *ALLOC_FN)(SQLSMALLINT, SQLHANDLE, SQLHANDLE *);
typedef SQLRETURN(SQL_API *FREE_FN)(SQLSMALLINT, SQLHANDLE);

/* a statement or descriptor on a connection that is open. */
SQLRETURN OdbcDriverAlloc(OdbcDbc *dbc, SQLSMALLINT type, SQLHANDLE *output)
{
    ALLOC_FN alloc = OdbcDrvFn(dbc->driver, "SQLAllocHandle");
    SQLHANDLE drv_dbc = dbc->drv_dbc;
    OdbcUnlock();
    SQLHANDLE handle = nullptr;
    SQLRETURN rc = alloc ? alloc(type, drv_dbc, &handle) : SQL_ERROR;
    OdbcLock();
    if (SQL_SUCCEEDED(rc) && handle) {
        OdbcAddSub(dbc, handle, type);
        *output = handle;
    } else {
        *output = SQL_NULL_HANDLE;
        dbc->diag_driver = TRUE;
    }
    return rc;
}

/* frees a statement or descriptor of a driver. */
SQLRETURN OdbcDriverFree(SQLSMALLINT type, SQLHANDLE handle)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver(handle, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    FREE_FN free_handle = OdbcDrvFn(driver, "SQLFreeHandle");
    SQLRETURN rc = free_handle ? free_handle(type, handle) : SQL_ERROR;
    if (SQL_SUCCEEDED(rc)) {
        OdbcLock();
        OdbcRemoveSub(handle);
        OdbcUnlock();
    }
    return rc;
}

/* ------------------------------------------------------------------ connecting */

typedef SQLRETURN(SQL_API *SET_ENV_FN)(SQLHENV, SQLINTEGER, SQLPOINTER, SQLINTEGER);
typedef SQLRETURN(SQL_API *SET_CONNECT_FN)(SQLHDBC, SQLINTEGER, SQLPOINTER, SQLINTEGER);

/* the driver's environment and connection, with the attributes set before connecting. */
static BOOL OdbcOpenDriverHandles(OdbcDbc *dbc, OdbcDriver *driver, SQLUINTEGER version)
{
    ALLOC_FN alloc = OdbcDrvFn(driver, "SQLAllocHandle");
    if (!alloc)
        return FALSE;
    SQLHANDLE env = nullptr, connection = nullptr;
    if (!SQL_SUCCEEDED(alloc(SQL_HANDLE_ENV, nullptr, &env)))
        return FALSE;
    SET_ENV_FN set_env = OdbcDrvFn(driver, "SQLSetEnvAttr");
    if (set_env)
        set_env(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)(ULONG_PTR)(version ? version : SQL_OV_ODBC3), 0);
    if (!SQL_SUCCEEDED(alloc(SQL_HANDLE_DBC, env, &connection))) {
        FREE_FN free_handle = OdbcDrvFn(driver, "SQLFreeHandle");
        if (free_handle)
            free_handle(SQL_HANDLE_ENV, env);
        return FALSE;
    }
    dbc->driver = driver;
    dbc->drv_env = env;
    dbc->drv_dbc = connection;
    SET_CONNECT_FN set = OdbcDrvFn(driver, "SQLSetConnectAttrW");
    for (ULONG i = 0; set && i < dbc->attr_count; i++) {
        OdbcAttr *attr = &dbc->attrs[i];
        /* the manager traces for itself. */
        if (attr->id == SQL_ATTR_TRACE || attr->id == SQL_ATTR_TRACEFILE)
            continue;
        set(connection, attr->id, attr->string ? (SQLPOINTER)attr->string : (SQLPOINTER)(ULONG_PTR)attr->value,
            attr->string ? SQL_NTS : 0);
    }
    return TRUE;
}

/* lets go of the driver's handles of a connection that is not open. */
void OdbcCloseDriverHandles(OdbcDbc *dbc)
{
    if (!dbc->driver)
        return;
    FREE_FN free_handle = OdbcDrvFn(dbc->driver, "SQLFreeHandle");
    OdbcDropSubs(dbc);
    if (free_handle) {
        if (dbc->drv_dbc)
            free_handle(SQL_HANDLE_DBC, dbc->drv_dbc);
        if (dbc->drv_env)
            free_handle(SQL_HANDLE_ENV, dbc->drv_env);
    }
    dbc->driver = nullptr;
    dbc->drv_env = dbc->drv_dbc = nullptr;
    dbc->connected = FALSE;
    dbc->diag_driver = FALSE;
}

/* the driver a connection string names, or the one of the data source it names. */
static BOOL OdbcResolveDriver(PCWSTR connection_string, PCWSTR dsn, PWSTR path)
{
    WCHAR value[MAX_PATH];
    if (dsn)
        return OdbcDriverPathByDsn(dsn, path);
    if (OdbcKeyword(connection_string, L"DSN", value, MAX_PATH))
        return OdbcDriverPathByDsn(value, path);
    if (OdbcKeyword(connection_string, L"DRIVER", value, MAX_PATH)) {
        if (wcschr(value, L'\\')) {
            lstrcpynW(path, value, MAX_PATH);
            return TRUE;
        }
        return OdbcDriverPathByName(value, path);
    }
    /* without either, the data source named Default. */
    return OdbcDriverPathByDsn(L"Default", path);
}

typedef SQLRETURN(SQL_API *DRIVER_CONNECT_W)(SQLHDBC, SQLHWND, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT,
                                             SQLSMALLINT *, SQLUSMALLINT);
typedef SQLRETURN(SQL_API *DRIVER_CONNECT_A)(SQLHDBC, SQLHWND, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT,
                                             SQLSMALLINT *, SQLUSMALLINT);
typedef SQLRETURN(SQL_API *CONNECT_W)(SQLHDBC, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT);
typedef SQLRETURN(SQL_API *CONNECT_A)(SQLHDBC, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT);
typedef SQLRETURN(SQL_API *BROWSE_W)(SQLHDBC, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLSMALLINT *);
typedef SQLRETURN(SQL_API *BROWSE_A)(SQLHDBC, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLSMALLINT *);

/*
 * connects through the driver the strings name: kind 0 is a data source, 1 a connection string and 2 a browse. the
 * argument checks are the caller's, and so are the answers for a driver that is not there or does not load.
 */
SQLRETURN OdbcConnectToDriver(SQLHDBC handle, int kind, BOOL ansi, SQLHWND window, const void *in, SQLSMALLINT in_length,
                              void *out, SQLSMALLINT out_max, SQLSMALLINT *out_length, SQLUSMALLINT completion,
                              const void *user, SQLSMALLINT user_length, const void *password,
                              SQLSMALLINT password_length)
{
    /* the data source or driver, as wide text whatever the call. */
    PWSTR wide = ansi ? OdbcToWide(in, in_length) : OdbcDupW(in, in_length);
    if (!wide)
        return SQL_ERROR;
    WCHAR path[MAX_PATH];
    BOOL found = OdbcResolveDriver(kind == 0 ? L"" : wide, kind == 0 ? wide : nullptr, path);
    OdbcFree(wide);
    if (!found)
        return ODBC_NO_DRIVER;
    OdbcDriver *driver = OdbcLoadDriver(path);
    if (!driver)
        return ODBC_DRIVER_NOT_LOADED;

    OdbcLock();
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    if (!dbc) {
        OdbcUnlock();
        return SQL_INVALID_HANDLE;
    }
    if (dbc->driver)
        OdbcCloseDriverHandles(dbc);
    SQLUINTEGER version = dbc->h.env->version;
    BOOL opened = OdbcOpenDriverHandles(dbc, driver, version);
    SQLHDBC drv = dbc->drv_dbc;
    OdbcUnlock();
    if (!opened)
        return ODBC_DRIVER_NOT_LOADED;

    SQLRETURN rc = SQL_ERROR;
    if (kind == 0) {
        if (ansi) {
            CONNECT_A fn = OdbcDrvFn(driver, "SQLConnect");
            rc = fn ? fn(drv, (SQLCHAR *)in, in_length, (SQLCHAR *)user, user_length, (SQLCHAR *)password,
                         password_length)
                    : SQL_ERROR;
        } else {
            CONNECT_W fn = OdbcDrvFn(driver, "SQLConnectW");
            rc = fn ? fn(drv, (SQLWCHAR *)in, in_length, (SQLWCHAR *)user, user_length, (SQLWCHAR *)password,
                         password_length)
                    : SQL_ERROR;
        }
    } else if (kind == 1) {
        if (ansi) {
            DRIVER_CONNECT_A fn = OdbcDrvFn(driver, "SQLDriverConnect");
            rc = fn ? fn(drv, window, (SQLCHAR *)in, in_length, out, out_max, out_length, completion) : SQL_ERROR;
        } else {
            DRIVER_CONNECT_W fn = OdbcDrvFn(driver, "SQLDriverConnectW");
            rc = fn ? fn(drv, window, (SQLWCHAR *)in, in_length, out, out_max, out_length, completion) : SQL_ERROR;
        }
    } else {
        if (ansi) {
            BROWSE_A fn = OdbcDrvFn(driver, "SQLBrowseConnect");
            rc = fn ? fn(drv, (SQLCHAR *)in, in_length, out, out_max, out_length) : SQL_ERROR;
        } else {
            BROWSE_W fn = OdbcDrvFn(driver, "SQLBrowseConnectW");
            rc = fn ? fn(drv, (SQLWCHAR *)in, in_length, out, out_max, out_length) : SQL_ERROR;
        }
    }
    OdbcLock();
    dbc = OdbcLookupDbc(handle);
    if (dbc) {
        /* the driver's own diagnostics answer for the call, whether or not it connected. */
        dbc->diag_driver = TRUE;
        dbc->connected = SQL_SUCCEEDED(rc);
        /* browsing goes on while the driver wants more. */
        if (kind == 2 && rc == SQL_NEED_DATA)
            dbc->connected = FALSE;
    }
    OdbcUnlock();
    return rc;
}

typedef SQLRETURN(SQL_API *DISCONNECT_FN)(SQLHDBC);

SQLRETURN OdbcDisconnectDriver(SQLHDBC handle)
{
    OdbcLock();
    OdbcDbc *dbc = OdbcLookupDbc(handle);
    if (!dbc) {
        OdbcUnlock();
        return SQL_INVALID_HANDLE;
    }
    OdbcDriver *driver = dbc->driver;
    SQLHDBC drv = dbc->drv_dbc;
    OdbcUnlock();
    DISCONNECT_FN fn = OdbcDrvFn(driver, "SQLDisconnect");
    SQLRETURN rc = fn ? fn(drv) : SQL_ERROR;
    OdbcLock();
    dbc = OdbcLookupDbc(handle);
    if (dbc) {
        if (SQL_SUCCEEDED(rc)) {
            OdbcCloseDriverHandles(dbc);
        } else {
            dbc->diag_driver = TRUE;
        }
    }
    OdbcUnlock();
    return rc;
}
