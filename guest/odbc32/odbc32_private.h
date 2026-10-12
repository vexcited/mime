/*
 * odbc32 internal interface: handle objects, diagnostics and string helpers.
 */
#ifndef MIME_ODBC32_PRIVATE_H
#define MIME_ODBC32_PRIVATE_H

#define MIME_BUILD_ODBC32
#include <mime/win32/sql.h>
#include <mime/win32/winnls.h>

/* the driver manager's own diagnostics. */
enum OdbcError {
    ODBC_E_01004, /* string data, right truncated. */
    ODBC_E_08002, /* connection name in use. */
    ODBC_E_08003, /* connection not open. */
    ODBC_E_HY001, /* memory allocation error. */
    ODBC_E_HY009, /* invalid use of null pointer. */
    ODBC_E_HY010, /* function sequence error. */
    ODBC_E_HY011, /* attribute cannot be set now. */
    ODBC_E_HY012, /* invalid transaction operation code. */
    ODBC_E_HY024, /* invalid attribute value. */
    ODBC_E_HY090, /* invalid string or buffer length. */
    ODBC_E_HY092, /* invalid attribute/option identifier. */
    ODBC_E_HY095, /* function type out of range. */
    ODBC_E_HY103, /* invalid retrieval code. */
    ODBC_E_HY110, /* invalid driver completion. */
    ODBC_E_HYC00, /* optional feature not implemented. */
    ODBC_E_IM002, /* data source name not found and no default driver specified. */
    ODBC_E_IM010, /* data source name too long. */
    ODBC_E_COUNT,
};

#define ODBC_MAX_DIAGS 16
#define ODBC_MAX_ATTRS 32
/* the longest diagnostic the driver manager builds, prefix included. */
#define ODBC_MESSAGE_MAX 160

typedef struct OdbcEnv OdbcEnv;

/* fields every handle starts with. */
typedef struct OdbcHandle {
    SQLSMALLINT type;
    struct OdbcHandle *next;
    OdbcEnv *env;
    SQLRETURN result;
    USHORT diag_count;
    USHORT diag_read;
    UCHAR diags[ODBC_MAX_DIAGS];
} OdbcHandle;

struct OdbcEnv {
    OdbcHandle h;
    SQLUINTEGER version;
    SQLUINTEGER pooling;
    SQLUINTEGER match;
    ULONG connections;
};

typedef struct OdbcAttr {
    SQLINTEGER id;
    SQLULEN value;
    PWSTR string;
} OdbcAttr;

typedef struct OdbcDbc {
    OdbcHandle h;
    ULONG attr_count;
    OdbcAttr attrs[ODBC_MAX_ATTRS];
} OdbcDbc;

FORCEINLINE PVOID OdbcAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

FORCEINLINE void OdbcFree(PVOID p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* one lock serializes every call into the driver manager. */
void OdbcLock(void);
void OdbcUnlock(void);

/* finds a live handle of the given type, NULL when the value is not one. */
OdbcHandle *OdbcLookup(SQLHANDLE handle, SQLSMALLINT type);
OdbcEnv *OdbcLookupEnv(SQLHANDLE handle);
OdbcDbc *OdbcLookupDbc(SQLHANDLE handle);

/* starts a function on a handle: its previous diagnostics are cleared. */
void OdbcBegin(OdbcHandle *h);
/* records the return code for SQL_DIAG_RETURNCODE and returns it. */
SQLRETURN OdbcReturn(OdbcHandle *h, SQLRETURN rc);
/* posts a diagnostic record; does not change the return code. */
void OdbcPost(OdbcHandle *h, enum OdbcError error);
/* posts a diagnostic and returns SQL_ERROR. */
SQLRETURN OdbcFail(OdbcHandle *h, enum OdbcError error);

/* the sqlstate a handle reports for an error, following its odbc version. */
PCWSTR OdbcState(const OdbcHandle *h, enum OdbcError error);
/* builds the full message text, returns its length in characters. */
int OdbcMessage(enum OdbcError error, PWSTR buffer, int size);

/* true when a length argument is neither SQL_NTS nor positive or zero. */
FORCEINLINE BOOL OdbcBadLength(SQLINTEGER length)
{
    return length < 0 && length != SQL_NTS;
}

/*
 * copies a string into a caller buffer of max units, always terminating it.
 * returns TRUE when it was truncated.
 */
BOOL OdbcCopyW(PCWSTR src, SQLWCHAR *dst, SQLLEN max);
BOOL OdbcCopyA(PCSTR src, SQLCHAR *dst, SQLLEN max);

/* converts between the ansi code page and utf-16; results come from the heap. */
PWSTR OdbcToWide(const SQLCHAR *s, SQLLEN length);
PSTR OdbcToAnsi(PCWSTR s);
PWSTR OdbcDupW(const SQLWCHAR *s, SQLLEN length);

/* dbc helpers shared by the ansi and unicode entry points. */
SQLRETURN OdbcSetConnectAttr(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length, BOOL ansi);
SQLRETURN OdbcGetConnectAttr(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER buffer_length,
                             SQLINTEGER *length, BOOL ansi);
void OdbcFreeDbcAttrs(OdbcDbc *dbc);

#endif
