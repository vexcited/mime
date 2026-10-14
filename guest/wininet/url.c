/*
 * url strings: InternetCrackUrl splits a url into its components and InternetCanonicalizeUrl encodes it and
 * removes its dot segments. both work on code units, so the ansi forms see each byte as one unit and keep
 * offsets into the caller's string.
 */
#include "wininet_private.h"

typedef struct {
    const void *Text;
    BOOL Wide;
    DWORD Length;
} URL_TEXT;

static WCHAR UnitAt(const URL_TEXT *url, DWORD i)
{
    return url->Wide ? ((const WCHAR *)url->Text)[i] : (WCHAR)((const unsigned char *)url->Text)[i];
}

static DWORD TextLength(const void *text, BOOL wide)
{
    DWORD n = 0;
    if (wide)
        while (((const WCHAR *)text)[n])
            n++;
    else
        while (((const char *)text)[n])
            n++;
    return n;
}

static BOOL IsAlpha(WCHAR c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

static BOOL IsDigit(WCHAR c)
{
    return c >= L'0' && c <= L'9';
}

static int HexValue(WCHAR c)
{
    if (IsDigit(c))
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

static BOOL IsEscape(const URL_TEXT *url, DWORD i, DWORD end)
{
    return i + 2 < end && UnitAt(url, i) == L'%' && HexValue(UnitAt(url, i + 1)) >= 0 &&
           HexValue(UnitAt(url, i + 2)) >= 0;
}

/* two slashes, backslashes being accepted for them as windows does. */
static BOOL IsSlashes(const URL_TEXT *url, DWORD at)
{
    if (at + 2 > url->Length)
        return FALSE;
    WCHAR a = UnitAt(url, at), b = UnitAt(url, at + 1);
    return (a == L'/' || a == L'\\') && (b == L'/' || b == L'\\');
}

/* the length of a scheme ending with a colon at the start of the url, or 0. */
static DWORD SchemeLength(const URL_TEXT *url)
{
    DWORD i = 0;
    while (i < url->Length) {
        WCHAR c = UnitAt(url, i);
        if (!IsAlpha(c) && !(i && (IsDigit(c) || c == L'+' || c == L'-' || c == L'.')))
            break;
        i++;
    }
    return i && i < url->Length && UnitAt(url, i) == L':' ? i : 0;
}

static const struct {
    LPCWSTR Name;
    INTERNET_SCHEME Scheme;
    INTERNET_PORT Port;
} Schemes[] = {
    {L"ftp", INTERNET_SCHEME_FTP, INTERNET_DEFAULT_FTP_PORT},
    {L"gopher", INTERNET_SCHEME_GOPHER, INTERNET_DEFAULT_GOPHER_PORT},
    {L"http", INTERNET_SCHEME_HTTP, INTERNET_DEFAULT_HTTP_PORT},
    {L"https", INTERNET_SCHEME_HTTPS, INTERNET_DEFAULT_HTTPS_PORT},
    {L"file", INTERNET_SCHEME_FILE, 0},
    {L"news", INTERNET_SCHEME_NEWS, 0},
    {L"mailto", INTERNET_SCHEME_MAILTO, 0},
    {L"socks", INTERNET_SCHEME_SOCKS, INTERNET_DEFAULT_SOCKS_PORT},
    {L"javascript", INTERNET_SCHEME_JAVASCRIPT, 0},
    {L"vbscript", INTERNET_SCHEME_VBSCRIPT, 0},
    {L"res", INTERNET_SCHEME_RES, 0},
};

static int SchemeIndex(const URL_TEXT *url, DWORD length)
{
    for (int s = 0; s < (int)(sizeof(Schemes) / sizeof(Schemes[0])); s++) {
        LPCWSTR name = Schemes[s].Name;
        DWORD i = 0;
        for (; i < length && name[i]; i++) {
            WCHAR c = UnitAt(url, i);
            if (c >= L'A' && c <= L'Z')
                c += L'a' - L'A';
            if (c != name[i])
                break;
        }
        if (i == length && !name[i])
            return s;
    }
    return -1;
}

/* ---------------------------------------------------------------- InternetCrackUrl */

typedef struct {
    DWORD Start;
    DWORD Length;
    BOOL Present;
} URL_RANGE;

enum { PART_SCHEME, PART_HOST, PART_USER, PART_PASSWORD, PART_PATH, PART_EXTRA, PART_COUNT };

static DWORD SplitUrl(const URL_TEXT *url, BOOL want_extra, URL_RANGE parts[PART_COUNT], INTERNET_SCHEME *scheme,
                      INTERNET_PORT *port)
{
    for (int i = 0; i < PART_COUNT; i++)
        parts[i] = (URL_RANGE){0, 0, FALSE};
    DWORD scheme_length = SchemeLength(url);
    if (!scheme_length)
        return ERROR_INTERNET_UNRECOGNIZED_SCHEME;
    int index = SchemeIndex(url, scheme_length);
    *scheme = index < 0 ? INTERNET_SCHEME_UNKNOWN : Schemes[index].Scheme;
    *port = index < 0 ? INTERNET_INVALID_PORT_NUMBER : Schemes[index].Port;
    parts[PART_SCHEME] = (URL_RANGE){0, scheme_length, TRUE};
    DWORD at = scheme_length + 1;
    if (IsSlashes(url, at)) {
        DWORD start = at + 2, end = start;
        while (end < url->Length) {
            WCHAR c = UnitAt(url, end);
            if (c == L'/' || c == L'\\' || c == L'?' || c == L'#')
                break;
            end++;
        }
        DWORD host = start;
        for (DWORD i = start; i < end; i++)
            if (UnitAt(url, i) == L'@')
                host = i + 1;
        if (host != start) {
            DWORD colon = start;
            while (colon < host - 1 && UnitAt(url, colon) != L':')
                colon++;
            parts[PART_USER] = (URL_RANGE){start, colon - start, TRUE};
            if (colon < host - 1)
                parts[PART_PASSWORD] = (URL_RANGE){colon + 1, host - 1 - colon - 1, TRUE};
        }
        DWORD host_end = end, search = host;
        if (host < end && UnitAt(url, host) == L'[') {
            while (search < end && UnitAt(url, search) != L']')
                search++;
            if (search == end)
                return ERROR_INTERNET_INVALID_URL;
        }
        for (DWORD i = search; i < end; i++)
            if (UnitAt(url, i) == L':') {
                host_end = i;
                break;
            }
        parts[PART_HOST] = (URL_RANGE){host, host_end - host, TRUE};
        if (host_end < end) {
            DWORD value = 0;
            for (DWORD i = host_end + 1; i < end; i++) {
                WCHAR c = UnitAt(url, i);
                if (!IsDigit(c) || (value = value * 10 + (c - L'0')) > INTERNET_MAX_PORT_NUMBER_VALUE)
                    return ERROR_INTERNET_INVALID_URL;
            }
            if (host_end + 1 < end)
                *port = (INTERNET_PORT)value;
        }
        at = end;
    } else {
        parts[PART_HOST] = (URL_RANGE){at, 0, TRUE};
    }
    DWORD path_end = url->Length;
    if (want_extra)
        for (DWORD i = at; i < url->Length; i++) {
            WCHAR c = UnitAt(url, i);
            if (c == L'?' || c == L'#') {
                path_end = i;
                break;
            }
        }
    parts[PART_PATH] = (URL_RANGE){at, path_end - at, TRUE};
    parts[PART_EXTRA] = (URL_RANGE){path_end, url->Length - path_end, TRUE};
    return ERROR_SUCCESS;
}

/* fills one component: a pointer into the url when the caller gave none, else a copy into its buffer. */
static DWORD StoreComponent(const URL_TEXT *url, URL_RANGE range, void **pointer, DWORD *length, BOOL decode)
{
    if (!*length)
        return ERROR_SUCCESS;
    SIZE_T unit = url->Wide ? sizeof(WCHAR) : 1;
    if (!*pointer) {
        *pointer = range.Present ? (char *)url->Text + range.Start * unit : NULL;
        *length = range.Length;
        return ERROR_SUCCESS;
    }
    DWORD needed = 0;
    for (DWORD i = range.Start; i < range.Start + range.Length; i++, needed++)
        if (decode && IsEscape(url, i, range.Start + range.Length))
            i += 2;
    if (*length < needed + 1) {
        *length = needed + 1;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    DWORD n = 0;
    for (DWORD i = range.Start; i < range.Start + range.Length; i++, n++) {
        WCHAR c = UnitAt(url, i);
        if (decode && IsEscape(url, i, range.Start + range.Length)) {
            c = (WCHAR)(HexValue(UnitAt(url, i + 1)) * 16 + HexValue(UnitAt(url, i + 2)));
            i += 2;
        }
        if (url->Wide)
            ((WCHAR *)*pointer)[n] = c;
        else
            ((char *)*pointer)[n] = (char)c;
    }
    if (url->Wide)
        ((WCHAR *)*pointer)[n] = 0;
    else
        ((char *)*pointer)[n] = 0;
    *length = n;
    return ERROR_SUCCESS;
}

/* both forms share the layout of URL_COMPONENTSW, only the string pointer types differ. */
static BOOL CrackUrl(const void *text, BOOL wide, DWORD length, DWORD flags, URL_COMPONENTSW *components)
{
    if (!text || !components || components->dwStructSize != sizeof(URL_COMPONENTSW))
        return InetFail(ERROR_INVALID_PARAMETER);
    URL_TEXT url = {text, wide, length ? length : TextLength(text, wide)};
    URL_RANGE parts[PART_COUNT];
    INTERNET_SCHEME scheme;
    INTERNET_PORT port;
    DWORD error = SplitUrl(&url, components->dwExtraInfoLength != 0, parts, &scheme, &port);
    if (error)
        return InetFail(error);
    BOOL decode = (flags & (ICU_DECODE | ICU_ESCAPE)) != 0;
    struct {
        void **Pointer;
        DWORD *Length;
        BOOL Decode;
    } fields[PART_COUNT] = {
        {(void **)&components->lpszScheme, &components->dwSchemeLength, FALSE},
        {(void **)&components->lpszHostName, &components->dwHostNameLength, FALSE},
        {(void **)&components->lpszUserName, &components->dwUserNameLength, decode},
        {(void **)&components->lpszPassword, &components->dwPasswordLength, decode},
        {(void **)&components->lpszUrlPath, &components->dwUrlPathLength, decode},
        {(void **)&components->lpszExtraInfo, &components->dwExtraInfoLength, decode},
    };
    for (int i = 0; i < PART_COUNT; i++) {
        DWORD result = StoreComponent(&url, parts[i], fields[i].Pointer, fields[i].Length, fields[i].Decode);
        if (result && !error)
            error = result;
    }
    components->nScheme = scheme;
    components->nPort = port;
    if (error)
        return InetFail(error);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI InternetCrackUrlW(LPCWSTR url, DWORD length, DWORD flags, LPURL_COMPONENTSW components)
{
    return CrackUrl(url, TRUE, length, flags, components);
}

DECLSPEC_EXPORT BOOL WINAPI InternetCrackUrlA(LPCSTR url, DWORD length, DWORD flags, LPURL_COMPONENTSA components)
{
    _Static_assert(sizeof(URL_COMPONENTSA) == sizeof(URL_COMPONENTSW), "url components layout");
    return CrackUrl(url, FALSE, length, flags, (URL_COMPONENTSW *)components);
}

/* ---------------------------------------------------------------- InternetCanonicalizeUrl */

typedef struct {
    WCHAR *Units;
    DWORD Count;
} URL_OUTPUT;

static void Put(URL_OUTPUT *out, WCHAR c)
{
    out->Units[out->Count++] = c;
}

static BOOL IsUnsafe(WCHAR c, DWORD flags)
{
    if (flags & ICU_ENCODE_SPACES_ONLY)
        return c == L' ';
    if (c == L'%')
        return (flags & ICU_ENCODE_PERCENT) != 0;
    return c <= L' ' || c >= 0x7F || c == L'"' || c == L'<' || c == L'>' || c == L'\\' || c == L'^' || c == L'`' ||
           c == L'{' || c == L'|' || c == L'}';
}

static void PutEscaped(URL_OUTPUT *out, unsigned value)
{
    static const char hex[] = "0123456789ABCDEF";
    Put(out, L'%');
    Put(out, (WCHAR)hex[(value >> 4) & 15]);
    Put(out, (WCHAR)hex[value & 15]);
}

/* writes the unit at i with the encoding the flags ask for and returns the units used. wide urls escape
 * units above ascii as utf-8, ansi urls escape each byte. */
static DWORD PutEncoded(URL_OUTPUT *out, const WCHAR *units, DWORD count, DWORD i, BOOL wide, DWORD flags)
{
    WCHAR c = units[i];
    if ((flags & ICU_NO_ENCODE) || !IsUnsafe(c, flags)) {
        Put(out, c);
        return 1;
    }
    if (c < 0x80 || !wide) {
        PutEscaped(out, c);
        return 1;
    }
    unsigned code = c;
    DWORD used = 1;
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < count && units[i + 1] >= 0xDC00 && units[i + 1] < 0xE000) {
        code = 0x10000 + ((c - 0xD800) << 10) + (units[i + 1] - 0xDC00);
        used = 2;
    }
    if (code < 0x800) {
        PutEscaped(out, 0xC0 | (code >> 6));
    } else if (code < 0x10000) {
        PutEscaped(out, 0xE0 | (code >> 12));
        PutEscaped(out, 0x80 | ((code >> 6) & 0x3F));
    } else {
        PutEscaped(out, 0xF0 | (code >> 18));
        PutEscaped(out, 0x80 | ((code >> 12) & 0x3F));
        PutEscaped(out, 0x80 | ((code >> 6) & 0x3F));
    }
    PutEscaped(out, 0x80 | (code & 0x3F));
    return used;
}

/* encodes or decodes a run of units as the flags ask. */
static void PutRun(URL_OUTPUT *out, const WCHAR *units, DWORD count, BOOL wide, DWORD flags)
{
    for (DWORD i = 0; i < count;) {
        if (flags & ICU_DECODE) {
            if (i + 2 < count && units[i] == L'%' && HexValue(units[i + 1]) >= 0 && HexValue(units[i + 2]) >= 0) {
                Put(out, (WCHAR)(HexValue(units[i + 1]) * 16 + HexValue(units[i + 2])));
                i += 3;
            } else {
                Put(out, units[i++]);
            }
        } else {
            i += PutEncoded(out, units, count, i, wide, flags);
        }
    }
}

/* the end of the next path segment of units starting at a slash or at the path start. */
static DWORD SegmentEnd(const WCHAR *units, DWORD at, DWORD end)
{
    DWORD i = at < end && units[at] == L'/' ? at + 1 : at;
    while (i < end && units[i] != L'/')
        i++;
    return i;
}

static BOOL Matches(const WCHAR *units, DWORD at, DWORD end, const char *text)
{
    for (; *text; text++, at++)
        if (at >= end || units[at] != (WCHAR)*text)
            return FALSE;
    return TRUE;
}

/* removes the dot segments of a path in place, as in rfc 3986 section 5.2.4, and returns its new length. */
static DWORD RemoveDotSegments(WCHAR *units, DWORD length)
{
    DWORD in = 0, out = 0;
    while (in < length) {
        if (Matches(units, in, length, "../")) {
            in += 3;
        } else if (Matches(units, in, length, "./")) {
            in += 2;
        } else if (Matches(units, in, length, "/./")) {
            in += 2;
        } else if (Matches(units, in, length, "/.") && in + 2 == length) {
            units[++in] = L'/';
        } else if (Matches(units, in, length, "/../") || (Matches(units, in, length, "/..") && in + 3 == length)) {
            if (in + 3 == length) {
                in += 2;
                units[in] = L'/';
            } else {
                in += 3;
            }
            while (out && units[out - 1] != L'/')
                out--;
            if (out)
                out--;
        } else if ((Matches(units, in, length, ".") && in + 1 == length) ||
                   (Matches(units, in, length, "..") && in + 2 == length)) {
            in = length;
        } else {
            DWORD end = SegmentEnd(units, in, length);
            while (in < end)
                units[out++] = units[in++];
        }
    }
    return out;
}

static DWORD Canonicalize(const URL_TEXT *url, DWORD flags, URL_OUTPUT *out)
{
    /* a widened working copy, ansi bytes becoming one unit each. */
    WCHAR *units = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)url->Length + 2) * sizeof(WCHAR));
    if (!units)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (DWORD i = 0; i < url->Length; i++)
        units[i] = UnitAt(url, i);
    DWORD scheme_length = SchemeLength(url);
    int scheme = scheme_length ? SchemeIndex(url, scheme_length) : -1;
    DWORD at = scheme_length ? scheme_length + 1 : 0;
    BOOL hierarchical = scheme_length && IsSlashes(url, at);
    for (DWORD i = 0; i < at; i++)
        Put(out, units[i]);
    DWORD rest = at;
    while (rest < url->Length && units[rest] != L'?' && units[rest] != L'#')
        rest++;
    /* the authority and path, with backslashes as slashes and dot segments removed in hierarchical urls. */
    DWORD length = rest;
    if (hierarchical) {
        for (DWORD i = at; i < rest; i++)
            if (units[i] == L'\\')
                units[i] = L'/';
        DWORD authority = at + 2;
        while (authority < rest && units[authority] != L'/')
            authority++;
        BOOL needs_root = scheme >= 0 && (Schemes[scheme].Scheme == INTERNET_SCHEME_HTTP ||
                                          Schemes[scheme].Scheme == INTERNET_SCHEME_HTTPS ||
                                          Schemes[scheme].Scheme == INTERNET_SCHEME_FTP ||
                                          Schemes[scheme].Scheme == INTERNET_SCHEME_GOPHER);
        if (!(flags & ICU_NO_META))
            length = authority + RemoveDotSegments(units + authority, rest - authority);
        PutRun(out, units + at, length - at, url->Wide, flags);
        if (authority == length && needs_root)
            Put(out, L'/');
    } else {
        PutRun(out, units + at, length - at, url->Wide, flags);
    }
    /* browser mode leaves the query and fragment as they are. */
    if (flags & ICU_BROWSER_MODE)
        for (DWORD i = rest; i < url->Length; i++)
            Put(out, units[i]);
    else
        PutRun(out, units + rest, url->Length - rest, url->Wide, flags);
    InetFree(units);
    return ERROR_SUCCESS;
}

static BOOL CanonicalizeUrl(const void *text, BOOL wide, void *buffer, LPDWORD length, DWORD flags)
{
    if (!text || !buffer || !length)
        return InetFail(ERROR_INVALID_PARAMETER);
    URL_TEXT url = {text, wide, TextLength(text, wide)};
    URL_OUTPUT out = {HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)url.Length * 12 + 4) * sizeof(WCHAR)), 0};
    if (!out.Units)
        return InetFail(ERROR_NOT_ENOUGH_MEMORY);
    DWORD error = Canonicalize(&url, flags, &out);
    if (!error && *length <= out.Count) {
        *length = out.Count + 1;
        error = ERROR_INSUFFICIENT_BUFFER;
    }
    if (!error) {
        for (DWORD i = 0; i < out.Count; i++)
            if (wide)
                ((WCHAR *)buffer)[i] = out.Units[i];
            else
                ((char *)buffer)[i] = (char)out.Units[i];
        if (wide)
            ((WCHAR *)buffer)[out.Count] = 0;
        else
            ((char *)buffer)[out.Count] = 0;
        *length = out.Count;
    }
    InetFree(out.Units);
    if (error)
        return InetFail(error);
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI InternetCanonicalizeUrlW(LPCWSTR url, LPWSTR buffer, LPDWORD length, DWORD flags)
{
    return CanonicalizeUrl(url, TRUE, buffer, length, flags);
}

DECLSPEC_EXPORT BOOL WINAPI InternetCanonicalizeUrlA(LPCSTR url, LPSTR buffer, LPDWORD length, DWORD flags)
{
    return CanonicalizeUrl(url, FALSE, buffer, length, flags);
}
