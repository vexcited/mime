/* the mof compiler object and the text of status codes. */
#include "wbemcore.h"

/* ------------------------------------------------------------------ the compiler */

typedef struct ComCompiler {
    const IMofCompilerVtbl *lpVtbl;
    LONG refs;
} ComCompiler;

static HRESULT STDMETHODCALLTYPE mc_QueryInterface(IMofCompiler *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMofCompiler)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("mc", riid, out);
}

static ULONG STDMETHODCALLTYPE mc_AddRef(IMofCompiler *This)
{
    return (ULONG)InterlockedIncrement(&((ComCompiler *)This)->refs);
}

static ULONG STDMETHODCALLTYPE mc_Release(IMofCompiler *This)
{
    ComCompiler *c = (ComCompiler *)This;
    LONG n = InterlockedDecrement(&c->refs);
    if (!n) {
        wb_free(c);
        InterlockedDecrement(&g_wbem_locks);
    }
    return (ULONG)n;
}

/* the text of a file as wide characters, whatever its encoding. */
static WCHAR *decode(const BYTE *bytes, DWORD size, SIZE_T *length)
{
    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        *length = (size - 2) / sizeof(WCHAR);
        return wb_wcsndup((const WCHAR *)(bytes + 2), *length);
    }
    UINT page = CP_ACP;
    if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        bytes += 3;
        size -= 3;
        page = CP_UTF8;
    }
    int n = MultiByteToWideChar(page, 0, (const char *)bytes, (int)size, nullptr, 0);
    WCHAR *text = wb_alloc(((SIZE_T)n + 1) * sizeof(WCHAR));
    MultiByteToWideChar(page, 0, (const char *)bytes, (int)size, text, n);
    *length = (SIZE_T)n;
    return text;
}

static void fill_info(WBEM_COMPILE_STATUS_INFO *info, const WMofResult *r)
{
    if (!info)
        return;
    memset(info, 0, sizeof(*info));
    info->lPhaseError = r->phase;
    info->hRes = r->result;
    info->ObjectNum = r->object_number;
    info->FirstLine = r->first_line;
    info->LastLine = r->last_line;
}

static HRESULT compile_text(const WCHAR *text, SIZE_T length, const WCHAR *target, LONG options, LONG class_flags,
                            LONG instance_flags, WBEM_COMPILE_STATUS_INFO *info)
{
    WMofResult result;
    WPath path;
    wp_init(&path);
    WCHAR *ns = nullptr;
    if (target && *target) {
        if (SUCCEEDED(wp_parse(&path, target, WBEMPATH_CREATE_ACCEPT_ALL | WBEMPATH_TREAT_SINGLE_IDENT_AS_NS)))
            ns = wp_text(&path, WBEMPATH_GET_NAMESPACE_ONLY);
    }
    HRESULT hr = wmof_compile(text, length, ns && *ns ? ns : L"root\\default", options, class_flags, instance_flags, &result,
                              FALSE);
    fill_info(info, &result);
    wb_free(ns);
    wp_free(&path);
    return hr;
}

/* a file that asks for autorecover is remembered, so that a damaged repository can be rebuilt from it. */
static void remember_autorecover(const WCHAR *file, const WCHAR *text)
{
    if (!wcsstr(text, L"#pragma autorecover") && !wcsstr(text, L"#pragma AUTORECOVER") && !wcsstr(text, L"#PRAGMA AUTORECOVER"))
        return;
    HKEY key;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\WBEM\\CIMOM", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key,
                        nullptr))
        return;
    WCHAR full[MAX_PATH];
    if (!GetFullPathNameW(file, MAX_PATH, full, nullptr))
        wcscpy(full, file);
    DWORD size = 0, type;
    BYTE *old = nullptr;
    if (!RegQueryValueExW(key, L"Autorecover MOFs", nullptr, &type, nullptr, &size) && type == REG_MULTI_SZ && size) {
        old = wb_alloc(size + sizeof(WCHAR));
        RegQueryValueExW(key, L"Autorecover MOFs", nullptr, &type, old, &size);
        for (const WCHAR *c = (const WCHAR *)old; *c; c += wcslen(c) + 1) {
            if (wb_eq(c, full)) {
                wb_free(old);
                RegCloseKey(key);
                return;
            }
        }
    }
    SIZE_T old_chars = old ? size / sizeof(WCHAR) : 0;
    while (old_chars && !((WCHAR *)old)[old_chars - 1])
        old_chars--;
    SIZE_T add = wcslen(full) + 1;
    WCHAR *merged = wb_alloc((old_chars + 1 + add + 1) * sizeof(WCHAR));
    if (old_chars) {
        memcpy(merged, old, old_chars * sizeof(WCHAR));
        merged[old_chars++] = 0;
    }
    memcpy(merged + old_chars, full, add * sizeof(WCHAR));
    SIZE_T total = old_chars + add + 1;
    RegSetValueExW(key, L"Autorecover MOFs", 0, REG_MULTI_SZ, (const BYTE *)merged, (DWORD)(total * sizeof(WCHAR)));
    wb_free(merged);
    wb_free(old);
    RegCloseKey(key);
}

static HRESULT STDMETHODCALLTYPE mc_CompileFile(IMofCompiler *This, LPWSTR file, LPWSTR target, LPWSTR user, LPWSTR authority,
                                                LPWSTR password, LONG options, LONG class_flags, LONG instance_flags,
                                                WBEM_COMPILE_STATUS_INFO *info)
{
    if (!file)
        return WBEM_E_INVALID_PARAMETER;
    HANDLE h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (info) {
            memset(info, 0, sizeof(*info));
            info->hRes = WBEM_E_INVALID_PARAMETER;
        }
        return WBEM_E_INVALID_PARAMETER;
    }
    DWORD size = GetFileSize(h, nullptr), got = 0;
    BYTE *bytes = wb_alloc((SIZE_T)size + 4);
    BOOL ok = ReadFile(h, bytes, size, &got, nullptr);
    CloseHandle(h);
    if (!ok) {
        wb_free(bytes);
        return WBEM_E_FAILED;
    }
    SIZE_T length;
    WCHAR *text = decode(bytes, got, &length);
    wb_free(bytes);
    HRESULT hr = compile_text(text, length, target, options, class_flags, instance_flags, info);
    if (SUCCEEDED(hr) && !(options & WBEM_FLAG_CHECK_ONLY))
        remember_autorecover(file, text);
    wb_free(text);
    return hr == S_OK ? S_OK : hr;
}

static HRESULT STDMETHODCALLTYPE mc_CompileBuffer(IMofCompiler *This, BYTE *buffer, DWORD size, LPWSTR target, LPWSTR user,
                                                  LPWSTR authority, LPWSTR password, LONG options, LONG class_flags,
                                                  LONG instance_flags, WBEM_COMPILE_STATUS_INFO *info)
{
    if (!buffer)
        return WBEM_E_INVALID_PARAMETER;
    SIZE_T length;
    WCHAR *text = decode(buffer, size, &length);
    HRESULT hr = compile_text(text, length, target, options, class_flags, instance_flags, info);
    wb_free(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE mc_CreateBMOF(IMofCompiler *This, LPWSTR text_file, LPWSTR bmof_file, LPWSTR target, LONG options,
                                               LONG class_flags, LONG instance_flags, WBEM_COMPILE_STATUS_INFO *info)
{
    return WBEM_E_NOT_SUPPORTED;
}

static const IMofCompilerVtbl COMPILER_VTBL = {
    mc_QueryInterface, mc_AddRef, mc_Release, mc_CompileFile, mc_CompileBuffer, mc_CreateBMOF,
};

HRESULT wm_compiler_new(REFIID riid, void **out)
{
    ComCompiler *c = wb_alloc(sizeof(ComCompiler));
    if (!c)
        return E_OUTOFMEMORY;
    c->lpVtbl = &COMPILER_VTBL;
    c->refs = 1;
    InterlockedIncrement(&g_wbem_locks);
    HRESULT hr = mc_QueryInterface((IMofCompiler *)c, riid, out);
    mc_Release((IMofCompiler *)c);
    return hr;
}

/* ------------------------------------------------------------------ status text */

static const struct {
    HRESULT code;
    const WCHAR *text;
} MESSAGES[] = {
    {WBEM_S_NO_ERROR, L"The operation completed successfully. "},
    {WBEM_S_FALSE, L"The call failed. "},
    {WBEM_S_ALREADY_EXISTS, L"The object already exists. "},
    {WBEM_S_RESET_TO_DEFAULT, L"The property was reset to its default. "},
    {WBEM_S_DIFFERENT, L"The objects are different. "},
    {WBEM_S_TIMEDOUT, L"The call timed out. "},
    {WBEM_S_NO_MORE_DATA, L"No more data. "},
    {WBEM_S_OPERATION_CANCELLED, L"The operation was cancelled. "},
    {WBEM_S_PENDING, L"The operation is pending. "},
    {WBEM_S_DUPLICATE_OBJECTS, L"Duplicate objects. "},
    {WBEM_S_ACCESS_DENIED, L"Access denied. "},
    {WBEM_S_PARTIAL_RESULTS, L"Partial results. "},
    {WBEM_E_FAILED, L"Call failed. "},
    {WBEM_E_NOT_FOUND, L"Object not found. "},
    {WBEM_E_ACCESS_DENIED, L"Access denied. "},
    {WBEM_E_PROVIDER_FAILURE, L"Provider failure. "},
    {WBEM_E_TYPE_MISMATCH, L"Type mismatch. "},
    {WBEM_E_OUT_OF_MEMORY, L"Out of memory. "},
    {WBEM_E_INVALID_CONTEXT, L"Invalid context. "},
    {WBEM_E_INVALID_PARAMETER, L"Invalid parameter. "},
    {WBEM_E_NOT_AVAILABLE, L"Not available. "},
    {WBEM_E_CRITICAL_ERROR, L"Critical error. "},
    {WBEM_E_INVALID_STREAM, L"Invalid stream. "},
    {WBEM_E_NOT_SUPPORTED, L"Not supported. "},
    {WBEM_E_INVALID_SUPERCLASS, L"Invalid superclass. "},
    {WBEM_E_INVALID_NAMESPACE, L"Invalid namespace. "},
    {WBEM_E_INVALID_OBJECT, L"Invalid object. "},
    {WBEM_E_INVALID_CLASS, L"Invalid class. "},
    {WBEM_E_PROVIDER_NOT_FOUND, L"Provider not found. "},
    {WBEM_E_INVALID_PROVIDER_REGISTRATION, L"Invalid provider registration. "},
    {WBEM_E_PROVIDER_LOAD_FAILURE, L"Provider load failure. "},
    {WBEM_E_INITIALIZATION_FAILURE, L"Initialization failure. "},
    {WBEM_E_TRANSPORT_FAILURE, L"Transport failure. "},
    {WBEM_E_INVALID_OPERATION, L"Invalid operation. "},
    {WBEM_E_INVALID_QUERY, L"Invalid query. "},
    {WBEM_E_INVALID_QUERY_TYPE, L"Invalid query type. "},
    {WBEM_E_ALREADY_EXISTS, L"Already exists. "},
    {WBEM_E_OVERRIDE_NOT_ALLOWED, L"Override not allowed. "},
    {WBEM_E_PROPAGATED_QUALIFIER, L"Propagated qualifier. "},
    {WBEM_E_PROPAGATED_PROPERTY, L"Propagated property. "},
    {WBEM_E_UNEXPECTED, L"Unexpected error. "},
    {WBEM_E_ILLEGAL_OPERATION, L"Illegal operation. "},
    {WBEM_E_CANNOT_BE_KEY, L"Cannot be key. "},
    {WBEM_E_INCOMPLETE_CLASS, L"Incomplete class. "},
    {WBEM_E_INVALID_SYNTAX, L"Invalid syntax. "},
    {WBEM_E_NONDECORATED_OBJECT, L"Nondecorated object. "},
    {WBEM_E_READ_ONLY, L"Read only. "},
    {WBEM_E_PROVIDER_NOT_CAPABLE, L"Provider not capable. "},
    {WBEM_E_CLASS_HAS_CHILDREN, L"Class has children. "},
    {WBEM_E_CLASS_HAS_INSTANCES, L"Class has instances. "},
    {WBEM_E_QUERY_NOT_IMPLEMENTED, L"Query not implemented. "},
    {WBEM_E_ILLEGAL_NULL, L"Illegal null. "},
    {WBEM_E_INVALID_QUALIFIER_TYPE, L"Invalid qualifier type. "},
    {WBEM_E_INVALID_PROPERTY_TYPE, L"Invalid property type. "},
    {WBEM_E_VALUE_OUT_OF_RANGE, L"Value out of range. "},
    {WBEM_E_CANNOT_BE_SINGLETON, L"Cannot be singleton. "},
    {WBEM_E_INVALID_CIM_TYPE, L"Invalid CIM type. "},
    {WBEM_E_INVALID_METHOD, L"Invalid method. "},
    {WBEM_E_INVALID_METHOD_PARAMETERS, L"Invalid method parameters. "},
    {WBEM_E_SYSTEM_PROPERTY, L"System property. "},
    {WBEM_E_INVALID_PROPERTY, L"Invalid property. "},
    {WBEM_E_CALL_CANCELLED, L"Call cancelled. "},
    {WBEM_E_SHUTTING_DOWN, L"Shutting down. "},
    {WBEM_E_PROPAGATED_METHOD, L"Propagated method. "},
    {WBEM_E_UNSUPPORTED_PARAMETER, L"Unsupported parameter. "},
    {WBEM_E_MISSING_PARAMETER_ID, L"Missing parameter ID. "},
    {WBEM_E_INVALID_PARAMETER_ID, L"Invalid parameter ID. "},
    {WBEM_E_NONCONSECUTIVE_PARAMETER_IDS, L"Nonconsecutive parameter IDs. "},
    {WBEM_E_PARAMETER_ID_ON_RETVAL, L"Parameter ID on retval. "},
    {WBEM_E_INVALID_OBJECT_PATH, L"Invalid object path. "},
    {WBEM_E_OUT_OF_DISK_SPACE, L"Out of disk space. "},
    {WBEM_E_BUFFER_TOO_SMALL, L"Buffer too small. "},
    {WBEM_E_UNSUPPORTED_PUT_EXTENSION, L"Unsupported put extension. "},
    {WBEM_E_UNKNOWN_OBJECT_TYPE, L"Unknown object type. "},
    {WBEM_E_UNKNOWN_PACKET_TYPE, L"Unknown packet type. "},
    {WBEM_E_MARSHAL_VERSION_MISMATCH, L"Marshal version mismatch. "},
    {WBEM_E_MARSHAL_INVALID_SIGNATURE, L"Marshal invalid signature. "},
    {WBEM_E_INVALID_QUALIFIER, L"Invalid qualifier. "},
    {WBEM_E_INVALID_DUPLICATE_PARAMETER, L"Invalid duplicate parameter. "},
    {WBEM_E_TOO_MUCH_DATA, L"Too much data. "},
    {WBEM_E_SERVER_TOO_BUSY, L"Server too busy. "},
    {WBEM_E_INVALID_FLAVOR, L"Invalid flavor. "},
    {WBEM_E_CIRCULAR_REFERENCE, L"Circular reference. "},
    {WBEM_E_UNSUPPORTED_CLASS_UPDATE, L"Unsupported class update. "},
    {WBEM_E_CANNOT_CHANGE_KEY_INHERITANCE, L"Cannot change key inheritance. "},
    {WBEM_E_TOO_MANY_PROPERTIES, L"Too many properties. "},
    {WBEM_E_UPDATE_TYPE_MISMATCH, L"Update type mismatch. "},
    {WBEM_E_UPDATE_OVERRIDE_NOT_ALLOWED, L"Update override not allowed. "},
    {WBEM_E_UPDATE_PROPAGATED_METHOD, L"Update propagated method. "},
    {WBEM_E_METHOD_NOT_IMPLEMENTED, L"Method not implemented. "},
    {WBEM_E_METHOD_DISABLED, L"Method disabled. "},
    {WBEM_E_REFRESHER_BUSY, L"Refresher busy. "},
    {WBEM_E_UNPARSABLE_QUERY, L"Unparsable query. "},
    {WBEM_E_NOT_EVENT_CLASS, L"Not event class. "},
    {WBEM_E_MISSING_GROUP_WITHIN, L"Missing GROUP WITHIN. "},
    {WBEM_E_MISSING_AGGREGATION_LIST, L"Missing aggregation list. "},
    {WBEM_E_PROPERTY_NOT_AN_OBJECT, L"Property not an object. "},
    {WBEM_E_AGGREGATING_BY_OBJECT, L"Aggregating by object. "},
    {WBEM_E_UNINTERPRETABLE_PROVIDER_QUERY, L"Uninterpretable provider query. "},
    {WBEM_E_BACKUP_RESTORE_WINMGMT_RUNNING, L"Backup restore WinMgmt running. "},
    {WBEM_E_QUEUE_OVERFLOW, L"Queue overflow. "},
    {WBEM_E_PRIVILEGE_NOT_HELD, L"Privilege not held. "},
    {WBEM_E_INVALID_OPERATOR, L"Invalid operator. "},
    {WBEM_E_LOCAL_CREDENTIALS, L"Local credentials. "},
    {WBEM_E_CANNOT_BE_ABSTRACT, L"Cannot be abstract. "},
    {WBEM_E_AMENDED_OBJECT, L"Amended object. "},
    {WBEM_E_CLIENT_TOO_SLOW, L"Client too slow. "},
    {WBEM_E_NULL_SECURITY_DESCRIPTOR, L"Null security descriptor. "},
    {WBEM_E_TIMED_OUT, L"Timed out. "},
    {WBEM_E_INVALID_ASSOCIATION, L"Invalid association. "},
    {WBEM_E_AMBIGUOUS_OPERATION, L"Ambiguous operation. "},
    {WBEM_E_QUOTA_VIOLATION, L"Quota violation. "},
    {WBEM_E_UNSUPPORTED_LOCALE, L"Unsupported locale. "},
    {WBEM_E_HANDLE_OUT_OF_DATE, L"Handle out of date. "},
    {WBEM_E_CONNECTION_FAILED, L"Connection failed. "},
    {WBEM_E_INVALID_HANDLE_REQUEST, L"Invalid handle request. "},
    {WBEM_E_PROPERTY_NAME_TOO_WIDE, L"Property name too wide. "},
    {WBEM_E_CLASS_NAME_TOO_WIDE, L"Class name too wide. "},
    {WBEM_E_METHOD_NAME_TOO_WIDE, L"Method name too wide. "},
    {WBEM_E_QUALIFIER_NAME_TOO_WIDE, L"Qualifier name too wide. "},
    {WBEM_E_RERUN_COMMAND, L"Rerun command. "},
    {WBEM_E_DATABASE_VER_MISMATCH, L"Database version mismatch. "},
    {WBEM_E_VETO_DELETE, L"Veto delete. "},
    {WBEM_E_VETO_PUT, L"Veto put. "},
    {WBEM_E_INVALID_LOCALE, L"Invalid locale. "},
    {WBEM_E_PROVIDER_SUSPENDED, L"Provider suspended. "},
    {WBEM_E_SYNCHRONIZATION_REQUIRED, L"Synchronization required. "},
    {WBEM_E_NO_SCHEMA, L"No schema. "},
    {WBEM_E_PROVIDER_ALREADY_REGISTERED, L"Provider already registered. "},
    {WBEM_E_PROVIDER_NOT_REGISTERED, L"Provider not registered. "},
    {WBEM_E_FATAL_TRANSPORT_ERROR, L"Fatal transport error. "},
    {WBEM_E_ENCRYPTED_CONNECTION_REQUIRED, L"Encrypted connection required. "},
};

typedef struct ComStatus {
    const IWbemStatusCodeTextVtbl *lpVtbl;
    LONG refs;
} ComStatus;

static HRESULT STDMETHODCALLTYPE st_QueryInterface(IWbemStatusCodeText *This, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWbemStatusCodeText)) {
        *out = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return wb_no_interface("st", riid, out);
}

static ULONG STDMETHODCALLTYPE st_AddRef(IWbemStatusCodeText *This)
{
    return (ULONG)InterlockedIncrement(&((ComStatus *)This)->refs);
}

static ULONG STDMETHODCALLTYPE st_Release(IWbemStatusCodeText *This)
{
    ComStatus *s = (ComStatus *)This;
    LONG n = InterlockedDecrement(&s->refs);
    if (!n) {
        wb_free(s);
        InterlockedDecrement(&g_wbem_locks);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE st_GetErrorCodeText(IWbemStatusCodeText *This, HRESULT code, LCID locale, LONG flags, BSTR *text)
{
    if (!text)
        return WBEM_E_INVALID_PARAMETER;
    *text = nullptr;
    for (UINT i = 0; i < RTL_NUMBER_OF(MESSAGES); i++) {
        if (MESSAGES[i].code == code) {
            *text = SysAllocString(MESSAGES[i].text);
            return S_OK;
        }
    }
    WCHAR *buffer = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                             (DWORD)code, 0, (LPWSTR)&buffer, 0, nullptr);
    if (!n || !buffer)
        return WBEM_E_NOT_FOUND;
    *text = SysAllocString(buffer);
    LocalFree(buffer);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE st_GetFacilityCodeText(IWbemStatusCodeText *This, HRESULT code, LCID locale, LONG flags,
                                                        BSTR *text)
{
    if (!text)
        return WBEM_E_INVALID_PARAMETER;
    ULONG facility = ((ULONG)code >> 16) & 0x7FF;
    const WCHAR *name = facility == 4 ? L"WBEM" : facility == 7 ? L"Win32" : facility == 1 ? L"RPC" : facility == 0 ? L"Null" : nullptr;
    *text = nullptr;
    if (!name)
        return WBEM_E_NOT_FOUND;
    *text = SysAllocString(name);
    return S_OK;
}

static const IWbemStatusCodeTextVtbl STATUS_VTBL = {
    st_QueryInterface, st_AddRef, st_Release, st_GetErrorCodeText, st_GetFacilityCodeText,
};

HRESULT wm_status_text_new(REFIID riid, void **out)
{
    ComStatus *s = wb_alloc(sizeof(ComStatus));
    if (!s)
        return E_OUTOFMEMORY;
    s->lpVtbl = &STATUS_VTBL;
    s->refs = 1;
    InterlockedIncrement(&g_wbem_locks);
    HRESULT hr = st_QueryInterface((IWbemStatusCodeText *)s, riid, out);
    st_Release((IWbemStatusCodeText *)s);
    return hr;
}
