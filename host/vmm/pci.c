#include "pci.h"
#include "vmm.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PCI_ADDRESS 0xCF8
#define PCI_RESET 0xCF9
#define PCI_DATA 0xCFC
#define PCI_ENABLE 0x80000000u
#define MCFG 0x4746434Du

typedef struct PciFunction {
    uint8_t bus, devfn;
    int32_t parent; /* index of the bridge above it, -1 on a root bus. */
    uint64_t sizes[7];
    uint8_t config[MIME_PCI_CONFIG];
    uint8_t writable[MIME_PCI_CONFIG]; /* bits a write sets. */
    uint8_t clear[MIME_PCI_CONFIG];    /* bits a write of one clears. */
} PciFunction;

struct MimePciBus {
    pthread_mutex_t lock;
    uint32_t address;      /* 0xcf8 */
    uint32_t address_bits; /* those it keeps of a write. */
    uint8_t reset;         /* 0xcf9 */
    uint64_t ecam;         /* bus 0 of the mcfg window, which spans ecam_start to ecam_end. */
    uint64_t ecam_start, ecam_end;
    uint32_t count;
    PciFunction functions[];
};

static uint16_t get16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static uint32_t get32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static void writable(PciFunction *f, uint32_t at, uint32_t bits, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes && at + i < MIME_PCI_CONFIG; i++)
        f->writable[at + i] |= (uint8_t)(bits >> 8 * i);
}

static void clearable(PciFunction *f, uint32_t at, uint32_t bits, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes && at + i < MIME_PCI_CONFIG; i++)
        f->clear[at + i] |= (uint8_t)(bits >> 8 * i);
}

/* the standard capability of an id, 0 without one. */
static uint32_t capability(const PciFunction *f, uint8_t id)
{
    if (!(f->config[0x06] & 0x10))
        return 0;
    uint32_t at = f->config[0x34] & 0xFC;
    for (int n = 0; n < 48 && at >= 0x40; n++, at = f->config[at + 1] & 0xFC)
        if (f->config[at] == id)
            return at;
    return 0;
}

/* a bar keeps the bits under its size and its type, so writing ones reads back the size. io decodes 16 bits. */
static void bar_masks(PciFunction *f, uint32_t bars)
{
    for (uint32_t i = 0; i < bars; i++) {
        uint32_t at = 0x10 + 4 * i, value = get32(f->config + at);
        bool wide = !(value & 1) && (value & 6) == 4;
        uint64_t size = f->sizes[i];
        if (size) {
            uint64_t mask = ~(size - 1);
            if (value & 1) {
                writable(f, at, (uint32_t)mask & 0xFFFCu, 4);
            } else {
                writable(f, at, (uint32_t)mask & ~0xFu, 4);
                if (wide && i + 1 < bars)
                    writable(f, at + 4, (uint32_t)(mask >> 32), 4);
            }
        }
        if (wide)
            i++;
    }
}

static void rom_mask(PciFunction *f, uint32_t at)
{
    if (f->sizes[6])
        writable(f, at, ((uint32_t)~(f->sizes[6] - 1) & 0xFFFFF800u) | 1, 4);
}

static void express_masks(PciFunction *f, uint32_t at)
{
    uint16_t flags = get16(f->config + at + 2);
    uint32_t version = flags & 0xF, port = flags >> 4 & 0xF;
    writable(f, at + 0x08, 0x7FFF, 2); /* device control, whose function level reset reads 0. */
    clearable(f, at + 0x0A, 0x000F, 2);
    writable(f, at + 0x10, 0x0FDB, 2); /* link control, whose retrain reads 0. */
    clearable(f, at + 0x12, 0xC000, 2);
    if (flags & 0x100) {
        writable(f, at + 0x18, 0x1FFF, 2);
        clearable(f, at + 0x1A, 0x011F, 2);
    }
    if (port == 4 || port == 10) {
        writable(f, at + 0x1C, 0x001F, 2);
        clearable(f, at + 0x20, 0x00010000, 4);
    }
    if (version >= 2) {
        writable(f, at + 0x28, 0xFFFF, 2);
        writable(f, at + 0x30, 0xFFFF, 2);
    }
}

static void capability_masks(PciFunction *f)
{
    if (!(f->config[0x06] & 0x10))
        return;
    uint32_t at = f->config[0x34] & 0xFC;
    for (int n = 0; n < 48 && at >= 0x40; n++, at = f->config[at + 1] & 0xFC) {
        switch (f->config[at]) {
        case 0x01: /* power management: the state and pme enable, and pme status */
            writable(f, at + 4, 0x0103, 2);
            clearable(f, at + 4, 0x8000, 2);
            break;
        case 0x05: { /* msi */
            uint16_t control = get16(f->config + at + 2);
            uint32_t data = control & 0x80 ? at + 12 : at + 8;
            writable(f, at + 2, 0x0071, 2);
            writable(f, at + 4, 0xFFFFFFFCu, 4);
            if (control & 0x80)
                writable(f, at + 8, 0xFFFFFFFFu, 4);
            writable(f, data, 0xFFFF, 2);
            if (control & 0x100)
                writable(f, data + 4, 0xFFFFFFFFu, 4);
            break;
        }
        case 0x10:
            express_masks(f, at);
            break;
        case 0x11: /* msi-x enable and function mask */
            writable(f, at + 2, 0xC000, 2);
            break;
        default:
            break;
        }
    }
}

/* what a write changes, as the specification has each register; device specific registers stay read only. */
static void masks(PciFunction *f)
{
    bool express = capability(f, 0x10) != 0;
    writable(f, 0x04, 0x0547, 2); /* io, memory, bus master, parity, serr and intx disable */
    clearable(f, 0x06, 0xF900, 2);
    writable(f, 0x0C, 0xFF, 1);
    if (!express)
        writable(f, 0x0D, 0xFF, 1);
    writable(f, 0x3C, 0xFF, 1);
    switch (f->config[0x0E] & 0x7F) {
    case 0:
        bar_masks(f, 6);
        rom_mask(f, 0x30);
        break;
    case 1:
        bar_masks(f, 2);
        writable(f, 0x18, 0xFFFFFF, 3); /* primary, secondary and subordinate bus */
        if (!express)
            writable(f, 0x1B, 0xFF, 1);
        writable(f, 0x1C, 0xF0F0, 2);
        clearable(f, 0x1E, 0xF900, 2);
        writable(f, 0x20, 0xFFF0FFF0u, 4);
        writable(f, 0x24, 0xFFF0FFF0u, 4);
        if ((f->config[0x24] & 0xF) == 1) {
            writable(f, 0x28, 0xFFFFFFFFu, 4);
            writable(f, 0x2C, 0xFFFFFFFFu, 4);
        }
        if ((f->config[0x1C] & 0xF) == 1)
            writable(f, 0x30, 0xFFFFFFFFu, 4);
        rom_mask(f, 0x38);
        writable(f, 0x3E, 0x005F, 2);
        break;
    default:
        break;
    }
    capability_masks(f);
}

static bool bridge(const PciFunction *f)
{
    return (f->config[0x0E] & 0x7F) == 1;
}

static uint32_t bus_of(const MimePciBus *b, const PciFunction *f)
{
    return f->parent < 0 ? f->bus : b->functions[f->parent].config[0x19];
}

/* bus numbers route through the bridges as they are programmed now, so renumbering a bridge moves what is behind it. */
static PciFunction *find(MimePciBus *b, uint32_t bus, uint32_t devfn)
{
    for (uint32_t i = 0; i < b->count; i++) {
        PciFunction *f = &b->functions[i];
        if (f->devfn != devfn || bus_of(b, f) != bus)
            continue;
        bool routed = true;
        uint32_t depth = 0;
        for (int32_t p = f->parent; p >= 0 && routed && depth++ < b->count; p = b->functions[p].parent)
            routed = bus >= b->functions[p].config[0x19] && bus <= b->functions[p].config[0x1A];
        if (routed)
            return f;
    }
    return nullptr;
}

static void access_byte(MimePciBus *b, uint32_t bus, uint32_t devfn, uint32_t offset, uint8_t *data, bool write)
{
    PciFunction *f = find(b, bus, devfn);
    if (!f) {
        if (!write)
            *data = 0xFF;
        return;
    }
    if (!write) {
        *data = f->config[offset];
        return;
    }
    uint8_t value = *data, kept = (uint8_t)(f->config[offset] & ~f->writable[offset]);
    f->config[offset] = (uint8_t)((kept | (value & f->writable[offset])) & ~(value & f->clear[offset]));
}

bool mime_pci_io(MimeVm *vm, uint16_t port, uint32_t size, bool write, uint8_t *data)
{
    MimePciBus *b = vm->pci;
    if (!b || port < PCI_ADDRESS || port > PCI_DATA + 3)
        return false;
    pthread_mutex_lock(&b->lock);
    if (port == PCI_ADDRESS && size == 4) {
        if (write)
            b->address = get32(data) & b->address_bits;
        else
            memcpy(data, &b->address, 4);
    } else if (port >= PCI_DATA) {
        for (uint32_t i = 0; i < size; i++) {
            uint32_t at = port + i;
            if (at > PCI_DATA + 3 || !(b->address & PCI_ENABLE)) {
                if (!write)
                    data[i] = 0xFF;
                continue;
            }
            access_byte(b, b->address >> 16 & 0xFF, b->address >> 8 & 0xFF, (b->address & 0xFC) + (at - PCI_DATA),
                        &data[i], write);
        }
    } else if (port == PCI_RESET && size == 1) {
        /* the reset control register: a reset is the machine's to make, the guest gets none. */
        if (!write) {
            data[0] = b->reset;
        } else {
            b->reset = data[0] & 0x0A;
            if (data[0] & 4)
                mime_warn("pci: the guest asked for a reset through 0xcf9, it does not happen");
        }
    } else if (!write) {
        memset(data, 0xFF, size);
    }
    pthread_mutex_unlock(&b->lock);
    return true;
}

/* the memory a function's bars and a bridge's windows decode now. */
static bool decodes(const PciFunction *f, uint64_t address)
{
    const uint8_t *c = f->config;
    uint32_t bars = bridge(f) ? 2 : (c[0x0E] & 0x7F) == 0 ? 6 : 0;
    for (uint32_t i = 0; i < bars; i++) {
        uint32_t value = get32(c + 0x10 + 4 * i);
        bool wide = !(value & 1) && (value & 6) == 4;
        uint64_t base = value & ~0xFull, size = f->sizes[i];
        if (wide && i + 1 < bars)
            base |= (uint64_t)get32(c + 0x14 + 4 * i) << 32;
        if (!(value & 1) && size && base && address >= base && address - base < size)
            return true;
        if (wide)
            i++;
    }
    uint32_t rom = get32(c + (bridge(f) ? 0x38 : 0x30));
    if ((rom & 1) && f->sizes[6] && (rom & ~0x7FFu) && address >= (rom & ~0x7FFu) &&
        address - (rom & ~0x7FFu) < f->sizes[6])
        return true;
    if (!bridge(f))
        return false;
    uint64_t base = (uint64_t)(get16(c + 0x20) & 0xFFF0) << 16, limit = (uint64_t)(get16(c + 0x22) & 0xFFF0) << 16 | 0xFFFFF;
    if (base && base <= limit && address >= base && address <= limit)
        return true;
    base = (uint64_t)(get16(c + 0x24) & 0xFFF0) << 16;
    limit = (uint64_t)(get16(c + 0x26) & 0xFFF0) << 16 | 0xFFFFF;
    if ((c[0x24] & 0xF) == 1) {
        base |= (uint64_t)get32(c + 0x28) << 32;
        limit |= (uint64_t)get32(c + 0x2C) << 32;
    }
    return base && base <= limit && address >= base && address <= limit;
}

/*
 * the ecam window answers as the ports do. memory a bar or a window decodes reads all ones, as a device whose
 * registers mime does not have, where any other memory without ram stops the machine.
 */
bool mime_pci_mmio(MimeVm *vm, uint64_t address, uint32_t size, bool write, uint8_t *data)
{
    MimePciBus *b = vm->pci;
    if (!b)
        return false;
    pthread_mutex_lock(&b->lock);
    bool ours = address >= b->ecam_start && address < b->ecam_end;
    if (ours) {
        for (uint32_t i = 0; i < size; i++) {
            uint64_t at = address + i;
            if (at >= b->ecam_end) {
                if (!write)
                    data[i] = 0xFF;
                continue;
            }
            at -= b->ecam;
            access_byte(b, (uint32_t)(at >> 20) & 0xFF, (uint32_t)(at >> 12) & 0xFF, (uint32_t)at & 0xFFF, &data[i],
                        write);
        }
    } else {
        for (uint32_t i = 0; i < b->count && !ours; i++)
            ours = decodes(&b->functions[i], address);
        if (ours && !write)
            memset(data, 0xFF, size);
    }
    pthread_mutex_unlock(&b->lock);
    return ours;
}

static bool pci_path(const MimePaths *paths, char *out, size_t size)
{
    mime_path_join(out, size, paths->state, "pci.bin");
    return out[0] != 0;
}

bool mime_pci_save(const MimePaths *paths, const MimePciCapture *capture, size_t size)
{
    char path[MIME_PATH_MAX];
    int fd = pci_path(paths, path, sizeof(path)) ? open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600) : -1;
    bool saved = fd >= 0 && write(fd, capture, size) == (ssize_t)size;
    if (fd >= 0)
        close(fd);
    if (!saved) {
        mime_error("cannot write this machine's pci devices to %s", path);
        unlink(path);
    }
    return saved;
}

static MimePciCapture *load(const MimePaths *paths)
{
    char path[MIME_PATH_MAX];
    int fd = pci_path(paths, path, sizeof(path)) ? open(path, O_RDONLY | O_CLOEXEC) : -1;
    if (fd < 0)
        return nullptr;
    struct stat st;
    MimePciCapture *capture = nullptr;
    if (fstat(fd, &st) == 0 && st.st_size > (off_t)sizeof(MimePciCapture) && st.st_size < (64 << 20) &&
        (capture = malloc((size_t)st.st_size))) {
        size_t total = 0;
        ssize_t got;
        while (total < (size_t)st.st_size && (got = read(fd, (uint8_t *)capture + total, (size_t)st.st_size - total)) > 0)
            total += (size_t)got;
        if (total != (size_t)st.st_size || capture->magic != MIME_PCI_MAGIC ||
            sizeof(*capture) + (size_t)capture->count * sizeof(MimePciHostFunction) != total) {
            free(capture);
            capture = nullptr;
        }
    }
    close(fd);
    return capture;
}

/* the mcfg the guest is served says where it looks for the configuration spaces in memory. */
static void find_ecam(MimeVm *vm, MimePciBus *b)
{
    MimeBootInfo *boot = vm->boot;
    if (!boot->firmware_phys || boot->firmware_size < sizeof(MimeFirmware))
        return;
    const uint8_t *blob = vm->ram + (boot->firmware_phys - MIME_RAM_BASE);
    const MimeFirmware *firmware = (const MimeFirmware *)blob;
    for (uint32_t i = 0; i < firmware->count; i++) {
        const MimeFirmwareTable *t = &firmware->tables[i];
        if (t->signature != MCFG || (uint64_t)t->offset + t->length > boot->firmware_size)
            continue;
        for (uint32_t at = 44; at + 16 <= t->length; at += 16) {
            const uint8_t *e = blob + t->offset + at;
            uint64_t base;
            memcpy(&base, e, sizeof(base));
            if (get16(e + 8) != 0 || e[11] < e[10])
                continue;
            b->ecam = base;
            b->ecam_start = base + ((uint64_t)e[10] << 20);
            b->ecam_end = base + (((uint64_t)e[11] + 1) << 20);
        }
    }
    bool ram = b->ecam_start < MIME_RAM_BASE + vm->ram_size && b->ecam_end > MIME_RAM_BASE;
    bool windows = b->ecam_start < MIME_WINDOW_BASE + MIME_WINDOW_SIZE && b->ecam_end > MIME_WINDOW_BASE;
    if (ram || windows) {
        mime_warn("pci: the ecam window at %#llx falls on guest memory, only the ports answer",
                  (unsigned long long)b->ecam_start);
        b->ecam_start = b->ecam_end = 0;
    }
}

bool mime_pci_init(MimeVm *vm)
{
#if defined(__x86_64__)
    MimePciCapture *capture = load(vm->paths);
    if (!capture) {
        mime_error("the pci devices of this machine were not saved, start the machine with `sudo mime up`");
        return false;
    }
    MimePciBus *b = calloc(1, sizeof(*b) + capture->count * sizeof(PciFunction));
    if (!b) {
        free(capture);
        mime_error("cannot reserve memory for the pci bus");
        return false;
    }
    for (uint32_t i = 0; i < capture->count; i++) {
        const MimePciHostFunction *h = &capture->functions[i];
        PciFunction *f = &b->functions[b->count++];
        f->bus = h->bus;
        f->devfn = h->devfn;
        f->parent = -1;
        memcpy(f->sizes, h->sizes, sizeof(f->sizes));
        memcpy(f->config, h->config, MIME_MIN(h->length, (uint32_t)MIME_PCI_CONFIG));
        /* a user reads the header alone, so the list of capabilities would point at nothing. */
        if (h->length < 256) {
            f->config[0x06] &= ~0x10;
            f->config[0x34] = 0;
        }
    }
    for (uint32_t i = 0; i < b->count; i++) {
        uint16_t parent = capture->functions[i].parent;
        for (uint32_t j = 0; parent != MIME_PCI_ROOT && j < b->count; j++)
            if (j != i && (b->functions[j].bus << 8 | b->functions[j].devfn) == parent && bridge(&b->functions[j]))
                b->functions[i].parent = (int32_t)j;
        masks(&b->functions[i]);
    }
    free(capture);
    /* amd latches the extended register number in bits 24 to 27, intel keeps them zero. */
    const PciFunction *host = find(b, 0, 0);
    b->address_bits = host && get16(host->config) == 0x1022 ? 0x8FFFFFFCu : 0x80FFFFFCu;
    pthread_mutex_init(&b->lock, nullptr);
    find_ecam(vm, b);
    vm->pci = b;
    if (b->ecam_end)
        mime_info("pci: %u functions, ecam at %#llx", b->count, (unsigned long long)b->ecam_start);
    else
        mime_info("pci: %u functions", b->count);
#else
    (void)vm;
#endif
    return true;
}

void mime_pci_destroy(MimeVm *vm)
{
    if (!vm->pci)
        return;
    pthread_mutex_destroy(&vm->pci->lock);
    free(vm->pci);
    vm->pci = nullptr;
}
