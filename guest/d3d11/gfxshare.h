/*
 * what d3d11.dll and d3d12.dll do to share resources and fences between devices and processes: the kernel object
 * behind a shared nt handle (mime/gfxshare.h) and the bytes a creator describes its resource with, which the
 * opening device builds its own object from. The code is the same in both dlls.
 */
#ifndef MIME_GFXSHARE_LIB_H
#define MIME_GFXSHARE_LIB_H

#include <mime/win32/winbase.h>
#include <mime/dxgi_mime.h>
#include <mime/gfxshare.h>

#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan_core.h>

/* the kernel object of a resource or fence a device made, once it was given a handle. */
typedef struct MimeShare {
    HANDLE handle; /* the device's own handle, which keeps the object while it lives */
    UINT64 id;
    UINT kind;
    UINT flags;
    UINT legacy; /* the legacy shared handle, 0 for none */
} MimeShare;

/* DXGI_SHARED_RESOURCE_READ and WRITE, the rights a handle of a shared resource can be asked for. */
#define MIME_SHARED_READ 0x80000000u
#define MIME_SHARED_WRITE 0x1u

/*
 * the bytes of a shared resource or fence. Fields are fixed width, so 32-bit and 64-bit processes, and
 * direct3d 11 and 12, read the same.
 */
#define MIME_SHARE_MAGIC 0x52485347u /* GSHR */

enum {
    MIME_SHARE_TEXTURE = 1,
    MIME_SHARE_BUFFER = 2,
    MIME_SHARE_FENCE = 3,
};

/* what the resource can be bound as: direct3d 12's resource flags and direct3d 11's bind flags both come from these. */
enum {
    MIME_SHARE_CAN_RENDER = 0x1,
    MIME_SHARE_CAN_DEPTH = 0x2,
    MIME_SHARE_CAN_WRITE = 0x4,    /* unordered access */
    MIME_SHARE_CAN_READ = 0x8,     /* shader resource, absent when denied */
    MIME_SHARE_CUBE = 0x10,
    MIME_SHARE_ORIGIN_D3D12 = 0x20, /* made by direct3d 12 */
    MIME_SHARE_NT_HANDLE = 0x40,    /* made to be shared by nt handle */
};

typedef struct MimeShareBlob {
    UINT32 magic;
    UINT32 kind;
    UINT32 caps;          /* MIME_SHARE_* above */
    UINT32 dimension;     /* 1, 2 or 3: a texture's number of dimensions */
    UINT32 format;        /* DXGI_FORMAT the resource was made with */
    UINT32 width;
    UINT32 height;
    UINT32 depth;         /* depth, or array size for 1d and 2d */
    UINT32 mips;
    UINT32 samples;
    UINT32 quality;
    UINT32 d3d11_bind;    /* what a direct3d 11 resource was made with, for its opener to report */
    UINT32 d3d11_misc;
    UINT32 d3d12_flags;   /* resource flags of a direct3d 12 resource */
    UINT32 d3d12_heap_flags;
    UINT32 vk_format;     /* the image as vulkan was asked for it */
    UINT32 vk_usage;
    UINT32 vk_flags;
    UINT32 vk_layers;
    UINT32 type_index;    /* the exporting memory's type and size, which the importing memory is made as */
    UINT64 memory_size;
    UINT64 size;          /* a buffer's size */
    LUID adapter;
    UINT32 fence_flags;   /* a fence's D3D12_FENCE_FLAGS */
    UINT32 reserved[3];
} MimeShareBlob;

_Static_assert(sizeof(MimeShareBlob) <= MIME_GFXSHARE_MAX_DATA, "the description fits the kernel object");

/* a status of the services as the error codes of the runtimes. */
HRESULT MimeShareHResult(NTSTATUS status);

/* the object's first handle, the device keeping it; flags are MIME_GFXSHARE_*. */
HRESULT MimeShareCreate(MimeShare *share, UINT kind, const void *data, UINT size, UINT flags, LPCWSTR name,
                        const SECURITY_ATTRIBUTES *attributes);
/* another handle to the object for the program, with the rights asked for. */
HRESULT MimeShareHandle(const MimeShare *share, DWORD access, const SECURITY_ATTRIBUTES *attributes, HANDLE *out);
/* the object a handle names, described: a handle the device keeps for itself, the id and the bytes. */
HRESULT MimeShareOpen(HANDLE handle, UINT kind, MimeShare *share, void *data, UINT capacity, UINT *size);
/* what kind of object a handle names, MIME_GFXSHARE_RESOURCE or MIME_GFXSHARE_SYNC. */
HRESULT MimeShareKind(HANDLE handle, UINT *kind);
HRESULT MimeShareOpenName(LPCWSTR name, UINT kind, HANDLE *out);
/*
 * Legacy shared handles are not handles: a number of the object with bit 1 set, which no handle has, so the
 * runtimes tell them apart. They open only while the resource that made them lives.
 */
#define MIME_LEGACY_HANDLE_BIT 2
#define MimeLegacyHandle(number) ((HANDLE)(ULONG_PTR)(((ULONG_PTR)(number) << 2) | MIME_LEGACY_HANDLE_BIT))
#define MimeLegacyNumber(handle) ((UINT)((ULONG_PTR)(handle) >> 2))
/* a legacy shared handle: only the object of a process that still has its resource opens. */
HRESULT MimeShareOpenLegacy(UINT legacy, HANDLE *out);
/* the device lets go of the object. */
void MimeShareClose(MimeShare *share);

/* the keyed mutex of a shared resource: the status is STATUS_SUCCESS, STATUS_TIMEOUT or STATUS_ABANDONED. */
NTSTATUS MimeShareAcquire(HANDLE handle, UINT64 key, UINT timeout);
NTSTATUS MimeShareRelease(HANDLE handle, UINT64 key);
/* the device that holds the mutex goes: the next acquire gets WAIT_ABANDONED. */
void MimeShareAbandon(HANDLE handle);

#endif
