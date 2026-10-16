/*
 * FormatMessage: message lookup in message tables and insert formatting.
 */
#include "kernelbase_private.h"

#define MAX_INSERTS 99

/* ------------------------------------------------------------------ system messages */

typedef struct BUILTIN_MESSAGE {
    DWORD Id;
    PCWSTR Text;
} BUILTIN_MESSAGE;

/* used when the localized kernelbase.dll.mui with the real message table is missing. */
static const BUILTIN_MESSAGE g_builtin[] = {
    {0, L"The operation completed successfully.\r\n"},
    {1, L"Incorrect function.\r\n"},
    {2, L"The system cannot find the file specified.\r\n"},
    {3, L"The system cannot find the path specified.\r\n"},
    {4, L"The system cannot open the file.\r\n"},
    {5, L"Access is denied.\r\n"},
    {6, L"The handle is invalid.\r\n"},
    {8, L"Not enough memory resources are available to process this command.\r\n"},
    {13, L"The data is invalid.\r\n"},
    {14, L"Not enough memory resources are available to complete this operation.\r\n"},
    {18, L"There are no more files.\r\n"},
    {32, L"The process cannot access the file because it is being used by another process.\r\n"},
    {38, L"Reached the end of the file.\r\n"},
    {50, L"The request is not supported.\r\n"},
    {80, L"The file exists.\r\n"},
    {87, L"The parameter is incorrect.\r\n"},
    {109, L"The pipe has been ended.\r\n"},
    {112, L"There is not enough space on the disk.\r\n"},
    {120, L"This function is not supported on this system.\r\n"},
    {122, L"The data area passed to a system call is too small.\r\n"},
    {123, L"The filename, directory name, or volume label syntax is incorrect.\r\n"},
    {126, L"The specified module could not be found.\r\n"},
    {127, L"The specified procedure could not be found.\r\n"},
    {145, L"The directory is not empty.\r\n"},
    {183, L"Cannot create a file when that file already exists.\r\n"},
    {203, L"The system could not find the environment option that was entered.\r\n"},
    {234, L"More data is available.\r\n"},
    {258, L"The wait operation timed out.\r\n"},
    {259, L"No more data is available.\r\n"},
    {267, L"The directory name is invalid.\r\n"},
    {317, L"The system cannot find message text for message number 0x%1 in the message file for %2.\r\n"},
    {998, L"Invalid access to memory location.\r\n"},
    {1168, L"Element not found.\r\n"},
    {1460, L"This operation returned because the timeout period expired.\r\n"},
};

static HMODULE g_system_messages;
static RTL_RUN_ONCE g_system_messages_once;

static ULONG NTAPI load_system_messages(PRTL_RUN_ONCE once, PVOID parameter, PVOID *context)
{
    WCHAR path[MAX_PATH + 64];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n && n < MAX_PATH) {
        wcscat(path, L"\\en-US\\kernelbase.dll.mui");
        g_system_messages = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    }
    return TRUE;
}

static BOOL find_system_message(DWORD id, DWORD lang, LPWSTR *text, BOOL *allocated)
{
    DWORD saved = GetLastError();
    RtlRunOnceExecuteOnce(&g_system_messages_once, load_system_messages, nullptr, nullptr);
    KbSetError(saved);
    *allocated = FALSE;
    if (g_system_messages) {
        if (KbFindMessage(g_system_messages, id, lang, text, allocated))
            return TRUE;
        if (HRESULT_FACILITY(id) == FACILITY_WIN32 && (id & 0x80000000) &&
            KbFindMessage(g_system_messages, HRESULT_CODE(id), lang, text, allocated))
            return TRUE;
        if (GetLastError() == ERROR_RESOURCE_LANG_NOT_FOUND)
            return FALSE;
    }
    DWORD code = (HRESULT_FACILITY(id) == FACILITY_WIN32 && (id & 0x80000000)) ? HRESULT_CODE(id) : id;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_builtin); i++) {
        if (g_builtin[i].Id == code) {
            *text = (LPWSTR)g_builtin[i].Text;
            return TRUE;
        }
    }
    KbSetError(ERROR_MR_MID_NOT_FOUND);
    return FALSE;
}

/* ------------------------------------------------------------------ output buffer */

typedef struct OUT_BUFFER {
    PWSTR Data;
    SIZE_T Length;
    SIZE_T Capacity;
    BOOL Failed;
} OUT_BUFFER;

static void out_append(OUT_BUFFER *o, PCWSTR text, SIZE_T n)
{
    if (o->Failed)
        return;
    if (o->Length + n + 1 > o->Capacity) {
        SIZE_T capacity = o->Capacity ? o->Capacity * 2 : 256;
        while (capacity < o->Length + n + 1)
            capacity *= 2;
        PWSTR data = RtlReAllocateHeap(KbPeb()->ProcessHeap, 0, o->Data, capacity * sizeof(WCHAR));
        if (!data && !o->Data)
            data = KbAlloc(capacity * sizeof(WCHAR));
        if (!data) {
            o->Failed = TRUE;
            return;
        }
        o->Data = data;
        o->Capacity = capacity;
    }
    memcpy(o->Data + o->Length, text, n * sizeof(WCHAR));
    o->Length += n;
    o->Data[o->Length] = 0;
}

static void out_char(OUT_BUFFER *o, WCHAR c)
{
    out_append(o, &c, 1);
}

/* ------------------------------------------------------------------ insert formatting */

typedef struct INSERT_SPEC {
    WCHAR Format[32]; /* the printf conversion, without '%'. */
    UCHAR Stars;      /* '*' width or precision arguments consumed before the value */
} INSERT_SPEC;

/* parses "%N!fmt!" at p, just after the '%'; returns the characters consumed or 0. */
static SIZE_T parse_insert(PCWSTR p, ULONG *index, INSERT_SPEC *spec)
{
    PCWSTR start = p;
    if (*p < '1' || *p > '9')
        return 0;
    ULONG n = (ULONG)(*p++ - '0');
    if (*p >= '0' && *p <= '9')
        n = n * 10 + (ULONG)(*p++ - '0');
    *index = n;
    wcscpy(spec->Format, L"s");
    spec->Stars = 0;
    if (*p == '!') {
        PCWSTR end = wcschr(p + 1, '!');
        if (end && end - p - 1 < (ptrdiff_t)RTL_NUMBER_OF(spec->Format)) {
            SIZE_T len = (SIZE_T)(end - p - 1);
            memcpy(spec->Format, p + 1, len * sizeof(WCHAR));
            spec->Format[len] = 0;
            for (SIZE_T i = 0; i < len; i++)
                if (spec->Format[i] == '*')
                    spec->Stars++;
            p = end + 1;
        }
    }
    return (SIZE_T)(p - start);
}

/* formats one insert; ansi callers pass narrow strings for %s. */
static void format_insert(OUT_BUFFER *o, const INSERT_SPEC *spec, const ULONG_PTR *args, ULONG first, BOOL ansi)
{
    WCHAR fmt[40];
    WCHAR conv = spec->Format[wcslen(spec->Format) - 1];
    PCWSTR prefix = L"";
    /* s and c follow the caller's character width; S and C are the other one. */
    if (conv == 's' || conv == 'c')
        prefix = ansi ? L"h" : L"l";
    else if (conv == 'S' || conv == 'C')
        prefix = ansi ? L"l" : L"h";
    SIZE_T body = wcslen(spec->Format) - 1;
    WCHAR head[32];
    memcpy(head, spec->Format, body * sizeof(WCHAR));
    head[body] = 0;
    BOOL has_size = wcschr(head, 'l') || wcschr(head, 'h') || wcschr(head, 'I') || wcschr(head, 'w');
    if (conv == 'S' || conv == 'C')
        conv = conv == 'S' ? 's' : 'c';
    KbFormat(fmt, RTL_NUMBER_OF(fmt), L"%%%s%s%c", head, has_size ? L"" : prefix, conv);
    WCHAR buffer[1024];
    int n;
    switch (spec->Stars) {
    case 0:
        n = KbFormat(buffer, RTL_NUMBER_OF(buffer), fmt, args[first]);
        break;
    case 1:
        n = KbFormat(buffer, RTL_NUMBER_OF(buffer), fmt, (int)args[first], args[first + 1]);
        break;
    default:
        n = KbFormat(buffer, RTL_NUMBER_OF(buffer), fmt, (int)args[first], (int)args[first + 1], args[first + 2]);
        break;
    }
    if (n < 0)
        n = (int)wcslen(buffer);
    out_append(o, buffer, (SIZE_T)n);
}

/*
 * expands a message. inserts are resolved by number; when they come from a
 * va_list every argument up to the highest one used is fetched first.
 */
static BOOL format_message(PCWSTR text, DWORD flags, va_list *args, BOOL ansi, OUT_BUFFER *o)
{
    BOOL ignore = (flags & FORMAT_MESSAGE_IGNORE_INSERTS) != 0;
    DWORD width = flags & FORMAT_MESSAGE_MAX_WIDTH_MASK;
    ULONG_PTR fetched[MAX_INSERTS + 2 * MAX_INSERTS];
    const ULONG_PTR *array = nullptr;
    ULONG slot_of[MAX_INSERTS + 1];
    if (!ignore) {
        if (!args) {
            /* inserts without arguments are an error, but plain text is fine. */
            for (PCWSTR p = text; *p; p++) {
                if (*p == '%' && p[1] >= '1' && p[1] <= '9') {
                    KbSetError(ERROR_INVALID_PARAMETER);
                    return FALSE;
                }
                if (*p == '%' && p[1])
                    p++;
            }
        } else if (flags & FORMAT_MESSAGE_ARGUMENT_ARRAY) {
            array = (const ULONG_PTR *)args;
        } else {
            /* scan the message to learn how many slots each insert number uses. */
            UCHAR stars[MAX_INSERTS + 1] = {};
            ULONG highest = 0;
            for (PCWSTR p = text; *p; p++) {
                if (*p != '%')
                    continue;
                ULONG index;
                INSERT_SPEC spec;
                SIZE_T used = parse_insert(p + 1, &index, &spec);
                if (used) {
                    if (index > highest)
                        highest = index;
                    stars[index] = spec.Stars;
                    p += used;
                } else if (p[1]) {
                    p++;
                }
            }
            ULONG pos = 0;
            for (ULONG i = 1; i <= highest; i++) {
                slot_of[i] = pos;
                for (ULONG k = 0; k <= stars[i]; k++)
                    fetched[pos++] = va_arg(*args, ULONG_PTR);
            }
        }
    }
    SIZE_T line_start = o->Length;
    SIZE_T last_space = (SIZE_T)-1;
    for (PCWSTR p = text; *p;) {
        WCHAR c = *p;
        if (c == '%') {
            WCHAR next = p[1];
            if (next >= '1' && next <= '9') {
                ULONG index;
                INSERT_SPEC spec;
                SIZE_T used = parse_insert(p + 1, &index, &spec);
                if (ignore) {
                    out_append(o, p, used + 1);
                } else if (array) {
                    /* argument arrays hold consecutive slots, stars included, starting at insert one. */
                    ULONG first = index - 1;
                    format_insert(o, &spec, array, first, ansi);
                } else {
                    format_insert(o, &spec, fetched, slot_of[index], ansi);
                }
                p += used + 1;
                continue;
            }
            p += 2;
            switch (next) {
            case 0:
                p--;
                break;
            case '0':
                return !o->Failed;
            case 'n':
                out_append(o, L"\r\n", 2);
                line_start = o->Length;
                last_space = (SIZE_T)-1;
                continue;
            case 'r':
                out_char(o, '\r');
                continue;
            case 't':
                out_char(o, '\t');
                continue;
            default:
                if (ignore && next != '%' && next != '.' && next != '!' && next != ' ') {
                    out_char(o, '%');
                    out_char(o, next);
                } else {
                    out_char(o, next);
                }
                continue;
            }
            continue;
        }
        if (width && (c == '\r' || c == '\n')) {
            /* with a width limit, line breaks in the message become spaces. */
            if (c == '\r' && p[1] == '\n')
                p++;
            p++;
            c = ' ';
        } else {
            p++;
        }
        if (width && width != FORMAT_MESSAGE_MAX_WIDTH_MASK) {
            if (c == ' ')
                last_space = o->Length;
            if (o->Length - line_start >= width && last_space != (SIZE_T)-1 && last_space >= line_start) {
                /* wrap at the last space of the line. */
                o->Data[last_space] = '\r';
                PWSTR rest = KbAlloc((o->Length - last_space) * sizeof(WCHAR));
                if (rest) {
                    SIZE_T tail = o->Length - last_space - 1;
                    memcpy(rest, o->Data + last_space + 1, tail * sizeof(WCHAR));
                    o->Length = last_space + 1;
                    out_char(o, '\n');
                    out_append(o, rest, tail);
                    KbFree(rest);
                    line_start = last_space + 2;
                }
                last_space = (SIZE_T)-1;
            }
        }
        out_char(o, c);
    }
    return !o->Failed;
}

/* ------------------------------------------------------------------ public functions */

static BOOL get_message_text(DWORD flags, LPCVOID source, DWORD id, DWORD lang, LPWSTR *text, BOOL *allocated,
                             BOOL ansi_source)
{
    *allocated = FALSE;
    if (flags & FORMAT_MESSAGE_FROM_STRING) {
        if (!source) {
            KbSetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (ansi_source) {
            *text = KbAnsiToWideCp(CP_ACP, source);
            if (!*text)
                return FALSE;
            *allocated = TRUE;
        } else {
            *text = (LPWSTR)source;
        }
        return TRUE;
    }
    if (!(flags & (FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM))) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (flags & FORMAT_MESSAGE_FROM_HMODULE) {
        HMODULE module = source ? (HMODULE)source : (HMODULE)KbPeb()->ImageBaseAddress;
        if (KbFindMessage(module, id, lang, text, allocated))
            return TRUE;
        if (!(flags & FORMAT_MESSAGE_FROM_SYSTEM))
            return FALSE;
    }
    return find_system_message(id, lang, text, allocated);
}

static DWORD deliver_wide(DWORD flags, OUT_BUFFER *o, LPWSTR buffer, DWORD size)
{
    if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
        SIZE_T chars = o->Length + 1 > size ? o->Length + 1 : size;
        LPWSTR result = LocalAlloc(LMEM_FIXED, chars * sizeof(WCHAR));
        if (!result) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        memcpy(result, o->Data ? o->Data : L"", (o->Length + 1) * sizeof(WCHAR));
        *(LPWSTR *)buffer = result;
        return (DWORD)o->Length;
    }
    if (!buffer || o->Length + 1 > size) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(buffer, o->Data ? o->Data : L"", (o->Length + 1) * sizeof(WCHAR));
    return (DWORD)o->Length;
}

DWORD WINAPI FormatMessageW(DWORD flags, LPCVOID source, DWORD id, DWORD lang, LPWSTR buffer, DWORD size,
                            va_list *args)
{
    if (!buffer) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
        *(LPWSTR *)buffer = nullptr;
    LPWSTR text;
    BOOL allocated;
    if (!get_message_text(flags, source, id, lang, &text, &allocated, FALSE))
        return 0;
    OUT_BUFFER o = {};
    BOOL ok = format_message(text, flags, args, FALSE, &o);
    if (allocated)
        KbFree(text);
    DWORD result = 0;
    if (!ok) {
        if (o.Failed)
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
    } else {
        result = deliver_wide(flags, &o, buffer, size);
    }
    KbFree(o.Data);
    return result;
}

DWORD WINAPI FormatMessageA(DWORD flags, LPCVOID source, DWORD id, DWORD lang, LPSTR buffer, DWORD size,
                            va_list *args)
{
    if (!buffer) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER)
        *(LPSTR *)buffer = nullptr;
    LPWSTR text;
    BOOL allocated;
    if (!get_message_text(flags, source, id, lang, &text, &allocated, TRUE))
        return 0;
    OUT_BUFFER o = {};
    BOOL ok = format_message(text, flags, args, TRUE, &o);
    if (allocated)
        KbFree(text);
    DWORD result = 0;
    if (!ok) {
        if (o.Failed)
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        KbFree(o.Data);
        return 0;
    }
    PCWSTR wide = o.Data ? o.Data : L"";
    int need = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
        DWORD bytes = (DWORD)need > size ? (DWORD)need : size;
        LPSTR out = LocalAlloc(LMEM_FIXED, bytes);
        if (!out) {
            KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        } else {
            WideCharToMultiByte(CP_ACP, 0, wide, -1, out, need, nullptr, nullptr);
            *(LPSTR *)buffer = out;
            result = (DWORD)need - 1;
        }
    } else if ((DWORD)need > size) {
        KbSetError(ERROR_INSUFFICIENT_BUFFER);
    } else {
        WideCharToMultiByte(CP_ACP, 0, wide, -1, buffer, (int)size, nullptr, nullptr);
        result = (DWORD)need - 1;
    }
    KbFree(o.Data);
    return result;
}
