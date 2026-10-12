/*
 * statement and descriptor functions. both kinds of handle are allocated on
 * an open connection, which cannot exist without an odbc driver, so no value
 * an application passes can be a valid statement or descriptor handle.
 */
#include "odbc32_private.h"

__declspec(dllexport) SQLRETURN SQL_API SQLBindCol(SQLHSTMT stmt, SQLUSMALLINT column, SQLSMALLINT type,
                                                   SQLPOINTER value, SQLLEN buffer_length, SQLLEN *indicator)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLBindParam(SQLHSTMT stmt, SQLUSMALLINT number, SQLSMALLINT value_type,
                                                     SQLSMALLINT parameter_type, SQLULEN size, SQLSMALLINT digits,
                                                     SQLPOINTER value, SQLLEN *indicator)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLBindParameter(SQLHSTMT stmt, SQLUSMALLINT number, SQLSMALLINT io_type,
                                                         SQLSMALLINT value_type, SQLSMALLINT parameter_type,
                                                         SQLULEN size, SQLSMALLINT digits, SQLPOINTER value,
                                                         SQLLEN buffer_length, SQLLEN *indicator)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLBulkOperations(SQLHSTMT stmt, SQLSMALLINT operation)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLCancel(SQLHSTMT stmt)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLCloseCursor(SQLHSTMT stmt)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLColAttributeA(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field,
                                                         SQLPOINTER text, SQLSMALLINT buffer_length,
                                                         SQLSMALLINT *length, SQLLEN *numeric)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLColAttributeW(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field,
                                                         SQLPOINTER text, SQLSMALLINT buffer_length,
                                                         SQLSMALLINT *length, SQLLEN *numeric)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLColAttributesA(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field,
                                                          SQLPOINTER text, SQLSMALLINT buffer_length,
                                                          SQLSMALLINT *length, SQLLEN *numeric)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLColAttributesW(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field,
                                                          SQLPOINTER text, SQLSMALLINT buffer_length,
                                                          SQLSMALLINT *length, SQLLEN *numeric)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLDescribeColA(SQLHSTMT stmt, SQLUSMALLINT column, SQLCHAR *name,
                                                        SQLSMALLINT name_max, SQLSMALLINT *name_length,
                                                        SQLSMALLINT *type, SQLULEN *size, SQLSMALLINT *digits,
                                                        SQLSMALLINT *nullable)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLDescribeColW(SQLHSTMT stmt, SQLUSMALLINT column, SQLWCHAR *name,
                                                        SQLSMALLINT name_max, SQLSMALLINT *name_length,
                                                        SQLSMALLINT *type, SQLULEN *size, SQLSMALLINT *digits,
                                                        SQLSMALLINT *nullable)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLDescribeParam(SQLHSTMT stmt, SQLUSMALLINT number, SQLSMALLINT *type,
                                                         SQLULEN *size, SQLSMALLINT *digits, SQLSMALLINT *nullable)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLExecDirectA(SQLHSTMT stmt, SQLCHAR *text, SQLINTEGER length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLExecDirectW(SQLHSTMT stmt, SQLWCHAR *text, SQLINTEGER length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLExecute(SQLHSTMT stmt)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLExtendedFetch(SQLHSTMT stmt, SQLUSMALLINT orientation, SQLLEN offset,
                                                         SQLULEN *row_count, SQLUSMALLINT *row_status)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLFetch(SQLHSTMT stmt)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLFetchScroll(SQLHSTMT stmt, SQLSMALLINT orientation, SQLLEN offset)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetCursorNameA(SQLHSTMT stmt, SQLCHAR *name, SQLSMALLINT name_max,
                                                          SQLSMALLINT *name_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetCursorNameW(SQLHSTMT stmt, SQLWCHAR *name, SQLSMALLINT name_max,
                                                          SQLSMALLINT *name_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetData(SQLHSTMT stmt, SQLUSMALLINT column, SQLSMALLINT type,
                                                   SQLPOINTER value, SQLLEN buffer_length, SQLLEN *indicator)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetStmtAttrA(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value,
                                                        SQLINTEGER buffer_length, SQLINTEGER *length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetStmtAttrW(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value,
                                                        SQLINTEGER buffer_length, SQLINTEGER *length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetStmtOption(SQLHSTMT stmt, SQLUSMALLINT option, SQLPOINTER value)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetTypeInfoA(SQLHSTMT stmt, SQLSMALLINT type)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetTypeInfoW(SQLHSTMT stmt, SQLSMALLINT type)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLMoreResults(SQLHSTMT stmt)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLNumParams(SQLHSTMT stmt, SQLSMALLINT *count)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLNumResultCols(SQLHSTMT stmt, SQLSMALLINT *count)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLParamData(SQLHSTMT stmt, SQLPOINTER *value)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLParamOptions(SQLHSTMT stmt, SQLULEN rows, SQLULEN *row_number)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLPrepareA(SQLHSTMT stmt, SQLCHAR *text, SQLINTEGER length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLPrepareW(SQLHSTMT stmt, SQLWCHAR *text, SQLINTEGER length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLPutData(SQLHSTMT stmt, SQLPOINTER data, SQLLEN length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLRowCount(SQLHSTMT stmt, SQLLEN *count)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSetCursorNameA(SQLHSTMT stmt, SQLCHAR *name, SQLSMALLINT length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSetCursorNameW(SQLHSTMT stmt, SQLWCHAR *name, SQLSMALLINT length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSetParam(SQLHSTMT stmt, SQLUSMALLINT number, SQLSMALLINT value_type,
                                                    SQLSMALLINT parameter_type, SQLULEN size, SQLSMALLINT digits,
                                                    SQLPOINTER value, SQLLEN *indicator)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSetPos(SQLHSTMT stmt, SQLSETPOSIROW row, SQLUSMALLINT operation,
                                                  SQLUSMALLINT lock)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSetScrollOptions(SQLHSTMT stmt, SQLUSMALLINT concurrency, SQLLEN keyset,
                                                            SQLUSMALLINT rowset)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSetStmtAttrA(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value,
                                                        SQLINTEGER length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSetStmtAttrW(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value,
                                                        SQLINTEGER length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSetStmtOption(SQLHSTMT stmt, SQLUSMALLINT option, SQLULEN value)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLColumnPrivilegesA(SQLHSTMT stmt, SQLCHAR *catalog,
                                                             SQLSMALLINT catalog_length, SQLCHAR *schema,
                                                             SQLSMALLINT schema_length, SQLCHAR *table,
                                                             SQLSMALLINT table_length, SQLCHAR *column,
                                                             SQLSMALLINT column_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLColumnPrivilegesW(SQLHSTMT stmt, SQLWCHAR *catalog,
                                                             SQLSMALLINT catalog_length, SQLWCHAR *schema,
                                                             SQLSMALLINT schema_length, SQLWCHAR *table,
                                                             SQLSMALLINT table_length, SQLWCHAR *column,
                                                             SQLSMALLINT column_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLColumnsA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                    SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                    SQLSMALLINT table_length, SQLCHAR *column,
                                                    SQLSMALLINT column_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLColumnsW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                    SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                                    SQLSMALLINT table_length, SQLWCHAR *column,
                                                    SQLSMALLINT column_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLForeignKeysA(SQLHSTMT stmt, SQLCHAR *pk_catalog,
                                                        SQLSMALLINT pk_catalog_length, SQLCHAR *pk_schema,
                                                        SQLSMALLINT pk_schema_length, SQLCHAR *pk_table,
                                                        SQLSMALLINT pk_table_length, SQLCHAR *fk_catalog,
                                                        SQLSMALLINT fk_catalog_length, SQLCHAR *fk_schema,
                                                        SQLSMALLINT fk_schema_length, SQLCHAR *fk_table,
                                                        SQLSMALLINT fk_table_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLForeignKeysW(SQLHSTMT stmt, SQLWCHAR *pk_catalog,
                                                        SQLSMALLINT pk_catalog_length, SQLWCHAR *pk_schema,
                                                        SQLSMALLINT pk_schema_length, SQLWCHAR *pk_table,
                                                        SQLSMALLINT pk_table_length, SQLWCHAR *fk_catalog,
                                                        SQLSMALLINT fk_catalog_length, SQLWCHAR *fk_schema,
                                                        SQLSMALLINT fk_schema_length, SQLWCHAR *fk_table,
                                                        SQLSMALLINT fk_table_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLPrimaryKeysA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                        SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                        SQLSMALLINT table_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLPrimaryKeysW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                        SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                                        SQLSMALLINT table_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLProcedureColumnsA(SQLHSTMT stmt, SQLCHAR *catalog,
                                                             SQLSMALLINT catalog_length, SQLCHAR *schema,
                                                             SQLSMALLINT schema_length, SQLCHAR *procedure,
                                                             SQLSMALLINT procedure_length, SQLCHAR *column,
                                                             SQLSMALLINT column_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLProcedureColumnsW(SQLHSTMT stmt, SQLWCHAR *catalog,
                                                             SQLSMALLINT catalog_length, SQLWCHAR *schema,
                                                             SQLSMALLINT schema_length, SQLWCHAR *procedure,
                                                             SQLSMALLINT procedure_length, SQLWCHAR *column,
                                                             SQLSMALLINT column_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLProceduresA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                       SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *procedure,
                                                       SQLSMALLINT procedure_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLProceduresW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                       SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *procedure,
                                                       SQLSMALLINT procedure_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSpecialColumnsA(SQLHSTMT stmt, SQLUSMALLINT identifier, SQLCHAR *catalog,
                                                           SQLSMALLINT catalog_length, SQLCHAR *schema,
                                                           SQLSMALLINT schema_length, SQLCHAR *table,
                                                           SQLSMALLINT table_length, SQLUSMALLINT scope,
                                                           SQLUSMALLINT nullable)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSpecialColumnsW(SQLHSTMT stmt, SQLUSMALLINT identifier, SQLWCHAR *catalog,
                                                           SQLSMALLINT catalog_length, SQLWCHAR *schema,
                                                           SQLSMALLINT schema_length, SQLWCHAR *table,
                                                           SQLSMALLINT table_length, SQLUSMALLINT scope,
                                                           SQLUSMALLINT nullable)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLStatisticsA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                       SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                       SQLSMALLINT table_length, SQLUSMALLINT unique,
                                                       SQLUSMALLINT reserved)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLStatisticsW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                       SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                                       SQLSMALLINT table_length, SQLUSMALLINT unique,
                                                       SQLUSMALLINT reserved)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLTablePrivilegesA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                            SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                            SQLSMALLINT table_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLTablePrivilegesW(SQLHSTMT stmt, SQLWCHAR *catalog,
                                                            SQLSMALLINT catalog_length, SQLWCHAR *schema,
                                                            SQLSMALLINT schema_length, SQLWCHAR *table,
                                                            SQLSMALLINT table_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLTablesA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length,
                                                   SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table,
                                                   SQLSMALLINT table_length, SQLCHAR *type, SQLSMALLINT type_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLTablesW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length,
                                                   SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table,
                                                   SQLSMALLINT table_length, SQLWCHAR *type, SQLSMALLINT type_length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLCopyDesc(SQLHDESC source, SQLHDESC target)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetDescFieldA(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field,
                                                         SQLPOINTER value, SQLINTEGER buffer_length, SQLINTEGER *length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetDescFieldW(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field,
                                                         SQLPOINTER value, SQLINTEGER buffer_length, SQLINTEGER *length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetDescRecA(SQLHDESC desc, SQLSMALLINT record, SQLCHAR *name,
                                                       SQLSMALLINT name_max, SQLSMALLINT *name_length,
                                                       SQLSMALLINT *type, SQLSMALLINT *subtype, SQLLEN *length,
                                                       SQLSMALLINT *precision, SQLSMALLINT *scale,
                                                       SQLSMALLINT *nullable)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLGetDescRecW(SQLHDESC desc, SQLSMALLINT record, SQLWCHAR *name,
                                                       SQLSMALLINT name_max, SQLSMALLINT *name_length,
                                                       SQLSMALLINT *type, SQLSMALLINT *subtype, SQLLEN *length,
                                                       SQLSMALLINT *precision, SQLSMALLINT *scale,
                                                       SQLSMALLINT *nullable)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSetDescFieldA(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field,
                                                         SQLPOINTER value, SQLINTEGER length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSetDescFieldW(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field,
                                                         SQLPOINTER value, SQLINTEGER length)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLSetDescRec(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT type,
                                                      SQLSMALLINT subtype, SQLLEN length, SQLSMALLINT precision,
                                                      SQLSMALLINT scale, SQLPOINTER data, SQLLEN *string_length,
                                                      SQLLEN *indicator)
{
    return SQL_INVALID_HANDLE;
}

__declspec(dllexport) SQLRETURN SQL_API SQLFreeStmt(SQLHSTMT stmt, SQLUSMALLINT option)
{
    return SQL_INVALID_HANDLE;
}
