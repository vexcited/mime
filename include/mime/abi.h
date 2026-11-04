/*
 * host and guest ABI. fixed-width fields keep layouts identical between the LP64 host and LLP64 Windows
 * guest.
 *
 * the guest runs on the host processor. ntoskrnl.exe issues hypercalls from EL1 on ARM64 with `hvc #0`:
 * x0 = MIME_HC_* number, x1..x6 = arguments
 * x0 = MIME_HC_STATUS_* on return, x1 = result value
 * on x64 it writes MIME_HC_PORT from ring 0 with `outb %al`:
 * rax = MIME_HC_* number, rdi rsi rdx r10 r8 r9 = arguments
 * rax = MIME_HC_STATUS_* on return, rdx = result value
 * buffers use guest physical addresses and must be physically contiguous.
 */
#ifndef MIME_ABI_H
#define MIME_ABI_H

#include <stdint.h>

#define MIME_ABI_VERSION 4

/* guest physical layout. */
#ifdef __x86_64__
/*
 * x64 reserves the first 4 GiB for system files and the KVM APIC page at 0xfee00000, so guest RAM never has to split around it.
 */
#define MIME_ROM_BASE 0x10000000ull /* the system files, read only, below RAM. */
#define MIME_RAM_BASE 0x100000000ull
/*
 * host files placed read only by MIME_HC_FS_MAP, above RAM, mapped from
 * the host page cache.
 */
#define MIME_WINDOW_BASE 0x10000000000ull
#define MIME_WINDOW_SIZE 0x10000000000ull
#else
#define MIME_GIC_BASE 0x08000000ull /* the GIC registers, below RAM. */
#define MIME_ROM_BASE 0x10000000ull /* the system files, read only, below RAM. */
#define MIME_RAM_BASE 0x40000000ull
/*
 * host files placed read only by MIME_HC_FS_MAP, above ram. the host fills
 * them as the guest touches them, so only data read costs memory.
 */
#define MIME_WINDOW_BASE 0x800000000ull
#define MIME_WINDOW_SIZE 0x800000000ull
#endif
#define MIME_WINDOW_ALIGN 0x10000ull
#define MIME_PAGE_SIZE 4096ull

/* x64: the i/o port of hypercalls and the apic vector of the host's interrupt. */
#define MIME_HC_PORT 0x5E
#define MIME_X64_IRQ_VECTOR 0x81

/*
 * guest kernel virtual layout, 48 bit addresses with a 4K granule.
 * user space is 0 .. 0x00007FFF'FFFFFFFF exactly like windows.
 */
#define MIME_KVA_PHYSMAP 0xFFFF800000000000ull /* direct map of guest physical memory. */
#define MIME_KVA_KUSER_SHARED 0xFFFFF78000000000ull
#define MIME_KVA_KERNEL 0xFFFFF80000000000ull /* ntoskrnl.exe and boot drivers. */
#define MIME_KVA_SYSTEM 0xFFFFF90000000000ull /* kernel stacks, driver images, mappings. */
#define MIME_KVA_SYSTEM_SIZE 0x0000010000000000ull
#define MIME_USER_KUSER_SHARED 0x7FFE0000ull
#define MIME_USER_ADDRESS_LIMIT 0x00007FFFFFFFFFFFull

#define MIME_BOOT_MAGIC 0x544F4F42454D494Dull /* "MIMEBOOT". */
#define MIME_BOOT_MAX_RANGES 16

typedef struct MimeMemRange {
    uint64_t base;
    uint64_t size;
} MimeMemRange;

typedef struct MimeDisplayInfo {
    uint32_t width;  /* guest pixels of the primary display. */
    uint32_t height;
    uint32_t work_x; /* usable work area, excluding menu bar and dock. */
    uint32_t work_y;
    uint32_t work_width;
    uint32_t work_height;
    uint32_t dpi; /* 96 * backing scale factor. */
    uint32_t refresh_hz;
    uint32_t flags; /* MIME_DISPLAY_*. */
} MimeDisplayInfo;

#define MIME_DISPLAY_STRETCH 0x1 /* windows drawn at a lower dpi than the display's are stretched by the host, see MimeWinConfig. */

/*
 * MIME_HC_TRAY manages notification icons, balloons and menus. commands occupy up to MIME_TRAY_COMMAND_SIZE
 * bytes at the supplied address. returns MIME_HC_STATUS_UNSUPPORTED without a host desktop bus.
 */
enum {
    MIME_TRAY_SET = 1,     /* the icon is there with this tip, picture and state: created on the first one. */
    MIME_TRAY_REMOVE = 2,  /* the icon goes. */
    MIME_TRAY_BALLOON = 3, /* a notification for the icon: title, text and picture. */
    MIME_TRAY_MENU = 4,    /* the menu the icon shows, the answer to MIME_TRAY_EVENT_MENU_REQUEST, items in menu[]. */
};

enum {
    MIME_TRAY_HIDDEN = 1u << 0,      /* the icon is kept out of sight: passive. */
    MIME_TRAY_HAS_PICTURE = 1u << 1, /* pixels holds the picture. */
    MIME_TRAY_HAS_TIP = 1u << 2,
    MIME_TRAY_ERROR = 1u << 3,       /* a balloon of an error: critical. */
    MIME_TRAY_WARNING = 1u << 4,
    MIME_TRAY_SILENT = 1u << 5,      /* a balloon without a sound. */
};

#define MIME_TRAY_PICTURE_MAX 64   /* a picture is at most this many pixels on a side. */
#define MIME_TRAY_MENU_MAX 256

enum {
    MIME_TRAY_ITEM_SEPARATOR = 1u << 0,
    MIME_TRAY_ITEM_DISABLED = 1u << 1,
    MIME_TRAY_ITEM_CHECKED = 1u << 2,
    MIME_TRAY_ITEM_RADIO = 1u << 3,   /* a mark that is a dot. */
    MIME_TRAY_ITEM_DEFAULT = 1u << 4, /* in bold. */
};

typedef struct MimeTrayItem {
    uint32_t parent; /* 0 for the menu itself, else 1 + the index of the item whose submenu this is. */
    uint32_t flags;  /* MIME_TRAY_ITEM_* */
    uint16_t label[64];
} MimeTrayItem;

typedef struct MimeTrayCommand {
    uint32_t op;    /* MIME_TRAY_* */
    uint32_t id;    /* the icon, as the guest numbers it. */
    uint32_t flags; /* MIME_TRAY_* */
    uint32_t process_id;
    uint32_t width, height;                 /* of the picture of the icon. */
    uint32_t balloon_width, balloon_height; /* and of the balloon's. */
    uint32_t menu_count;
    uint16_t app[64];            /* the name of the program. */
    uint16_t tip[128];
    uint16_t balloon_title[64];
    uint16_t balloon_text[256];
    uint8_t pixels[MIME_TRAY_PICTURE_MAX * MIME_TRAY_PICTURE_MAX * 4];         /* ARGB, a, r, g, b in this order, rows of width * 4. */
    uint8_t balloon_pixels[MIME_TRAY_PICTURE_MAX * MIME_TRAY_PICTURE_MAX * 4];
    MimeTrayItem menu[MIME_TRAY_MENU_MAX];
} MimeTrayCommand;

#define MIME_TRAY_COMMAND_SIZE sizeof(MimeTrayCommand)

/* MIME_INPUT_TRAY: what happened to an icon. */
enum {
    MIME_TRAY_EVENT_ACTIVATE = 1,     /* clicked: x, y where. */
    MIME_TRAY_EVENT_CONTEXT = 2,      /* its context menu asked for with the pointer: x, y where. */
    MIME_TRAY_EVENT_SECONDARY = 3,    /* a click of another button. */
    MIME_TRAY_EVENT_SCROLL = 4,       /* the wheel over it: ch is the step, positive up. */
    MIME_TRAY_EVENT_MENU_REQUEST = 5, /* the desktop is about to show its menu: answer with MIME_TRAY_MENU. */
    MIME_TRAY_EVENT_MENU_CHOSEN = 6,  /* an item of the menu was chosen: ch is its index. */
    MIME_TRAY_EVENT_MENU_CLOSED = 7,  /* the menu went away with no choice. */
    MIME_TRAY_EVENT_BALLOON_SHOWN = 8,
    MIME_TRAY_EVENT_BALLOON_CLICKED = 9,
    MIME_TRAY_EVENT_BALLOON_TIMEOUT = 10, /* gone by itself or dismissed with its close button. */
    MIME_TRAY_EVENT_BALLOON_HIDDEN = 11,  /* taken away by the program. */
};

/*
 * MIME_HC_SHELL commands begin with an operation below. returns MIME_HC_STATUS_UNSUPPORTED without a host
 * desktop bus.
 */
enum {
    MIME_SHELL_TOAST_SHOW = 1,   /* MimeShellToast: show a toast, replacing the one with the same application, tag and group. */
    MIME_SHELL_TOAST_HIDE = 2,   /* MimeShellToast: take the toast id away. */
    MIME_SHELL_TOAST_REMOVE = 3, /* MimeShellToast: take away those of the application aumid, with the tag and group if given. */
    MIME_SHELL_TOAST_PROBE = 4,  /* MimeShellToast: whether toasts can be shown at all. */
    MIME_SHELL_TOAST_REPLY = 5,  /* MimeShellToast: the text of a reply to the toast id is written to body, its units are the value. */
    MIME_SHELL_WINDOW = 10,      /* MimeShellWindow: the application user model id of a window or of a process. */
    MIME_SHELL_LAUNCHER = 11,    /* MimeShellWindow: the progress, badge and attention of a window's button in the launcher. */
    MIME_SHELL_FLASH = 12,       /* MimeShellWindow: a window asks for attention, or stops. */
    MIME_SHELL_JUMPLIST = 13,    /* MimeShellJumpList: the tasks of an application's jump list. */
};

enum {
    MIME_TOAST_SILENT = 1u << 0,
    MIME_TOAST_URGENT = 1u << 1,   /* alarm, incoming call and urgent scenarios: critical, stays until dealt with. */
    MIME_TOAST_RESIDENT = 1u << 2, /* the reminder scenario: stays until dealt with. */
    MIME_TOAST_REPLY = 1u << 3,    /* the first action takes text, the placeholder is the hint. */
    MIME_TOAST_NO_POPUP = 1u << 4, /* the program wants it in the history only. */
};

#define MIME_TOAST_ACTIONS 5

typedef struct MimeToastAction {
    uint32_t flags;
    uint16_t label[64];
} MimeToastAction;

typedef struct MimeShellToast {
    uint32_t op;
    uint32_t id; /* guest kernel toast id to hide. */
    uint32_t flags;
    uint32_t process_id;
    uint32_t timeout_ms; /* 0 for the server's own. */
    uint32_t action_count;
    uint16_t aumid[128];
    uint16_t app[64]; /* the name the application is known by. */
    uint16_t tag[64];
    uint16_t group[64];
    uint16_t title[256];
    uint16_t body[1024];
    uint16_t logo[260];    /* windows paths of pictures: the logo, the hero and the inline one. */
    uint16_t hero[260];
    uint16_t picture[260];
    uint16_t placeholder[64];
    MimeToastAction actions[MIME_TOAST_ACTIONS];
} MimeShellToast;

/* MIME_SHELL_TOAST_PROBE answers in the value of the hypercall: 1 when notifications can be shown. */

enum {
    MIME_SHELL_EVENT_TOAST_ACTIVATED = 1, /* x is the action, -1 for the toast itself. */
    MIME_SHELL_EVENT_TOAST_REPLIED = 2,   /* x is the action, the text is fetched with MIME_SHELL_TOAST_REPLY. */
    MIME_SHELL_EVENT_TOAST_DISMISSED = 3, /* x is 0 for the person, 1 for the program, 2 for the time. */
    MIME_SHELL_EVENT_TOAST_FAILED = 4,    /* x is the error. */
};

/* launcher window and application state keyed by application user model id. */
enum {
    MIME_SHELL_WINDOW_APPID = 1,         /* the window belongs to the application aumid; none to leave it to its process. */
    MIME_SHELL_PROCESS_APPID = 2,        /* the windows of the process, that have no id of their own, belong to the application aumid. */
};

enum {
    MIME_LAUNCHER_PROGRESS = 1u << 0, /* progress holds the fraction, 0 to 10000; progress_visible is whether it shows. */
    MIME_LAUNCHER_BADGE = 1u << 1,    /* count is the number on the badge, 0 for none. */
    MIME_LAUNCHER_ATTENTION = 1u << 2, /* urgent is whether the program wants attention. */
};

typedef struct MimeShellWindow {
    uint32_t op;
    uint32_t window_id;
    uint32_t kind; /* MIME_SHELL_WINDOW_* */
    uint32_t process_id;
    uint32_t changed; /* MIME_LAUNCHER_* */
    uint32_t progress;
    uint32_t progress_visible;
    uint32_t indeterminate;
    uint32_t count;
    uint32_t urgent; /* for MIME_SHELL_FLASH: 0 stops, 1 flashes for a moment, 2 until the window is in front. */
    uint16_t aumid[128];
} MimeShellWindow;

#define MIME_JUMPLIST_TASKS 16

/* jump list launch settings and presentation, using guest Windows paths. */
typedef struct MimeJumpTask {
    uint16_t title[64];
    uint16_t path[260];
    uint16_t arguments[260];
    uint16_t directory[260];
    uint16_t icon[260]; /* the file with the icon, and its index. */
    int32_t icon_index;
    uint32_t pad;
} MimeJumpTask;

typedef struct MimeShellJumpList {
    uint32_t op;
    uint32_t process_id;
    uint32_t task_count;
    uint32_t pad;
    uint16_t aumid[128];
    MimeJumpTask tasks[MIME_JUMPLIST_TASKS];
} MimeShellJumpList;

/* passed to the kernel entry point in x0, as a physmap virtual address. */
typedef struct MimeBootInfo {
    uint64_t magic;
    uint32_t version;
    uint32_t size;

    uint64_t ram_base;
    uint64_t ram_size;
    uint64_t first_free_phys; /* everything below was used by the loader. */

    uint64_t kernel_phys;
    uint64_t kernel_virt;
    uint64_t kernel_size;

    uint64_t rom_phys; /* MimeRomHeader, also mapped read only at physmap + rom_phys. */
    uint64_t rom_size;

    uint64_t ttbr1_phys;  /* kernel translation tables built by the vmm. */
    uint64_t shared_phys; /* MimeSharedPage */
    uint64_t boot_stack_phys;
    uint64_t boot_stack_size;

    uint64_t timer_frequency;
    /* GICv3 emulated by the host: distributor, redistributors, both mapped at physmap + phys as device memory. */
    uint64_t gic_dist_phys;
    uint64_t gic_dist_size;
    uint64_t gic_redist_phys;
    uint64_t gic_redist_size;
    uint32_t irq_intid; /* edge triggered spi raised by the host, see MimeSharedPage, the vector on x64. */
    uint32_t timer_intid; /* ppi of the el1 virtual timer. */
    uint64_t boot_counter;  /* CNTVCT_EL0 matching boot_filetime, the tsc on x64. */
    uint64_t boot_filetime; /* 100ns units since 1601-01-01 UTC. */
    int32_t timezone_bias;  /* minutes, UTC = local + bias. */
    uint32_t cpu_count;

    MimeDisplayInfo display;

    /* identity of the installation, chosen once by the host, see host/identity.c. */
    char computer_name[16];
    char user_name[24];
    uint32_t user_sid[3];  /* the machine sid S-1-5-21-a-b-c, the user is its rid 1001. */
    uint32_t volume_serial; /* of C: */
    uint64_t install_filetime;
    /*
     * reference Windows 11 ARM64 processor and firmware identity, reported to the guest in place of the macOS
     * host and Apple hardware.
     */
    char cpu_name[64];          /* brand string of the reference processor. */
    uint32_t cpu_mhz;           /* its nominal peak frequency, 0 when unknown. */
    uint64_t cpu_midr;          /* its MIDR_EL1, 0 to report the hardware one. */
    char system_manufacturer[64];
    char system_product[64];
    char system_family[64];
    char system_sku[48];        /* the surface sku of the model. */
    char bios_vendor[64];
    char bios_version[64];
    char bios_date[16];         /* mm/dd/yyyy, as firmware reports it. */
    char system_serial[24];     /* of this installation, drawn once. */
    uint8_t system_uuid[16];    /* of this installation, drawn once. */
    uint32_t pad1;
    char host_volume_label[32]; /* name of the host root volume, the Z: label. */

    uint32_t reserved_count;
    /*
     * discard whole host memory blocks to avoid splitting them: 1 << memory_block_shift bytes, using 2 MiB
     * huge pages on Linux.
     */
    uint32_t memory_block_shift;
    MimeMemRange reserved[MIME_BOOT_MAX_RANGES];

    char system_root[64]; /* "C:\\Windows". */
    char cmdline[256];
    /* the host's swap: the guest has no paging file and reports RAM plus this as its commit limit. */
    uint64_t swap_size;
    /* the host's time zone: its IANA id, and the key of the Windows zone it is under Time Zones, empty when unknown. */
    char timezone_name[64];
    char timezone_key[64];
    /*
     * the host initializes the desktop during guest boot and sets this to 1 once display is ready. the window
     * manager waits before reading display.
     */
    uint32_t display_ready;
    uint32_t pad2;
    /* the locale of the user and the code pages that follow from it, set at setup, see host/locale.c. */
    uint32_t locale_lcid;
    uint16_t ansi_code_page;
    uint16_t oem_code_page;
    char locale_name[32];
} MimeBootInfo;

/*
 * embedded kernel, drivers and DLLs. files are page-aligned for direct guest mapping; DLLs use 4K file
 * alignment to match their memory layout.
 */
#define MIME_ROM_MAGIC 0x004D4F52454D494Dull /* "MIMEROM". */

typedef struct MimeRomEntry {
    char path[96]; /* relative to C:, e.g. \Windows\System32\ntdll.dll, NUL padded. */
    uint64_t offset; /* from the rom header, page aligned. */
    uint64_t size;
} MimeRomEntry;

typedef struct MimeRomHeader {
    uint64_t magic;
    uint32_t count;
    uint32_t built; /* when the build made it, in seconds since 1970: the time of every file in it. */
    MimeRomEntry entries[];
} MimeRomHeader;

/*
 * page shared by host and guest. irq_pending tells the guest why the host
 * raised irq_intid; the guest clears the bits it services.
 */
#define MIME_IRQ_CTRL (1u << 0)
#define MIME_IRQ_INPUT (1u << 1)
#define MIME_IRQ_DISPLAY (1u << 2)
#define MIME_IRQ_GPU (1u << 3)
#define MIME_IRQ_NET (1u << 4)
#define MIME_IRQ_DUMP (1u << 5) /* log the guest's threads, on the host's SIGUSR1. */
#define MIME_IRQ_FS (1u << 6)   /* the host changed files in a directory the guest watches: MIME_HC_FS_CHANGES. */
#define MIME_IRQ_WINDOW (1u << 7) /* pages of a file window are gone, the file was shortened: MIME_HC_FS_WINDOW_LOST. */
#define MIME_IRQ_MEMORY (1u << 8) /* the host's memory pressure changed, see MimeSharedPage.memory_pressure. */

/* how short of memory the host is, in MimeSharedPage.memory_pressure. */
enum {
    MIME_MEMORY_NORMAL = 0,
    MIME_MEMORY_LOW = 1,      /* the guest gives back what it caches and does not need. */
    MIME_MEMORY_CRITICAL = 2, /* the host is close to swapping or killing: the guest gives back everything it can. */
};

typedef struct MimeSharedPage {
    volatile uint32_t irq_pending;
    volatile uint32_t guest_ready;
    volatile uint64_t host_filetime; /* refreshed by the host. */
    volatile uint32_t memory_pressure; /* MIME_MEMORY_*, set before MIME_IRQ_MEMORY is raised. */
    volatile uint32_t memory_serial;   /* counts the host's requests to trim, so the guest tells a new one from an old one. */
} MimeSharedPage;

/* one run of free guest pages in a MIME_HC_MEM_REPORT list. */
typedef struct MimeReportRun {
    uint64_t pa;
    uint64_t bytes;
} MimeReportRun;

/* hypercalls. */
enum {
    MIME_HC_LOG = 0x01,         /* (pa, len) */
    MIME_HC_PANIC = 0x02,       /* (pa, len) */
    MIME_HC_HALT = 0x04,        /* (exit_code) machine powered off */
    MIME_HC_CTRL_RECV = 0x05,   /* (pa, capacity) -> length, 0 when empty */
    MIME_HC_CTRL_SEND = 0x06,   /* (pa, len) */
    MIME_HC_INPUT_RECV = 0x07,  /* (pa, max_events) -> count */
    MIME_HC_TIME = 0x08,        /* () -> filetime */
    MIME_HC_CPU_START = 0x09,   /* (CPU, pc, x0, sp) starts a secondary processor at el1 with interrupts masked */
    MIME_HC_LOCK_WAIT = 0x0A,   /* (pa, ticket) sleeps while the ticket lock word at pa does not serve ticket */
    MIME_HC_LOCK_WAKE = 0x0B,   /* (pa) wakes the processors sleeping on the lock word at pa */
    MIME_HC_MEM_REPORT = 0x0C,  /* (pa of MimeReportRun[], count) -> bytes released: free guest RAM the host may drop, many runs in one call. They read as zeros until written. */

    MIME_HC_FS_OPEN = 0x10,     /* (MimeFsOpen pa) -> handle */
    MIME_HC_FS_CLOSE = 0x11,    /* (handle) */
    MIME_HC_FS_READ = 0x12,     /* (handle, offset, pa, len) -> bytes */
    MIME_HC_FS_WRITE = 0x13,    /* (handle, offset, pa, len) -> bytes */
    MIME_HC_FS_QUERY = 0x14,    /* (handle, MimeFsInfo pa) */
    MIME_HC_FS_READDIR = 0x15,  /* (handle, index, MimeFsDirEntry pa, count) -> entries written, 0 at end; no count is a count of one */
    MIME_HC_FS_SET_SIZE = 0x16, /* (handle, size) */
    MIME_HC_FS_DELETE = 0x17,   /* (handle) delete on close */
    MIME_HC_FS_MAP = 0x18,      /* handle and flags: the guest physical address of the whole file, filled on touch; read only, or with MIME_FS_MAP_WRITABLE shared and writable, so the guest's writes are the file's. */
    MIME_HC_FS_UNMAP = 0x19,    /* (pa) */
    MIME_HC_MEM_DISCARD = 0x1A, /* (pa, size) free guest RAM whose contents the host may drop */
    MIME_HC_FS_VOLUME_QUERY = 0x1C, /* volume id and volume information address. */
    MIME_HC_FS_FLUSH = 0x1B,    /* flush a host file handle. */
    MIME_HC_FS_WATCH = 0x63,    /* directory handle, id, filter and tree: watches the changes the host makes below a directory, UNSUPPORTED where it cannot. */
    MIME_HC_FS_UNWATCH = 0x64,  /* the watch to end. */
    MIME_HC_FS_CHANGES = 0x65,  /* pa and capacity: the bytes of MimeFsChange records written, 0 once the queue is empty. */
    MIME_HC_FS_SYNC = 0x62,     /* () writes the host files of the drive out to the disk, for a shutdown */
    MIME_HC_MEM_ADVISE = 0x1D,  /* (pa, size, MIME_MEM_*) how the guest uses a range of RAM */
    MIME_HC_FS_GET_SD = 0x1E,   /* (handle, pa, capacity, up) -> distance << 32 | length of the nearest stored descriptor */
    MIME_HC_FS_SET_SD = 0x1F,   /* (handle, pa, length) stores a security descriptor, none for length 0 */
    MIME_HC_FS_WARM = 0x61,     /* (pa) maps a file window into the guest's tables ahead of its first reads, without waiting for it */
    MIME_HC_FS_RENAME = 0x60,   /* (handle, directory handle, pa, chars, MIME_FS_RENAME_*) moves a file or directory to a name in a directory */
    MIME_HC_FS_SET_BASIC = 0x66, /* (handle, MimeFsBasic pa) sets the times and the read only attribute the mask names, see MimeFsBasic */
    MIME_HC_FS_LINK = 0x67,     /* handle, directory handle or 0 for the file's own, pa, chars and MIME_FS_LINK_* flags: makes a hard link to the file in a directory, returns the links the file has now. */
    MIME_HC_FS_APPEND = 0x6C,   /* handle, unused, pa and len: writes at the end of the file in one step that no other writer can get in the middle of, host programs that append included; returns the offset it was written at, the whole length or nothing is written. */
    MIME_HC_FS_LINKS = 0x68,    /* handle, pa and capacity: every name of the file on its volume as MimeFsLinkName records, returns the bytes they need. */
    MIME_HC_FS_SET_REPARSE = 0x69, /* handle and MimeFsReparse pa: makes the empty file or directory a symbolic link or a junction. */
    MIME_HC_FS_GET_REPARSE = 0x6A, /* handle and MimeFsReparse pa: what the link the handle was opened as says, MIME_HC_STATUS_NOT_REPARSE for any other. */
    MIME_HC_FS_DELETE_REPARSE = 0x6B, /* handle and the tag the guest expects: the link becomes the empty file or directory it was made from. */
    MIME_HC_FS_WINDOW_LOST = 0xA0, /* (pa of MimeWindowLost) the next range of window pages whose file lost them, after MIME_IRQ_WINDOW */

    MIME_HC_WIN_CREATE = 0x20,  /* (window_id) */
    MIME_HC_WIN_DESTROY = 0x21, /* (window_id) */
    MIME_HC_WIN_CONFIG = 0x22,  /* (MimeWinConfig pa) */
    MIME_HC_WIN_PRESENT = 0x23, /* (MimeWinPresent pa) */
    MIME_HC_WIN_CURSOR = 0x24,  /* (MIME_CURSOR_*) */
    MIME_HC_BEEP = 0x25,        /* (type) */
    MIME_HC_CLIP_SET = 0x26,    /* (pa, len, offset, total) UTF-16LE text in chunks, committed at the last byte */
    MIME_HC_CLIP_GET = 0x27,    /* (pa, capacity, offset) -> total bytes of the host UTF-16LE text */
    MIME_HC_GPU_OPEN = 0x28,    /* (pa, size) -> channel, see mime/gpu.h */
    MIME_HC_GPU_BLOCK = 0x29,   /* (channel, index, pa, size) adds a heap block */
    MIME_HC_GPU_KICK = 0x2A,    /* (channel) new commands while the host is idle */
    MIME_HC_GPU_CLOSE = 0x2B,   /* (channel) returns once the host and the GPU no longer use its memory */
    MIME_HC_WIN_ICON = 0x2C,    /* window icon pixels described by MimeWinIcon. */
    MIME_HC_WIN_READBACK = 0x43, /* (MimeWinReadback pa) the last image presented to a window through Vulkan */
    MIME_HC_VK_OPEN = 0x2D,     /* (pa, size) -> connection, see mime/mvk_wire.h */
    MIME_HC_VK_KICK = 0x2E,     /* (connection) new calls while the host is idle */
    MIME_HC_VK_CLOSE = 0x2F,    /* (connection) returns once the host and the GPU no longer use its memory */
    MIME_HC_GFX_SHARE_FREE = 0x70, /* (id) the last handle of a shared Direct3D resource or fence went: the host drops what it kept of it for other processes */

    /* host sockets, failing with MIME_HC_STATUS_NET_ERROR and a winsock error as the value. */
    MIME_HC_NET_SOCKET = 0x30,     /* (family, type, protocol) -> socket */
    MIME_HC_NET_CLOSE = 0x31,      /* (socket) */
    MIME_HC_NET_BIND = 0x32,       /* (socket, MimeNetAddr pa) */
    MIME_HC_NET_LISTEN = 0x33,     /* (socket, backlog) */
    MIME_HC_NET_CONNECT = 0x34,    /* (socket, MimeNetAddr pa) WSAEWOULDBLOCK while it goes on */
    MIME_HC_NET_ACCEPT = 0x35,     /* (socket, MimeNetAddr pa) -> socket of the connection */
    MIME_HC_NET_SEND = 0x36,       /* (socket, pa, len, MSG_*, MimeNetAddr pa or 0) -> bytes */
    MIME_HC_NET_RECV = 0x37,       /* (socket, pa, len, MSG_*, MimeNetAddr pa or 0) -> bytes */
    MIME_HC_NET_SHUTDOWN = 0x38,   /* (socket, SD_*) */
    MIME_HC_NET_NAME = 0x39,       /* (socket, peer, MimeNetAddr pa) */
    MIME_HC_NET_OPTION = 0x3A,     /* (socket, MIME_NET_OPT_*, set, value) -> value */
    MIME_HC_NET_POLL = 0x3B,       /* (MimeNetPoll pa, count, arm) -> ready count, then watches the asked events */
    MIME_HC_NET_EVENTS = 0x3C,     /* (pa, max) -> count of uint32 sockets whose watch fired, or resolve ids */
    MIME_HC_NET_RESOLVE = 0x3D,    /* (pa, len, family) UTF-8 host name -> id, MIME_NET_RESOLVED | id fires */
    MIME_HC_NET_ANSWER = 0x3E,     /* (id, MimeNetAddr pa, max) -> count, frees the answer */
    MIME_HC_NET_INTERFACES = 0x3F, /* (MimeNetInterface pa, max) -> count, one per address */

    /* audio output streams, see mime/audio.h. */
    MIME_HC_AUDIO_OPEN = 0x40,  /* (pa, size) -> stream, plays the format of the header at pa */
    MIME_HC_AUDIO_START = 0x41, /* (stream, running) */
    MIME_HC_AUDIO_CLOSE = 0x42, /* (stream) returns once the host no longer reads its memory */

    MIME_HC_POINTER = 0x44,     /* (MIME_POINTER_*, window_id, left, top, right, bottom) screen rectangle */
    MIME_HC_PAD_RUMBLE = 0x45,  /* (pad, low frequency motor, high frequency motor) from 0 to 65535 */
    MIME_HC_GAMMA = 0x46,       /* (pa of three channels of 256 values, red first, or 0 for the identity) */
    MIME_HC_NET_CONFIG = 0x47,  /* (MimeNetConfig pa, size) -> bytes written, as many entries as fit */

    /* the clipboard in every format the host and the guest can exchange, see MIME_CLIP_*. */
    MIME_HC_CLIP_OFFER = 0x48,   /* (MIME_CLIP_* mask) the guest owns the clipboard and can render these, none to withdraw */
    MIME_HC_CLIP_FETCH = 0x49,   /* (MIME_CLIP_* kind, pa, capacity, offset, generation) -> total size of the host's data */
    MIME_HC_CLIP_PROVIDE = 0x4A, /* (request, pa, length, offset, total) answers MIME_INPUT_CLIP_REQUEST in chunks */

    /* drag and drop between the host's programs and the guest's, see MIME_INPUT_DND_*. */
    MIME_HC_DND_EFFECT = 0x4B,   /* (session, DROPEFFECT_*) what the target under a host drag does with it */
    MIME_HC_DND_FETCH = 0x4C,    /* (session, MIME_CLIP_* kind, pa, capacity, offset) -> total size of a host drag's data */
    MIME_HC_DND_FINISH = 0x4D,   /* (session, DROPEFFECT_*) the drop has been handled, with its effect */
    MIME_HC_DND_START = 0x4E,    /* (MIME_CLIP_* kinds, DROPEFFECT_* allowed, 0, icon pa, icon bytes) -> session: a drag of the guest leaves its windows; the icon is a MimeDragIcon the host copies before returning, 0 for none */
    MIME_HC_DND_STOP = 0x4F,     /* (session) the guest ends the drag it started */

    /* input methods, see MIME_INPUT_IME. */
    MIME_HC_IME_TEXT = 0x50,     /* (pa, capacity) -> UTF-16 units of the oldest queued preedit or commit text, copied to pa and dropped */
    MIME_HC_IME_AREA = 0x51,     /* (window, left, top, right, bottom, cursor x) where text is entered, window relative, for the input method's popups */
    MIME_HC_IME_CONTROL = 0x52,  /* (MIME_IME_*, window) */

    /* the host's monitors. */
    MIME_HC_MONITORS = 0x53,     /* (pa, capacity) -> count: fills up to capacity MimeMonitor, the primary first */

    /* the compositor moves or sizes a window with the pointer, the button of a press still held. */
    MIME_HC_WIN_MOVESIZE = 0x54, /* (window_id, edge) edge 0 moves, 1 to 8 are the WMSZ_* sizing edges; OK when the compositor took over */

    /* the notification area and notifications of the desktop. */
    MIME_HC_TRAY = 0x55,         /* (pa, size) a MimeTrayCommand */
    MIME_HC_WIN_LAYER = 0x56,    /* (MimeWinLayer pa) where a window that presents lies now, so its layer follows it between presents */

    /* the desktop's shell: toast notifications, application identity and the taskbar. see MimeShell*. */
    MIME_HC_SHELL = 0x80,        /* (pa, size) a MimeShell* command, whose first word is MIME_SHELL_* */

    /* cameras, see mime/camera.h. */
    MIME_HC_CAMERA_LIST = 0x90,    /* (pa, capacity) -> count of the host's cameras, fills up to capacity MimeCameraInfo */
    MIME_HC_CAMERA_FORMATS = 0x91, /* (camera, pa, capacity) -> count of the camera's formats, fills up to capacity MimeCameraFormat */
    MIME_HC_CAMERA_OPEN = 0x92,    /* (camera, pa, size) -> stream, runs the mode the MimeCameraHeader at pa asks for */
    MIME_HC_CAMERA_START = 0x93,   /* (stream, running) */
    MIME_HC_CAMERA_CLOSE = 0x94,   /* (stream) returns once the host no longer uses the memory */
    MIME_HC_CAMERA_CONTROL = 0x95, /* (pa) a MimeCameraControl, answered in place */
};

/*
 * MIME_HC_DND_START points to a MimeDragIcon followed by width * height pixels. the hotspot lies under the
 * pointer. pixels are 32-bit premultiplied ARGB8888, top row first, as in Wayland buffers.
 */
typedef struct MimeDragIcon {
    uint32_t width, height;
    int32_t hot_x, hot_y;
    uint32_t pixels[];
} MimeDragIcon;
#define MIME_DRAG_ICON_MAX 512 /* pixels each way. */

/* MIME_HC_IME_CONTROL probes host protocol support with 1 or 0, or ends composition. */
enum {
    MIME_IME_QUERY = 1,
    MIME_IME_CANCEL = 2,
};

/* what the host pointer does over a window: as usual, kept in a rectangle, or locked to report motion alone. */
enum {
    MIME_POINTER_FREE = 0,
    MIME_POINTER_CONFINED = 1,
    MIME_POINTER_RELATIVE = 2,
};

enum {
    MIME_MEM_SPARSE = 1, /* touched here and there, such as the pfn database: back it in small pages. */
};

enum {
    MIME_HC_STATUS_OK = 0,
    MIME_HC_STATUS_INVALID = 1,
    MIME_HC_STATUS_NOT_FOUND = 2,
    MIME_HC_STATUS_ACCESS_DENIED = 3,
    MIME_HC_STATUS_EXISTS = 4,
    MIME_HC_STATUS_IO_ERROR = 5,
    MIME_HC_STATUS_NOT_DIRECTORY = 6,
    MIME_HC_STATUS_IS_DIRECTORY = 7,
    MIME_HC_STATUS_NO_MEMORY = 8,
    MIME_HC_STATUS_NOT_EMPTY = 9,
    MIME_HC_STATUS_UNSUPPORTED = 10,
    MIME_HC_STATUS_NET_ERROR = 11,
    MIME_HC_STATUS_CROSS_DEVICE = 12,   /* the two names are on different host file systems. */
    MIME_HC_STATUS_TOO_MANY_LINKS = 13, /* the file has as many names as a volume allows. */
    MIME_HC_STATUS_REPARSE = 14,        /* a component of the path is a link, see the reparse fields of MimeFsOpen. */
    MIME_HC_STATUS_NOT_REPARSE = 15,    /* the file is not a link. */
    MIME_HC_STATUS_TAG_MISMATCH = 16,   /* the link has another tag than the one asked about. */
};

/*
 * networking. the host keeps non blocking sockets; the guest arms a watch
 * on what would block and the host raises MIME_IRQ_NET once it fired.
 */
typedef struct MimeNetAddr {
    uint16_t family; /* a windows sockaddr_in or sockaddr_in6, AF_INET 2 or AF_INET6 23. */
    uint8_t data[26];
} MimeNetAddr;

enum {
    MIME_NET_READ = 1u << 0,    /* data, a datagram or a connection to accept. */
    MIME_NET_WRITE = 1u << 1,   /* room to send, or a connect that finished. */
    MIME_NET_HUP = 1u << 2,     /* the stream ended and nothing is left to read. */
    MIME_NET_ERROR = 1u << 3,   /* the pending socket error, taken into error. */
    MIME_NET_INVALID = 1u << 4, /* no such socket. */
};

typedef struct MimeNetPoll {
    uint32_t socket;
    uint16_t events; /* MIME_NET_READ and MIME_NET_WRITE to count and watch, revents has every state. */
    uint16_t revents;
    int32_t error;
} MimeNetPoll;

enum {
    MIME_NET_OPT_REUSEADDR = 1,
    MIME_NET_OPT_KEEPALIVE = 2,
    MIME_NET_OPT_NODELAY = 3,
    MIME_NET_OPT_SNDBUF = 4,
    MIME_NET_OPT_RCVBUF = 5,
    MIME_NET_OPT_BROADCAST = 6,
    MIME_NET_OPT_V6ONLY = 7,
    MIME_NET_OPT_LINGER = 8, /* on in bit 16, seconds below. */
    MIME_NET_OPT_ERROR = 9,  /* read and clears the pending error. */
    MIME_NET_OPT_OOBINLINE = 10,
    MIME_NET_OPT_TTL = 11,
    MIME_NET_OPT_NREAD = 12, /* bytes waiting, the size of the next datagram. */
    MIME_NET_OPT_KEEPIDLE = 13,
    MIME_NET_OPT_KEEPINTVL = 14,
    MIME_NET_OPT_KEEPCNT = 15,
    MIME_NET_OPT_ATMARK = 16,
};

#define MIME_NET_RESOLVED 0x80000000u

enum {
    MIME_NET_IF_UP = 1u << 0,
    MIME_NET_IF_LOOPBACK = 1u << 1,
    MIME_NET_IF_BROADCAST = 1u << 2,
    MIME_NET_IF_MULTICAST = 1u << 3,
    MIME_NET_IF_POINTTOPOINT = 1u << 4,
    MIME_NET_IF_RUNNING = 1u << 5, /* the link is up: a carrier, or a virtual link that is ready. */
    MIME_NET_IF_VIRTUAL = 1u << 6, /* no hardware behind it, such as a bridge, a tunnel or a veth. */
};

typedef struct MimeNetInterface {
    char name[16];
    uint32_t index;
    uint32_t flags;
    uint32_t mtu;
    uint8_t prefix; /* bits of the network part of addr. */
    uint8_t mac_length;
    uint8_t mac[8];
    uint8_t pad[2];
    MimeNetAddr addr;
} MimeNetInterface;

/*
 * host links, addresses, gateway routes, DNS servers and search domains for Windows adapters. arrays follow
 * the header in this order.
 */
enum {
    MIME_NET_LINK_OTHER = 0,
    MIME_NET_LINK_ETHERNET = 1,
    MIME_NET_LINK_WIRELESS = 2,
    MIME_NET_LINK_LOOPBACK = 3,
    MIME_NET_LINK_TUNNEL = 4, /* an IP tunnel without link layer, such as wireguard or a tun device. */
    MIME_NET_LINK_PPP = 5,
};

typedef struct MimeNetLink {
    char name[16];
    uint32_t index;
    uint32_t flags; /* MIME_NET_IF_* */
    uint32_t mtu;
    uint32_t type;  /* MIME_NET_LINK_* */
    uint64_t speed; /* bits per second, 0 when unknown. */
    uint8_t mac_length;
    uint8_t mac[8];
    uint8_t pad[7];
} MimeNetLink;

enum {
    MIME_NET_ADDR_DYNAMIC = 1u << 0,   /* leased by DHCP or configured from router advertisements. */
    MIME_NET_ADDR_TEMPORARY = 1u << 1, /* an IPv6 privacy address. */
    MIME_NET_ADDR_DEPRECATED = 1u << 2,
    MIME_NET_ADDR_TENTATIVE = 1u << 3,
};

typedef struct MimeNetAddress {
    uint32_t index; /* of the link. */
    uint32_t flags; /* MIME_NET_ADDR_* */
    uint32_t valid; /* seconds left, UINT32_MAX for ever. */
    uint32_t preferred;
    uint8_t prefix;
    uint8_t pad[3];
    MimeNetAddr addr;
} MimeNetAddress;

enum {
    MIME_NET_ROUTE_STATIC = 0,
    MIME_NET_ROUTE_DHCP = 1,
    MIME_NET_ROUTE_RA = 2, /* learned from router advertisements. */
};

typedef struct MimeNetRoute {
    uint32_t index;  /* of the link. */
    uint32_t metric; /* the host's, 0 when it has none. */
    uint8_t prefix;  /* 0 for a default route. */
    uint8_t origin;  /* MIME_NET_ROUTE_* */
    uint8_t pad[2];
    MimeNetAddr destination;
    MimeNetAddr gateway;
} MimeNetRoute;

#define MIME_NET_MAX_DNS 8

typedef struct MimeNetConfig {
    uint32_t size; /* bytes written, this header and the arrays. */
    uint32_t links;
    uint32_t addresses;
    uint32_t routes;
    uint32_t servers;
    uint32_t pad;
    char domain[256]; /* UTF-8, the resolver's domain. */
    char search[256]; /* the search domains, separated by spaces. */
    MimeNetAddr server[MIME_NET_MAX_DNS];
    /* MimeNetLink link[links], MimeNetAddress address[addresses], MimeNetRoute route[routes]. */
} MimeNetConfig;

/*
 * control channel. the host queues messages and raises MIME_IRQ_CTRL, the
 * guest pulls them with MIME_HC_CTRL_RECV. strings are utf-16le.
 */
enum {
    MIME_CTRL_RUN = 1,
    MIME_CTRL_SHUTDOWN = 2,
    MIME_CTRL_CONSOLE_INPUT = 3, /* MimeCtrlConsole: bytes typed or piped in, none for end of file. */
    MIME_CTRL_CONSOLE_EVENT = 4, /* MimeCtrlConsole: value is CTRL_C_EVENT, CTRL_BREAK_EVENT or CTRL_CLOSE_EVENT. */
    MIME_CTRL_CONSOLE_SIZE = 5,  /* MimeCtrlConsole: value is columns | rows << 16. */

    MIME_EVT_READY = 100,
    MIME_EVT_PROCESS_STARTED = 101, /* MimeEvtProcess: status is the image subsystem. */
    MIME_EVT_PROCESS_EXIT = 102,
    MIME_EVT_RUN_FAILED = 103,
    MIME_EVT_DEBUG_OUTPUT = 104,
    MIME_EVT_CONSOLE_OUTPUT = 105, /* MimeCtrlConsole: value is 1 for stdout, 2 for stderr. */
    MIME_EVT_CONSOLE_MODE = 106,   /* MimeCtrlConsole: value is the console input mode. */
    MIME_EVT_CONSOLE_READ = 107,   /* MimeCtrlConsole: value is input bytes the console took, to send more. */
};

enum {
    MIME_RUN_TTY_INPUT = 1u << 0,  /* the terminal is the input, not a pipe or file. */
    MIME_RUN_TTY_OUTPUT = 1u << 1, /* and the standard output. */
    MIME_RUN_TTY_ERROR = 1u << 2,  /* and the standard error. */
};

/*
 * manifest execution level in flag bits 4 and 5: 0 absent, 1 asInvoker, 2 highestAvailable, 3
 * requireAdministrator.
 */
#define MIME_RUN_LEVEL_SHIFT 4
#define MIME_RUN_LEVEL_MASK (3u << MIME_RUN_LEVEL_SHIFT)

/* subsystems MIME_EVT_PROCESS_STARTED reports, as in the image header. */
#define MIME_SUBSYSTEM_GUI 2
#define MIME_SUBSYSTEM_CUI 3

/* the guest console takes at most this much unread input. */
#define MIME_CONSOLE_INPUT_WINDOW 65536

typedef struct MimeCtrlHeader {
    uint32_t type;
    uint32_t size; /* total size including this header. */
} MimeCtrlHeader;

/*
 * MIME_CTRL_SHUTDOWN gives programs timeout_ms total to exit, then terminates any remaining programs. a
 * header-only request from older hosts uses defaults.
 */
enum {
    MIME_SHUTDOWN_FORCE = 1u << 0, /* nothing is asked: every program ends at once. */
};
#define MIME_SHUTDOWN_DEFAULT_MS 30000
typedef struct MimeCtrlShutdown {
    MimeCtrlHeader header;
    uint32_t flags;      /* MIME_SHUTDOWN_... */
    uint32_t timeout_ms; /* 0 for the default. */
} MimeCtrlShutdown;

typedef struct MimeCtrlRun {
    MimeCtrlHeader header;
    uint64_t request_id;
    uint32_t image_chars;  /* dos path of the image, e.g. Z:\Users\me\a.exe. */
    uint32_t cmdline_chars;
    uint32_t cwd_chars;
    uint32_t flags;        /* MIME_RUN_... */
    uint32_t console_size;   /* columns | rows << 16 of the terminal, 0 when unknown. */
    uint32_t console_cursor; /* the cursor of the terminal, column + 1 | row + 1 << 16, 0 when unknown. */
    /* uint16_t image[image_chars], cmdline[cmdline_chars], cwd[cwd_chars]. */
} MimeCtrlRun;

typedef struct MimeEvtProcess {
    MimeCtrlHeader header;
    uint64_t request_id;
    uint32_t status; /* exit code or ntstatus for MIME_EVT_RUN_FAILED. */
    uint32_t process_id;
} MimeEvtProcess;

typedef struct MimeEvtDebugOutput {
    MimeCtrlHeader header;
    uint64_t request_id;
    uint32_t process_id;
    uint32_t length; /* UTF-8 bytes. */
    /* char text[length]. */
} MimeEvtDebugOutput;

typedef struct MimeCtrlConsole {
    MimeCtrlHeader header;
    uint64_t request_id;
    uint32_t value;
    uint32_t length;
    /* uint8_t data[length]. */
} MimeCtrlConsole;

#define MIME_CTRL_MAX_MESSAGE 8192

/*
 * file system channel. the host exposes volumes; paths are UTF-16LE,
 * backslash separated and relative to the volume root.
 */
enum {
    MIME_FS_VOLUME_SYSTEM = 0, /* C:, the drive_c directory. */
    MIME_FS_VOLUME_HOST = 1,   /* Z:, the host root. */
};

enum {
    MIME_FS_OPEN_READ = 1u << 0,
    MIME_FS_OPEN_WRITE = 1u << 1,
    MIME_FS_OPEN_DIRECTORY = 1u << 2,     /* must be a directory. */
    MIME_FS_OPEN_NON_DIRECTORY = 1u << 3, /* must not be a directory. */
    MIME_FS_OPEN_STAT_ONLY = 1u << 4,     /* returns info_out without creating a handle to close. */
    MIME_FS_OPEN_WANT_SD = 1u << 5,       /* also the nearest stored security descriptor, see sd_length: for a guest that checks the open. */
    MIME_FS_OPEN_NO_ACCESS_TIME = 1u << 7, /* reads leave the access time alone: NtfsDisableLastAccessUpdate. */
    MIME_FS_OPEN_REPARSE_POINT = 1u << 8, /* a link as the last component is opened itself and not followed. */
    MIME_FS_OPEN_BY_ID = 1u << 6,         /* path holds a file id, 8 bytes or 16 with the high half 0, and comes back as the file's volume relative name. */
};

enum {
    MIME_FS_DISP_OPEN = 0,      /* FILE_OPEN */
    MIME_FS_DISP_CREATE = 1,    /* FILE_CREATE */
    MIME_FS_DISP_OPEN_IF = 2,   /* FILE_OPEN_IF */
    MIME_FS_DISP_OVERWRITE = 3, /* FILE_OVERWRITE */
    MIME_FS_DISP_OVERWRITE_IF = 4,
    MIME_FS_DISP_SUPERSEDE = 5,
};

/*
 * MIME_HC_FS_CHANGES uses FILE_ACTION_* plus MIME_FS_CHANGE_OVERFLOW, which has no name and reports lost
 * changes.
 */
enum {
    MIME_FS_CHANGE_ADDED = 1,
    MIME_FS_CHANGE_REMOVED = 2,
    MIME_FS_CHANGE_MODIFIED = 3,
    MIME_FS_CHANGE_RENAMED_OLD = 4,
    MIME_FS_CHANGE_RENAMED_NEW = 5,
    MIME_FS_CHANGE_OVERFLOW = 0x100,
};

/* higher watch ids belong to the guest attribute cache; these also report directory removal. */
#define MIME_FS_WATCH_ID_ATTRIBUTES 0x40000000u

typedef struct MimeFsChange {
    uint32_t size; /* of the record, a multiple of 4. */
    uint32_t watch;
    uint32_t action;
    uint32_t name_chars;
    uint16_t name[]; /* below the watched directory, with backslashes. */
} MimeFsChange;

/*
 * when a host file shrinks, notify the guest before replacing lost window pages. it removes mappings so
 * programs get errors rather than zeros. each request returns the next lost range and acknowledges the
 * previous serial.
 */
typedef struct MimeWindowLost {
    uint64_t acked;  /* in: the serial of the range dealt with, 0 for none. */
    uint64_t serial; /* out: of the range below, 0 when there is none. */
    uint64_t ipa;    /* out: guest physical address of its first page. */
    uint64_t size;   /* out: bytes, a whole number of pages. */
} MimeWindowLost;

/* MIME_HC_FS_MAP flags. a writable window needs a handle opened for writing, and is UNSUPPORTED where the host fills windows itself. */
#define MIME_FS_MAP_WRITABLE 1u

/* MIME_HC_FS_RENAME flags. */
#define MIME_FS_RENAME_REPLACE 1u /* an existing file of the new name is replaced, a directory never is. */

/* MIME_HC_FS_LINK flags. */
#define MIME_FS_LINK_REPLACE 1u          /* an existing file of the new name is replaced, a non empty directory is not and an empty one neither. */
#define MIME_FS_LINK_IGNORE_READONLY 2u  /* a read only file may be replaced. */

/* most names one file can have, as on NTFS. */
#define MIME_FS_MAX_LINKS 1024u

/* what MIME_HC_FS_LINKS writes: one record per name, each size bytes long, a multiple of 8. */
typedef struct MimeFsLinkName {
    uint32_t size;
    uint32_t name_chars;
    uint64_t parent_id; /* file id of the directory the name is in. */
    uint16_t name[];    /* the last component, not terminated. */
} MimeFsLinkName;

typedef struct MimeFsInfo {
    uint64_t size;
    uint64_t allocation_size;
    uint64_t creation_time; /* filetime */
    uint64_t access_time;
    uint64_t write_time;
    uint64_t change_time;
    uint64_t file_id;
    uint32_t attributes; /* FILE_ATTRIBUTE_* */
    uint32_t links;
    uint32_t reparse_tag; /* of a link, else 0. */
    uint32_t pad;
} MimeFsInfo;

/*
 * a link as the host keeps it. the host stores a symbolic link or a junction as a symbolic link of its own, so that the
 * programs of the host see the drive as it is: a file link is the target, a directory link has a slash after it and a
 * junction has a slash and a dot. a target the host cannot reach is kept as the name the guest gave. fsreparse.c.
 *
 * names holds the substitute name and then the print name, in UTF-16 without terminators. flags is SYMLINK_FLAG_RELATIVE
 * for a symbolic link with a relative target. directory is set for a link to a directory and for a junction.
 */
#define MIME_REPARSE_TAG_MOUNT_POINT 0xA0000003u
#define MIME_REPARSE_TAG_SYMLINK 0xA000000Cu
#define MIME_REPARSE_MAX_NAME 520

typedef struct MimeFsReparse {
    uint32_t tag;
    uint32_t flags;
    uint32_t directory;
    uint32_t substitute_chars;
    uint32_t print_chars;
    uint32_t pad;
    uint16_t names[MIME_REPARSE_MAX_NAME];
} MimeFsReparse;

/*
 * MIME_HC_FS_SET_BASIC sets the times the mask names and the read-only attribute. a change time given is kept as it is;
 * otherwise any change the call makes sets it to now, unless the handle stopped its automatic updates.
 */
#define MIME_FS_BASIC_ACCESS 1u
#define MIME_FS_BASIC_WRITE 2u
#define MIME_FS_BASIC_READONLY 4u /* attributes has FILE_ATTRIBUTE_READONLY or not. */
#define MIME_FS_BASIC_CREATION 8u
#define MIME_FS_BASIC_CHANGE 16u
#define MIME_FS_BASIC_TOUCH 32u /* a read: the access time becomes now, unless the handle keeps it or the open said not to. */

/* the times a handle can stop updating on its own, in MimeFsBasic freeze and thaw: a time set or given as -1 is frozen, -2 thaws it. */
#define MIME_FS_TIME_WRITE 1u
#define MIME_FS_TIME_ACCESS 2u
#define MIME_FS_TIME_CHANGE 4u

typedef struct MimeFsBasic {
    uint64_t creation_time;
    uint64_t access_time;
    uint64_t write_time;
    uint64_t change_time;
    uint32_t attributes;
    uint32_t mask;
    uint32_t freeze;
    uint32_t thaw;
} MimeFsBasic;

typedef struct MimeFsOpen {
    uint32_t volume;
    uint32_t flags;
    uint32_t disposition;
    uint32_t path_chars;
    uint64_t handle_out;
    uint32_t action_out; /* FILE_OPENED, FILE_CREATED... */
    /*
     * successful MIME_FS_OPEN_WANT_SD opens return the nearest stored descriptor, as MIME_HC_FS_GET_SD would.
     * sd_length is 0 if absent; sd_data holds bytes if they fit, and sd_distance counts ancestor levels.
     * non-OK sd_status means reading failed and the guest retries with MIME_HC_FS_GET_SD.
     */
    uint32_t sd_length;
    MimeFsInfo info_out; /* what MIME_HC_FS_QUERY gives for the file just opened, so that an open is one exit. */
    uint32_t sd_distance;
    uint32_t sd_status;
    uint8_t sd_data[512];
    /*
     * MIME_HC_STATUS_REPARSE: path_chars of the path are through the link, path holds the substitute name of the link and
     * then its print name, reparse_flags is SYMLINK_FLAG_RELATIVE for a relative symbolic link.
     */
    uint32_t reparse_tag;
    uint32_t reparse_flags;
    uint32_t reparse_prefix;
    uint32_t reparse_substitute;
    uint16_t path[520];
} MimeFsOpen;

typedef struct MimeFsVolumeInfo {
    uint64_t total_units;
    uint64_t available_units;
    uint64_t free_units;
    uint32_t bytes_per_unit;
    uint32_t reserved;
} MimeFsVolumeInfo;

typedef struct MimeFsDirEntry {
    MimeFsInfo info;
    uint32_t name_chars;
    uint32_t pad;
    uint16_t name[256];
} MimeFsDirEntry;

/*
 * each visible top level window has a host window. the guest renders its pixels and frame into guest memory
 * and presents dirty rectangles.
 */
#define MIME_WIN_MAX_DRAG_RECTS 4

typedef struct MimeRect {
    int32_t left, top, right, bottom;
} MimeRect;

/*
 * monitors retain host compositor adjacency in guest pixels, with the primary at 0, 0. each device pixel is
 * one guest pixel, so DPI is 96 times the monitor scale.
 */
#define MIME_MAX_MONITORS 8
#define MIME_MONITOR_PRIMARY 1

typedef struct MimeMonitor {
    uint32_t flags; /* MIME_MONITOR_* */
    uint32_t dpi;
    uint32_t refresh_hz;
    uint32_t width_mm; /* 0 when unknown. */
    uint32_t height_mm;
    MimeRect rect; /* in the virtual screen. */
    MimeRect work; /* without panels and docks, in the virtual screen too. */
    uint16_t name[32]; /* the host's name for it, such as eDP-1. */
} MimeMonitor;

enum {
    MIME_WIN_VISIBLE = 1u << 0,
    MIME_WIN_TOPMOST = 1u << 1,
    MIME_WIN_ACTIVATE = 1u << 2, /* bring to front and take focus. */
    MIME_WIN_SHADOW = 1u << 3,
    MIME_WIN_TOOL = 1u << 4,     /* no activation, used for menus and tooltips. */
    MIME_WIN_MINIMIZED = 1u << 5,
    MIME_WIN_MAXIMIZED = 1u << 6,
    MIME_WIN_RESIZABLE = 1u << 7,
    MIME_WIN_APPWINDOW = 1u << 8,
    MIME_WIN_FULLSCREEN = 1u << 9,
    MIME_WIN_MINIMIZABLE = 1u << 10,
    MIME_WIN_MAXIMIZABLE = 1u << 11,
    MIME_WIN_ACTIVE = 1u << 12, /* the frame is drawn active. */
    MIME_WIN_NOINPUT = 1u << 13, /* takes no mouse input: a transparent window, such as a tooltip. */
};

typedef struct MimeWinConfig {
    uint32_t window_id;
    uint32_t flags;
    uint32_t state_request; /* last completed native window-state request. */
    MimeRect rect;    /* screen coordinates in guest pixels. */
    MimeRect visible; /* window relative part that is drawn opaque/visible. */
    MimeRect input;   /* window relative part that takes the mouse, the visible part plus any resize border. */
    uint32_t corner_radius;
    uint32_t border_color; /* 0xAARRGGBB 1 pixel border drawn by the host over the visible rect, 0 for none. */
    uint32_t drag_count;
    MimeRect drag[MIME_WIN_MAX_DRAG_RECTS]; /* window relative caption areas. */
    uint32_t owner_id; /* keeps owned windows above their owner, 0 if none. */
    uint32_t process_id;
    uint32_t dpi; /* guest pixel DPI: the host window uses this many guest pixels per 96 size units, or the display DPI when 0. */
    uint32_t surface_dpi; /* the DPI of the presented surface, 0 when it is the guest pixel DPI. a lower one is stretched to the window. */
    int32_t min_width, min_height; /* the smallest and largest size the window can be sized to, window rectangle in guest pixels, 0 for no limit. */
    int32_t max_width, max_height;
    uint32_t image_chars;
    uint16_t image_path[512];
    uint32_t title_chars;
    uint16_t title[256];
} MimeWinConfig;

typedef struct MimeWinPresent {
    uint32_t window_id;
    uint32_t width;  /* surface size, matches rect unless the window is stretched, see surface_dpi. */
    uint32_t height;
    uint32_t stride; /* bytes */
    uint64_t surface_phys; /* BGRA8 premultiplied, physically contiguous. */
    MimeRect dirty;
} MimeWinPresent;

/*
 * reads the last image a window of the guest presented into BGRA rows at pixels_phys if capacity permits, forcing
 * opaque alpha. window_id is the top level window and source the window that presented. returns width, height,
 * window-relative client rectangle and image serial. MIME_HC_STATUS_NOT_FOUND means no presented image.
 */
typedef struct MimeWinReadback {
    uint32_t window_id;
    uint32_t source; /* the window that presented, one image is kept for each. */
    uint32_t capacity_width, capacity_height;
    uint32_t stride;
    uint32_t pad;
    uint64_t pixels_phys;
    uint32_t width, height;
    MimeRect client;
    uint64_t serial; /* counts the images the window presented, so a capture can tell whether it changed. */
} MimeWinReadback;

/* where a window that presents lies in its top level window, window relative pixels of its own. */
typedef struct MimeWinLayer {
    uint32_t window_id; /* the top level window. */
    uint32_t source;    /* the window that presents. */
    uint32_t order;     /* its place in the paint order of the tree. */
    uint32_t pad;
    MimeRect client;
    MimeRect visible;   /* the part of the client area that shows, empty while it is hidden or covered. */
} MimeWinLayer;

typedef struct MimeWinIcon {
    uint32_t window_id;
    uint32_t width, height, stride;
    uint64_t pixels_phys;
} MimeWinIcon;

enum {
    MIME_CURSOR_ARROW = 0,
    MIME_CURSOR_IBEAM,
    MIME_CURSOR_WAIT,
    MIME_CURSOR_CROSS,
    MIME_CURSOR_HAND,
    MIME_CURSOR_SIZE_NS,
    MIME_CURSOR_SIZE_WE,
    MIME_CURSOR_SIZE_NWSE,
    MIME_CURSOR_SIZE_NESW,
    MIME_CURSOR_SIZE_ALL,
    MIME_CURSOR_NO,
    MIME_CURSOR_HIDDEN,
};

/* input events, pulled with MIME_HC_INPUT_RECV after MIME_IRQ_INPUT. */
enum {
    MIME_INPUT_MOUSE_MOVE = 1,
    MIME_INPUT_MOUSE_DOWN = 2,
    MIME_INPUT_MOUSE_UP = 3,
    MIME_INPUT_MOUSE_WHEEL = 4,
    MIME_INPUT_KEY_DOWN = 5,
    MIME_INPUT_KEY_UP = 6,
    MIME_INPUT_CHAR = 7,
    MIME_INPUT_WINDOW_MOVED = 8,   /* host moved the window: x, y. */
    MIME_INPUT_WINDOW_FOCUS = 9,   /* x = 1 gained, 0 lost. */
    MIME_INPUT_WINDOW_CLOSE = 10,  /* host close request, such as Command+W or Dock. */
    MIME_INPUT_MOUSE_LEAVE = 11,
    MIME_INPUT_CLIPBOARD = 12,     /* another host application changed the clipboard: x is the MIME_CLIP_* kinds it holds, y its generation. */
    MIME_INPUT_WINDOW_STATE = 13,  /* x is a windows show command. */
    MIME_INPUT_WINDOW_FULLSCREEN = 14, /* flags is 1 on entry, 0 on exit. */
    MIME_INPUT_DISPLAY = 15,       /* the boot information display or the monitors changed: width and height of the primary. */
    MIME_INPUT_PAD_ADDED = 16,     /* a game controller: pad, pad_vid, pad_pid. */
    MIME_INPUT_PAD_REMOVED = 17,   /* pad */
    MIME_INPUT_PAD_STATE = 18,     /* pad, pad_buttons, pad_axes. */
    MIME_INPUT_CLIP_REQUEST = 19,  /* another host program pastes: x is the MIME_CLIP_* kind, button the request id. */
    /*
     * host drag: x and y are guest screen pixels, flags lists MIME_CLIP_* formats, wheel lists allowed
     * DROPEFFECT_* values, modifiers holds keys and pad the session. answer enter/over with
     * MIME_HC_DND_EFFECT and drop with MIME_HC_DND_FINISH.
     */
    MIME_INPUT_DND_ENTER = 20,
    MIME_INPUT_DND_OVER = 21,
    MIME_INPUT_DND_LEAVE = 22,
    MIME_INPUT_DND_DROP = 23,
    MIME_INPUT_DND_REQUEST = 24,   /* a host program reads the data of the guest's drag: x is the kind, button the request id. */
    MIME_INPUT_DND_END = 25,       /* the guest's drag ended: pad is the session, flags the DROPEFFECT_*, x is 1 for a drop. */
    /*
     * input method composition: MIME_IME_PREEDIT uses x and y for the selection in UTF-16 units, or -1 for
     * none. fetch preedit or MIME_IME_COMMIT text with MIME_HC_IME_TEXT. MIME_IME_CANCELLED drops
     * composition; direct typing uses MIME_INPUT_CHAR.
     */
    MIME_INPUT_IME = 26,
    /* the compositor shows a window on another monitor: x is its index in MIME_HC_MONITORS' list. */
    MIME_INPUT_WINDOW_DISPLAY = 27,
    /* the compositor gave the window another size: width and height are those of the host window, the part of the window it covers, in guest pixels. */
    MIME_INPUT_WINDOW_SIZE = 28,
    /*
     * notification area event: pad is the guest icon id, flags the MIME_TRAY_EVENT_*, x and y the screen
     * position, ch the menu index or wheel step.
     */
    MIME_INPUT_TRAY = 29,
    /*
     * MIME_CAMERA_EVENT_CHANGED requests camera enumeration; MIME_CAMERA_EVENT_FRAME means stream x has
     * frames waiting.
     */
    MIME_INPUT_CAMERA = 30,
    /*
     * shell reply: flags is MIME_SHELL_EVENT_*, pad the toast id, x the action or reason, and y the available
     * reply text units.
     */
    MIME_INPUT_SHELL = 40,
};

enum {
    MIME_CAMERA_EVENT_CHANGED = 1,
    MIME_CAMERA_EVENT_FRAME = 2,
};

enum {
    MIME_IME_PREEDIT = 1,
    MIME_IME_COMMIT = 2,
    MIME_IME_CANCELLED = 3,
};

/*
 * Xbox 360 controller state: XINPUT_GAMEPAD button bits, sticks from -32768 to 32767 with positive Y upward,
 * and triggers from 0 to 255.
 */
enum {
    MIME_PAD_LX, MIME_PAD_LY, MIME_PAD_RX, MIME_PAD_RY, MIME_PAD_LT, MIME_PAD_RT, MIME_PAD_AXES,
};

/*
 * clipboard formats, each a mask bit. MIME_CLIP_TEXT is UTF-16LE with Windows line endings and no NUL;
 * MIME_CLIP_DIB and MIME_CLIP_DIBV5 contain CF_DIB and CF_DIBV5 bytes. MIME_CLIP_PNG is a PNG file,
 * MIME_CLIP_HTML contains "HTML Format" including its header, MIME_CLIP_HDROP is a DROPFILES block of guest
 * paths, and MIME_CLIP_URL is "UniformResourceLocatorW" text.
 *
 * MIME_CLIP_DROPEFFECT carries the "Preferred DropEffect" DWORD beside files: DROPEFFECT_MOVE for cut,
 * DROPEFFECT_COPY for copy. the host maps it to x-special/gnome-copied-files or
 * application/x-kde-cutselection. this format applies only to clipboard data, never drags. the host converts
 * formats for native applications.
 */
enum {
    MIME_CLIP_TEXT = 1u << 0,
    MIME_CLIP_DIB = 1u << 1,
    MIME_CLIP_DIBV5 = 1u << 2,
    MIME_CLIP_PNG = 1u << 3,
    MIME_CLIP_HTML = 1u << 4,
    MIME_CLIP_HDROP = 1u << 5,
    MIME_CLIP_URL = 1u << 6,
    MIME_CLIP_DROPEFFECT = 1u << 7,
};

/* MIME_HC_CLIP_PROVIDE's total when the guest has nothing to give for the request. */
#define MIME_CLIP_NONE UINT64_MAX

enum {
    MIME_MOUSE_LEFT = 1,
    MIME_MOUSE_RIGHT = 2,
    MIME_MOUSE_MIDDLE = 3,
    MIME_MOUSE_X1 = 4,
    MIME_MOUSE_X2 = 5,
};

enum {
    MIME_MOD_SHIFT = 1u << 0,
    MIME_MOD_CONTROL = 1u << 1,
    MIME_MOD_ALT = 1u << 2,
    MIME_MOD_WIN = 1u << 3,
    MIME_MOD_CAPS = 1u << 4,
};

typedef struct MimeInputEvent {
    uint32_t type;
    uint32_t window_id;
    int32_t x, y; /* screen coordinates in guest pixels. */
    uint32_t button;
    uint32_t modifiers;
    uint32_t vk;       /* windows virtual key. */
    uint32_t scancode; /* set 1 scancode, 0x100 marks extended keys. */
    uint32_t ch;       /* UTF-32 code point for MIME_INPUT_CHAR. */
    int32_t wheel;     /* WHEEL_DELTA units, positive away from the user. */
    uint32_t flags;    /* 1 = horizontal wheel, 2 = key repeat, 4 = relative motion, x and y unknown, 8 = the pointer is outside its window, as a held button lets it be. */
    uint32_t state_request; /* native window-state request id. */
    uint64_t time_ms;
    int32_t width, height; /* fullscreen frame dimensions in guest pixels. */
    int32_t dx, dy;    /* motion of a mouse move in guest pixels, unaccelerated when the host can tell. */
    uint32_t pad;      /* the host's id of a game controller. */
    uint16_t pad_buttons;
    uint16_t pad_vid, pad_pid;
    int16_t pad_axes[MIME_PAD_AXES];
} MimeInputEvent;

#endif
