#include "win32k.h"
#include <mime/gpu_identity.h>

NTKERNELAPI extern MimeBootInfo *HalBootInfo;

#define KMT_HANDLE_BASE 0x40000000u
#define KMT_HANDLE_STEP 0x40u
#define KMT_ADAPTERS (MIME_BOOT_GPUS + 1)

#define KMTQAITYPE_UMDRIVERNAME 1
#define KMTQAITYPE_GETSEGMENTSIZE 3
#define KMTQAITYPE_CHECKDRIVERUPDATESTATUS 11
#define KMTQAITYPE_DRIVERVERSION 13
#define KMTQAITYPE_ADAPTERTYPE 15
#define KMTQAITYPE_UMD_DRIVER_VERSION 18
#define KMTQAITYPE_PHYSICALADAPTERCOUNT 30
#define KMTQAITYPE_PHYSICALADAPTERDEVICEIDS 31
#define KMTQAITYPE_DRIVERVERSION_RENDER 56
#define KMTQAITYPE_DRIVER_DESCRIPTION 66

/* D3DKMT_ADAPTERTYPE. */
#define ADAPTER_RENDER 0x1
#define ADAPTER_DISPLAY 0x2
#define ADAPTER_SOFTWARE 0x4
#define ADAPTER_POST 0x8
#define ADAPTER_ACG 0x100

/* KMT_DRIVERVERSION_WDDM_3_2 for every adapter but the basic display, which windows keeps at 1.3. */
#define WDDM_3_2 3200
#define WDDM_1_3 1300
/* the version of d3d10warp.dll, 10.0.26100.9818. */
#define WARP_VERSION 0x000A000065F4265Aull
#define LUID_GPU 0xD3D0u
#define LUID_WARP 0xD3D8u
#define LUID_BASIC_DISPLAY 0xD3D9u

typedef struct KMT_ADAPTER {
    ULONG Luid;
    ULONG Type;
    ULONG Vendor, Device, SubVendor, SubSystem, Revision, Bus;
    ULONG Wddm;
    ULONG64 UmdVersion;
    ULONG64 Dedicated;
    WCHAR Name[128];
    const WCHAR *Umd;
} KMT_ADAPTER;

typedef struct KMT_ADAPTERINFO {
    UINT Handle;
    LUID AdapterLuid;
    ULONG NumOfSources;
    BOOL PrecisePresentRegionsPreferred;
} KMT_ADAPTERINFO;
MIME_STATIC_ASSERT(sizeof(KMT_ADAPTERINFO) == 20, "adapter info");

typedef struct KMT_ENUMADAPTERS3 {
    ULONG64 Filter;
    UINT NumAdapters;
    KMT_ADAPTERINFO *Adapters;
} KMT_ENUMADAPTERS3;

typedef struct KMT_QUERYADAPTERINFO {
    UINT Adapter;
    UINT Type;
    PVOID Data;
    UINT Size;
} KMT_QUERYADAPTERINFO;

static void basic(KMT_ADAPTER *a, ULONG luid, ULONG type, ULONG wddm, const WCHAR *name)
{
    memset(a, 0, sizeof(*a));
    a->Luid = luid;
    a->Type = type;
    a->Vendor = 0x1414;
    a->Device = 0x8C;
    a->Bus = 3;
    a->Wddm = wddm;
    a->UmdVersion = WARP_VERSION;
    a->Umd = L"d3d10warp.dll";
    wcscpy(a->Name, name);
}

/*
 * windows 11 lists each gpu, then the basic render driver; a machine without a gpu has the basic display adapter in
 * its place. the luids are dxgi's and those of the directx database, see guest/dxgi/main.c and guest/ntos/cm/boot.c.
 * the answers of the basic adapters were read on windows 11 with kmt.c of scratch/steam/kmt in the research
 * repository, those of a gpu follow the same layouts with what the host tells of it.
 */
static ULONG adapters(KMT_ADAPTER *list)
{
    ULONG n = 0;
#if defined(__aarch64__)
    KMT_ADAPTER *a = &list[n++];
    memset(a, 0, sizeof(*a));
    a->Luid = LUID_GPU;
    a->Type = ADAPTER_RENDER | ADAPTER_DISPLAY | ADAPTER_ACG;
    a->Vendor = 0x4D4F4351; /* "QCOM" */
    a->Device = 0x36334330;
    a->SubVendor = 0x0C69;
    a->SubSystem = 0x1028;
    a->Revision = 0x86;
    a->Wddm = WDDM_3_2;
    a->UmdVersion = (31ull << 48) | (63ull << 16) | 7;
    a->Umd = L"qcdx11arm64xum.dll";
    wcscpy(a->Name, L"Qualcomm(R) Adreno(TM) X1-85 GPU");
#else
    if (!HalBootInfo->gpu_count) {
        basic(&list[n++], LUID_BASIC_DISPLAY, ADAPTER_DISPLAY | ADAPTER_POST | ADAPTER_ACG, WDDM_1_3,
              L"Microsoft Basic Render Driver");
    }
    for (ULONG i = 0; i < HalBootInfo->gpu_count && i < MIME_BOOT_GPUS; i++) {
        const MimeBootGpu *gpu = &HalBootInfo->gpus[i];
        KMT_ADAPTER *a = &list[n++];
        memset(a, 0, sizeof(*a));
        a->Luid = LUID_GPU + i;
        a->Type = ADAPTER_RENDER | ADAPTER_ACG | (i ? 0 : ADAPTER_DISPLAY);
        a->Vendor = gpu->vendor;
        a->Device = gpu->device;
        a->SubVendor = gpu->subsystem & 0xFFFF;
        a->SubSystem = gpu->subsystem >> 16;
        a->Revision = gpu->revision;
        a->Bus = 3;
        a->Wddm = WDDM_3_2;
        USHORT parts[4];
        MimeGpuDriverVersion(gpu->vendor, gpu->driver_version, parts);
        a->UmdVersion = (ULONG64)parts[0] << 48 | (ULONG64)parts[1] << 32 | (ULONG64)parts[2] << 16 | parts[3];
        a->Dedicated = gpu->memory;
        MimeGpuName(gpu->name, a->Name, RTL_NUMBER_OF(a->Name));
    }
#endif
    basic(&list[n++], LUID_WARP, ADAPTER_RENDER | ADAPTER_SOFTWARE | ADAPTER_ACG, WDDM_3_2,
          L"Microsoft Basic Render Driver");
    return n;
}

static UINT handle_of(ULONG index)
{
    return KMT_HANDLE_BASE + index * KMT_HANDLE_STEP;
}

static BOOLEAN lookup(UINT handle, KMT_ADAPTER *out)
{
    KMT_ADAPTER list[KMT_ADAPTERS];
    ULONG count = adapters(list);
    if (handle < KMT_HANDLE_BASE || (handle - KMT_HANDLE_BASE) % KMT_HANDLE_STEP)
        return FALSE;
    ULONG index = (handle - KMT_HANDLE_BASE) / KMT_HANDLE_STEP;
    if (index >= count)
        return FALSE;
    *out = list[index];
    return TRUE;
}

/* a room too small for the adapters takes none and asks for more, an empty one is told how many there are. */
NTSTATUS NTAPI NtDxgkEnumAdapters3(KMT_ENUMADAPTERS3 *user)
{
    KMT_ENUMADAPTERS3 e;
    if (!NT_SUCCESS(MimeCopyFromUser(&e, user, sizeof(e))))
        return STATUS_ACCESS_VIOLATION;
    KMT_ADAPTER list[KMT_ADAPTERS];
    ULONG count = adapters(list);
    if (!e.Adapters) {
        e.NumAdapters = count;
        return NT_SUCCESS(MimeCopyToUser(&user->NumAdapters, &e.NumAdapters, sizeof(UINT))) ? STATUS_SUCCESS
                                                                                            : STATUS_ACCESS_VIOLATION;
    }
    if (e.NumAdapters < count) {
        e.NumAdapters = count;
        MimeCopyToUser(&user->NumAdapters, &e.NumAdapters, sizeof(UINT));
        return STATUS_BUFFER_TOO_SMALL;
    }
    for (ULONG i = 0; i < count; i++) {
        KMT_ADAPTERINFO info = {handle_of(i), {list[i].Luid, 0}, 0, FALSE};
        if (!NT_SUCCESS(MimeCopyToUser(&e.Adapters[i], &info, sizeof(info))))
            return STATUS_ACCESS_VIOLATION;
    }
    e.NumAdapters = count;
    return NT_SUCCESS(MimeCopyToUser(&user->NumAdapters, &e.NumAdapters, sizeof(UINT))) ? STATUS_SUCCESS
                                                                                        : STATUS_ACCESS_VIOLATION;
}

NTSTATUS NTAPI NtGdiDdDDIOpenAdapterFromLuid(PVOID user)
{
    struct {
        LUID AdapterLuid;
        UINT Adapter;
    } open;
    if (!NT_SUCCESS(MimeCopyFromUser(&open, user, sizeof(open))))
        return STATUS_ACCESS_VIOLATION;
    KMT_ADAPTER list[KMT_ADAPTERS];
    ULONG count = adapters(list);
    for (ULONG i = 0; i < count; i++) {
        if (open.AdapterLuid.LowPart == list[i].Luid && !open.AdapterLuid.HighPart) {
            open.Adapter = handle_of(i);
            return NT_SUCCESS(MimeCopyToUser(user, &open, sizeof(open))) ? STATUS_SUCCESS : STATUS_ACCESS_VIOLATION;
        }
    }
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS NTAPI NtGdiDdDDICloseAdapter(const UINT *user)
{
    UINT handle;
    KMT_ADAPTER a;
    if (!NT_SUCCESS(MimeCopyFromUser(&handle, user, sizeof(handle))))
        return STATUS_ACCESS_VIOLATION;
    return lookup(handle, &a) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static NTSTATUS answer(PVOID data, UINT size, const void *value, UINT length)
{
    if (size != length)
        return STATUS_INVALID_PARAMETER;
    return NT_SUCCESS(MimeCopyToUser(data, value, length)) ? STATUS_SUCCESS : STATUS_ACCESS_VIOLATION;
}

NTSTATUS NTAPI NtGdiDdDDIQueryAdapterInfo(const KMT_QUERYADAPTERINFO *user)
{
    KMT_QUERYADAPTERINFO q;
    KMT_ADAPTER a;
    if (!NT_SUCCESS(MimeCopyFromUser(&q, user, sizeof(q))))
        return STATUS_ACCESS_VIOLATION;
    if (!lookup(q.Adapter, &a))
        return STATUS_INVALID_PARAMETER;
    switch (q.Type) {
    case KMTQAITYPE_ADAPTERTYPE:
        return answer(q.Data, q.Size, &a.Type, sizeof(ULONG));
    case KMTQAITYPE_PHYSICALADAPTERCOUNT: {
        ULONG one = 1;
        return answer(q.Data, q.Size, &one, sizeof(one));
    }
    case KMTQAITYPE_PHYSICALADAPTERDEVICEIDS: {
        ULONG ids[7];
        if (q.Size != sizeof(ids) || !NT_SUCCESS(MimeCopyFromUser(ids, q.Data, sizeof(ULONG))))
            return STATUS_INVALID_PARAMETER;
        if (ids[0])
            return STATUS_INVALID_PARAMETER;
        ids[1] = a.Vendor;
        ids[2] = a.Device;
        ids[3] = a.SubVendor;
        ids[4] = a.SubSystem;
        ids[5] = a.Revision;
        ids[6] = a.Bus;
        return answer(q.Data, q.Size, ids, sizeof(ids));
    }
    case KMTQAITYPE_UMD_DRIVER_VERSION:
        return answer(q.Data, q.Size, &a.UmdVersion, sizeof(a.UmdVersion));
    case KMTQAITYPE_GETSEGMENTSIZE: {
        ULONG64 sizes[3] = {a.Dedicated, 0, HalBootInfo->ram_size / 2};
        return answer(q.Data, q.Size, sizes, sizeof(sizes));
    }
    case KMTQAITYPE_DRIVERVERSION:
    case KMTQAITYPE_DRIVERVERSION_RENDER: {
        ULONG version = q.Type == KMTQAITYPE_DRIVERVERSION_RENDER ? WDDM_3_2 : a.Wddm;
        return answer(q.Data, q.Size, &version, sizeof(version));
    }
    case KMTQAITYPE_CHECKDRIVERUPDATESTATUS: {
        ULONG none = 0;
        return answer(q.Data, q.Size, &none, sizeof(none));
    }
    case KMTQAITYPE_DRIVER_DESCRIPTION: {
        WCHAR name[128] = {};
        if (q.Size < sizeof(name))
            return STATUS_INVALID_PARAMETER;
        wcscpy(name, (a.Type & ADAPTER_SOFTWARE) || a.Vendor == 0x1414 ? L"Microsoft Basic Render Driver" : a.Name);
        return NT_SUCCESS(MimeCopyToUser(q.Data, name, sizeof(name))) ? STATUS_SUCCESS : STATUS_ACCESS_VIOLATION;
    }
    case KMTQAITYPE_UMDRIVERNAME: {
        struct {
            ULONG Version;
            WCHAR File[260];
        } umd;
        /* the gpus have no user mode driver of their own, mime's runtimes are theirs. */
        if (!a.Umd || q.Size != sizeof(umd) || !NT_SUCCESS(MimeCopyFromUser(&umd.Version, q.Data, sizeof(ULONG))) ||
            umd.Version > 3)
            return STATUS_INVALID_PARAMETER;
        memset(umd.File, 0, sizeof(umd.File));
        wcscpy(umd.File, a.Umd);
        return answer(q.Data, q.Size, &umd, sizeof(umd));
    }
    default:
        /* as windows refuses a type an adapter has no answer for. */
        return STATUS_INVALID_PARAMETER;
    }
}
