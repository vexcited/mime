/* advertising a product: MsiAdvertiseProduct and the installation with ACTION=ADVERTISE that it runs. */
#include "msi_private.h"

/* the text of an installation that advertises, for the package and the options the caller gave. */
static UINT advertise_command(DWORD context, PCWSTR transforms, LANGID language, DWORD options, PWSTR *out)
{
    SIZE_T capacity = (transforms ? wcslen(transforms) : 0) + 128;
    PWSTR command = MimeMsiAllocate(capacity * sizeof(WCHAR));
    if (!command)
        return ERROR_NOT_ENOUGH_MEMORY;
    SIZE_T at = (SIZE_T)_snwprintf(command, capacity, L"ACTION=ADVERTISE ALLUSERS=%s", context == 0 ? L"1" : L"\"\"");
    if (language)
        at += (SIZE_T)_snwprintf(command + at, capacity - at, L" PRODUCTLANGUAGE=%u", language);
    if (options & 1)
        at += (SIZE_T)_snwprintf(command + at, capacity - at, L" MSINEWINSTANCE=1");
    if (transforms && *transforms)
        _snwprintf(command + at, capacity - at, L" TRANSFORMS=\"%s\"", transforms);
    *out = command;
    return ERROR_SUCCESS;
}

/* the script argument is a path, or a small number that says who the product is advertised to. */
UINT WINAPI MsiAdvertiseProductExW(LPCWSTR package, LPCWSTR script, LPCWSTR transforms, LANGID language, DWORD platform,
                                   DWORD options)
{
    if (!package || wcslen(package) > MAX_PATH || ((UINT_PTR)script > 1 && wcslen(script) > MAX_PATH) ||
        (platform & ~0xfu) || (options & ~1u) || ((options & 1) && (!transforms || !*transforms)))
        return ERROR_INVALID_PARAMETER;
    if ((UINT_PTR)script > 1)
        return MimeMsiAdvertiseScriptFile(package, script, transforms, language, platform, options);
    if (platform & 7)
        return ERROR_INVALID_PARAMETER;
    PWSTR command;
    UINT error = advertise_command((DWORD)(UINT_PTR)script, transforms, language, options, &command);
    if (!error) {
        error = MsiInstallProductW(package, command);
        MimeMsiFree(command);
    }
    return error;
}

UINT WINAPI MsiAdvertiseProductW(LPCWSTR package, LPCWSTR script, LPCWSTR transforms, LANGID language)
{
    return MsiAdvertiseProductExW(package, script, transforms, language, 0, 0);
}

UINT MimeMsiAdvertiseScriptFile(PCWSTR package, PCWSTR script, PCWSTR transforms, LANGID language, DWORD platform, DWORD options)
{
    return ERROR_CALL_NOT_IMPLEMENTED;
}
