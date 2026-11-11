#include "mime_host.h"

#include <mime/abi.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __linux__
#include <sys/sysmacros.h>
#else
#include <sys/mount.h>
#endif

#define KEY_MAX 160

typedef struct Found {
    MimeHostDrive drive;
    char key[KEY_MAX];
} Found;

static void ini_path(const MimePaths *paths, char *out, size_t size)
{
    snprintf(out, size, "%s/drives.ini", paths->data);
}

/* the keys of the letters, "uuid:..." or "path:...", empty for a free letter. */
static void read_letters(const MimePaths *paths, char keys[26][KEY_MAX])
{
    memset(keys, 0, 26 * KEY_MAX);
    char path[MIME_PATH_MAX];
    ini_path(paths, path, sizeof(path));
    char *text = mime_read_file(path, nullptr);
    if (!text)
        return;
    char *save = nullptr;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(nullptr, "\n", &save)) {
        char letter = (char)toupper((unsigned char)line[0]);
        if (letter < MIME_FS_DRIVE_FIRST || letter > MIME_FS_DRIVE_LAST || line[1] != '=')
            continue;
        size_t n = strcspn(line + 2, "\r");
        if (n && n < KEY_MAX) {
            memcpy(keys[letter - 'A'], line + 2, n);
            keys[letter - 'A'][n] = 0;
        }
    }
    free(text);
}

static void write_letters(const MimePaths *paths, char keys[26][KEY_MAX])
{
    char text[26 * (KEY_MAX + 4) + 16];
    size_t n = (size_t)snprintf(text, sizeof(text), "[drives]\n");
    for (int i = MIME_FS_DRIVE_FIRST - 'A'; i <= MIME_FS_DRIVE_LAST - 'A'; i++)
        if (keys[i][0])
            n += (size_t)snprintf(text + n, sizeof(text) - n, "%c=%s\n", 'A' + i, keys[i]);
    char path[MIME_PATH_MAX];
    ini_path(paths, path, sizeof(path));
    if (!mime_write_file(path, text, n))
        mime_info("drives: cannot record the letters in %s", path);
}

static uint32_t fnv(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

/* the label cut to the 31 bytes the guest takes, on a character boundary. */
static void set_label(MimeHostDrive *d, const char *label)
{
    size_t n = strlen(label);
    if (n >= sizeof(d->label)) {
        n = sizeof(d->label) - 1;
        while (n && ((uint8_t)label[n] & 0xC0) == 0x80)
            n--;
    }
    memcpy(d->label, label, n);
    d->label[n] = 0;
}

static void label_from_mount(MimeHostDrive *d)
{
    const char *slash = strrchr(d->root, '/');
    set_label(d, slash ? slash + 1 : d->root);
}

/*
 * the serial windows shows for the file system: fat and exfat keep it as their uuid, XXXX-XXXX, and ntfs as the low
 * half of its 64 bit one. others get one from the key.
 */
static uint32_t serial_of(const char *uuid, const char *key)
{
    size_t n = uuid ? strlen(uuid) : 0;
    char hex[9];
    if (n == 9 && uuid[4] == '-') {
        memcpy(hex, uuid, 4);
        memcpy(hex + 4, uuid + 5, 4);
    } else if (n == 16 && strspn(uuid, "0123456789abcdefABCDEF") == 16) {
        memcpy(hex, uuid + 8, 8);
    } else {
        return fnv(key) | 1;
    }
    hex[8] = 0;
    char *end;
    uint32_t serial = (uint32_t)strtoul(hex, &end, 16);
    return *end || !serial ? fnv(key) | 1 : serial;
}

#ifdef __linux__

/* mountinfo writes space, tab, newline and backslash as \ooo. */
static void unescape_octal(char *s)
{
    char *out = s;
    for (char *p = s; *p; p++) {
        if (p[0] == '\\' && p[1] >= '0' && p[1] <= '7' && p[2] >= '0' && p[2] <= '7' && p[3] >= '0' && p[3] <= '7') {
            *out++ = (char)((p[1] - '0') << 6 | (p[2] - '0') << 3 | (p[3] - '0'));
            p += 3;
        } else {
            *out++ = *p;
        }
    }
    *out = 0;
}

/* udev writes the characters it does not keep in link names as \xHH. */
static void unescape_hex(char *s)
{
    char *out = s;
    for (char *p = s; *p; p++) {
        if (p[0] == '\\' && p[1] == 'x' && isxdigit((unsigned char)p[2]) && isxdigit((unsigned char)p[3])) {
            char hex[3] = {p[2], p[3], 0};
            *out++ = (char)strtoul(hex, nullptr, 16);
            p += 3;
        } else {
            *out++ = *p;
        }
    }
    *out = 0;
}

/* the name of the device's link in a /dev/disk directory, by-uuid or by-label. */
static bool disk_link(const char *dir, dev_t dev, char *out, size_t size)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    bool found = false;
    for (struct dirent *e; !found && (e = readdir(d));) {
        if (e->d_name[0] == '.')
            continue;
        char path[MIME_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISBLK(st.st_mode) && st.st_rdev == dev) {
            mime_strlcpy(out, e->d_name, size);
            found = true;
        }
    }
    closedir(d);
    return found;
}

static bool sysfs_flag(unsigned major, unsigned minor, const char *name)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/dev/block/%u:%u/%s", major, minor, name);
    char *text = mime_read_file(path, nullptr);
    if (!text) {
        /* a partition has the attribute on its disk. */
        snprintf(path, sizeof(path), "/sys/dev/block/%u:%u/../%s", major, minor, name);
        text = mime_read_file(path, nullptr);
    }
    bool set = text && text[0] == '1';
    free(text);
    return set;
}

static bool under(const char *path, const char *dir)
{
    size_t n = strlen(dir);
    return !strncmp(path, dir, n) && (path[n] == '/' || !path[n]);
}

/* mounts that are part of the system rather than a drive of the user. */
static bool system_mount(const char *mount, const MimePaths *paths)
{
    static const char *const trees[] = {"/boot", "/efi",  "/usr",  "/var",      "/opt", "/srv", "/tmp",
                                        "/nix",  "/gnu",  "/snap", "/proc",     "/sys", "/dev", "/etc",
                                        "/bin",  "/sbin", "/lib",  "/lib64",    "/recovery", "/swap"};
    if (!strcmp(mount, "/") || !strcmp(mount, "/home") || !strcmp(mount, "/root"))
        return true;
    for (size_t i = 0; i < MIME_ARRAY_COUNT(trees); i++)
        if (under(mount, trees[i]))
            return true;
    if (under(mount, "/run") && !under(mount, "/run/media"))
        return true;
    /* drive_c's own tree is C: already. */
    return under(mount, paths->drive_c);
}

static size_t scan_host(const MimePaths *paths, Found *out, size_t max)
{
    FILE *f = fopen("/proc/self/mountinfo", "re");
    if (!f)
        return 0;
    size_t count = 0;
    dev_t taken[64];
    size_t taken_count = 0;
    char line[4 * MIME_PATH_MAX];
    while (count < max && fgets(line, sizeof(line), f)) {
        char *fields[64];
        int n = 0;
        char *save = nullptr;
        for (char *t = strtok_r(line, " \n", &save); t && n < 64; t = strtok_r(nullptr, " \n", &save))
            fields[n++] = t;
        int dash = 6;
        while (dash < n && strcmp(fields[dash], "-"))
            dash++;
        if (n < 10 || dash + 3 > n)
            continue;
        unsigned major, minor;
        if (sscanf(fields[2], "%u:%u", &major, &minor) != 2)
            continue;
        char *root = fields[3], *mount = fields[4], *options = fields[5];
        const char *type = fields[dash + 1];
        char *source = fields[dash + 2];
        unescape_octal(root);
        unescape_octal(mount);
        unescape_octal(source);
        /* a bind mount or a btrfs subvolume shows part of a file system, not a drive. */
        if (strcmp(root, "/") || strncmp(source, "/dev/", 5) || system_mount(mount, paths))
            continue;
        if (strlen(mount) >= sizeof(out->drive.root))
            continue;
        dev_t dev = makedev(major, minor);
        bool seen = false;
        for (size_t i = 0; i < taken_count; i++)
            seen |= taken[i] == dev;
        if (seen)
            continue;
        if (taken_count < MIME_ARRAY_COUNT(taken))
            taken[taken_count++] = dev;

        Found *d = &out[count++];
        memset(d, 0, sizeof(*d));
        mime_strlcpy(d->drive.root, mount, sizeof(d->drive.root));
        char uuid[96] = {}, label[96];
        if (disk_link("/dev/disk/by-uuid", dev, uuid, sizeof(uuid)))
            snprintf(d->key, sizeof(d->key), "uuid:%s", uuid);
        else
            snprintf(d->key, sizeof(d->key), "path:%s", mount);
        d->drive.serial = serial_of(uuid[0] ? uuid : nullptr, d->key);
        if (disk_link("/dev/disk/by-label", dev, label, sizeof(label))) {
            unescape_hex(label);
            set_label(&d->drive, label);
        } else {
            label_from_mount(&d->drive);
        }
        if (!strcmp(type, "iso9660") || !strcmp(type, "udf") || major == 11)
            d->drive.flags |= MIME_FS_DRIVE_CDROM | MIME_FS_DRIVE_READ_ONLY;
        else if (sysfs_flag(major, minor, "removable"))
            d->drive.flags |= MIME_FS_DRIVE_REMOVABLE;
        if (!strcmp(options, "ro") || !strncmp(options, "ro,", 3))
            d->drive.flags |= MIME_FS_DRIVE_READ_ONLY;
    }
    fclose(f);
    return count;
}

#else

static size_t scan_host(const MimePaths *paths, Found *out, size_t max)
{
    struct statfs *mounts;
    int n = getmntinfo(&mounts, MNT_NOWAIT);
    size_t count = 0;
    for (int i = 0; i < n && count < max; i++) {
        const struct statfs *m = &mounts[i];
        if (strncmp(m->f_mntonname, "/Volumes/", 9) || !(m->f_flags & MNT_LOCAL) || (m->f_flags & MNT_DONTBROWSE))
            continue;
        if (strlen(m->f_mntonname) >= sizeof(out->drive.root))
            continue;
        size_t c = strlen(paths->drive_c);
        if (!strncmp(m->f_mntonname, paths->drive_c, c) && (!m->f_mntonname[c] || m->f_mntonname[c] == '/'))
            continue;
        Found *d = &out[count++];
        memset(d, 0, sizeof(*d));
        mime_strlcpy(d->drive.root, m->f_mntonname, sizeof(d->drive.root));
        snprintf(d->key, sizeof(d->key), "path:%s", m->f_mntonname);
        d->drive.serial = serial_of(nullptr, d->key);
        label_from_mount(&d->drive);
        if (!strcmp(m->f_fstypename, "cd9660") || !strcmp(m->f_fstypename, "udf"))
            d->drive.flags |= MIME_FS_DRIVE_CDROM;
        if (m->f_flags & MNT_RDONLY)
            d->drive.flags |= MIME_FS_DRIVE_READ_ONLY;
    }
    return count;
}

#endif

static int by_root(const void *a, const void *b)
{
    return strcmp(((const Found *)a)->drive.root, ((const Found *)b)->drive.root);
}

/*
 * a letter stays with its file system across plugs and restarts, as windows keeps it in MountedDevices: data/drives.ini
 * records which file system has which.
 */
size_t mime_drives_scan(const MimePaths *paths, bool assign, MimeHostDrive *out, size_t max)
{
    Found *found = mime_xcalloc(MIME_FS_MAX_DRIVES * 2, sizeof(Found));
    size_t count = scan_host(paths, found, MIME_FS_MAX_DRIVES * 2);
    qsort(found, count, sizeof(Found), by_root);
    char (*keys)[KEY_MAX] = mime_xcalloc(26, KEY_MAX);
    read_letters(paths, keys);
    bool used[26] = {}, changed = false;
    for (size_t i = 0; i < count; i++)
        for (int l = MIME_FS_DRIVE_FIRST - 'A'; l <= MIME_FS_DRIVE_LAST - 'A'; l++)
            if (!found[i].drive.letter && !used[l] && !strcmp(keys[l], found[i].key)) {
                found[i].drive.letter = (char)('A' + l);
                used[l] = true;
            }
    for (size_t i = 0; assign && i < count; i++) {
        if (found[i].drive.letter)
            continue;
        /* a letter nobody had, else the one of a drive that is not plugged in. */
        int pick = -1;
        for (int l = MIME_FS_DRIVE_FIRST - 'A'; l <= MIME_FS_DRIVE_LAST - 'A' && pick < 0; l++)
            if (!used[l] && !keys[l][0])
                pick = l;
        for (int l = MIME_FS_DRIVE_FIRST - 'A'; l <= MIME_FS_DRIVE_LAST - 'A' && pick < 0; l++)
            if (!used[l])
                pick = l;
        if (pick < 0)
            break;
        mime_strlcpy(keys[pick], found[i].key, KEY_MAX);
        found[i].drive.letter = (char)('A' + pick);
        used[pick] = true;
        changed = true;
    }
    if (changed)
        write_letters(paths, keys);
    size_t n = 0;
    for (size_t i = 0; i < count && n < max; i++)
        if (found[i].drive.letter)
            out[n++] = found[i].drive;
    free(keys);
    free(found);
    return n;
}

bool mime_paths_drive_root(const MimePaths *paths, char letter, char *out, size_t size)
{
    letter = (char)toupper((unsigned char)letter);
    if (letter == 'C' || letter == 'Z') {
        mime_strlcpy(out, letter == 'C' ? paths->drive_c : "", size);
        return true;
    }
    if (letter < MIME_FS_DRIVE_FIRST || letter > MIME_FS_DRIVE_LAST)
        return false;
    MimeHostDrive *drives = mime_xcalloc(MIME_FS_MAX_DRIVES, sizeof(*drives));
    size_t count = mime_drives_scan(paths, false, drives, MIME_FS_MAX_DRIVES);
    bool found = false;
    for (size_t i = 0; i < count && !found; i++)
        if (drives[i].letter == letter) {
            mime_strlcpy(out, drives[i].root, size);
            found = true;
        }
    free(drives);
    return found;
}
