/*
 * resource and fence sharing for d3d11.dll and d3d12.dll: the kernel object behind a shared nt handle, see
 * mime/gfxshare.h, and the description the opening device rebuilds the resource from. both dlls use this code.
 */
#ifndef MIME_GFXSHARE_LIB_H
#define MIME_GFXSHARE_LIB_H

#include <mime/win32/winbase.h>
#include <mime/dxgi_mime.h>
#include <mime/gfxshare.h>

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan_core.h>

typedef struct MimeShare {
    HANDLE handle; /* the device's own handle, keeps the object alive. */
    UINT64 id;
    UINT kind;
    UINT flags;
    UINT legacy; /* legacy shared handle, 0 for none. */
} MimeShare;

/* DXGI_SHARED_RESOURCE_READ and DXGI_SHARED_RESOURCE_WRITE. */
#define MIME_SHARED_READ 0x80000000u
#define MIME_SHARED_WRITE 0x1u

/* shared resource description: fixed width fields, so 32-bit, 64-bit, direct3d 11 and 12 all read it the same. */
#define MIME_SHARE_MAGIC 0x52485347u /* GSHR */

enum {
    MIME_SHARE_TEXTURE = 1,
    MIME_SHARE_BUFFER = 2,
    MIME_SHARE_FENCE = 3,
};

/* bind capabilities, mapped to both direct3d 12 resource flags and direct3d 11 bind flags. */
enum {
    MIME_SHARE_CAN_RENDER = 0x1,
    MIME_SHARE_CAN_DEPTH = 0x2,
    MIME_SHARE_CAN_WRITE = 0x4,    /* unordered access. */
    MIME_SHARE_CAN_READ = 0x8,     /* shader resource, absent when denied. */
    MIME_SHARE_CUBE = 0x10,
    MIME_SHARE_ORIGIN_D3D12 = 0x20,
    MIME_SHARE_NT_HANDLE = 0x40,
};

typedef struct MimeShareBlob {
    UINT32 magic;
    UINT32 kind;
    UINT32 caps;          /* MIME_SHARE_* above. */
    UINT32 dimension;     /* 1, 2 or 3. */
    UINT32 format;        /* DXGI_FORMAT. */
    UINT32 width;
    UINT32 height;
    UINT32 depth;         /* array size for 1d and 2d. */
    UINT32 mips;
    UINT32 samples;
    UINT32 quality;
    UINT32 d3d11_bind;    /* reported back by a direct3d 11 opener. */
    UINT32 d3d11_misc;
    UINT32 d3d12_flags;
    UINT32 d3d12_heap_flags;
    UINT32 vk_format;     /* the VkImageCreateInfo the exporter used. */
    UINT32 vk_usage;
    UINT32 vk_flags;
    UINT32 vk_layers;
    UINT32 type_index;    /* the importer allocates with the exporter's memory type and size. */
    UINT64 memory_size;
    UINT64 size;          /* buffers only. */
    LUID adapter;
    UINT32 fence_flags;   /* D3D12_FENCE_FLAGS. */
    UINT32 reserved[3];
} MimeShareBlob;

_Static_assert(sizeof(MimeShareBlob) <= MIME_GFXSHARE_MAX_DATA, "the description fits the kernel object");

HRESULT MimeShareHResult(NTSTATUS status);

/* creates the object with the handle the device keeps, flags are MIME_GFXSHARE_*. */
HRESULT MimeShareCreate(MimeShare *share, UINT kind, const void *data, UINT size, UINT flags, LPCWSTR name,
                        const SECURITY_ATTRIBUTES *attributes);
/* a new handle for the program. */
HRESULT MimeShareHandle(const MimeShare *share, DWORD access, const SECURITY_ATTRIBUTES *attributes, HANDLE *out);
/* returns a handle for the device to keep, the id and the description. */
HRESULT MimeShareOpen(HANDLE handle, UINT kind, MimeShare *share, void *data, UINT capacity, UINT *size);
/* MIME_GFXSHARE_RESOURCE or MIME_GFXSHARE_SYNC. */
HRESULT MimeShareKind(HANDLE handle, UINT *kind);
HRESULT MimeShareOpenName(LPCWSTR name, UINT kind, HANDLE *out);
/*
 * legacy shared handles are not real handles: an object number with bit 1 set, which no handle has, so the
 * runtimes can tell them apart. they only open while the resource that made them lives.
 */
#define MIME_LEGACY_HANDLE_BIT 2
#define MimeLegacyHandle(number) ((HANDLE)(ULONG_PTR)(((ULONG_PTR)(number) << 2) | MIME_LEGACY_HANDLE_BIT))
#define MimeLegacyNumber(handle) ((UINT)((ULONG_PTR)(handle) >> 2))
HRESULT MimeShareOpenLegacy(UINT legacy, HANDLE *out);
void MimeShareClose(MimeShare *share);

/* keyed mutex: returns STATUS_SUCCESS, STATUS_TIMEOUT or STATUS_ABANDONED. */
NTSTATUS MimeShareAcquire(HANDLE handle, UINT64 key, UINT timeout);
NTSTATUS MimeShareRelease(HANDLE handle, UINT64 key);
/* the holder is going away: the next acquire gets WAIT_ABANDONED. */
void MimeShareAbandon(HANDLE handle);

#endif
