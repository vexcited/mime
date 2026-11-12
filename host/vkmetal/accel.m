#include "vkm_cmd.h"

/* ------------------------------------------------------------------ the table of structures by address. */

/*
 * shaders have the table's address and size compiled in, so it never moves: entries are written in place, a removed
 * one becomes a tombstone the lookups probe past and the next insertion reuses.
 */
static constexpr uint32_t TABLE_ENTRIES = 1u << 17;
static constexpr uint32_t TABLE_MASK = TABLE_ENTRIES - 1;
static constexpr uint64_t TOMBSTONE = 1; /* no structure is at an address that is not 256 byte aligned. */

/*
 * a top level structure's entry also has its instances' shader binding table offsets, for ray tracing pipelines, and
 * any entry the structure's compacted size once the gpu was asked for it.
 */
typedef struct Entry {
    uint64_t address, rid, offsets, compacted;
} Entry;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static id<MTLBuffer> g_table;
static Entry *g_entries;
/* which structure wrote each entry, so destroying one leaves another's entry at the same address. */
static VkAccelerationStructureKHR *g_owners;

static uint32_t slot_of(uint64_t address)
{
    return (uint32_t)(((address >> 8) * 0x9e3779b97f4a7c15ull) >> 32) & TABLE_MASK;
}

/* under the lock. */
static void table_set(uint64_t address, uint64_t rid, uint64_t offsets, VkAccelerationStructureKHR owner)
{
    owner->slot = UINT32_MAX;
    if (!g_entries || !address)
        return;
    uint32_t free_slot = UINT32_MAX, at = slot_of(address);
    for (uint32_t n = 0; n < TABLE_ENTRIES; n++, at = (at + 1) & TABLE_MASK) {
        uint64_t a = g_entries[at].address;
        if (a == address) {
            free_slot = at;
            break;
        }
        if (a == TOMBSTONE && free_slot == UINT32_MAX)
            free_slot = at;
        if (!a) {
            if (free_slot == UINT32_MAX)
                free_slot = at;
            break;
        }
    }
    if (free_slot == UINT32_MAX) {
        VKM_ERROR("the table of acceleration structures is full");
        return;
    }
    /* the gpu reads entries while they change: the structure is there before the address that finds it. */
    if (g_entries[free_slot].rid != rid)
        __atomic_store_n(&g_entries[free_slot].compacted, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_entries[free_slot].rid, rid, __ATOMIC_RELEASE);
    __atomic_store_n(&g_entries[free_slot].offsets, offsets, __ATOMIC_RELEASE);
    __atomic_store_n(&g_entries[free_slot].address, address, __ATOMIC_RELEASE);
    g_owners[free_slot] = owner;
    owner->slot = free_slot;
}

static void table_remove(uint64_t address, VkAccelerationStructureKHR owner)
{
    if (!g_entries || !address)
        return;
    uint32_t at = slot_of(address);
    for (uint32_t n = 0; n < TABLE_ENTRIES; n++, at = (at + 1) & TABLE_MASK) {
        uint64_t a = g_entries[at].address;
        if (!a)
            return;
        if (a == address) {
            if (g_owners[at] == owner) {
                __atomic_store_n(&g_entries[at].address, TOMBSTONE, __ATOMIC_RELEASE);
                g_owners[at] = nullptr;
            }
            return;
        }
    }
}

void vkm_as_table(uint64_t *address, uint32_t *mask)
{
    pthread_mutex_lock(&g_lock);
    *address = g_table ? g_table.gpuAddress : 0;
    *mask = g_table ? TABLE_MASK : 0;
    pthread_mutex_unlock(&g_lock);
}

bool vkm_as_init(VkDevice device)
{
    pthread_mutex_lock(&g_lock);
    if (!g_table) {
        g_table = [device->mtl newBufferWithLength:sizeof(Entry) * TABLE_ENTRIES
                                           options:MTLResourceStorageModeShared |
                                                   MTLResourceHazardTrackingModeUntracked];
        g_owners = vkm_zalloc(sizeof(*g_owners) * TABLE_ENTRIES);
        if (!g_table || !g_owners) {
            [g_table release];
            g_table = nil;
            free(g_owners);
            g_owners = nullptr;
        } else {
            g_entries = g_table.contents;
            memset(g_entries, 0, sizeof(Entry) * TABLE_ENTRIES);
            g_table.label = @"acceleration structures";
        }
    }
    id<MTLBuffer> table = g_table;
    pthread_mutex_unlock(&g_lock);
    if (table)
        vkm_resident(device, table);
    return table != nil;
}

/* the structure in the memory of its buffer, when metal can place it there: retained, its size the one it took. */
static id<MTLAccelerationStructure> placed(VkDevice device, VkAccelerationStructureKHR as, uint64_t *size)
{
    VkDeviceMemory m = as->memory;
    if (!m || !m->heap || m->heap.type != MTLHeapTypePlacement || m->heap.storageMode != MTLStorageModePrivate)
        return nil;
    MTLSizeAndAlign sa = [device->mtl heapAccelerationStructureSizeAndAlignWithSize:*size];
    if (as->memory_offset % sa.align || sa.size > as->size || as->memory_offset + sa.size > m->size)
        return nil;
    id<MTLAccelerationStructure> made = [m->heap newAccelerationStructureWithSize:sa.size offset:as->memory_offset];
    if (made)
        *size = sa.size;
    return made;
}

static constexpr uint64_t POOL_HEAP = 64ull << 20;
static constexpr uint64_t POOL_MOST = 8ull << 20;

/*
 * a structure from the device's heaps, under the lock: one heap in the residency set holds thousands of them, where
 * each would be an allocation of the set otherwise. the largest are allocations of their own.
 */
static id<MTLAccelerationStructure> pooled(VkDevice device, uint64_t size, bool *resident)
{
    *resident = false;
    if (size <= POOL_MOST) {
        /* the last heaps: older ones are full, but for what was freed in them. */
        for (uint32_t n = 0, i = device->as_heap_count; n < 4 && i-- > 0; n++) {
            id<MTLAccelerationStructure> made = [device->as_heaps[i] newAccelerationStructureWithSize:size];
            if (made)
                return made;
        }
        id<MTLHeap> *grown = realloc(device->as_heaps, sizeof(*grown) * (device->as_heap_count + 1));
        if (grown) {
            device->as_heaps = grown;
            MTLHeapDescriptor *desc = [[MTLHeapDescriptor alloc] init];
            desc.type = MTLHeapTypeAutomatic;
            desc.storageMode = MTLStorageModePrivate;
            desc.hazardTrackingMode = MTLHazardTrackingModeUntracked;
            desc.size = POOL_HEAP;
            id<MTLHeap> heap = [device->mtl newHeapWithDescriptor:desc];
            [desc release];
            if (heap) {
                heap.label = @"acceleration structures";
                vkm_resident(device, heap);
                device->as_heaps[device->as_heap_count++] = heap;
                id<MTLAccelerationStructure> made = [heap newAccelerationStructureWithSize:size];
                if (made)
                    return made;
            }
        }
    }
    id<MTLAccelerationStructure> made = [device->mtl newAccelerationStructureWithSize:size];
    if (made) {
        vkm_resident(device, made);
        *resident = true;
    }
    return made;
}

void vkm_as_device_free(VkDevice device)
{
    for (uint32_t i = 0; i < device->as_heap_count; i++)
        [device->as_heaps[i] release];
    free(device->as_heaps);
    device->as_heaps = nullptr;
    device->as_heap_count = 0;
}

id<MTLAccelerationStructure> vkm_as_storage(VkDevice device, VkAccelerationStructureKHR as, uint64_t size)
{
    size = MIME_MAX(size, (uint64_t)256);
    pthread_mutex_lock(&g_lock);
    if (as->mtl && as->mtl_size >= size) {
        id<MTLAccelerationStructure> kept = as->mtl;
        pthread_mutex_unlock(&g_lock);
        return kept;
    }
    bool resident = false;
    id<MTLAccelerationStructure> made = placed(device, as, &size);
    if (!made)
        made = pooled(device, size, &resident);
    if (!made) {
        pthread_mutex_unlock(&g_lock);
        VKM_ERROR("no acceleration structure of %llu bytes", (unsigned long long)size);
        return nil;
    }
    if (as->mtl)
        vkm_release_later(device, as->mtl, as->resident);
    as->mtl = made;
    as->mtl_size = size;
    as->resident = resident;
    as->compact_done = 0;
    table_set(as->address, made.gpuResourceID._impl, as->offsets ? as->offsets.gpuAddress : 0, as);
    pthread_mutex_unlock(&g_lock);
    return made;
}

/* the gpu address of the buffer of a top level structure's offsets, made or grown to hold count instances. */
static uint64_t offsets_of(VkDevice device, VkAccelerationStructureKHR as, uint32_t count)
{
    uint64_t size = MIME_MAX((uint64_t)count, (uint64_t)64) * 4;
    pthread_mutex_lock(&g_lock);
    if (!as->offsets || as->offsets.length < size) {
        id<MTLBuffer> made = [device->mtl newBufferWithLength:size
                                                      options:MTLResourceStorageModePrivate |
                                                              MTLResourceHazardTrackingModeUntracked];
        if (!made) {
            pthread_mutex_unlock(&g_lock);
            return 0;
        }
        vkm_resident(device, made);
        if (as->offsets)
            vkm_release_later(device, as->offsets, true);
        as->offsets = made;
        if (as->mtl)
            table_set(as->address, as->mtl.gpuResourceID._impl, made.gpuAddress, as);
    }
    uint64_t address = as->offsets.gpuAddress;
    pthread_mutex_unlock(&g_lock);
    return address;
}

/* ------------------------------------------------------------------ objects. */

static VKAPI_ATTR VkResult VKAPI_CALL CreateAccelerationStructureKHR(VkDevice device,
                                                                     const VkAccelerationStructureCreateInfoKHR *info,
                                                                     const VkAllocationCallbacks *a,
                                                                     VkAccelerationStructureKHR *out)
{
    if (info->createFlags & VK_ACCELERATION_STRUCTURE_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_KHR)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    VkAccelerationStructureKHR as = vkm_zalloc(sizeof(*as));
    if (!as)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    as->address = info->buffer->gpu + info->offset;
    as->memory = info->buffer->memory;
    as->memory_offset = info->buffer->offset + info->offset;
    as->slot = UINT32_MAX;
    as->size = info->size;
    as->type = info->type;
    *out = as;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyAccelerationStructureKHR(VkDevice device, VkAccelerationStructureKHR as,
                                                                  const VkAllocationCallbacks *a)
{
    if (!as)
        return;
    pthread_mutex_lock(&g_lock);
    table_remove(as->address, as);
    pthread_mutex_unlock(&g_lock);
    vkm_release_later(device, as->mtl, as->resident);
    vkm_release_later(device, as->offsets, true);
    free(as);
}

static VKAPI_ATTR VkDeviceAddress VKAPI_CALL
GetAccelerationStructureDeviceAddressKHR(VkDevice device, const VkAccelerationStructureDeviceAddressInfoKHR *info)
{
    return info->accelerationStructure ? info->accelerationStructure->address : 0;
}

/* nothing serialized here deserializes: metal has no serialization, so programs build again. */
static VKAPI_ATTR void VKAPI_CALL
GetDeviceAccelerationStructureCompatibilityKHR(VkDevice device, const VkAccelerationStructureVersionInfoKHR *info,
                                               VkAccelerationStructureCompatibilityKHR *out)
{
    *out = VK_ACCELERATION_STRUCTURE_COMPATIBILITY_INCOMPATIBLE_KHR;
}

/* ------------------------------------------------------------------ build inputs. */

static constexpr uint64_t INSTANCE_SIZE = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);

static const VkAccelerationStructureGeometryKHR *geometry_at(const VkAccelerationStructureBuildGeometryInfoKHR *info,
                                                            uint32_t i)
{
    return info->pGeometries ? &info->pGeometries[i] : info->ppGeometries[i];
}

static uint32_t usage_of(VkBuildAccelerationStructureFlagsKHR flags)
{
    uint32_t usage = MTLAccelerationStructureUsageNone;
    if (flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR)
        usage |= MTLAccelerationStructureUsageRefit;
    if (flags & VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR)
        usage |= MTLAccelerationStructureUsagePreferFastBuild;
    else if (flags & VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR)
        usage |= MTLAccelerationStructureUsagePreferFastIntersection;
    if (flags & VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR)
        usage |= MTLAccelerationStructureUsageMinimizeMemory;
    return usage;
}

/*
 * a bottom level build's geometries, from the ranges of a build or the counts of a size query, which has no
 * addresses. false for what metal cannot build.
 */
static bool geometries_of(const VkAccelerationStructureBuildGeometryInfoKHR *info,
                          const VkAccelerationStructureBuildRangeInfoKHR *ranges, const uint32_t *counts,
                          VkmAsGeometry *out)
{
    for (uint32_t i = 0; i < info->geometryCount; i++) {
        const VkAccelerationStructureGeometryKHR *g = geometry_at(info, i);
        VkmAsGeometry *o = &out[i];
        memset(o, 0, sizeof(*o));
        o->kind = (uint8_t)g->geometryType;
        o->opaque = !!(g->flags & VK_GEOMETRY_OPAQUE_BIT_KHR);
        o->no_duplicates = !!(g->flags & VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR);
        o->count = ranges ? ranges[i].primitiveCount : counts[i];
        uint64_t offset = ranges ? ranges[i].primitiveOffset : 0;
        if (g->geometryType == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
            const VkAccelerationStructureGeometryTrianglesDataKHR *t = &g->geometry.triangles;
            o->vertex_format = vkm_format(t->vertexFormat)->vertex;
            if (!o->vertex_format) {
                VKM_ERROR("acceleration structure vertices of format %u", t->vertexFormat);
                return false;
            }
            switch (t->indexType) {
            case VK_INDEX_TYPE_NONE_KHR: break;
            case VK_INDEX_TYPE_UINT16: o->index_type = MTLIndexTypeUInt16 + 1; break;
            case VK_INDEX_TYPE_UINT32: o->index_type = MTLIndexTypeUInt32 + 1; break;
            default:
                VKM_ERROR("acceleration structure indices of type %u", t->indexType);
                return false;
            }
            o->vertex_stride = t->vertexStride;
            uint64_t first = ranges ? (uint64_t)ranges[i].firstVertex * t->vertexStride : 0;
            if (o->index_type) {
                o->index = t->indexData.deviceAddress + offset;
                o->index_length = (uint64_t)o->count * 3 * (o->index_type == MTLIndexTypeUInt16 + 1 ? 2 : 4);
                o->vertex = t->vertexData.deviceAddress + first;
                o->vertex_length = ((uint64_t)t->maxVertex + 1) * t->vertexStride;
            } else {
                o->vertex = t->vertexData.deviceAddress + offset + first;
                o->vertex_length = (uint64_t)o->count * 3 * t->vertexStride;
            }
            if (t->transformData.deviceAddress)
                o->transform = t->transformData.deviceAddress + (ranges ? ranges[i].transformOffset : 0);
        } else if (g->geometryType == VK_GEOMETRY_TYPE_AABBS_KHR) {
            o->vertex = g->geometry.aabbs.data.deviceAddress + offset;
            o->vertex_stride = g->geometry.aabbs.stride;
            o->vertex_length = (uint64_t)o->count * o->vertex_stride;
        } else {
            return false;
        }
    }
    return true;
}

/* autoreleased. */
static MTL4AccelerationStructureDescriptor *descriptor_of(const VkmAsGeometry *geometries, uint32_t count, bool top,
                                                          uint64_t instances, uint32_t instance_count,
                                                          uint64_t instance_counts, uint32_t usage)
{
    if (top && instance_counts) {
        /* the count is read on the gpu. */
        MTL4IndirectInstanceAccelerationStructureDescriptor *d =
            [[[MTL4IndirectInstanceAccelerationStructureDescriptor alloc] init] autorelease];
        d.instanceDescriptorBuffer = (MTL4BufferRange){instances, (uint64_t)instance_count * INSTANCE_SIZE};
        d.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect;
        d.maxInstanceCount = instance_count;
        d.instanceCountBuffer = (MTL4BufferRange){instance_counts, 4};
        d.instanceTransformationMatrixLayout = MTLMatrixLayoutRowMajor;
        d.usage = usage;
        return d;
    }
    if (top) {
        MTL4InstanceAccelerationStructureDescriptor *d =
            [[[MTL4InstanceAccelerationStructureDescriptor alloc] init] autorelease];
        d.instanceDescriptorBuffer = (MTL4BufferRange){instances, (uint64_t)instance_count * INSTANCE_SIZE};
        d.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect;
        d.instanceCount = instance_count;
        d.instanceTransformationMatrixLayout = MTLMatrixLayoutRowMajor;
        d.usage = usage;
        return d;
    }
    NSMutableArray<MTL4AccelerationStructureGeometryDescriptor *> *list = [NSMutableArray arrayWithCapacity:count];
    for (uint32_t i = 0; i < count; i++) {
        const VkmAsGeometry *g = &geometries[i];
        MTL4AccelerationStructureGeometryDescriptor *made;
        if (g->kind == VK_GEOMETRY_TYPE_TRIANGLES_KHR) {
            MTL4AccelerationStructureTriangleGeometryDescriptor *t =
                [[MTL4AccelerationStructureTriangleGeometryDescriptor alloc] init];
            t.vertexBuffer = (MTL4BufferRange){g->vertex, g->vertex_length};
            t.vertexFormat = (MTLAttributeFormat)g->vertex_format;
            t.vertexStride = g->vertex_stride;
            if (g->index_type) {
                t.indexBuffer = (MTL4BufferRange){g->index, g->index_length};
                t.indexType = (MTLIndexType)(g->index_type - 1);
            }
            t.triangleCount = g->count;
            if (g->transform) {
                t.transformationMatrixBuffer = (MTL4BufferRange){g->transform, 48};
                t.transformationMatrixLayout = MTLMatrixLayoutRowMajor;
            }
            made = t;
        } else {
            MTL4AccelerationStructureBoundingBoxGeometryDescriptor *b =
                [[MTL4AccelerationStructureBoundingBoxGeometryDescriptor alloc] init];
            b.boundingBoxBuffer = (MTL4BufferRange){g->vertex, g->vertex_length};
            b.boundingBoxStride = g->vertex_stride;
            b.boundingBoxCount = g->count;
            made = b;
        }
        made.opaque = g->opaque;
        made.allowDuplicateIntersectionFunctionInvocation = !g->no_duplicates;
        made.intersectionFunctionTableOffset = i;
        [list addObject:made];
        [made release];
    }
    MTL4PrimitiveAccelerationStructureDescriptor *d =
        [[[MTL4PrimitiveAccelerationStructureDescriptor alloc] init] autorelease];
    d.geometryDescriptors = list;
    d.usage = usage;
    return d;
}

static bool is_top(const VkAccelerationStructureBuildGeometryInfoKHR *info)
{
    return info->type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
}

static VKAPI_ATTR void VKAPI_CALL
GetAccelerationStructureBuildSizesKHR(VkDevice device, VkAccelerationStructureBuildTypeKHR type,
                                      const VkAccelerationStructureBuildGeometryInfoKHR *info, const uint32_t *counts,
                                      VkAccelerationStructureBuildSizesInfoKHR *out)
{
    out->accelerationStructureSize = out->buildScratchSize = out->updateScratchSize = 0;
    bool top = is_top(info);
    uint32_t instances = top && info->geometryCount ? counts[0] : 0;
    VkmAsGeometry *geometries = top ? nullptr : malloc(sizeof(*geometries) * MIME_MAX(info->geometryCount, 1u));
    if (!top && (!geometries || !geometries_of(info, nullptr, counts, geometries))) {
        free(geometries);
        return;
    }
    @autoreleasepool {
        MTL4AccelerationStructureDescriptor *d =
            descriptor_of(geometries, info->geometryCount, top, 0, instances, 0, usage_of(info->flags));
        MTLAccelerationStructureSizes s = [device->mtl accelerationStructureSizesWithDescriptor:d];
        if (top) {
            /* an indirect build of as many instances may take more. */
            d = descriptor_of(nullptr, 0, true, 0, instances, 1, usage_of(info->flags));
            MTLAccelerationStructureSizes i = [device->mtl accelerationStructureSizesWithDescriptor:d];
            s.accelerationStructureSize = MIME_MAX(s.accelerationStructureSize, i.accelerationStructureSize);
            s.buildScratchBufferSize = MIME_MAX(s.buildScratchBufferSize, i.buildScratchBufferSize);
            s.refitScratchBufferSize = MIME_MAX(s.refitScratchBufferSize, i.refitScratchBufferSize);
        }
        /* the scratch also holds the instances in metal's layout, which the build converts them to. */
        uint64_t converted = (uint64_t)instances * INSTANCE_SIZE;
        /* what the structure takes placed in a heap, so one of this size at a heap's alignment is placed. */
        MTLSizeAndAlign heap = [device->mtl
            heapAccelerationStructureSizeAndAlignWithSize:MIME_MAX(s.accelerationStructureSize, (NSUInteger)1)];
        out->accelerationStructureSize = MIME_ALIGN_UP((uint64_t)heap.size, 256);
        out->buildScratchSize = MIME_ALIGN_UP((uint64_t)s.buildScratchBufferSize, 256) + converted;
        out->updateScratchSize = MIME_ALIGN_UP((uint64_t)s.refitScratchBufferSize, 256) + converted;
        out->buildScratchSize = MIME_MAX(out->buildScratchSize, (VkDeviceSize)256);
        out->updateScratchSize = MIME_MAX(out->updateScratchSize, (VkDeviceSize)256);
    }
    free(geometries);
}

/* ------------------------------------------------------------------ builds. */

/* vulkan's instances to metal's indirect ones, their bottom level addresses to the structures the table gives. */
static const char g_instances[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct In { float m[12]; uint custom_mask, sbt_flags; ulong reference; };\n"
    "struct Out { float m[12]; uint options, mask, ift, user; ulong rid; };\n"
    "struct Entry { ulong address, rid, offsets, compacted; };\n"
    "struct P { ulong src, dst, table, offsets, range; uint count, pointers, mask; };\n"
    "kernel void meta_instances(constant P &p [[buffer(0)]], uint i [[thread_position_in_grid]]) {\n"
    "    uint count = p.count;\n"
    "    ulong src = p.src;\n"
    /* an indirect build's count and offset, of vulkan's range. */
    "    if (p.range) {\n"
    "        device const uint *r = reinterpret_cast<device const uint *>(p.range);\n"
    "        count = min(count, r[0]);\n"
    "        src += r[1];\n"
    "    }\n"
    "    if (i >= count)\n"
    "        return;\n"
    "    device const In *in = p.pointers\n"
    "        ? reinterpret_cast<device const In *>(reinterpret_cast<device const ulong *>(src)[i])\n"
    "        : reinterpret_cast<device const In *>(src) + i;\n"
    "    device Out *out = reinterpret_cast<device Out *>(p.dst) + i;\n"
    "    for (uint k = 0; k < 12; k++)\n"
    "        out->m[k] = in->m[k];\n"
    /* the four flags have metal's bits: culling off, counterclockwise front faces, opaque and not opaque. */
    "    out->options = (in->sbt_flags >> 24) & 15u;\n"
    "    out->mask = in->custom_mask >> 24;\n"
    "    out->ift = in->sbt_flags & 0xffffffu;\n"
    "    reinterpret_cast<device uint *>(p.offsets)[i] = out->ift;\n"
    "    out->user = in->custom_mask & 0xffffffu;\n"
    "    ulong address = in->reference, rid = 0;\n"
    "    device const Entry *table = reinterpret_cast<device const Entry *>(p.table);\n"
    "    uint at = uint(((address >> 8) * 0x9e3779b97f4a7c15ul) >> 32) & p.mask;\n"
    "    for (uint n = 0; n <= p.mask && address; n++, at = (at + 1) & p.mask) {\n"
    "        ulong a = table[at].address;\n"
    "        if (a == address) {\n"
    "            rid = table[at].rid;\n"
    "            break;\n"
    "        }\n"
    "        if (!a)\n"
    "            break;\n"
    "    }\n"
    /* an instance of no structure is inactive, as vulkan's of address 0 is. */
    "    out->rid = rid;\n"
    "    if (!rid)\n"
    "        out->mask = 0;\n"
    "}\n";

id<MTLComputePipelineState> vkm_meta_instances(VkDevice device)
{
    static id<MTLComputePipelineState> state;
    return vkm_meta_kernel(device, g_instances, "meta_instances", &state);
}

/* the instances' conversion, a kernel run as the generation of indirect commands is. */
static bool convert_instances(VkCommandBuffer cmd, uint64_t src, bool pointers, uint32_t count, uint64_t dst,
                              uint64_t offsets, uint64_t range)
{
    id<MTLComputePipelineState> pipeline = vkm_meta_instances(cmd->device);
    struct {
        uint64_t src, dst, table, offsets, range;
        uint32_t count, pointers, mask;
    } *p;
    uint64_t params;
    if (!pipeline || !offsets || !(p = vkm_upload(cmd, sizeof(*p), 16, &params, nullptr, nullptr)))
        return false;
    uint64_t table;
    uint32_t mask;
    vkm_as_table(&table, &mask);
    *p = (typeof(*p)){src, dst, table, offsets, range, count, pointers, mask};
    VkmOpGenerate *g = vkm_emit(cmd, VKM_OP_GENERATE, sizeof(*g));
    if (!g)
        return false;
    g->pipeline = pipeline;
    g->threads = count;
    g->params = params;
    /* the kernel took the compute encoder's pipeline and buffers. */
    vkm_forget(cmd);
    return true;
}

/*
 * an indirect build's ranges are read on the gpu, where metal wants a bottom level build's counts and addresses
 * known: a kernel writes, for each geometry, as many triangles or boxes as it may have at most. triangles become
 * indices, the range's offsets folded in, those past its count three times vertex 0, which no ray hits; boxes are
 * copied, those past the count empty. the build then takes the most of everything from the command buffer's memory.
 */
static const char g_ranges[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct P { ulong range, index, dst, transform, transform_dst, boxes; uint most, boxed, index_type, stride; };\n"
    "kernel void meta_ranges(constant P &p [[buffer(0)]], uint i [[thread_position_in_grid]]) {\n"
    "    if (i >= p.most)\n"
    "        return;\n"
    "    device const uint *r = reinterpret_cast<device const uint *>(p.range);\n"
    "    uint count = r[0], offset = r[1], first = r[2];\n"
    "    if (p.boxed) {\n"
    "        device float *o = reinterpret_cast<device float *>(p.dst) + 6 * i;\n"
    "        device const float *b = reinterpret_cast<device const float *>(p.boxes + offset + ulong(i) * p.stride);\n"
    "        for (uint k = 0; k < 6; k++)\n"
    "            o[k] = i < count ? b[k] : (k < 3 ? 1e-20f : -1e-20f);\n"
    "        return;\n"
    "    }\n"
    "    device uint *o = reinterpret_cast<device uint *>(p.dst) + 3 * i;\n"
    "    for (uint k = 0; k < 3; k++) {\n"
    "        uint v = 0;\n"
    "        if (i < count) {\n"
    "            if (p.index_type == 1)\n"
    "                v = reinterpret_cast<device const ushort *>(p.index + offset)[3 * i + k];\n"
    "            else if (p.index_type == 2)\n"
    "                v = reinterpret_cast<device const uint *>(p.index + offset)[3 * i + k];\n"
    "            else\n"
    "                v = 3 * i + k + (p.stride ? offset / p.stride : 0);\n"
    "            v += first;\n"
    "        }\n"
    "        o[k] = v;\n"
    "    }\n"
    "    if (i == 0 && p.transform) {\n"
    "        device const float *t = reinterpret_cast<device const float *>(p.transform + r[3]);\n"
    "        device float *to = reinterpret_cast<device float *>(p.transform_dst);\n"
    "        for (uint k = 0; k < 12; k++)\n"
    "            to[k] = t[k];\n"
    "    }\n"
    "}\n";

static bool indirect_geometries(VkCommandBuffer cmd, uint64_t indirect, uint32_t stride, VkmAsGeometry *geometries,
                                uint32_t count, const VkAccelerationStructureBuildGeometryInfoKHR *info)
{
    static id<MTLComputePipelineState> state;
    id<MTLComputePipelineState> pipeline = vkm_meta_kernel(cmd->device, g_ranges, "meta_ranges", &state);
    if (!pipeline)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        VkmAsGeometry *g = &geometries[i];
        if (!g->count)
            continue;
        bool boxed = g->kind == VK_GEOMETRY_TYPE_AABBS_KHR;
        struct {
            uint64_t range, index, dst, transform, transform_dst, boxes;
            uint32_t most, boxed, index_type, stride;
        } *p;
        uint64_t params, made, words = boxed ? 24ull * g->count : 12ull * g->count;
        if (!(p = vkm_upload(cmd, sizeof(*p), 16, &params, nullptr, nullptr)) ||
            !vkm_upload(cmd, words + 48, 16, &made, nullptr, nullptr))
            return false;
        *p = (typeof(*p)){indirect + (uint64_t)i * stride, g->index, made, g->transform, made + words, g->vertex,
                          g->count, boxed, g->index_type, (uint32_t)g->vertex_stride};
        VkmOpGenerate *o = vkm_emit(cmd, VKM_OP_GENERATE, sizeof(*o));
        if (!o)
            return false;
        o->pipeline = pipeline;
        o->threads = g->count;
        o->params = params;
        if (boxed) {
            g->vertex = made;
            g->vertex_stride = 24;
            g->vertex_length = words;
        } else {
            const VkAccelerationStructureGeometryTrianglesDataKHR *t = &geometry_at(info, i)->geometry.triangles;
            g->vertex_length = MIME_MAX(g->vertex_length, ((uint64_t)t->maxVertex + 1) * t->vertexStride);
            g->index = made;
            g->index_length = words;
            g->index_type = MTLIndexTypeUInt32 + 1;
            if (g->transform)
                g->transform = made + words;
        }
    }
    vkm_forget(cmd);
    return true;
}

/* with an address, an indirect build: its ranges are there, stride bytes apart, and the counts are the most. */
static void record_build(VkCommandBuffer cmd, const VkAccelerationStructureBuildGeometryInfoKHR *info,
                         const VkAccelerationStructureBuildRangeInfoKHR *ranges, uint64_t indirect, uint32_t stride,
                         const uint32_t *most)
{
    VkDevice device = cmd->device;
    VkAccelerationStructureKHR dst = info->dstAccelerationStructure;
    if (!dst)
        return;
    bool top = is_top(info), update = info->mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    uint32_t count = top ? 0 : info->geometryCount;
    size_t size = sizeof(VkmOpAsBuild) + sizeof(VkmAsGeometry) * count;
    /* filled here and copied in last, as the conversion's command comes first and may move the list. */
    VkmOpAsBuild *b = vkm_zalloc(size);
    if (!b)
        return;
    if (!top && (!geometries_of(info, ranges, most, b->geometries) ||
                 (indirect && !indirect_geometries(cmd, indirect, stride, b->geometries, count, info)))) {
        free(b);
        return;
    }
    b->dst = dst;
    b->src = update ? info->srcAccelerationStructure : nullptr;
    b->update = update && b->src;
    b->top = top;
    b->usage = usage_of(info->flags);
    b->geometry_count = count;
    b->instance_count = top && info->geometryCount ? (indirect ? most[0] : ranges[0].primitiveCount) : 0;
    b->instance_counts = top ? indirect : 0;
    bool stored;
    @autoreleasepool {
        MTL4AccelerationStructureDescriptor *d =
            descriptor_of(b->geometries, count, top, 0, b->instance_count, b->instance_counts, b->usage);
        MTLAccelerationStructureSizes s = [device->mtl accelerationStructureSizesWithDescriptor:d];
        uint64_t scratch = b->update ? s.refitScratchBufferSize : s.buildScratchBufferSize;
        b->scratch = info->scratchData.deviceAddress;
        b->scratch_size = MIME_MAX(scratch, (uint64_t)1);
        if (top)
            b->instances = b->scratch + MIME_ALIGN_UP(scratch, (uint64_t)256);
        stored = vkm_as_storage(device, dst, s.accelerationStructureSize) != nil;
        /* what it compacts to is asked again. */
        dst->compact_done = 0;
    }
    if (stored && top) {
        dst->instances = b->instance_count;
        const VkAccelerationStructureGeometryInstancesDataKHR *in = &geometry_at(info, 0)->geometry.instances;
        stored = !b->instance_count ||
                 convert_instances(cmd, in->data.deviceAddress + (indirect ? 0 : ranges[0].primitiveOffset),
                                   in->arrayOfPointers, b->instance_count, b->instances,
                                   offsets_of(device, dst, b->instance_count), indirect);
    }
    VkmOpAsBuild *o = stored ? vkm_emit(cmd, VKM_OP_AS_BUILD, size) : nullptr;
    if (o)
        memcpy((uint8_t *)o + sizeof(VkmOp), (uint8_t *)b + sizeof(VkmOp), size - sizeof(VkmOp));
    free(b);
}

static VKAPI_ATTR void VKAPI_CALL
CmdBuildAccelerationStructuresKHR(VkCommandBuffer cmd, uint32_t count,
                                  const VkAccelerationStructureBuildGeometryInfoKHR *infos,
                                  const VkAccelerationStructureBuildRangeInfoKHR *const *ranges)
{
    if (cmd->state->rendering) {
        VKM_ERROR("an acceleration structure build inside a render pass");
        return;
    }
    for (uint32_t i = 0; i < count; i++)
        record_build(cmd, &infos[i], ranges[i], 0, 0, nullptr);
}

static VKAPI_ATTR void VKAPI_CALL
CmdBuildAccelerationStructuresIndirectKHR(VkCommandBuffer cmd, uint32_t count,
                                          const VkAccelerationStructureBuildGeometryInfoKHR *infos,
                                          const VkDeviceAddress *addresses, const uint32_t *strides,
                                          const uint32_t *const *max_counts)
{
    if (cmd->state->rendering) {
        VKM_ERROR("an acceleration structure build inside a render pass");
        return;
    }
    for (uint32_t i = 0; i < count; i++)
        record_build(cmd, &infos[i], nullptr, addresses[i], strides[i], max_counts[i]);
}

void vkm_encode_as_build(VkDevice device, id<MTL4ComputeCommandEncoder> c, const VkmOpAsBuild *o)
{
    if (!o->dst || !o->dst->mtl)
        return;
    @autoreleasepool {
        if (o->top)
            [c barrierAfterEncoderStages:MTLStageDispatch
                     beforeEncoderStages:MTLStageAccelerationStructure
                       visibilityOptions:MTL4VisibilityOptionDevice];
        MTL4AccelerationStructureDescriptor *d =
            descriptor_of(o->geometries, o->geometry_count, o->top, o->instances, o->instance_count,
                          o->instance_counts, o->usage);
        MTL4BufferRange scratch = {o->scratch, o->scratch_size};
        if (o->update && o->src->mtl)
            [c refitAccelerationStructure:o->src->mtl descriptor:d destination:o->dst->mtl scratchBuffer:scratch];
        else
            [c buildAccelerationStructure:o->dst->mtl descriptor:d scratchBuffer:scratch];
    }
}

/* ------------------------------------------------------------------ copies and queries. */

static VKAPI_ATTR void VKAPI_CALL CmdCopyAccelerationStructureKHR(VkCommandBuffer cmd,
                                                                  const VkCopyAccelerationStructureInfoKHR *info)
{
    VkAccelerationStructureKHR src = info->src, dst = info->dst;
    if (info->mode != VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR &&
        info->mode != VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR) {
        VKM_ERROR("an acceleration structure copy of mode %u", info->mode);
        return;
    }
    if (!src->mtl) {
        VKM_ERROR("a copy of an acceleration structure nothing was built into");
        return;
    }
    bool compact = info->mode == VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
    uint64_t size = src->mtl_size;
    if (compact) {
        /*
         * the compacted size the gpu wrote when it is there, as it is once a program read it back to size the copy;
         * else the size the program gave the copy, which holds it by vulkan's rules.
         */
        uint64_t known = 0;
        pthread_mutex_lock(&g_lock);
        if (src->compact_done && src->slot != UINT32_MAX &&
            vkm_event_wait(src->compact_queue->event, src->compact_done, 0))
            known = (uint32_t)__atomic_load_n(&g_entries[src->slot].compacted, __ATOMIC_ACQUIRE);
        pthread_mutex_unlock(&g_lock);
        size = known ? known : MIME_MIN(size, MIME_MAX(dst->size, (uint64_t)256));
    }
    if (!vkm_as_storage(cmd->device, dst, size))
        return;
    dst->instances = src->instances;
    if (src->offsets && src->instances) {
        /* a top level copy has the offsets of its instances too. */
        uint64_t to = offsets_of(cmd->device, dst, src->instances);
        VkmOpCopyBuffer *c = to ? vkm_emit(cmd, VKM_OP_COPY_BUFFER, sizeof(*c)) : nullptr;
        if (c) {
            c->src = src->offsets;
            c->dst = dst->offsets;
            c->size = (uint64_t)src->instances * 4;
        }
    }
    VkmOpAsCopy *o = vkm_emit(cmd, VKM_OP_AS_COPY, sizeof(*o));
    if (!o)
        return;
    o->src = src;
    o->dst = dst;
    o->compact = compact;
}

void vkm_encode_as_copy(id<MTL4ComputeCommandEncoder> c, const VkmOpAsCopy *o)
{
    if (!o->src->mtl || !o->dst->mtl)
        return;
    if (o->compact)
        [c copyAndCompactAccelerationStructure:o->src->mtl toAccelerationStructure:o->dst->mtl];
    else
        [c copyAccelerationStructure:o->src->mtl toAccelerationStructure:o->dst->mtl];
}

/* vulkan's header of serialized data: two uuids, the serialized and deserialized sizes, then bottom level handles. */
static uint64_t serialized_size(VkAccelerationStructureKHR as)
{
    return 2 * VK_UUID_SIZE + 3 * 8 + 8ull * as->instances;
}

static VKAPI_ATTR void VKAPI_CALL
CmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer cmd, const VkCopyAccelerationStructureToMemoryInfoKHR *info)
{
    /* the header alone, which no device takes back as compatible. */
    uint64_t size = serialized_size(info->src);
    VkDeviceMemory m = vkm_memory_at(cmd->device, info->dst.deviceAddress);
    id<MTLBuffer> buffer;
    uint64_t offset;
    uint8_t *header = vkm_upload(cmd, size, 16, nullptr, &buffer, &offset);
    if (!m || !header)
        return;
    memset(header, 0, size);
    uint64_t sizes[3] = {size, info->src->mtl_size, info->src->instances};
    memcpy(header + 2 * VK_UUID_SIZE, sizes, sizeof(sizes));
    VkmOpCopyBuffer *o = vkm_emit(cmd, VKM_OP_COPY_BUFFER, sizeof(*o));
    if (!o)
        return;
    o->src = buffer;
    o->src_offset = offset;
    o->dst = m->buffer;
    o->dst_offset = info->dst.deviceAddress - m->gpu;
    o->size = size;
}

static VKAPI_ATTR void VKAPI_CALL
CmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer cmd, const VkCopyMemoryToAccelerationStructureInfoKHR *info)
{
    VKM_ERROR("a deserialization of an acceleration structure, which no data is compatible with");
}

static VKAPI_ATTR void VKAPI_CALL
CmdWriteAccelerationStructuresPropertiesKHR(VkCommandBuffer cmd, uint32_t count,
                                            const VkAccelerationStructureKHR *structures, VkQueryType type,
                                            VkQueryPool pool, uint32_t first)
{
    /* after the pool's reset, as query commands are in vulkan. */
    vkm_emit(cmd, VKM_OP_BARRIER, sizeof(VkmOpBarrier));
    for (uint32_t i = 0; i < count; i++) {
        VkAccelerationStructureKHR as = structures[i];
        if (first + i >= pool->count)
            return;
        /* what the cpu knows is copied on the gpu's timeline, after the pool's reset before it. */
        if (type != VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR) {
            uint64_t value = type == VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR ? serialized_size(as)
                             : type == VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_BOTTOM_LEVEL_POINTERS_KHR
                                 ? as->instances
                                 : as->mtl_size;
            id<MTLBuffer> buffer;
            uint64_t offset;
            uint64_t *at = vkm_upload(cmd, 8, 8, nullptr, &buffer, &offset);
            VkmOpCopyBuffer *c = at ? vkm_emit(cmd, VKM_OP_COPY_BUFFER, sizeof(*c)) : nullptr;
            if (!c)
                return;
            *at = value;
            c->src = buffer;
            c->src_offset = offset;
            c->dst = pool->buffer;
            c->dst_offset = 8ull * (first + i);
            c->size = 8;
        }
        VkmOpAsProperty *o = vkm_emit(cmd, VKM_OP_AS_PROPERTY, sizeof(*o));
        if (!o)
            return;
        o->as = as;
        o->pool = pool;
        o->query = first + i;
        o->type = type;
    }
}

void vkm_encode_as_property(id<MTL4ComputeCommandEncoder> c, const VkmOpAsProperty *o, VkQueue queue,
                            uint64_t value)
{
    VkQueryPool pool = o->pool;
    if (o->query >= pool->count)
        return;
    /* metal writes 32 bits of the 64, whose high half the pool's reset zeroed. */
    if (o->type == VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR && o->as->mtl) {
        [c writeCompactedAccelerationStructureSize:o->as->mtl
                                          toBuffer:(MTL4BufferRange){pool->buffer.gpuAddress + 8ull * o->query, 4}];
        /* and to the structure's entry, for the copy that compacts it to know its size. */
        pthread_mutex_lock(&g_lock);
        uint32_t slot = o->as->slot;
        if (slot != UINT32_MAX) {
            uint64_t at = g_table.gpuAddress + (uint64_t)slot * sizeof(Entry) + offsetof(Entry, compacted);
            [c writeCompactedAccelerationStructureSize:o->as->mtl toBuffer:(MTL4BufferRange){at, 4}];
            o->as->compact_queue = queue;
            o->as->compact_done = value;
        }
        pthread_mutex_unlock(&g_lock);
    }
    vkm_query_written(pool, o->query, 1, queue, value);
}

/* ------------------------------------------------------------------ host commands, which the device does not offer. */

static VKAPI_ATTR VkResult VKAPI_CALL
BuildAccelerationStructuresKHR(VkDevice device, VkDeferredOperationKHR op, uint32_t count,
                               const VkAccelerationStructureBuildGeometryInfoKHR *infos,
                               const VkAccelerationStructureBuildRangeInfoKHR *const *ranges)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

static VKAPI_ATTR VkResult VKAPI_CALL CopyAccelerationStructureKHR(VkDevice device, VkDeferredOperationKHR op,
                                                                   const VkCopyAccelerationStructureInfoKHR *info)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

static VKAPI_ATTR VkResult VKAPI_CALL CopyAccelerationStructureToMemoryKHR(
    VkDevice device, VkDeferredOperationKHR op, const VkCopyAccelerationStructureToMemoryInfoKHR *info)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

static VKAPI_ATTR VkResult VKAPI_CALL CopyMemoryToAccelerationStructureKHR(
    VkDevice device, VkDeferredOperationKHR op, const VkCopyMemoryToAccelerationStructureInfoKHR *info)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

static VKAPI_ATTR VkResult VKAPI_CALL WriteAccelerationStructuresPropertiesKHR(
    VkDevice device, uint32_t count, const VkAccelerationStructureKHR *structures, VkQueryType type, size_t size,
    void *data, size_t stride)
{
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

/* deferred operations run nothing: the only commands taking one are the host commands above. */
struct VkDeferredOperationKHR_T {
    uint32_t unused;
};

static VKAPI_ATTR VkResult VKAPI_CALL CreateDeferredOperationKHR(VkDevice device, const VkAllocationCallbacks *a,
                                                                 VkDeferredOperationKHR *out)
{
    *out = vkm_zalloc(sizeof(**out));
    return *out ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

static VKAPI_ATTR void VKAPI_CALL DestroyDeferredOperationKHR(VkDevice device, VkDeferredOperationKHR op,
                                                              const VkAllocationCallbacks *a)
{
    free(op);
}

static VKAPI_ATTR uint32_t VKAPI_CALL GetDeferredOperationMaxConcurrencyKHR(VkDevice device,
                                                                           VkDeferredOperationKHR op)
{
    return 1;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetDeferredOperationResultKHR(VkDevice device, VkDeferredOperationKHR op)
{
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL DeferredOperationJoinKHR(VkDevice device, VkDeferredOperationKHR op)
{
    return VK_SUCCESS;
}

const VkmEntry vkm_accel_entries[] = {
    VKM_ENTRY(CreateAccelerationStructureKHR),
    VKM_ENTRY(DestroyAccelerationStructureKHR),
    VKM_ENTRY(GetAccelerationStructureDeviceAddressKHR),
    VKM_ENTRY(GetDeviceAccelerationStructureCompatibilityKHR),
    VKM_ENTRY(GetAccelerationStructureBuildSizesKHR),
    VKM_ENTRY(CmdBuildAccelerationStructuresKHR),
    VKM_ENTRY(CmdBuildAccelerationStructuresIndirectKHR),
    VKM_ENTRY(CmdCopyAccelerationStructureKHR),
    VKM_ENTRY(CmdCopyAccelerationStructureToMemoryKHR),
    VKM_ENTRY(CmdCopyMemoryToAccelerationStructureKHR),
    VKM_ENTRY(CmdWriteAccelerationStructuresPropertiesKHR),
    VKM_ENTRY(BuildAccelerationStructuresKHR),
    VKM_ENTRY(CopyAccelerationStructureKHR),
    VKM_ENTRY(CopyAccelerationStructureToMemoryKHR),
    VKM_ENTRY(CopyMemoryToAccelerationStructureKHR),
    VKM_ENTRY(WriteAccelerationStructuresPropertiesKHR),
    VKM_ENTRY(CreateDeferredOperationKHR),
    VKM_ENTRY(DestroyDeferredOperationKHR),
    VKM_ENTRY(GetDeferredOperationMaxConcurrencyKHR),
    VKM_ENTRY(GetDeferredOperationResultKHR),
    VKM_ENTRY(DeferredOperationJoinKHR),
    {nullptr, nullptr},
};
