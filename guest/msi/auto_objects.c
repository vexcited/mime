/*
 * the records, databases, views, summaries and sessions the scripts reach through the installer object.
 */
#include "automation.h"
#include <mime/win32/winnls.h>

#define ENTRY(id, help, flags, method, names) {id, help, flags, method, names}
#define GET DISPATCH_PROPERTYGET
#define PUT DISPATCH_PROPERTYPUT
#define CALL DISPATCH_METHOD
#define COUNT_OF(table) ((ULONG)(sizeof(table) / sizeof((table)[0])))

/* a window of the numbers the installer uses for the state of a script that reports one. */
static HRESULT bad_index(void)
{
    return DISP_E_BADINDEX;
}

/* ------------------------------------------------------------------ record */

static HRESULT record_field_count(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return AutoSetInt(args, (int)MsiRecordGetFieldCount(object->Handle));
}

static HRESULT record_string_data(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, field);
    if (args->Flags & PUT) {
        PCWSTR value = nullptr;
        VARIANT *variant = AutoArgument(args, 0);
        if (variant->vt) {
            HRESULT hr = AutoGetString(variant, &value);
            if (hr)
                return hr;
        }
        return (HRESULT)MsiRecordSetStringW(object->Handle, (UINT)field, value);
    }
    AUTO_TEXT text;
    AutoTextBegin(&text, 1024);
    UINT error;
    do
        error = MsiRecordGetStringW(object->Handle, (UINT)field, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT record_integer_data(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, field);
    if (args->Flags & PUT) {
        AUTO_INT_ARG(args, 0, value);
        return (HRESULT)MsiRecordSetInteger(object->Handle, (UINT)field, value);
    }
    return AutoSetInt(args, MsiRecordGetInteger(object->Handle, (UINT)field));
}

static HRESULT record_set_stream(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, field);
    PCWSTR path = nullptr;
    if (AutoPresent(args, 2) && AutoArgument(args, 2)->vt != VT_NULL) {
        HRESULT hr = AutoGetString(AutoArgument(args, 2), &path);
        if (hr)
            return hr;
    }
    return (HRESULT)MsiRecordSetStreamW(object->Handle, (UINT)field, path);
}

/* the stream of a field as the number, the bytes, the ansi text or the raw bytes of a string. */
static HRESULT record_read_stream(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, field);
    AUTO_INT_ARG(args, 2, length);
    AUTO_INT_ARG(args, 3, format);
    if ((UINT)format > 3)
        return bad_index();
    if (MsiRecordIsNull(object->Handle, (UINT)field))
        return S_OK;
    DWORD size = (DWORD)length;
    UINT error = MsiRecordReadStream(object->Handle, (UINT)field, nullptr, &size);
    if (error)
        return (HRESULT)error;
    if (!size)
        return S_OK;
    if (size > (DWORD)length)
        size = (DWORD)length;
    if (size > 0x7FFFFFFF)
        return bad_index();
    if (format == 0) {
        if (size > 4)
            return bad_index();
        int number = 0;
        MsiRecordReadStream(object->Handle, (UINT)field, (char *)&number, &size);
        return AutoSetInt(args, number);
    }
    if (format == 3) {
        BSTR text = SysAllocStringLen(nullptr, (size + 1) >> 1);
        if (!text)
            return E_OUTOFMEMORY;
        MsiRecordReadStream(object->Handle, (UINT)field, (char *)text, &size);
        if (size & 1)
            ((BYTE *)text)[size] = 0;
        return AutoSetBstr(args, text);
    }
    char *bytes = MimeMsiAllocate(size);
    if (!bytes)
        return E_OUTOFMEMORY;
    MsiRecordReadStream(object->Handle, (UINT)field, bytes, &size);
    BSTR text;
    if (format == 1) {
        text = SysAllocStringLen(nullptr, size);
        if (text) {
            for (DWORD i = 0; i < size; i++)
                text[i] = (BYTE)bytes[i];
            text[size] = 0;
        }
    } else {
        int wide = MultiByteToWideChar(CP_ACP, 0, bytes, (int)size, nullptr, 0);
        text = SysAllocStringLen(nullptr, wide);
        if (text) {
            MultiByteToWideChar(CP_ACP, 0, bytes, (int)size, text, wide);
            text[wide] = 0;
        }
    }
    MimeMsiFree(bytes);
    return text ? AutoSetBstr(args, text) : E_OUTOFMEMORY;
}

static HRESULT record_data_size(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, field);
    return AutoSetInt(args, (int)MsiRecordDataSize(object->Handle, (UINT)field));
}

static HRESULT record_is_null(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, field);
    return AutoSetBool(args, MsiRecordIsNull(object->Handle, (UINT)field));
}

static HRESULT record_clear_data(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return (HRESULT)MsiRecordClearData(object->Handle);
}

static HRESULT record_format_text(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_TEXT text;
    AutoTextBegin(&text, 260);
    UINT error;
    do
        error = MsiFormatRecordW(0, object->Handle, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT object_handle(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return AutoSetInt(args, (int)object->Handle);
}

static const AUTO_ENTRY record_entries[] = {
    ENTRY(0, 9301, GET, record_field_count, L"FieldCount"),
    ENTRY(1, 9302, GET | PUT, record_string_data, L"StringData,Field"),
    ENTRY(2, 9303, GET | PUT, record_integer_data, L"IntegerData,Field"),
    ENTRY(3, 9306, CALL, record_set_stream, L"SetStream,Field,FilePath"),
    ENTRY(4, 9307, CALL, record_read_stream, L"ReadStream,Field,Length,Format"),
    ENTRY(5, 9304, GET, record_data_size, L"DataSize,Field"),
    ENTRY(6, 9305, GET, record_is_null, L"IsNull,Field"),
    ENTRY(7, 9308, CALL, record_clear_data, L"ClearData"),
    ENTRY(8, 9309, CALL, record_format_text, L"FormatText"),
    ENTRY(AUTO_ID_HANDLE, 0, GET, object_handle, L""),
};

AUTO_OBJECT *AutoNewRecord(MSIHANDLE handle)
{
    return AutoAllocate(sizeof(AUTO_OBJECT), record_entries, COUNT_OF(record_entries), &IID_AutoRecord, handle, nullptr);
}

/* ------------------------------------------------------------------ view */

static HRESULT view_execute(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    MSIHANDLE record = 0;
    if (AutoPresent(args, 1))
        record = AutoGetHandle(AutoArgument(args, 1), &IID_AutoRecord);
    return (HRESULT)MsiViewExecute(object->Handle, record);
}

static HRESULT view_fetch(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    MSIHANDLE record = 0;
    UINT error = MsiViewFetch(object->Handle, &record);
    AUTO_OBJECT *result = nullptr;
    if (error == ERROR_NO_MORE_ITEMS)
        return AutoSetObject(args, nullptr);
    if (error)
        return (HRESULT)error;
    result = AutoNewRecord(record);
    return AutoSetObject(args, result);
}

static HRESULT view_modify(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, mode);
    MSIHANDLE record = AutoGetHandle(AutoArgument(args, 2), &IID_AutoRecord);
    return (HRESULT)MsiViewModify(object->Handle, mode, record);
}

static HRESULT view_column_info(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, info);
    MSIHANDLE record = 0;
    UINT error = MsiViewGetColumnInfo(object->Handle, (UINT)info, &record);
    if (error)
        return (HRESULT)error;
    return AutoSetObject(args, AutoNewRecord(record));
}

static HRESULT view_close(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return (HRESULT)MsiViewClose(object->Handle);
}

/* the number of the error as two digits, then the name of the column it is about. */
static HRESULT view_get_error(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_TEXT text;
    AutoTextBegin(&text, 66);
    int error;
    for (;;) {
        DWORD size = text.Size - 2;
        error = (int)MsiViewGetErrorW(object->Handle, text.Buffer + 2, &size);
        text.Size = size + 2;
        if (error != -3 || !AutoTextGrow(&text))
            break;
    }
    HRESULT hr;
    if (error == -1) {
        hr = E_FAIL;
    } else if (error == -2) {
        hr = bad_index();
    } else {
        text.Buffer[0] = (WCHAR)(L'0' + error / 10);
        text.Buffer[1] = (WCHAR)(L'0' + error % 10);
        hr = AutoSetString(args, text.Buffer);
    }
    AutoTextEnd(&text);
    return hr;
}

static const AUTO_ENTRY view_entries[] = {
    ENTRY(1, 9401, CALL, view_execute, L"Execute,Params"),
    ENTRY(2, 9402, CALL, view_fetch, L"Fetch"),
    ENTRY(3, 9403, CALL, view_modify, L"Modify,Mode,Record"),
    ENTRY(5, 9405, GET, view_column_info, L"ColumnInfo,Info"),
    ENTRY(4, 9404, CALL, view_close, L"Close"),
    ENTRY(6, 9406, CALL, view_get_error, L"GetError"),
};

AUTO_OBJECT *AutoNewView(MSIHANDLE handle)
{
    return AutoAllocate(sizeof(AUTO_OBJECT), view_entries, COUNT_OF(view_entries), &IID_AutoView, handle, nullptr);
}

/* ------------------------------------------------------------------ summary information */

static HRESULT summary_property(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, id);
    if (args->Flags & PUT) {
        const VARIANT *value = AutoArgument(args, 0);
        UINT type = 0;
        int number = 0;
        FILETIME time = {};
        PCWSTR text = nullptr;
        double date;
        BOOL is_date = FALSE;
        switch (value->vt) {
        case VT_EMPTY:
            break;
        case VT_I2:
            type = 3;
            number = value->iVal;
            break;
        case VT_I4:
            type = 3;
            number = value->lVal;
            break;
        case VT_R8:
        case VT_DATE:
            is_date = TRUE;
            date = value->dblVal;
            break;
        case VT_BSTR:
            type = 30;
            text = value->bstrVal;
            break;
        case VT_BYREF | VT_I2:
            type = 3;
            number = *(SHORT *)value->byref;
            break;
        case VT_BYREF | VT_I4:
            type = 3;
            number = *(LONG *)value->byref;
            break;
        case VT_BYREF | VT_R8:
        case VT_BYREF | VT_DATE:
            is_date = TRUE;
            date = *(DOUBLE *)value->byref;
            break;
        case VT_BYREF | VT_BSTR:
            type = 30;
            text = *value->pbstrVal;
            break;
        default:
            return DISP_E_TYPEMISMATCH;
        }
        if (is_date) {
            SYSTEMTIME system;
            type = 64;
            if (!VariantTimeToSystemTime(date, &system) || !SystemTimeToFileTime(&system, &time))
                return DISP_E_TYPEMISMATCH;
        }
        return (HRESULT)AutoApiSummaryInfoSetProperty(object->Handle, (UINT)id, type, number, &time, text);
    }
    AUTO_TEXT text;
    AutoTextBegin(&text, 260);
    UINT type = 0, error;
    int number = 0;
    FILETIME time = {};
    do
        error = MsiSummaryInfoGetPropertyW(object->Handle, (UINT)id, &type, &number, &time, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = (HRESULT)error;
    if (!error) {
        switch (type) {
        case 0:
            hr = S_OK;
            break;
        case 2:
        case 3:
            hr = AutoSetInt(args, number);
            break;
        case 30:
            hr = AutoSetString(args, text.Buffer);
            break;
        case 64: {
            SYSTEMTIME system;
            double date = 0;
            if (FileTimeToSystemTime(&time, &system) && SystemTimeToVariantTime(&system, &date))
                hr = AutoSetDate(args, date);
            else
                hr = E_FAIL;
            break;
        }
        default:
            hr = E_FAIL;
        }
    }
    AutoTextEnd(&text);
    return hr;
}

static HRESULT summary_property_count(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    UINT count = 0;
    UINT error = MsiSummaryInfoGetPropertyCount(object->Handle, &count);
    return error ? (HRESULT)error : AutoSetInt(args, (int)count);
}

static HRESULT summary_persist(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return (HRESULT)AutoApiSummaryInfoPersist(object->Handle);
}

static const AUTO_ENTRY summary_entries[] = {
    ENTRY(1, 9601, GET | PUT, summary_property, L"Property,Pid"),
    ENTRY(2, 9602, GET, summary_property_count, L"PropertyCount"),
    ENTRY(3, 9603, CALL, summary_persist, L"Persist"),
};

AUTO_OBJECT *AutoNewSummaryInfo(MSIHANDLE handle)
{
    return AutoAllocate(sizeof(AUTO_OBJECT), summary_entries, COUNT_OF(summary_entries), &IID_AutoSummaryInfo, handle,
                        nullptr);
}

/* ------------------------------------------------------------------ database */

static HRESULT database_state(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return AutoSetInt(args, MsiGetDatabaseState(object->Handle));
}

static HRESULT database_summary(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    int updates = 0;
    if (AutoPresent(args, 1)) {
        HRESULT hr = AutoGetInt(AutoArgument(args, 1), &updates);
        if (hr)
            return hr;
    }
    MSIHANDLE summary = 0;
    UINT error = MsiGetSummaryInformationW(object->Handle, nullptr, (UINT)updates, &summary);
    if (error)
        return (HRESULT)error;
    return AutoSetObject(args, AutoNewSummaryInfo(summary));
}

static HRESULT database_open_view(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, query);
    MSIHANDLE view = 0;
    UINT error = MsiDatabaseOpenViewW(object->Handle, query, &view);
    if (error)
        return (HRESULT)error;
    return AutoSetObject(args, AutoNewView(view));
}

static HRESULT database_commit(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return (HRESULT)AutoApiDatabaseCommit(object->Handle);
}

static HRESULT database_primary_keys(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, table);
    MSIHANDLE record = 0;
    UINT error = MsiDatabaseGetPrimaryKeysW(object->Handle, table, &record);
    if (error)
        return (HRESULT)error;
    return AutoSetObject(args, AutoNewRecord(record));
}

static HRESULT database_import(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, folder);
    AUTO_STRING_ARG(args, 2, file);
    return (HRESULT)AutoApiDatabaseImport(object->Handle, folder, file);
}

static HRESULT database_export(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, table);
    AUTO_STRING_ARG(args, 2, folder);
    AUTO_STRING_ARG(args, 3, file);
    return (HRESULT)AutoApiDatabaseExport(object->Handle, table, folder, file);
}

/* a merge or a transform that finds differences answers true. */
static HRESULT database_merge(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    PCWSTR table = nullptr;
    if (AutoPresent(args, 2)) {
        HRESULT hr = AutoGetString(AutoArgument(args, 2), &table);
        if (hr)
            return hr;
    }
    MSIHANDLE other = AutoGetHandle(AutoArgument(args, 1), &IID_AutoDatabase);
    UINT error = AutoApiDatabaseMerge(object->Handle, other, table);
    if (error != ERROR_SUCCESS && error != ERROR_FUNCTION_FAILED)
        return (HRESULT)error;
    return AutoSetBool(args, error == ERROR_FUNCTION_FAILED);
}

static HRESULT database_generate_transform(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    PCWSTR file = nullptr;
    if (AutoPresent(args, 2)) {
        HRESULT hr = AutoGetString(AutoArgument(args, 2), &file);
        if (hr)
            return hr;
    }
    MSIHANDLE reference = AutoGetHandle(AutoArgument(args, 1), &IID_AutoDatabase);
    UINT error = AutoApiDatabaseGenerateTransform(object->Handle, reference, file);
    if (error != ERROR_SUCCESS && error != ERROR_NO_DATA)
        return (HRESULT)error;
    return AutoSetBool(args, error == ERROR_SUCCESS);
}

static HRESULT database_apply_transform(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, file);
    AUTO_INT_ARG(args, 2, conditions);
    return (HRESULT)MsiDatabaseApplyTransformW(object->Handle, file, conditions);
}

static HRESULT database_enable_preview(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    MSIHANDLE preview = 0;
    UINT error = AutoApiEnableUIPreview(object->Handle, &preview);
    if (error)
        return (HRESULT)error;
    return AutoSetObject(args, AutoNewUIPreview(preview));
}

static HRESULT database_table_persistent(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, table);
    return AutoSetInt(args, (int)MsiDatabaseIsTablePersistentW(object->Handle, table));
}

static HRESULT database_create_transform_summary(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 2, file);
    AUTO_INT_ARG(args, 3, conditions);
    AUTO_INT_ARG(args, 4, validation);
    MSIHANDLE reference = AutoGetHandle(AutoArgument(args, 1), &IID_AutoDatabase);
    return (HRESULT)AutoApiCreateTransformSummaryInfo(object->Handle, reference, file, conditions, validation);
}

static const AUTO_ENTRY database_entries[] = {
    ENTRY(1, 9501, GET, database_state, L"DatabaseState"),
    ENTRY(2, 9502, GET, database_summary, L"SummaryInformation,UpdateCount"),
    ENTRY(3, 9503, CALL, database_open_view, L"OpenView,Sql"),
    ENTRY(4, 9504, CALL, database_commit, L"Commit"),
    ENTRY(5, 9505, CALL, database_primary_keys, L"PrimaryKeys,Table"),
    ENTRY(6, 9506, CALL, database_import, L"Import,Folder,File"),
    ENTRY(7, 9507, CALL, database_export, L"Export,Table,Folder,File"),
    ENTRY(8, 9508, CALL, database_merge, L"Merge,Database,ErrorTable"),
    ENTRY(9, 9509, CALL, database_generate_transform, L"GenerateTransform,ReferenceDatabase,TransformFile"),
    ENTRY(10, 9510, CALL, database_apply_transform, L"ApplyTransform,TransformFile,ErrorConditions"),
    ENTRY(11, 9512, CALL, database_enable_preview, L"EnableUIPreview"),
    ENTRY(12, 9511, GET, database_table_persistent, L"TablePersistent,Table"),
    ENTRY(13, 9513, CALL, database_create_transform_summary,
          L"CreateTransformSummaryInfo,ReferenceDatabase,TransformFile,ErrorConditions,Validation"),
    ENTRY(AUTO_ID_HANDLE, 0, GET, object_handle, L""),
};

AUTO_OBJECT *AutoNewDatabase(MSIHANDLE handle)
{
    return AutoAllocate(sizeof(AUTO_OBJECT), database_entries, COUNT_OF(database_entries), &IID_AutoDatabase, handle,
                        nullptr);
}

/* ------------------------------------------------------------------ user interface preview */

static HRESULT preview_property(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return ERROR_CALL_NOT_IMPLEMENTED;
}

static const AUTO_ENTRY preview_entries[] = {
    ENTRY(1, 9702, GET | PUT, preview_property, L"Property,Name"),
    ENTRY(2, 9801, CALL, preview_property, L"ViewDialog,Dialog"),
    ENTRY(3, 9802, CALL, preview_property, L"ViewBillboard,Control,Billboard"),
};

AUTO_OBJECT *AutoNewUIPreview(MSIHANDLE handle)
{
    return AutoAllocate(sizeof(AUTO_OBJECT), preview_entries, COUNT_OF(preview_entries), &IID_AutoUIPreview, handle,
                        nullptr);
}

/* ------------------------------------------------------------------ session */

/* what MsiDoAction and MsiSequence returned, as the number of the status a script sees, or -1. */
static int action_status(UINT error)
{
    switch (error) {
    case ERROR_SUCCESS: return 1;
    case ERROR_INVALID_DATA: return 7;
    case ERROR_MORE_DATA: return 5;
    case ERROR_INSTALL_USEREXIT: return 2;
    case ERROR_INSTALL_FAILURE: return 3;
    case ERROR_INSTALL_SUSPEND: return 4;
    case ERROR_INVALID_HANDLE_STATE: return 6;
    case ERROR_FUNCTION_NOT_CALLED: return 0;
    }
    return -1;
}

static HRESULT session_installer(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AutoAddRef(object->Owner);
    return AutoSetObject(args, object->Owner);
}

static HRESULT session_property(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, name);
    if (args->Flags & PUT) {
        PCWSTR value = nullptr;
        VARIANT *variant = AutoArgument(args, 0);
        if (variant->vt) {
            HRESULT hr = AutoGetString(variant, &value);
            if (hr)
                return hr;
        }
        return (HRESULT)MsiSetPropertyW(object->Handle, name, value);
    }
    AUTO_TEXT text;
    AutoTextBegin(&text, 1024);
    UINT error;
    do
        error = MsiGetPropertyW(object->Handle, name, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT session_language(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return AutoSetInt(args, (int)MsiGetLanguage(object->Handle));
}

static HRESULT session_mode(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, mode);
    if (args->Flags & PUT) {
        BOOL state;
        HRESULT hr = AutoGetBool(AutoArgument(args, 0), &state);
        return hr ? hr : (HRESULT)MsiSetMode(object->Handle, (UINT)mode, state);
    }
    return AutoSetBool(args, MsiGetMode(object->Handle, (UINT)mode));
}

static HRESULT session_database(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    MSIHANDLE database = MsiGetActiveDatabase(object->Handle);
    if (!database)
        return E_FAIL;
    return AutoSetObject(args, AutoNewDatabase(database));
}

static HRESULT session_path(AUTO_OBJECT *object, AUTO_ARGS *args, BOOL source)
{
    AUTO_STRING_ARG(args, 1, folder);
    if (!source && (args->Flags & PUT)) {
        PCWSTR path = nullptr;
        HRESULT hr = AutoGetString(AutoArgument(args, 0), &path);
        return hr ? hr : (HRESULT)MsiSetTargetPathW(object->Handle, folder, path);
    }
    AUTO_TEXT text;
    AutoTextBegin(&text, 260);
    UINT error;
    do
        error = source ? MsiGetSourcePathW(object->Handle, folder, text.Buffer, &text.Size) :
                         MsiGetTargetPathW(object->Handle, folder, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT session_source_path(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return session_path(object, args, TRUE);
}

static HRESULT session_target_path(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return session_path(object, args, FALSE);
}

static HRESULT session_do_action(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, action);
    int status = action_status(MsiDoActionW(object->Handle, action));
    return status < 0 ? E_FAIL : AutoSetInt(args, status);
}

static HRESULT session_sequence(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, table);
    int mode = 0;
    if (AutoPresent(args, 2)) {
        HRESULT hr = AutoGetInt(AutoArgument(args, 2), &mode);
        if (hr)
            return hr;
    }
    int status = action_status(MsiSequenceW(object->Handle, table, mode));
    return status < 0 ? E_FAIL : AutoSetInt(args, status);
}

static HRESULT session_evaluate_condition(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, condition);
    return AutoSetInt(args, (int)MsiEvaluateConditionW(object->Handle, condition));
}

static HRESULT session_format_record(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_TEXT text;
    AutoTextBegin(&text, 260);
    UINT error;
    do {
        MSIHANDLE record = AutoGetHandle(AutoArgument(args, 1), &IID_AutoRecord);
        error = MsiFormatRecordW(object->Handle, record, text.Buffer, &text.Size);
    } while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT session_message(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, kind);
    MSIHANDLE record = AutoGetHandle(AutoArgument(args, 2), &IID_AutoRecord);
    int result = MsiProcessMessage(object->Handle, (INSTALLMESSAGE)kind, record);
    return result < 0 ? E_FAIL : AutoSetInt(args, result);
}

static HRESULT session_feature_current_state(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, feature);
    INSTALLSTATE installed = INSTALLSTATE_BROKEN, action = INSTALLSTATE_BROKEN;
    UINT error = MsiGetFeatureStateW(object->Handle, feature, &installed, &action);
    return error ? (HRESULT)error : AutoSetInt(args, installed);
}

static HRESULT session_feature_request_state(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, feature);
    if (args->Flags & PUT) {
        AUTO_INT_ARG(args, 0, state);
        return (HRESULT)MsiSetFeatureStateW(object->Handle, feature, (INSTALLSTATE)state);
    }
    INSTALLSTATE installed = INSTALLSTATE_BROKEN, action = INSTALLSTATE_BROKEN;
    UINT error = MsiGetFeatureStateW(object->Handle, feature, &installed, &action);
    return error ? (HRESULT)error : AutoSetInt(args, action);
}

static HRESULT session_feature_valid_states(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, feature);
    DWORD states = 0;
    UINT error = AutoApiGetFeatureValidStates(object->Handle, feature, &states);
    return error ? (HRESULT)error : AutoSetInt(args, (int)states);
}

static HRESULT session_feature_cost(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, feature);
    AUTO_INT_ARG(args, 2, tree);
    AUTO_INT_ARG(args, 3, state);
    int cost = 0;
    UINT error = MsiGetFeatureCostW(object->Handle, feature, (MSICOSTTREE)tree, (INSTALLSTATE)state, &cost);
    return error ? (HRESULT)error : AutoSetInt(args, cost);
}

static HRESULT session_component_current_state(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, component);
    INSTALLSTATE installed = INSTALLSTATE_BROKEN, action = INSTALLSTATE_BROKEN;
    UINT error = MsiGetComponentStateW(object->Handle, component, &installed, &action);
    return error ? (HRESULT)error : AutoSetInt(args, installed);
}

static HRESULT session_component_request_state(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, component);
    if (args->Flags & PUT) {
        AUTO_INT_ARG(args, 0, state);
        return (HRESULT)AutoApiSetComponentState(object->Handle, component, (INSTALLSTATE)state);
    }
    INSTALLSTATE installed = INSTALLSTATE_BROKEN, action = INSTALLSTATE_BROKEN;
    UINT error = MsiGetComponentStateW(object->Handle, component, &installed, &action);
    return error ? (HRESULT)error : AutoSetInt(args, action);
}

static HRESULT session_set_install_level(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_INT_ARG(args, 1, level);
    return (HRESULT)MsiSetInstallLevel(object->Handle, level);
}

/* enough room answers true, and so does an error that the volumes are out of space. */
static HRESULT session_verify_disk_space(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    UINT error = MsiVerifyDiskSpace(object->Handle);
    if (error && error != ERROR_DISK_FULL)
        return (HRESULT)error;
    return AutoSetBool(args, !error);
}

static HRESULT session_product_property(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, property);
    AUTO_TEXT text;
    AutoTextBegin(&text, 256);
    UINT error;
    do
        error = MsiGetProductPropertyW(object->Handle, property, text.Buffer, &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT session_feature_info(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, feature);
    AUTO_OBJECT *info = AutoNewFeatureInfo(object->Handle, feature);
    if (!info)
        return DISP_E_BADINDEX;
    info->Owner = object;
    AutoAddRef(object);
    return AutoSetObject(args, info);
}

typedef struct AUTO_COST {
    WCHAR Drive[64];
    int Cost, Temporary;
} AUTO_COST;

static HRESULT session_component_costs(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_STRING_ARG(args, 1, component);
    AUTO_INT_ARG(args, 2, state);
    AUTO_LIST *list = AutoListCreate(TRUE);
    if (!list)
        return E_OUTOFMEMORY;
    UINT error;
    for (DWORD index = 0;; index++) {
        WCHAR drive[260];
        DWORD size = RTL_NUMBER_OF(drive);
        int cost = 0, temporary = 0;
        error = AutoApiEnumComponentCosts(object->Handle, component, index, (INSTALLSTATE)state, drive, &size, &cost,
                                          &temporary);
        if (error)
            break;
        MSIHANDLE record = MsiCreateRecord(3);
        if (!record || MsiRecordSetStringW(record, 1, drive) || MsiRecordSetInteger(record, 2, cost) ||
            MsiRecordSetInteger(record, 3, temporary)) {
            if (record)
                MsiCloseHandle(record);
            error = ERROR_OUTOFMEMORY;
            break;
        }
        AUTO_OBJECT *item = AutoNewRecord(record);
        if (!item || !AutoListAddObject(list, item)) {
            if (item)
                AutoRelease(item);
            error = ERROR_OUTOFMEMORY;
            break;
        }
    }
    if (error != ERROR_NO_MORE_ITEMS) {
        AutoListDiscard(list);
        return error == ERROR_OUTOFMEMORY ? E_OUTOFMEMORY : (HRESULT)(error ? AutoWin32ToHresult(error) : E_FAIL);
    }
    return AutoListReturn(args, list);
}

static const AUTO_ENTRY session_entries[] = {
    ENTRY(1, 9701, GET, session_installer, L"Installer"),
    ENTRY(2, 9702, GET | PUT, session_property, L"Property,Name"),
    ENTRY(3, 9703, GET, session_language, L"Language"),
    ENTRY(4, 9704, GET | PUT, session_mode, L"Mode,Flag"),
    ENTRY(5, 9705, GET, session_database, L"Database"),
    ENTRY(6, 9711, GET, session_source_path, L"SourcePath,Folder"),
    ENTRY(7, 9712, GET | PUT, session_target_path, L"TargetPath,Folder"),
    ENTRY(8, 9706, CALL, session_do_action, L"DoAction,Action"),
    ENTRY(9, 9707, CALL, session_sequence, L"Sequence,Table,Mode"),
    ENTRY(10, 9710, CALL, session_evaluate_condition, L"EvaluateCondition,Expression"),
    ENTRY(11, 9709, CALL, session_format_record, L"FormatRecord,Record"),
    ENTRY(12, 9708, CALL, session_message, L"Message,Kind,Record"),
    ENTRY(13, 9713, GET, session_feature_current_state, L"FeatureCurrentState,Feature"),
    ENTRY(14, 9714, GET | PUT, session_feature_request_state, L"FeatureRequestState,Feature"),
    ENTRY(15, 9715, GET, session_feature_valid_states, L"FeatureValidStates,Feature"),
    ENTRY(16, 9716, GET, session_feature_cost, L"FeatureCost,Feature,CostTree,State"),
    ENTRY(17, 9717, GET, session_component_current_state, L"ComponentCurrentState,Component"),
    ENTRY(18, 9718, GET | PUT, session_component_request_state, L"ComponentRequestState,Component"),
    ENTRY(19, 9719, CALL, session_set_install_level, L"SetInstallLevel,Level"),
    ENTRY(20, 9720, GET, session_verify_disk_space, L"VerifyDiskSpace"),
    ENTRY(21, 9721, GET, session_product_property, L"ProductProperty,Property"),
    ENTRY(22, 9722, GET, session_feature_info, L"FeatureInfo,Feature"),
    ENTRY(23, 9723, GET, session_component_costs, L"ComponentCosts,Component,State"),
};

AUTO_OBJECT *AutoNewSession(MSIHANDLE handle, AUTO_OBJECT *installer)
{
    AUTO_OBJECT *session = AutoAllocate(sizeof(AUTO_OBJECT), session_entries, COUNT_OF(session_entries),
                                        &IID_AutoSession, handle, nullptr);
    if (session) {
        session->Owner = installer;
        AutoAddRef(installer);
    }
    return session;
}

/* ------------------------------------------------------------------ feature information of a session */

typedef struct AUTO_FEATURE {
    AUTO_OBJECT Base;
    MSIHANDLE Session;
    DWORD Attributes;
    WCHAR Feature[39], Title[100], Description[256];
} AUTO_FEATURE;

static HRESULT feature_title(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return AutoSetString(args, ((AUTO_FEATURE *)object)->Title);
}

static HRESULT feature_description(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return AutoSetString(args, ((AUTO_FEATURE *)object)->Description);
}

static HRESULT feature_attributes(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_FEATURE *feature = (AUTO_FEATURE *)object;
    if (!(args->Flags & PUT))
        return AutoSetInt(args, (int)feature->Attributes);
    if (!feature->Feature[0])
        return E_INVALIDARG;
    AUTO_INT_ARG(args, 0, attributes);
    return (HRESULT)AutoApiSetFeatureAttributes(feature->Session, feature->Feature, (DWORD)attributes);
}

static const AUTO_ENTRY feature_entries[] = {
    ENTRY(1, 9101, GET, feature_title, L"Title"),
    ENTRY(2, 9102, GET, feature_description, L"Description"),
    ENTRY(3, 9103, GET | PUT, feature_attributes, L"Attributes"),
};

AUTO_OBJECT *AutoNewFeatureInfo(MSIHANDLE session, PCWSTR feature)
{
    AUTO_FEATURE *info = (AUTO_FEATURE *)AutoAllocate(sizeof(AUTO_FEATURE), feature_entries, COUNT_OF(feature_entries),
                                                      &IID_IUnknown, 0, nullptr);
    if (!info)
        return nullptr;
    info->Session = session;
    if (feature && wcslen(feature) < RTL_NUMBER_OF(info->Feature))
        wcscpy(info->Feature, feature);
    DWORD title = RTL_NUMBER_OF(info->Title), help = RTL_NUMBER_OF(info->Description);
    UINT error = MsiGetFeatureInfoW(session, feature, &info->Attributes, info->Title, &title, info->Description, &help);
    if (error && error != ERROR_MORE_DATA) {
        AutoRelease(&info->Base);
        return nullptr;
    }
    return &info->Base;
}
