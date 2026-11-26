#include "firmware.h"
#include "mime_host.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FIRMWARE_ACPI_MAX 64
#define FIRMWARE_TABLE_MAX 0x40000 /* a dsdt is well over 64 KiB. */
#define FIRMWARE_DMI_MAX 0x8000

static bool firmware_path(const MimePaths *paths, char *out, size_t size)
{
    mime_path_join(out, size, paths->state, "firmware.bin");
    return out[0] != 0;
}

static uint8_t *firmware_alloc(void)
{
    uint8_t *blob = aligned_alloc(0x1000, MIME_FIRMWARE_MAX);
    if (blob)
        memset(blob, 0, MIME_FIRMWARE_MAX);
    return blob;
}

#ifdef __linux__

#include <dirent.h>

static bool read_path(const char *path, uint8_t *out, size_t cap, size_t *length)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    /* sysfs hands a binary file out a page at a time. */
    size_t total = 0;
    ssize_t got;
    while (total < cap && (got = read(fd, out + total, cap - total)) > 0)
        total += (size_t)got;
    close(fd);
    if (!total)
        return false;
    *length = total;
    return true;
}

/* the smbios entry point carries the version windows puts in the provider's header; the dmi table follows it. */
static void take_smbios(MimeFirmware *f, uint8_t *blob, size_t cap, size_t *at)
{
    uint8_t point[0x20];
    size_t point_length = 0;
    if (!read_path("/sys/firmware/dmi/tables/smbios_entry_point", point, sizeof(point), &point_length) ||
        point_length < 0x18)
        return;
    size_t length = 0;
    if (*at + sizeof(MimeFirmwareSmbios) >= cap)
        return;
    uint8_t *header = blob + *at;
    if (!read_path("/sys/firmware/dmi/tables/DMI", header + sizeof(MimeFirmwareSmbios),
                   MIME_MIN(cap - *at - sizeof(MimeFirmwareSmbios), FIRMWARE_DMI_MAX), &length))
        return;
    /* the 64-bit entry point has its length where the 32-bit one has the major version. */
    bool sm3 = memcmp(point, "_SM3_", 5) == 0;
    header[1] = point[sm3 ? 7 : 6];
    header[2] = point[sm3 ? 8 : 7];
    memcpy(header + 4, &(uint32_t){(uint32_t)length}, sizeof(uint32_t));
    f->smbios_offset = (uint32_t)*at;
    f->smbios_length = (uint32_t)(sizeof(MimeFirmwareSmbios) + length);
    *at += f->smbios_length;
}

/* the edid of a panel of this machine, which names the monitor of the guest as the host's. */
static void take_edid(MimeFirmware *f, uint8_t *blob, size_t cap, size_t *at)
{
    if (*at + 128 > cap)
        return;
    DIR *dir = opendir("/sys/class/drm");
    if (!dir)
        return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "card", 4) != 0 || !strchr(entry->d_name, '-'))
            continue;
        char path[96];
        snprintf(path, sizeof(path), "/sys/class/drm/%s/edid", entry->d_name);
        size_t length = 0;
        if (!read_path(path, blob + *at, 128, &length) || length != 128)
            continue;
        const uint8_t *edid = blob + *at;
        static const uint8_t head[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
        if (memcmp(edid, head, sizeof(head)))
            continue;
        uint8_t sum = 0;
        for (uint32_t i = 0; i < 128; i++)
            sum = (uint8_t)(sum + edid[i]);
        if (sum) {
            /* the checksum of the block, last byte included. */
            continue;
        }
        f->edid_offset = (uint32_t)*at;
        f->edid_length = 128;
        *at += 128;
        break;
    }
    closedir(dir);
}

/* the kernel numbers the tables that share a signature, SSDT1 to SSDT10 here: by signature, then by number. */
static int table_order(const void *a, const void *b)
{
    const char *x = a, *y = b;
    int order = strncmp(x, y, 4);
    return order ? order : atoi(x + 4) - atoi(y + 4);
}

static void take_acpi(MimeFirmware *f, uint8_t *blob, size_t cap, size_t *at, size_t tables_at)
{
    MimeFirmwareTable *tables = (MimeFirmwareTable *)(blob + tables_at);
    DIR *dir = opendir("/sys/firmware/acpi/tables");
    if (!dir)
        return;
    char names[FIRMWARE_ACPI_MAX][8];
    uint32_t count = 0;
    struct dirent *entry;
    while (count < FIRMWARE_ACPI_MAX && (entry = readdir(dir))) {
        size_t n = strlen(entry->d_name);
        if (n < 4 || n > 6 || strspn(entry->d_name + 4, "0123456789") != n - 4)
            continue;
        strcpy(names[count++], entry->d_name);
    }
    closedir(dir);
    qsort(names, count, sizeof(names[0]), table_order);
    for (uint32_t i = 0; i < count; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/sys/firmware/acpi/tables/%s", names[i]);
        size_t length = 0;
        if (*at + 4 >= cap || *at + FIRMWARE_TABLE_MAX > cap)
            break;
        if (!read_path(path, blob + *at, MIME_MIN(cap - *at, FIRMWARE_TABLE_MAX), &length))
            continue;
        memcpy(&tables[f->count].signature, names[i], sizeof(uint32_t));
        tables[f->count].offset = (uint32_t)*at;
        tables[f->count].length = (uint32_t)length;
        f->count++;
        *at += length;
    }
}


bool mime_firmware_build(MimeFirmware **out, size_t *size)
{
    uint8_t *blob = firmware_alloc();
    if (!blob)
        return false;
    MimeFirmware *f = (MimeFirmware *)blob;
    f->magic = MIME_FIRMWARE_MAGIC;
    /* the table array is walked before its count is known, so it sits at the front. */
    size_t tables_at = sizeof(MimeFirmware);
    size_t at = tables_at + FIRMWARE_ACPI_MAX * sizeof(MimeFirmwareTable);
    take_acpi(f, blob, MIME_FIRMWARE_MAX, &at, tables_at);
    take_smbios(f, blob, MIME_FIRMWARE_MAX, &at);
    take_edid(f, blob, MIME_FIRMWARE_MAX, &at);
    if (!f->count && !f->smbios_length && !f->edid_length) {
        free(blob);
        mime_warn("firmware: nothing of this machine's firmware could be read, the reference data is served");
        return false;
    }
    mime_info("firmware: %u acpi tables, smbios %u bytes, edid %u of this machine", f->count, f->smbios_length,
              f->edid_length);
    *out = f;
    *size = at;
    return true;
}

#else

bool mime_firmware_build(MimeFirmware **out, size_t *size)
{
    (void)out;
    (void)size;
    return false;
}

#endif

/* the firmware mime up read as root, handed to the machine, which runs as the user. */
bool mime_firmware_load(const MimePaths *paths, MimeFirmware **out, size_t *size)
{
    char path[MIME_PATH_MAX];
    int fd = firmware_path(paths, path, sizeof(path)) ? open(path, O_RDONLY | O_CLOEXEC) : -1;
    if (fd < 0)
        return false;
    uint8_t *blob = firmware_alloc();
    ssize_t got = blob ? read(fd, blob, MIME_FIRMWARE_MAX) : -1;
    close(fd);
    if (got > (ssize_t)sizeof(MimeFirmware) && ((MimeFirmware *)blob)->magic == MIME_FIRMWARE_MAGIC) {
        *out = (MimeFirmware *)blob;
        *size = (size_t)got;
        return true;
    }
    free(blob);
    return false;
}

void mime_firmware_save(const MimePaths *paths, const MimeFirmware *firmware, size_t size)
{
    char path[MIME_PATH_MAX];
    if (!firmware_path(paths, path, sizeof(path)))
        return;
    /* without a read of this machine the reference tables are served, not those of an earlier start. */
    if (!firmware) {
        unlink(path);
        return;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0 || write(fd, firmware, size) != (ssize_t)size) {
        mime_warn("cannot write %s, the guest gets the reference firmware", path);
        if (fd >= 0) {
            close(fd);
            unlink(path);
        }
        return;
    }
    close(fd);
}
