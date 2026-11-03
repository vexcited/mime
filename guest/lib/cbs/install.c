/* lays out the components of a servicing cabinet: files, links, global assembly cache entries and registry values. */
#include "cbs.h"
#include "../cabinet/cabinet.h"
#include "../crypto/crypto.h"
#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif

/* ------------------------------------------------------------------ text helpers */

static char fold(char c)
{
    if (c == '/')
        return '\\';
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

/* compares as paths do: case and the kind of slash do not matter. */
static int path_compare(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = fold(*a), y = fold(*b);
        if (x != y)
            return (uint8_t)x < (uint8_t)y ? -1 : 1;
        if (!x)
            return 0;
    }
}

static int path_compare_n(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        char x = fold(*a), y = fold(*b);
        if (x != y)
            return (uint8_t)x < (uint8_t)y ? -1 : 1;
        if (!x)
            return 0;
    }
    return 0;
}

static const char *base_name(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    return base;
}

/* ------------------------------------------------------------------ the installation */

typedef struct PLAN {
    uint32_t CabIndex;
    uint32_t Order; /* keeps equal cabinet entries in manifest order. */
    const CBS_COMPONENT *Component;
    const CBS_FILE_ENTRY *File;
    const char *Path;
    const char **Links;
    unsigned LinkCount, LinkCapacity;
    uint32_t Size;
} PLAN;

typedef struct NAME_ENTRY {
    const char *Name;
    uint32_t Index;
} NAME_ENTRY;

typedef struct DIRECTORY_SET {
    const char **Slot;
    unsigned Capacity, Count;
} DIRECTORY_SET;

typedef struct INSTALL {
    const CBS_LAYOUT *Layout;
    const CBS_TARGET *Target;
    const CBS_PROGRESS *Progress;
    MIME_CAB *Cab;
    const uint8_t *Data;
    unsigned CabFiles;
    NAME_ENTRY *Names; /* sorted by name, for the payload lookups. */
    const char **FileName; /* by cabinet index, in stored order, for finding an entry from its name pointer. */
    uint32_t *FileSize;
    CBS_ARENA *Arena;
    const CBS_COMPONENT **Components;
    unsigned ComponentCount, ComponentCapacity;
    PLAN *Plans;
    unsigned PlanCount, PlanCapacity;
    DIRECTORY_SET Directories;
    uint64_t Total, Done, Reported;
    int Status;
    /* the manifests still to read. */
    unsigned ManifestsLeft;
} INSTALL;

static void log_line(const INSTALL *I, const char *message)
{
    if (I->Target->Log)
        I->Target->Log(I->Target->Context, message);
}

/* a line with a path after the message. */
static void log_path(const INSTALL *I, const char *message, const char *path)
{
    char line[700];
    size_t a = strlen(message), b = strlen(path);
    if (!I->Target->Log || a + b + 2 > sizeof(line))
        return;
    memcpy(line, message, a);
    line[a] = ' ';
    memcpy(line + a + 1, path, b + 1);
    I->Target->Log(I->Target->Context, line);
}

/* ------------------------------------------------------------------ variables */

static const char *system_directory(const INSTALL *I, CBS_ARCH arch)
{
    if (I->Layout->Is64Bit && (arch == CBS_ARCH_X86 || arch == CBS_ARCH_WOW64))
        return I->Layout->SysWow64;
    return I->Layout->System32;
}

/* expands the variables of a manifest string; writes to out when it is not null and returns the length. */
static size_t expand_into(const INSTALL *I, CBS_ARCH arch, const char *in, char *out)
{
    size_t n = 0;
    while (*in) {
        if (in[0] == '$' && in[1] == '(') {
            const char *close = strchr(in + 2, ')');
            if (close) {
                size_t length = (size_t)(close - in - 2);
                const char *name = in + 2;
                const char *value = nullptr;
                const char *suffix = "";
                if (length == 15 && !_strnicmp(name, "runtime.windows", 15))
                    value = I->Layout->Windows;
                else if (length == 16 && !_strnicmp(name, "runtime.system32", 16))
                    value = system_directory(I, arch);
                else if (length == 11 && !_strnicmp(name, "runtime.inf", 11)) {
                    value = I->Layout->Windows;
                    suffix = "\\inf";
                } else if (length == 12 && !_strnicmp(name, "runtime.wbem", 12)) {
                    value = system_directory(I, arch);
                    suffix = "\\wbem";
                } else if (length == 19 && !_strnicmp(name, "runtime.programData", 19))
                    value = I->Layout->ProgramData;
                else if (length == 17 && !_strnicmp(name, "runtime.startmenu", 17)) {
                    value = I->Layout->ProgramData;
                    suffix = "\\Microsoft\\Windows\\Start Menu";
                } else if (length == 20 && !_strnicmp(name, "runtime.programFiles", 20))
                    value = I->Layout->ProgramFiles;
                else if (length == 14 && !_strnicmp(name, "assembly.empty", 14))
                    value = "";
                if (value) {
                    size_t a = strlen(value), b = strlen(suffix);
                    if (out) {
                        memcpy(out + n, value, a);
                        memcpy(out + n + a, suffix, b);
                    }
                    n += a + b;
                    in = close + 1;
                    continue;
                }
            }
        }
        if (out)
            out[n] = *in;
        n++;
        in++;
    }
    if (out)
        out[n] = 0;
    return n;
}

static char *expand(INSTALL *I, CBS_ARCH arch, const char *in)
{
    size_t n = expand_into(I, arch, in, nullptr);
    char *out = CbsArenaAlloc(I->Arena, n + 1);
    if (out)
        expand_into(I, arch, in, out);
    return out;
}

/* a path that climbs out of the directory it names with a ".." component is not one a manifest has. */
static bool climbs(const char *path)
{
    for (const char *p = path; *p; p++) {
        bool start = p == path || p[-1] == '\\' || p[-1] == '/';
        if (start && p[0] == '.' && p[1] == '.' && (!p[2] || p[2] == '\\' || p[2] == '/'))
            return true;
    }
    return false;
}

/* directory + file name, with one backslash between. */
static char *join(INSTALL *I, const char *directory, const char *name)
{
    size_t a = strlen(directory), b = strlen(name);
    bool slash = a && directory[a - 1] != '\\';
    char *out = CbsArenaAlloc(I->Arena, a + b + 2);
    if (!out)
        return nullptr;
    memcpy(out, directory, a);
    if (slash)
        out[a++] = '\\';
    memcpy(out + a, name, b + 1);
    return out;
}

/* ------------------------------------------------------------------ directories */

static unsigned hash_path(const char *path)
{
    unsigned h = 2166136261u;
    for (; *path; path++)
        h = (h ^ (uint8_t)fold(*path)) * 16777619u;
    return h;
}

static int ensure_directory(INSTALL *I, const char *directory)
{
    DIRECTORY_SET *set = &I->Directories;
    if (set->Count * 2 >= set->Capacity) {
        unsigned capacity = set->Capacity ? set->Capacity * 2 : 256;
        const char **slots = CbsAllocZero(sizeof(char *) * capacity);
        if (!slots)
            return CBS_MEMORY;
        for (unsigned i = 0; i < set->Capacity; i++) {
            if (!set->Slot[i])
                continue;
            unsigned at = hash_path(set->Slot[i]) & (capacity - 1);
            while (slots[at])
                at = (at + 1) & (capacity - 1);
            slots[at] = set->Slot[i];
        }
        CbsFree(set->Slot);
        set->Slot = slots;
        set->Capacity = capacity;
    }
    unsigned at = hash_path(directory) & (set->Capacity - 1);
    while (set->Slot[at]) {
        if (!path_compare(set->Slot[at], directory))
            return CBS_OK;
        at = (at + 1) & (set->Capacity - 1);
    }
    if (I->Target->CreateDirectory(I->Target->Context, directory))
        return CBS_TARGET_FAILED;
    size_t n = strlen(directory);
    char *copy = CbsArenaString(I->Arena, directory, n);
    if (!copy)
        return CBS_MEMORY;
    set->Slot[at] = copy;
    set->Count++;
    return CBS_OK;
}

/* the directory part of a path, in a buffer of the caller's. */
static int ensure_parent(INSTALL *I, const char *path)
{
    const char *base = base_name(path);
    size_t n = (size_t)(base - path);
    while (n > 0 && (path[n - 1] == '\\' || path[n - 1] == '/') && n > 3)
        n--;
    char *parent = CbsArenaString(I->Arena, path, n);
    return parent ? ensure_directory(I, parent) : CBS_MEMORY;
}

/* ------------------------------------------------------------------ sorting */

static void sort_names(NAME_ENTRY *a, unsigned n)
{
    for (unsigned gap = n / 2; gap; gap = gap == 2 ? 1 : gap * 5 / 11) {
        for (unsigned i = gap; i < n; i++) {
            NAME_ENTRY t = a[i];
            unsigned j = i;
            for (; j >= gap && path_compare(a[j - gap].Name, t.Name) > 0; j -= gap)
                a[j] = a[j - gap];
            a[j] = t;
        }
        if (gap == 1)
            break;
    }
}

static void sort_plans(PLAN *a, unsigned n)
{
    for (unsigned gap = n / 2; gap; gap = gap == 2 ? 1 : gap * 5 / 11) {
        for (unsigned i = gap; i < n; i++) {
            PLAN t = a[i];
            unsigned j = i;
            for (; j >= gap && (a[j - gap].CabIndex > t.CabIndex ||
                                (a[j - gap].CabIndex == t.CabIndex && a[j - gap].Order > t.Order));
                 j -= gap)
                a[j] = a[j - gap];
            a[j] = t;
        }
        if (gap == 1)
            break;
    }
}

static const NAME_ENTRY *find_name(const INSTALL *I, const char *name)
{
    unsigned lo = 0, hi = I->CabFiles;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        int c = path_compare(I->Names[mid].Name, name);
        if (!c)
            return &I->Names[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return nullptr;
}

/* the cabinet index of an entry from the pointer to its name, which MimeCabFile hands out. */
static int index_of(const INSTALL *I, const char *name)
{
    unsigned lo = 0, hi = I->CabFiles;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (I->FileName[mid] == name)
            return (int)mid;
        if (I->FileName[mid] < name)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

/* ------------------------------------------------------------------ reading the manifests */

typedef struct MANIFEST_READ {
    INSTALL *I;
    uint8_t *Data;
    size_t Size, Used;
    const char *Name;
    bool Failed;
} MANIFEST_READ;

static bool arch_wanted(const INSTALL *I, const char *file_name)
{
    CBS_ARCH native = I->Layout->Native;
    size_t n = 0;
    while (file_name[n] && file_name[n] != '_')
        n++;
    if (!file_name[n])
        return false;
    bool x86 = n == 3 && !_strnicmp(file_name, "x86", 3);
    bool amd64 = n == 5 && !_strnicmp(file_name, "amd64", 5);
    bool arm64 = n == 5 && !_strnicmp(file_name, "arm64", 5);
    bool wow64 = n == 5 && !_strnicmp(file_name, "wow64", 5);
    bool msil = n == 4 && !_strnicmp(file_name, "msil", 4);
    if (native == CBS_ARCH_AMD64)
        return x86 || amd64 || wow64 || msil;
    if (native == CBS_ARCH_ARM64)
        return x86 || arm64 || wow64 || msil;
    return x86 || msil;
}

/* the hash named stubs of a servicing package only point at the components that hold the files. */
static bool is_stub(const char *file_name)
{
    const char *p = strchr(file_name, '_');
    if (!p)
        return false;
    p++;
    for (int i = 0; i < 32; i++) {
        char c = p[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return p[32] == '_';
}

static bool is_manifest_name(const char *name)
{
    size_t n = strlen(name);
    if (n < 10 || _stricmp(name + n - 9, ".manifest"))
        return false;
    for (const char *p = name; *p; p++)
        if (*p == '\\' || *p == '/')
            return false;
    return true;
}

static void *manifest_begin(void *context, const MIME_CAB_FILE *file)
{
    INSTALL *I = context;
    if (I->Status != CBS_OK || !I->ManifestsLeft)
        return nullptr;
    if (!is_manifest_name(file->Name) || is_stub(file->Name) || !arch_wanted(I, file->Name))
        return nullptr;
    MANIFEST_READ *m = CbsAllocZero(sizeof(*m));
    if (!m) {
        I->Status = CBS_MEMORY;
        return nullptr;
    }
    m->I = I;
    m->Name = file->Name;
    m->Size = file->Size;
    m->Data = CbsAlloc(file->Size + 1);
    if (!m->Data) {
        CbsFree(m);
        I->Status = CBS_MEMORY;
        return nullptr;
    }
    return m;
}

static int manifest_write(void *context, void *file_context, const uint8_t *data, size_t length)
{
    MANIFEST_READ *m = file_context;
    if (m->Used + length > m->Size) {
        /* the cabinet does not end a file whose write failed. */
        ((INSTALL *)context)->Status = CBS_CORRUPT;
        CbsFree(m->Data);
        CbsFree(m);
        return 1;
    }
    memcpy(m->Data + m->Used, data, length);
    m->Used += length;
    return 0;
}

static int manifest_end(void *context, void *file_context, const MIME_CAB_FILE *file)
{
    INSTALL *I = context;
    MANIFEST_READ *m = file_context;
    (void)file;
    int status = CBS_OK;
    if (m->Used != m->Size)
        status = CBS_CORRUPT;
    else {
        CBS_COMPONENT *component;
        status = CbsManifestParse(I->Arena, m->Name, (const char *)m->Data, m->Size, &component);
        if (status == CBS_OK) {
            if (I->ComponentCount == I->ComponentCapacity) {
                unsigned capacity = I->ComponentCapacity ? I->ComponentCapacity * 2 : 512;
                const CBS_COMPONENT **list = CbsAlloc(sizeof(*list) * capacity);
                if (!list)
                    status = CBS_MEMORY;
                else {
                    if (I->ComponentCount)
                        memcpy(list, I->Components, sizeof(*list) * I->ComponentCount);
                    CbsFree(I->Components);
                    I->Components = list;
                    I->ComponentCapacity = capacity;
                }
            }
            if (status == CBS_OK)
                I->Components[I->ComponentCount++] = component;
        }
    }
    CbsFree(m->Data);
    CbsFree(m);
    if (I->ManifestsLeft)
        I->ManifestsLeft--;
    if (status != CBS_OK) {
        I->Status = status;
        return 1;
    }
    /* the manifests come first in the cabinet, so the rest of it is not read for them. */
    return I->ManifestsLeft ? 0 : 1;
}

/* ------------------------------------------------------------------ planning */

static int add_plan(INSTALL *I, const PLAN *plan, unsigned *index)
{
    if (I->PlanCount == I->PlanCapacity) {
        unsigned capacity = I->PlanCapacity ? I->PlanCapacity * 2 : 2048;
        PLAN *plans = CbsAlloc(sizeof(PLAN) * capacity);
        if (!plans)
            return CBS_MEMORY;
        if (I->PlanCount)
            memcpy(plans, I->Plans, sizeof(PLAN) * I->PlanCount);
        CbsFree(I->Plans);
        I->Plans = plans;
        I->PlanCapacity = capacity;
    }
    *index = I->PlanCount;
    I->Plans[I->PlanCount++] = *plan;
    return CBS_OK;
}

static int add_link(INSTALL *I, PLAN *plan, const char *path)
{
    for (unsigned i = 0; i < plan->LinkCount; i++)
        if (!path_compare(plan->Links[i], path))
            return CBS_OK;
    if (plan->LinkCount == plan->LinkCapacity) {
        unsigned capacity = plan->LinkCapacity ? plan->LinkCapacity * 2 : 4;
        const char **links = CbsArenaAlloc(I->Arena, sizeof(char *) * capacity);
        if (!links)
            return CBS_MEMORY;
        if (plan->LinkCount)
            memcpy(links, plan->Links, sizeof(char *) * plan->LinkCount);
        plan->Links = links;
        plan->LinkCapacity = capacity;
    }
    plan->Links[plan->LinkCount++] = path;
    return CBS_OK;
}

static const char *gac_folder(CBS_GAC gac)
{
    switch (gac) {
    case CBS_GAC_X86:
        return "GAC_32";
    case CBS_GAC_AMD64:
        return "GAC_64";
    case CBS_GAC_ARM64:
        return "GAC_ARM64";
    default:
        return "GAC_MSIL";
    }
}

/* the folder of the cache a file goes into: below Microsoft.NET\\Assembly, by processor, assembly name and version. */
static char *gac_directory(INSTALL *I, const CBS_COMPONENT *c, const CBS_FILE_ENTRY *file)
{
    char folder_name[160];
    const char *version = file->GacVersion ? file->GacVersion : "0.0.0.0";
    const char *culture = file->GacCulture ? file->GacCulture : "";
    const char *token = c->Id.Token ? c->Id.Token : "";
    size_t version_length = strlen(version), culture_length = strlen(culture), token_length = strlen(token);
    if (version_length + culture_length + token_length + 8 > sizeof(folder_name))
        return nullptr;
    /* v4.0_<assembly version>_<culture>_<token>, the culture of an assembly for every language being empty. */
    memcpy(folder_name, "v4.0_", 5);
    memcpy(folder_name + 5, version, version_length);
    folder_name[5 + version_length] = '_';
    memcpy(folder_name + 6 + version_length, culture, culture_length);
    folder_name[6 + version_length + culture_length] = '_';
    memcpy(folder_name + 7 + version_length + culture_length, token, token_length + 1);
    char *path = join(I, I->Layout->Windows, "Microsoft.NET\\Assembly");
    path = path ? join(I, path, gac_folder(file->Gac)) : nullptr;
    path = path ? join(I, path, c->Id.Name) : nullptr;
    return path ? join(I, path, folder_name) : nullptr;
}

/* plans one component: where each of its files goes, with the links that go with it. */
static int plan_component(INSTALL *I, const CBS_COMPONENT *c)
{
    CBS_ARCH arch = c->Id.Arch;
    unsigned first = I->PlanCount;
    for (unsigned f = 0; f < c->FileCount; f++) {
        const CBS_FILE_ENTRY *file = &c->Files[f];
        char payload[1024];
        size_t folder = strlen(c->Folder), name = strlen(file->Name);
        if (folder + name + 2 > sizeof(payload))
            return CBS_BAD;
        memcpy(payload, c->Folder, folder);
        payload[folder] = '\\';
        memcpy(payload + folder + 1, file->Name, name + 1);
        const NAME_ENTRY *entry = find_name(I, payload);
        if (!entry && I->Layout->SkipMissing)
            continue;
        if (!entry)
            return CBS_NOT_FOUND;
        char *directory;
        if (file->Destination)
            directory = expand(I, arch, file->Destination);
        else if (file->Gac != CBS_GAC_NONE)
            directory = gac_directory(I, c, file);
        else {
            /* a file that only follows the assembly goes where the assembly goes. */
            const CBS_FILE_ENTRY *assembly = nullptr;
            for (unsigned g = 0; g < c->FileCount && !assembly; g++)
                if (c->Files[g].Gac != CBS_GAC_NONE)
                    assembly = &c->Files[g];
            if (!assembly)
                return CBS_BAD;
            directory = gac_directory(I, c, assembly);
        }
        if (!directory)
            return CBS_MEMORY;
        PLAN plan = {};
        plan.CabIndex = entry->Index;
        plan.Order = I->PlanCount;
        plan.Component = c;
        plan.File = file;
        plan.Path = join(I, directory, base_name(file->Name));
        plan.Size = I->FileSize[entry->Index];
        if (!plan.Path)
            return CBS_MEMORY;
        if (climbs(plan.Path))
            return CBS_BAD;
        unsigned index;
        int status = add_plan(I, &plan, &index);
        if (status != CBS_OK)
            return status;
        for (unsigned l = 0; l < file->LinkCount; l++) {
            char *link = expand(I, arch, c->Links[file->FirstLink + l]);
            if (!link)
                return CBS_MEMORY;
            if (climbs(link))
                return CBS_BAD;
            status = add_link(I, &I->Plans[index], link);
            if (status != CBS_OK)
                return status;
        }
    }
    /* a file that goes into the cache itself brings along the files of its own directory. */
    for (unsigned f = 0; f < c->FileCount; f++) {
        const CBS_FILE_ENTRY *file = &c->Files[f];
        if (file->Gac == CBS_GAC_NONE || !file->GacCopy || !file->Destination)
            continue;
        char *cache = gac_directory(I, c, file);
        if (!cache)
            return CBS_MEMORY;
        for (unsigned g = 0; g < c->FileCount; g++) {
            if (!c->Files[g].Destination || path_compare(c->Files[g].Destination, file->Destination))
                continue;
            unsigned plan = first;
            while (plan < I->PlanCount && I->Plans[plan].File != &c->Files[g])
                plan++;
            if (plan == I->PlanCount)
                continue;
            char *link = join(I, cache, base_name(c->Files[g].Name));
            if (!link)
                return CBS_MEMORY;
            int status = add_link(I, &I->Plans[plan], link);
            if (status != CBS_OK)
                return status;
        }
    }
    return CBS_OK;
}

/* reports the work done now and then; true when the user cancelled. */
static bool report(INSTALL *I, bool force)
{
    if (!I->Progress || !I->Progress->Report)
        return false;
    if (!force && I->Done - I->Reported < (1u << 20))
        return false;
    I->Reported = I->Done;
    if (!I->Progress->Report(I->Progress->Context, I->Done, I->Total))
        return false;
    if (I->Status == CBS_OK)
        I->Status = CBS_STOPPED;
    return true;
}

static void step(const INSTALL *I, const char *text)
{
    if (I->Progress && I->Progress->Step)
        I->Progress->Step(I->Progress->Context, text);
}

/* ------------------------------------------------------------------ writing the files */

typedef struct WRITE {
    INSTALL *I;
    unsigned First, Count; /* the plans that share this payload. */
    void *Handle;
    MimeHashCtx Hash;
    uint64_t Written, Reported;
    bool Failed;
} WRITE;

static bool in_system_directory(const INSTALL *I, const char *path)
{
    const char *dirs[2] = {I->Layout->System32, I->Layout->SysWow64};
    for (int i = 0; i < 2; i++) {
        size_t n = strlen(dirs[i]);
        if (!path_compare_n(path, dirs[i], n) && path[n] == '\\' && !strchr(path + n + 1, '\\'))
            return true;
    }
    return false;
}

/* hard links of a finished file, and the other names of the same payload. */
static int place_names(INSTALL *I, unsigned first, unsigned count)
{
    const CBS_TARGET *t = I->Target;
    const PLAN *main = &I->Plans[first];
    const uint8_t *digest = main->File->HasDigest ? main->File->Digest : nullptr;
    for (unsigned k = 0; k < count; k++) {
        const PLAN *plan = &I->Plans[first + k];
        for (unsigned n = 0; n < plan->LinkCount + (k ? 1 : 0); n++) {
            /* the first name of a plan other than the main one is the plan's own path. */
            const char *path = k ? (n == 0 ? plan->Path : plan->Links[n - 1]) : plan->Links[n];
            int state;
            if (ensure_parent(I, path) != CBS_OK)
                return CBS_TARGET_FAILED;
            if (t->FileState(t->Context, path, main->Size, digest, &state))
                return CBS_TARGET_FAILED;
            if (state == 1)
                continue;
            if (t->Link(t->Context, main->Path, path)) {
                log_path(I, "a link could not be made:", path);
                return CBS_TARGET_FAILED;
            }
        }
    }
    return CBS_OK;
}

static void *payload_begin(void *context, const MIME_CAB_FILE *file)
{
    INSTALL *I = context;
    if (I->Status != CBS_OK)
        return nullptr;
    int index = index_of(I, file->Name);
    if (index < 0)
        return nullptr;
    unsigned lo = 0, hi = I->PlanCount;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (I->Plans[mid].CabIndex < (uint32_t)index)
            lo = mid + 1;
        else
            hi = mid;
    }
    unsigned first = lo, count = 0;
    while (first + count < I->PlanCount && I->Plans[first + count].CabIndex == (uint32_t)index)
        count++;
    if (!count)
        return nullptr;
    const CBS_TARGET *t = I->Target;
    const PLAN *main = &I->Plans[first];
    const uint8_t *digest = main->File->HasDigest ? main->File->Digest : nullptr;
    int status = ensure_parent(I, main->Path);
    int state = 0;
    if (status == CBS_OK && t->FileState(t->Context, main->Path, file->Size, digest, &state))
        status = CBS_TARGET_FAILED;
    /* a system directory file that is there already stays, it may be one that the system provides. */
    if (status == CBS_OK && state != 0 && in_system_directory(I, main->Path)) {
        I->Done += file->Size;
        report(I, false);
        return nullptr;
    }
    if (status == CBS_OK && state == 1) {
        status = place_names(I, first, count);
        I->Done += file->Size;
        report(I, false);
        if (status != CBS_OK)
            I->Status = status;
        return nullptr;
    }
    WRITE *w = CbsAllocZero(sizeof(*w));
    if (!w) {
        I->Status = CBS_MEMORY;
        return nullptr;
    }
    w->I = I;
    w->First = first;
    w->Count = count;
    if (status == CBS_OK && t->Begin(t->Context, main->Path, file->Size, &w->Handle))
        status = CBS_TARGET_FAILED;
    if (status != CBS_OK) {
        I->Status = status;
        w->Failed = true;
        return w;
    }
    mime_sha256.init(&w->Hash);
    return w;
}

static int payload_write(void *context, void *file_context, const uint8_t *data, size_t length)
{
    INSTALL *I = context;
    WRITE *w = file_context;
    if (w->Failed) {
        CbsFree(w);
        return 1;
    }
    mime_sha256.update(&w->Hash, data, length);
    bool stop = false;
    if (I->Target->Write(I->Target->Context, w->Handle, data, length)) {
        I->Status = CBS_TARGET_FAILED;
        stop = true;
    } else {
        w->Written += length;
        I->Done += length;
        stop = report(I, false);
    }
    if (stop) {
        /* the cabinet does not end a file whose write stopped, the half written one goes. */
        I->Target->Abort(I->Target->Context, w->Handle);
        CbsFree(w);
    }
    return stop ? 1 : 0;
}

static int payload_end(void *context, void *file_context, const MIME_CAB_FILE *file)
{
    INSTALL *I = context;
    WRITE *w = file_context;
    (void)file;
    const CBS_TARGET *t = I->Target;
    const PLAN *main = &I->Plans[w->First];
    int status = I->Status;
    if (w->Failed) {
        CbsFree(w);
        return 1;
    }
    uint8_t digest[32];
    mime_sha256.final(&w->Hash, digest);
    if (status == CBS_OK && main->File->HasDigest && memcmp(digest, main->File->Digest, 32)) {
        log_path(I, "a file does not match the hash of its manifest:", main->Path);
        status = CBS_CORRUPT;
    }
    if (status != CBS_OK) {
        t->Abort(t->Context, w->Handle);
    } else if (t->End(t->Context, w->Handle)) {
        status = CBS_TARGET_FAILED;
    } else {
        status = place_names(I, w->First, w->Count);
    }
    CbsFree(w);
    if (status != CBS_OK) {
        I->Status = status;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ registry */

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* a key name of a manifest as the target wants it, below HKEY_LOCAL_MACHINE; null for another root. */
static char *registry_key(INSTALL *I, CBS_ARCH arch, const char *name)
{
    char *key = expand(I, arch, name);
    if (!key)
        return nullptr;
    size_t n = strlen(key);
    while (n && key[n - 1] == '\\')
        key[--n] = 0;
    static const char hklm[] = "HKEY_LOCAL_MACHINE\\";
    static const char hkcr[] = "HKEY_CLASSES_ROOT\\";
    if (n >= sizeof(hklm) - 1 && !_strnicmp(key, hklm, sizeof(hklm) - 1))
        return key + sizeof(hklm) - 1;
    if (n >= sizeof(hkcr) - 1 && !_strnicmp(key, hkcr, sizeof(hkcr) - 1)) {
        static const char classes[] = "SOFTWARE\\Classes\\";
        size_t rest = n - (sizeof(hkcr) - 1);
        char *out = CbsArenaAlloc(I->Arena, sizeof(classes) + rest);
        if (!out)
            return nullptr;
        memcpy(out, classes, sizeof(classes) - 1);
        memcpy(out + sizeof(classes) - 1, key + sizeof(hkcr) - 1, rest + 1);
        return out;
    }
    return nullptr;
}

/* the bytes of a value the way the target takes them; returns false when the manifest's text is not valid. */
static bool registry_data(INSTALL *I, CBS_ARCH arch, const CBS_REG_VALUE *v, uint8_t **data, size_t *size)
{
    const char *text = v->Data ? v->Data : "";
    *data = nullptr;
    switch (v->Type) {
    case CBS_REG_DWORD: {
        uint64_t number = 0;
        const char *p = text;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            if (!*p)
                return false;
            for (; *p; p++) {
                int d = hex_value(*p);
                if (d < 0)
                    return false;
                number = number << 4 | (uint64_t)d;
            }
        } else {
            if (!*p)
                return false;
            for (; *p; p++) {
                if (*p < '0' || *p > '9')
                    return false;
                number = number * 10 + (uint64_t)(*p - '0');
            }
        }
        uint8_t *out = CbsArenaAlloc(I->Arena, 4);
        if (!out)
            return false;
        for (int i = 0; i < 4; i++)
            out[i] = (uint8_t)(number >> (8 * i));
        *data = out;
        *size = 4;
        return true;
    }
    case CBS_REG_BINARY: {
        size_t n = strlen(text);
        if (n % 2)
            return false;
        uint8_t *out = CbsArenaAlloc(I->Arena, n / 2 + 1);
        if (!out)
            return false;
        for (size_t i = 0; i < n / 2; i++) {
            int hi = hex_value(text[2 * i]), lo = hex_value(text[2 * i + 1]);
            if (hi < 0 || lo < 0)
                return false;
            out[i] = (uint8_t)(hi << 4 | lo);
        }
        *data = out;
        *size = n / 2;
        return true;
    }
    case CBS_REG_MULTI_SZ: {
        /* "a","b" : each string in quotes and the strings separated by commas, as the .reg files of the hive have it. */
        char *expanded = expand(I, arch, text);
        if (!expanded)
            return false;
        size_t n = strlen(expanded);
        uint8_t *out = CbsArenaAlloc(I->Arena, n + 3);
        if (!out)
            return false;
        size_t used = 0;
        const char *p = expanded;
        while (*p) {
            if (*p != '"')
                return false;
            p++;
            size_t begin = used;
            while (*p && *p != '"')
                out[used++] = (uint8_t)*p++;
            if (*p != '"')
                return false;
            p++;
            /* an empty string has no place in a list that ends with an empty one, so it is left out. */
            if (used != begin)
                out[used++] = 0;
            if (*p == ',')
                p++;
            else if (*p)
                return false;
        }
        out[used++] = 0;
        if (used == 1)
            out[used++] = 0;
        *data = out;
        *size = used;
        return true;
    }
    default: {
        char *expanded = expand(I, arch, text);
        if (!expanded)
            return false;
        *data = (uint8_t *)expanded;
        *size = strlen(expanded) + 1;
        return true;
    }
    }
}

static int registry_view(const INSTALL *I, CBS_ARCH arch)
{
    return I->Layout->Is64Bit && (arch == CBS_ARCH_X86 || arch == CBS_ARCH_WOW64) ? 32 : 64;
}

static int write_registry(INSTALL *I, const CBS_COMPONENT *c)
{
    const CBS_TARGET *t = I->Target;
    int view = registry_view(I, c->Id.Arch);
    for (unsigned k = 0; k < c->KeyCount; k++) {
        const CBS_REG_KEY *key = &c->Keys[k];
        char *name = registry_key(I, c->Id.Arch, key->Name);
        if (!name)
            continue;
        int created = t->CreateKey(t->Context, view, name);
        if (created == CBS_DENIED) {
            log_path(I, "a registry key that is protected stays as it is:", name);
            continue;
        }
        if (created) {
            log_line(I, name);
            return CBS_TARGET_FAILED;
        }
        /* a newer release number that is there already stays, and so does the version beside it. */
        bool keep_release = false;
        for (unsigned v = 0; v < key->ValueCount; v++) {
            const CBS_REG_VALUE *value = &c->Values[key->FirstValue + v];
            if (value->Name && value->Type == CBS_REG_DWORD && !_stricmp(value->Name, "Release")) {
                uint8_t *data;
                size_t size;
                uint32_t existing;
                if (registry_data(I, c->Id.Arch, value, &data, &size) &&
                    !t->GetDword(t->Context, view, name, "Release", &existing)) {
                    uint32_t wanted = (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
                    keep_release = existing > wanted;
                }
            }
        }
        for (unsigned v = 0; v < key->ValueCount; v++) {
            const CBS_REG_VALUE *value = &c->Values[key->FirstValue + v];
            if (keep_release && value->Name &&
                (!_stricmp(value->Name, "Release") || !_stricmp(value->Name, "Version")))
                continue;
            char *value_name = value->Name ? expand(I, c->Id.Arch, value->Name) : nullptr;
            if (value->Name && !value_name)
                return CBS_MEMORY;
            uint8_t *data;
            size_t size;
            if (!registry_data(I, c->Id.Arch, value, &data, &size)) {
                log_line(I, "a registry value of a manifest is not valid");
                return CBS_BAD;
            }
            int set = t->SetValue(t->Context, view, name, value_name, (uint32_t)value->Type, data, size);
            if (set == CBS_DENIED) {
                log_path(I, "a registry value that is protected stays as it is:", name);
                continue;
            }
            if (set) {
                log_line(I, name);
                return CBS_TARGET_FAILED;
            }
        }
    }
    return CBS_OK;
}

/* ------------------------------------------------------------------ the whole installation */

static void release(INSTALL *I)
{
    if (I->Cab)
        MimeCabClose(I->Cab);
    CbsFree(I->Names);
    CbsFree(I->FileName);
    CbsFree(I->FileSize);
    CbsFree(I->Components);
    CbsFree(I->Plans);
    CbsFree(I->Directories.Slot);
    CbsArenaDestroy(I->Arena);
}

static int map_cab_status(int status)
{
    switch (status) {
    case MIME_CAB_OK:
        return CBS_OK;
    case MIME_CAB_UNSUPPORTED:
        return CBS_UNSUPPORTED;
    case MIME_CAB_CORRUPT:
        return CBS_CORRUPT;
    case MIME_CAB_MEMORY:
        return CBS_MEMORY;
    case MIME_CAB_STOPPED:
        return CBS_STOPPED;
    default:
        return CBS_BAD;
    }
}

int CbsInstallCabinet(const uint8_t *cabinet, size_t size, const CBS_LAYOUT *layout, const CBS_TARGET *target,
                      const CBS_PROGRESS *progress)
{
    INSTALL I = {};
    I.Layout = layout;
    I.Target = target;
    I.Progress = progress;
    I.Data = cabinet;
    I.Arena = CbsArenaCreate();
    if (!I.Arena)
        return CBS_MEMORY;
    int status = map_cab_status(MimeCabOpen(&I.Cab, cabinet, size, CbsAlloc, CbsFree));
    if (status != CBS_OK) {
        release(&I);
        return status;
    }
    MimeCabSequential(I.Cab);
    I.CabFiles = MimeCabFileCount(I.Cab);
    I.Names = CbsAlloc(sizeof(NAME_ENTRY) * (I.CabFiles + 1));
    I.FileName = CbsAlloc(sizeof(char *) * (I.CabFiles + 1));
    I.FileSize = CbsAlloc(sizeof(uint32_t) * (I.CabFiles + 1));
    if (!I.Names || !I.FileName || !I.FileSize) {
        release(&I);
        return CBS_MEMORY;
    }
    unsigned manifests = 0;
    for (unsigned i = 0; i < I.CabFiles; i++) {
        MIME_CAB_FILE f;
        MimeCabFile(I.Cab, i, &f);
        I.Names[i].Name = f.Name;
        I.Names[i].Index = i;
        I.FileName[i] = f.Name;
        I.FileSize[i] = f.Size;
        if (is_manifest_name(f.Name) && !is_stub(f.Name) && arch_wanted(&I, f.Name))
            manifests++;
    }
    /* stored order is by name pointer already, the names sorted for the lookups need their own order. */
    sort_names(I.Names, I.CabFiles);
    I.ManifestsLeft = manifests;
    if (!manifests) {
        release(&I);
        return CBS_NOT_FOUND;
    }
    step(&I, "Reading the package");
    MIME_CAB_FILE_SINK manifest_sink = {manifest_begin, manifest_write, manifest_end};
    status = map_cab_status(MimeCabExtract(I.Cab, &manifest_sink, &I));
    if (I.Status != CBS_OK)
        status = I.Status;
    else if (status == CBS_STOPPED && !I.ManifestsLeft)
        status = CBS_OK;
    if (status != CBS_OK) {
        release(&I);
        return status;
    }
    for (unsigned c = 0; c < I.ComponentCount && status == CBS_OK; c++)
        status = plan_component(&I, I.Components[c]);
    if (status != CBS_OK) {
        release(&I);
        return status;
    }
    sort_plans(I.Plans, I.PlanCount);
    /* the sizes to report: each payload once. */
    for (unsigned p = 0; p < I.PlanCount; p++)
        if (!p || I.Plans[p].CabIndex != I.Plans[p - 1].CabIndex)
            I.Total += I.Plans[p].Size;
    report(&I, true);
    for (unsigned c = 0; c < I.ComponentCount && status == CBS_OK; c++) {
        const CBS_COMPONENT *component = I.Components[c];
        for (unsigned d = 0; d < component->DirectoryCount && status == CBS_OK; d++) {
            char *directory = expand(&I, component->Id.Arch, component->Directories[d]);
            status = !directory ? CBS_MEMORY : climbs(directory) ? CBS_BAD : ensure_directory(&I, directory);
        }
    }
    if (status == CBS_OK) {
        step(&I, "Copying the files");
        MIME_CAB_FILE_SINK payload_sink = {payload_begin, payload_write, payload_end};
        status = map_cab_status(MimeCabExtract(I.Cab, &payload_sink, &I));
        if (I.Status != CBS_OK)
            status = I.Status;
    }
    if (status == CBS_OK)
        step(&I, "Writing the registry");
    for (unsigned c = 0; c < I.ComponentCount && status == CBS_OK; c++)
        status = write_registry(&I, I.Components[c]);
    if (status == CBS_OK) {
        I.Done = I.Total;
        report(&I, true);
        status = I.Status;
    }
    release(&I);
    return status;
}
