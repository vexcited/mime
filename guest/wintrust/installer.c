#include "trust_private.h"
#include "../lib/compound/compound.h"

/*
 * the signature of a windows installer package (or of a patch), which is a compound file. It sits in the stream named
 * with the character 5 and DigitalSignature, a signed data structure whose content names the package format with a
 * sip identifier and carries the digest of the package. The digest is made of the bytes of the streams in order:
 * sorted by the bytes of their names as the directory keeps them, a shorter name before a longer one that starts with
 * it, the signature streams left out; a storage is the bytes of its own children and then its class id, and the bytes
 * of the root end with its class id. A package that has the extended signature (the stream whose name has the
 * character 5 and MsiDigitalSignatureEx) begins with the bytes of that stream. The extended signature holds a digest of the
 * names and sizes of the streams, which is not computed here: only that it is part of what was signed is checked.
 */
#define MAX_ENTRIES 65536
#define SIGNATURE_NAME L"\x05" L"DigitalSignature"
#define EXTENDED_NAME L"\x05" L"MsiDigitalSignatureEx"

typedef struct {
    const MIME_CFB *File;
    BYTE *Out;
    DWORD Size, Capacity;
    BOOL Failed;
} COLLECT;

static const BYTE *raw_entry(const MIME_CFB *file, uint32_t index)
{
    return index < file->DirectorySize / 128 ? file->Directory + (size_t)index * 128 : nullptr;
}

static void append(COLLECT *c, const void *data, DWORD size)
{
    if (c->Failed || size > c->Capacity - c->Size) {
        c->Failed = TRUE;
        return;
    }
    memcpy(c->Out + c->Size, data, size);
    c->Size += size;
}

/* the names as the directory holds them, the first bytes that differ decide, else the shorter name goes first. */
static int compare(const void *first, const void *second)
{
    const BYTE *a = *(const BYTE *const *)first, *b = *(const BYTE *const *)second;
    DWORD la = a[64] | (DWORD)a[65] << 8, lb = b[64] | (DWORD)b[65] << 8;
    DWORD n = la < lb ? la : lb;
    int c = memcmp(a, b, n);
    return c ? c : (la > lb) - (la < lb);
}

/* a shell sort, as the lists are short and the sort needs nothing of the C library. */
static void sort(const BYTE **list, DWORD count)
{
    for (DWORD gap = count / 2; gap; gap /= 2) {
        for (DWORD i = gap; i < count; i++) {
            const BYTE *value = list[i];
            DWORD j = i;
            for (; j >= gap && compare(&list[j - gap], &value) > 0; j -= gap)
                list[j] = list[j - gap];
            list[j] = value;
        }
    }
}

static BOOL name_is(const BYTE *entry, PCWSTR name)
{
    DWORD length = entry[64] | (DWORD)entry[65] << 8;
    SIZE_T chars = wcslen(name);
    return length == (chars + 1) * 2 && !memcmp(entry, name, chars * 2);
}

/* the entries of a storage by the order of the tree, which the sort then replaces. */
static BOOL children(const MIME_CFB *file, uint32_t first, const BYTE **list, DWORD *count)
{
    uint32_t stack[64];
    DWORD depth = 0;
    uint32_t at = first;
    *count = 0;
    while (at != 0xFFFFFFFF || depth) {
        while (at != 0xFFFFFFFF) {
            if (at >= file->DirectorySize / 128 || depth == RTL_NUMBER_OF(stack))
                return FALSE;
            stack[depth++] = at;
            at = *(const DWORD *)(raw_entry(file, at) + 68);
        }
        at = stack[--depth];
        if (*count >= MAX_ENTRIES)
            return FALSE;
        list[(*count)++] = raw_entry(file, at);
        at = *(const DWORD *)(raw_entry(file, at) + 72);
    }
    return TRUE;
}

static BOOL walk(COLLECT *c, uint32_t storage, BOOL root)
{
    const MIME_CFB *file = c->File;
    const BYTE *entry = raw_entry(file, storage);
    const BYTE **list = LocalAlloc(LMEM_FIXED, sizeof(*list) * MAX_ENTRIES);
    DWORD count = 0, kept = 0;
    if (!list)
        return FALSE;
    BOOL ok = children(file, *(const DWORD *)(entry + 76), list, &count);
    for (DWORD i = 0; ok && i < count; i++)
        if (!root || (!name_is(list[i], SIGNATURE_NAME) && !name_is(list[i], EXTENDED_NAME)))
            list[kept++] = list[i];
    if (ok)
        sort(list, kept);
    for (DWORD i = 0; ok && i < kept; i++) {
        uint32_t index = (uint32_t)((list[i] - file->Directory) / 128);
        MIME_CFB_ENTRY item;
        if (!MimeCfbEntry(file, index, &item)) {
            ok = FALSE;
        } else if (list[i][66] == 2) {
            if (item.Size > c->Capacity - c->Size) {
                ok = FALSE;
            } else if (!MimeCfbRead(file, &item, c->Out + c->Size, (size_t)item.Size)) {
                ok = FALSE;
            } else {
                c->Size += (DWORD)item.Size;
            }
        } else if (list[i][66] == 1) {
            ok = walk(c, index, FALSE);
        }
    }
    append(c, entry + 80, 16);
    LocalFree(list);
    return ok && !c->Failed;
}

static void *cfb_allocate(size_t size)
{
    return LocalAlloc(LMEM_FIXED, size);
}

static void cfb_release(void *memory)
{
    LocalFree(memory);
}

LONG WtVerifyInstaller(const BYTE *bytes, DWORD size, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain)
{
    MIME_CFB file;
    if (!MimeCfbOpen(&file, bytes, size, cfb_allocate, cfb_release))
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    LONG result = TRUST_E_NOSIGNATURE;
    BYTE *input = nullptr;
    const BYTE *signature = nullptr, *extended = nullptr;
    const BYTE **list = LocalAlloc(LMEM_FIXED, sizeof(*list) * MAX_ENTRIES);
    DWORD count = 0;
    if (!list) {
        MimeCfbClose(&file);
        return E_OUTOFMEMORY;
    }
    if (!children(&file, *(const DWORD *)(raw_entry(&file, 0) + 76), list, &count)) {
        LocalFree(list);
        MimeCfbClose(&file);
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    }
    for (DWORD i = 0; i < count; i++) {
        if (name_is(list[i], SIGNATURE_NAME))
            signature = list[i];
        else if (name_is(list[i], EXTENDED_NAME))
            extended = list[i];
    }
    if (signature) {
        MIME_CFB_ENTRY item, ex_item = {};
        BYTE *sig = nullptr;
        result = TRUST_E_SUBJECT_FORM_UNKNOWN;
        if (MimeCfbEntry(&file, (uint32_t)((signature - file.Directory) / 128), &item) && item.Size &&
            item.Size < 1024 * 1024 && (sig = LocalAlloc(LMEM_FIXED, (SIZE_T)item.Size)) &&
            MimeCfbRead(&file, &item, sig, (size_t)item.Size) &&
            (!extended || MimeCfbEntry(&file, (uint32_t)((extended - file.Directory) / 128), &ex_item))) {
            COLLECT collect = {&file, nullptr, 0, size};
            /* the streams cannot be longer than the file. */
            collect.Capacity = size + (DWORD)ex_item.Size + 4096;
            input = LocalAlloc(LMEM_FIXED, collect.Capacity);
            collect.Out = input;
            BOOL ok = input != nullptr;
            if (ok && extended && ex_item.Size) {
                ok = ex_item.Size < collect.Capacity &&
                     MimeCfbRead(&file, &ex_item, collect.Out, (size_t)ex_item.Size);
                collect.Size = (DWORD)ex_item.Size;
            }
            if (ok && walk(&collect, 0, TRUE)) {
                WT_SUBJECT subject = {.digest_input = input, .digest_size = collect.Size, .installer = TRUE};
                result = WtVerifySignedData((WT_BYTES){sig, (DWORD)item.Size}, &subject, options, chain);
            }
        }
        LocalFree(sig);
        LocalFree(input);
    }
    LocalFree(list);
    MimeCfbClose(&file);
    return result;
}
