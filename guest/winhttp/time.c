/* HTTP date conversion and SYSTEMTIME header parsing. */
#include "winhttp_private.h"

static const char *const Days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const Months[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

#define WORD_ZONE (-3)
#define WORD_UNKNOWN (-2)

/* match day and month names by their leading letters. */
static int MapWord(const char *p)
{
    char a = (char)(p[0] & 0xDF), b = (char)(p[1] & 0xDF), c = p[1] ? (char)(p[2] & 0xDF) : 0;
    switch (a) {
    case 'A':
        return b == 'P' ? 4 : b == 'U' ? 8 : WORD_UNKNOWN;
    case 'D':
        return 12;
    case 'F':
        return b == 'E' ? 2 : b == 'R' ? 5 : WORD_UNKNOWN;
    case 'G':
    case 'U':
        return WORD_ZONE;
    case 'J':
        if (b == 'A')
            return 1;
        if (b != 'U')
            return WORD_UNKNOWN;
        return c == 'L' ? 7 : c == 'N' ? 6 : WORD_UNKNOWN;
    case 'M':
        if (b == 'A')
            return c == 'R' ? 3 : c == 'Y' ? 5 : WORD_UNKNOWN;
        return b == 'O' ? 1 : WORD_UNKNOWN;
    case 'N':
        return 11;
    case 'O':
        return 10;
    case 'S':
        return b == 'A' ? 6 : b == 'E' ? 9 : b == 'U' ? 0 : WORD_UNKNOWN;
    case 'T':
        return b == 'H' ? 4 : b == 'U' ? 2 : WORD_UNKNOWN;
    case 'W':
        return 3;
    }
    return WORD_UNKNOWN;
}

BOOL WhParseHttpDate(const char *text, SYSTEMTIME *time)
{
    int fields[8] = {0};
    int count = 0, last_word = -1;
    BOOL asctime = FALSE;
    const char *p = text;
    while (*p && count < 8) {
        if (*p >= '0' && *p <= '9') {
            int value = 0;
            while (*p >= '0' && *p <= '9')
                value = value * 10 + (*p++ - '0');
            fields[count++] = value;
        } else if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            int value = MapWord(p);
            last_word = count;
            if (value == WORD_UNKNOWN && (!asctime || count != 6))
                return FALSE;
            if (count == 1)
                asctime = TRUE;
            fields[count++] = value;
            while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
                p++;
        } else {
            p++;
        }
    }
    SYSTEMTIME st = {0};
    st.wDayOfWeek = (WORD)fields[0];
    if (!asctime) {
        st.wDay = (WORD)fields[1];
        st.wMonth = (WORD)fields[2];
        st.wYear = (WORD)fields[3];
        st.wHour = (WORD)fields[4];
        st.wMinute = (WORD)fields[5];
        st.wSecond = (WORD)fields[6];
    } else {
        st.wMonth = (WORD)fields[1];
        st.wDay = (WORD)fields[2];
        st.wHour = (WORD)fields[3];
        st.wMinute = (WORD)fields[4];
        st.wSecond = (WORD)fields[5];
        st.wYear = (WORD)(last_word == 6 ? fields[7] : fields[6]);
    }
    if (st.wYear < 100)
        st.wYear += st.wYear < 80 ? 2000 : 1900;
    if (count < 4) {
        SYSTEMTIME now;
        GetSystemTime(&now);
        if (count >= 2) {
            st.wHour = now.wHour;
            st.wMinute = now.wMinute;
            st.wSecond = now.wSecond;
        } else {
            st = now;
        }
    }
    if (st.wDayOfWeek > 6 || st.wDay > 31 || st.wHour > 23 || st.wMinute > 59 || st.wSecond > 59)
        return FALSE;
    if (last_word == 6) {
        FILETIME local, utc;
        if (!SystemTimeToFileTime(&st, &local) || !LocalFileTimeToFileTime(&local, &utc) ||
            !FileTimeToSystemTime(&utc, &st))
            return FALSE;
    }
    st.wMilliseconds = 0;
    *time = st;
    return TRUE;
}

BOOL WINAPI WinHttpTimeFromSystemTime(const SYSTEMTIME *time, LPWSTR text)
{
    if (!time || !text)
        return WhFail(ERROR_INVALID_PARAMETER);
    FILETIME file;
    SYSTEMTIME st;
    if (!SystemTimeToFileTime(time, &file) || !FileTimeToSystemTime(&file, &st))
        return WhFail(ERROR_INVALID_PARAMETER);
    char ansi[64];
    wsprintfA(ansi, "%s, %02i %s %02i %02i:%02i:%02i GMT", Days[st.wDayOfWeek], st.wDay, Months[st.wMonth],
              st.wYear, st.wHour, st.wMinute, st.wSecond);
    if (!MultiByteToWideChar(CP_ACP, 0, ansi, -1, text, WINHTTP_TIME_FORMAT_BUFSIZE / sizeof(WCHAR)))
        return WhFail(ERROR_INSUFFICIENT_BUFFER);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI WinHttpTimeToSystemTime(LPCWSTR text, SYSTEMTIME *time)
{
    if (!text || !time)
        return WhFail(ERROR_INVALID_PARAMETER);
    BOOL lossy = FALSE;
    char *ansi = WhWideToAnsi(text, (DWORD)wcslen(text), &lossy);
    if (!ansi)
        return WhFail(ERROR_NOT_ENOUGH_MEMORY);
    if (lossy) {
        WhFree(ansi);
        return WhFail(ERROR_INVALID_PARAMETER);
    }
    BOOL ok = WhParseHttpDate(ansi, time);
    WhFree(ansi);
    if (!ok)
        return WhFail(ERROR_INVALID_PARAMETER);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}
