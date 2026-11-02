/*
 * activation context generation: what sxs.dll does inside csrss on Windows
 * for CreateActCtx and CreateProcess, done here in the process itself.
 *
 * the root manifest comes from a file or an RT_MANIFEST resource. its
 * dependencies resolve to private assemblies probed in the application
 * directory as sxs.dll does, or to the few system assemblies mime maps
 * onto its own dlls. the result is ACTIVATION_CONTEXT_DATA in the Windows
 * layout, built in pages of its own.
 */
#include "sxs.h"

#define PUBLIC_KEY_TOKEN_MICROSOFT L"6595b64144ccf1df"
#define IS_INTRESOURCE(x) (((ULONG_PTR)(x) >> 16) == 0)

#ifdef __i386__
#define SYSTEM_DIRECTORY L"C:\\Windows\\SysWOW64\\"
#define PROCESS_ARCH L"x86"
#elif defined(__x86_64__) && !defined(MIME_X64_NATIVE)
#define SYSTEM_DIRECTORY L"C:\\Windows\\SysX64\\"
#define PROCESS_ARCH L"amd64"
#elifdef __x86_64__
#define SYSTEM_DIRECTORY L"C:\\Windows\\System32\\"
#define PROCESS_ARCH L"amd64"
#else
#define SYSTEM_DIRECTORY L"C:\\Windows\\System32\\"
#define PROCESS_ARCH L"arm64"
#endif

PCWSTR SxspSystemDirectory(void)
{
    return SYSTEM_DIRECTORY;
}

ULONG SxspHashString(PCWCH s, SIZE_T chars)
{
    ULONG hash = 0;
    for (SIZE_T i = 0; i < chars; i++)
        hash = hash * 65599 + RtlUpcaseUnicodeChar(s[i]);
    return hash;
}

static PVOID gen_alloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, size);
}

static void gen_free(PVOID p)
{
    if (p)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
}

static PWSTR gen_strdup(PCWSTR s)
{
    if (!s)
        return nullptr;
    SIZE_T n = wcslen(s);
    PWSTR copy = gen_alloc((n + 1) * sizeof(WCHAR));
    if (copy)
        memcpy(copy, s, n * sizeof(WCHAR));
    return copy;
}

/* concatenates up to four strings into a new heap string. */
static PWSTR gen_concat(PCWSTR a, PCWSTR b, PCWSTR c, PCWSTR d)
{
    PCWSTR parts[] = {a, b, c, d};
    SIZE_T total = 0;
    for (ULONG i = 0; i < 4; i++)
        total += parts[i] ? wcslen(parts[i]) : 0;
    PWSTR s = gen_alloc((total + 1) * sizeof(WCHAR));
    if (!s)
        return nullptr;
    for (ULONG i = 0; i < 4; i++)
        if (parts[i])
            wcscat(s, parts[i]);
    return s;
}

/* the directory part of a path, with its trailing backslash. */
static PWSTR directory_of(PCWSTR path)
{
    PCWSTR slash = wcsrchr(path, '\\');
    PCWSTR alt = wcsrchr(path, '/');
    if (alt > slash)
        slash = alt;
    SIZE_T n = slash ? (SIZE_T)(slash - path) + 1 : 0;
    PWSTR dir = gen_alloc((n + 1) * sizeof(WCHAR));
    if (dir)
        memcpy(dir, path, n * sizeof(WCHAR));
    return dir;
}

/* ------------------------------------------------------------------ reading sources */

typedef struct MAPPED_FILE {
    PVOID View;
    SIZE_T Size;
} MAPPED_FILE;

static NTSTATUS map_file(PCWSTR path, MAPPED_FILE *out)
{
    out->View = nullptr;
    UNICODE_STRING nt;
    if (!RtlDosPathNameToNtPathName_U(path, &nt, nullptr, nullptr))
        return STATUS_OBJECT_PATH_SYNTAX_BAD;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    IO_STATUS_BLOCK iosb;
    HANDLE file;
    NTSTATUS status = NtOpenFile(&file, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    RtlFreeUnicodeString(&nt);
    if (!NT_SUCCESS(status))
        return status;
    FILE_STANDARD_INFORMATION info;
    status = NtQueryInformationFile(file, &iosb, &info, sizeof(info), FileStandardInformation);
    if (NT_SUCCESS(status) && !info.EndOfFile.QuadPart)
        status = STATUS_SXS_CANT_GEN_ACTCTX;
    HANDLE section = nullptr;
    if (NT_SUCCESS(status))
        status = NtCreateSection(&section, SECTION_MAP_READ | SECTION_QUERY, nullptr, nullptr, PAGE_READONLY,
                                 SEC_COMMIT, file);
    NtClose(file);
    if (!NT_SUCCESS(status))
        return status;
    SIZE_T view_size = 0;
    status = NtMapViewOfSection(section, NtCurrentProcess(), &out->View, 0, 0, nullptr, &view_size, ViewShare, 0,
                                PAGE_READONLY);
    NtClose(section);
    out->Size = (SIZE_T)info.EndOfFile.QuadPart;
    return status;
}

static void unmap_file(MAPPED_FILE *f)
{
    if (f->View)
        NtUnmapViewOfSection(NtCurrentProcess(), f->View);
    f->View = nullptr;
}

/* an RT_MANIFEST resource of a mapped image or data file, in any language. */
static NTSTATUS find_manifest_resource(PVOID module, ULONG_PTR name, const void **data, SIZE_T *size)
{
    LDR_RESOURCE_INFO info = {24, name, 0};
    IMAGE_RESOURCE_DIRECTORY *dir;
    NTSTATUS status = LdrFindResourceDirectory_U(module, &info, 2, &dir);
    if (!NT_SUCCESS(status))
        return status;
    if (!dir->NumberOfNamedEntries && !dir->NumberOfIdEntries)
        return STATUS_RESOURCE_LANG_NOT_FOUND;
    /* the first language, as the loader takes it when it asks for none. */
    IMAGE_RESOURCE_DIRECTORY_ENTRY *entry = (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(dir + 1);
    info.Language = entry->Name & 0xFFFF;
    IMAGE_RESOURCE_DATA_ENTRY *data_entry;
    status = LdrFindResource_U(module, &info, 3, &data_entry);
    if (!NT_SUCCESS(status))
        return status;
    ULONG length;
    status = LdrAccessResource(module, data_entry, (PVOID *)data, &length);
    *size = length;
    return status;
}

static BOOLEAN resource_missing(NTSTATUS status)
{
    return status == STATUS_RESOURCE_DATA_NOT_FOUND || status == STATUS_RESOURCE_TYPE_NOT_FOUND ||
        status == STATUS_RESOURCE_NAME_NOT_FOUND || status == STATUS_RESOURCE_LANG_NOT_FOUND;
}

static BOOLEAN file_missing(NTSTATUS status)
{
    return status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_PATH_NOT_FOUND ||
        status == STATUS_NO_SUCH_FILE || status == STATUS_OBJECT_PATH_SYNTAX_BAD;
}

static NTSTATUS load_manifest_file(PCWSTR path, SXS_MANIFEST *manifest)
{
    MAPPED_FILE f;
    NTSTATUS status = map_file(path, &f);
    if (NT_SUCCESS(status))
        status = SxspParseManifest(f.View, f.Size, manifest);
    unmap_file(&f);
    return status;
}

/* parses the manifest resource of an image file that is not loaded. */
static NTSTATUS load_manifest_image(PCWSTR path, ULONG_PTR name, SXS_MANIFEST *manifest)
{
    MAPPED_FILE f;
    NTSTATUS status = map_file(path, &f);
    if (NT_SUCCESS(status)) {
        const void *data;
        SIZE_T size;
        if (!LdrpNtHeaders(f.View))
            status = STATUS_INVALID_IMAGE_FORMAT;
        else
            status = find_manifest_resource((PVOID)((ULONG_PTR)f.View | 1), name, &data, &size);
        if (NT_SUCCESS(status))
            status = SxspParseManifest(data, size, manifest);
    }
    unmap_file(&f);
    return status;
}

/* whether Windows prefers <image>.manifest files to embedded manifests on this machine. */
static BOOLEAN prefer_external_manifest(void)
{
    static LONG cached = -1;
    if (cached != -1)
        return cached != 0;
    LONG value = 0;
    UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software\\Microsoft\\Windows\\CurrentVersion\\SideBySide");
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    HANDLE key;
    if (NT_SUCCESS(NtOpenKey(&key, 1 /* KEY_QUERY_VALUE */, &oa))) {
        UNICODE_STRING value_name = RTL_CONSTANT_STRING(L"PreferExternalManifest");
        struct {
            ULONG TitleIndex, Type, DataLength;
            LONG Data[2];
        } info;
        ULONG returned;
        if (NT_SUCCESS(NtQueryValueKey(key, &value_name, 2 /* KeyValuePartialInformation */, &info, sizeof(info),
                                       &returned))) {
            /* sxs reads anything but a REG_DWORD other than -1 as set. */
            value = 1;
            if (info.Type == 4 /* REG_DWORD */ && info.DataLength == 4 && info.Data[0] != -1)
                value = info.Data[0];
        }
        NtClose(key);
    }
    cached = value;
    return value != 0;
}

static BOOLEAN has_image_extension(PCWSTR path)
{
    PCWSTR dot = wcsrchr(path, '.');
    return dot && !wcschr(dot, '\\') && (!_wcsicmp(dot, L".dll") || !_wcsicmp(dot, L".exe"));
}

/* ------------------------------------------------------------------ the assembly graph */

typedef struct GEN_ASSEMBLY {
    SXS_MANIFEST Manifest;
    PWSTR ManifestPath;
    ULONG ManifestPathType;
    PWSTR Directory; /* the storage root of a private assembly, with a trailing backslash */
    PWSTR Language;
    BOOLEAN Private;
    BOOLEAN System; /* one of the Windows assemblies mime maps onto its own dlls */
    PCWSTR SystemFile;
} GEN_ASSEMBLY;

/* sxs.dll takes the application directory and nine private paths at most. */
#define GEN_MAX_ROOTS 10

typedef struct GEN {
    GEN_ASSEMBLY *Assemblies;
    ULONG Count, Capacity;
    PCWSTR AppDirectory;
    ULONG ResourceName;
    /* where private assemblies are probed: the application directory, then the private paths of its configuration. */
    PWSTR Roots[GEN_MAX_ROOTS];
    ULONG RootCount;
} GEN;

static GEN_ASSEMBLY *add_assembly(GEN *g)
{
    if (g->Count == g->Capacity) {
        ULONG capacity = g->Capacity ? g->Capacity * 2 : 8;
        GEN_ASSEMBLY *grown = gen_alloc(capacity * sizeof(*grown));
        if (!grown)
            return nullptr;
        if (g->Count)
            memcpy(grown, g->Assemblies, g->Count * sizeof(*grown));
        gen_free(g->Assemblies);
        g->Assemblies = grown;
        g->Capacity = capacity;
    }
    GEN_ASSEMBLY *a = &g->Assemblies[g->Count++];
    memset(a, 0, sizeof(*a));
    return a;
}

static void free_gen(GEN *g)
{
    for (ULONG i = 0; i < g->Count; i++) {
        GEN_ASSEMBLY *a = &g->Assemblies[i];
        SxspFreeManifest(&a->Manifest);
        gen_free(a->ManifestPath);
        gen_free(a->Directory);
        gen_free(a->Language);
    }
    gen_free(g->Assemblies);
    /* the first root is the application directory, owned by the caller. */
    for (ULONG i = 1; i < g->RootCount; i++)
        gen_free(g->Roots[i]);
}

/* the Windows assemblies mime provides itself, each a single dll in the system directory. */
static const struct {
    PCWSTR Name;
    PCWSTR File;
} g_system_assemblies[] = {
    {L"Microsoft.Windows.Common-Controls", L"comctl32.dll"},
    {L"Microsoft.Windows.GdiPlus", L"gdiplus.dll"},
};

static BOOLEAN same_identity_name(PCWSTR a, PCWSTR b)
{
    return a && b && !_wcsicmp(a, b);
}

static NTSTATUS add_system_assembly(GEN *g, const SXS_IDENTITY *ref, PCWSTR file)
{
    GEN_ASSEMBLY *a = add_assembly(g);
    if (!a)
        return STATUS_NO_MEMORY;
    a->System = TRUE;
    a->SystemFile = file;
    a->ManifestPathType = ACTCTX_PATH_TYPE_NONE;
    /* a manifest of its own, so the identity strings live with the assembly. */
    WCHAR text[512];
    _snwprintf(text, RTL_NUMBER_OF(text),
               L"<assembly xmlns='urn:schemas-microsoft-com:asm.v1' manifestVersion='1.0'><assemblyIdentity "
               L"type='win32' name='%s' version='%u.%u.%u.%u' processorArchitecture='%s' publicKeyToken='%s'/>"
               L"<file name='%s'/></assembly>",
               ref->Name, ref->Version[0], ref->Version[1], ref->Version[2], ref->Version[3], PROCESS_ARCH,
               ref->Token ? ref->Token : PUBLIC_KEY_TOKEN_MICROSOFT, file);
    text[RTL_NUMBER_OF(text) - 1] = 0;
    UCHAR bom_text[sizeof(text) + 2] = {0xFF, 0xFE};
    SIZE_T bytes = wcslen(text) * sizeof(WCHAR);
    memcpy(bom_text + 2, text, bytes);
    return SxspParseManifest(bom_text, bytes + 2, &a->Manifest);
}

/* the cultures a reference is probed in: its own, or the ui languages for "*", then neutral. */
static ULONG reference_cultures(const SXS_IDENTITY *ref, PCWSTR cultures[3])
{
    ULONG n = 0;
    PCWSTR lang = ref->Language;
    if (lang && *lang && _wcsicmp(lang, L"neutral") && _wcsicmp(lang, L"none")) {
        if (!wcscmp(lang, L"*")) {
            /* stands for the user's ui language and its parent; mime only knows en-US. */
            cultures[n++] = L"en-US\\";
            cultures[n++] = L"en\\";
        } else {
            cultures[n++] = lang;
        }
    }
    cultures[n++] = L"";
    return n;
}

static BOOLEAN same_text(PCWSTR a, PCWSTR b)
{
    return a && b ? !_wcsicmp(a, b) : a == b;
}

static PCWSTR language_or_none(PCWSTR language)
{
    return language && *language && _wcsicmp(language, L"neutral") ? language : nullptr;
}

/*
 * sxs.dll compares the identity of a manifest with the reference it was probed for
 * attribute by attribute: the same set, the values without regard to case. a neutral
 * language is no attribute, and a version is required.
 */
static BOOLEAN identity_matches(const SXS_IDENTITY *id, const SXS_IDENTITY *want)
{
    return same_text(id->Name, want->Name) && same_text(id->Type, want->Type) && same_text(id->Arch, want->Arch) &&
        same_text(id->Token, want->Token) &&
        same_text(language_or_none(id->Language), language_or_none(want->Language)) && id->HasVersion &&
        want->HasVersion && !memcmp(id->Version, want->Version, sizeof(want->Version));
}

/* the reference as probed in a culture: a wildcard architecture is the process one, a wildcard language the culture. */
static SXS_IDENTITY probed_identity(const SXS_IDENTITY *ref, PCWSTR language)
{
    SXS_IDENTITY want = *ref;
    if (want.Arch && !wcscmp(want.Arch, L"*"))
        want.Arch = (PWSTR)PROCESS_ARCH;
    if (want.Language && !wcscmp(want.Language, L"*"))
        want.Language = (PWSTR)language;
    return want;
}

/*
 * probes a private assembly in the application directory and the private paths of its configuration, as sxs.dll does:
 * <name>.dll with its manifest as resource 1, <name>.manifest, then the
 * same inside a <name> directory, culture by culture. the first candidate that
 * exists is the assembly: its manifest must parse and, when it has an identity,
 * describe the reference. a manifest without an identity takes the reference's.
 */
static NTSTATUS probe_private(GEN *g, const SXS_IDENTITY *ref, GEN_ASSEMBLY **found)
{
    PCWSTR cultures[3];
    ULONG culture_count = reference_cultures(ref, cultures);
    *found = nullptr;
    for (ULONG c = 0; c < culture_count; c++) {
        PCWSTR culture = cultures[c];
        WCHAR lang_dir[96] = {};
        if (*culture) {
            wcscpy(lang_dir, culture);
            if (lang_dir[wcslen(lang_dir) - 1] != '\\')
                wcscat(lang_dir, L"\\");
        }
        PWSTR sub = gen_concat(ref->Name, L"\\", nullptr, nullptr);
        if (!sub)
            return STATUS_NO_MEMORY;
        PCWSTR subdirs[] = {L"", sub};
        /* the roots in order, and in each of them the file, then the directory of the assembly. */
        for (ULONG root_sub = 0; root_sub < g->RootCount * 2 && !*found; root_sub++) {
            ULONG s = root_sub % 2;
            for (ULONG kind = 0; kind < 2 && !*found; kind++) {
                PWSTR dir = gen_concat(g->Roots[root_sub / 2], lang_dir, subdirs[s], nullptr);
                PWSTR path = dir ? gen_concat(dir, ref->Name, kind ? L".manifest" : L".dll", nullptr) : nullptr;
                if (!path) {
                    gen_free(dir);
                    gen_free(sub);
                    return STATUS_NO_MEMORY;
                }
                SXS_MANIFEST m;
                NTSTATUS status = kind ? load_manifest_file(path, &m) : load_manifest_image(path, 1, &m);
                if (NT_SUCCESS(status)) {
                    PWSTR language = nullptr;
                    if (*culture) {
                        language = gen_strdup(culture);
                        SIZE_T n = language ? wcslen(language) : 0;
                        if (n && language[n - 1] == '\\')
                            language[n - 1] = 0;
                    }
                    SXS_IDENTITY want = probed_identity(ref, language);
                    if (m.HasIdentity && !identity_matches(&m.Identity, &want)) {
                        DbgPrint("sxs: %ls does not describe %ls\n", path, ref->Name);
                        SxspFreeManifest(&m);
                        gen_free(language);
                        gen_free(dir);
                        gen_free(path);
                        gen_free(sub);
                        /* a shared assembly would come from winsxs first, which mime does not have. */
                        return ref->Token ? STATUS_SXS_ASSEMBLY_NOT_FOUND : STATUS_SXS_CANT_GEN_ACTCTX;
                    }
                    GEN_ASSEMBLY *a = add_assembly(g);
                    if (!a) {
                        SxspFreeManifest(&m);
                        gen_free(language);
                        gen_free(dir);
                        gen_free(path);
                        gen_free(sub);
                        return STATUS_NO_MEMORY;
                    }
                    if (!m.HasIdentity) {
                        m.HasIdentity = TRUE;
                        m.Identity = want;
                    }
                    a->Manifest = m;
                    a->ManifestPath = path;
                    a->ManifestPathType = ACTCTX_PATH_TYPE_WIN32_FILE;
                    a->Directory = dir;
                    a->Private = TRUE;
                    a->Language = language;
                    *found = a;
                    break;
                }
                if (!file_missing(status) && !resource_missing(status)) {
                    /* a manifest that exists but does not load or parse fails the whole context. */
                    DbgPrint("sxs: %ls is not a valid manifest (%08x)\n", path, status);
                    gen_free(dir);
                    gen_free(path);
                    gen_free(sub);
                    return status;
                }
                gen_free(dir);
                gen_free(path);
            }
        }
        gen_free(sub);
        if (*found)
            return STATUS_SUCCESS;
    }
    return STATUS_SXS_ASSEMBLY_NOT_FOUND;
}

static NTSTATUS resolve_dependencies(GEN *g)
{
    for (ULONG i = 0; i < g->Count; i++) {
        for (ULONG d = 0; d < g->Assemblies[i].Manifest.DependencyCount; d++) {
            /* the array may move while assemblies are added: index it each time. */
            const SXS_DEPENDENCY *dep = &g->Assemblies[i].Manifest.Dependencies[d];
            const SXS_IDENTITY *ref = &dep->Identity;
            BOOLEAN present = FALSE;
            for (ULONG j = 0; j < g->Count && !present; j++)
                present = g->Assemblies[j].Manifest.HasIdentity &&
                    same_identity_name(g->Assemblies[j].Manifest.Identity.Name, ref->Name);
            if (present)
                continue;
            PCWSTR system_file = nullptr;
            for (ULONG k = 0; k < RTL_NUMBER_OF(g_system_assemblies); k++)
                if (same_identity_name(ref->Name, g_system_assemblies[k].Name) && ref->Token &&
                    !_wcsicmp(ref->Token, PUBLIC_KEY_TOKEN_MICROSOFT))
                    system_file = g_system_assemblies[k].File;
            NTSTATUS status;
            if (system_file) {
                status = add_system_assembly(g, ref, system_file);
            } else {
                GEN_ASSEMBLY *found;
                status = probe_private(g, ref, &found);
                if (status == STATUS_SXS_ASSEMBLY_NOT_FOUND && (dep->Optional || ref->Token)) {
                    /*
                     * Windows fails without the assembly in winsxs. mime has no winsxs, so a shared
                     * assembly it does not provide is left out and the dlls resolve by the search path.
                     */
                    DbgPrint("sxs: assembly %ls not available, ignored\n", ref->Name);
                    status = STATUS_SUCCESS;
                } else if (status == STATUS_SXS_ASSEMBLY_NOT_FOUND) {
                    DbgPrint("sxs: dependent assembly %ls not found\n", ref->Name);
                }
            }
            if (!NT_SUCCESS(status))
                return status == STATUS_NO_MEMORY ? status : STATUS_SXS_CANT_GEN_ACTCTX;
        }
    }
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ buffers */

typedef struct BUF {
    UCHAR *Data;
    ULONG Size, Capacity;
    BOOLEAN Failed;
} BUF;

static ULONG buf_reserve(BUF *b, ULONG size, ULONG align)
{
    ULONG at = (b->Size + align - 1) & ~(align - 1);
    if (b->Failed || at + size < at)
        goto fail;
    if (at + size > b->Capacity) {
        ULONG capacity = b->Capacity ? b->Capacity : 4096;
        while (capacity < at + size)
            capacity *= 2;
        UCHAR *grown = gen_alloc(capacity);
        if (!grown)
            goto fail;
        if (b->Size)
            memcpy(grown, b->Data, b->Size);
        gen_free(b->Data);
        b->Data = grown;
        b->Capacity = capacity;
    }
    memset(b->Data + b->Size, 0, at + size - b->Size);
    b->Size = at + size;
    return at;
fail:
    b->Failed = TRUE;
    return 0;
}

static ULONG buf_append(BUF *b, const void *data, ULONG size, ULONG align)
{
    ULONG at = buf_reserve(b, size, align);
    if (!b->Failed && size)
        memcpy(b->Data + at, data, size);
    return at;
}

/* appends a string with its terminator; *length gets its byte length without it. */
static ULONG buf_string(BUF *b, PCWSTR s, ULONG *length)
{
    SIZE_T chars = s ? wcslen(s) : 0;
    if (length)
        *length = (ULONG)(chars * sizeof(WCHAR));
    ULONG at = buf_reserve(b, (ULONG)((chars + 1) * sizeof(WCHAR)), 2);
    if (!b->Failed && chars)
        memcpy(b->Data + at, s, chars * sizeof(WCHAR));
    return at;
}

#define FIELD(b, at, type, field) (((type *)((b)->Data + (at)))->field)

/* ------------------------------------------------------------------ sections */

/*
 * an element of a section: its key, its data built in a buffer of its own,
 * and the offsets inside that data whose value is relative to the element
 * or to the section's user data and must become relative to the section.
 */
typedef struct SECTION_ITEM {
    PCWSTR Key;
    GUID Guid;
    ULONG Roster;
    BUF Data;
    ULONG ItemFixups[8];
    ULONG ItemFixupCount;
    ULONG UserFixups[4];
    ULONG UserFixupCount;
} SECTION_ITEM;

typedef struct SECTION {
    ULONG Id;
    BOOLEAN Guids;
    BOOLEAN Raw; /* the data of the first item is the whole section */
    ULONG DataFormatVersion;
    SECTION_ITEM *Items;
    ULONG Count, Capacity;
    BUF UserData;
    ULONG UserFixups[4];
    ULONG UserFixupCount;
    BUF Out;
    BOOLEAN Conflict;
} SECTION;

static SECTION_ITEM *section_add(SECTION *s, PCWSTR key, const GUID *guid, ULONG roster)
{
    for (ULONG i = 0; i < s->Count; i++) {
        /* the first assembly to name a key keeps it. */
        if (key && s->Items[i].Key && !_wcsicmp(s->Items[i].Key, key))
            return nullptr;
        if (guid && s->Guids && !memcmp(&s->Items[i].Guid, guid, sizeof(*guid)))
            return nullptr;
    }
    if (s->Count == s->Capacity) {
        ULONG capacity = s->Capacity ? s->Capacity * 2 : 16;
        SECTION_ITEM *grown = gen_alloc(capacity * sizeof(*grown));
        if (!grown)
            return nullptr;
        if (s->Count)
            memcpy(grown, s->Items, s->Count * sizeof(*grown));
        gen_free(s->Items);
        s->Items = grown;
        s->Capacity = capacity;
    }
    SECTION_ITEM *item = &s->Items[s->Count++];
    memset(item, 0, sizeof(*item));
    item->Key = key;
    if (guid)
        item->Guid = *guid;
    item->Roster = roster;
    return item;
}

static void section_free(SECTION *s)
{
    for (ULONG i = 0; i < s->Count; i++)
        gen_free(s->Items[i].Data.Data);
    gen_free(s->Items);
    gen_free(s->UserData.Data);
    gen_free(s->Out.Data);
}

static void item_fixup(SECTION_ITEM *item, ULONG offset)
{
    if (item->ItemFixupCount < RTL_NUMBER_OF(item->ItemFixups))
        item->ItemFixups[item->ItemFixupCount++] = offset;
}

static void user_fixup(SECTION_ITEM *item, ULONG offset)
{
    if (item->UserFixupCount < RTL_NUMBER_OF(item->UserFixups))
        item->UserFixups[item->UserFixupCount++] = offset;
}

static int compare_keys(const SECTION_ITEM *a, const SECTION_ITEM *b)
{
    ULONG ka = SxspHashString(a->Key, wcslen(a->Key)), kb = SxspHashString(b->Key, wcslen(b->Key));
    return ka < kb ? -1 : ka > kb;
}

/* lays a section out: header, elements, keys, element data, user data. */
static void section_build(SECTION *s)
{
    BUF *o = &s->Out;
    if (s->Raw) {
        if (s->Count)
            buf_append(o, s->Items[0].Data.Data, s->Items[0].Data.Size, 8);
        return;
    }
    /* element order by pseudo key, which lets the reader search them by halves. */
    if (!s->Guids)
        for (ULONG i = 1; i < s->Count; i++)
            for (ULONG j = i; j > 0 && compare_keys(&s->Items[j - 1], &s->Items[j]) > 0; j--) {
                SECTION_ITEM t = s->Items[j];
                s->Items[j] = s->Items[j - 1];
                s->Items[j - 1] = t;
            }
    ULONG header_size = s->Guids ? sizeof(ACTCTX_GUID_SECTION_HEADER) : sizeof(ACTCTX_STRING_SECTION_HEADER);
    ULONG entry_size = s->Guids ? sizeof(ACTCTX_GUID_SECTION_ENTRY) : sizeof(ACTCTX_STRING_SECTION_ENTRY);
    buf_reserve(o, header_size, 4);
    ULONG entries = buf_reserve(o, entry_size * s->Count, 4);
    ULONG *keys = gen_alloc((s->Count + 1) * sizeof(ULONG));
    ULONG *datas = gen_alloc((s->Count + 1) * sizeof(ULONG));
    if (!keys || !datas) {
        o->Failed = TRUE;
        gen_free(keys);
        gen_free(datas);
        return;
    }
    if (!s->Guids)
        for (ULONG i = 0; i < s->Count; i++)
            keys[i] = buf_string(o, s->Items[i].Key, nullptr);
    for (ULONG i = 0; i < s->Count; i++)
        datas[i] = buf_append(o, s->Items[i].Data.Data, s->Items[i].Data.Size, 4);
    ULONG user = s->UserData.Size ? buf_append(o, s->UserData.Data, s->UserData.Size, 4) : 0;
    if (o->Failed) {
        gen_free(keys);
        gen_free(datas);
        return;
    }
    for (ULONG i = 0; i < s->Count; i++) {
        SECTION_ITEM *item = &s->Items[i];
        for (ULONG f = 0; f < item->ItemFixupCount; f++)
            *(ULONG *)(o->Data + datas[i] + item->ItemFixups[f]) += datas[i];
        for (ULONG f = 0; f < item->UserFixupCount; f++)
            *(ULONG *)(o->Data + datas[i] + item->UserFixups[f]) += user;
        if (s->Guids) {
            ACTCTX_GUID_SECTION_ENTRY *e = (void *)(o->Data + entries + i * entry_size);
            e->Guid = item->Guid;
            e->Offset = datas[i];
            e->Length = item->Data.Size;
            e->AssemblyRosterIndex = item->Roster;
        } else {
            ACTCTX_STRING_SECTION_ENTRY *e = (void *)(o->Data + entries + i * entry_size);
            SIZE_T chars = wcslen(item->Key);
            e->PseudoKey = SxspHashString(item->Key, chars);
            e->KeyOffset = keys[i];
            e->KeyLength = (ULONG)(chars * sizeof(WCHAR));
            e->Offset = datas[i];
            e->Length = item->Data.Size;
            e->AssemblyRosterIndex = item->Roster;
        }
    }
    for (ULONG f = 0; f < s->UserFixupCount; f++)
        *(ULONG *)(o->Data + user + s->UserFixups[f]) += user;
    if (s->Guids) {
        ACTCTX_GUID_SECTION_HEADER *h = (void *)o->Data;
        *h = (ACTCTX_GUID_SECTION_HEADER){ACTCTX_GUID_SECTION_MAGIC, header_size, 1, s->DataFormatVersion, 0,
                                          s->Count, s->Count ? entries : 0, 0, user, s->UserData.Size};
    } else {
        ACTCTX_STRING_SECTION_HEADER *h = (void *)o->Data;
        *h = (ACTCTX_STRING_SECTION_HEADER){ACTCTX_STRING_SECTION_MAGIC,
                                            header_size,
                                            1,
                                            s->DataFormatVersion,
                                            ACTCTX_STRING_SECTION_CASE_INSENSITIVE |
                                                ACTCTX_STRING_SECTION_INPSEUDOKEYORDER,
                                            s->Count,
                                            s->Count ? entries : 0,
                                            ACTCTX_HASH_X65599,
                                            0,
                                            user,
                                            s->UserData.Size};
    }
    gen_free(keys);
    gen_free(datas);
}

/* ------------------------------------------------------------------ section contents */

/* name,attr="value",... with the attributes in alphabetical order, as sxs encodes identities. */
static PWSTR encode_identity(const SXS_IDENTITY *id)
{
    WCHAR version[32] = {};
    if (id->HasVersion)
        _snwprintf(version, RTL_NUMBER_OF(version), L"%u.%u.%u.%u", id->Version[0], id->Version[1], id->Version[2],
                   id->Version[3]);
    const struct {
        PCWSTR Name;
        PCWSTR Value;
    } attrs[] = {{L"language", id->Language},
                 {L"processorArchitecture", id->Arch},
                 {L"publicKeyToken", id->Token},
                 {L"type", id->Type},
                 {L"version", id->HasVersion ? version : nullptr}};
    SIZE_T total = (id->Name ? wcslen(id->Name) : 0) + 1;
    for (ULONG i = 0; i < RTL_NUMBER_OF(attrs); i++)
        if (attrs[i].Value)
            total += wcslen(attrs[i].Name) + wcslen(attrs[i].Value) + 4;
    PWSTR s = gen_alloc(total * sizeof(WCHAR));
    if (!s)
        return nullptr;
    if (id->Name)
        wcscpy(s, id->Name);
    for (ULONG i = 0; i < RTL_NUMBER_OF(attrs); i++) {
        if (!attrs[i].Value)
            continue;
        SIZE_T n = wcslen(s);
        _snwprintf(s + n, total - n, L",%s=\"%s\"", attrs[i].Name, attrs[i].Value);
    }
    return s;
}

static void build_assembly_information(GEN *g, SECTION *s)
{
    s->DataFormatVersion = 1;
    for (ULONG i = 0; i < g->Count; i++) {
        GEN_ASSEMBLY *a = &g->Assemblies[i];
        SXS_MANIFEST *m = &a->Manifest;
        SECTION_ITEM *item = section_add(s, m->HasIdentity && m->Identity.Name ? m->Identity.Name : L"", nullptr, i + 1);
        if (!item) {
            /* two assemblies with one name: Windows rejects that as a version conflict. */
            s->Conflict = TRUE;
            return;
        }
        BUF *d = &item->Data;
        ULONG info = buf_reserve(d, sizeof(ACTCTX_ASSEMBLY_INFORMATION), 4);
        PWSTR encoded = m->HasIdentity ? encode_identity(&m->Identity) : nullptr;
        ULONG length;
        if (encoded) {
            ULONG at = buf_string(d, encoded, &length);
            FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, EncodedAssemblyIdentityLength) = length;
            FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, EncodedAssemblyIdentityOffset) = at;
            item_fixup(item, offsetof(ACTCTX_ASSEMBLY_INFORMATION, EncodedAssemblyIdentityOffset));
            gen_free(encoded);
        }
        FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, ManifestPathType) = a->ManifestPathType;
        if (a->ManifestPath) {
            ULONG at = buf_string(d, a->ManifestPath, &length);
            FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, ManifestPathLength) = length;
            FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, ManifestPathOffset) = at;
            item_fixup(item, offsetof(ACTCTX_ASSEMBLY_INFORMATION, ManifestPathOffset));
        }
        FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, PolicyPathType) = ACTCTX_PATH_TYPE_NONE;
        if (a->Language) {
            ULONG at = buf_string(d, a->Language, &length);
            FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, LanguageLength) = length;
            FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, LanguageOffset) = at;
            item_fixup(item, offsetof(ACTCTX_ASSEMBLY_INFORMATION, LanguageOffset));
        }
        FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, Size) = sizeof(ACTCTX_ASSEMBLY_INFORMATION);
        FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, Flags) =
            (a->Private ? ACTCTX_ASSEMBLY_PRIVATE : 0) | (i == 0 ? ACTCTX_ASSEMBLY_ROOT : 0);
        FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, ManifestVersionMajor) = 1;
        FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, NumOfFilesInAssembly) = m->FileCount;
        if (i == 0) {
            FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, RunLevel) = m->RunLevel;
            FIELD(d, info, ACTCTX_ASSEMBLY_INFORMATION, UiAccess) = m->UiAccess;
        }
    }
    BUF *u = &s->UserData;
    ULONG global = buf_reserve(u, sizeof(ACTCTX_ASSEMBLY_GLOBAL_INFORMATION), 4);
    ULONG length = 0, at = buf_string(u, g->AppDirectory, &length);
    FIELD(u, global, ACTCTX_ASSEMBLY_GLOBAL_INFORMATION, Size) = sizeof(ACTCTX_ASSEMBLY_GLOBAL_INFORMATION);
    FIELD(u, global, ACTCTX_ASSEMBLY_GLOBAL_INFORMATION, ApplicationDirectoryPathType) = ACTCTX_PATH_TYPE_WIN32_FILE;
    FIELD(u, global, ACTCTX_ASSEMBLY_GLOBAL_INFORMATION, ApplicationDirectoryLength) = length;
    FIELD(u, global, ACTCTX_ASSEMBLY_GLOBAL_INFORMATION, ApplicationDirectoryOffset) = at;
    FIELD(u, global, ACTCTX_ASSEMBLY_GLOBAL_INFORMATION, ResourceName) = g->ResourceName;
    s->UserFixups[s->UserFixupCount++] = offsetof(ACTCTX_ASSEMBLY_GLOBAL_INFORMATION, ApplicationDirectoryOffset);
}

static void build_dll_redirection(GEN *g, SECTION *s)
{
    s->DataFormatVersion = 1;
    for (ULONG i = 0; i < g->Count; i++) {
        GEN_ASSEMBLY *a = &g->Assemblies[i];
        for (ULONG f = 0; f < a->Manifest.FileCount; f++) {
            SECTION_ITEM *item = section_add(s, a->Manifest.Files[f].Name, nullptr, i + 1);
            if (!item)
                continue;
            BUF *d = &item->Data;
            ULONG r = buf_reserve(d, sizeof(ACTCTX_DLL_REDIRECTION), 4);
            FIELD(d, r, ACTCTX_DLL_REDIRECTION, Size) = sizeof(ACTCTX_DLL_REDIRECTION);
            if (a->System) {
                /* the whole path in one segment, the base name included. */
                PWSTR path = gen_concat(SYSTEM_DIRECTORY, a->SystemFile, nullptr, nullptr);
                ULONG seg = buf_reserve(d, sizeof(ACTCTX_DLL_PATH_SEGMENT), 4);
                ULONG length = 0, at = buf_string(d, path, &length);
                gen_free(path);
                FIELD(d, seg, ACTCTX_DLL_PATH_SEGMENT, Length) = length;
                FIELD(d, seg, ACTCTX_DLL_PATH_SEGMENT, Offset) = at;
                FIELD(d, r, ACTCTX_DLL_REDIRECTION, Flags) = ACTCTX_DLL_PATH_INCLUDES_BASE_NAME;
                FIELD(d, r, ACTCTX_DLL_REDIRECTION, TotalPathLength) = length;
                FIELD(d, r, ACTCTX_DLL_REDIRECTION, PathSegmentCount) = 1;
                FIELD(d, r, ACTCTX_DLL_REDIRECTION, PathSegmentOffset) = seg;
                item_fixup(item, seg + offsetof(ACTCTX_DLL_PATH_SEGMENT, Offset));
                item_fixup(item, offsetof(ACTCTX_DLL_REDIRECTION, PathSegmentOffset));
            } else {
                /* the assembly's directory followed by the name asked for. */
                FIELD(d, r, ACTCTX_DLL_REDIRECTION, Flags) = ACTCTX_DLL_OMITS_ASSEMBLY_ROOT;
            }
        }
    }
}

static void build_window_classes(GEN *g, SECTION *s)
{
    s->DataFormatVersion = 1;
    for (ULONG i = 0; i < g->Count; i++) {
        SXS_MANIFEST *m = &g->Assemblies[i].Manifest;
        for (ULONG f = 0; f < m->FileCount; f++) {
            for (ULONG c = 0; c < m->Files[f].ClassCount; c++) {
                SXS_WINDOW_CLASS *wc = &m->Files[f].Classes[c];
                SECTION_ITEM *item = section_add(s, wc->Name, nullptr, i + 1);
                if (!item)
                    continue;
                BUF *d = &item->Data;
                ULONG r = buf_reserve(d, sizeof(ACTCTX_WINDOW_CLASS_REDIRECTION), 4);
                WCHAR prefix[40] = {};
                if (wc->Versioned && m->HasIdentity && m->Identity.HasVersion)
                    _snwprintf(prefix, RTL_NUMBER_OF(prefix), L"%u.%u.%u.%u!", m->Identity.Version[0],
                               m->Identity.Version[1], m->Identity.Version[2], m->Identity.Version[3]);
                PWSTR versioned = gen_concat(prefix, wc->Name, nullptr, nullptr);
                ULONG length = 0, at = buf_string(d, versioned, &length);
                gen_free(versioned);
                FIELD(d, r, ACTCTX_WINDOW_CLASS_REDIRECTION, Size) = sizeof(ACTCTX_WINDOW_CLASS_REDIRECTION);
                FIELD(d, r, ACTCTX_WINDOW_CLASS_REDIRECTION, VersionSpecificClassNameLength) = length;
                FIELD(d, r, ACTCTX_WINDOW_CLASS_REDIRECTION, VersionSpecificClassNameOffset) = at;
                at = buf_string(d, m->Files[f].Name, &length);
                FIELD(d, r, ACTCTX_WINDOW_CLASS_REDIRECTION, DllNameLength) = length;
                FIELD(d, r, ACTCTX_WINDOW_CLASS_REDIRECTION, DllNameOffset) = at;
            }
        }
    }
}

/* the module names com sections share, in their user data; the offset is relative to it. */
static ULONG module_name(SECTION *s, PCWSTR file, ULONG *length)
{
    for (ULONG at = 0; at < s->UserData.Size;) {
        PCWSTR existing = (PCWSTR)(s->UserData.Data + at);
        SIZE_T chars = wcslen(existing);
        if (!_wcsicmp(existing, file)) {
            *length = (ULONG)(chars * sizeof(WCHAR));
            return at;
        }
        at += (ULONG)((chars + 1) * sizeof(WCHAR));
    }
    return buf_string(&s->UserData, file, length);
}

static void build_com(GEN *g, SECTION *servers, SECTION *progids, SECTION *interfaces, SECTION *typelibs)
{
    servers->DataFormatVersion = progids->DataFormatVersion = interfaces->DataFormatVersion =
        typelibs->DataFormatVersion = 1;
    for (ULONG i = 0; i < g->Count; i++) {
        SXS_MANIFEST *m = &g->Assemblies[i].Manifest;
        for (ULONG f = 0; f < m->FileCount; f++) {
            SXS_FILE *file = &m->Files[f];
            for (ULONG c = 0; c < file->ComClassCount; c++) {
                SXS_COM_CLASS *cc = &file->ComClasses[c];
                SECTION_ITEM *item = section_add(servers, nullptr, &cc->Clsid, i + 1);
                if (!item)
                    continue;
                BUF *d = &item->Data;
                ULONG r = buf_reserve(d, sizeof(ACTCTX_COM_SERVER_REDIRECTION), 4);
                ACTCTX_COM_SERVER_REDIRECTION *srv = (void *)(d->Data + r);
                if (d->Failed)
                    continue;
                srv->Size = sizeof(*srv);
                srv->ThreadingModel = cc->ThreadingModel;
                srv->ReferenceClsid = srv->ConfiguredClsid = srv->ImplementedClsid = cc->Clsid;
                srv->TypeLibraryId = cc->TypeLibrary;
                srv->MiscStatusDefault = cc->MiscStatus[0];
                srv->MiscStatusContent = cc->MiscStatus[1];
                srv->MiscStatusThumbnail = cc->MiscStatus[2];
                srv->MiscStatusIcon = cc->MiscStatus[3];
                srv->MiscStatusDocPrint = cc->MiscStatus[4];
                ULONG length = 0;
                ULONG module = module_name(servers, file->Name, &length);
                srv = (void *)(d->Data + r);
                srv->ModuleLength = length;
                srv->ModuleOffset = module;
                user_fixup(item, offsetof(ACTCTX_COM_SERVER_REDIRECTION, ModuleOffset));
                PCWSTR progid = cc->ProgId ? cc->ProgId : cc->ProgIdCount ? cc->ProgIds[0] : nullptr;
                if (progid) {
                    ULONG at = buf_string(d, progid, &length);
                    srv = (void *)(d->Data + r);
                    srv->ProgIdLength = length;
                    srv->ProgIdOffset = at;
                }
                /* every progid maps to the class through a clsid kept in the progid section's user data. */
                ULONG clsid = buf_append(&progids->UserData, &cc->Clsid, sizeof(GUID), 4);
                for (ULONG p = 0; p <= cc->ProgIdCount; p++) {
                    PCWSTR name = p == 0 ? cc->ProgId : cc->ProgIds[p - 1];
                    if (!name || !*name)
                        continue;
                    SECTION_ITEM *pi = section_add(progids, name, nullptr, i + 1);
                    if (!pi)
                        continue;
                    ULONG pr = buf_reserve(&pi->Data, sizeof(ACTCTX_COM_PROGID_REDIRECTION), 4);
                    FIELD(&pi->Data, pr, ACTCTX_COM_PROGID_REDIRECTION, Size) = sizeof(ACTCTX_COM_PROGID_REDIRECTION);
                    FIELD(&pi->Data, pr, ACTCTX_COM_PROGID_REDIRECTION, ConfiguredClsidOffset) = clsid;
                    user_fixup(pi, offsetof(ACTCTX_COM_PROGID_REDIRECTION, ConfiguredClsidOffset));
                }
            }
            for (ULONG t = 0; t < file->TypeLibCount; t++) {
                SXS_TYPELIB *tl = &file->TypeLibs[t];
                SECTION_ITEM *item = section_add(typelibs, nullptr, &tl->Id, i + 1);
                if (!item)
                    continue;
                BUF *d = &item->Data;
                ULONG r = buf_reserve(d, sizeof(ACTCTX_COM_TYPELIB_REDIRECTION), 4);
                ULONG length = 0, module = module_name(typelibs, file->Name, &length);
                FIELD(d, r, ACTCTX_COM_TYPELIB_REDIRECTION, Size) = sizeof(ACTCTX_COM_TYPELIB_REDIRECTION);
                FIELD(d, r, ACTCTX_COM_TYPELIB_REDIRECTION, NameLength) = length + sizeof(WCHAR);
                FIELD(d, r, ACTCTX_COM_TYPELIB_REDIRECTION, NameOffset) = module;
                FIELD(d, r, ACTCTX_COM_TYPELIB_REDIRECTION, MajorVersion) = tl->Major;
                FIELD(d, r, ACTCTX_COM_TYPELIB_REDIRECTION, MinorVersion) = tl->Minor;
                FIELD(d, r, ACTCTX_COM_TYPELIB_REDIRECTION, LibraryFlags) = tl->Flags;
                user_fixup(item, offsetof(ACTCTX_COM_TYPELIB_REDIRECTION, NameOffset));
                if (tl->HelpDir) {
                    ULONG at = buf_string(d, tl->HelpDir, &length);
                    FIELD(d, r, ACTCTX_COM_TYPELIB_REDIRECTION, HelpDirLength) = length;
                    FIELD(d, r, ACTCTX_COM_TYPELIB_REDIRECTION, HelpDirOffset) = at;
                }
            }
        }
        for (ULONG k = 0; k < m->FileCount + 1; k++) {
            SXS_INTERFACE *list = k < m->FileCount ? m->Files[k].Interfaces : m->ExternalProxies;
            ULONG count = k < m->FileCount ? m->Files[k].InterfaceCount : m->ExternalProxyCount;
            for (ULONG n = 0; n < count; n++) {
                SXS_INTERFACE *itf = &list[n];
                SECTION_ITEM *item = section_add(interfaces, nullptr, &itf->Iid, i + 1);
                if (!item)
                    continue;
                BUF *d = &item->Data;
                ULONG r = buf_reserve(d, sizeof(ACTCTX_COM_INTERFACE_REDIRECTION), 4);
                ACTCTX_COM_INTERFACE_REDIRECTION *ir = (void *)(d->Data + r);
                if (d->Failed)
                    continue;
                ir->Size = sizeof(*ir);
                ir->Flags = (itf->HasNumMethods ? 1 : 0) | (itf->HasBase ? 2 : 0);
                ir->ProxyStubClsid32 = itf->ProxyStubClsid;
                ir->NumMethods = itf->NumMethods;
                ir->TypeLibraryId = itf->TypeLibrary;
                ir->BaseInterface = itf->Base;
                if (itf->Name) {
                    ULONG length = 0, at = buf_string(d, itf->Name, &length);
                    ir = (void *)(d->Data + r);
                    ir->NameLength = length;
                    ir->NameOffset = at;
                }
            }
        }
    }
}

static void build_settings(GEN *g, SECTION *s)
{
    s->DataFormatVersion = 1;
    SXS_MANIFEST *m = &g->Assemblies[0].Manifest;
    for (ULONG i = 0; i < m->SettingCount; i++) {
        SXS_SETTING *setting = &m->Settings[i];
        SECTION_ITEM *item = section_add(s, setting->Name, nullptr, 1);
        if (!item)
            continue;
        BUF *d = &item->Data;
        ULONG r = buf_reserve(d, sizeof(ACTCTX_APPLICATION_SETTING), 4);
        ULONG length = 0, at = buf_string(d, setting->Namespace, &length);
        FIELD(d, r, ACTCTX_APPLICATION_SETTING, Size) = sizeof(ACTCTX_APPLICATION_SETTING);
        FIELD(d, r, ACTCTX_APPLICATION_SETTING, SettingNamespaceLength) = length;
        FIELD(d, r, ACTCTX_APPLICATION_SETTING, SettingNamespaceOffset) = at;
        at = buf_string(d, setting->Name, &length);
        FIELD(d, r, ACTCTX_APPLICATION_SETTING, SettingNameLength) = length;
        FIELD(d, r, ACTCTX_APPLICATION_SETTING, SettingNameOffset) = at;
        at = buf_string(d, setting->Value, &length);
        FIELD(d, r, ACTCTX_APPLICATION_SETTING, SettingValueLength) = length;
        FIELD(d, r, ACTCTX_APPLICATION_SETTING, SettingValueOffset) = at;
    }
}

static void build_compatibility(GEN *g, SECTION *s)
{
    SXS_MANIFEST *m = &g->Assemblies[0].Manifest;
    s->Raw = TRUE;
    if (!m->CompatibilityCount)
        return;
    SECTION_ITEM *item = section_add(s, nullptr, nullptr, 1);
    if (!item)
        return;
    BUF *d = &item->Data;
    ULONG header = buf_reserve(d, 8, 8);
    *(ULONG *)(d->Data + header) = m->CompatibilityCount;
    for (ULONG i = 0; i < m->CompatibilityCount; i++) {
        COMPATIBILITY_CONTEXT_ELEMENT e = {m->Compatibility[i].Id, (ACTCTX_COMPATIBILITY_ELEMENT_TYPE)m->Compatibility[i].Type,
                                           m->Compatibility[i].MaxVersionTested};
        buf_append(d, &e, sizeof(e), 8);
    }
}

/* ------------------------------------------------------------------ the data */

static NTSTATUS build_data(GEN *g, PVOID *out)
{
    enum { SECTIONS = 9 };
    SECTION sections[SECTIONS] = {};
    sections[0].Id = ACTIVATION_CONTEXT_SECTION_ASSEMBLY_INFORMATION;
    sections[1].Id = ACTIVATION_CONTEXT_SECTION_DLL_REDIRECTION;
    sections[2].Id = ACTIVATION_CONTEXT_SECTION_WINDOW_CLASS_REDIRECTION;
    sections[3].Id = ACTIVATION_CONTEXT_SECTION_COM_SERVER_REDIRECTION;
    sections[4].Id = ACTIVATION_CONTEXT_SECTION_COM_INTERFACE_REDIRECTION;
    sections[5].Id = ACTIVATION_CONTEXT_SECTION_COM_TYPE_LIBRARY_REDIRECTION;
    sections[6].Id = ACTIVATION_CONTEXT_SECTION_COM_PROGID_REDIRECTION;
    sections[7].Id = ACTIVATION_CONTEXT_SECTION_APPLICATION_SETTINGS;
    sections[8].Id = ACTIVATION_CONTEXT_SECTION_COMPATIBILITY_INFO;
    sections[3].Guids = sections[4].Guids = sections[5].Guids = TRUE;
    build_assembly_information(g, &sections[0]);
    build_dll_redirection(g, &sections[1]);
    build_window_classes(g, &sections[2]);
    build_com(g, &sections[3], &sections[6], &sections[4], &sections[5]);
    build_settings(g, &sections[7]);
    build_compatibility(g, &sections[8]);

    NTSTATUS status = STATUS_SUCCESS;
    ULONG present = 0;
    for (ULONG i = 0; i < SECTIONS; i++) {
        if (sections[i].Count || i == 0) {
            section_build(&sections[i]);
            present++;
        }
        if (sections[i].Conflict)
            status = STATUS_SXS_CANT_GEN_ACTCTX;
        else if (sections[i].Out.Failed || sections[i].UserData.Failed)
            status = STATUS_NO_MEMORY;
        for (ULONG k = 0; k < sections[i].Count; k++)
            if (sections[i].Items[k].Data.Failed)
                status = STATUS_NO_MEMORY;
    }

    BUF b = {};
    buf_reserve(&b, sizeof(ACTCTX_DATA), 8);
    ULONG toc = buf_reserve(&b, sizeof(ACTCTX_TOC_HEADER) + present * sizeof(ACTCTX_TOC_ENTRY), 4);
    ULONG roster = buf_reserve(&b, sizeof(ACTCTX_ROSTER_HEADER) + (g->Count + 1) * sizeof(ACTCTX_ROSTER_ENTRY), 4);
    ULONG names[64];
    for (ULONG i = 0; i < g->Count && i < RTL_NUMBER_OF(names); i++) {
        SXS_MANIFEST *m = &g->Assemblies[i].Manifest;
        names[i] = buf_string(&b, m->HasIdentity && m->Identity.Name ? m->Identity.Name : L"", nullptr);
    }
    ULONG offsets[SECTIONS] = {};
    for (ULONG i = 0; i < SECTIONS; i++)
        if (sections[i].Out.Size)
            offsets[i] = buf_append(&b, sections[i].Out.Data, sections[i].Out.Size, 8);
    if (g->Count > RTL_NUMBER_OF(names))
        status = STATUS_SXS_CANT_GEN_ACTCTX;
    if (b.Failed && NT_SUCCESS(status))
        status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(status)) {
        ACTCTX_DATA *h = (void *)b.Data;
        *h = (ACTCTX_DATA){ACTCTX_DATA_MAGIC, sizeof(ACTCTX_DATA), 1, b.Size, toc, 0, roster,
                           g->Assemblies[0].Manifest.NoInherit ? ACTCTX_DATA_FLAG_NO_INHERIT : 0};
        ACTCTX_TOC_HEADER *th = (void *)(b.Data + toc);
        *th = (ACTCTX_TOC_HEADER){sizeof(*th), present, toc + sizeof(*th), ACTCTX_TOC_INORDER};
        ACTCTX_TOC_ENTRY *te = (void *)(th + 1);
        for (ULONG i = 0; i < SECTIONS; i++) {
            if (!sections[i].Out.Size)
                continue;
            *te++ = (ACTCTX_TOC_ENTRY){sections[i].Id, offsets[i], sections[i].Out.Size, sections[i].Raw ? 0 : 1};
        }
        ACTCTX_ROSTER_HEADER *rh = (void *)(b.Data + roster);
        *rh = (ACTCTX_ROSTER_HEADER){sizeof(*rh), ACTCTX_HASH_X65599, g->Count + 1, roster + sizeof(*rh), offsets[0]};
        ACTCTX_ROSTER_ENTRY *re = (void *)(rh + 1);
        re[0].Flags = ACTCTX_ROSTER_ENTRY_INVALID;
        const ACTCTX_STRING_SECTION_HEADER *info = (void *)(b.Data + offsets[0]);
        const ACTCTX_STRING_SECTION_ENTRY *elements = (void *)((PUCHAR)info + info->ElementListOffset);
        for (ULONG i = 0; i < g->Count; i++) {
            SXS_MANIFEST *m = &g->Assemblies[i].Manifest;
            PCWSTR name = m->HasIdentity && m->Identity.Name ? m->Identity.Name : L"";
            ACTCTX_ROSTER_ENTRY *e = &re[i + 1];
            e->Flags = i == 0 ? ACTCTX_ROSTER_ENTRY_ROOT : 0;
            e->PseudoKey = SxspHashString(name, wcslen(name));
            e->AssemblyNameOffset = names[i];
            e->AssemblyNameLength = (ULONG)(wcslen(name) * sizeof(WCHAR));
            for (ULONG k = 0; k < info->ElementCount; k++) {
                if (elements[k].AssemblyRosterIndex == i + 1) {
                    e->AssemblyInformationOffset = offsets[0] + elements[k].Offset;
                    e->AssemblyInformationLength = elements[k].Length;
                }
            }
        }
    }
    for (ULONG i = 0; i < SECTIONS; i++)
        section_free(&sections[i]);
    if (!NT_SUCCESS(status)) {
        gen_free(b.Data);
        return status;
    }
    /* pages of its own, read only, as csrss maps the data into the process. */
    PVOID pages = nullptr;
    SIZE_T size = b.Size;
    status = NtAllocateVirtualMemory(NtCurrentProcess(), &pages, 0, &size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (NT_SUCCESS(status)) {
        memcpy(pages, b.Data, b.Size);
        ULONG old;
        PVOID base = pages;
        NtProtectVirtualMemory(NtCurrentProcess(), &base, &size, PAGE_READONLY, &old);
        *out = pages;
    }
    gen_free(b.Data);
    return status;
}

/* ------------------------------------------------------------------ entry */

/* the manifest resource of the source, found but not parsed yet. */
static NTSTATUS open_resource(const MIME_SXS_GENERATE *r, ULONG_PTR name, MAPPED_FILE *file, const void **data,
                              SIZE_T *size)
{
    file->View = nullptr;
    if (r->Module)
        return find_manifest_resource(r->Module, name, data, size);
    NTSTATUS status = map_file(r->Source, file);
    if (NT_SUCCESS(status) && !LdrpNtHeaders(file->View))
        status = STATUS_INVALID_IMAGE_FORMAT;
    if (NT_SUCCESS(status))
        status = find_manifest_resource((PVOID)((ULONG_PTR)file->View | 1), name, data, size);
    if (!NT_SUCCESS(status))
        unmap_file(file);
    return status;
}

/*
 * reads the root manifest the request names. an image's manifest is its
 * resource, or the <image>.manifest file beside it, <image>.<id>.manifest
 * for other resource ids: the resource first unless the machine prefers
 * external manifests. the stream that opens is the one parsed, a broken
 * manifest never falls back to the other one.
 */
static NTSTATUS load_root(const MIME_SXS_GENERATE *r, GEN_ASSEMBLY *root, ULONG *resource_id)
{
    ULONG_PTR name = (ULONG_PTR)r->ResourceName;
    *resource_id = 0;
    if (!name && !r->Module && !has_image_extension(r->Source)) {
        root->ManifestPath = gen_strdup(r->Source);
        return root->ManifestPath ? load_manifest_file(r->Source, &root->Manifest) : STATUS_NO_MEMORY;
    }
    if (!name)
        return STATUS_RESOURCE_TYPE_NOT_FOUND;
    if (IS_INTRESOURCE(name))
        *resource_id = (ULONG)name;
    WCHAR suffix[24] = L".manifest";
    if (IS_INTRESOURCE(name) && name != 1)
        _snwprintf(suffix, RTL_NUMBER_OF(suffix), L".%u.manifest", (ULONG)name);
    PWSTR external = gen_concat(r->Source, suffix, nullptr, nullptr);
    if (!external)
        return STATUS_NO_MEMORY;
    MAPPED_FILE image = {}, file = {};
    const void *data = nullptr;
    SIZE_T size = 0;
    NTSTATUS status;
    BOOLEAN from_file;
    if (prefer_external_manifest()) {
        status = map_file(external, &file);
        from_file = NT_SUCCESS(status);
        if (!from_file && file_missing(status))
            status = open_resource(r, name, &image, &data, &size);
    } else {
        status = open_resource(r, name, &image, &data, &size);
        from_file = FALSE;
        if (resource_missing(status)) {
            NTSTATUS file_status = map_file(external, &file);
            from_file = NT_SUCCESS(file_status);
            if (from_file || !file_missing(file_status))
                status = file_status;
        }
    }
    if (NT_SUCCESS(status)) {
        if (from_file)
            status = SxspParseManifest(file.View, file.Size, &root->Manifest);
        else
            status = SxspParseManifest(data, size, &root->Manifest);
    }
    if (NT_SUCCESS(status)) {
        root->ManifestPath = from_file ? external : gen_strdup(r->Source);
        if (from_file)
            external = nullptr;
        if (!root->ManifestPath)
            status = STATUS_NO_MEMORY;
    }
    unmap_file(&image);
    unmap_file(&file);
    gen_free(external);
    return status;
}

/* ------------------------------------------------------------------ application configuration */

static BOOLEAN is_separator(WCHAR c)
{
    return c == '\\' || c == '/';
}

/*
 * a private path of a configuration file is relative: no colon, no empty part, and ".." only
 * as the first one or two parts, as sxs.dll checks.
 */
static BOOLEAN private_segment_valid(PCWSTR s, SIZE_T n)
{
    if (!n || is_separator(s[0]) || is_separator(s[n - 1]))
        return FALSE;
    ULONG parents = 0;
    BOOLEAN named = FALSE;
    SIZE_T start = 0;
    for (SIZE_T i = 0; i <= n; i++) {
        if (i < n && s[i] == ':')
            return FALSE;
        if (i < n && !is_separator(s[i]))
            continue;
        SIZE_T length = i - start;
        if (!length)
            return FALSE;
        if (length == 2 && s[start] == '.' && s[start + 1] == '.') {
            if (named || parents >= 2)
                return FALSE;
            parents++;
        } else {
            named = TRUE;
        }
        start = i + 1;
    }
    return TRUE;
}

/* the application directory with the segment added, as GetFullPathName writes it, with a trailing backslash. */
static PWSTR private_root(PCWSTR app_directory, PCWSTR segment, SIZE_T n)
{
    SIZE_T base = wcslen(app_directory);
    PWSTR path = gen_alloc((base + n + 2) * sizeof(WCHAR));
    if (!path)
        return nullptr;
    memcpy(path, app_directory, base * sizeof(WCHAR));
    SIZE_T length = base;
    SIZE_T floor = 0;
    /* the root of the path, a drive or a share, is not walked out of. */
    if (base >= 3 && path[1] == ':')
        floor = 3;
    for (SIZE_T i = 0, start = 0; i <= n; i++) {
        if (i < n && !is_separator(segment[i]))
            continue;
        SIZE_T part = i - start;
        if (part == 2 && segment[start] == '.' && segment[start + 1] == '.') {
            if (length <= floor) {
                gen_free(path);
                return nullptr;
            }
            length--;
            while (length > floor && path[length - 1] != '\\')
                length--;
        } else if (part && !(part == 1 && segment[start] == '.')) {
            memcpy(path + length, segment + start, part * sizeof(WCHAR));
            length += part;
            path[length++] = '\\';
        }
        start = i + 1;
    }
    path[length] = 0;
    return path;
}

static NTSTATUS add_private_segment(GEN *g, PCWSTR segment, SIZE_T n)
{
    if (!private_segment_valid(segment, n))
        return STATUS_SXS_CANT_GEN_ACTCTX;
    PWSTR root = private_root(g->Roots[0], segment, n);
    if (!root)
        return STATUS_SXS_CANT_GEN_ACTCTX;
    if (g->RootCount >= GEN_MAX_ROOTS) {
        gen_free(root);
        return STATUS_SXS_CANT_GEN_ACTCTX;
    }
    g->Roots[g->RootCount++] = root;
    return STATUS_SUCCESS;
}

/* the value of a privatePath attribute: segments divided by semicolons. */
static NTSTATUS add_private_path(PCWSTR value, void *context)
{
    GEN *g = context;
    PCWSTR start = value;
    for (PCWSTR p = value;; p++) {
        if (*p && *p != ';')
            continue;
        if (*p || p != start) {
            NTSTATUS status = add_private_segment(g, start, (SIZE_T)(p - start));
            if (!NT_SUCCESS(status))
                return status;
        }
        if (!*p)
            return STATUS_SUCCESS;
        start = p + 1;
    }
}

/* <program>.config beside a program, read for the probing of its private assemblies. */
static NTSTATUS load_application_config(GEN *g, PCWSTR program)
{
    PWSTR path = gen_concat(program, L".config", nullptr, nullptr);
    if (!path)
        return STATUS_NO_MEMORY;
    MAPPED_FILE f;
    NTSTATUS status = map_file(path, &f);
    if (NT_SUCCESS(status)) {
        status = SxspParseApplicationConfig(f.View, f.Size, add_private_path, g);
        if (!NT_SUCCESS(status))
            DbgPrint("sxs: %ls is not a valid configuration (%08x)\n", path, status);
        unmap_file(&f);
    } else if (file_missing(status)) {
        status = STATUS_SUCCESS;
    }
    gen_free(path);
    return status;
}

static NTSTATUS generate(const MIME_SXS_GENERATE *request, const void *text, SIZE_T text_size, PVOID *data)
{
    *data = nullptr;
    GEN g = {};
    PWSTR app_directory = request->AssemblyDirectory ? gen_strdup(request->AssemblyDirectory)
                                                     : directory_of(request->Source);
    if (app_directory && *app_directory && app_directory[wcslen(app_directory) - 1] != '\\') {
        PWSTR slashed = gen_concat(app_directory, L"\\", nullptr, nullptr);
        gen_free(app_directory);
        app_directory = slashed;
    }
    if (!app_directory)
        return STATUS_NO_MEMORY;
    g.AppDirectory = app_directory;
    g.Roots[g.RootCount++] = app_directory;
    GEN_ASSEMBLY *root = add_assembly(&g);
    NTSTATUS status = root ? STATUS_SUCCESS : STATUS_NO_MEMORY;
    if (NT_SUCCESS(status) && request->ApplicationConfig)
        status = load_application_config(&g, request->Source);
    if (NT_SUCCESS(status)) {
        root->Private = TRUE;
        root->ManifestPathType = ACTCTX_PATH_TYPE_WIN32_FILE;
        if (text) {
            root->ManifestPath = gen_strdup(request->Source);
            status = root->ManifestPath ? SxspParseManifest(text, text_size, &root->Manifest) : STATUS_NO_MEMORY;
        } else {
            status = load_root(request, root, &g.ResourceName);
        }
    }
    if (NT_SUCCESS(status)) {
        root->Directory = directory_of(root->ManifestPath);
        status = root->Directory ? resolve_dependencies(&g) : STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(status))
        status = build_data(&g, data);
    free_gen(&g);
    gen_free(app_directory);
    return status;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlGenerateActivationContext(const MIME_SXS_GENERATE *request, PVOID *data)
{
    if (!request || !request->Source || !data)
        return STATUS_INVALID_PARAMETER;
    return generate(request, nullptr, 0, data);
}

NTSTATUS SxspGenerateFromText(const void *text, SIZE_T size, PCWSTR path, PVOID *data)
{
    MIME_SXS_GENERATE request = {.Source = path};
    return generate(&request, text, size, data);
}

NTDLL_EXPORT void NTAPI RtlFreeActivationContextData(PVOID data)
{
    if (!data)
        return;
    SIZE_T size = 0;
    NtFreeVirtualMemory(NtCurrentProcess(), &data, &size, MEM_RELEASE);
}
