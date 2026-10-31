/*
 * the configuration of the channels and the calls that take or give a channel, a publisher or a log file. The classic
 * channels exist and hold no event, no publisher is registered, and what the event log service would do is refused
 * the way a call to a service that is not running is.
 */
#include "wevtapi_private.h"

#define ERROR_EVT_CHANNEL_NOT_FOUND_ 15007L

enum { VT_NULL = 0, VT_STRING = 1, VT_UINT32 = 8, VT_UINT64 = 10, VT_BOOLEAN = 13, VT_GUID = 15, VT_ARRAY = 128 };

/* the properties of a channel in the order of EVT_CHANNEL_CONFIG_PROPERTY_ID. */
enum {
    CFG_ENABLED, CFG_ISOLATION, CFG_TYPE, CFG_OWNING_PUBLISHER, CFG_CLASSIC, CFG_ACCESS, CFG_RETENTION,
    CFG_AUTO_BACKUP, CFG_MAX_SIZE, CFG_LOG_FILE_PATH, CFG_LEVEL, CFG_KEYWORDS, CFG_CONTROL_GUID, CFG_BUFFER_SIZE,
    CFG_MIN_BUFFERS, CFG_MAX_BUFFERS, CFG_LATENCY, CFG_CLOCK_TYPE, CFG_SID_TYPE, CFG_PUBLISHER_LIST, CFG_FILE_MAX,
    CFG_COUNT
};

#define TEXT_MAX 512

typedef struct {
    BYTE set;
    DWORD type;
    ULONGLONG value;
    GUID guid;
    WCHAR text[TEXT_MAX];
} PROPERTY;

typedef struct {
    PROPERTY properties[CFG_COUNT];
} CONFIG;

/* ------------------------------------------------------------------ enumerations */

static BOOL string_check(DWORD size, LPWSTR buffer, PDWORD used)
{
    if (!used || (size && !buffer)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

EVT_HANDLE WINAPI EvtOpenChannelEnum(EVT_HANDLE session, DWORD flags)
{
    if (flags)
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    DWORD error = EvtCheckSession(session);
    if (error)
        return EvtFailHandle(error);
    return EvtCreateObject(OBJ_CHANNEL_ENUM, FALSE);
}

BOOL WINAPI EvtNextChannelPath(EVT_HANDLE enumerator, DWORD size, LPWSTR buffer, PDWORD used)
{
    if (!string_check(size, buffer, used))
        return FALSE;
    OBJECT *object = EvtLookupObject(enumerator, OBJ_CHANNEL_ENUM);
    if (!object)
        return EvtFail(ERROR_INVALID_HANDLE);
    if (object->index >= CHANNEL_COUNT)
        return EvtFail(ERROR_NO_MORE_ITEMS);
    LPCWSTR name = g_evt_channels[object->index];
    DWORD needed = (DWORD)wcslen(name) + 1;
    *used = needed;
    if (size < needed)
        return EvtFail(ERROR_INSUFFICIENT_BUFFER);
    memcpy(buffer, name, needed * sizeof(WCHAR));
    object->index++;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/* no publisher is registered. */
EVT_HANDLE WINAPI EvtOpenPublisherEnum(EVT_HANDLE session, DWORD flags)
{
    if (flags)
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    DWORD error = EvtCheckSession(session);
    if (error)
        return EvtFailHandle(error);
    return EvtCreateObject(OBJ_PUBLISHER_ENUM, FALSE);
}

BOOL WINAPI EvtNextPublisherId(EVT_HANDLE enumerator, DWORD size, LPWSTR buffer, PDWORD used)
{
    if (!string_check(size, buffer, used))
        return FALSE;
    if (!EvtLookupObject(enumerator, OBJ_PUBLISHER_ENUM))
        return EvtFail(ERROR_INVALID_HANDLE);
    return EvtFail(ERROR_NO_MORE_ITEMS);
}

/* ------------------------------------------------------------------ channel configuration */

static const LPCWSTR g_application_access =
    L"O:BAG:SYD:(A;;0xf0007;;;SY)(A;;0x7;;;BA)(A;;0x7;;;SO)(A;;0x3;;;IU)(A;;0x3;;;SU)(A;;0x3;;;S-1-5-3)"
    L"(A;;0x3;;;S-1-5-33)(A;;0x1;;;S-1-5-32-573)";
static const LPCWSTR g_system_access =
    L"O:BAG:SYD:(A;;0xf0007;;;SY)(A;;0x7;;;BA)(A;;0x3;;;BO)(A;;0x5;;;SO)(A;;0x1;;;IU)(A;;0x3;;;SU)(A;;0x1;;;S-1-5-3)"
    L"(A;;0x2;;;S-1-5-33)(A;;0x1;;;S-1-5-32-573)";
static const LPCWSTR g_security_access = L"O:BAG:SYD:(A;;0xf0005;;;SY)(A;;0x5;;;BA)(A;;0x1;;;S-1-5-32-573)";

/* what the machine has for a channel: the Windows defaults of the classic channels. */
static void default_property(int channel, DWORD id, PROPERTY *out)
{
    memset(out, 0, sizeof(*out));
    out->type = VT_UINT32;
    switch (id) {
    case CFG_ENABLED: out->type = VT_BOOLEAN; out->value = 1; break;
    case CFG_ISOLATION: out->value = channel == 1 ? 1 : channel == 3 ? 2 : 0; break;
    case CFG_TYPE: out->value = channel == 2 || channel == 4 ? 1 : 0; break;
    case CFG_OWNING_PUBLISHER: out->type = VT_STRING; break;
    case CFG_CLASSIC: out->type = VT_BOOLEAN; out->value = channel <= 1 || channel == 3; break;
    case CFG_ACCESS:
        out->type = VT_STRING;
        wcscpy(out->text, channel == 1 ? g_system_access : channel == 3 ? g_security_access : g_application_access);
        break;
    case CFG_RETENTION: case CFG_AUTO_BACKUP: out->type = VT_BOOLEAN; break;
    case CFG_MAX_SIZE: out->type = VT_UINT64; out->value = channel == 2 ? 1052672 : 20971520; break;
    case CFG_LOG_FILE_PATH:
        out->type = VT_STRING;
        wcscpy(out->text, L"%SystemRoot%\\System32\\Winevt\\Logs\\");
        wcscat(out->text, g_evt_channels[channel]);
        wcscat(out->text, L".evtx");
        break;
    case CFG_KEYWORDS: out->type = VT_UINT64; break;
    case CFG_CONTROL_GUID: out->type = VT_GUID; break;
    case CFG_PUBLISHER_LIST: out->type = VT_STRING | VT_ARRAY; break;
    case CFG_FILE_MAX: out->value = 1; break;
    }
}

EVT_HANDLE WINAPI EvtOpenChannelConfig(EVT_HANDLE session, LPCWSTR path, DWORD flags)
{
    if (flags || !path)
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    DWORD error = EvtCheckSession(session);
    if (error)
        return EvtFailHandle(error);
    int channel = EvtFindChannel(path);
    if (channel < 0)
        return EvtFailHandle(ERROR_EVT_CHANNEL_NOT_FOUND_);
    CONFIG *config = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*config));
    if (!config)
        return EvtFailHandle(ERROR_OUTOFMEMORY);
    EVT_HANDLE handle = EvtCreateObject(OBJ_CHANNEL_CONFIG, FALSE);
    if (!handle) {
        HeapFree(GetProcessHeap(), 0, config);
        return nullptr;
    }
    OBJECT *object = EvtLookupObject(handle, OBJ_CHANNEL_CONFIG);
    object->channel = channel;
    object->data = config;
    SetLastError(ERROR_SUCCESS);
    return handle;
}

/* a value in the buffer behind the variant, which is how the strings and arrays come back. */
static BOOL put_variant(DWORD type, DWORD count, ULONGLONG value, const void *extra, DWORD extra_size, DWORD size,
                        EVT_VARIANT *out, PDWORD used)
{
    DWORD needed = sizeof(EVT_VARIANT) + extra_size;
    *used = needed;
    if (size < needed || !out)
        return EvtFail(ERROR_INSUFFICIENT_BUFFER);
    memset(out, 0, sizeof(*out));
    out->Type = type;
    out->Count = count;
    if (extra_size) {
        memcpy(out + 1, extra, extra_size);
        out->Pointer = out + 1;
    } else {
        out->UInt64Val = value;
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI EvtGetChannelConfigProperty(EVT_HANDLE config_handle, DWORD id, DWORD flags, DWORD size, EVT_VARIANT *value,
                                        PDWORD used)
{
    if (flags || !used || (size && !value))
        return EvtFail(ERROR_INVALID_PARAMETER);
    OBJECT *object = EvtLookupObject(config_handle, OBJ_CHANNEL_CONFIG);
    if (!object)
        return EvtFail(ERROR_INVALID_HANDLE);
    if (id >= CFG_COUNT)
        return EvtFail(ERROR_INVALID_PARAMETER);
    CONFIG *config = object->data;
    PROPERTY fallback;
    PROPERTY *property = &config->properties[id];
    if (!property->set) {
        default_property(object->channel, id, &fallback);
        property = &fallback;
    }
    switch (property->type) {
    case VT_NULL:
        return put_variant(VT_NULL, 0, 0, nullptr, 0, size, value, used);
    case VT_STRING:
        return put_variant(VT_STRING, 0, 0, property->text, (DWORD)(wcslen(property->text) + 1) * sizeof(WCHAR), size,
                           value, used);
    case VT_GUID:
        return put_variant(VT_GUID, 0, 0, &property->guid, sizeof(GUID), size, value, used);
    case VT_STRING | VT_ARRAY:
        return put_variant(VT_STRING | VT_ARRAY, 0, 0, nullptr, 0, size, value, used);
    case VT_BOOLEAN:
        return put_variant(VT_BOOLEAN, 0, property->value != 0, nullptr, 0, size, value, used);
    default:
        return put_variant(property->type, 0, property->value, nullptr, 0, size, value, used);
    }
}

BOOL WINAPI EvtSetChannelConfigProperty(EVT_HANDLE config_handle, DWORD id, DWORD flags, EVT_VARIANT *value)
{
    if (flags || !value)
        return EvtFail(ERROR_INVALID_PARAMETER);
    OBJECT *object = EvtLookupObject(config_handle, OBJ_CHANNEL_CONFIG);
    if (!object)
        return EvtFail(ERROR_INVALID_HANDLE);
    if (id == CFG_PUBLISHER_LIST)
        return EvtFail(ERROR_INVALID_OPERATION);
    if (id >= CFG_COUNT)
        return EvtFail(ERROR_INVALID_PARAMETER);
    PROPERTY next;
    memset(&next, 0, sizeof(next));
    next.set = 1;
    next.type = value->Type;
    switch (value->Type) {
    case VT_NULL:
        break;
    case VT_STRING:
        if (!value->Pointer || wcslen(value->Pointer) >= TEXT_MAX)
            return EvtFail(ERROR_INVALID_PARAMETER);
        wcscpy(next.text, value->Pointer);
        break;
    case VT_UINT32:
        next.value = value->UInt32Val;
        break;
    case VT_UINT64:
        next.value = value->UInt64Val;
        break;
    case VT_BOOLEAN:
        next.value = value->BooleanVal != 0;
        break;
    case VT_GUID:
        if (!value->Pointer)
            return EvtFail(ERROR_INVALID_PARAMETER);
        next.guid = *(GUID *)value->Pointer;
        break;
    default:
        return EvtFail(ERROR_INVALID_PARAMETER);
    }
    ((CONFIG *)object->data)->properties[id] = next;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/* the configuration is kept by the event log service, which is not here to take it. */
BOOL WINAPI EvtSaveChannelConfig(EVT_HANDLE config_handle, DWORD flags)
{
    if (flags)
        return EvtFail(ERROR_INVALID_PARAMETER);
    if (!EvtLookupObject(config_handle, OBJ_CHANNEL_CONFIG))
        return EvtFail(ERROR_INVALID_HANDLE);
    return EvtFail(RPC_S_SERVER_UNAVAILABLE);
}

/* ------------------------------------------------------------------ queries */

BOOL WINAPI EvtGetQueryInfo(EVT_HANDLE query, DWORD property, DWORD size, EVT_VARIANT *value, PDWORD used)
{
    if ((size && !value) || !used)
        return EvtFail(ERROR_INVALID_PARAMETER);
    OBJECT *object = EvtLookupObject(query, OBJ_RESULTS);
    if (!object)
        object = EvtLookupObject(query, OBJ_SUBSCRIPTION);
    if (!object)
        return EvtFail(ERROR_INVALID_HANDLE);
    if (property > 1)
        return EvtFail(ERROR_INVALID_PARAMETER);
    DWORD count = object->path ? 1 : 0;
    if (property == 1) {
        DWORD status = 0;
        return put_variant(VT_UINT32 | VT_ARRAY, count, 0, &status, count * sizeof(DWORD), size, value, used);
    }
    /* the pointers to the names come first, then the names. */
    DWORD length = object->path ? (DWORD)(wcslen(object->path) + 1) * sizeof(WCHAR) : 0;
    DWORD needed = sizeof(EVT_VARIANT) + count * sizeof(LPWSTR) + length;
    *used = needed;
    if (size < needed || !value)
        return EvtFail(ERROR_INSUFFICIENT_BUFFER);
    memset(value, 0, sizeof(*value));
    value->Type = VT_STRING | VT_ARRAY;
    value->Count = count;
    if (count) {
        LPWSTR *names = (LPWSTR *)(value + 1);
        WCHAR *text = (WCHAR *)(names + 1);
        memcpy(text, object->path, length);
        names[0] = text;
        value->Pointer = names;
    }
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/* an event is never given, so the questions about one have no handle to ask. */
BOOL WINAPI EvtGetEventInfo(EVT_HANDLE event, DWORD property, DWORD size, EVT_VARIANT *value, PDWORD used)
{
    if ((size && !value) || !used)
        return EvtFail(ERROR_INVALID_PARAMETER);
    return EvtFail(ERROR_INVALID_HANDLE);
}

/* ------------------------------------------------------------------ publishers */

EVT_HANDLE WINAPI EvtOpenPublisherMetadata(EVT_HANDLE session, LPCWSTR publisher, LPCWSTR log_file, DWORD locale,
                                           DWORD flags)
{
    if (flags)
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    DWORD error = EvtCheckSession(session);
    if (error)
        return EvtFailHandle(error);
    if (log_file && GetFileAttributesW(log_file) == INVALID_FILE_ATTRIBUTES)
        return EvtFailHandle(GetLastError());
    if (!publisher)
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    return EvtFailHandle(ERROR_FILE_NOT_FOUND);
}

/* no publisher metadata, event metadata or array of objects can be opened, so each has no handle. */
BOOL WINAPI EvtGetPublisherMetadataProperty(EVT_HANDLE publisher, DWORD property, DWORD flags, DWORD size,
                                            EVT_VARIANT *value, PDWORD used)
{
    if (flags || (size && !value) || !used)
        return EvtFail(ERROR_INVALID_PARAMETER);
    return EvtFail(ERROR_INVALID_HANDLE);
}

EVT_HANDLE WINAPI EvtOpenEventMetadataEnum(EVT_HANDLE publisher, DWORD flags)
{
    if (flags)
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    return EvtFailHandle(ERROR_INVALID_HANDLE);
}

EVT_HANDLE WINAPI EvtNextEventMetadata(EVT_HANDLE enumerator, DWORD flags)
{
    if (flags)
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    return EvtFailHandle(ERROR_INVALID_HANDLE);
}

BOOL WINAPI EvtGetEventMetadataProperty(EVT_HANDLE event_metadata, DWORD property, DWORD flags, DWORD size,
                                        EVT_VARIANT *value, PDWORD used)
{
    if (flags || (size && !value) || !used)
        return EvtFail(ERROR_INVALID_PARAMETER);
    return EvtFail(ERROR_INVALID_HANDLE);
}

BOOL WINAPI EvtGetObjectArraySize(EVT_HANDLE array, PDWORD size)
{
    if (!size)
        return EvtFail(ERROR_INVALID_PARAMETER);
    return EvtFail(ERROR_INVALID_HANDLE);
}

BOOL WINAPI EvtGetObjectArrayProperty(EVT_HANDLE array, DWORD property, DWORD index, DWORD flags, DWORD size,
                                      EVT_VARIANT *value, PDWORD used)
{
    if (flags)
        return EvtFail(ERROR_INVALID_PARAMETER);
    return EvtFail(ERROR_INVALID_HANDLE);
}

/*
 * the message of an event or of a message number of a publisher. an event is never given and no publisher can be
 * opened, so what is left is the check of the arguments.
 */
BOOL WINAPI EvtFormatMessage(EVT_HANDLE publisher, EVT_HANDLE event, DWORD message_id, DWORD count, EVT_VARIANT *values,
                             DWORD flags, DWORD size, LPWSTR buffer, PDWORD used)
{
    if (used)
        *used = 0;
    if (flags == 0 || flags > 9)
        return EvtFail(ERROR_INVALID_PARAMETER);
    BOOL by_id = flags == 8 && message_id != (DWORD)-1;
    if (by_id ? event != nullptr : event == nullptr)
        return EvtFail(ERROR_INVALID_PARAMETER);
    if (!buffer && size)
        return EvtFail(ERROR_INVALID_PARAMETER);
    return EvtFail(ERROR_INVALID_HANDLE);
}

/* ------------------------------------------------------------------ log files */

static ULONG crc32(const BYTE *data, SIZE_T length)
{
    ULONG crc = 0xFFFFFFFF;
    while (length--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320 & -(LONG)(crc & 1));
    }
    return ~crc;
}

/* the header of an event log file that has no chunk, as the file of a log that holds nothing. */
static DWORD write_empty_log(LPCWSTR target)
{
    BYTE header[4096] = {'E', 'l', 'f', 'F', 'i', 'l', 'e', 0};
    *(ULONGLONG *)(header + 24) = 1;
    *(DWORD *)(header + 32) = 128;
    *(WORD *)(header + 36) = 1;
    *(WORD *)(header + 38) = 3;
    *(WORD *)(header + 40) = 4096;
    *(DWORD *)(header + 124) = crc32(header, 120);
    HANDLE file = CreateFileW(target, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    DWORD written = 0;
    BOOL ok = WriteFile(file, header, sizeof(header), &written, nullptr);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    return error;
}

BOOL WINAPI EvtExportLog(EVT_HANDLE session, LPCWSTR path, LPCWSTR query, LPCWSTR target, DWORD flags)
{
    if (!flags || (flags & 0xFFFFCEFC) || !target)
        return EvtFail(ERROR_INVALID_PARAMETER);
    BOOL file = (flags & EvtQueryFilePath) != 0;
    if (file && !path)
        return EvtFail(ERROR_INVALID_PARAMETER);
    DWORD error = EvtCheckSession(session);
    if (!error)
        error = EvtCheckSource(path, file);
    if (!error)
        error = write_empty_log(target);
    if (error)
        return EvtFail(error);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/* the events of an exported log have no publisher to take the messages from, and a log of none has none. */
BOOL WINAPI EvtArchiveExportedLog(EVT_HANDLE session, LPCWSTR path, DWORD locale, DWORD flags)
{
    if (flags || !path)
        return EvtFail(ERROR_INVALID_PARAMETER);
    DWORD error = EvtCheckSession(session);
    if (!error && GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        error = GetLastError();
    if (error)
        return EvtFail(error);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI EvtClearLog(EVT_HANDLE session, LPCWSTR path, LPCWSTR target, DWORD flags)
{
    if (flags || !path)
        return EvtFail(ERROR_INVALID_PARAMETER);
    DWORD error = EvtCheckSession(session);
    if (!error && EvtFindChannel(path) < 0)
        error = ERROR_EVT_CHANNEL_NOT_FOUND_;
    if (!error && target)
        error = write_empty_log(target);
    if (error)
        return EvtFail(error);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/* ------------------------------------------------------------------ internal calls */

DWORD WINAPI EvtIntSysprepCleanup(void)
{
    return ERROR_SUCCESS;
}

/* the registration of a publisher's channels is the service's. */
BOOL WINAPI EvtIntAssertConfig(ULONG_PTR a1, ULONG_PTR path, ULONG_PTR flags)
{
    if (flags & ~1u)
        return EvtFail(ERROR_INVALID_PARAMETER);
    return EvtFail(RPC_S_SERVER_UNAVAILABLE);
}

BOOL WINAPI EvtIntRetractConfig(ULONG_PTR a1, ULONG_PTR path, ULONG_PTR flags)
{
    if (flags & ~1u)
        return EvtFail(ERROR_INVALID_PARAMETER);
    return EvtFail(RPC_S_SERVER_UNAVAILABLE);
}

/* a log file for the program to write needs a writer of event log files. */
EVT_HANDLE WINAPI EvtIntCreateLocalLogfile(LPCWSTR path, DWORD flags)
{
    if (flags || !path)
        return EvtFailHandle(ERROR_INVALID_PARAMETER);
    return EvtFailHandle(ERROR_NOT_SUPPORTED);
}

BOOL WINAPI EvtIntWriteXmlEventToLocalLogfile(EVT_HANDLE log, LPCWSTR xml, DWORD flags)
{
    return EvtFail(ERROR_INVALID_HANDLE);
}

DWORD WINAPI EvtIntCreateBinXMLFromCustomXML(ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5,
                                             ULONG_PTR a6, ULONG_PTR a7, ULONG_PTR a8)
{
    return ERROR_NOT_SUPPORTED;
}

/* the name of a classic log is its channel's. */
BOOL WINAPI EvtIntGetClassicLogDisplayName(ULONG_PTR a1, LPCWSTR path, DWORD locale, DWORD flags, DWORD size,
                                           LPWSTR buffer, PDWORD used)
{
    if (flags || !path || !used || (!buffer && size))
        return EvtFail(ERROR_INVALID_PARAMETER);
    int channel = EvtFindChannel(path);
    if (channel < 0)
        return EvtFail(ERROR_EVT_CHANNEL_NOT_FOUND_);
    DWORD needed = (DWORD)wcslen(g_evt_channels[channel]) + 1;
    *used = needed;
    if (size < needed)
        return EvtFail(ERROR_INSUFFICIENT_BUFFER);
    memcpy(buffer, g_evt_channels[channel], needed * sizeof(WCHAR));
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/* the template of an event is read from the resources of its publisher, which none has. */
BOOL WINAPI EvtIntRenderResourceEventTemplate(ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5,
                                              ULONG_PTR a6, ULONG_PTR a7, ULONG_PTR a8)
{
    return EvtFail(ERROR_NOT_SUPPORTED);
}
