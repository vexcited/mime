/*
 * imports fonts, themes, icons and localized strings from a user-supplied Windows installation ISO and
 * creates the guest C: drive.
 */
#include "../mime_host.h"
#include "../mime_cli.h"
#include "udf.h"
#include "../identity.h"
#include "../locale.h"
#include "wim.h"

#include <errno.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct ExtractRule {
    const char *source;      /* path inside the image. */
    const char *destination; /* path relative to drive_c. */
    bool optional;
} ExtractRule;

/* paths below the source of a rule that are left out. */
typedef struct SkipRule {
    const char *source;
    const char *const *patterns; /* shell patterns, null terminated. */
} SkipRule;

/* the Edge WebView2 runtime without the Copilot app, the sign in helpers and the update resiliency copies. */
static const char *const g_webview_skip[] = {
    "*/*/ResiliencyLinks",
    "*/*/undocked_copilot",
    "*/*/VisualElements",
    "*/*/identity_proxy",
    "*/*/edge_game_assist",
    "*/*/edge_feedback",
    "*/*/BHO",
    "*/*/Installer/copilot_setup.exe",
    "*/*/Copilot.dat",
    "*/*/copilotapp.exe",
    "*/*/mscopilot.exe",
    "*/*/copilot_overlay_*.pak",
    nullptr,
};

static const SkipRule g_skips[] = {
    {"Program Files (x86)/Microsoft/EdgeWebView", g_webview_skip},
};

/*
 * system code is reimplemented; data comes from the user's Windows copy. the icons, cursors, strings and message
 * tables of the system libraries are Mime's own and are not taken from it.
 */
static const ExtractRule g_rules[] = {
    {"Windows/Fonts", "Windows/Fonts", false},
    {"Windows/Resources/Themes/aero", "Windows/Resources/Themes/aero", false},
    /* the in-box .NET Framework 4.8.x, 64-bit and 32-bit, its assembly caches and its pregenerated native images. */
    {"Windows/Microsoft.NET", "Windows/Microsoft.NET", true},
    {"Windows/assembly", "Windows/assembly", true},
#define NETFX_RUNTIME(name)                                                                                  \
    {"Windows/System32/" name, "Windows/System32/" name, true},                                              \
    {"Windows/SysWOW64/" name, "Windows/SysWOW64/" name, true}
    NETFX_RUNTIME("clrhost.dll"),
    NETFX_RUNTIME("mscorier.dll"),
    NETFX_RUNTIME("mscories.dll"),
    NETFX_RUNTIME("netfxperf.dll"),
    NETFX_RUNTIME("msvcp120_clr0400.dll"),
    NETFX_RUNTIME("msvcp140_clr0400.dll"),
    NETFX_RUNTIME("msvcr100_clr0400.dll"),
    NETFX_RUNTIME("msvcr120_clr0400.dll"),
    NETFX_RUNTIME("ucrtbase_clr0400.dll"),
    NETFX_RUNTIME("vcruntime140_clr0400.dll"),
    NETFX_RUNTIME("vcruntime140_1_clr0400.dll"),
#undef NETFX_RUNTIME
    /* the in-box Edge WebView2 runtime, which the registry of the image declares installed. */
    {"Program Files (x86)/Microsoft/EdgeWebView", "Program Files (x86)/Microsoft/EdgeWebView", true},
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

typedef struct PendingFile {
    const MimeWimDentry *dentry;
    char *host_path;
    uint64_t order;
} PendingFile;

typedef struct PendingList {
    PendingFile *items;
    size_t count, capacity;
    uint64_t kept; /* files left alone because the drive already has them. */
    bool keep_existing;
} PendingList;

static const char *const *skip_patterns(const char *source)
{
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_skips); i++) {
        if (strcmp(g_skips[i].source, source) == 0)
            return g_skips[i].patterns;
    }
    return nullptr;
}

static bool is_skipped(const char *const *skip, const char *relative)
{
    for (; skip && *skip; skip++) {
        if (fnmatch(*skip, relative, FNM_PATHNAME | FNM_CASEFOLD) == 0)
            return true;
    }
    return false;
}

/* creates directories immediately and queues file extraction. */
static bool collect_tree(MimeWim *wim, const MimeWimDentry *d, const char *host_path, const char *relative,
                         const char *const *skip, PendingList *list)
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
            char child_relative[MIME_PATH_MAX] = "";
            if (skip) {
                snprintf(child_relative, sizeof(child_relative), "%s%s%s", relative, relative[0] ? "/" : "", c->name);
                if (is_skipped(skip, child_relative))
                    continue;
            }
            mime_path_join(child, sizeof(child), host_path, c->name);
            if (!collect_tree(wim, c, child, child_relative, skip, list))
                return false;
        }
        return true;
    }
    struct stat existing;
    if (list->keep_existing && lstat(host_path, &existing) == 0) {
        list->kept++;
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

/* equal contents end up next to each other. */
static int compare_pending(const void *a, const void *b)
{
    const PendingFile *x = a, *y = b;
    if (x->order != y->order)
        return x->order < y->order ? -1 : 1;
    return memcmp(x->dentry->hash, y->dentry->hash, sizeof(x->dentry->hash));
}

static bool has_contents(const MimeWimDentry *d)
{
    static const uint8_t empty[sizeof(d->hash)];
    return memcmp(d->hash, empty, sizeof(empty)) != 0;
}

/*
 * extracts in WIM order so a compressed chunk is read once for all its files. Windows keeps one copy of a
 * file under several names as hard links, so the later names of a content become links to the first one.
 */
static bool extract_pending(MimeWim *wim, PendingList *list, ExtractStats *stats)
{
    qsort(list->items, list->count, sizeof(PendingFile), compare_pending);
    const PendingFile *first = nullptr;
    for (size_t i = 0; i < list->count; i++) {
        const PendingFile *f = &list->items[i];
        bool linked = false;
        if (first && has_contents(f->dentry) && memcmp(first->dentry->hash, f->dentry->hash, sizeof(f->dentry->hash)) == 0) {
            unlink(f->host_path);
            linked = link(first->host_path, f->host_path) == 0;
        } else {
            first = f;
        }
        if (!linked && !mime_wim_extract_file(wim, f->dentry, f->host_path)) {
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
    /* drag and drop from Finder quotes paths or escapes spaces. */
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
    printf("mime needs a Windows 11 installation ISO to import fonts, the theme, system resources, the .NET Framework and the WebView2 runtime.\n");
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
    bool overwrite = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--overwrite") == 0) {
            overwrite = true;
        } else if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
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
    /* changing only the language updates the existing installation. */
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
    bool already_set_up = mime_is_setup(paths);
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

    /*
     * on a drive that is already set up only the missing files are added, so the user
     * profiles and everything the programs wrote stay as they are.
     */
    bool add_only = already_set_up && !overwrite;
    ExtractStats stats = {};
    PendingList pending = {.keep_existing = add_only};
    const MimeWimDentry *sources[MIME_ARRAY_COUNT(g_rules)];
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_rules); i++) {
        sources[i] = mime_wim_lookup(wim, g_rules[i].source);
        if (!sources[i] && !g_rules[i].optional) {
            mime_error("%s not found in the image", g_rules[i].source);
            goto fail;
        }
    }
    if (add_only)
        printf("The drive is already set up, adding the files it lacks and keeping the others.\n");
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_rules); i++) {
        if (!sources[i])
            continue;
        char dst[MIME_PATH_MAX];
        mime_path_join(dst, sizeof(dst), paths->drive_c, g_rules[i].destination);
        if (!collect_tree(wim, sources[i], dst, "", skip_patterns(g_rules[i].source), &pending)) {
            free_pending(&pending);
            goto fail;
        }
    }
    for (size_t i = 0; i < pending.count; i++)
        stats.total_bytes += mime_wim_blob_size(wim, pending.items[i].dentry->hash);
    uint64_t kept = pending.kept;
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
    if (add_only) {
        printf("Added %llu files, %llu were already there. C: drive is at %s\n", (unsigned long long)stats.files,
               (unsigned long long)kept, paths->drive_c);
    } else {
        write_stamp(paths, iso, &info);
        printf("Setup complete. C: drive is at %s\n", paths->drive_c);
        printf("Start the machine with `mime up`, then `mime run <program.exe>`.\n");
    }

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
