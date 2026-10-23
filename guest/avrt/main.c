/* there is no MMCSS: threads keep their normal priority. */
#include <mime/win32/winbase.h>

#define ERROR_SERVICE_NOT_ACTIVE 1062L

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

DECLSPEC_EXPORT HANDLE WINAPI AvSetMmThreadCharacteristicsW(LPCWSTR task, DWORD *index)
{
    SetLastError(task && index ? ERROR_SERVICE_NOT_ACTIVE : ERROR_INVALID_PARAMETER);
    return nullptr;
}

DECLSPEC_EXPORT BOOL WINAPI AvSetMmThreadPriority(HANDLE task, int priority)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

DECLSPEC_EXPORT BOOL WINAPI AvRevertMmThreadCharacteristics(HANDLE task)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}
