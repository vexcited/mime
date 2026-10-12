/*
 * mime host <-> guest abi.
 *
 * this header is shared by the macos host (lp64) and the guest, which is
 * compiled for aarch64-pc-windows-msvc (llp64). it only uses fixed width
 * types so both sides agree on every layout.
 *
 * the guest is a hardware virtual machine. its kernel (ntoskrnl.exe, built
 * from guest/ntos) runs at el1 and talks to the host with `hvc #0`:
 *   x0 = MIME_HC_* number, x1..x6 = arguments
 *   x0 = MIME_HC_STATUS_* on return, x1 = result value
 * buffers are passed as guest physical addresses and must be physically
 * contiguous.
 */
#ifndef MIME_ABI_H
#define MIME_ABI_H

#include <stdint.h>

#define MIME_ABI_VERSION 3

/* guest physical layout. */
#define MIME_GIC_BASE 0x08000000ull /* the gic registers, below ram */
#define MIME_ROM_BASE 0x10000000ull /* the system files, read only, below ram */
#define MIME_RAM_BASE 0x40000000ull
#define MIME_PAGE_SIZE 4096ull

/*
 * guest kernel virtual layout, 48 bit addresses with a 4K granule.
 * user space is 0 .. 0x00007FFF'FFFFFFFF exactly like windows.
 */
#define MIME_KVA_PHYSMAP 0xFFFF800000000000ull /* direct map of guest physical memory */
#define MIME_KVA_KUSER_SHARED 0xFFFFF78000000000ull
#define MIME_KVA_KERNEL 0xFFFFF80000000000ull /* ntoskrnl.exe and boot drivers */
#define MIME_KVA_SYSTEM 0xFFFFF90000000000ull /* kernel stacks, driver images, mappings */
#define MIME_KVA_SYSTEM_SIZE 0x0000010000000000ull
#define MIME_USER_KUSER_SHARED 0x7FFE0000ull
#define MIME_USER_ADDRESS_LIMIT 0x00007FFFFFFFFFFFull

#define MIME_BOOT_MAGIC 0x544F4F42454D494Dull /* "MIMEBOOT" */
#define MIME_BOOT_MAX_RANGES 16

typedef struct MimeMemRange {
    uint64_t base;
    uint64_t size;
} MimeMemRange;

typedef struct MimeDisplayInfo {
    uint32_t width;  /* guest pixels of the primary display */
    uint32_t height;
    uint32_t work_x; /* usable work area, excluding menu bar and dock */
    uint32_t work_y;
    uint32_t work_width;
    uint32_t work_height;
    uint32_t dpi; /* 96 * backing scale factor */
    uint32_t refresh_hz;
} MimeDisplayInfo;

/* passed to the kernel entry point in x0, as a physmap virtual address. */
typedef struct MimeBootInfo {
    uint64_t magic;
    uint32_t version;
    uint32_t size;

    uint64_t ram_base;
    uint64_t ram_size;
    uint64_t first_free_phys; /* everything below was used by the loader */

    uint64_t kernel_phys;
    uint64_t kernel_virt;
    uint64_t kernel_size;

    uint64_t rom_phys; /* MimeRomHeader, also mapped read only at physmap + rom_phys */
    uint64_t rom_size;

    uint64_t ttbr1_phys;  /* kernel translation tables built by the vmm */
    uint64_t shared_phys; /* MimeSharedPage */
    uint64_t boot_stack_phys;
    uint64_t boot_stack_size;

    uint64_t timer_frequency;
    /* gicv3 emulated by the host: distributor, redistributors, both mapped at physmap + phys as device memory. */
    uint64_t gic_dist_phys;
    uint64_t gic_dist_size;
    uint64_t gic_redist_phys;
    uint64_t gic_redist_size;
    uint32_t irq_intid; /* edge triggered spi raised by the host, see MimeSharedPage */
    uint32_t timer_intid; /* ppi of the el1 virtual timer */
    uint64_t boot_counter;  /* CNTVCT_EL0 matching boot_filetime */
    uint64_t boot_filetime; /* 100ns units since 1601-01-01 utc */
    int32_t timezone_bias;  /* minutes, utc = local + bias */
    uint32_t cpu_count;

    MimeDisplayInfo display;

    /* identity of the installation, chosen once by the host, see host/identity.c. */
    char computer_name[16];
    char user_name[24];
    uint32_t user_sid[3];  /* the machine sid S-1-5-21-a-b-c, the user is its rid 1001 */
    uint32_t volume_serial; /* of C: */
    uint64_t install_filetime;
    /*
     * the processor and firmware of the reference machine, a stock windows
     * 11 arm64 computer. the guest reports these instead of the mac it runs
     * on, so that nothing in it can tell the virtual machine, the macos
     * hypervisor or the apple hardware from a real installation.
     */
    char cpu_name[64];          /* brand string of the reference processor */
    uint32_t cpu_mhz;           /* its nominal peak frequency, 0 when unknown */
    uint64_t cpu_midr;          /* its MIDR_EL1, 0 to report the hardware one */
    char system_manufacturer[32];
    char system_product[32];
    char system_family[24];
    char bios_vendor[24];
    char bios_version[32];
    char bios_date[16];         /* mm/dd/yyyy, as firmware reports it */
    char system_serial[20];     /* of this installation, drawn once */
    uint8_t system_uuid[16];    /* of this installation, drawn once */
    uint32_t pad1;
    char host_volume_label[32]; /* name of the host root volume, the Z: label */

    uint32_t reserved_count;
    uint32_t pad0;
    MimeMemRange reserved[MIME_BOOT_MAX_RANGES];

    char system_root[64]; /* "C:\\Windows" */
    char cmdline[256];
} MimeBootInfo;

/*
 * the system files built with mime (kernel, drivers and dlls), embedded in
 * the host binary and mapped into the guest. each file is page aligned, so
 * the guest maps its pages in place; dlls are linked with a 4K file
 * alignment so their file layout matches their memory layout.
 */
#define MIME_ROM_MAGIC 0x004D4F52454D494Dull /* "MIMEROM" */

typedef struct MimeRomEntry {
    char path[48]; /* relative to C:, e.g. \Windows\System32\ntdll.dll, nul padded */
    uint64_t offset; /* from the rom header, page aligned */
    uint64_t size;
} MimeRomEntry;

typedef struct MimeRomHeader {
    uint64_t magic;
    uint32_t count;
    uint32_t pad;
    MimeRomEntry entries[];
} MimeRomHeader;

/*
 * page shared by host and guest. irq_pending tells the guest why the host
 * raised irq_intid; the guest clears the bits it services.
 */
#define MIME_IRQ_CTRL (1u << 0)
#define MIME_IRQ_INPUT (1u << 1)
#define MIME_IRQ_DISPLAY (1u << 2)

typedef struct MimeSharedPage {
    volatile uint32_t irq_pending;
    volatile uint32_t guest_ready;
    volatile uint64_t host_filetime; /* refreshed by the host */
} MimeSharedPage;

/* hypercalls. */
enum {
    MIME_HC_LOG = 0x01,         /* (pa, len) */
    MIME_HC_PANIC = 0x02,       /* (pa, len) */
    MIME_HC_HALT = 0x04,        /* (exit_code) machine powered off */
    MIME_HC_CTRL_RECV = 0x05,   /* (pa, capacity) -> length, 0 when empty */
    MIME_HC_CTRL_SEND = 0x06,   /* (pa, len) */
    MIME_HC_INPUT_RECV = 0x07,  /* (pa, max_events) -> count */
    MIME_HC_TIME = 0x08,        /* () -> filetime */

    MIME_HC_FS_OPEN = 0x10,     /* (MimeFsOpen pa) -> handle */
    MIME_HC_FS_CLOSE = 0x11,    /* (handle) */
    MIME_HC_FS_READ = 0x12,     /* (handle, offset, pa, len) -> bytes */
    MIME_HC_FS_WRITE = 0x13,    /* (handle, offset, pa, len) -> bytes */
    MIME_HC_FS_QUERY = 0x14,    /* (handle, MimeFsInfo pa) */
    MIME_HC_FS_READDIR = 0x15,  /* (handle, index, MimeFsDirEntry pa) -> 1 or 0 at end */
    MIME_HC_FS_SET_SIZE = 0x16, /* (handle, size) */
    MIME_HC_FS_DELETE = 0x17,   /* (handle) delete on close */

    MIME_HC_WIN_CREATE = 0x20,  /* (window_id) */
    MIME_HC_WIN_DESTROY = 0x21, /* (window_id) */
    MIME_HC_WIN_CONFIG = 0x22,  /* (MimeWinConfig pa) */
    MIME_HC_WIN_PRESENT = 0x23, /* (MimeWinPresent pa) */
    MIME_HC_WIN_CURSOR = 0x24,  /* (MIME_CURSOR_*) */
    MIME_HC_BEEP = 0x25,        /* (type) */
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
};

/*
 * control channel. the host queues messages and raises MIME_IRQ_CTRL, the
 * guest pulls them with MIME_HC_CTRL_RECV. strings are utf-16le.
 */
enum {
    MIME_CTRL_RUN = 1,
    MIME_CTRL_SHUTDOWN = 2,

    MIME_EVT_READY = 100,
    MIME_EVT_PROCESS_STARTED = 101,
    MIME_EVT_PROCESS_EXIT = 102,
    MIME_EVT_RUN_FAILED = 103,
    MIME_EVT_DEBUG_OUTPUT = 104,
};

typedef struct MimeCtrlHeader {
    uint32_t type;
    uint32_t size; /* total size including this header */
} MimeCtrlHeader;

typedef struct MimeCtrlRun {
    MimeCtrlHeader header;
    uint64_t request_id;
    uint32_t image_chars;  /* dos path of the image, e.g. Z:\Users\me\a.exe */
    uint32_t cmdline_chars;
    uint32_t cwd_chars;
    uint32_t flags;
    /* uint16_t image[image_chars], cmdline[cmdline_chars], cwd[cwd_chars] */
} MimeCtrlRun;

typedef struct MimeEvtProcess {
    MimeCtrlHeader header;
    uint64_t request_id;
    uint32_t status; /* exit code or ntstatus for MIME_EVT_RUN_FAILED */
    uint32_t process_id;
} MimeEvtProcess;

typedef struct MimeEvtDebugOutput {
    MimeCtrlHeader header;
    uint64_t request_id;
    uint32_t process_id;
    uint32_t length; /* utf-8 bytes */
    /* char text[length] */
} MimeEvtDebugOutput;

#define MIME_CTRL_MAX_MESSAGE 8192

/*
 * file system channel. the host exposes volumes; paths are utf-16le,
 * backslash separated and relative to the volume root.
 */
enum {
    MIME_FS_VOLUME_SYSTEM = 0, /* C:, the drive_c directory */
    MIME_FS_VOLUME_HOST = 1,   /* Z:, the host root */
};

enum {
    MIME_FS_OPEN_READ = 1u << 0,
    MIME_FS_OPEN_WRITE = 1u << 1,
    MIME_FS_OPEN_DIRECTORY = 1u << 2,     /* must be a directory */
    MIME_FS_OPEN_NON_DIRECTORY = 1u << 3, /* must not be a directory */
};

enum {
    MIME_FS_DISP_OPEN = 0,      /* FILE_OPEN */
    MIME_FS_DISP_CREATE = 1,    /* FILE_CREATE */
    MIME_FS_DISP_OPEN_IF = 2,   /* FILE_OPEN_IF */
    MIME_FS_DISP_OVERWRITE = 3, /* FILE_OVERWRITE */
    MIME_FS_DISP_OVERWRITE_IF = 4,
    MIME_FS_DISP_SUPERSEDE = 5,
};

typedef struct MimeFsOpen {
    uint32_t volume;
    uint32_t flags;
    uint32_t disposition;
    uint32_t path_chars;
    uint64_t handle_out;
    uint32_t action_out; /* FILE_OPENED, FILE_CREATED... */
    uint32_t pad;
    uint16_t path[520];
} MimeFsOpen;

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
} MimeFsInfo;

typedef struct MimeFsDirEntry {
    MimeFsInfo info;
    uint32_t name_chars;
    uint32_t pad;
    uint16_t name[256];
} MimeFsDirEntry;

/*
 * display channel. every visible top level window is a host window; the
 * guest renders the whole window (frame included) into a surface in guest
 * memory and presents dirty rectangles.
 */
#define MIME_WIN_MAX_DRAG_RECTS 4

typedef struct MimeRect {
    int32_t left, top, right, bottom;
} MimeRect;

enum {
    MIME_WIN_VISIBLE = 1u << 0,
    MIME_WIN_TOPMOST = 1u << 1,
    MIME_WIN_ACTIVATE = 1u << 2, /* bring to front and take focus */
    MIME_WIN_SHADOW = 1u << 3,
    MIME_WIN_TOOL = 1u << 4,     /* no activation, used for menus and tooltips */
};

typedef struct MimeWinConfig {
    uint32_t window_id;
    uint32_t flags;
    MimeRect rect;    /* screen coordinates in guest pixels */
    MimeRect visible; /* window relative part that is drawn opaque/visible */
    uint32_t corner_radius;
    uint32_t border_color; /* 0xAARRGGBB 1 pixel border drawn by the host over the visible rect, 0 for none */
    uint32_t drag_count;
    MimeRect drag[MIME_WIN_MAX_DRAG_RECTS]; /* window relative caption areas */
    uint32_t owner_id; /* keeps owned windows above their owner, 0 if none */
    uint32_t title_chars;
    uint16_t title[256];
} MimeWinConfig;

typedef struct MimeWinPresent {
    uint32_t window_id;
    uint32_t width;  /* surface size, matches rect */
    uint32_t height;
    uint32_t stride; /* bytes */
    uint64_t surface_phys; /* bgra8 premultiplied, physically contiguous */
    MimeRect dirty;
} MimeWinPresent;

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
    MIME_INPUT_WINDOW_MOVED = 8,   /* host moved the window: x, y */
    MIME_INPUT_WINDOW_FOCUS = 9,   /* x = 1 gained, 0 lost */
    MIME_INPUT_WINDOW_CLOSE = 10,  /* close requested by the host (cmd+w, dock) */
    MIME_INPUT_MOUSE_LEAVE = 11,
};

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
    int32_t x, y; /* screen coordinates in guest pixels */
    uint32_t button;
    uint32_t modifiers;
    uint32_t vk;       /* windows virtual key */
    uint32_t scancode; /* set 1 scancode, 0x100 marks extended keys */
    uint32_t ch;       /* utf-32 code point for MIME_INPUT_CHAR */
    int32_t wheel;     /* WHEEL_DELTA units, positive away from the user */
    uint32_t flags;    /* 1 = horizontal wheel, 2 = key repeat */
    uint32_t pad;
    uint64_t time_ms;
} MimeInputEvent;

#endif
