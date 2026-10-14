/*
 * memory: virtual memory, heaps, the local and global allocators and file
 * mappings.
 */
#include "kernelbase_private.h"

/* ------------------------------------------------------------------ virtual memory */

LPVOID WINAPI VirtualAllocEx(HANDLE process, LPVOID address, SIZE_T size, DWORD type, DWORD protect)
{
    PVOID base = address;
    SIZE_T length = size;
    NTSTATUS status = NtAllocateVirtualMemory(process, &base, 0, &length, type, protect);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return NULL;
    }
    return base;
}

LPVOID WINAPI VirtualAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protect)
{
    return VirtualAllocEx(NtCurrentProcess(), address, size, type, protect);
}

BOOL WINAPI VirtualFreeEx(HANDLE process, LPVOID address, SIZE_T size, DWORD type)
{
    if ((type & MEM_RELEASE) && size) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PVOID base = address;
    SIZE_T length = size;
    NTSTATUS status = NtFreeVirtualMemory(process, &base, &length, type);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI VirtualFree(LPVOID address, SIZE_T size, DWORD type)
{
    return VirtualFreeEx(NtCurrentProcess(), address, size, type);
}

BOOL WINAPI VirtualProtectEx(HANDLE process, LPVOID address, SIZE_T size, DWORD protect, PDWORD old)
{
    if (!old) {
        KbSetError(ERROR_NOACCESS);
        return FALSE;
    }
    PVOID base = address;
    SIZE_T length = size;
    ULONG previous = 0;
    NTSTATUS status = NtProtectVirtualMemory(process, &base, &length, protect, &previous);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    *old = previous;
    return TRUE;
}

BOOL WINAPI VirtualProtect(LPVOID address, SIZE_T size, DWORD protect, PDWORD old)
{
    return VirtualProtectEx(NtCurrentProcess(), address, size, protect, old);
}

SIZE_T WINAPI VirtualQueryEx(HANDLE process, LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T length)
{
    SIZE_T returned = 0;
    NTSTATUS status = NtQueryVirtualMemory(process, (PVOID)address, MemoryBasicInformation, info, length, &returned);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return 0;
    }
    return returned;
}

SIZE_T WINAPI VirtualQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T length)
{
    return VirtualQueryEx(NtCurrentProcess(), address, info, length);
}

BOOL WINAPI VirtualLock(LPVOID address, SIZE_T size)
{
    return TRUE;
}

BOOL WINAPI VirtualUnlock(LPVOID address, SIZE_T size)
{
    return TRUE;
}

BOOL WINAPI ReadProcessMemory(HANDLE process, LPCVOID base, LPVOID buffer, SIZE_T size, SIZE_T *read)
{
    SIZE_T done = 0;
    NTSTATUS status = NtReadVirtualMemory(process, (PVOID)base, buffer, size, &done);
    if (read)
        *read = done;
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI WriteProcessMemory(HANDLE process, LPVOID base, LPCVOID buffer, SIZE_T size, SIZE_T *written)
{
    SIZE_T done = 0;
    NTSTATUS status = NtWriteVirtualMemory(process, base, (PVOID)buffer, size, &done);
    if (written)
        *written = done;
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI FlushInstructionCache(HANDLE process, LPCVOID base, SIZE_T size)
{
    NTSTATUS status = NtFlushInstructionCache(process, (PVOID)base, size);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

void WINAPI FlushProcessWriteBuffers(void)
{
    NtFlushProcessWriteBuffers();
}

BOOL WINAPI PrefetchVirtualMemory(HANDLE process, ULONG_PTR count, PWIN32_MEMORY_RANGE_ENTRY entries, ULONG flags)
{
    return TRUE;
}

UINT WINAPI GetWriteWatch(DWORD flags, PVOID base, SIZE_T size, PVOID *addresses, ULONG_PTR *count,
                          LPDWORD granularity)
{
    KbSetError(ERROR_INVALID_PARAMETER);
    return (UINT)-1;
}

UINT WINAPI ResetWriteWatch(LPVOID base, SIZE_T size)
{
    KbSetError(ERROR_INVALID_PARAMETER);
    return (UINT)-1;
}

SIZE_T WINAPI GetLargePageMinimum(void)
{
    return USER_SHARED_DATA->LargePageMinimum;
}

/* ------------------------------------------------------------------ pointer validation */

static BOOL readable_protection(ULONG protect)
{
    if (protect & (PAGE_GUARD | PAGE_NOACCESS))
        return FALSE;
    return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                       PAGE_EXECUTE_WRITECOPY)) != 0;
}

static BOOL writable_protection(ULONG protect)
{
    if (protect & (PAGE_GUARD | PAGE_NOACCESS))
        return FALSE;
    return (protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

/* checks that every byte of the range is committed with the requested access. */
static BOOL range_accessible(const void *p, UINT_PTR size, BOOL write)
{
    if (!size)
        return TRUE;
    if (!p)
        return FALSE;
    ULONG_PTR start = (ULONG_PTR)p, end = start + size;
    if (end < start)
        return FALSE;
    while (start < end) {
        MEMORY_BASIC_INFORMATION mbi;
        SIZE_T returned = 0;
        if (!NT_SUCCESS(NtQueryVirtualMemory(NtCurrentProcess(), (PVOID)start, MemoryBasicInformation, &mbi,
                                             sizeof(mbi), &returned)))
            return FALSE;
        if (mbi.State != MEM_COMMIT)
            return FALSE;
        if (write ? !writable_protection(mbi.Protect) : !readable_protection(mbi.Protect))
            return FALSE;
        ULONG_PTR next = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;
        if (next <= start)
            return FALSE;
        start = next;
    }
    return TRUE;
}

BOOL WINAPI IsBadReadPtr(LPCVOID p, UINT_PTR size)
{
    return !range_accessible(p, size, FALSE);
}

BOOL WINAPI IsBadWritePtr(LPVOID p, UINT_PTR size)
{
    return !range_accessible(p, size, TRUE);
}

BOOL WINAPI IsBadHugeReadPtr(LPCVOID p, UINT_PTR size)
{
    return IsBadReadPtr(p, size);
}

BOOL WINAPI IsBadHugeWritePtr(LPVOID p, UINT_PTR size)
{
    return IsBadWritePtr(p, size);
}

BOOL WINAPI IsBadCodePtr(FARPROC p)
{
    return IsBadReadPtr((LPCVOID)p, 1);
}

BOOL WINAPI IsBadStringPtrA(LPCSTR p, UINT_PTR max)
{
    if (!max)
        return FALSE;
    for (UINT_PTR i = 0; i < max; i++) {
        if (((ULONG_PTR)(p + i) & 0xFFF) == 0 || i == 0)
            if (IsBadReadPtr(p + i, 1))
                return TRUE;
        if (!p[i])
            break;
    }
    return FALSE;
}

BOOL WINAPI IsBadStringPtrW(LPCWSTR p, UINT_PTR max)
{
    if (!max)
        return FALSE;
    for (UINT_PTR i = 0; i < max; i++) {
        if (((ULONG_PTR)(p + i) & 0xFFF) < sizeof(WCHAR) || i == 0)
            if (IsBadReadPtr(p + i, sizeof(WCHAR)))
                return TRUE;
        if (!p[i])
            break;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ heaps */

HANDLE WINAPI HeapCreate(DWORD options, SIZE_T initial, SIZE_T maximum)
{
    ULONG flags = options & (HEAP_GENERATE_EXCEPTIONS | HEAP_NO_SERIALIZE | HEAP_CREATE_ENABLE_EXECUTE);
    if (!maximum)
        flags |= HEAP_GROWABLE;
    HANDLE heap = RtlCreateHeap(flags, NULL, maximum, initial, NULL, NULL);
    if (!heap)
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
    return heap;
}

BOOL WINAPI HeapDestroy(HANDLE heap)
{
    if (RtlDestroyHeap(heap)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

LPVOID WINAPI HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size)
{
    return RtlAllocateHeap(heap, flags, size);
}

LPVOID WINAPI HeapReAlloc(HANDLE heap, DWORD flags, LPVOID p, SIZE_T size)
{
    return RtlReAllocateHeap(heap, flags, p, size);
}

BOOL WINAPI HeapFree(HANDLE heap, DWORD flags, LPVOID p)
{
    if (!RtlFreeHeap(heap, flags, p)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return TRUE;
}

SIZE_T WINAPI HeapSize(HANDLE heap, DWORD flags, LPCVOID p)
{
    return RtlSizeHeap(heap, flags, p);
}

BOOL WINAPI HeapValidate(HANDLE heap, DWORD flags, LPCVOID p)
{
    return RtlValidateHeap(heap, flags, (PVOID)p);
}

SIZE_T WINAPI HeapCompact(HANDLE heap, DWORD flags)
{
    return RtlCompactHeap(heap, flags);
}

BOOL WINAPI HeapLock(HANDLE heap)
{
    return RtlLockHeap(heap);
}

BOOL WINAPI HeapUnlock(HANDLE heap)
{
    return RtlUnlockHeap(heap);
}

BOOL WINAPI HeapWalk(HANDLE heap, LPPROCESS_HEAP_ENTRY entry)
{
    KbSetError(ERROR_NO_MORE_ITEMS);
    return FALSE;
}

BOOL WINAPI HeapSummary(HANDLE heap, DWORD flags, LPHEAP_SUMMARY summary)
{
    if (!summary || summary->cb < sizeof(*summary)) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    summary->cbAllocated = summary->cbCommitted = summary->cbReserved = summary->cbMaxReserve = 0;
    return TRUE;
}

BOOL WINAPI HeapSetInformation(HANDLE heap, HEAP_INFORMATION_CLASS klass, PVOID info, SIZE_T length)
{
    NTSTATUS status = RtlSetHeapInformation(heap, klass, info, length);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI HeapQueryInformation(HANDLE heap, HEAP_INFORMATION_CLASS klass, PVOID info, SIZE_T length,
                                 PSIZE_T returned)
{
    NTSTATUS status = RtlQueryHeapInformation(heap, klass, info, length, returned);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

HANDLE WINAPI GetProcessHeap(void)
{
    return KbPeb()->ProcessHeap;
}

DWORD WINAPI GetProcessHeaps(DWORD count, PHANDLE heaps)
{
    return RtlGetProcessHeaps(count, heaps);
}

/* ------------------------------------------------------------------ local and global memory */

/*
 * moveable blocks: the handle points at the Pointer field of a 16 byte
 * aligned descriptor, so handles end in 8 while heap pointers never do.
 * the data block starts with a header pointing back to its descriptor.
 */
#define MOVEABLE_MAGIC 0x564F4D4Bu /* "KMOV". */
#define DATA_MAGIC 0x5441444Bu     /* "KDAT". */
#define FLAG_DISCARDABLE 0x0001

typedef struct DECLSPEC_ALIGN(16) MOVEABLE {
    ULONG Magic;
    USHORT Flags;
    UCHAR LockCount;
    UCHAR Pad;
    PVOID Pointer;
} MOVEABLE;
MIME_STATIC_ASSERT(sizeof(MOVEABLE) == 16, "MOVEABLE");

typedef struct DECLSPEC_ALIGN(16) DATA_HEADER {
    ULONG Magic;
    ULONG Pad;
    MOVEABLE *Owner;
} DATA_HEADER;
MIME_STATIC_ASSERT(sizeof(DATA_HEADER) == 16, "DATA_HEADER");

static RTL_SRWLOCK g_global_lock;

static MOVEABLE *moveable_of(HGLOBAL h)
{
    if (((ULONG_PTR)h & 0xF) != 8)
        return NULL;
    MOVEABLE *m = (MOVEABLE *)((PUCHAR)h - 8);
    return m->Magic == MOVEABLE_MAGIC ? m : NULL;
}

static PVOID data_block(MOVEABLE *m)
{
    return m->Pointer ? (PUCHAR)m->Pointer - sizeof(DATA_HEADER) : NULL;
}

static PVOID allocate_data(MOVEABLE *m, SIZE_T size, BOOL zero)
{
    DATA_HEADER *h = RtlAllocateHeap(KbPeb()->ProcessHeap, zero ? HEAP_ZERO_MEMORY : 0, size + sizeof(DATA_HEADER));
    if (!h)
        return NULL;
    h->Magic = DATA_MAGIC;
    h->Pad = 0;
    h->Owner = m;
    return h + 1;
}

static HGLOBAL global_alloc(UINT flags, SIZE_T size, BOOL discardable)
{
    PVOID heap = KbPeb()->ProcessHeap;
    if (!(flags & GMEM_MOVEABLE)) {
        PVOID p = RtlAllocateHeap(heap, (flags & GMEM_ZEROINIT) ? HEAP_ZERO_MEMORY : 0, size);
        if (!p)
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return p;
    }
    MOVEABLE *m = RtlAllocateHeap(heap, HEAP_ZERO_MEMORY, sizeof(MOVEABLE));
    if (!m) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    m->Magic = MOVEABLE_MAGIC;
    m->Flags = discardable ? FLAG_DISCARDABLE : 0;
    if (size) {
        m->Pointer = allocate_data(m, size, (flags & GMEM_ZEROINIT) != 0);
        if (!m->Pointer) {
            m->Magic = 0;
            RtlFreeHeap(heap, 0, m);
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
    }
    return &m->Pointer;
}

static HGLOBAL global_free(HGLOBAL h)
{
    if (!h)
        return NULL;
    PVOID heap = KbPeb()->ProcessHeap;
    MOVEABLE *m = moveable_of(h);
    if (m) {
        RtlAcquireSRWLockExclusive(&g_global_lock);
        PVOID block = data_block(m);
        if (block)
            ((DATA_HEADER *)block)->Magic = 0;
        m->Magic = 0;
        RtlReleaseSRWLockExclusive(&g_global_lock);
        RtlFreeHeap(heap, 0, block);
        RtlFreeHeap(heap, 0, m);
        return NULL;
    }
    if (!RtlFreeHeap(heap, 0, h)) {
        KbSetError(ERROR_INVALID_HANDLE);
        return h;
    }
    return NULL;
}

static LPVOID global_lock(HGLOBAL h)
{
    MOVEABLE *m = moveable_of(h);
    if (!m) {
        if (!h)
            KbSetError(ERROR_INVALID_HANDLE);
        return h;
    }
    RtlAcquireSRWLockExclusive(&g_global_lock);
    PVOID p = m->Pointer;
    if (p && m->LockCount < 0xFF)
        m->LockCount++;
    RtlReleaseSRWLockExclusive(&g_global_lock);
    if (!p)
        KbSetError(ERROR_DISCARDED);
    return p;
}

static BOOL global_unlock(HGLOBAL h, BOOL fixed_result)
{
    MOVEABLE *m = moveable_of(h);
    if (!m) {
        if (!fixed_result)
            KbSetError(ERROR_NOT_LOCKED);
        return fixed_result;
    }
    RtlAcquireSRWLockExclusive(&g_global_lock);
    BOOL locked;
    if (!m->LockCount) {
        KbSetError(ERROR_NOT_LOCKED);
        locked = FALSE;
    } else {
        if (m->LockCount < 0xFF)
            m->LockCount--;
        locked = m->LockCount != 0;
        if (!locked)
            KbSetError(NO_ERROR);
    }
    RtlReleaseSRWLockExclusive(&g_global_lock);
    return locked;
}

static SIZE_T global_size(HGLOBAL h)
{
    MOVEABLE *m = moveable_of(h);
    PVOID heap = KbPeb()->ProcessHeap;
    if (m) {
        PVOID block = data_block(m);
        return block ? RtlSizeHeap(heap, 0, block) - sizeof(DATA_HEADER) : 0;
    }
    SIZE_T size = RtlSizeHeap(heap, 0, h);
    if (size == (SIZE_T)-1) {
        KbSetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    return size;
}

static HGLOBAL global_handle(LPCVOID p)
{
    if (!p) {
        KbSetError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    if (moveable_of((HGLOBAL)p))
        return (HGLOBAL)p;
    if (!((ULONG_PTR)p & 0xF)) {
        const DATA_HEADER *h = (const DATA_HEADER *)p - 1;
        if (h->Magic == DATA_MAGIC && h->Owner && h->Owner->Magic == MOVEABLE_MAGIC && h->Owner->Pointer == p)
            return &h->Owner->Pointer;
    }
    if (RtlSizeHeap(KbPeb()->ProcessHeap, 0, p) == (SIZE_T)-1) {
        KbSetError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    return (HGLOBAL)p;
}

static HGLOBAL global_realloc(HGLOBAL h, SIZE_T size, UINT flags, BOOL discardable_flag)
{
    PVOID heap = KbPeb()->ProcessHeap;
    MOVEABLE *m = moveable_of(h);
    if (flags & GMEM_MODIFY) {
        if (m) {
            if (discardable_flag)
                m->Flags |= FLAG_DISCARDABLE;
            else
                m->Flags &= (USHORT)~FLAG_DISCARDABLE;
            return h;
        }
        if (flags & GMEM_MOVEABLE) {
            /* converts fixed memory into a moveable block holding a copy of it. */
            SIZE_T old = RtlSizeHeap(heap, 0, h);
            if (old == (SIZE_T)-1) {
                KbSetError(ERROR_INVALID_HANDLE);
                return NULL;
            }
            HGLOBAL nh = global_alloc(GMEM_MOVEABLE, old, discardable_flag);
            if (!nh)
                return NULL;
            MOVEABLE *nm = moveable_of(nh);
            if (old)
                memcpy(nm->Pointer, h, old);
            RtlFreeHeap(heap, 0, h);
            return nh;
        }
        return h;
    }
    if (!m) {
        ULONG hf = (flags & GMEM_ZEROINIT) ? HEAP_ZERO_MEMORY : 0;
        if (!(flags & GMEM_MOVEABLE))
            hf |= HEAP_REALLOC_IN_PLACE_ONLY;
        PVOID p = RtlReAllocateHeap(heap, hf, h, size);
        if (!p)
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return p;
    }
    RtlAcquireSRWLockExclusive(&g_global_lock);
    HGLOBAL result = h;
    if (!size) {
        if (!m->LockCount) {
            PVOID block = data_block(m);
            if (block) {
                ((DATA_HEADER *)block)->Magic = 0;
                RtlFreeHeap(heap, 0, block);
            }
            m->Pointer = NULL;
        } else {
            KbSetError(ERROR_INVALID_HANDLE);
            result = NULL;
        }
    } else if (!m->Pointer) {
        m->Pointer = allocate_data(m, size, (flags & GMEM_ZEROINIT) != 0);
        if (!m->Pointer) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            result = NULL;
        }
    } else {
        ULONG hf = (flags & GMEM_ZEROINIT) ? HEAP_ZERO_MEMORY : 0;
        if (m->LockCount && !(flags & GMEM_MOVEABLE))
            hf |= HEAP_REALLOC_IN_PLACE_ONLY;
        DATA_HEADER *block = RtlReAllocateHeap(heap, hf, data_block(m), size + sizeof(DATA_HEADER));
        if (block) {
            m->Pointer = block + 1;
        } else {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            result = NULL;
        }
    }
    RtlReleaseSRWLockExclusive(&g_global_lock);
    return result;
}

static UINT global_flags(HGLOBAL h, UINT discardable_value, UINT invalid_value)
{
    MOVEABLE *m = moveable_of(h);
    if (!m) {
        if (!h || RtlSizeHeap(KbPeb()->ProcessHeap, 0, h) == (SIZE_T)-1) {
            KbSetError(ERROR_INVALID_HANDLE);
            return invalid_value;
        }
        return 0;
    }
    UINT flags = m->LockCount;
    if (!m->Pointer)
        flags |= GMEM_DISCARDED;
    if (m->Flags & FLAG_DISCARDABLE)
        flags |= discardable_value;
    return flags;
}

HGLOBAL WINAPI GlobalAlloc(UINT flags, SIZE_T size)
{
    return global_alloc(flags, size, (flags & GMEM_DISCARDABLE) != 0);
}

HGLOBAL WINAPI GlobalFree(HGLOBAL mem)
{
    return global_free(mem);
}

HGLOBAL WINAPI GlobalReAlloc(HGLOBAL mem, SIZE_T size, UINT flags)
{
    return global_realloc(mem, size, flags, (flags & GMEM_DISCARDABLE) != 0);
}

SIZE_T WINAPI GlobalSize(HGLOBAL mem)
{
    return global_size(mem);
}

LPVOID WINAPI GlobalLock(HGLOBAL mem)
{
    return global_lock(mem);
}

BOOL WINAPI GlobalUnlock(HGLOBAL mem)
{
    return global_unlock(mem, TRUE);
}

HGLOBAL WINAPI GlobalHandle(LPCVOID p)
{
    return global_handle(p);
}

UINT WINAPI GlobalFlags(HGLOBAL mem)
{
    return global_flags(mem, GMEM_DISCARDABLE, GMEM_INVALID_HANDLE);
}

SIZE_T WINAPI GlobalCompact(DWORD min_free)
{
    return 0x100000;
}

void WINAPI GlobalFix(HGLOBAL mem)
{
    GlobalLock(mem);
}

void WINAPI GlobalUnfix(HGLOBAL mem)
{
    GlobalUnlock(mem);
}

LPVOID WINAPI GlobalWire(HGLOBAL mem)
{
    return GlobalLock(mem);
}

BOOL WINAPI GlobalUnWire(HGLOBAL mem)
{
    return GlobalUnlock(mem);
}

HLOCAL WINAPI LocalAlloc(UINT flags, SIZE_T size)
{
    return global_alloc(flags & (LMEM_MOVEABLE | LMEM_ZEROINIT), size, (flags & LMEM_DISCARDABLE) != 0);
}

HLOCAL WINAPI LocalFree(HLOCAL mem)
{
    return global_free(mem);
}

HLOCAL WINAPI LocalReAlloc(HLOCAL mem, SIZE_T size, UINT flags)
{
    return global_realloc(mem, size, flags & (LMEM_MOVEABLE | LMEM_ZEROINIT | LMEM_MODIFY),
                          (flags & LMEM_DISCARDABLE) != 0);
}

SIZE_T WINAPI LocalSize(HLOCAL mem)
{
    return global_size(mem);
}

LPVOID WINAPI LocalLock(HLOCAL mem)
{
    return global_lock(mem);
}

BOOL WINAPI LocalUnlock(HLOCAL mem)
{
    return global_unlock(mem, FALSE);
}

HLOCAL WINAPI LocalHandle(LPCVOID p)
{
    return global_handle(p);
}

UINT WINAPI LocalFlags(HLOCAL mem)
{
    return global_flags(mem, LMEM_DISCARDABLE, LMEM_INVALID_HANDLE);
}

SIZE_T WINAPI LocalCompact(UINT min_free)
{
    return 0;
}

SIZE_T WINAPI LocalShrink(HLOCAL mem, UINT size)
{
    return 0;
}

/* ------------------------------------------------------------------ file mappings */

static ULONG map_access_to_protect(DWORD access)
{
    BOOL exec = (access & FILE_MAP_EXECUTE) != 0;
    if (access & FILE_MAP_COPY && !(access & FILE_MAP_WRITE))
        return exec ? PAGE_EXECUTE_WRITECOPY : PAGE_WRITECOPY;
    if (access & FILE_MAP_WRITE)
        return exec ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    if (access & (FILE_MAP_READ | FILE_MAP_COPY))
        return exec ? PAGE_EXECUTE_READ : PAGE_READONLY;
    return exec ? PAGE_EXECUTE : PAGE_NOACCESS;
}

HANDLE WINAPI CreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD protect, DWORD size_high,
                                 DWORD size_low, LPCWSTR name)
{
    ULONG page = protect & 0xFF;
    ULONG sec = protect & 0xFFFFFF00;
    if (!sec)
        sec = SEC_COMMIT;
    ACCESS_MASK access = STANDARD_RIGHTS_REQUIRED | SECTION_QUERY | SECTION_MAP_READ;
    switch (page) {
    case PAGE_READONLY:
    case PAGE_WRITECOPY:
        break;
    case PAGE_READWRITE:
        access |= SECTION_MAP_WRITE;
        break;
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_WRITECOPY:
        access |= SECTION_MAP_EXECUTE;
        break;
    case PAGE_EXECUTE_READWRITE:
        access |= SECTION_MAP_WRITE | SECTION_MAP_EXECUTE;
        break;
    default:
        KbSetError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    LARGE_INTEGER size;
    size.LowPart = size_low;
    size.HighPart = (LONG)size_high;
    if (file == INVALID_HANDLE_VALUE) {
        file = NULL;
        if (!size.QuadPart) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return NULL;
        }
    } else if (!size.QuadPart) {
        LARGE_INTEGER current;
        if (GetFileSizeEx(file, &current) && !current.QuadPart) {
            KbSetError(ERROR_FILE_INVALID);
            return NULL;
        }
    }
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    if (!KbNamedObjectAttributes(&oa, &us, name, FALSE, sa))
        return NULL;
    if (name)
        oa.Attributes |= OBJ_OPENIF;
    HANDLE handle;
    NTSTATUS status = NtCreateSection(&handle, access, &oa, size.QuadPart ? &size : NULL, page, sec, file);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return NULL;
    }
    KbSetError(status == STATUS_OBJECT_NAME_EXISTS ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return handle;
}

HANDLE WINAPI CreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD protect, DWORD size_high,
                                 DWORD size_low, LPCSTR name)
{
    PWSTR wname = KbAnsiToWideCp(CP_ACP, name);
    if (name && !wname)
        return NULL;
    HANDLE h = CreateFileMappingW(file, sa, protect, size_high, size_low, wname);
    KbFree(wname);
    return h;
}

HANDLE WINAPI OpenFileMappingW(DWORD access, BOOL inherit, LPCWSTR name)
{
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING us;
    if (!KbNamedObjectAttributes(&oa, &us, name, inherit, NULL))
        return NULL;
    if (access == FILE_MAP_COPY)
        access = SECTION_MAP_READ;
    HANDLE handle;
    NTSTATUS status = NtOpenSection(&handle, access, &oa);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return NULL;
    }
    return handle;
}

HANDLE WINAPI OpenFileMappingA(DWORD access, BOOL inherit, LPCSTR name)
{
    PWSTR wname = KbAnsiToWideCp(CP_ACP, name);
    if (name && !wname)
        return NULL;
    HANDLE h = OpenFileMappingW(access, inherit, wname);
    KbFree(wname);
    return h;
}

LPVOID WINAPI MapViewOfFileEx(HANDLE mapping, DWORD access, DWORD offset_high, DWORD offset_low, SIZE_T size,
                              LPVOID base)
{
    LARGE_INTEGER offset;
    offset.LowPart = offset_low;
    offset.HighPart = (LONG)offset_high;
    PVOID address = base;
    SIZE_T view = size;
    NTSTATUS status = NtMapViewOfSection(mapping, NtCurrentProcess(), &address, 0, 0, &offset, &view, ViewShare, 0,
                                         map_access_to_protect(access));
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return NULL;
    }
    return address;
}

LPVOID WINAPI MapViewOfFile(HANDLE mapping, DWORD access, DWORD offset_high, DWORD offset_low, SIZE_T size)
{
    return MapViewOfFileEx(mapping, access, offset_high, offset_low, size, NULL);
}

BOOL WINAPI UnmapViewOfFile(LPCVOID base)
{
    NTSTATUS status = NtUnmapViewOfSection(NtCurrentProcess(), (PVOID)base);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI UnmapViewOfFileEx(PVOID base, ULONG flags)
{
    NTSTATUS status = NtUnmapViewOfSectionEx(NtCurrentProcess(), base, flags);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

BOOL WINAPI FlushViewOfFile(LPCVOID base, SIZE_T size)
{
    return TRUE;
}
