/*
 * tdh.dll: trace data helper. event providers describe their events with manifests that the system loads, and no
 * provider has registered one, so no event can be described and no manifest loaded.
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

ULONG WINAPI TdhGetEventInformation(PVOID event, ULONG context_count, PVOID contexts, PVOID info, PULONG size)
{
    if (!event || !size)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_FOUND;
}

ULONG WINAPI TdhGetEventMapInformation(PVOID event, PWSTR map_name, PVOID info, PULONG size)
{
    if (!event || !map_name || !size)
        return ERROR_INVALID_PARAMETER;
    return ERROR_NOT_FOUND;
}

ULONG WINAPI TdhLoadManifest(PWSTR manifest)
{
    return manifest ? ERROR_NOT_SUPPORTED : ERROR_INVALID_PARAMETER;
}

ULONG WINAPI TdhUnloadManifest(PWSTR manifest)
{
    return manifest ? ERROR_NOT_FOUND : ERROR_INVALID_PARAMETER;
}
