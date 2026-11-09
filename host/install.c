/*
 * creates the C: drive profile before boot. system files remain embedded in Mime and mapped through rom.h; they are
 * never written to the drive.
 */
#include "identity.h"
#include "mime_cli.h"
#include "rom.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <Security/Security.h>
#endif

static bool make_dirs(const MimePaths *paths, const char *root, const char *const *folders, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        char dir[MIME_PATH_MAX];
        snprintf(dir, sizeof(dir), "%s/%s/%s", paths->drive_c, root, folders[i]);
        if (!mime_dir_exists(dir) && !mime_mkdir_p(dir, 0755)) {
            mime_error("cannot create %s: %s", dir, strerror(errno));
            return false;
        }
    }
    return true;
}

/*
 * creates a folder below the drive with the case of what is there: the machine matches names without regard to case and
 * the installers of the features wrote some of these with another one, so a second folder that differs only by its case
 * must not appear next to the first.
 */
static bool make_dir_nocase(const char *root, const char *relative)
{
    char dir[MIME_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", root);
    size_t length = strlen(dir);
    while (*relative) {
        char name[256];
        size_t n = strcspn(relative, "/");
        if (!n || n >= sizeof(name))
            return false;
        memcpy(name, relative, n);
        name[n] = 0;
        relative += n + (relative[n] == '/');
        DIR *handle = opendir(dir);
        if (handle) {
            for (struct dirent *entry; (entry = readdir(handle));) {
                if (!strcasecmp(entry->d_name, name)) {
                    snprintf(name, sizeof(name), "%s", entry->d_name);
                    break;
                }
            }
            closedir(handle);
        }
        if (length + 1 + strlen(name) + 1 >= sizeof(dir))
            return false;
        dir[length++] = '/';
        memcpy(dir + length, name, strlen(name) + 1);
        length += strlen(name);
        if (mkdir(dir, 0755) && errno != EEXIST)
            return false;
    }
    return true;
}

static bool make_dirs_nocase(const MimePaths *paths, const char *const *folders, size_t count)
{
    for (size_t i = 0; i < count; i++)
        if (!make_dir_nocase(paths->drive_c, folders[i])) {
            mime_error("cannot create %s: %s", folders[i], strerror(errno));
            return false;
        }
    return true;
}

/* the folders of a user profile, the shell folders and the places programs write their data. */
static bool make_profile(const MimePaths *paths, const char *name)
{
    static const char *const folders[] = {
        "Desktop", "Documents", "Downloads", "Music", "Pictures", "Videos", "Favorites", "Links", "Saved Games",
        "AppData/Local/Temp", "AppData/LocalLow", "AppData/Local/Microsoft/Windows/INetCache",
        "AppData/Local/Microsoft/Windows/History", "AppData/Local/Microsoft/Windows/INetCookies",
        "AppData/Local/Microsoft/Windows/Shell", "AppData/Local/Microsoft/WindowsApps",
        "AppData/Roaming/Microsoft/Internet Explorer/Quick Launch", "AppData/Roaming/Microsoft/Windows/Network Shortcuts",
        "AppData/Roaming/Microsoft/Windows/Printer Shortcuts", "AppData/Roaming/Microsoft/Windows/Recent",
        "AppData/Roaming/Microsoft/Windows/SendTo", "AppData/Roaming/Microsoft/Windows/Start Menu/Programs/Startup",
        "AppData/Roaming/Microsoft/Windows/Templates",
    };
    char root[MIME_PATH_MAX];
    snprintf(root, sizeof(root), "Users/%s", name);
    if (!make_dirs(paths, root, folders, MIME_ARRAY_COUNT(folders)))
        return false;
    /* the junctions windows keeps for the programs of older versions, and its old shell folder names. */
    static const struct {
        const char *link, *target;
    } links[] = {
        {"Application Data", "AppData/Roaming"},
        {"Cookies", "AppData/Roaming/Microsoft/Windows/Cookies"},
        {"Local Settings", "AppData/Local"},
        {"My Documents", "Documents"},
        {"My Music", "Music"},
        {"My Pictures", "Pictures"},
        {"My Videos", "Videos"},
        {"NetHood", "AppData/Roaming/Microsoft/Windows/Network Shortcuts"},
        {"PrintHood", "AppData/Roaming/Microsoft/Windows/Printer Shortcuts"},
        {"Recent", "AppData/Roaming/Microsoft/Windows/Recent"},
        {"SendTo", "AppData/Roaming/Microsoft/Windows/SendTo"},
        {"Start Menu", "AppData/Roaming/Microsoft/Windows/Start Menu"},
        {"Templates", "AppData/Roaming/Microsoft/Windows/Templates"},
    };
    char path[MIME_PATH_MAX];
    for (size_t i = 0; i < MIME_ARRAY_COUNT(links); i++) {
        snprintf(path, sizeof(path), "%s/%s/%s", paths->drive_c, root, links[i].link);
        symlink(links[i].target, path);
    }
    return true;
}

/* the profile of the user, and the default one new profiles start from. its NTUSER.DAT is in the ROM. */
static bool create_profile(const MimePaths *paths)
{
    MimeIdentity id;
    mime_identity_load(paths, &id);
    return make_profile(paths, "Default") && make_profile(paths, id.user_name);
}

/*
 * the folders of a used installation that windows never lists: the recycle bin of the account, the volume's own
 * tracking folder, and the junction of the old name of the profile folder. z: gets none of them.
 */
static bool create_hidden_items(const MimePaths *paths)
{
    MimeIdentity identity;
    if (!mime_identity_load(paths, &identity))
        return true;
    char recycle[MIME_PATH_MAX], bin[MIME_PATH_MAX], file[MIME_PATH_MAX], system[MIME_PATH_MAX], legacy[MIME_PATH_MAX];
    snprintf(recycle, sizeof(recycle), "%s/$Recycle.Bin", paths->drive_c);
    snprintf(bin, sizeof(bin), "%s/S-1-5-21-%u-%u-%u-1001", recycle, identity.sid[0], identity.sid[1], identity.sid[2]);
    if (!mime_mkdir_p(recycle, 0755) || !mime_mkdir_p(bin, 0755))
        return false;
    snprintf(file, sizeof(file), "%s/desktop.ini", bin);
    FILE *f = fopen(file, "w");
    if (f) {
        fputs("[.ShellClassInfo]\nCLSID={645FF040-5081-101B-9F08-00AA002F954E}\n", f);
        fclose(f);
    }
    snprintf(system, sizeof(system), "%s/System Volume Information", paths->drive_c);
    if (!mime_mkdir_p(system, 0755))
        return false;
    snprintf(file, sizeof(file), "%s/IndexerVolumeGuid", system);
    f = fopen(file, "w");
    if (f) {
        fprintf(f, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n", identity.volume_serial, identity.sid[0] & 0xFFFF,
                identity.sid[1] & 0xFFFF, identity.sid[2] & 0xFF, identity.sid[2] >> 8 & 0xFF, identity.sid[0] & 0xFF,
                identity.sid[1] & 0xFF, identity.sid[2] & 0xFF, identity.sid[0] >> 8 & 0xFF, identity.sid[1] >> 8 & 0xFF,
                identity.sid[2] >> 16 & 0xFF);
        fclose(f);
    }
    snprintf(file, sizeof(file), "%s/tracking.log", system);
    f = fopen(file, "wb");
    if (f) {
        static const unsigned char header[] = {'G', 't', 'B', '\0', 1, 0, 0, 0};
        fwrite(header, 1, sizeof(header), f);
        fclose(f);
    }
    snprintf(legacy, sizeof(legacy), "%s/Documents and Settings", paths->drive_c);
    symlink("Users", legacy);
    snprintf(legacy, sizeof(legacy), "%s/Users/Default User", paths->drive_c);
    symlink("Default", legacy);
    snprintf(legacy, sizeof(legacy), "%s/Users/All Users", paths->drive_c);
    symlink("../ProgramData", legacy);
    snprintf(legacy, sizeof(legacy), "%s/ProgramData/Application Data", paths->drive_c);
    symlink(".", legacy);
    /* the system's small swap file, which uefi installations always have. */
    snprintf(file, sizeof(file), "%s/swapfile.sys", paths->drive_c);
    int fd = open(file, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd >= 0) {
        ftruncate(fd, 16 << 20);
        close(fd);
    }
    return true;
}

/*
 * creates the hosts-file directory so programs can read or watch it. a missing directory can prevent browsers from
 * initializing their resolvers. the new file contains no name entries, like a fresh Windows installation.
 */
static void create_name_files(const MimePaths *paths)
{
    char dir[MIME_PATH_MAX], path[MIME_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/Windows/System32/drivers/etc", paths->drive_c);
    if (!mime_mkdir_p(dir, 0755))
        return;
    mime_path_join(path, sizeof(path), dir, "hosts");
    if (mime_file_exists(path))
        return;
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fputs("# the names of machines and the addresses they have, one to a line, an address first and then its names.\r\n"
          "# a line that begins with a number sign is a comment. the name localhost is known without a line here.\r\n"
          "#\r\n"
          "#     102.54.94.97     rhino.example.com\r\n",
          f);
    fclose(f);
}

/*
 * creates shared profile, Start Menu and Common Files folders. shortcut creation may fail if either shared or per-user
 * folders are missing, and installers check they can write to Common Files before they start.
 */
static void create_shared_folders(const MimePaths *paths)
{
    static const char *const folders[] = {
        "Users/Public/Desktop",   "Users/Public/Documents", "Users/Public/Downloads", "Users/Public/Music",
        "Users/Public/Pictures",  "Users/Public/Videos",    "ProgramData/Microsoft/Windows/Start Menu/Programs",
        "ProgramData/Microsoft/Windows/Start Menu/Programs/Startup",
        "Program Files/Common Files", "Program Files (x86)/Common Files",
#ifdef __aarch64__
        "Program Files (Arm)/Common Files",
#endif
    };
    for (size_t i = 0; i < MIME_ARRAY_COUNT(folders); i++) {
        char dir[MIME_PATH_MAX];
        mime_path_join(dir, sizeof(dir), paths->drive_c, folders[i]);
        if (!mime_dir_exists(dir))
            mime_mkdir_p(dir, 0755);
    }
}

/* what a new C: drive holds before the first program runs, the system files come from the ROM. */
static bool create_skeleton(const MimePaths *paths)
{
    static const char *const folders[] = {
        "Windows/System32/config", "Windows/System32/drivers", "Windows/System32/en-US", "Windows/System32/wbem",
        "Windows/SysWOW64", "Windows/SysWOW64/wbem",
        /* the profiles of the system and of the two service accounts. */
        "Windows/System32/config/systemprofile/AppData/Local/Temp",
        "Windows/System32/config/systemprofile/AppData/Roaming",
        "Windows/ServiceProfiles/LocalService/AppData/Local/Temp", "Windows/ServiceProfiles/LocalService/AppData/Roaming",
        "Windows/ServiceProfiles/NetworkService/AppData/Local/Temp",
        "Windows/ServiceProfiles/NetworkService/AppData/Roaming",
        /* the keys of the machine scope of the data protection api, which a program of any account reads and makes. */
        "Windows/System32/Microsoft/Protect/S-1-5-18/User",
        /* the color directory, whose srgb profile comes from the ROM and where programs install theirs. */
        "Windows/System32/spool/drivers/color",
        "Windows/Media", "Windows/SystemResources", "Windows/Fonts", "Windows/Temp", "Program Files",
        "Program Files (x86)",
#ifdef __aarch64__
        "Program Files (Arm)", /* arm64 windows only. */
#endif
        "ProgramData", "Users/Public",
    };
    if (!make_dirs(paths, ".", folders, MIME_ARRAY_COUNT(folders)))
        return false;
    /*
     * the folders of the features that are fetched when a program needs them, with no file in them. Windows 11 has the
     * folders whether or not a feature is on, so a program that looks at them finds them and is not asked anything.
     * guest/features/handlers.c makes them again when it removes a feature.
     */
    static const char *const feature_folders[] = {
        "Windows/System32/WindowsPowerShell/v1.0/Modules", "Windows/System32/WindowsPowerShell/v1.0/Examples",
        "Windows/System32/WindowsPowerShell/v1.0/Schemas", "Windows/System32/WindowsPowerShell/v1.0/en-US",
        "Windows/SysWOW64/WindowsPowerShell/v1.0/Modules", "Windows/SysWOW64/WindowsPowerShell/v1.0/Examples",
        "Windows/SysWOW64/WindowsPowerShell/v1.0/Schemas", "Windows/SysWOW64/WindowsPowerShell/v1.0/en-US",
        "Windows/Microsoft.NET/Framework/v4.0.30319", "Windows/Microsoft.NET/Framework64/v4.0.30319",
        "Windows/Microsoft.NET/Assembly/GAC_32", "Windows/Microsoft.NET/Assembly/GAC_64",
        "Windows/Microsoft.NET/Assembly/GAC_MSIL", "Windows/assembly/GAC_32", "Windows/assembly/GAC_64",
        "Windows/assembly/GAC_MSIL", "Windows/assembly/NativeImages_v4.0.30319_32",
        "Windows/assembly/NativeImages_v4.0.30319_64", "Program Files (x86)/Microsoft/EdgeWebView/Application",
    };
    return make_dirs_nocase(paths, feature_folders, MIME_ARRAY_COUNT(feature_folders));
}

#ifdef __APPLE__
static void put_base64(FILE *f, const uint8_t *data, size_t size)
{
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t column = 0;
    for (size_t i = 0; i < size; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16 | (i + 1 < size ? (uint32_t)data[i + 1] << 8 : 0) |
                     (i + 2 < size ? data[i + 2] : 0);
        char out[4] = {digits[v >> 18 & 63], digits[v >> 12 & 63], i + 1 < size ? digits[v >> 6 & 63] : '=',
                       i + 2 < size ? digits[v & 63] : '='};
        fwrite(out, 1, 4, f);
        if ((column += 4) == 64) {
            fputc('\n', f);
            column = 0;
        }
    }
    if (column)
        fputc('\n', f);
}

/*
 * the roots macos trusts, for the machine's AuthRoot store. they live in the system keychain: the /etc/ssl/cert.pem
 * bundle the kernel reads otherwise lags behind it and lacks roots that sites are signed under. written at every start,
 * so the machine follows the system's updates.
 */
static void export_roots(const MimePaths *paths)
{
    CFArrayRef anchors = nullptr;
    if (SecTrustCopyAnchorCertificates(&anchors) != errSecSuccess || !anchors)
        return;
    char path[MIME_PATH_MAX], temporary[MIME_PATH_MAX];
    snprintf(path, sizeof(path), "%s/ProgramData/Mime/roots.pem", paths->drive_c);
    snprintf(temporary, sizeof(temporary), "%s.%d", path, (int)getpid());
    FILE *f = make_dir_nocase(paths->drive_c, "ProgramData/Mime") ? fopen(temporary, "w") : nullptr;
    for (CFIndex i = 0; f && i < CFArrayGetCount(anchors); i++) {
        CFDataRef der = SecCertificateCopyData((SecCertificateRef)CFArrayGetValueAtIndex(anchors, i));
        if (!der)
            continue;
        fputs("-----BEGIN CERTIFICATE-----\n", f);
        put_base64(f, CFDataGetBytePtr(der), (size_t)CFDataGetLength(der));
        fputs("-----END CERTIFICATE-----\n", f);
        CFRelease(der);
    }
    CFRelease(anchors);
    if (f && fclose(f) == 0 && rename(temporary, path) == 0)
        return;
    unlink(temporary);
}
#endif

bool mime_prepare_drive(const MimePaths *paths)
{
    if (!create_skeleton(paths))
        return false;
    /* copies written by older versions are shadowed by the ROM, remove them. */
    const MimeRomHeader *h = mime_rom_header();
    for (uint32_t i = 0; i < h->count; i++) {
        char path[MIME_PATH_MAX];
        snprintf(path, sizeof(path), "%s%s", paths->drive_c, h->entries[i].path);
        for (char *c = path; *c; c++)
            if (*c == '\\')
                *c = '/';
        unlink(path);
    }
    create_name_files(paths);
    create_shared_folders(paths);
    if (!create_hidden_items(paths))
        return false;
#ifdef __APPLE__
    export_roots(paths);
#endif
    return create_profile(paths);
}
