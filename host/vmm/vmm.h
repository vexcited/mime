/*
 * mime virtual machine monitor, built on Hypervisor.framework.
 */
#ifndef MIME_VMM_H
#define MIME_VMM_H

#include "../mime_host.h"
#include "../../include/mime/abi.h"

#include <pthread.h>

#define MIME_VMM_MAX_VCPUS 1
#define MIME_VMM_DEFAULT_RAM (4ull << 30)

typedef struct MimeVm MimeVm;

typedef struct MimeVcpu {
    MimeVm *vm;
    uint32_t index;
    uint64_t handle; /* hv_vcpu_t */
    void *exit_info; /* hv_vcpu_exit_t */
    pthread_t thread;
    uint64_t vtimer_offset;
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

#define MIME_VMM_MAX_FS_HANDLES 4096
#define MIME_VMM_INPUT_QUEUE 1024

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

    char host_root_label[8];
};

/* vm.c */
bool mime_vm_create(MimeVm *vm, const MimePaths *paths, uint64_t ram_size);
bool mime_vm_start(MimeVm *vm);
void mime_vm_request_stop(MimeVm *vm, int exit_code);
void mime_vm_destroy(MimeVm *vm);
void *mime_vm_phys(MimeVm *vm, uint64_t pa, uint64_t len);
void mime_vm_raise_irq(MimeVm *vm, uint32_t bits);
void mime_vm_queue_ctrl(MimeVm *vm, const void *msg, uint32_t size);
void mime_vm_queue_input(MimeVm *vm, const MimeInputEvent *ev);

/* loader.c */
bool mime_vm_load_kernel(MimeVm *vm);

/* vcpu.c */
bool mime_vcpu_start(MimeVcpu *vcpu);
void mime_vcpu_kick(MimeVcpu *vcpu);
void mime_vcpu_request_dump(MimeVm *vm);

/* hcall.c */
void mime_hcall_dispatch(MimeVcpu *vcpu, uint64_t regs[8]);

/* fs.c */
uint64_t mime_fs_hcall(MimeVm *vm, uint64_t nr, uint64_t regs[8]);
void mime_fs_close_all(MimeVm *vm);

/* daemon.c, events emitted by the guest. */
void mime_daemon_guest_event(MimeVm *vm, const MimeCtrlHeader *msg);
void mime_daemon_guest_halted(MimeVm *vm, int exit_code);

#endif
