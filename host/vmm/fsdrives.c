#include "vmm.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_rwlock_t g_lock = PTHREAD_RWLOCK_INITIALIZER;
static MimeHostDrive g_drives[MIME_FS_MAX_DRIVES];
static size_t g_count;
static MimeVm *g_vm;
static bool g_stopped;

static bool same(const MimeHostDrive *a, size_t a_count, const MimeHostDrive *b, size_t b_count)
{
    if (a_count != b_count)
        return false;
    for (size_t i = 0; i < a_count; i++)
        if (a[i].letter != b[i].letter || a[i].flags != b[i].flags || a[i].serial != b[i].serial ||
            strcmp(a[i].label, b[i].label) || strcmp(a[i].root, b[i].root))
            return false;
    return true;
}

static bool rescan(void)
{
    MimeHostDrive *found = mime_xcalloc(MIME_FS_MAX_DRIVES, sizeof(*found));
    size_t count = mime_drives_scan(g_vm->paths, true, found, MIME_FS_MAX_DRIVES);
    pthread_rwlock_wrlock(&g_lock);
    bool changed = !same(g_drives, g_count, found, count);
    if (changed) {
        memcpy(g_drives, found, count * sizeof(*found));
        g_count = count;
    }
    pthread_rwlock_unlock(&g_lock);
    if (changed)
        for (size_t i = 0; i < count; i++)
            mime_info("drives: %c: is %s", found[i].letter, found[i].root);
    free(found);
    return changed;
}

static void tell_guest(void)
{
    pthread_rwlock_rdlock(&g_lock);
    if (!g_stopped)
        mime_vm_raise_irq(g_vm, MIME_IRQ_DRIVES);
    pthread_rwlock_unlock(&g_lock);
}

/* linux tells a change of the mount table as an exceptional condition of mountinfo; macOS is asked every few seconds. */
static void *watch_main(void *arg)
{
    (void)arg;
    mime_set_thread_name("mime-drives");
#ifdef __linux__
    int fd = open("/proc/self/mountinfo", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return nullptr;
    for (;;) {
        struct pollfd p = {.fd = fd, .events = POLLPRI};
        if (poll(&p, 1, -1) < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        /* a desktop mounts a plugged disk in a few steps, the table is read once they settle. */
        usleep(250000);
        char buffer[4096];
        lseek(fd, 0, SEEK_SET);
        while (read(fd, buffer, sizeof(buffer)) > 0) {
        }
        if (rescan())
            tell_guest();
    }
    close(fd);
#else
    for (;;) {
        sleep(3);
        if (rescan())
            tell_guest();
    }
#endif
    return nullptr;
}

void mime_fs_drives_start(MimeVm *vm)
{
    g_vm = vm;
    rescan();
    pthread_t thread;
    if (pthread_create(&thread, nullptr, watch_main, nullptr) == 0)
        pthread_detach(thread);
}

void mime_fs_drives_stop(void)
{
    pthread_rwlock_wrlock(&g_lock);
    g_stopped = true;
    pthread_rwlock_unlock(&g_lock);
}

bool mime_fs_volume_root(MimeVm *vm, uint32_t volume, char *out, size_t size)
{
    if (volume == MIME_FS_VOLUME_SYSTEM || volume == MIME_FS_VOLUME_HOST) {
        mime_strlcpy(out, volume == MIME_FS_VOLUME_SYSTEM ? vm->paths->drive_c : "", size);
        return true;
    }
    if (volume < MIME_FS_VOLUME_DRIVE + MIME_FS_DRIVE_FIRST - 'A' || volume > MIME_FS_VOLUME_DRIVE + MIME_FS_DRIVE_LAST - 'A')
        return false;
    char letter = (char)('A' + volume - MIME_FS_VOLUME_DRIVE);
    bool found = false;
    pthread_rwlock_rdlock(&g_lock);
    for (size_t i = 0; i < g_count && !found; i++)
        if (g_drives[i].letter == letter) {
            mime_strlcpy(out, g_drives[i].root, size);
            found = true;
        }
    pthread_rwlock_unlock(&g_lock);
    return found;
}

uint64_t mime_fs_drives(MimeVm *vm, uint64_t pa, uint64_t capacity, uint64_t *count)
{
    if (capacity > MIME_FS_MAX_DRIVES)
        capacity = MIME_FS_MAX_DRIVES;
    MimeFsDrive *out = capacity ? mime_vm_phys(vm, pa, capacity * sizeof(MimeFsDrive)) : nullptr;
    if (capacity && !out)
        return MIME_HC_STATUS_INVALID;
    pthread_rwlock_rdlock(&g_lock);
    size_t n = MIME_MIN(g_count, capacity);
    for (size_t i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].volume = MIME_FS_VOLUME_DRIVE + (uint32_t)(g_drives[i].letter - 'A');
        out[i].serial = g_drives[i].serial;
        out[i].flags = g_drives[i].flags;
        mime_strlcpy(out[i].label, g_drives[i].label, sizeof(out[i].label));
    }
    *count = g_count;
    pthread_rwlock_unlock(&g_lock);
    return MIME_HC_STATUS_OK;
}

bool mime_fs_letter_root(MimeVm *vm, char letter, char *out, size_t size)
{
    letter = (char)(letter & ~0x20);
    if (letter < 'A' || letter > 'Z')
        return false;
    uint32_t volume = letter == 'C' ? MIME_FS_VOLUME_SYSTEM :
                      letter == 'Z' ? MIME_FS_VOLUME_HOST : MIME_FS_VOLUME_DRIVE + (uint32_t)(letter - 'A');
    return mime_fs_volume_root(vm, volume, out, size);
}

char mime_fs_drive_of(const char *host, size_t *root_length)
{
    char letter = 0;
    size_t best = 0;
    pthread_rwlock_rdlock(&g_lock);
    for (size_t i = 0; i < g_count; i++) {
        size_t n = strlen(g_drives[i].root);
        if (n > best && !strncmp(host, g_drives[i].root, n) && (host[n] == '/' || !host[n])) {
            best = n;
            letter = g_drives[i].letter;
        }
    }
    pthread_rwlock_unlock(&g_lock);
    *root_length = best;
    return letter;
}
