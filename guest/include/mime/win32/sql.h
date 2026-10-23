/*
 * odbc32.dll: the types and constants of sqltypes.h, sql.h, sqlext.h and sqlucode.h.
 */
#ifndef MIME_WIN32_SQL_H
#define MIME_WIN32_SQL_H

#include <mime/win32/winbase.h>

#ifdef MIME_BUILD_ODBC32
#define SQL_EXPORT DECLSPEC_EXPORT
#else
#define SQL_EXPORT DECLSPEC_IMPORT
#endif
#define SQL_API WINAPI

#ifndef ODBCVER
#define ODBCVER 0x0380
#endif

/* ------------------------------------------------------------------ types */

typedef unsigned char SQLCHAR;
typedef signed char SQLSCHAR;
typedef unsigned char SQLDATE;
typedef unsigned char SQLDECIMAL;
typedef double SQLDOUBLE;
typedef double SQLFLOAT;
typedef long SQLINTEGER;
typedef unsigned long SQLUINTEGER;
typedef unsigned char SQLNUMERIC;
typedef void *SQLPOINTER;
typedef float SQLREAL;
typedef short SQLSMALLINT;
typedef unsigned short SQLUSMALLINT;
typedef unsigned char SQLTIME;
typedef unsigned char SQLTIMESTAMP;
typedef unsigned char SQLVARCHAR;
typedef WCHAR SQLWCHAR;
typedef INT64 SQLLEN;
typedef UINT64 SQLULEN;
typedef UINT64 SQLSETPOSIROW;
typedef SQLULEN SQLROWCOUNT;
typedef SQLULEN SQLROWSETSIZE;
typedef SQLULEN SQLTRANSID;
typedef SQLLEN SQLROWOFFSET;
typedef INT64 SQLBIGINT;
typedef UINT64 SQLUBIGINT;
typedef SQLSMALLINT SQLRETURN;
typedef void *SQLHANDLE;
typedef SQLHANDLE SQLHENV;
typedef SQLHANDLE SQLHDBC;
typedef SQLHANDLE SQLHSTMT;
typedef SQLHANDLE SQLHDESC;
typedef HWND SQLHWND;
typedef short RETCODE;
typedef void *HENV;
typedef void *HDBC;
typedef void *HSTMT;
typedef GUID SQLGUID;

typedef struct tagDATE_STRUCT {
    SQLSMALLINT year;
    SQLUSMALLINT month;
    SQLUSMALLINT day;
} DATE_STRUCT, SQL_DATE_STRUCT;

typedef struct tagTIME_STRUCT {
    SQLUSMALLINT hour;
    SQLUSMALLINT minute;
    SQLUSMALLINT second;
} TIME_STRUCT, SQL_TIME_STRUCT;

typedef struct tagTIMESTAMP_STRUCT {
    SQLSMALLINT year;
    SQLUSMALLINT month;
    SQLUSMALLINT day;
    SQLUSMALLINT hour;
    SQLUSMALLINT minute;
    SQLUSMALLINT second;
    SQLUINTEGER fraction;
} TIMESTAMP_STRUCT, SQL_TIMESTAMP_STRUCT;

#define SQL_MAX_NUMERIC_LEN 16
typedef struct tagSQL_NUMERIC_STRUCT {
    SQLCHAR precision;
    SQLSCHAR scale;
    SQLCHAR sign;
    SQLCHAR val[SQL_MAX_NUMERIC_LEN];
} SQL_NUMERIC_STRUCT;

/* ------------------------------------------------------------------ return codes and lengths */

#define SQL_SUCCESS 0
#define SQL_SUCCESS_WITH_INFO 1
#define SQL_NO_DATA 100
#define SQL_NO_DATA_FOUND SQL_NO_DATA
#define SQL_PARAM_DATA_AVAILABLE 101
#define SQL_ERROR (-1)
#define SQL_INVALID_HANDLE (-2)
#define SQL_STILL_EXECUTING 2
#define SQL_NEED_DATA 99
#define SQL_SUCCEEDED(rc) (((rc) & (~1)) == 0)

#define SQL_NTS (-3)
#define SQL_NTSL (-3L)
#define SQL_NULL_DATA (-1)
#define SQL_DATA_AT_EXEC (-2)
#define SQL_IS_POINTER (-4)
#define SQL_IS_UINTEGER (-5)
#define SQL_IS_INTEGER (-6)
#define SQL_IS_USMALLINT (-7)
#define SQL_IS_SMALLINT (-8)

#define SQL_MAX_MESSAGE_LENGTH 512
#define SQL_SQLSTATE_SIZE 5
#define SQL_MAX_DSN_LENGTH 32
#define SQL_MAX_OPTION_STRING_LENGTH 256

#define SQL_FALSE 0
#define SQL_TRUE 1

/* ------------------------------------------------------------------ handles */

#define SQL_HANDLE_ENV 1
#define SQL_HANDLE_DBC 2
#define SQL_HANDLE_STMT 3
#define SQL_HANDLE_DESC 4
#define SQL_HANDLE_SENV 5

#define SQL_NULL_HANDLE 0L
#define SQL_NULL_HENV 0
#define SQL_NULL_HDBC 0
#define SQL_NULL_HSTMT 0
#define SQL_NULL_HDESC 0

/* ------------------------------------------------------------------ environment attributes */

#define SQL_ATTR_ODBC_VERSION 200
#define SQL_ATTR_CONNECTION_POOLING 201
#define SQL_ATTR_CP_MATCH 202
#define SQL_ATTR_APPLICATION_KEY 203
#define SQL_ATTR_OUTPUT_NTS 10001

#define SQL_OV_ODBC2 2UL
#define SQL_OV_ODBC3 3UL
#define SQL_OV_ODBC3_80 380UL

#define SQL_CP_OFF 0UL
#define SQL_CP_ONE_PER_DRIVER 1UL
#define SQL_CP_ONE_PER_HENV 2UL
#define SQL_CP_DRIVER_AWARE 3UL
#define SQL_CP_DEFAULT SQL_CP_OFF
#define SQL_CP_STRICT_MATCH 0UL
#define SQL_CP_RELAXED_MATCH 1UL
#define SQL_CP_MATCH_DEFAULT SQL_CP_STRICT_MATCH

/* ------------------------------------------------------------------ connection attributes */

#define SQL_ACCESS_MODE 101
#define SQL_AUTOCOMMIT 102
#define SQL_LOGIN_TIMEOUT 103
#define SQL_OPT_TRACE 104
#define SQL_OPT_TRACEFILE 105
#define SQL_TRANSLATE_DLL 106
#define SQL_TRANSLATE_OPTION 107
#define SQL_TXN_ISOLATION 108
#define SQL_CURRENT_QUALIFIER 109
#define SQL_ODBC_CURSORS 110
#define SQL_QUIET_MODE 111
#define SQL_PACKET_SIZE 112

#define SQL_ATTR_ASYNC_ENABLE 4
#define SQL_ATTR_ACCESS_MODE SQL_ACCESS_MODE
#define SQL_ATTR_AUTOCOMMIT SQL_AUTOCOMMIT
#define SQL_ATTR_LOGIN_TIMEOUT SQL_LOGIN_TIMEOUT
#define SQL_ATTR_TRACE SQL_OPT_TRACE
#define SQL_ATTR_TRACEFILE SQL_OPT_TRACEFILE
#define SQL_ATTR_TRANSLATE_LIB SQL_TRANSLATE_DLL
#define SQL_ATTR_TRANSLATE_OPTION SQL_TRANSLATE_OPTION
#define SQL_ATTR_TXN_ISOLATION SQL_TXN_ISOLATION
#define SQL_ATTR_CURRENT_CATALOG SQL_CURRENT_QUALIFIER
#define SQL_ATTR_ODBC_CURSORS SQL_ODBC_CURSORS
#define SQL_ATTR_QUIET_MODE SQL_QUIET_MODE
#define SQL_ATTR_PACKET_SIZE SQL_PACKET_SIZE
#define SQL_ATTR_CONNECTION_TIMEOUT 113
#define SQL_ATTR_DISCONNECT_BEHAVIOR 114
#define SQL_ATTR_ANSI_APP 115
#define SQL_ATTR_RESET_CONNECTION 116
#define SQL_ATTR_ASYNC_DBC_FUNCTIONS_ENABLE 117
#define SQL_ATTR_DBC_INFO_TOKEN 118
#define SQL_ATTR_ASYNC_DBC_EVENT 119
#define SQL_ATTR_ENLIST_IN_DTC 1207
#define SQL_ATTR_ENLIST_IN_XA 1208
#define SQL_ATTR_CONNECTION_DEAD 1209
#define SQL_ATTR_AUTO_IPD 10001
#define SQL_ATTR_METADATA_ID 10014

#define SQL_MODE_READ_WRITE 0UL
#define SQL_MODE_READ_ONLY 1UL
#define SQL_AUTOCOMMIT_OFF 0UL
#define SQL_AUTOCOMMIT_ON 1UL
#define SQL_OPT_TRACE_OFF 0UL
#define SQL_OPT_TRACE_ON 1UL
#define SQL_OPT_TRACE_FILE_DEFAULT L"\\SQL.LOG"
#define SQL_CUR_USE_IF_NEEDED 0UL
#define SQL_CUR_USE_ODBC 1UL
#define SQL_CUR_USE_DRIVER 2UL
#define SQL_ASYNC_ENABLE_OFF 0UL
#define SQL_ASYNC_ENABLE_ON 1UL
#define SQL_CD_TRUE 1L
#define SQL_CD_FALSE 0L

#define SQL_TXN_READ_UNCOMMITTED 0x00000001L
#define SQL_TXN_READ_COMMITTED 0x00000002L
#define SQL_TXN_REPEATABLE_READ 0x00000004L
#define SQL_TXN_SERIALIZABLE 0x00000008L

/* ------------------------------------------------------------------ statement attributes */

#define SQL_QUERY_TIMEOUT 0
#define SQL_MAX_ROWS 1
#define SQL_NOSCAN 2
#define SQL_MAX_LENGTH 3
#define SQL_ASYNC_ENABLE 4
#define SQL_BIND_TYPE 5
#define SQL_CURSOR_TYPE 6
#define SQL_CONCURRENCY 7
#define SQL_KEYSET_SIZE 8
#define SQL_ROWSET_SIZE 9
#define SQL_SIMULATE_CURSOR 10
#define SQL_RETRIEVE_DATA 11
#define SQL_USE_BOOKMARKS 12
#define SQL_GET_BOOKMARK 13
#define SQL_ROW_NUMBER 14

#define SQL_ATTR_QUERY_TIMEOUT SQL_QUERY_TIMEOUT
#define SQL_ATTR_MAX_ROWS SQL_MAX_ROWS
#define SQL_ATTR_NOSCAN SQL_NOSCAN
#define SQL_ATTR_MAX_LENGTH SQL_MAX_LENGTH
#define SQL_ATTR_ROW_BIND_TYPE SQL_BIND_TYPE
#define SQL_ATTR_CURSOR_TYPE SQL_CURSOR_TYPE
#define SQL_ATTR_CONCURRENCY SQL_CONCURRENCY
#define SQL_ATTR_KEYSET_SIZE SQL_KEYSET_SIZE
#define SQL_ATTR_SIMULATE_CURSOR SQL_SIMULATE_CURSOR
#define SQL_ATTR_RETRIEVE_DATA SQL_RETRIEVE_DATA
#define SQL_ATTR_USE_BOOKMARKS SQL_USE_BOOKMARKS
#define SQL_ATTR_ROW_NUMBER SQL_ROW_NUMBER
#define SQL_ATTR_ENABLE_AUTO_IPD 15
#define SQL_ATTR_FETCH_BOOKMARK_PTR 16
#define SQL_ATTR_PARAM_BIND_OFFSET_PTR 17
#define SQL_ATTR_PARAM_BIND_TYPE 18
#define SQL_ATTR_PARAM_OPERATION_PTR 19
#define SQL_ATTR_PARAM_STATUS_PTR 20
#define SQL_ATTR_PARAMS_PROCESSED_PTR 21
#define SQL_ATTR_PARAMSET_SIZE 22
#define SQL_ATTR_ROW_BIND_OFFSET_PTR 23
#define SQL_ATTR_ROW_OPERATION_PTR 24
#define SQL_ATTR_ROW_STATUS_PTR 25
#define SQL_ATTR_ROWS_FETCHED_PTR 26
#define SQL_ATTR_ROW_ARRAY_SIZE 27
#define SQL_ATTR_ASYNC_STMT_EVENT 29
#define SQL_ATTR_CURSOR_SCROLLABLE (-1)
#define SQL_ATTR_CURSOR_SENSITIVITY (-2)
#define SQL_ATTR_APP_ROW_DESC 10010
#define SQL_ATTR_APP_PARAM_DESC 10011
#define SQL_ATTR_IMP_ROW_DESC 10012
#define SQL_ATTR_IMP_PARAM_DESC 10013

#define SQL_CURSOR_FORWARD_ONLY 0UL
#define SQL_CURSOR_KEYSET_DRIVEN 1UL
#define SQL_CURSOR_DYNAMIC 2UL
#define SQL_CURSOR_STATIC 3UL
#define SQL_CONCUR_READ_ONLY 1
#define SQL_CONCUR_LOCK 2
#define SQL_CONCUR_ROWVER 3
#define SQL_CONCUR_VALUES 4

/* ------------------------------------------------------------------ diagnostics */

#define SQL_DIAG_RETURNCODE 1
#define SQL_DIAG_NUMBER 2
#define SQL_DIAG_ROW_COUNT 3
#define SQL_DIAG_SQLSTATE 4
#define SQL_DIAG_NATIVE 5
#define SQL_DIAG_MESSAGE_TEXT 6
#define SQL_DIAG_DYNAMIC_FUNCTION 7
#define SQL_DIAG_CLASS_ORIGIN 8
#define SQL_DIAG_SUBCLASS_ORIGIN 9
#define SQL_DIAG_CONNECTION_NAME 10
#define SQL_DIAG_SERVER_NAME 11
#define SQL_DIAG_DYNAMIC_FUNCTION_CODE 12
#define SQL_DIAG_CURSOR_ROW_COUNT (-1249)
#define SQL_DIAG_ROW_NUMBER (-1248)
#define SQL_DIAG_COLUMN_NUMBER (-1247)

#define SQL_NO_ROW_NUMBER (-1)
#define SQL_NO_COLUMN_NUMBER (-1)
#define SQL_ROW_NUMBER_UNKNOWN (-2)
#define SQL_COLUMN_NUMBER_UNKNOWN (-2)
#define SQL_DIAG_UNKNOWN_STATEMENT 0

/* ------------------------------------------------------------------ connecting and catalogs */

#define SQL_DRIVER_NOPROMPT 0
#define SQL_DRIVER_COMPLETE 1
#define SQL_DRIVER_PROMPT 2
#define SQL_DRIVER_COMPLETE_REQUIRED 3

#define SQL_FETCH_NEXT 1
#define SQL_FETCH_FIRST 2
#define SQL_FETCH_LAST 3
#define SQL_FETCH_PRIOR 4
#define SQL_FETCH_ABSOLUTE 5
#define SQL_FETCH_RELATIVE 6
#define SQL_FETCH_BOOKMARK 8
#define SQL_FETCH_FIRST_USER 31
#define SQL_FETCH_FIRST_SYSTEM 32

#define SQL_COMMIT 0
#define SQL_ROLLBACK 1

#define SQL_CLOSE 0
#define SQL_DROP 1
#define SQL_UNBIND 2
#define SQL_RESET_PARAMS 3

/* ------------------------------------------------------------------ information types */

#define SQL_ACTIVE_CONNECTIONS 0
#define SQL_MAX_DRIVER_CONNECTIONS 0
#define SQL_ACTIVE_STATEMENTS 1
#define SQL_MAX_CONCURRENT_ACTIVITIES 1
#define SQL_DATA_SOURCE_NAME 2
#define SQL_DRIVER_HDBC 3
#define SQL_DRIVER_HENV 4
#define SQL_DRIVER_HSTMT 5
#define SQL_DRIVER_NAME 6
#define SQL_DRIVER_VER 7
#define SQL_FETCH_DIRECTION 8
#define SQL_ODBC_API_CONFORMANCE 9
#define SQL_ODBC_VER 10
#define SQL_ROW_UPDATES 11
#define SQL_ODBC_SAG_CLI_CONFORMANCE 12
#define SQL_SERVER_NAME 13
#define SQL_SEARCH_PATTERN_ESCAPE 14
#define SQL_DBMS_NAME 17
#define SQL_DBMS_VER 18
#define SQL_DRIVER_ODBC_VER 77
#define SQL_DM_VER 171

/* ------------------------------------------------------------------ function identifiers */

#define SQL_API_ALL_FUNCTIONS 0
#define SQL_API_ODBC3_ALL_FUNCTIONS 999
#define SQL_API_ODBC3_ALL_FUNCTIONS_SIZE 250
#define SQL_API_SQLALLOCCONNECT 1
#define SQL_API_SQLFETCH 13
#define SQL_API_SQLFREESTMT 16
#define SQL_API_SQLALLOCHANDLE 1001
#define SQL_API_SQLCLOSECURSOR 1003
#define SQL_API_SQLCOPYDESC 1004
#define SQL_API_SQLENDTRAN 1005
#define SQL_API_SQLFREEHANDLE 1006
#define SQL_API_SQLGETCONNECTATTR 1007
#define SQL_API_SQLGETDESCFIELD 1008
#define SQL_API_SQLGETDESCREC 1009
#define SQL_API_SQLGETDIAGFIELD 1010
#define SQL_API_SQLGETDIAGREC 1011
#define SQL_API_SQLGETENVATTR 1012
#define SQL_API_SQLGETSTMTATTR 1014
#define SQL_API_SQLSETCONNECTATTR 1016
#define SQL_API_SQLSETDESCFIELD 1017
#define SQL_API_SQLSETDESCREC 1018
#define SQL_API_SQLSETENVATTR 1019
#define SQL_API_SQLSETSTMTATTR 1020
#define SQL_API_SQLFETCHSCROLL 1021
#define SQL_API_SQLCANCELHANDLE 1550
#define SQL_API_SQLCOMPLETEASYNC 1551

/* ------------------------------------------------------------------ data types */

#define SQL_UNKNOWN_TYPE 0
#define SQL_CHAR 1
#define SQL_NUMERIC 2
#define SQL_DECIMAL 3
#define SQL_INTEGER 4
#define SQL_SMALLINT 5
#define SQL_FLOAT 6
#define SQL_REAL 7
#define SQL_DOUBLE 8
#define SQL_DATETIME 9
#define SQL_VARCHAR 12
#define SQL_TYPE_DATE 91
#define SQL_TYPE_TIME 92
#define SQL_TYPE_TIMESTAMP 93
#define SQL_LONGVARCHAR (-1)
#define SQL_BINARY (-2)
#define SQL_VARBINARY (-3)
#define SQL_LONGVARBINARY (-4)
#define SQL_BIGINT (-5)
#define SQL_TINYINT (-6)
#define SQL_BIT (-7)
#define SQL_WCHAR (-8)
#define SQL_WVARCHAR (-9)
#define SQL_WLONGVARCHAR (-10)
#define SQL_GUID (-11)
#define SQL_ALL_TYPES 0
#define SQL_DEFAULT 99

#define SQL_C_CHAR SQL_CHAR
#define SQL_C_WCHAR SQL_WCHAR
#define SQL_C_LONG SQL_INTEGER
#define SQL_C_SHORT SQL_SMALLINT
#define SQL_C_FLOAT SQL_REAL
#define SQL_C_DOUBLE SQL_DOUBLE
#define SQL_C_NUMERIC SQL_NUMERIC
#define SQL_C_DEFAULT 99
#define SQL_C_BINARY SQL_BINARY
#define SQL_C_BIT SQL_BIT
#define SQL_C_TINYINT SQL_TINYINT
#define SQL_C_TYPE_DATE SQL_TYPE_DATE
#define SQL_C_TYPE_TIME SQL_TYPE_TIME
#define SQL_C_TYPE_TIMESTAMP SQL_TYPE_TIMESTAMP
#define SQL_SIGNED_OFFSET (-20)
#define SQL_UNSIGNED_OFFSET (-22)
#define SQL_C_SBIGINT (SQL_BIGINT + SQL_SIGNED_OFFSET)
#define SQL_C_UBIGINT (SQL_BIGINT + SQL_UNSIGNED_OFFSET)
#define SQL_C_GUID SQL_GUID

#define SQL_PARAM_INPUT 1
#define SQL_PARAM_INPUT_OUTPUT 2
#define SQL_PARAM_OUTPUT 4

#define SQL_NO_NULLS 0
#define SQL_NULLABLE 1
#define SQL_NULLABLE_UNKNOWN 2

/* ------------------------------------------------------------------ environment and connection functions */

SQL_EXPORT SQLRETURN SQL_API SQLAllocConnect(SQLHENV env, SQLHDBC *dbc);
SQL_EXPORT SQLRETURN SQL_API SQLAllocEnv(SQLHENV *env);
SQL_EXPORT SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT type, SQLHANDLE input, SQLHANDLE *output);
SQL_EXPORT SQLRETURN SQL_API SQLAllocHandleStd(SQLSMALLINT type, SQLHANDLE input, SQLHANDLE *output);
SQL_EXPORT SQLRETURN SQL_API SQLAllocStmt(SQLHDBC dbc, SQLHSTMT *stmt);
SQL_EXPORT SQLRETURN SQL_API SQLFreeConnect(SQLHDBC dbc);
SQL_EXPORT SQLRETURN SQL_API SQLFreeEnv(SQLHENV env);
SQL_EXPORT SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT type, SQLHANDLE handle);
SQL_EXPORT SQLRETURN SQL_API SQLFreeStmt(SQLHSTMT stmt, SQLUSMALLINT option);

SQL_EXPORT SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV env, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLGetEnvAttr(SQLHENV env, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER buffer_length,
                                           SQLINTEGER *length);

SQL_EXPORT SQLRETURN SQL_API SQLSetConnectAttr(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLSetConnectAttrA(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLSetConnectAttrW(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLGetConnectAttr(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value,
                                               SQLINTEGER buffer_length, SQLINTEGER *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetConnectAttrA(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value,
                                                SQLINTEGER buffer_length, SQLINTEGER *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetConnectAttrW(SQLHDBC dbc, SQLINTEGER attribute, SQLPOINTER value,
                                                SQLINTEGER buffer_length, SQLINTEGER *length);
SQL_EXPORT SQLRETURN SQL_API SQLSetConnectOption(SQLHDBC dbc, SQLUSMALLINT option, SQLULEN value);
SQL_EXPORT SQLRETURN SQL_API SQLSetConnectOptionA(SQLHDBC dbc, SQLUSMALLINT option, SQLULEN value);
SQL_EXPORT SQLRETURN SQL_API SQLSetConnectOptionW(SQLHDBC dbc, SQLUSMALLINT option, SQLULEN value);
SQL_EXPORT SQLRETURN SQL_API SQLGetConnectOption(SQLHDBC dbc, SQLUSMALLINT option, SQLPOINTER value);
SQL_EXPORT SQLRETURN SQL_API SQLGetConnectOptionA(SQLHDBC dbc, SQLUSMALLINT option, SQLPOINTER value);
SQL_EXPORT SQLRETURN SQL_API SQLGetConnectOptionW(SQLHDBC dbc, SQLUSMALLINT option, SQLPOINTER value);

SQL_EXPORT SQLRETURN SQL_API SQLConnect(SQLHDBC dbc, SQLCHAR *dsn, SQLSMALLINT dsn_length, SQLCHAR *user,
                                        SQLSMALLINT user_length, SQLCHAR *password, SQLSMALLINT password_length);
SQL_EXPORT SQLRETURN SQL_API SQLConnectA(SQLHDBC dbc, SQLCHAR *dsn, SQLSMALLINT dsn_length, SQLCHAR *user,
                                         SQLSMALLINT user_length, SQLCHAR *password, SQLSMALLINT password_length);
SQL_EXPORT SQLRETURN SQL_API SQLConnectW(SQLHDBC dbc, SQLWCHAR *dsn, SQLSMALLINT dsn_length, SQLWCHAR *user,
                                         SQLSMALLINT user_length, SQLWCHAR *password, SQLSMALLINT password_length);
SQL_EXPORT SQLRETURN SQL_API SQLDriverConnect(SQLHDBC dbc, SQLHWND window, SQLCHAR *in, SQLSMALLINT in_length,
                                              SQLCHAR *out, SQLSMALLINT out_max, SQLSMALLINT *out_length,
                                              SQLUSMALLINT completion);
SQL_EXPORT SQLRETURN SQL_API SQLDriverConnectA(SQLHDBC dbc, SQLHWND window, SQLCHAR *in, SQLSMALLINT in_length,
                                               SQLCHAR *out, SQLSMALLINT out_max, SQLSMALLINT *out_length,
                                               SQLUSMALLINT completion);
SQL_EXPORT SQLRETURN SQL_API SQLDriverConnectW(SQLHDBC dbc, SQLHWND window, SQLWCHAR *in, SQLSMALLINT in_length,
                                               SQLWCHAR *out, SQLSMALLINT out_max, SQLSMALLINT *out_length,
                                               SQLUSMALLINT completion);
SQL_EXPORT SQLRETURN SQL_API SQLBrowseConnect(SQLHDBC dbc, SQLCHAR *in, SQLSMALLINT in_length, SQLCHAR *out,
                                              SQLSMALLINT out_max, SQLSMALLINT *out_length);
SQL_EXPORT SQLRETURN SQL_API SQLBrowseConnectA(SQLHDBC dbc, SQLCHAR *in, SQLSMALLINT in_length, SQLCHAR *out,
                                               SQLSMALLINT out_max, SQLSMALLINT *out_length);
SQL_EXPORT SQLRETURN SQL_API SQLBrowseConnectW(SQLHDBC dbc, SQLWCHAR *in, SQLSMALLINT in_length, SQLWCHAR *out,
                                               SQLSMALLINT out_max, SQLSMALLINT *out_length);
SQL_EXPORT SQLRETURN SQL_API SQLDisconnect(SQLHDBC dbc);

SQL_EXPORT SQLRETURN SQL_API SQLDataSources(SQLHENV env, SQLUSMALLINT direction, SQLCHAR *name, SQLSMALLINT name_max,
                                            SQLSMALLINT *name_length, SQLCHAR *description,
                                            SQLSMALLINT description_max, SQLSMALLINT *description_length);
SQL_EXPORT SQLRETURN SQL_API SQLDataSourcesA(SQLHENV env, SQLUSMALLINT direction, SQLCHAR *name,
                                             SQLSMALLINT name_max, SQLSMALLINT *name_length, SQLCHAR *description,
                                             SQLSMALLINT description_max, SQLSMALLINT *description_length);
SQL_EXPORT SQLRETURN SQL_API SQLDataSourcesW(SQLHENV env, SQLUSMALLINT direction, SQLWCHAR *name,
                                             SQLSMALLINT name_max, SQLSMALLINT *name_length, SQLWCHAR *description,
                                             SQLSMALLINT description_max, SQLSMALLINT *description_length);
SQL_EXPORT SQLRETURN SQL_API SQLDrivers(SQLHENV env, SQLUSMALLINT direction, SQLCHAR *description,
                                        SQLSMALLINT description_max, SQLSMALLINT *description_length,
                                        SQLCHAR *attributes, SQLSMALLINT attributes_max,
                                        SQLSMALLINT *attributes_length);
SQL_EXPORT SQLRETURN SQL_API SQLDriversA(SQLHENV env, SQLUSMALLINT direction, SQLCHAR *description,
                                         SQLSMALLINT description_max, SQLSMALLINT *description_length,
                                         SQLCHAR *attributes, SQLSMALLINT attributes_max,
                                         SQLSMALLINT *attributes_length);
SQL_EXPORT SQLRETURN SQL_API SQLDriversW(SQLHENV env, SQLUSMALLINT direction, SQLWCHAR *description,
                                         SQLSMALLINT description_max, SQLSMALLINT *description_length,
                                         SQLWCHAR *attributes, SQLSMALLINT attributes_max,
                                         SQLSMALLINT *attributes_length);

SQL_EXPORT SQLRETURN SQL_API SQLGetInfo(SQLHDBC dbc, SQLUSMALLINT type, SQLPOINTER value, SQLSMALLINT buffer_length,
                                        SQLSMALLINT *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetInfoA(SQLHDBC dbc, SQLUSMALLINT type, SQLPOINTER value, SQLSMALLINT buffer_length,
                                         SQLSMALLINT *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetInfoW(SQLHDBC dbc, SQLUSMALLINT type, SQLPOINTER value, SQLSMALLINT buffer_length,
                                         SQLSMALLINT *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetFunctions(SQLHDBC dbc, SQLUSMALLINT function, SQLUSMALLINT *supported);
SQL_EXPORT SQLRETURN SQL_API SQLNativeSql(SQLHDBC dbc, SQLCHAR *in, SQLINTEGER in_length, SQLCHAR *out,
                                          SQLINTEGER out_max, SQLINTEGER *out_length);
SQL_EXPORT SQLRETURN SQL_API SQLNativeSqlA(SQLHDBC dbc, SQLCHAR *in, SQLINTEGER in_length, SQLCHAR *out,
                                           SQLINTEGER out_max, SQLINTEGER *out_length);
SQL_EXPORT SQLRETURN SQL_API SQLNativeSqlW(SQLHDBC dbc, SQLWCHAR *in, SQLINTEGER in_length, SQLWCHAR *out,
                                           SQLINTEGER out_max, SQLINTEGER *out_length);
SQL_EXPORT SQLRETURN SQL_API SQLEndTran(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT completion);
SQL_EXPORT SQLRETURN SQL_API SQLTransact(SQLHENV env, SQLHDBC dbc, SQLUSMALLINT completion);
SQL_EXPORT SQLRETURN SQL_API SQLCancelHandle(SQLSMALLINT type, SQLHANDLE handle);
SQL_EXPORT SQLRETURN SQL_API SQLCompleteAsync(SQLSMALLINT type, SQLHANDLE handle, RETCODE *result);

/* ------------------------------------------------------------------ diagnostics */

SQL_EXPORT SQLRETURN SQL_API SQLError(SQLHENV env, SQLHDBC dbc, SQLHSTMT stmt, SQLCHAR *state, SQLINTEGER *native,
                                      SQLCHAR *message, SQLSMALLINT message_max, SQLSMALLINT *message_length);
SQL_EXPORT SQLRETURN SQL_API SQLErrorA(SQLHENV env, SQLHDBC dbc, SQLHSTMT stmt, SQLCHAR *state, SQLINTEGER *native,
                                       SQLCHAR *message, SQLSMALLINT message_max, SQLSMALLINT *message_length);
SQL_EXPORT SQLRETURN SQL_API SQLErrorW(SQLHENV env, SQLHDBC dbc, SQLHSTMT stmt, SQLWCHAR *state, SQLINTEGER *native,
                                       SQLWCHAR *message, SQLSMALLINT message_max, SQLSMALLINT *message_length);
SQL_EXPORT SQLRETURN SQL_API SQLGetDiagRec(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT record, SQLCHAR *state,
                                           SQLINTEGER *native, SQLCHAR *message, SQLSMALLINT message_max,
                                           SQLSMALLINT *message_length);
SQL_EXPORT SQLRETURN SQL_API SQLGetDiagRecA(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT record, SQLCHAR *state,
                                            SQLINTEGER *native, SQLCHAR *message, SQLSMALLINT message_max,
                                            SQLSMALLINT *message_length);
SQL_EXPORT SQLRETURN SQL_API SQLGetDiagRecW(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT record, SQLWCHAR *state,
                                            SQLINTEGER *native, SQLWCHAR *message, SQLSMALLINT message_max,
                                            SQLSMALLINT *message_length);
SQL_EXPORT SQLRETURN SQL_API SQLGetDiagField(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT record,
                                             SQLSMALLINT field, SQLPOINTER value, SQLSMALLINT buffer_length,
                                             SQLSMALLINT *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetDiagFieldA(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT record,
                                              SQLSMALLINT field, SQLPOINTER value, SQLSMALLINT buffer_length,
                                              SQLSMALLINT *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetDiagFieldW(SQLSMALLINT type, SQLHANDLE handle, SQLSMALLINT record,
                                              SQLSMALLINT field, SQLPOINTER value, SQLSMALLINT buffer_length,
                                              SQLSMALLINT *length);

/* ------------------------------------------------------------------ statements */

SQL_EXPORT SQLRETURN SQL_API SQLBindCol(SQLHSTMT stmt, SQLUSMALLINT column, SQLSMALLINT type, SQLPOINTER value,
                                        SQLLEN buffer_length, SQLLEN *indicator);
SQL_EXPORT SQLRETURN SQL_API SQLBindParam(SQLHSTMT stmt, SQLUSMALLINT number, SQLSMALLINT value_type,
                                          SQLSMALLINT parameter_type, SQLULEN size, SQLSMALLINT digits,
                                          SQLPOINTER value, SQLLEN *indicator);
SQL_EXPORT SQLRETURN SQL_API SQLBindParameter(SQLHSTMT stmt, SQLUSMALLINT number, SQLSMALLINT io_type,
                                              SQLSMALLINT value_type, SQLSMALLINT parameter_type, SQLULEN size,
                                              SQLSMALLINT digits, SQLPOINTER value, SQLLEN buffer_length,
                                              SQLLEN *indicator);
SQL_EXPORT SQLRETURN SQL_API SQLBulkOperations(SQLHSTMT stmt, SQLSMALLINT operation);
SQL_EXPORT SQLRETURN SQL_API SQLCancel(SQLHSTMT stmt);
SQL_EXPORT SQLRETURN SQL_API SQLCloseCursor(SQLHSTMT stmt);
SQL_EXPORT SQLRETURN SQL_API SQLColAttribute(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field,
                                             SQLPOINTER text, SQLSMALLINT buffer_length, SQLSMALLINT *length,
                                             SQLLEN *numeric);
SQL_EXPORT SQLRETURN SQL_API SQLColAttributeA(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field,
                                              SQLPOINTER text, SQLSMALLINT buffer_length, SQLSMALLINT *length,
                                              SQLLEN *numeric);
SQL_EXPORT SQLRETURN SQL_API SQLColAttributeW(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field,
                                              SQLPOINTER text, SQLSMALLINT buffer_length, SQLSMALLINT *length,
                                              SQLLEN *numeric);
SQL_EXPORT SQLRETURN SQL_API SQLColAttributes(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field,
                                              SQLPOINTER text, SQLSMALLINT buffer_length, SQLSMALLINT *length,
                                              SQLLEN *numeric);
SQL_EXPORT SQLRETURN SQL_API SQLColAttributesA(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field,
                                               SQLPOINTER text, SQLSMALLINT buffer_length, SQLSMALLINT *length,
                                               SQLLEN *numeric);
SQL_EXPORT SQLRETURN SQL_API SQLColAttributesW(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field,
                                               SQLPOINTER text, SQLSMALLINT buffer_length, SQLSMALLINT *length,
                                               SQLLEN *numeric);
SQL_EXPORT SQLRETURN SQL_API SQLDescribeCol(SQLHSTMT stmt, SQLUSMALLINT column, SQLCHAR *name, SQLSMALLINT name_max,
                                            SQLSMALLINT *name_length, SQLSMALLINT *type, SQLULEN *size,
                                            SQLSMALLINT *digits, SQLSMALLINT *nullable);
SQL_EXPORT SQLRETURN SQL_API SQLDescribeColA(SQLHSTMT stmt, SQLUSMALLINT column, SQLCHAR *name,
                                             SQLSMALLINT name_max, SQLSMALLINT *name_length, SQLSMALLINT *type,
                                             SQLULEN *size, SQLSMALLINT *digits, SQLSMALLINT *nullable);
SQL_EXPORT SQLRETURN SQL_API SQLDescribeColW(SQLHSTMT stmt, SQLUSMALLINT column, SQLWCHAR *name,
                                             SQLSMALLINT name_max, SQLSMALLINT *name_length, SQLSMALLINT *type,
                                             SQLULEN *size, SQLSMALLINT *digits, SQLSMALLINT *nullable);
SQL_EXPORT SQLRETURN SQL_API SQLDescribeParam(SQLHSTMT stmt, SQLUSMALLINT number, SQLSMALLINT *type, SQLULEN *size,
                                              SQLSMALLINT *digits, SQLSMALLINT *nullable);
SQL_EXPORT SQLRETURN SQL_API SQLExecDirect(SQLHSTMT stmt, SQLCHAR *text, SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLExecDirectA(SQLHSTMT stmt, SQLCHAR *text, SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLExecDirectW(SQLHSTMT stmt, SQLWCHAR *text, SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLExecute(SQLHSTMT stmt);
SQL_EXPORT SQLRETURN SQL_API SQLExtendedFetch(SQLHSTMT stmt, SQLUSMALLINT orientation, SQLLEN offset,
                                              SQLULEN *row_count, SQLUSMALLINT *row_status);
SQL_EXPORT SQLRETURN SQL_API SQLFetch(SQLHSTMT stmt);
SQL_EXPORT SQLRETURN SQL_API SQLFetchScroll(SQLHSTMT stmt, SQLSMALLINT orientation, SQLLEN offset);
SQL_EXPORT SQLRETURN SQL_API SQLGetCursorName(SQLHSTMT stmt, SQLCHAR *name, SQLSMALLINT name_max,
                                              SQLSMALLINT *name_length);
SQL_EXPORT SQLRETURN SQL_API SQLGetCursorNameA(SQLHSTMT stmt, SQLCHAR *name, SQLSMALLINT name_max,
                                               SQLSMALLINT *name_length);
SQL_EXPORT SQLRETURN SQL_API SQLGetCursorNameW(SQLHSTMT stmt, SQLWCHAR *name, SQLSMALLINT name_max,
                                               SQLSMALLINT *name_length);
SQL_EXPORT SQLRETURN SQL_API SQLGetData(SQLHSTMT stmt, SQLUSMALLINT column, SQLSMALLINT type, SQLPOINTER value,
                                        SQLLEN buffer_length, SQLLEN *indicator);
SQL_EXPORT SQLRETURN SQL_API SQLGetStmtAttr(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value,
                                            SQLINTEGER buffer_length, SQLINTEGER *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetStmtAttrA(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value,
                                             SQLINTEGER buffer_length, SQLINTEGER *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetStmtAttrW(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value,
                                             SQLINTEGER buffer_length, SQLINTEGER *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetStmtOption(SQLHSTMT stmt, SQLUSMALLINT option, SQLPOINTER value);
SQL_EXPORT SQLRETURN SQL_API SQLGetTypeInfo(SQLHSTMT stmt, SQLSMALLINT type);
SQL_EXPORT SQLRETURN SQL_API SQLGetTypeInfoA(SQLHSTMT stmt, SQLSMALLINT type);
SQL_EXPORT SQLRETURN SQL_API SQLGetTypeInfoW(SQLHSTMT stmt, SQLSMALLINT type);
SQL_EXPORT SQLRETURN SQL_API SQLMoreResults(SQLHSTMT stmt);
SQL_EXPORT SQLRETURN SQL_API SQLNumParams(SQLHSTMT stmt, SQLSMALLINT *count);
SQL_EXPORT SQLRETURN SQL_API SQLNumResultCols(SQLHSTMT stmt, SQLSMALLINT *count);
SQL_EXPORT SQLRETURN SQL_API SQLParamData(SQLHSTMT stmt, SQLPOINTER *value);
SQL_EXPORT SQLRETURN SQL_API SQLParamOptions(SQLHSTMT stmt, SQLULEN rows, SQLULEN *row_number);
SQL_EXPORT SQLRETURN SQL_API SQLPrepare(SQLHSTMT stmt, SQLCHAR *text, SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLPrepareA(SQLHSTMT stmt, SQLCHAR *text, SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLPrepareW(SQLHSTMT stmt, SQLWCHAR *text, SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLPutData(SQLHSTMT stmt, SQLPOINTER data, SQLLEN length);
SQL_EXPORT SQLRETURN SQL_API SQLRowCount(SQLHSTMT stmt, SQLLEN *count);
SQL_EXPORT SQLRETURN SQL_API SQLSetCursorName(SQLHSTMT stmt, SQLCHAR *name, SQLSMALLINT length);
SQL_EXPORT SQLRETURN SQL_API SQLSetCursorNameA(SQLHSTMT stmt, SQLCHAR *name, SQLSMALLINT length);
SQL_EXPORT SQLRETURN SQL_API SQLSetCursorNameW(SQLHSTMT stmt, SQLWCHAR *name, SQLSMALLINT length);
SQL_EXPORT SQLRETURN SQL_API SQLSetParam(SQLHSTMT stmt, SQLUSMALLINT number, SQLSMALLINT value_type,
                                         SQLSMALLINT parameter_type, SQLULEN size, SQLSMALLINT digits,
                                         SQLPOINTER value, SQLLEN *indicator);
SQL_EXPORT SQLRETURN SQL_API SQLSetPos(SQLHSTMT stmt, SQLSETPOSIROW row, SQLUSMALLINT operation,
                                       SQLUSMALLINT lock);
SQL_EXPORT SQLRETURN SQL_API SQLSetScrollOptions(SQLHSTMT stmt, SQLUSMALLINT concurrency, SQLLEN keyset,
                                                 SQLUSMALLINT rowset);
SQL_EXPORT SQLRETURN SQL_API SQLSetStmtAttr(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value,
                                            SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLSetStmtAttrA(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value,
                                             SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLSetStmtAttrW(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value,
                                             SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLSetStmtOption(SQLHSTMT stmt, SQLUSMALLINT option, SQLULEN value);

/* catalog functions take name and length pairs. */
SQL_EXPORT SQLRETURN SQL_API SQLColumnPrivileges(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                 SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                 SQLSMALLINT table_length, SQLCHAR *column,
                                                 SQLSMALLINT column_length);
SQL_EXPORT SQLRETURN SQL_API SQLColumnPrivilegesA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                  SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                  SQLSMALLINT table_length, SQLCHAR *column,
                                                  SQLSMALLINT column_length);
SQL_EXPORT SQLRETURN SQL_API SQLColumnPrivilegesW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                  SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                                  SQLSMALLINT table_length, SQLWCHAR *column,
                                                  SQLSMALLINT column_length);
SQL_EXPORT SQLRETURN SQL_API SQLColumns(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length, SQLCHAR *schema,
                                        SQLSMALLINT schema_length, SQLCHAR *table, SQLSMALLINT table_length,
                                        SQLCHAR *column, SQLSMALLINT column_length);
SQL_EXPORT SQLRETURN SQL_API SQLColumnsA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                         SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                         SQLSMALLINT table_length, SQLCHAR *column, SQLSMALLINT column_length);
SQL_EXPORT SQLRETURN SQL_API SQLColumnsW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                         SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                         SQLSMALLINT table_length, SQLWCHAR *column, SQLSMALLINT column_length);
SQL_EXPORT SQLRETURN SQL_API SQLForeignKeys(SQLHSTMT stmt, SQLCHAR *pk_catalog, SQLSMALLINT pk_catalog_length,
                                            SQLCHAR *pk_schema, SQLSMALLINT pk_schema_length, SQLCHAR *pk_table,
                                            SQLSMALLINT pk_table_length, SQLCHAR *fk_catalog,
                                            SQLSMALLINT fk_catalog_length, SQLCHAR *fk_schema,
                                            SQLSMALLINT fk_schema_length, SQLCHAR *fk_table,
                                            SQLSMALLINT fk_table_length);
SQL_EXPORT SQLRETURN SQL_API SQLForeignKeysA(SQLHSTMT stmt, SQLCHAR *pk_catalog, SQLSMALLINT pk_catalog_length,
                                             SQLCHAR *pk_schema, SQLSMALLINT pk_schema_length, SQLCHAR *pk_table,
                                             SQLSMALLINT pk_table_length, SQLCHAR *fk_catalog,
                                             SQLSMALLINT fk_catalog_length, SQLCHAR *fk_schema,
                                             SQLSMALLINT fk_schema_length, SQLCHAR *fk_table,
                                             SQLSMALLINT fk_table_length);
SQL_EXPORT SQLRETURN SQL_API SQLForeignKeysW(SQLHSTMT stmt, SQLWCHAR *pk_catalog, SQLSMALLINT pk_catalog_length,
                                             SQLWCHAR *pk_schema, SQLSMALLINT pk_schema_length, SQLWCHAR *pk_table,
                                             SQLSMALLINT pk_table_length, SQLWCHAR *fk_catalog,
                                             SQLSMALLINT fk_catalog_length, SQLWCHAR *fk_schema,
                                             SQLSMALLINT fk_schema_length, SQLWCHAR *fk_table,
                                             SQLSMALLINT fk_table_length);
SQL_EXPORT SQLRETURN SQL_API SQLPrimaryKeys(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                            SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                            SQLSMALLINT table_length);
SQL_EXPORT SQLRETURN SQL_API SQLPrimaryKeysA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                             SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                             SQLSMALLINT table_length);
SQL_EXPORT SQLRETURN SQL_API SQLPrimaryKeysW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                             SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                             SQLSMALLINT table_length);
SQL_EXPORT SQLRETURN SQL_API SQLProcedureColumns(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                 SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *procedure,
                                                 SQLSMALLINT procedure_length, SQLCHAR *column,
                                                 SQLSMALLINT column_length);
SQL_EXPORT SQLRETURN SQL_API SQLProcedureColumnsA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                  SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *procedure,
                                                  SQLSMALLINT procedure_length, SQLCHAR *column,
                                                  SQLSMALLINT column_length);
SQL_EXPORT SQLRETURN SQL_API SQLProcedureColumnsW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                  SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *procedure,
                                                  SQLSMALLINT procedure_length, SQLWCHAR *column,
                                                  SQLSMALLINT column_length);
SQL_EXPORT SQLRETURN SQL_API SQLProcedures(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                           SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *procedure,
                                           SQLSMALLINT procedure_length);
SQL_EXPORT SQLRETURN SQL_API SQLProceduresA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                            SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *procedure,
                                            SQLSMALLINT procedure_length);
SQL_EXPORT SQLRETURN SQL_API SQLProceduresW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                            SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *procedure,
                                            SQLSMALLINT procedure_length);
SQL_EXPORT SQLRETURN SQL_API SQLSpecialColumns(SQLHSTMT stmt, SQLUSMALLINT identifier, SQLCHAR *catalog,
                                               SQLSMALLINT catalog_length, SQLCHAR *schema,
                                               SQLSMALLINT schema_length, SQLCHAR *table, SQLSMALLINT table_length,
                                               SQLUSMALLINT scope, SQLUSMALLINT nullable);
SQL_EXPORT SQLRETURN SQL_API SQLSpecialColumnsA(SQLHSTMT stmt, SQLUSMALLINT identifier, SQLCHAR *catalog,
                                                SQLSMALLINT catalog_length, SQLCHAR *schema,
                                                SQLSMALLINT schema_length, SQLCHAR *table, SQLSMALLINT table_length,
                                                SQLUSMALLINT scope, SQLUSMALLINT nullable);
SQL_EXPORT SQLRETURN SQL_API SQLSpecialColumnsW(SQLHSTMT stmt, SQLUSMALLINT identifier, SQLWCHAR *catalog,
                                                SQLSMALLINT catalog_length, SQLWCHAR *schema,
                                                SQLSMALLINT schema_length, SQLWCHAR *table,
                                                SQLSMALLINT table_length, SQLUSMALLINT scope, SQLUSMALLINT nullable);
SQL_EXPORT SQLRETURN SQL_API SQLStatistics(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                           SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                           SQLSMALLINT table_length, SQLUSMALLINT unique, SQLUSMALLINT reserved);
SQL_EXPORT SQLRETURN SQL_API SQLStatisticsA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                            SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                            SQLSMALLINT table_length, SQLUSMALLINT unique, SQLUSMALLINT reserved);
SQL_EXPORT SQLRETURN SQL_API SQLStatisticsW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                            SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                            SQLSMALLINT table_length, SQLUSMALLINT unique, SQLUSMALLINT reserved);
SQL_EXPORT SQLRETURN SQL_API SQLTablePrivileges(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                SQLSMALLINT table_length);
SQL_EXPORT SQLRETURN SQL_API SQLTablePrivilegesA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                 SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                 SQLSMALLINT table_length);
SQL_EXPORT SQLRETURN SQL_API SQLTablePrivilegesW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                 SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                                 SQLSMALLINT table_length);
SQL_EXPORT SQLRETURN SQL_API SQLTables(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length, SQLCHAR *schema,
                                       SQLSMALLINT schema_length, SQLCHAR *table, SQLSMALLINT table_length,
                                       SQLCHAR *type, SQLSMALLINT type_length);
SQL_EXPORT SQLRETURN SQL_API SQLTablesA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                        SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                        SQLSMALLINT table_length, SQLCHAR *type, SQLSMALLINT type_length);
SQL_EXPORT SQLRETURN SQL_API SQLTablesW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                        SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                        SQLSMALLINT table_length, SQLWCHAR *type, SQLSMALLINT type_length);

/* ------------------------------------------------------------------ descriptors */

SQL_EXPORT SQLRETURN SQL_API SQLCopyDesc(SQLHDESC source, SQLHDESC target);
SQL_EXPORT SQLRETURN SQL_API SQLGetDescField(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field, SQLPOINTER value,
                                             SQLINTEGER buffer_length, SQLINTEGER *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetDescFieldA(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field,
                                              SQLPOINTER value, SQLINTEGER buffer_length, SQLINTEGER *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetDescFieldW(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field,
                                              SQLPOINTER value, SQLINTEGER buffer_length, SQLINTEGER *length);
SQL_EXPORT SQLRETURN SQL_API SQLGetDescRec(SQLHDESC desc, SQLSMALLINT record, SQLCHAR *name, SQLSMALLINT name_max,
                                           SQLSMALLINT *name_length, SQLSMALLINT *type, SQLSMALLINT *subtype,
                                           SQLLEN *length, SQLSMALLINT *precision, SQLSMALLINT *scale,
                                           SQLSMALLINT *nullable);
SQL_EXPORT SQLRETURN SQL_API SQLGetDescRecA(SQLHDESC desc, SQLSMALLINT record, SQLCHAR *name, SQLSMALLINT name_max,
                                            SQLSMALLINT *name_length, SQLSMALLINT *type, SQLSMALLINT *subtype,
                                            SQLLEN *length, SQLSMALLINT *precision, SQLSMALLINT *scale,
                                            SQLSMALLINT *nullable);
SQL_EXPORT SQLRETURN SQL_API SQLGetDescRecW(SQLHDESC desc, SQLSMALLINT record, SQLWCHAR *name, SQLSMALLINT name_max,
                                            SQLSMALLINT *name_length, SQLSMALLINT *type, SQLSMALLINT *subtype,
                                            SQLLEN *length, SQLSMALLINT *precision, SQLSMALLINT *scale,
                                            SQLSMALLINT *nullable);
SQL_EXPORT SQLRETURN SQL_API SQLSetDescField(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field, SQLPOINTER value,
                                             SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLSetDescFieldA(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field,
                                              SQLPOINTER value, SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLSetDescFieldW(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field,
                                              SQLPOINTER value, SQLINTEGER length);
SQL_EXPORT SQLRETURN SQL_API SQLSetDescRec(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT type, SQLSMALLINT subtype,
                                           SQLLEN length, SQLSMALLINT precision, SQLSMALLINT scale, SQLPOINTER data,
                                           SQLLEN *string_length, SQLLEN *indicator);

/* ------------------------------------------------------------------ connection pooling */

SQL_EXPORT DWORD SQL_API ODBCGetTryWaitValue(void);
SQL_EXPORT BOOL SQL_API ODBCSetTryWaitValue(DWORD value);

#endif
