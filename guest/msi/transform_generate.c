#include "msi_private.h"

static const BYTE transform_class[16] = {0x82, 0x10, 0x0c, 0, 0, 0, 0, 0, 0xc0, 0, 0, 0, 0, 0, 0, 0x46};

typedef struct MST_BUFFER {
    BYTE *Data;
    SIZE_T Size, Capacity;
} MST_BUFFER;

typedef struct MST_STRING {
    PCWSTR Text;
    ULONG References;
} MST_STRING;

typedef struct MST_POOL {
    MST_STRING *Strings;
    ULONG *Index;
    ULONG Count, Capacity;
} MST_POOL;

static void put_number(BYTE *data, ULONG value, ULONG width)
{
    for (ULONG i = 0; i < width; i++)
        data[i] = (BYTE)(value >> (8 * i));
}

static UINT append(MST_BUFFER *buffer, const void *data, SIZE_T size)
{
    if (size > SIZE_MAX - buffer->Size)
        return ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T required = buffer->Size + size;
    if (required > buffer->Capacity) {
        SIZE_T capacity = buffer->Capacity ? buffer->Capacity : 128;
        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) {
                capacity = required;
                break;
            }
            capacity *= 2;
        }
        BYTE *copy = MimeMsiAllocate(capacity);
        if (!copy)
            return ERROR_NOT_ENOUGH_MEMORY;
        if (buffer->Size)
            memcpy(copy, buffer->Data, buffer->Size);
        MimeMsiFree(buffer->Data);
        buffer->Data = copy;
        buffer->Capacity = capacity;
    }
    if (size) {
        if (data)
            memcpy(buffer->Data + buffer->Size, data, size);
        else
            memset(buffer->Data + buffer->Size, 0, size);
    }
    buffer->Size = required;
    return ERROR_SUCCESS;
}

static UINT append_number(MST_BUFFER *buffer, ULONG value, ULONG width)
{
    BYTE data[4];
    put_number(data, value, width);
    return append(buffer, data, width);
}

static void free_nodes(MIME_CFB_NODE *nodes)
{
    while (nodes) {
        MIME_CFB_NODE *node = nodes;
        nodes = node->Next;
        free_nodes(node->Children);
        MimeMsiFree((void *)node->Data);
        MimeMsiFree(node);
    }
}

/* Takes ownership of data, including on failure. */
static UINT put_stream(MIME_CFB_NODE **nodes, PCWSTR name, BYTE *data, SIZE_T size)
{
    MIME_CFB_NODE *node = MimeMsiAllocate(sizeof(*node));
    if (!node) {
        MimeMsiFree(data);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    memcpy(node->Name, name, (wcslen(name) + 1) * sizeof(WCHAR));
    node->Type = 2;
    node->Data = data;
    node->Size = size;
    node->Next = *nodes;
    *nodes = node;
    return ERROR_SUCCESS;
}

static UINT put_table_stream(MIME_CFB_NODE **nodes, PCWSTR name, MST_BUFFER *buffer)
{
    WCHAR encoded[32];
    if (!MimeMsiEncodeName(name, TRUE, encoded))
        return ERROR_INSTALL_PACKAGE_INVALID;
    BYTE *data = buffer->Data;
    SIZE_T size = buffer->Size;
    memset(buffer, 0, sizeof(*buffer));
    return put_stream(nodes, encoded, data, size);
}

static ULONG hash_text(PCWSTR text)
{
    ULONG hash = 2166136261u;
    while (*text)
        hash = (hash ^ *text++) * 16777619u;
    return hash;
}

static UINT pool_string(MST_POOL *pool, PCWSTR text, ULONG *id)
{
    if (!text || !*text)
        return ERROR_INVALID_DATA;
    if (!pool->Capacity || pool->Count == pool->Capacity) {
        /* A two-byte transform string ID is independent of both databases' IDs. */
        if (pool->Count == 65535)
            return ERROR_CALL_NOT_IMPLEMENTED;
        ULONG capacity = pool->Capacity ? pool->Capacity * 2 : 32;
        MST_STRING *strings = MimeMsiAllocate((SIZE_T)capacity * sizeof(*strings));
        ULONG *index = MimeMsiAllocate((SIZE_T)capacity * 2 * sizeof(*index));
        if (!strings || !index) {
            MimeMsiFree(strings);
            MimeMsiFree(index);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        if (pool->Count)
            memcpy(strings, pool->Strings, (SIZE_T)pool->Count * sizeof(*strings));
        for (ULONG i = 0; i < pool->Count; i++) {
            ULONG slot = hash_text(strings[i].Text) & (capacity * 2 - 1);
            while (index[slot])
                slot = (slot + 1) & (capacity * 2 - 1);
            index[slot] = i + 1;
        }
        MimeMsiFree(pool->Strings);
        MimeMsiFree(pool->Index);
        pool->Strings = strings;
        pool->Index = index;
        pool->Capacity = capacity;
    }
    ULONG slot = hash_text(text) & (pool->Capacity * 2 - 1);
    while (pool->Index[slot]) {
        ULONG existing = pool->Index[slot];
        if (!wcscmp(text, pool->Strings[existing - 1].Text)) {
            if (pool->Strings[existing - 1].References < 65535)
                pool->Strings[existing - 1].References++;
            *id = existing;
            return ERROR_SUCCESS;
        }
        slot = (slot + 1) & (pool->Capacity * 2 - 1);
    }
    if (pool->Count == 65535)
        return ERROR_CALL_NOT_IMPLEMENTED;
    pool->Strings[pool->Count] = (MST_STRING){text, 1};
    *id = pool->Index[slot] = ++pool->Count;
    return ERROR_SUCCESS;
}

/* A best-fit conversion is data loss too: require a lossless round trip. */
static UINT encode_text(PCWSTR text, UINT codepage, BOOL terminate, BYTE **out, ULONG *size)
{
    *out = nullptr;
    SIZE_T length = wcslen(text) + !!terminate;
    if (length > INT32_MAX)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!length) {
        *size = 0;
        return ERROR_SUCCESS;
    }
    int bytes = WideCharToMultiByte(codepage, 0, text, (int)length, nullptr, 0, nullptr, nullptr);
    if (!bytes)
        return GetLastError() ? GetLastError() : ERROR_NO_UNICODE_TRANSLATION;
    BYTE *data = MimeMsiAllocate((SIZE_T)bytes);
    PWSTR back = MimeMsiAllocate((length + 1) * sizeof(WCHAR));
    UINT error = !data || !back ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
    if (!error && (WideCharToMultiByte(codepage, 0, text, (int)length, (PSTR)data, bytes, nullptr, nullptr) != bytes ||
                   MultiByteToWideChar(codepage, 0, (PCSTR)data, bytes, back, (int)length) != (int)length ||
                   memcmp(text, back, length * sizeof(WCHAR))))
        error = ERROR_NO_UNICODE_TRANSLATION;
    MimeMsiFree(back);
    if (error)
        MimeMsiFree(data);
    else {
        *out = data;
        *size = (ULONG)bytes;
    }
    return error;
}

static UINT write_pool(MST_POOL *pool, UINT codepage, MIME_CFB_NODE **nodes)
{
    MST_BUFFER lengths = {}, strings = {};
    UINT error = append_number(&lengths, codepage, 4);
    for (ULONG i = 0; !error && i < pool->Count; i++) {
        BYTE *data = nullptr;
        ULONG size = 0, references = pool->Strings[i].References;
        error = encode_text(pool->Strings[i].Text, codepage, FALSE, &data, &size);
        if (!error)
            error = append_number(&lengths, (size > 65535 ? 0 : size) | (references << 16), 4);
        if (!error && size > 65535)
            error = append_number(&lengths, size, 4);
        if (!error)
            error = append(&strings, data, size);
        MimeMsiFree(data);
    }
    if (!error)
        error = put_table_stream(nodes, L"_StringPool", &lengths);
    if (!error)
        error = put_table_stream(nodes, L"_StringData", &strings);
    MimeMsiFree(lengths.Data);
    MimeMsiFree(strings.Data);
    return error;
}

static BOOL is_text(USHORT type)
{
    return (type & 0x0c00) == 0x0c00;
}

static BOOL is_binary(USHORT type)
{
    return (type & 0x0fff) == 0x0900;
}

static UINT validate_table(MIME_MSI_DATABASE *database, const MIME_MSI_TABLE *table)
{
    if (!table->Count || table->Count > 64 || (table->Rows && !table->Data))
        return ERROR_INSTALL_PACKAGE_INVALID;
    BOOL keys = FALSE;
    for (ULONG c = 0; c < table->Count; c++) {
        const MIME_MSI_COLUMN *column = &table->Columns[c];
        USHORT type = column->Type;
        keys |= !!(type & 0x2000);
        if (!column->Name || (column->Width != 2 && column->Width != 3 && column->Width != 4))
            return ERROR_INSTALL_PACKAGE_INVALID;
        if (!is_text(type) && !is_binary(type) && ((type & 0x0800) || column->Width == 3))
            return ERROR_CALL_NOT_IMPLEMENTED;
        if (is_binary(type) && ((type & 0x2000) || column->Width != 2))
            return ERROR_CALL_NOT_IMPLEMENTED;
        for (ULONG row = 0; row < table->Rows; row++) {
            ULONG value = MimeMsiCell(table, row, c);
            if (!value && !(type & 0x1000))
                return ERROR_INVALID_DATA;
            if (is_text(type) && value && (value >= database->StringCount || !database->Strings[value] ||
                                          !*database->Strings[value]))
                return ERROR_INVALID_DATA;
        }
    }
    return keys ? ERROR_SUCCESS : ERROR_CALL_NOT_IMPLEMENTED;
}

static BOOL equal_cell(MIME_MSI_DATABASE *a, const MIME_MSI_TABLE *ta, ULONG ra,
                       MIME_MSI_DATABASE *b, const MIME_MSI_TABLE *tb, ULONG rb, ULONG c)
{
    ULONG va = MimeMsiCell(ta, ra, c), vb = MimeMsiCell(tb, rb, c);
    if (!va || !vb)
        return va == vb;
    if (is_text(ta->Columns[c].Type))
        return !wcscmp(a->Strings[va], b->Strings[vb]);
    if (is_binary(ta->Columns[c].Type))
        return TRUE; /* Presence only; the effective binary streams are compared separately. */
    return MimeMsiInteger(ta, ra, c) == MimeMsiInteger(tb, rb, c);
}

static BOOL equal_key(MIME_MSI_DATABASE *a, const MIME_MSI_TABLE *ta, ULONG ra,
                      MIME_MSI_DATABASE *b, const MIME_MSI_TABLE *tb, ULONG rb)
{
    for (ULONG c = 0; c < ta->Count; c++)
        if ((ta->Columns[c].Type & 0x2000) && !equal_cell(a, ta, ra, b, tb, rb, c))
            return FALSE;
    return TRUE;
}

static ULONG row_hash(MIME_MSI_DATABASE *database, const MIME_MSI_TABLE *table, ULONG row)
{
    ULONG hash = 2166136261u;
    for (ULONG c = 0; c < table->Count; c++) {
        if (!(table->Columns[c].Type & 0x2000))
            continue;
        ULONG value = MimeMsiCell(table, row, c);
        if (is_text(table->Columns[c].Type))
            value = value ? hash_text(database->Strings[value]) : 0;
        else
            value = (ULONG)MimeMsiInteger(table, row, c);
        hash = (hash ^ value) * 16777619u;
    }
    return hash;
}

typedef struct MST_INDEX {
    MIME_MSI_DATABASE *Database;
    const MIME_MSI_TABLE *Table;
    ULONG *Slots;
    SIZE_T Capacity;
} MST_INDEX;

static SIZE_T lookup(const MST_INDEX *index, MIME_MSI_DATABASE *database, const MIME_MSI_TABLE *table, ULONG row)
{
    SIZE_T slot = row_hash(database, table, row) & (index->Capacity - 1);
    while (index->Slots[slot] && !equal_key(database, table, row, index->Database, index->Table, index->Slots[slot] - 1))
        slot = (slot + 1) & (index->Capacity - 1);
    return slot;
}

static UINT build_index(MST_INDEX *index, MIME_MSI_DATABASE *database, const MIME_MSI_TABLE *table)
{
    *index = (MST_INDEX){database, table, nullptr, 1};
    if ((SIZE_T)table->Rows > (SIZE_MAX / sizeof(ULONG) - 1) / 2)
        return ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T required = (SIZE_T)table->Rows * 2 + 1;
    while (index->Capacity < required) {
        if (index->Capacity > SIZE_MAX / sizeof(ULONG) / 2)
            return ERROR_NOT_ENOUGH_MEMORY;
        index->Capacity *= 2;
    }
    index->Slots = MimeMsiAllocate(index->Capacity * sizeof(ULONG));
    if (!index->Slots)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (ULONG row = 0; row < table->Rows; row++) {
        SIZE_T slot = lookup(index, database, table, row);
        if (index->Slots[slot])
            return ERROR_INVALID_DATA; /* Ambiguous/duplicate primary key. */
        index->Slots[slot] = row + 1;
    }
    return ERROR_SUCCESS;
}

static UINT emit_row(MST_BUFFER *bytes, MST_POOL *pool, MIME_MSI_DATABASE *database,
                     const MIME_MSI_TABLE *table, ULONG row, ULONG mask)
{
    UINT error = append_number(bytes, mask, 2);
    for (ULONG c = 0; !error && c < table->Count; c++) {
        USHORT type = table->Columns[c].Type;
        if (!(mask & 1) && !(mask & (1u << c)) && !(type & 0x2000))
            continue;
        ULONG value = MimeMsiCell(table, row, c), width = table->Columns[c].Width;
        if (is_text(type)) {
            width = 2;
            if (value)
                error = pool_string(pool, database->Strings[value], &value);
        }
        if (!error)
            error = append_number(bytes, value, width);
    }
    return error;
}

static BOOL binary_row(const MIME_MSI_TABLE *table, ULONG row)
{
    for (ULONG c = 0; c < table->Count; c++)
        if (is_binary(table->Columns[c].Type) && MimeMsiCell(table, row, c))
            return TRUE;
    return FALSE;
}

/* Schemas may contain more than 16 columns, but only unchanged such tables are supported by the current apply code. */
static UINT diff_table(MIME_MSI_DATABASE *database, MIME_MSI_DATABASE *reference, PCWSTR name,
                       BOOL schema, MST_POOL *pool, MIME_CFB_NODE **nodes, BOOL *changed)
{
    MIME_MSI_TABLE *target, *source;
    UINT error = MimeMsiLoadTable(database, name, &target);
    if (!error)
        error = MimeMsiLoadTable(reference, name, &source);
    if (error)
        return error;
    if (target->Count != source->Count)
        return ERROR_CALL_NOT_IMPLEMENTED;
    if (!source->Count || source->Count > 64)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG c = 0; c < source->Count; c++)
        if (target->Columns[c].Type != source->Columns[c].Type || !target->Columns[c].Name || !source->Columns[c].Name ||
            wcscmp(target->Columns[c].Name, source->Columns[c].Name) ||
            (!is_text(source->Columns[c].Type) && target->Columns[c].Width != source->Columns[c].Width))
            return ERROR_CALL_NOT_IMPLEMENTED;
    error = validate_table(database, target);
    if (!error)
        error = validate_table(reference, source);
    MST_INDEX a = {}, b = {};
    if (!error)
        error = build_index(&a, database, target);
    if (!error)
        error = build_index(&b, reference, source);
    MST_BUFFER bytes = {};
    for (ULONG row = 0; !error && row < source->Rows; row++) {
        ULONG found = a.Slots[lookup(&a, reference, source, row)];
        uint64_t mask = 0;
        if (found)
            for (ULONG c = 0; c < source->Count; c++)
                if (!equal_cell(reference, source, row, database, target, found - 1, c))
                    mask |= (uint64_t)1 << c;
        if (found && !mask)
            continue;
        if (schema || source->Count > 16) {
            error = ERROR_CALL_NOT_IMPLEMENTED;
            break;
        }
        /* Bit zero denotes insertion. A non-key first-column update must replace the row. */
        BOOL replace = !!(mask & 1);
        if ((!found || replace) && binary_row(source, row)) {
            error = ERROR_CALL_NOT_IMPLEMENTED;
            break;
        }
        for (ULONG c = 0; c < source->Count; c++)
            if ((mask & ((uint64_t)1 << c)) && is_binary(source->Columns[c].Type))
                error = ERROR_CALL_NOT_IMPLEMENTED;
        if (error)
            break;
        if (replace && binary_row(target, found - 1)) {
            error = ERROR_CALL_NOT_IMPLEMENTED;
            break;
        }
        *changed = TRUE;
        if (!found || replace)
            error = emit_row(&bytes, pool, reference, source, row, 0);
        if (!error && found)
            error = emit_row(&bytes, pool, database, target, found - 1,
                             replace ? (target->Count << 8) | 1 : (ULONG)mask);
    }
    for (ULONG row = 0; !error && row < target->Rows; row++) {
        if (b.Slots[lookup(&b, database, target, row)])
            continue;
        if (schema || target->Count > 16 || binary_row(target, row)) {
            error = ERROR_CALL_NOT_IMPLEMENTED;
            break;
        }
        *changed = TRUE;
        error = emit_row(&bytes, pool, database, target, row, (target->Count << 8) | 1);
    }
    if (!error && bytes.Size)
        error = put_table_stream(nodes, name, &bytes);
    MimeMsiFree(bytes.Data);
    MimeMsiFree(a.Slots);
    MimeMsiFree(b.Slots);
    if (error)
        DbgPrint("msi: transform table %ws failed: %u\n", name, error);
    return error;
}

static BOOL table_or_summary(PCWSTR name)
{
    return name[0] == 0x4840 || !wcscmp(name, L"\x0005SummaryInformation");
}

/* Used both to preserve every MST stream during summary replacement and to detect unsupported binary/storage changes.
 * At the database root, table bytes and MSI summary bytes are deliberately not compared as opaque streams. */
static UINT collect_nodes(const MIME_CFB *file, ULONG index, MIME_CFB_NODE **nodes,
                          SIZE_T *budget, ULONG depth, BOOL binaries)
{
    if (index == UINT32_MAX)
        return ERROR_SUCCESS;
    MIME_CFB_ENTRY entry;
    if (!*budget || depth > 512 || !MimeCfbEntry(file, index, &entry))
        return ERROR_INSTALL_PACKAGE_INVALID;
    --*budget;
    UINT error = collect_nodes(file, entry.Left, nodes, budget, depth + 1, binaries);
    if (!error)
        error = collect_nodes(file, entry.Right, nodes, budget, depth + 1, binaries);
    if (error || (binaries && entry.Type == 2 && table_or_summary((PCWSTR)entry.Name)))
        return error;
    if ((entry.Type != 1 && entry.Type != 2) || !entry.Name[0])
        return ERROR_INSTALL_PACKAGE_INVALID;
    MIME_CFB_NODE *node = MimeMsiAllocate(sizeof(*node));
    if (!node)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(node->Name, entry.Name, sizeof(node->Name));
    memcpy(node->ClassId, file->Directory + (SIZE_T)index * 128 + 80, 16);
    node->Type = entry.Type;
    node->Next = *nodes;
    *nodes = node;
    if (entry.Type == 1)
        return collect_nodes(file, entry.Child, &node->Children, budget, depth + 1, FALSE);
    if (entry.Size > file->Size || entry.Size > SIZE_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    node->Size = (SIZE_T)entry.Size;
    node->Data = MimeMsiAllocate(node->Size ? node->Size : 1);
    if (!node->Data)
        return ERROR_NOT_ENOUGH_MEMORY;
    return MimeCfbRead(file, &entry, (void *)node->Data, node->Size) ? ERROR_SUCCESS : ERROR_INSTALL_PACKAGE_INVALID;
}

static MIME_CFB_NODE **find_node(MIME_CFB_NODE **nodes, PCWSTR name)
{
    for (; *nodes; nodes = &(*nodes)->Next)
        if (!_wcsicmp((PCWSTR)(*nodes)->Name, name))
            return nodes;
    return nullptr;
}

static UINT database_binaries(MIME_MSI_DATABASE *database, MIME_CFB_NODE **nodes)
{
    MIME_CFB_ENTRY root;
    if (!MimeCfbEntry(&database->File, database->Root, &root))
        return ERROR_INSTALL_PACKAGE_INVALID;
    SIZE_T budget = database->File.DirectorySize / 128;
    UINT error = collect_nodes(&database->File, root.Child, nodes, &budget, 0, TRUE);
    /* The first overlay wins, just as in MimeMsiReadStream. Do not silently compare the old on-disk blob. */
    for (MIME_MSI_STREAM *stream = database->Streams; !error && stream; stream = stream->Next) {
        if (table_or_summary(stream->Name))
            continue;
        BOOL seen = FALSE;
        for (MIME_MSI_STREAM *earlier = database->Streams; earlier != stream; earlier = earlier->Next)
            seen |= !_wcsicmp(earlier->Name, stream->Name);
        if (seen)
            continue;
        MIME_CFB_NODE **link = find_node(nodes, stream->Name);
        if (link) {
            MIME_CFB_NODE *old = *link;
            *link = old->Next;
            old->Next = nullptr;
            free_nodes(old);
        }
        BYTE *copy = MimeMsiAllocate(stream->Size ? stream->Size : 1);
        if (!copy)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else {
            if (stream->Size)
                memcpy(copy, stream->Data, stream->Size);
            error = put_stream(nodes, stream->Name, copy, stream->Size);
        }
    }
    return error;
}

static BOOL equal_nodes(MIME_CFB_NODE *a, MIME_CFB_NODE *b)
{
    ULONG na = 0, nb = 0;
    for (MIME_CFB_NODE *node = b; node; node = node->Next)
        nb++;
    for (MIME_CFB_NODE *node = a; node; node = node->Next) {
        na++;
        MIME_CFB_NODE **found = find_node(&b, (PCWSTR)node->Name);
        if (!found || node->Type != (*found)->Type || node->Size != (*found)->Size ||
            memcmp(node->ClassId, (*found)->ClassId, 16) ||
            (node->Size && memcmp(node->Data, (*found)->Data, node->Size)) ||
            !equal_nodes(node->Children, (*found)->Children))
            return FALSE;
    }
    return na == nb;
}

static BOOL known_table(MIME_MSI_DATABASE *database, const MIME_MSI_TABLE *tables, PCWSTR name)
{
    if (!wcscmp(name, L"_Tables") || !wcscmp(name, L"_Columns"))
        return TRUE;
    for (ULONG row = 0; row < tables->Rows; row++)
        if (!wcscmp(database->Strings[MimeMsiCell(tables, row, 0)], name))
            return TRUE;
    return FALSE;
}

/* Refuse orphan/cached-only table data rather than silently dropping a change outside the catalog. */
static UINT check_table_inventory(MIME_MSI_DATABASE *database)
{
    MIME_MSI_TABLE *tables, *columns;
    UINT error = MimeMsiLoadTable(database, L"_Tables", &tables);
    if (!error)
        error = MimeMsiLoadTable(database, L"_Columns", &columns);
    if (error)
        return error;
    for (MIME_MSI_TABLE *table = database->Tables; table; table = table->Next)
        if (!known_table(database, tables, table->Name))
            return ERROR_CALL_NOT_IMPLEMENTED;
    for (ULONG row = 0; row < columns->Rows; row++)
        if (!known_table(database, tables, database->Strings[MimeMsiCell(columns, row, 0)]))
            return ERROR_CALL_NOT_IMPLEMENTED;
    ULONG *children = nullptr, count = 0;
    error = MimeMsiChildren(database, &children, &count);
    for (ULONG i = 0; !error && i < count; i++) {
        MIME_CFB_ENTRY entry;
        if (!MimeCfbEntry(&database->File, children[i], &entry)) {
            error = ERROR_INSTALL_PACKAGE_INVALID;
            break;
        }
        if (entry.Type != 2 || entry.Name[0] != 0x4840)
            continue;
        WCHAR name[64];
        if (!MimeMsiDecodeTableName((PCWSTR)entry.Name, name))
            error = ERROR_INSTALL_PACKAGE_INVALID;
        else if (wcscmp(name, L"_StringPool") && wcscmp(name, L"_StringData") && !known_table(database, tables, name))
            error = ERROR_CALL_NOT_IMPLEMENTED;
    }
    for (MIME_MSI_STREAM *stream = database->Streams; !error && stream; stream = stream->Next) {
        if (stream->Name[0] != 0x4840)
            continue;
        WCHAR name[64];
        if (!MimeMsiDecodeTableName(stream->Name, name))
            error = ERROR_INSTALL_PACKAGE_INVALID;
        else if (!known_table(database, tables, name))
            error = ERROR_CALL_NOT_IMPLEMENTED;
    }
    MimeMsiFree(children);
    return error;
}

static UINT generate_nodes(MIME_MSI_DATABASE *database, MIME_MSI_DATABASE *reference, MIME_CFB_NODE **nodes)
{
    if (database->Root || reference->Root || database->Parent || reference->Parent || database->CodePage != reference->CodePage)
        return ERROR_CALL_NOT_IMPLEMENTED;
    MST_POOL pool = {};
    BOOL changed = FALSE;
    UINT error = diff_table(database, reference, L"_Tables", TRUE, &pool, nodes, &changed);
    if (!error)
        error = diff_table(database, reference, L"_Columns", TRUE, &pool, nodes, &changed);
    if (!error)
        error = check_table_inventory(database);
    if (!error)
        error = check_table_inventory(reference);
    if (error)
        DbgPrint("msi: transform schema/inventory failed: %u\n", error);
    MIME_MSI_TABLE *tables = nullptr;
    if (!error)
        error = MimeMsiLoadTable(reference, L"_Tables", &tables);
    for (ULONG row = 0; !error && row < tables->Rows; row++) {
        PCWSTR name = reference->Strings[MimeMsiCell(tables, row, 0)];
        if (!wcscmp(name, L"_Tables") || !wcscmp(name, L"_Columns"))
            continue;
        error = diff_table(database, reference, name, FALSE, &pool, nodes, &changed);
    }
    MIME_CFB_NODE *a = nullptr, *b = nullptr;
    if (!error)
        error = database_binaries(database, &a);
    if (!error)
        error = database_binaries(reference, &b);
    if (!error && !equal_nodes(a, b))
        error = ERROR_CALL_NOT_IMPLEMENTED;
    free_nodes(a);
    free_nodes(b);
    if (!error)
        error = changed ? write_pool(&pool, database->CodePage, nodes) : ERROR_NO_DATA;
    MimeMsiFree(pool.Strings);
    MimeMsiFree(pool.Index);
    return error;
}

static UINT full_path(PCWSTR path, PWSTR *out)
{
    *out = nullptr;
    DWORD size = GetFullPathNameW(path, 0, nullptr, nullptr);
    if (!size)
        return GetLastError() ? GetLastError() : ERROR_INVALID_NAME;
    if (size > 32767)
        return ERROR_FILENAME_EXCED_RANGE;
    PWSTR result = MimeMsiAllocate((SIZE_T)size * sizeof(WCHAR));
    if (!result)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD length = GetFullPathNameW(path, size, result, nullptr);
    UINT error = !length ? GetLastError() : length >= size ? ERROR_FILENAME_EXCED_RANGE : ERROR_SUCCESS;
    if (!length && !error)
        error = ERROR_INVALID_NAME;
    if (error)
        MimeMsiFree(result);
    else
        *out = result;
    return error;
}

static UINT file_identity(PCWSTR path, BY_HANDLE_FILE_INFORMATION *identity)
{
    HANDLE file = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError() ? GetLastError() : ERROR_OPEN_FAILED;
    UINT error = GetFileInformationByHandle(file, identity) ? ERROR_SUCCESS :
                 GetLastError() ? GetLastError() : ERROR_FUNCTION_FAILED;
    if (!CloseHandle(file) && !error)
        error = GetLastError() ? GetLastError() : ERROR_INVALID_HANDLE;
    return error;
}

static UINT output_path(MIME_MSI_DATABASE *database, MIME_MSI_DATABASE *reference, PCWSTR path, PWSTR *out)
{
    if (!path || !*path)
        return ERROR_INVALID_PARAMETER;
    UINT error = full_path(path, out);
    BY_HANDLE_FILE_INFORMATION destination = {};
    BOOL exists = FALSE;
    if (!error) {
        error = file_identity(*out, &destination);
        exists = !error;
        if (error == ERROR_FILE_NOT_FOUND)
            error = ERROR_SUCCESS;
    }
    MIME_MSI_DATABASE *inputs[] = {database, reference};
    for (ULONG i = 0; !error && i < RTL_NUMBER_OF(inputs); i++) {
        if (!inputs[i]->Path)
            continue;
        PWSTR source = nullptr;
        error = full_path(inputs[i]->Path, &source);
        if (!error && !_wcsicmp(source, *out))
            error = ERROR_INVALID_PARAMETER;
        /* Also refuse hard links, short names and alternate path spellings of either input. */
        if (!error && exists) {
            BY_HANDLE_FILE_INFORMATION input = {};
            error = file_identity(source, &input);
            if (!error && input.dwVolumeSerialNumber == destination.dwVolumeSerialNumber &&
                input.nFileIndexHigh == destination.nFileIndexHigh && input.nFileIndexLow == destination.nFileIndexLow)
                error = ERROR_INVALID_PARAMETER;
        }
        MimeMsiFree(source);
    }
    return error;
}

/* No CREATE_ALWAYS on the destination: an error must leave the previous transform intact. */
static UINT save_atomic(PCWSTR path, const BYTE *data, SIZE_T size)
{
    static LONG sequence;
    SIZE_T length = wcslen(path);
    if (length > 32700)
        return ERROR_FILENAME_EXCED_RANGE;
    PWSTR temporary = MimeMsiAllocate((length + 64) * sizeof(WCHAR));
    if (!temporary)
        return ERROR_NOT_ENOUGH_MEMORY;
    HANDLE file = INVALID_HANDLE_VALUE;
    UINT error = ERROR_FILE_EXISTS;
    for (ULONG attempt = 0; attempt < 64; attempt++) {
        int formatted = _snwprintf(temporary, length + 64, L"%ls.mime-%lu-%lu.tmp", path, GetCurrentProcessId(),
                                   (ULONG)InterlockedIncrement(&sequence));
        if (formatted < 0 || (SIZE_T)formatted >= length + 64) {
            error = ERROR_FILENAME_EXCED_RANGE;
            break;
        }
        file = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            error = ERROR_SUCCESS;
            break;
        }
        error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            break;
    }
    if (file != INVALID_HANDLE_VALUE) {
        SIZE_T offset = 0;
        while (!error && offset < size) {
            DWORD part = size - offset > 0x40000000 ? 0x40000000 : (DWORD)(size - offset), written = 0;
            if (!WriteFile(file, data + offset, part, &written, nullptr))
                error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
            else if (!written || written > part)
                error = ERROR_WRITE_FAULT;
            else
                offset += written;
        }
        if (!error && !FlushFileBuffers(file))
            error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        if (!CloseHandle(file) && !error)
            error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        if (!error && !MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            error = GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
        if (error)
            DeleteFileW(temporary);
    } else if (!error)
        error = ERROR_OPEN_FAILED;
    MimeMsiFree(temporary);
    return error;
}

UINT MimeMsiGenerateTransform(MSIHANDLE handle, MSIHANDLE reference_handle, PCWSTR path)
{
    MIME_CFB_NODE *nodes = nullptr;
    BYTE *image = nullptr;
    SIZE_T size = 0;
    PWSTR destination = nullptr;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_DATABASE *database = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
    MIME_MSI_DATABASE *reference = (void *)MimeMsiGetHandle(reference_handle, MIME_MSI_KIND_DATABASE);
    UINT error = !database || !reference ? ERROR_INVALID_HANDLE : ERROR_SUCCESS;
    if (!error && path)
        error = output_path(database, reference, path, &destination);
    if (!error)
        error = generate_nodes(database, reference, &nodes);
    if (!error && path && !MimeCfbWrite(nodes, transform_class, MimeMsiAllocate, MimeMsiFree, &image, &size))
        error = ERROR_NOT_ENOUGH_MEMORY;
    free_nodes(nodes);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!error && path)
        error = save_atomic(destination, image, size);
    MimeMsiFree(image);
    MimeMsiFree(destination);
    return error;
}

/* ------------------------------------------------------------------ transform summary */

typedef struct MST_SUMMARY_PROPERTY {
    ULONG Id, Type, Number;
    PCWSTR Text;
} MST_SUMMARY_PROPERTY;

static UINT build_summary(const MST_SUMMARY_PROPERTY *properties, ULONG count, BYTE **out, SIZE_T *size)
{
    static const BYTE format[16] = {0xe0, 0x85, 0x9f, 0xf2, 0xf9, 0x4f, 0x68, 0x10, 0xab, 0x91, 8, 0, 0x2b, 0x27, 0xb3, 0xd9};
    MST_BUFFER buffer = {};
    /* 48-byte property-set header, section header, then ID/offset pairs. */
    UINT error = append(&buffer, nullptr, 56 + (SIZE_T)count * 8);
    for (ULONG i = 0; !error && i < count; i++) {
        const MST_SUMMARY_PROPERTY *property = &properties[i];
        if (buffer.Size > UINT32_MAX) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        put_number(buffer.Data + 56 + (SIZE_T)i * 8, property->Id, 4);
        put_number(buffer.Data + 60 + (SIZE_T)i * 8, (ULONG)(buffer.Size - 48), 4);
        error = append_number(&buffer, property->Type, 4);
        if (!error && property->Type == 30) {
            BYTE *text = nullptr;
            ULONG bytes = 0;
            /* Required transform metadata is GUID/version/platform/language ASCII, encoded as Windows-1252. */
            error = encode_text(property->Text, 1252, TRUE, &text, &bytes);
            if (!error)
                error = append_number(&buffer, bytes, 4);
            if (!error)
                error = append(&buffer, text, bytes);
            if (!error)
                error = append(&buffer, nullptr, (4 - (buffer.Size & 3)) & 3);
            MimeMsiFree(text);
        } else if (!error)
            error = append_number(&buffer, property->Number, 4); /* VT_I2 plus two padding bytes, or VT_I4. */
    }
    if (!error && buffer.Size > UINT32_MAX)
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (error) {
        MimeMsiFree(buffer.Data);
        return error;
    }
    put_number(buffer.Data, 0xfffe, 2);
    put_number(buffer.Data + 4, 0x00020005, 4);
    put_number(buffer.Data + 24, 1, 4);
    memcpy(buffer.Data + 28, format, 16);
    put_number(buffer.Data + 44, 48, 4);
    put_number(buffer.Data + 48, (ULONG)(buffer.Size - 48), 4);
    put_number(buffer.Data + 52, count, 4);
    *out = buffer.Data;
    *size = buffer.Size;
    return ERROR_SUCCESS;
}

static UINT product_property(MIME_MSI_DATABASE *database, PCWSTR name, PCWSTR *value)
{
    *value = nullptr;
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(database, L"Property", &table);
    if (error)
        return error;
    error = validate_table(database, table);
    if (error)
        return error;
    ULONG key = MimeMsiColumn(table, L"Property", TRUE), field = MimeMsiColumn(table, L"Value", TRUE);
    if (key == UINT32_MAX || field == UINT32_MAX)
        return ERROR_INSTALL_PACKAGE_INVALID;
    for (ULONG row = 0; row < table->Rows; row++) {
        PCWSTR text = MimeMsiText(database, table, row, key);
        if (!text || wcscmp(text, name))
            continue;
        if (*value)
            return ERROR_INVALID_DATA;
        *value = MimeMsiText(database, table, row, field);
    }
    return ERROR_SUCCESS;
}

static BOOL valid_guid(PCWSTR text)
{
    WCHAR packed[33];
    return text && wcslen(text) == 38 && MimeMsiPackGuid(text, packed);
}

static BOOL valid_version(PCWSTR text)
{
    if (!text || !*text)
        return FALSE;
    /* MSI compares three fields; a fourth numeric field is permitted and ignored in version comparisons. */
    ULONG fields = 0;
    while (*text) {
        ULONG value = 0, digits = 0, limit = fields < 2 ? 255 : 65535;
        while (*text >= L'0' && *text <= L'9') {
            if (value > (limit - (*text - L'0')) / 10)
                return FALSE;
            value = value * 10 + (*text++ - L'0');
            digits++;
        }
        if (!digits || ++fields > 4)
            return FALSE;
        if (!*text)
            return fields >= 3;
        if (*text++ != L'.' || !*text)
            return FALSE;
    }
    return FALSE;
}

/* A transform Template has a single language, unlike a merge module template. */
static BOOL valid_template(PCWSTR text)
{
    if (!text)
        return FALSE;
    PCWSTR semi = wcschr(text, L';');
    if (!semi)
        return FALSE;
    for (PCWSTR p = text; p < semi; p++)
        if (*p > 0x7f || *p < 0x20 || *p == L',')
            return FALSE;
    ULONG language = 0;
    for (PCWSTR p = semi + 1; *p; p++) {
        if (*p < L'0' || *p > L'9' || language > (65535u - (*p - L'0')) / 10)
            return FALSE;
        language = language * 10 + (*p - L'0');
    }
    return TRUE;
}

static UINT transform_summary(MIME_MSI_DATABASE *database, MIME_MSI_DATABASE *reference,
                              int errors, int validation, BYTE **out, SIZE_T *size)
{
    /* The low six error flags and the documented validation bits; VIEWTRANSFORM is not a persisted condition. */
    if (((UINT)errors & ~0x3fu) || ((UINT)validation & ~0xffbu))
        return ERROR_INVALID_PARAMETER;
    UINT versions = (UINT)validation & 0x38, relations = (UINT)validation & 0x7c0;
    if ((versions && (versions & (versions - 1))) || (relations && (relations & (relations - 1))))
        return ERROR_INVALID_PARAMETER;
    MIME_MSI_SUMMARY_VALUE source_template = {}, target_template = {}, source_pages = {}, target_pages = {};
    UINT error = MimeMsiSummaryProperty(reference, 7, &source_template);
    if (!error)
        error = MimeMsiSummaryProperty(database, 7, &target_template);
    if (!error)
        error = MimeMsiSummaryProperty(reference, 14, &source_pages);
    if (!error)
        error = MimeMsiSummaryProperty(database, 14, &target_pages);
    if (!error && (source_template.Type != 30 || target_template.Type != 30 || !valid_template(source_template.Text) ||
                   !valid_template(target_template.Text) || source_pages.Type != 3 || target_pages.Type != 3 ||
                   source_pages.Number <= 0 || target_pages.Number <= 0))
        error = ERROR_INVALID_DATA;
    PCWSTR source_code = nullptr, target_code = nullptr, source_version = nullptr, target_version = nullptr;
    PCWSTR upgrade = nullptr, target_upgrade = nullptr;
    if (!error)
        error = product_property(reference, L"ProductCode", &source_code);
    if (!error)
        error = product_property(database, L"ProductCode", &target_code);
    if (!error)
        error = product_property(reference, L"ProductVersion", &source_version);
    if (!error)
        error = product_property(database, L"ProductVersion", &target_version);
    if (!error)
        error = product_property(reference, L"UpgradeCode", &upgrade);
    if (!error)
        error = product_property(database, L"UpgradeCode", &target_upgrade);
    if (!error && (!valid_guid(source_code) || !valid_guid(target_code) || !valid_version(source_version) ||
                   !valid_version(target_version) || (upgrade && !valid_guid(upgrade)) ||
                   (target_upgrade && !valid_guid(target_upgrade)) || ((validation & 0x800) && (!upgrade || !target_upgrade))))
        error = ERROR_INSTALL_PACKAGE_INVALID;
    PWSTR revision = nullptr;
    if (!error) {
        SIZE_T length = wcslen(source_code) + wcslen(target_code) + wcslen(source_version) + wcslen(target_version) +
                        (upgrade ? wcslen(upgrade) : 0) + 3;
        revision = MimeMsiAllocate(length * sizeof(WCHAR));
        if (!revision)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else
            _snwprintf(revision, length, L"%ls%ls;%ls%ls;%ls", source_code, source_version, target_code, target_version, upgrade ? upgrade : L"");
    }
    if (!error) {
        ULONG pages = (ULONG)(source_pages.Number > target_pages.Number ? source_pages.Number : target_pages.Number);
        MST_SUMMARY_PROPERTY properties[] = {
            {1, 2, 1252},
            {2, 30, 0, L"Transform"},
            {7, 30, 0, source_template.Text},
            {8, 30, 0, target_template.Text},
            {9, 30, 0, revision},
            {14, 3, pages},
            {16, 3, (UINT)errors | ((UINT)validation << 16)},
            {19, 3, 4},
        };
        error = build_summary(properties, RTL_NUMBER_OF(properties), out, size);
    }
    MimeMsiFree(revision);
    MimeMsiFree(source_template.Text);
    MimeMsiFree(target_template.Text);
    MimeMsiFree(source_pages.Text);
    MimeMsiFree(target_pages.Text);
    return error;
}

UINT MimeMsiCreateTransformSummaryInfo(MSIHANDLE handle, MSIHANDLE reference_handle, PCWSTR path, int errors, int validation)
{
    BYTE *summary = nullptr, *image = nullptr;
    SIZE_T summary_size = 0, image_size = 0;
    PWSTR destination = nullptr;
    MIME_CFB_NODE *nodes = nullptr;
    MIME_MSI_DATABASE *transform = nullptr;
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_DATABASE *database = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
    MIME_MSI_DATABASE *reference = (void *)MimeMsiGetHandle(reference_handle, MIME_MSI_KIND_DATABASE);
    UINT error = !database || !reference ? ERROR_INVALID_HANDLE : ERROR_SUCCESS;
    if (!error)
        error = output_path(database, reference, path, &destination);
    if (!error)
        error = transform_summary(database, reference, errors, validation, &summary, &summary_size);
    if (!error)
        error = MimeMsiOpenDatabaseFile(destination, TRUE, &transform);
    if (!error && (transform->File.DirectorySize < 128 || memcmp(transform->File.Directory + 80, transform_class, 16)))
        error = ERROR_INSTALL_PACKAGE_INVALID;
    if (!error) {
        MIME_CFB_ENTRY root;
        SIZE_T budget = transform->File.DirectorySize / 128;
        if (!MimeCfbEntry(&transform->File, 0, &root))
            error = ERROR_INSTALL_PACKAGE_INVALID;
        else
            error = collect_nodes(&transform->File, root.Child, &nodes, &budget, 0, FALSE);
    }
    if (!error) {
        MIME_CFB_NODE **link = find_node(&nodes, L"\x0005SummaryInformation");
        if (link) {
            MIME_CFB_NODE *old = *link;
            *link = old->Next;
            old->Next = nullptr;
            free_nodes(old);
        }
        error = put_stream(&nodes, L"\x0005SummaryInformation", summary, summary_size);
        summary = nullptr;
    }
    if (!error && !MimeCfbWrite(nodes, transform_class, MimeMsiAllocate, MimeMsiFree, &image, &image_size))
        error = ERROR_NOT_ENOUGH_MEMORY;
    /* Release the old file mapping before replacing its name. */
    if (transform)
        transform->Object.Destroy(&transform->Object);
    free_nodes(nodes);
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (!error)
        error = save_atomic(destination, image, image_size);
    MimeMsiFree(summary);
    MimeMsiFree(image);
    MimeMsiFree(destination);
    return error;
}
