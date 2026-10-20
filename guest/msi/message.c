#include "msi_private.h"
#include <mime/win32/winuser.h>

static INSTALLUILEVEL ui_level = INSTALLUILEVEL_DEFAULT;
static HWND ui_window;
static INSTALLUI_HANDLER_RECORD ui_handler;
static DWORD ui_filter;
static PVOID ui_context;
/* the handler of MsiSetExternalUI, which is given the text of the message and not the record. */
static INSTALLUI_HANDLERW text_handler;
static BOOL text_handler_ansi;
static DWORD text_filter;
static PVOID text_context;
static HANDLE log_file;
static DWORD log_mode, log_attributes, log_lines;

void MimeMsiCloseLog(void)
{
    if (log_file) {
        FlushFileBuffers(log_file);
        CloseHandle(log_file);
        log_file = nullptr;
    }
    log_lines = 0;
}

BOOL MimeMsiLogEnabled(void)
{
    return log_file != nullptr;
}

INSTALLUILEVEL WINAPI MsiSetInternalUI(INSTALLUILEVEL level, HWND *window)
{
    if (((UINT)level & 0x1f) > INSTALLUILEVEL_FULL || ((UINT)level & ~0x3ffu))
        return INSTALLUILEVEL_NOCHANGE;
    AcquireSRWLockExclusive(&MimeMsiLock);
    INSTALLUILEVEL previous = ui_level;
    if (level != INSTALLUILEVEL_NOCHANGE)
        ui_level = level;
    if (window) {
        HWND old_window = ui_window;
        ui_window = *window;
        *window = old_window;
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return previous;
}

UINT WINAPI MsiSetExternalUIRecord(INSTALLUI_HANDLER_RECORD handler, DWORD filter, PVOID context,
                                  PINSTALLUI_HANDLER_RECORD previous)
{
    if (filter & ~0x0e00ffffu)
        return ERROR_INVALID_PARAMETER;
    AcquireSRWLockExclusive(&MimeMsiLock);
    if (previous)
        *previous = ui_handler;
    ui_handler = handler;
    ui_filter = filter;
    ui_context = context;
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return ERROR_SUCCESS;
}

/* the filter takes the message kinds and nothing of the bits of the log modes that are not messages. */
static BOOL set_text_handler(INSTALLUI_HANDLERW handler, DWORD filter, PVOID context, BOOL ansi,
                             INSTALLUI_HANDLERW *previous)
{
    AcquireSRWLockExclusive(&MimeMsiLock);
    *previous = text_handler_ansi == ansi ? text_handler : nullptr;
    text_handler = handler;
    text_handler_ansi = ansi;
    text_filter = filter;
    text_context = context;
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return TRUE;
}

INSTALLUI_HANDLERW WINAPI MsiSetExternalUIW(INSTALLUI_HANDLERW handler, DWORD filter, PVOID context)
{
    INSTALLUI_HANDLERW previous;
    set_text_handler(handler, filter, context, FALSE, &previous);
    return previous;
}

INSTALLUI_HANDLERA WINAPI MsiSetExternalUIA(INSTALLUI_HANDLERA handler, DWORD filter, PVOID context)
{
    INSTALLUI_HANDLERW previous;
    set_text_handler((INSTALLUI_HANDLERW)handler, filter, context, TRUE, &previous);
    return (INSTALLUI_HANDLERA)previous;
}

UINT WINAPI MsiEnableLogW(DWORD mode, LPCWSTR path, DWORD attributes)
{
    if (attributes & ~3u || (path && (!*path || !mode || (mode & ~0xffffu))))
        return ERROR_INVALID_PARAMETER;
    if (path && (mode & INSTALLLOGMODE_LOGONLYONERROR))
        return ERROR_CALL_NOT_IMPLEMENTED;
    HANDLE file = nullptr;
    if (path) {
        file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           attributes & INSTALLLOGATTRIBUTES_APPEND ? OPEN_ALWAYS : CREATE_ALWAYS, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return ERROR_INSTALL_LOG_FAILURE;
        LARGE_INTEGER zero = {}, position;
        if (!SetFilePointerEx(file, zero, &position, FILE_END)) {
            CloseHandle(file);
            return ERROR_INSTALL_LOG_FAILURE;
        }
        if (!position.QuadPart) {
            WCHAR bom = 0xfeff;
            DWORD written;
            if (!WriteFile(file, &bom, sizeof(bom), &written, nullptr) || written != sizeof(bom)) {
                CloseHandle(file);
                return ERROR_INSTALL_LOG_FAILURE;
            }
        }
    }
    AcquireSRWLockExclusive(&MimeMsiLock);
    MimeMsiCloseLog();
    log_file = file;
    log_mode = mode;
    log_attributes = attributes;
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return ERROR_SUCCESS;
}

static BOOL log_message(UINT kind, PCWSTR text, DWORD length)
{
    if (!log_file || kind == 5 || kind == 10 || (kind >= 12 && kind <= 14) || kind >= 25)
        return TRUE;
    if (!(log_mode & (1u << kind)) && !(log_mode & INSTALLLOGMODE_VERBOSE))
        return TRUE;
    if (length > UINT32_MAX / sizeof(WCHAR))
        return FALSE;
    DWORD bytes = length * sizeof(WCHAR), written;
    if (!WriteFile(log_file, text, bytes, &written, nullptr) || written != bytes ||
        !WriteFile(log_file, L"\r\n", 4, &written, nullptr) || written != 4)
        return FALSE;
    if (++log_lines >= 20 || (log_attributes & INSTALLLOGATTRIBUTES_FLUSHEACHLINE)) {
        log_lines = 0;
        return FlushFileBuffers(log_file);
    }
    return TRUE;
}

static PCWSTR message_template(MIME_MSI_PACKAGE *package, UINT kind, const MIME_MSI_RECORD *record)
{
    if (record->Fields[0].Kind != FIELD_NULL)
        return nullptr;
    if (kind == 9)
        return package->ActionFormat;
    if (kind > 3 || !record->Count)
        return nullptr;
    int code = MimeMsiFieldGetInteger(&record->Fields[1]);
    MIME_MSI_TABLE *table;
    if (code == MSI_NULL_INTEGER || MimeMsiLoadTable(package->Database, L"Error", &table) || table->Count != 2)
        return nullptr;
    if ((table->Columns[1].Type & 0x0c00) != 0x0c00 || table->Columns[0].Width != 2)
        return nullptr;
    for (ULONG row = 0; row < table->Rows; row++)
        if ((int)MimeMsiCell(table, row, 0) - 0x8000 == code)
            return package->Database->Strings[MimeMsiCell(table, row, 1)];
    return nullptr;
}

int WINAPI MsiProcessMessage(MSIHANDLE install, INSTALLMESSAGE type, MSIHANDLE handle)
{
    MIME_REMOTE(PROXY_ProcessMessage, int, MIME_ARG(install), MIME_ARG(type), MIME_ARG(handle));
    UINT kind = (UINT)type >> 24;
    if (kind > 27 || (kind > 15 && kind < 25))
        return -1;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *package = (void *)MimeMsiGetHandle(install, MIME_MSI_KIND_PACKAGE);
    MIME_MSI_RECORD *record = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_RECORD);
    UINT error = package && record ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
    MIME_MSI_RECORD *formatted_record = record, *copy = nullptr;
    PWSTR text = nullptr;
    DWORD length = 0;
    if (!error && kind == 8 && record->Count >= 3) {
        PCWSTR template = record->Fields[3].Kind == FIELD_STRING ? record->Fields[3].String : L"";
        PWSTR replacement = MimeMsiDuplicate(template);
        if (!replacement)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else {
            MimeMsiFree(package->ActionFormat);
            package->ActionFormat = replacement;
        }
    }
    if (!error) {
        PCWSTR template = message_template(package, kind, record);
        if (template) {
            SIZE_T bytes = sizeof(*record) + (record->Count + 1) * sizeof(MIME_MSI_FIELD);
            copy = MimeMsiAllocate(bytes);
            if (!copy)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else {
                memcpy(copy, record, bytes);
                copy->Fields[0] = (MIME_MSI_FIELD){FIELD_STRING, 0, (PWSTR)template};
                formatted_record = copy;
            }
        }
    }
    if (!error)
        error = MimeMsiFormatRecord(package, formatted_record, &text, &length);
    if (!error && !log_message(kind, text, length))
        error = ERROR_INSTALL_LOG_FAILURE;
    MimeMsiFree(copy);
    INSTALLUI_HANDLER_RECORD handler = (ui_filter & (1u << kind)) ? ui_handler : nullptr;
    PVOID context = ui_context;
    HWND window = ui_window;
    INSTALLUILEVEL level = ui_level;
    /* the record handler wins over the one of the text. */
    INSTALLUI_HANDLERW legacy = !handler && (text_filter & (1u << kind)) ? text_handler : nullptr;
    BOOL legacy_ansi = text_handler_ansi;
    PVOID legacy_context = text_context;
    ReleaseSRWLockExclusive(&MimeMsiLock);
    int result = error ? -1 : 0;
    if (!error && handler) {
        result = handler(context, (UINT)type, handle);
    } else if (!error && legacy) {
        if (!legacy_ansi) {
            result = legacy(legacy_context, (UINT)type, text);
        } else {
            int need = text ? WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr) : 0;
            char *ansi = need ? MimeMsiAllocate(need) : nullptr;
            if (ansi)
                WideCharToMultiByte(CP_ACP, 0, text, -1, ansi, need, nullptr, nullptr);
            result = ((INSTALLUI_HANDLERA)legacy)(legacy_context, (UINT)type, ansi ? ansi : "");
            MimeMsiFree(ansi);
        }
    }
    if (!result && kind <= 3 && ((UINT)level & 0x1f) != INSTALLUILEVEL_NONE &&
        !(level & INSTALLUILEVEL_PROGRESSONLY))
        result = MessageBoxW(window, text, L"Windows Installer", (UINT)type & 0x00ffffff);
    MimeMsiFree(text);
    return result;
}
