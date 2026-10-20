/*
 * prepares the C: drive before a boot. the system files themselves are
 * never written to it: the machine maps them from the mime binary, see
 * rom.h. this creates the user's profile, as windows does at first logon.
 */
#include "identity.h"
#include "mime_cli.h"
#include "rom.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static bool copy_tree(const char *src, const char *dst)
{
    DIR *dir = opendir(src);
    if (!dir)
        return false;
    bool ok = mime_mkdir_p(dst, 0755);
    struct dirent *e;
    while (ok && (e = readdir(dir))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char s[MIME_PATH_MAX], d[MIME_PATH_MAX];
        mime_path_join(s, sizeof(s), src, e->d_name);
        mime_path_join(d, sizeof(d), dst, e->d_name);
        ok = mime_dir_exists(s) ? copy_tree(s, d) : mime_copy_file(s, d);
    }
    closedir(dir);
    return ok;
}

/* new profiles start as a copy of the default one, NTUSER.DAT included. */
static bool create_profile(const MimePaths *paths)
{
    MimeIdentity id;
    mime_identity_load(paths, &id);
    char profile[MIME_PATH_MAX], template[MIME_PATH_MAX];
    snprintf(profile, sizeof(profile), "%s/Users/%s", paths->drive_c, id.user_name);
    if (mime_dir_exists(profile))
        return true;
    snprintf(template, sizeof(template), "%s/Users/Default", paths->drive_c);
    if (!mime_dir_exists(template)) {
        mime_error("the default user profile is missing, run `mime setup` again");
        return false;
    }
    if (!copy_tree(template, profile)) {
        mime_error("cannot create the profile %s: %s", profile, strerror(errno));
        return false;
    }
    static const char *const folders[] = {"Desktop",          "Documents",         "Downloads", "Music",
                                          "Pictures",         "Videos",            "Favorites", "Links",
                                          "AppData/Local/Temp", "AppData/LocalLow", "AppData/Roaming"};
    for (size_t i = 0; i < MIME_ARRAY_COUNT(folders); i++) {
        char dir[MIME_PATH_MAX];
        mime_path_join(dir, sizeof(dir), profile, folders[i]);
        mime_mkdir_p(dir, 0755);
    }
    return true;
}

/*
 * the folder of the name files, which every installation has: programs read the hosts file for the names it gives
 * and some watch its folder for changes, as a browser does to keep its resolver's configuration current, and one that
 * cannot find the folder has no resolver of its own. the file made is empty of names, as the one of a new Windows is.
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
 * the folders of the shared profile and the start menu of every user, which an installation has: a program that makes
 * a shortcut asks for the folders of the machine as well as the user's, and one that cannot find them gives the
 * shortcut up.
 */
static void create_shared_folders(const MimePaths *paths)
{
    static const char *const folders[] = {
        "Users/Public/Desktop",   "Users/Public/Documents", "Users/Public/Downloads", "Users/Public/Music",
        "Users/Public/Pictures",  "Users/Public/Videos",    "ProgramData/Microsoft/Windows/Start Menu/Programs",
        "ProgramData/Microsoft/Windows/Start Menu/Programs/Startup"};
    for (size_t i = 0; i < MIME_ARRAY_COUNT(folders); i++) {
        char dir[MIME_PATH_MAX];
        mime_path_join(dir, sizeof(dir), paths->drive_c, folders[i]);
        if (!mime_dir_exists(dir))
            mime_mkdir_p(dir, 0755);
    }
}

bool mime_prepare_drive(const MimePaths *paths)
{
    char dir[MIME_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/Windows/System32", paths->drive_c);
    if (!mime_mkdir_p(dir, 0755))
        return false;
    /* where 32-bit programs see System32, so files they make there have somewhere to go. */
    snprintf(dir, sizeof(dir), "%s/Windows/SysWOW64", paths->drive_c);
    if (!mime_mkdir_p(dir, 0755))
        return false;
    /* copies written by older versions are shadowed by the rom, remove them. */
    const MimeRomHeader *h = mime_rom_header();
    for (uint32_t i = 0; i < h->count; i++) {
        char path[MIME_PATH_MAX];
        snprintf(path, sizeof(path), "%s%s", paths->drive_c, h->entries[i].path);
        for (char *c = path; *c; c++)
            if (*c == '\\')
                *c = '/';
        unlink(path);
    }
    snprintf(dir, sizeof(dir), "%s/Windows/System32/config/SOFTWARE", paths->drive_c);
    if (!mime_file_exists(dir)) {
        mime_error("the registry hives are missing, run `mime setup` again");
        return false;
    }
    create_name_files(paths);
    create_shared_folders(paths);
    return create_profile(paths);
}
