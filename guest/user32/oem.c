/*
 * the conversions between the OEM character set of the console and the ANSI character set of the windows: byte by
 * byte through a table for the ansi forms, which may work in place, and through Unicode for the wide ones. Windows
 * builds its two 256 byte tables when the session starts; here each is built the first time it is used, from the
 * code pages of the machine through Unicode, which is a guess at how windows fills it.
 */
#include "user32_private.h"
#include <mime/win32/winuser.h>

#define CP_OEMCP_ 1
#define MB_PRECOMPOSED_ 0x1
#define MB_USEGLYPHCHARS_ 0x4

static UCHAR g_oem_to_ansi[256];
static UCHAR g_ansi_to_oem[256];
static volatile LONG g_tables_ready;

static void convert_table(UINT from, UINT to, UCHAR *table)
{
    for (int b = 0; b < 256; b++) {
        CHAR in = (CHAR)b;
        WCHAR wide = 0;
        CHAR out = (CHAR)b;
        /* a lead byte or a byte the page does not have stays as it is. */
        if (b >= 0x80 && MultiByteToWideChar(from, MB_PRECOMPOSED_ | MB_USEGLYPHCHARS_, &in, 1, &wide, 1) == 1) {
            BOOL used = FALSE;
            CHAR fallback = '?';
            if (WideCharToMultiByte(to, 0, &wide, 1, &out, 1, &fallback, &used) != 1)
                out = (CHAR)b;
        }
        table[b] = (UCHAR)out;
    }
}

static void build_tables(void)
{
    if (g_tables_ready)
        return;
    convert_table(CP_OEMCP_, CP_ACP, g_oem_to_ansi);
    convert_table(CP_ACP, CP_OEMCP_, g_ansi_to_oem);
    InterlockedExchange(&g_tables_ready, 1);
}

WINUSERAPI BOOL WINAPI OemToCharBuffA(LPCSTR source, LPSTR destination, DWORD count)
{
    if (!source || !destination)
        return FALSE;
    build_tables();
    for (DWORD i = 0; i < count; i++)
        destination[i] = (CHAR)g_oem_to_ansi[(UCHAR)source[i]];
    return TRUE;
}

WINUSERAPI BOOL WINAPI OemToCharA(LPCSTR source, LPSTR destination)
{
    if (!source || !destination)
        return FALSE;
    build_tables();
    /* the terminator is converted too, which the table leaves as it is. */
    for (;; source++, destination++) {
        CHAR original = *source;
        *destination = (CHAR)g_oem_to_ansi[(UCHAR)original];
        if (!original)
            break;
    }
    return TRUE;
}

WINUSERAPI BOOL WINAPI CharToOemBuffA(LPCSTR source, LPSTR destination, DWORD count)
{
    if (!source || !destination)
        return FALSE;
    build_tables();
    for (DWORD i = 0; i < count; i++)
        destination[i] = (CHAR)g_ansi_to_oem[(UCHAR)source[i]];
    return TRUE;
}

WINUSERAPI BOOL WINAPI CharToOemA(LPCSTR source, LPSTR destination)
{
    if (!source || !destination)
        return FALSE;
    build_tables();
    for (;; source++, destination++) {
        CHAR original = *source;
        *destination = (CHAR)g_ansi_to_oem[(UCHAR)original];
        if (!original)
            break;
    }
    return TRUE;
}

/* the wide forms refuse a destination that is the source, which would be a different size. */
WINUSERAPI BOOL WINAPI OemToCharBuffW(LPCSTR source, LPWSTR destination, DWORD count)
{
    return source && destination && (const void *)source != (const void *)destination &&
           MultiByteToWideChar(CP_OEMCP_, MB_PRECOMPOSED_ | MB_USEGLYPHCHARS_, source, (int)count, destination, (int)count) != 0;
}

WINUSERAPI BOOL WINAPI OemToCharW(LPCSTR source, LPWSTR destination)
{
    if (!source || !destination || (const void *)source == (const void *)destination)
        return FALSE;
    int length = lstrlenA(source) + 1;
    MultiByteToWideChar(CP_OEMCP_, MB_PRECOMPOSED_ | MB_USEGLYPHCHARS_, source, length, destination, length);
    return TRUE;
}

/* a character the OEM page has no form for becomes an underscore, and the destination may take two bytes for each. */
WINUSERAPI BOOL WINAPI CharToOemBuffW(LPCWSTR source, LPSTR destination, DWORD count)
{
    if (!source || !destination || (const void *)source == (const void *)destination)
        return FALSE;
    WideCharToMultiByte(CP_OEMCP_, 0, source, (int)count, destination, (int)(2 * count), "_", nullptr);
    return TRUE;
}

WINUSERAPI BOOL WINAPI CharToOemW(LPCWSTR source, LPSTR destination)
{
    if (!source || !destination || (const void *)source == (const void *)destination)
        return FALSE;
    int length = lstrlenW(source) + 1;
    WideCharToMultiByte(CP_OEMCP_, 0, source, length, destination, 2 * length, "_", nullptr);
    return TRUE;
}
