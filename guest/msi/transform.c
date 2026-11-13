#include "msi_private.h"

typedef struct MSI_TABLE_EDIT {
    struct MSI_TABLE_EDIT *Next;
    MIME_MSI_TABLE *Original;
    MIME_MSI_TABLE Table;
} MSI_TABLE_EDIT;

BOOL MimeMsiDecodeTableName(const WCHAR encoded[32], WCHAR name[64])
{
    static const WCHAR alphabet[] = L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz._";
    if (encoded[0] != 0x4840)
        return FALSE;
    ULONG count = 0;
    for (ULONG i = 1; i < 32 && encoded[i]; i++) {
        WCHAR value = encoded[i];
        if (value >= 0x3800 && value < 0x4800) {
            value -= 0x3800;
            name[count++] = alphabet[value & 63];
            name[count++] = alphabet[value >> 6];
        } else if (value >= 0x4800 && value < 0x4840)
            name[count++] = alphabet[value - 0x4800];
        else
            name[count++] = value;
    }
    name[count] = 0;
    return count != 0;
}

static BOOL read_cell(const BYTE *data, SIZE_T size, SIZE_T *offset, ULONG width, ULONG *value)
{
    if (*offset > size || width > size - *offset)
        return FALSE;
    *value = 0;
    for (ULONG i = 0; i < width; i++)
        *value |= (ULONG)data[(*offset)++] << (i * 8);
    return TRUE;
}

static BOOL same_key(const MIME_MSI_TABLE *table, PWSTR *strings, const ULONG *a, const ULONG *b)
{
    for (ULONG i = 0; i < table->Count; i++) {
        if (!(table->Columns[i].Type & 0x2000))
            continue;
        if ((table->Columns[i].Type & 0xc00) == 0xc00) {
            PCWSTR left = strings[a[i]], right = strings[b[i]];
            if (!left || !right || wcscmp(left, right))
                return FALSE;
        } else if (a[i] != b[i])
            return FALSE;
    }
    return TRUE;
}

static UINT transform_table(MIME_MSI_DATABASE *database, MIME_MSI_DATABASE *transform, PWSTR *strings, ULONG base,
                             MIME_MSI_TABLE *table, const BYTE *data, SIZE_T size, UINT flags, MSI_TABLE_EDIT **out)
{
    if (!table->Count || table->Count > 16)
        return ERROR_CALL_NOT_IMPLEMENTED;
    ULONG keys = 0;
    SIZE_T row_bytes = table->Count * sizeof(ULONG);
    SIZE_T capacity = (SIZE_T)table->Rows + size / 2;
    if (capacity < table->Rows || capacity > UINT32_MAX || capacity > SIZE_MAX / row_bytes)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (ULONG column = 0; column < table->Count; column++)
        if (table->Columns[column].Type & 0x2000)
            keys |= 1u << column;
    if (!keys)
        return ERROR_INSTALL_PACKAGE_INVALID;
    ULONG *rows = MimeMsiAllocate(capacity ? capacity * row_bytes : 1);
    if (!rows)
        return ERROR_NOT_ENOUGH_MEMORY;
    ULONG count = table->Rows;
    for (ULONG row = 0; row < count; row++)
        for (ULONG column = 0; column < table->Count; column++)
            rows[(SIZE_T)row * table->Count + column] = MimeMsiCell(table, row, column);
    SIZE_T offset = 0;
    UINT error = ERROR_SUCCESS;
    while (offset < size && !error) {
        ULONG mask, values[16] = {};
        if (!read_cell(data, size, &offset, 2, &mask)) {
            error = ERROR_INSTALL_PACKAGE_INVALID;
            break;
        }
        BOOL insert = mask & 1;
        if (insert && ((mask & 0xff) != 1 || (mask >> 8) != table->Count)) {
            error = ERROR_CALL_NOT_IMPLEMENTED;
            break;
        }
        ULONG changed = insert ? (1u << (mask >> 8)) - 1 : mask | keys;
        if ((changed >> table->Count) || (changed & keys) != keys) {
            error = ERROR_INSTALL_PACKAGE_INVALID;
            break;
        }
        for (ULONG column = 0; column < table->Count; column++) {
            if (!(changed & (1u << column)))
                continue;
            USHORT type = table->Columns[column].Type;
            BOOL text = (type & 0xc00) == 0xc00;
            /* a binary cell is two bytes, its data is the stream named after the table and key. */
            ULONG width = text ? transform->StringWidth : table->Columns[column].Width;
            if (!read_cell(data, size, &offset, width, &values[column]) ||
                (text && values[column] >= transform->StringCount)) {
                error = ERROR_INSTALL_PACKAGE_INVALID;
                break;
            }
            if (text && values[column])
                values[column] += base - 1;
            /* columns a transform adds to _Columns are numbered by their order, not by a value of their own. */
            BOOL numbered = insert && column == 1 && !wcscmp(table->Name, L"_Columns");
            if ((!values[column] && !(type & 0x1000) && !numbered) ||
                (text && values[column] && !strings[values[column]])) {
                error = ERROR_INSTALL_PACKAGE_INVALID;
                break;
            }
        }
        if (error)
            break;
        if (insert && !wcscmp(table->Name, L"_Columns") && !values[1]) {
            ULONG before = 0;
            for (ULONG existing = 0; existing < count; existing++) {
                PCWSTR left = strings[rows[(SIZE_T)existing * table->Count]], right = strings[values[0]];
                if (left && right && !wcscmp(left, right))
                    before++;
            }
            values[1] = 0x8000 + before + 1;
        }
        ULONG row;
        for (row = 0; row < count; row++)
            if (same_key(table, strings, rows + (SIZE_T)row * table->Count, values))
                break;
        if (!mask) {
            if (row == count) {
                if (!(flags & MSITRANSFORM_ERROR_DELMISSINGROW))
                    error = ERROR_FUNCTION_FAILED;
            } else {
                memmove(rows + (SIZE_T)row * table->Count, rows + (SIZE_T)(row + 1) * table->Count,
                        (SIZE_T)(count - row - 1) * row_bytes);
                count--;
            }
            continue;
        }
        if (insert && row != count) {
            if (!(flags & MSITRANSFORM_ERROR_ADDEXISTINGROW))
                error = ERROR_FUNCTION_FAILED;
            continue;
        }
        if (!insert && row == count) {
            if (!(flags & MSITRANSFORM_ERROR_UPDATEMISSINGROW))
                error = ERROR_FUNCTION_FAILED;
            continue;
        }
        if (insert)
            count++;
        for (ULONG column = 0; column < table->Count; column++)
            if (changed & (1u << column))
                rows[(SIZE_T)row * table->Count + column] = values[column];
    }
    MSI_TABLE_EDIT *edit = nullptr;
    if (!error) {
        edit = MimeMsiAllocate(sizeof(*edit));
        if (!edit)
            error = ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!error) {
        edit->Original = table;
        edit->Table = *table;
        edit->Table.Rows = count;
        SIZE_T bytes = 0;
        for (ULONG column = 0; column < table->Count; column++) {
            edit->Table.Columns[column].Offset = bytes;
            SIZE_T extra = (SIZE_T)count * table->Columns[column].Width;
            if (extra / table->Columns[column].Width != count || extra > SIZE_MAX - bytes) {
                error = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
            bytes += extra;
        }
        edit->Table.Data = error ? nullptr : MimeMsiAllocate(bytes ? bytes : 1);
        if (!error && !edit->Table.Data)
            error = ERROR_NOT_ENOUGH_MEMORY;
        if (!error) {
            for (ULONG column = 0; column < table->Count; column++) {
                ULONG width = table->Columns[column].Width;
                BYTE *dest = edit->Table.Data + edit->Table.Columns[column].Offset;
                for (ULONG row = 0; row < count; row++) {
                    ULONG value = rows[(SIZE_T)row * table->Count + column];
                    for (ULONG i = 0; i < width; i++)
                        *dest++ = (BYTE)(value >> (i * 8));
                }
            }
        }
    }
    MimeMsiFree(rows);
    if (error && edit) {
        MimeMsiFree(edit->Table.Data);
        MimeMsiFree(edit);
    } else if (!error)
        *out = edit;
    return error;
}

/* added _Columns rows must be for tables the database lacks: a new column in a table with rows would change how they
 * are stored. */
static UINT check_schema(MIME_MSI_DATABASE *database, MIME_MSI_TABLE *original, const MIME_MSI_TABLE *edited, PWSTR *strings)
{
    if (edited->Rows < original->Rows)
        return ERROR_CALL_NOT_IMPLEMENTED;
    for (ULONG row = 0; row < edited->Rows; row++) {
        ULONG table = MimeMsiCell(edited, row, 0);
        BOOL known = FALSE;
        for (ULONG old = 0; old < original->Rows && !known; old++)
            known = strings[MimeMsiCell(original, old, 0)] && strings[table] &&
                    !wcscmp(strings[MimeMsiCell(original, old, 0)], strings[table]);
        if (known)
            continue;
        /* a table missing from the original rows may still have its own stream, whose schema must not change. */
        BYTE *data = nullptr;
        SIZE_T size;
        UINT error = strings[table] ? MimeMsiTableStream(database, strings[table], &data, &size) : ERROR_INVALID_DATA;
        MimeMsiFree(data);
        if (error != ERROR_FILE_NOT_FOUND)
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
    return ERROR_SUCCESS;
}

static void commit_edits(MSI_TABLE_EDIT *edits, BOOL keep)
{
    while (edits) {
        MSI_TABLE_EDIT *edit = edits;
        edits = edit->Next;
        if (!keep)
            MimeMsiFree(edit->Table.Data);
        else {
            MimeMsiFree(edit->Original->Data);
            MIME_MSI_TABLE *next = edit->Original->Next;
            *edit->Original = edit->Table;
            edit->Original->Next = next;
        }
        MimeMsiFree(edit);
    }
}

/* the streams the transform adds or replaces, such as the data of binary cells of rows it inserts. */
static UINT read_streams(MIME_MSI_DATABASE *transform, const ULONG *children, ULONG count, MIME_MSI_STREAM **out)
{
    UINT error = ERROR_SUCCESS;
    *out = nullptr;
    for (ULONG at = 0; !error && at < count; at++) {
        MIME_CFB_ENTRY entry;
        if (!MimeCfbEntry(&transform->File, children[at], &entry))
            return ERROR_INSTALL_PACKAGE_INVALID;
        if (entry.Type != 2 || entry.Name[0] == 0x4840 || entry.Name[0] == 0x0005)
            continue;
        MIME_MSI_STREAM *stream = MimeMsiAllocate(sizeof(*stream));
        if (!stream) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        memcpy(stream->Name, entry.Name, sizeof(stream->Name));
        stream->Name[31] = 0;
        error = MimeMsiReadStream(transform, stream->Name, &stream->Data, &stream->Size);
        stream->Next = *out;
        *out = stream;
    }
    return error;
}

static void free_streams(MIME_MSI_STREAM *streams)
{
    while (streams) {
        MIME_MSI_STREAM *next = streams->Next;
        MimeMsiFree(streams->Data);
        MimeMsiFree(streams);
        streams = next;
    }
}

/* the added streams go before the database's own, so a later one of the same name wins. */
static void add_streams(MIME_MSI_DATABASE *database, MIME_MSI_STREAM *streams)
{
    while (streams) {
        MIME_MSI_STREAM *next = streams->Next;
        for (MIME_MSI_STREAM **link = &database->Streams; *link; link = &(*link)->Next) {
            if (wcscmp((*link)->Name, streams->Name))
                continue;
            MIME_MSI_STREAM *old = *link;
            *link = old->Next;
            MimeMsiFree(old->Data);
            MimeMsiFree(old);
            break;
        }
        streams->Next = database->Streams;
        database->Streams = streams;
        streams = next;
    }
}

/* applies the transform's tables: _Tables and _Columns in the first pass, the rest in the second. */
static UINT transform_pass(MIME_MSI_DATABASE *database, MIME_MSI_DATABASE *transform, PWSTR *strings, ULONG base, UINT flags,
                           const ULONG *children, ULONG count, BOOL schema, MSI_TABLE_EDIT **out)
{
    UINT error = ERROR_SUCCESS;
    MSI_TABLE_EDIT *edits = nullptr;
    for (ULONG at = 0; !error && at < count; at++) {
        MIME_CFB_ENTRY entry;
        if (!MimeCfbEntry(&transform->File, children[at], &entry)) {
            error = ERROR_INSTALL_PACKAGE_INVALID;
            break;
        }
        if (!entry.Type)
            continue;
        WCHAR name[64];
        if (entry.Type == 2 && !wcscmp((PCWSTR)entry.Name, L"\x0005SummaryInformation"))
            continue;
        /* a stream that is no table is one the transform adds, which read_streams takes. */
        if (entry.Type == 2 && entry.Name[0] != 0x4840)
            continue;
        if (entry.Type != 2 || !MimeMsiDecodeTableName(entry.Name, name)) {
            error = ERROR_CALL_NOT_IMPLEMENTED;
            break;
        }
        if (!wcscmp(name, L"_StringPool") || !wcscmp(name, L"_StringData"))
            continue;
        BOOL is_schema = !wcscmp(name, L"_Tables") || !wcscmp(name, L"_Columns");
        if (is_schema != schema)
            continue;
        MIME_MSI_TABLE *table;
        error = MimeMsiLoadTable(database, name, &table);
        for (MSI_TABLE_EDIT *prior = edits; !error && prior; prior = prior->Next)
            if (prior->Original == table)
                error = ERROR_INSTALL_PACKAGE_INVALID;
        BYTE *data = nullptr;
        SIZE_T size;
        if (!error)
            error = MimeMsiReadStream(transform, (PCWSTR)entry.Name, &data, &size);
        MSI_TABLE_EDIT *edit = nullptr;
        if (!error)
            error = transform_table(database, transform, strings, base, table, data, size, flags, &edit);
        MimeMsiFree(data);
        if (!error && !wcscmp(name, L"_Columns"))
            error = check_schema(database, table, &edit->Table, strings);
        if (!error) {
            edit->Next = edits;
            edits = edit;
        }
    }
    if (error)
        commit_edits(edits, FALSE);
    else
        *out = edits;
    return error;
}

UINT MimeMsiApplyTransformStorage(MIME_MSI_DATABASE *database, MIME_MSI_DATABASE *transform, UINT flags)
{
    if (database->CodePage && transform->CodePage && database->CodePage != transform->CodePage &&
        !(flags & MSITRANSFORM_ERROR_CHANGECODEPAGE))
        return ERROR_FUNCTION_FAILED;
    ULONG limit = database->StringWidth == 2 ? 0x10000 : 0x1000000;
    if (database->StringCount > limit || transform->StringCount - 1 > limit - database->StringCount)
        return ERROR_CALL_NOT_IMPLEMENTED;
    ULONG total = database->StringCount + transform->StringCount - 1;
    PWSTR *strings = MimeMsiAllocate((SIZE_T)total * sizeof(PWSTR));
    if (!strings)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(strings, database->Strings, (SIZE_T)database->StringCount * sizeof(PWSTR));
    UINT error = ERROR_SUCCESS;
    for (ULONG i = 1; i < transform->StringCount; i++) {
        if (!transform->Strings[i])
            continue;
        strings[database->StringCount + i - 1] = MimeMsiDuplicate(transform->Strings[i]);
        if (!strings[database->StringCount + i - 1]) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
    }
    ULONG *children = nullptr, count = 0;
    if (!error)
        error = MimeMsiChildren(transform, &children, &count);
    MIME_MSI_STREAM *streams = nullptr;
    if (!error)
        error = read_streams(transform, children, count, &streams);
    MSI_TABLE_EDIT *edits = nullptr;
    BOOL swapped = FALSE;
    ULONG base = database->StringCount;
    if (!error)
        error = transform_pass(database, transform, strings, base, flags, children, count, TRUE, &edits);
    if (!error && edits) {
        /* tables the transform creates are read with the schema it adds, so the schema and strings go in first. */
        commit_edits(edits, TRUE);
        edits = nullptr;
        MimeMsiFree(database->Strings);
        database->Strings = strings;
        database->StringCount = total;
        database->Revision++;
        swapped = TRUE;
    }
    if (!error)
        error = transform_pass(database, transform, strings, base, flags, children, count, FALSE, &edits);
    MimeMsiFree(children);
    commit_edits(edits, !error);
    if (error)
        free_streams(streams);
    else
        add_streams(database, streams);
    if (swapped)
        return error;
    if (error) {
        for (ULONG i = database->StringCount; i < total; i++)
            MimeMsiFree(strings[i]);
        MimeMsiFree(strings);
    } else {
        MimeMsiFree(database->Strings);
        database->Strings = strings;
        database->StringCount = total;
        database->Revision++;
    }
    return error;
}

/*
 * applies a transform database to the database of the handle, which keeps the transform if it applied. a transform
 * applied from the TRANSFORMS property suppresses the errors its own summary information names.
 */
static UINT apply_opened(MSIHANDLE handle, MIME_MSI_DATABASE *transform, int flags, BOOL named_errors)
{
    if (named_errors) {
        MIME_MSI_SUMMARY_VALUE value;
        if (!MimeMsiSummaryProperty(transform, 16, &value)) {
            flags |= value.Number & 0xFFFF;
            MimeMsiFree(value.Text);
        }
    }
    MIME_MSI_TRANSFORM *applied = MimeMsiAllocate(sizeof(*applied));
    if (!applied) {
        transform->Object.Destroy(&transform->Object);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_DATABASE *database = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
    UINT error = !database ? ERROR_INVALID_HANDLE : database->Revision >= 256 ? ERROR_CALL_NOT_IMPLEMENTED :
                 MimeMsiApplyTransformStorage(database, transform, (UINT)flags);
    if (!error) {
        database->Changes++;
        applied->Database = transform;
        applied->Flags = (UINT)flags;
        transform->Object.References++;
        MIME_MSI_TRANSFORM **tail = &database->Transforms;
        while (*tail)
            tail = &(*tail)->Next;
        *tail = applied;
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    if (error) {
        MimeMsiFree(applied);
        transform->Object.Destroy(&transform->Object);
    }
    return error;
}

UINT MimeMsiApplyTransform(MSIHANDLE handle, PCWSTR path, int flags, BOOL named_errors)
{
    if (!path || !*path || (flags & ~0x3f))
        return ERROR_INVALID_PARAMETER;
    MIME_MSI_DATABASE *transform = nullptr;
    UINT error;
    if (*path == L':') {
        /* a transform kept in the package as a storage of its own. */
        if (!path[1])
            return ERROR_INVALID_PARAMETER;
        AcquireSRWLockShared(&MimeMsiLock);
        MIME_MSI_DATABASE *database = (void *)MimeMsiGetHandle(handle, MIME_MSI_KIND_DATABASE);
        error = database ? MimeMsiOpenSubStorage(database, path + 1, &transform) : ERROR_INVALID_HANDLE;
        ReleaseSRWLockShared(&MimeMsiLock);
    } else {
        error = MimeMsiOpenDatabaseFile(path, TRUE, &transform);
    }
    if (error)
        return error;
    return apply_opened(handle, transform, flags, named_errors);
}

UINT WINAPI MsiDatabaseApplyTransformW(MSIHANDLE handle, LPCWSTR path, int flags)
{
    return MimeMsiApplyTransform(handle, path, flags, FALSE);
}
