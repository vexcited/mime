/*
 * ANSI character functions forwarded here by kernelbase: case mapping, character navigation and classification.
 */
#include <mime/win32/winuser.h>
#include <mime/win32/winnls.h>
#include <rtl.h>

/* maps through Unicode and returns the number of ANSI bytes written. */
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
        /* leave the text unchanged if mapping fails. */
        if (LCMapStringEx(nullptr, flag, wide, count, mapped, count, nullptr, nullptr, 0) != count)
            memcpy(mapped, wide, count * sizeof(WCHAR));
        result = (DWORD)WideCharToMultiByte(CP_ACP, 0, mapped, count, text, (int)length, nullptr, nullptr);
    }
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, wide);
    return result;
}

DWORD CharUpperLowerBuffA(LPSTR text, DWORD length, BOOL upper)
{
    return map_buffer(text, length, upper ? LCMAP_UPPERCASE : LCMAP_LOWERCASE);
}

WINUSERAPI DWORD WINAPI CharUpperBuffA(LPSTR text, DWORD length)
{
    return map_buffer(text, length, LCMAP_UPPERCASE);
}

WINUSERAPI DWORD WINAPI CharLowerBuffA(LPSTR text, DWORD length)
{
    return map_buffer(text, length, LCMAP_LOWERCASE);
}

/* walk from the start to distinguish trail bytes from lead bytes. */
WINUSERAPI LPSTR WINAPI CharPrevA(LPCSTR start, LPCSTR current)
{
    if (current <= start)
        return (LPSTR)current;
    LPCSTR p = start, previous = start;
    while (p < current && *p) {
        previous = p;
        p = CharNextA(p);
    }
    return (LPSTR)previous;
}

static BOOL character_type(CHAR c, WORD mask)
{
    BYTE byte = (BYTE)c;
    if (IsDBCSLeadByte(byte))
        return FALSE;
    WCHAR wide = 0;
    if (MultiByteToWideChar(CP_ACP, 0, (LPCCH)&byte, 1, &wide, 1) != 1)
        return FALSE;
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
