/*
 * the host half of the vulkan wire, for the generated decoders and the
 * hand written calls: a connection per guest process, its handle table and
 * the calls being run.
 */
#ifndef MIME_MVK_HOST_H
#define MIME_MVK_HOST_H

#include "../vmm/vmm.h"

#include <mime/mvk_wire.h>

#include "mvk_protocol.h"
#include "mvk_dispatch.h"

typedef struct MvkConnection MvkConnection;

typedef struct MvkCall {
    MvkDecoder in;
    MvkConnection *conn;
    const MvkDispatch *fn;
    VkCommandBuffer command_buffer; /* while a recorded stream is replayed */
    MvkCallHeader header;
    MvkEncoder out;
    int replying;
} MvkCall;

typedef void (*MvkRun)(MvkCall *c);

extern const MvkRun mvk_generated_commands[MVK_OP_GENERATED_COUNT];
extern const char *const mvk_generated_names[MVK_OP_GENERATED_COUNT];

uint64_t mvk_get_handle(MvkDecoder *d, int dispatchable);
void mvk_bind_id(MvkCall *c, uint64_t id, uint64_t handle);
uint64_t mvk_host_id(MvkEncoder *e, uint64_t handle);
void mvk_check(MvkCall *c, VkResult result, const char *what);
void mvk_unsupported(MvkCall *c, const char *what);
MvkEncoder *mvk_reply_begin(MvkCall *c);
void mvk_reply_end(MvkCall *c);

void *mvk_get_chain(MvkDecoder *d);
void *mvk_get_out_shape(MvkDecoder *d, size_t size);
void mvk_get_out_shape_into(MvkDecoder *d, void *dst, size_t size);
void mvk_reply_chain(MvkEncoder *e, const void *next);
void mvk_reply_chain_struct(MvkEncoder *e, const VkBaseOutStructure *s);

/* the guest side encoders also build on the host; these never run there. */
void mvk_put_chain(MvkEncoder *e, const void *next);
uint64_t mvk_dispatch_id(const void *object);

#endif
