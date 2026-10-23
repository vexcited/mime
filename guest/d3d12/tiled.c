/*
 * UpdateTileMappings and CopyTileMappings become one vkQueueBindSparse, which Vulkan does not order with the
 * queue's submissions. it waits for the queue's last timeline serial and signals its own, which the queue's next
 * submission waits for.
 */
#include "d3d12_private.h"

#define TILE_SIZE 65536u

/* buffers and mip tails bind as ranges, standard mips as boxes. */
typedef struct Binds {
    VkSparseMemoryBind *ranges;
    VkSparseImageMemoryBind *boxes;
    UINT range_count, box_count;
} Binds;

static void add_bind(const D12Resource *r, Binds *b, UINT tile, const D12Tile *to)
{
    D12TilePlace place;
    if (!D12TileAt(r, tile, &place))
        return;
    VkDeviceMemory memory = to->heap ? to->heap->memory : VK_NULL_HANDLE;
    VkDeviceSize offset = to->heap ? (VkDeviceSize)to->tile * TILE_SIZE : 0;
    if (place.packed) {
        VkSparseMemoryBind *last = b->range_count ? &b->ranges[b->range_count - 1] : nullptr;
        /* adjacent tiles mapping adjacent memory, or none, merge into one range. */
        if (last && last->resourceOffset + last->size == place.opaque && last->memory == memory &&
            (!memory || last->memoryOffset + last->size == offset)) {
            last->size += TILE_SIZE;
            return;
        }
        b->ranges[b->range_count++] = (VkSparseMemoryBind){place.opaque, TILE_SIZE, memory, offset, 0};
        return;
    }
    VkSparseImageMemoryBind *box = &b->boxes[b->box_count++];
    *box = (VkSparseImageMemoryBind){{VK_IMAGE_ASPECT_COLOR_BIT, place.mip, place.layer}, place.at, place.extent,
                                     memory, offset, 0};
}

/* also records the new mappings, for later CopyTileMappings. */
static void bind_tiles(D12Queue *queue, D12Resource *r, const UINT *tiles, const D12Tile *to, UINT count)
{
    D12Device *device = queue->child.device;
    if (!count)
        return;
    Binds b = {};
    b.ranges = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*b.ranges));
    b.boxes = HeapAlloc(GetProcessHeap(), 0, count * sizeof(*b.boxes));
    if (!b.ranges || !b.boxes) {
        HeapFree(GetProcessHeap(), 0, b.ranges);
        HeapFree(GetProcessHeap(), 0, b.boxes);
        return;
    }
    AcquireSRWLockExclusive(&r->tiles_lock);
    for (UINT i = 0; i < count; i++) {
        r->tiles[tiles[i]] = to[i];
        add_bind(r, &b, tiles[i], &to[i]);
    }
    ReleaseSRWLockExclusive(&r->tiles_lock);

    VkSparseBufferMemoryBindInfo buffer = {r->buffer, b.range_count, b.ranges};
    VkSparseImageOpaqueMemoryBindInfo opaque = {r->image, b.range_count, b.ranges};
    VkSparseImageMemoryBindInfo image = {r->image, b.box_count, b.boxes};
    VkBindSparseInfo bind = {VK_STRUCTURE_TYPE_BIND_SPARSE_INFO};
    if (r->image) {
        bind.imageOpaqueBindCount = b.range_count ? 1 : 0;
        bind.pImageOpaqueBinds = &opaque;
        bind.imageBindCount = b.box_count ? 1 : 0;
        bind.pImageBinds = &image;
    } else {
        bind.bufferBindCount = b.range_count ? 1 : 0;
        bind.pBufferBinds = &buffer;
    }
    AcquireSRWLockExclusive(queue->queue_lock);
    UINT64 serial = queue->next_serial++, last = serial - 1;
    VkTimelineSemaphoreSubmitInfo values = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    values.waitSemaphoreValueCount = last ? 1 : 0;
    values.pWaitSemaphoreValues = &last;
    values.signalSemaphoreValueCount = 1;
    values.pSignalSemaphoreValues = &serial;
    bind.pNext = &values;
    bind.waitSemaphoreCount = last ? 1 : 0;
    bind.pWaitSemaphores = &queue->timeline;
    bind.signalSemaphoreCount = 1;
    bind.pSignalSemaphores = &queue->timeline;
    if (device->vk.vkQueueBindSparse(queue->queue, 1, &bind, VK_NULL_HANDLE) != VK_SUCCESS)
        D12Log("d3d12: vkQueueBindSparse failed");
    queue->bind_serial = serial;
    ReleaseSRWLockExclusive(queue->queue_lock);
    HeapFree(GetProcessHeap(), 0, b.ranges);
    HeapFree(GetProcessHeap(), 0, b.boxes);
}

/*
 * the regions' tiles take the ranges' heap tiles in order. a range maps tiles from its start on, all to its start,
 * to nothing, or skips them. one region with no starts and sizes is the whole resource, and one range with no
 * counts covers every tile of the regions.
 */
void D12UpdateTileMappings(D12Queue *queue, D12Resource *r, UINT region_count,
                           const D3D12_TILED_RESOURCE_COORDINATE *starts, const D3D12_TILE_REGION_SIZE *sizes,
                           D12Heap *heap, UINT range_count, const D3D12_TILE_RANGE_FLAGS *flags,
                           const UINT *heap_starts, const UINT *range_counts)
{
    if (!r || !r->reserved || !region_count)
        return;
    UINT total = 0;
    for (UINT i = 0; i < region_count; i++)
        total += !sizes ? (!starts && region_count == 1 ? r->tile_count : 1)
                        : sizes[i].UseBox ? sizes[i].Width * sizes[i].Height * sizes[i].Depth : sizes[i].NumTiles;
    UINT *tiles = HeapAlloc(GetProcessHeap(), 0, (total + 1) * sizeof(UINT));
    D12Tile *to = HeapAlloc(GetProcessHeap(), 0, (total + 1) * sizeof(D12Tile));
    UINT *mapped = HeapAlloc(GetProcessHeap(), 0, (total + 1) * sizeof(UINT));
    if (!tiles || !to || !mapped)
        goto done;
    UINT n = 0;
    for (UINT i = 0; i < region_count; i++) {
        D3D12_TILE_REGION_SIZE whole = {r->tile_count};
        const D3D12_TILE_REGION_SIZE *size = sizes ? &sizes[i] : !starts && region_count == 1 ? &whole : nullptr;
        n += D12RegionTiles(r, starts ? &starts[i] : nullptr, size, tiles + n, total - n);
    }
    UINT at = 0, count = 0;
    for (UINT range = 0; range < range_count && at < n; range++) {
        UINT flag = flags ? flags[range] : D3D12_TILE_RANGE_FLAG_NONE;
        UINT length = range_counts ? range_counts[range] : range_count == 1 ? n : 1;
        UINT start = heap_starts ? heap_starts[range] : 0;
        for (UINT k = 0; k < length && at < n; k++, at++) {
            if (flag & D3D12_TILE_RANGE_FLAG_SKIP)
                continue;
            D12Tile tile = {nullptr, 0};
            if (!(flag & D3D12_TILE_RANGE_FLAG_NULL) && heap)
                tile = (D12Tile){heap, start + (flag & D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE ? 0 : k)};
            if (tile.heap && (UINT64)(tile.tile + 1) * TILE_SIZE > tile.heap->desc.SizeInBytes)
                tile.heap = nullptr;
            mapped[count] = tiles[at];
            to[count++] = tile;
        }
    }
    bind_tiles(queue, r, mapped, to, count);
done:
    HeapFree(GetProcessHeap(), 0, tiles);
    HeapFree(GetProcessHeap(), 0, to);
    HeapFree(GetProcessHeap(), 0, mapped);
}

/* copies another region's mappings, read before any change. */
void D12CopyTileMappings(D12Queue *queue, D12Resource *dst, const D3D12_TILED_RESOURCE_COORDINATE *dst_start,
                         D12Resource *src, const D3D12_TILED_RESOURCE_COORDINATE *src_start,
                         const D3D12_TILE_REGION_SIZE *size)
{
    if (!dst || !src || !dst->reserved || !src->reserved || !size)
        return;
    UINT total = size->UseBox ? size->Width * size->Height * size->Depth : size->NumTiles;
    UINT *from = HeapAlloc(GetProcessHeap(), 0, (total + 1) * sizeof(UINT));
    UINT *tiles = HeapAlloc(GetProcessHeap(), 0, (total + 1) * sizeof(UINT));
    D12Tile *to = HeapAlloc(GetProcessHeap(), 0, (total + 1) * sizeof(D12Tile));
    if (from && tiles && to) {
        UINT n = D12_MIN(D12RegionTiles(src, src_start, size, from, total),
                         D12RegionTiles(dst, dst_start, size, tiles, total));
        AcquireSRWLockShared(&src->tiles_lock);
        for (UINT i = 0; i < n; i++)
            to[i] = src->tiles[from[i]];
        ReleaseSRWLockShared(&src->tiles_lock);
        bind_tiles(queue, dst, tiles, to, n);
    }
    HeapFree(GetProcessHeap(), 0, from);
    HeapFree(GetProcessHeap(), 0, tiles);
    HeapFree(GetProcessHeap(), 0, to);
}
