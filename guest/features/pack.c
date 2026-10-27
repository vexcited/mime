/*
 * The fonts of a feature once they are fetched: the files that were read out of a zip file or a Windows image, in a file
 * of the cache that has one entry for each. Nothing here is trusted, every entry is checked against the hash the catalog
 * names before it is installed.
 *
 *   "MFNT", version 1, count, then for each entry: the length of its name, the name, the length of the data, the data.
 *
 * The numbers are 32 bit and little endian.
 */
#include "featurep.h"

#define PACK_MAGIC 0x544E464Du /* "MFNT" */
#define PACK_VERSION 1

BOOL PackCreate(PCWSTR path, HANDLE *file)
{
    *file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (*file == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD header[3] = {PACK_MAGIC, PACK_VERSION, 0}, written;
    return WriteFile(*file, header, sizeof(header), &written, nullptr) && written == sizeof(header);
}

BOOL PackAppend(HANDLE file, const char *name, const void *data, SIZE_T size)
{
    DWORD name_length = (DWORD)strlen(name), data_length = (DWORD)size, written;
    if (size > 0x7FFFFFFF)
        return FALSE;
    return WriteFile(file, &name_length, 4, &written, nullptr) && WriteFile(file, name, name_length, &written, nullptr) &&
           WriteFile(file, &data_length, 4, &written, nullptr) && (!size || WriteFile(file, data, data_length, &written, nullptr));
}

BOOL PackFinish(HANDLE file, DWORD count)
{
    LARGE_INTEGER at = {.QuadPart = 8};
    DWORD written;
    BOOL ok = SetFilePointerEx(file, at, nullptr, FILE_BEGIN) && WriteFile(file, &count, 4, &written, nullptr);
    CloseHandle(file);
    return ok;
}

/* Walks the entries of a pack in memory. Returns FALSE when the memory is no pack, or is cut off. */
BOOL PackWalk(const BYTE *data, SIZE_T size, BOOL (*entry)(void *context, const char *name, const BYTE *bytes, SIZE_T length),
              void *context)
{
    if (size < 12)
        return FALSE;
    const DWORD *header = (const DWORD *)data;
    if (header[0] != PACK_MAGIC || header[1] != PACK_VERSION)
        return FALSE;
    SIZE_T at = 12;
    for (DWORD i = 0; i < header[2]; i++) {
        DWORD name_length, length;
        if (at + 4 > size)
            return FALSE;
        memcpy(&name_length, data + at, 4);
        at += 4;
        if (!name_length || name_length > 255 || at + name_length + 4 > size)
            return FALSE;
        char name[256];
        memcpy(name, data + at, name_length);
        name[name_length] = 0;
        at += name_length;
        memcpy(&length, data + at, 4);
        at += 4;
        if (length > size - at)
            return FALSE;
        if (!entry(context, name, data + at, length))
            return FALSE;
        at += length;
    }
    return TRUE;
}
