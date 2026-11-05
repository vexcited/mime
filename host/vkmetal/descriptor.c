/*
 * descriptor sets as Metal 3 argument buffers: each element is one 8 byte resource id or gpu address, at the
 * slots SPIRV-Cross gives the set's bindings in its argument buffer struct. a combined image sampler takes its
 * textures' slots, then its samplers'. inline uniform blocks are a pointer to bytes after the slots.
 */
#include "vkm.h"

#define SET_ALIGN 64

static uint32_t binding_slots(VkDescriptorType type, uint32_t count)
{
    switch (type) {
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return 2 * count;
    case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: return 1;
    default: return count;
    }
}

static int binding_order(const void *a, const void *b)
{
    const VkmBinding *x = a, *y = b;
    return x->binding < y->binding ? -1 : x->binding > y->binding;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorSetLayout(VkDevice device,
                                                                const VkDescriptorSetLayoutCreateInfo *info,
                                                                const VkAllocationCallbacks *a,
                                                                VkDescriptorSetLayout *out)
{
    VkDescriptorSetLayout layout = vkm_zalloc(sizeof(*layout));
    if (!layout)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    layout->refs = 1;
    layout->flags = info->flags;
    layout->count = info->bindingCount;
    layout->bindings = vkm_zalloc(sizeof(VkmBinding) * MIME_MAX(info->bindingCount, 1u));
    const VkDescriptorSetLayoutBindingFlagsCreateInfo *flags =
        vkm_find(info->pNext, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
    for (uint32_t i = 0; i < info->bindingCount; i++) {
        const VkDescriptorSetLayoutBinding *b = &info->pBindings[i];
        VkmBinding *o = &layout->bindings[i];
        o->binding = b->binding;
        o->type = b->descriptorType;
        o->count = b->descriptorCount;
        o->stages = b->stageFlags;
        o->flags = flags && i < flags->bindingCount ? flags->pBindingFlags[i] : 0;
        bool sampler = b->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
                       b->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        if (sampler && b->pImmutableSamplers && b->descriptorCount) {
            o->immutable = malloc(sizeof(VkSampler) * b->descriptorCount);
            if (o->immutable)
                memcpy(o->immutable, b->pImmutableSamplers, sizeof(VkSampler) * b->descriptorCount);
        }
    }
    qsort(layout->bindings, layout->count, sizeof(VkmBinding), binding_order);
    uint32_t slot = 0;
    for (uint32_t i = 0; i < layout->count; i++) {
        layout->bindings[i].slot = slot;
        slot += binding_slots(layout->bindings[i].type, layout->bindings[i].count);
    }
    layout->slots = slot;
    /* a descriptor buffer binds embedded samplers on their own, from memory the layout keeps. */
    if (info->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_EMBEDDED_IMMUTABLE_SAMPLERS_BIT_EXT) {
        layout->device = device;
        layout->embedded = MTL_MSG(id (*)(id, SEL, NSUInteger, NSUInteger), device->mtl, "newBufferWithLength:options:",
                                   MIME_MAX((uint64_t)slot * 8, (uint64_t)16),
                                   MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked);
        if (!layout->embedded) {
            vkm_set_layout_unref(layout);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        uint64_t *slots = M_PTR(layout->embedded, "contents");
        for (uint32_t i = 0; i < layout->count; i++) {
            const VkmBinding *b = &layout->bindings[i];
            uint32_t at = b->slot + (b->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ? b->count : 0);
            for (uint32_t e = 0; b->immutable && e < b->count; e++)
                slots[at + e] = b->immutable[e]->rid.impl;
        }
        layout->embedded_gpu = M_U64(layout->embedded, "gpuAddress");
        vkm_resident(device, layout->embedded);
    }
    *out = layout;
    return VK_SUCCESS;
}

void vkm_set_layout_unref(VkDescriptorSetLayout layout)
{
    if (!layout || __atomic_sub_fetch(&layout->refs, 1, __ATOMIC_ACQ_REL))
        return;
    for (uint32_t i = 0; i < layout->count; i++)
        free(layout->bindings[i].immutable);
    if (layout->embedded)
        vkm_release_later(layout->device, layout->embedded, true);
    free(layout->bindings);
    free(layout);
}

static VKAPI_ATTR void VKAPI_CALL DestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout layout,
                                                             const VkAllocationCallbacks *a)
{
    vkm_set_layout_unref(layout);
}

const VkmBinding *vkm_set_binding(VkDescriptorSetLayout layout, uint32_t binding)
{
    uint32_t lo = 0, hi = layout->count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (layout->bindings[mid].binding < binding)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < layout->count && layout->bindings[lo].binding == binding ? &layout->bindings[lo] : nullptr;
}

/* the bytes of a set: its slots, then its inline uniform blocks. */
static uint64_t set_size(VkDescriptorSetLayout layout, uint32_t variable)
{
    uint64_t slots = layout->slots, inline_bytes = 0;
    for (uint32_t i = 0; i < layout->count; i++) {
        const VkmBinding *b = &layout->bindings[i];
        bool last = i + 1 == layout->count;
        if (last && (b->flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) &&
            b->type != VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
            slots = b->slot + binding_slots(b->type, variable);
        if (b->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
            inline_bytes += MIME_ALIGN_UP((uint64_t)b->count, (uint64_t)16);
    }
    return MIME_ALIGN_UP(slots * 8, (uint64_t)16) + inline_bytes;
}

/* where a binding's inline bytes start in the set. */
static uint64_t inline_offset(VkDescriptorSet set, const VkmBinding *target)
{
    VkDescriptorSetLayout layout = set->layout;
    uint64_t at = MIME_ALIGN_UP((uint64_t)layout->slots * 8, (uint64_t)16);
    for (uint32_t i = 0; i < layout->count; i++) {
        const VkmBinding *b = &layout->bindings[i];
        if (b == target)
            return at;
        if (b->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
            at += MIME_ALIGN_UP((uint64_t)b->count, (uint64_t)16);
    }
    return at;
}

static VKAPI_ATTR void VKAPI_CALL GetDescriptorSetLayoutSupport(VkDevice device,
                                                               const VkDescriptorSetLayoutCreateInfo *info,
                                                               VkDescriptorSetLayoutSupport *out)
{
    out->supported = VK_TRUE;
    VkDescriptorSetVariableDescriptorCountLayoutSupport *variable =
        (void *)vkm_find(out->pNext, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT);
    if (variable)
        variable->maxVariableDescriptorCount = 1u << 20;
}

/* ------------------------------------------------------------------ pools. */

static VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *info,
                                                           const VkAllocationCallbacks *a, VkDescriptorPool *out)
{
    uint64_t size = (uint64_t)info->maxSets * SET_ALIGN;
    for (uint32_t i = 0; i < info->poolSizeCount; i++) {
        const VkDescriptorPoolSize *p = &info->pPoolSizes[i];
        size += p->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK ? p->descriptorCount + 16
                                                                    : (uint64_t)binding_slots(p->type, p->descriptorCount) * 8;
    }
    const VkDescriptorPoolInlineUniformBlockCreateInfo *blocks =
        vkm_find(info->pNext, VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_INLINE_UNIFORM_BLOCK_CREATE_INFO);
    if (blocks)
        size += (uint64_t)blocks->maxInlineUniformBlockBindings * 16;
    VkDescriptorPool pool = vkm_zalloc(sizeof(*pool));
    if (!pool)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    pool->flags = info->flags;
    pool->size = MIME_MAX(size, (uint64_t)SET_ALIGN);
    pool->buffer = MTL_MSG(id (*)(id, SEL, NSUInteger, NSUInteger), device->mtl, "newBufferWithLength:options:",
                           pool->size, MTLResourceStorageModeShared | MTLResourceHazardTrackingModeUntracked);
    if (!pool->buffer) {
        free(pool);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    pool->map = M_PTR(pool->buffer, "contents");
    pool->gpu = M_U64(pool->buffer, "gpuAddress");
    vkm_resident(device, pool->buffer);
    *out = pool;
    return VK_SUCCESS;
}

static void pool_forget(VkDescriptorPool pool)
{
    for (VkDescriptorSet s = pool->sets, next; s; s = next) {
        next = s->next;
        vkm_set_layout_unref(s->layout);
        free(s);
    }
    pool->sets = nullptr;
    pool->used = 0;
    pool->hole_count = 0;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDescriptorPool(VkDevice device, VkDescriptorPool pool,
                                                        const VkAllocationCallbacks *a)
{
    if (!pool)
        return;
    pool_forget(pool);
    vkm_release_later(device, pool->buffer, true);
    free(pool->holes);
    free(pool);
}

static VKAPI_ATTR VkResult VKAPI_CALL ResetDescriptorPool(VkDevice device, VkDescriptorPool pool,
                                                          VkDescriptorPoolResetFlags flags)
{
    pool_forget(pool);
    return VK_SUCCESS;
}

static bool pool_take(VkDescriptorPool pool, uint64_t size, uint64_t *offset)
{
    size = MIME_ALIGN_UP(size, (uint64_t)SET_ALIGN);
    for (uint32_t i = 0; i < pool->hole_count; i++)
        if (pool->holes[i].size >= size) {
            *offset = pool->holes[i].offset;
            pool->holes[i].offset += size;
            pool->holes[i].size -= size;
            if (!pool->holes[i].size)
                pool->holes[i] = pool->holes[--pool->hole_count];
            return true;
        }
    if (pool->used + size > pool->size)
        return false;
    *offset = pool->used;
    pool->used += size;
    return true;
}

static void pool_give(VkDescriptorPool pool, uint64_t offset, uint64_t size)
{
    size = MIME_ALIGN_UP(size, (uint64_t)SET_ALIGN);
    if (offset + size == pool->used) {
        pool->used = offset;
        return;
    }
    if (pool->hole_count == pool->hole_capacity) {
        uint32_t capacity = pool->hole_capacity ? pool->hole_capacity * 2 : 16;
        void *grown = realloc(pool->holes, capacity * sizeof(*pool->holes));
        if (!grown)
            return;
        pool->holes = grown;
        pool->hole_capacity = capacity;
    }
    pool->holes[pool->hole_count++] = (struct VkmRange){offset, size};
}

static uint64_t *slot_at(VkDescriptorSet set, uint32_t slot)
{
    return (uint64_t *)(set->map + (uint64_t)slot * 8);
}

static VKAPI_ATTR VkResult VKAPI_CALL AllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *info,
                                                             VkDescriptorSet *out)
{
    VkDescriptorPool pool = info->descriptorPool;
    const VkDescriptorSetVariableDescriptorCountAllocateInfo *variable =
        vkm_find(info->pNext, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);
    for (uint32_t i = 0; i < info->descriptorSetCount; i++) {
        VkDescriptorSetLayout layout = info->pSetLayouts[i];
        uint32_t count = variable && i < variable->descriptorSetCount ? variable->pDescriptorCounts[i] : 0;
        uint64_t size = set_size(layout, count), offset;
        VkDescriptorSet set = vkm_zalloc(sizeof(*set));
        if (!set || !pool_take(pool, size, &offset)) {
            free(set);
            for (uint32_t k = 0; k < i; k++)
                out[k] = VK_NULL_HANDLE;
            return set ? VK_ERROR_OUT_OF_POOL_MEMORY : VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        __atomic_add_fetch(&layout->refs, 1, __ATOMIC_ACQ_REL);
        set->pool = pool;
        set->layout = layout;
        set->offset = offset;
        set->size = size;
        set->map = pool->map + offset;
        set->gpu = pool->gpu + offset;
        set->variable = count;
        memset(set->map, 0, size);
        for (uint32_t b = 0; b < layout->count; b++) {
            const VkmBinding *bind = &layout->bindings[b];
            uint32_t sampler_slot = bind->slot + (bind->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ? bind->count : 0);
            if (bind->immutable)
                for (uint32_t e = 0; e < bind->count; e++)
                    *slot_at(set, sampler_slot + e) = bind->immutable[e]->rid.impl;
            if (bind->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
                *slot_at(set, bind->slot) = set->gpu + inline_offset(set, bind);
        }
        set->next = pool->sets;
        pool->sets = set;
        out[i] = set;
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL FreeDescriptorSets(VkDevice device, VkDescriptorPool pool, uint32_t count,
                                                         const VkDescriptorSet *sets)
{
    for (uint32_t i = 0; i < count; i++) {
        VkDescriptorSet set = sets[i];
        if (!set)
            continue;
        for (VkDescriptorSet *link = &pool->sets; *link; link = &(*link)->next)
            if (*link == set) {
                *link = set->next;
                break;
            }
        pool_give(pool, set->offset, set->size);
        vkm_set_layout_unref(set->layout);
        free(set);
    }
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ writes. */

typedef struct Element {
    VkSampler sampler;
    VkImageView view;
    VkBufferView texel;
    VkBuffer buffer;
    VkDeviceSize offset;
} Element;

/* a mutable binding takes the type of each write. */
static void write_element(VkDescriptorSet set, const VkmBinding *b, VkDescriptorType type, uint32_t index,
                          const Element *e)
{
    if (index >= (b->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK ? 1u : MIME_MAX(b->count, set->variable)))
        return;
    switch (b->type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT ? type : b->type) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        if (!b->immutable)
            *slot_at(set, b->slot + index) = e->sampler ? e->sampler->rid.impl : 0;
        break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        *slot_at(set, b->slot + index) = e->view ? e->view->rid.impl : 0;
        if (!b->immutable)
            *slot_at(set, b->slot + b->count + index) = e->sampler ? e->sampler->rid.impl : 0;
        break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        *slot_at(set, b->slot + index) = e->view ? e->view->rid.impl : 0;
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        *slot_at(set, b->slot + index) = e->texel ? e->texel->rid.impl : 0;
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        *slot_at(set, b->slot + index) = e->buffer ? e->buffer->gpu + e->offset : 0;
        break;
    default:
        break;
    }
}

/* the next element of a write that runs past its binding's count into the following bindings. */
static const VkmBinding *advance(VkDescriptorSet set, const VkmBinding *b, uint32_t *index)
{
    uint32_t count = MIME_MAX(b->count, set->variable);
    while (b && *index >= count) {
        *index -= count;
        const VkmBinding *end = set->layout->bindings + set->layout->count;
        b = b + 1 < end ? b + 1 : nullptr;
        count = b ? b->count : 0;
    }
    return b;
}

static void write_set(const VkWriteDescriptorSet *w)
{
    VkDescriptorSet set = w->dstSet;
    const VkmBinding *b = vkm_set_binding(set->layout, w->dstBinding);
    if (!b)
        return;
    if (w->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
        const VkWriteDescriptorSetInlineUniformBlock *block =
            vkm_find(w->pNext, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK);
        if (block && w->dstArrayElement + block->dataSize <= b->count)
            memcpy(set->map + inline_offset(set, b) + w->dstArrayElement, block->pData, block->dataSize);
        return;
    }
    uint32_t index = w->dstArrayElement;
    for (uint32_t i = 0; i < w->descriptorCount; i++, index++) {
        b = advance(set, b, &index);
        if (!b)
            return;
        Element e = {};
        switch (w->descriptorType) {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            e.sampler = w->pImageInfo[i].sampler;
            e.view = w->pImageInfo[i].imageView;
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            e.texel = w->pTexelBufferView[i];
            break;
        default:
            e.buffer = w->pBufferInfo[i].buffer;
            e.offset = w->pBufferInfo[i].offset;
            break;
        }
        write_element(set, b, w->descriptorType, index, &e);
    }
}

static void copy_set(const VkCopyDescriptorSet *c)
{
    const VkmBinding *from = vkm_set_binding(c->srcSet->layout, c->srcBinding);
    const VkmBinding *to = vkm_set_binding(c->dstSet->layout, c->dstBinding);
    if (!from || !to)
        return;
    if (from->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
        memcpy(c->dstSet->map + inline_offset(c->dstSet, to) + c->dstArrayElement,
               c->srcSet->map + inline_offset(c->srcSet, from) + c->srcArrayElement, c->descriptorCount);
        return;
    }
    uint32_t si = c->srcArrayElement, di = c->dstArrayElement;
    for (uint32_t i = 0; i < c->descriptorCount; i++, si++, di++) {
        from = advance(c->srcSet, from, &si);
        to = advance(c->dstSet, to, &di);
        if (!from || !to)
            return;
        *slot_at(c->dstSet, to->slot + di) = *slot_at(c->srcSet, from->slot + si);
        if (to->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && !to->immutable)
            *slot_at(c->dstSet, to->slot + to->count + di) = *slot_at(c->srcSet, from->slot + from->count + si);
    }
}

static VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSets(VkDevice device, uint32_t write_count,
                                                       const VkWriteDescriptorSet *writes, uint32_t copy_count,
                                                       const VkCopyDescriptorSet *copies)
{
    for (uint32_t i = 0; i < write_count; i++)
        write_set(&writes[i]);
    for (uint32_t i = 0; i < copy_count; i++)
        copy_set(&copies[i]);
}

/* ------------------------------------------------------------------ templates. */

struct VkDescriptorUpdateTemplate_T {
    uint32_t count;
    VkDescriptorUpdateTemplateEntry entries[];
};

static VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorUpdateTemplate(VkDevice device,
                                                                     const VkDescriptorUpdateTemplateCreateInfo *info,
                                                                     const VkAllocationCallbacks *a,
                                                                     VkDescriptorUpdateTemplate *out)
{
    VkDescriptorUpdateTemplate t =
        malloc(sizeof(*t) + sizeof(VkDescriptorUpdateTemplateEntry) * info->descriptorUpdateEntryCount);
    if (!t)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    t->count = info->descriptorUpdateEntryCount;
    memcpy(t->entries, info->pDescriptorUpdateEntries, sizeof(VkDescriptorUpdateTemplateEntry) * t->count);
    *out = t;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate t,
                                                                  const VkAllocationCallbacks *a)
{
    free(t);
}

static VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet set,
                                                                  VkDescriptorUpdateTemplate t, const void *data)
{
    for (uint32_t i = 0; i < t->count; i++) {
        const VkDescriptorUpdateTemplateEntry *e = &t->entries[i];
        const uint8_t *p = (const uint8_t *)data + e->offset;
        VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, e->dstBinding,
                                  e->dstArrayElement, 1, e->descriptorType};
        if (e->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
            VkWriteDescriptorSetInlineUniformBlock block = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK,
                                                            nullptr, e->descriptorCount, p};
            w.pNext = &block;
            w.descriptorCount = e->descriptorCount;
            write_set(&w);
            continue;
        }
        for (uint32_t k = 0; k < e->descriptorCount; k++, p += e->stride) {
            w.dstArrayElement = e->dstArrayElement + k;
            switch (e->descriptorType) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                w.pTexelBufferView = (const VkBufferView *)p;
                break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                w.pBufferInfo = (const VkDescriptorBufferInfo *)p;
                break;
            default:
                w.pImageInfo = (const VkDescriptorImageInfo *)p;
                break;
            }
            write_set(&w);
        }
    }
}

/* ------------------------------------------------------------------ descriptor buffers. */

/*
 * a descriptor buffer holds a set's argument buffer as the set would: 8 bytes for each element, at the slots of its
 * binding. a combined image sampler is its texture then its sampler, which is how a single one lies in a set.
 */
/* a variable count binding counts all its descriptors, as the layout declares them. */
static VKAPI_ATTR void VKAPI_CALL GetDescriptorSetLayoutSizeEXT(VkDevice device, VkDescriptorSetLayout layout,
                                                                 VkDeviceSize *size)
{
    *size = set_size(layout, layout->count ? layout->bindings[layout->count - 1].count : 0);
}

static VKAPI_ATTR void VKAPI_CALL GetDescriptorSetLayoutBindingOffsetEXT(VkDevice device, VkDescriptorSetLayout layout,
                                                                          uint32_t binding, VkDeviceSize *offset)
{
    const VkmBinding *b = vkm_set_binding(layout, binding);
    if (!b)
        *offset = 0;
    else if (b->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
        *offset = MIME_ALIGN_UP((uint64_t)layout->slots * 8, (uint64_t)16);
    else
        *offset = (uint64_t)b->slot * 8;
}

static uint64_t image_rid(const VkDescriptorImageInfo *info)
{
    return info && info->imageView ? info->imageView->rid.impl : 0;
}

static uint64_t texel_rid(VkDevice device, const VkDescriptorAddressInfoEXT *info)
{
    return info && info->address ? vkm_texel_rid(device, info->address, info->range, info->format).impl : 0;
}

/* a null descriptor is zero, which a shader reads as no resource. */
static VKAPI_ATTR void VKAPI_CALL GetDescriptorEXT(VkDevice device, const VkDescriptorGetInfoEXT *info, size_t size,
                                                    void *out)
{
    uint64_t words[2] = {0, 0};
    switch (info->type) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        words[0] = info->data.pSampler && *info->data.pSampler ? (*info->data.pSampler)->rid.impl : 0;
        break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
        const VkDescriptorImageInfo *image = info->data.pCombinedImageSampler;
        words[0] = image_rid(image);
        words[1] = image && image->sampler ? image->sampler->rid.impl : 0;
        break;
    }
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        words[0] = image_rid(info->data.pSampledImage);
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        words[0] = image_rid(info->data.pStorageImage);
        break;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        words[0] = image_rid(info->data.pInputAttachmentImage);
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        words[0] = texel_rid(device, info->data.pUniformTexelBuffer);
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        words[0] = texel_rid(device, info->data.pStorageTexelBuffer);
        break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        words[0] = info->data.pUniformBuffer ? info->data.pUniformBuffer->address : 0;
        break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        words[0] = info->data.pStorageBuffer ? info->data.pStorageBuffer->address : 0;
        break;
    case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
        words[0] = info->data.accelerationStructure;
        break;
    default:
        break;
    }
    memcpy(out, words, MIME_MIN(size, sizeof(words)));
}

/* ------------------------------------------------------------------ pipeline layouts. */

static VKAPI_ATTR VkResult VKAPI_CALL CreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *info,
                                                           const VkAllocationCallbacks *a, VkPipelineLayout *out)
{
    if (info->setLayoutCount > VKM_SETS)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkPipelineLayout layout = vkm_zalloc(sizeof(*layout));
    if (!layout)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    layout->refs = 1;
    layout->set_count = info->setLayoutCount;
    for (uint32_t i = 0; i < info->setLayoutCount; i++) {
        layout->sets[i] = info->pSetLayouts[i];
        if (layout->sets[i])
            __atomic_add_fetch(&layout->sets[i]->refs, 1, __ATOMIC_ACQ_REL);
    }
    for (uint32_t i = 0; i < info->pushConstantRangeCount; i++)
        layout->push_size =
            MIME_MAX(layout->push_size, info->pPushConstantRanges[i].offset + info->pPushConstantRanges[i].size);
    *out = layout;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyPipelineLayout(VkDevice device, VkPipelineLayout layout,
                                                        const VkAllocationCallbacks *a)
{
    if (!layout || __atomic_sub_fetch(&layout->refs, 1, __ATOMIC_ACQ_REL))
        return;
    for (uint32_t i = 0; i < layout->set_count; i++)
        vkm_set_layout_unref(layout->sets[i]);
    free(layout);
}

const VkmEntry vkm_descriptor_entries[] = {
    VKM_ENTRY(CreateDescriptorSetLayout),
    VKM_ENTRY(DestroyDescriptorSetLayout),
    VKM_ENTRY(GetDescriptorSetLayoutSupport),
    VKM_ALIAS("vkGetDescriptorSetLayoutSupportKHR", GetDescriptorSetLayoutSupport),
    VKM_ENTRY(CreateDescriptorPool),
    VKM_ENTRY(DestroyDescriptorPool),
    VKM_ENTRY(ResetDescriptorPool),
    VKM_ENTRY(AllocateDescriptorSets),
    VKM_ENTRY(FreeDescriptorSets),
    VKM_ENTRY(UpdateDescriptorSets),
    VKM_ENTRY(CreateDescriptorUpdateTemplate),
    VKM_ALIAS("vkCreateDescriptorUpdateTemplateKHR", CreateDescriptorUpdateTemplate),
    VKM_ENTRY(DestroyDescriptorUpdateTemplate),
    VKM_ALIAS("vkDestroyDescriptorUpdateTemplateKHR", DestroyDescriptorUpdateTemplate),
    VKM_ENTRY(UpdateDescriptorSetWithTemplate),
    VKM_ALIAS("vkUpdateDescriptorSetWithTemplateKHR", UpdateDescriptorSetWithTemplate),
    VKM_ENTRY(CreatePipelineLayout),
    VKM_ENTRY(DestroyPipelineLayout),
    VKM_ENTRY(GetDescriptorSetLayoutSizeEXT),
    VKM_ENTRY(GetDescriptorSetLayoutBindingOffsetEXT),
    VKM_ENTRY(GetDescriptorEXT),
    {nullptr, nullptr},
};
