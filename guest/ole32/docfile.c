/*
 * structured storage: compound files made and read by StgCreateDocfile, StgOpenStorage and their forms over a byte
 * array. The file is read whole into a tree of storages and streams in memory, the elements work on the tree, and
 * the tree is written out again whole as a version 3 compound file with 512 byte sectors when it is committed, and
 * when the last of the objects of a direct mode file goes. Files of version 4 (4096 byte sectors) are read too.
 *
 * Mime's choices, where nothing says otherwise: a stream or storage opened for a transaction works on a copy of
 * its element that a commit puts back; a transacted root is written only by its own commit; the file is rewritten
 * as a whole each time; locks of regions are refused as the compound file of Windows refuses them.
 */
#include "ole32_private.h"
#include <mime/win32/objidl.h>
#include <mime/win32/winnls.h>

#ifndef STG_E_PATHNOTFOUND
#define STG_E_PATHNOTFOUND ((HRESULT)0x80030003L)
#endif
#ifndef STG_E_TOOMANYOPENFILES
#define STG_E_TOOMANYOPENFILES ((HRESULT)0x80030004L)
#endif
#ifndef STG_E_INSUFFICIENTMEMORY
#define STG_E_INSUFFICIENTMEMORY ((HRESULT)0x80030008L)
#endif
#ifndef STG_E_FILEALREADYEXISTS
#define STG_E_FILEALREADYEXISTS ((HRESULT)0x80030050L)
#endif
#ifndef STG_E_INVALIDFLAG
#define STG_E_INVALIDFLAG ((HRESULT)0x800300FFL)
#endif
#ifndef STG_E_LOCKVIOLATION
#define STG_E_LOCKVIOLATION ((HRESULT)0x80030021L)
#endif
#ifndef STG_E_REVERTED
#define STG_E_REVERTED ((HRESULT)0x80030102L)
#endif
#ifndef STG_E_DOCFILECORRUPT
#define STG_E_DOCFILECORRUPT ((HRESULT)0x80030109L)
#endif
#define STGMOVE_MOVE 0
#define STGMOVE_COPY 1
#define STGM_ACCESS_MASK 3
#define STGM_SHARE_MASK 0x70

#define FREESECT 0xFFFFFFFFu
#define ENDOFCHAIN 0xFFFFFFFEu
#define FATSECT 0xFFFFFFFDu
#define DIFSECT 0xFFFFFFFCu
#define NOSTREAM 0xFFFFFFFFu
#define MINI_CUTOFF 4096u
#define MINI_SECTOR 64u
#define NAME_CHARS 31
#define HEADER_DIFAT 109

static const BYTE g_signature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

/* ------------------------------------------------------------------ the tree */

typedef struct NODE NODE;
struct NODE {
    LONG Refs;
    WCHAR Name[NAME_CHARS + 1];
    BYTE Type;
    CLSID Clsid;
    DWORD State;
    FILETIME Created, Modified;
    /* the storage this is an element of, nullptr for the root and for what has been destroyed. */
    NODE *Parent;
    BOOL Detached;
    NODE **Children;
    ULONG Count, Capacity;
    BYTE *Data;
    ULONGLONG Size, Capacity_;
};

static void node_addref(NODE *node)
{
    InterlockedIncrement(&node->Refs);
}

static void node_release(NODE *node)
{
    if (!node || InterlockedDecrement(&node->Refs))
        return;
    for (ULONG i = 0; i < node->Count; i++) {
        node->Children[i]->Parent = nullptr;
        node_release(node->Children[i]);
    }
    OleFree(node->Children);
    OleFree(node->Data);
    OleFree(node);
}

static void set_name(WCHAR *to, PCWSTR from)
{
    SIZE_T n = 0;
    while (from[n] && n < NAME_CHARS) {
        to[n] = from[n];
        n++;
    }
    to[n] = 0;
}

static void now(FILETIME *time)
{
    GetSystemTimeAsFileTime(time);
}

static NODE *node_new(PCWSTR name, BYTE type)
{
    NODE *node = OleAllocZero(sizeof(*node));
    if (!node)
        return nullptr;
    node->Refs = 1;
    node->Type = type;
    if (name)
        set_name(node->Name, name);
    if (type != STGTY_STREAM) {
        now(&node->Created);
        node->Modified = node->Created;
    }
    return node;
}

/* the order of the elements of a storage: the shorter name first, then by upper case code units. */
static int name_compare(PCWSTR a, PCWSTR b)
{
    int la = (int)wcslen(a), lb = (int)wcslen(b);
    if (la != lb)
        return la < lb ? -1 : 1;
    int r = CompareStringOrdinal(a, la, b, lb, TRUE);
    return r == CSTR_LESS_THAN ? -1 : r == CSTR_GREATER_THAN ? 1 : 0;
}

static ULONG child_position(NODE *storage, PCWSTR name, BOOL *found)
{
    ULONG low = 0, high = storage->Count;
    while (low < high) {
        ULONG middle = (low + high) / 2;
        int c = name_compare(storage->Children[middle]->Name, name);
        if (!c) {
            *found = TRUE;
            return middle;
        }
        if (c < 0)
            low = middle + 1;
        else
            high = middle;
    }
    *found = FALSE;
    return low;
}

static NODE *child_find(NODE *storage, PCWSTR name)
{
    BOOL found;
    ULONG at = child_position(storage, name, &found);
    return found ? storage->Children[at] : nullptr;
}

/* the child is put in its place and the tree owns the reference given. */
static HRESULT child_add(NODE *storage, NODE *child)
{
    BOOL found;
    ULONG at = child_position(storage, child->Name, &found);
    if (found)
        return STG_E_FILEALREADYEXISTS;
    if (storage->Count == storage->Capacity) {
        ULONG capacity = storage->Capacity ? storage->Capacity * 2 : 8;
        NODE **bigger = OleAlloc(capacity * sizeof(*bigger));
        if (!bigger)
            return STG_E_INSUFFICIENTMEMORY;
        if (storage->Count)
            memcpy(bigger, storage->Children, storage->Count * sizeof(*bigger));
        OleFree(storage->Children);
        storage->Children = bigger;
        storage->Capacity = capacity;
    }
    memmove(storage->Children + at + 1, storage->Children + at, (storage->Count - at) * sizeof(*storage->Children));
    storage->Children[at] = child;
    storage->Count++;
    child->Parent = storage;
    return S_OK;
}

/* what is below a destroyed element reads as reverted. */
static void detach(NODE *node)
{
    node->Detached = TRUE;
    node->Parent = nullptr;
    for (ULONG i = 0; i < node->Count; i++)
        detach(node->Children[i]);
}

static void child_remove(NODE *storage, NODE *child)
{
    for (ULONG i = 0; i < storage->Count; i++) {
        if (storage->Children[i] == child) {
            memmove(storage->Children + i, storage->Children + i + 1, (storage->Count - i - 1) * sizeof(*storage->Children));
            storage->Count--;
            break;
        }
    }
    detach(child);
    node_release(child);
}

static HRESULT data_resize(NODE *node, ULONGLONG size)
{
    if (size > 0x7FFFFFFFull * 4)
        return STG_E_MEDIUMFULL;
    if (size > node->Capacity_) {
        ULONGLONG capacity = node->Capacity_ ? node->Capacity_ * 2 : 256;
        while (capacity < size)
            capacity *= 2;
        BYTE *bigger = OleAlloc((SIZE_T)capacity);
        if (!bigger)
            return STG_E_MEDIUMFULL;
        if (node->Size)
            memcpy(bigger, node->Data, (SIZE_T)node->Size);
        OleFree(node->Data);
        node->Data = bigger;
        node->Capacity_ = capacity;
    }
    if (size > node->Size)
        memset(node->Data + node->Size, 0, (SIZE_T)(size - node->Size));
    node->Size = size;
    return S_OK;
}

static NODE *node_copy(const NODE *source)
{
    NODE *node = node_new(source->Name, source->Type);
    if (!node)
        return nullptr;
    node->Clsid = source->Clsid;
    node->State = source->State;
    node->Created = source->Created;
    node->Modified = source->Modified;
    if (source->Type == STGTY_STREAM) {
        if (FAILED(data_resize(node, source->Size))) {
            node_release(node);
            return nullptr;
        }
        if (source->Size)
            memcpy(node->Data, source->Data, (SIZE_T)source->Size);
        return node;
    }
    for (ULONG i = 0; i < source->Count; i++) {
        NODE *child = node_copy(source->Children[i]);
        if (!child || FAILED(child_add(node, child))) {
            node_release(child);
            node_release(node);
            return nullptr;
        }
    }
    return node;
}

/* the contents of a storage or stream taken from another node of the same kind, which is left empty. */
static void node_take(NODE *to, NODE *from)
{
    for (ULONG i = 0; i < to->Count; i++) {
        detach(to->Children[i]);
        node_release(to->Children[i]);
    }
    OleFree(to->Children);
    OleFree(to->Data);
    to->Children = from->Children;
    to->Count = from->Count;
    to->Capacity = from->Capacity;
    to->Data = from->Data;
    to->Size = from->Size;
    to->Capacity_ = from->Capacity_;
    to->Clsid = from->Clsid;
    to->State = from->State;
    to->Modified = from->Modified;
    from->Children = nullptr;
    from->Count = from->Capacity = 0;
    from->Data = nullptr;
    from->Size = from->Capacity_ = 0;
    for (ULONG i = 0; i < to->Count; i++)
        to->Children[i]->Parent = to;
}

/* ------------------------------------------------------------------ reading a compound file */

typedef struct {
    const BYTE *Image;
    ULONGLONG Length;
    ULONG SectorShift, SectorSize;
    ULONG *Fat;
    ULONG FatCount;
    ULONG *MiniFat;
    ULONG MiniFatCount;
    BYTE *Mini;
    ULONGLONG MiniSize;
    ULONG MiniCutoff;
} READER;

static ULONG le32(const BYTE *p)
{
    return (ULONG)p[0] | (ULONG)p[1] << 8 | (ULONG)p[2] << 16 | (ULONG)p[3] << 24;
}

static ULONGLONG le64(const BYTE *p)
{
    return (ULONGLONG)le32(p) | (ULONGLONG)le32(p + 4) << 32;
}

static USHORT le16(const BYTE *p)
{
    return (USHORT)(p[0] | p[1] << 8);
}

static const BYTE *sector_at(const READER *r, ULONG sector)
{
    ULONGLONG offset = ((ULONGLONG)sector + 1) << r->SectorShift;
    if (offset + r->SectorSize > r->Length)
        return nullptr;
    return r->Image + offset;
}

/* the bytes of a chain, up to the size when it is given and as long as the chain goes when it is not. */
static HRESULT read_chain(const READER *r, ULONG first, ULONGLONG size, BOOL sized, BYTE **out, ULONGLONG *out_size)
{
    ULONGLONG sectors = 0;
    ULONG s = first;
    while (s < 0xFFFFFFFAu) {
        if (s >= r->FatCount || ++sectors > r->FatCount)
            return STG_E_DOCFILECORRUPT;
        s = r->Fat[s];
    }
    if (s != ENDOFCHAIN && s != FREESECT)
        return STG_E_DOCFILECORRUPT;
    ULONGLONG have = sectors * r->SectorSize;
    if (sized) {
        if (size > have)
            return STG_E_DOCFILECORRUPT;
        have = size;
    }
    BYTE *buffer = OleAlloc(have ? (SIZE_T)have : 1);
    if (!buffer)
        return STG_E_INSUFFICIENTMEMORY;
    ULONGLONG done = 0;
    for (s = first; done < have; s = r->Fat[s]) {
        const BYTE *p = sector_at(r, s);
        if (!p) {
            OleFree(buffer);
            return STG_E_DOCFILECORRUPT;
        }
        ULONGLONG n = have - done < r->SectorSize ? have - done : r->SectorSize;
        memcpy(buffer + done, p, (SIZE_T)n);
        done += n;
    }
    *out = buffer;
    *out_size = have;
    return S_OK;
}

static HRESULT read_mini_chain(const READER *r, ULONG first, ULONGLONG size, BYTE **out)
{
    BYTE *buffer = OleAlloc(size ? (SIZE_T)size : 1);
    if (!buffer)
        return STG_E_INSUFFICIENTMEMORY;
    ULONGLONG done = 0;
    ULONG s = first;
    while (done < size) {
        if (s >= r->MiniFatCount || (ULONGLONG)(s + 1) * MINI_SECTOR > r->MiniSize) {
            OleFree(buffer);
            return STG_E_DOCFILECORRUPT;
        }
        ULONGLONG n = size - done < MINI_SECTOR ? size - done : MINI_SECTOR;
        memcpy(buffer + done, r->Mini + (ULONGLONG)s * MINI_SECTOR, (SIZE_T)n);
        done += n;
        s = r->MiniFat[s];
    }
    *out = buffer;
    return S_OK;
}

typedef struct {
    const BYTE *Entries;
    ULONG Count;
    BYTE *Seen;
} DIRECTORY;

static HRESULT build_children(READER *r, DIRECTORY *d, NODE *parent, ULONG id, ULONG depth)
{
    /* the siblings of a storage are a tree, walked in order with the left subtree first. */
    if (id == NOSTREAM)
        return S_OK;
    if (id >= d->Count || d->Seen[id] || depth > 4096)
        return STG_E_DOCFILECORRUPT;
    d->Seen[id] = 1;
    const BYTE *e = d->Entries + (SIZE_T)id * 128;
    HRESULT hr = build_children(r, d, parent, le32(e + 68), depth + 1);
    if (FAILED(hr))
        return hr;
    BYTE type = e[66];
    USHORT name_bytes = le16(e + 64);
    if ((type != STGTY_STORAGE && type != STGTY_STREAM) || name_bytes < 2 || name_bytes > 64 || (name_bytes & 1))
        return STG_E_DOCFILECORRUPT;
    WCHAR name[NAME_CHARS + 1];
    ULONG chars = name_bytes / 2 - 1;
    for (ULONG i = 0; i < chars; i++)
        name[i] = (WCHAR)le16(e + i * 2);
    name[chars] = 0;
    NODE *node = node_new(name, type);
    if (!node)
        return STG_E_INSUFFICIENTMEMORY;
    memcpy(&node->Clsid, e + 80, sizeof(CLSID));
    node->State = le32(e + 96);
    ULONGLONG created = le64(e + 100), modified = le64(e + 108);
    node->Created.dwLowDateTime = (DWORD)created;
    node->Created.dwHighDateTime = (DWORD)(created >> 32);
    node->Modified.dwLowDateTime = (DWORD)modified;
    node->Modified.dwHighDateTime = (DWORD)(modified >> 32);
    hr = child_add(parent, node);
    if (FAILED(hr)) {
        node_release(node);
        return hr == STG_E_FILEALREADYEXISTS ? STG_E_DOCFILECORRUPT : hr;
    }
    ULONG start = le32(e + 116);
    ULONGLONG size = le64(e + 120);
    if (r->SectorSize == 512)
        size &= 0xFFFFFFFFull;
    if (type == STGTY_STORAGE) {
        hr = build_children(r, d, node, le32(e + 76), depth + 1);
        if (FAILED(hr))
            return hr;
    } else if (size) {
        BYTE *data = nullptr;
        if (size < r->MiniCutoff) {
            hr = read_mini_chain(r, start, size, &data);
        } else {
            ULONGLONG got;
            hr = read_chain(r, start, size, TRUE, &data, &got);
        }
        if (FAILED(hr))
            return hr;
        node->Data = data;
        node->Size = node->Capacity_ = size;
    }
    return build_children(r, d, parent, le32(e + 72), depth + 1);
}

static HRESULT parse_image(const BYTE *image, ULONGLONG length, NODE **root_out)
{
    READER r = {.Image = image, .Length = length};
    HRESULT hr = STG_E_DOCFILECORRUPT;
    BYTE *directory = nullptr, *minifat_bytes = nullptr, *fat_bytes = nullptr;
    DIRECTORY d = {};
    NODE *root = nullptr;
    if (length < 512 || memcmp(image, g_signature, 8) || le16(image + 28) != 0xFFFE)
        return STG_E_DOCFILECORRUPT;
    r.SectorShift = le16(image + 30);
    if ((r.SectorShift != 9 && r.SectorShift != 12) || le16(image + 32) != 6)
        return STG_E_DOCFILECORRUPT;
    r.SectorSize = 1u << r.SectorShift;
    r.MiniCutoff = le32(image + 56);
    ULONG fat_sectors = le32(image + 44), first_directory = le32(image + 48);
    ULONG first_mini = le32(image + 60), mini_count = le32(image + 64);
    ULONG first_difat = le32(image + 68), difat_count = le32(image + 72);
    if (!fat_sectors || (ULONGLONG)fat_sectors * r.SectorSize > length)
        return STG_E_DOCFILECORRUPT;
    /* the table of the fat: the sectors named by the header, then by the chain of the extra sectors. */
    ULONG *fat_list = OleAlloc((SIZE_T)fat_sectors * sizeof(ULONG));
    if (!fat_list)
        return STG_E_INSUFFICIENTMEMORY;
    ULONG found = 0;
    for (ULONG i = 0; i < HEADER_DIFAT && found < fat_sectors; i++) {
        ULONG s = le32(image + 76 + i * 4);
        if (s >= 0xFFFFFFFAu)
            break;
        fat_list[found++] = s;
    }
    ULONG difat = first_difat;
    for (ULONG n = 0; found < fat_sectors && n < difat_count && difat < 0xFFFFFFFAu; n++) {
        const BYTE *p = sector_at(&r, difat);
        if (!p)
            goto done;
        ULONG per = r.SectorSize / 4 - 1;
        for (ULONG i = 0; i < per && found < fat_sectors; i++) {
            ULONG s = le32(p + i * 4);
            if (s >= 0xFFFFFFFAu)
                break;
            fat_list[found++] = s;
        }
        difat = le32(p + per * 4);
    }
    if (found != fat_sectors)
        goto done;
    ULONG per_sector = r.SectorSize / 4;
    r.FatCount = fat_sectors * per_sector;
    r.Fat = OleAlloc((SIZE_T)r.FatCount * sizeof(ULONG));
    if (!r.Fat)
        goto done;
    for (ULONG i = 0; i < fat_sectors; i++) {
        const BYTE *p = sector_at(&r, fat_list[i]);
        if (!p)
            goto done;
        for (ULONG j = 0; j < per_sector; j++)
            r.Fat[i * per_sector + j] = le32(p + j * 4);
    }
    ULONGLONG dir_size;
    hr = read_chain(&r, first_directory, 0, FALSE, &directory, &dir_size);
    if (FAILED(hr))
        goto done;
    hr = STG_E_DOCFILECORRUPT;
    if (dir_size < 128)
        goto done;
    d.Entries = directory;
    d.Count = (ULONG)(dir_size / 128);
    if (directory[66] != 5)
        goto done;
    /* the mini fat and the mini stream, which is the stream of the root entry. */
    if (mini_count && first_mini < 0xFFFFFFFAu) {
        ULONGLONG got;
        hr = read_chain(&r, first_mini, 0, FALSE, &minifat_bytes, &got);
        if (FAILED(hr))
            goto done;
        r.MiniFatCount = (ULONG)(got / 4);
        r.MiniFat = OleAlloc((SIZE_T)r.MiniFatCount * sizeof(ULONG) + 4);
        if (!r.MiniFat)
            goto done;
        for (ULONG i = 0; i < r.MiniFatCount; i++)
            r.MiniFat[i] = le32(minifat_bytes + i * 4);
    }
    ULONG mini_start = le32(directory + 116);
    ULONGLONG mini_size = le64(directory + 120);
    if (r.SectorSize == 512)
        mini_size &= 0xFFFFFFFFull;
    if (mini_size && mini_start < 0xFFFFFFFAu) {
        hr = read_chain(&r, mini_start, mini_size, TRUE, &r.Mini, &r.MiniSize);
        if (FAILED(hr))
            goto done;
    }
    root = node_new(L"Root Entry", STGTY_STORAGE);
    hr = STG_E_INSUFFICIENTMEMORY;
    if (!root)
        goto done;
    memcpy(&root->Clsid, directory + 80, sizeof(CLSID));
    root->State = le32(directory + 96);
    ULONGLONG created = le64(directory + 100), modified = le64(directory + 108);
    root->Created.dwLowDateTime = (DWORD)created;
    root->Created.dwHighDateTime = (DWORD)(created >> 32);
    root->Modified.dwLowDateTime = (DWORD)modified;
    root->Modified.dwHighDateTime = (DWORD)(modified >> 32);
    d.Seen = OleAllocZero(d.Count);
    if (!d.Seen)
        goto done;
    d.Seen[0] = 1;
    hr = build_children(&r, &d, root, le32(directory + 76), 0);
done:
    OleFree(d.Seen);
    OleFree(directory);
    OleFree(minifat_bytes);
    OleFree(fat_bytes);
    OleFree(fat_list);
    OleFree(r.Fat);
    OleFree(r.MiniFat);
    OleFree(r.Mini);
    if (FAILED(hr)) {
        node_release(root);
        return hr;
    }
    *root_out = root;
    return S_OK;
}

/* ------------------------------------------------------------------ writing a compound file */

typedef struct {
    NODE *Node;
    ULONG Left, Right, Child;
    BYTE Color;
    ULONG Start;
    ULONGLONG Size;
} ENTRY;

typedef struct {
    ENTRY *Entries;
    ULONG Count, Capacity;
} ENTRIES;

static HRESULT entry_push(ENTRIES *list, NODE *node, ULONG *id)
{
    if (list->Count == list->Capacity) {
        ULONG capacity = list->Capacity ? list->Capacity * 2 : 16;
        ENTRY *bigger = OleAlloc(capacity * sizeof(ENTRY));
        if (!bigger)
            return STG_E_INSUFFICIENTMEMORY;
        if (list->Count)
            memcpy(bigger, list->Entries, list->Count * sizeof(ENTRY));
        OleFree(list->Entries);
        list->Entries = bigger;
        list->Capacity = capacity;
    }
    list->Entries[list->Count] = (ENTRY){.Node = node, .Left = NOSTREAM, .Right = NOSTREAM, .Child = NOSTREAM,
                                         .Start = ENDOFCHAIN};
    *id = list->Count++;
    return S_OK;
}

/*
 * the siblings first..first+count-1 are sorted, and become a balanced tree, red where its deepest level is not full and
 * black everywhere else, which keeps the rule of the red black tree that the compound file of Windows relies on.
 */
static ULONG link_siblings(ENTRY *entries, ULONG first, ULONG count, ULONG depth, LONG red_depth)
{
    if (!count)
        return NOSTREAM;
    ULONG middle = count / 2;
    ULONG id = first + middle;
    entries[id].Color = (LONG)depth == red_depth ? 0 : 1;
    entries[id].Left = link_siblings(entries, first, middle, depth + 1, red_depth);
    entries[id].Right = link_siblings(entries, id + 1, count - middle - 1, depth + 1, red_depth);
    return id;
}

static HRESULT layout_storage(ENTRIES *list, ULONG storage_id)
{
    NODE *storage = list->Entries[storage_id].Node;
    if (!storage->Count)
        return S_OK;
    ULONG first = list->Count;
    for (ULONG i = 0; i < storage->Count; i++) {
        ULONG id;
        HRESULT hr = entry_push(list, storage->Children[i], &id);
        if (FAILED(hr))
            return hr;
    }
    ULONG count = storage->Count, deepest = 0;
    while ((2ull << deepest) <= count)
        deepest++;
    BOOL perfect = (2ull << deepest) - 1 == count;
    list->Entries[storage_id].Child = link_siblings(list->Entries, first, count, 0, perfect ? -1 : (LONG)deepest);
    for (ULONG i = 0; i < count; i++) {
        if (storage->Children[i]->Type == STGTY_STORAGE) {
            HRESULT hr = layout_storage(list, first + i);
            if (FAILED(hr))
                return hr;
        }
    }
    return S_OK;
}

static void put32(BYTE *p, ULONG v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
    p[2] = (BYTE)(v >> 16);
    p[3] = (BYTE)(v >> 24);
}

static void put64(BYTE *p, ULONGLONG v)
{
    put32(p, (ULONG)v);
    put32(p + 4, (ULONG)(v >> 32));
}

static void put16(BYTE *p, USHORT v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
}

static ULONG ceil_div(ULONGLONG a, ULONG b)
{
    return (ULONG)((a + b - 1) / b);
}

/* the image of the tree: BYTE buffer from the heap and its length. */
static HRESULT build_image(NODE *root, BYTE **image_out, ULONGLONG *length_out)
{
    ENTRIES list = {};
    HRESULT hr;
    ULONG id;
    BYTE *image = nullptr, *mini = nullptr;
    ULONG *fat = nullptr, *minifat = nullptr;
    hr = entry_push(&list, root, &id);
    if (FAILED(hr))
        return hr;
    hr = layout_storage(&list, 0);
    if (FAILED(hr))
        goto done;
    /* the mini stream holds the streams under the cutoff, in sectors of 64 bytes. */
    ULONGLONG mini_sectors = 0, big_sectors = 0;
    for (ULONG i = 1; i < list.Count; i++) {
        NODE *n = list.Entries[i].Node;
        if (n->Type != STGTY_STREAM)
            continue;
        list.Entries[i].Size = n->Size;
        if (n->Size && n->Size < MINI_CUTOFF)
            mini_sectors += ceil_div(n->Size, MINI_SECTOR);
        else if (n->Size)
            big_sectors += ceil_div(n->Size, 512);
    }
    ULONG dir_sectors = ceil_div((ULONGLONG)list.Count, 4);
    ULONG minifat_sectors = ceil_div(mini_sectors * 4, 512);
    ULONG mini_stream_sectors = ceil_div(mini_sectors * MINI_SECTOR, 512);
    ULONGLONG content = (ULONGLONG)dir_sectors + minifat_sectors + mini_stream_sectors + big_sectors;
    if (content > 0x7FFFFF00ull) {
        hr = STG_E_MEDIUMFULL;
        goto done;
    }
    ULONG fat_sectors = 0, difat_sectors = 0;
    for (;;) {
        ULONGLONG total = content + fat_sectors + difat_sectors;
        ULONG need_fat = ceil_div(total, 128);
        ULONG need_difat = need_fat > HEADER_DIFAT ? ceil_div(need_fat - HEADER_DIFAT, 127) : 0;
        if (need_fat == fat_sectors && need_difat == difat_sectors)
            break;
        fat_sectors = need_fat;
        difat_sectors = need_difat;
    }
    ULONGLONG total = content + fat_sectors + difat_sectors;
    ULONGLONG length = 512 + total * 512;
    image = OleAllocZero((SIZE_T)length);
    fat = OleAlloc((SIZE_T)(fat_sectors * 128ull) * sizeof(ULONG));
    mini = OleAllocZero((SIZE_T)(mini_stream_sectors ? mini_stream_sectors * 512ull : 1));
    minifat = OleAlloc((SIZE_T)(minifat_sectors * 128ull + 1) * sizeof(ULONG));
    if (!image || !fat || !mini || !minifat) {
        hr = STG_E_INSUFFICIENTMEMORY;
        goto done;
    }
    for (ULONG i = 0; i < fat_sectors * 128; i++)
        fat[i] = FREESECT;
    for (ULONG i = 0; i < minifat_sectors * 128; i++)
        minifat[i] = FREESECT;
    /* the sectors in order: directory, mini fat, mini stream, big streams, fat, difat. */
    ULONG next = 0;
    ULONG dir_start = next, minifat_start = ENDOFCHAIN, mini_start = ENDOFCHAIN;
    for (ULONG i = 0; i < dir_sectors; i++, next++)
        fat[next] = i + 1 < dir_sectors ? next + 1 : ENDOFCHAIN;
    if (minifat_sectors) {
        minifat_start = next;
        for (ULONG i = 0; i < minifat_sectors; i++, next++)
            fat[next] = i + 1 < minifat_sectors ? next + 1 : ENDOFCHAIN;
    }
    if (mini_stream_sectors) {
        mini_start = next;
        for (ULONG i = 0; i < mini_stream_sectors; i++, next++)
            fat[next] = i + 1 < mini_stream_sectors ? next + 1 : ENDOFCHAIN;
    }
    ULONG mini_next = 0;
    for (ULONG i = 1; i < list.Count; i++) {
        NODE *n = list.Entries[i].Node;
        if (n->Type != STGTY_STREAM || !n->Size)
            continue;
        if (n->Size < MINI_CUTOFF) {
            ULONG count = ceil_div(n->Size, MINI_SECTOR);
            list.Entries[i].Start = mini_next;
            memcpy(mini + (SIZE_T)mini_next * MINI_SECTOR, n->Data, (SIZE_T)n->Size);
            for (ULONG k = 0; k < count; k++, mini_next++)
                minifat[mini_next] = k + 1 < count ? mini_next + 1 : ENDOFCHAIN;
        } else {
            ULONG count = ceil_div(n->Size, 512);
            list.Entries[i].Start = next;
            memcpy(image + 512 + (SIZE_T)next * 512, n->Data, (SIZE_T)n->Size);
            for (ULONG k = 0; k < count; k++, next++)
                fat[next] = k + 1 < count ? next + 1 : ENDOFCHAIN;
        }
    }
    ULONG fat_start = next;
    for (ULONG i = 0; i < fat_sectors; i++, next++)
        fat[next] = FATSECT;
    ULONG difat_start = difat_sectors ? next : ENDOFCHAIN;
    for (ULONG i = 0; i < difat_sectors; i++, next++)
        fat[next] = DIFSECT;
    /* the directory. */
    BYTE *dir = image + 512 + (SIZE_T)dir_start * 512;
    for (ULONG i = 0; i < dir_sectors * 4; i++) {
        BYTE *e = dir + (SIZE_T)i * 128;
        if (i >= list.Count) {
            put32(e + 68, NOSTREAM);
            put32(e + 72, NOSTREAM);
            put32(e + 76, NOSTREAM);
            continue;
        }
        ENTRY *entry = &list.Entries[i];
        NODE *n = entry->Node;
        PCWSTR name = i == 0 ? L"Root Entry" : n->Name;
        ULONG chars = (ULONG)wcslen(name);
        for (ULONG k = 0; k < chars; k++)
            put16(e + k * 2, name[k]);
        put16(e + 64, (USHORT)((chars + 1) * 2));
        e[66] = i == 0 ? 5 : n->Type;
        e[67] = i == 0 ? 1 : entry->Color;
        put32(e + 68, entry->Left);
        put32(e + 72, entry->Right);
        put32(e + 76, entry->Child);
        memcpy(e + 80, &n->Clsid, sizeof(CLSID));
        put32(e + 96, n->State);
        put64(e + 100, (ULONGLONG)n->Created.dwHighDateTime << 32 | n->Created.dwLowDateTime);
        put64(e + 108, (ULONGLONG)n->Modified.dwHighDateTime << 32 | n->Modified.dwLowDateTime);
        if (i == 0) {
            put32(e + 116, mini_start);
            put64(e + 120, mini_sectors * MINI_SECTOR);
        } else if (n->Type == STGTY_STREAM) {
            put32(e + 116, n->Size ? entry->Start : ENDOFCHAIN);
            put64(e + 120, n->Size);
        } else {
            put32(e + 116, 0);
        }
    }
    /* what the sectors hold: the mini fat, the mini stream, the fat and the extra sectors of the difat. */
    if (minifat_sectors)
        for (ULONG i = 0; i < minifat_sectors * 128; i++)
            put32(image + 512 + (SIZE_T)minifat_start * 512 + i * 4, minifat[i]);
    if (mini_stream_sectors)
        memcpy(image + 512 + (SIZE_T)mini_start * 512, mini, (SIZE_T)mini_stream_sectors * 512);
    for (ULONG i = 0; i < fat_sectors * 128; i++)
        put32(image + 512 + ((SIZE_T)fat_start * 512) + i * 4, fat[i]);
    for (ULONG i = 0; i < difat_sectors; i++) {
        BYTE *p = image + 512 + ((SIZE_T)difat_start + i) * 512;
        for (ULONG k = 0; k < 127; k++) {
            ULONG index = HEADER_DIFAT + i * 127 + k;
            put32(p + k * 4, index < fat_sectors ? fat_start + index : FREESECT);
        }
        put32(p + 127 * 4, i + 1 < difat_sectors ? difat_start + i + 1 : ENDOFCHAIN);
    }
    /* the header. */
    memcpy(image, g_signature, 8);
    put16(image + 24, 0x3E);
    put16(image + 26, 3);
    put16(image + 28, 0xFFFE);
    put16(image + 30, 9);
    put16(image + 32, 6);
    put32(image + 44, fat_sectors);
    put32(image + 48, dir_start);
    put32(image + 56, MINI_CUTOFF);
    put32(image + 60, minifat_start);
    put32(image + 64, minifat_sectors);
    put32(image + 68, difat_start);
    put32(image + 72, difat_sectors);
    for (ULONG i = 0; i < HEADER_DIFAT; i++)
        put32(image + 76 + i * 4, i < fat_sectors ? fat_start + i : FREESECT);
    *image_out = image;
    *length_out = length;
    image = nullptr;
    hr = S_OK;
done:
    OleFree(image);
    OleFree(fat);
    OleFree(mini);
    OleFree(minifat);
    OleFree(list.Entries);
    return hr;
}

/* ------------------------------------------------------------------ the document */

typedef struct {
    LONG Refs;
    SRWLOCK Lock;
    ILockBytes *Bytes;
    NODE *Root;
    DWORD Mode;
    BOOL Dirty, Transacted, Writable, DeleteOnRelease;
    PWSTR Path;
} DOC;

static HRESULT doc_write(DOC *doc)
{
    BYTE *image;
    ULONGLONG length;
    HRESULT hr = build_image(doc->Root, &image, &length);
    if (FAILED(hr))
        return hr;
    ULARGE_INTEGER size = {.QuadPart = length}, zero = {};
    hr = doc->Bytes->lpVtbl->SetSize(doc->Bytes, size);
    ULONGLONG done = 0;
    while (SUCCEEDED(hr) && done < length) {
        ULONG chunk = length - done > 0x100000 ? 0x100000 : (ULONG)(length - done), written = 0;
        ULARGE_INTEGER at = {.QuadPart = done};
        hr = doc->Bytes->lpVtbl->WriteAt(doc->Bytes, at, image + done, chunk, &written);
        if (SUCCEEDED(hr) && written != chunk)
            hr = STG_E_WRITEFAULT;
        done += chunk;
    }
    (void)zero;
    OleFree(image);
    if (SUCCEEDED(hr))
        hr = doc->Bytes->lpVtbl->Flush(doc->Bytes);
    if (SUCCEEDED(hr))
        doc->Dirty = FALSE;
    return hr;
}

static void doc_addref(DOC *doc)
{
    InterlockedIncrement(&doc->Refs);
}

static void doc_release(DOC *doc)
{
    if (InterlockedDecrement(&doc->Refs))
        return;
    if (doc->Dirty && doc->Writable && !doc->Transacted)
        doc_write(doc);
    if (doc->Bytes)
        doc->Bytes->lpVtbl->Release(doc->Bytes);
    if (doc->DeleteOnRelease && doc->Path)
        DeleteFileW(doc->Path);
    node_release(doc->Root);
    OleFree(doc->Path);
    OleFree(doc);
}

/* ------------------------------------------------------------------ streams */

typedef struct {
    IStream Interface;
    LONG Refs;
    DOC *Doc;
    NODE *Node;
    ULONGLONG Position;
    DWORD Mode;
} DF_STREAM;

static const IStreamVtbl g_stream_vtbl;

static HRESULT stream_check(DF_STREAM *s)
{
    return s->Node->Detached ? STG_E_REVERTED : S_OK;
}

static HRESULT WINAPI df_stream_query(IStream *self, REFIID iid, void **result)
{
    if (!result)
        return STG_E_INVALIDPOINTER;
    *result = nullptr;
    if (!iid || (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IStream) &&
                 !IsEqualIID(iid, &IID_ISequentialStream)))
        return E_NOINTERFACE;
    *result = self;
    self->lpVtbl->AddRef(self);
    return S_OK;
}

static ULONG WINAPI df_stream_addref(IStream *self)
{
    return InterlockedIncrement(&((DF_STREAM *)self)->Refs);
}

static ULONG WINAPI df_stream_release(IStream *self)
{
    DF_STREAM *s = (DF_STREAM *)self;
    LONG refs = InterlockedDecrement(&s->Refs);
    if (!refs) {
        node_release(s->Node);
        doc_release(s->Doc);
        OleFree(s);
    }
    return (ULONG)refs;
}

static HRESULT WINAPI df_stream_read(IStream *self, void *buffer, ULONG size, ULONG *done)
{
    DF_STREAM *s = (DF_STREAM *)self;
    if (done)
        *done = 0;
    if (!buffer && size)
        return STG_E_INVALIDPOINTER;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = stream_check(s);
    if (SUCCEEDED(hr)) {
        ULONGLONG available = s->Position < s->Node->Size ? s->Node->Size - s->Position : 0;
        ULONG n = available < size ? (ULONG)available : size;
        if (n)
            memcpy(buffer, s->Node->Data + s->Position, n);
        s->Position += n;
        if (done)
            *done = n;
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_stream_write(IStream *self, const void *buffer, ULONG size, ULONG *done)
{
    DF_STREAM *s = (DF_STREAM *)self;
    if (done)
        *done = 0;
    if (!buffer && size)
        return STG_E_INVALIDPOINTER;
    if (!(s->Mode & (STGM_WRITE | STGM_READWRITE)))
        return STG_E_ACCESSDENIED;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = stream_check(s);
    if (SUCCEEDED(hr) && size) {
        ULONGLONG end = s->Position + size;
        if (end < s->Position)
            hr = STG_E_MEDIUMFULL;
        else if (end > s->Node->Size)
            hr = data_resize(s->Node, end);
        if (SUCCEEDED(hr)) {
            memcpy(s->Node->Data + s->Position, buffer, size);
            s->Position = end;
            s->Doc->Dirty = TRUE;
            if (done)
                *done = size;
        }
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_stream_seek(IStream *self, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position)
{
    DF_STREAM *s = (DF_STREAM *)self;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = stream_check(s);
    if (SUCCEEDED(hr)) {
        LONGLONG base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? (LONGLONG)s->Position
                                                                                  : (LONGLONG)s->Node->Size;
        LONGLONG target = base + move.QuadPart;
        if (origin > STREAM_SEEK_END || target < 0 || (move.QuadPart > 0 && target < base))
            hr = STG_E_INVALIDFUNCTION;
        else {
            s->Position = (ULONGLONG)target;
            if (position)
                position->QuadPart = s->Position;
        }
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_stream_set_size(IStream *self, ULARGE_INTEGER size)
{
    DF_STREAM *s = (DF_STREAM *)self;
    if (!(s->Mode & (STGM_WRITE | STGM_READWRITE)))
        return STG_E_ACCESSDENIED;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = stream_check(s);
    if (SUCCEEDED(hr)) {
        hr = data_resize(s->Node, size.QuadPart);
        if (SUCCEEDED(hr))
            s->Doc->Dirty = TRUE;
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_stream_copy_to(IStream *self, IStream *target, ULARGE_INTEGER count, ULARGE_INTEGER *read,
                                        ULARGE_INTEGER *written)
{
    if (read)
        read->QuadPart = 0;
    if (written)
        written->QuadPart = 0;
    if (!target)
        return STG_E_INVALIDPOINTER;
    BYTE *buffer = OleAlloc(65536);
    if (!buffer)
        return STG_E_INSUFFICIENTMEMORY;
    ULONGLONG total_read = 0, total_written = 0, left = count.QuadPart;
    HRESULT hr = S_OK;
    while (left) {
        ULONG want = left > 65536 ? 65536 : (ULONG)left, got = 0, put = 0;
        hr = self->lpVtbl->Read(self, buffer, want, &got);
        if (FAILED(hr) || !got)
            break;
        total_read += got;
        hr = target->lpVtbl->Write(target, buffer, got, &put);
        total_written += put;
        if (FAILED(hr) || put != got) {
            if (SUCCEEDED(hr))
                hr = STG_E_MEDIUMFULL;
            break;
        }
        left -= got;
    }
    OleFree(buffer);
    if (read)
        read->QuadPart = total_read;
    if (written)
        written->QuadPart = total_written;
    return hr;
}

static HRESULT WINAPI df_stream_commit(IStream *self, DWORD flags)
{
    DF_STREAM *s = (DF_STREAM *)self;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = stream_check(s);
    /* a stream has nothing of its own to write: the root of a direct mode file is written when it is released. */
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_stream_revert(IStream *self)
{
    return S_OK;
}

static HRESULT WINAPI df_stream_lock(IStream *self, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT stat_node(NODE *node, STATSTG *stat, DWORD flags, PCWSTR name_override, DWORD mode)
{
    memset(stat, 0, sizeof(*stat));
    PCWSTR name = name_override ? name_override : node->Name;
    if (!(flags & STATFLAG_NONAME) && name) {
        SIZE_T bytes = (wcslen(name) + 1) * sizeof(WCHAR);
        stat->pwcsName = CoTaskMemAlloc(bytes);
        if (!stat->pwcsName)
            return STG_E_INSUFFICIENTMEMORY;
        memcpy(stat->pwcsName, name, bytes);
    }
    stat->type = node->Type == STGTY_STREAM ? STGTY_STREAM : STGTY_STORAGE;
    stat->cbSize.QuadPart = node->Type == STGTY_STREAM ? node->Size : 0;
    stat->mtime = node->Modified;
    stat->ctime = node->Created;
    stat->grfMode = mode;
    stat->grfLocksSupported = 0;
    stat->clsid = node->Clsid;
    stat->grfStateBits = node->State;
    return S_OK;
}

static HRESULT WINAPI df_stream_stat(IStream *self, STATSTG *stat, DWORD flags)
{
    DF_STREAM *s = (DF_STREAM *)self;
    if (!stat)
        return STG_E_INVALIDPOINTER;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = stream_check(s);
    if (SUCCEEDED(hr))
        hr = stat_node(s->Node, stat, flags, nullptr, s->Mode);
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT stream_open(DOC *doc, NODE *node, DWORD mode, IStream **out)
{
    DF_STREAM *s = OleAllocZero(sizeof(*s));
    if (!s)
        return STG_E_INSUFFICIENTMEMORY;
    s->Interface.lpVtbl = &g_stream_vtbl;
    s->Refs = 1;
    s->Doc = doc;
    s->Node = node;
    s->Mode = mode;
    doc_addref(doc);
    node_addref(node);
    *out = &s->Interface;
    return S_OK;
}

static HRESULT WINAPI df_stream_clone(IStream *self, IStream **clone)
{
    DF_STREAM *s = (DF_STREAM *)self;
    if (!clone)
        return STG_E_INVALIDPOINTER;
    *clone = nullptr;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = stream_check(s);
    if (SUCCEEDED(hr)) {
        hr = stream_open(s->Doc, s->Node, s->Mode, clone);
        if (SUCCEEDED(hr))
            ((DF_STREAM *)*clone)->Position = s->Position;
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static const IStreamVtbl g_stream_vtbl = {df_stream_query, df_stream_addref, df_stream_release, df_stream_read,
                                          df_stream_write, df_stream_seek, df_stream_set_size, df_stream_copy_to,
                                          df_stream_commit, df_stream_revert, df_stream_lock, df_stream_lock,
                                          df_stream_stat, df_stream_clone};

/* ------------------------------------------------------------------ the enumerator of elements */

typedef struct DF_ENUM DF_ENUM;
typedef struct {
    HRESULT(WINAPI *QueryInterface)(DF_ENUM *, REFIID, void **);
    ULONG(WINAPI *AddRef)(DF_ENUM *);
    ULONG(WINAPI *Release)(DF_ENUM *);
    HRESULT(WINAPI *Next)(DF_ENUM *, ULONG, STATSTG *, ULONG *);
    HRESULT(WINAPI *Skip)(DF_ENUM *, ULONG);
    HRESULT(WINAPI *Reset)(DF_ENUM *);
    HRESULT(WINAPI *Clone)(DF_ENUM *, DF_ENUM **);
} DF_ENUM_VTBL;
struct DF_ENUM {
    const DF_ENUM_VTBL *lpVtbl;
    LONG Refs;
    STATSTG *Items;
    ULONG Count, Position;
};

static const DF_ENUM_VTBL g_enum_vtbl;
MIME_DEFINE_GUID(DF_IID_IEnumSTATSTG, 0x0000000d, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46);

static HRESULT WINAPI df_enum_query(DF_ENUM *self, REFIID iid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!iid || (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &DF_IID_IEnumSTATSTG)))
        return E_NOINTERFACE;
    *result = self;
    InterlockedIncrement(&self->Refs);
    return S_OK;
}

static ULONG WINAPI df_enum_addref(DF_ENUM *self)
{
    return (ULONG)InterlockedIncrement(&self->Refs);
}

static ULONG WINAPI df_enum_release(DF_ENUM *self)
{
    LONG refs = InterlockedDecrement(&self->Refs);
    if (!refs) {
        for (ULONG i = 0; i < self->Count; i++)
            CoTaskMemFree(self->Items[i].pwcsName);
        OleFree(self->Items);
        OleFree(self);
    }
    return (ULONG)refs;
}

static HRESULT WINAPI df_enum_next(DF_ENUM *self, ULONG count, STATSTG *out, ULONG *fetched)
{
    ULONG done = 0;
    if (!out && count)
        return STG_E_INVALIDPOINTER;
    while (done < count && self->Position < self->Count) {
        STATSTG item = self->Items[self->Position];
        if (item.pwcsName) {
            SIZE_T bytes = (wcslen(item.pwcsName) + 1) * sizeof(WCHAR);
            PWSTR copy = CoTaskMemAlloc(bytes);
            if (!copy) {
                while (done)
                    CoTaskMemFree(out[--done].pwcsName);
                return STG_E_INSUFFICIENTMEMORY;
            }
            memcpy(copy, item.pwcsName, bytes);
            item.pwcsName = copy;
        }
        out[done++] = item;
        self->Position++;
    }
    if (fetched)
        *fetched = done;
    return done == count ? S_OK : S_FALSE;
}

static HRESULT WINAPI df_enum_skip(DF_ENUM *self, ULONG count)
{
    ULONG left = self->Count - self->Position;
    if (count > left) {
        self->Position = self->Count;
        return S_FALSE;
    }
    self->Position += count;
    return S_OK;
}

static HRESULT WINAPI df_enum_reset(DF_ENUM *self)
{
    self->Position = 0;
    return S_OK;
}

static DF_ENUM *enum_make(ULONG count)
{
    DF_ENUM *e = OleAllocZero(sizeof(*e));
    if (!e)
        return nullptr;
    e->lpVtbl = &g_enum_vtbl;
    e->Refs = 1;
    e->Items = OleAllocZero((count ? count : 1) * sizeof(STATSTG));
    if (!e->Items) {
        OleFree(e);
        return nullptr;
    }
    return e;
}

static HRESULT WINAPI df_enum_clone(DF_ENUM *self, DF_ENUM **clone)
{
    if (!clone)
        return E_POINTER;
    *clone = nullptr;
    DF_ENUM *e = enum_make(self->Count);
    if (!e)
        return STG_E_INSUFFICIENTMEMORY;
    for (ULONG i = 0; i < self->Count; i++) {
        e->Items[i] = self->Items[i];
        if (self->Items[i].pwcsName) {
            SIZE_T bytes = (wcslen(self->Items[i].pwcsName) + 1) * sizeof(WCHAR);
            e->Items[i].pwcsName = CoTaskMemAlloc(bytes);
            if (!e->Items[i].pwcsName) {
                e->Count = i;
                df_enum_release(e);
                return STG_E_INSUFFICIENTMEMORY;
            }
            memcpy(e->Items[i].pwcsName, self->Items[i].pwcsName, bytes);
        }
    }
    e->Count = self->Count;
    e->Position = self->Position;
    *clone = e;
    return S_OK;
}

static const DF_ENUM_VTBL g_enum_vtbl = {df_enum_query, df_enum_addref, df_enum_release, df_enum_next,
                                         df_enum_skip, df_enum_reset, df_enum_clone};

/* ------------------------------------------------------------------ storages */

typedef struct DF_STORAGE {
    IStorage Interface;
    LONG Refs;
    DOC *Doc;
    /* what the object works on: the element itself, or for a transaction a copy of it. */
    NODE *Node;
    NODE *Original;
    DWORD Mode;
    BOOL Root;
} DF_STORAGE;

static const IStorageVtbl g_storage_vtbl;

static HRESULT storage_open_object(DOC *doc, NODE *node, NODE *original, DWORD mode, BOOL root, IStorage **out);

static BOOL name_valid(PCWSTR name)
{
    if (!name || !name[0])
        return FALSE;
    SIZE_T n = wcslen(name);
    if (n > NAME_CHARS)
        return FALSE;
    for (SIZE_T i = 0; i < n; i++)
        if (name[i] == L'/' || name[i] == L'\\' || name[i] == L':' || name[i] == L'!')
            return FALSE;
    return TRUE;
}

static HRESULT WINAPI df_storage_query(IStorage *self, REFIID iid, void **result)
{
    if (!result)
        return STG_E_INVALIDPOINTER;
    *result = nullptr;
    if (!iid || (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IStorage)))
        return E_NOINTERFACE;
    *result = self;
    self->lpVtbl->AddRef(self);
    return S_OK;
}

static ULONG WINAPI df_storage_addref(IStorage *self)
{
    return (ULONG)InterlockedIncrement(&((DF_STORAGE *)self)->Refs);
}

static ULONG WINAPI df_storage_release(IStorage *self)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    LONG refs = InterlockedDecrement(&s->Refs);
    if (refs)
        return (ULONG)refs;
    if (s->Root) {
        /* a direct mode file is written when its root goes, a transacted one only by its own commit. */
        AcquireSRWLockExclusive(&s->Doc->Lock);
        if (s->Doc->Dirty && s->Doc->Writable && !s->Doc->Transacted)
            doc_write(s->Doc);
        ReleaseSRWLockExclusive(&s->Doc->Lock);
    }
    if (s->Original && s->Original != s->Node)
        node_release(s->Original);
    node_release(s->Node);
    doc_release(s->Doc);
    OleFree(s);
    return 0;
}

static BOOL storage_writable(DF_STORAGE *s)
{
    return (s->Mode & (STGM_WRITE | STGM_READWRITE)) != 0 && s->Doc->Writable;
}

static HRESULT element_mode_check(DF_STORAGE *s, DWORD mode, BOOL create)
{
    if ((mode & STGM_SHARE_MASK) != STGM_SHARE_EXCLUSIVE)
        return STG_E_INVALIDFLAG;
    if ((create || (mode & (STGM_WRITE | STGM_READWRITE))) && !storage_writable(s))
        return STG_E_ACCESSDENIED;
    return S_OK;
}

static HRESULT WINAPI df_storage_create_stream(IStorage *self, LPCOLESTR name, DWORD mode, DWORD r1, DWORD r2,
                                               IStream **stream)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    if (!stream)
        return STG_E_INVALIDPOINTER;
    *stream = nullptr;
    if (r1 || r2)
        return STG_E_INVALIDPARAMETER;
    if (!name_valid(name))
        return STG_E_INVALIDNAME;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : element_mode_check(s, mode, TRUE);
    if (SUCCEEDED(hr)) {
        NODE *node = child_find(s->Node, name);
        if (node && (node->Type != STGTY_STREAM || !(mode & STGM_CREATE))) {
            hr = STG_E_FILEALREADYEXISTS;
        } else if (node) {
            hr = data_resize(node, 0);
            s->Doc->Dirty = TRUE;
        } else {
            node = node_new(name, STGTY_STREAM);
            hr = node ? child_add(s->Node, node) : STG_E_INSUFFICIENTMEMORY;
            if (FAILED(hr))
                node_release(node);
            else
                s->Doc->Dirty = TRUE;
        }
        if (SUCCEEDED(hr))
            hr = stream_open(s->Doc, node, mode, stream);
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_storage_open_stream(IStorage *self, LPCOLESTR name, void *reserved1, DWORD mode,
                                             DWORD reserved2, IStream **stream)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    if (!stream)
        return STG_E_INVALIDPOINTER;
    *stream = nullptr;
    if (reserved1 || reserved2)
        return STG_E_INVALIDPARAMETER;
    if (!name_valid(name))
        return STG_E_INVALIDNAME;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : element_mode_check(s, mode, FALSE);
    if (SUCCEEDED(hr)) {
        NODE *node = child_find(s->Node, name);
        hr = node && node->Type == STGTY_STREAM ? stream_open(s->Doc, node, mode, stream) : STG_E_FILENOTFOUND;
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_storage_create_storage(IStorage *self, LPCOLESTR name, DWORD mode, DWORD r1, DWORD r2,
                                                IStorage **storage)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    if (!storage)
        return STG_E_INVALIDPOINTER;
    *storage = nullptr;
    if (r1 || r2)
        return STG_E_INVALIDPARAMETER;
    if (!name_valid(name))
        return STG_E_INVALIDNAME;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : element_mode_check(s, mode, TRUE);
    if (SUCCEEDED(hr)) {
        NODE *node = child_find(s->Node, name);
        if (node && (node->Type != STGTY_STORAGE || !(mode & STGM_CREATE))) {
            hr = STG_E_FILEALREADYEXISTS;
        } else {
            if (node) {
                /* made again, it starts empty. */
                NODE *fresh = node_new(name, STGTY_STORAGE);
                if (!fresh) {
                    hr = STG_E_INSUFFICIENTMEMORY;
                } else {
                    node_take(node, fresh);
                    node_release(fresh);
                }
            } else {
                node = node_new(name, STGTY_STORAGE);
                hr = node ? child_add(s->Node, node) : STG_E_INSUFFICIENTMEMORY;
                if (FAILED(hr))
                    node_release(node);
            }
            if (SUCCEEDED(hr)) {
                s->Doc->Dirty = TRUE;
                NODE *work = node;
                if (mode & STGM_TRANSACTED) {
                    work = node_copy(node);
                    hr = work ? S_OK : STG_E_INSUFFICIENTMEMORY;
                }
                if (SUCCEEDED(hr))
                    hr = storage_open_object(s->Doc, work, work == node ? nullptr : node, mode, FALSE, storage);
                if (work != node)
                    node_release(work);
            }
        }
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_storage_open_storage(IStorage *self, LPCOLESTR name, IStorage *priority, DWORD mode,
                                              SNB exclude, DWORD reserved, IStorage **storage)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    if (!storage)
        return STG_E_INVALIDPOINTER;
    *storage = nullptr;
    if (priority || reserved)
        return STG_E_INVALIDPARAMETER;
    if (!name_valid(name))
        return STG_E_INVALIDNAME;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : element_mode_check(s, mode, FALSE);
    if (SUCCEEDED(hr)) {
        NODE *node = child_find(s->Node, name);
        if (!node || node->Type != STGTY_STORAGE) {
            hr = STG_E_FILENOTFOUND;
        } else {
            NODE *work = node;
            if (mode & STGM_TRANSACTED) {
                work = node_copy(node);
                hr = work ? S_OK : STG_E_INSUFFICIENTMEMORY;
            }
            if (SUCCEEDED(hr))
                hr = storage_open_object(s->Doc, work, work == node ? nullptr : node, mode, FALSE, storage);
            if (work != node)
                node_release(work);
        }
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

/* a storage's elements into another storage, any implementation of it. */
static HRESULT copy_elements(NODE *node, IStorage *dest, DWORD excluded_types, SNB names)
{
    for (ULONG i = 0; i < node->Count; i++) {
        NODE *child = node->Children[i];
        BOOL skip = FALSE;
        if (names)
            for (SNB n = names; *n; n++)
                if (!name_compare(*n, child->Name))
                    skip = TRUE;
        if (skip)
            continue;
        if (child->Type == STGTY_STREAM) {
            if (excluded_types & 2)
                continue;
            IStream *to = nullptr;
            HRESULT hr = dest->lpVtbl->CreateStream(dest, child->Name, STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
                                                    0, 0, &to);
            if (FAILED(hr))
                return hr;
            ULONG put = 0;
            hr = child->Size ? to->lpVtbl->Write(to, child->Data, (ULONG)child->Size, &put) : S_OK;
            to->lpVtbl->Release(to);
            if (FAILED(hr))
                return hr;
        } else {
            if (excluded_types & 1)
                continue;
            IStorage *to = nullptr;
            HRESULT hr = dest->lpVtbl->CreateStorage(dest, child->Name, STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
                                                     0, 0, &to);
            if (FAILED(hr))
                return hr;
            to->lpVtbl->SetClass(to, &child->Clsid);
            to->lpVtbl->SetStateBits(to, child->State, 0xFFFFFFFF);
            hr = copy_elements(child, to, excluded_types, nullptr);
            if (SUCCEEDED(hr))
                hr = to->lpVtbl->Commit(to, STGC_DEFAULT);
            to->lpVtbl->Release(to);
            if (FAILED(hr))
                return hr;
        }
    }
    return S_OK;
}

static BOOL is_below(NODE *ancestor, NODE *node)
{
    for (; node; node = node->Parent)
        if (node == ancestor)
            return TRUE;
    return FALSE;
}

static HRESULT WINAPI df_storage_copy_to(IStorage *self, DWORD exclude_count, const IID *exclude, SNB names,
                                         IStorage *dest)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    if (!dest)
        return STG_E_INVALIDPOINTER;
    DWORD excluded = 0;
    for (DWORD i = 0; exclude && i < exclude_count; i++) {
        if (IsEqualIID(&exclude[i], &IID_IStorage))
            excluded |= 1;
        else if (IsEqualIID(&exclude[i], &IID_IStream))
            excluded |= 2;
    }
    /* the elements are copied from a snapshot, so that a destination in the same file can take its own lock. */
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : S_OK;
    NODE *snapshot = nullptr;
    if (SUCCEEDED(hr) && dest->lpVtbl == &g_storage_vtbl && is_below(s->Node, ((DF_STORAGE *)dest)->Node))
        hr = STG_E_ACCESSDENIED;
    if (SUCCEEDED(hr)) {
        snapshot = node_copy(s->Node);
        if (!snapshot)
            hr = STG_E_INSUFFICIENTMEMORY;
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    if (SUCCEEDED(hr)) {
        dest->lpVtbl->SetClass(dest, &snapshot->Clsid);
        hr = copy_elements(snapshot, dest, excluded, names);
    }
    node_release(snapshot);
    return hr;
}

static HRESULT WINAPI df_storage_destroy_element(IStorage *self, LPCOLESTR name);

static HRESULT WINAPI df_storage_move_element_to(IStorage *self, LPCOLESTR name, IStorage *dest, LPCOLESTR new_name,
                                                 DWORD flags)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    if (!dest)
        return STG_E_INVALIDPOINTER;
    if (!name_valid(name) || !name_valid(new_name))
        return STG_E_INVALIDNAME;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    NODE *node = s->Node->Detached ? nullptr : child_find(s->Node, name);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : node ? S_OK : STG_E_FILENOTFOUND;
    if (SUCCEEDED(hr) && flags == STGMOVE_MOVE && !storage_writable(s))
        hr = STG_E_ACCESSDENIED;
    NODE *snapshot = nullptr;
    if (SUCCEEDED(hr)) {
        snapshot = node_copy(node);
        if (!snapshot)
            hr = STG_E_INSUFFICIENTMEMORY;
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    if (SUCCEEDED(hr) && snapshot->Type == STGTY_STREAM) {
        IStream *to = nullptr;
        hr = dest->lpVtbl->CreateStream(dest, new_name, STGM_WRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &to);
        if (SUCCEEDED(hr)) {
            ULONG put = 0;
            hr = snapshot->Size ? to->lpVtbl->Write(to, snapshot->Data, (ULONG)snapshot->Size, &put) : S_OK;
            to->lpVtbl->Release(to);
        }
    } else if (SUCCEEDED(hr)) {
        IStorage *to = nullptr;
        hr = dest->lpVtbl->CreateStorage(dest, new_name, STGM_WRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &to);
        if (SUCCEEDED(hr)) {
            to->lpVtbl->SetClass(to, &snapshot->Clsid);
            hr = copy_elements(snapshot, to, 0, nullptr);
            to->lpVtbl->Release(to);
        }
    }
    node_release(snapshot);
    if (SUCCEEDED(hr) && flags == STGMOVE_MOVE)
        hr = df_storage_destroy_element(self, name);
    return hr;
}

static HRESULT WINAPI df_storage_commit(IStorage *self, DWORD flags)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : S_OK;
    if (SUCCEEDED(hr) && s->Original && s->Original != s->Node) {
        /* a transaction on an element puts the copy back; the copy goes on as it is. */
        if (!s->Original->Detached) {
            NODE *copy = node_copy(s->Node);
            if (copy) {
                node_take(s->Original, copy);
                node_release(copy);
                s->Doc->Dirty = TRUE;
            } else {
                hr = STG_E_INSUFFICIENTMEMORY;
            }
        }
    }
    /* what a direct mode file has is written at once, and a transacted one by the commit of its root. */
    BOOL write = s->Root || !s->Doc->Transacted;
    if (SUCCEEDED(hr) && write && s->Doc->Writable && (s->Doc->Dirty || s->Root))
        hr = doc_write(s->Doc);
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_storage_revert(IStorage *self)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = S_OK;
    if (s->Original && s->Original != s->Node) {
        NODE *fresh = node_copy(s->Original);
        if (fresh) {
            node_take(s->Node, fresh);
            node_release(fresh);
        } else {
            hr = STG_E_INSUFFICIENTMEMORY;
        }
    } else if (s->Root && s->Doc->Transacted) {
        /* the file is read again, which is what is in it. */
        ILockBytes *bytes = s->Doc->Bytes;
        STATSTG stat;
        hr = bytes->lpVtbl->Stat(bytes, &stat, STATFLAG_NONAME);
        BYTE *image = nullptr;
        if (SUCCEEDED(hr) && stat.cbSize.QuadPart && stat.cbSize.QuadPart < 0x7FFFFFFF)
            image = OleAlloc((SIZE_T)stat.cbSize.QuadPart);
        if (image) {
            ULONG got = 0;
            ULARGE_INTEGER zero = {};
            hr = bytes->lpVtbl->ReadAt(bytes, zero, image, (ULONG)stat.cbSize.QuadPart, &got);
            NODE *root = nullptr;
            if (SUCCEEDED(hr))
                hr = parse_image(image, got, &root);
            if (SUCCEEDED(hr)) {
                node_take(s->Node, root);
                node_release(root);
                s->Doc->Dirty = FALSE;
            }
            OleFree(image);
        }
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_storage_enum_elements(IStorage *self, DWORD r1, void *r2, DWORD r3, IEnumSTATSTG **out)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    if (!out)
        return STG_E_INVALIDPOINTER;
    *out = nullptr;
    if (r1 || r2 || r3)
        return STG_E_INVALIDPARAMETER;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : S_OK;
    if (SUCCEEDED(hr)) {
        DF_ENUM *e = enum_make(s->Node->Count);
        if (!e) {
            hr = STG_E_INSUFFICIENTMEMORY;
        } else {
            for (ULONG i = 0; i < s->Node->Count && SUCCEEDED(hr); i++) {
                hr = stat_node(s->Node->Children[i], &e->Items[i], STATFLAG_DEFAULT, nullptr, 0);
                if (SUCCEEDED(hr))
                    e->Count++;
            }
            if (FAILED(hr))
                df_enum_release(e);
            else
                *out = (IEnumSTATSTG *)e;
        }
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_storage_destroy_element(IStorage *self, LPCOLESTR name)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    if (!name_valid(name))
        return STG_E_INVALIDNAME;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : storage_writable(s) ? S_OK : STG_E_ACCESSDENIED;
    if (SUCCEEDED(hr)) {
        NODE *node = child_find(s->Node, name);
        if (!node) {
            hr = STG_E_FILENOTFOUND;
        } else {
            child_remove(s->Node, node);
            s->Doc->Dirty = TRUE;
        }
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_storage_rename_element(IStorage *self, LPCOLESTR old_name, LPCOLESTR new_name)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    if (!name_valid(old_name) || !name_valid(new_name))
        return STG_E_INVALIDNAME;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : storage_writable(s) ? S_OK : STG_E_ACCESSDENIED;
    if (SUCCEEDED(hr)) {
        NODE *node = child_find(s->Node, old_name);
        if (!node) {
            hr = STG_E_FILENOTFOUND;
        } else if (child_find(s->Node, new_name) && name_compare(old_name, new_name)) {
            hr = STG_E_ACCESSDENIED;
        } else {
            /* taken out and put back under its new name, so the order of the elements stays right. */
            BOOL found;
            ULONG at = child_position(s->Node, node->Name, &found);
            memmove(s->Node->Children + at, s->Node->Children + at + 1, (s->Node->Count - at - 1) * sizeof(NODE *));
            s->Node->Count--;
            set_name(node->Name, new_name);
            node->Parent = nullptr;
            hr = child_add(s->Node, node);
            s->Doc->Dirty = TRUE;
        }
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_storage_set_element_times(IStorage *self, LPCOLESTR name, const FILETIME *created,
                                                   const FILETIME *accessed, const FILETIME *modified)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : storage_writable(s) ? S_OK : STG_E_ACCESSDENIED;
    if (SUCCEEDED(hr)) {
        NODE *node = s->Node;
        if (name) {
            node = name_valid(name) ? child_find(s->Node, name) : nullptr;
            if (!node)
                hr = name_valid(name) ? STG_E_FILENOTFOUND : STG_E_INVALIDNAME;
        }
        if (SUCCEEDED(hr)) {
            if (created)
                node->Created = *created;
            if (modified)
                node->Modified = *modified;
            s->Doc->Dirty = TRUE;
        }
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_storage_set_class(IStorage *self, REFCLSID clsid)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    if (!clsid)
        return STG_E_INVALIDPOINTER;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : storage_writable(s) ? S_OK : STG_E_ACCESSDENIED;
    if (SUCCEEDED(hr)) {
        s->Node->Clsid = *clsid;
        s->Doc->Dirty = TRUE;
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_storage_set_state_bits(IStorage *self, DWORD state, DWORD mask)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : storage_writable(s) ? S_OK : STG_E_ACCESSDENIED;
    if (SUCCEEDED(hr)) {
        s->Node->State = (s->Node->State & ~mask) | (state & mask);
        s->Doc->Dirty = TRUE;
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static HRESULT WINAPI df_storage_stat(IStorage *self, STATSTG *stat, DWORD flags)
{
    DF_STORAGE *s = (DF_STORAGE *)self;
    if (!stat)
        return STG_E_INVALIDPOINTER;
    AcquireSRWLockExclusive(&s->Doc->Lock);
    HRESULT hr = s->Node->Detached ? STG_E_REVERTED : S_OK;
    if (SUCCEEDED(hr)) {
        /* the root of a file is named by the path of the file. */
        PCWSTR name = s->Root && s->Doc->Path ? s->Doc->Path : nullptr;
        hr = stat_node(s->Node, stat, flags, name, s->Mode);
    }
    ReleaseSRWLockExclusive(&s->Doc->Lock);
    return hr;
}

static const IStorageVtbl g_storage_vtbl = {
    df_storage_query, df_storage_addref, df_storage_release, df_storage_create_stream, df_storage_open_stream,
    df_storage_create_storage, df_storage_open_storage, df_storage_copy_to, df_storage_move_element_to,
    df_storage_commit, df_storage_revert, df_storage_enum_elements, df_storage_destroy_element,
    df_storage_rename_element, df_storage_set_element_times, df_storage_set_class, df_storage_set_state_bits,
    df_storage_stat};

/* the node is referenced once more for the object, and so is the element it is a copy of. */
static HRESULT storage_open_object(DOC *doc, NODE *node, NODE *original, DWORD mode, BOOL root, IStorage **out)
{
    DF_STORAGE *s = OleAllocZero(sizeof(*s));
    if (!s)
        return STG_E_INSUFFICIENTMEMORY;
    s->Interface.lpVtbl = &g_storage_vtbl;
    s->Refs = 1;
    s->Doc = doc;
    s->Node = node;
    s->Original = original;
    s->Mode = mode;
    s->Root = root;
    doc_addref(doc);
    node_addref(node);
    if (original)
        node_addref(original);
    *out = &s->Interface;
    return S_OK;
}

/* ------------------------------------------------------------------ the file as byte array */

typedef struct {
    ILockBytes Interface;
    LONG Refs;
    SRWLOCK Lock;
    HANDLE File;
    PWSTR Path;
    BOOL Delete;
} FILE_BYTES;

static const ILockBytesVtbl g_file_bytes_vtbl;

static HRESULT file_error(DWORD error)
{
    switch (error) {
    case ERROR_ACCESS_DENIED:
        return STG_E_ACCESSDENIED;
    case ERROR_SHARING_VIOLATION:
        return STG_E_SHAREVIOLATION;
    case ERROR_LOCK_VIOLATION:
        return STG_E_LOCKVIOLATION;
    case ERROR_FILE_NOT_FOUND:
        return STG_E_FILENOTFOUND;
    case ERROR_PATH_NOT_FOUND:
        return STG_E_PATHNOTFOUND;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        return STG_E_FILEALREADYEXISTS;
    case ERROR_TOO_MANY_OPEN_FILES:
        return STG_E_TOOMANYOPENFILES;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        return STG_E_MEDIUMFULL;
    default:
        return STG_E_ACCESSDENIED;
    }
}

static HRESULT WINAPI fb_query(ILockBytes *self, REFIID iid, void **result)
{
    if (!result)
        return STG_E_INVALIDPOINTER;
    *result = nullptr;
    if (!iid || (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_ILockBytes)))
        return E_NOINTERFACE;
    *result = self;
    self->lpVtbl->AddRef(self);
    return S_OK;
}

static ULONG WINAPI fb_addref(ILockBytes *self)
{
    return (ULONG)InterlockedIncrement(&((FILE_BYTES *)self)->Refs);
}

static ULONG WINAPI fb_release(ILockBytes *self)
{
    FILE_BYTES *b = (FILE_BYTES *)self;
    LONG refs = InterlockedDecrement(&b->Refs);
    if (!refs) {
        CloseHandle(b->File);
        if (b->Delete && b->Path)
            DeleteFileW(b->Path);
        OleFree(b->Path);
        OleFree(b);
    }
    return (ULONG)refs;
}

static HRESULT WINAPI fb_read_at(ILockBytes *self, ULARGE_INTEGER offset, void *buffer, ULONG size, ULONG *read)
{
    FILE_BYTES *b = (FILE_BYTES *)self;
    if (read)
        *read = 0;
    if (!buffer && size)
        return STG_E_INVALIDPOINTER;
    AcquireSRWLockExclusive(&b->Lock);
    LARGE_INTEGER at = {.QuadPart = (LONGLONG)offset.QuadPart};
    DWORD got = 0;
    HRESULT hr = S_OK;
    if (!SetFilePointerEx(b->File, at, nullptr, FILE_BEGIN) || !ReadFile(b->File, buffer, size, &got, nullptr))
        hr = STG_E_READFAULT;
    ReleaseSRWLockExclusive(&b->Lock);
    if (read)
        *read = got;
    return hr;
}

static HRESULT WINAPI fb_write_at(ILockBytes *self, ULARGE_INTEGER offset, const void *buffer, ULONG size,
                                  ULONG *written)
{
    FILE_BYTES *b = (FILE_BYTES *)self;
    if (written)
        *written = 0;
    if (!buffer && size)
        return STG_E_INVALIDPOINTER;
    AcquireSRWLockExclusive(&b->Lock);
    LARGE_INTEGER at = {.QuadPart = (LONGLONG)offset.QuadPart};
    DWORD put = 0;
    HRESULT hr = S_OK;
    if (!SetFilePointerEx(b->File, at, nullptr, FILE_BEGIN) || !WriteFile(b->File, buffer, size, &put, nullptr))
        hr = file_error(GetLastError());
    ReleaseSRWLockExclusive(&b->Lock);
    if (written)
        *written = put;
    return hr;
}

static HRESULT WINAPI fb_flush(ILockBytes *self)
{
    FILE_BYTES *b = (FILE_BYTES *)self;
    return FlushFileBuffers(b->File) ? S_OK : file_error(GetLastError());
}

static HRESULT WINAPI fb_set_size(ILockBytes *self, ULARGE_INTEGER size)
{
    FILE_BYTES *b = (FILE_BYTES *)self;
    AcquireSRWLockExclusive(&b->Lock);
    LARGE_INTEGER at = {.QuadPart = (LONGLONG)size.QuadPart};
    HRESULT hr = S_OK;
    if (!SetFilePointerEx(b->File, at, nullptr, FILE_BEGIN) || !SetEndOfFile(b->File))
        hr = file_error(GetLastError());
    ReleaseSRWLockExclusive(&b->Lock);
    return hr;
}

static HRESULT WINAPI fb_lock(ILockBytes *self, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type)
{
    return STG_E_INVALIDFUNCTION;
}

static HRESULT WINAPI fb_stat(ILockBytes *self, STATSTG *stat, DWORD flags)
{
    FILE_BYTES *b = (FILE_BYTES *)self;
    if (!stat)
        return STG_E_INVALIDPOINTER;
    memset(stat, 0, sizeof(*stat));
    LARGE_INTEGER size;
    if (!GetFileSizeEx(b->File, &size))
        return file_error(GetLastError());
    stat->type = STGTY_LOCKBYTES;
    stat->cbSize.QuadPart = (ULONGLONG)size.QuadPart;
    GetFileTime(b->File, &stat->ctime, &stat->atime, &stat->mtime);
    if (!(flags & STATFLAG_NONAME) && b->Path) {
        SIZE_T bytes = (wcslen(b->Path) + 1) * sizeof(WCHAR);
        stat->pwcsName = CoTaskMemAlloc(bytes);
        if (!stat->pwcsName)
            return STG_E_INSUFFICIENTMEMORY;
        memcpy(stat->pwcsName, b->Path, bytes);
    }
    return S_OK;
}

static const ILockBytesVtbl g_file_bytes_vtbl = {fb_query, fb_addref, fb_release, fb_read_at, fb_write_at,
                                                 fb_flush, fb_set_size, fb_lock, fb_lock, fb_stat};

/* ------------------------------------------------------------------ making and opening documents */

static DWORD share_of(DWORD mode)
{
    switch (mode & STGM_SHARE_MASK) {
    case STGM_SHARE_EXCLUSIVE:
        return 0;
    case STGM_SHARE_DENY_WRITE:
        return FILE_SHARE_READ;
    case STGM_SHARE_DENY_READ:
        return FILE_SHARE_WRITE;
    default:
        return FILE_SHARE_READ | FILE_SHARE_WRITE;
    }
}

static PWSTR copy_text(PCWSTR text)
{
    SIZE_T bytes = (wcslen(text) + 1) * sizeof(WCHAR);
    PWSTR copy = OleAlloc(bytes);
    if (copy)
        memcpy(copy, text, bytes);
    return copy;
}

static HRESULT doc_start(ILockBytes *bytes, NODE *root, DWORD mode, BOOL writable, PCWSTR path, BOOL delete_on_release,
                         IStorage **out)
{
    DOC *doc = OleAllocZero(sizeof(*doc));
    if (!doc) {
        node_release(root);
        return STG_E_INSUFFICIENTMEMORY;
    }
    doc->Refs = 1;
    InitializeSRWLock(&doc->Lock);
    doc->Bytes = bytes;
    bytes->lpVtbl->AddRef(bytes);
    doc->Root = root;
    doc->Mode = mode;
    doc->Writable = writable;
    doc->Transacted = (mode & STGM_TRANSACTED) != 0;
    doc->DeleteOnRelease = delete_on_release;
    if (path)
        doc->Path = copy_text(path);
    HRESULT hr = S_OK;
    if (writable && !root->Count && !root->Size) {
        /* a new file is a compound file at once. */
        hr = doc_write(doc);
    }
    if (SUCCEEDED(hr))
        hr = storage_open_object(doc, root, nullptr, mode, TRUE, out);
    doc_release(doc);
    return hr;
}

static HRESULT create_on_bytes(ILockBytes *bytes, DWORD mode, PCWSTR path, BOOL delete_on_release, IStorage **out)
{
    NODE *root = node_new(L"Root Entry", STGTY_STORAGE);
    if (!root)
        return STG_E_INSUFFICIENTMEMORY;
    return doc_start(bytes, root, mode | STGM_READWRITE, TRUE, path, delete_on_release, out);
}

static HRESULT open_on_bytes(ILockBytes *bytes, DWORD mode, PCWSTR path, BOOL delete_on_release, IStorage **out)
{
    STATSTG stat;
    HRESULT hr = bytes->lpVtbl->Stat(bytes, &stat, STATFLAG_NONAME);
    if (FAILED(hr))
        return hr;
    if (stat.cbSize.QuadPart < 8 || stat.cbSize.QuadPart > 0x7FFFFFFFull)
        return stat.cbSize.QuadPart < 8 ? STG_E_FILEALREADYEXISTS : STG_E_MEDIUMFULL;
    BYTE *image = OleAlloc((SIZE_T)stat.cbSize.QuadPart);
    if (!image)
        return STG_E_INSUFFICIENTMEMORY;
    ULONGLONG done = 0;
    ULONG got = 1;
    while (done < stat.cbSize.QuadPart && got) {
        ULONGLONG left = stat.cbSize.QuadPart - done;
        ULONG chunk = left > 0x100000 ? 0x100000 : (ULONG)left;
        ULARGE_INTEGER at = {.QuadPart = done};
        got = 0;
        hr = bytes->lpVtbl->ReadAt(bytes, at, image + done, chunk, &got);
        if (FAILED(hr)) {
            OleFree(image);
            return hr;
        }
        done += got;
    }
    NODE *root = nullptr;
    if (memcmp(image, g_signature, 8))
        hr = STG_E_FILEALREADYEXISTS;
    else
        hr = parse_image(image, done, &root);
    OleFree(image);
    if (FAILED(hr))
        return hr;
    BOOL writable = (mode & (STGM_WRITE | STGM_READWRITE)) != 0;
    return doc_start(bytes, root, mode, writable, path, delete_on_release, out);
}

static HRESULT file_bytes_open(PCWSTR name, DWORD access, DWORD share, DWORD disposition, BOOL delete_on_release,
                               ILockBytes **out)
{
    HANDLE file = CreateFileW(name, access, share, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return file_error(GetLastError());
    FILE_BYTES *b = OleAllocZero(sizeof(*b));
    if (!b) {
        CloseHandle(file);
        return STG_E_INSUFFICIENTMEMORY;
    }
    b->Interface.lpVtbl = &g_file_bytes_vtbl;
    b->Refs = 1;
    InitializeSRWLock(&b->Lock);
    b->File = file;
    b->Path = copy_text(name);
    b->Delete = delete_on_release;
    *out = &b->Interface;
    return S_OK;
}

WINOLEAPI HRESULT WINAPI StgCreateDocfile(LPCOLESTR name, DWORD mode, DWORD reserved, IStorage **storage)
{
    if (!storage)
        return STG_E_INVALIDPOINTER;
    *storage = nullptr;
    if (reserved)
        return STG_E_INVALIDPARAMETER;
    WCHAR temporary[MAX_PATH];
    BOOL delete_on_release = (mode & STGM_DELETEONRELEASE) != 0;
    if (!name) {
        /* with no name the file is made in the temp folder and goes with the storage. */
        WCHAR folder[MAX_PATH];
        if (!GetTempPathW(MAX_PATH, folder) || !GetTempFileNameW(folder, L"~DF", 0, temporary))
            return STG_E_INSUFFICIENTMEMORY;
        name = temporary;
        delete_on_release = TRUE;
        mode |= STGM_CREATE;
    }
    ILockBytes *bytes = nullptr;
    HRESULT hr = file_bytes_open(name, GENERIC_READ | GENERIC_WRITE, share_of(mode),
                                 (mode & STGM_CREATE) ? CREATE_ALWAYS : CREATE_NEW, delete_on_release, &bytes);
    if (FAILED(hr))
        return hr;
    hr = create_on_bytes(bytes, mode, name, delete_on_release, storage);
    bytes->lpVtbl->Release(bytes);
    return hr;
}

WINOLEAPI HRESULT WINAPI StgOpenStorage(LPCOLESTR name, IStorage *priority, DWORD mode, SNB exclude, DWORD reserved,
                                        IStorage **storage)
{
    if (!storage)
        return STG_E_INVALIDPOINTER;
    *storage = nullptr;
    if (!name)
        return STG_E_INVALIDNAME;
    if (reserved || priority)
        return STG_E_INVALIDPARAMETER;
    BOOL writable = (mode & (STGM_WRITE | STGM_READWRITE)) != 0;
    ILockBytes *bytes = nullptr;
    HRESULT hr = file_bytes_open(name, writable ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ, share_of(mode),
                                 OPEN_EXISTING, (mode & STGM_DELETEONRELEASE) != 0, &bytes);
    if (FAILED(hr))
        return hr;
    hr = open_on_bytes(bytes, mode, name, (mode & STGM_DELETEONRELEASE) != 0, storage);
    bytes->lpVtbl->Release(bytes);
    return hr;
}

WINOLEAPI HRESULT WINAPI StgCreateDocfileOnILockBytes(ILockBytes *bytes, DWORD mode, DWORD reserved, IStorage **storage)
{
    if (!storage)
        return STG_E_INVALIDPOINTER;
    *storage = nullptr;
    if (!bytes)
        return STG_E_INVALIDPOINTER;
    if (reserved)
        return STG_E_INVALIDPARAMETER;
    return create_on_bytes(bytes, mode, nullptr, FALSE, storage);
}

WINOLEAPI HRESULT WINAPI StgOpenStorageOnILockBytes(ILockBytes *bytes, IStorage *priority, DWORD mode, SNB exclude,
                                                    DWORD reserved, IStorage **storage)
{
    if (!storage)
        return STG_E_INVALIDPOINTER;
    *storage = nullptr;
    if (!bytes)
        return STG_E_INVALIDPOINTER;
    if (reserved || priority)
        return STG_E_INVALIDPARAMETER;
    return open_on_bytes(bytes, mode, nullptr, FALSE, storage);
}
