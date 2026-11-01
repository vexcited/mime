/*
 * the verifying pool, switched on by the kernel option verify-pool. every allocation gets pages of its own between two
 * unmapped guard pages, as the special pool of Windows has them, and the pages go away when it is freed. the address
 * range of a freed allocation stays unmapped for a long while, so a late access faults on the spot instead of
 * corrupting whatever got the memory next. what the allocator can only check when it is called, such as a free with
 * the wrong tag, a free twice or bytes written past the end but inside the last page, is checked then, and each
 * report names the tag, the size and who allocated and freed the block.
 *
 * a block of up to 2 KiB ends at the guard page behind it, a larger one starts at the page after the guard in front,
 * as the ordinary pool places them. the bytes in between are canaries.
 */
#include "../ntos.h"

#define SP_WINDOW_PAGES (1ull << 22)
#define SP_MAX_SLOTS (1u << 18)
#define SP_QUARANTINE 16384u
#define SP_HASH_SIZE (1u << 19)
#define SP_CALLERS 6
#define SP_FREE_CLASSES 256
#define SP_SWEEP_EVERY 20000u
#define SP_LARGE_BYTES 2048

#define SP_PRE_BYTE 0xBD
#define SP_TAIL_BYTE 0xA5
#define SP_FILL_BYTE 0xCD

#define SP_UNUSED 0u
#define SP_LIVE 1u
#define SP_FREED 2u

typedef struct SP_SLOT {
    ULONG_PTR Va; /* the first guard page, then Pages data pages, then the last guard page. */
    ULONG_PTR User;
    SIZE_T Size;
    ULONG Tag;
    ULONG Pages;
    ULONG64 Pa;
    ULONG64 AllocSeq;
    ULONG64 FreeSeq;
    ULONG State;
    ULONG Next; /* hash chain, or the list of address ranges ready for reuse. */
    PVOID AllocCallers[SP_CALLERS];
    PVOID FreeCallers[SP_CALLERS];
} SP_SLOT;

BOOLEAN MmVerifyPool;
BOOLEAN MmVerifyPoolFill;

static BOOLEAN g_ready;
static BOOLEAN g_failed;
static ULONG_PTR g_window;
static ULONG64 g_bump; /* pages of the window used. */
static SP_SLOT *g_slots;
static ULONG g_slot_count;
static ULONG g_slot_mapped; /* pages of the slot array that have memory. */
static ULONG *g_hash;
static ULONG g_free_list[SP_FREE_CLASSES + 1]; /* by pages in the range, slot index plus one. */
static ULONG g_free_big;
static ULONG g_quarantine[SP_QUARANTINE];
static ULONG g_quarantine_head, g_quarantine_count;
static ULONG64 g_seq;
static ULONG64 g_live_pages, g_budget_pages;
static ULONG64 g_sweep_due;
static ULONG64 g_fallbacks, g_live_blocks;
static BOOLEAN g_reporting;

static BOOLEAN map_array(ULONG_PTR base, SIZE_T bytes)
{
    for (SIZE_T off = 0; off < bytes; off += PAGE_SIZE) {
        ULONG64 pa = MmAllocatePage();
        if (!pa || !MiMapPage(MiKernelRoot(), base + off, pa, MM_KERNEL_RW))
            return FALSE;
    }
    return TRUE;
}

static BOOLEAN initialize(void)
{
    if (g_ready)
        return TRUE;
    if (g_failed)
        return FALSE;
    g_failed = TRUE;
    PVOID window = MmAllocateSystemVa(SP_WINDOW_PAGES);
    PVOID slots = MmAllocateSystemVa(BYTES_TO_PAGES((SIZE_T)SP_MAX_SLOTS * sizeof(SP_SLOT)));
    PVOID hash = MmAllocateSystemVa(BYTES_TO_PAGES((SIZE_T)SP_HASH_SIZE * sizeof(ULONG)));
    if (!window || !slots || !hash || !map_array((ULONG_PTR)hash, (SIZE_T)SP_HASH_SIZE * sizeof(ULONG)))
        return FALSE;
    g_window = (ULONG_PTR)window;
    g_slots = slots;
    g_hash = hash;
    g_budget_pages = MmGetTotalPages() / 3;
    g_sweep_due = SP_SWEEP_EVERY;
    g_failed = FALSE;
    g_ready = TRUE;
    KiLogPrintf("verifying pool on, %llu MiB of memory for blocks, every block has its own guard pages",
                (ULONG64)(g_budget_pages >> 8));
    return TRUE;
}

static BOOLEAN in_window(ULONG_PTR p)
{
    return g_ready && p >= g_window && p < g_window + (SP_WINDOW_PAGES << PAGE_SHIFT);
}

BOOLEAN MiSpecialPoolContains(PVOID p)
{
    return in_window((ULONG_PTR)p);
}

static ULONG hash_of(ULONG_PTR user)
{
    return (ULONG)(((user >> 4) * 0x9E3779B97F4A7C15ull) >> 40) & (SP_HASH_SIZE - 1);
}

static SP_SLOT *find(ULONG_PTR user)
{
    for (ULONG i = g_hash[hash_of(user)]; i; i = g_slots[i - 1].Next)
        if (g_slots[i - 1].User == user)
            return &g_slots[i - 1];
    return nullptr;
}

static void hash_remove(SP_SLOT *s)
{
    ULONG *link = &g_hash[hash_of(s->User)];
    while (*link && &g_slots[*link - 1] != s)
        link = &g_slots[*link - 1].Next;
    if (*link)
        *link = s->Next;
    s->Next = 0;
}

/* what the heuristic stack walk takes for a call: the address behind a call instruction in loaded code. */
static BOOLEAN follows_call(ULONG_PTR ra)
{
    if (!MiIsCodeAddress(ra) || !MmIsAddressValid((PVOID)(ra - 8)))
        return FALSE;
    const UCHAR *c = (const UCHAR *)ra;
#ifdef __x86_64__
    if (c[-5] == 0xE8)
        return TRUE;
    static constexpr int lengths[] = {2, 3, 4, 6, 7};
    for (int i = 0; i < 5; i++) {
        int n = lengths[i];
        if (c[-n] == 0xFF && ((c[-n + 1] >> 3) & 7) == 2)
            return TRUE;
        if (n > 2 && c[-n] == 0x41 && c[-n + 1] == 0xFF && ((c[-n + 2] >> 3) & 7) == 2)
            return TRUE;
    }
    return FALSE;
#else
    ULONG insn = *(const ULONG *)(ra - 4);
    return (insn & 0xFC000000) == 0x94000000 || (insn & 0xFFFFFC1F) == 0xD63F0000;
#endif
}

/*
 * the kernel keeps no frame pointers, so the callers are what on the stack looks like a return address: the first is the
 * caller of the allocator itself, the rest are the best guess of the ones behind it.
 */
[[clang::noinline]] static void capture_callers(PVOID ra, PVOID out[SP_CALLERS])
{
    volatile ULONG_PTR here = 0;
    const ULONG_PTR *word = (const ULONG_PTR *)&here;
    PKTHREAD thread = KiCurrentThread();
    ULONG_PTR limit = thread ? (ULONG_PTR)thread->InitialStack : 0;
    ULONG_PTR top = (ULONG_PTR)word + 512 * sizeof(ULONG_PTR);
    if (limit > (ULONG_PTR)word && top > limit)
        top = limit;
    const ULONG_PTR *end = (const ULONG_PTR *)top;
    const ULONG_PTR *start = word;
    for (const ULONG_PTR *w = word; w < word + 64 && w < end; w++)
        if (*w == (ULONG_PTR)ra)
            start = w + 1;
    ULONG n = 0;
    out[n++] = ra;
    for (const ULONG_PTR *w = start; w < end && n < SP_CALLERS; w++) {
        if (((ULONG_PTR)w & (PAGE_SIZE - 1)) == 0 && !MmIsAddressValid((PVOID)w))
            break;
        if (*w != (ULONG_PTR)out[n - 1] && follows_call(*w))
            out[n++] = (PVOID)*w;
    }
    while (n < SP_CALLERS)
        out[n++] = nullptr;
}

static void name_tag(ULONG tag, char out[5])
{
    for (int i = 0; i < 4; i++) {
        UCHAR c = (UCHAR)(tag >> (8 * i));
        out[i] = c >= 0x20 && c < 0x7F ? (char)c : '.';
    }
    out[4] = 0;
}

static void print_callers(const char *what, PVOID *callers)
{
    char text[SP_CALLERS][64];
    ULONG n = 0;
    for (; n < SP_CALLERS && callers[n]; n++)
        MiDescribeCodeAddress((ULONG_PTR)callers[n], text[n], sizeof(text[n]));
    KiLogPrintf("  %s: %s %s %s %s %s %s", what, n > 0 ? text[0] : "-", n > 1 ? text[1] : "", n > 2 ? text[2] : "",
                n > 3 ? text[3] : "", n > 4 ? text[4] : "", n > 5 ? text[5] : "");
}

static void print_slot(const SP_SLOT *s)
{
    char tag[5];
    name_tag(s->Tag, tag);
    KiLogPrintf("  block %p, %llu bytes, tag '%s', %s, allocation %llu", (PVOID)s->User, (ULONG64)s->Size, tag,
                s->State == SP_LIVE ? "allocated" : s->State == SP_FREED ? "freed" : "unused", s->AllocSeq);
    print_callers("allocated by", (PVOID *)s->AllocCallers);
    if (s->State == SP_FREED) {
        KiLogPrintf("  freed as number %llu", s->FreeSeq);
        print_callers("freed by", (PVOID *)s->FreeCallers);
    }
}

static SP_SLOT *slot_around(ULONG_PTR address)
{
    for (ULONG i = 0; i < g_slot_count; i++) {
        SP_SLOT *s = &g_slots[i];
        if (s->State != SP_UNUSED && address >= s->Va && address < s->Va + ((ULONG_PTR)(s->Pages + 2) << PAGE_SHIFT))
            return s;
    }
    return nullptr;
}

static void enter_report(const char *what, PVOID *callers)
{
    KiDisableInterrupts();
    if (g_reporting)
        HalPanic("pool report inside a pool report");
    g_reporting = TRUE;
    KiLogPrintf("pool: %s", what);
    print_callers("by", callers);
}

/* the bug check codes are those of the special pool of Windows. */
#define BUGCHECK_BAD_POOL_CALLER 0x000000C2
#define BUGCHECK_SPECIAL_POOL_CORRUPT 0x000000C1
#define BUGCHECK_FREED_SPECIAL_POOL 0x000000D5
#define BUGCHECK_BEYOND_ALLOCATION 0x000000D6

static BOOLEAN bytes_are(const UCHAR *p, SIZE_T n, UCHAR value, SIZE_T *bad)
{
    for (SIZE_T i = 0; i < n; i++)
        if (p[i] != value) {
            *bad = i;
            return FALSE;
        }
    return TRUE;
}

/* the canaries of a live block: false and a description when one was written over. */
static BOOLEAN check_canaries(SP_SLOT *s, const char **what, SIZE_T *offset)
{
    ULONG_PTR data = s->Va + PAGE_SIZE;
    ULONG_PTR data_end = data + ((ULONG_PTR)s->Pages << PAGE_SHIFT);
    if (s->User > data && !bytes_are((const UCHAR *)data, s->User - data, SP_PRE_BYTE, offset)) {
        *what = "bytes in front of the block were written";
        return FALSE;
    }
    ULONG_PTR tail = s->User + s->Size;
    if (tail < data_end && !bytes_are((const UCHAR *)tail, data_end - tail, SP_TAIL_BYTE, offset)) {
        *what = "bytes behind the block were written";
        return FALSE;
    }
    return TRUE;
}

/* every live block's canaries, so a write that stayed inside the last page is found soon after it. */
ULONG MiSpecialPoolSweep(BOOLEAN fatal)
{
    ULONG bad = 0;
    if (!g_ready)
        return 0;
    for (ULONG i = 0; i < g_slot_count; i++) {
        SP_SLOT *s = &g_slots[i];
        const char *what;
        SIZE_T offset;
        if (s->State != SP_LIVE || !MmIsAddressValid((PVOID)(s->Va + PAGE_SIZE)) || check_canaries(s, &what, &offset))
            continue;
        bad++;
        KiLogPrintf("pool: %s, %llu bytes in, found by a sweep", what, (ULONG64)offset);
        print_slot(s);
        if (!fatal)
            return bad;
    }
    return bad;
}

static void report_all(void)
{
    KiLogPrintf("pool: %llu blocks live, %llu allocations, %llu without guard pages", g_live_blocks, g_seq, g_fallbacks);
    if (!MiSpecialPoolSweep(TRUE))
        KiLogPrintf("pool: no canary of a live block is damaged");
}

/* from every bug check, so a failure of the ordinary pool names the blocks that are damaged. */
void MiSpecialPoolBugCheckReport(void)
{
    if (!g_ready || g_reporting)
        return;
    g_reporting = TRUE;
    report_all();
}

static void release_range(SP_SLOT *s)
{
    ULONG index = (ULONG)(s - g_slots) + 1;
    ULONG total = s->Pages + 2;
    s->State = SP_UNUSED;
    s->User = 0;
    if (total <= SP_FREE_CLASSES) {
        s->Next = g_free_list[total];
        g_free_list[total] = index;
    } else {
        s->Next = g_free_big;
        g_free_big = index;
    }
}

static SP_SLOT *take_range(ULONG total)
{
    if (total <= SP_FREE_CLASSES) {
        ULONG i = g_free_list[total];
        if (i) {
            g_free_list[total] = g_slots[i - 1].Next;
            g_slots[i - 1].Next = 0;
            return &g_slots[i - 1];
        }
    } else {
        for (ULONG *link = &g_free_big; *link; link = &g_slots[*link - 1].Next) {
            SP_SLOT *s = &g_slots[*link - 1];
            if (s->Pages + 2 == total) {
                *link = s->Next;
                s->Next = 0;
                return s;
            }
        }
    }
    if (g_slot_count >= SP_MAX_SLOTS || g_bump + total > SP_WINDOW_PAGES)
        return nullptr;
    ULONG needed = (ULONG)(((ULONG64)(g_slot_count + 1) * sizeof(SP_SLOT) + PAGE_SIZE - 1) >> PAGE_SHIFT);
    while (g_slot_mapped < needed) {
        ULONG64 pa = MmAllocatePage();
        if (!pa || !MiMapPage(MiKernelRoot(), (ULONG_PTR)g_slots + ((ULONG_PTR)g_slot_mapped << PAGE_SHIFT), pa, MM_KERNEL_RW))
            return nullptr;
        g_slot_mapped++;
    }
    SP_SLOT *s = &g_slots[g_slot_count++];
    s->Va = g_window + (g_bump << PAGE_SHIFT);
    s->Pages = total - 2;
    g_bump += total;
    return s;
}

PVOID MiSpecialPoolAllocate(SIZE_T size, ULONG tag, BOOLEAN zero, PVOID ra)
{
    if (!g_ready && !initialize())
        return nullptr;
    if (size == 0)
        size = 1;
    SIZE_T pages = BYTES_TO_PAGES(size);
    if (pages > g_budget_pages || g_live_pages + pages > g_budget_pages || g_reporting) {
        g_fallbacks++;
        return nullptr;
    }
    PVOID callers[SP_CALLERS];
    capture_callers(ra, callers);

    ULONG64 state = KiDisableInterrupts();
    SP_SLOT *s = take_range((ULONG)pages + 2);
    if (!s) {
        KiRestoreInterrupts(state);
        g_fallbacks++;
        return nullptr;
    }
    ULONG64 pa = MmAllocateContiguous(pages);
    if (!pa) {
        release_range(s);
        KiRestoreInterrupts(state);
        return nullptr;
    }
    ULONG_PTR data = s->Va + PAGE_SIZE;
    for (SIZE_T i = 0; i < pages; i++)
        if (!MiMapPage(MiKernelRoot(), data + (i << PAGE_SHIFT), pa + (i << PAGE_SHIFT), MM_KERNEL_RW))
            KeBugCheckEx(BUGCHECK_SPECIAL_POOL_CORRUPT, data, 0, 0, 0x40);
    ULONG_PTR data_end = data + (pages << PAGE_SHIFT);
    ULONG_PTR user = size > SP_LARGE_BYTES ? data : data_end - ((size + 15) & ~(SIZE_T)15);
    s->User = user;
    s->Size = size;
    s->Tag = tag;
    s->Pa = pa;
    s->State = SP_LIVE;
    s->AllocSeq = ++g_seq;
    s->FreeSeq = 0;
    for (ULONG i = 0; i < SP_CALLERS; i++) {
        s->AllocCallers[i] = callers[i];
        s->FreeCallers[i] = nullptr;
    }
    ULONG *head = &g_hash[hash_of(user)];
    s->Next = *head;
    *head = (ULONG)(s - g_slots) + 1;
    g_live_pages += pages;
    g_live_blocks++;
    BOOLEAN sweep = g_seq >= g_sweep_due;
    if (sweep)
        g_sweep_due = g_seq + SP_SWEEP_EVERY;
    KiRestoreInterrupts(state);

    memset((PVOID)data, SP_PRE_BYTE, user - data);
    memset((PVOID)(user + size), SP_TAIL_BYTE, data_end - user - size);
    if (MmVerifyPoolFill && !zero)
        memset((PVOID)user, SP_FILL_BYTE, size);
    if (sweep && MiSpecialPoolSweep(FALSE)) {
        g_reporting = TRUE;
        KeBugCheckEx(BUGCHECK_SPECIAL_POOL_CORRUPT, user, 0, 0, 0x23);
    }
    return (PVOID)user;
}

[[noreturn]] static void misuse(ULONG code, const char *what, SP_SLOT *s, ULONG_PTR p, PVOID *callers, ULONG_PTR extra)
{
    enter_report(what, callers);
    if (s)
        print_slot(s);
    report_all();
    KeBugCheckEx(code, p, extra, s ? s->Tag : 0, s ? s->Size : 0);
    __builtin_unreachable();
}

void MiSpecialPoolFree(PVOID block, ULONG tag, PVOID ra)
{
    ULONG_PTR p = (ULONG_PTR)block;
    PVOID callers[SP_CALLERS];
    capture_callers(ra, callers);

    SP_SLOT *s = find(p);
    if (!s) {
        SP_SLOT *around = slot_around(p);
        if (around && around->State == SP_LIVE)
            misuse(BUGCHECK_BAD_POOL_CALLER, "free of an address inside a block, not its start", around, p, callers, 0x20);
        if (around)
            misuse(BUGCHECK_BAD_POOL_CALLER, "free of an address in a block freed already", around, p, callers, 7);
        misuse(BUGCHECK_BAD_POOL_CALLER, "free of an address in the verifying pool that was never a block, or one freed long ago",
               nullptr, p, callers, 7);
    }
    if (s->State != SP_LIVE)
        misuse(BUGCHECK_BAD_POOL_CALLER, "block freed twice", s, p, callers, 7);
    if (tag && tag != s->Tag) {
        char want[5];
        name_tag(tag, want);
        KiLogPrintf("pool: freed with the tag '%s'", want);
        misuse(BUGCHECK_BAD_POOL_CALLER, "block freed with another tag than it was allocated with", s, p, callers, 0x0A);
    }
    const char *what;
    SIZE_T offset;
    if (!check_canaries(s, &what, &offset)) {
        KiLogPrintf("pool: first damaged byte is %llu bytes into the canary", (ULONG64)offset);
        misuse(BUGCHECK_SPECIAL_POOL_CORRUPT, what, s, p, callers, 0x21);
    }

    ULONG64 state = KiDisableInterrupts();
    ULONG pages = s->Pages;
    ULONG_PTR data = s->Va + PAGE_SIZE;
    for (ULONG i = 0; i < pages; i++)
        MiUnmapPage(MiKernelRoot(), data + ((ULONG_PTR)i << PAGE_SHIFT), 0);
    MmFreeContiguous(s->Pa, pages);
    s->State = SP_FREED;
    s->FreeSeq = ++g_seq;
    for (ULONG i = 0; i < SP_CALLERS; i++)
        s->FreeCallers[i] = callers[i];
    g_live_pages -= pages;
    g_live_blocks--;
    if (g_quarantine_count == SP_QUARANTINE) {
        SP_SLOT *old = &g_slots[g_quarantine[g_quarantine_head] - 1];
        hash_remove(old);
        release_range(old);
        g_quarantine_count--;
        g_quarantine[g_quarantine_head] = 0;
        g_quarantine_head = (g_quarantine_head + 1) % SP_QUARANTINE;
    }
    g_quarantine[(g_quarantine_head + g_quarantine_count++) % SP_QUARANTINE] = (ULONG)(s - g_slots) + 1;
    KiRestoreInterrupts(state);
}

/* a fault at an address of the window: what the access ran into. never returns when it is one. */
void MiSpecialPoolFault(ULONG_PTR address, ULONG error, ULONG_PTR rip)
{
    if (!in_window(address))
        return;
    BOOLEAN write = (error & 2) != 0;
    char where[64];
    MiDescribeCodeAddress(rip, where, sizeof(where));
    SP_SLOT *s = slot_around(address);
    KiDisableInterrupts();
    g_reporting = TRUE;
    if (!s) {
        KiLogPrintf("pool: %s of %p at %s, an address of the verifying pool that is not in any block", write ? "write" : "read",
                    (PVOID)address, where);
        report_all();
        KeBugCheckEx(BUGCHECK_BEYOND_ALLOCATION, address, write, rip, 2);
    }
    ULONG_PTR data = s->Va + PAGE_SIZE;
    ULONG_PTR data_end = data + ((ULONG_PTR)s->Pages << PAGE_SHIFT);
    const char *kind;
    ULONG code = BUGCHECK_BEYOND_ALLOCATION;
    if (s->State != SP_LIVE) {
        kind = s->State == SP_FREED ? "use of a block after it was freed" : "use of a range whose block was freed long ago";
        code = BUGCHECK_FREED_SPECIAL_POOL;
    } else if (address >= data_end) {
        kind = "access past the end of the block";
    } else {
        kind = "access before the start of the block";
    }
    KiLogPrintf("pool: %s of %p at %s, %s", write ? "write" : "read", (PVOID)address, where, kind);
    if (s->State != SP_UNUSED) {
        KiLogPrintf("  %lld bytes from the start of the block", (LONG64)(address - s->User));
        print_slot(s);
    } else {
        KiLogPrintf("  the range was a block of %u pages", s->Pages);
    }
    report_all();
    KeBugCheckEx(code, address, write, rip, s->State == SP_LIVE ? 0 : 1);
}

/* what the verifying pool is doing, for tests: the counts it keeps. */
void MiSpecialPoolQuery(MIME_POOL_INFORMATION *out)
{
    memset(out, 0, sizeof(*out));
    out->Verifying = MmVerifyPool && g_ready;
    out->Fill = MmVerifyPoolFill;
    out->LiveBlocks = g_live_blocks;
    out->LivePages = g_live_pages;
    out->Allocations = g_seq;
    out->Fallbacks = g_fallbacks;
}

/* misuse on purpose, each ends in a bug check that names it. */
NTSTATUS MiSpecialPoolSelfTest(ULONG operation)
{
    if (!MmVerifyPool)
        return STATUS_NOT_SUPPORTED;
    SIZE_T size = operation == MIME_POOL_TEST_UNDERRUN_GUARD ? 8192 : operation == MIME_POOL_TEST_TAIL_CANARY ? 60 : 64;
    if (operation == MIME_POOL_TEST_UNDERRUN_CANARY)
        size = 100;
    volatile UCHAR *p = ExAllocatePoolWithTag(NonPagedPool, size, KI_TAG('T', 's', 't', '1'));
    if (!p)
        return STATUS_NO_MEMORY;
    switch (operation) {
    case MIME_POOL_TEST_DOUBLE_FREE:
        ExFreePool((PVOID)p);
        ExFreePool((PVOID)p);
        break;
    case MIME_POOL_TEST_WRONG_TAG:
        ExFreePoolWithTag((PVOID)p, KI_TAG('B', 'a', 'd', '2'));
        break;
    case MIME_POOL_TEST_OVERRUN:
        p[64] = 1;
        break;
    case MIME_POOL_TEST_USE_AFTER_FREE:
        ExFreePool((PVOID)p);
        p[0] = 1;
        break;
    case MIME_POOL_TEST_TAIL_CANARY:
        p[62] = 0;
        ExFreePool((PVOID)p);
        break;
    case MIME_POOL_TEST_UNDERRUN_CANARY:
        p[-1] = 0;
        ExFreePool((PVOID)p);
        break;
    case MIME_POOL_TEST_UNDERRUN_GUARD:
        p[-1] = 1;
        break;
    case MIME_POOL_TEST_INTERIOR_FREE:
        ExFreePool((PVOID)(p + 16));
        break;
    default:
        ExFreePool((PVOID)p);
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_UNSUCCESSFUL;
}
