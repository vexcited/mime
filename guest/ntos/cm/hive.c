/*
 * reading regf hive files: key nodes, subkey index lists and values. cells
 * are read on demand, the file cache keeps the hot parts in memory.
 */
#include "cm.h"

#define HBIN_START 0x1000
#define BIG_DATA_SEGMENT 16344

static NTSTATUS read_bytes(CM_HIVE *hive, ULONG64 offset, PVOID buffer, ULONG length)
{
    if (offset + length > hive->Size)
        return STATUS_REGISTRY_CORRUPT;
    ULONG got = 0;
    NTSTATUS status = IoReadFileObject(hive->File, buffer, length, offset, &got);
    return NT_SUCCESS(status) && got == length ? STATUS_SUCCESS : STATUS_REGISTRY_CORRUPT;
}

/* reads length bytes at offset inside the data of a cell, checked against the cell size. */
static NTSTATUS read_cell(CM_HIVE *hive, ULONG cell, ULONG offset, PVOID buffer, ULONG length)
{
    if (cell & 7)
        return STATUS_REGISTRY_CORRUPT;
    LONG size;
    NTSTATUS status = read_bytes(hive, HBIN_START + (ULONG64)cell, &size, sizeof(size));
    if (!NT_SUCCESS(status))
        return status;
    if (size >= 0 || (ULONG64)offset + length > (ULONG64)(-(LONG64)size) - 4)
        return STATUS_REGISTRY_CORRUPT;
    return read_bytes(hive, HBIN_START + (ULONG64)cell + 4 + offset, buffer, length);
}

NTSTATUS CmpOpenHive(PCWSTR path, CM_HIVE **out)
{
    UNICODE_STRING us;
    RtlInitUnicodeString(&us, path);
    PFILE_OBJECT file;
    NTSTATUS status = IopOpenFileObject(&us, FILE_READ_DATA | SYNCHRONIZE, FILE_NON_DIRECTORY_FILE, &file);
    if (!NT_SUCCESS(status))
        return status;
    CM_HIVE *hive = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*hive), KI_TAG('C', 'm', 'H', 'v'));
    if (!hive) {
        ObDereferenceObject(file);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    hive->File = file;
    status = IoQueryFileSize(file, &hive->Size);
    UCHAR base[0x30];
    if (NT_SUCCESS(status))
        status = read_bytes(hive, 0, base, sizeof(base));
    if (NT_SUCCESS(status) && memcmp(base, "regf", 4) != 0)
        status = STATUS_REGISTRY_CORRUPT;
    if (!NT_SUCCESS(status)) {
        ObDereferenceObject(file);
        ExFreePool(hive);
        return status;
    }
    memcpy(&hive->Minor, base + 0x18, 4);
    memcpy(&hive->RootCell, base + 0x24, 4);
    *out = hive;
    return STATUS_SUCCESS;
}

static void decode_name(const UCHAR *raw, USHORT bytes, BOOLEAN ascii, PWCHAR out, PUSHORT chars)
{
    if (ascii) {
        if (bytes > 255)
            bytes = 255;
        for (USHORT i = 0; i < bytes; i++)
            out[i] = raw[i]; /* latin-1 */
        *chars = bytes;
    } else {
        if (bytes > 510)
            bytes = 510;
        memcpy(out, raw, bytes);
        *chars = bytes / sizeof(WCHAR);
    }
}

NTSTATUS CmpReadKey(CM_HIVE *hive, ULONG cell, CM_HIVE_KEY *key)
{
    UCHAR nk[0x4C + 512];
    NTSTATUS status = read_cell(hive, cell, 0, nk, 0x4C);
    if (!NT_SUCCESS(status))
        return status;
    if (nk[0] != 'n' || nk[1] != 'k')
        return STATUS_REGISTRY_CORRUPT;
    USHORT name_bytes;
    memcpy(&key->Flags, nk + 2, 2);
    memcpy(&key->LastWrite, nk + 4, 8);
    memcpy(&key->SubKeyCount, nk + 0x14, 4);
    memcpy(&key->SubKeyList, nk + 0x1C, 4);
    memcpy(&key->ValueCount, nk + 0x24, 4);
    memcpy(&key->ValueList, nk + 0x28, 4);
    memcpy(&name_bytes, nk + 0x48, 2);
    if (name_bytes > 512)
        return STATUS_REGISTRY_CORRUPT;
    status = read_cell(hive, cell, 0x4C, nk + 0x4C, name_bytes);
    if (!NT_SUCCESS(status))
        return status;
    decode_name(nk + 0x4C, name_bytes, (key->Flags & CM_HIVE_NAME_ASCII) != 0, key->Name, &key->NameLength);
    return STATUS_SUCCESS;
}

/* the security cell of a key node: a signature, two links, a count of the nodes sharing it, a size, the descriptor. */
NTSTATUS CmpReadSecurity(CM_HIVE *hive, ULONG cell, SD_BLOB **out)
{
    ULONG security;
    NTSTATUS status = read_cell(hive, cell, 0x2C, &security, 4);
    if (!NT_SUCCESS(status))
        return status;
    UCHAR head[0x14];
    if (security == 0xFFFFFFFF)
        return STATUS_NOT_FOUND;
    status = read_cell(hive, security, 0, head, sizeof(head));
    if (!NT_SUCCESS(status))
        return status;
    ULONG size;
    memcpy(&size, head + 0x10, 4);
    if (head[0] != 's' || head[1] != 'k' || size < 20 || size > SD_MAX_LENGTH)
        return STATUS_REGISTRY_CORRUPT;
    PUCHAR bytes = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KI_TAG('C', 'm', 'S', 'k'));
    if (!bytes)
        return STATUS_INSUFFICIENT_RESOURCES;
    status = read_cell(hive, security, sizeof(head), bytes, size);
    if (NT_SUCCESS(status))
        status = SeSdFromBytes(bytes, size, out) == STATUS_SUCCESS ? STATUS_SUCCESS : STATUS_REGISTRY_CORRUPT;
    ExFreePool(bytes);
    return status;
}

/* walks an lf, lh, li or ri index list. */
static NTSTATUS walk_list(CM_HIVE *hive, ULONG cell, int depth, NTSTATUS (*fn)(PVOID, ULONG), PVOID ctx)
{
    UCHAR header[4];
    NTSTATUS status = read_cell(hive, cell, 0, header, 4);
    if (!NT_SUCCESS(status))
        return status;
    USHORT count;
    memcpy(&count, header + 2, 2);
    BOOLEAN pairs = header[1] == 'f' || header[1] == 'h';
    BOOLEAN valid = header[0] == 'l' ? (pairs || header[1] == 'i') : (header[0] == 'r' && header[1] == 'i' && depth == 0);
    if (!valid)
        return STATUS_REGISTRY_CORRUPT;
    ULONG stride = pairs ? 8 : 4;
    ULONG *entries = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)count * stride + 8, KI_TAG('C', 'm', 'L', 's'));
    if (!entries)
        return STATUS_INSUFFICIENT_RESOURCES;
    status = read_cell(hive, cell, 4, entries, count * stride);
    for (ULONG i = 0; i < count && NT_SUCCESS(status); i++) {
        ULONG target = entries[i * (stride / 4)];
        status = header[0] == 'r' ? walk_list(hive, target, depth + 1, fn, ctx) : fn(ctx, target);
    }
    ExFreePool(entries);
    return status;
}

NTSTATUS CmpForEachSubKey(CM_HIVE *hive, const CM_HIVE_KEY *key, NTSTATUS (*fn)(PVOID ctx, ULONG cell), PVOID ctx)
{
    if (!key->SubKeyCount || key->SubKeyList == 0xFFFFFFFF)
        return STATUS_SUCCESS;
    return walk_list(hive, key->SubKeyList, 0, fn, ctx);
}

static NTSTATUS read_data(CM_HIVE *hive, ULONG cell, ULONG length, PUCHAR out)
{
    if (hive->Minor < 4 || length <= BIG_DATA_SEGMENT)
        return read_cell(hive, cell, 0, out, length);
    UCHAR db[8];
    NTSTATUS status = read_cell(hive, cell, 0, db, 8);
    if (!NT_SUCCESS(status))
        return status;
    if (db[0] != 'd' || db[1] != 'b')
        return read_cell(hive, cell, 0, out, length);
    USHORT segments;
    ULONG list;
    memcpy(&segments, db + 2, 2);
    memcpy(&list, db + 4, 4);
    ULONG done = 0;
    for (USHORT i = 0; i < segments && done < length && NT_SUCCESS(status); i++) {
        ULONG segment;
        status = read_cell(hive, list, i * 4, &segment, 4);
        ULONG n = length - done < BIG_DATA_SEGMENT ? length - done : BIG_DATA_SEGMENT;
        if (NT_SUCCESS(status))
            status = read_cell(hive, segment, 0, out + done, n);
        done += n;
    }
    return NT_SUCCESS(status) && done == length ? STATUS_SUCCESS : STATUS_REGISTRY_CORRUPT;
}

NTSTATUS CmpReadValue(CM_HIVE *hive, const CM_HIVE_KEY *key, ULONG index, PWCHAR name, PUSHORT name_chars,
                      PULONG type, PUCHAR *data, PULONG data_length)
{
    *data = nullptr;
    ULONG vk_cell;
    NTSTATUS status = read_cell(hive, key->ValueList, index * 4, &vk_cell, 4);
    UCHAR vk[0x14 + 512];
    if (NT_SUCCESS(status))
        status = read_cell(hive, vk_cell, 0, vk, 0x14);
    if (!NT_SUCCESS(status))
        return status;
    if (vk[0] != 'v' || vk[1] != 'k')
        return STATUS_REGISTRY_CORRUPT;
    USHORT name_bytes, flags;
    ULONG size, offset;
    memcpy(&name_bytes, vk + 2, 2);
    memcpy(&size, vk + 4, 4);
    memcpy(&offset, vk + 8, 4);
    memcpy(type, vk + 0xC, 4);
    memcpy(&flags, vk + 0x10, 2);
    if (name_bytes > 512)
        return STATUS_REGISTRY_CORRUPT;
    status = read_cell(hive, vk_cell, 0x14, vk + 0x14, name_bytes);
    if (!NT_SUCCESS(status))
        return status;
    decode_name(vk + 0x14, name_bytes, (flags & 1) != 0, name, name_chars);

    BOOLEAN inline_data = (size & 0x80000000u) != 0;
    ULONG length = size & 0x7FFFFFFFu;
    if (inline_data && length > 4)
        return STATUS_REGISTRY_CORRUPT;
    if (length > (64u << 20))
        return STATUS_REGISTRY_CORRUPT;
    PUCHAR buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, length ? length : 1, KI_TAG('C', 'm', 'V', 'd'));
    if (!buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (inline_data)
        memcpy(buffer, &offset, length);
    else if (length)
        status = read_data(hive, offset, length, buffer);
    if (!NT_SUCCESS(status)) {
        ExFreePool(buffer);
        return status;
    }
    *data = buffer;
    *data_length = length;
    return STATUS_SUCCESS;
}
