/* component manifests: the identity of a component, its files, links, directories and registry values. */
#include "cbs.h"
#ifdef _WIN32
#include <rtl.h>
#else
#include <string.h>
#include <strings.h>
#define _stricmp strcasecmp
#endif

/* ------------------------------------------------------------------ arena */

#define ARENA_CHUNK (256u * 1024u)

typedef struct ARENA_BLOCK {
    struct ARENA_BLOCK *Next;
    size_t Used, Size;
} ARENA_BLOCK;

struct CBS_ARENA {
    ARENA_BLOCK *Head;
};

CBS_ARENA *CbsArenaCreate(void)
{
    return CbsAllocZero(sizeof(CBS_ARENA));
}

void CbsArenaDestroy(CBS_ARENA *arena)
{
    if (!arena)
        return;
    for (ARENA_BLOCK *block = arena->Head; block;) {
        ARENA_BLOCK *next = block->Next;
        CbsFree(block);
        block = next;
    }
    CbsFree(arena);
}

void *CbsArenaAlloc(CBS_ARENA *arena, size_t size)
{
    size = (size + 15) & ~(size_t)15;
    ARENA_BLOCK *block = arena->Head;
    if (!block || block->Size - block->Used < size) {
        size_t chunk = size + sizeof(ARENA_BLOCK) > ARENA_CHUNK ? size + sizeof(ARENA_BLOCK) : ARENA_CHUNK;
        block = CbsAlloc(chunk);
        if (!block)
            return nullptr;
        block->Next = arena->Head;
        block->Used = (sizeof(ARENA_BLOCK) + 15) & ~(size_t)15;
        block->Size = chunk;
        arena->Head = block;
    }
    void *p = (uint8_t *)block + block->Used;
    block->Used += size;
    return p;
}

char *CbsArenaString(CBS_ARENA *arena, const char *text, size_t length)
{
    char *copy = CbsArenaAlloc(arena, length + 1);
    if (copy) {
        memcpy(copy, text, length);
        copy[length] = 0;
    }
    return copy;
}

/* ------------------------------------------------------------------ vectors in the arena */

typedef struct VECTOR {
    void *Items;
    unsigned Count, Capacity;
} VECTOR;

static void *push(CBS_ARENA *arena, VECTOR *v, size_t size)
{
    if (v->Count == v->Capacity) {
        unsigned capacity = v->Capacity ? v->Capacity * 2 : 8;
        void *items = CbsArenaAlloc(arena, (size_t)capacity * size);
        if (!items)
            return nullptr;
        if (v->Count)
            memcpy(items, v->Items, (size_t)v->Count * size);
        v->Items = items;
        v->Capacity = capacity;
    }
    void *item = (uint8_t *)v->Items + (size_t)v->Count * size;
    memset(item, 0, size);
    v->Count++;
    return item;
}

/* ------------------------------------------------------------------ parsing */

static CBS_ARCH parse_arch(const char *text)
{
    if (!text)
        return CBS_ARCH_NEUTRAL;
    if (!_stricmp(text, "x86"))
        return CBS_ARCH_X86;
    if (!_stricmp(text, "amd64"))
        return CBS_ARCH_AMD64;
    if (!_stricmp(text, "arm64"))
        return CBS_ARCH_ARM64;
    if (!_stricmp(text, "wow64"))
        return CBS_ARCH_WOW64;
    return CBS_ARCH_NEUTRAL;
}

static int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

/* a base64 text of exactly 32 bytes. */
static bool parse_digest(const char *text, uint8_t out[32])
{
    size_t n = strlen(text);
    if (n != 44 || text[43] != '=' || text[42] == '=')
        return false;
    uint8_t *o = out;
    for (size_t i = 0; i < 44; i += 4) {
        int a = base64_value(text[i]), b = base64_value(text[i + 1]), c = base64_value(text[i + 2]);
        int d = text[i + 3] == '=' ? 0 : base64_value(text[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0)
            return false;
        uint32_t v = (uint32_t)a << 18 | (uint32_t)b << 12 | (uint32_t)c << 6 | (uint32_t)d;
        *o++ = (uint8_t)(v >> 16);
        if (i + 4 < 44) {
            *o++ = (uint8_t)(v >> 8);
            *o++ = (uint8_t)v;
        } else
            *o++ = (uint8_t)(v >> 8);
    }
    return true;
}

static CBS_REG_TYPE parse_reg_type(const char *text)
{
    if (!text)
        return CBS_REG_SZ;
    if (!_stricmp(text, "REG_DWORD"))
        return CBS_REG_DWORD;
    if (!_stricmp(text, "REG_BINARY"))
        return CBS_REG_BINARY;
    if (!_stricmp(text, "REG_MULTI_SZ"))
        return CBS_REG_MULTI_SZ;
    if (!_stricmp(text, "REG_EXPAND_SZ"))
        return CBS_REG_EXPAND_SZ;
    return CBS_REG_SZ;
}

static const char *copy_attribute(CBS_ARENA *arena, const CBS_XML *xml, const char *name)
{
    const char *value = CbsXmlAttribute(xml, name);
    return value ? CbsArenaString(arena, value, strlen(value)) : nullptr;
}

static void read_identity(CBS_ARENA *arena, const CBS_XML *xml, CBS_ID *id)
{
    id->Name = copy_attribute(arena, xml, "name");
    id->Version = copy_attribute(arena, xml, "version");
    id->Token = copy_attribute(arena, xml, "publicKeyToken");
    id->Arch = parse_arch(CbsXmlAttribute(xml, "processorArchitecture"));
}

#define MAX_STACK 16

int CbsManifestParse(CBS_ARENA *arena, const char *file_name, const char *text, size_t length,
                     CBS_COMPONENT **out)
{
    *out = nullptr;
    CBS_COMPONENT *component = CbsArenaAlloc(arena, sizeof(*component));
    if (!component)
        return CBS_MEMORY;
    memset(component, 0, sizeof(*component));
    size_t folder_length = strlen(file_name);
    if (folder_length > 9 && !_stricmp(file_name + folder_length - 9, ".manifest"))
        folder_length -= 9;
    component->Folder = CbsArenaString(arena, file_name, folder_length);

    VECTOR dependencies = {}, files = {}, links = {}, directories = {}, keys = {}, values = {};
    CBS_XML xml;
    CbsXmlOpen(&xml, text, length);
    char stack[MAX_STACK][40];
    CBS_FILE_ENTRY *file = nullptr;
    CBS_REG_KEY *key = nullptr;
    bool sha256 = false, digest_value = false;
    int status = CBS_OK;
    bool root_seen = false;
    for (;;) {
        CBS_XML_EVENT event = CbsXmlNext(&xml);
        if (event == CBS_XML_END)
            break;
        if (event == CBS_XML_ERROR) {
            status = CBS_BAD;
            break;
        }
        unsigned depth = xml.Depth;
        if (event == CBS_XML_CLOSE) {
            if (depth == 2) {
                file = nullptr;
                key = nullptr;
            }
            digest_value = false;
            continue;
        }
        if (event == CBS_XML_TEXT) {
            if (digest_value && file && sha256)
                file->HasDigest = parse_digest(xml.Text, file->Digest);
            continue;
        }
        if (depth <= MAX_STACK) {
            size_t n = strlen(xml.Name);
            if (n > sizeof(stack[0]) - 1)
                n = sizeof(stack[0]) - 1;
            memcpy(stack[depth - 1], xml.Name, n);
            stack[depth - 1][n] = 0;
        }
        if (depth == 1) {
            if (strcmp(xml.Name, "assembly")) {
                status = CBS_BAD;
                break;
            }
            root_seen = true;
            continue;
        }
        if (depth > MAX_STACK)
            continue;
        const char *parent = stack[depth - 2];
        const char *name = stack[depth - 1];
        if (depth == 2 && !strcmp(name, "assemblyIdentity")) {
            read_identity(arena, &xml, &component->Id);
        } else if (depth == 4 && !strcmp(name, "assemblyIdentity") && !strcmp(parent, "dependentAssembly")) {
            CBS_ID *id = push(arena, &dependencies, sizeof(CBS_ID));
            if (!id) {
                status = CBS_MEMORY;
                break;
            }
            read_identity(arena, &xml, id);
        } else if (depth == 2 && !strcmp(name, "file")) {
            file = push(arena, &files, sizeof(CBS_FILE_ENTRY));
            if (!file) {
                status = CBS_MEMORY;
                break;
            }
            file->Name = copy_attribute(arena, &xml, "name");
            file->Destination = copy_attribute(arena, &xml, "destinationPath");
            sha256 = false;
            file->FirstLink = links.Count;
            if (!file->Name || !file->Destination) {
                status = CBS_BAD;
                break;
            }
        } else if (depth == 4 && file && !strcmp(parent, "hash") && !strcmp(name, "DigestMethod")) {
            const char *algorithm = CbsXmlAttribute(&xml, "Algorithm");
            size_t n = algorithm ? strlen(algorithm) : 0;
            sha256 = n >= 6 && !_stricmp(algorithm + n - 6, "sha256");
        } else if (depth == 4 && file && !strcmp(parent, "hash") && !strcmp(name, "DigestValue")) {
            digest_value = true;
        } else if (depth == 3 && file && !strcmp(parent, "file") && !strcmp(name, "link")) {
            const char **link = push(arena, &links, sizeof(char *));
            const char *destination = CbsXmlAttribute(&xml, "destination");
            if (!link || !destination) {
                status = link ? CBS_BAD : CBS_MEMORY;
                break;
            }
            *link = CbsArenaString(arena, destination, strlen(destination));
            file->LinkCount++;
        } else if (depth == 3 && file && !strcmp(parent, "file") && !strcmp(name, "gac")) {
            const char *arch = CbsXmlAttribute(&xml, "clrarch");
            const char *copy = CbsXmlAttribute(&xml, "fileCopy");
            file->Gac = !arch                 ? CBS_GAC_NONE
                        : !_stricmp(arch, "msil")  ? CBS_GAC_MSIL
                        : !_stricmp(arch, "x86")   ? CBS_GAC_X86
                        : !_stricmp(arch, "amd64") ? CBS_GAC_AMD64
                        : !_stricmp(arch, "arm64") ? CBS_GAC_ARM64
                                                   : CBS_GAC_NONE;
            file->GacCopy = !copy || _stricmp(copy, "false");
            file->GacVersion = copy_attribute(arena, &xml, "managedversion");
        } else if (depth == 3 && !strcmp(parent, "directories") && !strcmp(name, "directory")) {
            const char **directory = push(arena, &directories, sizeof(char *));
            const char *path = CbsXmlAttribute(&xml, "destinationPath");
            if (!directory || !path) {
                status = directory ? CBS_BAD : CBS_MEMORY;
                break;
            }
            *directory = CbsArenaString(arena, path, strlen(path));
        } else if (depth == 3 && !strcmp(parent, "registryKeys") && !strcmp(name, "registryKey")) {
            key = push(arena, &keys, sizeof(CBS_REG_KEY));
            const char *key_name = CbsXmlAttribute(&xml, "keyName");
            if (!key || !key_name) {
                status = key ? CBS_BAD : CBS_MEMORY;
                break;
            }
            key->Name = CbsArenaString(arena, key_name, strlen(key_name));
            key->FirstValue = values.Count;
        } else if (depth == 4 && key && !strcmp(parent, "registryKey") && !strcmp(name, "registryValue")) {
            CBS_REG_VALUE *value = push(arena, &values, sizeof(CBS_REG_VALUE));
            if (!value) {
                status = CBS_MEMORY;
                break;
            }
            value->Name = copy_attribute(arena, &xml, "name");
            value->Data = copy_attribute(arena, &xml, "value");
            value->Type = parse_reg_type(CbsXmlAttribute(&xml, "valueType"));
            key->ValueCount++;
        }
    }
    CbsXmlClose(&xml);
    if (status == CBS_OK && (!root_seen || !component->Id.Name))
        status = CBS_BAD;
    if (status != CBS_OK)
        return status;
    component->Dependencies = dependencies.Items;
    component->DependencyCount = dependencies.Count;
    component->Files = files.Items;
    component->FileCount = files.Count;
    component->Links = links.Items;
    component->LinkCount = links.Count;
    component->Directories = directories.Items;
    component->DirectoryCount = directories.Count;
    component->Keys = keys.Items;
    component->KeyCount = keys.Count;
    component->Values = values.Items;
    component->ValueCount = values.Count;
    *out = component;
    return CBS_OK;
}
