#include "msi_private.h"

SRWLOCK MimeMsiLock;
static struct {
    MIME_MSI_OBJECT *Object;
    USHORT Generation;
    DWORD Thread;
} handles[4096];

void *MimeMsiAllocate(size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

void MimeMsiFree(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

MSIHANDLE MimeMsiAddHandle(MIME_MSI_OBJECT *object)
{
    for (ULONG i = 1; i < RTL_NUMBER_OF(handles); i++) {
        if (handles[i].Object)
            continue;
        if (!++handles[i].Generation)
            handles[i].Generation++;
        handles[i].Object = object;
        object->References++;
        handles[i].Thread = GetCurrentThreadId();
        return ((ULONG)handles[i].Generation << 16) | i;
    }
    return 0;
}

MIME_MSI_OBJECT *MimeMsiGetHandle(MSIHANDLE handle, ULONG kind)
{
    ULONG slot = handle & 0xFFFF;
    if (!slot || slot >= RTL_NUMBER_OF(handles) || handles[slot].Generation != handle >> 16)
        return nullptr;
    MIME_MSI_OBJECT *object = handles[slot].Object;
    return object && (!kind || object->Kind == kind) ? object : nullptr;
}

void MimeMsiRelease(MIME_MSI_OBJECT *object)
{
    if (!--object->References)
        object->Destroy(object);
}

UINT WINAPI MsiCloseHandle(MSIHANDLE handle)
{
    AcquireSRWLockExclusive(&MimeMsiLock);
    MIME_MSI_OBJECT *object = MimeMsiGetHandle(handle, 0);
    if (object) {
        handles[handle & 0xFFFF].Object = nullptr;
        MimeMsiRelease(object);
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return object ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
}

UINT WINAPI MsiCloseAllHandles(void)
{
    UINT count = 0;
    DWORD thread = GetCurrentThreadId();
    AcquireSRWLockExclusive(&MimeMsiLock);
    for (ULONG i = 1; i < RTL_NUMBER_OF(handles); i++) {
        MIME_MSI_OBJECT *object = handles[i].Object;
        if (!object || handles[i].Thread != thread)
            continue;
        handles[i].Object = nullptr;
        MimeMsiRelease(object);
        count++;
    }
    ReleaseSRWLockExclusive(&MimeMsiLock);
    return count;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH && !reserved) {
        MimeMsiCloseLog();
        for (ULONG i = 1; i < RTL_NUMBER_OF(handles); i++) {
            if (handles[i].Object) {
                MimeMsiRelease(handles[i].Object);
                handles[i].Object = nullptr;
            }
        }
    }
    return TRUE;
}
