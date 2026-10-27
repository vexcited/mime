/* NetfxInstallFromPackage: the windows side of the servicing library, files and registry of the machine. */
#include "cbs.h"
#include <mime/feature.h>
#include "../crypto/crypto.h"
#include <mime/win32/winnls.h>
#include <rtl.h>

#ifndef ERROR_FILE_CORRUPT
#define ERROR_FILE_CORRUPT 1392L
#endif

/* ------------------------------------------------------------------ text */

static bool to_wide(const char *text, WCHAR *out, int capacity)
{
    return MultiByteToWideChar(CP_UTF8, 0, text, -1, out, capacity) > 0;
}

static char *to_utf8(PCWSTR text)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    char *out = n > 0 ? CbsAlloc((size_t)n) : nullptr;
    if (out && WideCharToMultiByte(CP_UTF8, 0, text, -1, out, n, nullptr, nullptr) <= 0) {
        CbsFree(out);
        return nullptr;
    }
    return out;
}

/* ------------------------------------------------------------------ the target */

typedef struct WIN_TARGET {
    DWORD Error;
    HKEY Key;
    int KeyView;
    char KeyName[512];
} WIN_TARGET;

typedef struct OUT_FILE {
    HANDLE Handle;
    WCHAR Path[1024];
} OUT_FILE;

static int fail(WIN_TARGET *w)
{
    w->Error = GetLastError();
    if (!w->Error)
        w->Error = ERROR_GEN_FAILURE;
    return 1;
}

static int create_directory_wide(WIN_TARGET *w, WCHAR *path)
{
    if (CreateDirectoryW(path, nullptr))
        return 0;
    DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS)
        return 0;
    if (error == ERROR_PATH_NOT_FOUND) {
        WCHAR *slash = nullptr;
        for (WCHAR *p = path; *p; p++)
            if (*p == L'\\')
                slash = p;
        if (!slash || slash < path + 3)
            return fail(w);
        *slash = 0;
        int r = create_directory_wide(w, path);
        *slash = L'\\';
        if (r)
            return r;
        if (CreateDirectoryW(path, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS)
            return 0;
    }
    return fail(w);
}

static int t_create_directory(void *context, const char *path)
{
    WIN_TARGET *w = context;
    WCHAR wide[1024];
    if (!to_wide(path, wide, 1024))
        return fail(w);
    return create_directory_wide(w, wide);
}

static int t_file_state(void *context, const char *path, uint64_t size, const uint8_t *digest_expected, int *state)
{
    WIN_TARGET *w = context;
    WCHAR wide[1024];
    if (!to_wide(path, wide, 1024))
        return fail(w);
    HANDLE h = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            *state = 0;
            return 0;
        }
        return fail(w);
    }
    LARGE_INTEGER have;
    if (!GetFileSizeEx(h, &have)) {
        CloseHandle(h);
        return fail(w);
    }
    if ((uint64_t)have.QuadPart != size) {
        CloseHandle(h);
        *state = 2;
        return 0;
    }
    *state = 1;
    if (digest_expected) {
        uint8_t *buffer = CbsAlloc(1u << 18);
        if (!buffer) {
            CloseHandle(h);
            w->Error = ERROR_NOT_ENOUGH_MEMORY;
            return 1;
        }
        MimeHashCtx hash;
        mime_sha256.init(&hash);
        for (;;) {
            DWORD got;
            if (!ReadFile(h, buffer, 1u << 18, &got, nullptr)) {
                CbsFree(buffer);
                CloseHandle(h);
                return fail(w);
            }
            if (!got)
                break;
            mime_sha256.update(&hash, buffer, got);
        }
        uint8_t digest[32];
        mime_sha256.final(&hash, digest);
        if (memcmp(digest, digest_expected, 32))
            *state = 2;
        CbsFree(buffer);
    }
    CloseHandle(h);
    return 0;
}

static int t_begin(void *context, const char *path, uint64_t size, void **file)
{
    WIN_TARGET *w = context;
    (void)size;
    OUT_FILE *f = CbsAlloc(sizeof(*f));
    if (!f) {
        w->Error = ERROR_NOT_ENOUGH_MEMORY;
        return 1;
    }
    if (!to_wide(path, f->Path, 1024)) {
        CbsFree(f);
        return fail(w);
    }
    /* never through a link that other names share: the old name goes first. */
    DWORD attributes = GetFileAttributesW(f->Path);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (attributes & FILE_ATTRIBUTE_READONLY)
            SetFileAttributesW(f->Path, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
        DeleteFileW(f->Path);
    }
    f->Handle = CreateFileW(f->Path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f->Handle == INVALID_HANDLE_VALUE) {
        CbsFree(f);
        return fail(w);
    }
    *file = f;
    return 0;
}

static int t_write(void *context, void *file, const uint8_t *data, size_t length)
{
    WIN_TARGET *w = context;
    OUT_FILE *f = file;
    while (length) {
        DWORD chunk = length > (1u << 24) ? (1u << 24) : (DWORD)length, written;
        if (!WriteFile(f->Handle, data, chunk, &written, nullptr) || !written)
            return fail(w);
        data += written;
        length -= written;
    }
    return 0;
}

static int t_end(void *context, void *file)
{
    WIN_TARGET *w = context;
    OUT_FILE *f = file;
    BOOL ok = CloseHandle(f->Handle);
    CbsFree(f);
    return ok ? 0 : fail(w);
}

static void t_abort(void *context, void *file)
{
    (void)context;
    OUT_FILE *f = file;
    CloseHandle(f->Handle);
    DeleteFileW(f->Path);
    CbsFree(f);
}

static int t_link(void *context, const char *existing, const char *path)
{
    WIN_TARGET *w = context;
    WCHAR from[1024], to[1024];
    if (!to_wide(existing, from, 1024) || !to_wide(path, to, 1024))
        return fail(w);
    DWORD attributes = GetFileAttributesW(to);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (attributes & FILE_ATTRIBUTE_READONLY)
            SetFileAttributesW(to, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
        DeleteFileW(to);
    }
    if (CreateHardLinkW(to, from, nullptr))
        return 0;
    if (CopyFileW(from, to, FALSE))
        return 0;
    return fail(w);
}

/* ------------------------------------------------------------------ registry */

static void close_key(WIN_TARGET *w)
{
    if (w->Key)
        RegCloseKey(w->Key);
    w->Key = nullptr;
    w->KeyName[0] = 0;
}

static HKEY open_key(WIN_TARGET *w, int view, const char *key, bool create)
{
    if (w->Key && w->KeyView == view && !_stricmp(w->KeyName, key))
        return w->Key;
    close_key(w);
    WCHAR wide[512];
    if (strlen(key) >= sizeof(w->KeyName) || !to_wide(key, wide, 512)) {
        SetLastError(ERROR_BAD_PATHNAME);
        return nullptr;
    }
    REGSAM access = KEY_READ | KEY_WRITE | (view == 32 ? KEY_WOW64_32KEY : KEY_WOW64_64KEY);
    HKEY h;
    LSTATUS status = create ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, wide, 0, nullptr, 0, access, nullptr, &h, nullptr)
                            : RegOpenKeyExW(HKEY_LOCAL_MACHINE, wide, 0, access, &h);
    if (status != ERROR_SUCCESS) {
        SetLastError((DWORD)status);
        return nullptr;
    }
    w->Key = h;
    w->KeyView = view;
    strcpy(w->KeyName, key);
    return h;
}

/* a key that is there and may not be changed is not a failure of the installation. */
static int registry_fail(WIN_TARGET *w)
{
    DWORD error = GetLastError();
    if (error == ERROR_ACCESS_DENIED)
        return CBS_DENIED;
    SetLastError(error);
    return fail(w);
}

static int t_create_key(void *context, int view, const char *key)
{
    WIN_TARGET *w = context;
    return open_key(w, view, key, true) ? 0 : registry_fail(w);
}

static int t_set_value(void *context, int view, const char *key, const char *name, uint32_t type, const void *data,
                       size_t size)
{
    WIN_TARGET *w = context;
    HKEY h = open_key(w, view, key, true);
    if (!h)
        return registry_fail(w);
    WCHAR wide_name[256];
    if (name && !to_wide(name, wide_name, 256)) {
        SetLastError(ERROR_BAD_PATHNAME);
        return fail(w);
    }
    LSTATUS status;
    if (type == CBS_REG_SZ || type == CBS_REG_EXPAND_SZ || type == CBS_REG_MULTI_SZ) {
        int count = MultiByteToWideChar(CP_UTF8, 0, data, (int)size, nullptr, 0);
        WCHAR *text = CbsAlloc(((size_t)count + 2) * sizeof(WCHAR));
        if (!text) {
            w->Error = ERROR_NOT_ENOUGH_MEMORY;
            return 1;
        }
        if (count && !MultiByteToWideChar(CP_UTF8, 0, data, (int)size, text, count)) {
            CbsFree(text);
            return fail(w);
        }
        status = RegSetValueExW(h, name ? wide_name : L"", 0, type, (const BYTE *)text,
                                (DWORD)((size_t)count * sizeof(WCHAR)));
        CbsFree(text);
    } else
        status = RegSetValueExW(h, name ? wide_name : L"", 0, type, data, (DWORD)size);
    if (status != ERROR_SUCCESS) {
        SetLastError((DWORD)status);
        return registry_fail(w);
    }
    return 0;
}

static int t_get_dword(void *context, int view, const char *key, const char *name, uint32_t *value)
{
    WIN_TARGET *w = context;
    HKEY h = open_key(w, view, key, false);
    if (!h)
        return 1;
    WCHAR wide_name[256];
    if (!to_wide(name, wide_name, 256))
        return 1;
    DWORD type, size = sizeof(*value);
    return RegQueryValueExW(h, wide_name, nullptr, &type, (BYTE *)value, &size) == ERROR_SUCCESS && type == REG_DWORD &&
                   size == sizeof(*value)
               ? 0
               : 1;
}

static void t_log(void *context, const char *message)
{
    (void)context;
    OutputDebugStringA("cbs: ");
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

/* ------------------------------------------------------------------ the package */

typedef struct VIEW {
    HANDLE File, Mapping;
    const uint8_t *Data;
    size_t Size;
} VIEW;

static DWORD map_file(PCWSTR path, VIEW *v)
{
    memset(v, 0, sizeof(*v));
    v->File = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (v->File == INVALID_HANDLE_VALUE) {
        v->File = nullptr;
        return GetLastError();
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(v->File, &size) || size.QuadPart <= 0 || (uint64_t)size.QuadPart > (size_t)-1 / 2) {
        DWORD error = GetLastError();
        CloseHandle(v->File);
        v->File = nullptr;
        return error ? error : ERROR_BAD_FORMAT;
    }
    v->Mapping = CreateFileMappingW(v->File, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!v->Mapping) {
        DWORD error = GetLastError();
        CloseHandle(v->File);
        v->File = nullptr;
        return error;
    }
    v->Data = MapViewOfFile(v->Mapping, FILE_MAP_READ, 0, 0, 0);
    if (!v->Data) {
        DWORD error = GetLastError();
        CloseHandle(v->Mapping);
        CloseHandle(v->File);
        v->File = v->Mapping = nullptr;
        return error;
    }
    v->Size = (size_t)size.QuadPart;
    return ERROR_SUCCESS;
}

static void unmap_file(VIEW *v)
{
    if (v->Data)
        UnmapViewOfFile(v->Data);
    if (v->Mapping)
        CloseHandle(v->Mapping);
    if (v->File)
        CloseHandle(v->File);
    memset(v, 0, sizeof(*v));
}

static DWORD error_of(int status, const WIN_TARGET *w)
{
    switch (status) {
    case CBS_OK:
        return ERROR_SUCCESS;
    case CBS_BAD:
        return ERROR_BAD_FORMAT;
    case CBS_UNSUPPORTED:
        return ERROR_NOT_SUPPORTED;
    case CBS_CORRUPT:
        return ERROR_FILE_CORRUPT;
    case CBS_MEMORY:
        return ERROR_NOT_ENOUGH_MEMORY;
    case CBS_STOPPED:
        return ERROR_CANCELLED;
    case CBS_NOT_FOUND:
        return ERROR_FILE_NOT_FOUND;
    default:
        return w->Error ? w->Error : ERROR_GEN_FAILURE;
    }
}

/* the 7z archive of the installer: after the image, where the signature and a valid start header are. */
static const uint8_t *find_archive(const uint8_t *data, size_t size, size_t *archive_size)
{
    static const uint8_t signature[6] = {'7', 'z', 0xBC, 0xAF, 0x27, 0x1C};
    size_t from = 0;
    if (size > 0x40 && data[0] == 'M' && data[1] == 'Z') {
        uint32_t pe = (uint32_t)data[0x3C] | (uint32_t)data[0x3D] << 8 | (uint32_t)data[0x3E] << 16 | (uint32_t)data[0x3F] << 24;
        if (pe < size - 0x100 && !memcmp(data + pe, "PE\0\0", 4)) {
            unsigned sections = data[pe + 6] | data[pe + 7] << 8;
            unsigned optional = data[pe + 20] | data[pe + 21] << 8;
            size_t table = pe + 24 + optional;
            for (unsigned i = 0; i < sections && table + (i + 1) * 40 <= size; i++) {
                const uint8_t *s = data + table + i * 40;
                size_t raw = (size_t)s[16] | (size_t)s[17] << 8 | (size_t)s[18] << 16 | (size_t)s[19] << 24;
                size_t at = (size_t)s[20] | (size_t)s[21] << 8 | (size_t)s[22] << 16 | (size_t)s[23] << 24;
                if (at + raw > from && at + raw <= size)
                    from = at + raw;
            }
        }
    }
    size_t limit = from + (16u << 20);
    if (limit > size)
        limit = size;
    for (size_t at = from; at + 32 <= limit; at++) {
        if (data[at] != '7' || memcmp(data + at, signature, 6))
            continue;
        uint32_t crc = (uint32_t)data[at + 8] | (uint32_t)data[at + 9] << 8 | (uint32_t)data[at + 10] << 16 | (uint32_t)data[at + 11] << 24;
        if (CbsCrc32(0, data + at + 12, 20) == crc) {
            *archive_size = size - at;
            return data + at;
        }
    }
    return nullptr;
}

/* the cabinet of the archive that is for the processor of the machine. */
static int pick_cabinet(const CBS_7Z *archive, CBS_ARCH native, unsigned *index)
{
    const char *suffix = native == CBS_ARCH_AMD64 ? "-x64.cab" : native == CBS_ARCH_ARM64 ? "-arm64.cab" : "-x86.cab";
    size_t suffix_length = strlen(suffix);
    for (unsigned i = 0; i < Cbs7zCount(archive); i++) {
        const char *name;
        uint64_t size;
        uint32_t crc;
        bool has_crc;
        Cbs7zInfo(archive, i, &name, &size, &crc, &has_crc);
        size_t n = strlen(name);
        if (n > suffix_length && !_stricmp(name + n - suffix_length, suffix))
            return *index = i, CBS_OK;
    }
    return CBS_NOT_FOUND;
}

/* what the handler tells the caller: the bytes of the two phases, extracting then installing, and the step. */
typedef struct REPORTER {
    MIME_FEATURE_PROGRESS Progress;
    void *Context;
    uint64_t Base, Span, Scale; /* this phase covers Span of Scale from Base. */
    uint64_t LastDone, LastTotal;
    WCHAR Step[64];
    bool Cancelled;
} REPORTER;

static void set_step(REPORTER *r, const char *text)
{
    MultiByteToWideChar(CP_UTF8, 0, text, -1, r->Step, 64);
}

/* true when the caller cancelled. done of total is how far the phase is. */
static bool tell(REPORTER *r, uint64_t done, uint64_t total)
{
    r->LastDone = done;
    r->LastTotal = total;
    if (!r->Progress)
        return false;
    uint64_t position = r->Base + (total ? done * r->Span / total : 0);
    if (!r->Progress(r->Context, position, r->Scale, r->Step))
        r->Cancelled = true;
    return r->Cancelled;
}

typedef struct EXTRACT_STATE {
    WIN_TARGET *Target;
    OUT_FILE *File;
    REPORTER *Reporter;
    uint64_t Done, Reported, Total;
} EXTRACT_STATE;

static int extract_sink(void *context, const uint8_t *data, size_t length)
{
    EXTRACT_STATE *e = context;
    if (t_write(e->Target, e->File, data, length))
        return 1;
    e->Done += length;
    if (e->Done - e->Reported >= (1u << 22)) {
        e->Reported = e->Done;
        if (tell(e->Reporter, e->Done, e->Total))
            return 1;
    }
    return 0;
}

static int install_report(void *context, uint64_t done, uint64_t total)
{
    return tell(context, done, total) ? 1 : 0;
}

static void install_step(void *context, const char *step)
{
    REPORTER *r = context;
    set_step(r, step);
    tell(r, r->LastDone, r->LastTotal);
}

static CBS_ARCH native_arch(void)
{
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        return CBS_ARCH_AMD64;
    case PROCESSOR_ARCHITECTURE_ARM64:
        return CBS_ARCH_ARM64;
    default:
        return CBS_ARCH_X86;
    }
}

/* the places of the system below the root of the drive, as the manifests' variables stand for them. */
typedef struct PLACES {
    char Windows[600], System32[600], SysWow64[600], ProgramData[600], ProgramFiles[600];
} PLACES;

static bool make_layout(PCWSTR root_wide, CBS_ARCH native, PLACES *p, CBS_LAYOUT *layout)
{
    char *root = to_utf8(root_wide);
    if (!root)
        return false;
    size_t n = strlen(root);
    while (n > 0 && root[n - 1] == '\\')
        root[--n] = 0;
    bool fits = n > 0 && n + 40 < sizeof(p->Windows);
    if (fits) {
        memcpy(p->Windows, root, n);
        memcpy(p->Windows + n, "\\Windows", 9);
        size_t w = n + 8;
        memcpy(p->System32, p->Windows, w);
        memcpy(p->System32 + w, "\\System32", 10);
        memcpy(p->SysWow64, p->Windows, w);
        memcpy(p->SysWow64 + w, native == CBS_ARCH_X86 ? "\\System32" : "\\SysWOW64", 10);
        memcpy(p->ProgramData, root, n);
        memcpy(p->ProgramData + n, "\\ProgramData", 13);
        memcpy(p->ProgramFiles, root, n);
        if (native == CBS_ARCH_X86)
            memcpy(p->ProgramFiles + n, "\\Program Files", 15);
        else
            memcpy(p->ProgramFiles + n, "\\Program Files (x86)", 21);
        memset(layout, 0, sizeof(*layout));
        layout->Windows = p->Windows;
        layout->System32 = p->System32;
        layout->SysWow64 = p->SysWow64;
        layout->ProgramData = p->ProgramData;
        layout->ProgramFiles = p->ProgramFiles;
        layout->Is64Bit = native != CBS_ARCH_X86;
        layout->Native = native;
    }
    CbsFree(root);
    return fits;
}

/* writes the cabinet of the archive that is for this processor to a file below the Windows directory. */
static int extract_cabinet(WIN_TARGET *w, REPORTER *reporter, const CBS_LAYOUT *layout, const VIEW *package,
                           CBS_ARCH native, WCHAR *cabinet_path)
{
    size_t archive_size;
    const uint8_t *archive_data = find_archive(package->Data, package->Size, &archive_size);
    CBS_7Z *archive = nullptr;
    unsigned index = 0;
    int status = archive_data ? Cbs7zOpen(&archive, archive_data, archive_size) : CBS_BAD;
    if (status == CBS_OK)
        status = pick_cabinet(archive, native, &index);
    if (status != CBS_OK) {
        Cbs7zClose(archive);
        return status;
    }
    const char *name;
    uint64_t size;
    uint32_t crc;
    bool has_crc;
    Cbs7zInfo(archive, index, &name, &size, &crc, &has_crc);
    /* the extraction is the first half of the work and the installation the second. */
    reporter->Base = 0;
    reporter->Span = size;
    reporter->Scale = size * 2;
    set_step(reporter, "Extracting the package");
    WCHAR directory[1024];
    if (!to_wide(layout->Windows, directory, 1000)) {
        Cbs7zClose(archive);
        return CBS_BAD;
    }
    wcscat(directory, L"\\Temp");
    wcscpy(cabinet_path, directory);
    wcscat(cabinet_path, L"\\netfx481.cab");
    char path[1100];
    EXTRACT_STATE e = {};
    e.Target = w;
    e.Reporter = reporter;
    e.Total = size;
    if (create_directory_wide(w, directory) ||
        !WideCharToMultiByte(CP_UTF8, 0, cabinet_path, -1, path, sizeof(path), nullptr, nullptr))
        status = CBS_TARGET_FAILED;
    else if (t_begin(w, path, size, (void **)&e.File))
        status = CBS_TARGET_FAILED;
    else {
        status = Cbs7zExtract(archive, index, extract_sink, &e);
        if (status == CBS_STOPPED && !reporter->Cancelled)
            status = CBS_TARGET_FAILED;
        if (status == CBS_OK && t_end(w, e.File))
            status = CBS_TARGET_FAILED;
        else if (status != CBS_OK)
            t_abort(w, e.File);
    }
    Cbs7zClose(archive);
    reporter->Base = size;
    reporter->LastDone = reporter->LastTotal = 0;
    return status;
}

DWORD NetfxInstallFromPackage(PCWSTR package, PCWSTR root, MIME_FEATURE_PROGRESS progress, void *context)
{
    if (!package || !root || !*root)
        return ERROR_INVALID_PARAMETER;
    WIN_TARGET w = {};
    CBS_ARCH native = native_arch();
    PLACES places;
    CBS_LAYOUT layout;
    if (!make_layout(root, native, &places, &layout))
        return ERROR_BAD_PATHNAME;
    CBS_TARGET target = {};
    target.Context = &w;
    target.CreateDirectory = t_create_directory;
    target.FileState = t_file_state;
    target.Begin = t_begin;
    target.Write = t_write;
    target.End = t_end;
    target.Abort = t_abort;
    target.Link = t_link;
    target.CreateKey = t_create_key;
    target.SetValue = t_set_value;
    target.GetDword = t_get_dword;
    target.Log = t_log;
    REPORTER reporter = {};
    reporter.Progress = progress;
    reporter.Context = context;
    set_step(&reporter, "Reading the package");

    VIEW package_view;
    DWORD error = map_file(package, &package_view);
    if (error != ERROR_SUCCESS)
        return error;
    VIEW cabinet_view = {};
    WCHAR cabinet_path[1100] = {};
    int status = CBS_OK;
    const uint8_t *cabinet = package_view.Data;
    size_t cabinet_size = package_view.Size;
    if (package_view.Size >= 4 && !memcmp(package_view.Data, "MSCF", 4)) {
        reporter.Span = reporter.Scale = cabinet_size;
    } else {
        status = extract_cabinet(&w, &reporter, &layout, &package_view, native, cabinet_path);
        if (status == CBS_OK) {
            error = map_file(cabinet_path, &cabinet_view);
            if (error != ERROR_SUCCESS) {
                w.Error = error;
                status = CBS_TARGET_FAILED;
            } else {
                cabinet = cabinet_view.Data;
                cabinet_size = cabinet_view.Size;
            }
        }
    }
    if (status == CBS_OK) {
        CBS_PROGRESS install = {install_report, install_step, &reporter};
        status = CbsInstallCabinet(cabinet, cabinet_size, &layout, &target, &install);
        if (status == CBS_OK) {
            set_step(&reporter, "Writing the registry");
            status = CbsInstallFrameworkRegistry(&layout, &target);
            if (status == CBS_OK && tell(&reporter, 1, 1))
                status = CBS_STOPPED;
        }
    }
    close_key(&w);
    unmap_file(&cabinet_view);
    if (cabinet_path[0])
        DeleteFileW(cabinet_path);
    unmap_file(&package_view);
    return error_of(status, &w);
}
