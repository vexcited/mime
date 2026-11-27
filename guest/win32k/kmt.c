#include "win32k.h"
#include <mime/gpu_identity.h>

NTKERNELAPI extern MimeBootInfo *HalBootInfo;

#define KMT_HANDLE_BASE 0x40000000u
#define KMT_HANDLE_STEP 0x40u
#define KMT_ADAPTERS (MIME_BOOT_GPUS + 1)

#ifndef KEY_QUERY_VALUE
#define KEY_QUERY_VALUE 0x0001
#endif

#define KMTQAITYPE_UMDRIVERNAME 1
#define KMTQAITYPE_GETSEGMENTSIZE 3
#define KMTQAITYPE_ADAPTERADDRESS 6
#define KMTQAITYPE_ADAPTERREGISTRYINFO 8
#define KMTQAITYPE_CHECKDRIVERUPDATESTATUS 11
#define KMTQAITYPE_DRIVERVERSION 13
#define KMTQAITYPE_ADAPTERTYPE 15
#define KMTQAITYPE_UMD_DRIVER_VERSION 18
#define KMTQAITYPE_NODEMETADATA 25
#define KMTQAITYPE_PHYSICALADAPTERCOUNT 30
#define KMTQAITYPE_PHYSICALADAPTERDEVICEIDS 31
#define KMTQAITYPE_DRIVERVERSION_RENDER 56
#define KMTQAITYPE_NODEPERFDATA 61
#define KMTQAITYPE_ADAPTERPERFDATA 62
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
    /* the bus, device and function of a pci adapter, and the number of its key under the display class or -1. */
    ULONG Place[3];
    LONG Key;
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
    a->Place[0] = ~0u;
    a->Place[1] = a->Place[2] = 0xFFFF;
    a->Key = -1;
    wcscpy(a->Name, name);
}

/*
 * windows 11 lists each gpu, then the basic render driver; a machine without a gpu has the basic display adapter in its
 * place. the luids are dxgi's and those of the directx database, see guest/dxgi/main.c and guest/ntos/cm/boot.c. the
 * answers of the basic adapters were read on windows 11, those of a gpu follow the same layouts with what the host
 * tells of it.
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
    a->Place[0] = ~0u;
    a->Place[1] = a->Place[2] = 0xFFFF;
    wcscpy(a->Name, L"Qualcomm(R) Adreno(TM) X1-85 GPU");
#else
    if (!HalBootInfo->gpu_count) {
        basic(&list[n], LUID_BASIC_DISPLAY, ADAPTER_DISPLAY | ADAPTER_POST | ADAPTER_ACG, WDDM_1_3,
              L"Microsoft Basic Render Driver");
        /* the vga device of the machine, at device 3 of bus 0. */
        list[n].Place[0] = 0;
        list[n].Place[1] = 3;
        list[n].Place[2] = 0;
        list[n++].Key = 0;
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
        a->Place[0] = gpu->bus;
        a->Place[1] = gpu->slot >> 3;
        a->Place[2] = gpu->slot & 7;
        a->Key = (LONG)i;
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

#define KMT_SESSION_ADAPTERS 34 /* GetMaximumAdapterCountInSession of dxgkrnl on windows 11. */
#define FILTER_BITS 0x7ull
#define FILTER_DISPLAY_ONLY 0x2ull

typedef struct KMT_ENUMADAPTERS2 {
    UINT NumAdapters;
    KMT_ADAPTERINFO *Adapters;
} KMT_ENUMADAPTERS2;

/*
 * DxgkEnumAdapters2Impl and DxgkEnumAdapters3Impl of dxgkrnl 26300: no list is told the most adapters a session has,
 * 34, a list too short fails with STATUS_BUFFER_TOO_SMALL and is left as it was, its count too, and a longer one than
 * 34 counts as 34. enum2 lists every adapter with its video sources, one for an adapter that displays. enum3 tells no
 * sources and leaves out the adapters that only display unless its filter asks for them.
 */
static UINT adapter_infos(KMT_ADAPTERINFO *infos, ULONG64 filter, BOOLEAN sources)
{
    KMT_ADAPTER list[KMT_ADAPTERS];
    ULONG all = adapters(list);
    UINT count = 0;
    for (ULONG i = 0; i < all; i++) {
        BOOLEAN display_only = (list[i].Type & ADAPTER_DISPLAY) && !(list[i].Type & ADAPTER_RENDER);
        if (display_only && !(filter & FILTER_DISPLAY_ONLY))
            continue;
        infos[count++] = (KMT_ADAPTERINFO){handle_of(i), {list[i].Luid, 0},
                                           sources && (list[i].Type & ADAPTER_DISPLAY) ? 1 : 0, FALSE};
    }
    return count;
}

static NTSTATUS enum_adapters(UINT *user_count, KMT_ADAPTERINFO *user_list, UINT room, ULONG64 filter,
                              BOOLEAN sources)
{
    UINT count = KMT_SESSION_ADAPTERS;
    if (user_list) {
        KMT_ADAPTERINFO infos[KMT_ADAPTERS];
        count = adapter_infos(infos, filter, sources);
        if ((room < KMT_SESSION_ADAPTERS ? room : KMT_SESSION_ADAPTERS) < count)
            return STATUS_BUFFER_TOO_SMALL;
        if (!NT_SUCCESS(MimeCopyToUser(user_list, infos, count * sizeof(KMT_ADAPTERINFO))))
            return STATUS_ACCESS_VIOLATION;
    }
    return NT_SUCCESS(MimeCopyToUser(user_count, &count, sizeof(count))) ? STATUS_SUCCESS : STATUS_ACCESS_VIOLATION;
}

NTSTATUS NTAPI NtDxgkEnumAdapters3(KMT_ENUMADAPTERS3 *user)
{
    KMT_ENUMADAPTERS3 e;
    if (!user)
        return STATUS_INVALID_PARAMETER;
    if (!NT_SUCCESS(MimeCopyFromUser(&e, user, sizeof(e))))
        return STATUS_ACCESS_VIOLATION;
    if (e.Filter & ~FILTER_BITS)
        return STATUS_INVALID_PARAMETER;
    return enum_adapters(&user->NumAdapters, e.Adapters, e.NumAdapters, e.Filter, FALSE);
}

NTSTATUS NTAPI NtGdiDdDDIEnumAdapters2(KMT_ENUMADAPTERS2 *user)
{
    KMT_ENUMADAPTERS2 e;
    if (!user)
        return STATUS_INVALID_PARAMETER;
    if (!NT_SUCCESS(MimeCopyFromUser(&e, user, sizeof(e))))
        return STATUS_ACCESS_VIOLATION;
    return enum_adapters(&user->NumAdapters, e.Adapters, e.NumAdapters, FILTER_DISPLAY_ONLY, TRUE);
}

#define KMT_ENUM_ADAPTERS 16

typedef struct KMT_ENUMADAPTERS {
    UINT NumAdapters;
    KMT_ADAPTERINFO Adapters[KMT_ENUM_ADAPTERS];
} KMT_ENUMADAPTERS;
MIME_STATIC_ASSERT(KMT_ADAPTERS <= KMT_ENUM_ADAPTERS, "adapters of the first list");

/*
 * DxgkEnumAdaptersImpl of dxgkrnl 26300: the adapters of the second list in sixteen places written whole, the unused
 * ones as zeros. a list that cannot be written is a wrong parameter.
 */
NTSTATUS NTAPI NtGdiDdDDIEnumAdapters(KMT_ENUMADAPTERS *user)
{
    KMT_ENUMADAPTERS e = {0};
    if (!user)
        return STATUS_INVALID_PARAMETER;
    e.NumAdapters = adapter_infos(e.Adapters, FILTER_DISPLAY_ONLY, TRUE);
    return NT_SUCCESS(MimeCopyToUser(user, &e, sizeof(e))) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
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

#define COMPUTE_ARRIVAL L"#{1024e4c9-47c9-48d3-b4a8-f9df78523b53}"
#define DISPLAY_ARRIVAL L"#{1ca05180-a699-450a-9a0c-de4fbe3ddd89}"
#define NAME_CHARS 256

/* whether the instance name, \ written #, starts with text, ignoring case. */
static BOOLEAN instance_starts(PCWSTR instance, ULONG chars, PCWSTR text)
{
    ULONG n = (ULONG)wcslen(text);
    if (chars < n)
        return FALSE;
    for (ULONG i = 0; i < n; i++) {
        WCHAR a = instance[i] == L'#' ? L'\\' : instance[i];
        if (RtlUpcaseUnicodeChar(a) != RtlUpcaseUnicodeChar(text[i]))
            return FALSE;
    }
    return TRUE;
}

/* whether the slot, device << 3 | function, ends the instance or follows its last & as pci.sys and the host write it. */
static BOOLEAN instance_slot(PCWSTR instance, ULONG chars, ULONG slot)
{
    WCHAR hex[3];
    _snwprintf(hex, RTL_NUMBER_OF(hex), L"%02X", slot & 0xFF);
    ULONG amp = chars;
    while (amp && instance[amp - 1] != L'&')
        amp--;
    return (chars >= 2 && !_wcsnicmp(instance + chars - 2, hex, 2)) ||
           (amp && chars - amp >= 2 && !_wcsnicmp(instance + amp, hex, 2));
}

/*
 * the adapter the device of an instance runs: warp on the root device of the basic render driver, the basic display
 * adapter on the pci vga device when the host gives no gpu, else the gpu whose hardware id begins the instance.
 */
static LONG adapter_of_instance(PCWSTR instance, ULONG chars, const KMT_ADAPTER *list, ULONG count)
{
    for (ULONG i = 0; i < count; i++)
        if (list[i].Luid == LUID_WARP && instance_starts(instance, chars, L"ROOT\\BasicRender\\"))
            return (LONG)i;
    if (!instance_starts(instance, chars, L"PCI\\"))
        return -1;
#if !defined(__aarch64__)
    if (!HalBootInfo->gpu_count)
        return list[0].Luid == LUID_BASIC_DISPLAY ? 0 : -1;
    LONG found = -1;
    ULONG matches = 0;
    for (ULONG i = 0; i < HalBootInfo->gpu_count && i < MIME_BOOT_GPUS; i++) {
        const MimeBootGpu *gpu = &HalBootInfo->gpus[i];
        WCHAR id[64];
        _snwprintf(id, RTL_NUMBER_OF(id), L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X&REV_%02X\\", gpu->vendor, gpu->device,
                   gpu->subsystem, gpu->revision & 0xFF);
        if (!instance_starts(instance, chars, id))
            continue;
        /* two gpus of one model are told apart by their slot. */
        if (!matches++ || instance_slot(instance, chars, gpu->slot))
            found = (LONG)i;
    }
    for (ULONG i = 0; found >= 0 && i < count; i++)
        if (list[i].Luid == LUID_GPU + (ULONG)found)
            return (LONG)i;
#endif
    return -1;
}

/* the handle at 8 and the luid at 12 of D3DKMT_OPENADAPTERFROMDEVICENAME, written one after the other as dxgkrnl does. */
static NTSTATUS write_open(PVOID user, const UINT *adapter, const LUID *luid)
{
    NTSTATUS status = MimeCopyToUser((PUCHAR)user + 8, adapter, sizeof(*adapter));
    return NT_SUCCESS(status) ? MimeCopyToUser((PUCHAR)user + 12, luid, sizeof(*luid)) : status;
}

/*
 * D3DKMTOpenAdapterFromDeviceName, as DxgkOpenAdapterFromDeviceName of dxgkrnl 26300 answers it: a name of at most 255
 * characters, \\?\ taken as \??\, that is the compute or display arrival interface of an adapter, ignoring case, opens
 * that adapter. any other name is STATUS_INVALID_PARAMETER with the request left as it was; an empty one passes, as it
 * equals the partition interfaces no adapter here has, and fails to open with both outputs zeroed. measured on windows.
 */
NTSTATUS NTAPI NtGdiDdDDIOpenAdapterFromDeviceName(PVOID user)
{
    struct {
        PCWSTR DeviceName;
        UINT Adapter;
        LUID AdapterLuid;
    } open;
    WCHAR name[NAME_CHARS];
    if (!NT_SUCCESS(MimeCopyFromUser(&open, user, sizeof(open))) || !open.DeviceName)
        return STATUS_INVALID_PARAMETER;
    ULONG chars = 0;
    for (;; chars++) {
        if (chars == NAME_CHARS - 1 || !NT_SUCCESS(MimeCopyFromUser(&name[chars], open.DeviceName + chars, sizeof(WCHAR))))
            return STATUS_INVALID_PARAMETER;
        if (!name[chars])
            break;
    }
    if (!chars) {
        open.Adapter = 0;
        open.AdapterLuid = (LUID){};
        write_open(user, &open.Adapter, &open.AdapterLuid);
        return STATUS_OBJECT_PATH_SYNTAX_BAD;
    }
    if (chars > 1 && name[1] == L'\\')
        name[1] = L'?';
    static const WCHAR prefix[] = L"\\??\\";
    ULONG suffix = RTL_NUMBER_OF(COMPUTE_ARRIVAL) - 1, start = RTL_NUMBER_OF(prefix) - 1;
    if (chars <= start + suffix || _wcsnicmp(name, prefix, start) ||
        (_wcsicmp(name + chars - suffix, COMPUTE_ARRIVAL) && _wcsicmp(name + chars - suffix, DISPLAY_ARRIVAL)))
        return STATUS_INVALID_PARAMETER;
    KMT_ADAPTER list[KMT_ADAPTERS];
    ULONG count = adapters(list);
    LONG index = adapter_of_instance(name + start, chars - start - suffix, list, count);
    if (index < 0)
        return STATUS_INVALID_PARAMETER;
    /* the interface must be one the device tree registered: its link leads to the device. */
    UNICODE_STRING link = {(USHORT)(chars * sizeof(WCHAR)), (USHORT)(chars * sizeof(WCHAR)), name};
    PVOID device;
    if (!NT_SUCCESS(ObReferenceObjectByName(&link, OBJ_CASE_INSENSITIVE, nullptr, 0, nullptr, KernelMode, nullptr,
                                            &device)))
        return STATUS_INVALID_PARAMETER;
    ObDereferenceObject(device);
    open.Adapter = handle_of((ULONG)index);
    open.AdapterLuid = (LUID){list[index].Luid, 0};
    return write_open(user, &open.Adapter, &open.AdapterLuid);
}

NTSTATUS NTAPI NtGdiDdDDICloseAdapter(const UINT *user)
{
    UINT handle;
    KMT_ADAPTER a;
    if (!NT_SUCCESS(MimeCopyFromUser(&handle, user, sizeof(handle))))
        return STATUS_ACCESS_VIOLATION;
    return lookup(handle, &a) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/*
 * D3DKMTIsFeatureEnabled, as dxgkrnl 26300 answers it on a desktop with no overrides: {adapter, feature, result}, of
 * which only the result is written. the top four bits of a feature are its category. adapter 0 asks the features of
 * the system, and a feature known only to adapters is a wrong parameter then. an adapter answers like a driver with
 * none of the feature interface, hardware scheduling or a flip queue. a feature nobody knows is 0, and no error.
 */
#define FEATURE_KNOWN 0x00020000u        /* the feature exists. */
#define FEATURE_ON 0x00130001u           /* known, supported and enabled, version 1. */
#define FEATURE_DRIVER_ON 0x001F0001u    /* the same, and the driver supports it on this configuration. */
#define FEATURE_SUPPORTED 0x00120000u    /* known and supported, not enabled. */
#define FEATURE_ADAPTER_ONLY 0xFFFFFFFFu /* known to adapters alone: a wrong parameter for the system. */

static const ULONG g_driver_features[50] = {
    [0 ... 49] = FEATURE_KNOWN, [3] = FEATURE_DRIVER_ON, [5] = FEATURE_DRIVER_ON,
    [34] = FEATURE_ON,          [35] = FEATURE_ON,       [36] = FEATURE_ON,
};
static const ULONG g_os_features[10] = {FEATURE_ON,        FEATURE_KNOWN, FEATURE_KNOWN, FEATURE_ON,    FEATURE_SUPPORTED,
                                        FEATURE_ON,        FEATURE_ON,    FEATURE_KNOWN, FEATURE_KNOWN, FEATURE_KNOWN};
static const ULONG g_os2_features[3] = {FEATURE_ON, FEATURE_ON, FEATURE_ON};
/* the test features: 0 to 7 follow their defaults and those they depend on, the others are shaped by test settings. */
static const ULONG g_test_features[19] = {
    FEATURE_ON, FEATURE_SUPPORTED, FEATURE_ON, FEATURE_SUPPORTED, FEATURE_ON, FEATURE_SUPPORTED, FEATURE_ON, FEATURE_SUPPORTED,
    [8 ... 17] = FEATURE_KNOWN, [18] = FEATURE_SUPPORTED};

static ULONG feature_answer(UINT id, BOOLEAN adapter)
{
    UINT index = id & 0x0FFFFFFF;
    switch (id >> 28) {
    case 0:
        if (index >= RTL_NUMBER_OF(g_driver_features))
            return 0;
        return adapter || index == 36 ? g_driver_features[index] : FEATURE_ADAPTER_ONLY;
    case 1:
        return index < RTL_NUMBER_OF(g_os_features) ? g_os_features[index] : 0;
    case 2:
        if (index >= RTL_NUMBER_OF(g_os2_features))
            return 0;
        return adapter || index == 2 ? g_os2_features[index] : FEATURE_ADAPTER_ONLY;
    case 3:
        if (index >= RTL_NUMBER_OF(g_test_features))
            return 0;
        return adapter || index == 18 ? g_test_features[index] : FEATURE_ADAPTER_ONLY;
    default:
        return 0;
    }
}

NTSTATUS NTAPI NtDxgkIsFeatureEnabled(UINT *user)
{
    UINT in[3];
    KMT_ADAPTER a;
    if (!NT_SUCCESS(MimeCopyFromUser(in, user, sizeof(in))))
        return STATUS_INVALID_PARAMETER;
    if (in[0] && !lookup(in[0], &a))
        return STATUS_INVALID_PARAMETER;
    ULONG result = feature_answer(in[1], in[0] != 0);
    if (result == FEATURE_ADAPTER_ONLY)
        return STATUS_INVALID_PARAMETER;
    return NT_SUCCESS(MimeCopyToUser(&user[2], &result, sizeof(result))) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static NTSTATUS answer(PVOID data, UINT size, const void *value, UINT length)
{
    if (size != length)
        return STATUS_INVALID_PARAMETER;
    return NT_SUCCESS(MimeCopyToUser(data, value, length)) ? STATUS_SUCCESS : STATUS_ACCESS_VIOLATION;
}

NTKERNELAPI NTSTATUS NTAPI ZwOpenKey(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes);
NTKERNELAPI NTSTATUS NTAPI ZwQueryValueKey(HANDLE key, PUNICODE_STRING name, ULONG klass, PVOID info, ULONG length,
                                           PULONG returned);

#define REGISTRY_CHARS 260
#define REGISTRY_BYTES 518
#define REGISTRY_INFO_BYTES (4 * REGISTRY_CHARS * sizeof(WCHAR))

/* DpiReadPnpRegistryValue: a value of the adapter's driver key, whatever its type, cut to the room it has. */
static NTSTATUS registry_value(const KMT_ADAPTER *a, PCWSTR value, WCHAR *out)
{
    WCHAR path[160];
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES oa;
    HANDLE key;
    if (a->Key < 0)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    _snwprintf(path, RTL_NUMBER_OF(path) - 1,
               L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\%04d",
               (int)a->Key);
    path[RTL_NUMBER_OF(path) - 1] = 0;
    RtlInitUnicodeString(&name, path);
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    NTSTATUS status = ZwOpenKey(&key, KEY_QUERY_VALUE, &oa);
    if (!NT_SUCCESS(status))
        return status;
    struct {
        ULONG title, type, size;
        UCHAR data[REGISTRY_BYTES + 2];
    } info;
    ULONG got = 0;
    RtlInitUnicodeString(&name, value);
    status = ZwQueryValueKey(key, &name, 2, &info, sizeof(info), &got);
    ZwClose(key);
    if (!NT_SUCCESS(status) && status != STATUS_BUFFER_OVERFLOW)
        return status;
    memcpy(out, info.data, info.size < REGISTRY_BYTES ? info.size : REGISTRY_BYTES);
    return STATUS_SUCCESS;
}

/*
 * DpiQueryAdapterRegistryInfo of dxgkrnl 26300: the adapter, bios, dac and chip strings of the driver key. a string
 * that is not there stays empty, and with none of them the answer is that nothing was found.
 */
static NTSTATUS registry_info(const KMT_ADAPTER *a, WCHAR *info)
{
    NTSTATUS first = registry_value(a, L"HardwareInformation.AdapterString", info);
    if (!NT_SUCCESS(first) && first != STATUS_OBJECT_NAME_NOT_FOUND)
        return first;
    NTSTATUS status = registry_value(a, L"HardwareInformation.BiosString", info + REGISTRY_CHARS);
    BOOLEAN found = NT_SUCCESS(status) || NT_SUCCESS(first);
    if (!NT_SUCCESS(status) && status != STATUS_OBJECT_NAME_NOT_FOUND)
        return status;
    status = registry_value(a, L"HardwareInformation.DacType", info + 2 * REGISTRY_CHARS);
    if (NT_SUCCESS(status))
        found = TRUE;
    else if (status != STATUS_OBJECT_NAME_NOT_FOUND)
        return status;
    status = registry_value(a, L"HardwareInformation.ChipType", info + 3 * REGISTRY_CHARS);
    return status == STATUS_OBJECT_NAME_NOT_FOUND && found ? STATUS_SUCCESS : status;
}

static NTSTATUS engine_query(const KMT_ADAPTER *a, ULONG type, PVOID data, UINT size);

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
    case KMTQAITYPE_ADAPTERADDRESS:
        return answer(q.Data, q.Size, a.Place, sizeof(a.Place));
    case KMTQAITYPE_ADAPTERREGISTRYINFO: {
        if (q.Size != REGISTRY_INFO_BYTES || !q.Data)
            return STATUS_INVALID_PARAMETER;
        WCHAR *strings = ExAllocatePool2(POOL_FLAG_PAGED, REGISTRY_INFO_BYTES, 'tmkM');
        if (!strings)
            return STATUS_NO_MEMORY;
        NTSTATUS status = registry_info(&a, strings);
        if (NT_SUCCESS(status))
            status = answer(q.Data, q.Size, strings, REGISTRY_INFO_BYTES);
        ExFreePool(strings);
        return status;
    }
    case KMTQAITYPE_NODEMETADATA:
    case KMTQAITYPE_NODEPERFDATA:
    case KMTQAITYPE_ADAPTERPERFDATA:
        return engine_query(&a, q.Type, q.Data, q.Size);
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

/*
 * D3DKMTQueryStatistics, as dxgkrnl 26300 and dxgmms2 answer it. the request is 0x328 bytes, the result the 0x308 at
 * +0x18, written back only on success. Mime has no video memory manager or scheduler, so every counter is 0 and only
 * the shape of each adapter shows. no process
 * creates a dxgkrnl device here, so the queries about a process on an adapter fail as they do on windows without one.
 */
#define QS_SIZE 0x328
#define QS_RESULT 0x18
#define QS_INPUT 0x320

enum {
    QS_ADAPTER, QS_PROCESS, QS_PROCESS_ADAPTER, QS_SEGMENT, QS_PROCESS_SEGMENT, QS_NODE, QS_PROCESS_NODE,
    QS_VIDPNSOURCE, QS_PROCESS_VIDPNSOURCE, QS_PROCESS_SEGMENT_GROUP, QS_PHYSICAL_ADAPTER, QS_ADAPTER2, QS_SEGMENT2,
    QS_PROCESS_ADAPTER2, QS_PROCESS_SEGMENT2, QS_PROCESS_SEGMENT_GROUP2, QS_SEGMENT_USAGE, QS_SEGMENT_GROUP_USAGE,
    QS_NODE2, QS_PROCESS_NODE2,
};

typedef struct KMT_SEGMENT {
    ULONG64 Size;       /* all ones for the system memory segment video memory management adds. */
    BOOLEAN Aperture;   /* an aperture or system memory segment. */
    ULONG Power;        /* PowerFlags. */
    ULONG Properties;   /* SegmentProperties: system memory, type, memory group, all cpu visible. */
} KMT_SEGMENT;

typedef struct KMT_SHAPE {
    ULONG Segments, Nodes, Sources, Groups;
    ULONG64 Group[2];
    BOOLEAN Driver; /* the driver answers the performance data queries. */
    BOOLEAN VSync;
    KMT_SEGMENT Segment[2];
} KMT_SHAPE;

/*
 * the basic render driver has one aperture segment the size of the memory, cpu visible, a 3d engine per processor and
 * one more, between 3 and 62, and no performance data; its memory group is half the memory. measured on windows 11. a
 * gpu has its local memory and the system memory group, and one engine: Mime submits on one queue. both get the system
 * memory segment of video memory management last.
 */
static void shape_of(const KMT_ADAPTER *a, KMT_SHAPE *s)
{
    memset(s, 0, sizeof(*s));
    s->Sources = 1;
    s->Segments = 2;
    if (a->Vendor == 0x1414) {
        ULONG nodes = HalBootInfo->cpu_count + 1;
        s->Nodes = nodes < 3 ? 3 : nodes > 62 ? 62 : nodes;
        s->Groups = 1;
        s->Group[0] = HalBootInfo->ram_size / 2;
        s->Segment[0] = (KMT_SEGMENT){HalBootInfo->ram_size, TRUE, 0, 0x101};
        s->Segment[1] = (KMT_SEGMENT){~0ull, TRUE, 3, 0x109};
        return;
    }
    s->Nodes = 1;
    s->Groups = 2;
    s->Group[0] = a->Dedicated;
    s->Group[1] = HalBootInfo->ram_size / 2;
    s->Driver = TRUE;
    s->VSync = (a->Type & ADAPTER_DISPLAY) != 0;
    s->Segment[0] = (KMT_SEGMENT){a->Dedicated, FALSE, 0, 1 << 2};
    s->Segment[1] = (KMT_SEGMENT){~0ull, TRUE, 3, 0x149};
}

#define NODEMETADATA_SIZE 78
#define NODEPERFDATA_SIZE 56
#define ADAPTERPERFDATA_SIZE 64
#define ENGINE_3D 1

/*
 * what dxgkrnl 26300 tells of the engines: every node is a 3d engine named 3DNode and its number, under an io mmu,
 * where the driver names none. the frequencies, voltages, fan and temperature are the driver's: the basic drivers
 * have none and refuse, and a gpu's are zeros, Mime reads none of them from the host. an adapter that only displays
 * is answered by the basic render driver, as for the statistics. measured on windows 11 for the basic adapters.
 */
static NTSTATUS engine_query(const KMT_ADAPTER *a, ULONG type, PVOID data, UINT size)
{
    KMT_ADAPTER list[KMT_ADAPTERS];
    KMT_SHAPE s;
    UCHAR in[NODEMETADATA_SIZE];
    UINT wanted = type == KMTQAITYPE_NODEMETADATA ? NODEMETADATA_SIZE
                  : type == KMTQAITYPE_NODEPERFDATA ? NODEPERFDATA_SIZE : ADAPTERPERFDATA_SIZE;
    if (size != wanted || !NT_SUCCESS(MimeCopyFromUser(in, data, wanted)))
        return STATUS_INVALID_PARAMETER;
    if (!(a->Type & ADAPTER_RENDER))
        a = &list[adapters(list) - 1];
    shape_of(a, &s);
    ULONG first = *(ULONG *)in, second = *(ULONG *)(in + 4);
    switch (type) {
    case KMTQAITYPE_NODEMETADATA: {
        if (first >> 16 || first >= s.Nodes)
            return STATUS_INVALID_PARAMETER;
        memset(in + 4, 0, sizeof(in) - 4);
        *(ULONG *)(in + 4) = ENGINE_3D;
        WCHAR name[12];
        _snwprintf(name, RTL_NUMBER_OF(name), L"3DNode%02u", first);
        memcpy(in + 8, name, sizeof(L"3DNode00"));
        in[77] = TRUE;
        break;
    }
    case KMTQAITYPE_NODEPERFDATA:
        if (!s.Driver || first >= s.Nodes || second)
            return STATUS_INVALID_PARAMETER;
        memset(in + 8, 0, wanted - 8);
        break;
    default:
        if (!s.Driver || first)
            return STATUS_INVALID_PARAMETER;
        memset(in + 4, 0, wanted - 4);
        break;
    }
    return answer(data, size, in, wanted);
}

static const KMT_ADAPTER *adapter_by_luid(const KMT_ADAPTER *list, ULONG count, LUID luid)
{
    for (ULONG i = 0; i < count; i++)
        if (!luid.HighPart && luid.LowPart == list[i].Luid)
            return &list[i];
    return nullptr;
}

static NTSTATUS query_statistics(const UCHAR *in, const KMT_ADAPTER *a, PUCHAR out)
{
    KMT_SHAPE s;
    shape_of(a, &s);
    ULONG type = *(const ULONG *)in;
    ULONG id = *(const ULONG *)(in + QS_INPUT);
    USHORT physical = *(const USHORT *)(in + QS_INPUT), second = *(const USHORT *)(in + QS_INPUT + 2);
    switch (type) {
    case QS_ADAPTER:
    case QS_ADAPTER2:
        if (type == QS_ADAPTER2 && physical)
            return STATUS_INVALID_PARAMETER;
        *(ULONG *)(out + 0) = s.Segments;
        *(ULONG *)(out + 4) = s.Nodes;
        *(ULONG *)(out + 8) = s.Sources;
        *(ULONG *)(out + 12) = s.VSync;
        *(ULONG *)(out + 712) = s.Groups;
        return STATUS_SUCCESS;
    case QS_PROCESS:
        *(ULONG *)(out + 0) = s.Nodes;
        *(ULONG *)(out + 4) = s.Sources;
        return STATUS_SUCCESS;
    case QS_SEGMENT:
    case QS_SEGMENT2:
    case QS_SEGMENT_USAGE: {
        ULONG segment = type == QS_SEGMENT ? id : second;
        if ((type != QS_SEGMENT && physical) || segment >= s.Segments)
            return STATUS_INVALID_PARAMETER;
        const KMT_SEGMENT *g = &s.Segment[segment];
        if (type == QS_SEGMENT_USAGE) {
            *(ULONG64 *)(out + 8) = g->Size;
            return STATUS_SUCCESS;
        }
        *(ULONG64 *)(out + 0) = g->Size;
        *(ULONG *)(out + 40) = g->Aperture;
        *(ULONG64 *)(out + 96) = g->Power;
        *(ULONG64 *)(out + 104) = g->Properties;
        return STATUS_SUCCESS;
    }
    case QS_SEGMENT_GROUP_USAGE: {
        if (physical || second > 1)
            return STATUS_INVALID_PARAMETER;
        *(ULONG64 *)(out + 8) = s.Group[second];
        return STATUS_SUCCESS;
    }
    case QS_NODE:
    case QS_NODE2:
        if ((type == QS_NODE2 && physical) || (type == QS_NODE ? id : second) >= s.Nodes)
            return STATUS_INVALID_PARAMETER;
        /* the driver's frequencies and voltages follow the counters: a driver without them fails the query. */
        return s.Driver ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    case QS_VIDPNSOURCE:
        if (id >= s.Sources)
            return STATUS_INVALID_PARAMETER;
        *(ULONG *)(out + 16) = s.VSync;
        return STATUS_SUCCESS;
    case QS_PHYSICAL_ADAPTER:
        if (a->Wddm < 2400)
            return STATUS_NOT_IMPLEMENTED;
        if (id || !s.Driver)
            return STATUS_INVALID_PARAMETER;
        return STATUS_SUCCESS;
    default:
        /* the process ones need a device of the process on the adapter, and other types are unknown. */
        return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS NTAPI NtGdiDdDDIQueryStatistics(PVOID user)
{
    UCHAR request[QS_SIZE];
    if (!NT_SUCCESS(MimeCopyFromUser(request, user, sizeof(request))))
        return STATUS_INVALID_PARAMETER;
    UCHAR result[QS_INPUT - QS_RESULT];
    memset(result, 0, sizeof(result));
    KMT_ADAPTER list[KMT_ADAPTERS];
    ULONG count = adapters(list);
    LUID luid;
    memcpy(&luid, request + 4, sizeof(luid));
    const KMT_ADAPTER *a = adapter_by_luid(list, count, luid);
    if (!a)
        return STATUS_INVALID_PARAMETER;
    HANDLE process = *(HANDLE *)(request + 0x10);
    if (process) {
        PEPROCESS target;
        if (!NT_SUCCESS(ObReferenceObjectByHandle(process, PROCESS_QUERY_INFORMATION, PsProcessType, UserMode,
                                                  (PVOID *)&target, nullptr)))
            return STATUS_INVALID_PARAMETER;
        BOOLEAN exiting = PsGetProcessExitStatus(target) != STATUS_PENDING;
        ObDereferenceObject(target);
        if (exiting)
            return STATUS_INVALID_PARAMETER;
    }
    /* a display adapter with no render engine is answered by the basic render driver it is paired with. */
    if (!(a->Type & ADAPTER_RENDER))
        a = &list[count - 1];
    NTSTATUS status = query_statistics(request, a, result);
    if (!NT_SUCCESS(status))
        return status;
    return NT_SUCCESS(MimeCopyToUser((PUCHAR)user + QS_RESULT, result, sizeof(result))) ? STATUS_SUCCESS
                                                                                         : STATUS_INVALID_PARAMETER;
}
