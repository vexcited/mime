/* each guest application gets a named native dock proxy for the shared vm. */
#include "app_identity.h"
#include "pe_icon.h"
#include "../rom.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <copyfile.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define SEL_(name) sel_registerName(name)
#define CLS(name) ((id)objc_getClass(name))
#define MSG_ID(obj, sel) ((id (*)(id, SEL))objc_msgSend)((id)(obj), SEL_(sel))
#define MSG_ID_ID(obj, sel, a) ((id (*)(id, SEL, id))objc_msgSend)((id)(obj), SEL_(sel), (id)(a))
#define MSG_V(obj, sel) ((void (*)(id, SEL))objc_msgSend)((id)(obj), SEL_(sel))
#define MSG_V_ID(obj, sel, a) ((void (*)(id, SEL, id))objc_msgSend)((id)(obj), SEL_(sel), (id)(a))

#define APP_PROXY_FD 5
#define APP_LIMIT 256

typedef struct AppIdentity {
    uint32_t process_id;
    uint32_t window_count;
    uint64_t retry_after;
    bool icon_override;
    int fd;
    dispatch_source_t reader;
    char executable[MIME_PATH_MAX];
    char icon_path[MIME_PATH_MAX];
    char override_path[MIME_PATH_MAX];
} AppIdentity;

typedef struct AppWindow {
    uint32_t window_id;
    AppIdentity *app;
} AppWindow;

typedef struct AppReader {
    AppIdentity *app;
    int fd;
} AppReader;

static MimePaths g_paths;
static MimeAppActionSink g_sink;
static void *g_sink_ctx;
static bool g_initialized;
static AppIdentity g_apps[APP_LIMIT];
static AppWindow g_windows[APP_LIMIT];
static constexpr char g_proxy_magic[] = "mime app proxy 1";
static id g_proxy_app;
static char g_proxy_icon[MIME_PATH_MAX];
static char g_proxy_override[MIME_PATH_MAX];
static char g_proxy_fallback[MIME_PATH_MAX];
static bool g_proxy_ready;

static id string(const char *s)
{
    return ((id (*)(id, SEL, const char *))objc_msgSend)(CLS("NSString"), SEL_("stringWithUTF8String:"), s);
}

static bool format_path(char *out, size_t size, const char *format, const char *a, const char *b)
{
    int n = snprintf(out, size, format, a, b);
    return n >= 0 && (size_t)n < size;
}

static bool guest_image_path(const char *image, char *path, size_t size)
{
    if (strncmp(image, "\\??\\", 4) == 0 || strncmp(image, "\\\\?\\", 4) == 0)
        image += 4;
    if (!image[0] || image[1] != ':' || (image[2] != '\\' && image[2] != '/'))
        return false;
    char root[MIME_PATH_MAX];
    if (!mime_paths_drive_root(&g_paths, image[0], root, sizeof(root)) || !format_path(path, size, "%s%s", root, image + 2))
        return false;
    for (char *p = path; *p; p++)
        if (*p == '\\')
            *p = '/';
    return true;
}

static void image_name(const char *image, char *name, size_t size)
{
    const char *base = image;
    for (const char *p = image; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;
    mime_strlcpy(name, *base ? base : "Windows application", size);
    for (char *p = name; *p; p++)
        if ((unsigned char)*p < 32 || *p == ':' || *p == '/')
            *p = '_';
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        mime_strlcpy(name, "Windows application", size);
}

static bool write_plist(const char *path, const char *name, const char *identifier)
{
    CFMutableDictionaryRef plist = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                             &kCFTypeDictionaryValueCallBacks);
    if (!plist)
        return false;
    CFStringRef display_name = CFStringCreateWithCString(nullptr, name, kCFStringEncodingUTF8);
    CFStringRef bundle_id = CFStringCreateWithCString(nullptr, identifier, kCFStringEncodingUTF8);
    bool result = false;
    if (display_name && bundle_id) {
        CFDictionarySetValue(plist, CFSTR("CFBundleExecutable"), display_name);
        CFDictionarySetValue(plist, CFSTR("CFBundleName"), display_name);
        CFDictionarySetValue(plist, CFSTR("CFBundleDisplayName"), display_name);
        CFDictionarySetValue(plist, CFSTR("CFBundleIdentifier"), bundle_id);
        CFDictionarySetValue(plist, CFSTR("CFBundlePackageType"), CFSTR("APPL"));
        CFDictionarySetValue(plist, CFSTR("CFBundleVersion"), CFSTR("1"));
        CFDictionarySetValue(plist, CFSTR("NSHighResolutionCapable"), kCFBooleanTrue);
        CFDataRef data = CFPropertyListCreateData(nullptr, plist, kCFPropertyListXMLFormat_v1_0, 0, nullptr);
        if (data) {
            result = mime_write_file(path, CFDataGetBytePtr(data), (size_t)CFDataGetLength(data));
            CFRelease(data);
        }
    }
    if (display_name)
        CFRelease(display_name);
    if (bundle_id)
        CFRelease(bundle_id);
    CFRelease(plist);
    return result;
}

static void write_fallback_icon(const char *destination)
{
    const MimeRomEntry *entry = mime_rom_find("\\Windows\\System32\\imageres.dll");
    void *icon;
    size_t size;
    if (entry && mime_pe_icon_extract((const uint8_t *)mime_rom + entry->offset, entry->size, 15, &icon, &size)) {
        bool written = mime_write_file(destination, icon, size);
        free(icon);
        if (written)
            return;
    }
    unlink(destination);
}

static bool prepare_bundle(AppIdentity *app, const char *image)
{
    char current[MIME_PATH_MAX];
    uint32_t size = sizeof(current);
    struct stat st;
    if (_NSGetExecutablePath(current, &size) != 0 || stat(current, &st) != 0)
        return false;
    MimeSha1 sha;
    uint8_t digest[20];
    mime_sha1_init(&sha);
    mime_sha1_update(&sha, image, strlen(image));
    mime_sha1_update(&sha, current, strlen(current));
    mime_sha1_update(&sha, &st.st_mtimespec, sizeof(st.st_mtimespec));
    mime_sha1_update(&sha, &st.st_size, sizeof(st.st_size));
    mime_sha1_final(&sha, digest);
    char key[41];
    for (unsigned i = 0; i < sizeof(digest); i++)
        snprintf(key + i * 2, 3, "%02x", digest[i]);
    char name[192], bundle[MIME_PATH_MAX], directory[MIME_PATH_MAX], path[MIME_PATH_MAX];
    image_name(image, name, sizeof(name));
    int n = snprintf(bundle, sizeof(bundle), "%s/apps/%s/%u/%s.app", g_paths.cache, key, app->process_id, name);
    if (n < 0 || (size_t)n >= sizeof(bundle) ||
        !format_path(directory, sizeof(directory), "%s/Contents/%s", bundle, "MacOS") ||
        !mime_mkdir_p(directory, 0700) ||
        !format_path(app->executable, sizeof(app->executable), "%s/%s", directory, name))
        return false;
    if (!mime_file_exists(app->executable)) {
        if (copyfile(current, app->executable, nullptr, COPYFILE_ALL | COPYFILE_CLONE | COPYFILE_EXCL) != 0) {
            unlink(app->executable);
            return false;
        }
        if (chmod(app->executable, 0700) != 0)
            return false;
    }
    if (!format_path(directory, sizeof(directory), "%s/Contents/%s", bundle, "Resources") ||
        !mime_mkdir_p(directory, 0700) ||
        !format_path(app->icon_path, sizeof(app->icon_path), "%s/%s", directory, "application.ico") ||
        !format_path(app->override_path, sizeof(app->override_path), "%s/%s", directory, "window.png"))
        return false;
    if (!app->icon_override)
        unlink(app->override_path);
    char host_image[MIME_PATH_MAX] = {};
    guest_image_path(image, host_image, sizeof(host_image));
    void *icon;
    size_t icon_size;
    if (mime_pe_icon_read(host_image, -1, &icon, &icon_size)) {
        mime_write_file(app->icon_path, icon, icon_size);
        free(icon);
    } else {
        unlink(app->icon_path);
    }
    if (!format_path(path, sizeof(path), "%s/%s", directory, "fallback.ico"))
        return false;
    write_fallback_icon(path);
    char identifier[96];
    snprintf(identifier, sizeof(identifier), "org.mime.app.%s.p%u", key, app->process_id);
    return format_path(path, sizeof(path), "%s/Contents/%s", bundle, "Info.plist") &&
           write_plist(path, name, identifier);
}

static void close_fd(void *ctx)
{
    AppReader *reader = ctx;
    close(reader->fd);
    free(reader);
}

static void reap_proxy(void *ctx)
{
    dispatch_source_t source = ctx;
    pid_t pid = (pid_t)dispatch_source_get_handle(source);
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
    dispatch_source_cancel(source);
    dispatch_release(source);
}

static void stop_proxy(AppIdentity *app)
{
    if (app->fd >= 0) {
        send(app->fd, "x", 1, 0);
        shutdown(app->fd, SHUT_RDWR);
        app->fd = -1;
    }
    if (app->reader) {
        dispatch_source_cancel(app->reader);
        dispatch_release(app->reader);
        app->reader = nullptr;
    }
}

static void read_proxy(void *ctx)
{
    AppReader *reader = ctx;
    AppIdentity *app = reader->app;
    if (app->fd != reader->fd)
        return;
    char actions[32];
    ssize_t n = recv(reader->fd, actions, sizeof(actions), 0);
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EINTR))
            stop_proxy(app);
        return;
    }
    for (ssize_t i = 0; i < n; i++) {
        if (app->fd != reader->fd)
            return;
        uint32_t action = actions[i] == 'a' ? MIME_APP_ACTIVATE : actions[i] == 'q' ? MIME_APP_CLOSE : 0;
        if (action && g_sink && app->window_count)
            g_sink(g_sink_ctx, app->process_id, action);
    }
}

static bool start_proxy(AppIdentity *app, const char *image)
{
    if (!prepare_bundle(app, image))
        return false;
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
        return false;
    int inherited = fcntl(pair[1], F_DUPFD_CLOEXEC, 16);
    close(pair[1]);
    if (inherited < 0) {
        close(pair[0]);
        return false;
    }
    fcntl(pair[0], F_SETFD, FD_CLOEXEC);
    int yes = 1;
    setsockopt(pair[0], SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, inherited, APP_PROXY_FD);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
    char *argv[] = {app->executable, "--mime-app-proxy", nullptr};
    pid_t pid;
    int error = posix_spawn(&pid, app->executable, &actions, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(inherited);
    if (error) {
        close(pair[0]);
        errno = error;
        return false;
    }
    dispatch_source_t reaper = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, (uintptr_t)pid,
                                                       DISPATCH_PROC_EXIT, dispatch_get_main_queue());
    dispatch_set_context(reaper, reaper);
    dispatch_source_set_event_handler_f(reaper, reap_proxy);
    dispatch_resume(reaper);
    if (send(pair[0], g_proxy_magic, sizeof(g_proxy_magic), 0) != sizeof(g_proxy_magic)) {
        close(pair[0]);
        return false;
    }
    fcntl(pair[0], F_SETFL, O_NONBLOCK);
    app->fd = pair[0];
    app->reader = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (uintptr_t)app->fd, 0,
                                         dispatch_get_main_queue());
    AppReader *reader = mime_xmalloc(sizeof(*reader));
    *reader = (AppReader){.app = app, .fd = app->fd};
    dispatch_set_context(app->reader, reader);
    dispatch_source_set_event_handler_f(app->reader, read_proxy);
    /* the cancellation handler owns the descriptor even if this slot is reused. */
    dispatch_source_set_cancel_handler_f(app->reader, close_fd);
    dispatch_resume(app->reader);
    return true;
}

static void ensure_proxy(AppIdentity *app, const char *image)
{
    /* a run in the background has no dock entries, whose apps would come forward. */
    const char *background = getenv("MIME_BACKGROUND");
    if (app->fd >= 0 || mime_now_ns() < app->retry_after || (background && *background && strcmp(background, "0")))
        return;
    app->retry_after = mime_now_ns() + 5000000000ull;
    if (!start_proxy(app, image))
        mime_warn("cannot create dock identity for %s: %s", image, strerror(errno));
}

void mime_app_init(const MimePaths *paths, MimeAppActionSink sink, void *ctx)
{
    if (paths)
        g_paths = *paths;
    else if (!mime_paths_init(&g_paths))
        return;
    g_sink = sink;
    g_sink_ctx = ctx;
    if (!g_initialized)
        for (size_t i = 0; i < MIME_ARRAY_COUNT(g_apps); i++)
            g_apps[i].fd = -1;
    g_initialized = true;
}

void mime_app_window_remove(uint32_t window_id)
{
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_windows); i++) {
        AppWindow *w = &g_windows[i];
        if (!w->app || w->window_id != window_id)
            continue;
        AppIdentity *app = w->app;
        *w = (AppWindow){0};
        if (--app->window_count == 0) {
            stop_proxy(app);
            app->process_id = 0;
            app->retry_after = 0;
            app->icon_override = false;
        }
        return;
    }
}

void mime_app_window_set(uint32_t window_id, uint32_t process_id, const char *image, bool taskbar_visible)
{
    if (!g_initialized || !process_id || !image || !*image || !taskbar_visible) {
        mime_app_window_remove(window_id);
        return;
    }
    AppWindow *window = nullptr;
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_windows); i++) {
        AppWindow *w = &g_windows[i];
        if (w->app && w->window_id == window_id) {
            if (w->app->process_id == process_id) {
                ensure_proxy(w->app, image);
                return;
            }
            mime_app_window_remove(window_id);
        }
        if (!w->app && !window)
            window = w;
    }
    if (!window)
        return;
    AppIdentity *app = nullptr;
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_apps); i++)
        if (g_apps[i].process_id == process_id) {
            app = &g_apps[i];
            break;
        }
    if (!app)
        for (size_t i = 0; i < MIME_ARRAY_COUNT(g_apps); i++)
            if (!g_apps[i].process_id) {
                app = &g_apps[i];
                app->process_id = process_id;
                break;
            }
    if (!app)
        return;
    window->window_id = window_id;
    window->app = app;
    app->window_count++;
    ensure_proxy(app, image);
}

void mime_app_process_exit(uint32_t process_id)
{
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_windows); i++)
        if (g_windows[i].app && g_windows[i].app->process_id == process_id)
            mime_app_window_remove(g_windows[i].window_id);
}

void mime_app_window_icon(uint32_t window_id, const void *pixels, uint32_t width, uint32_t height,
                          uint32_t stride)
{
    bool reset = !pixels && !width && !height;
    if (!reset && (!pixels || !width || !height || width > 1024 || height > 1024 || stride < width * 4))
        return;
    AppIdentity *app = nullptr;
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_windows); i++)
        if (g_windows[i].app && g_windows[i].window_id == window_id)
            app = g_windows[i].app;
    if (!app || app->fd < 0)
        return;
    if (reset) {
        if (unlink(app->override_path) == 0 || errno == ENOENT) {
            app->icon_override = false;
            send(app->fd, "i", 1, 0);
        }
        return;
    }
    char temporary[MIME_PATH_MAX];
    if (!format_path(temporary, sizeof(temporary), "%s%s", app->override_path, ".tmp"))
        return;
    CGColorSpaceRef colors = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, pixels, (size_t)stride * height, nullptr);
    CGImageRef image = CGImageCreate(width, height, 8, 32, stride, colors,
                                    kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst, provider, nullptr, false,
                                    kCGRenderingIntentDefault);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8 *)temporary,
                                                          (CFIndex)strlen(temporary), false);
    CGImageDestinationRef destination = image && url ?
        CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr) : nullptr;
    if (destination) {
        CGImageDestinationAddImage(destination, image, nullptr);
        if (CGImageDestinationFinalize(destination) && rename(temporary, app->override_path) == 0) {
            app->icon_override = true;
            send(app->fd, "i", 1, 0);
        }
        CFRelease(destination);
    }
    unlink(temporary);
    if (url)
        CFRelease(url);
    if (image)
        CGImageRelease(image);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colors);
}

void mime_app_shutdown(void)
{
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_apps); i++)
        if (g_apps[i].process_id)
            stop_proxy(&g_apps[i]);
    memset(g_windows, 0, sizeof(g_windows));
    memset(g_apps, 0, sizeof(g_apps));
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_apps); i++)
        g_apps[i].fd = -1;
}

static void proxy_load_icon(void)
{
    const char *paths[] = {g_proxy_override, g_proxy_icon, g_proxy_fallback};
    id icon = nil;
    for (size_t i = 0; i < MIME_ARRAY_COUNT(paths); i++) {
        icon = MSG_ID_ID(MSG_ID(CLS("NSImage"), "alloc"), "initWithContentsOfFile:", string(paths[i]));
        if (icon && ((BOOL (*)(id, SEL))objc_msgSend)(icon, SEL_("isValid")))
            break;
        MSG_V(icon, "release");
        icon = nil;
    }
    MSG_V_ID(g_proxy_app, "setApplicationIconImage:", icon);
    MSG_V(icon, "release");
}

static void proxy_read(void *ctx)
{
    char actions[32];
    ssize_t n = recv(APP_PROXY_FD, actions, sizeof(actions), 0);
    if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN))
        exit(0);
    for (ssize_t i = 0; i < n; i++) {
        if (actions[i] == 'x')
            exit(0);
        if (actions[i] == 'i')
            proxy_load_icon();
    }
}

static BOOL proxy_reopen(id self, SEL sel, id app, BOOL visible)
{
    send(APP_PROXY_FD, "a", 1, 0);
    return NO;
}

static void proxy_active(id self, SEL sel, id notification)
{
    if (g_proxy_ready)
        send(APP_PROXY_FD, "a", 1, 0);
}

static unsigned long proxy_terminate(id self, SEL sel, id app)
{
    send(APP_PROXY_FD, "q", 1, 0);
    return 0;
}

int mime_app_proxy_main(int argc, char **argv)
{
    if (argc != 2 || strcmp(argv[1], "--mime-app-proxy") != 0)
        return -1;
    pid_t parent = 0;
    socklen_t length = sizeof(parent);
    if (getsockopt(APP_PROXY_FD, SOL_LOCAL, LOCAL_PEERPID, &parent, &length) != 0 || parent != getppid())
        return 1;
    char magic[sizeof(g_proxy_magic)];
    size_t received = 0;
    while (received < sizeof(magic)) {
        ssize_t n = recv(APP_PROXY_FD, magic + received, sizeof(magic) - received, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return 1;
        received += (size_t)n;
    }
    if (memcmp(magic, g_proxy_magic, sizeof(magic)) != 0)
        return 1;
    signal(SIGPIPE, SIG_IGN);
    fcntl(APP_PROXY_FD, F_SETFL, O_NONBLOCK);
    id pool = MSG_ID(MSG_ID(CLS("NSAutoreleasePool"), "alloc"), "init");
    id bundle = MSG_ID(CLS("NSBundle"), "mainBundle");
    id resource = MSG_ID(bundle, "resourcePath");
    const char *resource_path = ((const char *(*)(id, SEL))objc_msgSend)(resource, SEL_("fileSystemRepresentation"));
    if (!resource_path || !format_path(g_proxy_icon, sizeof(g_proxy_icon), "%s/%s", resource_path, "application.ico") ||
        !format_path(g_proxy_override, sizeof(g_proxy_override), "%s/%s", resource_path, "window.png") ||
        !format_path(g_proxy_fallback, sizeof(g_proxy_fallback), "%s/%s", resource_path, "fallback.ico"))
        return 1;
    id name = MSG_ID_ID(bundle, "objectForInfoDictionaryKey:", string("CFBundleName"));
    if (name)
        MSG_V_ID(MSG_ID(CLS("NSProcessInfo"), "processInfo"), "setProcessName:", name);
    g_proxy_app = MSG_ID(CLS("NSApplication"), "sharedApplication");
    Class delegate_class = objc_allocateClassPair(objc_getClass("NSObject"), "MimeAppProxyDelegate", 0);
    class_addMethod(delegate_class, SEL_("applicationShouldHandleReopen:hasVisibleWindows:"), (IMP)proxy_reopen, "c@:@c");
    class_addMethod(delegate_class, SEL_("applicationDidBecomeActive:"), (IMP)proxy_active, "v@:@");
    class_addMethod(delegate_class, SEL_("applicationShouldTerminate:"), (IMP)proxy_terminate, "Q@:@");
    objc_registerClassPair(delegate_class);
    id delegate = MSG_ID(MSG_ID((id)delegate_class, "alloc"), "init");
    MSG_V_ID(g_proxy_app, "setDelegate:", delegate);
    ((BOOL (*)(id, SEL, long))objc_msgSend)(g_proxy_app, SEL_("setActivationPolicy:"), 0);
    proxy_load_icon();
    dispatch_source_t reader = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, APP_PROXY_FD, 0,
                                                      dispatch_get_main_queue());
    dispatch_source_set_event_handler_f(reader, proxy_read);
    dispatch_resume(reader);
    MSG_V(g_proxy_app, "finishLaunching");
    g_proxy_ready = true;
    MSG_V(g_proxy_app, "run");
    MSG_V(pool, "drain");
    return 0;
}
