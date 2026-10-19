/*
 * the guest's shortcuts as entries of the host's applications menu. a program that makes a shortcut in the start menu or
 * on the desktop of the guest, such as the "Install app" of a browser, gets an entry in the host's menu that starts it
 * through `mime run`, with the picture of the shortcut as its icon, the group of windows the program's application
 * user model id gives as its window class, and the tasks of its jump list as its actions. an entry goes with the
 * shortcut.
 *
 * it writes into the user's own directory of applications, so it is off unless asked for: the line `shortcuts = true`
 * in the section [desktop] of config.ini in Mime's config directory, or MIME_DESKTOP_SHORTCUTS=1 in the environment of
 * the machine. MIME_DESKTOP_DATA names the directory that stands for the data directory of the desktop, where the
 * applications and the icons go; tests give one of their own and the user's files are never touched.
 *
 * the shortcuts are read from the host's side of the guest's drive: the start menu folders and the desktops are
 * watched with inotify, so whatever way a program made a shortcut is seen, and the entries are made again when the machine
 * starts. only the entries this made are ever changed or removed, they say so in X-Mime-Managed.
 */
#include "ui_internal.h"

#include "../icon.h"
#include "../identity.h"
#include "../lnk.h"
#include "../ui/pe_icon.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_WATCHES 512
#define MAX_SOURCES 8
#define MAX_DEPTH 4
#define SETTLE_MS 150
#define MAX_PENDING 128

typedef struct Task {
    char title[128];
    char exec[2400];
} Task;

typedef struct TaskList {
    struct TaskList *next;
    char aumid[160];
    uint32_t count;
    Task tasks[MIME_JUMPLIST_TASKS];
} TaskList;

typedef struct Entry {
    struct Entry *next;
    char id[200];
    char desktop_file[MIME_PATH_MAX];
    char icon_file[MIME_PATH_MAX];
    char sources[MAX_SOURCES][MIME_PATH_MAX];
    int source_count;
    char aumid[160];
    char name[160];
    char comment[260];
    char exec[3000];
    char path[MIME_PATH_MAX];
    char window_class[200];
} Entry;

typedef struct Watch {
    int wd;
    char path[MIME_PATH_MAX];
    int depth;
} Watch;

typedef struct Pending {
    char path[MIME_PATH_MAX];
    uint64_t due;
    int tries;
} Pending;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static const MimePaths *g_paths;
static char g_user[32];
static char g_data[MIME_PATH_MAX];  /* the data directory of the desktop */
static char g_exe[MIME_PATH_MAX];   /* the mime that starts the programs */
static int g_inotify = -1;
static Watch g_watches[MAX_WATCHES];
static int g_watch_count;
static Pending g_pending[MAX_PENDING];
static int g_pending_count;
static Entry *g_entries;
static TaskList *g_tasks;
static bool g_running;

/* ------------------------------------------------------------------ the setting */

static bool parse_bool(const char *text)
{
    return !strcasecmp(text, "true") || !strcasecmp(text, "yes") || !strcasecmp(text, "on") || !strcmp(text, "1");
}

/* the value of `key` in `[section]` of an ini file, or false. */
static bool config_value(const char *path, const char *section, const char *key, char *out, size_t size)
{
    size_t length;
    char *text = mime_read_file(path, &length);
    if (!text)
        return false;
    char *copy = mime_xmalloc(length + 1);
    memcpy(copy, text, length);
    copy[length] = 0;
    free(text);
    bool found = false, in_section = false;
    char *save = nullptr;
    for (char *line = strtok_r(copy, "\r\n", &save); line && !found; line = strtok_r(nullptr, "\r\n", &save)) {
        while (isspace((unsigned char)*line))
            line++;
        if (*line == '#' || *line == ';' || !*line)
            continue;
        if (*line == '[') {
            char *end = strchr(line, ']');
            in_section = end && (size_t)(end - line - 1) == strlen(section) && !strncasecmp(line + 1, section, strlen(section));
            continue;
        }
        char *eq = strchr(line, '=');
        if (!in_section || !eq)
            continue;
        char *name_end = eq;
        while (name_end > line && isspace((unsigned char)name_end[-1]))
            name_end--;
        if ((size_t)(name_end - line) != strlen(key) || strncasecmp(line, key, strlen(key)))
            continue;
        char *value = eq + 1;
        while (isspace((unsigned char)*value))
            value++;
        char *end = value + strlen(value);
        while (end > value && isspace((unsigned char)end[-1]))
            end--;
        *end = 0;
        mime_strlcpy(out, value, size);
        found = true;
    }
    free(copy);
    return found;
}

static bool setting_enabled(const MimePaths *paths)
{
    const char *env = getenv("MIME_DESKTOP_SHORTCUTS");
    if (env && *env)
        return parse_bool(env);
    char path[MIME_PATH_MAX], value[32];
    mime_path_join(path, sizeof(path), paths->config, "config.ini");
    return config_value(path, "desktop", "shortcuts", value, sizeof(value)) && parse_bool(value);
}

static void data_directory(char *out, size_t size)
{
    const char *env = getenv("MIME_DESKTOP_DATA");
    if (!env || env[0] != '/')
        env = getenv("XDG_DATA_HOME");
    if (env && env[0] == '/') {
        mime_strlcpy(out, env, size);
        return;
    }
    const char *home = getenv("HOME");
    snprintf(out, size, "%s/.local/share", home && *home ? home : "");
}

/* ------------------------------------------------------------------ paths of the guest */

static bool env_value(const char *name, char *out, size_t size)
{
    static const struct {
        const char *name;
        const char *value; /* the user's name is %U */
    } table[] = {{"USERPROFILE", "C:\\Users\\%U"},
                 {"HOMEPATH", "\\Users\\%U"},
                 {"HOMEDRIVE", "C:"},
                 {"APPDATA", "C:\\Users\\%U\\AppData\\Roaming"},
                 {"LOCALAPPDATA", "C:\\Users\\%U\\AppData\\Local"},
                 {"TEMP", "C:\\Users\\%U\\AppData\\Local\\Temp"},
                 {"TMP", "C:\\Users\\%U\\AppData\\Local\\Temp"},
                 {"PROGRAMDATA", "C:\\ProgramData"},
                 {"ALLUSERSPROFILE", "C:\\ProgramData"},
                 {"PUBLIC", "C:\\Users\\Public"},
                 {"SYSTEMROOT", "C:\\Windows"},
                 {"WINDIR", "C:\\Windows"},
                 {"SYSTEMDRIVE", "C:"},
                 {"PROGRAMFILES", "C:\\Program Files"},
                 {"PROGRAMFILES(X86)", "C:\\Program Files (x86)"},
                 {"PROGRAMW6432", "C:\\Program Files"},
                 {"COMMONPROGRAMFILES", "C:\\Program Files\\Common Files"}};
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcasecmp(name, table[i].name))
            continue;
        size_t n = 0;
        for (const char *p = table[i].value; *p && n + 1 < size; p++) {
            if (p[0] == '%' && p[1] == 'U') {
                n += (size_t)snprintf(out + n, size - n, "%s", g_user);
                p++;
            } else {
                out[n++] = *p;
            }
        }
        out[n < size ? n : size - 1] = 0;
        return true;
    }
    return false;
}

/* the path with the variables of the guest's environment in it filled in. */
static void expand(const char *in, char *out, size_t size)
{
    size_t n = 0;
    for (const char *p = in; *p && n + 1 < size;) {
        if (*p == '%') {
            const char *end = strchr(p + 1, '%');
            if (end && end - p - 1 < 40) {
                char name[48], value[MIME_PATH_MAX];
                memcpy(name, p + 1, (size_t)(end - p - 1));
                name[end - p - 1] = 0;
                if (env_value(name, value, sizeof(value))) {
                    n += (size_t)snprintf(out + n, size - n, "%s", value);
                    if (n >= size)
                        n = size - 1;
                    p = end + 1;
                    continue;
                }
            }
        }
        out[n++] = *p++;
    }
    out[n] = 0;
}

/* the file or folder of a guest path, found whatever the case of its names on the host's disk. */
static bool resolve_guest_path(const char *guest, char *host, size_t size)
{
    char drive = (char)toupper((unsigned char)guest[0]);
    if ((drive != 'C' && drive != 'Z') || guest[1] != ':')
        return false;
    char current[MIME_PATH_MAX];
    mime_strlcpy(current, drive == 'C' ? g_paths->drive_c : "", sizeof(current));
    char copy[MIME_PATH_MAX * 2];
    mime_strlcpy(copy, guest + 2, sizeof(copy));
    char *save = nullptr;
    for (char *p = copy; *p; p++)
        if (*p == '\\')
            *p = '/';
    for (char *component = strtok_r(copy, "/", &save); component; component = strtok_r(nullptr, "/", &save)) {
        if (!*component || !strcmp(component, ".") || !strcmp(component, ".."))
            return false;
        char next[MIME_PATH_MAX * 2];
        snprintf(next, sizeof(next), "%s/%s", current, component);
        struct stat st;
        if (stat(next, &st) != 0) {
            DIR *dir = opendir(current[0] ? current : "/");
            bool found = false;
            for (struct dirent *e; dir && (e = readdir(dir));)
                if (!strcasecmp(e->d_name, component)) {
                    snprintf(next, sizeof(next), "%s/%s", current, e->d_name);
                    found = true;
                    break;
                }
            if (dir)
                closedir(dir);
            if (!found)
                return false;
        }
        if (strlen(next) >= MIME_PATH_MAX)
            return false;
        mime_strlcpy(current, next, sizeof(current));
    }
    mime_strlcpy(host, current[0] ? current : "/", size);
    return true;
}

/* ------------------------------------------------------------------ text of an entry */

/* a name for a file: letters, digits, dots, dashes and underscores. */
static void slug(char *out, size_t size, const char *in)
{
    size_t n = 0;
    for (const char *p = in; *p && n + 1 < size; p++)
        out[n++] = isalnum((unsigned char)*p) || *p == '.' || *p == '_' || *p == '-' ? *p : '-';
    out[n] = 0;
}

static uint32_t fnv(const char *text)
{
    uint32_t h = 2166136261u;
    for (; *text; text++)
        h = (h ^ (uint8_t)*text) * 16777619u;
    return h;
}

/* a value of a desktop entry: the backslash and the line breaks are escaped. */
static void escape_value(FILE *f, const char *text)
{
    for (; *text; text++) {
        if (*text == '\\')
            fputs("\\\\", f);
        else if (*text == '\n')
            fputs("\\n", f);
        else if (*text == '\r')
            fputs("\\r", f);
        else
            fputc(*text, f);
    }
}

/* an argument of Exec: in double quotes when it needs them, with the characters the specification names escaped. */
static void append_argument(char *out, size_t size, const char *arg)
{
    size_t n = strlen(out);
    bool quote = !*arg;
    for (const char *p = arg; *p; p++)
        if (strchr(" \t\n\"'\\><~|&;$*?#()`%", *p))
            quote = true;
    if (n + 1 < size && n)
        out[n++] = ' ';
    if (quote && n + 1 < size)
        out[n++] = '"';
    for (const char *p = arg; *p && n + 8 < size; p++) {
        if (*p == '%') {
            out[n++] = '%';
        } else if (quote && strchr("\"`$\\", *p)) {
            out[n++] = '\\';
        }
        out[n++] = *p;
    }
    if (quote && n + 1 < size)
        out[n++] = '"';
    out[n] = 0;
}

/* the arguments of a command line of Windows, as CommandLineToArgvW splits them. */
static int split_command_line(const char *line, char argv[][512], int max)
{
    int argc = 0;
    const char *p = line;
    while (argc < max) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        char *out = argv[argc];
        size_t n = 0;
        bool quoted = false;
        while (*p && (quoted || (*p != ' ' && *p != '\t'))) {
            size_t slashes = 0;
            while (*p == '\\') {
                slashes++;
                p++;
            }
            if (*p == '"') {
                for (size_t i = 0; i < slashes / 2 && n < 510; i++)
                    out[n++] = '\\';
                if (slashes % 2) {
                    if (n < 510)
                        out[n++] = '"';
                } else if (quoted && p[1] == '"') {
                    if (n < 510)
                        out[n++] = '"';
                    p++;
                } else {
                    quoted = !quoted;
                }
                p++;
            } else {
                for (size_t i = 0; i < slashes && n < 510; i++)
                    out[n++] = '\\';
                if (*p && n < 510)
                    out[n++] = *p++;
                else if (*p)
                    p++;
            }
        }
        out[n] = 0;
        argc++;
    }
    return argc;
}

/* `mime run program arguments` for a program of the guest. */
static void make_exec(char *out, size_t size, const char *program_host, const char *arguments)
{
    out[0] = 0;
    append_argument(out, size, g_exe);
    append_argument(out, size, "run");
    append_argument(out, size, program_host);
    char (*argv)[512] = mime_xmalloc(32 * 512);
    int argc = split_command_line(arguments, argv, 32);
    for (int i = 0; i < argc; i++)
        append_argument(out, size, argv[i]);
    free(argv);
}

/* ------------------------------------------------------------------ the entries */

static Entry *find_entry(const char *id)
{
    for (Entry *e = g_entries; e; e = e->next)
        if (!strcmp(e->id, id))
            return e;
    return nullptr;
}

static TaskList *find_tasks(const char *aumid)
{
    for (TaskList *t = g_tasks; t; t = t->next)
        if (!strcmp(t->aumid, aumid))
            return t;
    return nullptr;
}

static void applications_directory(char *out, size_t size)
{
    snprintf(out, size, "%s/applications", g_data);
}

static void write_entry(Entry *e)
{
    char dir[MIME_PATH_MAX];
    applications_directory(dir, sizeof(dir));
    if (!mime_mkdir_p(dir, 0755))
        return;
    char temp[MIME_PATH_MAX + 16];
    snprintf(temp, sizeof(temp), "%s.tmp%d", e->desktop_file, (int)getpid());
    FILE *f = fopen(temp, "w");
    if (!f)
        return;
    fputs("[Desktop Entry]\nType=Application\nVersion=1.1\nName=", f);
    escape_value(f, e->name);
    if (e->comment[0]) {
        fputs("\nComment=", f);
        escape_value(f, e->comment);
    }
    fputs("\nExec=", f);
    escape_value(f, e->exec);
    if (e->path[0]) {
        fputs("\nPath=", f);
        escape_value(f, e->path);
    }
    if (e->icon_file[0]) {
        fputs("\nIcon=", f);
        escape_value(f, e->icon_file);
    }
    fputs("\nTerminal=false\nCategories=Utility;\n", f);
    if (e->window_class[0]) {
        fputs("StartupWMClass=", f);
        escape_value(f, e->window_class);
        fputc('\n', f);
    }
    fprintf(f, "X-Mime-Managed=true\nX-Mime-Shortcut=");
    escape_value(f, e->sources[0]);
    if (e->aumid[0]) {
        fputs("\nX-Mime-AppUserModelId=", f);
        escape_value(f, e->aumid);
    }
    fputc('\n', f);
    TaskList *tasks = e->aumid[0] ? find_tasks(e->aumid) : nullptr;
    if (tasks && tasks->count) {
        fputs("Actions=", f);
        for (uint32_t i = 0; i < tasks->count; i++)
            fprintf(f, "task%u;", i);
        fputc('\n', f);
        for (uint32_t i = 0; i < tasks->count; i++) {
            fprintf(f, "\n[Desktop Action task%u]\nName=", i);
            escape_value(f, tasks->tasks[i].title);
            fputs("\nExec=", f);
            escape_value(f, tasks->tasks[i].exec);
            fputc('\n', f);
        }
    }
    fclose(f);
    rename(temp, e->desktop_file);
}

static void drop_entry(Entry *e)
{
    unlink(e->desktop_file);
    if (e->icon_file[0])
        unlink(e->icon_file);
    for (Entry **link = &g_entries; *link; link = &(*link)->next)
        if (*link == e) {
            *link = e->next;
            break;
        }
    mime_info("desktop: removed %s", e->desktop_file);
    free(e);
}

static void remove_source(const char *lnk)
{
    for (Entry *e = g_entries; e;) {
        Entry *next = e->next;
        for (int i = 0; i < e->source_count; i++)
            if (!strcmp(e->sources[i], lnk)) {
                memmove(e->sources[i], e->sources[i + 1], (size_t)(e->source_count - i - 1) * sizeof(e->sources[0]));
                e->source_count--;
                break;
            }
        if (!e->source_count)
            drop_entry(e);
        e = next;
    }
}

/* the standard sizes of icon themes, for a picture that is not one. */
static uint32_t theme_size(uint32_t side)
{
    static const uint32_t sizes[] = {16, 22, 24, 32, 36, 48, 64, 72, 96, 128, 192, 256, 512};
    uint32_t best = sizes[0];
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        if (side >= sizes[i])
            best = sizes[i];
    return best;
}

/* writes the icon of a shortcut into the theme and gives its file, or nothing. */
static bool make_icon(const MimeLink *link, const char *program_host, const char *id, char *out, size_t size)
{
    char location[MIME_PATH_MAX], host[MIME_PATH_MAX];
    expand(link->icon_raw[0] ? link->icon_raw : link->icon, location, sizeof(location));
    bool from_program = !location[0];
    if (from_program) {
        mime_strlcpy(host, program_host, sizeof(host));
    } else if (!resolve_guest_path(location, host, sizeof(host))) {
        mime_strlcpy(host, program_host, sizeof(host));
        from_program = true;
    }
    void *ico = nullptr;
    size_t ico_size = 0;
    size_t n = strlen(host);
    bool plain = n > 4 && !strcasecmp(host + n - 4, ".ico");
    if (plain) {
        ico = mime_read_file(host, &ico_size);
    } else {
        int group = (!from_program && link->icon_index < 0) ? -link->icon_index : -1;
        if (!mime_pe_icon_read(host, group, &ico, &ico_size) && group >= 0)
            mime_pe_icon_read(host, -1, &ico, &ico_size);
    }
    if (!ico)
        return false;
    void *png = nullptr;
    size_t png_size = 0;
    uint32_t side = 0;
    bool ok = mime_ico_to_png(ico, ico_size, &png, &png_size, &side);
    free(ico);
    if (!ok)
        return false;
    uint32_t dir_size = theme_size(side);
    char dir[MIME_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/icons/hicolor/%ux%u/apps", g_data, dir_size, dir_size);
    ok = mime_mkdir_p(dir, 0755);
    if (ok) {
        snprintf(out, size, "%s/%s.png", dir, id);
        ok = mime_write_file(out, png, png_size);
    }
    free(png);
    return ok;
}

static void process_link(const char *lnk_host)
{
    size_t size;
    void *data = mime_read_file(lnk_host, &size);
    MimeLink link;
    if (!data || !mime_lnk_parse(data, size, &link)) {
        free(data);
        remove_source(lnk_host);
        return;
    }
    free(data);
    /* the program: a file of the guest below the folders programs live in, never the system's own tools. */
    char target[MIME_PATH_MAX], program_host[MIME_PATH_MAX];
    expand(link.target_raw[0] ? link.target_raw : link.target, target, sizeof(target));
    bool eligible = resolve_guest_path(target, program_host, sizeof(program_host));
    if (!eligible && link.target[0] && strcmp(link.target, target))
        eligible = resolve_guest_path(link.target, program_host, sizeof(program_host));
    struct stat st;
    size_t tn = strlen(target);
    eligible = eligible && stat(program_host, &st) == 0 && S_ISREG(st.st_mode) && tn > 4 &&
               !strcasecmp(target + tn - 4, ".exe") && strncasecmp(target, "C:\\Windows\\", 11) != 0;
    if (!eligible) {
        remove_source(lnk_host);
        return;
    }
    char name[160];
    const char *base = strrchr(lnk_host, '/');
    mime_strlcpy(name, base ? base + 1 : lnk_host, sizeof(name));
    size_t nn = strlen(name);
    if (nn > 4 && !strcasecmp(name + nn - 4, ".lnk"))
        name[nn - 4] = 0;

    char id[200], key[2300];
    if (link.aumid[0]) {
        char s[170];
        slug(s, sizeof(s), link.aumid);
        snprintf(id, sizeof(id), "mime.%s", s);
    } else {
        char s[80];
        slug(s, sizeof(s), name);
        snprintf(key, sizeof(key), "%s|%s", program_host, link.arguments);
        snprintf(id, sizeof(id), "mime-%s-%08x", s, fnv(key));
    }
    /* a shortcut that changed identity leaves the entry it had. */
    for (Entry *e = g_entries; e;) {
        Entry *next = e->next;
        if (strcmp(e->id, id))
            for (int i = 0; i < e->source_count; i++)
                if (!strcmp(e->sources[i], lnk_host)) {
                    memmove(e->sources[i], e->sources[i + 1], (size_t)(e->source_count - i - 1) * sizeof(e->sources[0]));
                    e->source_count--;
                    if (!e->source_count)
                        drop_entry(e);
                    break;
                }
        e = next;
    }
    Entry *e = find_entry(id);
    bool created = !e;
    if (!e) {
        e = mime_xcalloc(1, sizeof(*e));
        mime_strlcpy(e->id, id, sizeof(e->id));
        char dir[MIME_PATH_MAX];
        applications_directory(dir, sizeof(dir));
        snprintf(e->desktop_file, sizeof(e->desktop_file), "%s/%s.desktop", dir, id);
        e->next = g_entries;
        g_entries = e;
    }
    bool known = false;
    for (int i = 0; i < e->source_count; i++)
        known |= !strcmp(e->sources[i], lnk_host);
    if (!known && e->source_count < MAX_SOURCES)
        mime_strlcpy(e->sources[e->source_count++], lnk_host, MIME_PATH_MAX);
    mime_strlcpy(e->aumid, link.aumid, sizeof(e->aumid));
    mime_strlcpy(e->name, name, sizeof(e->name));
    mime_strlcpy(e->comment, link.description, sizeof(e->comment));
    make_exec(e->exec, sizeof(e->exec), program_host, link.arguments);
    e->path[0] = 0;
    if (link.directory[0]) {
        char directory[MIME_PATH_MAX];
        expand(link.directory, directory, sizeof(directory));
        if (!resolve_guest_path(directory, e->path, sizeof(e->path)))
            e->path[0] = 0;
    }
    /* windows of the program have the class of the identity it gave, the id of the entry. */
    if (link.aumid[0])
        mime_strlcpy(e->window_class, id, sizeof(e->window_class));
    else
        e->window_class[0] = 0;
    e->icon_file[0] = 0;
    make_icon(&link, program_host, id, e->icon_file, sizeof(e->icon_file));
    write_entry(e);
    if (created)
        mime_info("desktop: %s for %s", e->desktop_file, lnk_host);
}

/* ------------------------------------------------------------------ watching */

static void add_pending(const char *path)
{
    uint64_t due = mime_now_ns() / 1000000 + SETTLE_MS;
    for (int i = 0; i < g_pending_count; i++)
        if (!strcmp(g_pending[i].path, path)) {
            g_pending[i].due = due;
            return;
        }
    if (g_pending_count < MAX_PENDING) {
        mime_strlcpy(g_pending[g_pending_count].path, path, MIME_PATH_MAX);
        g_pending[g_pending_count].due = due;
        g_pending[g_pending_count].tries = 0;
        g_pending_count++;
    }
}

static bool is_lnk(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".lnk");
}

static void watch_tree(const char *path, int depth, bool scan)
{
    if (g_watch_count < MAX_WATCHES) {
        int wd = inotify_add_watch(g_inotify, path,
                                   IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE | IN_CREATE | IN_DELETE_SELF);
        if (wd >= 0) {
            bool known = false;
            for (int i = 0; i < g_watch_count; i++)
                if (g_watches[i].wd == wd) {
                    mime_strlcpy(g_watches[i].path, path, MIME_PATH_MAX);
                    known = true;
                }
            if (!known) {
                g_watches[g_watch_count].wd = wd;
                mime_strlcpy(g_watches[g_watch_count].path, path, MIME_PATH_MAX);
                g_watches[g_watch_count].depth = depth;
                g_watch_count++;
            }
        }
    }
    DIR *dir = opendir(path);
    if (!dir)
        return;
    for (struct dirent *e; (e = readdir(dir));) {
        if (e->d_name[0] == '.')
            continue;
        char child[MIME_PATH_MAX * 2];
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        if (strlen(child) >= MIME_PATH_MAX)
            continue;
        struct stat st;
        if (stat(child, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode) && depth > 0)
            watch_tree(child, depth - 1, scan);
        else if (S_ISREG(st.st_mode) && scan && is_lnk(e->d_name))
            add_pending(child);
    }
    closedir(dir);
}

static void handle_event(const struct inotify_event *ev)
{
    const char *dir = nullptr;
    int depth = 0;
    for (int i = 0; i < g_watch_count; i++)
        if (g_watches[i].wd == ev->wd) {
            dir = g_watches[i].path;
            depth = g_watches[i].depth;
        }
    if (!dir)
        return;
    if (ev->mask & IN_DELETE_SELF) {
        size_t n = strlen(dir);
        for (Entry *e = g_entries; e;) {
            Entry *next = e->next;
            for (int i = 0; i < e->source_count;)
                if (!strncmp(e->sources[i], dir, n) && e->sources[i][n] == '/') {
                    memmove(e->sources[i], e->sources[i + 1], (size_t)(e->source_count - i - 1) * sizeof(e->sources[0]));
                    e->source_count--;
                } else {
                    i++;
                }
            if (!e->source_count)
                drop_entry(e);
            e = next;
        }
        return;
    }
    if (!ev->len)
        return;
    char path[MIME_PATH_MAX * 2];
    snprintf(path, sizeof(path), "%s/%s", dir, ev->name);
    if (strlen(path) >= MIME_PATH_MAX)
        return;
    if (ev->mask & IN_ISDIR) {
        if ((ev->mask & (IN_CREATE | IN_MOVED_TO)) && depth > 0)
            watch_tree(path, depth - 1, true);
        return;
    }
    if (!is_lnk(ev->name))
        return;
    if (ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO))
        add_pending(path);
    else if (ev->mask & (IN_DELETE | IN_MOVED_FROM))
        remove_source(path);
}

/* the shortcuts that settled: a shortcut is read some time after the write, for a program may still be writing it. */
static void run_pending(void)
{
    uint64_t now = mime_now_ns() / 1000000;
    for (int i = 0; i < g_pending_count;) {
        if (g_pending[i].due > now) {
            i++;
            continue;
        }
        char path[MIME_PATH_MAX];
        mime_strlcpy(path, g_pending[i].path, sizeof(path));
        memmove(&g_pending[i], &g_pending[i + 1], (size_t)(g_pending_count - i - 1) * sizeof(g_pending[0]));
        g_pending_count--;
        process_link(path);
    }
}

/* the entries this made that no shortcut has any more are removed. */
static void reconcile(void)
{
    char dir[MIME_PATH_MAX];
    applications_directory(dir, sizeof(dir));
    DIR *d = opendir(dir);
    if (!d)
        return;
    for (struct dirent *e; (e = readdir(d));) {
        size_t n = strlen(e->d_name);
        if (n < 9 || strcmp(e->d_name + n - 8, ".desktop"))
            continue;
        char path[MIME_PATH_MAX * 2];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        size_t length;
        char *text = mime_read_file(path, &length);
        bool managed = text && strstr(text, "\nX-Mime-Managed=true\n") != nullptr;
        free(text);
        if (!managed)
            continue;
        char id[200];
        mime_strlcpy(id, e->d_name, sizeof(id));
        id[strlen(id) - 8] = 0;
        if (!find_entry(id)) {
            char icon[MIME_PATH_MAX * 2];
            unlink(path);
            /* its icons, in each size of the theme. */
            static const uint32_t sizes[] = {16, 22, 24, 32, 36, 48, 64, 72, 96, 128, 192, 256, 512};
            for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
                snprintf(icon, sizeof(icon), "%s/icons/hicolor/%ux%u/apps/%s.png", g_data, sizes[i], sizes[i], id);
                unlink(icon);
            }
            mime_info("desktop: removed %s, its shortcut is gone", path);
        }
    }
    closedir(d);
}

static void *watch_main(void *arg)
{
    mime_set_thread_name("mime-desktop");
    char user_profile[MIME_PATH_MAX];
    const char *roots_guest[] = {"C:\\Users\\%U\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs",
                                 "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs", "C:\\Users\\%U\\Desktop",
                                 "C:\\Users\\Public\\Desktop"};
    pthread_mutex_lock(&g_lock);
    for (size_t i = 0; i < sizeof(roots_guest) / sizeof(roots_guest[0]); i++) {
        expand(roots_guest[i], user_profile, sizeof(user_profile));
        char host[MIME_PATH_MAX];
        if (resolve_guest_path(user_profile, host, sizeof(host)))
            watch_tree(host, MAX_DEPTH, true);
    }
    pthread_mutex_unlock(&g_lock);
    /* the first pass reads every shortcut at once. */
    for (int i = 0; i < g_pending_count; i++)
        g_pending[i].due = 0;
    pthread_mutex_lock(&g_lock);
    run_pending();
    reconcile();
    pthread_mutex_unlock(&g_lock);
    mime_info("desktop: shortcuts of the guest are entries of %s/applications", g_data);
    char buffer[8192] __attribute__((aligned(__alignof__(struct inotify_event))));
    for (;;) {
        int timeout = -1;
        pthread_mutex_lock(&g_lock);
        if (g_pending_count) {
            uint64_t now = mime_now_ns() / 1000000, due = UINT64_MAX;
            for (int i = 0; i < g_pending_count; i++)
                if (g_pending[i].due < due)
                    due = g_pending[i].due;
            timeout = due > now ? (int)(due - now) : 0;
        }
        pthread_mutex_unlock(&g_lock);
        struct pollfd pfd = {.fd = g_inotify, .events = POLLIN};
        int r = poll(&pfd, 1, timeout);
        pthread_mutex_lock(&g_lock);
        if (r > 0) {
            ssize_t got = read(g_inotify, buffer, sizeof(buffer));
            for (ssize_t at = 0; got > 0 && at < got;) {
                const struct inotify_event *ev = (const struct inotify_event *)(buffer + at);
                handle_event(ev);
                at += (ssize_t)sizeof(*ev) + ev->len;
            }
        }
        run_pending();
        pthread_mutex_unlock(&g_lock);
    }
    return nullptr;
}

/* ------------------------------------------------------------------ the jump lists */

/* the tasks of a program's jump list are the actions of its entry, when it has one. */
void mime_desktop_jump_list(const MimeShellJumpList *list)
{
    if (!g_running)
        return;
    char aumid[160];
    mime_utf16_to_utf8(aumid, sizeof(aumid), list->aumid, 128);
    if (!aumid[0])
        return;
    pthread_mutex_lock(&g_lock);
    TaskList *t = find_tasks(aumid);
    if (!t) {
        t = mime_xcalloc(1, sizeof(*t));
        mime_strlcpy(t->aumid, aumid, sizeof(t->aumid));
        t->next = g_tasks;
        g_tasks = t;
    }
    t->count = 0;
    uint32_t count = list->task_count < MIME_JUMPLIST_TASKS ? list->task_count : MIME_JUMPLIST_TASKS;
    for (uint32_t i = 0; i < count; i++) {
        const MimeJumpTask *task = &list->tasks[i];
        char title[128], path[MIME_PATH_MAX], guest[MIME_PATH_MAX], arguments[1100], host[MIME_PATH_MAX];
        mime_utf16_to_utf8(title, sizeof(title), task->title, 64);
        mime_utf16_to_utf8(guest, sizeof(guest), task->path, 260);
        mime_utf16_to_utf8(arguments, sizeof(arguments), task->arguments, 260);
        expand(guest, path, sizeof(path));
        if (!title[0] || !resolve_guest_path(path, host, sizeof(host)))
            continue;
        Task *out = &t->tasks[t->count++];
        mime_strlcpy(out->title, title, sizeof(out->title));
        make_exec(out->exec, sizeof(out->exec), host, arguments);
    }
    for (Entry *e = g_entries; e; e = e->next)
        if (!strcmp(e->aumid, aumid))
            write_entry(e);
    pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------------ start */

void mime_ui_desktop_start(const MimePaths *paths)
{
    if (!setting_enabled(paths))
        return;
    g_paths = paths;
    MimeIdentity id;
    if (!mime_identity_load(paths, &id))
        return;
    mime_strlcpy(g_user, id.user_name, sizeof(g_user));
    data_directory(g_data, sizeof(g_data));
    ssize_t n = readlink("/proc/self/exe", g_exe, sizeof(g_exe) - 1);
    if (n <= 0)
        return;
    g_exe[n] = 0;
    g_inotify = inotify_init1(IN_CLOEXEC);
    if (g_inotify < 0)
        return;
    pthread_t thread;
    if (pthread_create(&thread, nullptr, watch_main, nullptr) != 0)
        return;
    pthread_detach(thread);
    g_running = true;
}
