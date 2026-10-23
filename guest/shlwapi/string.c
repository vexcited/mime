/*
 * shlwapi's own string functions: byte sizes, time intervals, dates, STRRET, bounded printf, code page
 * conversions and guids.
 */
#include "shlwapi_private.h"

LPWSTR WINAPI StrCatW(LPWSTR dest, LPCWSTR src)
{
    if (dest && src)
        wcscat(dest, src);
    return dest;
}

LPWSTR WINAPI StrCpyW(LPWSTR dest, LPCWSTR src)
{
    if (dest && src)
        wcscpy(dest, src);
    return dest;
}

/* count includes the terminator, like lstrcpyn's. */
LPWSTR WINAPI StrNCatW(LPWSTR dest, LPCWSTR src, int count)
{
    if (!dest || !src || count <= 0)
        return dest;
    lstrcpynW(dest + wcslen(dest), src, count);
    return dest;
}

LPSTR WINAPI StrNCatA(LPSTR dest, LPCSTR src, int count)
{
    if (!dest || !src || count <= 0)
        return dest;
    lstrcpynA(dest + strlen(dest), src, count);
    return dest;
}

/* ------------------------------------------------------------------ printf */

/* -1 when the text does not fit, though the output is still terminated. */
int WINAPI wvnsprintfW(LPWSTR out, int size, LPCWSTR format, va_list args)
{
    if (!out || size <= 0)
        return -1;
    if (!format) {
        *out = 0;
        return -1;
    }
    int n = _vsnwprintf(out, (size_t)size - 1, format, args);
    if (n < 0 || n >= size) {
        out[size - 1] = 0;
        return -1;
    }
    out[n] = 0;
    return n;
}

int WINAPI wvnsprintfA(LPSTR out, int size, LPCSTR format, va_list args)
{
    if (!out || size <= 0)
        return -1;
    if (!format) {
        *out = 0;
        return -1;
    }
    int n = _vsnprintf(out, (size_t)size - 1, format, args);
    if (n < 0 || n >= size) {
        out[size - 1] = 0;
        return -1;
    }
    out[n] = 0;
    return n;
}

int WINAPIV wnsprintfW(LPWSTR out, int size, LPCWSTR format, ...)
{
    va_list args;
    va_start(args, format);
    int n = wvnsprintfW(out, size, format, args);
    va_end(args);
    return n;
}

int WINAPIV wnsprintfA(LPSTR out, int size, LPCSTR format, ...)
{
    va_list args;
    va_start(args, format);
    int n = wvnsprintfA(out, size, format, args);
    va_end(args);
    return n;
}

/* ------------------------------------------------------------------ code pages */

/* returns characters written including the terminator. a too small buffer is truncated but terminated. */
int WINAPI SHAnsiToUnicodeCP(UINT code_page, LPCSTR source, LPWSTR out, int size)
{
    if (!out || size <= 0)
        return 0;
    *out = 0;
    if (!source)
        return 0;
    int n = MultiByteToWideChar(code_page, 0, source, -1, out, size);
    if (!n) {
        int needed = MultiByteToWideChar(code_page, 0, source, -1, nullptr, 0);
        if (needed <= 0)
            return 0;
        LPWSTR all = ShlAlloc((SIZE_T)needed * sizeof(WCHAR));
        if (!all)
            return 0;
        MultiByteToWideChar(code_page, 0, source, -1, all, needed);
        memcpy(out, all, ((SIZE_T)size - 1) * sizeof(WCHAR));
        out[size - 1] = 0;
        ShlFree(all);
        return size;
    }
    return n;
}

/* returns bytes written including the terminator. truncation never splits a double byte character. */
int WINAPI SHUnicodeToAnsiCP(UINT code_page, LPCWSTR source, LPSTR out, int size)
{
    if (!out || size <= 0)
        return 0;
    *out = 0;
    if (!source)
        return 0;
    int n = WideCharToMultiByte(code_page, 0, source, -1, out, size, nullptr, nullptr);
    if (n)
        return n;
    int needed = WideCharToMultiByte(code_page, 0, source, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return 0;
    LPSTR all = ShlAlloc((SIZE_T)needed);
    if (!all)
        return 0;
    WideCharToMultiByte(code_page, 0, source, -1, all, needed, nullptr, nullptr);
    int used = 0;
    while (used < size - 1) {
        int step = IsDBCSLeadByteEx(code_page, (BYTE)all[used]) ? 2 : 1;
        if (used + step > size - 1)
            break;
        used += step;
    }
    memcpy(out, all, (SIZE_T)used);
    out[used] = 0;
    ShlFree(all);
    return used + 1;
}

HRESULT WINAPI SHUnicodeToAnsiCPAlloc(UINT code_page, LPCWSTR source, LPSTR *out);
HRESULT WINAPI SHUnicodeToAnsiCPAlloc(UINT code_page, LPCWSTR source, LPSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!source)
        return E_INVALIDARG;
    int n = WideCharToMultiByte(code_page, 0, source, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return HRESULT_FROM_WIN32(GetLastError());
    *out = LocalAlloc(LMEM_FIXED, (SIZE_T)n);
    if (!*out)
        return E_OUTOFMEMORY;
    WideCharToMultiByte(code_page, 0, source, -1, *out, n, nullptr, nullptr);
    return S_OK;
}

HRESULT WINAPI SHAnsiToUnicodeCPAlloc(UINT code_page, LPCSTR source, LPWSTR *out);
HRESULT WINAPI SHAnsiToUnicodeCPAlloc(UINT code_page, LPCSTR source, LPWSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!source)
        return E_INVALIDARG;
    int n = MultiByteToWideChar(code_page, 0, source, -1, nullptr, 0);
    if (n <= 0)
        return HRESULT_FROM_WIN32(GetLastError());
    *out = LocalAlloc(LMEM_FIXED, (SIZE_T)n * sizeof(WCHAR));
    if (!*out)
        return E_OUTOFMEMORY;
    MultiByteToWideChar(code_page, 0, source, -1, *out, n);
    return S_OK;
}

/* the caller frees the copy with CoTaskMemFree. */
HRESULT WINAPI DupWideToAnsi(LPCWSTR source, LPSTR *out);
HRESULT WINAPI DupWideToAnsi(LPCWSTR source, LPSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!source)
        return E_INVALIDARG;
    int n = WideCharToMultiByte(CP_ACP, 0, source, -1, nullptr, 0, nullptr, nullptr);
    *out = ShlTaskAlloc((SIZE_T)(n > 0 ? n : 1));
    if (!*out)
        return E_OUTOFMEMORY;
    WideCharToMultiByte(CP_ACP, 0, source, -1, *out, n, nullptr, nullptr);
    return S_OK;
}

/* whether the string survives the trip to the other character set and back. */
BOOL WINAPI DoesStringRoundTripW(LPCWSTR source, LPSTR out, UINT size);
BOOL WINAPI DoesStringRoundTripW(LPCWSTR source, LPSTR out, UINT size)
{
    if (!source || !out || !size)
        return FALSE;
    SHUnicodeToAnsiCP(CP_ACP, source, out, (int)size);
    SIZE_T length = wcslen(source) + 1;
    LPWSTR back = ShlAlloc(length * sizeof(WCHAR));
    if (!back)
        return FALSE;
    SHAnsiToUnicodeCP(CP_ACP, out, back, (int)length);
    BOOL same = !wcscmp(source, back);
    ShlFree(back);
    return same;
}

BOOL WINAPI DoesStringRoundTripA(LPCSTR source, LPWSTR out, UINT size);
BOOL WINAPI DoesStringRoundTripA(LPCSTR source, LPWSTR out, UINT size)
{
    if (!source || !out || !size)
        return FALSE;
    SHAnsiToUnicodeCP(CP_ACP, source, out, (int)size);
    SIZE_T length = strlen(source) + 1;
    LPSTR back = ShlAlloc(length * 2);
    if (!back)
        return FALSE;
    SHUnicodeToAnsiCP(CP_ACP, out, back, (int)length * 2);
    BOOL same = !strcmp(source, back);
    ShlFree(back);
    return same;
}

BOOL WINAPI GetStringType3ExW(LPCWSTR text, int count, WORD *types);
BOOL WINAPI GetStringType3ExW(LPCWSTR text, int count, WORD *types)
{
    return GetStringTypeW(CT_CTYPE3, text, count, types);
}

LPSTR WINAPI CharUpperNoDBCSA(LPSTR text);
LPSTR WINAPI CharUpperNoDBCSA(LPSTR text)
{
    for (LPSTR p = text; p && *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p -= 32;
    return text;
}

LPSTR WINAPI CharLowerNoDBCSA(LPSTR text);
LPSTR WINAPI CharLowerNoDBCSA(LPSTR text)
{
    for (LPSTR p = text; p && *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p += 32;
    return text;
}

LPWSTR WINAPI CharUpperNoDBCSW(LPWSTR text);
LPWSTR WINAPI CharUpperNoDBCSW(LPWSTR text)
{
    return CharUpperW(text);
}

LPWSTR WINAPI CharLowerNoDBCSW(LPWSTR text);
LPWSTR WINAPI CharLowerNoDBCSW(LPWSTR text)
{
    return CharLowerW(text);
}

/* ------------------------------------------------------------------ mnemonics */

/* the first "&x" loses its ampersand and x is returned, "&&" stays a literal ampersand. */
WCHAR WINAPI SHStripMneumonicW(LPWSTR text)
{
    if (!text)
        return 0;
    for (LPWSTR p = text; *p; p++) {
        if (*p != L'&')
            continue;
        if (p[1] == L'&') {
            p++;
            continue;
        }
        WCHAR mnemonic = p[1];
        memmove(p, p + 1, (wcslen(p + 1) + 1) * sizeof(WCHAR));
        return mnemonic;
    }
    return 0;
}

CHAR WINAPI SHStripMneumonicA(LPSTR text)
{
    if (!text)
        return 0;
    for (LPSTR p = text; *p; p++) {
        if (*p != '&')
            continue;
        if (p[1] == '&') {
            p++;
            continue;
        }
        CHAR mnemonic = p[1];
        memmove(p, p + 1, strlen(p + 1) + 1);
        return mnemonic;
    }
    return 0;
}

/* ------------------------------------------------------------------ guids */

static int hex_digit(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

static BOOL hex_field(LPCWSTR *p, int digits, ULONG *value)
{
    ULONG v = 0;
    for (int i = 0; i < digits; i++) {
        int d = hex_digit((*p)[i]);
        if (d < 0)
            return FALSE;
        v = v << 4 | (ULONG)d;
    }
    *p += digits;
    *value = v;
    return TRUE;
}

/* exactly "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}". */
BOOL WINAPI GUIDFromStringW(LPCWSTR text, GUID *guid);
BOOL WINAPI GUIDFromStringW(LPCWSTR text, GUID *guid)
{
    if (!text || !guid || *text++ != L'{')
        return FALSE;
    ULONG v;
    GUID g;
    if (!hex_field(&text, 8, &v) || *text++ != L'-')
        return FALSE;
    g.Data1 = v;
    if (!hex_field(&text, 4, &v) || *text++ != L'-')
        return FALSE;
    g.Data2 = (USHORT)v;
    if (!hex_field(&text, 4, &v) || *text++ != L'-')
        return FALSE;
    g.Data3 = (USHORT)v;
    for (int i = 0; i < 8; i++) {
        if (i == 2 && *text++ != L'-')
            return FALSE;
        if (!hex_field(&text, 2, &v))
            return FALSE;
        g.Data4[i] = (BYTE)v;
    }
    if (*text++ != L'}' || *text)
        return FALSE;
    *guid = g;
    return TRUE;
}

BOOL WINAPI GUIDFromStringA(LPCSTR text, GUID *guid);
BOOL WINAPI GUIDFromStringA(LPCSTR text, GUID *guid)
{
    WCHAR wide[40];
    if (!text || !guid || !MultiByteToWideChar(CP_ACP, 0, text, -1, wide, RTL_NUMBER_OF(wide)))
        return FALSE;
    return GUIDFromStringW(wide, guid);
}

/* returns characters written including the terminator, or 0 when the 39 needed do not fit. */
int WINAPI SHStringFromGUIDW(REFGUID guid, LPWSTR out, int size);
int WINAPI SHStringFromGUIDW(REFGUID guid, LPWSTR out, int size)
{
    WCHAR text[40];
    wsprintfW(text, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", (unsigned long)guid->Data1,
              guid->Data2, guid->Data3, guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
              guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
    if (!out || size < 39)
        return 0;
    memcpy(out, text, 39 * sizeof(WCHAR));
    return 39;
}

int WINAPI SHStringFromGUIDA(REFGUID guid, LPSTR out, int size);
int WINAPI SHStringFromGUIDA(REFGUID guid, LPSTR out, int size)
{
    WCHAR text[40];
    if (!out || size < 39 || !SHStringFromGUIDW(guid, text, RTL_NUMBER_OF(text)))
        return 0;
    for (int i = 0; i < 39; i++)
        out[i] = (char)text[i];
    return 39;
}

/* ------------------------------------------------------------------ byte sizes */

static const PCWSTR size_units[] = {L" KB", L" MB", L" GB", L" TB", L" PB", L" EB"};

/* formats with the user's grouping and decimal separator. */
static BOOL group_number(LPCWSTR digits, UINT decimals, LPWSTR out, int size)
{
    WCHAR decimal[8], thousand[8], grouping[16];
    if (!GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, decimal, RTL_NUMBER_OF(decimal)))
        wcscpy(decimal, L".");
    if (!GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, thousand, RTL_NUMBER_OF(thousand)))
        wcscpy(thousand, L",");
    UINT group = 3;
    if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SGROUPING, grouping, RTL_NUMBER_OF(grouping)) &&
        grouping[0] >= L'0' && grouping[0] <= L'9')
        group = grouping[0] - L'0';
    NUMBERFMTW format = {decimals, 1, group, decimal, thousand, 1};
    return GetNumberFormatW(LOCALE_USER_DEFAULT, 0, digits, &format, out, size) != 0;
}

/* bytes below 1024 as is, else the largest unit that keeps the whole part below 1000, with three
   significant digits, truncated or rounded. */
static HRESULT format_byte_size(ULONGLONG bytes, BOOL round, LPWSTR out, UINT size)
{
    WCHAR number[64], text[96];
    if (bytes < 1024) {
        wsprintfW(text, L"%u bytes", (unsigned)bytes);
    } else {
        int unit = 1;
        while (unit < 6 && (bytes >> (10 * unit)) >= 1000)
            unit++;
        int shift = 10 * unit;
        ULONGLONG whole = bytes >> shift, rest = bytes & ((1ull << shift) - 1);
        WCHAR digits[32];
        int n = 0;
        ULONGLONG w = whole;
        do {
            digits[n++] = (WCHAR)(L'0' + w % 10);
            w /= 10;
        } while (w);
        for (int i = 0; i < n / 2; i++) {
            WCHAR t = digits[i];
            digits[i] = digits[n - 1 - i];
            digits[n - 1 - i] = t;
        }
        int decimals = n == 1 ? 2 : n == 2 ? 1 : 0;
        WCHAR fraction[4];
        for (int i = 0; i <= decimals; i++) {
            rest *= 10;
            fraction[i] = (WCHAR)(L'0' + (rest >> shift));
            rest &= (1ull << shift) - 1;
        }
        int length = n;
        if (decimals) {
            digits[length++] = L'.';
            for (int i = 0; i < decimals; i++)
                digits[length++] = fraction[i];
        }
        digits[length] = 0;
        if (round && fraction[decimals] >= L'5') {
            int i = length - 1;
            for (; i >= 0; i--) {
                if (digits[i] == L'.')
                    continue;
                if (digits[i] != L'9') {
                    digits[i]++;
                    break;
                }
                digits[i] = L'0';
            }
            if (i < 0) {
                memmove(digits + 1, digits, ((SIZE_T)length + 1) * sizeof(WCHAR));
                digits[0] = L'1';
            }
        }
        if (!group_number(digits, (UINT)decimals, number, RTL_NUMBER_OF(number)))
            return HRESULT_FROM_WIN32(GetLastError());
        wsprintfW(text, L"%s%s", number, size_units[unit - 1]);
    }
    SIZE_T length = wcslen(text);
    if (length >= size)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    memcpy(out, text, (length + 1) * sizeof(WCHAR));
    return S_OK;
}

/* flags must be exactly one of SFBS_FLAGS_ROUND_TO_NEAREST_DISPLAYED_DIGIT and
   SFBS_FLAGS_TRUNCATE_UNDISPLAYED_DECIMAL_DIGITS. */
HRESULT WINAPI StrFormatByteSizeEx(ULONGLONG size, SFBS_FLAGS flags, LPWSTR out, UINT length)
{
    if (!out || !length)
        return E_INVALIDARG;
    *out = 0;
    if (flags != 1 && flags != 2)
        return E_INVALIDARG;
    HRESULT hr = format_byte_size(size, flags == 1, out, length);
    if (FAILED(hr))
        *out = 0;
    return hr;
}

LPWSTR WINAPI StrFormatByteSizeW(LONGLONG size, LPWSTR out, UINT length)
{
    return SUCCEEDED(StrFormatByteSizeEx((ULONGLONG)size, 2, out, length)) ? out : nullptr;
}

LPSTR WINAPI StrFormatByteSize64A(LONGLONG size, LPSTR out, UINT length)
{
    WCHAR wide[64];
    if (!out || !length || !StrFormatByteSizeW(size, wide, RTL_NUMBER_OF(wide)))
        return nullptr;
    SHUnicodeToAnsiCP(CP_ACP, wide, out, (int)length);
    return out;
}

LPSTR WINAPI StrFormatByteSizeA(DWORD size, LPSTR out, UINT length)
{
    return StrFormatByteSize64A(size, out, length);
}

/* rounds up to whole kilobytes, and truncates to the buffer rather than failing. */
LPWSTR WINAPI StrFormatKBSizeW(LONGLONG size, LPWSTR out, UINT length)
{
    if (!out || !length)
        return out;
    ULONGLONG kb = ((ULONGLONG)size + 1023) / 1024;
    WCHAR digits[32], number[64];
    int n = 0;
    WCHAR reverse[32];
    do {
        reverse[n++] = (WCHAR)(L'0' + kb % 10);
        kb /= 10;
    } while (kb);
    for (int i = 0; i < n; i++)
        digits[i] = reverse[n - 1 - i];
    digits[n] = 0;
    if (!group_number(digits, 0, number, RTL_NUMBER_OF(number)))
        wcscpy(number, digits);
    wcscat(number, L" KB");
    lstrcpynW(out, number, (int)length);
    return out;
}

LPSTR WINAPI StrFormatKBSizeA(LONGLONG size, LPSTR out, UINT length)
{
    WCHAR wide[64];
    if (!out || !length)
        return out;
    StrFormatKBSizeW(size, wide, RTL_NUMBER_OF(wide));
    SHUnicodeToAnsiCP(CP_ACP, wide, out, (int)length);
    return out;
}

/* ------------------------------------------------------------------ time intervals */

/* keeps as many leading digits as the budget has left, zeroing the rest. */
static DWORD significant(DWORD value, int *budget)
{
    int digits = 0;
    for (DWORD v = value; v; v /= 10)
        digits++;
    if (digits <= *budget) {
        *budget -= digits;
        return value;
    }
    DWORD scale = 1;
    for (int i = 0; i < digits - *budget; i++)
        scale *= 10;
    *budget = 0;
    return value / scale * scale;
}

/* " 1 hr 2 min 3 sec" to digits significant digits, leaving out zero parts. */
int WINAPI StrFromTimeIntervalW(LPWSTR out, UINT length, DWORD milliseconds, int digits)
{
    DWORD seconds = (DWORD)(((ULONGLONG)milliseconds + 500) / 1000);
    int budget = digits > 0 ? digits : 0;
    DWORD hours = significant(seconds / 3600, &budget);
    DWORD minutes = significant(seconds / 60 % 60, &budget);
    DWORD secs = significant(seconds % 60, &budget);
    WCHAR text[64];
    int n = 0;
    if (hours)
        n += wsprintfW(text + n, L" %u hr", hours);
    if (minutes)
        n += wsprintfW(text + n, L" %u min", minutes);
    if (secs || !n)
        n += wsprintfW(text + n, L" %u sec", secs);
    if (!out)
        return n;
    if (!length)
        return 0;
    lstrcpynW(out, text, (int)length);
    return (int)wcslen(out);
}

int WINAPI StrFromTimeIntervalA(LPSTR out, UINT length, DWORD milliseconds, int digits)
{
    WCHAR wide[64];
    int n = StrFromTimeIntervalW(wide, RTL_NUMBER_OF(wide), milliseconds, digits);
    if (!out)
        return n;
    if (!length)
        return 0;
    SHUnicodeToAnsiCP(CP_ACP, wide, out, (int)length);
    return (int)strlen(out);
}

/* ------------------------------------------------------------------ dates */

/* formats a UTC file time in local time. a zero time gives 0. */
int WINAPI SHFormatDateTimeW(const FILETIME *time, DWORD *flags, LPWSTR out, UINT size)
{
    if (!out || !size)
        return 0;
    *out = 0;
    if (!time || (!time->dwLowDateTime && !time->dwHighDateTime))
        return 0;
    DWORD f = flags ? *flags : FDTF_DEFAULT;
    FILETIME local;
    SYSTEMTIME st, now;
    if (!FileTimeToLocalFileTime(time, &local) || !FileTimeToSystemTime(&local, &st))
        return 0;
    GetLocalTime(&now);
    WCHAR text[160];
    int n = 0;
    BOOL date = (f & (FDTF_SHORTDATE | FDTF_LONGDATE)) != 0;
    BOOL clock = (f & (FDTF_SHORTTIME | FDTF_LONGTIME)) != 0;
    if (date) {
        BOOL relative = FALSE;
        if (f & FDTF_RELATIVE) {
            FILETIME today_ft, then_ft;
            SYSTEMTIME today = now, then = st;
            today.wHour = today.wMinute = today.wSecond = today.wMilliseconds = 0;
            then.wHour = then.wMinute = then.wSecond = then.wMilliseconds = 0;
            if (SystemTimeToFileTime(&today, &today_ft) && SystemTimeToFileTime(&then, &then_ft)) {
                ULONGLONG a = ((ULONGLONG)today_ft.dwHighDateTime << 32) | today_ft.dwLowDateTime;
                ULONGLONG b = ((ULONGLONG)then_ft.dwHighDateTime << 32) | then_ft.dwLowDateTime;
                if (a == b) {
                    n = wsprintfW(text, L"Today");
                    relative = TRUE;
                } else if (a > b && a - b == 864000000000ull) {
                    n = wsprintfW(text, L"Yesterday");
                    relative = TRUE;
                }
            }
        }
        if (!relative) {
            DWORD date_flags = (f & FDTF_LONGDATE) ? DATE_LONGDATE : DATE_SHORTDATE;
            if (f & FDTF_LTRDATE)
                date_flags |= DATE_LTRREADING;
            else if (f & FDTF_RTLDATE)
                date_flags |= DATE_RTLREADING;
            int length = GetDateFormatW(LOCALE_USER_DEFAULT, date_flags, &st, nullptr, text, RTL_NUMBER_OF(text));
            if (!length)
                return 0;
            n = length - 1;
        }
    }
    if (clock) {
        if (n)
            n += wsprintfW(text + n, (f & FDTF_LONGDATE) ? L", " : L" ");
        int length = GetTimeFormatW(LOCALE_USER_DEFAULT, (f & FDTF_LONGTIME) ? 0 : TIME_NOSECONDS, &st, nullptr,
                                    text + n, (int)RTL_NUMBER_OF(text) - n);
        if (!length)
            return 0;
        n += length - 1;
    }
    text[n] = 0;
    lstrcpynW(out, text, (int)size);
    return (int)wcslen(out);
}

/* the ANSI form returns bytes written including the terminator. */
int WINAPI SHFormatDateTimeA(const FILETIME *time, DWORD *flags, LPSTR out, UINT size)
{
    WCHAR wide[160];
    if (!out || !size)
        return 0;
    *out = 0;
    if (!SHFormatDateTimeW(time, flags, wide, RTL_NUMBER_OF(wide)))
        return 0;
    return SHUnicodeToAnsiCP(CP_ACP, wide, out, (int)size);
}

/* ------------------------------------------------------------------ STRRET */

static LPCSTR strret_offset(const STRRET *strret, const struct _ITEMIDLIST *pidl)
{
    return (LPCSTR)pidl + strret->uOffset;
}

/* a STRRET_WSTR string is handed over, the others are copied into task memory. */
HRESULT WINAPI StrRetToStrW(STRRET *strret, const struct _ITEMIDLIST *pidl, LPWSTR *out)
{
    if (!strret || !out)
        return E_INVALIDARG;
    *out = nullptr;
    LPCSTR ansi;
    switch (strret->uType) {
    case STRRET_WSTR:
        if (!strret->pOleStr)
            return E_FAIL;
        *out = strret->pOleStr;
        strret->pOleStr = nullptr;
        return S_OK;
    case STRRET_CSTR:
        ansi = strret->cStr;
        break;
    case STRRET_OFFSET:
        if (!pidl)
            return E_INVALIDARG;
        ansi = strret_offset(strret, pidl);
        break;
    default:
        return E_FAIL;
    }
    int n = MultiByteToWideChar(CP_ACP, 0, ansi, -1, nullptr, 0);
    *out = ShlTaskAlloc((SIZE_T)(n > 0 ? n : 1) * sizeof(WCHAR));
    if (!*out)
        return E_OUTOFMEMORY;
    if (!MultiByteToWideChar(CP_ACP, 0, ansi, -1, *out, n))
        **out = 0;
    return S_OK;
}

HRESULT WINAPI StrRetToStrA(STRRET *strret, const struct _ITEMIDLIST *pidl, LPSTR *out)
{
    if (!strret || !out)
        return E_INVALIDARG;
    *out = nullptr;
    LPCSTR ansi;
    switch (strret->uType) {
    case STRRET_WSTR: {
        if (!strret->pOleStr)
            return E_FAIL;
        HRESULT hr = DupWideToAnsi(strret->pOleStr, out);
        ShlTaskFree(strret->pOleStr);
        strret->pOleStr = nullptr;
        return hr;
    }
    case STRRET_CSTR:
        ansi = strret->cStr;
        break;
    case STRRET_OFFSET:
        if (!pidl)
            return E_INVALIDARG;
        ansi = strret_offset(strret, pidl);
        break;
    default:
        return E_FAIL;
    }
    SIZE_T n = strlen(ansi) + 1;
    *out = ShlTaskAlloc(n);
    if (!*out)
        return E_OUTOFMEMORY;
    memcpy(*out, ansi, n);
    return S_OK;
}

/* a STRRET_WSTR string is freed after the copy. */
HRESULT WINAPI StrRetToBufW(STRRET *strret, const struct _ITEMIDLIST *pidl, LPWSTR out, UINT size)
{
    if (!strret || !out || !size)
        return E_INVALIDARG;
    *out = 0;
    switch (strret->uType) {
    case STRRET_WSTR:
        if (!strret->pOleStr)
            return E_FAIL;
        lstrcpynW(out, strret->pOleStr, (int)size);
        ShlTaskFree(strret->pOleStr);
        strret->pOleStr = nullptr;
        return S_OK;
    case STRRET_CSTR:
        SHAnsiToUnicodeCP(CP_ACP, strret->cStr, out, (int)size);
        return S_OK;
    case STRRET_OFFSET:
        if (!pidl)
            return E_INVALIDARG;
        SHAnsiToUnicodeCP(CP_ACP, strret_offset(strret, pidl), out, (int)size);
        return S_OK;
    default:
        return E_FAIL;
    }
}

HRESULT WINAPI StrRetToBufA(STRRET *strret, const struct _ITEMIDLIST *pidl, LPSTR out, UINT size)
{
    if (!strret || !out || !size)
        return E_INVALIDARG;
    *out = 0;
    switch (strret->uType) {
    case STRRET_WSTR:
        if (!strret->pOleStr)
            return E_FAIL;
        SHUnicodeToAnsiCP(CP_ACP, strret->pOleStr, out, (int)size);
        ShlTaskFree(strret->pOleStr);
        strret->pOleStr = nullptr;
        return S_OK;
    case STRRET_CSTR:
        lstrcpynA(out, strret->cStr, (int)size);
        return S_OK;
    case STRRET_OFFSET:
        if (!pidl)
            return E_INVALIDARG;
        lstrcpynA(out, strret_offset(strret, pidl), (int)size);
        return S_OK;
    default:
        return E_FAIL;
    }
}

HRESULT WINAPI StrRetToBSTR(STRRET *strret, const struct _ITEMIDLIST *pidl, WCHAR **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    LPWSTR text;
    HRESULT hr = StrRetToStrW(strret, pidl, &text);
    if (FAILED(hr))
        return hr;
    BSTR(WINAPI * alloc)(const OLECHAR *) = (BSTR(WINAPI *)(const OLECHAR *))ShlProc(L"oleaut32.dll",
                                                                                      "SysAllocString");
    *out = alloc ? alloc(text) : nullptr;
    ShlTaskFree(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ strings in the registry */

/* a REG_SZ value, through SHLoadIndirectString when it starts with "@". */
HRESULT WINAPI SHLoadRegUIStringW(HKEY key, LPCWSTR value, LPWSTR out, DWORD size)
{
    if (!out || !size)
        return E_INVALIDARG;
    *out = 0;
    DWORD bytes = size * sizeof(WCHAR), type;
    LONG error = RegQueryValueExW(key, value, nullptr, &type, (BYTE *)out, &bytes);
    if (error)
        return HRESULT_FROM_WIN32(error);
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        *out = 0;
        return E_FAIL;
    }
    out[size - 1] = 0;
    if (out[0] == L'@') {
        SIZE_T length = wcslen(out) + 1;
        LPWSTR source = ShlAlloc(length * sizeof(WCHAR));
        if (!source)
            return E_OUTOFMEMORY;
        memcpy(source, out, length * sizeof(WCHAR));
        HRESULT hr = SHLoadIndirectString(source, out, size, nullptr);
        ShlFree(source);
        return hr;
    }
    return S_OK;
}

HRESULT WINAPI SHLoadRegUIStringA(HKEY key, LPCSTR value, LPSTR out, DWORD size);
HRESULT WINAPI SHLoadRegUIStringA(HKEY key, LPCSTR value, LPSTR out, DWORD size)
{
    if (!out || !size)
        return E_INVALIDARG;
    LPWSTR name = ShlWide(value);
    LPWSTR wide = ShlAlloc((SIZE_T)size * sizeof(WCHAR));
    HRESULT hr = E_OUTOFMEMORY;
    if (wide && (name || !value)) {
        hr = SHLoadRegUIStringW(key, name, wide, size);
        if (SUCCEEDED(hr))
            SHUnicodeToAnsiCP(CP_ACP, wide, out, (int)size);
    }
    ShlFree(wide);
    ShlFree(name);
    return hr;
}
