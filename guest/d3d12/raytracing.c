/*
 * raytracing: acceleration structures over the memory at the gpu virtual
 * addresses direct3d gives them. a gpu virtual address is a vulkan device
 * address, and direct3d's instance descriptions are vulkan's, so top level
 * structures read the addresses of bottom level ones as they are. each
 * address that held a build gets one VkAccelerationStructureKHR over the
 * rest of the resource made for acceleration structures holding it, as
 * direct3d never says how large one is.
 */
#include "d3d12_private.h"

/* ------------------------------------------------------------------ objects */

/* the region of the buffers made for acceleration structures holding va, NULL outside them. */
static const D3D12_GPU_VIRTUAL_ADDRESS_RANGE *region_of(D12Device *device, D3D12_GPU_VIRTUAL_ADDRESS va)
{
    UINT lo = 0, hi = device->as_region_count;
    while (lo < hi) {
        UINT mid = (lo + hi) / 2;
        const D3D12_GPU_VIRTUAL_ADDRESS_RANGE *r = &device->as_regions[mid];
        if (va < r->StartAddress)
            hi = mid;
        else if (va >= r->StartAddress + r->SizeInBytes)
            lo = mid + 1;
        else
            return r;
    }
    return NULL;
}

void D12AccelerationRegionAdd(D12Device *device, D3D12_GPU_VIRTUAL_ADDRESS va, UINT64 size)
{
    AcquireSRWLockExclusive(&device->as_lock);
    if (device->as_region_count == device->as_region_capacity) {
        UINT capacity = device->as_region_capacity ? device->as_region_capacity * 2 : 64;
        D3D12_GPU_VIRTUAL_ADDRESS_RANGE *list =
            device->as_regions ? HeapReAlloc(GetProcessHeap(), 0, device->as_regions, capacity * sizeof(*list))
                               : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
        if (!list) {
            ReleaseSRWLockExclusive(&device->as_lock);
            return;
        }
        device->as_regions = list;
        device->as_region_capacity = capacity;
    }
    UINT at = device->as_region_count;
    while (at && device->as_regions[at - 1].StartAddress > va) {
        device->as_regions[at] = device->as_regions[at - 1];
        at--;
    }
    device->as_regions[at] = (D3D12_GPU_VIRTUAL_ADDRESS_RANGE){va, size};
    device->as_region_count++;
    ReleaseSRWLockExclusive(&device->as_lock);
}

/* the first entry at or after va, in the list sorted by address. */
static UINT find(D12Device *device, D3D12_GPU_VIRTUAL_ADDRESS va)
{
    UINT lo = 0, hi = device->as_count;
    while (lo < hi) {
        UINT mid = (lo + hi) / 2;
        if (device->as_list[mid].va < va)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

VkAccelerationStructureKHR D12AccelerationStructureAt(D12Device *device, D3D12_GPU_VIRTUAL_ADDRESS va)
{
    D12Vk *vk = &device->vk;
    AcquireSRWLockShared(&device->as_lock);
    UINT at = find(device, va);
    VkAccelerationStructureKHR found = at < device->as_count && device->as_list[at].va == va ? device->as_list[at].handle
                                                                                           : VK_NULL_HANDLE;
    const D3D12_GPU_VIRTUAL_ADDRESS_RANGE *region = region_of(device, va);
    UINT64 size = region ? region->StartAddress + region->SizeInBytes - va : 0;
    ReleaseSRWLockShared(&device->as_lock);
    if (found)
        return found;

    VkBuffer buffer;
    VkDeviceSize offset;
    UINT64 available;
    if (!D12AddressLookup(device, va, &buffer, &offset, &available)) {
        D12Log("d3d12: no memory at acceleration structure address %llx", (unsigned long long)va);
        return VK_NULL_HANDLE;
    }
    VkAccelerationStructureCreateInfoKHR info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    info.buffer = buffer;
    info.offset = offset;
    info.size = size ? D12_MIN(size, available) : available;
    info.type = VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR;
    VkAccelerationStructureKHR handle;
    if (vk->vkCreateAccelerationStructureKHR(vk->device, &info, NULL, &handle) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    VkAccelerationStructureDeviceAddressInfoKHR address = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    address.accelerationStructure = handle;
    if (vk->vkGetAccelerationStructureDeviceAddressKHR(vk->device, &address) != va)
        D12Log("d3d12: the acceleration structure at %llx has another address", (unsigned long long)va);

    AcquireSRWLockExclusive(&device->as_lock);
    at = find(device, va);
    if (at < device->as_count && device->as_list[at].va == va) {
        /* another thread made one first. */
        found = device->as_list[at].handle;
    } else if (device->as_count == device->as_capacity) {
        UINT capacity = device->as_capacity ? device->as_capacity * 2 : 64;
        D12AccelerationStructure *list =
            device->as_list ? HeapReAlloc(GetProcessHeap(), 0, device->as_list, capacity * sizeof(*list))
                            : HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*list));
        if (list) {
            device->as_list = list;
            device->as_capacity = capacity;
        }
    }
    if (!found && device->as_count < device->as_capacity) {
        memmove(&device->as_list[at + 1], &device->as_list[at], (device->as_count - at) * sizeof(*device->as_list));
        device->as_list[at] = (D12AccelerationStructure){va, handle};
        device->as_count++;
        found = handle;
        handle = VK_NULL_HANDLE;
    }
    ReleaseSRWLockExclusive(&device->as_lock);
    if (handle)
        vk->vkDestroyAccelerationStructureKHR(vk->device, handle, NULL);
    return found;
}

void D12AccelerationStructuresForget(D12Device *device, D3D12_GPU_VIRTUAL_ADDRESS va, UINT64 size)
{
    if (!device->as_count && !device->as_region_count)
        return;
    D12Vk *vk = &device->vk;
    AcquireSRWLockExclusive(&device->as_lock);
    UINT first = find(device, va), last = first;
    while (last < device->as_count && device->as_list[last].va < va + size)
        vk->vkDestroyAccelerationStructureKHR(vk->device, device->as_list[last++].handle, NULL);
    memmove(&device->as_list[first], &device->as_list[last], (device->as_count - last) * sizeof(*device->as_list));
    device->as_count -= last - first;
    UINT kept = 0;
    for (UINT i = 0; i < device->as_region_count; i++)
        if (device->as_regions[i].StartAddress < va || device->as_regions[i].StartAddress >= va + size)
            device->as_regions[kept++] = device->as_regions[i];
    device->as_region_count = kept;
    ReleaseSRWLockExclusive(&device->as_lock);
}

void D12AccelerationStructuresDestroy(D12Device *device)
{
    for (UINT i = 0; i < device->as_count; i++)
        device->vk.vkDestroyAccelerationStructureKHR(device->vk.device, device->as_list[i].handle, NULL);
    HeapFree(GetProcessHeap(), 0, device->as_list);
    HeapFree(GetProcessHeap(), 0, device->as_regions);
    device->as_list = NULL;
    device->as_regions = NULL;
    device->as_count = device->as_capacity = device->as_region_count = device->as_region_capacity = 0;
}

/* ------------------------------------------------------------------ build inputs */

typedef struct BuildInputs {
    VkAccelerationStructureBuildGeometryInfoKHR info;
    VkAccelerationStructureGeometryKHR *geometries;
    VkAccelerationStructureBuildRangeInfoKHR *ranges;
    UINT32 *counts;
} BuildInputs;

static void free_inputs(BuildInputs *b)
{
    HeapFree(GetProcessHeap(), 0, b->geometries);
}

static VkIndexType index_type(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_R16_UINT: return VK_INDEX_TYPE_UINT16;
    case DXGI_FORMAT_R32_UINT: return VK_INDEX_TYPE_UINT32;
    default: return VK_INDEX_TYPE_NONE_KHR;
    }
}

/* direct3d's flags of builds and geometries are vulkan's. */
static BOOL convert_inputs(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *in, BuildInputs *out)
{
    memset(out, 0, sizeof(*out));
    VkAccelerationStructureBuildGeometryInfoKHR *info = &out->info;
    info->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    info->flags = in->Flags & ~D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    info->mode = in->Flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE
                     ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                     : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    BOOL top = in->Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    info->type = top ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    UINT n = top ? 1 : in->NumDescs;
    SIZE_T bytes = n * (sizeof(*out->geometries) + sizeof(*out->ranges) + sizeof(*out->counts));
    BYTE *memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes ? bytes : 1);
    if (!memory)
        return FALSE;
    out->geometries = (VkAccelerationStructureGeometryKHR *)memory;
    out->ranges = (VkAccelerationStructureBuildRangeInfoKHR *)(out->geometries + n);
    out->counts = (UINT32 *)(out->ranges + n);
    info->geometryCount = n;
    info->pGeometries = out->geometries;
    if (top) {
        VkAccelerationStructureGeometryKHR *g = &out->geometries[0];
        g->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        g->geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        g->geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        g->geometry.instances.arrayOfPointers = in->DescsLayout == D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS;
        g->geometry.instances.data.deviceAddress = in->InstanceDescs;
        out->ranges[0].primitiveCount = out->counts[0] = in->NumDescs;
        return TRUE;
    }
    for (UINT i = 0; i < n; i++) {
        const D3D12_RAYTRACING_GEOMETRY_DESC *d =
            in->DescsLayout == D3D12_ELEMENTS_LAYOUT_ARRAY_OF_POINTERS ? in->ppGeometryDescs[i] : &in->pGeometryDescs[i];
        VkAccelerationStructureGeometryKHR *g = &out->geometries[i];
        g->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        g->flags = d->Flags;
        if (d->Type == D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES) {
            const D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC *t = &d->Triangles;
            VkAccelerationStructureGeometryTrianglesDataKHR *v = &g->geometry.triangles;
            g->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            v->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            v->vertexFormat = D3dGetFormat(t->VertexFormat)->vk;
            v->vertexData.deviceAddress = t->VertexBuffer.StartAddress;
            v->vertexStride = t->VertexBuffer.StrideInBytes;
            v->maxVertex = t->VertexCount ? t->VertexCount - 1 : 0;
            v->indexType = index_type(t->IndexFormat);
            v->indexData.deviceAddress = v->indexType != VK_INDEX_TYPE_NONE_KHR ? t->IndexBuffer : 0;
            v->transformData.deviceAddress = t->Transform3x4;
            out->counts[i] = (v->indexType != VK_INDEX_TYPE_NONE_KHR ? t->IndexCount : t->VertexCount) / 3;
        } else {
            g->geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
            g->geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
            g->geometry.aabbs.data.deviceAddress = d->AABBs.AABBs.StartAddress;
            g->geometry.aabbs.stride = d->AABBs.AABBs.StrideInBytes;
            out->counts[i] = (UINT32)d->AABBs.AABBCount;
        }
        out->ranges[i].primitiveCount = out->counts[i];
    }
    return TRUE;
}

void D12PrebuildInfo(D12Device *device, const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS *inputs,
                     D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO *info)
{
    memset(info, 0, sizeof(*info));
    BuildInputs b;
    if (!device->has_rt || !convert_inputs(inputs, &b))
        return;
    VkAccelerationStructureBuildSizesInfoKHR sizes = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    b.info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    device->vk.vkGetAccelerationStructureBuildSizesKHR(device->vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                       &b.info, b.counts, &sizes);
    free_inputs(&b);
    UINT64 align = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
    info->ResultDataMaxSizeInBytes = D12_ALIGN(sizes.accelerationStructureSize, align);
    info->ScratchDataSizeInBytes = D12_ALIGN(sizes.buildScratchSize, align);
    info->UpdateScratchDataSizeInBytes = D12_ALIGN(sizes.updateScratchSize, align);
}

/* ------------------------------------------------------------------ commands */

void D12BuildAccelerationStructure(D12Device *device, VkCommandBuffer cmd,
                                   const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC *desc)
{
    BuildInputs b;
    if (!convert_inputs(&desc->Inputs, &b))
        return;
    b.info.dstAccelerationStructure = D12AccelerationStructureAt(device, desc->DestAccelerationStructureData);
    if (b.info.mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR)
        b.info.srcAccelerationStructure = desc->SourceAccelerationStructureData
                                              ? D12AccelerationStructureAt(device, desc->SourceAccelerationStructureData)
                                              : b.info.dstAccelerationStructure;
    b.info.scratchData.deviceAddress = desc->ScratchAccelerationStructureData;
    const VkAccelerationStructureBuildRangeInfoKHR *ranges = b.ranges;
    if (b.info.dstAccelerationStructure &&
        (b.info.mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR || b.info.srcAccelerationStructure))
        device->vk.vkCmdBuildAccelerationStructuresKHR(cmd, 1, &b.info, &ranges);
    free_inputs(&b);
}

/* the queries of a kind of postbuild information, and where each goes in direct3d's structure. */
static UINT postbuild_queries(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_TYPE type, VkQueryType out[2],
                              UINT *stride)
{
    switch (type) {
    case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE:
        out[0] = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        *stride = 8;
        return 1;
    case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_SERIALIZATION:
        out[0] = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_SIZE_KHR;
        out[1] = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SERIALIZATION_BOTTOM_LEVEL_POINTERS_KHR;
        *stride = 16;
        return 2;
    /* the size decoded for tools is a guess: the structure's own. */
    case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_TOOLS_VISUALIZATION:
    case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_CURRENT_SIZE:
        out[0] = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_SIZE_KHR;
        *stride = 8;
        return 1;
    default: return 0;
    }
}

void D12EmitPostbuildInfo(D12CommandAllocator *allocator, VkCommandBuffer cmd,
                          const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC *desc, UINT count,
                          const D3D12_GPU_VIRTUAL_ADDRESS *sources)
{
    D12Device *device = allocator->child.device;
    D12Vk *vk = &device->vk;
    VkQueryType types[2];
    UINT stride;
    UINT kinds = postbuild_queries(desc->InfoType, types, &stride);
    VkBuffer buffer;
    VkDeviceSize offset;
    if (!kinds || !count || !D12AddressLookup(device, desc->DestBuffer, &buffer, &offset, NULL))
        return;
    VkAccelerationStructureKHR stack[16], *handles = count <= 16 ? stack : HeapAlloc(GetProcessHeap(), 0, count * sizeof(*handles));
    if (!handles)
        return;
    BOOL complete = TRUE;
    for (UINT i = 0; i < count; i++)
        complete &= (handles[i] = D12AccelerationStructureAt(device, sources[i])) != VK_NULL_HANDLE;
    for (UINT k = 0; k < kinds && complete; k++) {
        VkQueryPool pool;
        UINT first;
        if (!D12QueryAlloc(allocator, types[k], count, &pool, &first))
            break;
        vk->vkCmdResetQueryPool(cmd, pool, first, count);
        vk->vkCmdWriteAccelerationStructuresPropertiesKHR(cmd, count, handles, types[k], pool, first);
        vk->vkCmdCopyQueryPoolResults(cmd, pool, first, count, buffer, offset + 8 * k, stride,
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    }
    if (handles != stack)
        HeapFree(GetProcessHeap(), 0, handles);
}

void D12CopyAccelerationStructure(D12Device *device, VkCommandBuffer cmd, D3D12_GPU_VIRTUAL_ADDRESS dst,
                                  D3D12_GPU_VIRTUAL_ADDRESS src, UINT mode)
{
    D12Vk *vk = &device->vk;
    switch (mode) {
    case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE:
    case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT: {
        VkCopyAccelerationStructureInfoKHR info = {VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
        info.src = D12AccelerationStructureAt(device, src);
        info.dst = D12AccelerationStructureAt(device, dst);
        info.mode = mode == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE
                        ? VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR
                        : VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
        if (info.src && info.dst)
            vk->vkCmdCopyAccelerationStructureKHR(cmd, &info);
        break;
    }
    /* direct3d's serialized header is vulkan's: the driver's identifier, the sizes and the count of addresses. */
    case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_SERIALIZE: {
        VkCopyAccelerationStructureToMemoryInfoKHR info = {
            VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_TO_MEMORY_INFO_KHR};
        info.src = D12AccelerationStructureAt(device, src);
        info.dst.deviceAddress = dst;
        info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_SERIALIZE_KHR;
        if (info.src)
            vk->vkCmdCopyAccelerationStructureToMemoryKHR(cmd, &info);
        break;
    }
    case D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_DESERIALIZE: {
        VkCopyMemoryToAccelerationStructureInfoKHR info = {
            VK_STRUCTURE_TYPE_COPY_MEMORY_TO_ACCELERATION_STRUCTURE_INFO_KHR};
        info.src.deviceAddress = src;
        info.dst = D12AccelerationStructureAt(device, dst);
        info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_DESERIALIZE_KHR;
        if (info.dst)
            vk->vkCmdCopyMemoryToAccelerationStructureKHR(cmd, &info);
        break;
    }
    default:
        D12Log("d3d12: acceleration structure copy mode %u not supported", mode);
        break;
    }
}

/* direct3d's identifier of serialized data is vulkan's version data: the driver's uuid and its compatibility. */
UINT D12CheckDriverMatchingIdentifier(D12Device *device, UINT type, const void *identifier)
{
    if (type != 0)
        return 1;
    if (!device->has_rt)
        return 2;
    VkAccelerationStructureVersionInfoKHR version = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_VERSION_INFO_KHR};
    version.pVersionData = identifier;
    VkAccelerationStructureCompatibilityKHR compatible = VK_ACCELERATION_STRUCTURE_COMPATIBILITY_INCOMPATIBLE_KHR;
    device->vk.vkGetDeviceAccelerationStructureCompatibilityKHR(device->vk.device, &version, &compatible);
    return compatible == VK_ACCELERATION_STRUCTURE_COMPATIBILITY_COMPATIBLE_KHR ? 0 : 2;
}
