/*
 * creates the C: drive profile before boot. system files remain embedded in Mime and mapped through rom.h; they are
 * never written to the drive.
 */
#include "identity.h"
#include "mime_cli.h"
#include "rom.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
    return make_dirs(paths, root, folders, MIME_ARRAY_COUNT(folders));
}

/* the profile of the user, and the default one new profiles start from. its NTUSER.DAT is in the ROM. */
static bool create_profile(const MimePaths *paths)
{
    MimeIdentity id;
    mime_identity_load(paths, &id);
    return make_profile(paths, "Default") && make_profile(paths, id.user_name);
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
        "Windows/System32/config", "Windows/System32/drivers", "Windows/System32/en-US", "Windows/SysWOW64",
        "Windows/Globalization/Sorting", "Windows/SystemResources", "Windows/Fonts", "Windows/Temp", "Program Files",
        "Program Files (x86)",
#ifdef __aarch64__
        "Program Files (Arm)", /* arm64 windows only. */
#endif
        "ProgramData", "Users/Public",
    };
    return make_dirs(paths, ".", folders, MIME_ARRAY_COUNT(folders));
}

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
    return create_profile(paths);
}
