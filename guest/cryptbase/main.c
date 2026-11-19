#include <mime/nt.h>
#include <mime/win32/winbase.h>

__declspec(dllexport) ULONG_PTR WINAPI SystemFunction001(void)
{
    DbgPrint("MISSING cryptbase %s\n", "SystemFunction001");
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI SystemFunction002(void)
{
    DbgPrint("MISSING cryptbase %s\n", "SystemFunction002");
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI SystemFunction003(void)
{
    DbgPrint("MISSING cryptbase %s\n", "SystemFunction003");
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI SystemFunction004(void)
{
    DbgPrint("MISSING cryptbase %s\n", "SystemFunction004");
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI SystemFunction005(void)
{
    DbgPrint("MISSING cryptbase %s\n", "SystemFunction005");
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI SystemFunction028(void)
{
    DbgPrint("MISSING cryptbase %s\n", "SystemFunction028");
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI SystemFunction029(void)
{
    DbgPrint("MISSING cryptbase %s\n", "SystemFunction029");
    return 0;
}

__declspec(dllexport) ULONG_PTR WINAPI SystemFunction034(void)
{
    DbgPrint("MISSING cryptbase %s\n", "SystemFunction034");
    return 0;
}

typedef BOOL(WINAPI *PROCESS_PRNG)(PBYTE data, SIZE_T size);

/* RtlGenRandom: the process generator of bcryptprimitives, as windows 10 and later have it. */
__declspec(dllexport) BOOLEAN WINAPI SystemFunction036(PVOID buffer, ULONG length)
{
    static PROCESS_PRNG prng;
    if (!prng) {
        UNICODE_STRING name = RTL_CONSTANT_STRING(L"bcryptprimitives.dll");
        ANSI_STRING entry = RTL_CONSTANT_STRING("ProcessPrng");
        PVOID module, address;
        if (!NT_SUCCESS(LdrLoadDll(nullptr, nullptr, &name, &module)) ||
            !NT_SUCCESS(LdrGetProcedureAddress(module, &entry, 0, &address)))
            return FALSE;
        prng = (PROCESS_PRNG)address;
    }
    return prng(buffer, length) != 0;
}

/*
 * RtlEncryptMemory and RtlDecryptMemory: KsecDD keys the blocks by the option, the process, any process or the logon
 * session, and transforms the memory where it lies.
 */
static NTSTATUS crypt_memory(PVOID memory, ULONG size, ULONG options, BOOL encrypt)
{
    static HANDLE device;
    static const ULONG codes[3][2] = {{0x39000E, 0x390012}, {0x390016, 0x39001A}, {0x39001E, 0x390022}};
    if (options > 2)
        return STATUS_INVALID_PARAMETER;
    if (!device) {
        UNICODE_STRING name = RTL_CONSTANT_STRING(L"\\Device\\KsecDD");
        OBJECT_ATTRIBUTES oa;
        InitializeObjectAttributes(&oa, &name, 0, nullptr, nullptr);
        IO_STATUS_BLOCK io;
        HANDLE h;
        NTSTATUS status = NtOpenFile(&h, SYNCHRONIZE | FILE_READ_DATA | FILE_WRITE_DATA, &oa, &io,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_SYNCHRONOUS_IO_NONALERT);
        if (!NT_SUCCESS(status))
            return status;
        if (InterlockedCompareExchangePointer(&device, h, nullptr))
            NtClose(h);
    }
    IO_STATUS_BLOCK io;
    return NtDeviceIoControlFile(device, nullptr, nullptr, nullptr, &io, codes[options][encrypt ? 0 : 1], memory, size,
                                 memory, size);
}

__declspec(dllexport) NTSTATUS WINAPI SystemFunction040(PVOID memory, ULONG size, ULONG options)
{
    return crypt_memory(memory, size, options, TRUE);
}

__declspec(dllexport) NTSTATUS WINAPI SystemFunction041(PVOID memory, ULONG size, ULONG options)
{
    return crypt_memory(memory, size, options, FALSE);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}