#include "identity.h"
#include "locale.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void ini_path(const MimePaths *paths, char *out, size_t size)
{
    snprintf(out, size, "%s/machine.ini", paths->data);
}

/* like windows setup: DESKTOP- and 7 random letters or digits. */
static void make_computer_name(char *out, size_t size)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char suffix[8];
    for (int i = 0; i < 7; i++)
        suffix[i] = alphabet[arc4random_uniform(sizeof(alphabet) - 1)];
    suffix[7] = 0;
    snprintf(out, size, "DESKTOP-%s", suffix);
}

/* serial number of the case, like the one printed under a real machine. */
static void make_machine_serial(char *out, size_t size)
{
    size = MIME_MIN(size, 15);
    for (size_t i = 0; i + 1 < size; i++)
        out[i] = (char)('0' + arc4random_uniform(10));
    out[size - 1] = 0;
}

/* random uuid version 4, the form windows setup gives every machine. */
static void make_machine_uuid(char *out, size_t size)
{
    uint32_t r[4];
    for (int i = 0; i < 4; i++)
        r[i] = arc4random();
    r[1] = (r[1] & 0xFFFF0FFF) | 0x00004000; /* version 4 */
    r[2] = (r[2] & 0x3FFFFFFF) | 0x80000000; /* the rfc 4122 variant */
    snprintf(out, size, "%08X-%04X-%04X-%04X-%04X%08X", r[0], r[1] >> 16, r[1] & 0xFFFF, r[2] >> 16,
             r[2] & 0xFFFF, r[3]);
}

#ifdef __linux__
/* a line of a sysfs file, empty when it cannot be read. */
static void read_line(const char *path, char *out, size_t size)
{
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    if (fgets(out, (int)size, f)) {
        size_t n = strlen(out);
        while (n && (out[n - 1] == '\n' || out[n - 1] == ' '))
            out[--n] = 0;
    }
    fclose(f);
}

/*
 * the brand string of cpuid, which programs in the guest read themselves
 * anyway. windows skips its leading spaces and keeps the trailing ones,
 * which linux trims.
 */
static void cpu_brand(char *out, size_t size)
{
    uint32_t regs[12];
    for (uint32_t i = 0; i < 3; i++)
        __asm__ volatile("cpuid"
                         : "=a"(regs[i * 4]), "=b"(regs[i * 4 + 1]), "=c"(regs[i * 4 + 2]), "=d"(regs[i * 4 + 3])
                         : "a"(0x80000002 + i), "c"(0));
    char brand[49];
    memcpy(brand, regs, 48);
    brand[48] = 0;
    const char *p = brand;
    while (*p == ' ')
        p++;
    mime_strlcpy(out, p, size);
}

/*
 * on a pc the guest is the machine it runs on: the processor and the
 * firmware tables are the host's, as a windows installed on it would
 * report them, so nothing tells a virtual machine apart.
 */
const MimeHardwareProfile *mime_hardware_profile(void)
{
    static char cpu[64], manufacturer[64], product[64], family[64], bios_vendor[64], bios_version[64], bios_date[16];
    static MimeHardwareProfile profile;
    if (profile.cpu_name)
        return &profile;
    cpu_brand(cpu, sizeof(cpu));
    read_line("/sys/class/dmi/id/sys_vendor", manufacturer, sizeof(manufacturer));
    read_line("/sys/class/dmi/id/product_name", product, sizeof(product));
    read_line("/sys/class/dmi/id/product_family", family, sizeof(family));
    read_line("/sys/class/dmi/id/bios_vendor", bios_vendor, sizeof(bios_vendor));
    read_line("/sys/class/dmi/id/bios_version", bios_version, sizeof(bios_version));
    read_line("/sys/class/dmi/id/bios_date", bios_date, sizeof(bios_date));
    char mhz[32];
    read_line("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", mhz, sizeof(mhz));
    profile.cpu_name = cpu;
    profile.cpu_mhz = (uint32_t)(strtoull(mhz, nullptr, 10) / 1000);
    profile.cpu_midr = 0;
    profile.manufacturer = manufacturer[0] ? manufacturer : "System manufacturer";
    profile.product = product[0] ? product : "System Product Name";
    profile.family = family[0] ? family : "To be filled by O.E.M.";
    profile.bios_vendor = bios_vendor[0] ? bios_vendor : "American Megatrends Inc.";
    profile.bios_version = bios_version[0] ? bios_version : "1.0";
    profile.bios_date = bios_date[0] ? bios_date : "01/01/2024";
    return &profile;
}
#else
/*
 * the reference machine: a microsoft surface laptop 7, the windows 11 arm64
 * flagship, with the snapdragon x elite that machine carries. a program in
 * the guest sees these as the processor and firmware of the computer it
 * runs on, in the registry, the smbios tables and the environment.
 */
const MimeHardwareProfile *mime_hardware_profile(void)
{
    static const MimeHardwareProfile profile = {
        .cpu_name = "Qualcomm(R) Snapdragon(TM) X Elite - X1E-80-100",
        .cpu_mhz = 3800,
        /* qualcomm oryon: implementer 0x51, part 0x001. */
        .cpu_midr = 0x511F0012ull,
        .manufacturer = "Microsoft Corporation",
        .product = "Surface Laptop 7",
        .family = "Surface",
        .bios_vendor = "Microsoft Corporation",
        .bios_version = "1.14.140.0",
        .bios_date = "03/03/2025",
    };
    return &profile;
}

#endif

/* the host login name, restricted to what windows allows in an account name. */
static void make_user_name(char *out, size_t size)
{
    const struct passwd *pw = getpwuid(getuid());
    const char *name = pw && pw->pw_name ? pw->pw_name : "User";
    size_t n = 0;
    for (const char *c = name; *c && n + 1 < size && n < 20; c++)
        if (!strchr("\"/\\[]:;|=,+*?<>@ ", *c) && (unsigned char)*c >= 0x20 && (unsigned char)*c < 0x7F)
            out[n++] = *c;
    out[n] = 0;
    if (n == 0)
        mime_strlcpy(out, "User", size);
}

static void save(const MimePaths *paths, const MimeIdentity *id)
{
    char path[MIME_PATH_MAX], text[600];
    ini_path(paths, path, sizeof(path));
    int n = snprintf(text, sizeof(text),
                     "[machine]\ncomputer=%s\nuser=%s\nsid=S-1-5-21-%u-%u-%u\nserial=%08X\ninstalled=%llu\n"
                     "machineserial=%s\nuuid=%s\nlocale=%s\n",
                     id->computer_name, id->user_name, id->sid[0], id->sid[1], id->sid[2], id->volume_serial,
                     (unsigned long long)id->install_filetime, id->machine_serial, id->machine_uuid, id->locale);
    mime_write_file(path, text, (size_t)n);
}

bool mime_identity_load(const MimePaths *paths, MimeIdentity *out)
{
    memset(out, 0, sizeof(*out));
    char path[MIME_PATH_MAX];
    ini_path(paths, path, sizeof(path));
    size_t size;
    char *text = mime_read_file(path, &size);
    if (text) {
        for (char *line = strtok(text, "\n"); line; line = strtok(nullptr, "\n")) {
            char *eq = strchr(line, '=');
            if (!eq)
                continue;
            *eq = 0;
            const char *v = eq + 1;
            if (strcmp(line, "computer") == 0)
                mime_strlcpy(out->computer_name, v, sizeof(out->computer_name));
            else if (strcmp(line, "user") == 0)
                mime_strlcpy(out->user_name, v, sizeof(out->user_name));
            else if (strcmp(line, "sid") == 0)
                sscanf(v, "S-1-5-21-%u-%u-%u", &out->sid[0], &out->sid[1], &out->sid[2]);
            else if (strcmp(line, "serial") == 0)
                out->volume_serial = (uint32_t)strtoul(v, nullptr, 16);
            else if (strcmp(line, "installed") == 0)
                out->install_filetime = strtoull(v, nullptr, 10);
            else if (strcmp(line, "machineserial") == 0)
                mime_strlcpy(out->machine_serial, v, sizeof(out->machine_serial));
            else if (strcmp(line, "uuid") == 0)
                mime_strlcpy(out->machine_uuid, v, sizeof(out->machine_uuid));
            else if (strcmp(line, "locale") == 0)
                mime_strlcpy(out->locale, v, sizeof(out->locale));
        }
        free(text);
    }
    bool changed = false;
    if (!out->computer_name[0]) {
        make_computer_name(out->computer_name, sizeof(out->computer_name));
        changed = true;
    }
    if (!out->user_name[0]) {
        make_user_name(out->user_name, sizeof(out->user_name));
        changed = true;
    }
    if (!out->sid[0]) {
        /* the three sub authorities windows draws at install, never below 2^28 in practice. */
        for (int i = 0; i < 3; i++)
            out->sid[i] = 0x10000000u + arc4random_uniform(0xE0000000u);
        changed = true;
    }
    if (!out->volume_serial) {
        out->volume_serial = arc4random() | 1;
        changed = true;
    }
    if (!out->install_filetime) {
        out->install_filetime = ((uint64_t)time(nullptr) + 11644473600ull) * 10000000ull;
        changed = true;
    }
    if (!out->machine_serial[0]) {
        make_machine_serial(out->machine_serial, sizeof(out->machine_serial));
        changed = true;
    }
    if (!out->machine_uuid[0]) {
        make_machine_uuid(out->machine_uuid, sizeof(out->machine_uuid));
        changed = true;
    }
    /* like the language setup asks for, taken from the settings of the host the first time. */
    MimeLocale locale;
    if (!out->locale[0] || !mime_locale_find(out->locale, &locale)) {
        mime_locale_from_host(&locale);
        mime_strlcpy(out->locale, locale.name, sizeof(out->locale));
        changed = true;
    }
    if (changed)
        save(paths, out);
    return true;
}

bool mime_identity_set_locale(const MimePaths *paths, const char *name)
{
    MimeLocale locale;
    if (!mime_locale_find(name, &locale))
        return false;
    MimeIdentity id;
    mime_identity_load(paths, &id);
    mime_strlcpy(id.locale, locale.name, sizeof(id.locale));
    save(paths, &id);
    return true;
}
