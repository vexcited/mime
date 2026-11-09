#include "../vmm/vmm.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
