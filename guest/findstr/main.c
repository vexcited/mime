/*
 * findstr.exe: prints the lines of files or of the input that match strings.
 *
 *   findstr [/B] [/E] [/L] [/R] [/S] [/I] [/X] [/V] [/N] [/M] [/O] [/P] [/F:file] [/C:string] [/G:file] [/D:dirs]
 *           [/OFF[LINE]] [/A:attr] [/Q:u] strings [file...]
 *
 * the strings are literal with /L and expressions with /R. a string of /C is literal, the others are expressions, as
 * windows has it. the expressions know . * ^ $ [class] [^class] [x-y] \x \< \>. the exit code is 0 when something matched,
 * 1 when nothing did and 2 for a wrong command line.
 */
#include "tool.h"

#define FILE_ATTRIBUTE_OFFLINE_ 0x1000
#define CT_CTYPE1_ 1
#define C1_ALPHA_ 0x100

WINBASEAPI BOOL WINAPI GetStringTypeW(DWORD type, LPCWCH source, int count, LPWORD result);

enum { NODE_LITERAL, NODE_ANY, NODE_CLASS, NODE_WORD_START, NODE_WORD_END };

typedef struct NODE {
    BYTE Type;
    BYTE Star;
    BYTE Char;
    BYTE Set[32];
} NODE;

typedef struct PATTERN {
    NODE *Nodes;
    int Count;
    BOOL AnchorStart;
    BOOL AnchorEnd;
    /* a pattern of plain bytes is searched without the matcher. */
    BOOL Plain;
    BYTE *Bytes;
    SIZE_T Length;
} PATTERN;

typedef struct OPTIONS {
    BOOL Begin, End, Literal, Regex, Recurse, IgnoreCase, Exact, Invert, Numbers, FilesOnly, Offsets, SkipBinary;
    BOOL Offline, QuietUnicode;
    PATTERN *Patterns;
    int PatternCount;
    int PatternCapacity;
    BOOL Prefix;
    BOOL Matched;
    BOOL Failed;
} OPTIONS;

static BYTE g_fold[256];
static BOOL g_word[256];

static void build_tables(void)
{
    UINT cp = GetOEMCP();
    for (int b = 0; b < 256; b++) {
        BYTE in = (BYTE)b;
        WCHAR wide;
        g_fold[b] = (BYTE)b;
        g_word[b] = (b >= '0' && b <= '9') || (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z');
        if (MultiByteToWideChar(cp, 0, (LPCSTR)&in, 1, &wide, 1) != 1)
            continue;
        WCHAR lower = tu_lower(wide);
        BYTE out;
        if (lower != wide && WideCharToMultiByte(cp, 0, &lower, 1, (LPSTR)&out, 1, nullptr, nullptr) == 1)
            g_fold[b] = out;
        if (b >= 128) {
            WORD type = 0;
            if (GetStringTypeW(CT_CTYPE1_, &wide, 1, &type) && (type & C1_ALPHA_))
                g_word[b] = TRUE;
        }
    }
}

static void set_bit(BYTE *set, BYTE c)
{
    set[c >> 3] |= (BYTE)(1 << (c & 7));
}

static BOOL has_bit(const BYTE *set, BYTE c)
{
    return (set[c >> 3] >> (c & 7)) & 1;
}

/* ------------------------------------------------------------------ the expressions */

/* a class holds the other case of its members when the case does not matter. */
static void fold_set(BYTE *set)
{
    BYTE folded[32] = {};
    for (int v = 0; v < 256; v++)
        if (has_bit(set, (BYTE)v))
            set_bit(folded, g_fold[v]);
    for (int w = 0; w < 256; w++)
        if (has_bit(folded, g_fold[w]))
            set_bit(set, (BYTE)w);
}

static void compile(PATTERN *p, const BYTE *text, SIZE_T length, BOOL regex, BOOL ignore_case)
{
    memset(p, 0, sizeof(*p));
    p->Nodes = tu_alloc((length + 1) * sizeof(NODE));
    for (SIZE_T i = 0; i < length;) {
        BYTE c = text[i];
        NODE *n = &p->Nodes[p->Count];
        if (regex && c == '^' && i == 0) {
            p->AnchorStart = TRUE;
            i++;
            continue;
        }
        if (regex && c == '$' && i == length - 1) {
            p->AnchorEnd = TRUE;
            i++;
            continue;
        }
        if (regex && c == '*' && p->Count) {
            NODE *previous = &p->Nodes[p->Count - 1];
            if (previous->Type != NODE_WORD_START && previous->Type != NODE_WORD_END && !previous->Star) {
                previous->Star = 1;
                i++;
                continue;
            }
        }
        if (regex && c == '.') {
            n->Type = NODE_ANY;
            i++;
        } else if (regex && c == '[') {
            SIZE_T j = i + 1;
            BOOL negate = FALSE;
            if (j < length && text[j] == '^') {
                negate = TRUE;
                j++;
            }
            BYTE set[32] = {};
            SIZE_T first = j;
            BOOL closed = FALSE;
            while (j < length) {
                BYTE a = text[j];
                if (a == ']' && j > first) {
                    closed = TRUE;
                    break;
                }
                if (a == '\\' && j + 1 < length)
                    a = text[++j];
                if (j + 2 < length && text[j + 1] == '-' && text[j + 2] != ']') {
                    BYTE b = text[j + 2];
                    int skip = 3;
                    if (b == '\\' && j + 3 < length) {
                        b = text[j + 3];
                        skip = 4;
                    }
                    for (int v = a; v <= b; v++)
                        set_bit(set, (BYTE)v);
                    j += skip;
                } else {
                    set_bit(set, a);
                    j++;
                }
            }
            if (!closed) {
                n->Type = NODE_LITERAL;
                n->Char = ignore_case ? g_fold['['] : '[';
                i++;
            } else {
                if (ignore_case)
                    fold_set(set);
                if (negate)
                    for (int k = 0; k < 32; k++)
                        set[k] = (BYTE)~set[k];
                n->Type = NODE_CLASS;
                memcpy(n->Set, set, 32);
                i = j + 1;
            }
        } else if (regex && c == '\\' && i + 1 < length) {
            BYTE e = text[i + 1];
            if (e == '<') {
                n->Type = NODE_WORD_START;
            } else if (e == '>') {
                n->Type = NODE_WORD_END;
            } else {
                n->Type = NODE_LITERAL;
                n->Char = ignore_case ? g_fold[e] : e;
            }
            i += 2;
        } else {
            n->Type = NODE_LITERAL;
            n->Char = ignore_case ? g_fold[c] : c;
            i++;
        }
        p->Count++;
    }
    p->Plain = TRUE;
    for (int i = 0; i < p->Count; i++)
        if (p->Nodes[i].Type != NODE_LITERAL || p->Nodes[i].Star)
            p->Plain = FALSE;
    if (p->Plain) {
        p->Bytes = tu_alloc(p->Count + 1);
        for (int i = 0; i < p->Count; i++)
            p->Bytes[i] = p->Nodes[i].Char;
        p->Length = (SIZE_T)p->Count;
    }
}

static BOOL node_matches(const NODE *n, BYTE c, BOOL ignore_case)
{
    switch (n->Type) {
    case NODE_ANY: return TRUE;
    case NODE_CLASS: return has_bit(n->Set, c);
    default: return (ignore_case ? g_fold[c] : c) == n->Char;
    }
}

/* the nodes from index ni at position pos; end_required when the match has to reach the end of the line. */
static BOOL match_from(const NODE *nodes, int count, int ni, const BYTE *t, SIZE_T len, SIZE_T pos, BOOL end_required,
                       BOOL ignore_case)
{
    for (; ni < count; ni++) {
        const NODE *n = &nodes[ni];
        if (n->Type == NODE_WORD_START || n->Type == NODE_WORD_END) {
            BOOL before = pos > 0 && g_word[t[pos - 1]];
            BOOL after = pos < len && g_word[t[pos]];
            if (n->Type == NODE_WORD_START ? (before || !after) : (!before || after))
                return FALSE;
            continue;
        }
        if (n->Star) {
            SIZE_T run = 0;
            while (pos + run < len && node_matches(n, t[pos + run], ignore_case))
                run++;
            for (;; run--) {
                if (match_from(nodes, count, ni + 1, t, len, pos + run, end_required, ignore_case))
                    return TRUE;
                if (!run)
                    return FALSE;
            }
        }
        if (pos >= len || !node_matches(n, t[pos], ignore_case))
            return FALSE;
        pos++;
    }
    return !end_required || pos == len;
}

static BOOL same_bytes(const OPTIONS *o, const BYTE *line, const BYTE *pattern, SIZE_T n)
{
    if (o->IgnoreCase) {
        for (SIZE_T i = 0; i < n; i++)
            if (g_fold[line[i]] != pattern[i])
                return FALSE;
        return TRUE;
    }
    return !memcmp(line, pattern, n);
}

static BOOL pattern_matches(const OPTIONS *o, const PATTERN *p, const BYTE *line, SIZE_T length)
{
    BOOL start_only = o->Begin || o->Exact || p->AnchorStart;
    BOOL end_only = o->End || o->Exact || p->AnchorEnd;
    if (p->Plain) {
        if (p->Length > length)
            return FALSE;
        if (start_only && end_only)
            return length == p->Length && same_bytes(o, line, p->Bytes, p->Length);
        if (start_only)
            return same_bytes(o, line, p->Bytes, p->Length);
        if (end_only)
            return same_bytes(o, line + length - p->Length, p->Bytes, p->Length);
        BYTE first = p->Length ? p->Bytes[0] : 0;
        for (SIZE_T i = 0; i + p->Length <= length; i++) {
            if (p->Length && (o->IgnoreCase ? g_fold[line[i]] : line[i]) != first)
                continue;
            if (same_bytes(o, line + i, p->Bytes, p->Length))
                return TRUE;
        }
        return FALSE;
    }
    if (start_only)
        return match_from(p->Nodes, p->Count, 0, line, length, 0, end_only, o->IgnoreCase);
    for (SIZE_T pos = 0; pos <= length; pos++)
        if (match_from(p->Nodes, p->Count, 0, line, length, pos, end_only, o->IgnoreCase))
            return TRUE;
    return FALSE;
}

static BOOL line_matches(const OPTIONS *o, const BYTE *line, SIZE_T length)
{
    for (int i = 0; i < o->PatternCount; i++)
        if (pattern_matches(o, &o->Patterns[i], line, length))
            return TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ the strings */

static void add_pattern(OPTIONS *o, const WCHAR *text, BOOL regex)
{
    int n = WideCharToMultiByte(GetOEMCP(), 0, text, -1, nullptr, 0, nullptr, nullptr);
    BYTE *bytes = tu_alloc(n + 1);
    if (n)
        WideCharToMultiByte(GetOEMCP(), 0, text, -1, (LPSTR)bytes, n, nullptr, nullptr);
    SIZE_T length = n ? (SIZE_T)n - 1 : 0;
    if (o->PatternCount == o->PatternCapacity) {
        o->PatternCapacity = o->PatternCapacity ? o->PatternCapacity * 2 : 8;
        o->Patterns = tu_realloc(o->Patterns, o->PatternCapacity * sizeof(PATTERN));
    }
    compile(&o->Patterns[o->PatternCount++], bytes, length, regex, o->IgnoreCase);
    tu_free(bytes);
}

/* the lines of a text file or of the console, as wide text. */
static WCHAR **read_lines(const WCHAR *path, int *count)
{
    HANDLE h;
    if (tu_eq(path, L"/"))
        h = GetStdHandle(STD_INPUT_HANDLE);
    else
        h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE || !h)
        return nullptr;
    TU_READER reader;
    tu_reader_init(&reader, h);
    WCHAR **lines = nullptr;
    int n = 0, capacity = 0;
    const BYTE *line;
    SIZE_T length;
    while (tu_reader_line(&reader, &line, &length)) {
        WCHAR *wide = tu_alloc((length + 1) * sizeof(WCHAR));
        int converted = length ? MultiByteToWideChar(GetOEMCP(), 0, (LPCSTR)line, (int)length, wide, (int)length) : 0;
        wide[converted] = 0;
        if (n == capacity) {
            capacity = capacity ? capacity * 2 : 16;
            lines = tu_realloc(lines, capacity * sizeof(WCHAR *));
        }
        lines[n++] = wide;
    }
    tu_reader_free(&reader);
    if (!tu_eq(path, L"/"))
        CloseHandle(h);
    *count = n;
    if (!lines)
        lines = tu_alloc(sizeof(WCHAR *));
    return lines;
}

/* ------------------------------------------------------------------ searching */

static BOOL is_binary(const BYTE *data, SIZE_T length)
{
    for (SIZE_T i = 0; i < length; i++)
        if (data[i] < 32 && data[i] != '\t' && data[i] != '\n' && data[i] != '\r' && data[i] != 0x1A && data[i] != '\f')
            return TRUE;
    return FALSE;
}

/* one input; the result is whether a line (or with /M the file) was printed. */
static BOOL search_stream(OPTIONS *o, HANDLE h, const WCHAR *name)
{
    TU_READER reader;
    tu_reader_init(&reader, h);
    const BYTE *line;
    SIZE_T length;
    DWORD number = 0;
    BOOL printed = FALSE;
    while (tu_reader_line(&reader, &line, &length)) {
        number++;
        if (number == 1) {
            UINT64 base = reader.base;
            if (base == 0 && reader.size >= 2 && ((reader.data[0] == 0xFF && reader.data[1] == 0xFE) ||
                                                  (reader.data[0] == 0xFE && reader.data[1] == 0xFF)) &&
                !o->QuietUnicode) {
                if (name)
                    tu_eprint(L"FINDSTR: Warning - input file %s is in Unicode format.\n", name);
                else
                    tu_eprint(L"FINDSTR: Warning - input is in Unicode format.\n");
            }
            if (o->SkipBinary && is_binary(reader.data, reader.size < 4096 ? reader.size : 4096))
                break;
        }
        if (line_matches(o, line, length) == o->Invert)
            continue;
        printed = TRUE;
        if (o->FilesOnly)
            break;
        TU_STR prefix = {};
        if (o->Prefix && name)
            tu_str_format(&prefix, L"%s:", name);
        if (o->Numbers)
            tu_str_format(&prefix, L"%lu:", number);
        if (o->Offsets)
            tu_str_format(&prefix, L"%llu:", reader.base + (UINT64)(line - reader.data));
        if (prefix.length)
            tu_write(TU_STDOUT, prefix.data, prefix.length);
        tu_str_free(&prefix);
        tu_write_bytes(line, length);
        tu_write_bytes((const BYTE *)"\r\n", 2);
    }
    tu_reader_free(&reader);
    if (printed && o->FilesOnly && name)
        tu_print(L"%s\n", name);
    return printed;
}

static void search_file(OPTIONS *o, const WCHAR *path, const WCHAR *shown)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        tu_print(L"FINDSTR: Cannot open %s\n", shown);
        o->Failed = TRUE;
        return;
    }
    if (search_stream(o, h, shown))
        o->Matched = TRUE;
    CloseHandle(h);
}

/* the files of one directory that fit the name pattern, and under it with /S. prefix ends with a separator or is empty. */
static void search_directory(OPTIONS *o, const WCHAR *prefix, const WCHAR *pattern, const WCHAR *display_prefix, int depth,
                             BOOL *found_any)
{
    TU_STR spec = {};
    tu_str_format(&spec, L"%s%s", prefix, pattern);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(tu_str_text(&spec), &data);
    tu_str_free(&spec);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE_) && !o->Offline)
                continue;
            *found_any = TRUE;
            TU_STR path = {}, shown = {};
            tu_str_format(&path, L"%s%s", prefix, data.cFileName);
            tu_str_format(&shown, L"%s%s", display_prefix, data.cFileName);
            search_file(o, tu_str_text(&path), tu_str_text(&shown));
            tu_str_free(&path);
            tu_str_free(&shown);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    if (!o->Recurse || depth > 64)
        return;
    TU_STR all = {};
    tu_str_format(&all, L"%s*", prefix);
    find = FindFirstFileW(tu_str_text(&all), &data);
    tu_str_free(&all);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(data.cFileName, L".") ||
            !wcscmp(data.cFileName, L"..") || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        TU_STR next = {}, next_display = {};
        tu_str_format(&next, L"%s%s\\", prefix, data.cFileName);
        tu_str_format(&next_display, L"%s%s\\", display_prefix, data.cFileName);
        search_directory(o, tu_str_text(&next), pattern, tu_str_text(&next_display), depth + 1, found_any);
        tu_str_free(&next);
        tu_str_free(&next_display);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static void search_spec(OPTIONS *o, const WCHAR *spec, const WCHAR *const *extra_dirs, int extra_count)
{
    SIZE_T dir_length = 0;
    for (SIZE_T k = 0; spec[k]; k++)
        if (spec[k] == L'\\' || spec[k] == L'/' || spec[k] == L':')
            dir_length = k + 1;
    WCHAR *prefix = tu_dup_n(spec, dir_length);
    const WCHAR *pattern = spec + dir_length;
    BOOL found = FALSE;
    if (extra_count) {
        for (int i = 0; i < extra_count; i++) {
            TU_STR dir = {};
            tu_str_puts(&dir, extra_dirs[i]);
            if (dir.length && dir.data[dir.length - 1] != L'\\')
                tu_str_putc(&dir, L'\\');
            tu_str_puts(&dir, prefix);
            search_directory(o, tu_str_text(&dir), pattern, tu_str_text(&dir), 0, &found);
            tu_str_free(&dir);
        }
    } else {
        search_directory(o, prefix, pattern, prefix, 0, &found);
    }
    if (!found && !o->Recurse && !wcschr(spec, L'*') && !wcschr(spec, L'?')) {
        tu_print(L"FINDSTR: Cannot open %s\n", spec);
        o->Failed = TRUE;
    }
    tu_free(prefix);
}

/* ------------------------------------------------------------------ the command line */

static int fail(const WCHAR *format, const WCHAR *argument)
{
    tu_flush();
    tu_eprint(format, argument);
    return 2;
}

static BOOL option_with_value(const WCHAR *a, WCHAR letter, const WCHAR **value)
{
    if (a[0] != L'/' || tu_upper(a[1]) != letter || a[2] != L':')
        return FALSE;
    *value = a + 3;
    return TRUE;
}

static const WCHAR kUsage[] =
    L"Searches for strings in files.\n\n"
    L"FINDSTR [/B] [/E] [/L] [/R] [/S] [/I] [/X] [/V] [/N] [/M] [/O] [/P] [/F:file]\n"
    L"        [/C:string] [/G:file] [/D:dir list] [/A:color attributes] [/OFF[LINE]]\n"
    L"        strings [[drive:][path]filename[ ...]]\n\n"
    L"  /B         Matches pattern if at the beginning of a line.\n"
    L"  /E         Matches pattern if at the end of a line.\n"
    L"  /L         Uses search strings literally.\n"
    L"  /R         Uses search strings as regular expressions.\n"
    L"  /S         Searches for matching files in the current directory and all\n"
    L"             subdirectories.\n"
    L"  /I         Specifies that the search is not to be case-sensitive.\n"
    L"  /X         Prints lines that match exactly.\n"
    L"  /V         Prints only lines that do not contain a match.\n"
    L"  /N         Prints the line number before each line that matches.\n"
    L"  /M         Prints only the filename if a file contains a match.\n"
    L"  /O         Prints character offset before each matching line.\n"
    L"  /P         Skip files with non-printable characters.\n"
    L"  /OFF[LINE] Do not skip files with offline attribute set.\n"
    L"  /A:attr    Specifies color attribute with two hex digits. See \"color /?\"\n"
    L"  /F:file    Reads file list from the specified file(/ stands for console).\n"
    L"  /C:string  Uses specified string as a literal search string.\n"
    L"  /G:file    Gets search strings from the specified file(/ stands for console).\n"
    L"  /D:dir     Search a semicolon delimited list of directories\n"
    L"  /Q:qflags  Quiet mode flags:\n"
    L"             u           Suppress warning about unsupported Unicode formats\n"
    L"  strings    Text to be searched for.\n"
    L"  [drive:][path]filename\n"
    L"             Specifies a file or files to search.\n\n"
    L"Use spaces to separate multiple search strings unless the argument is prefixed\n"
    L"with /C.  For example, 'FINDSTR \"hello there\" x.y' searches for \"hello\" or\n"
    L"\"there\" in file x.y.  'FINDSTR /C:\"hello there\" x.y' searches for\n"
    L"\"hello there\" in file x.y.\n\n"
    L"Regular expression quick reference:\n"
    L"  .        Wildcard: any character\n"
    L"  *        Repeat: zero or more occurrences of previous character or class\n"
    L"  ^        Line position: beginning of line\n"
    L"  $        Line position: end of line\n"
    L"  [class]  Character class: any one character in set\n"
    L"  [^class] Inverse class: any one character not in set\n"
    L"  [x-y]    Range: any characters within the specified range\n"
    L"  \\x       Escape: literal use of metacharacter x\n"
    L"  \\<xyz    Word position: beginning of word\n"
    L"  xyz\\>    Word position: end of word\n\n"
    L"For full information on FINDSTR regular expressions refer to the online Command\n"
    L"Reference.\n";

int tool_main(void)
{
    OPTIONS o = {};
    build_tables();
    const WCHAR *positional_strings = nullptr, *file_list = nullptr, *string_file = nullptr, *dirs = nullptr;
    const WCHAR **specs = tu_alloc((tu_argc + 1) * sizeof(WCHAR *));
    const WCHAR **literal_strings = tu_alloc((tu_argc + 1) * sizeof(WCHAR *));
    int spec_count = 0, literal_count = 0;
    BOOL options_done = FALSE;
    for (int i = 1; i < tu_argc; i++) {
        const WCHAR *a = tu_argv[i], *value;
        if (a[0] == L'/' && a[1] && !options_done) {
            if (!a[2]) {
                WCHAR c = tu_upper(a[1]);
                if (c == L'?') {
                    tu_puts(kUsage);
                    return 0;
                }
                if (c == L'B') o.Begin = TRUE;
                else if (c == L'E') o.End = TRUE;
                else if (c == L'L') o.Literal = TRUE;
                else if (c == L'R') o.Regex = TRUE;
                else if (c == L'S') o.Recurse = TRUE;
                else if (c == L'I') o.IgnoreCase = TRUE;
                else if (c == L'X') o.Exact = TRUE;
                else if (c == L'V') o.Invert = TRUE;
                else if (c == L'N') o.Numbers = TRUE;
                else if (c == L'M') o.FilesOnly = TRUE;
                else if (c == L'O') o.Offsets = TRUE;
                else if (c == L'P') o.SkipBinary = TRUE;
                else return fail(L"FINDSTR: Bad command line\n", nullptr);
            } else if (tu_eq(a + 1, L"OFF") || tu_eq(a + 1, L"OFFLINE")) {
                o.Offline = TRUE;
            } else if (option_with_value(a, L'C', &value)) {
                literal_strings[literal_count++] = value;
            } else if (option_with_value(a, L'F', &value)) {
                file_list = value;
            } else if (option_with_value(a, L'G', &value)) {
                string_file = value;
            } else if (option_with_value(a, L'D', &value)) {
                dirs = value;
            } else if (option_with_value(a, L'A', &value)) {
                /* the color is accepted and not used. */
            } else if (option_with_value(a, L'Q', &value)) {
                if (tu_eq(value, L"u"))
                    o.QuietUnicode = TRUE;
                else
                    return fail(L"FINDSTR: Error - invalid quiet mode flag.\n", nullptr);
            } else {
                return fail(L"FINDSTR: Bad command line\n", nullptr);
            }
            continue;
        }
        if (!positional_strings && !literal_count && !string_file) {
            /* the strings come first when no /C or /G has been seen; one seen later is checked below. */
            positional_strings = a;
        } else {
            specs[spec_count++] = a;
        }
    }
    if (o.Literal && o.Regex)
        return fail(L"Specify only /L or /R.\n", nullptr);
    /* a /C or /G that came after the first word makes that word a file name. */
    if (positional_strings && (literal_count || string_file)) {
        memmove(specs + 1, specs, spec_count * sizeof(WCHAR *));
        specs[0] = positional_strings;
        spec_count++;
        positional_strings = nullptr;
    }

    BOOL regex_default = o.Regex || !o.Literal;
    if (string_file) {
        int count = 0;
        WCHAR **lines = read_lines(string_file, &count);
        if (!lines)
            return fail(L"FINDSTR: Cannot read strings from %s\n", string_file);
        for (int i = 0; i < count; i++) {
            if (*lines[i])
                add_pattern(&o, lines[i], regex_default);
            tu_free(lines[i]);
        }
        tu_free(lines);
    }
    for (int i = 0; i < literal_count; i++)
        add_pattern(&o, literal_strings[i], o.Regex);
    if (positional_strings) {
        WCHAR *copy = tu_dup(positional_strings);
        for (WCHAR *p = copy; *p;) {
            while (*p == L' ' || *p == L'\t')
                p++;
            if (!*p)
                break;
            WCHAR *start = p;
            while (*p && *p != L' ' && *p != L'\t')
                p++;
            if (*p)
                *p++ = 0;
            add_pattern(&o, start, regex_default);
        }
        tu_free(copy);
    }
    if (!o.PatternCount)
        return fail(L"FINDSTR: No search strings\n", nullptr);

    /* the files named in a list come after those on the command line. */
    WCHAR **listed = nullptr;
    int listed_count = 0;
    if (file_list) {
        listed = read_lines(file_list, &listed_count);
        if (!listed)
            return fail(L"FINDSTR: Cannot read file list from %s\n", file_list);
    }
    const WCHAR **extra_dirs = nullptr;
    int extra_count = 0;
    WCHAR *dirs_copy = nullptr;
    if (dirs) {
        dirs_copy = tu_dup(dirs);
        extra_dirs = tu_alloc((wcslen(dirs) + 2) * sizeof(WCHAR *));
        for (WCHAR *d = dirs_copy; d;) {
            WCHAR *semi = wcschr(d, L';');
            if (semi)
                *semi = 0;
            if (*d)
                extra_dirs[extra_count++] = d;
            d = semi ? semi + 1 : nullptr;
        }
    }
    int total_specs = spec_count + listed_count;
    o.Prefix = total_specs > 1 || o.Recurse || extra_count;
    for (int i = 0; i < total_specs && !o.Prefix; i++) {
        const WCHAR *s = i < spec_count ? specs[i] : listed[i - spec_count];
        if (wcschr(s, L'*') || wcschr(s, L'?'))
            o.Prefix = TRUE;
    }

    if (!total_specs) {
        if (search_stream(&o, GetStdHandle(STD_INPUT_HANDLE), nullptr))
            o.Matched = TRUE;
    } else {
        for (int i = 0; i < total_specs; i++) {
            const WCHAR *s = i < spec_count ? specs[i] : listed[i - spec_count];
            WCHAR *trimmed = tu_dup(s);
            SIZE_T n = wcslen(trimmed);
            while (n && (trimmed[n - 1] == L' ' || trimmed[n - 1] == L'\t'))
                trimmed[--n] = 0;
            if (n >= 2 && trimmed[0] == L'"' && trimmed[n - 1] == L'"') {
                trimmed[n - 1] = 0;
                search_spec(&o, trimmed + 1, extra_dirs, extra_count);
            } else if (n) {
                search_spec(&o, trimmed, extra_dirs, extra_count);
            }
            tu_free(trimmed);
        }
    }
    return o.Matched ? 0 : (o.Failed ? 2 : 1);
}
