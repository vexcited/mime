/*
 * mciSendString: a command line read word by word against the command
 * tables into the parameter block mciSendCommand takes, and the answer
 * printed back. a device the string names that is not open is opened for
 * the command on mci's window thread, and closed again after it.
 */
#define MIME_BUILD_WINMM
#include "winmm_private.h"
#include "mci_private.h"

#define MAX_ARGS 48

typedef struct ARG {
    const WCHAR *word;
    DWORD flag;
    WORD kind;
    UINT offset;
    const BYTE *items; /* the constants of a group */
} ARG;

static BOOL is_space(WCHAR c)
{
    return c == ' ';
}

/*
 * a word up to a space outside quotes. a quote opens quoting only at the
 * start, two quotes inside give one, and nothing may follow the closing one.
 */
static MCIERROR token(const WCHAR **at, WCHAR *out)
{
    const WCHAR *p = *at;
    while (is_space(*p))
        p++;
    int n = 0;
    if (*p == '"') {
        p++;
        for (;;) {
            if (!*p)
                return MCIERR_NO_CLOSING_QUOTE;
            if (*p == '"') {
                if (p[1] == '"') {
                    out[n++] = '"';
                    p += 2;
                    continue;
                }
                p++;
                break;
            }
            out[n++] = *p++;
        }
        if (*p && !is_space(*p))
            return MCIERR_EXTRA_CHARACTERS;
    } else {
        while (*p && !is_space(*p))
            out[n++] = *p++;
    }
    out[n] = 0;
    *at = p;
    return MMSYSERR_NOERROR;
}

/* a keyword at p: letters of the input folded to lower case, one space of it matching a run of spaces. */
static int match(const WCHAR *keyword, const WCHAR *p)
{
    const WCHAR *start = p;
    if (!*keyword)
        return 0;
    while (*keyword) {
        if (*keyword == ' ') {
            if (!is_space(*p))
                return 0;
            while (is_space(*p))
                p++;
            keyword++;
            continue;
        }
        WCHAR c = *p >= 'A' && *p <= 'Z' ? *p + 32 : *p;
        if (c != *keyword)
            return 0;
        p++;
        keyword++;
    }
    return !*p || is_space(*p) ? (int)(p - start) : 0;
}

/* decimal with an optional minus, or up to four bytes as a:b:c:d, the first lowest. */
static MCIERROR integer(const WCHAR **at, LONG64 *out)
{
    const WCHAR *p = *at;
    while (is_space(*p))
        p++;
    BOOL negative = *p == '-';
    if (negative)
        p++;
    ULONG64 value = 0, fields[4] = {};
    int count = 0, digits = 0;
    for (;;) {
        if (*p >= '0' && *p <= '9') {
            value = value * 10 + (*p - '0');
            if (value > 0xFFFFFFFFull)
                return MCIERR_BAD_INTEGER;
            digits++;
            p++;
        } else if (*p == ':') {
            if (negative || count == 3 || !digits || value > 255)
                return MCIERR_BAD_INTEGER;
            fields[count++] = value;
            value = 0;
            digits = 0;
            p++;
        } else {
            break;
        }
    }
    if (!digits || (*p && !is_space(*p)))
        return MCIERR_BAD_INTEGER;
    if (count) {
        if (value > 255)
            return MCIERR_BAD_INTEGER;
        fields[count++] = value;
        value = 0;
        for (int i = 0; i < count; i++)
            value |= fields[i] << (8 * i);
    } else if (negative) {
        if (value > 2147483647)
            return MCIERR_BAD_INTEGER;
        *out = -(LONG64)value;
        *at = p;
        return MMSYSERR_NOERROR;
    }
    *out = (LONG64)value;
    *at = p;
    return MMSYSERR_NOERROR;
}

static UINT return_size(WORD kind)
{
    switch (kind) {
    case MCI_STRING:
        return sizeof(void *) + sizeof(DWORD);
    case MCI_INTEGER:
        return sizeof(DWORD);
    case MCI_RECT:
        return 4 * sizeof(DWORD);
    default:
        return sizeof(DWORD_PTR);
    }
}

static UINT arg_size(WORD kind)
{
    switch (kind) {
    case MCI_STRING:
    case MCI_HWND:
    case MCI_HPAL:
    case MCI_HDC:
    case MCI_INTEGER64:
        return sizeof(void *);
    case MCI_INTEGER:
    case MCI_CONSTANT:
        return sizeof(DWORD);
    case MCI_RECT:
        return 4 * sizeof(DWORD);
    default:
        return 0;
    }
}

/* the arguments of a command in table order, each with its place in the block. */
static UINT layout(const BYTE *command, ARG *args, WORD *return_kind, UINT *size)
{
    const WCHAR *word;
    DWORD value;
    WORD kind;
    const BYTE *p = MciEntry(command, &word, &value, &kind);
    UINT offset = sizeof(DWORD_PTR), n = 0;
    *return_kind = 0;
    for (;;) {
        p = MciEntry(p, &word, &value, &kind);
        if (kind == MCI_END_COMMAND || kind == MCI_END_COMMAND_LIST || kind == MCI_COMMAND_HEAD)
            break;
        if (kind == MCI_RETURN) {
            *return_kind = (WORD)value;
            offset += return_size((WORD)value);
            continue;
        }
        if (n == MAX_ARGS)
            break;
        args[n] = (ARG){word, value, kind, offset, nullptr};
        offset += arg_size(kind);
        if (kind == MCI_CONSTANT) {
            args[n].items = p;
            /* the group ends at its end entry, whatever kinds the entries inside claim. */
            for (;;) {
                p = MciEntry(p, &word, &value, &kind);
                if (kind == MCI_END_CONSTANT || kind == MCI_END_COMMAND_LIST)
                    break;
            }
        }
        n++;
    }
    *size = offset;
    return n;
}

static void put(BYTE *block, UINT offset, const void *data, UINT size)
{
    if (offset + size <= MCI_BLOCK_SIZE)
        memcpy(block + offset, data, size);
}

static MCIERROR constant(const ARG *a, const WCHAR **at, DWORD *out)
{
    const WCHAR *p = *at;
    while (is_space(*p))
        p++;
    if (!*p)
        return MCIERR_NO_INTEGER;
    const WCHAR *word;
    DWORD value;
    WORD kind;
    for (const BYTE *e = a->items;;) {
        e = MciEntry(e, &word, &value, &kind);
        if (kind == MCI_END_CONSTANT || kind == MCI_END_COMMAND_LIST)
            break;
        int n = match(word, p);
        if (n) {
            *out = value;
            *at = p + n;
            return MMSYSERR_NOERROR;
        }
    }
    LONG64 number;
    if (integer(&p, &number))
        return MCIERR_BAD_CONSTANT;
    *out = (DWORD)number;
    *at = p;
    return MMSYSERR_NOERROR;
}

static MCIERROR argument(const ARG *a, const WCHAR **at, MCI_REPARSE *out)
{
    switch (a->kind) {
    case MCI_FLAG:
        return MMSYSERR_NOERROR;
    case MCI_INTEGER:
    case MCI_INTEGER64:
    case MCI_HWND:
    case MCI_HPAL:
    case MCI_HDC: {
        LONG64 value;
        if (integer(at, &value))
            return MCIERR_BAD_INTEGER;
        if (a->kind == MCI_INTEGER) {
            DWORD v = (DWORD)value;
            put(out->block, a->offset, &v, 4);
        } else {
            DWORD_PTR v = (DWORD_PTR)(LONG_PTR)(LONG)value;
            if (a->kind == MCI_INTEGER64)
                v = (DWORD_PTR)value;
            put(out->block, a->offset, &v, sizeof(v));
        }
        return MMSYSERR_NOERROR;
    }
    case MCI_RECT: {
        LONG rect[4];
        for (int i = 0; i < 4; i++) {
            LONG64 value;
            if (integer(at, &value) || value < -32767 || value > 32768)
                return MCIERR_BAD_INTEGER;
            rect[i] = (LONG)value;
        }
        put(out->block, a->offset, rect, sizeof(rect));
        return MMSYSERR_NOERROR;
    }
    case MCI_CONSTANT: {
        DWORD value;
        MCIERROR r = constant(a, at, &value);
        if (!r)
            put(out->block, a->offset, &value, 4);
        return r;
    }
    case MCI_STRING: {
        const WCHAR *p = *at;
        while (is_space(*p))
            p++;
        if (!*p)
            return MCIERR_MISSING_STRING_ARGUMENT;
        if (out->string_count == MCI_MAX_STRINGS)
            return MCIERR_PARAM_OVERFLOW;
        WCHAR *s = HeapAlloc(GetProcessHeap(), 0, (lstrlenW(p) + 1) * sizeof(WCHAR));
        if (!s)
            return MCIERR_OUT_OF_MEMORY;
        MCIERROR r = token(&p, s);
        if (r) {
            HeapFree(GetProcessHeap(), 0, s);
            return r;
        }
        out->strings[out->string_count++] = s;
        put(out->block, a->offset, &s, sizeof(s));
        *at = p;
        return MMSYSERR_NOERROR;
    }
    default:
        return MCIERR_PARSER_INTERNAL;
    }
}

static void free_strings(MCI_REPARSE *r)
{
    for (UINT i = 0; i < r->string_count; i++)
        HeapFree(GetProcessHeap(), 0, r->strings[i]);
    r->string_count = 0;
}

/*
 * the words after the device against the command's entries, the first
 * matching entry winning; a word no keyword names goes to the unnamed
 * entry. open passes over words it does not know, for the device's own
 * open command to read once its driver loaded.
 */
MCIERROR MciParseArgs(const BYTE *command, const WCHAR *args, BOOL open, MCI_REPARSE *out)
{
    ARG list[MAX_ARGS];
    WORD return_kind;
    UINT size, n = layout(command, list, &return_kind, &size);
    free_strings(out);
    memset(out->block, 0, sizeof(out->block));
    out->flags = 0;
    out->deferred = 0;
    if (size > MCI_BLOCK_SIZE)
        return MCIERR_PARAM_OVERFLOW;
    DWORD constants = 0;
    const WCHAR *p = args;
    for (;;) {
        while (is_space(*p))
            p++;
        if (!*p)
            break;
        const ARG *found = nullptr;
        for (UINT i = 0; i < n && !found; i++) {
            int length = match(list[i].word, p);
            if (length) {
                found = &list[i];
                p += length;
            }
        }
        if (!found)
            for (UINT i = 0; i < n && !found; i++)
                if (!list[i].word[0])
                    found = &list[i];
        if (!found) {
            if (!open)
                return MCIERR_UNRECOGNIZED_KEYWORD;
            WCHAR skip[MAX_PATH];
            const WCHAR *q = p;
            while (*q && !is_space(*q))
                q++;
            if (q - p < MAX_PATH) {
                if (token(&p, skip))
                    p = q;
            } else {
                p = q;
            }
            out->deferred = MCIERR_UNRECOGNIZED_KEYWORD;
            continue;
        }
        if (found->kind == MCI_CONSTANT) {
            if (constants & found->flag)
                return MCIERR_FLAGS_NOT_COMPATIBLE;
            constants |= found->flag;
        } else if (out->flags & found->flag) {
            return MCIERR_DUPLICATE_FLAGS;
        }
        out->flags |= found->flag;
        MCIERROR r = argument(found, &p, out);
        if (r)
            return r;
    }
    return MMSYSERR_NOERROR;
}

/* ------------------------------------------------------------------ answers */

static MCIERROR print_number(WCHAR *ret, UINT size, LONG value)
{
    if (size < 12)
        return MCIERR_PARAM_OVERFLOW;
    wsprintfW(ret, L"%ld", value);
    return MMSYSERR_NOERROR;
}

/* bytes as two digits, three from 100; bytes past 127 are read signed, as windows reads them. */
static MCIERROR colonized(WCHAR *ret, UINT size, DWORD value, int fields)
{
    WCHAR text[24];
    int n = 0;
    for (int i = 0; i < fields; i++) {
        signed char b = (signed char)(value >> (8 * i));
        int v = (BYTE)b;
        if (i)
            text[n++] = ':';
        if (b < 100) {
            text[n++] = (WCHAR)('0' + b / 10);
            text[n++] = (WCHAR)('0' + b % 10);
        } else {
            text[n++] = (WCHAR)('0' + v / 100);
            text[n++] = (WCHAR)('0' + v / 10 % 10);
            text[n++] = (WCHAR)('0' + v % 10);
        }
    }
    text[n] = 0;
    if ((UINT)n >= size)
        return MCIERR_PARAM_OVERFLOW;
    lstrcpyW(ret, text);
    return MMSYSERR_NOERROR;
}

static MCIERROR answer(MCIDEVICEID id, MCIERROR result, WORD kind, const BYTE *block, WCHAR *ret, UINT size)
{
    DWORD flags = result & 0xFFFF0000;
    if (kind == MCI_STRING) {
        if (flags & MCI_INTEGER_RETURNED) {
            DWORD value;
            memcpy(&value, ret, 4);
            return print_number(ret, size, (LONG)value);
        }
        return MMSYSERR_NOERROR;
    }
    DWORD value;
    memcpy(&value, block + sizeof(DWORD_PTR), 4);
    if (flags & MCI_RESOURCE_RETURNED) {
        HMODULE module = MciStringModule(id, (flags & MCI_RESOURCE_DRIVER) != 0);
        if (!LoadStringW(module, HIWORD(value), ret, size) && module != WinmmModule)
            LoadStringW(WinmmModule, HIWORD(value), ret, size);
        return MMSYSERR_NOERROR;
    }
    if (flags & MCI_COLONIZED3_RETURN)
        return colonized(ret, size, value, 3);
    if (flags & MCI_COLONIZED4_RETURN)
        return colonized(ret, size, value, 4);
    switch (kind) {
    case MCI_INTEGER:
    case MCI_INTEGER64:
    case MCI_HWND:
    case MCI_HPAL:
    case MCI_HDC:
        return print_number(ret, size, (LONG)value);
    case MCI_RECT: {
        LONG r[4];
        memcpy(r, block + sizeof(DWORD_PTR), sizeof(r));
        if (size < 28)
            return MCIERR_PARAM_OVERFLOW;
        wsprintfW(ret, L"%d %d %d %d", r[0], r[1], r[2], r[3]);
        return MMSYSERR_NOERROR;
    }
    default:
        return MCIERR_PARSER_INTERNAL;
    }
}

/* ------------------------------------------------------------------ mciSendString */

static BOOL has_notify(const WCHAR *s)
{
    for (const WCHAR *p = s; *p; p++)
        if (p > s && p[-1] == ' ' && match(L"notify", p))
            return TRUE;
    return FALSE;
}

static MCIERROR auto_open(const WCHAR *device, const WCHAR *command, WCHAR *ret, UINT size, DWORD requester)
{
    if (has_notify(command))
        return MCIERR_NOTIFY_ON_AUTO_OPEN;
    int n = lstrlenW(device) + 8;
    WCHAR *open = HeapAlloc(GetProcessHeap(), 0, n * sizeof(WCHAR));
    if (!open)
        return MCIERR_OUT_OF_MEMORY;
    wsprintfW(open, L"%s %s", L"open", device);
    MCIERROR r = MciRelay(open, nullptr, 0, requester, FALSE);
    if (!LOWORD(r)) {
        r = MciRelay(command, ret, size, requester, TRUE);
        if (LOWORD(r)) {
            wsprintfW(open, L"%s %s", L"close", device);
            MciRelay(open, nullptr, 0, requester, FALSE);
        }
    }
    HeapFree(GetProcessHeap(), 0, open);
    if (LOWORD(r) >= MCIERR_CUSTOM_DRIVER_BASE)
        r = LOWORD(r);
    return r;
}

static void shape_open(MCI_REPARSE *parse, WCHAR *device, BOOL is_new, MCIERROR *error)
{
    MCI_OPEN_PARMSW *open = (MCI_OPEN_PARMSW *)parse->block;
    if (is_new) {
        if (!(parse->flags & MCI_OPEN_TYPE)) {
            *error = MCIERR_INVALID_DEVICE_NAME;
            return;
        }
        open->lpstrElementName = L"";
        parse->flags |= MCI_OPEN_ELEMENT;
    } else if (parse->flags & MCI_OPEN_TYPE) {
        open->lpstrElementName = device;
        parse->flags |= MCI_OPEN_ELEMENT;
    } else {
        open->lpstrDeviceType = device;
        parse->flags |= MCI_OPEN_TYPE;
    }
}

MCIERROR MciSendString(const WCHAR *command, WCHAR *ret, UINT size, HWND callback, DWORD requester, BOOL relayed,
                       BOOL force_notify)
{
    if (!MciInit())
        return MCIERR_OUT_OF_MEMORY;
    if (!command)
        return MCIERR_MISSING_COMMAND_STRING;
    int length = lstrlenW(command);
    WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, (3 * length + 8) * sizeof(WCHAR));
    if (!copy)
        return MCIERR_OUT_OF_MEMORY;
    WCHAR *verb = copy + length + 1, *device = verb + length + 1;
    BOOL quoted = FALSE;
    for (int i = 0; i <= length; i++) {
        WCHAR c = command[i];
        if (c == '"')
            quoted = !quoted;
        copy[i] = c == '\t' && !quoted ? ' ' : c;
    }
    if (ret)
        ret[0] = 0;
    else
        size = 0;
    MCI_REPARSE *parse = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*parse));
    MCIERROR r = parse ? MMSYSERR_NOERROR : MCIERR_OUT_OF_MEMORY;
    const WCHAR *p = copy;
    while (*p == ' ')
        p++;
    if (!r && !*p)
        r = MCIERR_MISSING_COMMAND_STRING;
    if (!r)
        r = token(&p, verb);
    if (!r) {
        device[0] = 0;
        r = token(&p, device);
    }
    if (r)
        goto done;
    /* on the window thread a device word sequencer!song.mid names the device song.mid. */
    WCHAR *name = device;
    if (relayed) {
        for (WCHAR *c = device; *c; c++)
            if (*c == '!') {
                name = c + 1;
                break;
            }
    }
    MCIDEVICEID id = MciDeviceByName(name, GetCurrentThreadId());
    BOOL is_new = FALSE;
    if (!id && !lstrcmpiW(name, L"new")) {
        is_new = TRUE;
        name[0] = 0;
    }
    UINT table;
    const BYTE *entry = MciFindCommand(id, verb, &table);
    UINT message = 0;
    if (entry) {
        const WCHAR *word;
        WORD kind;
        DWORD value;
        MciEntry(entry, &word, &value, &kind);
        message = value;
    }
    MCI_DEVICE *d;
    RtlEnterCriticalSection(&MciLock);
    d = id != MCI_ALL_DEVICE_ID ? MciDevice(id) : nullptr;
    BOOL locked = d && d->closing_by_notify;
    BOOL foreign = d && d->requester != d->owner && d->owner != GetCurrentThreadId();
    RtlLeaveCriticalSection(&MciLock);
    if (locked) {
        r = MCIERR_DEVICE_LOCKED;
        goto done;
    }
    if (foreign && message != MCI_SYSINFO) {
        r = has_notify(copy) ? MCIERR_NOTIFY_ON_AUTO_OPEN : MciRelay(command, ret, size, requester, FALSE);
        goto done;
    }
    if (!name[0] && message != MCI_SOUND && !(is_new && message == MCI_OPEN)) {
        r = MCIERR_MISSING_DEVICE_NAME;
        goto done;
    }
    if (!entry) {
        r = MCIERR_UNRECOGNIZED_COMMAND;
        goto done;
    }
    if (is_new && message != MCI_OPEN) {
        r = MCIERR_INVALID_DEVICE_NAME;
        goto done;
    }
    if (!id && message != MCI_OPEN && message != MCI_SYSINFO && message != MCI_SOUND) {
        if (message == MCI_CLOSE || message == MCI_BREAK)
            r = MCIERR_INVALID_DEVICE_NAME;
        else
            r = auto_open(name, command, ret, size, requester);
        goto done;
    }
    BOOL open = message == MCI_OPEN;
    r = MciParseArgs(entry, p, open, parse);
    if (r)
        goto done;
    if (is_new && !(parse->flags & MCI_OPEN_ALIAS)) {
        r = MCIERR_NEW_REQUIRES_ALIAS;
        goto done;
    }
    ARG list[MAX_ARGS];
    WORD return_kind;
    UINT block_size;
    layout(entry, list, &return_kind, &block_size);
    if (open) {
        shape_open(parse, name, is_new, &r);
        if (r)
            goto done;
        parse->args = p;
        parse->device = name;
        parse->is_new = is_new;
    } else if (message == MCI_SOUND) {
        if (!lstrcmpiW(name, L"notify"))
            parse->flags |= MCI_NOTIFY;
        else if (!lstrcmpiW(name, L"wait"))
            parse->flags |= MCI_WAIT;
        else if (name[0]) {
            ((MCI_SOUND_PARMSW *)parse->block)->lpstrSoundName = name;
            parse->flags |= MCI_SOUND_NAME;
        }
    } else if (message == MCI_SYSINFO) {
        ((MCI_SYSINFO_PARMSW *)parse->block)->wDeviceType = id == MCI_ALL_DEVICE_ID ? 0 : MciTypeOf(name);
    }
    if (return_kind && id == MCI_ALL_DEVICE_ID && message != MCI_SYSINFO) {
        r = MCIERR_CANNOT_USE_ALL;
        goto done;
    }
    DWORD_PTR hwnd = (DWORD_PTR)callback;
    memcpy(parse->block, &hwnd, sizeof(hwnd));
    if (force_notify)
        parse->flags |= MCI_NOTIFY;
    if (return_kind == MCI_STRING) {
        memcpy(parse->block + sizeof(DWORD_PTR), &ret, sizeof(ret));
        memcpy(parse->block + sizeof(DWORD_PTR) + sizeof(ret), &size, sizeof(DWORD));
    }
    MCI_REPARSE *again = nullptr;
    if (open) {
        /* the device's open command reads the same words again into a block of its own. */
        again = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*again));
        if (!again) {
            r = MCIERR_OUT_OF_MEMORY;
            goto done;
        }
        again->args = p;
        again->deferred = parse->deferred;
    }
    r = MciCommand(open ? 0 : id, message, parse->flags, (DWORD_PTR)parse->block, requester, again);
    if (again) {
        free_strings(again);
        HeapFree(GetProcessHeap(), 0, again);
    }
    if (!LOWORD(r) && size && return_kind)
        r = answer(open ? ((MCI_OPEN_PARMSW *)parse->block)->wDeviceID : id, r, return_kind, parse->block, ret, size);
    else if (!LOWORD(r))
        r = 0;
    if (LOWORD(r) >= MCIERR_CUSTOM_DRIVER_BASE && !HIWORD(r))
        r = MAKELONG(LOWORD(r), id == MCI_ALL_DEVICE_ID ? 0xFFFF : id);
    else if (HIWORD(r) && LOWORD(r) < MCIERR_CUSTOM_DRIVER_BASE)
        r = LOWORD(r);
done:
    if (parse) {
        free_strings(parse);
        HeapFree(GetProcessHeap(), 0, parse);
    }
    HeapFree(GetProcessHeap(), 0, copy);
    return r;
}

WINMMAPI MCIERROR WINAPI mciSendStringW(LPCWSTR command, LPWSTR ret, UINT size, HWND callback)
{
    MCIERROR r;
    do
        r = MciSendString(command, ret, size, callback, GetCurrentThreadId(), FALSE, FALSE);
    while (r == MCI_RELAY_SENTINEL);
    return r;
}

WINMMAPI MCIERROR WINAPI mciSendStringA(LPCSTR command, LPSTR ret, UINT size, HWND callback)
{
    if (!command)
        return mciSendStringW(nullptr, nullptr, 0, callback);
    int n = MultiByteToWideChar(CP_ACP, 0, command, -1, nullptr, 0);
    WCHAR *wide = HeapAlloc(GetProcessHeap(), 0, (n + (ret ? size : 0) + 1) * sizeof(WCHAR));
    if (!wide)
        return MCIERR_OUT_OF_MEMORY;
    MultiByteToWideChar(CP_ACP, 0, command, -1, wide, n);
    WCHAR *out = ret && size ? wide + n : nullptr;
    if (out)
        out[0] = 0;
    MCIERROR r = mciSendStringW(wide, out, out ? size : 0, callback);
    if (ret && size) {
        ret[0] = 0;
        if (out && !WideCharToMultiByte(CP_ACP, 0, out, -1, ret, size, nullptr, nullptr)) {
            ret[size - 1] = 0;
            if (!LOWORD(r))
                r = MCIERR_OUT_OF_MEMORY;
        }
    }
    HeapFree(GetProcessHeap(), 0, wide);
    return r;
}

/* ------------------------------------------------------------------ mciSendCommandA */

static WCHAR *widen(const char *s)
{
    if (!s)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    WCHAR *w = HeapAlloc(GetProcessHeap(), 0, n * sizeof(WCHAR));
    if (w)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
    return w;
}

/*
 * the strings of an ansi block, found by the command table of the
 * message, made wide for the call and the answer made narrow again.
 */
WINMMAPI MCIERROR WINAPI mciSendCommandA(MCIDEVICEID id, UINT message, DWORD_PTR flags, DWORD_PTR parms)
{
    if (!parms)
        return mciSendCommandW(id, message, flags, parms);
    if (message == MCI_OPEN) {
        MCI_OPEN_PARMSA *a = (MCI_OPEN_PARMSA *)parms;
        LPCSTR saved[3] = {a->lpstrDeviceType, a->lpstrElementName, a->lpstrAlias};
        WCHAR *w[3] = {};
        if ((flags & MCI_OPEN_TYPE) && !(flags & MCI_OPEN_TYPE_ID))
            w[0] = widen(a->lpstrDeviceType);
        if ((flags & MCI_OPEN_ELEMENT) && !(flags & MCI_OPEN_ELEMENT_ID))
            w[1] = widen(a->lpstrElementName);
        if (flags & MCI_OPEN_ALIAS)
            w[2] = widen(a->lpstrAlias);
        if (w[0])
            a->lpstrDeviceType = (LPCSTR)w[0];
        if (w[1])
            a->lpstrElementName = (LPCSTR)w[1];
        if (w[2])
            a->lpstrAlias = (LPCSTR)w[2];
        MCIERROR r = mciSendCommandW(id, message, flags, parms);
        a->lpstrDeviceType = saved[0];
        a->lpstrElementName = saved[1];
        a->lpstrAlias = saved[2];
        for (int i = 0; i < 3; i++)
            HeapFree(GetProcessHeap(), 0, w[i]);
        return r;
    }
    if (!MciInit())
        return MCIERR_OUT_OF_MEMORY;
    UINT table;
    const BYTE *entry = MciFindMessage(id == MCI_ALL_DEVICE_ID ? 0 : id, message, &table);
    if (!entry)
        return MCIERR_UNSUPPORTED_FUNCTION;
    ARG list[MAX_ARGS];
    WORD return_kind;
    UINT size, n = layout(entry, list, &return_kind, &size);
    BYTE *block = (BYTE *)parms;
    WCHAR *made[MAX_ARGS] = {};
    char *saved[MAX_ARGS] = {};
    for (UINT i = 0; i < n; i++)
        if (list[i].kind == MCI_STRING && (flags & list[i].flag)) {
            memcpy(&saved[i], block + list[i].offset, sizeof(char *));
            made[i] = widen(saved[i]);
            memcpy(block + list[i].offset, &made[i], sizeof(WCHAR *));
        }
    char *text = nullptr;
    DWORD text_size = 0;
    WCHAR *wide = nullptr;
    if (return_kind == MCI_STRING) {
        memcpy(&text, block + sizeof(DWORD_PTR), sizeof(text));
        memcpy(&text_size, block + sizeof(DWORD_PTR) + sizeof(text), sizeof(DWORD));
        if (text && text_size) {
            wide = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (text_size + 2) * sizeof(WCHAR));
            memcpy(block + sizeof(DWORD_PTR), &wide, sizeof(wide));
        }
    }
    MCIERROR r = mciSendCommandW(id, message, flags, parms);
    if (wide) {
        memcpy(block + sizeof(DWORD_PTR), &text, sizeof(text));
        if (!LOWORD(r)) {
            if (message == MCI_SYSINFO && (flags & MCI_SYSINFO_QUANTITY))
                memcpy(text, wide, sizeof(DWORD));
            else if (!WideCharToMultiByte(CP_ACP, 0, wide, -1, text, text_size, nullptr, nullptr))
                text[text_size - 1] = 0;
        }
        HeapFree(GetProcessHeap(), 0, wide);
    }
    for (UINT i = 0; i < n; i++)
        if (made[i]) {
            memcpy(block + list[i].offset, &saved[i], sizeof(char *));
            HeapFree(GetProcessHeap(), 0, made[i]);
        }
    return r;
}
