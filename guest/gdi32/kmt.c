/*
 * the adapter thunks of the kernel graphics interface, answered from the adapters dxgi enumerates, so the luids,
 * ids and memory sizes programs read here match what dxgi and d3d12 give them.
 */
#include "gdi32_private.h"
#include <mime/win32/dxgi.h>

#define KMT_SUCCESS 0
#define KMT_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#define KMT_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023)
#define KMT_NOT_SUPPORTED ((NTSTATUS)0xC00000BB)
#define KMT_NO_SUCH_DEVICE ((NTSTATUS)0xC000000E)

typedef UINT D3DKMT_HANDLE;

typedef struct _D3DKMT_ADAPTERINFO {
    D3DKMT_HANDLE hAdapter;
    LUID AdapterLuid;
    ULONG NumOfSources;
    BOOL bPrecisePresentRegionsPreferred;
} D3DKMT_ADAPTERINFO;

typedef struct _D3DKMT_ENUMADAPTERS {
    ULONG NumAdapters;
    D3DKMT_ADAPTERINFO Adapters[16];
} D3DKMT_ENUMADAPTERS;

typedef struct _D3DKMT_ENUMADAPTERS2 {
    ULONG NumAdapters;
    D3DKMT_ADAPTERINFO *pAdapters;
} D3DKMT_ENUMADAPTERS2;

typedef struct _D3DKMT_OPENADAPTERFROMLUID {
    LUID AdapterLuid;
    D3DKMT_HANDLE hAdapter;
} D3DKMT_OPENADAPTERFROMLUID;

typedef struct _D3DKMT_CLOSEADAPTER {
    D3DKMT_HANDLE hAdapter;
} D3DKMT_CLOSEADAPTER;

typedef struct _D3DKMT_QUERYADAPTERINFO {
    D3DKMT_HANDLE hAdapter;
    UINT Type;
    void *pPrivateDriverData;
    UINT PrivateDriverDataSize;
} D3DKMT_QUERYADAPTERINFO;

enum {
    KMTQAITYPE_UMDRIVERNAME = 1,
    KMTQAITYPE_GETSEGMENTSIZE = 3,
    KMTQAITYPE_ADAPTERGUID = 4,
    KMTQAITYPE_ADAPTERADDRESS = 6,
    KMTQAITYPE_ADAPTERREGISTRYINFO = 8,
    KMTQAITYPE_CHECKDRIVERUPDATESTATUS = 11,
    KMTQAITYPE_DRIVERVERSION = 13,
    KMTQAITYPE_ADAPTERTYPE = 15,
    KMTQAITYPE_UMD_DRIVER_VERSION = 18,
    KMTQAITYPE_PHYSICALADAPTERCOUNT = 30,
    KMTQAITYPE_PHYSICALADAPTERDEVICEIDS = 31,
    KMTQAITYPE_ADAPTERGUID_RENDER = 52,
    KMTQAITYPE_ADAPTERADDRESS_RENDER = 53,
    KMTQAITYPE_ADAPTERREGISTRYINFO_RENDER = 54,
    KMTQAITYPE_CHECKDRIVERUPDATESTATUS_RENDER = 55,
    KMTQAITYPE_DRIVERVERSION_RENDER = 56,
    KMTQAITYPE_ADAPTERTYPE_RENDER = 57,
    KMTQAITYPE_QUERY_ADAPTER_UNIQUE_GUID = 60,
    KMTQAITYPE_DRIVER_DESCRIPTION = 65,
    KMTQAITYPE_DRIVER_DESCRIPTION_RENDER = 66,
};

typedef struct _D3DKMT_SEGMENTSIZEINFO {
    ULONGLONG DedicatedVideoMemorySize;
    ULONGLONG DedicatedSystemMemorySize;
    ULONGLONG SharedSystemMemorySize;
} D3DKMT_SEGMENTSIZEINFO;

typedef struct _D3DKMT_ADAPTERADDRESS {
    UINT BusNumber;
    UINT DeviceNumber;
    UINT FunctionNumber;
} D3DKMT_ADAPTERADDRESS;

typedef struct _D3DKMT_ADAPTERREGISTRYINFO {
    WCHAR AdapterString[MAX_PATH];
    WCHAR BiosString[MAX_PATH];
    WCHAR DacType[MAX_PATH];
    WCHAR ChipType[MAX_PATH];
} D3DKMT_ADAPTERREGISTRYINFO;

typedef struct _D3DKMT_UMDFILENAMEINFO {
    UINT Version;
    WCHAR UmdFileName[MAX_PATH];
} D3DKMT_UMDFILENAMEINFO;

typedef struct _D3DKMT_DEVICE_IDS {
    UINT VendorID;
    UINT DeviceID;
    UINT SubVendorID;
    UINT SubSystemID;
    UINT RevisionID;
    UINT BusType;
} D3DKMT_DEVICE_IDS;

typedef struct _D3DKMT_QUERY_DEVICE_IDS {
    UINT PhysicalAdapterIndex;
    D3DKMT_DEVICE_IDS DeviceIds;
} D3DKMT_QUERY_DEVICE_IDS;

typedef struct _D3DKMT_DRIVER_DESCRIPTION {
    WCHAR DriverDescription[4096];
} D3DKMT_DRIVER_DESCRIPTION;

/* the adapter type bits: render, display and software. */
#define KMT_TYPE_RENDER 0x1
#define KMT_TYPE_DISPLAY 0x2
#define KMT_TYPE_SOFTWARE 0x4

#define KMT_DRIVERVERSION_WDDM_3_1 3100
#define KMT_BUS_PCI 3

#define MAX_ADAPTERS 16
/* windows' adapter handles are kernel handles; these are only told apart by their index. */
#define HANDLE_BASE 0x40000000u

typedef struct KMT_ADAPTER {
    DXGI_ADAPTER_DESC1 Desc;
    LARGE_INTEGER UmdVersion;
} KMT_ADAPTER;

static KMT_ADAPTER g_adapters[MAX_ADAPTERS];
static UINT g_count;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static volatile LONG g_open[MAX_ADAPTERS];

typedef HRESULT(WINAPI *CREATE_FACTORY)(REFIID riid, void **factory);

static BOOL CALLBACK enumerate(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    CREATE_FACTORY create = dxgi ? (CREATE_FACTORY)(void *)GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr;
    IDXGIFactory1 *factory = nullptr;
    if (!create || FAILED(create(&IID_IDXGIFactory1, (void **)&factory)) || !factory)
        return TRUE;
    IDXGIAdapter1 *adapter = nullptr;
    for (UINT i = 0; g_count < MAX_ADAPTERS && SUCCEEDED(factory->lpVtbl->EnumAdapters1(factory, i, &adapter)); i++) {
        KMT_ADAPTER *a = &g_adapters[g_count];
        if (SUCCEEDED(adapter->lpVtbl->GetDesc1(adapter, &a->Desc))) {
            if (FAILED(adapter->lpVtbl->CheckInterfaceSupport(adapter, &IID_IDXGIDevice, &a->UmdVersion)))
                a->UmdVersion.QuadPart = 0;
            g_count++;
        }
        adapter->lpVtbl->Release(adapter);
    }
    factory->lpVtbl->Release(factory);
    return TRUE;
}

static UINT adapters(void)
{
    InitOnceExecuteOnce(&g_once, enumerate, nullptr, nullptr);
    return g_count;
}

static KMT_ADAPTER *adapter_of(D3DKMT_HANDLE handle)
{
    UINT index = handle - HANDLE_BASE;
    if (handle < HANDLE_BASE || index >= adapters() || !g_open[index])
        return nullptr;
    return &g_adapters[index];
}

static D3DKMT_HANDLE open_index(UINT index)
{
    InterlockedIncrement(&g_open[index]);
    return HANDLE_BASE + index;
}

static void fill_info(UINT index, D3DKMT_ADAPTERINFO *info)
{
    const DXGI_ADAPTER_DESC1 *d = &g_adapters[index].Desc;
    info->hAdapter = open_index(index);
    info->AdapterLuid = d->AdapterLuid;
    info->NumOfSources = (d->Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? 0 : 1;
    info->bPrecisePresentRegionsPreferred = FALSE;
}

/* each listed adapter is opened, and the caller closes it. */
WINGDIAPI NTSTATUS APIENTRY D3DKMTEnumAdapters2(D3DKMT_ENUMADAPTERS2 *enumerate)
{
    if (!enumerate)
        return KMT_INVALID_PARAMETER;
    UINT count = adapters();
    if (!enumerate->pAdapters) {
        enumerate->NumAdapters = count;
        return KMT_SUCCESS;
    }
    if (enumerate->NumAdapters < count) {
        enumerate->NumAdapters = count;
        return KMT_BUFFER_TOO_SMALL;
    }
    for (UINT i = 0; i < count; i++)
        fill_info(i, &enumerate->pAdapters[i]);
    enumerate->NumAdapters = count;
    return KMT_SUCCESS;
}

WINGDIAPI NTSTATUS APIENTRY D3DKMTEnumAdapters(D3DKMT_ENUMADAPTERS *enumerate)
{
    if (!enumerate)
        return KMT_INVALID_PARAMETER;
    UINT count = adapters();
    for (UINT i = 0; i < count; i++)
        fill_info(i, &enumerate->Adapters[i]);
    enumerate->NumAdapters = count;
    return KMT_SUCCESS;
}

WINGDIAPI NTSTATUS APIENTRY D3DKMTOpenAdapterFromLuid(D3DKMT_OPENADAPTERFROMLUID *open)
{
    if (!open)
        return KMT_INVALID_PARAMETER;
    UINT count = adapters();
    for (UINT i = 0; i < count; i++)
        if (g_adapters[i].Desc.AdapterLuid.LowPart == open->AdapterLuid.LowPart &&
            g_adapters[i].Desc.AdapterLuid.HighPart == open->AdapterLuid.HighPart) {
            open->hAdapter = open_index(i);
            return KMT_SUCCESS;
        }
    return KMT_INVALID_PARAMETER;
}

WINGDIAPI NTSTATUS APIENTRY D3DKMTCloseAdapter(const D3DKMT_CLOSEADAPTER *close)
{
    if (!close || !adapter_of(close->hAdapter))
        return KMT_INVALID_PARAMETER;
    InterlockedDecrement(&g_open[close->hAdapter - HANDLE_BASE]);
    return KMT_SUCCESS;
}

#define NEED(type)                                                                                                     \
    do {                                                                                                               \
        if (query->PrivateDriverDataSize != sizeof(type))                                                              \
            return KMT_INVALID_PARAMETER;                                                                              \
    } while (0)

WINGDIAPI NTSTATUS APIENTRY D3DKMTQueryAdapterInfo(const D3DKMT_QUERYADAPTERINFO *query)
{
    if (!query || !query->pPrivateDriverData)
        return KMT_INVALID_PARAMETER;
    KMT_ADAPTER *a = adapter_of(query->hAdapter);
    if (!a)
        return KMT_INVALID_PARAMETER;
    const DXGI_ADAPTER_DESC1 *d = &a->Desc;
    BOOL software = (d->Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    void *data = query->pPrivateDriverData;
    switch (query->Type) {
    case KMTQAITYPE_GETSEGMENTSIZE: {
        NEED(D3DKMT_SEGMENTSIZEINFO);
        D3DKMT_SEGMENTSIZEINFO *size = data;
        size->DedicatedVideoMemorySize = d->DedicatedVideoMemory;
        size->DedicatedSystemMemorySize = d->DedicatedSystemMemory;
        size->SharedSystemMemorySize = d->SharedSystemMemory;
        return KMT_SUCCESS;
    }
    case KMTQAITYPE_ADAPTERTYPE:
    case KMTQAITYPE_ADAPTERTYPE_RENDER:
        NEED(UINT);
        *(UINT *)data = software ? KMT_TYPE_RENDER | KMT_TYPE_SOFTWARE : KMT_TYPE_RENDER | KMT_TYPE_DISPLAY;
        return KMT_SUCCESS;
    case KMTQAITYPE_DRIVERVERSION:
    case KMTQAITYPE_DRIVERVERSION_RENDER:
        NEED(UINT);
        *(UINT *)data = KMT_DRIVERVERSION_WDDM_3_1;
        return KMT_SUCCESS;
    case KMTQAITYPE_UMD_DRIVER_VERSION:
        NEED(LARGE_INTEGER);
        *(LARGE_INTEGER *)data = a->UmdVersion;
        return KMT_SUCCESS;
    case KMTQAITYPE_CHECKDRIVERUPDATESTATUS:
    case KMTQAITYPE_CHECKDRIVERUPDATESTATUS_RENDER:
        NEED(BOOL);
        *(BOOL *)data = FALSE;
        return KMT_SUCCESS;
    case KMTQAITYPE_PHYSICALADAPTERCOUNT:
        NEED(UINT);
        *(UINT *)data = 1;
        return KMT_SUCCESS;
    case KMTQAITYPE_PHYSICALADAPTERDEVICEIDS: {
        NEED(D3DKMT_QUERY_DEVICE_IDS);
        D3DKMT_QUERY_DEVICE_IDS *ids = data;
        if (ids->PhysicalAdapterIndex)
            return KMT_INVALID_PARAMETER;
        ids->DeviceIds.VendorID = d->VendorId;
        ids->DeviceIds.DeviceID = d->DeviceId;
        ids->DeviceIds.SubVendorID = d->SubSysId & 0xFFFF;
        ids->DeviceIds.SubSystemID = d->SubSysId >> 16;
        ids->DeviceIds.RevisionID = d->Revision;
        ids->DeviceIds.BusType = software ? 0 : KMT_BUS_PCI;
        return KMT_SUCCESS;
    }
    case KMTQAITYPE_ADAPTERADDRESS:
    case KMTQAITYPE_ADAPTERADDRESS_RENDER: {
        NEED(D3DKMT_ADAPTERADDRESS);
        D3DKMT_ADAPTERADDRESS *address = data;
        UINT index = query->hAdapter - HANDLE_BASE;
        address->BusNumber = software ? 0 : index + 1;
        address->DeviceNumber = 0;
        address->FunctionNumber = 0;
        return KMT_SUCCESS;
    }
    /* the adapter's guid is made from its luid and ids, so it stays the same across processes. */
    case KMTQAITYPE_ADAPTERGUID:
    case KMTQAITYPE_ADAPTERGUID_RENDER:
    case KMTQAITYPE_QUERY_ADAPTER_UNIQUE_GUID: {
        NEED(GUID);
        GUID *guid = data;
        guid->Data1 = d->AdapterLuid.LowPart;
        guid->Data2 = (USHORT)d->VendorId;
        guid->Data3 = (USHORT)d->DeviceId;
        memset(guid->Data4, 0, sizeof(guid->Data4));
        guid->Data4[0] = 0x4D; /* "MIME" */
        guid->Data4[1] = 0x49;
        guid->Data4[2] = 0x4D;
        guid->Data4[3] = 0x45;
        return KMT_SUCCESS;
    }
    case KMTQAITYPE_ADAPTERREGISTRYINFO:
    case KMTQAITYPE_ADAPTERREGISTRYINFO_RENDER: {
        NEED(D3DKMT_ADAPTERREGISTRYINFO);
        D3DKMT_ADAPTERREGISTRYINFO *info = data;
        memset(info, 0, sizeof(*info));
        lstrcpynW(info->AdapterString, d->Description, MAX_PATH);
        lstrcpynW(info->ChipType, d->Description, MAX_PATH);
        lstrcpynW(info->DacType, software ? L"" : L"Integrated RAMDAC", MAX_PATH);
        return KMT_SUCCESS;
    }
    case KMTQAITYPE_DRIVER_DESCRIPTION:
    case KMTQAITYPE_DRIVER_DESCRIPTION_RENDER: {
        NEED(D3DKMT_DRIVER_DESCRIPTION);
        D3DKMT_DRIVER_DESCRIPTION *description = data;
        memset(description, 0, sizeof(*description));
        lstrcpynW(description->DriverDescription, d->Description, RTL_NUMBER_OF(description->DriverDescription));
        return KMT_SUCCESS;
    }
    /* the user mode driver of every adapter is mime's d3d runtime itself. */
    case KMTQAITYPE_UMDRIVERNAME: {
        NEED(D3DKMT_UMDFILENAMEINFO);
        D3DKMT_UMDFILENAMEINFO *name = data;
        static const WCHAR *const drivers[] = {L"d3d9.dll", L"d3d10core.dll", L"d3d11.dll", L"d3d12.dll"};
        if (name->Version >= RTL_NUMBER_OF(drivers))
            return KMT_INVALID_PARAMETER;
        UINT n = GetSystemDirectoryW(name->UmdFileName, MAX_PATH);
        if (!n || n + 1 + lstrlenW(drivers[name->Version]) >= MAX_PATH)
            return KMT_INVALID_PARAMETER;
        name->UmdFileName[n] = L'\\';
        lstrcpyW(name->UmdFileName + n + 1, drivers[name->Version]);
        return KMT_SUCCESS;
    }
    default: {
        char text[96];
        _snprintf(text, sizeof(text), "gdi32: D3DKMTQueryAdapterInfo type %u size %u is not answered\n", query->Type,
                  query->PrivateDriverDataSize);
        OutputDebugStringA(text);
        return KMT_NOT_SUPPORTED;
    }
    }
}
