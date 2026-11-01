/*
 * statement and descriptor functions. the handles are the driver's own, so each call goes to the driver that made the
 * handle, by the name of the function: the wide functions to the wide ones of the driver and the ansi functions to
 * the ones without a suffix. a handle the manager does not know, which every application gets before it connects, is
 * invalid.
 */
#include "odbc32_private.h"

DECLSPEC_EXPORT SQLRETURN SQL_API SQLBindCol(SQLHSTMT stmt, SQLUSMALLINT column, SQLSMALLINT type, SQLPOINTER value, SQLLEN buffer_length, SQLLEN *indicator)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLSMALLINT, SQLPOINTER, SQLLEN, SQLLEN *) = OdbcDrvFn(driver, "SQLBindCol");
    SQLRETURN rc = fn ? fn(stmt, column, type, value, buffer_length, indicator) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLBindParam(SQLHSTMT stmt, SQLUSMALLINT number, SQLSMALLINT value_type, SQLSMALLINT parameter_type, SQLULEN size, SQLSMALLINT digits, SQLPOINTER value, SQLLEN *indicator)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLSMALLINT, SQLSMALLINT, SQLULEN, SQLSMALLINT, SQLPOINTER, SQLLEN *) = OdbcDrvFn(driver, "SQLBindParam");
    SQLRETURN rc = fn ? fn(stmt, number, value_type, parameter_type, size, digits, value, indicator) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLBindParameter(SQLHSTMT stmt, SQLUSMALLINT number, SQLSMALLINT io_type, SQLSMALLINT value_type, SQLSMALLINT parameter_type, SQLULEN size, SQLSMALLINT digits, SQLPOINTER value, SQLLEN buffer_length, SQLLEN *indicator)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLSMALLINT, SQLSMALLINT, SQLSMALLINT, SQLULEN, SQLSMALLINT, SQLPOINTER, SQLLEN, SQLLEN *) = OdbcDrvFn(driver, "SQLBindParameter");
    SQLRETURN rc = fn ? fn(stmt, number, io_type, value_type, parameter_type, size, digits, value, buffer_length, indicator) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLBulkOperations(SQLHSTMT stmt, SQLSMALLINT operation)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLSMALLINT) = OdbcDrvFn(driver, "SQLBulkOperations");
    SQLRETURN rc = fn ? fn(stmt, operation) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLCancel(SQLHSTMT stmt)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT) = OdbcDrvFn(driver, "SQLCancel");
    SQLRETURN rc = fn ? fn(stmt) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLCloseCursor(SQLHSTMT stmt)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT) = OdbcDrvFn(driver, "SQLCloseCursor");
    SQLRETURN rc = fn ? fn(stmt) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLColAttributeA(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field, SQLPOINTER text, SQLSMALLINT buffer_length, SQLSMALLINT *length, SQLLEN *numeric)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLUSMALLINT, SQLPOINTER, SQLSMALLINT, SQLSMALLINT *, SQLLEN *) = OdbcDrvFn(driver, "SQLColAttribute");
    SQLRETURN rc = fn ? fn(stmt, column, field, text, buffer_length, length, numeric) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLColAttributeW(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field, SQLPOINTER text, SQLSMALLINT buffer_length, SQLSMALLINT *length, SQLLEN *numeric)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLUSMALLINT, SQLPOINTER, SQLSMALLINT, SQLSMALLINT *, SQLLEN *) = OdbcDrvFn(driver, "SQLColAttributeW");
    SQLRETURN rc = fn ? fn(stmt, column, field, text, buffer_length, length, numeric) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLColAttributesA(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field, SQLPOINTER text, SQLSMALLINT buffer_length, SQLSMALLINT *length, SQLLEN *numeric)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLUSMALLINT, SQLPOINTER, SQLSMALLINT, SQLSMALLINT *, SQLLEN *) = OdbcDrvFn(driver, "SQLColAttributes");
    SQLRETURN rc = fn ? fn(stmt, column, field, text, buffer_length, length, numeric) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLColAttributesW(SQLHSTMT stmt, SQLUSMALLINT column, SQLUSMALLINT field, SQLPOINTER text, SQLSMALLINT buffer_length, SQLSMALLINT *length, SQLLEN *numeric)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLUSMALLINT, SQLPOINTER, SQLSMALLINT, SQLSMALLINT *, SQLLEN *) = OdbcDrvFn(driver, "SQLColAttributesW");
    SQLRETURN rc = fn ? fn(stmt, column, field, text, buffer_length, length, numeric) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLDescribeColA(SQLHSTMT stmt, SQLUSMALLINT column, SQLCHAR *name, SQLSMALLINT name_max, SQLSMALLINT *name_length, SQLSMALLINT *type, SQLULEN *size, SQLSMALLINT *digits, SQLSMALLINT *nullable)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLCHAR *, SQLSMALLINT, SQLSMALLINT *, SQLSMALLINT *, SQLULEN *, SQLSMALLINT *, SQLSMALLINT *) = OdbcDrvFn(driver, "SQLDescribeCol");
    SQLRETURN rc = fn ? fn(stmt, column, name, name_max, name_length, type, size, digits, nullable) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLDescribeColW(SQLHSTMT stmt, SQLUSMALLINT column, SQLWCHAR *name, SQLSMALLINT name_max, SQLSMALLINT *name_length, SQLSMALLINT *type, SQLULEN *size, SQLSMALLINT *digits, SQLSMALLINT *nullable)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLSMALLINT *, SQLSMALLINT *, SQLULEN *, SQLSMALLINT *, SQLSMALLINT *) = OdbcDrvFn(driver, "SQLDescribeColW");
    SQLRETURN rc = fn ? fn(stmt, column, name, name_max, name_length, type, size, digits, nullable) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLDescribeParam(SQLHSTMT stmt, SQLUSMALLINT number, SQLSMALLINT *type, SQLULEN *size, SQLSMALLINT *digits, SQLSMALLINT *nullable)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLSMALLINT *, SQLULEN *, SQLSMALLINT *, SQLSMALLINT *) = OdbcDrvFn(driver, "SQLDescribeParam");
    SQLRETURN rc = fn ? fn(stmt, number, type, size, digits, nullable) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLExecDirectA(SQLHSTMT stmt, SQLCHAR *text, SQLINTEGER length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLINTEGER) = OdbcDrvFn(driver, "SQLExecDirect");
    SQLRETURN rc = fn ? fn(stmt, text, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLExecDirectW(SQLHSTMT stmt, SQLWCHAR *text, SQLINTEGER length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLINTEGER) = OdbcDrvFn(driver, "SQLExecDirectW");
    SQLRETURN rc = fn ? fn(stmt, text, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLExecute(SQLHSTMT stmt)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT) = OdbcDrvFn(driver, "SQLExecute");
    SQLRETURN rc = fn ? fn(stmt) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLExtendedFetch(SQLHSTMT stmt, SQLUSMALLINT orientation, SQLLEN offset, SQLULEN *row_count, SQLUSMALLINT *row_status)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLLEN, SQLULEN *, SQLUSMALLINT *) = OdbcDrvFn(driver, "SQLExtendedFetch");
    SQLRETURN rc = fn ? fn(stmt, orientation, offset, row_count, row_status) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLFetch(SQLHSTMT stmt)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT) = OdbcDrvFn(driver, "SQLFetch");
    SQLRETURN rc = fn ? fn(stmt) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLFetchScroll(SQLHSTMT stmt, SQLSMALLINT orientation, SQLLEN offset)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLSMALLINT, SQLLEN) = OdbcDrvFn(driver, "SQLFetchScroll");
    SQLRETURN rc = fn ? fn(stmt, orientation, offset) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetCursorNameA(SQLHSTMT stmt, SQLCHAR *name, SQLSMALLINT name_max, SQLSMALLINT *name_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLSMALLINT, SQLSMALLINT *) = OdbcDrvFn(driver, "SQLGetCursorName");
    SQLRETURN rc = fn ? fn(stmt, name, name_max, name_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetCursorNameW(SQLHSTMT stmt, SQLWCHAR *name, SQLSMALLINT name_max, SQLSMALLINT *name_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLSMALLINT, SQLSMALLINT *) = OdbcDrvFn(driver, "SQLGetCursorNameW");
    SQLRETURN rc = fn ? fn(stmt, name, name_max, name_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetData(SQLHSTMT stmt, SQLUSMALLINT column, SQLSMALLINT type, SQLPOINTER value, SQLLEN buffer_length, SQLLEN *indicator)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLSMALLINT, SQLPOINTER, SQLLEN, SQLLEN *) = OdbcDrvFn(driver, "SQLGetData");
    SQLRETURN rc = fn ? fn(stmt, column, type, value, buffer_length, indicator) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetStmtAttrA(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER buffer_length, SQLINTEGER *length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLINTEGER, SQLPOINTER, SQLINTEGER, SQLINTEGER *) = OdbcDrvFn(driver, "SQLGetStmtAttr");
    SQLRETURN rc = fn ? fn(stmt, attribute, value, buffer_length, length) : SQL_ERROR;
    if (SQL_SUCCEEDED(rc) && value && attribute >= SQL_ATTR_APP_ROW_DESC && attribute <= SQL_ATTR_IMP_PARAM_DESC)
        OdbcNoteDescriptor(stmt, *(SQLHANDLE *)value);
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetStmtAttrW(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER buffer_length, SQLINTEGER *length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLINTEGER, SQLPOINTER, SQLINTEGER, SQLINTEGER *) = OdbcDrvFn(driver, "SQLGetStmtAttrW");
    SQLRETURN rc = fn ? fn(stmt, attribute, value, buffer_length, length) : SQL_ERROR;
    if (SQL_SUCCEEDED(rc) && value && attribute >= SQL_ATTR_APP_ROW_DESC && attribute <= SQL_ATTR_IMP_PARAM_DESC)
        OdbcNoteDescriptor(stmt, *(SQLHANDLE *)value);
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetStmtOption(SQLHSTMT stmt, SQLUSMALLINT option, SQLPOINTER value)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLPOINTER) = OdbcDrvFn(driver, "SQLGetStmtOption");
    SQLRETURN rc = fn ? fn(stmt, option, value) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetTypeInfoA(SQLHSTMT stmt, SQLSMALLINT type)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLSMALLINT) = OdbcDrvFn(driver, "SQLGetTypeInfo");
    SQLRETURN rc = fn ? fn(stmt, type) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetTypeInfoW(SQLHSTMT stmt, SQLSMALLINT type)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLSMALLINT) = OdbcDrvFn(driver, "SQLGetTypeInfoW");
    SQLRETURN rc = fn ? fn(stmt, type) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLMoreResults(SQLHSTMT stmt)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT) = OdbcDrvFn(driver, "SQLMoreResults");
    SQLRETURN rc = fn ? fn(stmt) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLNumParams(SQLHSTMT stmt, SQLSMALLINT *count)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLSMALLINT *) = OdbcDrvFn(driver, "SQLNumParams");
    SQLRETURN rc = fn ? fn(stmt, count) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLNumResultCols(SQLHSTMT stmt, SQLSMALLINT *count)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLSMALLINT *) = OdbcDrvFn(driver, "SQLNumResultCols");
    SQLRETURN rc = fn ? fn(stmt, count) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLParamData(SQLHSTMT stmt, SQLPOINTER *value)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLPOINTER *) = OdbcDrvFn(driver, "SQLParamData");
    SQLRETURN rc = fn ? fn(stmt, value) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLParamOptions(SQLHSTMT stmt, SQLULEN rows, SQLULEN *row_number)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLULEN, SQLULEN *) = OdbcDrvFn(driver, "SQLParamOptions");
    SQLRETURN rc = fn ? fn(stmt, rows, row_number) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLPrepareA(SQLHSTMT stmt, SQLCHAR *text, SQLINTEGER length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLINTEGER) = OdbcDrvFn(driver, "SQLPrepare");
    SQLRETURN rc = fn ? fn(stmt, text, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLPrepareW(SQLHSTMT stmt, SQLWCHAR *text, SQLINTEGER length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLINTEGER) = OdbcDrvFn(driver, "SQLPrepareW");
    SQLRETURN rc = fn ? fn(stmt, text, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLPutData(SQLHSTMT stmt, SQLPOINTER data, SQLLEN length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLPOINTER, SQLLEN) = OdbcDrvFn(driver, "SQLPutData");
    SQLRETURN rc = fn ? fn(stmt, data, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLRowCount(SQLHSTMT stmt, SQLLEN *count)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLLEN *) = OdbcDrvFn(driver, "SQLRowCount");
    SQLRETURN rc = fn ? fn(stmt, count) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetCursorNameA(SQLHSTMT stmt, SQLCHAR *name, SQLSMALLINT length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLSetCursorName");
    SQLRETURN rc = fn ? fn(stmt, name, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetCursorNameW(SQLHSTMT stmt, SQLWCHAR *name, SQLSMALLINT length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLSetCursorNameW");
    SQLRETURN rc = fn ? fn(stmt, name, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetParam(SQLHSTMT stmt, SQLUSMALLINT number, SQLSMALLINT value_type, SQLSMALLINT parameter_type, SQLULEN size, SQLSMALLINT digits, SQLPOINTER value, SQLLEN *indicator)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLSMALLINT, SQLSMALLINT, SQLULEN, SQLSMALLINT, SQLPOINTER, SQLLEN *) = OdbcDrvFn(driver, "SQLSetParam");
    SQLRETURN rc = fn ? fn(stmt, number, value_type, parameter_type, size, digits, value, indicator) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetPos(SQLHSTMT stmt, SQLSETPOSIROW row, SQLUSMALLINT operation, SQLUSMALLINT lock)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLSETPOSIROW, SQLUSMALLINT, SQLUSMALLINT) = OdbcDrvFn(driver, "SQLSetPos");
    SQLRETURN rc = fn ? fn(stmt, row, operation, lock) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetScrollOptions(SQLHSTMT stmt, SQLUSMALLINT concurrency, SQLLEN keyset, SQLUSMALLINT rowset)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLLEN, SQLUSMALLINT) = OdbcDrvFn(driver, "SQLSetScrollOptions");
    SQLRETURN rc = fn ? fn(stmt, concurrency, keyset, rowset) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetStmtAttrA(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLINTEGER, SQLPOINTER, SQLINTEGER) = OdbcDrvFn(driver, "SQLSetStmtAttr");
    SQLRETURN rc = fn ? fn(stmt, attribute, value, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetStmtAttrW(SQLHSTMT stmt, SQLINTEGER attribute, SQLPOINTER value, SQLINTEGER length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLINTEGER, SQLPOINTER, SQLINTEGER) = OdbcDrvFn(driver, "SQLSetStmtAttrW");
    SQLRETURN rc = fn ? fn(stmt, attribute, value, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetStmtOption(SQLHSTMT stmt, SQLUSMALLINT option, SQLULEN value)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLULEN) = OdbcDrvFn(driver, "SQLSetStmtOption");
    SQLRETURN rc = fn ? fn(stmt, option, value) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLColumnPrivilegesA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length, SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table, SQLSMALLINT table_length, SQLCHAR *column, SQLSMALLINT column_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLColumnPrivileges");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, table, table_length, column, column_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLColumnPrivilegesW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length, SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table, SQLSMALLINT table_length, SQLWCHAR *column, SQLSMALLINT column_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLColumnPrivilegesW");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, table, table_length, column, column_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLColumnsA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length, SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table, SQLSMALLINT table_length, SQLCHAR *column, SQLSMALLINT column_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLColumns");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, table, table_length, column, column_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLColumnsW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length, SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table, SQLSMALLINT table_length, SQLWCHAR *column, SQLSMALLINT column_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLColumnsW");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, table, table_length, column, column_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLForeignKeysA(SQLHSTMT stmt, SQLCHAR *pk_catalog, SQLSMALLINT pk_catalog_length, SQLCHAR *pk_schema, SQLSMALLINT pk_schema_length, SQLCHAR *pk_table, SQLSMALLINT pk_table_length, SQLCHAR *fk_catalog, SQLSMALLINT fk_catalog_length, SQLCHAR *fk_schema, SQLSMALLINT fk_schema_length, SQLCHAR *fk_table, SQLSMALLINT fk_table_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLForeignKeys");
    SQLRETURN rc = fn ? fn(stmt, pk_catalog, pk_catalog_length, pk_schema, pk_schema_length, pk_table, pk_table_length, fk_catalog, fk_catalog_length, fk_schema, fk_schema_length, fk_table, fk_table_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLForeignKeysW(SQLHSTMT stmt, SQLWCHAR *pk_catalog, SQLSMALLINT pk_catalog_length, SQLWCHAR *pk_schema, SQLSMALLINT pk_schema_length, SQLWCHAR *pk_table, SQLSMALLINT pk_table_length, SQLWCHAR *fk_catalog, SQLSMALLINT fk_catalog_length, SQLWCHAR *fk_schema, SQLSMALLINT fk_schema_length, SQLWCHAR *fk_table, SQLSMALLINT fk_table_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLForeignKeysW");
    SQLRETURN rc = fn ? fn(stmt, pk_catalog, pk_catalog_length, pk_schema, pk_schema_length, pk_table, pk_table_length, fk_catalog, fk_catalog_length, fk_schema, fk_schema_length, fk_table, fk_table_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLPrimaryKeysA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length, SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table, SQLSMALLINT table_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLPrimaryKeys");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, table, table_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLPrimaryKeysW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length, SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table, SQLSMALLINT table_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLPrimaryKeysW");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, table, table_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLProcedureColumnsA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length, SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *procedure, SQLSMALLINT procedure_length, SQLCHAR *column, SQLSMALLINT column_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLProcedureColumns");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, procedure, procedure_length, column, column_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLProcedureColumnsW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length, SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *procedure, SQLSMALLINT procedure_length, SQLWCHAR *column, SQLSMALLINT column_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLProcedureColumnsW");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, procedure, procedure_length, column, column_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLProceduresA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length, SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *procedure, SQLSMALLINT procedure_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLProcedures");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, procedure, procedure_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLProceduresW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length, SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *procedure, SQLSMALLINT procedure_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLProceduresW");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, procedure, procedure_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSpecialColumnsA(SQLHSTMT stmt, SQLUSMALLINT identifier, SQLCHAR *catalog, SQLSMALLINT catalog_length, SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table, SQLSMALLINT table_length, SQLUSMALLINT scope, SQLUSMALLINT nullable)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLUSMALLINT, SQLUSMALLINT) = OdbcDrvFn(driver, "SQLSpecialColumns");
    SQLRETURN rc = fn ? fn(stmt, identifier, catalog, catalog_length, schema, schema_length, table, table_length, scope, nullable) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSpecialColumnsW(SQLHSTMT stmt, SQLUSMALLINT identifier, SQLWCHAR *catalog, SQLSMALLINT catalog_length, SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table, SQLSMALLINT table_length, SQLUSMALLINT scope, SQLUSMALLINT nullable)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLUSMALLINT, SQLUSMALLINT) = OdbcDrvFn(driver, "SQLSpecialColumnsW");
    SQLRETURN rc = fn ? fn(stmt, identifier, catalog, catalog_length, schema, schema_length, table, table_length, scope, nullable) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLStatisticsA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length, SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table, SQLSMALLINT table_length, SQLUSMALLINT unique, SQLUSMALLINT reserved)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLUSMALLINT, SQLUSMALLINT) = OdbcDrvFn(driver, "SQLStatistics");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, table, table_length, unique, reserved) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLStatisticsW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length, SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table, SQLSMALLINT table_length, SQLUSMALLINT unique, SQLUSMALLINT reserved)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLUSMALLINT, SQLUSMALLINT) = OdbcDrvFn(driver, "SQLStatisticsW");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, table, table_length, unique, reserved) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLTablePrivilegesA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length, SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table, SQLSMALLINT table_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLTablePrivileges");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, table, table_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLTablePrivilegesW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length, SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table, SQLSMALLINT table_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLTablePrivilegesW");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, table, table_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLTablesA(SQLHSTMT stmt, SQLCHAR *catalog, SQLSMALLINT catalog_length, SQLCHAR *schema, SQLSMALLINT schema_length, SQLCHAR *table, SQLSMALLINT table_length, SQLCHAR *type, SQLSMALLINT type_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLTables");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, table, table_length, type, type_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLTablesW(SQLHSTMT stmt, SQLWCHAR *catalog, SQLSMALLINT catalog_length, SQLWCHAR *schema, SQLSMALLINT schema_length, SQLWCHAR *table, SQLSMALLINT table_length, SQLWCHAR *type, SQLSMALLINT type_length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT) = OdbcDrvFn(driver, "SQLTablesW");
    SQLRETURN rc = fn ? fn(stmt, catalog, catalog_length, schema, schema_length, table, table_length, type, type_length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLCopyDesc(SQLHDESC source, SQLHDESC target)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)source, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHDESC, SQLHDESC) = OdbcDrvFn(driver, "SQLCopyDesc");
    SQLRETURN rc = fn ? fn(source, target) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetDescFieldA(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field, SQLPOINTER value, SQLINTEGER buffer_length, SQLINTEGER *length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)desc, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHDESC, SQLSMALLINT, SQLSMALLINT, SQLPOINTER, SQLINTEGER, SQLINTEGER *) = OdbcDrvFn(driver, "SQLGetDescField");
    SQLRETURN rc = fn ? fn(desc, record, field, value, buffer_length, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetDescFieldW(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field, SQLPOINTER value, SQLINTEGER buffer_length, SQLINTEGER *length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)desc, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHDESC, SQLSMALLINT, SQLSMALLINT, SQLPOINTER, SQLINTEGER, SQLINTEGER *) = OdbcDrvFn(driver, "SQLGetDescFieldW");
    SQLRETURN rc = fn ? fn(desc, record, field, value, buffer_length, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetDescRecA(SQLHDESC desc, SQLSMALLINT record, SQLCHAR *name, SQLSMALLINT name_max, SQLSMALLINT *name_length, SQLSMALLINT *type, SQLSMALLINT *subtype, SQLLEN *length, SQLSMALLINT *precision, SQLSMALLINT *scale, SQLSMALLINT *nullable)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)desc, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHDESC, SQLSMALLINT, SQLCHAR *, SQLSMALLINT, SQLSMALLINT *, SQLSMALLINT *, SQLSMALLINT *, SQLLEN *, SQLSMALLINT *, SQLSMALLINT *, SQLSMALLINT *) = OdbcDrvFn(driver, "SQLGetDescRec");
    SQLRETURN rc = fn ? fn(desc, record, name, name_max, name_length, type, subtype, length, precision, scale, nullable) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLGetDescRecW(SQLHDESC desc, SQLSMALLINT record, SQLWCHAR *name, SQLSMALLINT name_max, SQLSMALLINT *name_length, SQLSMALLINT *type, SQLSMALLINT *subtype, SQLLEN *length, SQLSMALLINT *precision, SQLSMALLINT *scale, SQLSMALLINT *nullable)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)desc, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHDESC, SQLSMALLINT, SQLWCHAR *, SQLSMALLINT, SQLSMALLINT *, SQLSMALLINT *, SQLSMALLINT *, SQLLEN *, SQLSMALLINT *, SQLSMALLINT *, SQLSMALLINT *) = OdbcDrvFn(driver, "SQLGetDescRecW");
    SQLRETURN rc = fn ? fn(desc, record, name, name_max, name_length, type, subtype, length, precision, scale, nullable) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetDescFieldA(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field, SQLPOINTER value, SQLINTEGER length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)desc, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHDESC, SQLSMALLINT, SQLSMALLINT, SQLPOINTER, SQLINTEGER) = OdbcDrvFn(driver, "SQLSetDescField");
    SQLRETURN rc = fn ? fn(desc, record, field, value, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetDescFieldW(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT field, SQLPOINTER value, SQLINTEGER length)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)desc, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHDESC, SQLSMALLINT, SQLSMALLINT, SQLPOINTER, SQLINTEGER) = OdbcDrvFn(driver, "SQLSetDescFieldW");
    SQLRETURN rc = fn ? fn(desc, record, field, value, length) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLSetDescRec(SQLHDESC desc, SQLSMALLINT record, SQLSMALLINT type, SQLSMALLINT subtype, SQLLEN length, SQLSMALLINT precision, SQLSMALLINT scale, SQLPOINTER data, SQLLEN *string_length, SQLLEN *indicator)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)desc, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHDESC, SQLSMALLINT, SQLSMALLINT, SQLSMALLINT, SQLLEN, SQLSMALLINT, SQLSMALLINT, SQLPOINTER, SQLLEN *, SQLLEN *) = OdbcDrvFn(driver, "SQLSetDescRec");
    SQLRETURN rc = fn ? fn(desc, record, type, subtype, length, precision, scale, data, string_length, indicator) : SQL_ERROR;
    return rc;
}

DECLSPEC_EXPORT SQLRETURN SQL_API SQLFreeStmt(SQLHSTMT stmt, SQLUSMALLINT option)
{
    OdbcDriver *driver;
    if (!OdbcSubDriver((SQLHANDLE)stmt, &driver, nullptr))
        return SQL_INVALID_HANDLE;
    SQLRETURN(SQL_API * fn)(SQLHSTMT, SQLUSMALLINT) = OdbcDrvFn(driver, "SQLFreeStmt");
    SQLRETURN rc = fn ? fn(stmt, option) : SQL_ERROR;
    if (SQL_SUCCEEDED(rc) && option == SQL_DROP) {
        OdbcLock();
        OdbcRemoveSub(stmt);
        OdbcUnlock();
    }
    return rc;
}
