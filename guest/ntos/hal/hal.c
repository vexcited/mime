/*
 * hardware abstraction layer for the mime paravirtual machine, the parts
 * every processor shares. the interrupt controller, the timer and the
 * hypercall instruction live in arch/<processor>/hal.c.
 */
#include "../ntos.h"

MimeBootInfo *HalBootInfo;
MimeSharedPage *HalSharedPage;

static char g_log_buffer[1024];
static ULONG64 g_freq;

ULONG64 NTAPI HalMimeHypercall(ULONG64 nr, ULONG64 a1, ULONG64 a2, ULONG64 a3, ULONG64 a4, ULONG64 a5, ULONG64 a6,
                               PULONG64 value)
{
    return HalHypercall(nr, a1, a2, a3, a4, a5, a6, value);
}

ULONG64 NTAPI HalMimeHypercallUnlocked(ULONG64 nr, ULONG64 a1, ULONG64 a2, ULONG64 a3, ULONG64 a4, ULONG64 a5,
                                       ULONG64 a6, PULONG64 value)
{
    return HalHypercallUnlocked(nr, a1, a2, a3, a4, a5, a6, value);
}

const void *NTAPI HalMimeGetBootInfo(void)
{
    return HalBootInfo;
}

PKEVENT NTAPI HalMimeGetInputEvent(void)
{
    return &MimeInputReadyEvent;
}

PKEVENT NTAPI HalMimeGetGpuEvent(void)
{
    return &MimeGpuEvent;
}

void *HalPhysmap(ULONG64 pa)
{
    return (void *)(MIME_KVA_PHYSMAP + pa);
}

void HalLog(const char *text, SIZE_T length)
{
    ULONG64 state = KiDisableInterrupts();
    while (length) {
        SIZE_T n = length < sizeof(g_log_buffer) ? length : sizeof(g_log_buffer);
        memcpy(g_log_buffer, text, n);
        HalHypercall(MIME_HC_LOG, HalPhysicalOf(g_log_buffer), n, 0, 0, 0, 0, nullptr);
        text += n;
        length -= n;
    }
    KiRestoreInterrupts(state);
}

void HalPanic(const char *text)
{
    KiDisableInterrupts();
    SIZE_T n = strlen(text);
    if (n > sizeof(g_log_buffer))
        n = sizeof(g_log_buffer);
    memcpy(g_log_buffer, text, n);
    for (;;)
        HalHypercall(MIME_HC_PANIC, HalPhysicalOf(g_log_buffer), n, 0, 0, 0, 0, nullptr);
}

ULONG64 HalCounterToHns(ULONG64 delta)
{
    /* split to avoid 128 bit division, which needs compiler runtime helpers. */
    return delta / g_freq * 10000000u + delta % g_freq * 10000000u / g_freq;
}

ULONG64 HalHnsToCounter(ULONG64 hns)
{
    return hns / 10000000u * g_freq + (hns % 10000000u * g_freq + 9999999u) / 10000000u;
}

static const MimeRomHeader *g_rom;

static WCHAR fold(WCHAR c)
{
    return c >= 'a' && c <= 'z' ? (WCHAR)(c - 32) : c;
}

/* the files of the rom by the case folded hash of their names, for the lookup every open of the system drive makes. */
#define ROM_HASH_SLOTS 4096 /* a power of two, more than twice the files */
static USHORT g_rom_hash[ROM_HASH_SLOTS]; /* the index of an entry plus one */
static BOOLEAN g_rom_hashed;

static ULONG rom_hash(const WCHAR *text, ULONG chars)
{
    ULONG h = 2166136261u;
    for (ULONG i = 0; i < chars; i++)
        h = (h ^ fold(text[i])) * 16777619u;
    return h;
}

static void rom_index(void)
{
    g_rom_hashed = FALSE;
    if (g_rom->count * 2 > ROM_HASH_SLOTS)
        return;
    for (ULONG i = 0; i < g_rom->count; i++) {
        const char *p = g_rom->entries[i].path;
        WCHAR wide[sizeof(g_rom->entries[i].path)];
        ULONG n = 0;
        while (n < sizeof(wide) && p[n]) {
            wide[n] = (WCHAR)(UCHAR)p[n];
            n++;
        }
        ULONG slot = rom_hash(wide, n) & (ROM_HASH_SLOTS - 1);
        while (g_rom_hash[slot])
            slot = (slot + 1) & (ROM_HASH_SLOTS - 1);
        g_rom_hash[slot] = (USHORT)(i + 1);
    }
    g_rom_hashed = TRUE;
}

const MimeRomEntry *HalRomFind(PCWSTR path, ULONG chars)
{
    if (g_rom_hashed) {
        for (ULONG slot = rom_hash(path, chars) & (ROM_HASH_SLOTS - 1); g_rom_hash[slot];
             slot = (slot + 1) & (ROM_HASH_SLOTS - 1)) {
            const MimeRomEntry *e = &g_rom->entries[g_rom_hash[slot] - 1];
            ULONG n = 0;
            while (n < chars && n < sizeof(e->path) && e->path[n] && fold((WCHAR)(UCHAR)e->path[n]) == fold(path[n]))
                n++;
            if (n == chars && (n == sizeof(e->path) || !e->path[n]))
                return e;
        }
        return nullptr;
    }
    for (ULONG i = 0; i < g_rom->count; i++) {
        const char *p = g_rom->entries[i].path;
        ULONG n = 0;
        while (n < chars && n < sizeof(g_rom->entries[i].path) && p[n] && fold((WCHAR)(UCHAR)p[n]) == fold(path[n]))
            n++;
        if (n == chars && (n == sizeof(g_rom->entries[i].path) || !p[n]))
            return &g_rom->entries[i];
    }
    return nullptr;
}

const MimeRomEntry *HalRomEntry(ULONG index)
{
    return index < g_rom->count ? &g_rom->entries[index] : nullptr;
}

ULONG64 HalRomPhysical(const MimeRomEntry *entry)
{
    return HalBootInfo->rom_phys + entry->offset;
}

BOOLEAN HalIsRomPhysical(ULONG64 pa)
{
    return pa >= HalBootInfo->rom_phys && pa < HalBootInfo->rom_phys + HalBootInfo->rom_size;
}

void HalInitialize(MimeBootInfo *boot)
{
    HalBootInfo = boot;
    HalSharedPage = (MimeSharedPage *)HalPhysmap(boot->shared_phys);
    g_rom = (const MimeRomHeader *)HalPhysmap(boot->rom_phys);
    rom_index();
    g_freq = boot->timer_frequency;
    HalInitializeArch();
}
