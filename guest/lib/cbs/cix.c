/*
 * servicing cabinets in the form of Windows Update: the files of the cabinet are numbered, and an index says for each
 * file of each component which numbered entry holds it, as itself or as an MSDelta patch against another file of the
 * package. this reads the index, makes the files that the wanted components hold and writes them, with the manifests, into
 * a plain cabinet of the form that CbsInstallCabinet knows.
 */
#include "cbs.h"
#include "../cabinet/cabinet.h"
#include "../crypto/crypto.h"
#include "../msdelta/msdelta.h"
#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif

#define INDEX_NAME "_manifest_.cix.xml"

typedef struct CIX_FILE {
    char *Name; /* the folder of the component, a backslash and the name below it. */
    uint32_t Length;
    uint8_t Hash[32];
    bool HasHash, Patch;
    char *Source;    /* the name of the entry of the cabinet. */
    int32_t Basis;   /* the index of the file a patch applies to, or -1. */
    int32_t Entry;   /* the entry of the cabinet that holds the source, or -1. */
    uint8_t *Data;   /* the file, once it is made. */
    size_t Size;
    uint8_t State;   /* 0 not made, 1 made, 2 failed. */
    int Status;
} CIX_FILE;

typedef struct CIX {
    CIX_FILE *Files;
    unsigned Count, Capacity;
    int32_t *ById;
    unsigned ByIdCount;
    CBS_ARENA *Arena;
} CIX;

typedef struct ENTRY {
    const char *Name;
    uint32_t Index;
    uint32_t Size;
} ENTRY;

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

static bool read_hash(const char *text, uint8_t *out)
{
    for (int i = 0; i < 32; i++) {
        int hi = hex_value(text[2 * i]), lo = hex_value(text[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}

static uint32_t to_number(const char *text)
{
    uint64_t value = 0;
    for (; *text >= '0' && *text <= '9'; text++)
        value = value * 10 + (uint64_t)(*text - '0');
    return value > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)value;
}

static int add_file(CIX *cix, CIX_FILE **file)
{
    if (cix->Count == cix->Capacity) {
        unsigned capacity = cix->Capacity ? cix->Capacity * 2 : 4096;
        CIX_FILE *grown = CbsAlloc(capacity * sizeof(*grown));
        if (!grown)
            return CBS_MEMORY;
        if (cix->Count)
            memcpy(grown, cix->Files, cix->Count * sizeof(*grown));
        CbsFree(cix->Files);
        cix->Files = grown;
        cix->Capacity = capacity;
    }
    *file = &cix->Files[cix->Count++];
    memset(*file, 0, sizeof(**file));
    (*file)->Basis = (*file)->Entry = -1;
    return CBS_OK;
}

/* reads the index: a File element with its hash, its Source and perhaps its Basis. ids of Basis refer to the id of File. */
static int parse_index(CIX *cix, const char *text, size_t length)
{
    CBS_XML xml;
    if (CbsXmlOpen(&xml, text, length) != CBS_OK)
        return CBS_BAD;
    uint32_t *ids = nullptr, *bases = nullptr;
    unsigned id_capacity = 0;
    int status = CBS_OK;
    CIX_FILE *file = nullptr;
    unsigned file_depth = 0;
    bool in_source = false;
    uint32_t id = 0, basis = 0xFFFFFFFFu;
    for (;;) {
        CBS_XML_EVENT event = CbsXmlNext(&xml);
        if (event == CBS_XML_ERROR) {
            status = CBS_BAD;
            break;
        }
        if (event == CBS_XML_END)
            break;
        if (event == CBS_XML_START) {
            if (!strcmp(xml.Name, "File")) {
                const char *name = CbsXmlAttribute(&xml, "name"), *length_text = CbsXmlAttribute(&xml, "length");
                const char *id_text = CbsXmlAttribute(&xml, "id");
                if (!name || !length_text || !id_text) {
                    status = CBS_BAD;
                    break;
                }
                status = add_file(cix, &file);
                if (status != CBS_OK)
                    break;
                file->Name = CbsArenaString(cix->Arena, name, strlen(name));
                file->Length = to_number(length_text);
                id = to_number(id_text);
                basis = 0xFFFFFFFFu;
                file_depth = xml.Depth;
                in_source = false;
                if (cix->Count > id_capacity) {
                    unsigned capacity = id_capacity ? id_capacity * 2 : 4096;
                    uint32_t *a = CbsAlloc(capacity * sizeof(*a)), *b = CbsAlloc(capacity * sizeof(*b));
                    if (!a || !b) {
                        CbsFree(a);
                        CbsFree(b);
                        status = CBS_MEMORY;
                        break;
                    }
                    if (id_capacity) {
                        memcpy(a, ids, id_capacity * sizeof(*a));
                        memcpy(b, bases, id_capacity * sizeof(*b));
                    }
                    CbsFree(ids);
                    CbsFree(bases);
                    ids = a;
                    bases = b;
                    id_capacity = capacity;
                }
                if (!file->Name) {
                    status = CBS_MEMORY;
                    break;
                }
                ids[cix->Count - 1] = id;
            } else if (file && !strcmp(xml.Name, "Source")) {
                const char *type = CbsXmlAttribute(&xml, "type"), *name = CbsXmlAttribute(&xml, "name");
                if (!type || !name) {
                    status = CBS_BAD;
                    break;
                }
                file->Patch = !!strcmp(type, "RAW");
                file->Source = CbsArenaString(cix->Arena, name, strlen(name));
                in_source = true;
            } else if (file && !strcmp(xml.Name, "Hash") && !in_source && !file->HasHash) {
                const char *value = CbsXmlAttribute(&xml, "value"), *alg = CbsXmlAttribute(&xml, "alg");
                if (!value || !alg || strcmp(alg, "SHA256") || strlen(value) != 64 || !read_hash(value, file->Hash)) {
                    status = CBS_BAD;
                    break;
                }
                file->HasHash = true;
            } else if (file && !strcmp(xml.Name, "Basis")) {
                const char *value = CbsXmlAttribute(&xml, "file");
                if (!value) {
                    status = CBS_BAD;
                    break;
                }
                basis = to_number(value);
                bases[cix->Count - 1] = basis;
                file->Basis = -2; /* resolved when every id is known. */
            }
        } else if (event == CBS_XML_CLOSE) {
            if (file && xml.Depth == file_depth)
                file = nullptr;
            if (file && !strcmp(xml.Name, "Source"))
                in_source = false;
        }
    }
    CbsXmlClose(&xml);
    if (status == CBS_OK) {
        uint32_t most = 0;
        for (unsigned i = 0; i < cix->Count; i++)
            if (ids[i] > most)
                most = ids[i];
        if (most > 1000000) {
            status = CBS_BAD;
        } else {
            cix->ByIdCount = most + 1;
            cix->ById = CbsAlloc(cix->ByIdCount * sizeof(*cix->ById));
            if (!cix->ById)
                status = CBS_MEMORY;
            else {
                for (unsigned i = 0; i < cix->ByIdCount; i++)
                    cix->ById[i] = -1;
                for (unsigned i = 0; i < cix->Count; i++)
                    cix->ById[ids[i]] = (int32_t)i;
                for (unsigned i = 0; i < cix->Count; i++) {
                    if (cix->Files[i].Basis == -2) {
                        uint32_t b = bases[i];
                        cix->Files[i].Basis = b < cix->ByIdCount ? cix->ById[b] : -1;
                        if (cix->Files[i].Basis < 0)
                            status = CBS_BAD;
                    }
                }
            }
        }
    }
    CbsFree(ids);
    CbsFree(bases);
    return status;
}

/* ------------------------------------------------------------------ the entries of the cabinet */

static void sort_entries(ENTRY *a, unsigned n)
{
    for (unsigned gap = n / 2; gap; gap /= 2)
        for (unsigned i = gap; i < n; i++) {
            ENTRY t = a[i];
            unsigned j = i;
            while (j >= gap && strcmp(a[j - gap].Name, t.Name) > 0) {
                a[j] = a[j - gap];
                j -= gap;
            }
            a[j] = t;
        }
}

static const ENTRY *find_entry(const ENTRY *entries, unsigned n, const char *name)
{
    unsigned lo = 0, hi = n;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        int c = strcmp(entries[mid].Name, name);
        if (!c)
            return &entries[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return nullptr;
}

typedef struct EXTRACT {
    const ENTRY *Entries;
    unsigned Count;
    const bool *Wanted; /* by index in the cabinet. */
    uint8_t **Data;
    size_t *Sizes;
    int Status;
} EXTRACT;

static void *extract_begin(void *context, const MIME_CAB_FILE *file)
{
    EXTRACT *e = context;
    if (e->Status != CBS_OK)
        return nullptr;
    const ENTRY *entry = find_entry(e->Entries, e->Count, file->Name);
    if (!entry || !e->Wanted[entry->Index])
        return nullptr;
    uint8_t *data = CbsAlloc(file->Size ? file->Size : 1);
    if (!data) {
        e->Status = CBS_MEMORY;
        return nullptr;
    }
    e->Data[entry->Index] = data;
    e->Sizes[entry->Index] = 0;
    return (void *)(uintptr_t)(entry->Index + 1);
}

static int extract_write(void *context, void *file_context, const uint8_t *data, size_t length)
{
    EXTRACT *e = context;
    unsigned index = (unsigned)(uintptr_t)file_context - 1;
    memcpy(e->Data[index] + e->Sizes[index], data, length);
    e->Sizes[index] += length;
    return 0;
}

static int extract_end(void *context, void *file_context, const MIME_CAB_FILE *file)
{
    (void)context;
    (void)file_context;
    (void)file;
    return 0;
}

/* ------------------------------------------------------------------ making the files */

typedef struct EXPAND {
    CIX Cix;
    ENTRY *Entries;
    unsigned EntryCount;
    uint8_t **EntryData;
    size_t *EntrySizes;
    bool *EntryWanted;
    const CBS_PROGRESS *Progress;
    unsigned Made, ToMake;
} EXPAND;

static bool names_equal_prefix(const char *name, const char *folder)
{
    size_t n = strlen(folder);
    return !_strnicmp(name, folder, n) && name[n] == '\\';
}

/* marks the entries that the file and the files it applies to need. */
static void mark_needed(EXPAND *x, unsigned index, unsigned depth)
{
    CIX_FILE *file = &x->Cix.Files[index];
    if (file->Entry >= 0 || depth > 8192)
        return;
    const ENTRY *entry = file->Source ? find_entry(x->Entries, x->EntryCount, file->Source) : nullptr;
    file->Entry = entry ? (int32_t)entry->Index : -3;
    if (entry)
        x->EntryWanted[entry->Index] = true;
    if (file->Basis >= 0)
        mark_needed(x, (unsigned)file->Basis, depth + 1);
}

static int make_file(EXPAND *x, unsigned index, unsigned depth)
{
    CIX_FILE *file = &x->Cix.Files[index];
    if (file->State == 1)
        return CBS_OK;
    if (file->State == 2)
        return file->Status;
    file->State = 2;
    file->Status = CBS_BAD;
    if (depth > 8192 || file->Entry < 0 || !x->EntryData[file->Entry] || !file->HasHash)
        return file->Status;
    const uint8_t *piece = x->EntryData[file->Entry];
    size_t piece_size = x->EntrySizes[file->Entry];
    uint8_t *data = nullptr;
    size_t size = 0;
    if (!file->Patch) {
        data = CbsAlloc(piece_size ? piece_size : 1);
        if (!data) {
            file->Status = CBS_MEMORY;
            return file->Status;
        }
        memcpy(data, piece, piece_size);
        size = piece_size;
    } else {
        const uint8_t *source = (const uint8_t *)"";
        size_t source_size = 0;
        if (file->Basis >= 0) {
            int status = make_file(x, (unsigned)file->Basis, depth + 1);
            if (status != CBS_OK) {
                file->Status = status;
                return status;
            }
            source = x->Cix.Files[file->Basis].Data;
            source_size = x->Cix.Files[file->Basis].Size;
        }
        uint8_t *made;
        int result = MsDeltaApply(source, source_size, piece, piece_size, &made, &size);
        if (result != MSDELTA_OK) {
            file->Status = result == MSDELTA_UNSUPPORTED ? CBS_UNSUPPORTED : result == MSDELTA_MEMORY ? CBS_MEMORY : CBS_CORRUPT;
            return file->Status;
        }
        data = CbsAlloc(size ? size : 1);
        if (!data) {
            MsDeltaFree(made);
            file->Status = CBS_MEMORY;
            return file->Status;
        }
        memcpy(data, made, size);
        MsDeltaFree(made);
    }
    uint8_t digest[32];
    mime_hash(&mime_sha256, data, size, digest);
    if (size != file->Length || memcmp(digest, file->Hash, 32)) {
        CbsFree(data);
        file->Status = CBS_CORRUPT;
        return file->Status;
    }
    file->Data = data;
    file->Size = size;
    file->State = 1;
    file->Status = CBS_OK;
    return CBS_OK;
}

/* ------------------------------------------------------------------ the plain cabinet */

typedef struct OUT_FILE {
    const char *Name;
    const uint8_t *Data;
    size_t Size;
} OUT_FILE;

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

/* a cabinet with one folder whose data is stored, as the reader of cabinets takes it. */
static int write_plain_cabinet(const OUT_FILE *files, unsigned count, uint8_t **out, size_t *out_size)
{
    uint64_t total = 0, names = 0;
    for (unsigned i = 0; i < count; i++) {
        total += files[i].Size;
        names += strlen(files[i].Name) + 1 + 16;
    }
    uint64_t blocks = (total + 32767) / 32768;
    uint64_t data_start = 36 + 8 + names, cab_size = data_start + blocks * 8 + total;
    if (cab_size > 0x7FFFFFFFu || blocks > 0xFFFF || count > 0xFFFF)
        return CBS_BAD;
    uint8_t *cab = CbsAllocZero((size_t)cab_size);
    if (!cab)
        return CBS_MEMORY;
    memcpy(cab, "MSCF", 4);
    put32(cab + 8, (uint32_t)cab_size);
    put32(cab + 16, 36 + 8);
    cab[24] = 3;
    cab[25] = 1;
    put16(cab + 26, 1);
    put16(cab + 28, count);
    put32(cab + 36, (uint32_t)data_start);
    put16(cab + 40, (uint32_t)blocks);
    uint8_t *p = cab + 44;
    uint32_t offset = 0;
    for (unsigned i = 0; i < count; i++) {
        size_t n = strlen(files[i].Name) + 1;
        put32(p, (uint32_t)files[i].Size);
        put32(p + 4, offset);
        put16(p + 14, 0x20);
        memcpy(p + 16, files[i].Name, n);
        p += 16 + n;
        offset += (uint32_t)files[i].Size;
    }
    /* the data of the files one after the other, cut into blocks. */
    uint8_t *cursor = p, *block = nullptr, *block_data = nullptr;
    size_t fill = 0;
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *source = files[i].Data;
        size_t left = files[i].Size;
        while (left) {
            if (!fill) {
                block = cursor;
                block_data = cursor + 8;
            }
            size_t take = 32768 - fill < left ? 32768 - fill : left;
            memcpy(block_data + fill, source, take);
            fill += take;
            source += take;
            left -= take;
            put16(block + 4, (uint32_t)fill);
            put16(block + 6, (uint32_t)fill);
            if (fill == 32768) {
                cursor = block_data + fill;
                fill = 0;
            }
        }
    }
    if (fill)
        cursor = block_data + fill;
    *out = cab;
    *out_size = (size_t)(cursor - cab);
    return CBS_OK;
}

/* ------------------------------------------------------------------ the whole */

static bool arch_wanted(CBS_ARCH native, const char *file_name)
{
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

static bool is_manifest(const char *name)
{
    size_t n = strlen(name);
    if (n < 10 || _stricmp(name + n - 9, ".manifest"))
        return false;
    return !strchr(name, '\\') && !strchr(name, '/');
}

static bool is_stub_name(const char *name)
{
    const char *p = strchr(name, '_');
    if (!p)
        return false;
    p++;
    for (int i = 0; i < 32; i++)
        if (hex_value(p[i]) < 0)
            return false;
    return p[32] == '_';
}

static int report(EXPAND *x)
{
    if (x->Progress && x->Progress->Report)
        return x->Progress->Report(x->Progress->Context, x->Made, x->ToMake);
    return 0;
}

static void release_expand(EXPAND *x)
{
    for (unsigned i = 0; i < x->Cix.Count; i++)
        CbsFree(x->Cix.Files[i].Data);
    CbsFree(x->Cix.Files);
    CbsFree(x->Cix.ById);
    CbsArenaDestroy(x->Cix.Arena);
    if (x->EntryData)
        for (unsigned i = 0; i < x->EntryCount; i++)
            CbsFree(x->EntryData[i]);
    CbsFree(x->EntryData);
    CbsFree(x->EntrySizes);
    CbsFree(x->EntryWanted);
    CbsFree(x->Entries);
}

int CbsExpandIndexedCabinet(const uint8_t *cabinet, size_t size, CBS_ARCH native, CBS_COMPONENT_FILTER filter,
                            void *context, const CBS_PROGRESS *progress, uint8_t **plain, size_t *plain_size)
{
    EXPAND x = {};
    x.Progress = progress;
    x.Cix.Arena = CbsArenaCreate();
    MIME_CAB *cab = nullptr;
    int status = CBS_MEMORY;
    char **folders = nullptr;
    unsigned folder_count = 0;
    OUT_FILE *out = nullptr;
    if (!x.Cix.Arena)
        return status;
    status = MimeCabOpen(&cab, cabinet, size, CbsAlloc, CbsFree);
    if (status != MIME_CAB_OK) {
        status = CBS_BAD;
        goto done;
    }
    MimeCabSequential(cab);
    x.EntryCount = MimeCabFileCount(cab);
    x.Entries = CbsAlloc((x.EntryCount + 1) * sizeof(ENTRY));
    x.EntryData = CbsAllocZero((x.EntryCount + 1) * sizeof(uint8_t *));
    x.EntrySizes = CbsAllocZero((x.EntryCount + 1) * sizeof(size_t));
    x.EntryWanted = CbsAllocZero((x.EntryCount + 1) * sizeof(bool));
    status = CBS_MEMORY;
    if (!x.Entries || !x.EntryData || !x.EntrySizes || !x.EntryWanted)
        goto done;
    for (unsigned i = 0; i < x.EntryCount; i++) {
        MIME_CAB_FILE f;
        MimeCabFile(cab, i, &f);
        x.Entries[i] = (ENTRY){f.Name, i, f.Size};
    }
    sort_entries(x.Entries, x.EntryCount);
    /* the index first. */
    const ENTRY *index_entry = find_entry(x.Entries, x.EntryCount, INDEX_NAME);
    status = CBS_NOT_FOUND;
    if (!index_entry)
        goto done;
    x.EntryWanted[index_entry->Index] = true;
    for (int pass = 0; pass < 2; pass++) {
        EXTRACT e = {x.Entries, x.EntryCount, x.EntryWanted, x.EntryData, x.EntrySizes, CBS_OK};
        MIME_CAB_FILE_SINK sink = {extract_begin, extract_write, extract_end};
        status = MimeCabExtract(cab, &sink, &e);
        if (status != MIME_CAB_OK) {
            status = status == MIME_CAB_MEMORY ? CBS_MEMORY : CBS_CORRUPT;
            goto done;
        }
        if (e.Status != CBS_OK) {
            status = e.Status;
            goto done;
        }
        if (pass == 1)
            break;
        status = parse_index(&x.Cix, (const char *)x.EntryData[index_entry->Index], x.EntrySizes[index_entry->Index]);
        if (status != CBS_OK)
            goto done;
        CbsFree(x.EntryData[index_entry->Index]);
        x.EntryData[index_entry->Index] = nullptr;
        x.EntryWanted[index_entry->Index] = false;
        /* the manifests of the components that are wanted, and what their files need. */
        folders = CbsAllocZero((x.EntryCount + 1) * sizeof(char *));
        status = CBS_MEMORY;
        if (!folders)
            goto done;
        for (unsigned i = 0; i < x.EntryCount; i++) {
            const char *name = x.Entries[i].Name;
            if (!is_manifest(name) || is_stub_name(name) || !arch_wanted(native, name) || !filter(context, name))
                continue;
            size_t n = strlen(name) - 9;
            folders[folder_count] = CbsArenaString(x.Cix.Arena, name, n);
            if (!folders[folder_count])
                goto done;
            folder_count++;
            x.EntryWanted[x.Entries[i].Index] = true;
        }
        for (unsigned i = 0; i < x.Cix.Count; i++) {
            CIX_FILE *file = &x.Cix.Files[i];
            const char *slash = strchr(file->Name, '\\');
            if (!slash)
                continue;
            bool wanted = false;
            for (unsigned f = 0; f < folder_count && !wanted; f++)
                wanted = names_equal_prefix(file->Name, folders[f]);
            if (wanted) {
                mark_needed(&x, i, 0);
                x.ToMake++;
            }
        }
        status = CBS_OK;
    }
    /* make the files, and drop the components that have a file that cannot be made. */
    for (unsigned i = 0; i < x.Cix.Count; i++) {
        CIX_FILE *file = &x.Cix.Files[i];
        const char *slash = strchr(file->Name, '\\');
        if (!slash)
            continue;
        unsigned owner = folder_count;
        for (unsigned f = 0; f < folder_count; f++)
            if (names_equal_prefix(file->Name, folders[f])) {
                owner = f;
                break;
            }
        if (owner == folder_count)
            continue;
        int made = make_file(&x, i, 0);
        x.Made++;
        if (report(&x)) {
            status = CBS_STOPPED;
            goto done;
        }
        if (made == CBS_MEMORY) {
            status = made;
            goto done;
        }
        /* a file that cannot be made is left out, the installation skips what the cabinet lacks. */
        (void)owner;
    }
    {
        unsigned out_count = 0;
        out = CbsAllocZero((x.EntryCount + x.Cix.Count + 1) * sizeof(OUT_FILE));
        status = CBS_MEMORY;
        if (!out)
            goto done;
        for (unsigned i = 0; i < x.EntryCount; i++) {
            const char *name = x.Entries[i].Name;
            if (!x.EntryWanted[x.Entries[i].Index] || !is_manifest(name))
                continue;
            unsigned owner = 0;
            while (owner < folder_count && _strnicmp(name, folders[owner], strlen(folders[owner])))
                owner++;
            if (owner < folder_count)
                out[out_count++] = (OUT_FILE){name, x.EntryData[x.Entries[i].Index], x.EntrySizes[x.Entries[i].Index]};
        }
        unsigned manifests = out_count;
        for (unsigned i = 0; i < x.Cix.Count; i++) {
            CIX_FILE *file = &x.Cix.Files[i];
            if (!file->Data)
                continue;
            unsigned owner = 0;
            while (owner < folder_count && !names_equal_prefix(file->Name, folders[owner]))
                owner++;
            if (owner < folder_count)
                out[out_count++] = (OUT_FILE){file->Name, file->Data, file->Size};
        }
        status = manifests ? write_plain_cabinet(out, out_count, plain, plain_size) : CBS_NOT_FOUND;
    }
done:
    CbsFree(out);
    CbsFree(folders);
    MimeCabClose(cab);
    release_expand(&x);
    return status;
}
