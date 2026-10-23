/* HTTP dates: parse RFC 1123, RFC 850 and asctime forms from RFC 7231; serialize RFC 1123. */
#include "wininet_private.h"

static const char Days[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char Months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static BOOL IsAlpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static BOOL IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

static int DayOfWeek(int year, int month, int day)
{
    static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3)
        year--;
    return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
}

static int DaysIn(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    BOOL leap = (year % 4 == 0 && year % 100) || year % 400 == 0;
    return month == 2 && leap ? 29 : days[month - 1];
}

/* parse any supported form, matching names case-insensitively by their first three letters. */
BOOL InetParseTime(const char *text, SYSTEMTIME *time)
{
    int numbers[4], count = 0, month = 0, hour = -1, minute = 0, second = 0;
    const char *p = text;
    while (*p) {
        if (IsAlpha(*p)) {
            const char *start = p;
            while (IsAlpha(*p))
                p++;
            if (p - start >= 3)
                for (int i = 0; i < 12; i++)
                    if (!_strnicmp(start, Months[i], 3))
                        month = i + 1;
            continue;
        }
        if (IsDigit(*p)) {
            int value = 0, digits = 0;
            while (IsDigit(*p) && digits < 9) {
                value = value * 10 + (*p++ - '0');
                digits++;
            }
            if (*p == ':' && hour < 0) {
                hour = value;
                p++;
                minute = 0;
                while (IsDigit(*p))
                    minute = minute * 10 + (*p++ - '0');
                if (*p == ':') {
                    p++;
                    while (IsDigit(*p))
                        second = second * 10 + (*p++ - '0');
                }
                continue;
            }
            if (count < 4)
                numbers[count++] = digits >= 3 ? -value - 1 : value;
            continue;
        }
        p++;
    }
    if (!month || count < 2)
        return FALSE;
    /* the year is the number of three or more digits, or the second one. */
    int day = numbers[0], year = numbers[1];
    if (day < 0) {
        day = numbers[1];
        year = numbers[0];
    }
    year = year < 0 ? -year - 1 : year < 50 ? 2000 + year : year < 100 ? 1900 + year : year;
    if (hour < 0)
        hour = 0;
    if (day < 1 || day > DaysIn(year, month) || hour > 23 || minute > 59 || second > 60 || year < 1601)
        return FALSE;
    memset(time, 0, sizeof(*time));
    time->wYear = (WORD)year;
    time->wMonth = (WORD)month;
    time->wDay = (WORD)day;
    time->wHour = (WORD)hour;
    time->wMinute = (WORD)minute;
    time->wSecond = (WORD)(second > 59 ? 59 : second);
    time->wDayOfWeek = (WORD)DayOfWeek(year, month, day);
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI InternetTimeToSystemTimeA(LPCSTR text, SYSTEMTIME *time, DWORD reserved)
{
    if (!text || !time)
        return InetFail(ERROR_INVALID_PARAMETER);
    if (!InetParseTime(text, time))
        return InetFail(ERROR_INVALID_PARAMETER);
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI InternetTimeToSystemTimeW(LPCWSTR text, SYSTEMTIME *time, DWORD reserved)
{
    if (!text || !time)
        return InetFail(ERROR_INVALID_PARAMETER);
    char *narrow = InetNarrow(text, -1);
    if (!narrow)
        return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    BOOL ok = InternetTimeToSystemTimeA(narrow, time, reserved);
    InetFree(narrow);
    return ok;
}

static BOOL Format(const SYSTEMTIME *time, DWORD format, char *text)
{
    if (!time || format != INTERNET_RFC1123_FORMAT || time->wMonth < 1 || time->wMonth > 12)
        return FALSE;
    int weekday = DayOfWeek(time->wYear, time->wMonth, time->wDay);
    wsprintfA(text, "%s, %02u %s %04u %02u:%02u:%02u GMT", Days[weekday], time->wDay, Months[time->wMonth - 1],
              time->wYear, time->wHour, time->wMinute, time->wSecond);
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI InternetTimeFromSystemTimeA(const SYSTEMTIME *time, DWORD format, LPSTR text,
                                                       DWORD length)
{
    char buffer[64];
    if (!Format(time, format, buffer))
        return InetFail(ERROR_INVALID_PARAMETER);
    if (!text || length < INTERNET_RFC1123_BUFSIZE)
        return InetFail(ERROR_INSUFFICIENT_BUFFER);
    lstrcpynA(text, buffer, (int)length);
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI InternetTimeFromSystemTimeW(const SYSTEMTIME *time, DWORD format, LPWSTR text,
                                                       DWORD length)
{
    char buffer[64];
    if (!Format(time, format, buffer))
        return InetFail(ERROR_INVALID_PARAMETER);
    if (!text || length < INTERNET_RFC1123_BUFSIZE * sizeof(WCHAR))
        return InetFail(ERROR_INSUFFICIENT_BUFFER);
    MultiByteToWideChar(CP_ACP, 0, buffer, -1, text, (int)(length / sizeof(WCHAR)));
    return TRUE;
}
