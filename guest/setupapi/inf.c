/*
 * inf files: the text of an inf is read into sections of lines of fields, with the strings of its Strings section
 * replacing the %name% marks when a field is read, and a program walks them through contexts that name a file, a
 * section and a line. The files appended to a handle are searched after it, in the order they were appended.
 */
#include "setup_private.h"

void *SetupAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void SetupFree(void *memory)
{
    if (memory)
        HeapFree(GetProcessHeap(), 0, memory);
}

PWSTR SetupDuplicate(PCWSTR text)
{
    if (!text)
        return nullptr;
    SIZE_T n = wcslen(text) + 1;
    PWSTR copy = SetupAlloc(n * sizeof(WCHAR));
    if (copy)
        memcpy(copy, text, n * sizeof(WCHAR));
    return copy;
}

PWSTR SetupToWide(PCSTR text)
{
    if (!text)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    PWSTR wide = n ? SetupAlloc((SIZE_T)n * sizeof(WCHAR)) : nullptr;
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, text, -1, wide, n);
    return wide;
}

PSTR SetupToAnsi(PCWSTR text)
{
    if (!text)
        return nullptr;
    int n = WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr, 0, nullptr, nullptr);
    PSTR ansi = n ? SetupAlloc((SIZE_T)n) : nullptr;
    if (ansi)
        WideCharToMultiByte(CP_ACP, 0, text, -1, ansi, n, nullptr, nullptr);
    return ansi;
}

/* ------------------------------------------------------------------ reading the text */

static BOOL grow(void **array, ULONG *capacity, ULONG count, SIZE_T item)
{
    if (count < *capacity)
        return TRUE;
    ULONG wanted = *capacity ? *capacity * 2 : 32;
    void *bigger = SetupAlloc(wanted * item);
    if (!bigger)
        return FALSE;
    if (*array) {
        memcpy(bigger, *array, count * item);
        SetupFree(*array);
    }
    *array = bigger;
    *capacity = wanted;
    return TRUE;
}

static PWSTR piece(PCWSTR start, SIZE_T length)
{
    PWSTR text = SetupAlloc((length + 1) * sizeof(WCHAR));
    if (text) {
        memcpy(text, start, length * sizeof(WCHAR));
        text[length] = 0;
    }
    return text;
}

static BOOL is_space(WCHAR c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* a field of the text between two commas, without the blanks around it, and a pair of quotes made one, as the text says. */
static PWSTR make_field(PCWSTR start, PCWSTR end)
{
    while (start < end && is_space(*start))
        start++;
    while (end > start && is_space(end[-1]))
        end--;
    PWSTR text = SetupAlloc((SIZE_T)(end - start + 1) * sizeof(WCHAR));
    if (!text)
        return nullptr;
    SIZE_T n = 0;
    BOOL quoted = FALSE;
    for (PCWSTR p = start; p < end; p++) {
        if (*p == '"') {
            if (quoted && p + 1 < end && p[1] == '"') {
                text[n++] = '"';
                p++;
            } else {
                quoted = !quoted;
            }
            continue;
        }
        text[n++] = *p;
    }
    text[n] = 0;
    return text;
}

static BOOL add_field(INF_FILE *f, PWSTR text)
{
    if (!text || !grow((void **)&f->Fields, &f->FieldCapacity, f->FieldCount, sizeof(PWSTR)))
        return FALSE;
    f->Fields[f->FieldCount++] = text;
    return TRUE;
}

/* a logical line: the comment gone, the pieces after a backslash at the end of a line joined. */
static BOOL parse_line(INF_FILE *f, PCWSTR line, SIZE_T length, INF_SECTION **section, UINT *error)
{
    while (length && is_space(*line)) {
        line++;
        length--;
    }
    while (length && is_space(line[length - 1]))
        length--;
    if (!length)
        return TRUE;
    if (line[0] == '[') {
        SIZE_T end = 1;
        while (end < length && line[end] != ']')
            end++;
        if (end >= length) {
            *error = ERROR_BAD_SECTION_NAME_LINE;
            return FALSE;
        }
        PWSTR name = piece(line + 1, end - 1);
        if (!name || !grow((void **)&f->Sections, &f->SectionCapacity, f->SectionCount, sizeof(INF_SECTION)))
            return FALSE;
        /* blanks around the name are not part of it. */
        SIZE_T lead = 0, n = wcslen(name);
        while (name[lead] && is_space(name[lead]))
            lead++;
        while (n > lead && is_space(name[n - 1]))
            name[--n] = 0;
        if (lead)
            memmove(name, name + lead, (n - lead + 1) * sizeof(WCHAR));
        INF_SECTION *s = &f->Sections[f->SectionCount++];
        s->Name = name;
        s->FirstLine = f->LineCount;
        s->LineCount = 0;
        *section = s;
        return TRUE;
    }
    if (!*section) {
        *error = ERROR_EXPECTED_SECTION_NAME;
        return FALSE;
    }
    if (!grow((void **)&f->Lines, &f->LineCapacity, f->LineCount, sizeof(INF_LINE)))
        return FALSE;
    /* an equal sign outside the quotes makes what is before it the key, when no comma comes first: the fields of a line
       that has a key after its commas may have an equal sign in them. */
    SIZE_T equal = length;
    BOOL quoted = FALSE;
    for (SIZE_T i = 0; i < length; i++) {
        if (line[i] == '"')
            quoted = !quoted;
        else if (line[i] == ',' && !quoted)
            break;
        else if (line[i] == '=' && !quoted) {
            equal = i;
            break;
        }
    }
    INF_LINE *l = &f->Lines[f->LineCount];
    memset(l, 0, sizeof(*l));
    l->First = f->FieldCount;
    SIZE_T from = 0;
    if (equal < length) {
        l->HasKey = TRUE;
        if (!add_field(f, make_field(line, line + equal)))
            return FALSE;
        from = equal + 1;
    }
    /* what follows is split at the commas that are not inside quotes. */
    quoted = FALSE;
    SIZE_T start = from;
    BOOL any = from < length;
    for (SIZE_T i = from; i <= length; i++) {
        if (i < length && line[i] == '"')
            quoted = !quoted;
        if (i == length || (line[i] == ',' && !quoted)) {
            if (any && !add_field(f, make_field(line + start, line + i)))
                return FALSE;
            start = i + 1;
        }
    }
    l->Count = f->FieldCount - l->First;
    f->LineCount++;
    (*section)->LineCount++;
    return TRUE;
}

/* the comment at the end of a line is cut off: it starts at a semicolon outside the quotes. */
static SIZE_T without_comment(PCWSTR line, SIZE_T length)
{
    BOOL quoted = FALSE;
    for (SIZE_T i = 0; i < length; i++) {
        if (line[i] == '"')
            quoted = !quoted;
        else if (line[i] == ';' && !quoted)
            return i;
    }
    return length;
}

static BOOL parse_text(INF_FILE *f, PCWSTR text, SIZE_T length, UINT *error_line, DWORD *error)
{
    INF_SECTION *section = nullptr;
    WCHAR *joined = nullptr;
    SIZE_T joined_length = 0, joined_capacity = 0;
    UINT number = 0;
    for (SIZE_T at = 0; at < length;) {
        SIZE_T end = at;
        while (end < length && text[end] != '\n')
            end++;
        number++;
        SIZE_T line_length = without_comment(text + at, end - at);
        while (line_length && is_space(text[at + line_length - 1]))
            line_length--;
        BOOL more = line_length && text[at + line_length - 1] == '\\';
        if (more)
            line_length--;
        if (joined_length + line_length + 1 > joined_capacity) {
            SIZE_T wanted = (joined_length + line_length + 1) * 2;
            WCHAR *bigger = SetupAlloc(wanted * sizeof(WCHAR));
            if (!bigger) {
                SetupFree(joined);
                *error = ERROR_NOT_ENOUGH_MEMORY;
                return FALSE;
            }
            if (joined)
                memcpy(bigger, joined, joined_length * sizeof(WCHAR));
            SetupFree(joined);
            joined = bigger;
            joined_capacity = wanted;
        }
        /* a line that goes on starts after the blanks the break leaves. */
        PCWSTR from = text + at;
        SIZE_T n = line_length;
        if (joined_length)
            while (n && is_space(*from)) {
                from++;
                n--;
            }
        memcpy(joined + joined_length, from, n * sizeof(WCHAR));
        joined_length += n;
        if (!more || end >= length) {
            UINT problem = 0;
            if (!parse_line(f, joined, joined_length, &section, &problem)) {
                *error = problem ? problem : ERROR_NOT_ENOUGH_MEMORY;
                if (error_line)
                    *error_line = number;
                SetupFree(joined);
                return FALSE;
            }
            joined_length = 0;
        }
        at = end + 1;
    }
    SetupFree(joined);
    return TRUE;
}

static void free_file(INF_FILE *f)
{
    while (f) {
        INF_FILE *next = f->Next;
        for (ULONG i = 0; i < f->FieldCount; i++)
            SetupFree(f->Fields[i]);
        for (ULONG i = 0; i < f->SectionCount; i++)
            SetupFree(f->Sections[i].Name);
        for (ULONG i = 0; i < f->DirectoryCount; i++)
            SetupFree(f->Directories[i].Path);
        SetupFree(f->Fields);
        SetupFree(f->Sections);
        SetupFree(f->Lines);
        SetupFree(f->Directories);
        SetupFree(f);
        f = next;
    }
}

/* the file as bytes, as the characters they are: a mark of byte order says utf-16 or utf-8, else the ansi code page. */
static PWSTR read_text(PCWSTR path, SIZE_T *length)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(file);
        SetLastError(ERROR_FILE_TOO_LARGE);
        return nullptr;
    }
    BYTE *bytes = SetupAlloc((SIZE_T)size.QuadPart + 4);
    DWORD done = 0;
    BOOL ok = bytes && (size.QuadPart == 0 || ReadFile(file, bytes, (DWORD)size.QuadPart, &done, nullptr));
    CloseHandle(file);
    if (!ok) {
        SetupFree(bytes);
        return nullptr;
    }
    PWSTR text;
    if (done >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        SIZE_T n = (done - 2) / 2;
        text = SetupAlloc((n + 1) * sizeof(WCHAR));
        if (text)
            memcpy(text, bytes + 2, n * sizeof(WCHAR));
        *length = n;
    } else {
        UINT page = CP_ACP;
        BYTE *from = bytes;
        int n = (int)done;
        if (done >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
            page = CP_UTF8;
            from += 3;
            n -= 3;
        }
        int wide = n ? MultiByteToWideChar(page, 0, (PCSTR)from, n, nullptr, 0) : 0;
        text = SetupAlloc(((SIZE_T)wide + 1) * sizeof(WCHAR));
        if (text && wide)
            MultiByteToWideChar(page, 0, (PCSTR)from, n, text, wide);
        *length = (SIZE_T)wide;
    }
    SetupFree(bytes);
    return text;
}

LONG SetupInfFindSection(INF_FILE *file, PCWSTR name)
{
    for (ULONG i = 0; i < file->SectionCount; i++)
        if (!_wcsicmp(file->Sections[i].Name, name))
            return (LONG)i;
    return -1;
}

static PCWSTR value_of(INF_FILE *f, const INF_LINE *l, ULONG index)
{
    ULONG at = l->HasKey ? index : (index ? index - 1 : (ULONG)-1);
    if (at == (ULONG)-1 || at >= l->Count)
        return nullptr;
    return f->Fields[l->First + at];
}

PCWSTR SetupInfRawField(INF_FILE *file, const INF_LINE *line, ULONG index)
{
    return value_of(file, line, index);
}

/* the strings of the file or the files appended to it. */
static PCWSTR string_value(INF_FILE *head, PCWSTR key, SIZE_T length, INF_FILE **owner)
{
    for (INF_FILE *f = head; f; f = f->Next) {
        for (ULONG s = 0; s < f->SectionCount; s++) {
            if (_wcsnicmp(f->Sections[s].Name, L"Strings", 7) || (f->Sections[s].Name[7] && f->Sections[s].Name[7] != '.'))
                continue;
            for (ULONG i = 0; i < f->Sections[s].LineCount; i++) {
                const INF_LINE *l = &f->Lines[f->Sections[s].FirstLine + i];
                if (l->HasKey && l->Count >= 2 && !_wcsnicmp(f->Fields[l->First], key, length) && !f->Fields[l->First][length]) {
                    *owner = f;
                    return f->Fields[l->First + 1];
                }
            }
        }
    }
    return nullptr;
}

PWSTR SetupInfExpand(INF_FILE *file, PCWSTR raw)
{
    if (!raw)
        return nullptr;
    SIZE_T capacity = wcslen(raw) + 1, used = 0;
    PWSTR out = SetupAlloc(capacity * sizeof(WCHAR));
    for (PCWSTR p = raw; out && *p;) {
        PCWSTR add = p;
        SIZE_T add_length = 1, skip = 1;
        if (*p == '%') {
            if (p[1] == '%') {
                add_length = 1;
                skip = 2;
            } else {
                PCWSTR close = wcschr(p + 1, '%');
                if (close && close > p + 1) {
                    INF_FILE *owner;
                    PCWSTR value = string_value(file, p + 1, (SIZE_T)(close - p - 1), &owner);
                    if (value) {
                        /* the value goes in as the string has it, with a doubled percent sign made one. */
                        SIZE_T n = wcslen(value);
                        SIZE_T need = used + n + 1;
                        if (need > capacity) {
                            capacity = need * 2;
                            PWSTR bigger = SetupAlloc(capacity * sizeof(WCHAR));
                            if (!bigger) {
                                SetupFree(out);
                                return nullptr;
                            }
                            memcpy(bigger, out, used * sizeof(WCHAR));
                            SetupFree(out);
                            out = bigger;
                        }
                        for (SIZE_T i = 0; i < n; i++) {
                            out[used++] = value[i];
                            if (value[i] == '%' && i + 1 < n && value[i + 1] == '%')
                                i++;
                        }
                        out[used] = 0;
                        p = close + 1;
                        continue;
                    }
                }
            }
        }
        if (used + add_length + 1 > capacity) {
            capacity = (used + add_length + 1) * 2;
            PWSTR bigger = SetupAlloc(capacity * sizeof(WCHAR));
            if (!bigger) {
                SetupFree(out);
                return nullptr;
            }
            memcpy(bigger, out, used * sizeof(WCHAR));
            SetupFree(out);
            out = bigger;
        }
        memcpy(out + used, add, add_length * sizeof(WCHAR));
        used += add_length;
        out[used] = 0;
        p += skip;
    }
    return out;
}

/* ------------------------------------------------------------------ handles and contexts */

INF_FILE *SetupInfOf(HINF handle)
{
    INF_FILE *f = handle;
    if (!f || handle == INVALID_HANDLE_VALUE || f->Magic != INF_MAGIC) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    return f;
}

const INF_LINE *SetupInfLine(INF_FILE *file, ULONG section, ULONG index)
{
    if (section >= file->SectionCount || index >= file->Sections[section].LineCount)
        return nullptr;
    return &file->Lines[file->Sections[section].FirstLine + index];
}

const INF_LINE *SetupContextLine(const INFCONTEXT *context, INF_FILE **file)
{
    INF_FILE *f = context->CurrentInf;
    if (!f || f->Magic != INF_MAGIC) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    const INF_LINE *l = SetupInfLine(f, context->Section, context->Line);
    if (!l) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (file)
        *file = f;
    return l;
}

PWSTR SetupContextField(const INFCONTEXT *context, DWORD field)
{
    INF_FILE *f;
    const INF_LINE *l = SetupContextLine(context, &f);
    if (!l)
        return nullptr;
    PCWSTR raw = value_of(f, l, field);
    if (!raw)
        return nullptr;
    return SetupInfExpand(context->Inf, raw);
}

static BOOL find_line(INF_FILE *head, PCWSTR section, PCWSTR key, INF_FILE *from_file, ULONG from_section, LONG from_line,
                      INFCONTEXT *context)
{
    BOOL started = from_file == nullptr;
    for (INF_FILE *f = head; f; f = f->Next) {
        if (!started && f != from_file)
            continue;
        for (ULONG s = 0; s < f->SectionCount; s++) {
            if (!started && s < from_section)
                continue;
            if (section && _wcsicmp(f->Sections[s].Name, section))
                continue;
            ULONG first = 0;
            if (!started) {
                first = (ULONG)(from_line + 1);
                started = TRUE;
            }
            for (ULONG i = first; i < f->Sections[s].LineCount; i++) {
                const INF_LINE *l = &f->Lines[f->Sections[s].FirstLine + i];
                if (key) {
                    if (!l->HasKey)
                        continue;
                    PWSTR k = SetupInfExpand(head, f->Fields[l->First]);
                    BOOL same = k && !_wcsicmp(k, key);
                    SetupFree(k);
                    if (!same)
                        continue;
                }
                context->Inf = head;
                context->CurrentInf = f;
                context->Section = s;
                context->Line = i;
                return TRUE;
            }
            /* the lines of a section are found in its first file that has the section; the next files go on after. */
        }
    }
    SetLastError(ERROR_LINE_NOT_FOUND);
    return FALSE;
}

BOOL SetupFirstLine(HINF handle, PCWSTR section, PCWSTR key, INFCONTEXT *context)
{
    INF_FILE *head = SetupInfOf(handle);
    if (!head)
        return FALSE;
    if (!context) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL named = FALSE;
    for (INF_FILE *f = head; f && !named; f = f->Next)
        named = SetupInfFindSection(f, section) >= 0;
    if (!named) {
        SetLastError(ERROR_SECTION_NOT_FOUND);
        return FALSE;
    }
    return find_line(head, section, key, nullptr, 0, 0, context);
}

BOOL SetupNextLine(const INFCONTEXT *in, INFCONTEXT *out)
{
    INF_FILE *f;
    if (!SetupContextLine(in, &f))
        return FALSE;
    INF_FILE *head = in->Inf;
    PCWSTR section = f->Sections[in->Section].Name;
    INFCONTEXT found;
    if (find_line(head, section, nullptr, f, in->Section, (LONG)in->Line, &found)) {
        *out = found;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ platform decoration */

static const WCHAR *native_decoration(void)
{
    SYSTEM_INFO info;
    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture) {
    case 12: /* PROCESSOR_ARCHITECTURE_ARM64 */
        return L".NTarm64";
    case 0: /* x86 */
        return L".NTx86";
    default:
        return L".NTamd64";
    }
}

BOOL SetupDecoratedSection(INF_FILE *head, PCWSTR name, PWSTR out, SIZE_T chars)
{
    PCWSTR suffixes[] = {native_decoration(), L".NT", L""};
    for (int i = 0; i < 3; i++) {
        if (wcslen(name) + wcslen(suffixes[i]) + 1 > chars)
            return FALSE;
        wcscpy(out, name);
        wcscat(out, suffixes[i]);
        for (INF_FILE *f = head; f; f = f->Next)
            if (SetupInfFindSection(f, out) >= 0)
                return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ opening */

static INF_FILE *open_file(PCWSTR name, UINT *error_line, DWORD *error)
{
    WCHAR full[MAX_PATH];
    PCWSTR path = name;
    if (!wcschr(name, '\\') && !wcschr(name, '/') && !wcschr(name, ':')) {
        /* a name alone is looked for where windows keeps its inf files. */
        UINT n = GetWindowsDirectoryW(full, MAX_PATH);
        WCHAR tried[MAX_PATH];
        wcscpy(tried, name);
        if (GetFileAttributesW(name) == INVALID_FILE_ATTRIBUTES && n && n + wcslen(name) + 6 < MAX_PATH) {
            wcscpy(full + n, L"\\INF\\");
            wcscat(full, name);
            path = full;
        }
    }
    SIZE_T length;
    PWSTR text = read_text(path, &length);
    if (!text) {
        *error = GetLastError();
        return nullptr;
    }
    INF_FILE *f = SetupAlloc(sizeof(*f));
    if (!f) {
        SetupFree(text);
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return nullptr;
    }
    f->Magic = INF_MAGIC;
    if (!GetFullPathNameW(path, MAX_PATH, f->Path, nullptr))
        wcscpy(f->Path, path);
    BOOL ok = parse_text(f, text, length, error_line, error);
    SetupFree(text);
    if (!ok) {
        free_file(f);
        return nullptr;
    }
    return f;
}

static PCWSTR version_value(INF_FILE *f, PCWSTR key)
{
    LONG s = SetupInfFindSection(f, L"Version");
    for (ULONG i = 0; s >= 0 && i < f->Sections[s].LineCount; i++) {
        const INF_LINE *l = &f->Lines[f->Sections[s].FirstLine + i];
        if (l->HasKey && l->Count >= 2 && !_wcsicmp(f->Fields[l->First], key))
            return f->Fields[l->First + 1];
    }
    return nullptr;
}

SETUPAPI HINF WINAPI SetupOpenInfFileW(PCWSTR name, PCWSTR inf_class, DWORD style, PUINT error_line)
{
    if (!name || (style & ~(DWORD)(INF_STYLE_OLDNT | INF_STYLE_WIN4 | INF_STYLE_CACHE_ENABLE | INF_STYLE_CACHE_DISABLE |
                                    INF_STYLE_CACHE_IGNORE)) ||
        !(style & (INF_STYLE_OLDNT | INF_STYLE_WIN4))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    UINT line = 0;
    DWORD error = 0;
    INF_FILE *f = open_file(name, &line, &error);
    if (!f) {
        if (error_line)
            *error_line = line;
        SetLastError(error);
        return INVALID_HANDLE_VALUE;
    }
    if (style & INF_STYLE_WIN4) {
        PCWSTR signature = version_value(f, L"Signature");
        if (!signature || (_wcsicmp(signature, L"$Windows NT$") && _wcsicmp(signature, L"$Chicago$") &&
                           _wcsicmp(signature, L"$Windows 95$"))) {
            free_file(f);
            SetLastError(ERROR_WRONG_INF_STYLE);
            return INVALID_HANDLE_VALUE;
        }
    }
    if (inf_class) {
        PCWSTR value = version_value(f, L"Class");
        PWSTR expanded = value ? SetupInfExpand(f, value) : nullptr;
        BOOL same = expanded && !_wcsicmp(expanded, inf_class);
        SetupFree(expanded);
        if (!same) {
            free_file(f);
            SetLastError(ERROR_CLASS_MISMATCH);
            return INVALID_HANDLE_VALUE;
        }
    }
    return f;
}

SETUPAPI HINF WINAPI SetupOpenInfFileA(PCSTR name, PCSTR inf_class, DWORD style, PUINT error_line)
{
    PWSTR a = SetupToWide(name), b = SetupToWide(inf_class);
    HINF inf = (name && !a) || (inf_class && !b) ? INVALID_HANDLE_VALUE : SetupOpenInfFileW(a, b, style, error_line);
    SetupFree(a);
    SetupFree(b);
    return inf;
}

SETUPAPI HINF WINAPI SetupOpenMasterInf(void)
{
    return SetupOpenInfFileW(L"layout.inf", nullptr, INF_STYLE_WIN4, nullptr);
}

SETUPAPI BOOL WINAPI SetupOpenAppendInfFileW(PCWSTR name, HINF inf, PUINT error_line)
{
    INF_FILE *head = SetupInfOf(inf);
    if (!head)
        return FALSE;
    UINT line = 0;
    DWORD error = 0;
    /* no name appends the files the first one lists as its layout. */
    if (!name) {
        PCWSTR layout = version_value(head, L"LayoutFile");
        if (!layout) {
            SetLastError(ERROR_LINE_NOT_FOUND);
            return FALSE;
        }
        name = layout;
    }
    INF_FILE *f = open_file(name, &line, &error);
    if (!f) {
        if (error_line)
            *error_line = line;
        SetLastError(error);
        return FALSE;
    }
    INF_FILE *last = head;
    while (last->Next)
        last = last->Next;
    last->Next = f;
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupOpenAppendInfFileA(PCSTR name, HINF inf, PUINT error_line)
{
    PWSTR a = SetupToWide(name);
    BOOL ok = (name && !a) ? FALSE : SetupOpenAppendInfFileW(a, inf, error_line);
    SetupFree(a);
    return ok;
}

SETUPAPI VOID WINAPI SetupCloseInfFile(HINF inf)
{
    INF_FILE *f = inf;
    if (f && inf != INVALID_HANDLE_VALUE && f->Magic == INF_MAGIC) {
        f->Magic = 0;
        for (INF_FILE *n = f->Next; n; n = n->Next)
            n->Magic = 0;
        free_file(f);
    }
}

/* ------------------------------------------------------------------ information about a file */

/*
 * what SetupGetInfInformation hands over is a buffer the program passes back: the style, the number of files, then for
 * each file the length of what follows it, its path and the Version section's keys and values as text.
 */
typedef struct INF_PART {
    DWORD Length;
    DWORD Reserved;
    WCHAR Path[1];
} INF_PART;

static BOOL search_info(LPCVOID spec, DWORD search, INF_FILE **file, BOOL *own)
{
    *own = FALSE;
    if (search == INFINFO_INF_SPEC_IS_HINF) {
        *file = SetupInfOf((HINF)spec);
        return *file != nullptr;
    }
    if (!spec) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    UINT line;
    DWORD error = 0;
    INF_FILE *f = open_file((PCWSTR)spec, &line, &error);
    if (!f) {
        SetLastError(error);
        return FALSE;
    }
    *file = f;
    *own = TRUE;
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupGetInfInformationW(LPCVOID spec, DWORD search, PSP_INF_INFORMATION info, DWORD size, PDWORD required)
{
    if (search < INFINFO_INF_SPEC_IS_HINF || search > INFINFO_INF_PATH_LIST_SEARCH) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    INF_FILE *f;
    BOOL own;
    if (!search_info(spec, search, &f, &own))
        return FALSE;
    /* one record per file, the path then the version section's pairs of key and value. */
    DWORD need = offsetof(SP_INF_INFORMATION, VersionData);
    DWORD files = 0;
    for (INF_FILE *p = f; p; p = p->Next) {
        need += (DWORD)(offsetof(INF_PART, Path) + (wcslen(p->Path) + 1) * sizeof(WCHAR));
        files++;
        LONG s = SetupInfFindSection(p, L"Version");
        for (ULONG i = 0; s >= 0 && i < p->Sections[s].LineCount; i++) {
            const INF_LINE *l = &p->Lines[p->Sections[s].FirstLine + i];
            if (l->HasKey && l->Count >= 2)
                need += (DWORD)((wcslen(p->Fields[l->First]) + wcslen(p->Fields[l->First + 1]) + 2) * sizeof(WCHAR));
        }
        need = (need + 3) & ~3u;
        if (!own && search == INFINFO_INF_SPEC_IS_HINF)
            ;
    }
    if (required)
        *required = need;
    BOOL ok = TRUE;
    if (!info || size < need) {
        if (info) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            ok = FALSE;
        }
    } else {
        memset(info, 0, need);
        info->InfStyle = INF_STYLE_WIN4;
        info->InfCount = files;
        BYTE *out = info->VersionData;
        for (INF_FILE *p = f; p; p = p->Next) {
            INF_PART *part = (INF_PART *)out;
            wcscpy(part->Path, p->Path);
            PWSTR text = (PWSTR)((BYTE *)part + offsetof(INF_PART, Path) + (wcslen(p->Path) + 1) * sizeof(WCHAR));
            PWSTR at = text;
            LONG s = SetupInfFindSection(p, L"Version");
            for (ULONG i = 0; s >= 0 && i < p->Sections[s].LineCount; i++) {
                const INF_LINE *l = &p->Lines[p->Sections[s].FirstLine + i];
                if (l->HasKey && l->Count >= 2) {
                    wcscpy(at, p->Fields[l->First]);
                    at += wcslen(at) + 1;
                    wcscpy(at, p->Fields[l->First + 1]);
                    at += wcslen(at) + 1;
                }
            }
            DWORD used = (DWORD)((BYTE *)at - out);
            used = (used + 3) & ~3u;
            part->Length = used;
            out += used;
        }
    }
    if (own)
        free_file(f);
    return ok;
}

SETUPAPI BOOL WINAPI SetupGetInfInformationA(LPCVOID spec, DWORD search, PSP_INF_INFORMATION info, DWORD size, PDWORD required)
{
    if (search == INFINFO_INF_SPEC_IS_HINF)
        return SetupGetInfInformationW(spec, search, info, size, required);
    PWSTR name = SetupToWide((PCSTR)spec);
    BOOL ok = name ? SetupGetInfInformationW(name, search, info, size, required) : FALSE;
    SetupFree(name);
    return ok;
}

static INF_PART *part_at(PSP_INF_INFORMATION info, UINT index)
{
    if (!info || index >= info->InfCount)
        return nullptr;
    BYTE *at = info->VersionData;
    for (UINT i = 0; i < index; i++)
        at += ((INF_PART *)at)->Length;
    return (INF_PART *)at;
}

static BOOL give_wide(PCWSTR text, PWSTR buffer, DWORD size, PDWORD required)
{
    DWORD need = (DWORD)wcslen(text) + 1;
    if (required)
        *required = need;
    if (!buffer)
        return TRUE;
    if (size < need) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    wcscpy(buffer, text);
    return TRUE;
}

static BOOL give_ansi(PCWSTR text, PSTR buffer, DWORD size, PDWORD required)
{
    PSTR ansi = SetupToAnsi(text);
    if (!ansi) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    DWORD need = (DWORD)strlen(ansi) + 1;
    if (required)
        *required = need;
    BOOL ok = TRUE;
    if (buffer) {
        if (size < need) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            ok = FALSE;
        } else {
            memcpy(buffer, ansi, need);
        }
    }
    SetupFree(ansi);
    return ok;
}

SETUPAPI BOOL WINAPI SetupQueryInfFileInformationW(PSP_INF_INFORMATION info, UINT index, PWSTR buffer, DWORD size, PDWORD required)
{
    INF_PART *p = part_at(info, index);
    if (!p) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return give_wide(p->Path, buffer, size, required);
}

SETUPAPI BOOL WINAPI SetupQueryInfFileInformationA(PSP_INF_INFORMATION info, UINT index, PSTR buffer, DWORD size, PDWORD required)
{
    INF_PART *p = part_at(info, index);
    if (!p) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return give_ansi(p->Path, buffer, size, required);
}

SETUPAPI BOOL WINAPI SetupQueryInfVersionInformationW(PSP_INF_INFORMATION info, UINT index, PCWSTR key, PWSTR buffer, DWORD size,
                                                      PDWORD required)
{
    INF_PART *p = part_at(info, index);
    if (!p || !key) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR at = (PWSTR)((BYTE *)p + offsetof(INF_PART, Path) + (wcslen(p->Path) + 1) * sizeof(WCHAR));
    PWSTR end = (PWSTR)((BYTE *)p + p->Length);
    while (at < end && *at) {
        PWSTR name = at, value = at + wcslen(at) + 1;
        if (!_wcsicmp(name, key))
            return give_wide(value, buffer, size, required);
        at = value + wcslen(value) + 1;
    }
    SetLastError(ERROR_LINE_NOT_FOUND);
    return FALSE;
}

SETUPAPI BOOL WINAPI SetupQueryInfVersionInformationA(PSP_INF_INFORMATION info, UINT index, PCSTR key, PSTR buffer, DWORD size,
                                                      PDWORD required)
{
    PWSTR wide = SetupToWide(key);
    if (!wide)
        return FALSE;
    DWORD need = 0;
    BOOL ok = SetupQueryInfVersionInformationW(info, index, wide, nullptr, 0, &need);
    SetupFree(wide);
    if (!ok)
        return FALSE;
    PWSTR text = SetupAlloc((SIZE_T)need * sizeof(WCHAR));
    wide = SetupToWide(key);
    ok = text && SetupQueryInfVersionInformationW(info, index, wide, text, need, nullptr);
    SetupFree(wide);
    if (ok)
        ok = give_ansi(text, buffer, size, required);
    SetupFree(text);
    return ok;
}

SETUPAPI BOOL WINAPI SetupGetInfFileListW(PCWSTR directory, DWORD style, PWSTR buffer, DWORD size, PDWORD required)
{
    WCHAR where[MAX_PATH], pattern[MAX_PATH];
    if (directory) {
        wcscpy(where, directory);
    } else {
        UINT n = GetWindowsDirectoryW(where, MAX_PATH);
        if (!n || n + 5 >= MAX_PATH) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return FALSE;
        }
        wcscat(where, L"\\INF");
    }
    wcscpy(pattern, where);
    wcscat(pattern, L"\\*.inf");
    DWORD need = 1;
    WIN32_FIND_DATAW find;
    HANDLE h = FindFirstFileW(pattern, &find);
    /* the names, each ending in a zero, then one more. */
    for (BOOL ok = h != INVALID_HANDLE_VALUE; ok; ok = FindNextFileW(h, &find))
        need += (DWORD)wcslen(find.cFileName) + 1;
    if (h != INVALID_HANDLE_VALUE)
        FindClose(h);
    if (required)
        *required = need;
    if (!buffer)
        return TRUE;
    if (size < need) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    PWSTR at = buffer;
    h = FindFirstFileW(pattern, &find);
    for (BOOL ok = h != INVALID_HANDLE_VALUE; ok; ok = FindNextFileW(h, &find)) {
        wcscpy(at, find.cFileName);
        at += wcslen(at) + 1;
    }
    if (h != INVALID_HANDLE_VALUE)
        FindClose(h);
    *at = 0;
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupGetInfFileListA(PCSTR directory, DWORD style, PSTR buffer, DWORD size, PDWORD required)
{
    PWSTR where = SetupToWide(directory);
    DWORD need = 0;
    BOOL ok = SetupGetInfFileListW(where, style, nullptr, 0, &need);
    PWSTR list = ok ? SetupAlloc((SIZE_T)need * sizeof(WCHAR)) : nullptr;
    ok = list && SetupGetInfFileListW(where, style, list, need, nullptr);
    SetupFree(where);
    if (!ok) {
        SetupFree(list);
        return FALSE;
    }
    int bytes = WideCharToMultiByte(CP_ACP, 0, list, (int)need, nullptr, 0, nullptr, nullptr);
    if (required)
        *required = (DWORD)bytes;
    if (buffer) {
        if (size < (DWORD)bytes) {
            SetupFree(list);
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        WideCharToMultiByte(CP_ACP, 0, list, (int)need, buffer, bytes, nullptr, nullptr);
    }
    SetupFree(list);
    return TRUE;
}

/* ------------------------------------------------------------------ lines and fields */

SETUPAPI BOOL WINAPI SetupFindFirstLineW(HINF inf, PCWSTR section, PCWSTR key, PINFCONTEXT context)
{
    if (!section) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return SetupFirstLine(inf, section, key, context);
}

SETUPAPI BOOL WINAPI SetupFindFirstLineA(HINF inf, PCSTR section, PCSTR key, PINFCONTEXT context)
{
    PWSTR a = SetupToWide(section), b = SetupToWide(key);
    BOOL ok = (!section || a) && (!key || b) ? SetupFindFirstLineW(inf, a, b, context) : FALSE;
    SetupFree(a);
    SetupFree(b);
    return ok;
}

SETUPAPI BOOL WINAPI SetupFindNextLine(PINFCONTEXT in, PINFCONTEXT out)
{
    if (!in || !out) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return SetupNextLine(in, out);
}

SETUPAPI BOOL WINAPI SetupFindNextMatchLineW(PINFCONTEXT in, PCWSTR key, PINFCONTEXT out)
{
    if (!in || !out) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    INF_FILE *f;
    if (!SetupContextLine(in, &f))
        return FALSE;
    return find_line(in->Inf, f->Sections[in->Section].Name, key, f, in->Section, (LONG)in->Line, out);
}

SETUPAPI BOOL WINAPI SetupFindNextMatchLineA(PINFCONTEXT in, PCSTR key, PINFCONTEXT out)
{
    PWSTR wide = SetupToWide(key);
    BOOL ok = (!key || wide) ? SetupFindNextMatchLineW(in, wide, out) : FALSE;
    SetupFree(wide);
    return ok;
}

SETUPAPI BOOL WINAPI SetupGetLineByIndexW(HINF inf, PCWSTR section, DWORD index, PINFCONTEXT context)
{
    INFCONTEXT at;
    if (!section || !context) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!SetupFirstLine(inf, section, nullptr, &at))
        return FALSE;
    for (DWORD i = 0; i < index; i++) {
        INFCONTEXT next;
        if (!SetupNextLine(&at, &next)) {
            SetLastError(ERROR_LINE_NOT_FOUND);
            return FALSE;
        }
        at = next;
    }
    *context = at;
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupGetLineByIndexA(HINF inf, PCSTR section, DWORD index, PINFCONTEXT context)
{
    PWSTR wide = SetupToWide(section);
    BOOL ok = wide ? SetupGetLineByIndexW(inf, wide, index, context) : FALSE;
    SetupFree(wide);
    return ok;
}

SETUPAPI LONG WINAPI SetupGetLineCountW(HINF inf, PCWSTR section)
{
    INF_FILE *head = SetupInfOf(inf);
    if (!head || !section)
        return -1;
    LONG count = -1;
    for (INF_FILE *f = head; f; f = f->Next) {
        LONG s = SetupInfFindSection(f, section);
        if (s >= 0)
            count = (count < 0 ? 0 : count) + (LONG)f->Sections[s].LineCount;
    }
    if (count < 0)
        SetLastError(ERROR_SECTION_NOT_FOUND);
    return count;
}

SETUPAPI LONG WINAPI SetupGetLineCountA(HINF inf, PCSTR section)
{
    PWSTR wide = SetupToWide(section);
    LONG n = wide ? SetupGetLineCountW(inf, wide) : -1;
    SetupFree(wide);
    return n;
}

SETUPAPI DWORD WINAPI SetupGetFieldCount(PINFCONTEXT context)
{
    INF_FILE *f;
    const INF_LINE *l = context ? SetupContextLine(context, &f) : nullptr;
    if (!l) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return l->HasKey ? l->Count - 1 : l->Count;
}

SETUPAPI BOOL WINAPI SetupGetStringFieldW(PINFCONTEXT context, DWORD field, PWSTR buffer, DWORD size, PDWORD required)
{
    if (!context) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR text = SetupContextField(context, field);
    if (!text) {
        if (GetLastError() != ERROR_INVALID_PARAMETER)
            SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = give_wide(text, buffer, size, required);
    SetupFree(text);
    return ok;
}

SETUPAPI BOOL WINAPI SetupGetStringFieldA(PINFCONTEXT context, DWORD field, PSTR buffer, DWORD size, PDWORD required)
{
    if (!context) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR text = SetupContextField(context, field);
    if (!text) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = give_ansi(text, buffer, size, required);
    SetupFree(text);
    return ok;
}

/* a number as the inf has it: decimal, or hexadecimal with 0x. */
static BOOL parse_number(PCWSTR text, LONG64 *value)
{
    while (*text && is_space(*text))
        text++;
    BOOL negative = *text == '-';
    if (*text == '-' || *text == '+')
        text++;
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    if (!*text)
        return FALSE;
    LONG64 n = 0;
    for (; *text && !is_space(*text); text++) {
        int digit;
        if (*text >= '0' && *text <= '9')
            digit = *text - '0';
        else if (base == 16 && *text >= 'a' && *text <= 'f')
            digit = *text - 'a' + 10;
        else if (base == 16 && *text >= 'A' && *text <= 'F')
            digit = *text - 'A' + 10;
        else
            return FALSE;
        n = n * base + digit;
    }
    *value = negative ? -n : n;
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupGetIntField(PINFCONTEXT context, DWORD field, int *value)
{
    if (!context || !value) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR text = SetupContextField(context, field);
    if (!text) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG64 n;
    BOOL ok = parse_number(text, &n);
    SetupFree(text);
    if (!ok) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    *value = (int)n;
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupGetMultiSzFieldW(PINFCONTEXT context, DWORD field, PWSTR buffer, DWORD size, PDWORD required)
{
    INF_FILE *f;
    const INF_LINE *l = context ? SetupContextLine(context, &f) : nullptr;
    if (!l) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* the fields from this one on, each a string of its own, then an empty one. */
    DWORD count = SetupGetFieldCount(context), need = 1;
    PWSTR *parts = SetupAlloc((count + 1) * sizeof(PWSTR));
    if (!parts)
        return FALSE;
    DWORD n = 0;
    for (DWORD i = field; i <= count; i++) {
        parts[n] = SetupContextField(context, i);
        if (!parts[n])
            break;
        need += (DWORD)wcslen(parts[n]) + 1;
        n++;
    }
    if (required)
        *required = need;
    BOOL ok = TRUE;
    if (buffer) {
        if (size < need) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            ok = FALSE;
        } else {
            PWSTR at = buffer;
            for (DWORD i = 0; i < n; i++) {
                wcscpy(at, parts[i]);
                at += wcslen(at) + 1;
            }
            *at = 0;
        }
    }
    for (DWORD i = 0; i < n; i++)
        SetupFree(parts[i]);
    SetupFree(parts);
    return ok;
}

SETUPAPI BOOL WINAPI SetupGetMultiSzFieldA(PINFCONTEXT context, DWORD field, PSTR buffer, DWORD size, PDWORD required)
{
    DWORD need = 0;
    if (!SetupGetMultiSzFieldW(context, field, nullptr, 0, &need))
        return FALSE;
    PWSTR text = SetupAlloc((SIZE_T)need * sizeof(WCHAR));
    if (!text || !SetupGetMultiSzFieldW(context, field, text, need, nullptr)) {
        SetupFree(text);
        return FALSE;
    }
    int bytes = WideCharToMultiByte(CP_ACP, 0, text, (int)need, nullptr, 0, nullptr, nullptr);
    if (required)
        *required = (DWORD)bytes;
    BOOL ok = TRUE;
    if (buffer) {
        if (size < (DWORD)bytes) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            ok = FALSE;
        } else {
            WideCharToMultiByte(CP_ACP, 0, text, (int)need, buffer, bytes, nullptr, nullptr);
        }
    }
    SetupFree(text);
    return ok;
}

SETUPAPI BOOL WINAPI SetupGetBinaryField(PINFCONTEXT context, DWORD field, PBYTE buffer, DWORD size, PDWORD required)
{
    INF_FILE *f;
    const INF_LINE *l = context ? SetupContextLine(context, &f) : nullptr;
    if (!l || !field) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* each field from this one on is a byte, in hexadecimal. */
    DWORD count = SetupGetFieldCount(context);
    DWORD need = field <= count ? count - field + 1 : 0;
    if (required)
        *required = need;
    if (!buffer)
        return TRUE;
    if (size < need) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    for (DWORD i = 0; i < need; i++) {
        PWSTR text = SetupContextField(context, field + i);
        LONG64 n = 0;
        PCWSTR p = text ? text : L"";
        while (*p == ' ')
            p++;
        for (; *p; p++) {
            int digit = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 :
                        *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : -1;
            if (digit < 0) {
                SetupFree(text);
                SetLastError(ERROR_INVALID_DATA);
                return FALSE;
            }
            n = n * 16 + digit;
        }
        buffer[i] = (BYTE)n;
        SetupFree(text);
    }
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupGetLineTextW(PINFCONTEXT context, HINF inf, PCWSTR section, PCWSTR key, PWSTR buffer, DWORD size,
                                       PDWORD required)
{
    INFCONTEXT found;
    PINFCONTEXT at = context;
    if (!at) {
        if (!SetupFirstLine(inf, section, key, &found))
            return FALSE;
        at = &found;
    }
    DWORD count = SetupGetFieldCount(at), need = 1;
    PWSTR *parts = SetupAlloc((count + 1) * sizeof(PWSTR));
    if (!parts)
        return FALSE;
    for (DWORD i = 1; i <= count; i++) {
        parts[i - 1] = SetupContextField(at, i);
        need += (parts[i - 1] ? (DWORD)wcslen(parts[i - 1]) : 0) + (i > 1 ? 1 : 0);
    }
    if (required)
        *required = need;
    BOOL ok = TRUE;
    if (buffer) {
        if (size < need) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            ok = FALSE;
        } else {
            PWSTR out = buffer;
            *out = 0;
            for (DWORD i = 1; i <= count; i++) {
                if (i > 1)
                    *out++ = ',';
                if (parts[i - 1]) {
                    wcscpy(out, parts[i - 1]);
                    out += wcslen(out);
                }
            }
            *out = 0;
        }
    }
    for (DWORD i = 0; i < count; i++)
        SetupFree(parts[i]);
    SetupFree(parts);
    return ok;
}

SETUPAPI BOOL WINAPI SetupGetLineTextA(PINFCONTEXT context, HINF inf, PCSTR section, PCSTR key, PSTR buffer, DWORD size,
                                       PDWORD required)
{
    PWSTR s = SetupToWide(section), k = SetupToWide(key);
    DWORD need = 0;
    BOOL ok = SetupGetLineTextW(context, inf, s, k, nullptr, 0, &need);
    PWSTR text = ok ? SetupAlloc((SIZE_T)need * sizeof(WCHAR)) : nullptr;
    ok = ok && text && SetupGetLineTextW(context, inf, s, k, text, need, nullptr);
    SetupFree(s);
    SetupFree(k);
    if (ok)
        ok = give_ansi(text, buffer, size, required);
    SetupFree(text);
    return ok;
}
