/*
 * the ANSI entry points of the profile functions: arguments go to wide characters through the ANSI code page,
 * the work is the wide functions', and the results come back through the same code page. Windows' own versions
 * share one engine with the wide ones, so the results are the same.
 */
#include "kernelbase_private.h"

NTSYSAPI NTSTATUS NTAPI RtlCharToInteger(PCSTR string, ULONG base, PULONG value);

DWORD WINAPI GetPrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR fallback, LPWSTR out, DWORD size,
                                      LPCWSTR filename);
DWORD WINAPI GetPrivateProfileSectionW(LPCWSTR section, LPWSTR out, DWORD size, LPCWSTR filename);
BOOL WINAPI WritePrivateProfileStringW(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR filename);
BOOL WINAPI WritePrivateProfileSectionW(LPCWSTR section, LPCWSTR lines, LPCWSTR filename);
BOOL WINAPI GetPrivateProfileStructW(LPCWSTR section, LPCWSTR key, LPVOID structure, UINT size, LPCWSTR filename);
BOOL WINAPI WritePrivateProfileStructW(LPCWSTR section, LPCWSTR key, LPVOID structure, UINT size, LPCWSTR filename);

typedef struct {
    PWSTR Section, Key, Fallback, Filename;
} WIDE_ARGUMENTS;

static void free_arguments(WIDE_ARGUMENTS *w)
{
    KbFree(w->Section);
    KbFree(w->Key);
    KbFree(w->Fallback);
    KbFree(w->Filename);
}

/* converts the optional arguments; an argument that is given and cannot be converted fails with its error. */
static BOOL convert_arguments(WIDE_ARGUMENTS *w, LPCSTR section, LPCSTR key, LPCSTR fallback, LPCSTR filename)
{
    memset(w, 0, sizeof(*w));
    if ((section && !(w->Section = KbAnsiToWideCp(CP_ACP, section))) || (key && !(w->Key = KbAnsiToWideCp(CP_ACP, key))) ||
        (fallback && !(w->Fallback = KbAnsiToWideCp(CP_ACP, fallback))) ||
        (filename && !(w->Filename = KbAnsiToWideCp(CP_ACP, filename)))) {
        DWORD error = GetLastError();
        free_arguments(w);
        KbSetError(error ? error : ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return TRUE;
}

/* the length in bytes up to a limit that does not cut a double byte character in two. */
static DWORD whole_characters(const char *text, DWORD length)
{
    DWORD at = 0;
    while (at < length) {
        DWORD next = at + 1 + (IsDBCSLeadByte((BYTE)text[at]) && at + 1 < length);
        if (next > length)
            break;
        at = next;
    }
    return at;
}

DWORD WINAPI GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR fallback, LPSTR out, DWORD size,
                                      LPCSTR filename)
{
    if (!size)
        return 0;
    if (!out) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    WIDE_ARGUMENTS w;
    if (!convert_arguments(&w, section, key, fallback, filename))
        return 0;
    PWSTR wide = KbAlloc((SIZE_T)size * sizeof(WCHAR));
    if (!wide) {
        free_arguments(&w);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    BOOL list = !section || !key;
    DWORD chars = GetPrivateProfileStringW(w.Section, w.Key, w.Fallback, wide, size, w.Filename);
    DWORD error = GetLastError();
    /* a list ends with an empty string, which counts in the conversion. */
    int bytes = WideCharToMultiByte(CP_ACP, 0, wide, chars + (list ? 2 : 1), out, size, nullptr, nullptr);
    DWORD length;
    if (bytes > 0) {
        length = bytes - (list ? 2 : 1);
    } else {
        /* too long for the buffer: what fits, cut where a character ends. */
        int needed = WideCharToMultiByte(CP_ACP, 0, wide, chars + (list ? 2 : 1), nullptr, 0, nullptr, nullptr);
        PSTR all = needed > 0 ? KbAlloc(needed) : nullptr;
        if (all) {
            WideCharToMultiByte(CP_ACP, 0, wide, chars + (list ? 2 : 1), all, needed, nullptr, nullptr);
            length = whole_characters(all, list ? (size > 2 ? size - 2 : 0) : size - 1);
            memcpy(out, all, length);
            out[length] = 0;
            if (list && length + 1 < size)
                out[length + 1] = 0;
            KbFree(all);
            error = ERROR_MORE_DATA;
        } else {
            length = 0;
            out[0] = 0;
        }
    }
    KbFree(wide);
    free_arguments(&w);
    KbSetError(error);
    return length;
}

DWORD WINAPI GetPrivateProfileSectionNamesA(LPSTR out, DWORD size, LPCSTR filename)
{
    return GetPrivateProfileStringA(nullptr, nullptr, nullptr, out, size, filename);
}

UINT WINAPI GetPrivateProfileIntA(LPCSTR section, LPCSTR key, INT fallback, LPCSTR filename)
{
    CHAR text[256];
    if (GetPrivateProfileStringA(section, key, nullptr, text, sizeof(text), filename) == 0)
        return (UINT)fallback;
    ULONG value = 0;
    RtlCharToInteger(text, 0, &value);
    return value;
}

BOOL WINAPI WritePrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR filename)
{
    WIDE_ARGUMENTS w;
    if (!convert_arguments(&w, section, key, value, filename))
        return FALSE;
    BOOL result = WritePrivateProfileStringW(w.Section, w.Key, w.Fallback, w.Filename);
    DWORD error = GetLastError();
    free_arguments(&w);
    KbSetError(error);
    return result;
}

DWORD WINAPI GetPrivateProfileSectionA(LPCSTR section, LPSTR out, DWORD size, LPCSTR filename)
{
    if (!section || !out) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!size)
        return 0;
    WIDE_ARGUMENTS w;
    if (!convert_arguments(&w, section, nullptr, nullptr, filename))
        return 0;
    PWSTR wide = KbAlloc((SIZE_T)size * sizeof(WCHAR));
    if (!wide) {
        free_arguments(&w);
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    DWORD chars = GetPrivateProfileSectionW(w.Section, wide, size, w.Filename);
    DWORD error = GetLastError();
    DWORD length = 0;
    int bytes = WideCharToMultiByte(CP_ACP, 0, wide, chars + 2, out, size, nullptr, nullptr);
    if (bytes > 0) {
        length = bytes - 2;
    } else {
        int needed = WideCharToMultiByte(CP_ACP, 0, wide, chars + 2, nullptr, 0, nullptr, nullptr);
        PSTR all = needed > 0 ? KbAlloc(needed) : nullptr;
        if (all && size >= 2) {
            WideCharToMultiByte(CP_ACP, 0, wide, chars + 2, all, needed, nullptr, nullptr);
            length = whole_characters(all, size - 2);
            memcpy(out, all, length);
            out[length] = out[length + 1] = 0;
        } else if (size) {
            out[0] = 0;
        }
        KbFree(all);
    }
    KbFree(wide);
    free_arguments(&w);
    KbSetError(error);
    return length;
}

/* a list of ANSI strings ended by an empty one, as wide characters. */
static PWSTR list_to_wide(LPCSTR lines)
{
    SIZE_T length = 0;
    while (lines[length])
        length += strlen(lines + length) + 1;
    length++;
    int wide = MultiByteToWideChar(CP_ACP, 0, lines, (int)length, nullptr, 0);
    PWSTR out = wide > 0 ? KbAlloc((SIZE_T)wide * sizeof(WCHAR)) : nullptr;
    if (out)
        MultiByteToWideChar(CP_ACP, 0, lines, (int)length, out, wide);
    return out;
}

BOOL WINAPI WritePrivateProfileSectionA(LPCSTR section, LPCSTR lines, LPCSTR filename)
{
    WIDE_ARGUMENTS w;
    if (!convert_arguments(&w, section, nullptr, nullptr, filename))
        return FALSE;
    PWSTR wide = lines ? list_to_wide(lines) : nullptr;
    BOOL result = FALSE;
    if (lines && !wide) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
    } else {
        result = WritePrivateProfileSectionW(w.Section, wide, w.Filename);
    }
    DWORD error = GetLastError();
    KbFree(wide);
    free_arguments(&w);
    KbSetError(error);
    return result;
}

BOOL WINAPI GetPrivateProfileStructA(LPCSTR section, LPCSTR key, LPVOID structure, UINT size, LPCSTR filename)
{
    WIDE_ARGUMENTS w;
    if (!convert_arguments(&w, section, key, nullptr, filename))
        return FALSE;
    BOOL result = GetPrivateProfileStructW(w.Section, w.Key, structure, size, w.Filename);
    DWORD error = GetLastError();
    free_arguments(&w);
    KbSetError(error);
    return result;
}

BOOL WINAPI WritePrivateProfileStructA(LPCSTR section, LPCSTR key, LPVOID structure, UINT size, LPCSTR filename)
{
    WIDE_ARGUMENTS w;
    if (!convert_arguments(&w, section, key, nullptr, filename))
        return FALSE;
    BOOL result = WritePrivateProfileStructW(w.Section, w.Key, structure, size, w.Filename);
    DWORD error = GetLastError();
    free_arguments(&w);
    KbSetError(error);
    return result;
}

DWORD WINAPI GetProfileStringA(LPCSTR section, LPCSTR key, LPCSTR fallback, LPSTR out, DWORD size)
{
    return GetPrivateProfileStringA(section, key, fallback, out, size, nullptr);
}

UINT WINAPI GetProfileIntA(LPCSTR section, LPCSTR key, INT fallback)
{
    return GetPrivateProfileIntA(section, key, fallback, nullptr);
}

BOOL WINAPI WriteProfileStringA(LPCSTR section, LPCSTR key, LPCSTR value)
{
    return WritePrivateProfileStringA(section, key, value, nullptr);
}

DWORD WINAPI GetProfileSectionA(LPCSTR section, LPSTR out, DWORD size)
{
    return GetPrivateProfileSectionA(section, out, size, nullptr);
}

BOOL WINAPI WriteProfileSectionA(LPCSTR section, LPCSTR lines)
{
    return WritePrivateProfileSectionA(section, lines, nullptr);
}
