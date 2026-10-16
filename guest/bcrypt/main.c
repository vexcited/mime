/* the system rng uses the existing cryptographic random provider. */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

DECLSPEC_EXPORT NTSTATUS WINAPI BCryptGenRandom(HANDLE algorithm, BYTE *buffer, ULONG length, ULONG flags)
{
    if (flags & ~3u || (!buffer && length))
        return STATUS_INVALID_PARAMETER;
    if ((flags & 2) ? algorithm != nullptr : algorithm != (HANDLE)(ULONG_PTR)0x81)
        return STATUS_INVALID_HANDLE;
    if (!length)
        return STATUS_SUCCESS;
    HMODULE library = LoadLibraryW(L"advapi32.dll");
    typedef BOOLEAN (WINAPI *RANDOM)(void *, ULONG);
    RANDOM random = library ? (RANDOM)GetProcAddress(library, "SystemFunction036") : nullptr;
    NTSTATUS status = random && random(buffer, length) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    if (library)
        FreeLibrary(library);
    return status;
}
