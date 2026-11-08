#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

/*
 * the user profile routines windows exports by ordinal only. programs seen so far load the dll and check it without
 * calling into it, so each routine answers that it is not implemented.
 */
static HRESULT probe(const char *name, ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return E_NOTIMPL;
}

HRESULT WINAPI CreateEnvBlock(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("CreateEnvBlock", a, b, c, d);
}

HRESULT WINAPI DestroyEnvBlock(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("DestroyEnvBlock", a, b, c, d);
}

HRESULT WINAPI ExpandEnvStringForUser(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("ExpandEnvStringForUser", a, b, c, d);
}

HRESULT WINAPI GetBasicProfileFolderPath(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("GetBasicProfileFolderPath", a, b, c, d);
}

HRESULT WINAPI LoadProfileBasic(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("LoadProfileBasic", a, b, c, d);
}

HRESULT WINAPI UnloadProfileBasic(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("UnloadProfileBasic", a, b, c, d);
}

HRESULT WINAPI GetAppContainerRegistryHandle(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("GetAppContainerRegistryHandle", a, b, c, d);
}

HRESULT WINAPI CreateAppContainerEnumerator(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("CreateAppContainerEnumerator", a, b, c, d);
}

HRESULT WINAPI GetNextAppContainerSid(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("GetNextAppContainerSid", a, b, c, d);
}

HRESULT WINAPI DeleteAppContainerEnumerator(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("DeleteAppContainerEnumerator", a, b, c, d);
}

HRESULT WINAPI GetAppContainerPath(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("GetAppContainerPath", a, b, c, d);
}

HRESULT WINAPI GetAppContainerRegistryPath(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("GetAppContainerRegistryPath", a, b, c, d);
}

HRESULT WINAPI GetAppContainerSpecificSubPath(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("GetAppContainerSpecificSubPath", a, b, c, d);
}

HRESULT WINAPI GetAppContainerRegistryHandleFromName(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("GetAppContainerRegistryHandleFromName", a, b, c, d);
}

HRESULT WINAPI GetAppContainerPathFromSidString(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("GetAppContainerPathFromSidString", a, b, c, d);
}

HRESULT WINAPI GetBasicProfileFolderPathEx(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("GetBasicProfileFolderPathEx", a, b, c, d);
}

HRESULT WINAPI GetBasicProfileFolderPathAlloc(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d)
{
    return probe("GetBasicProfileFolderPathAlloc", a, b, c, d);
}
