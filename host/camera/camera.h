#ifndef MIME_HOST_CAMERA_H
#define MIME_HOST_CAMERA_H

#include "../vmm/vmm.h"

/* camera hypercalls returning MIME_HC_STATUS_*. shared layouts are in mime/camera.h. */

/* notifies the guest of camera changes or available frames; callable from any thread. */
typedef void (*MimeCameraNotify)(uint32_t kind, uint32_t stream);

/* starts looking at the host's cameras. notify is called from a thread of the camera service. */
void mime_camera_init(MimeCameraNotify notify);

uint64_t mime_camera_list(MimeVm *vm, uint64_t pa, uint64_t capacity, uint64_t *count);
uint64_t mime_camera_formats(MimeVm *vm, uint64_t camera, uint64_t pa, uint64_t capacity, uint64_t *count);
uint64_t mime_camera_open(MimeVm *vm, uint64_t camera, uint64_t pa, uint64_t size, uint64_t *stream);
uint64_t mime_camera_start(uint64_t stream, uint64_t running);
void mime_camera_close(uint64_t stream);
uint64_t mime_camera_control(MimeVm *vm, uint64_t pa);
void mime_camera_close_all(void);

#endif
