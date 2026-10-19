/*
 * the check CreateProcess makes before a program starts: whether the manifest of the program asks for more than the
 * token it would run with has. a program that requires an administrator, or the highest rights its user has, does not
 * start from a process whose token is the filtered half of an administrator's, or from a standard user's: the call
 * fails with ERROR_ELEVATION_REQUIRED, and whoever can ask for an elevation (ShellExecute) does. windows makes the
 * check after the process exists and ends it; here it is made first, which a program cannot tell apart.
 *
 * the manifest is looked for in the resources of the file without mapping it, and only a manifest that has the words
 * of a level above the invoker's is read by the real parser, so a program without any costs a few small reads.
 */
#include "kernelbase_private.h"

#define TokenElevationType_ 18
#define TokenElevation_ 20
#define ELEVATION_TYPE_FULL 2
#define ELEVATION_TYPE_LIMITED 3

#define RUN_LEVEL_HIGHEST_AVAILABLE 2
#define RUN_LEVEL_REQUIRE_ADMIN 3
#define RT_MANIFEST_ 24
#define CREATEPROCESS_MANIFEST_RESOURCE_ID_ 1
#define MANIFEST_LIMIT (256 * 1024)

NTSYSAPI NTSTATUS NTAPI RtlQueryElevationFlags(PULONG flags);

/* what a token is to the check: 0 whole, 1 the limited half of a pair, 2 a standard user's. */
static int user_type(HANDLE token)
{
    ULONG type = 1, returned;
    if (!NT_SUCCESS(NtQueryInformationToken(token, TokenElevationType_, &type, sizeof(type), &returned)))
        return 0;
    if (type == ELEVATION_TYPE_FULL)
        return 0;
    if (type == ELEVATION_TYPE_LIMITED)
        return 1;
    ULONG elevated = 0;
    NtQueryInformationToken(token, TokenElevation_, &elevated, sizeof(elevated), &returned);
    return elevated ? 0 : 2;
}

/* the file read in windows of a page, one at a time: the headers and the directories of the resources sit together. */
typedef struct FILE_WINDOW {
    HANDLE File;
    ULONGLONG Base;
    ULONG Size;
    BYTE Data[4096];
} FILE_WINDOW;

static BOOL window_fill(FILE_WINDOW *w, ULONGLONG offset)
{
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER at;
    at.QuadPart = (LONGLONG)(offset & ~(ULONGLONG)4095);
    w->Size = 0;
    if (!NT_SUCCESS(NtReadFile(w->File, nullptr, nullptr, nullptr, &iosb, w->Data, sizeof(w->Data), &at, nullptr)))
        return FALSE;
    w->Base = (ULONGLONG)at.QuadPart;
    w->Size = (ULONG)iosb.Information;
    return TRUE;
}

static BOOL read_at(FILE_WINDOW *w, ULONGLONG offset, void *buffer, DWORD size)
{
    BYTE *out = buffer;
    while (size) {
        if (offset < w->Base || offset - w->Base >= w->Size) {
            if (!window_fill(w, offset) || offset - w->Base >= w->Size)
                return FALSE;
        }
        ULONG at = (ULONG)(offset - w->Base), n = w->Size - at < size ? w->Size - at : size;
        memcpy(out, w->Data + at, n);
        out += n;
        offset += n;
        size -= n;
    }
    return TRUE;
}

static USHORT u16(const BYTE *p)
{
    return (USHORT)(p[0] | p[1] << 8);
}

static ULONG u32(const BYTE *p)
{
    return (ULONG)p[0] | (ULONG)p[1] << 8 | (ULONG)p[2] << 16 | (ULONG)p[3] << 24;
}

typedef struct IMAGE_LAYOUT {
    BYTE Sections[96 * 40];
    ULONG SectionCount;
    ULONG ResourceRva;
} IMAGE_LAYOUT;

static BOOL rva_to_offset(const IMAGE_LAYOUT *l, ULONG rva, ULONGLONG *offset)
{
    for (ULONG i = 0; i < l->SectionCount; i++) {
        const BYTE *s = l->Sections + i * 40;
        ULONG size = u32(s + 8) > u32(s + 16) ? u32(s + 8) : u32(s + 16), start = u32(s + 12);
        if (rva >= start && rva - start < size) {
            *offset = (ULONGLONG)u32(s + 20) + (rva - start);
            return TRUE;
        }
    }
    return FALSE;
}

/* the entry of a level of the resource tree: the one with the id, or the first one when the id is zero. */
static BOOL resource_entry(FILE_WINDOW *file, const IMAGE_LAYOUT *l, ULONG directory, ULONG id, ULONG *next, BOOL *branch)
{
    ULONGLONG at;
    BYTE header[16];
    if (!rva_to_offset(l, l->ResourceRva + directory, &at) || !read_at(file, at, header, sizeof(header)))
        return FALSE;
    ULONG named = u16(header + 12), ids = u16(header + 14), count = named + ids;
    if (!count || count > 256)
        return FALSE;
    BYTE entries[256 * 8];
    if (!read_at(file, at + 16, entries, count * 8))
        return FALSE;
    for (ULONG i = named; i < count; i++) {
        const BYTE *e = entries + i * 8;
        if (id && u32(e) != id)
            continue;
        ULONG target = u32(e + 4);
        *branch = (target & 0x80000000u) != 0;
        *next = target & 0x7FFFFFFFu;
        return TRUE;
    }
    return FALSE;
}

/* the manifest resource of the process, in a block of the heap; null when the file has none. */
static BYTE *read_manifest(FILE_WINDOW *file, ULONG *size)
{
    BYTE dos[64];
    if (!read_at(file, 0, dos, sizeof(dos)) || u16(dos) != 0x5A4D)
        return nullptr;
    ULONG nt = u32(dos + 0x3C);
    BYTE headers[24 + 240];
    if (nt > 0x10000 || !read_at(file, nt, headers, sizeof(headers)) || u32(headers) != 0x4550)
        return nullptr;
    IMAGE_LAYOUT *layout = KbAllocZero(sizeof(*layout));
    if (!layout)
        return nullptr;
    BYTE *manifest = nullptr;
    ULONG count = u16(headers + 6), optional = u16(headers + 20);
    USHORT magic = u16(headers + 24);
    ULONG directories = magic == 0x20B ? 112 : 96;
    if (count && count <= 96 && optional >= directories + 8 * 3 && optional <= 240 &&
        read_at(file, (ULONGLONG)nt + 24 + optional, layout->Sections, count * 40)) {
        layout->SectionCount = count;
        layout->ResourceRva = u32(headers + 24 + directories + 16);
        ULONG tree, data;
        BOOL branch;
        /* type manifest, then the name of the process manifest, then its first language. */
        if (layout->ResourceRva && resource_entry(file, layout, 0, RT_MANIFEST_, &tree, &branch) && branch &&
            resource_entry(file, layout, tree, CREATEPROCESS_MANIFEST_RESOURCE_ID_, &tree, &branch) && branch &&
            resource_entry(file, layout, tree, 0, &data, &branch) && !branch) {
            ULONGLONG at;
            BYTE entry[16];
            if (rva_to_offset(layout, layout->ResourceRva + data, &at) && read_at(file, at, entry, sizeof(entry))) {
                ULONG rva = u32(entry), length = u32(entry + 4);
                if (length && length <= MANIFEST_LIMIT && rva_to_offset(layout, rva, &at)) {
                    manifest = KbAlloc(length);
                    if (manifest && read_at(file, at, manifest, length)) {
                        *size = length;
                    } else {
                        KbFree(manifest);
                        manifest = nullptr;
                    }
                }
            }
        }
    }
    KbFree(layout);
    return manifest;
}

/* whether the text, ascii or utf-16, holds the word. */
static BOOL has_word(const BYTE *text, ULONG size, const char *word)
{
    SIZE_T n = 0;
    while (word[n])
        n++;
    for (ULONG i = 0; i + n <= size; i++) {
        SIZE_T k = 0;
        while (k < n && text[i + k] == (BYTE)word[k])
            k++;
        if (k == n)
            return TRUE;
    }
    for (ULONG i = 0; i + 2 * n <= size; i++) {
        SIZE_T k = 0;
        while (k < n && text[i + 2 * k] == (BYTE)word[k] && !text[i + 2 * k + 1])
            k++;
        if (k == n)
            return TRUE;
    }
    return FALSE;
}

/* the level the real parser finds in the manifest of the file; the activation context does what the loader does. */
static ULONG parsed_level(PCWSTR path)
{
    ACTCTXW context = {sizeof(context)};
    context.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
    context.lpSource = path;
    context.lpResourceName = MAKEINTRESOURCEW(CREATEPROCESS_MANIFEST_RESOURCE_ID_);
    HANDLE handle = CreateActCtxW(&context);
    if (handle == INVALID_HANDLE_VALUE)
        return 0;
    ULONG info[3] = {};
    SIZE_T written = 0;
    ULONG level = 0;
    if (QueryActCtxW(QUERY_ACTCTX_FLAG_NO_ADDREF, handle, nullptr, 5 /* RunlevelInformationInActivationContext */, info,
                     sizeof(info), &written) && written >= sizeof(info))
        level = info[1];
    ReleaseActCtx(handle);
    return level;
}

/*
 * 0 when the program may start with the token, ERROR_ELEVATION_REQUIRED when its manifest asks for more. the token is
 * the one given to CreateProcessAsUser, or the process' own.
 */
DWORD KbCheckElevation(HANDLE token, PCWSTR path)
{
    static volatile LONG process_type = -1;
    ULONG flags = 0;
    if (!NT_SUCCESS(RtlQueryElevationFlags(&flags)) || !(flags & 1))
        return ERROR_SUCCESS;
    int type;
    if (token) {
        type = user_type(token);
    } else {
        type = process_type;
        if (type < 0) {
            type = user_type((HANDLE)(LONG_PTR)-4);
            process_type = type;
        }
    }
    /* a whole token needs no elevation, whatever the manifest says. */
    if (!type)
        return ERROR_SUCCESS;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return ERROR_SUCCESS;
    ULONG size = 0;
    FILE_WINDOW *window = KbAlloc(sizeof(*window));
    BYTE *manifest = nullptr;
    if (window) {
        window->File = file;
        window->Size = 0;
        manifest = read_manifest(window, &size);
        KbFree(window);
    }
    CloseHandle(file);
    ULONG level = 0;
    if (manifest) {
        /* the common manifest names the invoker, and the words that matter are not in it. */
        if (has_word(manifest, size, "requireAdministrator") || has_word(manifest, size, "highestAvailable"))
            level = parsed_level(path);
        KbFree(manifest);
    }
    BOOL needs = (type == 1 && (level == RUN_LEVEL_HIGHEST_AVAILABLE || level == RUN_LEVEL_REQUIRE_ADMIN)) ||
                 (type == 2 && level == RUN_LEVEL_REQUIRE_ADMIN);
    return needs ? ERROR_ELEVATION_REQUIRED : ERROR_SUCCESS;
}
