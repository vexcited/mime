/*
 * descriptor update templates. the guest keeps each template and turns an
 * update through it into the plain writes it stands for, so the host sees
 * vkUpdateDescriptorSets and vkCmdPushDescriptorSet only.
 */
#include "mvk_guest.h"

typedef struct Template {
    uint32_t count;
    VkDescriptorUpdateTemplateEntry *entries;
    VkDescriptorUpdateTemplateType type;
    VkPipelineBindPoint bind_point;
    VkPipelineLayout layout;
    uint32_t set;
} Template;

VKAPI_ATTR VkResult VKAPI_CALL mvk_raw_vkCreateDescriptorUpdateTemplate(VkDevice device,
                                                                       const VkDescriptorUpdateTemplateCreateInfo *info,
                                                                       const VkAllocationCallbacks *allocator,
                                                                       VkDescriptorUpdateTemplate *out);
VKAPI_ATTR void VKAPI_CALL mvk_raw_vkDestroyDescriptorUpdateTemplate(VkDevice device,
                                                                    VkDescriptorUpdateTemplate tmpl,
                                                                    const VkAllocationCallbacks *allocator);

static SRWLOCK g_lock = SRWLOCK_INIT;
static Template **g_templates; /* by id, the ids being small */
static size_t g_capacity;

static Template *find(VkDescriptorUpdateTemplate tmpl)
{
    uint64_t id = (uint64_t)tmpl;
    AcquireSRWLockShared(&g_lock);
    Template *t = id < g_capacity ? g_templates[id] : nullptr;
    ReleaseSRWLockShared(&g_lock);
    return t;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorUpdateTemplate(VkDevice device,
                                                               const VkDescriptorUpdateTemplateCreateInfo *info,
                                                               const VkAllocationCallbacks *allocator,
                                                               VkDescriptorUpdateTemplate *out)
{
    Template *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
    VkDescriptorUpdateTemplateEntry *entries =
        HeapAlloc(GetProcessHeap(), 0, (info->descriptorUpdateEntryCount + 1) * sizeof(*entries));
    if (!t || !entries) {
        HeapFree(GetProcessHeap(), 0, t);
        HeapFree(GetProcessHeap(), 0, entries);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(entries, info->pDescriptorUpdateEntries, info->descriptorUpdateEntryCount * sizeof(*entries));
    t->count = info->descriptorUpdateEntryCount;
    t->entries = entries;
    t->type = info->templateType;
    t->bind_point = info->pipelineBindPoint;
    t->layout = info->pipelineLayout;
    t->set = info->set;
    VkResult r = mvk_raw_vkCreateDescriptorUpdateTemplate(device, info, allocator, out);
    if (r != VK_SUCCESS) {
        HeapFree(GetProcessHeap(), 0, entries);
        HeapFree(GetProcessHeap(), 0, t);
        return r;
    }
    uint64_t id = (uint64_t)*out;
    AcquireSRWLockExclusive(&g_lock);
    if (id >= g_capacity) {
        size_t n = g_capacity ? g_capacity : 1024;
        while (n <= id)
            n *= 2;
        Template **list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n * sizeof(*list));
        if (list && g_templates)
            memcpy(list, g_templates, g_capacity * sizeof(*list));
        if (list) {
            HeapFree(GetProcessHeap(), 0, g_templates);
            g_templates = list;
            g_capacity = n;
        }
    }
    if (id < g_capacity)
        g_templates[id] = t;
    ReleaseSRWLockExclusive(&g_lock);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorUpdateTemplate(VkDevice device, VkDescriptorUpdateTemplate tmpl,
                                                            const VkAllocationCallbacks *allocator)
{
    if (!tmpl)
        return;
    uint64_t id = (uint64_t)tmpl;
    AcquireSRWLockExclusive(&g_lock);
    Template *t = id < g_capacity ? g_templates[id] : nullptr;
    if (t)
        g_templates[id] = nullptr;
    ReleaseSRWLockExclusive(&g_lock);
    if (t) {
        HeapFree(GetProcessHeap(), 0, t->entries);
        HeapFree(GetProcessHeap(), 0, t);
    }
    mvk_raw_vkDestroyDescriptorUpdateTemplate(device, tmpl, allocator);
}

/*
 * the writes a template stands for, with their image, buffer and view
 * arrays gathered from the strided data into the scratch arrays given.
 */
typedef struct Writes {
    VkWriteDescriptorSet *writes;
    VkDescriptorImageInfo *images;
    VkDescriptorBufferInfo *buffers;
    VkBufferView *views;
    VkWriteDescriptorSetInlineUniformBlock *inline_blocks;
    VkWriteDescriptorSetAccelerationStructureKHR *structures;
    VkAccelerationStructureKHR *structure_handles;
    uint32_t count;
} Writes;

static BOOL build(const Template *t, VkDescriptorSet set, const void *data, Writes *w)
{
    uint32_t images = 0, buffers = 0, views = 0, blocks = 0, structures = 0;
    for (uint32_t i = 0; i < t->count; i++) {
        const VkDescriptorUpdateTemplateEntry *e = &t->entries[i];
        switch (e->descriptorType) {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            images += e->descriptorCount;
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            views += e->descriptorCount;
            break;
        case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
            blocks++;
            break;
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            structures += e->descriptorCount;
            break;
        default:
            buffers += e->descriptorCount;
            break;
        }
    }
    HANDLE heap = GetProcessHeap();
    w->writes = HeapAlloc(heap, HEAP_ZERO_MEMORY, (t->count + 1) * sizeof(*w->writes));
    w->images = HeapAlloc(heap, 0, (images + 1) * sizeof(*w->images));
    w->buffers = HeapAlloc(heap, 0, (buffers + 1) * sizeof(*w->buffers));
    w->views = HeapAlloc(heap, 0, (views + 1) * sizeof(*w->views));
    w->inline_blocks = HeapAlloc(heap, HEAP_ZERO_MEMORY, (blocks + 1) * sizeof(*w->inline_blocks));
    w->structures = HeapAlloc(heap, HEAP_ZERO_MEMORY, (t->count + 1) * sizeof(*w->structures));
    w->structure_handles = HeapAlloc(heap, 0, (structures + 1) * sizeof(*w->structure_handles));
    if (!w->writes || !w->images || !w->buffers || !w->views || !w->inline_blocks || !w->structures ||
        !w->structure_handles)
        return FALSE;
    images = buffers = views = blocks = structures = 0;
    for (uint32_t i = 0; i < t->count; i++) {
        const VkDescriptorUpdateTemplateEntry *e = &t->entries[i];
        VkWriteDescriptorSet *wr = &w->writes[i];
        const uint8_t *src = (const uint8_t *)data + e->offset;
        wr->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr->dstSet = set;
        wr->dstBinding = e->dstBinding;
        wr->dstArrayElement = e->dstArrayElement;
        wr->descriptorCount = e->descriptorCount;
        wr->descriptorType = e->descriptorType;
        switch (e->descriptorType) {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            wr->pImageInfo = &w->images[images];
            for (uint32_t k = 0; k < e->descriptorCount; k++)
                memcpy(&w->images[images++], src + k * e->stride, sizeof(VkDescriptorImageInfo));
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            wr->pTexelBufferView = &w->views[views];
            for (uint32_t k = 0; k < e->descriptorCount; k++)
                memcpy(&w->views[views++], src + k * e->stride, sizeof(VkBufferView));
            break;
        case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: {
            /* the count is in bytes, read from the offset at once. */
            VkWriteDescriptorSetInlineUniformBlock *b = &w->inline_blocks[blocks++];
            b->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK;
            b->dataSize = e->descriptorCount;
            b->pData = src;
            wr->pNext = b;
            break;
        }
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: {
            VkWriteDescriptorSetAccelerationStructureKHR *a = &w->structures[i];
            a->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            a->accelerationStructureCount = e->descriptorCount;
            a->pAccelerationStructures = &w->structure_handles[structures];
            for (uint32_t k = 0; k < e->descriptorCount; k++)
                memcpy(&w->structure_handles[structures++], src + k * e->stride, sizeof(VkAccelerationStructureKHR));
            wr->pNext = a;
            break;
        }
        default:
            wr->pBufferInfo = &w->buffers[buffers];
            for (uint32_t k = 0; k < e->descriptorCount; k++)
                memcpy(&w->buffers[buffers++], src + k * e->stride, sizeof(VkDescriptorBufferInfo));
            break;
        }
    }
    w->count = t->count;
    return TRUE;
}

static void release(Writes *w)
{
    HANDLE heap = GetProcessHeap();
    HeapFree(heap, 0, w->writes);
    HeapFree(heap, 0, w->images);
    HeapFree(heap, 0, w->buffers);
    HeapFree(heap, 0, w->views);
    HeapFree(heap, 0, w->inline_blocks);
    HeapFree(heap, 0, w->structures);
    HeapFree(heap, 0, w->structure_handles);
}

VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet set,
                                                             VkDescriptorUpdateTemplate tmpl, const void *data)
{
    Template *t = find(tmpl);
    Writes w = {};
    if (t && build(t, set, data, &w))
        vkUpdateDescriptorSets(device, w.count, w.writes, 0, nullptr);
    release(&w);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate(VkCommandBuffer buffer, VkDescriptorUpdateTemplate tmpl,
                                                              VkPipelineLayout layout, uint32_t set, const void *data)
{
    Template *t = find(tmpl);
    Writes w = {};
    if (t && build(t, VK_NULL_HANDLE, data, &w))
        vkCmdPushDescriptorSet(buffer, t->bind_point, layout, set, w.count, w.writes);
    release(&w);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPushDescriptorSetWithTemplate2(VkCommandBuffer buffer,
                                                               const VkPushDescriptorSetWithTemplateInfo *info)
{
    vkCmdPushDescriptorSetWithTemplate(buffer, info->descriptorUpdateTemplate, info->layout, info->set, info->pData);
}
