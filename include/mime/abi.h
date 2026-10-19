/*
 * mime host <-> guest abi.
 *
 * this header is shared by the macos host (lp64) and the guest, which is
 * compiled for aarch64-pc-windows-msvc (llp64). it only uses fixed width
 * types so both sides agree on every layout.
 *
 * the guest is a hardware virtual machine of the host's processor. its
 * kernel (ntoskrnl.exe, built from guest/ntos) talks to the host with
 * hypercalls. on arm64 it runs at el1 and uses `hvc #0`:
 *   x0 = MIME_HC_* number, x1..x6 = arguments
 *   x0 = MIME_HC_STATUS_* on return, x1 = result value
 * on x64 it runs in ring 0 and writes MIME_HC_PORT with `outb %al`:
 *   rax = MIME_HC_* number, rdi rsi rdx r10 r8 r9 = arguments
 *   rax = MIME_HC_STATUS_* on return, rdx = result value
 * buffers are passed as guest physical addresses and must be physically
 * contiguous.
 */
#ifndef MIME_ABI_H
#define MIME_ABI_H

#include <stdint.h>

#define MIME_ABI_VERSION 3

/* guest physical layout. */
#ifdef __x86_64__
/*
 * x64 keeps the first 4 GiB for the system files and the apic page kvm
 * places at 0xfee00000, so ram never has to split around it.
 */
#define MIME_ROM_BASE 0x10000000ull /* the system files, read only, below ram */
#define MIME_RAM_BASE 0x100000000ull
/*
 * host files placed read only by MIME_HC_FS_MAP, above ram, mapped from
 * the host page cache.
 */
#define MIME_WINDOW_BASE 0x10000000000ull
#define MIME_WINDOW_SIZE 0x10000000000ull
#else
#define MIME_GIC_BASE 0x08000000ull /* the gic registers, below ram */
#define MIME_ROM_BASE 0x10000000ull /* the system files, read only, below ram */
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
    uint32_t irq_intid; /* edge triggered spi raised by the host, see MimeSharedPage, the vector on x64 */
    uint32_t timer_intid; /* ppi of the el1 virtual timer */
    uint64_t boot_counter;  /* CNTVCT_EL0 matching boot_filetime, the tsc on x64 */
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
    char system_manufacturer[64];
    char system_product[64];
    char system_family[64];
    char bios_vendor[64];
    char bios_version[64];
    char bios_date[16];         /* mm/dd/yyyy, as firmware reports it */
    char system_serial[24];     /* of this installation, drawn once */
    uint8_t system_uuid[16];    /* of this installation, drawn once */
    uint32_t pad1;
    char host_volume_label[32]; /* name of the host root volume, the Z: label */

    uint32_t reserved_count;
    /*
     * the host backs ram in blocks of 1 << memory_block_shift bytes, 2 MiB
     * huge pages on linux: handing back less of a block splits it, so the
     * guest discards whole blocks.
     */
    uint32_t memory_block_shift;
    MimeMemRange reserved[MIME_BOOT_MAX_RANGES];

    char system_root[64]; /* "C:\\Windows" */
    char cmdline[256];
    /* the host's swap: the guest has no paging file and reports ram plus this as its commit limit. */
    uint64_t swap_size;
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
#define MIME_IRQ_GPU (1u << 3)
#define MIME_IRQ_NET (1u << 4)
#define MIME_IRQ_DUMP (1u << 5) /* log the guest's threads, on the host's SIGUSR1 */

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
    MIME_HC_CPU_START = 0x09,   /* (cpu, pc, x0, sp) starts a secondary processor at el1 with interrupts masked */
    MIME_HC_LOCK_WAIT = 0x0A,   /* (pa, ticket) sleeps while the ticket lock word at pa does not serve ticket */
    MIME_HC_LOCK_WAKE = 0x0B,   /* (pa) wakes the processors sleeping on the lock word at pa */

    MIME_HC_FS_OPEN = 0x10,     /* (MimeFsOpen pa) -> handle */
    MIME_HC_FS_CLOSE = 0x11,    /* (handle) */
    MIME_HC_FS_READ = 0x12,     /* (handle, offset, pa, len) -> bytes */
    MIME_HC_FS_WRITE = 0x13,    /* (handle, offset, pa, len) -> bytes */
    MIME_HC_FS_QUERY = 0x14,    /* (handle, MimeFsInfo pa) */
    MIME_HC_FS_READDIR = 0x15,  /* (handle, index, MimeFsDirEntry pa) -> 1 or 0 at end */
    MIME_HC_FS_SET_SIZE = 0x16, /* (handle, size) */
    MIME_HC_FS_DELETE = 0x17,   /* (handle) delete on close */
    MIME_HC_FS_MAP = 0x18,      /* (handle) -> guest physical address of the whole file, read only, filled on touch */
    MIME_HC_FS_UNMAP = 0x19,    /* (pa) */
    MIME_HC_MEM_DISCARD = 0x1A, /* (pa, size) free guest ram whose contents the host may drop */
    MIME_HC_FS_VOLUME_QUERY = 0x1C, /* volume id and volume information address. */
    MIME_HC_FS_FLUSH = 0x1B,    /* flush a host file handle. */
    MIME_HC_MEM_ADVISE = 0x1D,  /* (pa, size, MIME_MEM_*) how the guest uses a range of ram */
    MIME_HC_FS_GET_SD = 0x1E,   /* (handle, pa, capacity, up) -> distance << 32 | length of the nearest stored descriptor */
    MIME_HC_FS_SET_SD = 0x1F,   /* (handle, pa, length) stores a security descriptor, none for length 0 */

    MIME_HC_WIN_CREATE = 0x20,  /* (window_id) */
    MIME_HC_WIN_DESTROY = 0x21, /* (window_id) */
    MIME_HC_WIN_CONFIG = 0x22,  /* (MimeWinConfig pa) */
    MIME_HC_WIN_PRESENT = 0x23, /* (MimeWinPresent pa) */
    MIME_HC_WIN_CURSOR = 0x24,  /* (MIME_CURSOR_*) */
    MIME_HC_BEEP = 0x25,        /* (type) */
    MIME_HC_CLIP_SET = 0x26,    /* (pa, len, offset, total) utf-16le text in chunks, committed at the last byte */
    MIME_HC_CLIP_GET = 0x27,    /* (pa, capacity, offset) -> total bytes of the host utf-16le text */
    MIME_HC_GPU_OPEN = 0x28,    /* (pa, size) -> channel, see mime/gpu.h */
    MIME_HC_GPU_BLOCK = 0x29,   /* (channel, index, pa, size) adds a heap block */
    MIME_HC_GPU_KICK = 0x2A,    /* (channel) new commands while the host is idle */
    MIME_HC_GPU_CLOSE = 0x2B,   /* (channel) returns once the host and the gpu no longer use its memory */
    MIME_HC_WIN_ICON = 0x2C,    /* window icon pixels described by MimeWinIcon. */
    MIME_HC_WIN_READBACK = 0x43, /* (MimeWinReadback pa) the last image presented to a window through vulkan */
    MIME_HC_VK_OPEN = 0x2D,     /* (pa, size) -> connection, see mime/mvk_wire.h */
    MIME_HC_VK_KICK = 0x2E,     /* (connection) new calls while the host is idle */
    MIME_HC_VK_CLOSE = 0x2F,    /* (connection) returns once the host and the gpu no longer use its memory */

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
    MIME_HC_NET_RESOLVE = 0x3D,    /* (pa, len, family) utf-8 host name -> id, MIME_NET_RESOLVED | id fires */
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
    MIME_HC_DND_START = 0x4E,    /* (MIME_CLIP_* kinds, DROPEFFECT_* allowed, window) -> session: a drag of the guest leaves its windows */
    MIME_HC_DND_STOP = 0x4F,     /* (session) the guest ends the drag it started */
};

/* what the host pointer does over a window: as usual, kept in a rectangle, or locked to report motion alone. */
enum {
    MIME_POINTER_FREE = 0,
    MIME_POINTER_CONFINED = 1,
    MIME_POINTER_RELATIVE = 2,
};

enum {
    MIME_MEM_SPARSE = 1, /* touched here and there, such as the pfn database: back it in small pages */
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
};

/*
 * networking. the host keeps non blocking sockets; the guest arms a watch
 * on what would block and the host raises MIME_IRQ_NET once it fired.
 */
typedef struct MimeNetAddr {
    uint16_t family; /* a windows sockaddr_in or sockaddr_in6, AF_INET 2 or AF_INET6 23 */
    uint8_t data[26];
} MimeNetAddr;

enum {
    MIME_NET_READ = 1u << 0,    /* data, a datagram or a connection to accept */
    MIME_NET_WRITE = 1u << 1,   /* room to send, or a connect that finished */
    MIME_NET_HUP = 1u << 2,     /* the stream ended and nothing is left to read */
    MIME_NET_ERROR = 1u << 3,   /* the pending socket error, taken into error */
    MIME_NET_INVALID = 1u << 4, /* no such socket */
};

typedef struct MimeNetPoll {
    uint32_t socket;
    uint16_t events; /* MIME_NET_READ and MIME_NET_WRITE to count and watch, revents has every state */
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
    MIME_NET_OPT_LINGER = 8, /* on in bit 16, seconds below */
    MIME_NET_OPT_ERROR = 9,  /* read and clears the pending error */
    MIME_NET_OPT_OOBINLINE = 10,
    MIME_NET_OPT_TTL = 11,
    MIME_NET_OPT_NREAD = 12, /* bytes waiting, the size of the next datagram */
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
    MIME_NET_IF_RUNNING = 1u << 5, /* the link is up: a carrier, or a virtual link that is ready */
    MIME_NET_IF_VIRTUAL = 1u << 6, /* no hardware behind it, such as a bridge, a tunnel or a veth */
};

typedef struct MimeNetInterface {
    char name[16];
    uint32_t index;
    uint32_t flags;
    uint32_t mtu;
    uint8_t prefix; /* bits of the network part of addr */
    uint8_t mac_length;
    uint8_t mac[8];
    uint8_t pad[2];
    MimeNetAddr addr;
} MimeNetInterface;

/*
 * the host's network configuration as windows adapters show it: the links,
 * their addresses, the routes through a gateway and the resolver's servers
 * and search domains. the arrays follow the header in this order.
 */
enum {
    MIME_NET_LINK_OTHER = 0,
    MIME_NET_LINK_ETHERNET = 1,
    MIME_NET_LINK_WIRELESS = 2,
    MIME_NET_LINK_LOOPBACK = 3,
    MIME_NET_LINK_TUNNEL = 4, /* an ip tunnel without link layer, such as wireguard or a tun device */
    MIME_NET_LINK_PPP = 5,
};

typedef struct MimeNetLink {
    char name[16];
    uint32_t index;
    uint32_t flags; /* MIME_NET_IF_* */
    uint32_t mtu;
    uint32_t type;  /* MIME_NET_LINK_* */
    uint64_t speed; /* bits per second, 0 when unknown */
    uint8_t mac_length;
    uint8_t mac[8];
    uint8_t pad[7];
} MimeNetLink;

enum {
    MIME_NET_ADDR_DYNAMIC = 1u << 0,   /* leased by dhcp or configured from router advertisements */
    MIME_NET_ADDR_TEMPORARY = 1u << 1, /* an ipv6 privacy address */
    MIME_NET_ADDR_DEPRECATED = 1u << 2,
    MIME_NET_ADDR_TENTATIVE = 1u << 3,
};

typedef struct MimeNetAddress {
    uint32_t index; /* of the link */
    uint32_t flags; /* MIME_NET_ADDR_* */
    uint32_t valid; /* seconds left, UINT32_MAX for ever */
    uint32_t preferred;
    uint8_t prefix;
    uint8_t pad[3];
    MimeNetAddr addr;
} MimeNetAddress;

enum {
    MIME_NET_ROUTE_STATIC = 0,
    MIME_NET_ROUTE_DHCP = 1,
    MIME_NET_ROUTE_RA = 2, /* learned from router advertisements */
};

typedef struct MimeNetRoute {
    uint32_t index;  /* of the link */
    uint32_t metric; /* the host's, 0 when it has none */
    uint8_t prefix;  /* 0 for a default route */
    uint8_t origin;  /* MIME_NET_ROUTE_* */
    uint8_t pad[2];
    MimeNetAddr destination;
    MimeNetAddr gateway;
} MimeNetRoute;

#define MIME_NET_MAX_DNS 8

typedef struct MimeNetConfig {
    uint32_t size; /* bytes written, this header and the arrays */
    uint32_t links;
    uint32_t addresses;
    uint32_t routes;
    uint32_t servers;
    uint32_t pad;
    char domain[256]; /* utf-8, the resolver's domain */
    char search[256]; /* the search domains, separated by spaces */
    MimeNetAddr server[MIME_NET_MAX_DNS];
    /* MimeNetLink link[links], MimeNetAddress address[addresses], MimeNetRoute route[routes] */
} MimeNetConfig;

/*
 * control channel. the host queues messages and raises MIME_IRQ_CTRL, the
 * guest pulls them with MIME_HC_CTRL_RECV. strings are utf-16le.
 */
enum {
    MIME_CTRL_RUN = 1,
    MIME_CTRL_SHUTDOWN = 2,
    MIME_CTRL_CONSOLE_INPUT = 3, /* MimeCtrlConsole: bytes typed or piped in, none for end of file */
    MIME_CTRL_CONSOLE_EVENT = 4, /* MimeCtrlConsole: value is CTRL_C_EVENT, CTRL_BREAK_EVENT or CTRL_CLOSE_EVENT */
    MIME_CTRL_CONSOLE_SIZE = 5,  /* MimeCtrlConsole: value is columns | rows << 16 */

    MIME_EVT_READY = 100,
    MIME_EVT_PROCESS_STARTED = 101, /* MimeEvtProcess: status is the image subsystem */
    MIME_EVT_PROCESS_EXIT = 102,
    MIME_EVT_RUN_FAILED = 103,
    MIME_EVT_DEBUG_OUTPUT = 104,
    MIME_EVT_CONSOLE_OUTPUT = 105, /* MimeCtrlConsole: value is 1 for stdout, 2 for stderr */
    MIME_EVT_CONSOLE_MODE = 106,   /* MimeCtrlConsole: value is the console input mode */
    MIME_EVT_CONSOLE_READ = 107,   /* MimeCtrlConsole: value is input bytes the console took, to send more */
};

enum {
    MIME_RUN_TTY_INPUT = 1u << 0,  /* the terminal is the input, not a pipe or file */
    MIME_RUN_TTY_OUTPUT = 1u << 1, /* and the standard output */
    MIME_RUN_TTY_ERROR = 1u << 2,  /* and the standard error */
};

/* subsystems MIME_EVT_PROCESS_STARTED reports, as in the image header. */
#define MIME_SUBSYSTEM_GUI 2
#define MIME_SUBSYSTEM_CUI 3

/* the guest console takes at most this much unread input. */
#define MIME_CONSOLE_INPUT_WINDOW 65536

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
    uint32_t flags;        /* MIME_RUN_... */
    uint32_t console_size; /* columns | rows << 16 of the terminal, 0 when unknown */
    uint32_t pad;
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

typedef struct MimeCtrlConsole {
    MimeCtrlHeader header;
    uint64_t request_id;
    uint32_t value;
    uint32_t length;
    /* uint8_t data[length] */
} MimeCtrlConsole;

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
    MIME_WIN_MINIMIZED = 1u << 5,
    MIME_WIN_MAXIMIZED = 1u << 6,
    MIME_WIN_RESIZABLE = 1u << 7,
    MIME_WIN_APPWINDOW = 1u << 8,
    MIME_WIN_FULLSCREEN = 1u << 9,
    MIME_WIN_MINIMIZABLE = 1u << 10,
    MIME_WIN_MAXIMIZABLE = 1u << 11,
    MIME_WIN_ACTIVE = 1u << 12, /* the frame is drawn active */
    MIME_WIN_NOINPUT = 1u << 13, /* takes no mouse input: a transparent window, such as a tooltip */
};

typedef struct MimeWinConfig {
    uint32_t window_id;
    uint32_t flags;
    uint32_t state_request; /* last completed native window-state request. */
    MimeRect rect;    /* screen coordinates in guest pixels */
    MimeRect visible; /* window relative part that is drawn opaque/visible */
    MimeRect input;   /* window relative part that takes the mouse, the visible part plus any resize border */
    uint32_t corner_radius;
    uint32_t border_color; /* 0xAARRGGBB 1 pixel border drawn by the host over the visible rect, 0 for none */
    uint32_t drag_count;
    MimeRect drag[MIME_WIN_MAX_DRAG_RECTS]; /* window relative caption areas */
    uint32_t owner_id; /* keeps owned windows above their owner, 0 if none */
    uint32_t process_id;
    uint32_t image_chars;
    uint16_t image_path[512];
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

/*
 * a window's last vulkan present, read back as bgra rows into pixels_phys
 * when it fits capacity; the host fills width, height and the client
 * rectangle it showed in, relative to the window. MIME_HC_STATUS_NOT_FOUND
 * when the window presents nothing.
 */
typedef struct MimeWinReadback {
    uint32_t window_id;
    uint32_t capacity_width, capacity_height;
    uint32_t stride;
    uint64_t pixels_phys;
    uint32_t width, height;
    MimeRect client;
} MimeWinReadback;

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
    MIME_INPUT_WINDOW_MOVED = 8,   /* host moved the window: x, y */
    MIME_INPUT_WINDOW_FOCUS = 9,   /* x = 1 gained, 0 lost */
    MIME_INPUT_WINDOW_CLOSE = 10,  /* close requested by the host (cmd+w, dock) */
    MIME_INPUT_MOUSE_LEAVE = 11,
    MIME_INPUT_CLIPBOARD = 12,     /* another host application changed the clipboard: x is the MIME_CLIP_* kinds it holds, y its generation */
    MIME_INPUT_WINDOW_STATE = 13,  /* x is a windows show command. */
    MIME_INPUT_WINDOW_FULLSCREEN = 14, /* flags is 1 on entry, 0 on exit. */
    MIME_INPUT_DISPLAY = 15,       /* the boot information display changed: width and height. */
    MIME_INPUT_PAD_ADDED = 16,     /* a game controller: pad, pad_vid, pad_pid */
    MIME_INPUT_PAD_REMOVED = 17,   /* pad */
    MIME_INPUT_PAD_STATE = 18,     /* pad, pad_buttons, pad_axes */
    MIME_INPUT_CLIP_REQUEST = 19,  /* another host program pastes: x is the MIME_CLIP_* kind, button the request id */
    /*
     * a host program drags over a window: x and y are the position in guest pixels, flags the MIME_CLIP_* kinds the
     * drag holds, wheel the DROPEFFECT_* the source allows, modifiers the keys, pad the session. the target answers
     * enter and over with MIME_HC_DND_EFFECT, and the drop with MIME_HC_DND_FINISH.
     */
    MIME_INPUT_DND_ENTER = 20,
    MIME_INPUT_DND_OVER = 21,
    MIME_INPUT_DND_LEAVE = 22,
    MIME_INPUT_DND_DROP = 23,
    MIME_INPUT_DND_REQUEST = 24,   /* a host program reads the data of the guest's drag: x is the kind, button the request id */
    MIME_INPUT_DND_END = 25,       /* the guest's drag ended: pad is the session, flags the DROPEFFECT_*, x is 1 for a drop */
};

/*
 * a game controller's state as an xbox 360 controller's: XINPUT_GAMEPAD's
 * button bits, then the left and right sticks from -32768 to 32767 with up
 * positive, and the triggers from 0 to 255.
 */
enum {
    MIME_PAD_LX, MIME_PAD_LY, MIME_PAD_RX, MIME_PAD_RY, MIME_PAD_LT, MIME_PAD_RT, MIME_PAD_AXES,
};

/*
 * the formats the clipboard shares with the host, each a bit of a mask. the guest names them by what windows
 * programs use: MIME_CLIP_TEXT is utf-16le text with windows line ends and no nul, MIME_CLIP_DIB and
 * MIME_CLIP_DIBV5 are the bytes of CF_DIB and CF_DIBV5, MIME_CLIP_PNG a png file, MIME_CLIP_HTML the bytes of
 * "HTML Format" with its header, MIME_CLIP_HDROP a DROPFILES block of guest paths and MIME_CLIP_URL the text of
 * "UniformResourceLocatorW". the host converts to and from what other host programs use.
 */
enum {
    MIME_CLIP_TEXT = 1u << 0,
    MIME_CLIP_DIB = 1u << 1,
    MIME_CLIP_DIBV5 = 1u << 2,
    MIME_CLIP_PNG = 1u << 3,
    MIME_CLIP_HTML = 1u << 4,
    MIME_CLIP_HDROP = 1u << 5,
    MIME_CLIP_URL = 1u << 6,
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
    int32_t x, y; /* screen coordinates in guest pixels */
    uint32_t button;
    uint32_t modifiers;
    uint32_t vk;       /* windows virtual key */
    uint32_t scancode; /* set 1 scancode, 0x100 marks extended keys */
    uint32_t ch;       /* utf-32 code point for MIME_INPUT_CHAR */
    int32_t wheel;     /* WHEEL_DELTA units, positive away from the user */
    uint32_t flags;    /* 1 = horizontal wheel, 2 = key repeat, 4 = relative motion, x and y unknown, 8 = the pointer is outside its window, as a held button lets it be */
    uint32_t state_request; /* native window-state request id. */
    uint64_t time_ms;
    int32_t width, height; /* fullscreen frame dimensions in guest pixels. */
    int32_t dx, dy;    /* motion of a mouse move in guest pixels, unaccelerated when the host can tell */
    uint32_t pad;      /* the host's id of a game controller */
    uint16_t pad_buttons;
    uint16_t pad_vid, pad_pid;
    int16_t pad_axes[MIME_PAD_AXES];
} MimeInputEvent;

#endif
