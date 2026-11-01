#include "msi_private.h"
#include <mime/win32/winnls.h>

static ULONG read_number(const BYTE *data, ULONG bytes)
{
    ULONG value = 0;
    for (ULONG i = 0; i < bytes; i++)
        value |= (ULONG)data[i] << (8 * i);
    return value;
}

static int name_digit(WCHAR ch)
{
    PCWSTR alphabet = L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz._";
    for (int i = 0; i < 64; i++)
        if (alphabet[i] == ch)
            return i;
    return -1;
}

BOOL MimeMsiEncodeName(PCWSTR name, BOOL table, WCHAR out[32])
{
    ULONG count = 0;
    if (table)
        out[count++] = 0x4840;
    while (*name) {
        int first = name_digit(*name++);
        int second = *name ? name_digit(*name) : -1;
        if (count >= 31)
            return FALSE;
        if (first < 0)
            out[count++] = name[-1];
        else if (second < 0)
            out[count++] = (WCHAR)(0x4800 + first);
        else {
            out[count++] = (WCHAR)(0x3800 + first + 64 * second);
            name++;
        }
    }
    out[count] = 0;
    return TRUE;
}

static BOOL table_name(PCWSTR name, WCHAR out[32])
{
    return MimeMsiEncodeName(name, TRUE, out);
}

/* binary cells live in a stream named after the table and row key, in the packed alphabet. */
UINT MimeMsiReadBinary(MIME_MSI_DATABASE *database, PCWSTR name, BYTE **out, SIZE_T *size)
{
    WCHAR encoded[32];
    if (!MimeMsiEncodeName(name, FALSE, encoded))
        return ERROR_INVALID_PARAMETER;
    return MimeMsiReadStream(database, encoded, out, size);
}

UINT MimeMsiReadStream(MIME_MSI_DATABASE *database, PCWSTR name, BYTE **out, SIZE_T *size)
{
    MIME_CFB *file = &database->File;
    MIME_CFB_ENTRY entry;
    *out = nullptr;
    *size = 0;
    for (MIME_MSI_STREAM *stream = database->Streams; stream; stream = stream->Next) {
        if (wcscmp(stream->Name, name))
            continue;
        *out = MimeMsiAllocate(stream->Size ? stream->Size : 1);
        if (!*out)
            return ERROR_NOT_ENOUGH_MEMORY;
        memcpy(*out, stream->Data, stream->Size);
        *size = stream->Size;
        return ERROR_SUCCESS;
    }
    if (!MimeCfbEntry(file, database->Root, &entry))
        return ERROR_INSTALL_PACKAGE_INVALID;
    ULONG stack[64], count = 0;
    SIZE_T remaining = file->DirectorySize / 128;
    if (entry.Child != UINT32_MAX)
        stack[count++] = entry.Child;
    while (count) {
        if (!remaining-- || !MimeCfbEntry(file, stack[--count], &entry))
            return ERROR_INSTALL_PACKAGE_INVALID;
        if (!wcscmp((PCWSTR)entry.Name, name) && entry.Type == 2) {
            if (entry.Size > file->Size)
                return ERROR_INSTALL_PACKAGE_INVALID;
            BYTE *data = MimeMsiAllocate(entry.Size ? (SIZE_T)entry.Size : 1);
            if (!data)
                return ERROR_NOT_ENOUGH_MEMORY;
            if (!MimeCfbRead(file, &entry, data, (SIZE_T)entry.Size)) {
                MimeMsiFree(data);
                return ERROR_INSTALL_PACKAGE_INVALID;
            }
            *out = data;
            *size = (SIZE_T)entry.Size;
            return ERROR_SUCCESS;
        }
        if (count + 2 > RTL_NUMBER_OF(stack))
            return ERROR_INSTALL_PACKAGE_INVALID;
        if (entry.Left != UINT32_MAX)
            stack[count++] = entry.Left;
        if (entry.Right != UINT32_MAX)
            stack[count++] = entry.Right;
    }
    return ERROR_FILE_NOT_FOUND;
}

/* the root's direct children, in tree order. */
UINT MimeMsiChildren(MIME_MSI_DATABASE *database, ULONG **list, ULONG *count)
{
    MIME_CFB *file = &database->File;
    MIME_CFB_ENTRY entry;
    *list = nullptr;
    *count = 0;
    SIZE_T entries = file->DirectorySize / 128;
    if (!MimeCfbEntry(file, database->Root, &entry))
        return ERROR_INSTALL_PACKAGE_INVALID;
    ULONG *found = MimeMsiAllocate((entries ? entries : 1) * sizeof(ULONG));
    ULONG stack[64], depth = 0;
    if (!found)
        return ERROR_NOT_ENOUGH_MEMORY;
    /* in order walk: left, entry, right. */
    ULONG at = entry.Child;
    SIZE_T steps = 0;
    while (at != UINT32_MAX || depth) {
        while (at != UINT32_MAX) {
            if (depth == RTL_NUMBER_OF(stack) || steps++ > entries || !MimeCfbEntry(file, at, &entry)) {
                MimeMsiFree(found);
                return ERROR_INSTALL_PACKAGE_INVALID;
            }
            stack[depth++] = at;
            at = entry.Left;
        }
        at = stack[--depth];
        if (*count >= entries || !MimeCfbEntry(file, at, &entry)) {
            MimeMsiFree(found);
            return ERROR_INSTALL_PACKAGE_INVALID;
        }
        found[(*count)++] = at;
        at = entry.Right;
    }
    *list = found;
    return ERROR_SUCCESS;
}

UINT MimeMsiTableStream(MIME_MSI_DATABASE *database, PCWSTR name, BYTE **out, SIZE_T *size)
{
    WCHAR encoded[32];
    if (!table_name(name, encoded))
        return ERROR_INVALID_PARAMETER;
    return MimeMsiReadStream(database, encoded, out, size);
}

UINT MimeMsiLoadStrings(MIME_MSI_DATABASE *database)
{
    BYTE *pool = nullptr, *data = nullptr;
    SIZE_T pool_size, data_size;
    UINT error = MimeMsiTableStream(database, L"_StringPool", &pool, &pool_size);
    if (!error)
        error = MimeMsiTableStream(database, L"_StringData", &data, &data_size);
    if (error)
        goto done;
    if (pool_size < 4 || pool_size % 4 || pool_size / 4 > UINT32_MAX) {
        error = ERROR_INSTALL_PACKAGE_INVALID;
        goto done;
    }
    ULONG codepage = read_number(pool, 4);
    database->StringWidth = codepage & 0x80000000u ? 3 : 2;
    codepage &= 0x7FFFFFFFu;
    database->CodePage = codepage;
    /* 0 is a neutral database, read in the system code page. */
    if (codepage && !IsValidCodePage(codepage)) {
        error = ERROR_CALL_NOT_IMPLEMENTED;
        goto done;
    }
    /* an entry is a length and a reference count. a long string has length zero and a count, then a 4-byte length. */
    ULONG count = 1;
    for (SIZE_T at = 4; at < pool_size; count++) {
        if (at + 4 > pool_size) {
            error = ERROR_INSTALL_PACKAGE_INVALID;
            goto done;
        }
        BOOL big = !read_number(pool + at, 2) && read_number(pool + at + 2, 2);
        at += big ? 8 : 4;
    }
    database->Strings = MimeMsiAllocate(count * sizeof(PWSTR));
    if (!database->Strings) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto done;
    }
    database->StringCount = count;
    SIZE_T offset = 0, at = 4;
    for (ULONG i = 1; i < count; i++) {
        ULONG length = read_number(pool + at, 2);
        ULONG references = read_number(pool + at + 2, 2);
        at += 4;
        if (!length && references) {
            if (at + 4 > pool_size) {
                error = ERROR_INSTALL_PACKAGE_INVALID;
                break;
            }
            length = read_number(pool + at, 4);
            at += 4;
        }
        if (offset > data_size || length > data_size - offset) {
            error = ERROR_INSTALL_PACKAGE_INVALID;
            break;
        }
        if (!length)
            continue;
        int characters = MultiByteToWideChar(codepage, 0, (PCSTR)data + offset, length, nullptr, 0);
        if (!characters) {
            error = ERROR_INSTALL_PACKAGE_INVALID;
            break;
        }
        PWSTR string = MimeMsiAllocate(((SIZE_T)characters + 1) * sizeof(WCHAR));
        if (!string) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        MultiByteToWideChar(codepage, 0, (PCSTR)data + offset, length, string, characters);
        database->Strings[i] = string;
        offset += length;
    }
    if (!error && offset != data_size)
        error = ERROR_INSTALL_PACKAGE_INVALID;
done:
    MimeMsiFree(pool);
    MimeMsiFree(data);
    return error;
}

ULONG MimeMsiCell(const MIME_MSI_TABLE *table, ULONG row, ULONG column)
{
    const MIME_MSI_COLUMN *field = &table->Columns[column];
    return read_number(table->Data + field->Offset + (SIZE_T)row * field->Width, field->Width);
}

static UINT load_schema(MIME_MSI_DATABASE *database, MIME_MSI_TABLE *table)
{
    if (!wcscmp(table->Name, L"_Tables")) {
        table->Count = 1;
        table->Columns[0] = (MIME_MSI_COLUMN){L"Name", 0x2d40};
        return ERROR_SUCCESS;
    }
    if (!wcscmp(table->Name, L"_Columns")) {
        table->Count = 4;
        table->Columns[0] = (MIME_MSI_COLUMN){L"Table", 0x2d40};
        table->Columns[1] = (MIME_MSI_COLUMN){L"Number", 0x2502};
        table->Columns[2] = (MIME_MSI_COLUMN){L"Name", 0x0d40};
        table->Columns[3] = (MIME_MSI_COLUMN){L"Type", 0x0502};
        return ERROR_SUCCESS;
    }
    MIME_MSI_TABLE *columns;
    UINT error = MimeMsiLoadTable(database, L"_Columns", &columns);
    if (error)
        return error;
    for (ULONG i = 0; i < columns->Rows; i++) {
        ULONG id = MimeMsiCell(columns, i, 0);
        if (!id || id >= database->StringCount || !database->Strings[id])
            return ERROR_INSTALL_PACKAGE_INVALID;
        if (wcscmp(database->Strings[id], table->Name))
            continue;
        ULONG number = MimeMsiCell(columns, i, 1) ^ 0x8000;
        id = MimeMsiCell(columns, i, 2);
        if (!number || number > RTL_NUMBER_OF(table->Columns) || !id ||
            id >= database->StringCount || !database->Strings[id] || table->Columns[number - 1].Name)
            return ERROR_INSTALL_PACKAGE_INVALID;
        table->Columns[number - 1].Name = database->Strings[id];
        table->Columns[number - 1].Type = (USHORT)(MimeMsiCell(columns, i, 3) ^ 0x8000);
        if (number > table->Count)
            table->Count = number;
    }
    if (!table->Count)
        return ERROR_BAD_QUERY_SYNTAX;
    for (ULONG i = 0; i < table->Count; i++)
        if (!table->Columns[i].Name)
            return ERROR_INSTALL_PACKAGE_INVALID;
    return ERROR_SUCCESS;
}

UINT MimeMsiLoadTable(MIME_MSI_DATABASE *database, PCWSTR name, MIME_MSI_TABLE **out)
{
    for (MIME_MSI_TABLE *table = database->Tables; table; table = table->Next) {
        if (!wcscmp(table->Name, name)) {
            *out = table;
            return ERROR_SUCCESS;
        }
    }
    if (wcslen(name) >= RTL_NUMBER_OF(((MIME_MSI_TABLE *)0)->Name))
        return ERROR_BAD_QUERY_SYNTAX;
    MIME_MSI_TABLE *table = MimeMsiAllocate(sizeof(*table));
    if (!table)
        return ERROR_NOT_ENOUGH_MEMORY;
    wcscpy(table->Name, name);
    UINT error = load_schema(database, table);
    SIZE_T size = 0;
    if (!error) {
        error = MimeMsiTableStream(database, name, &table->Data, &size);
        if (error == ERROR_FILE_NOT_FOUND)
            error = ERROR_SUCCESS;
    }
    ULONG row_size = 0;
    if (!error) {
        for (ULONG i = 0; i < table->Count; i++) {
            MIME_MSI_COLUMN *column = &table->Columns[i];
            column->Width = (column->Type & 0x0c00) == 0x0c00 ? database->StringWidth :
                            (column->Type & 0x0fff) == 0x0900 ? 2 : column->Type & 0xff;
            if (column->Width != 2 && column->Width != 3 && column->Width != 4) {
                error = ERROR_INSTALL_PACKAGE_INVALID;
                break;
            }
            row_size += column->Width;
        }
        if (!error && (!row_size || size % row_size || size / row_size > UINT32_MAX))
            error = ERROR_INSTALL_PACKAGE_INVALID;
    }
    if (!error) {
        table->Rows = (ULONG)(size / row_size);
        SIZE_T offset = 0;
        for (ULONG i = 0; i < table->Count; i++) {
            table->Columns[i].Offset = offset;
            offset += (SIZE_T)table->Rows * table->Columns[i].Width;
            if ((table->Columns[i].Type & 0x0c00) == 0x0c00) {
                for (ULONG row = 0; row < table->Rows; row++) {
                    if (MimeMsiCell(table, row, i) >= database->StringCount) {
                        error = ERROR_INSTALL_PACKAGE_INVALID;
                        break;
                    }
                }
            }
        }
    }
    if (error) {
        MimeMsiFree(table->Data);
        MimeMsiFree(table);
        return error;
    }
    table->Next = database->Tables;
    database->Tables = table;
    *out = table;
    return ERROR_SUCCESS;
}

ULONG MimeMsiColumn(const MIME_MSI_TABLE *table, PCWSTR name, BOOL text)
{
    for (ULONG i = 0; i < table->Count; i++) {
        const MIME_MSI_COLUMN *column = &table->Columns[i];
        if (wcscmp(column->Name, name))
            continue;
        BOOL string = (column->Type & 0x0c00) == 0x0c00;
        if (string != text || (!text && (column->Type & 0x0fff) == 0x0900))
            return UINT32_MAX;
        return i;
    }
    return UINT32_MAX;
}

PCWSTR MimeMsiText(const MIME_MSI_DATABASE *database, const MIME_MSI_TABLE *table, ULONG row, ULONG column)
{
    if (row >= table->Rows || column >= table->Count || (table->Columns[column].Type & 0x0c00) != 0x0c00)
        return nullptr;
    return database->Strings[MimeMsiCell(table, row, column)];
}

int MimeMsiInteger(const MIME_MSI_TABLE *table, ULONG row, ULONG column)
{
    ULONG value = MimeMsiCell(table, row, column);
    if (!value)
        return MSI_NULL_INTEGER;
    return table->Columns[column].Width == 2 ? (int)value - 0x8000 : (int)(value ^ 0x80000000u);
}
