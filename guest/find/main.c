/*
 * find.exe: prints the lines of files or of the input that contain a text.
 *
 *   find [/V] [/C] [/N] [/I] [/OFF[LINE]] "string" [file...]
 *
 * the text is in double quotes, and two quotes in a row stand for one. every file gets a header line of hyphens and its
 * name; with /C the header has the count of lines. the exit code is 0 when a line was found, 1 when none was and 2 for
 * a wrong command line or a file that is not there.
 */
#include "tool.h"

#define FILE_ATTRIBUTE_OFFLINE_ 0x1000

typedef struct FIND_OPTIONS {
    BOOL Invert;
    BOOL Count;
    BOOL Numbers;
    BOOL IgnoreCase;
    BOOL Offline;
    BYTE *Needle;
    SIZE_T NeedleLength;
} FIND_OPTIONS;

static BYTE g_fold[256];

static void build_fold(void)
{
    UINT cp = GetOEMCP();
    for (int b = 0; b < 256; b++) {
        BYTE in = (BYTE)b;
        WCHAR wide;
        g_fold[b] = (BYTE)b;
        if (MultiByteToWideChar(cp, 0, (LPCSTR)&in, 1, &wide, 1) != 1)
            continue;
        WCHAR lower = tu_lower(wide);
        BYTE out;
        if (lower != wide && WideCharToMultiByte(cp, 0, &lower, 1, (LPSTR)&out, 1, nullptr, nullptr) == 1)
            g_fold[b] = out;
    }
}

static BOOL contains(const FIND_OPTIONS *o, const BYTE *line, SIZE_T length)
{
    if (!o->NeedleLength)
        return TRUE;
    if (length < o->NeedleLength)
        return FALSE;
    const BYTE *needle = o->Needle;
    SIZE_T last = length - o->NeedleLength;
    for (SIZE_T i = 0; i <= last; i++) {
        SIZE_T k = 0;
        if (o->IgnoreCase)
            while (k < o->NeedleLength && g_fold[line[i + k]] == needle[k])
                k++;
        else
            while (k < o->NeedleLength && line[i + k] == needle[k])
                k++;
        if (k == o->NeedleLength)
            return TRUE;
    }
    return FALSE;
}

/* a file that starts with a byte order mark of UTF-16 is turned into the output's code page. */
static BYTE *read_whole(HANDLE h, SIZE_T *size)
{
    SIZE_T capacity = 65536, length = 0;
    BYTE *data = tu_alloc(capacity);
    for (;;) {
        if (length == capacity) {
            capacity *= 2;
            data = tu_realloc(data, capacity);
        }
        DWORD got = 0;
        if (!ReadFile(h, data + length, (DWORD)(capacity - length > 0x100000 ? 0x100000 : capacity - length), &got, nullptr) || !got)
            break;
        length += got;
    }
    if (length >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        int chars = (int)((length - 2) / 2);
        int bytes = chars ? WideCharToMultiByte(GetOEMCP(), 0, (LPCWSTR)(data + 2), chars, nullptr, 0, nullptr, nullptr) : 0;
        BYTE *converted = tu_alloc(bytes + 1);
        if (bytes)
            WideCharToMultiByte(GetOEMCP(), 0, (LPCWSTR)(data + 2), chars, (LPSTR)converted, bytes, nullptr, nullptr);
        tu_free(data);
        data = converted;
        length = bytes;
    }
    *size = length;
    return data;
}

static DWORD search(const FIND_OPTIONS *o, HANDLE h, const WCHAR *header, BOOL count_header)
{
    SIZE_T size;
    BYTE *data = read_whole(h, &size);
    DWORD matches = 0, number = 0;
    if (header && !o->Count) {
        tu_print(L"\n---------- %s\n", header);
    }
    SIZE_T at = 0;
    while (at < size) {
        SIZE_T end = at;
        while (end < size && data[end] != '\n')
            end++;
        SIZE_T length = end - at;
        if (length && data[at + length - 1] == '\r')
            length--;
        number++;
        BOOL hit = contains(o, data + at, length) != o->Invert;
        if (hit) {
            matches++;
            if (!o->Count) {
                if (o->Numbers) {
                    tu_print(L"[%lu]", number);
                }
                tu_write_bytes(data + at, length);
                tu_write_bytes((const BYTE *)"\r\n", 2);
            }
        }
        at = end + 1;
    }
    if (o->Count) {
        if (count_header && header)
            tu_print(L"\n---------- %s: %lu\n", header, matches);
        else
            tu_print(L"%lu\n", matches);
    }
    tu_free(data);
    return matches;
}

/* the text to find has to be in double quotes on the command line itself. */
static BOOL text_is_quoted(void)
{
    const WCHAR *p = tu_tail;
    for (;;) {
        while (*p == L' ' || *p == L'\t')
            p++;
        if (!*p)
            return FALSE;
        if (*p == L'"')
            return TRUE;
        if (*p != L'/')
            return FALSE;
        while (*p && *p != L' ' && *p != L'\t')
            p++;
    }
}

static int bad_parameter(void)
{
    tu_eprint(L"FIND: Parameter format not correct\n");
    return 2;
}

static BOOL has_wildcards(const WCHAR *text)
{
    return wcschr(text, L'*') || wcschr(text, L'?');
}

/* the file name in capitals, as the header shows it. */
static WCHAR *upper_copy(const WCHAR *text)
{
    WCHAR *copy = tu_dup(text);
    for (WCHAR *p = copy; *p; p++)
        *p = tu_upper(*p);
    return copy;
}

int tool_main(void)
{
    FIND_OPTIONS o = {};
    build_fold();
    const WCHAR *text = nullptr;
    WCHAR **files = tu_alloc((tu_argc + 1) * sizeof(WCHAR *));
    int file_count = 0;
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *a = tu_argv[i];
        if ((a[0] == L'/' || a[0] == L'-') && a[1] && !text) {
            if (tu_eq(a + 1, L"?")) {
                tu_puts(L"Searches for a text string in a file or files.\n\n"
                        L"FIND [/V] [/C] [/N] [/I] [/OFF[LINE]] \"string\" [[drive:][path]filename[ ...]]\n\n"
                        L"  /V         Displays all lines NOT containing the specified string.\n"
                        L"  /C         Displays only the count of lines containing the string.\n"
                        L"  /N         Displays line numbers with the displayed lines.\n"
                        L"  /I         Ignores the case of characters when searching for the string.\n"
                        L"  /OFF[LINE] Do not skip files with offline attribute set.\n"
                        L"  \"string\"   Specifies the text string to find.\n"
                        L"  [drive:][path]filename\n"
                        L"             Specifies a file or files to search.\n\n"
                        L"If a path is not specified, FIND searches the text typed at the prompt\n"
                        L"or piped from another command.\n");
                return 0;
            }
            if (tu_eq(a + 1, L"V"))
                o.Invert = TRUE;
            else if (tu_eq(a + 1, L"C"))
                o.Count = TRUE;
            else if (tu_eq(a + 1, L"N"))
                o.Numbers = TRUE;
            else if (tu_eq(a + 1, L"I"))
                o.IgnoreCase = TRUE;
            else if (tu_eq(a + 1, L"OFF") || tu_eq(a + 1, L"OFFLINE"))
                o.Offline = TRUE;
            else {
                tu_eprint(L"FIND: Invalid switch\n");
                return 2;
            }
        } else if (!text) {
            text = a;
        } else {
            files[file_count++] = (WCHAR *)a;
        }
    }
    if (!text || !text_is_quoted())
        return bad_parameter();
    /* the text as the output's code page has it. */
    {
        int n = WideCharToMultiByte(GetOEMCP(), 0, text, -1, nullptr, 0, nullptr, nullptr);
        o.Needle = tu_alloc(n + 1);
        if (n)
            WideCharToMultiByte(GetOEMCP(), 0, text, -1, (LPSTR)o.Needle, n, nullptr, nullptr);
        o.NeedleLength = n ? n - 1 : 0;
        if (o.IgnoreCase)
            for (SIZE_T i = 0; i < o.NeedleLength; i++)
                o.Needle[i] = g_fold[o.Needle[i]];
    }

    DWORD total = 0;
    BOOL error = FALSE;
    if (!file_count) {
        total = search(&o, GetStdHandle(STD_INPUT_HANDLE), nullptr, FALSE);
    } else {
        for (int i = 0; i < file_count; i++) {
            const WCHAR *spec = files[i];
            WIN32_FIND_DATAW data;
            HANDLE find = FindFirstFileW(spec, &data);
            if (find == INVALID_HANDLE_VALUE) {
                WCHAR *shown = upper_copy(spec);
                tu_print(L"File not found - %s\n", shown);
                tu_free(shown);
                error = TRUE;
                continue;
            }
            /* the directory part of the pattern is kept for the names. */
            SIZE_T dir_length = 0;
            for (SIZE_T k = 0; spec[k]; k++)
                if (spec[k] == L'\\' || spec[k] == L'/' || spec[k] == L':')
                    dir_length = k + 1;
            do {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE_) && !o.Offline)
                    continue;
                TU_STR path = {};
                tu_str_append(&path, spec, dir_length);
                tu_str_puts(&path, data.cFileName);
                HANDLE h = CreateFileW(tu_str_text(&path), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
                if (h == INVALID_HANDLE_VALUE) {
                    WCHAR *shown = upper_copy(tu_str_text(&path));
                    tu_print(L"File not found - %s\n", shown);
                    tu_free(shown);
                    error = TRUE;
                } else {
                    TU_STR name = {};
                    tu_str_append(&name, spec, has_wildcards(spec) ? dir_length : 0);
                    if (has_wildcards(spec))
                        tu_str_puts(&name, data.cFileName);
                    else
                        tu_str_puts(&name, spec);
                    WCHAR *shown = upper_copy(tu_str_text(&name));
                    total += search(&o, h, shown, TRUE);
                    tu_free(shown);
                    tu_str_free(&name);
                    CloseHandle(h);
                }
                tu_str_free(&path);
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
    }
    if (error)
        return 2;
    return total ? 0 : 1;
}
