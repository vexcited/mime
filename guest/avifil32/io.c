/* the file layer of avifil32: positional reads through a small cache and sequential writes through a buffer. */
#include "avifil32_private.h"

#define READ_CACHE 0x10000
#define WRITE_BUFFER 0x10000

struct AviIo {
    HANDLE file;
    BYTE *cache;
    UINT64 cache_offset;
    DWORD cache_size;
    BYTE *buffer;
    UINT64 buffer_offset;
    DWORD buffer_size;
};

static BOOL seek_to(AviIo *io, UINT64 offset)
{
    LARGE_INTEGER to;
    to.QuadPart = (LONGLONG)offset;
    return SetFilePointerEx(io->file, to, nullptr, FILE_BEGIN);
}

static BOOL write_out(AviIo *io, UINT64 offset, const void *data, DWORD size)
{
    DWORD written;
    if (!seek_to(io, offset))
        return FALSE;
    return WriteFile(io->file, data, size, &written, nullptr) && written == size;
}

AviIo *AviIoOpen(const WCHAR *path, UINT mode)
{
    BOOL create = (mode & OF_CREATE) != 0;
    DWORD access = GENERIC_READ;
    DWORD share;
    if (create || (mode & 3))
        access |= GENERIC_WRITE;
    switch (mode & 0x70) {
    case OF_SHARE_EXCLUSIVE:
        share = 0;
        break;
    case OF_SHARE_DENY_WRITE:
        share = FILE_SHARE_READ;
        break;
    case OF_SHARE_DENY_READ:
        share = FILE_SHARE_WRITE;
        break;
    default:
        share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        break;
    }
    HANDLE file = CreateFileW(path, access, share, nullptr, create ? CREATE_ALWAYS : OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    AviIo *io = AviAlloc(sizeof(*io));
    if (!io) {
        CloseHandle(file);
        return nullptr;
    }
    io->file = file;
    return io;
}

BOOL AviIoFlush(AviIo *io)
{
    BOOL ok = TRUE;
    if (io->buffer_size) {
        ok = write_out(io, io->buffer_offset, io->buffer, io->buffer_size);
        io->buffer_size = 0;
    }
    return ok;
}

void AviIoClose(AviIo *io)
{
    if (!io)
        return;
    AviIoFlush(io);
    CloseHandle(io->file);
    AviFree(io->cache);
    AviFree(io->buffer);
    AviFree(io);
}

BOOL AviIoRead(AviIo *io, UINT64 offset, void *buffer, DWORD size)
{
    BYTE *out = buffer;
    if (!size)
        return TRUE;
    if (!AviIoFlush(io))
        return FALSE;
    if (size >= READ_CACHE) {
        DWORD read;
        return seek_to(io, offset) && ReadFile(io->file, out, size, &read, nullptr) && read == size;
    }
    while (size) {
        if (!(io->cache_size && offset >= io->cache_offset && offset < io->cache_offset + io->cache_size)) {
            DWORD read;
            if (!io->cache) {
                io->cache = AviAlloc(READ_CACHE);
                if (!io->cache)
                    return FALSE;
            }
            io->cache_size = 0;
            if (!seek_to(io, offset) || !ReadFile(io->file, io->cache, READ_CACHE, &read, nullptr) || !read)
                return FALSE;
            io->cache_offset = offset;
            io->cache_size = read;
        }
        DWORD skip = (DWORD)(offset - io->cache_offset);
        DWORD count = io->cache_size - skip;
        if (count > size)
            count = size;
        memcpy(out, io->cache + skip, count);
        out += count;
        offset += count;
        size -= count;
    }
    return TRUE;
}

BOOL AviIoWrite(AviIo *io, UINT64 offset, const void *buffer, DWORD size)
{
    if (!size)
        return TRUE;
    io->cache_size = 0;
    if (io->buffer_size && offset == io->buffer_offset + io->buffer_size && io->buffer_size + size <= WRITE_BUFFER) {
        memcpy(io->buffer + io->buffer_size, buffer, size);
        io->buffer_size += size;
        return TRUE;
    }
    if (!AviIoFlush(io))
        return FALSE;
    if (size >= WRITE_BUFFER)
        return write_out(io, offset, buffer, size);
    if (!io->buffer) {
        io->buffer = AviAlloc(WRITE_BUFFER);
        if (!io->buffer)
            return write_out(io, offset, buffer, size);
    }
    memcpy(io->buffer, buffer, size);
    io->buffer_offset = offset;
    io->buffer_size = size;
    return TRUE;
}

BOOL AviIoZero(AviIo *io, UINT64 offset, DWORD size)
{
    BYTE zeros[1024] = {0};
    while (size) {
        DWORD count = size < sizeof(zeros) ? size : sizeof(zeros);
        if (!AviIoWrite(io, offset, zeros, count))
            return FALSE;
        offset += count;
        size -= count;
    }
    return TRUE;
}

BOOL AviIoTruncate(AviIo *io, UINT64 offset)
{
    return AviIoFlush(io) && seek_to(io, offset) && SetEndOfFile(io->file);
}

UINT64 AviIoSize(AviIo *io)
{
    LARGE_INTEGER size;
    if (!AviIoFlush(io) || !GetFileSizeEx(io->file, &size))
        return 0;
    return (UINT64)size.QuadPart;
}

/* ------------------------------------------------------------------ chunks kept as they are */

HRESULT AviExtraAdd(AviExtra *extra, DWORD ckid, const void *data, DWORD size)
{
    DWORD padded = size + (size & 1);
    if (size > 0x7FFFFFF0 || extra->size > 0x7FFFFFF0 - padded)
        return AVIERR_BADPARAM;
    BYTE *grown = AviRealloc(extra->data, (SIZE_T)extra->size + 8 + padded);
    if (!grown)
        return AVIERR_MEMORY;
    extra->data = grown;
    BYTE *at = grown + extra->size;
    memcpy(at, &ckid, 4);
    memcpy(at + 4, &size, 4);
    memcpy(at + 8, data, size);
    if (size & 1)
        at[8 + size] = 0;
    extra->size += 8 + padded;
    return S_OK;
}

HRESULT AviExtraRead(const AviExtra *extra, DWORD ckid, void *data, LONG *size)
{
    DWORD at = 0;
    while (extra->size - at >= 8) {
        DWORD id, length;
        memcpy(&id, extra->data + at, 4);
        memcpy(&length, extra->data + at + 4, 4);
        if (id == ckid) {
            if (data) {
                LONG count = (LONG)length < *size ? (LONG)length : *size;
                if (count > 0)
                    memcpy(data, extra->data + at + 8, (SIZE_T)count);
            }
            *size = (LONG)length;
            return S_OK;
        }
        at += 8 + length + (length & 1);
        if (at > extra->size)
            break;
    }
    *size = 0;
    return AVIERR_NODATA;
}

void AviExtraFree(AviExtra *extra)
{
    AviFree(extra->data);
    extra->data = nullptr;
    extra->size = 0;
}
