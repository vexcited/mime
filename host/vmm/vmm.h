/*
 * mime virtual machine monitor, built on Hypervisor.framework on macos
 * and kvm on linux.
 */
#ifndef MIME_VMM_H
#define MIME_VMM_H

#include "../mime_host.h"
#include "../../include/mime/abi.h"

#include <pthread.h>
#include <time.h>

#define MIME_VMM_MAX_VCPUS 64
#define MIME_VMM_FALLBACK_RAM (4ull << 30)
#define MIME_VMM_MAX_RAM (MIME_WINDOW_BASE - MIME_RAM_BASE)

/* physical layout used by the loader at the start of guest ram. */
#define MIME_LAYOUT_SHARED 0x0000ull
#define MIME_LAYOUT_BOOT_INFO 0x1000ull
#define MIME_LAYOUT_BOOT_STACK 0x100000ull
#define MIME_LAYOUT_BOOT_STACK_SIZE 0x10000ull

typedef struct MimeVm MimeVm;

typedef struct MimeVcpu {
    MimeVm *vm;
    uint32_t index;
#if defined(__linux__)
    int fd;
    struct kvm_run *run;
    volatile bool running; /* in KVM_RUN, where a signal kicks it out */
    uint64_t exits_io;
    uint64_t exits_intr;
    uint64_t exits_halt;
#else
    uint64_t handle; /* hv_vcpu_t */
    void *exit_info; /* hv_vcpu_exit_t */
    uint64_t vtimer_offset;
#endif
    pthread_t thread;
    /* where a secondary starts, set by MIME_HC_CPU_START. */
    bool started;
    uint64_t start_pc, start_x0, start_sp;
    /* exit statistics, printed with the SIGUSR1 state dump. */
    uint64_t exits_total;
    uint64_t exits_vtimer;
    uint64_t exits_canceled;
    uint64_t exits_wfi;
    uint64_t exits_sysreg;
    uint64_t exits_hvc[64];
} MimeVcpu;

/* a queued control message waiting for MIME_HC_CTRL_RECV. */
typedef struct MimeCtrlNode {
    struct MimeCtrlNode *next;
    uint32_t size;
    uint8_t data[];
} MimeCtrlNode;

typedef struct MimeFsHandle {
    int fd;
    bool is_dir;
    bool delete_on_close;
    char *host_path;
    void *dir_entries; /* cached directory listing */
    uint32_t dir_count;
} MimeFsHandle;

/* the host page size: guest memory goes back to the host in these units. */
#if defined(__APPLE__)
#define MIME_HOST_PAGE 0x4000u
#else
#define MIME_HOST_PAGE 0x1000u
#endif

/* guest physical memory filled on first touch, see lazy.c. */
#define MIME_LAZY_PAGE 0x4000u   /* the host page size */
#define MIME_LAZY_CHUNK 0x4000u /* filled per fault */

typedef struct MimeLazyRegion {
    uint64_t ipa;
    uint64_t size;
    uint8_t *host;      /* anonymous memory behind the region */
    int fd;             /* source file, owned, or -1 */
    const uint8_t *src; /* source memory when there is no file */
    uint64_t src_size;
    uint64_t *filled; /* one bit per chunk */
    uint64_t filled_bytes;
    uint64_t last_use;
} MimeLazyRegion;

#define MIME_VMM_MAX_FS_HANDLES 4096
#define MIME_VMM_MAX_WINDOWS 1024
#define MIME_VMM_INPUT_QUEUE 1024

#if defined(__linux__)
/* a host file mapped read only into the guest from the page cache, its own memslot. */
typedef struct MimeFileWindow {
    uint64_t ipa;
    uint64_t size;
    void *host;
    uint32_t slot;
} MimeFileWindow;
#endif

struct MimeVm {
    const MimePaths *paths;
    uint8_t *ram;
    uint64_t ram_size;
    MimeBootInfo *boot;
    MimeSharedPage *shared;
    uint64_t kernel_entry;
    uint64_t boot_stack_top;
    uint32_t irq_intid;
    uint32_t vcpu_count;
    MimeVcpu vcpus[MIME_VMM_MAX_VCPUS];
#if defined(__linux__)
    int kvm_fd;
    int vm_fd;
    int irq_fd; /* eventfd raising the host interrupt through kvm's irqfd */
    uint32_t next_slot;
    uint32_t free_slots[MIME_VMM_MAX_WINDOWS];
    uint32_t free_slot_count;
    uint64_t tsc_hz;
    bool huge_pages; /* ram is backed by transparent huge pages */
#endif
    /* the framework fixes the topology once a vcpu runs: every one is created before the first runs. */
    pthread_cond_t vcpu_cond;
    uint32_t vcpus_created;

    pthread_mutex_t lock;
    volatile bool stopping;
    volatile bool halted;
    int exit_code;
    volatile bool guest_ready;

    MimeCtrlNode *ctrl_head, *ctrl_tail;

    MimeInputEvent input[MIME_VMM_INPUT_QUEUE];
    uint32_t input_head, input_tail;

    pthread_mutex_t fs_lock;
    MimeFsHandle *fs_handles[MIME_VMM_MAX_FS_HANDLES];
#if defined(__linux__)
    MimeFileWindow windows[MIME_VMM_MAX_WINDOWS]; /* host files of MIME_HC_FS_MAP, sorted by ipa */
#else
    MimeLazyRegion rom;
    MimeLazyRegion windows[MIME_VMM_MAX_WINDOWS]; /* host files of MIME_HC_FS_MAP, sorted by ipa */
#endif
    uint32_t window_count;
    uint64_t window_bytes; /* filled bytes of every window */
    uint64_t window_clock;

    char host_root_label[8];
};

/* machine.c, shared by every host. */
uint64_t mime_unix_to_filetime(struct timespec ts);
void mime_vm_init_boot(MimeVm *vm);

/* vm.c on macos, linux/kvm.c on linux. */
void mime_host_volume_label(char *out, size_t size);
/* sleeps while the guest word at host address word holds value, at most timeout_ns; wakes every sleeper. */
void mime_host_wait_word(uint32_t *word, uint32_t value, uint64_t timeout_ns);
void mime_host_wake_word(uint32_t *word);
uint64_t mime_vm_default_ram(void);
bool mime_vm_create(MimeVm *vm, const MimePaths *paths, uint64_t ram_size);
bool mime_vm_start(MimeVm *vm);
void mime_vm_request_stop(MimeVm *vm, int exit_code);
void mime_vm_destroy(MimeVm *vm);
void *mime_vm_phys(MimeVm *vm, uint64_t pa, uint64_t len);
void mime_vm_discard(MimeVm *vm, uint64_t pa, uint64_t len);
void mime_vm_raise_irq(MimeVm *vm, uint32_t bits);
void mime_vm_queue_ctrl(MimeVm *vm, const void *msg, uint32_t size);
void mime_vm_queue_input(MimeVm *vm, const MimeInputEvent *ev);

/* loader.c */
bool mime_vm_load_kernel(MimeVm *vm);

/* vcpu.c */
bool mime_vcpu_start(MimeVcpu *vcpu);
void mime_vcpu_kick(MimeVcpu *vcpu);
void mime_vcpu_request_dump(MimeVm *vm);
bool mime_vcpu_start_secondary(MimeVm *vm, uint32_t index, uint64_t pc, uint64_t x0, uint64_t sp);

/* hcall.c */
void mime_hcall_dispatch(MimeVcpu *vcpu, uint64_t regs[8]);

/* fs.c */
uint64_t mime_fs_hcall(MimeVm *vm, uint64_t nr, uint64_t regs[8]);
void mime_fs_close_all(MimeVm *vm);
bool mime_fs_lazy_fault(MimeVm *vm, uint64_t ipa);

/* window.c, with fs_lock held: host files mapped into the guest. */
uint64_t mime_window_map(MimeVm *vm, int fd, uint64_t *ipa_out);
uint64_t mime_window_unmap(MimeVm *vm, uint64_t ipa);
void mime_window_unmap_all(MimeVm *vm);

/* net.c */
uint64_t mime_net_hcall(MimeVm *vm, uint64_t nr, uint64_t regs[8]);
void mime_net_close_all(void);

/* lazy.c */
bool mime_lazy_init(MimeLazyRegion *r, uint64_t ipa, uint64_t size, int fd, const void *src, uint64_t src_size);
bool mime_lazy_contains(const MimeLazyRegion *r, uint64_t ipa);
bool mime_lazy_fill(MimeLazyRegion *r, uint64_t ipa);
void mime_lazy_drop(MimeLazyRegion *r);
void mime_lazy_free(MimeLazyRegion *r);

/* daemon.c, events emitted by the guest. */
void mime_daemon_guest_event(MimeVm *vm, const MimeCtrlHeader *msg);
void mime_daemon_guest_halted(MimeVm *vm, int exit_code);

#endif
