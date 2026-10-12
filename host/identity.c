#include "identity.h"

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
    char path[MIME_PATH_MAX], text[512];
    ini_path(paths, path, sizeof(path));
    int n = snprintf(text, sizeof(text),
                     "[machine]\ncomputer=%s\nuser=%s\nsid=S-1-5-21-%u-%u-%u\nserial=%08X\ninstalled=%llu\n"
                     "machineserial=%s\nuuid=%s\n",
                     id->computer_name, id->user_name, id->sid[0], id->sid[1], id->sid[2], id->volume_serial,
                     (unsigned long long)id->install_filetime, id->machine_serial, id->machine_uuid);
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
        for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
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
                out->volume_serial = (uint32_t)strtoul(v, NULL, 16);
            else if (strcmp(line, "installed") == 0)
                out->install_filetime = strtoull(v, NULL, 10);
            else if (strcmp(line, "machineserial") == 0)
                mime_strlcpy(out->machine_serial, v, sizeof(out->machine_serial));
            else if (strcmp(line, "uuid") == 0)
                mime_strlcpy(out->machine_uuid, v, sizeof(out->machine_uuid));
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
        out->install_filetime = ((uint64_t)time(NULL) + 11644473600ull) * 10000000ull;
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
    if (changed)
        save(paths, out);
    return true;
}
