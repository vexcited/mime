#ifndef MIME_HOST_CAMERA_BACKEND_H
#define MIME_HOST_CAMERA_BACKEND_H

/* camera backends: V4L2 on Linux and a synthetic source selected by MIME_FAKE_CAMERA. */
#include "../../include/mime/abi.h"
#include "../../include/mime/camera.h"
#include "../mime_host.h"

typedef struct CameraBackend CameraBackend;

typedef struct CameraSource {
    MimeCameraInfo info; /* id and generation belong to the service. */
    MimeCameraFormat *formats;
    uint32_t format_count;
    const CameraBackend *backend;
    char key[192]; /* stable identity across scans: device node and physical connection. */
    char path[128]; /* what the backend opens. */
} CameraSource;

typedef struct CameraList {
    CameraSource *source;
    uint32_t count, capacity;
} CameraList;

/* adds a camera to the list, transferring ownership of its format array. */
CameraSource *camera_list_add(CameraList *list);

/* frames come with the host's monotonic clock at their capture; flags are MIME_CAMERA_FRAME_*. */
typedef void (*CameraDeliver)(void *ctx, const uint8_t *data, size_t length, uint64_t host_ns, uint32_t flags);
/* reports streaming stopped; lost means the device was unplugged. */
typedef void (*CameraFailed)(void *ctx, bool lost);

struct CameraBackend {
    const char *name;
    void (*scan)(CameraList *list);
    /* a descriptor that becomes readable when the cameras may have changed, or -1. */
    int (*watch_fd)(void);
    /* drains the descriptor and returns whether this backend's cameras changed. */
    bool (*watch_drain)(void);
    /* poll interval in milliseconds when no descriptor is available, or null. */
    int (*poll_ms)(void);
    /*
     * starts delivery on a backend thread until stop. returns null with a MIME_HC_STATUS_* error on failure; EXISTS
     * means another program owns the camera.
     */
    void *(*start)(const CameraSource *source, const MimeCameraFormat *format, CameraDeliver deliver,
                   CameraFailed failed, void *ctx, uint64_t *error);
    /* returns once no frame is being delivered and none will be. */
    void (*stop)(void *stream);
    /* updates controls in place, reporting errors in status. */
    void (*control)(const CameraSource *source, MimeCameraControl *control);
};

extern const CameraBackend camera_backend_fake;
#ifdef __linux__
extern const CameraBackend camera_backend_v4l2;
#endif

uint64_t camera_now_ns(void);

#endif
