/*
 * shared camera streams. hypercalls enumerate host cameras and formats. each stream occupies contiguous guest
 * memory: a header and frame slots. the host publishes filled slots; the guest reads them in place and
 * releases them. frames do not cross the hypercall interface.
 *
 * conversion runs only when requested: motion JPEG and packed or planar YUV become YUY2, NV12 or I420,
 * so the guest never needs to decode or rearrange them.
 */
#ifndef MIME_CAMERA_H
#define MIME_CAMERA_H

#include <stdint.h>

#define MIME_CAMERA_MAX 8        /* cameras at once. */
#define MIME_CAMERA_MAX_SLOTS 8  /* frames of a stream in flight. */
#define MIME_CAMERA_HEADER_SIZE 4096u
#define MIME_CAMERA_MAX_FORMATS 512

/*
 * formats use the first 32 bits of their Media Foundation subtype, matching DirectX FOURCCs in little-endian
 * order.
 */
#define MIME_CAMERA_FOURCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define MIME_CAMERA_FOURCC_YUY2 MIME_CAMERA_FOURCC('Y', 'U', 'Y', '2')
#define MIME_CAMERA_FOURCC_UYVY MIME_CAMERA_FOURCC('U', 'Y', 'V', 'Y')
#define MIME_CAMERA_FOURCC_NV12 MIME_CAMERA_FOURCC('N', 'V', '1', '2')
#define MIME_CAMERA_FOURCC_I420 MIME_CAMERA_FOURCC('I', '4', '2', '0')
#define MIME_CAMERA_FOURCC_YV12 MIME_CAMERA_FOURCC('Y', 'V', '1', '2')
#define MIME_CAMERA_FOURCC_MJPG MIME_CAMERA_FOURCC('M', 'J', 'P', 'G')

enum {
    MIME_CAMERA_FLAG_FAKE = 1,   /* synthetic host camera. */
    MIME_CAMERA_FLAG_USB = 2,    /* vendor and product are a USB device's. */
};

typedef struct MimeCameraInfo {
    uint32_t id;      /* the host's number of the camera, for as long as it stays plugged. */
    uint32_t flags;   /* MIME_CAMERA_FLAG_* */
    uint16_t vendor;
    uint16_t product;
    uint16_t interface_number; /* USB video interface name, distinguishing cameras on a shared device. */
    uint16_t reserved;
    uint32_t formats;
    uint32_t generation; /* connection generation, changed after unplugging and reconnecting. */
    char name[128];   /* UTF-8. */
    char location[96]; /* where it is plugged, such as the host's bus path. */
} MimeCameraInfo;

/* a mode of a camera: a format, a size and a rate. the frame interval is fps_den / fps_num seconds. */
typedef struct MimeCameraFormat {
    uint32_t fourcc;
    uint32_t width, height;
    uint32_t fps_num, fps_den;
} MimeCameraFormat;

/*
 * output formats the host can convert to. 0 preserves the camera format, including variable-sized motion JPEG
 * frames.
 */
#define MIME_CAMERA_CONVERTS_TO(fourcc)                                                                                \
    ((fourcc) == MIME_CAMERA_FOURCC_YUY2 || (fourcc) == MIME_CAMERA_FOURCC_NV12 || (fourcc) == MIME_CAMERA_FOURCC_I420)

enum {
    MIME_CAMERA_SLOT_FREE = 0,    /* the host may take it. */
    MIME_CAMERA_SLOT_WRITING = 1, /* the host fills it. */
    MIME_CAMERA_SLOT_READY = 2,   /* a frame the guest may take. */
    MIME_CAMERA_SLOT_HELD = 3,    /* the guest reads it, and sets it free when done. */
};

enum {
    MIME_CAMERA_FRAME_CORRUPT = 1, /* the camera said the frame is damaged, or it did not decode. */
};

typedef struct MimeCameraSlot {
    uint32_t state;    /* MIME_CAMERA_SLOT_*, changed with atomic operations by both sides. */
    uint32_t length;   /* bytes of the frame. */
    uint64_t sequence; /* the frame's number, from 1. */
    uint64_t host_ns;  /* the host's monotonic clock when the camera captured the frame. */
    uint32_t flags;    /* MIME_CAMERA_FRAME_* */
    uint32_t reserved;
} MimeCameraSlot;

enum {
    MIME_CAMERA_ERROR_NONE = 0,
    MIME_CAMERA_ERROR_LOST = 1,   /* the camera was unplugged. */
    MIME_CAMERA_ERROR_FAILED = 2, /* the camera stopped sending frames. */
};

typedef struct MimeCameraHeader {
    /* the request, filled in by the guest before the stream opens. */
    uint32_t source_fourcc;
    uint32_t width, height;
    uint32_t fps_num, fps_den;
    uint32_t output_fourcc; /* 0 for the camera's own. */
    uint32_t slots;         /* 1 to MIME_CAMERA_MAX_SLOTS. */
    uint32_t slot_bytes;    /* each slot's room, a multiple of 4096. */
    uint32_t data_offset;   /* where slot 0 begins in the block, a multiple of 4096. */
    uint32_t request_reserved;

    /* the answer, written by the host while the stream opens. */
    uint32_t frame_fourcc;  /* what the slots hold. */
    uint32_t frame_bytes;   /* the size of a frame of that format, 0 for motion JPEG frames, which vary. */
    uint32_t stride;        /* bytes of a row of the first plane. */
    uint32_t wake;          /* 1 once the host has told the guest of a frame, set to 0 by the guest before it looks. */
    uint64_t open_ns;       /* the host's monotonic clock when the stream opened. */
    uint64_t interval_100ns; /* the camera's frame interval. */

    /* running, advanced by the host. */
    uint64_t sequence;      /* frames published. */
    uint64_t dropped;       /* frames lost for want of a free slot, or lost by the camera. */
    uint32_t error;         /* MIME_CAMERA_ERROR_* */
    uint32_t reserved[3];
    MimeCameraSlot slot[MIME_CAMERA_MAX_SLOTS];
} MimeCameraHeader;

_Static_assert(sizeof(MimeCameraHeader) <= MIME_CAMERA_HEADER_SIZE, "the header is one page");

/* the frame size of an uncompressed format, packed formats having a whole pair of pixels at the end of an odd row. */
static inline uint32_t mime_camera_frame_bytes(uint32_t fourcc, uint32_t width, uint32_t height)
{
    switch (fourcc) {
    case MIME_CAMERA_FOURCC_YUY2:
    case MIME_CAMERA_FOURCC_UYVY:
        return (width + 1) / 2 * 4 * height;
    case MIME_CAMERA_FOURCC_NV12:
    case MIME_CAMERA_FOURCC_I420:
    case MIME_CAMERA_FOURCC_YV12:
        return width * height + ((width + 1) / 2) * ((height + 1) / 2) * 2;
    default:
        return 0;
    }
}

/*
 * camera controls use DirectShow VideoProcAmp and CameraControl ids for host properties such as brightness
 * and focus.
 */
enum {
    MIME_CAMERA_CONTROL_PROCAMP = 0,  /* VideoProcAmp_*: brightness, contrast, hue, saturation, sharpness, gamma, ... */
    MIME_CAMERA_CONTROL_CAMERA = 1,   /* CameraControl_*: pan, tilt, roll, zoom, exposure, iris, focus. */
};

enum {
    MIME_CAMERA_CONTROL_RANGE = 0, /* min, max, step, default, and the flags the control supports. */
    MIME_CAMERA_CONTROL_GET = 1,
    MIME_CAMERA_CONTROL_SET = 2,
};

enum {
    MIME_CAMERA_CONTROL_FLAG_AUTO = 1,   /* KSPROPERTY_*_FLAGS_AUTO */
    MIME_CAMERA_CONTROL_FLAG_MANUAL = 2,
};

/* control request answered in place. */
typedef struct MimeCameraControl {
    uint32_t id;
    uint32_t set;      /* MIME_CAMERA_CONTROL_PROCAMP or _CAMERA. */
    uint32_t property; /* the property of that set, as the DirectShow interface numbers it. */
    uint32_t operation;
    int32_t min, max, step, default_value;
    int32_t value;
    uint32_t flags;    /* MIME_CAMERA_CONTROL_FLAG_*: what the control can do, or what the value is set with. */
    int32_t status;    /* 0, or a MIME_HC_STATUS_* when the control is not there or the camera refused. */
    uint32_t reserved;
} MimeCameraControl;

/* guest kernel camera device requests. */
#define MIME_CAMERA_DEVICE_TYPE 0x23u /* FILE_DEVICE_VIDEO */
#define MIME_CAMERA_IOCTL(n) ((MIME_CAMERA_DEVICE_TYPE << 16) | (((n) + 0x800u) << 2))

enum {
    MIME_CAMERA_IOCTL_INFO = 0,     /* out: MimeCameraInfo. */
    MIME_CAMERA_IOCTL_FORMATS = 1,  /* out: MimeCameraFormatList. */
    MIME_CAMERA_IOCTL_OPEN = 2,     /* in: MimeCameraOpen, out: MimeCameraOpened. */
    MIME_CAMERA_IOCTL_START = 3,    /* in: a uint32 that is 1 to run and 0 to pause. */
    MIME_CAMERA_IOCTL_CLOSE = 4,
    MIME_CAMERA_IOCTL_CONTROL = 5,  /* in and out: MimeCameraControl. */
};

typedef struct MimeCameraFormatList {
    uint32_t count;     /* how many the camera has, which may be more than fit. */
    uint32_t returned;
    MimeCameraFormat format[1];
} MimeCameraFormatList;

typedef struct MimeCameraOpen {
    MimeCameraFormat format; /* the camera's mode to run. */
    uint32_t output_fourcc;  /* 0 for as the camera sends it. */
    uint32_t slots;          /* 0 for the default. */
    uint32_t reserved;
    uint64_t event;          /* a handle to an event the kernel sets when frames are waiting. */
} MimeCameraOpen;

typedef struct MimeCameraOpened {
    uint64_t base;   /* where the block is mapped in the process. */
    uint32_t size;
    uint32_t reserved;
} MimeCameraOpened;

#endif
