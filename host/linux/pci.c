#include "../vmm/vmm.h"
#include "../vmm/pci.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PCI_DEVICES "/sys/bus/pci/devices"

static unsigned long read_hex(const char *device, const char *name)
{
    char path[512], text[32] = "";
    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/%s", device, name);
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    if (!fgets(text, sizeof(text), f))
        text[0] = 0;
    fclose(f);
    return strtoul(text, nullptr, 16);
}

/* the subsystem, revision and place of each gpu, from the display controllers with its vendor and device in turn. */
void mime_host_gpu_pci(MimeBootGpu *gpus, uint32_t count)
{
    DIR *dir = opendir("/sys/bus/pci/devices");
    if (!dir)
        return;
    bool taken[MIME_BOOT_GPUS] = {};
    struct dirent *e;
    while ((e = readdir(dir))) {
        unsigned domain, bus, slot, function;
        if (sscanf(e->d_name, "%x:%x:%x.%x", &domain, &bus, &slot, &function) != 4 ||
            read_hex(e->d_name, "class") >> 16 != 0x03)
            continue;
        uint32_t vendor = (uint32_t)read_hex(e->d_name, "vendor"), device = (uint32_t)read_hex(e->d_name, "device");
        for (uint32_t i = 0; i < count && i < MIME_BOOT_GPUS; i++) {
            if (taken[i] || gpus[i].vendor != vendor || gpus[i].device != device)
                continue;
            taken[i] = true;
            gpus[i].subsystem = (uint32_t)(read_hex(e->d_name, "subsystem_device") << 16 |
                                           read_hex(e->d_name, "subsystem_vendor"));
            gpus[i].revision = (uint32_t)read_hex(e->d_name, "revision");
            gpus[i].bus = bus;
            gpus[i].slot = slot << 3 | function;
            break;
        }
    }
    closedir(dir);
}

static bool parse_address(const char *name, unsigned *domain, unsigned *bus, unsigned *devfn)
{
    unsigned slot, function;
    if (sscanf(name, "%x:%x:%x.%x", domain, bus, &slot, &function) != 4 || *bus > 255 || slot > 31 || function > 7)
        return false;
    *devfn = slot << 3 | function;
    return true;
}

/* the directory a function's sysfs entry sits in is the bridge above it, or a pci0000:00 host bridge. */
static uint16_t parent_of(const char *name)
{
    char path[512], target[512];
    snprintf(path, sizeof(path), PCI_DEVICES "/%s", name);
    ssize_t length = readlink(path, target, sizeof(target) - 1);
    if (length <= 0)
        return MIME_PCI_ROOT;
    target[length] = 0;
    char *last = strrchr(target, '/');
    if (!last)
        return MIME_PCI_ROOT;
    *last = 0;
    const char *above = strrchr(target, '/');
    above = above ? above + 1 : target;
    unsigned domain, bus, devfn;
    return parse_address(above, &domain, &bus, &devfn) ? (uint16_t)(bus << 8 | devfn) : MIME_PCI_ROOT;
}

/* root reads the whole space, a user the 64 bytes of the header. */
static uint32_t read_config(const char *name, uint8_t *out)
{
    char path[512];
    snprintf(path, sizeof(path), PCI_DEVICES "/%s/config", name);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    size_t total = 0;
    while (total < MIME_PCI_CONFIG) {
        ssize_t got = pread(fd, out + total, MIME_PCI_CONFIG - total, (off_t)total);
        if (got <= 0)
            break;
        total += (size_t)got;
    }
    close(fd);
    return (uint32_t)total;
}

/* an unassigned bar keeps its size in sysfs, from 0 to its size less one. */
static void read_sizes(const char *name, uint64_t sizes[7])
{
    char path[512], line[160];
    snprintf(path, sizeof(path), PCI_DEVICES "/%s/resource", name);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    for (int i = 0; i < 7 && fgets(line, sizeof(line), f); i++) {
        unsigned long long start, end, flags;
        if (sscanf(line, "%llx %llx %llx", &start, &end, &flags) == 3 && end > start)
            sizes[i] = end - start + 1;
    }
    fclose(f);
}

static int by_address(const void *a, const void *b)
{
    const MimePciHostFunction *x = a, *y = b;
    return (x->bus << 8 | x->devfn) - (y->bus << 8 | y->devfn);
}

bool mime_pci_capture(MimePciCapture **out, size_t *size)
{
    DIR *dir = opendir(PCI_DEVICES);
    MimePciCapture *capture = dir ? calloc(1, sizeof(*capture)) : nullptr;
    uint32_t room = 0;
    struct dirent *e;
    while (capture && (e = readdir(dir))) {
        unsigned domain, bus, devfn;
        char path[512];
        if (!parse_address(e->d_name, &domain, &bus, &devfn) || domain != 0)
            continue;
        /* a virtual function answers no configuration cycle, its vendor reads all ones. */
        snprintf(path, sizeof(path), PCI_DEVICES "/%s/physfn", e->d_name);
        if (access(path, F_OK) == 0)
            continue;
        if (capture->count == room) {
            room = room ? room * 2 : 32;
            MimePciCapture *grown = realloc(capture, sizeof(*capture) + room * sizeof(MimePciHostFunction));
            if (!grown) {
                free(capture);
                capture = nullptr;
                break;
            }
            capture = grown;
        }
        MimePciHostFunction *f = &capture->functions[capture->count];
        memset(f, 0, sizeof(*f));
        f->bus = (uint8_t)bus;
        f->devfn = (uint8_t)devfn;
        f->parent = parent_of(e->d_name);
        f->length = read_config(e->d_name, f->config);
        if (f->length < 64)
            continue;
        read_sizes(e->d_name, f->sizes);
        capture->count++;
    }
    if (dir)
        closedir(dir);
    if (!capture || !capture->count) {
        free(capture);
        mime_error("cannot read this machine's pci devices from " PCI_DEVICES ", so the machine does not start");
        return false;
    }
    capture->magic = MIME_PCI_MAGIC;
    qsort(capture->functions, capture->count, sizeof(MimePciHostFunction), by_address);
    *out = capture;
    *size = sizeof(*capture) + capture->count * sizeof(MimePciHostFunction);
    mime_info("pci: %u functions of this machine, %s", capture->count,
              capture->functions[0].length > 64 ? "whole configuration spaces" : "their headers only");
    return true;
}
