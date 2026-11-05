/*
 * The x64 Visual C++ runtime for the x64 programs. Microsoft's redistributable is a Burn bundle: an x86 program whose
 * .wixburn section locates two cabinets, the one of the bootstrapper with the manifest, and the attached one with the
 * packages. On arm64 it installs only the arm64x runtime into System32. Mime runs x64 programs with x64 system dlls
 * from SysX64, so the files of the x64 packages are taken out of the bundle and written there, as their own installer
 * would write them into System32 on x64 Windows.
 */
#include "featurep.h"
#include "../lib/cabinet/cabinet.h"
#include <mime/win32/msi.h>

#define BURN_MAGIC 0x00F14300
#define MAX_PACKAGES 8
#define MAX_FILES 64

/* The names are those of the redistributable's packages, which the files list again. */
static const WCHAR *const g_patterns[] = {L"vcruntime140*.dll", L"msvcp140*.dll", L"concrt140.dll", L"vccorlib140.dll",
                                          L"vcamp140.dll",      L"vcomp140.dll",  L"mfc140*.dll",   L"mfcm140*.dll"};

typedef struct MEMBER {
    char Name[32];
    BYTE *Data;
    SIZE_T Size, Fill;
} MEMBER;

typedef struct GRAB {
    MEMBER *Members;
    ULONG Count;
} GRAB;

typedef struct RUNTIME_FILE {
    char Key[80];
    WCHAR Name[80];
    ULONG Size;
    BOOL Written;
} RUNTIME_FILE;

typedef struct WRITE {
    RUN *Run;
    RUNTIME_FILE *Files;
    ULONG Count;
    HANDLE Output;
    WCHAR Temporary[MAX_PATH], Target[MAX_PATH];
    DWORD Error;
} WRITE;

static void *cab_allocate(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static void cab_release(void *memory)
{
    HeapFree(GetProcessHeap(), 0, memory);
}

static void folder(RUN *run, WCHAR *path, SIZE_T chars)
{
    _snwprintf(path, chars, L"%lsWindows\\SysX64", run->Root);
    path[chars - 1] = 0;
}

/* ------------------------------------------------------------------ the bundle */

/* The cabinets of the bootstrapper and of the packages, found from the .wixburn section. */
static BOOL burn_containers(const BYTE *data, SIZE_T size, SIZE_T *ux, SIZE_T *ux_size, SIZE_T *attached,
                            SIZE_T *attached_size)
{
    if (size < 0x40 || data[0] != 'M' || data[1] != 'Z')
        return FALSE;
    ULONG pe = *(const ULONG *)(data + 0x3C);
    if (pe > size - 24 || memcmp(data + pe, "PE\0\0", 4))
        return FALSE;
    USHORT sections = *(const USHORT *)(data + pe + 6), optional = *(const USHORT *)(data + pe + 20);
    SIZE_T table = pe + 24 + optional;
    for (USHORT i = 0; i < sections; i++) {
        const BYTE *section = data + table + i * 40;
        if (table + (i + 1) * 40 > size)
            return FALSE;
        if (memcmp(section, ".wixburn", 8))
            continue;
        ULONG raw_size = *(const ULONG *)(section + 16), raw = *(const ULONG *)(section + 20);
        if (raw > size || raw_size > size - raw || raw_size < 56)
            return FALSE;
        const ULONG *header = (const ULONG *)(data + raw);
        if (header[0] != BURN_MAGIC || header[11] < 2)
            return FALSE;
        /* the stub, then the bootstrapper's cabinet, then the signature of both, then the attached cabinets. */
        ULONG stub = header[6], signature = header[8], signature_size = header[9];
        *ux = stub;
        *ux_size = header[12];
        *attached = signature_size ? (SIZE_T)signature + signature_size : stub + *ux_size;
        *attached_size = header[13];
        return *ux <= size && *ux_size <= size - *ux && *attached <= size && *attached_size <= size - *attached;
    }
    return FALSE;
}

static MEMBER *find_member(GRAB *grab, const char *name)
{
    for (ULONG i = 0; i < grab->Count; i++)
        if (!strcmp(grab->Members[i].Name, name))
            return &grab->Members[i];
    return nullptr;
}

static void *grab_begin(void *context, const MIME_CAB_FILE *file)
{
    MEMBER *member = find_member(context, file->Name);
    if (!member || member->Data)
        return nullptr;
    member->Data = HeapAlloc(GetProcessHeap(), 0, file->Size ? file->Size : 1);
    member->Size = file->Size;
    return member->Data ? member : nullptr;
}

static int grab_write(void *context, void *file_context, const uint8_t *data, size_t length)
{
    MEMBER *member = file_context;
    if (length > member->Size - member->Fill)
        return 1;
    memcpy(member->Data + member->Fill, data, length);
    member->Fill += length;
    return 0;
}

static int grab_end(void *context, void *file_context, const MIME_CAB_FILE *file)
{
    MEMBER *member = file_context;
    return member->Fill != member->Size;
}

/* Reads the members of a cabinet that the grab names. */
static DWORD grab(const BYTE *data, SIZE_T size, GRAB *grab)
{
    MIME_CAB *cab;
    int status = MimeCabOpen(&cab, data, size, cab_allocate, cab_release);
    if (status == MIME_CAB_OK) {
        static const MIME_CAB_FILE_SINK sink = {grab_begin, grab_write, grab_end};
        status = MimeCabExtract(cab, &sink, grab);
        MimeCabClose(cab);
    }
    if (status)
        return status == MIME_CAB_MEMORY ? ERROR_OUTOFMEMORY : ERROR_INVALID_DATA;
    for (ULONG i = 0; i < grab->Count; i++)
        if (!grab->Members[i].Data || grab->Members[i].Fill != grab->Members[i].Size)
            return ERROR_FILE_NOT_FOUND;
    return ERROR_SUCCESS;
}

static void grab_free(GRAB *grab)
{
    for (ULONG i = 0; i < grab->Count; i++)
        if (grab->Members[i].Data)
            HeapFree(GetProcessHeap(), 0, grab->Members[i].Data);
}

/* The value of an attribute of a manifest element, which ends at the element's end. */
static BOOL attribute(const char *element, const char *end, const char *name, char *value, SIZE_T chars)
{
    SIZE_T n = strlen(name);
    for (const char *p = element + 1; p + n + 2 < end; p++) {
        if (p[-1] != ' ' || memcmp(p, name, n) || p[n] != '=' || p[n + 1] != '"')
            continue;
        const char *start = p + n + 2, *stop = start;
        while (stop < end && *stop != '"')
            stop++;
        if (stop >= end || (SIZE_T)(stop - start) >= chars)
            return FALSE;
        memcpy(value, start, stop - start);
        value[stop - start] = 0;
        return TRUE;
    }
    return FALSE;
}

/*
 * The payloads of the x64 packages: each is a folder of packages whose name ends in _amd64, with the package and
 * its cabinet, both stored in the attached cabinet under the names the manifest gives.
 */
static ULONG x64_payloads(const MEMBER *manifest, char packages[][32], char cabinets[][32], ULONG max)
{
    char *text = HeapAlloc(GetProcessHeap(), 0, manifest->Size + 1);
    if (!text)
        return 0;
    memcpy(text, manifest->Data, manifest->Size);
    text[manifest->Size] = 0;
    char folders[MAX_PACKAGES][96];
    ULONG count = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (const char *p = strstr(text, "<Payload "); p; p = strstr(p + 1, "<Payload ")) {
            const char *end = strchr(p, '>');
            char path[160], source[32];
            if (!end || !attribute(p, end, "FilePath", path, sizeof(path)) ||
                !attribute(p, end, "SourcePath", source, sizeof(source)) || _strnicmp(path, "packages\\", 9))
                continue;
            char *slash = strchr(path + 9, '\\');
            if (!slash || slash - path < 15 || _strnicmp(slash - 6, "_amd64", 6))
                continue;
            *slash = 0;
            SIZE_T length = strlen(slash + 1);
            BOOL package = length > 4 && !_stricmp(slash + 1 + length - 4, ".msi");
            if (pass == 0 && package && count < max) {
                lstrcpynA(folders[count], path, sizeof(folders[count]));
                lstrcpynA(packages[count], source, 32);
                cabinets[count][0] = 0;
                count++;
            } else if (pass == 1 && !package) {
                for (ULONG i = 0; i < count; i++)
                    if (!_stricmp(folders[i], path))
                        lstrcpynA(cabinets[i], source, 32);
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, text);
    ULONG kept = 0;
    for (ULONG i = 0; i < count; i++) {
        if (!cabinets[i][0])
            continue;
        memmove(packages[kept], packages[i], 32);
        memmove(cabinets[kept], cabinets[i], 32);
        kept++;
    }
    return kept;
}

/* ------------------------------------------------------------------ the packages */

static BOOL record_text(MSIHANDLE record, UINT field, WCHAR *text, DWORD chars)
{
    DWORD size = chars;
    return MsiRecordGetStringW(record, field, text, &size) == ERROR_SUCCESS;
}

/* The files of a package that go to System64Folder, by the key that names them in its cabinet. */
static DWORD package_files(PCWSTR path, RUNTIME_FILE *files, ULONG *count, ULONG max)
{
    MSIHANDLE database, view, record;
    UINT error = MsiOpenDatabaseW(path, MSIDBOPEN_READONLY, &database);
    if (error)
        return error;
    WCHAR system[MAX_FILES][80];
    ULONG systems = 0;
    error = MsiDatabaseOpenViewW(database, L"SELECT `Component` FROM `Component` WHERE `Directory_` = 'System64Folder'", &view);
    if (!error && !(error = MsiViewExecute(view, 0))) {
        while (systems < MAX_FILES && MsiViewFetch(view, &record) == ERROR_SUCCESS) {
            if (record_text(record, 1, system[systems], RTL_NUMBER_OF(system[systems])))
                systems++;
            MsiCloseHandle(record);
        }
    }
    if (view)
        MsiCloseHandle(view);
    view = 0;
    if (!error)
        error = MsiDatabaseOpenViewW(database, L"SELECT `File`, `Component_`, `FileName`, `FileSize` FROM `File`", &view);
    if (!error && !(error = MsiViewExecute(view, 0))) {
        while (MsiViewFetch(view, &record) == ERROR_SUCCESS) {
            WCHAR key[80], component[80], name[200];
            if (record_text(record, 1, key, RTL_NUMBER_OF(key)) && record_text(record, 2, component, RTL_NUMBER_OF(component)) &&
                record_text(record, 3, name, RTL_NUMBER_OF(name))) {
                BOOL in_system = FALSE;
                for (ULONG i = 0; i < systems && !in_system; i++)
                    in_system = !wcscmp(system[i], component);
                PCWSTR long_name = wcschr(name, L'|') ? wcschr(name, L'|') + 1 : name;
                if (in_system && *count < max && wcslen(long_name) < RTL_NUMBER_OF(files[0].Name)) {
                    RUNTIME_FILE *file = &files[(*count)++];
                    WideCharToMultiByte(CP_ACP, 0, key, -1, file->Key, sizeof(file->Key), nullptr, nullptr);
                    wcscpy(file->Name, long_name);
                    file->Size = (ULONG)MsiRecordGetInteger(record, 4);
                    file->Written = FALSE;
                }
            }
            MsiCloseHandle(record);
        }
    }
    if (view)
        MsiCloseHandle(view);
    MsiCloseHandle(database);
    return error;
}

static void *write_begin(void *context, const MIME_CAB_FILE *file)
{
    WRITE *w = context;
    if (w->Error)
        return nullptr;
    for (ULONG i = 0; i < w->Count; i++) {
        RUNTIME_FILE *f = &w->Files[i];
        if (f->Written || strcmp(f->Key, file->Name))
            continue;
        if (f->Size != file->Size) {
            w->Error = ERROR_INVALID_DATA;
            return nullptr;
        }
        WCHAR directory[MAX_PATH];
        folder(w->Run, directory, RTL_NUMBER_OF(directory));
        _snwprintf(w->Target, RTL_NUMBER_OF(w->Target), L"%ls\\%ls", directory, f->Name);
        _snwprintf(w->Temporary, RTL_NUMBER_OF(w->Temporary), L"%ls\\%ls.new", directory, f->Name);
        w->Output = CreateFileW(w->Temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (w->Output == INVALID_HANDLE_VALUE) {
            w->Error = GetLastError();
            return nullptr;
        }
        return f;
    }
    return nullptr;
}

static int write_data(void *context, void *file_context, const uint8_t *data, size_t length)
{
    WRITE *w = context;
    DWORD written;
    if (!WriteFile(w->Output, data, (DWORD)length, &written, nullptr) || written != length) {
        w->Error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        return 1;
    }
    return 0;
}

static int write_end(void *context, void *file_context, const MIME_CAB_FILE *file)
{
    WRITE *w = context;
    RUNTIME_FILE *f = file_context;
    CloseHandle(w->Output);
    w->Output = INVALID_HANDLE_VALUE;
    if (!w->Error && !MoveFileExW(w->Temporary, w->Target, MOVEFILE_REPLACE_EXISTING))
        w->Error = GetLastError();
    if (w->Error) {
        DeleteFileW(w->Temporary);
        return 1;
    }
    f->Written = TRUE;
    return 0;
}

static DWORD write_files(RUN *run, const MEMBER *cabinet, RUNTIME_FILE *files, ULONG count)
{
    WRITE w = {run, files, count, INVALID_HANDLE_VALUE};
    MIME_CAB *cab;
    int status = MimeCabOpen(&cab, cabinet->Data, cabinet->Size, cab_allocate, cab_release);
    if (status == MIME_CAB_OK) {
        /* one output at a time, as files of a folder may share their data. */
        MimeCabOneAtATime(cab);
        static const MIME_CAB_FILE_SINK sink = {write_begin, write_data, write_end};
        status = MimeCabExtract(cab, &sink, &w);
        MimeCabClose(cab);
    }
    if (w.Error)
        return w.Error;
    if (status)
        return ERROR_INVALID_DATA;
    for (ULONG i = 0; i < count; i++)
        if (!files[i].Written)
            return ERROR_FILE_NOT_FOUND;
    return ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ the feature */

uint32_t VcRuntimeInstall(RUN *run, PCWSTR package)
{
    BYTE *data = nullptr;
    SIZE_T size = 0;
    DWORD error = ReadPackage(package, &data, &size);
    if (error)
        return error;
    SIZE_T ux, ux_size, attached, attached_size;
    if (!burn_containers(data, size, &ux, &ux_size, &attached, &attached_size)) {
        HeapFree(GetProcessHeap(), 0, data);
        return ERROR_INVALID_DATA;
    }
    RunReport(run, 0, 0, L"Reading the redistributable");
    MEMBER manifest_member = {"0"};
    GRAB manifest = {&manifest_member, 1};
    error = grab(data + ux, ux_size, &manifest);
    char packages[MAX_PACKAGES][32], cabinets[MAX_PACKAGES][32];
    ULONG count = error ? 0 : x64_payloads(&manifest_member, packages, cabinets, MAX_PACKAGES);
    grab_free(&manifest);
    if (!error && !count)
        error = ERROR_FILE_NOT_FOUND;
    MEMBER members[MAX_PACKAGES * 2] = {};
    GRAB payloads = {members, count * 2};
    for (ULONG i = 0; i < count; i++) {
        lstrcpynA(members[i * 2].Name, packages[i], sizeof(members[0].Name));
        lstrcpynA(members[i * 2 + 1].Name, cabinets[i], sizeof(members[0].Name));
    }
    if (!error)
        error = grab(data + attached, attached_size, &payloads);
    HeapFree(GetProcessHeap(), 0, data);
    WCHAR directory[MAX_PATH];
    folder(run, directory, RTL_NUMBER_OF(directory));
    CreateDirectoryW(directory, nullptr);
    RunReport(run, 0, 0, L"Writing the runtime");
    for (ULONG i = 0; i < count && !error; i++) {
        /* the database is read by msi.dll, from a file. */
        WCHAR path[MAX_PATH];
        _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\vcruntime%lu.msi", run->Cache, i);
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
        DWORD written = 0;
        if (file == INVALID_HANDLE_VALUE)
            error = GetLastError();
        else {
            if (!WriteFile(file, members[i * 2].Data, (DWORD)members[i * 2].Size, &written, nullptr) ||
                written != members[i * 2].Size)
                error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
            CloseHandle(file);
        }
        RUNTIME_FILE *files = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_FILES * sizeof(*files));
        ULONG found = 0;
        if (!files)
            error = ERROR_OUTOFMEMORY;
        if (!error)
            error = package_files(path, files, &found, MAX_FILES);
        DeleteFileW(path);
        if (!error && !found)
            error = ERROR_FILE_NOT_FOUND;
        if (!error)
            error = write_files(run, &members[i * 2 + 1], files, found);
        if (files)
            HeapFree(GetProcessHeap(), 0, files);
    }
    grab_free(&payloads);
    return error;
}

BOOL VcRuntimePresent(RUN *run)
{
    WCHAR directory[MAX_PATH], path[MAX_PATH];
    folder(run, directory, RTL_NUMBER_OF(directory));
    static const WCHAR *const needed[] = {L"vcruntime140.dll", L"vcruntime140_1.dll", L"msvcp140.dll"};
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(needed); i++) {
        _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", directory, needed[i]);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            return FALSE;
    }
    return TRUE;
}

uint32_t VcRuntimeRemove(RUN *run)
{
    WCHAR directory[MAX_PATH], path[MAX_PATH];
    folder(run, directory, RTL_NUMBER_OF(directory));
    DWORD error = ERROR_SUCCESS;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_patterns); i++) {
        _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", directory, g_patterns[i]);
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileW(path, &data);
        if (find == INVALID_HANDLE_VALUE)
            continue;
        do {
            _snwprintf(path, RTL_NUMBER_OF(path), L"%ls\\%ls", directory, data.cFileName);
            if (!DeleteFileW(path) && GetLastError() != ERROR_FILE_NOT_FOUND)
                error = GetLastError();
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    return error;
}
