/*
 * loads ntoskrnl.exe into guest memory and builds the initial kernel
 * translation tables, so the guest starts directly at its entry point
 * with the mmu enabled.
 */
#include "vmm.h"

#include "../rom.h"

#include <stdlib.h>
#include <string.h>

#define LAYOUT_TABLES 0x10000ull
#define LAYOUT_TABLES_END 0x100000ull
#define LAYOUT_KERNEL 0x200000ull

/* stage 1 descriptor bits, 4K granule. */
#define PTE_VALID (1ull << 0)
#define PTE_TABLE (1ull << 1)
#define PTE_PAGE (1ull << 1)
#define PTE_ATTR(i) ((uint64_t)(i) << 2)
#define PTE_AP_RO (2ull << 6)
#define PTE_SH_INNER (3ull << 8)
#define PTE_AF (1ull << 10)
#define PTE_PXN (1ull << 53)
#define PTE_UXN (1ull << 54)
#define PTE_ADDR_MASK 0x0000FFFFFFFFF000ull

#define IMAGE_SCN_MEM_EXECUTE 0x20000000u
#define IMAGE_SCN_MEM_WRITE 0x80000000u

typedef struct TableAlloc {
    MimeVm *vm;
    uint64_t next;
    uint64_t end;
} TableAlloc;

static uint64_t alloc_table(TableAlloc *ta)
{
    if (ta->next + MIME_PAGE_SIZE > ta->end)
        mime_fatal("out of boot translation table memory");
    uint64_t pa = ta->next;
    ta->next += MIME_PAGE_SIZE;
    memset(mime_vm_phys(ta->vm, pa, MIME_PAGE_SIZE), 0, MIME_PAGE_SIZE);
    return pa;
}

static uint64_t *table_ptr(TableAlloc *ta, uint64_t pa)
{
    return mime_vm_phys(ta->vm, pa, MIME_PAGE_SIZE);
}

/* returns the table for `level` covering va, creating intermediate tables. */
static uint64_t *walk(TableAlloc *ta, uint64_t root, uint64_t va, int level)
{
    uint64_t *table = table_ptr(ta, root);
    for (int l = 0; l < level; l++) {
        int shift = 39 - 9 * l;
        uint64_t idx = (va >> shift) & 0x1FF;
        if (!(table[idx] & PTE_VALID))
            table[idx] = alloc_table(ta) | PTE_VALID | PTE_TABLE;
        table = table_ptr(ta, table[idx] & PTE_ADDR_MASK);
    }
    return table;
}

static void map_page(TableAlloc *ta, uint64_t root, uint64_t va, uint64_t pa, uint64_t attrs)
{
    uint64_t *l3 = walk(ta, root, va, 3);
    l3[(va >> 12) & 0x1FF] = (pa & PTE_ADDR_MASK) | attrs | PTE_VALID | PTE_PAGE;
}

static void map_physmap(TableAlloc *ta, uint64_t root, uint64_t ram_base, uint64_t ram_size)
{
    uint64_t attrs = PTE_ATTR(0) | PTE_SH_INNER | PTE_AF | PTE_UXN | PTE_PXN;
    for (uint64_t pa = ram_base; pa < ram_base + ram_size; pa += 1ull << 21) {
        uint64_t va = MIME_KVA_PHYSMAP + pa;
        uint64_t *l2 = walk(ta, root, va, 2);
        l2[(va >> 21) & 0x1FF] = pa | attrs | PTE_VALID; /* 2 MiB block */
    }
}

/* the file window range, read only in 1 GiB blocks. */
static void map_windows(TableAlloc *ta, uint64_t root)
{
    uint64_t attrs = PTE_ATTR(0) | PTE_SH_INNER | PTE_AF | PTE_UXN | PTE_PXN | PTE_AP_RO;
    for (uint64_t pa = MIME_WINDOW_BASE; pa < MIME_WINDOW_BASE + MIME_WINDOW_SIZE; pa += 1ull << 30) {
        uint64_t va = MIME_KVA_PHYSMAP + pa;
        uint64_t *l1 = walk(ta, root, va, 1);
        l1[(va >> 30) & 0x1FF] = pa | attrs | PTE_VALID;
    }
}

/* device-nGnRnE, mair index 1, for the gic registers. */
static void map_device(TableAlloc *ta, uint64_t root, uint64_t pa, uint64_t size)
{
    uint64_t attrs = PTE_ATTR(1) | PTE_AF | PTE_UXN | PTE_PXN;
    for (uint64_t off = 0; off < size; off += MIME_PAGE_SIZE)
        map_page(ta, root, MIME_KVA_PHYSMAP + pa + off, pa + off, attrs);
}

static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | (uint64_t)rd32(p + 4) << 32;
}

static bool apply_relocations(uint8_t *image, uint32_t image_size, uint32_t reloc_rva, uint32_t reloc_size, int64_t delta)
{
    uint32_t off = 0;
    while (off + 8 <= reloc_size) {
        uint32_t page = rd32(image + reloc_rva + off);
        uint32_t block = rd32(image + reloc_rva + off + 4);
        if (block < 8 || off + block > reloc_size)
            return false;
        for (uint32_t i = 8; i + 2 <= block; i += 2) {
            uint16_t entry = rd16(image + reloc_rva + off + i);
            uint32_t type = entry >> 12;
            uint32_t rva = page + (entry & 0xFFF);
            if (type == 0)
                continue;
            if (type != 10 || rva + 8 > image_size)
                return false; /* only IMAGE_REL_BASED_DIR64 is valid on arm64 */
            uint64_t v = rd64(image + rva) + (uint64_t)delta;
            memcpy(image + rva, &v, 8);
        }
        off += block;
    }
    return true;
}

/* the system files, read only, at physmap + rom_phys. */
static void map_rom(TableAlloc *ta, uint64_t root, uint64_t pa, uint64_t size)
{
    uint64_t attrs = PTE_ATTR(0) | PTE_SH_INNER | PTE_AF | PTE_UXN | PTE_PXN | PTE_AP_RO;
    for (uint64_t off = 0; off < size; off += MIME_PAGE_SIZE)
        map_page(ta, root, MIME_KVA_PHYSMAP + pa + off, pa + off, attrs);
}

bool mime_vm_load_kernel(MimeVm *vm)
{
    const char *path = "\\Windows\\System32\\ntoskrnl.exe";
    const MimeRomEntry *entry = mime_rom_find(path);
    if (!entry) {
        mime_error("this mime binary embeds no kernel");
        return false;
    }
    const uint8_t *file = mime_rom + entry->offset;
    size_t file_size = entry->size;
    bool ok = false;
    if (file_size < 0x200 || rd16(file) != 0x5A4D)
        goto out;
    uint32_t pe = rd32(file + 0x3C);
    if (pe + 0x108 > file_size || rd32(file + pe) != 0x00004550)
        goto out;
    uint16_t machine = rd16(file + pe + 4);
    uint16_t nsections = rd16(file + pe + 6);
    uint16_t opt_size = rd16(file + pe + 20);
    const uint8_t *opt = file + pe + 24;
    if (machine != 0xAA64 || rd16(opt) != 0x20B) {
        mime_error("kernel image is not a pe32+ arm64 image");
        goto out;
    }
    uint32_t entry_rva = rd32(opt + 16);
    uint64_t image_base = rd64(opt + 24);
    uint32_t image_size = rd32(opt + 56);
    uint32_t headers_size = rd32(opt + 60);
    uint32_t reloc_rva = rd32(opt + 112 + 5 * 8);
    uint32_t reloc_size = rd32(opt + 112 + 5 * 8 + 4);
    const uint8_t *sections = opt + opt_size;
    if ((size_t)(sections - file) + (size_t)nsections * 40 > file_size)
        goto out;

    MimeBootInfo *boot = vm->boot;
    uint64_t kernel_phys = MIME_RAM_BASE + LAYOUT_KERNEL;
    uint64_t kernel_virt = MIME_KVA_KERNEL + LAYOUT_KERNEL;
    uint64_t span = MIME_ALIGN_UP((uint64_t)image_size, MIME_PAGE_SIZE);
    uint8_t *image = mime_vm_phys(vm, kernel_phys, span);
    if (!image)
        goto out;
    memset(image, 0, span);
    memcpy(image, file, MIME_MIN((size_t)headers_size, file_size));

    for (uint16_t i = 0; i < nsections; i++) {
        const uint8_t *s = sections + i * 40;
        uint32_t vsize = rd32(s + 8);
        uint32_t rva = rd32(s + 12);
        uint32_t raw_size = rd32(s + 16);
        uint32_t raw_ptr = rd32(s + 20);
        uint32_t copy = MIME_MIN(raw_size, vsize ? vsize : raw_size);
        if ((uint64_t)rva + MIME_MAX(vsize, copy) > span || (uint64_t)raw_ptr + copy > file_size)
            goto out;
        memcpy(image + rva, file + raw_ptr, copy);
    }

    int64_t delta = (int64_t)(kernel_virt - image_base);
    if (delta && reloc_size && !apply_relocations(image, (uint32_t)span, reloc_rva, reloc_size, delta)) {
        mime_error("bad relocations in kernel image");
        goto out;
    }

    TableAlloc ta = {vm, MIME_RAM_BASE + LAYOUT_TABLES, MIME_RAM_BASE + LAYOUT_TABLES_END};
    uint64_t root = alloc_table(&ta);
    map_physmap(&ta, root, vm->boot->ram_base, vm->boot->ram_size);
    map_device(&ta, root, boot->gic_dist_phys, boot->gic_dist_size);
    map_device(&ta, root, boot->gic_redist_phys, boot->gic_redist_size);
    map_rom(&ta, root, boot->rom_phys, boot->rom_size);
    map_windows(&ta, root);

    uint64_t ro = PTE_ATTR(0) | PTE_SH_INNER | PTE_AF | PTE_UXN | PTE_PXN | PTE_AP_RO;
    for (uint64_t off = 0; off < MIME_ALIGN_UP((uint64_t)headers_size, MIME_PAGE_SIZE); off += MIME_PAGE_SIZE)
        map_page(&ta, root, kernel_virt + off, kernel_phys + off, ro);
    for (uint16_t i = 0; i < nsections; i++) {
        const uint8_t *s = sections + i * 40;
        uint32_t vsize = rd32(s + 8);
        uint32_t rva = rd32(s + 12);
        uint32_t chars = rd32(s + 36);
        uint64_t attrs = PTE_ATTR(0) | PTE_SH_INNER | PTE_AF | PTE_UXN;
        if (!(chars & IMAGE_SCN_MEM_EXECUTE))
            attrs |= PTE_PXN;
        if (!(chars & IMAGE_SCN_MEM_WRITE))
            attrs |= PTE_AP_RO;
        uint64_t end = MIME_ALIGN_UP((uint64_t)rva + vsize, MIME_PAGE_SIZE);
        for (uint64_t off = MIME_ALIGN_DOWN((uint64_t)rva, MIME_PAGE_SIZE); off < end; off += MIME_PAGE_SIZE)
            map_page(&ta, root, kernel_virt + off, kernel_phys + off, attrs);
    }

    boot->kernel_phys = kernel_phys;
    boot->kernel_virt = kernel_virt;
    boot->kernel_size = span;
    boot->ttbr1_phys = root;
    boot->first_free_phys = MIME_ALIGN_UP(kernel_phys + span, 1ull << 21);
    boot->reserved_count = 0;
    vm->kernel_entry = kernel_virt + entry_rva;
    mime_info("kernel loaded at %#llx (%u KiB), entry %#llx", (unsigned long long)kernel_virt, image_size / 1024,
              (unsigned long long)vm->kernel_entry);
    ok = true;
out:
    if (!ok)
        mime_error("invalid kernel image %s", path);
    return ok;
}
