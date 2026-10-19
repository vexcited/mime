#ifndef MIME_HOST_CAMERA_BACKEND_H
#define MIME_HOST_CAMERA_BACKEND_H

/*
 * what the camera service needs of a source of cameras: the real ones of the host, v4l2 on linux, and the fake one
 * that tests select with MIME_FAKE_CAMERA.
 */
#include "../../include/mime/abi.h"
#include "../../include/mime/camera.h"
#include "../mime_host.h"

typedef struct CameraBackend CameraBackend;

typedef struct CameraSource {
    MimeCameraInfo info; /* id and generation belong to the service */
    MimeCameraFormat *formats;
    uint32_t format_count;
    const CameraBackend *backend;
    char key[192]; /* tells a camera from another across scans: its device node and where it is plugged */
    char path[128]; /* what the backend opens */
} CameraSource;

typedef struct CameraList {
    CameraSource *source;
    uint32_t count, capacity;
} CameraList;

/* adds a camera to a list being built, which owns the format array from now on. */
CameraSource *camera_list_add(CameraList *list);

/* frames come with the host's monotonic clock at their capture; flags are MIME_CAMERA_FRAME_*. */
typedef void (*CameraDeliver)(void *ctx, const uint8_t *data, size_t length, uint64_t host_ns, uint32_t flags);
/* the camera stopped sending: lost is true when it was unplugged. */
typedef void (*CameraFailed)(void *ctx, bool lost);

struct CameraBackend {
    const char *name;
    /* adds the cameras plugged in now. */
    void (*scan)(CameraList *list);
    /* a descriptor that becomes readable when the cameras may have changed, or -1. */
    int (*watch_fd)(void);
    void (*watch_drain)(void);
    /* how often to look again when there is no descriptor to wait on, in milliseconds, or null. */
    int (*poll_ms)(void);
    /*
     * starts streaming a mode, delivering frames from a thread of its own until stop. null when it cannot, with a
     * MIME_HC_STATUS_* in error: exists when another program has the camera.
     */
    void *(*start)(const CameraSource *source, const MimeCameraFormat *format, CameraDeliver deliver,
                   CameraFailed failed, void *ctx, uint64_t *error);
    /* returns once no frame is being delivered and none will be. */
    void (*stop)(void *stream);
    /* the camera's controls, answered in place; the status field says what went wrong. */
    void (*control)(const CameraSource *source, MimeCameraControl *control);
};

extern const CameraBackend camera_backend_fake;
#ifdef __linux__
extern const CameraBackend camera_backend_v4l2;
#endif

uint64_t camera_now_ns(void);

#endif
