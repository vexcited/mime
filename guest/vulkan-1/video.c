/*
 * video decoding through the host, see include/mime/mvk_video.h: private commands direct3d 11 finds through
 * vkGetDeviceProcAddr, sending dxva buffers over the wire to the host's video decode queue.
 */
#include "mvk_guest.h"

#include <mime/mvk_video.h>

/* fills at most capacity profiles; count gets the total. */
VKAPI_ATTR VkResult VKAPI_CALL vkMxVideoGetCaps(VkDevice device, uint32_t *count, MvkVideoCaps *caps, uint32_t capacity)
{
    *count = 0;
    MvkEncoder *e = mvk_call_begin(MVK_OP_VIDEO_CAPS);
    MvkDecoder *d = mvk_call_wait(e);
    if (!d)
        return VK_ERROR_INITIALIZATION_FAILED;
    VkResult result = (VkResult)mvk_get_u32(d);
    uint32_t n = result == VK_SUCCESS ? mvk_get_u32(d) : 0;
    for (uint32_t i = 0; i < n; i++) {
        MvkVideoCaps c;
        mvk_get_bytes(d, &c, sizeof(c));
        if (caps && i < capacity)
            caps[i] = c;
    }
    mvk_call_done(d);
    *count = n;
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMxVideoCreateSession(VkDevice device, const MvkVideoSessionInfo *info, uint64_t *session)
{
    uint64_t id = mvk_new_id();
    MvkEncoder *e = mvk_call_begin(MVK_OP_VIDEO_CREATE);
    mvk_put_u64(e, id);
    mvk_put_u32(e, info->codec);
    mvk_put_u32(e, info->profile);
    mvk_put_u32(e, info->bit_depth);
    mvk_put_u32(e, info->width);
    mvk_put_u32(e, info->height);
    MvkDecoder *d = mvk_call_wait(e);
    VkResult result = d ? (VkResult)mvk_get_u32(d) : VK_ERROR_INITIALIZATION_FAILED;
    mvk_call_done(d);
    if (result != VK_SUCCESS) {
        mvk_free_id(id);
        return result;
    }
    *session = id;
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkMxVideoDestroySession(VkDevice device, uint64_t session)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_VIDEO_DESTROY);
    mvk_put_u64(e, session);
    mvk_call_end(e);
    mvk_free_id(session);
}

/* no reply: the host runs it in order and signals the semaphore. */
VKAPI_ATTR VkResult VKAPI_CALL vkMxVideoDecode(VkDevice device, const MvkVideoDecodeInfo *info, uint64_t picture_image,
                                                 uint32_t picture_layer)
{
    MvkEncoder *e = mvk_call_begin(MVK_OP_VIDEO_DECODE);
    if (!e)
        return VK_ERROR_INITIALIZATION_FAILED;
    mvk_put_u64(e, info->session);
    mvk_put_u64(e, info->picture_view);
    mvk_put_u64(e, picture_image);
    mvk_put_u32(e, picture_layer);
    mvk_put_u32(e, 0);
    mvk_put_u64(e, info->wait_semaphore);
    mvk_put_u64(e, info->wait_value);
    mvk_put_u64(e, info->signal_semaphore);
    mvk_put_u64(e, info->signal_value);
    mvk_put_u32(e, info->slice_count);
    mvk_put_u32(e, 0);
    mvk_put_blob(e, info->params, info->params_size);
    mvk_put_blob(e, info->qmatrix, info->qmatrix_size);
    mvk_put_blob(e, info->slices, info->slices_size);
    mvk_put_blob(e, info->bitstream, info->bitstream_size);
    if (e->overflow) {
        mvk_log("vulkan: a video picture too large for the ring");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    mvk_call_end(e);
    return VK_SUCCESS;
}

/* for vkGetDeviceProcAddr. */
const MvkProc mvk_video_procs[] = {
    {"vkMxVideoGetCaps", (PFN_vkVoidFunction)vkMxVideoGetCaps},
    {"vkMxVideoCreateSession", (PFN_vkVoidFunction)vkMxVideoCreateSession},
    {"vkMxVideoDestroySession", (PFN_vkVoidFunction)vkMxVideoDestroySession},
    {"vkMxVideoDecode", (PFN_vkVoidFunction)vkMxVideoDecode},
    {nullptr, nullptr},
};
