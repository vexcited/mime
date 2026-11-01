/*
 * adodb.h: the interfaces of ADO (msado15.dll) that Mime serves: the connection with its events, the recordset with its
 * fields, and the errors. The vtables keep the order of the type library, which callers that bind early depend on.
 */
#ifndef MIME_WIN32_ADODB_H
#define MIME_WIN32_ADODB_H

#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>

MIME_DEFINE_GUID(CLSID_CadoConnection, 0x00000514, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(CLSID_CadoRecordset, 0x00000535, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(IID_ADOConnection15, 0x00000515, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(IID_ADOConnection, 0x00000550, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(IID_ADORecordset, 0x00000556, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(IID_ADORecordset21, 0x00000555, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(IID_ADORecordset20, 0x0000054F, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(IID_ADORecordset15, 0x0000050E, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(IID_ADOFields, 0x00000564, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(IID_ADOField, 0x00000569, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(IID_ADOErrors, 0x00000501, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(IID_ADOError, 0x00000500, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(IID_ConnectionEvents, 0x00000400, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);
MIME_DEFINE_GUID(IID_ConnectionEventsVt, 0x00000402, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x6D, 0x2E, 0xA4);

#ifndef CONNECT_E_NOCONNECTION
#define CONNECT_E_NOCONNECTION ((HRESULT)0x80040200L)
#define CONNECT_E_ADVISELIMIT ((HRESULT)0x80040201L)
#define CONNECT_E_CANNOTCONNECT ((HRESULT)0x80040202L)
#endif

#define ADO_STATE_CLOSED 0
#define ADO_STATE_OPEN 1
#define ADO_STATE_CONNECTING 2
#define ADO_STATE_EXECUTING 4
#define ADO_STATE_FETCHING 8

#define ADO_STATUS_OK 1
#define ADO_STATUS_ERRORS_OCCURRED 2
#define ADO_STATUS_CANT_DENY 3
#define ADO_STATUS_CANCEL 4
#define ADO_STATUS_UNWANTED_EVENT 5

#define ADO_USE_SERVER 2
#define ADO_USE_CLIENT 3

/* the errors of ADO that callers test for. */
#define ADO_E_PROVIDER_NOT_FOUND ((HRESULT)0x800A0E7AL)
#define ADO_E_OBJECT_CLOSED ((HRESULT)0x800A0E78L)
#define ADO_E_OBJECT_OPEN ((HRESULT)0x800A0E79L)
#define ADO_E_ITEM_NOT_FOUND ((HRESULT)0x800A0CC1L)
#define ADO_E_NO_CURRENT_RECORD ((HRESULT)0x800A0BCDL)
#define DB_E_ERRORSINCOMMAND ((HRESULT)0x80040E14L)
#define DB_SEC_E_AUTH_FAILED ((HRESULT)0x80040E4DL)

typedef struct ADOConnection ADOConnection;
typedef struct ADORecordset ADORecordset;
typedef struct ADOFields ADOFields;
typedef struct ADOField ADOField;
typedef struct ADOErrors ADOErrors;
typedef struct ADOError ADOError;
typedef struct ADOProperties ADOProperties;
typedef struct ADOConnectionEventsVt ADOConnectionEventsVt;

/* the type library gives every dual interface the methods of IDispatch before its own. */
#define ADO_DISPATCH(type)                                                                                             \
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(type * This, REFIID riid, void **object);                               \
    ULONG(STDMETHODCALLTYPE *AddRef)(type * This);                                                                     \
    ULONG(STDMETHODCALLTYPE *Release)(type * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetTypeInfoCount)(type * This, UINT * count);                                           \
    HRESULT(STDMETHODCALLTYPE *GetTypeInfo)(type * This, UINT index, LCID lcid, void **info);                          \
    HRESULT(STDMETHODCALLTYPE *GetIDsOfNames)(type * This, REFIID riid, LPOLESTR * names, UINT count, LCID lcid,       \
                                              DISPID * ids);                                                           \
    HRESULT(STDMETHODCALLTYPE *Invoke)(type * This, DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS * params, \
                                       VARIANT * result, EXCEPINFO * excep, UINT * arg_error)

/* ------------------------------------------------------------------ errors */

typedef struct ADOErrorVtbl {
    ADO_DISPATCH(ADOError);
    HRESULT(STDMETHODCALLTYPE *get_Number)(ADOError *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *get_Source)(ADOError *This, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *get_Description)(ADOError *This, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *get_HelpFile)(ADOError *This, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *get_HelpContext)(ADOError *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *get_SQLState)(ADOError *This, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *get_NativeError)(ADOError *This, LONG *value);
} ADOErrorVtbl;
struct ADOError {
    const ADOErrorVtbl *lpVtbl;
};

typedef struct ADOErrorsVtbl {
    ADO_DISPATCH(ADOErrors);
    HRESULT(STDMETHODCALLTYPE *get_Count)(ADOErrors *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *_NewEnum)(ADOErrors *This, IUnknown **value);
    HRESULT(STDMETHODCALLTYPE *Refresh)(ADOErrors *This);
    HRESULT(STDMETHODCALLTYPE *get_Item)(ADOErrors *This, VARIANT index, ADOError **value);
    HRESULT(STDMETHODCALLTYPE *Clear)(ADOErrors *This);
} ADOErrorsVtbl;
struct ADOErrors {
    const ADOErrorsVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ fields */

typedef struct ADOFieldVtbl {
    ADO_DISPATCH(ADOField);
    HRESULT(STDMETHODCALLTYPE *get_Properties)(ADOField *This, ADOProperties **value);
    HRESULT(STDMETHODCALLTYPE *get_ActualSize)(ADOField *This, LONG_PTR *value);
    HRESULT(STDMETHODCALLTYPE *get_Attributes)(ADOField *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *get_DefinedSize)(ADOField *This, LONG_PTR *value);
    HRESULT(STDMETHODCALLTYPE *get_Name)(ADOField *This, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *get_Type)(ADOField *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *get_Value)(ADOField *This, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *put_Value)(ADOField *This, VARIANT value);
    HRESULT(STDMETHODCALLTYPE *get_Precision)(ADOField *This, BYTE *value);
    HRESULT(STDMETHODCALLTYPE *get_NumericScale)(ADOField *This, BYTE *value);
    HRESULT(STDMETHODCALLTYPE *AppendChunk)(ADOField *This, VARIANT data);
    HRESULT(STDMETHODCALLTYPE *GetChunk)(ADOField *This, LONG length, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *get_OriginalValue)(ADOField *This, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *get_UnderlyingValue)(ADOField *This, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *get_DataFormat)(ADOField *This, IUnknown **value);
    HRESULT(STDMETHODCALLTYPE *putref_DataFormat)(ADOField *This, IUnknown *value);
    HRESULT(STDMETHODCALLTYPE *put_Precision)(ADOField *This, BYTE value);
    HRESULT(STDMETHODCALLTYPE *put_NumericScale)(ADOField *This, BYTE value);
    HRESULT(STDMETHODCALLTYPE *put_Type)(ADOField *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *put_DefinedSize)(ADOField *This, LONG_PTR value);
    HRESULT(STDMETHODCALLTYPE *put_Attributes)(ADOField *This, LONG value);
} ADOFieldVtbl;
struct ADOField {
    const ADOFieldVtbl *lpVtbl;
};

typedef struct ADOFieldsVtbl {
    ADO_DISPATCH(ADOFields);
    HRESULT(STDMETHODCALLTYPE *get_Count)(ADOFields *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *_NewEnum)(ADOFields *This, IUnknown **value);
    HRESULT(STDMETHODCALLTYPE *Refresh)(ADOFields *This);
    HRESULT(STDMETHODCALLTYPE *get_Item)(ADOFields *This, VARIANT index, ADOField **value);
    HRESULT(STDMETHODCALLTYPE *Append)(ADOFields *This, BSTR name, LONG type, LONG_PTR size, LONG attributes,
                                       VARIANT value);
    HRESULT(STDMETHODCALLTYPE *Delete)(ADOFields *This, VARIANT index);
} ADOFieldsVtbl;
struct ADOFields {
    const ADOFieldsVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ recordset */

typedef struct ADORecordsetVtbl {
    ADO_DISPATCH(ADORecordset);
    HRESULT(STDMETHODCALLTYPE *get_Properties)(ADORecordset *This, ADOProperties **value);
    HRESULT(STDMETHODCALLTYPE *get_AbsolutePosition)(ADORecordset *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_AbsolutePosition)(ADORecordset *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *putref_ActiveConnection)(ADORecordset *This, IDispatch *value);
    HRESULT(STDMETHODCALLTYPE *put_ActiveConnection)(ADORecordset *This, VARIANT value);
    HRESULT(STDMETHODCALLTYPE *get_ActiveConnection)(ADORecordset *This, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *get_BOF)(ADORecordset *This, VARIANT_BOOL *value);
    HRESULT(STDMETHODCALLTYPE *get_Bookmark)(ADORecordset *This, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *put_Bookmark)(ADORecordset *This, VARIANT value);
    HRESULT(STDMETHODCALLTYPE *get_CacheSize)(ADORecordset *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_CacheSize)(ADORecordset *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *get_CursorType)(ADORecordset *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_CursorType)(ADORecordset *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *get_EOF)(ADORecordset *This, VARIANT_BOOL *value);
    HRESULT(STDMETHODCALLTYPE *get_Fields)(ADORecordset *This, ADOFields **value);
    HRESULT(STDMETHODCALLTYPE *get_LockType)(ADORecordset *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_LockType)(ADORecordset *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *get_MaxRecords)(ADORecordset *This, LONG_PTR *value);
    HRESULT(STDMETHODCALLTYPE *put_MaxRecords)(ADORecordset *This, LONG_PTR value);
    HRESULT(STDMETHODCALLTYPE *get_RecordCount)(ADORecordset *This, LONG_PTR *value);
    HRESULT(STDMETHODCALLTYPE *putref_Source)(ADORecordset *This, IDispatch *value);
    HRESULT(STDMETHODCALLTYPE *put_Source)(ADORecordset *This, BSTR value);
    HRESULT(STDMETHODCALLTYPE *get_Source)(ADORecordset *This, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *AddNew)(ADORecordset *This, VARIANT fields, VARIANT values);
    HRESULT(STDMETHODCALLTYPE *CancelUpdate)(ADORecordset *This);
    HRESULT(STDMETHODCALLTYPE *Close)(ADORecordset *This);
    HRESULT(STDMETHODCALLTYPE *Delete)(ADORecordset *This, LONG affect);
    HRESULT(STDMETHODCALLTYPE *GetRows)(ADORecordset *This, LONG rows, VARIANT start, VARIANT fields, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *Move)(ADORecordset *This, LONG_PTR rows, VARIANT start);
    HRESULT(STDMETHODCALLTYPE *MoveNext)(ADORecordset *This);
    HRESULT(STDMETHODCALLTYPE *MovePrevious)(ADORecordset *This);
    HRESULT(STDMETHODCALLTYPE *MoveFirst)(ADORecordset *This);
    HRESULT(STDMETHODCALLTYPE *MoveLast)(ADORecordset *This);
    HRESULT(STDMETHODCALLTYPE *Open)(ADORecordset *This, VARIANT source, VARIANT connection, LONG cursor, LONG lock,
                                     LONG options);
    HRESULT(STDMETHODCALLTYPE *Requery)(ADORecordset *This, LONG options);
    HRESULT(STDMETHODCALLTYPE *_xResync)(ADORecordset *This, LONG affect);
    HRESULT(STDMETHODCALLTYPE *Update)(ADORecordset *This, VARIANT fields, VARIANT values);
    HRESULT(STDMETHODCALLTYPE *get_AbsolutePage)(ADORecordset *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_AbsolutePage)(ADORecordset *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *get_EditMode)(ADORecordset *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *get_Filter)(ADORecordset *This, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *put_Filter)(ADORecordset *This, VARIANT value);
    HRESULT(STDMETHODCALLTYPE *get_PageCount)(ADORecordset *This, LONG_PTR *value);
    HRESULT(STDMETHODCALLTYPE *get_PageSize)(ADORecordset *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_PageSize)(ADORecordset *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *get_Sort)(ADORecordset *This, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *put_Sort)(ADORecordset *This, BSTR value);
    HRESULT(STDMETHODCALLTYPE *get_Status)(ADORecordset *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *get_State)(ADORecordset *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *_xClone)(ADORecordset *This, ADORecordset **value);
    HRESULT(STDMETHODCALLTYPE *UpdateBatch)(ADORecordset *This, LONG affect);
    HRESULT(STDMETHODCALLTYPE *CancelBatch)(ADORecordset *This, LONG affect);
    HRESULT(STDMETHODCALLTYPE *get_CursorLocation)(ADORecordset *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_CursorLocation)(ADORecordset *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *NextRecordset)(ADORecordset *This, VARIANT *affected, ADORecordset **value);
    HRESULT(STDMETHODCALLTYPE *Supports)(ADORecordset *This, LONG options, VARIANT_BOOL *value);
    HRESULT(STDMETHODCALLTYPE *get_Collect)(ADORecordset *This, VARIANT index, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *put_Collect)(ADORecordset *This, VARIANT index, VARIANT value);
    HRESULT(STDMETHODCALLTYPE *get_MarshalOptions)(ADORecordset *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_MarshalOptions)(ADORecordset *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *Find)(ADORecordset *This, BSTR criteria, LONG skip, LONG direction, VARIANT start);
    HRESULT(STDMETHODCALLTYPE *Cancel)(ADORecordset *This);
    HRESULT(STDMETHODCALLTYPE *get_DataSource)(ADORecordset *This, IUnknown **value);
    HRESULT(STDMETHODCALLTYPE *putref_DataSource)(ADORecordset *This, IUnknown *value);
    HRESULT(STDMETHODCALLTYPE *_xSave)(ADORecordset *This, BSTR file, LONG format);
    HRESULT(STDMETHODCALLTYPE *get_ActiveCommand)(ADORecordset *This, IDispatch **value);
    HRESULT(STDMETHODCALLTYPE *put_StayInSync)(ADORecordset *This, VARIANT_BOOL value);
    HRESULT(STDMETHODCALLTYPE *get_StayInSync)(ADORecordset *This, VARIANT_BOOL *value);
    HRESULT(STDMETHODCALLTYPE *GetString)(ADORecordset *This, LONG format, LONG rows, BSTR column_delimiter,
                                          BSTR row_delimiter, BSTR null_expression, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *get_DataMember)(ADORecordset *This, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *put_DataMember)(ADORecordset *This, BSTR value);
    HRESULT(STDMETHODCALLTYPE *CompareBookmarks)(ADORecordset *This, VARIANT first, VARIANT second, LONG *value);
    HRESULT(STDMETHODCALLTYPE *Clone)(ADORecordset *This, LONG lock, ADORecordset **value);
    HRESULT(STDMETHODCALLTYPE *Resync)(ADORecordset *This, LONG affect, LONG resync);
    HRESULT(STDMETHODCALLTYPE *Seek)(ADORecordset *This, VARIANT key, LONG seek);
    HRESULT(STDMETHODCALLTYPE *put_Index)(ADORecordset *This, BSTR value);
    HRESULT(STDMETHODCALLTYPE *get_Index)(ADORecordset *This, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *Save)(ADORecordset *This, VARIANT destination, LONG format);
} ADORecordsetVtbl;
struct ADORecordset {
    const ADORecordsetVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ connection */

typedef struct ADOConnectionVtbl {
    ADO_DISPATCH(ADOConnection);
    HRESULT(STDMETHODCALLTYPE *get_Properties)(ADOConnection *This, ADOProperties **value);
    HRESULT(STDMETHODCALLTYPE *get_ConnectionString)(ADOConnection *This, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *put_ConnectionString)(ADOConnection *This, BSTR value);
    HRESULT(STDMETHODCALLTYPE *get_CommandTimeout)(ADOConnection *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_CommandTimeout)(ADOConnection *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *get_ConnectionTimeout)(ADOConnection *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_ConnectionTimeout)(ADOConnection *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *get_Version)(ADOConnection *This, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *Close)(ADOConnection *This);
    HRESULT(STDMETHODCALLTYPE *Execute)(ADOConnection *This, BSTR command, VARIANT *affected, LONG options,
                                        ADORecordset **value);
    HRESULT(STDMETHODCALLTYPE *BeginTrans)(ADOConnection *This, LONG *level);
    HRESULT(STDMETHODCALLTYPE *CommitTrans)(ADOConnection *This);
    HRESULT(STDMETHODCALLTYPE *RollbackTrans)(ADOConnection *This);
    HRESULT(STDMETHODCALLTYPE *Open)(ADOConnection *This, BSTR connection, BSTR user, BSTR password, LONG options);
    HRESULT(STDMETHODCALLTYPE *get_Errors)(ADOConnection *This, ADOErrors **value);
    HRESULT(STDMETHODCALLTYPE *get_DefaultDatabase)(ADOConnection *This, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *put_DefaultDatabase)(ADOConnection *This, BSTR value);
    HRESULT(STDMETHODCALLTYPE *get_IsolationLevel)(ADOConnection *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_IsolationLevel)(ADOConnection *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *get_Attributes)(ADOConnection *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_Attributes)(ADOConnection *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *get_CursorLocation)(ADOConnection *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_CursorLocation)(ADOConnection *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *get_Mode)(ADOConnection *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *put_Mode)(ADOConnection *This, LONG value);
    HRESULT(STDMETHODCALLTYPE *get_Provider)(ADOConnection *This, BSTR *value);
    HRESULT(STDMETHODCALLTYPE *put_Provider)(ADOConnection *This, BSTR value);
    HRESULT(STDMETHODCALLTYPE *get_State)(ADOConnection *This, LONG *value);
    HRESULT(STDMETHODCALLTYPE *OpenSchema)(ADOConnection *This, LONG schema, VARIANT restrictions, VARIANT schema_id,
                                           ADORecordset **value);
    HRESULT(STDMETHODCALLTYPE *Cancel)(ADOConnection *This);
} ADOConnectionVtbl;
struct ADOConnection {
    const ADOConnectionVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ the events of a connection */

struct ADOConnectionEventsVt {
    const struct ADOConnectionEventsVtVtbl {
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(ADOConnectionEventsVt *This, REFIID riid, void **object);
        ULONG(STDMETHODCALLTYPE *AddRef)(ADOConnectionEventsVt *This);
        ULONG(STDMETHODCALLTYPE *Release)(ADOConnectionEventsVt *This);
        HRESULT(STDMETHODCALLTYPE *InfoMessage)(ADOConnectionEventsVt *This, ADOError *error, LONG *status,
                                                ADOConnection *connection);
        HRESULT(STDMETHODCALLTYPE *BeginTransComplete)(ADOConnectionEventsVt *This, LONG level, ADOError *error,
                                                       LONG *status, ADOConnection *connection);
        HRESULT(STDMETHODCALLTYPE *CommitTransComplete)(ADOConnectionEventsVt *This, ADOError *error, LONG *status,
                                                        ADOConnection *connection);
        HRESULT(STDMETHODCALLTYPE *RollbackTransComplete)(ADOConnectionEventsVt *This, ADOError *error, LONG *status,
                                                          ADOConnection *connection);
        HRESULT(STDMETHODCALLTYPE *WillExecute)(ADOConnectionEventsVt *This, BSTR *source, LONG *cursor, LONG *lock,
                                                LONG *options, LONG *status, IDispatch *command,
                                                ADORecordset *recordset, ADOConnection *connection);
        HRESULT(STDMETHODCALLTYPE *ExecuteComplete)(ADOConnectionEventsVt *This, LONG affected, ADOError *error,
                                                    LONG *status, IDispatch *command, ADORecordset *recordset,
                                                    ADOConnection *connection);
        HRESULT(STDMETHODCALLTYPE *WillConnect)(ADOConnectionEventsVt *This, BSTR *connection_string, BSTR *user,
                                                BSTR *password, LONG *options, LONG *status, ADOConnection *connection);
        HRESULT(STDMETHODCALLTYPE *ConnectComplete)(ADOConnectionEventsVt *This, ADOError *error, LONG *status,
                                                    ADOConnection *connection);
        HRESULT(STDMETHODCALLTYPE *Disconnect)(ADOConnectionEventsVt *This, LONG *status, ADOConnection *connection);
    } *lpVtbl;
};

#endif
