/*
 * guest physical regions filled on first touch: the system files and the
 * host file windows. the guest sees read only memory; a region starts
 * unmapped and the stage 2 fault of a touched chunk copies the chunk into
 * anonymous memory and maps it, so only data the guest reads costs memory.
 *
 * file pages are never handed to the hypervisor: macos panics when a page
 * some process runs as code enters a vm's stage 2 tables.
 */
#include "vmm.h"

#include <Hypervisor/Hypervisor.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHUNK_BITS (sizeof(uint64_t) * 8)

bool mime_lazy_init(MimeLazyRegion *r, uint64_t ipa, uint64_t size, int fd, const void *src, uint64_t src_size)
{
    memset(r, 0, sizeof(*r));
    size = MIME_ALIGN_UP(size, MIME_LAZY_PAGE);
    void *host = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (host == MAP_FAILED)
        return false;
    uint64_t chunks = (size + MIME_LAZY_CHUNK - 1) / MIME_LAZY_CHUNK;
    r->filled = calloc((chunks + CHUNK_BITS - 1) / CHUNK_BITS, sizeof(uint64_t));
    if (!r->filled) {
        munmap(host, size);
        return false;
    }
    r->ipa = ipa;
    r->size = size;
    r->host = host;
    r->fd = fd;
    r->src = src;
    r->src_size = src_size;
    return true;
}

static uint64_t chunk_length(const MimeLazyRegion *r, uint64_t off)
{
    return MIME_MIN((uint64_t)MIME_LAZY_CHUNK, r->size - off);
}

/* unmaps every filled chunk and gives its memory back. */
void mime_lazy_drop(MimeLazyRegion *r)
{
    if (!r->filled_bytes)
        return;
    for (uint64_t off = 0; off < r->size; off += MIME_LAZY_CHUNK) {
        uint64_t c = off / MIME_LAZY_CHUNK;
        if (r->filled[c / CHUNK_BITS] & (1ull << (c % CHUNK_BITS))) {
            hv_vm_unmap(r->ipa + off, chunk_length(r, off));
            r->filled[c / CHUNK_BITS] &= ~(1ull << (c % CHUNK_BITS));
        }
    }
    mmap(r->host, r->size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
    r->filled_bytes = 0;
}

void mime_lazy_free(MimeLazyRegion *r)
{
    mime_lazy_drop(r);
    if (r->host)
        munmap(r->host, r->size);
    if (r->fd >= 0)
        close(r->fd);
    free(r->filled);
    memset(r, 0, sizeof(*r));
    r->fd = -1;
}

bool mime_lazy_contains(const MimeLazyRegion *r, uint64_t ipa)
{
    return r->host && ipa >= r->ipa && ipa < r->ipa + r->size;
}

/* fills and maps the chunk holding ipa. bytes past the end of the source read as zeros. */
bool mime_lazy_fill(MimeLazyRegion *r, uint64_t ipa)
{
    if (!mime_lazy_contains(r, ipa))
        return false;
    uint64_t off = MIME_ALIGN_DOWN(ipa - r->ipa, (uint64_t)MIME_LAZY_CHUNK);
    uint64_t c = off / MIME_LAZY_CHUNK;
    if (r->filled[c / CHUNK_BITS] & (1ull << (c % CHUNK_BITS)))
        return true; /* another vcpu filled it first */
    uint64_t len = chunk_length(r, off);
    uint64_t avail = off < r->src_size ? MIME_MIN(len, r->src_size - off) : 0;
    if (r->fd >= 0) {
        ssize_t got = avail ? pread(r->fd, r->host + off, (size_t)avail, (off_t)off) : 0;
        if (got < 0)
            got = 0;
        memset(r->host + off + got, 0, (size_t)(len - (uint64_t)got));
    } else if (avail) {
        memcpy(r->host + off, r->src + off, (size_t)avail);
    }
    if (hv_vm_map(r->host + off, r->ipa + off, len, HV_MEMORY_READ | HV_MEMORY_EXEC) != HV_SUCCESS)
        return false;
    r->filled[c / CHUNK_BITS] |= 1ull << (c % CHUNK_BITS);
    r->filled_bytes += len;
    return true;
}
