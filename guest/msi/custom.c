#include "msi_private.h"

/*
 * custom actions, the rows of the CustomAction table, run now or deferred to the script. the low three bits of the
 * type say what runs: 1 a library, 2 a program, 3 text, 5 and 6 scripts. bits 4 and 5 say where it comes from: 0 the
 * Binary table, 0x10 a package file, 0x20 a directory, 0x30 a property. the flags above: 0x40 ignores the result,
 * 0x80 does not wait, 0x400 defers the action to the script, with 0x100 for rollback and 0x200 for commit.
 */

enum { CA_DLL = 1, CA_EXE = 2, CA_TEXT = 3, CA_JSCRIPT = 5, CA_VBSCRIPT = 6, CA_INSTALL = 7 };
enum { SOURCE_BINARY = 0, SOURCE_FILE = 0x10, SOURCE_DIRECTORY = 0x20, SOURCE_PROPERTY = 0x30 };
#define CA_CONTINUE 0x40
#define CA_ASYNC 0x80
#define CA_INSCRIPT 0x400
#define CA_ROLLBACK 0x100
#define CA_COMMIT 0x200

UINT MimeMsiFormat(MIME_MSI_PACKAGE *package, PCWSTR template, PWSTR *out)
{
    *out = nullptr;
    if (!template || !*template) {
        *out = MimeMsiDuplicate(L"");
        return *out ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    }
    MIME_MSI_RECORD *record = MimeMsiRecordCreate(0);
    if (!record)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD length;
    UINT error = MimeMsiFieldString(&record->Fields[0], template);
    if (!error)
        error = MimeMsiFormatRecord(package, record, out, &length);
    record->Object.Destroy(&record->Object);
    return error;
}

void MimeMsiCustomActionFree(MIME_MSI_CA_ROW *row)
{
    MimeMsiFree(row->Name);
    MimeMsiFree(row->Source);
    MimeMsiFree(row->Target);
    memset(row, 0, sizeof(*row));
}

UINT MimeMsiCustomActionLookup(MIME_MSI_PACKAGE *package, PCWSTR name, MIME_MSI_CA_ROW *row)
{
    memset(row, 0, sizeof(*row));
    MIME_MSI_DATABASE *database = package->Database;
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(database, L"CustomAction", &table);
    if (error)
        return error == ERROR_BAD_QUERY_SYNTAX ? ERROR_FUNCTION_NOT_CALLED : error;
    ULONG action = MimeMsiColumn(table, L"Action", TRUE);
    ULONG type = MimeMsiColumn(table, L"Type", FALSE);
    ULONG source = MimeMsiColumn(table, L"Source", TRUE);
    ULONG target = MimeMsiColumn(table, L"Target", TRUE);
    if (action == UINT32_MAX || type == UINT32_MAX || source == UINT32_MAX || target == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG i = 0; i < table->Rows; i++) {
        PCWSTR candidate = MimeMsiText(database, table, i, action);
        if (!candidate || wcscmp(candidate, name))
            continue;
        int kind = MimeMsiInteger(table, i, type);
        if (kind == MSI_NULL_INTEGER || kind < 0)
            return ERROR_INSTALL_PACKAGE_INVALID;
        PCWSTR from = MimeMsiText(database, table, i, source);
        PCWSTR to = MimeMsiText(database, table, i, target);
        row->Type = (UINT)kind;
        row->Name = MimeMsiDuplicate(name);
        row->Source = MimeMsiDuplicate(from ? from : L"");
        row->Target = MimeMsiDuplicate(to ? to : L"");
        if (!row->Name || !row->Source || !row->Target) {
            MimeMsiCustomActionFree(row);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        return ERROR_SUCCESS;
    }
    return ERROR_FUNCTION_NOT_CALLED;
}

/* logs the text verbatim: it goes in a record field, not the template, so it is not formatted. */
static void info_message(MSIHANDLE handle, PCWSTR text)
{
    MSIHANDLE record = MsiCreateRecord(1);
    if (!record)
        return;
    if (!MsiRecordSetStringW(record, 0, L"[1]") && !MsiRecordSetStringW(record, 1, text))
        MsiProcessMessage(handle, INSTALLMESSAGE_INFO, record);
    MsiCloseHandle(record);
}

UINT MimeMsiMachineOfImage(const BYTE *data, SIZE_T size, WORD *machine)
{
    if (size < 0x40 || data[0] != 'M' || data[1] != 'Z')
        return ERROR_BAD_EXE_FORMAT;
    ULONG at = *(const ULONG *)(data + 0x3c);
    if (at > size - 6 || memcmp(data + at, "PE\0\0", 4))
        return ERROR_BAD_EXE_FORMAT;
    *machine = *(const WORD *)(data + at + 4);
    return ERROR_SUCCESS;
}

/* writes bytes to a temporary file with an extension the loader or the shell recognizes. */
static UINT write_temporary(const BYTE *data, SIZE_T size, PCWSTR extension, WCHAR path[MAX_PATH])
{
    WCHAR directory[MAX_PATH], name[MAX_PATH];
    if (!GetTempPathW(RTL_NUMBER_OF(directory), directory) || !GetTempFileNameW(directory, L"MSI", 0, name))
        return GetLastError();
    /* replace the unique name's extension with the wanted one. */
    wcscpy(path, name);
    SIZE_T length = wcslen(path);
    if (length > 4 && path[length - 4] == L'.')
        wcscpy(path + length - 4, extension);
    DeleteFileW(name);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    UINT error = ERROR_SUCCESS;
    while (size && !error) {
        DWORD wanted = size < 0x100000 ? (DWORD)size : 0x100000, written;
        if (!WriteFile(file, data, wanted, &written, nullptr) || written != wanted)
            error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        data += wanted;
        size -= wanted;
    }
    CloseHandle(file);
    if (error)
        DeleteFileW(path);
    return error;
}

static UINT installed_file_path(MIME_MSI_PACKAGE *package, PCWSTR key, PWSTR *out)
{
    MIME_MSI_TABLE *files;
    UINT error = MimeMsiLoadTable(package->Database, L"File", &files);
    if (error)
        return error;
    ULONG id = MimeMsiColumn(files, L"File", TRUE);
    ULONG component = MimeMsiColumn(files, L"Component_", TRUE);
    ULONG filename = MimeMsiColumn(files, L"FileName", TRUE);
    if (id == UINT32_MAX || component == UINT32_MAX || filename == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < files->Rows; row++) {
        PCWSTR candidate = MimeMsiText(package->Database, files, row, id);
        if (!candidate || wcscmp(candidate, key))
            continue;
        PCWSTR name = MimeMsiText(package->Database, files, row, filename);
        PCWSTR owner = MimeMsiText(package->Database, files, row, component);
        PCWSTR directory = nullptr;
        BOOL selected;
        if (!name || !owner)
            return ERROR_INSTALL_PACKAGE_INVALID;
        error = MimeMsiComponentSelected(package, owner, &directory, &selected);
        if (error)
            return error;
        PCWSTR long_name = wcsrchr(name, L'|');
        PWSTR folder = nullptr;
        error = MimeMsiDirectoryPath(package, directory, FALSE, &folder);
        if (error)
            return error;
        *out = MimeMsiJoinPath(folder, long_name ? long_name + 1 : name);
        MimeMsiFree(folder);
        return *out ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    }
    return ERROR_INSTALL_PACKAGE_INVALID;
}

/* maps a library or program result to what the action sequence expects. */
static UINT translate_result(UINT result)
{
    switch (result) {
    case ERROR_SUCCESS:
    case ERROR_NO_MORE_ITEMS:
    case ERROR_INSTALL_USEREXIT:
    case ERROR_INSTALL_FAILURE:
        return result;
    case ERROR_FUNCTION_NOT_CALLED:
        return ERROR_SUCCESS;
    default:
        return ERROR_INSTALL_FAILURE;
    }
}

typedef UINT (WINAPI *CA_ENTRY)(MSIHANDLE);

typedef struct CA_CALL {
    CA_ENTRY Function;
    MSIHANDLE Session;
    UINT Result;
} CA_CALL;

static DWORD WINAPI call_entry(PVOID parameter)
{
    CA_CALL *call = parameter;
    call->Result = call->Function(call->Session);
    return 0;
}

/* an action runs on a thread of its own as in the server of windows, so the handles it closes are only its own. */
static UINT call_on_thread(CA_ENTRY function, MSIHANDLE session, UINT *result)
{
    CA_CALL call = {.Function = function, .Session = session};
    HANDLE thread = CreateThread(nullptr, 0, call_entry, &call, 0, nullptr);
    if (!thread)
        return GetLastError();
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    *result = call.Result;
    return ERROR_SUCCESS;
}

static UINT run_library(MIME_MSI_PACKAGE *package, MSIHANDLE session, const MIME_MSI_CA_ROW *row, UINT *result)
{
    UINT source = row->Type & 0x30;
    WCHAR path[MAX_PATH];
    BOOL temporary = FALSE;
    AcquireSRWLockExclusive(&MimeMsiLock);
    UINT error = ERROR_SUCCESS;
    BYTE *bytes = nullptr;
    SIZE_T size = 0;
    PWSTR installed = nullptr;
    if (source == SOURCE_BINARY) {
        WCHAR name[256];
        _snwprintf(name, RTL_NUMBER_OF(name), L"Binary.%s", row->Source);
        error = MimeMsiReadBinary(package->Database, name, &bytes, &size);
        if (error == ERROR_FILE_NOT_FOUND)
            error = ERROR_INSTALL_PACKAGE_INVALID;
    } else if (source == SOURCE_FILE) {
        error = installed_file_path(package, row->Source, &installed);
        if (!error && wcslen(installed) >= MAX_PATH)
            error = ERROR_BAD_PATHNAME;
        if (!error)
            wcscpy(path, installed);
    } else
        error = ERROR_CALL_NOT_IMPLEMENTED;
    MimeMsiFree(installed);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    WORD machine = 0;
    if (!error && bytes) {
        error = MimeMsiMachineOfImage(bytes, size, &machine);
        if (!error)
            error = write_temporary(bytes, size, L".dll", path);
        temporary = !error;
    } else if (!error) {
        /* an installed library is also checked for its machine type. */
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        BYTE header[0x400];
        DWORD read = 0;
        if (file == INVALID_HANDLE_VALUE)
            error = GetLastError();
        else {
            if (!ReadFile(file, header, sizeof(header), &read, nullptr))
                error = GetLastError();
            CloseHandle(file);
            if (!error)
                error = MimeMsiMachineOfImage(header, read, &machine);
        }
    }
    MimeMsiFree(bytes);
    if (error)
        return error;
    char entry[256];
    int length = WideCharToMultiByte(CP_ACP, 0, row->Target, -1, entry, sizeof(entry), nullptr, nullptr);
    if (!length) {
        error = ERROR_INSTALL_PACKAGE_INVALID;
    } else if (machine == IMAGE_FILE_MACHINE_NATIVE) {
        HMODULE library = LoadLibraryW(path);
        CA_ENTRY function = library ? (CA_ENTRY)GetProcAddress(library, entry) : nullptr;
        if (!function) {
            error = library ? ERROR_PROC_NOT_FOUND : GetLastError();
            if (!error)
                error = ERROR_MOD_NOT_FOUND;
            if (library)
                FreeLibrary(library);
        } else {
            error = call_on_thread(function, session, result);
            FreeLibrary(library);
        }
    } else {
        error = MimeMsiRemoteRun(package, session, path, entry, machine, result);
    }
    if (temporary)
        DeleteFileW(path);
    return error;
}

/* the text of a script file or stream: utf-16 or utf-8 with a mark, or in the ansi code page. */
static PWSTR decode_script(const BYTE *data, SIZE_T size)
{
    PWSTR text;
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        SIZE_T count = (size - 2) / sizeof(WCHAR);
        text = MimeMsiAllocate((count + 1) * sizeof(WCHAR));
        if (text)
            memcpy(text, data + 2, count * sizeof(WCHAR));
        return text;
    }
    UINT page = CP_ACP;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        page = CP_UTF8;
        data += 3;
        size -= 3;
    }
    int count = size ? MultiByteToWideChar(page, 0, (PCSTR)data, (int)size, nullptr, 0) : 0;
    text = MimeMsiAllocate(((SIZE_T)count + 1) * sizeof(WCHAR));
    if (text && count)
        MultiByteToWideChar(page, 0, (PCSTR)data, (int)size, text, count);
    return text;
}

static UINT read_file_bytes(PCWSTR path, BYTE **data, SIZE_T *size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    LARGE_INTEGER length;
    UINT error = ERROR_SUCCESS;
    if (!GetFileSizeEx(file, &length) || length.QuadPart > 0x10000000)
        error = GetLastError() ? GetLastError() : ERROR_FILE_TOO_LARGE;
    else {
        *data = MimeMsiAllocate((SIZE_T)length.QuadPart + 1);
        DWORD read = 0;
        if (!*data)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else if (!ReadFile(file, *data, (DWORD)length.QuadPart, &read, nullptr))
            error = GetLastError();
        *size = read;
    }
    CloseHandle(file);
    return error;
}

/* the script of an action comes from the Binary table, an installed file, a property or the target itself. */
static UINT script_source(MIME_MSI_PACKAGE *package, const MIME_MSI_CA_ROW *row, PWSTR *text, PCWSTR *function)
{
    UINT source = row->Type & 0x30;
    UINT error = ERROR_SUCCESS;
    BYTE *bytes = nullptr;
    SIZE_T size = 0;
    *text = nullptr;
    *function = row->Target;
    AcquireSRWLockExclusive(&MimeMsiLock);
    if (source == SOURCE_BINARY) {
        WCHAR name[256];
        _snwprintf(name, RTL_NUMBER_OF(name), L"Binary.%s", row->Source);
        error = MimeMsiReadBinary(package->Database, name, &bytes, &size);
        if (error == ERROR_FILE_NOT_FOUND)
            error = ERROR_INSTALL_PACKAGE_INVALID;
    } else if (source == SOURCE_FILE) {
        PWSTR installed = nullptr;
        error = installed_file_path(package, row->Source, &installed);
        if (!error)
            error = read_file_bytes(installed, &bytes, &size);
        MimeMsiFree(installed);
    } else if (source == SOURCE_PROPERTY) {
        *text = MimeMsiDuplicate(MimeMsiGetProperty(package, row->Source));
        if (!*text)
            error = ERROR_NOT_ENOUGH_MEMORY;
    } else {
        /* the script is the target, and no function is called. */
        *text = MimeMsiDuplicate(row->Target);
        *function = L"";
        if (!*text)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!error && bytes) {
        *text = decode_script(bytes, size);
        if (!*text)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    MimeMsiFree(bytes);
    return error;
}

typedef struct CA_SCRIPT {
    MSIHANDLE Session;
    BOOL Js;
    PCWSTR Text, Function, Action;
    UINT Result, Error;
} CA_SCRIPT;

static DWORD WINAPI call_script(PVOID parameter)
{
    CA_SCRIPT *call = parameter;
    call->Error = MimeMsiRunActionScript(call->Session, call->Js, call->Text, call->Function, call->Action, &call->Result);
    return 0;
}

/* what a script returns is a button of a dialog: 1 is success, 2 a user exit, 3 a failure, 4 a suspension. */
static UINT script_return(UINT value)
{
    switch (value) {
    case 0: return ERROR_FUNCTION_NOT_CALLED;
    case 1: return ERROR_SUCCESS;
    case 2: return ERROR_INSTALL_USEREXIT;
    case 3: return ERROR_INSTALL_FAILURE;
    case 4: return ERROR_INSTALL_SUSPEND;
    default: return ERROR_NO_MORE_ITEMS;
    }
}

/* runs the script of an action. a 32-bit script of a 64-bit installer runs in the 32-bit host. */
static UINT run_script(MIME_MSI_PACKAGE *package, MSIHANDLE session, const MIME_MSI_CA_ROW *row, UINT *result)
{
    PWSTR text;
    PCWSTR function;
    UINT error = script_source(package, row, &text, &function);
    if (error) {
        MsiCloseHandle(session);
        return error;
    }
    BOOL js = (row->Type & 7) == CA_JSCRIPT;
    UINT value = 0;
    if (sizeof(void *) == 8 && !(row->Type & 0x1000)) {
        error = MimeMsiRemoteScript(package, session, js, text, function, row->Name, &value);
        MsiCloseHandle(session);
    } else {
        CA_SCRIPT call = {session, js, text, function, row->Name};
        HANDLE thread = CreateThread(nullptr, 0, call_script, &call, 0, nullptr);
        if (!thread) {
            error = GetLastError();
            MsiCloseHandle(session);
        } else {
            WaitForSingleObject(thread, INFINITE);
            CloseHandle(thread);
            error = call.Error;
            value = call.Result;
        }
    }
    MimeMsiFree(text);
    *result = error ? ERROR_INSTALL_FAILURE : script_return(value);
    return ERROR_SUCCESS;
}

static UINT run_program(MIME_MSI_PACKAGE *package, const MIME_MSI_CA_ROW *row, PCWSTR arguments, UINT *result)
{
    UINT source = row->Type & 0x30;
    WCHAR path[MAX_PATH];
    BOOL temporary = FALSE;
    PWSTR command_line = nullptr;
    AcquireSRWLockExclusive(&MimeMsiLock);
    UINT error = ERROR_SUCCESS;
    if (source == SOURCE_BINARY) {
        WCHAR name[256];
        BYTE *bytes = nullptr;
        SIZE_T size = 0;
        _snwprintf(name, RTL_NUMBER_OF(name), L"Binary.%s", row->Source);
        error = MimeMsiReadBinary(package->Database, name, &bytes, &size);
        if (!error) {
            error = write_temporary(bytes, size, L".exe", path);
            temporary = !error;
        }
        MimeMsiFree(bytes);
    } else if (source == SOURCE_FILE) {
        PWSTR installed = nullptr;
        error = installed_file_path(package, row->Source, &installed);
        if (!error && wcslen(installed) < MAX_PATH)
            wcscpy(path, installed);
        else if (!error)
            error = ERROR_BAD_PATHNAME;
        MimeMsiFree(installed);
    } else if (source == SOURCE_PROPERTY) {
        PCWSTR value = MimeMsiGetProperty(package, row->Source);
        if (!*value || wcslen(value) >= MAX_PATH)
            error = ERROR_INSTALL_PACKAGE_INVALID;
        else
            wcscpy(path, value);
    } else {
        PWSTR folder = nullptr;
        error = MimeMsiDirectoryPath(package, row->Source, FALSE, &folder);
        if (!error && wcslen(folder) < MAX_PATH)
            wcscpy(path, folder);
        else if (!error)
            error = ERROR_BAD_PATHNAME;
        MimeMsiFree(folder);
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (error)
        return error;
    SIZE_T capacity = wcslen(path) + wcslen(arguments) + 8;
    command_line = MimeMsiAllocate(capacity * sizeof(WCHAR));
    if (!command_line)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (source == SOURCE_DIRECTORY) {
        /* the target is the whole command line and the directory its working directory, where a program named
           without a path is. */
        PCWSTR program = arguments + (*arguments == L'"');
        BOOL full = (program[0] && program[1] == L':') || (program[0] == L'\\' && program[1] == L'\\');
        _snwprintf(command_line, capacity, L"%s%s", full ? L"" : path, arguments);
    } else
        _snwprintf(command_line, capacity, L"\"%s\" %s", path, arguments);
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(nullptr, command_line, nullptr, nullptr, FALSE, 0, nullptr,
                        source == SOURCE_DIRECTORY ? path : nullptr, &startup, &process)) {
        error = GetLastError();
        WCHAR note[300];
        _snwprintf(note, RTL_NUMBER_OF(note), L"msi: the program of an action did not start, error %u: %.200s\n", error, command_line);
        OutputDebugStringW(note);
    } else {
        CloseHandle(process.hThread);
        if (row->Type & CA_ASYNC) {
            *result = ERROR_SUCCESS;
        } else {
            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(process.hProcess, &code);
            *result = code == 0 ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
            if (code) {
                WCHAR note[300];
                _snwprintf(note, RTL_NUMBER_OF(note), L"msi: the program of an action ended with %u: %.200s\n", code, command_line);
                OutputDebugStringW(note);
            }
        }
        CloseHandle(process.hProcess);
    }
    MimeMsiFree(command_line);
    if (temporary && !(row->Type & CA_ASYNC))
        DeleteFileW(path);
    return error;
}

/* a deferred action's session only has its scheduled data and three package properties. */
static UINT open_session(MIME_MSI_PACKAGE *parent, PCWSTR data, ULONG kind, MSIHANDLE *handle)
{
    *handle = 0;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_PACKAGE *session = MimeMsiAllocate(sizeof(*session));
    UINT error = session ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    if (!error) {
        session->Object.Kind = MIME_MSI_KIND_PACKAGE;
        session->Object.Destroy = MimeMsiDestroyPackage;
        session->Database = parent->Database;
        parent->Database->Object.References++;
        session->Options = parent->Options;
        session->SessionKind = kind;
        session->InScript = TRUE;
        session->Is64Bit = parent->Is64Bit;
        error = MimeMsiSetProperty(session, L"CustomActionData", data);
        if (!error)
            error = MimeMsiSetProperty(session, L"ProductCode", MimeMsiGetProperty(parent, L"ProductCode"));
        if (!error)
            error = MimeMsiSetProperty(session, L"UserSID", MimeMsiGetProperty(parent, L"UserSID"));
        if (!error && !(*handle = MimeMsiAddHandle(&session->Object)))
            error = ERROR_NOT_ENOUGH_MEMORY;
        if (error)
            MimeMsiDestroyPackage(&session->Object);
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return error;
}

/* runs a script's library or program in a session of its own. */
static UINT run_scheduled(MIME_MSI_PACKAGE *package, MIME_MSI_SCRIPT_ACTION *action, ULONG kind)
{
    MSIHANDLE session;
    UINT error = open_session(package, action->Data ? action->Data : L"", kind, &session);
    if (error)
        return error;
    UINT result = ERROR_SUCCESS;
    UINT base = action->Row.Type & 7;
    if (base == CA_DLL)
        error = run_library(package, session, &action->Row, &result);
    else if (base == CA_EXE) {
        PWSTR arguments = nullptr;
        AcquireSRWLockExclusive(&MimeMsiLock);
        error = MimeMsiFormat(package, action->Row.Target, &arguments);
        ReleaseSRWLockExclusive(&MimeMsiLock);
        if (!error)
            error = run_program(package, &action->Row, arguments, &result);
        MimeMsiFree(arguments);
    } else if (base == CA_JSCRIPT || base == CA_VBSCRIPT) {
        /* the script takes the session along and closes it. */
        error = run_script(package, session, &action->Row, &result);
        if (error)
            return error;
        return translate_result(result);
    } else
        error = ERROR_CALL_NOT_IMPLEMENTED;
    MsiCloseHandle(session);
    if (error)
        return error;
    return translate_result(result);
}

static void free_action(MIME_MSI_SCRIPT_ACTION *action)
{
    MimeMsiCustomActionFree(&action->Row);
    MimeMsiFree(action->Data);
    MimeMsiFree(action);
}

static void free_list(MIME_MSI_SCRIPT_ACTION **list)
{
    while (*list) {
        MIME_MSI_SCRIPT_ACTION *action = *list;
        *list = action->Next;
        free_action(action);
    }
}

void MimeMsiClearScript(MIME_MSI_PACKAGE *package)
{
    free_list(&package->Deferred);
    package->DeferredTail = &package->Deferred;
    free_list(&package->Rollback);
    free_list(&package->Commit);
}

static UINT schedule(MIME_MSI_PACKAGE *package, const MIME_MSI_CA_ROW *row)
{
    MIME_MSI_SCRIPT_ACTION *action = MimeMsiAllocate(sizeof(*action));
    if (!action)
        return ERROR_NOT_ENOUGH_MEMORY;
    action->Row.Type = row->Type;
    action->Row.Name = MimeMsiDuplicate(row->Name);
    action->Row.Source = MimeMsiDuplicate(row->Source);
    action->Row.Target = MimeMsiDuplicate(row->Target);
    /* the action's data is the property named after it, captured when scheduled. */
    action->Data = MimeMsiDuplicate(MimeMsiGetProperty(package, row->Name));
    if (!action->Row.Name || !action->Row.Source || !action->Row.Target || !action->Data) {
        free_action(action);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (row->Type & CA_ROLLBACK) {
        /* the last scheduled is the first rolled back. */
        action->Next = package->Rollback;
        package->Rollback = action;
    } else if (row->Type & CA_COMMIT) {
        MIME_MSI_SCRIPT_ACTION **tail = &package->Commit;
        while (*tail)
            tail = &(*tail)->Next;
        *tail = action;
    } else {
        if (!package->DeferredTail)
            package->DeferredTail = &package->Deferred;
        *package->DeferredTail = action;
        package->DeferredTail = &action->Next;
    }
    return ERROR_SUCCESS;
}

UINT MimeMsiRunScript(MSIHANDLE handle, MIME_MSI_PACKAGE *package)
{
    UINT error = ERROR_SUCCESS;
    for (;;) {
        AcquireSRWLockExclusive(&MimeMsiLock);
        MIME_MSI_SCRIPT_ACTION *action = package->Deferred;
        if (action) {
            package->Deferred = action->Next;
            if (!package->Deferred)
                package->DeferredTail = &package->Deferred;
            action->Next = nullptr;
        }
        ReleaseSRWLockExclusive(&MimeMsiLock);
        if (!action)
            break;
        UINT result = run_scheduled(package, action, 2);
        WCHAR text[512];
        _snwprintf(text, RTL_NUMBER_OF(text), L"Deferred action %s returned %u", action->Row.Name, result);
        info_message(handle, text);
        BOOL ignore = !!(action->Row.Type & CA_CONTINUE);
        free_action(action);
        if (result && !ignore && result != ERROR_NO_MORE_ITEMS) {
            error = result;
            break;
        }
    }
    return error;
}

static UINT run_list(MIME_MSI_PACKAGE *package, MIME_MSI_SCRIPT_ACTION **list, ULONG kind)
{
    UINT error = ERROR_SUCCESS;
    for (;;) {
        AcquireSRWLockExclusive(&MimeMsiLock);
        MIME_MSI_SCRIPT_ACTION *action = *list;
        if (action)
            *list = action->Next;
        ReleaseSRWLockExclusive(&MimeMsiLock);
        if (!action)
            break;
        UINT result = run_scheduled(package, action, kind);
        if (result && !(action->Row.Type & CA_CONTINUE) && !error)
            error = result;
        free_action(action);
    }
    return error;
}

UINT MimeMsiRunRollback(MIME_MSI_PACKAGE *package)
{
    return run_list(package, &package->Rollback, 3);
}

UINT MimeMsiRunCommit(MIME_MSI_PACKAGE *package)
{
    return run_list(package, &package->Commit, 4);
}

UINT MimeMsiRunCustomAction(MSIHANDLE handle, MIME_MSI_PACKAGE *package, MIME_MSI_CA_ROW *row, PWSTR *message)
{
    UINT type = row->Type;
    UINT base = type & 7;
    UINT source = type & 0x30;
    UINT error = ERROR_SUCCESS;
    if ((type & CA_INSCRIPT) && base != CA_TEXT) {
        AcquireSRWLockExclusive(&MimeMsiLock);
        /* outside a transaction there is no script to defer to. */
        error = package->TransactionActive ? schedule(package, row) : ERROR_SUCCESS;
        ReleaseSRWLockExclusive(&MimeMsiLock);
        return error;
    }
    if (base == CA_TEXT) {
        PWSTR formatted = nullptr;
        AcquireSRWLockExclusive(&MimeMsiLock);
        error = MimeMsiFormat(package, row->Target, &formatted);
        if (!error && source == SOURCE_PROPERTY) {
            if (!*row->Source)
                error = ERROR_INSTALL_PACKAGE_INVALID;
            else
                error = MimeMsiSetProperty(package, row->Source, formatted);
        } else if (!error && source == SOURCE_DIRECTORY) {
            /* before costing ends this is the directory's property, after it the target path and its children. */
            error = !*row->Source ? ERROR_INSTALL_PACKAGE_INVALID :
                    package->CostStage >= 3 && *formatted ? MimeMsiSetTargetPath(package, row->Source, formatted) :
                    MimeMsiSetProperty(package, row->Source, formatted);
        } else if (!error && source == SOURCE_FILE) {
            *message = formatted;
            formatted = nullptr;
            error = ERROR_INSTALL_FAILURE;
        } else if (!error)
            error = ERROR_CALL_NOT_IMPLEMENTED;
        ReleaseSRWLockExclusive(&MimeMsiLock);
        MimeMsiFree(formatted);
        return error;
    }
    UINT result = ERROR_SUCCESS;
    if (base == CA_DLL) {
        /* the action gets a handle of its own to the session, which it may close. */
        AcquireSRWLockExclusive(&MimeMsiLock);
        MIME_MSI_OBJECT *object = MimeMsiGetHandle(handle, 0);
        MSIHANDLE own = object ? MimeMsiAddHandle(object) : 0;
        ReleaseSRWLockExclusive(&MimeMsiLock);
        error = !object ? ERROR_INVALID_HANDLE : own ? run_library(package, own, row, &result) : ERROR_NOT_ENOUGH_MEMORY;
        if (own)
            MsiCloseHandle(own);
    } else if (base == CA_EXE) {
        PWSTR arguments = nullptr;
        AcquireSRWLockExclusive(&MimeMsiLock);
        error = MimeMsiFormat(package, row->Target, &arguments);
        ReleaseSRWLockExclusive(&MimeMsiLock);
        if (!error)
            error = run_program(package, row, arguments, &result);
        MimeMsiFree(arguments);
    } else if (base == CA_JSCRIPT || base == CA_VBSCRIPT) {
        AcquireSRWLockExclusive(&MimeMsiLock);
        MIME_MSI_OBJECT *object = MimeMsiGetHandle(handle, 0);
        MSIHANDLE own = object ? MimeMsiAddHandle(object) : 0;
        ReleaseSRWLockExclusive(&MimeMsiLock);
        error = !object ? ERROR_INVALID_HANDLE : own ? run_script(package, own, row, &result) : ERROR_NOT_ENOUGH_MEMORY;
    } else {
        error = ERROR_CALL_NOT_IMPLEMENTED;
    }
    if (error) {
        WCHAR text[512];
        _snwprintf(text, RTL_NUMBER_OF(text), L"Custom action %s could not run, error %u", row->Name, error);
        info_message(handle, text);
        return (type & CA_CONTINUE) ? ERROR_SUCCESS : error == ERROR_CALL_NOT_IMPLEMENTED ? error : ERROR_INSTALL_FAILURE;
    }
    result = translate_result(result);
    package->LastCustomActionError = result;
    if ((type & CA_CONTINUE) && result != ERROR_NO_MORE_ITEMS)
        return ERROR_SUCCESS;
    return result;
}
