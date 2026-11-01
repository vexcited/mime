/*
 * msado15.dll internals shared by its source files. ADO here is built on ODBC: a connection string is turned into the
 * one of an installed ODBC driver, and a command runs on a statement of it, whose results the recordsets read.
 */
#ifndef MIME_ADO_PRIVATE_H
#define MIME_ADO_PRIVATE_H

#include <mime/win32/adodb.h>
#include <mime/win32/sql.h>
#include <rtl.h>

extern LONG g_ado_objects;

/* the description of a failure or message, as ADO shows it. */
typedef struct ERROR_DATA {
    LONG Number, Native;
    BSTR Source, Description, State;
} ERROR_DATA;

/* the errors and messages of a connection, shared by the collections that were taken from it. */
typedef struct ERROR_LIST {
    LONG Refs;
    ULONG Count;
    ERROR_DATA *Items;
} ERROR_LIST;

typedef struct STATEMENT STATEMENT;

typedef struct CONNECTION {
    ADOConnection Conn;
    IConnectionPointContainer Container;
    IConnectionPoint Point;
    LONG Refs;
    SQLHENV Env;
    SQLHDBC Dbc;
    BOOL Open;
    /* counts the connections made, so a statement can tell its handle went with the connection it came from. */
    LONG Generation;
    LPWSTR ConnectionString, Provider, DefaultDatabase;
    LONG CommandTimeout, ConnectionTimeout, CursorLocation, Mode, Attributes, Isolation, TransactionLevel;
    ERROR_LIST *Errors;
    ADOConnectionEventsVt *Sink;
    DWORD Cookie;
} CONNECTION;

struct STATEMENT {
    LONG Refs;
    CONNECTION *Connection;
    LONG Generation;
    SQLHSTMT Handle;
    /* the recordset that reads the current result, which a move to the next result closes. */
    struct RECORDSET *Current;
};

typedef struct COLUMN {
    BSTR Name;
    SQLSMALLINT SqlType, Digits, Nullable;
    SQLULEN Size;
    LONG AdoType;
    BOOL Unsigned;
} COLUMN;

typedef struct RECORDSET {
    ADORecordset Rs;
    LONG Refs;
    STATEMENT *Stmt;
    LONG State;
    LONG ColumnCount;
    COLUMN *Columns;
    VARIANT *Row;
    BOOL HasRow, Eof, Bof;
    LONG_PTR Position;
    LONG CursorLocation;
} RECORDSET;

/* the dispatch methods of an object that has no type information, so scripts cannot reach it by name. */
#define DISPATCH_STUBS(prefix, type)                                                                                   \
    static HRESULT STDMETHODCALLTYPE prefix##_GetTypeInfoCount(type *This, UINT *count)                                 \
    {                                                                                                                  \
        if (count)                                                                                                     \
            *count = 0;                                                                                                \
        return S_OK;                                                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_GetTypeInfo(type *This, UINT index, LCID lcid, void **info)              \
    {                                                                                                                  \
        if (info)                                                                                                      \
            *info = nullptr;                                                                                           \
        return DISP_E_BADINDEX;                                                                                        \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_GetIDsOfNames(type *This, REFIID riid, LPOLESTR *names, UINT count,      \
                                                            LCID lcid, DISPID *ids)                                    \
    {                                                                                                                  \
        for (UINT i = 0; ids && i < count; i++)                                                                        \
            ids[i] = DISPID_UNKNOWN;                                                                                   \
        return DISP_E_UNKNOWNNAME;                                                                                     \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_Invoke(type *This, DISPID id, REFIID riid, LCID lcid, WORD flags,        \
                                                     DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep,            \
                                                     UINT *arg_error)                                                  \
    {                                                                                                                  \
        return DISP_E_MEMBERNOTFOUND;                                                                                  \
    }

/* errors.c */
ERROR_LIST *ErrorListCreate(void);
void ErrorListAddRef(ERROR_LIST *list);
void ErrorListRelease(ERROR_LIST *list);
void ErrorListClear(ERROR_LIST *list);
HRESULT ErrorListAdd(ERROR_LIST *list, LONG number, LONG native, LPCWSTR source, LPCWSTR description, LPCWSTR state);
HRESULT ErrorsCreate(ERROR_LIST *list, ADOErrors **errors);
HRESULT ErrorCreate(const ERROR_DATA *data, ADOError **error);
/* the records of the diagnostics of an ODBC handle added to the list, those that are errors or messages. */
void ErrorListAddDiagnostics(ERROR_LIST *list, SQLSMALLINT type, SQLHANDLE handle, LONG error_number);

/* connection.c */
void ConnectionAddRef(CONNECTION *connection);
LONG ConnectionRelease(CONNECTION *connection);
HRESULT ConnectionCreate(REFIID riid, void **object);
HRESULT ConnectionRun(CONNECTION *connection, BSTR command, STATEMENT **out, LONG *affected);
STATEMENT *StatementCreate(CONNECTION *connection, SQLHSTMT handle);
void StatementRelease(STATEMENT *statement);
void ConnectionFireInfo(CONNECTION *connection);
/* whether the statement still has its handle, which a closed connection took. */
BOOL StatementValid(const STATEMENT *statement);

/* recordset.c */
HRESULT RecordsetCreate(STATEMENT *statement, REFIID riid, void **object);
HRESULT RecordsetCreateEmpty(REFIID riid, void **object);
BSTR AdoSysString(LPCWSTR text);

#endif
