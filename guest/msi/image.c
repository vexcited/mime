/*
 * writes a package database out as a file of its own, as an administrative installation does: the file as it is, with
 * streams left out, summary properties changed and a stream added, or the tables and strings as they are in memory
 * after a patch has changed them.
 */
#include "msi_private.h"
#include <mime/win32/winnls.h>

typedef struct IMAGE_NODE {
    MIME_CFB_NODE Node;
    BYTE *Owned;
} IMAGE_NODE;

static void free_nodes(MIME_CFB_NODE *list)
{
    while (list) {
        IMAGE_NODE *node = (IMAGE_NODE *)list;
        list = list->Next;
        free_nodes(node->Node.Children);
        MimeMsiFree(node->Owned);
        MimeMsiFree(node);
    }
}

static BOOL same_name(const uint16_t *a, const WCHAR *b)
{
    for (ULONG i = 0; i < 32; i++) {
        if (a[i] != b[i])
            return FALSE;
        if (!a[i])
            return TRUE;
    }
    return TRUE;
}

static MIME_CFB_NODE **find_node(MIME_CFB_NODE **list, const WCHAR *name)
{
    for (; *list; list = &(*list)->Next)
        if (same_name((*list)->Name, name))
            return list;
    return nullptr;
}

static void remove_node(MIME_CFB_NODE **list, const WCHAR *name)
{
    MIME_CFB_NODE **link = find_node(list, name);
    if (!link)
        return;
    MIME_CFB_NODE *node = *link;
    *link = node->Next;
    node->Next = nullptr;
    free_nodes(node);
}

/* a stream node that owns its bytes, which replaces any stream of the same name. */
static UINT put_stream(MIME_CFB_NODE **list, const WCHAR *name, BYTE *bytes, SIZE_T size)
{
    remove_node(list, name);
    IMAGE_NODE *node = MimeMsiAllocate(sizeof(*node));
    if (!node) {
        MimeMsiFree(bytes);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    for (ULONG i = 0; i < 31 && name[i]; i++)
        node->Node.Name[i] = name[i];
    node->Node.Type = 2;
    node->Owned = bytes;
    node->Node.Data = bytes;
    node->Node.Size = size;
    node->Node.Next = *list;
    *list = &node->Node;
    return ERROR_SUCCESS;
}

static UINT collect(const MIME_CFB *file, uint32_t index, MIME_CFB_NODE **list, SIZE_T *budget, ULONG depth)
{
    if (index == UINT32_MAX)
        return ERROR_SUCCESS;
    MIME_CFB_ENTRY entry;
    if (!*budget || depth > 512 || !MimeCfbEntry(file, index, &entry))
        return ERROR_INSTALL_PACKAGE_INVALID;
    --*budget;
    UINT error = collect(file, entry.Left, list, budget, depth + 1);
    if (!error)
        error = collect(file, entry.Right, list, budget, depth + 1);
    if (error)
        return error;
    if (entry.Type != 1 && entry.Type != 2)
        return ERROR_SUCCESS;
    IMAGE_NODE *node = MimeMsiAllocate(sizeof(*node));
    if (!node)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(node->Node.Name, entry.Name, sizeof(node->Node.Name));
    node->Node.Type = entry.Type;
    memcpy(node->Node.ClassId, file->Directory + (SIZE_T)index * 128 + 80, 16);
    memcpy(node->Node.Metadata, file->Directory + (SIZE_T)index * 128 + 96, 20);
    node->Node.Next = *list;
    *list = &node->Node;
    if (entry.Type == 1)
        return collect(file, entry.Child, &node->Node.Children, budget, depth + 1);
    if (entry.Size > file->Size)
        return ERROR_INSTALL_PACKAGE_INVALID;
    node->Owned = MimeMsiAllocate(entry.Size ? (SIZE_T)entry.Size : 1);
    if (!node->Owned)
        return ERROR_NOT_ENOUGH_MEMORY;
    node->Node.Data = node->Owned;
    node->Node.Size = (SIZE_T)entry.Size;
    return MimeCfbRead(file, &entry, node->Owned, (SIZE_T)entry.Size) ? ERROR_SUCCESS : ERROR_INSTALL_PACKAGE_INVALID;
}

/* ------------------------------------------------------------------ summary information */

static ULONG get32(const BYTE *p)
{
    return (ULONG)p[0] | (ULONG)p[1] << 8 | (ULONG)p[2] << 16 | (ULONG)p[3] << 24;
}

static void set32(BYTE *p, ULONG value)
{
    for (ULONG i = 0; i < 4; i++)
        p[i] = (BYTE)(value >> (8 * i));
}

typedef struct SUMMARY_VALUE {
    ULONG Id, Type, Size;
    BYTE Bytes[64];
    BYTE *Big;
} SUMMARY_VALUE;

/* the bytes of a property value including its type, padded to four. */
static UINT make_value(SUMMARY_VALUE *value, ULONG id, ULONG type, int number, const FILETIME *time, PCWSTR text, UINT codepage)
{
    memset(value, 0, sizeof(*value));
    value->Id = id;
    value->Type = type;
    set32(value->Bytes, type);
    if (type == 3) {
        set32(value->Bytes + 4, (ULONG)number);
        value->Size = 8;
    } else if (type == 64) {
        set32(value->Bytes + 4, time->dwLowDateTime);
        set32(value->Bytes + 8, time->dwHighDateTime);
        value->Size = 12;
    } else {
        SIZE_T bytes;
        if (codepage == 1200)
            bytes = (wcslen(text) + 1) * sizeof(WCHAR);
        else {
            int length = WideCharToMultiByte(codepage, 0, text, -1, nullptr, 0, nullptr, nullptr);
            if (length <= 0)
                return ERROR_INSTALL_PACKAGE_INVALID;
            bytes = (SIZE_T)length;
        }
        SIZE_T padded = (8 + bytes + 3) & ~(SIZE_T)3;
        BYTE *data = padded <= sizeof(value->Bytes) ? value->Bytes : (value->Big = MimeMsiAllocate(padded));
        if (!data)
            return ERROR_NOT_ENOUGH_MEMORY;
        memset(data, 0, padded);
        set32(data, 30);
        set32(data + 4, (ULONG)bytes);
        if (codepage == 1200)
            memcpy(data + 8, text, bytes);
        else if (!WideCharToMultiByte(codepage, 0, text, -1, (PSTR)data + 8, (int)bytes, nullptr, nullptr))
            return ERROR_INSTALL_PACKAGE_INVALID;
        value->Size = (ULONG)padded;
    }
    return ERROR_SUCCESS;
}

typedef struct SUMMARY_EDIT {
    ULONG Id, Type;
    int Number;
    FILETIME Time;
    PCWSTR Text;
} SUMMARY_EDIT;

static UINT rewrite_summary(const BYTE *old, SIZE_T length, const SUMMARY_EDIT *edits, ULONG edit_count, BYTE **out, SIZE_T *size)
{
    if (length < 48 || old[0] != 0xfe || old[1] != 0xff)
        return ERROR_INSTALL_PACKAGE_INVALID;
    ULONG offset = get32(old + 44);
    if (offset < 48 || offset > length - 8)
        return ERROR_INSTALL_PACKAGE_INVALID;
    const BYTE *section = old + offset;
    ULONG section_size = get32(section), count = get32(section + 4);
    if (section_size < 8 || section_size > length - offset || count > (section_size - 8) / 8)
        return ERROR_INSTALL_PACKAGE_INVALID;
    UINT codepage = 1252;
    for (ULONG i = 0; i < count; i++)
        if (get32(section + 8 + i * 8) == 1 && get32(section + 12 + i * 8) + 6 <= section_size)
            codepage = (UINT)section[get32(section + 12 + i * 8) + 4] | (UINT)section[get32(section + 12 + i * 8) + 5] << 8;
    if (!codepage)
        codepage = GetACP();
    SUMMARY_VALUE *added = MimeMsiAllocate(sizeof(*added) * (edit_count ? edit_count : 1));
    UINT error = added ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T capacity = section_size + 8;
    for (ULONG i = 0; !error && i < edit_count; i++) {
        error = make_value(&added[i], edits[i].Id, edits[i].Type, edits[i].Number, &edits[i].Time, edits[i].Text, codepage);
        capacity += added[i].Size + 8;
    }
    BYTE *copy = error ? nullptr : MimeMsiAllocate(capacity);
    if (!error && !copy)
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error) {
        ULONG extra = 0;
        for (ULONG e = 0; e < edit_count; e++) {
            BOOL known = FALSE;
            for (ULONG i = 0; i < count; i++)
                known |= get32(section + 8 + i * 8) == edits[e].Id;
            extra += !known;
        }
        BYTE *table = copy + 8;
        ULONG entries = 0;
        SIZE_T at = 8 + (SIZE_T)(count + extra) * 8;
        for (ULONG i = 0; !error && i < count; i++) {
            ULONG id = get32(section + 8 + i * 8), start = get32(section + 12 + i * 8), end = section_size;
            /* a value ends where the next one starts. */
            for (ULONG k = 0; k < count; k++) {
                ULONG other = get32(section + 12 + k * 8);
                if (other > start && other < end)
                    end = other;
            }
            if (start < 8 + count * 8 || start >= section_size) {
                error = ERROR_INSTALL_PACKAGE_INVALID;
                break;
            }
            const BYTE *bytes = section + start;
            SIZE_T bytes_length = end - start;
            for (ULONG e = 0; e < edit_count; e++) {
                if (edits[e].Id != id)
                    continue;
                bytes = added[e].Big ? added[e].Big : added[e].Bytes;
                bytes_length = added[e].Size;
            }
            set32(table + (SIZE_T)entries * 8, id);
            set32(table + (SIZE_T)entries * 8 + 4, (ULONG)at);
            entries++;
            memcpy(copy + at, bytes, bytes_length);
            at += bytes_length;
        }
        for (ULONG e = 0; !error && e < edit_count; e++) {
            BOOL known = FALSE;
            for (ULONG i = 0; i < count; i++)
                known |= get32(section + 8 + i * 8) == edits[e].Id;
            if (known)
                continue;
            set32(table + (SIZE_T)entries * 8, edits[e].Id);
            set32(table + (SIZE_T)entries * 8 + 4, (ULONG)at);
            entries++;
            memcpy(copy + at, added[e].Big ? added[e].Big : added[e].Bytes, added[e].Size);
            at += added[e].Size;
        }
        if (!error) {
            set32(copy, (ULONG)at);
            set32(copy + 4, entries);
            *out = MimeMsiAllocate(48 + at);
            if (!*out)
                error = ERROR_NOT_ENOUGH_MEMORY;
            else {
                memcpy(*out, old, 44);
                set32(*out + 44, 48);
                memcpy(*out + 48, copy, at);
                *size = 48 + at;
            }
        }
    }
    for (ULONG i = 0; added && i < edit_count; i++)
        MimeMsiFree(added[i].Big);
    MimeMsiFree(added);
    MimeMsiFree(copy);
    return error;
}

/* ------------------------------------------------------------------ strings and tables */

static UINT build_strings(MIME_MSI_DATABASE *database, const ULONG *references, BYTE **pool_out, SIZE_T *pool_size,
                          BYTE **data_out, SIZE_T *data_size)
{
    SIZE_T pool_bytes = 4, data_bytes = 0;
    BYTE **encoded = MimeMsiAllocate((SIZE_T)database->StringCount * sizeof(BYTE *));
    int *lengths = MimeMsiAllocate((SIZE_T)database->StringCount * sizeof(int));
    UINT error = !encoded || !lengths ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
    for (ULONG i = 1; !error && i < database->StringCount; i++) {
        PCWSTR text = database->Strings[i];
        if (!text || !*text) {
            pool_bytes += 4;
            continue;
        }
        int bytes = WideCharToMultiByte(database->CodePage, 0, text, (int)wcslen(text), nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) {
            error = ERROR_INSTALL_PACKAGE_INVALID;
            break;
        }
        encoded[i] = MimeMsiAllocate((SIZE_T)bytes);
        if (!encoded[i]) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        int converted = WideCharToMultiByte(database->CodePage, 0, text, (int)wcslen(text), (PSTR)encoded[i], bytes, nullptr, nullptr);
        int characters = converted == bytes ? MultiByteToWideChar(database->CodePage, 0, (PCSTR)encoded[i], bytes, nullptr, 0) : 0;
        PWSTR roundtrip = characters > 0 ? MimeMsiAllocate(((SIZE_T)characters + 1) * sizeof(WCHAR)) : nullptr;
        if (!roundtrip)
            error = characters > 0 ? ERROR_NOT_ENOUGH_MEMORY : ERROR_NO_UNICODE_TRANSLATION;
        else if (MultiByteToWideChar(database->CodePage, 0, (PCSTR)encoded[i], bytes, roundtrip, characters) != characters ||
                 wcscmp(text, roundtrip))
            error = ERROR_NO_UNICODE_TRANSLATION;
        MimeMsiFree(roundtrip);
        if (error)
            break;
        lengths[i] = bytes;
        pool_bytes += bytes > 0xffff ? 8 : 4;
        data_bytes += (SIZE_T)bytes;
    }
    BYTE *pool = error ? nullptr : MimeMsiAllocate(pool_bytes);
    BYTE *data = error ? nullptr : MimeMsiAllocate(data_bytes ? data_bytes : 1);
    if (!error && (!pool || !data))
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error) {
        set32(pool, database->CodePage | (database->StringWidth == 3 ? 0x80000000u : 0));
        SIZE_T at = 4, offset = 0;
        for (ULONG i = 1; i < database->StringCount; i++) {
            if (!encoded[i]) {
                set32(pool + at, 0);
                at += 4;
                continue;
            }
            ULONG count = references[i] ? references[i] : 1;
            if (count > 0xffff)
                count = 0xffff;
            if (lengths[i] > 0xffff) {
                set32(pool + at, count << 16);
                set32(pool + at + 4, (ULONG)lengths[i]);
                at += 8;
            } else {
                set32(pool + at, (ULONG)lengths[i] | count << 16);
                at += 4;
            }
            memcpy(data + offset, encoded[i], (SIZE_T)lengths[i]);
            offset += (SIZE_T)lengths[i];
        }
        *pool_out = pool;
        *pool_size = at;
        *data_out = data;
        *data_size = offset;
    } else {
        MimeMsiFree(pool);
        MimeMsiFree(data);
    }
    for (ULONG i = 1; encoded && i < database->StringCount; i++)
        MimeMsiFree(encoded[i]);
    MimeMsiFree(encoded);
    MimeMsiFree(lengths);
    return error;
}

static UINT table_bytes(const MIME_MSI_TABLE *table, BYTE **out, SIZE_T *size)
{
    SIZE_T bytes = 0;
    for (ULONG column = 0; column < table->Count; column++)
        bytes += (SIZE_T)table->Rows * table->Columns[column].Width;
    *out = MimeMsiAllocate(bytes ? bytes : 1);
    if (!*out)
        return ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T at = 0;
    for (ULONG column = 0; column < table->Count; column++) {
        SIZE_T part = (SIZE_T)table->Rows * table->Columns[column].Width;
        memcpy(*out + at, table->Data + table->Columns[column].Offset, part);
        at += part;
    }
    *size = bytes;
    return ERROR_SUCCESS;
}

static BOOL system_stream(PCWSTR name)
{
    return !wcscmp(name, L"_Tables") || !wcscmp(name, L"_Columns") || !wcscmp(name, L"_StringPool") || !wcscmp(name, L"_StringData");
}

static UINT put_table(MIME_MSI_DATABASE *database, MIME_CFB_NODE **list, PCWSTR name, ULONG *references)
{
    MIME_MSI_TABLE *table;
    UINT error = MimeMsiLoadTable(database, name, &table);
    if (error == ERROR_BAD_QUERY_SYNTAX)
        return ERROR_SUCCESS;
    if (error || !table->Rows)
        return error;
    for (ULONG column = 0; column < table->Count; column++) {
        if ((table->Columns[column].Type & 0x0c00) != 0x0c00)
            continue;
        for (ULONG row = 0; row < table->Rows; row++)
            references[MimeMsiCell(table, row, column)]++;
    }
    BYTE *bytes;
    SIZE_T size;
    WCHAR encoded[32];
    error = table_bytes(table, &bytes, &size);
    if (error)
        return error;
    if (!MimeMsiEncodeName(name, TRUE, encoded)) {
        MimeMsiFree(bytes);
        return ERROR_INSTALL_PACKAGE_INVALID;
    }
    return put_stream(list, encoded, bytes, size);
}

static UINT regenerate(MIME_MSI_DATABASE *database, MIME_CFB_NODE **list)
{
    ULONG *references = MimeMsiAllocate((SIZE_T)database->StringCount * sizeof(ULONG));
    if (!references)
        return ERROR_NOT_ENOUGH_MEMORY;
    /* every table stream goes, except the system tables nobody lists, which are put back from memory or kept. */
    MIME_CFB_NODE **link = list;
    while (*link) {
        WCHAR name[64];
        MIME_CFB_NODE *node = *link;
        if (node->Type == 2 && node->Name[0] == 0x4840 && MimeMsiDecodeTableName((const WCHAR *)node->Name, name) &&
            (system_stream(name) || name[0] != L'_')) {
            *link = node->Next;
            node->Next = nullptr;
            free_nodes(node);
        } else
            link = &node->Next;
    }
    MIME_MSI_TABLE *tables;
    UINT error = MimeMsiLoadTable(database, L"_Tables", &tables);
    for (ULONG row = 0; !error && row < tables->Rows; row++) {
        PCWSTR name = database->Strings[MimeMsiCell(tables, row, 0)];
        if (name && *name)
            error = put_table(database, list, name, references);
    }
    if (!error)
        error = put_table(database, list, L"_Tables", references);
    if (!error)
        error = put_table(database, list, L"_Columns", references);
    BYTE *pool = nullptr, *data = nullptr;
    SIZE_T pool_size = 0, data_size = 0;
    if (!error)
        error = build_strings(database, references, &pool, &pool_size, &data, &data_size);
    MimeMsiFree(references);
    WCHAR encoded[32];
    if (!error && MimeMsiEncodeName(L"_StringPool", TRUE, encoded))
        error = put_stream(list, encoded, pool, pool_size);
    else
        MimeMsiFree(pool);
    if (!error && MimeMsiEncodeName(L"_StringData", TRUE, encoded))
        error = put_stream(list, encoded, data, data_size);
    else
        MimeMsiFree(data);
    /* streams a transform added or replaced. */
    for (MIME_MSI_STREAM *stream = database->Streams; !error && stream; stream = stream->Next) {
        BYTE *copy = MimeMsiAllocate(stream->Size ? stream->Size : 1);
        if (!copy)
            return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(copy, stream->Data, stream->Size);
        error = put_stream(list, stream->Name, copy, stream->Size);
    }
    return error;
}

UINT MimeMsiBuildImage(MIME_MSI_DATABASE *database, const MIME_MSI_IMAGE_EDIT *edit, BYTE **out, SIZE_T *size)
{
    *out = nullptr;
    *size = 0;
    if (database->Root || database->Parent)
        return ERROR_INVALID_PARAMETER;
    MIME_CFB_NODE *list = nullptr;
    SIZE_T budget = database->File.DirectorySize / 128;
    MIME_CFB_ENTRY root;
    UINT error = MimeCfbEntry(&database->File, 0, &root) ? ERROR_SUCCESS : ERROR_INSTALL_PACKAGE_INVALID;
    if (!error)
        error = collect(&database->File, root.Child, &list, &budget, 0);
    for (ULONG i = 0; !error && i < edit->RemoveCount; i++)
        remove_node(&list, edit->Remove[i]);
    if (!error && edit->Regenerate)
        error = regenerate(database, &list);
    if (!error && (edit->WordCount >= 0 || edit->LastAuthor || edit->SaveTime || edit->Subject || edit->Comments || edit->Revision)) {
        MIME_CFB_NODE **link = find_node(&list, L"\x0005SummaryInformation");
        SUMMARY_EDIT edits[8];
        ULONG count = 0;
        if (!link)
            error = ERROR_INSTALL_PACKAGE_INVALID;
        if (!error && edit->LastAuthor)
            edits[count++] = (SUMMARY_EDIT){8, 30, 0, {}, edit->LastAuthor};
        if (!error && edit->SaveTime) {
            SUMMARY_EDIT time = {11, 64};
            GetSystemTimeAsFileTime(&time.Time);
            edits[count++] = time;
        }
        if (!error && edit->WordCount >= 0)
            edits[count++] = (SUMMARY_EDIT){15, 3, edit->WordCount, {}, nullptr};
        if (!error && edit->Revision)
            edits[count++] = (SUMMARY_EDIT){9, 30, 0, {}, edit->Revision};
        if (!error && edit->Subject)
            edits[count++] = (SUMMARY_EDIT){3, 30, 0, {}, edit->Subject};
        if (!error && edit->Comments)
            edits[count++] = (SUMMARY_EDIT){6, 30, 0, {}, edit->Comments};
        BYTE *summary = nullptr;
        SIZE_T summary_size = 0;
        if (!error)
            error = rewrite_summary((*link)->Data, (*link)->Size, edits, count, &summary, &summary_size);
        if (!error)
            error = put_stream(&list, L"\x0005SummaryInformation", summary, summary_size);
    }
    if (!error && edit->AdminProperties) {
        SIZE_T bytes = (wcslen(edit->AdminProperties) + 1) * sizeof(WCHAR);
        BYTE *copy = MimeMsiAllocate(bytes);
        if (!copy)
            error = ERROR_NOT_ENOUGH_MEMORY;
        else {
            memcpy(copy, edit->AdminProperties, bytes);
            error = put_stream(&list, L"AdminProperties", copy, bytes);
        }
    }
    if (!error) {
        if (!MimeCfbWrite(list, database->File.Directory + 80, MimeMsiAllocate, MimeMsiFree, out, size))
            error = ERROR_NOT_ENOUGH_MEMORY;
        else {
            /* The writer emits v3 sectors; retain the source root's storage metadata as well. */
            SIZE_T directory = ((SIZE_T)get32(*out + 48) + 1) * 512;
            memcpy(*out + directory + 96, database->File.Directory + 96, 20);
        }
    }
    free_nodes(list);
    return error;
}
