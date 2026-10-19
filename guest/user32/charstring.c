/*
 * the ANSI character functions of user32, which kernelbase forwards to, as the contracts of windows name them: case mapping of
 * buffers, the previous character and the class of one character, all through the ANSI code page.
 */
#include <mime/win32/winuser.h>
#include <mime/win32/winnls.h>
#include <rtl.h>

/* maps a buffer of the ANSI code page through its wide characters; the bytes written are returned. */
static DWORD map_buffer(LPSTR text, DWORD length, DWORD flag)
{
    if (!length)
        return 0;
    PWSTR wide = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, 2 * (SIZE_T)length * sizeof(WCHAR));
    if (!wide)
        return 0;
    PWSTR mapped = wide + length;
    DWORD result = 0;
    int count = MultiByteToWideChar(CP_ACP, 0, text, (int)length, wide, (int)length);
    if (count > 0) {
        /* a character the mapping does not know stays as it was. */
        if (LCMapStringEx(nullptr, flag, wide, count, mapped, count, nullptr, nullptr, 0) != count)
            memcpy(mapped, wide, count * sizeof(WCHAR));
        ULONG written = 0;
        RtlUnicodeToMultiByteN(text, length, &written, mapped, count * sizeof(WCHAR));
        result = written;
    }
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, wide);
    return result;
}

WINUSERAPI DWORD WINAPI CharUpperBuffA(LPSTR text, DWORD length)
{
    return map_buffer(text, length, LCMAP_UPPERCASE);
}

WINUSERAPI DWORD WINAPI CharLowerBuffA(LPSTR text, DWORD length)
{
    return map_buffer(text, length, LCMAP_LOWERCASE);
}

/* the character before a position, which is two bytes back when it ends a double byte character. */
WINUSERAPI LPSTR WINAPI CharPrevA(LPCSTR start, LPCSTR current)
{
    if (current <= start)
        return (LPSTR)current;
    LPCSTR previous = current - 1;
    if (previous - 1 < start)
        return (LPSTR)previous;
    /* an odd number of lead bytes in a row before it makes it a trail byte. */
    BOOL trail = FALSE;
    for (LPCSTR p = previous - 1; p >= start && IsDBCSLeadByte((BYTE)*p); p--)
        trail = !trail;
    return (LPSTR)(trail ? previous - 1 : previous);
}

static BOOL character_type(CHAR c, WORD mask)
{
    BYTE byte = (BYTE)c;
    if (IsDBCSLeadByte(byte))
        return FALSE;
    WCHAR wide = 0;
    ULONG written;
    RtlMultiByteToUnicodeN(&wide, sizeof(wide), &written, (PCCH)&byte, 1);
    WORD type = 0;
    GetStringTypeW(CT_CTYPE1, &wide, 1, &type);
    return (type & mask) != 0;
}

WINUSERAPI BOOL WINAPI IsCharAlphaA(CHAR c)
{
    return character_type(c, C1_ALPHA);
}

WINUSERAPI BOOL WINAPI IsCharAlphaNumericA(CHAR c)
{
    return character_type(c, C1_ALPHA | C1_DIGIT);
}

WINUSERAPI BOOL WINAPI IsCharLowerA(CHAR c)
{
    return character_type(c, C1_LOWER);
}

WINUSERAPI BOOL WINAPI IsCharUpperA(CHAR c)
{
    return character_type(c, C1_UPPER);
}
