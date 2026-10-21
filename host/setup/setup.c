/*
 * mime setup: imports the windows assets mime relies on (fonts, theme,
 * icons, localized strings) from a user supplied installation iso, and
 * lays out the C: drive of the machine.
 */
#include "../mime_host.h"
#include "../mime_cli.h"
#include "udf.h"
#include "../identity.h"
#include "../locale.h"
#include "wim.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct ExtractRule {
    const char *source;      /* path inside the image */
    const char *destination; /* path relative to drive_c */
    bool optional;
} ExtractRule;

/*
 * mime reimplements the code of every system module, but reuses data
 * that only the user's own windows copy can provide. real system binaries
 * that carry resources are stored as SystemResources/<name>.mun, the
 * windows mechanism for resource-only companions of system modules.
 */
static const ExtractRule g_rules[] = {
    {"Windows/Fonts", "Windows/Fonts", false},
    {"Windows/Resources/Themes/aero", "Windows/Resources/Themes/aero", false},
    {"Windows/System32/user32.dll", "Windows/SystemResources/user32.dll.mun", false},
    {"Windows/System32/en-US/user32.dll.mui", "Windows/System32/en-US/user32.dll.mui", false},
    {"Windows/SystemResources/imageres.dll.mun", "Windows/SystemResources/imageres.dll.mun", true},
    {"Windows/System32/en-US/kernelbase.dll.mui", "Windows/System32/en-US/kernelbase.dll.mui", true},
    {"Windows/System32/en-US/kernel32.dll.mui", "Windows/System32/en-US/kernel32.dll.mui", true},
    {"Windows/Media", "Windows/Media", true},
    /* the sort order of windows, which only the tests that compare Mime with it read. */
    {"Windows/Globalization/Sorting/SortDefault.nls", "Windows/Globalization/Sorting/SortDefault.nls", true},
    /* the sound set of the gs wavetable synth. */
    {"Windows/System32/drivers/gm.dls", "Windows/System32/drivers/gm.dls", true},
    /* the registry hives of a fresh install, which the kernel loads and specializes at boot. */
    {"Windows/System32/config/SYSTEM", "Windows/System32/config/SYSTEM", false},
    {"Windows/System32/config/SOFTWARE", "Windows/System32/config/SOFTWARE", false},
    {"Windows/System32/config/DEFAULT", "Windows/System32/config/DEFAULT", false},
    /* the template of new user profiles, with its NTUSER.DAT hive. */
    {"Users/Default", "Users/Default", false},
};

static const char *const g_skeleton[] = {
    "Windows/System32/en-US",
    "Windows/Globalization/Sorting",
    "Windows/SystemResources",
    "Windows/Fonts",
    "Windows/Temp",
    "Program Files",
    "Program Files (x86)",
#ifdef __aarch64__
    "Program Files (Arm)", /* arm64 windows only */
#endif
    "ProgramData",
    "Users/Public",
    "Windows/System32/config",
    "Windows/System32/drivers",
};

typedef struct UdfReaderCtx {
    MimeUdf *udf;
    MimeUdfFile file;
} UdfReaderCtx;

static bool udf_read_at(void *ctx, uint64_t offset, void *buf, size_t len)
{
    UdfReaderCtx *c = ctx;
    return mime_udf_read(c->udf, &c->file, offset, buf, len);
}

typedef struct ExtractStats {
    uint64_t files;
    uint64_t bytes;
    uint64_t total_bytes;
    uint64_t last_report;
} ExtractStats;

static void report_progress(ExtractStats *stats, bool force)
{
    uint64_t now = mime_now_ns();
    if (!force && now - stats->last_report < 100000000ull)
        return;
    stats->last_report = now;
    if (!isatty(STDOUT_FILENO))
        return;
    double pct = stats->total_bytes ? 100.0 * (double)stats->bytes / (double)stats->total_bytes : 100.0;
    printf("\r  extracting: %llu files, %.1f / %.1f MiB (%.0f%%)", (unsigned long long)stats->files,
           (double)stats->bytes / 1048576.0, (double)stats->total_bytes / 1048576.0, pct);
    fflush(stdout);
}

static uint64_t measure(MimeWim *wim, const MimeWimDentry *d)
{
    if (!(d->attributes & MIME_FILE_ATTRIBUTE_DIRECTORY))
        return mime_wim_blob_size(wim, d->hash);
    uint64_t total = 0;
    for (const MimeWimDentry *c = d->first_child; c; c = c->next_sibling)
        total += measure(wim, c);
    return total;
}

typedef struct PendingFile {
    const MimeWimDentry *dentry;
    char *host_path;
    uint64_t order;
} PendingFile;

typedef struct PendingList {
    PendingFile *items;
    size_t count, capacity;
} PendingList;

/* creates the directories right away and queues the files. */
static bool collect_tree(MimeWim *wim, const MimeWimDentry *d, const char *host_path, PendingList *list)
{
    if (d->attributes & MIME_FILE_ATTRIBUTE_REPARSE_POINT)
        return true;
    if (d->attributes & MIME_FILE_ATTRIBUTE_DIRECTORY) {
        if (!mime_mkdir_p(host_path, 0755)) {
            mime_error("cannot create %s: %s", host_path, strerror(errno));
            return false;
        }
        for (const MimeWimDentry *c = d->first_child; c; c = c->next_sibling) {
            char child[MIME_PATH_MAX];
            mime_path_join(child, sizeof(child), host_path, c->name);
            if (!collect_tree(wim, c, child, list))
                return false;
        }
        return true;
    }
    char parent[MIME_PATH_MAX];
    mime_strlcpy(parent, host_path, sizeof(parent));
    char *slash = strrchr(parent, '/');
    if (slash) {
        *slash = 0;
        mime_mkdir_p(parent, 0755);
    }
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 256;
        list->items = mime_xrealloc(list->items, list->capacity * sizeof(PendingFile));
    }
    PendingFile *f = &list->items[list->count++];
    f->dentry = d;
    f->host_path = mime_xstrdup(host_path);
    f->order = mime_wim_blob_order(wim, d->hash);
    return true;
}

static int compare_pending(const void *a, const void *b)
{
    const PendingFile *x = a, *y = b;
    return x->order < y->order ? -1 : x->order > y->order;
}

/* extracts in wim order so a compressed chunk is read once for all its files. */
static bool extract_pending(MimeWim *wim, PendingList *list, ExtractStats *stats)
{
    qsort(list->items, list->count, sizeof(PendingFile), compare_pending);
    for (size_t i = 0; i < list->count; i++) {
        const PendingFile *f = &list->items[i];
        if (!mime_wim_extract_file(wim, f->dentry, f->host_path)) {
            mime_error("failed to extract %s", f->host_path);
            return false;
        }
        stats->files++;
        stats->bytes += mime_wim_blob_size(wim, f->dentry->hash);
        report_progress(stats, false);
    }
    return true;
}

static void free_pending(PendingList *list)
{
    for (size_t i = 0; i < list->count; i++)
        free(list->items[i].host_path);
    free(list->items);
}

static void trim(char *s)
{
    size_t len = strlen(s);
    while (len && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t'))
        s[--len] = 0;
    size_t start = 0;
    while (s[start] == ' ' || s[start] == '\t')
        start++;
    if (start)
        memmove(s, s + start, len - start + 1);
    /* drag and drop from finder quotes paths or escapes spaces. */
    len = strlen(s);
    if (len >= 2 && (s[0] == '\'' || s[0] == '"') && s[len - 1] == s[0]) {
        memmove(s, s + 1, len - 2);
        s[len - 2] = 0;
    }
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '\\' && r[1])
            r++;
        *w++ = *r;
    }
    *w = 0;
}

static bool prompt_iso(char *out, size_t size)
{
    printf("mime needs a Windows 11 installation ISO to import fonts, the theme and system resources.\n");
    printf("Path to the ISO: ");
    fflush(stdout);
    if (!fgets(out, (int)size, stdin))
        return false;
    trim(out);
    if (out[0] == '~' && out[1] == '/') {
        char tmp[MIME_PATH_MAX];
        const char *home = getenv("HOME");
        snprintf(tmp, sizeof(tmp), "%s%s", home ? home : "", out + 1);
        mime_strlcpy(out, tmp, size);
    }
    return out[0] != 0;
}

static uint32_t pick_image(MimeWim *wim, uint32_t requested)
{
    uint32_t count = mime_wim_image_count(wim);
    if (requested)
        return requested <= count ? requested : 0;
    uint32_t pick = 1;
    for (uint32_t i = 1; i <= count; i++) {
        MimeWimImageInfo info;
        if (mime_wim_image_info(wim, i, &info) && strcasecmp(info.edition, "Professional") == 0) {
            pick = i;
            break;
        }
    }
    return pick;
}

static void write_stamp(const MimePaths *paths, const char *iso, const MimeWimImageInfo *info)
{
    char buf[2048];
    int n = snprintf(buf, sizeof(buf),
                     "[setup]\nversion=%s\niso=%s\nimage=%s\nedition=%s\narch=%s\nbuild=%u\n",
                     MIME_VERSION_STRING, iso, info->name, info->edition, info->arch, info->build);
    mime_write_file(paths->setup_stamp, buf, (size_t)n);
}

int mime_cmd_setup(const MimePaths *paths, int argc, char **argv)
{
    char iso[MIME_PATH_MAX] = {};
    uint32_t image_index = 0;
    const char *locale_name = nullptr;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
            image_index = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--locale") == 0 && i + 1 < argc) {
            locale_name = argv[++i];
        } else if (argv[i][0] == '-') {
            mime_error("unknown option %s", argv[i]);
            return 2;
        } else {
            mime_strlcpy(iso, argv[i], sizeof(iso));
        }
    }
    MimeLocale locale;
    if (locale_name && !mime_locale_find(locale_name, &locale)) {
        mime_error("%s is not a locale of the system", locale_name);
        return 2;
    }
    /* the language alone changes the installation that is already there. */
    if (locale_name && !iso[0] && mime_is_setup(paths)) {
        mime_identity_set_locale(paths, locale_name);
        printf("The locale of the user is %s.\n", locale.name);
        return 0;
    }
    if (!iso[0] && !prompt_iso(iso, sizeof(iso))) {
        mime_error("no iso given");
        return 2;
    }
    if (!mime_file_exists(iso)) {
        mime_error("%s does not exist", iso);
        return 1;
    }
    if (!mime_paths_ensure(paths))
        return 1;
    if (locale_name)
        mime_identity_set_locale(paths, locale_name);

    MimeUdf *udf = mime_udf_open(iso);
    if (!udf)
        return 1;
    UdfReaderCtx ctx = {.udf = udf};
    if (!mime_udf_lookup(udf, "sources/install.wim", &ctx.file) &&
        !mime_udf_lookup(udf, "sources/install.esd", &ctx.file)) {
        mime_error("neither sources/install.wim nor sources/install.esd found in %s", iso);
        mime_udf_close(udf);
        return 1;
    }
    MimeReader reader = {.ctx = &ctx, .read_at = udf_read_at, .size = ctx.file.size};
    MimeWim *wim = mime_wim_open(reader);
    if (!wim) {
        mime_udf_file_free(&ctx.file);
        mime_udf_close(udf);
        return 1;
    }

    uint32_t index = pick_image(wim, image_index);
    MimeWimImageInfo info;
    if (!index || !mime_wim_image_info(wim, index, &info)) {
        mime_error("invalid image index");
        goto fail;
    }
    printf("Using image %u: %s (%s, build %u)\n", index, info.name, info.arch, info.build);
    printf("  reading image metadata...\n");
    if (!mime_wim_select_image(wim, index)) {
        mime_error("cannot read image metadata");
        goto fail;
    }

    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_skeleton); i++) {
        char dir[MIME_PATH_MAX];
        mime_path_join(dir, sizeof(dir), paths->drive_c, g_skeleton[i]);
        if (!mime_mkdir_p(dir, 0755)) {
            mime_error("cannot create %s: %s", dir, strerror(errno));
            goto fail;
        }
    }

    ExtractStats stats = {};
    PendingList pending = {};
    const MimeWimDentry *sources[MIME_ARRAY_COUNT(g_rules)];
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_rules); i++) {
        sources[i] = mime_wim_lookup(wim, g_rules[i].source);
        if (!sources[i] && !g_rules[i].optional) {
            mime_error("%s not found in the image", g_rules[i].source);
            goto fail;
        }
        if (sources[i])
            stats.total_bytes += measure(wim, sources[i]);
    }
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_rules); i++) {
        if (!sources[i])
            continue;
        char dst[MIME_PATH_MAX];
        mime_path_join(dst, sizeof(dst), paths->drive_c, g_rules[i].destination);
        if (!collect_tree(wim, sources[i], dst, &pending)) {
            free_pending(&pending);
            goto fail;
        }
    }
    bool extracted = extract_pending(wim, &pending, &stats);
    free_pending(&pending);
    if (!extracted) {
        if (isatty(STDOUT_FILENO))
            printf("\n");
        goto fail;
    }
    report_progress(&stats, true);
    if (isatty(STDOUT_FILENO))
        printf("\n");

    if (!mime_prepare_drive(paths))
        goto fail;
    write_stamp(paths, iso, &info);
    printf("Setup complete. C: drive is at %s\n", paths->drive_c);
    printf("Start the machine with `mime up`, then `mime run <program.exe>`.\n");

    mime_wim_close(wim);
    mime_udf_file_free(&ctx.file);
    mime_udf_close(udf);
    return 0;

fail:
    mime_wim_close(wim);
    mime_udf_file_free(&ctx.file);
    mime_udf_close(udf);
    return 1;
}

bool mime_is_setup(const MimePaths *paths)
{
    return mime_file_exists(paths->setup_stamp);
}
