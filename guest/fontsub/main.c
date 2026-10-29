/*
 * fontsub.dll: font subsetting. a package is made of the glyphs that a document uses, and merged back with the glyphs
 * of another. neither is done here, so the calls report the generic failure of the library.
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

#define ERR_GENERIC 1000

ULONG WINAPI CreateFontPackage(const BYTE *source, ULONG source_size, BYTE **package, ULONG *package_size, ULONG *written,
                               USHORT flags, USHORT ttc_index, USHORT format, USHORT language, USHORT platform,
                               USHORT encoding, const USHORT *keep, USHORT keep_count, PVOID allocate, PVOID reallocate,
                               PVOID release, PVOID reserved)
{
    if (package)
        *package = nullptr;
    if (package_size)
        *package_size = 0;
    if (written)
        *written = 0;
    return ERR_GENERIC;
}

ULONG WINAPI MergeFontPackage(const BYTE *base, ULONG base_size, const BYTE *delta, ULONG delta_size, BYTE **merged,
                              ULONG *merged_size, ULONG *written, USHORT flags, PVOID allocate, PVOID reallocate,
                              PVOID release, PVOID reserved)
{
    if (merged)
        *merged = nullptr;
    if (merged_size)
        *merged_size = 0;
    if (written)
        *written = 0;
    return ERR_GENERIC;
}
